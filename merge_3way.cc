// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "merge_content.hh"

#include "interner.hh"
#include "lcs.hh"
#include "paths.hh"
#include "vector.hh"

using std::min;
using std::vector;
using std::string;
using std::swap;

//
// a 3-way merge works like this:
//
//            /---->   right
//    ancestor
//            \---->   left
//
// first you compute the edit list "EDITS(ancestor,left)".
//
// then you make an offset table "leftpos" which describes positions in
// "ancestor" as they map to "left"; that is, for 0 < apos <
// ancestor.size(), we have
//
//  left[leftpos[apos]] == ancestor[apos]
//
// you do this by walking through the edit list and either jumping the
// current index ahead an extra position, on an insert, or remaining still,
// on a delete. on an insert *or* a delete, you push the current index back
// onto the leftpos array.
//
// next you compute the edit list "EDITS(ancestor,right)".
//
// you then go through this edit list applying the edits to left, rather
// than ancestor, and using the table leftpos to map the position of each
// edit to an appropriate spot in left. this means you walk a "curr_left"
// index through the edits, and for each edit e:
//
// - if e is a delete (and e.pos is a position in ancestor)
//   - increment curr_left without copying anything to "merged"
//
// - if e is an insert (and e.pos is a position in right)
//   - copy right[e.pos] to "merged"
//   - leave curr_left alone
//
// - when advancing to apos (and apos is a position in ancestor)
//   - copy left[curr_left] to merged while curr_left < leftpos[apos]
//
//
// the practical upshot is that you apply the delta from ancestor->right
// to the adjusted contexts in left, producing something vaguely like
// the concatenation of delta(ancestor,left) :: delta(ancestor,right).
//
// NB: this is, as far as I can tell, what diff3 does. I don't think I'm
// infringing on anyone's fancy patents here.
//

struct conflict {};

typedef enum { preserved = 0, deleted = 1, changed = 2 } edit_t;
static const char etab[3][10] =
  {
    "preserved",
    "deleted",
    "changed"
  };

struct extent
{
  extent(size_t p, size_t l, edit_t t)
    : pos(p), len(l), type(t)
  {}
  size_t pos;
  size_t len;
  edit_t type;
};

void calculate_extents(vector<long, QA(long)> const & a_b_edits,
                       vector<long, QA(long)> const & b,
                       vector<long, QA(long)> & prefix,
                       vector<extent> & extents,
                       vector<long, QA(long)> & suffix,
                       size_t const a_len,
                       interner<long> & intern)
{
  extents.reserve(a_len * 2);

  size_t a_pos = 0, b_pos = 0;

  for (vector<long, QA(long)>::const_iterator i = a_b_edits.begin();
       i != a_b_edits.end(); ++i)
    {
      // L(FL("edit: %d") % *i);
      if (*i < 0)
        {
          // negative elements code the negation of the one-based index into A
          // of the element to be deleted
          size_t a_deleted = (-1 - *i);

          // fill positions out to the deletion point
          while (a_pos < a_deleted)
            {
              a_pos++;
              extents.push_back(extent(b_pos++, 1, preserved));
            }

          // L(FL(" -- delete at A-pos %d (B-pos = %d)") % a_deleted % b_pos);

          // skip the deleted line
          a_pos++;
          extents.push_back(extent(b_pos, 0, deleted));
        }
      else
        {
          // positive elements code the one-based index into B of the element to
          // be inserted
          size_t b_inserted = (*i - 1);

          // fill positions out to the insertion point
          while (b_pos < b_inserted)
            {
              a_pos++;
              extents.push_back(extent(b_pos++, 1, preserved));
            }

          // L(FL(" -- insert at B-pos %d (A-pos = %d) : '%s'")
          //   % b_inserted % a_pos % intern.lookup(b.at(b_inserted)));

          // record that there was an insertion, but a_pos did not move.
          if ((b_pos == 0 && extents.empty())
              || (b_pos == prefix.size()))
            {
              prefix.push_back(b.at(b_pos));
            }
          else if (a_len == a_pos)
            {
              suffix.push_back(b.at(b_pos));
            }
          else
            {
              // make the insertion
              extents.back().type = changed;
              extents.back().len++;
            }
          b_pos++;
        }
    }

  while (extents.size() < a_len)
    extents.push_back(extent(b_pos++, 1, preserved));
}

void normalize_extents(vector<extent> & a_b_map,
                       vector<long, QA(long)> const & a,
                       vector<long, QA(long)> const & b)
{
  for (size_t i = 0; i < a_b_map.size(); ++i)
    {
      if (i > 0)
      {
        size_t j = i;
        while (j > 0
               && (a_b_map.at(j-1).type == preserved)
               && (a_b_map.at(j).type == changed)
               && (a.at(j) == b.at(a_b_map.at(j).pos + a_b_map.at(j).len - 1)))
          {
            // This is implied by (a_b_map.at(j-1).type == preserved)
            I(a.at(j-1) == b.at(a_b_map.at(j-1).pos));

            // Coming into loop we have:
            //                     i
            //  z   --pres-->  z   0
            //  o   --pres-->  o   1
            //  a   --chng-->  a   2   The important thing here is that 'a' in
            //                 t       the LHS matches with ...
            //                 u
            //                 v
            //                 a       ... the a on the RHS here. Hence we can
            //  q  --pres-->   q   3   'shift' the entire 'changed' block
            //  e  --chng-->   d   4   upwards, leaving a 'preserved' line
            //  g  --pres-->   g   5   'a'->'a'
            //
            //  Want to end up with:
            //                     i
            //  z   --pres-->  z   0
            //  o   --chng-->  o   1
            //                 a
            //                 t
            //                 u
            //                 v
            //  a  --pres-->   a   2
            //  q  --pres-->   q   3
            //  e  --chng-->   d   4
            //  g  --pres-->   g   5
            //
            // Now all the 'changed' extents are normalised to the
            // earliest possible position.

            L(FL("exchanging preserved extent [%d+%d] with changed extent [%d+%d]")
              % a_b_map.at(j-1).pos
              % a_b_map.at(j-1).len
              % a_b_map.at(j).pos
              % a_b_map.at(j).len);

            swap(a_b_map.at(j-1).len, a_b_map.at(j).len);
            swap(a_b_map.at(j-1).type, a_b_map.at(j).type);

            // Adjust position of the later, preserved extent. It should
            // better point to the second 'a' in the above example.
            a_b_map.at(j).pos = a_b_map.at(j-1).pos + a_b_map.at(j-1).len;

            --j;
          }
      }
    }

  for (size_t i = 0; i < a_b_map.size(); ++i)
    {
      if (i > 0)
      {
        size_t j = i;
        while (j > 0
               && a_b_map.at(j).type == changed
               && a_b_map.at(j-1).type == changed
               && a_b_map.at(j).len > 1
               && a_b_map.at(j-1).pos + a_b_map.at(j-1).len == a_b_map.at(j).pos)
          {
            // step 1: move a chunk from this insert extent to its
            // predecessor
            size_t piece = a_b_map.at(j).len - 1;
            //      L(FL("moving change piece of len %d from pos %d to pos %d")
            //        % piece
            //        % a_b_map.at(j).pos
            //        % a_b_map.at(j-1).pos);
            a_b_map.at(j).len = 1;
            a_b_map.at(j).pos += piece;
            a_b_map.at(j-1).len += piece;

            // step 2: if this extent (now of length 1) has become a "changed"
            // extent identical to its previous state, switch it to a "preserved"
            // extent.
            if (b.at(a_b_map.at(j).pos) == a.at(j))
              {
                //              L(FL("changing normalized 'changed' extent at %d to 'preserved'")
                //                % a_b_map.at(j).pos);
                a_b_map.at(j).type = preserved;
              }
            --j;
          }
      }
    }
}


void merge_extents(vector<extent> const & a_b_map,
                   vector<extent> const & a_c_map,
                   vector<long, QA(long)> const & b,
                   vector<long, QA(long)> const & c,
                   interner<long> const & in,
                   vector<long, QA(long)> & merged)
{
  I(a_b_map.size() == a_c_map.size());

  vector<extent>::const_iterator i = a_b_map.begin();
  vector<extent>::const_iterator j = a_c_map.begin();
  merged.reserve(a_b_map.size() * 2);

  //   for (; i != a_b_map.end(); ++i, ++j)
  //     {

  //       L(FL("trying to merge: [%s %d %d] vs. [%s %d %d]")
  //            % etab[i->type] % i->pos % i->len
  //            % etab[j->type] % j->pos % j->len);
  //     }

  //   i = a_b_map.begin();
  //   j = a_c_map.begin();

  for (; i != a_b_map.end(); ++i, ++j)
    {

      //       L(FL("trying to merge: [%s %d %d] vs. [%s %d %d]")
      //                % etab[i->type] % i->pos % i->len
      //                % etab[j->type] % j->pos % j->len);

      // mutual, identical preserves / inserts / changes
      if (((i->type == changed && j->type == changed)
           || (i->type == preserved && j->type == preserved))
          && i->len == j->len)
        {
          for (size_t k = 0; k < i->len; ++k)
            {
              if (b.at(i->pos + k) != c.at(j->pos + k))
                {
                  L(FL("conflicting edits: %s %d[%d] '%s' vs. %s %d[%d] '%s'")
                    % etab[i->type] % i->pos % k % in.lookup(b.at(i->pos + k))
                    % etab[j->type] % j->pos % k % in.lookup(c.at(j->pos + k)));
                  throw conflict();
                }
              merged.push_back(b.at(i->pos + k));
            }
        }

      // mutual or single-edge deletes
      else if ((i->type == deleted && j->type == deleted)
               || (i->type == deleted && j->type == preserved)
               || (i->type == preserved && j->type == deleted))
        {
          // do nothing
        }

      // single-edge insert / changes
      else if (i->type == changed && j->type == preserved)
        for (size_t k = 0; k < i->len; ++k)
          merged.push_back(b.at(i->pos + k));

      else if (i->type == preserved && j->type == changed)
        for (size_t k = 0; k < j->len; ++k)
          merged.push_back(c.at(j->pos + k));

      else
        {
          L(FL("conflicting edits: [%s %d %d] vs. [%s %d %d]")
            % etab[i->type] % i->pos % i->len
            % etab[j->type] % j->pos % j->len);
          throw conflict();
        }

      //       if (merged.empty())
      //        L(FL(" --> EMPTY"));
      //       else
      //                L(FL(" --> [%d]: %s") % (merged.size() - 1) % in.lookup(merged.back()));
    }
}


void merge_via_edit_scripts(vector<string> const & ancestor,
                            vector<string> const & left,
                            vector<string> const & right,
                            vector<string> & merged)
{
  vector<long, QA(long)> anc_interned;
  vector<long, QA(long)> left_interned, right_interned;
  vector<long, QA(long)> left_edits, right_edits;
  vector<long, QA(long)> left_prefix, right_prefix;
  vector<long, QA(long)> left_suffix, right_suffix;
  vector<extent> left_extents, right_extents;
  vector<long, QA(long)> merged_interned;
  interner<long> in;

  //   for (int i = 0; i < min(min(left.size(), right.size()), ancestor.size()); ++i)
  //     {
  //       cerr << '[' << i << "] " << left[i] << ' ' << ancestor[i] <<  ' ' << right[i] << '\n';
  //     }

  anc_interned.reserve(ancestor.size());
  for (vector<string>::const_iterator i = ancestor.begin();
       i != ancestor.end(); ++i)
    anc_interned.push_back(in.intern(*i));

  left_interned.reserve(left.size());
  for (vector<string>::const_iterator i = left.begin();
       i != left.end(); ++i)
    left_interned.push_back(in.intern(*i));

  right_interned.reserve(right.size());
  for (vector<string>::const_iterator i = right.begin();
       i != right.end(); ++i)
    right_interned.push_back(in.intern(*i));

  L(FL("calculating left edit script on %d -> %d lines")
    % anc_interned.size() % left_interned.size());

  edit_script(anc_interned.begin(), anc_interned.end(),
              left_interned.begin(), left_interned.end(),
              min(ancestor.size(), left.size()),
              left_edits);

  L(FL("calculating right edit script on %d -> %d lines")
    % anc_interned.size() % right_interned.size());

  edit_script(anc_interned.begin(), anc_interned.end(),
              right_interned.begin(), right_interned.end(),
              min(ancestor.size(), right.size()),
              right_edits);

  L(FL("calculating left extents on %d edits") % left_edits.size());
  calculate_extents(left_edits, left_interned,
                    left_prefix, left_extents, left_suffix,
                    anc_interned.size(), in);

  L(FL("calculating right extents on %d edits") % right_edits.size());
  calculate_extents(right_edits, right_interned,
                    right_prefix, right_extents, right_suffix,
                    anc_interned.size(), in);

  L(FL("normalizing %d right extents") % right_extents.size());
  normalize_extents(right_extents, anc_interned, right_interned);

  L(FL("normalizing %d left extents") % left_extents.size());
  normalize_extents(left_extents, anc_interned, left_interned);


  if ((!right_prefix.empty()) && (!left_prefix.empty()))
    {
      L(FL("conflicting prefixes"));
      throw conflict();
    }

  if ((!right_suffix.empty()) && (!left_suffix.empty()))
    {
      L(FL("conflicting suffixes"));
      throw conflict();
    }

  L(FL("merging %d left, %d right extents")
    % left_extents.size() % right_extents.size());

  copy(left_prefix.begin(), left_prefix.end(), back_inserter(merged_interned));
  copy(right_prefix.begin(), right_prefix.end(), back_inserter(merged_interned));

  merge_extents(left_extents, right_extents,
                left_interned, right_interned,
                in, merged_interned);

  copy(left_suffix.begin(), left_suffix.end(), back_inserter(merged_interned));
  copy(right_suffix.begin(), right_suffix.end(), back_inserter(merged_interned));

  merged.reserve(merged_interned.size());
  for (vector<long, QA(long)>::const_iterator i = merged_interned.begin();
       i != merged_interned.end(); ++i)
    merged.push_back(in.lookup(*i));
}


bool merge3(vector<string> const & ancestor,
            vector<string> const & left,
            vector<string> const & right,
            vector<string> & merged)
{
  try
   {
      merge_via_edit_scripts(ancestor, left, right, merged);
    }
  catch(conflict &)
    {
      L(FL("conflict detected. no merge."));
      return false;
    }
  return true;
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
