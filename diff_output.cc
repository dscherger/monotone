// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "diff_output.hh"
#include "file_io.hh"
#include "interner.hh"
#include "lcs.hh"
#include "pcrewrap.hh"
#include "simplestring_xform.hh"

#include <ostream>
#include <sstream>
#include <iterator>
#include <boost/scoped_ptr.hpp>

using std::max;
using std::min;
using std::ostream;
using std::ostream_iterator;
using std::string;
using std::stringstream;
using std::vector;
using boost::scoped_ptr;

// This file handles printing out various diff formats for the case where
// someone wants to *read* a diff rather than apply it.  The actual diff
// computation is done in lcs.cc.

struct hunk_consumer
{
  vector<string> const & a;
  vector<string> const & b;
  size_t ctx;
  ostream & ost;
  boost::scoped_ptr<pcre::regex const> encloser_re;
  size_t a_begin, b_begin, a_len, b_len;
  long skew;

  vector<string>::const_reverse_iterator encloser_last_match;
  vector<string>::const_reverse_iterator encloser_last_search;

  colorizer color;

  virtual void flush_hunk(size_t pos) = 0;
  virtual void advance_to(size_t newpos) = 0;
  virtual void insert_at(size_t b_pos) = 0;
  virtual void delete_at(size_t a_pos) = 0;
  virtual void find_encloser(size_t pos, string & encloser);
  virtual ~hunk_consumer() {}
  hunk_consumer(vector<string> const & a,
                vector<string> const & b,
                size_t ctx,
                ostream & ost,
                string const & encloser_pattern,
                colorizer const & color)
    : a(a), b(b), ctx(ctx), ost(ost), encloser_re(0),
      a_begin(0), b_begin(0), a_len(0), b_len(0), skew(0),
      encloser_last_match(a.rend()), encloser_last_search(a.rend()),
      color(color)
  {
    if (encloser_pattern != "")
      encloser_re.reset(new pcre::regex(encloser_pattern, origin::user));
  }
};

/* Find, and write to ENCLOSER, the nearest line before POS which matches
   ENCLOSER_PATTERN.  We remember the last line scanned, and the matched, to
   avoid duplication of effort.  */

void
hunk_consumer::find_encloser(size_t pos, string & encloser)
{
  typedef vector<string>::const_reverse_iterator riter;

  // Precondition: encloser_last_search <= pos <= a.size().
  I(pos <= a.size());
  // static_cast<> to silence compiler unsigned vs. signed comparison
  // warning, after first making sure that the static_cast is safe.
  I(a.rend() - encloser_last_search >= 0);
  I(pos >= static_cast<size_t>(a.rend() - encloser_last_search));

  if (!encloser_re)
    return;

  riter last = encloser_last_search;
  riter i    = riter(a.begin() + pos);

  encloser_last_search = i;

  // i is a reverse_iterator, so this loop goes backward through the vector.
  for (; i != last; i++)
    if (encloser_re->match(*i, origin::user))
      {
        encloser_last_match = i;
        break;
      }

  if (encloser_last_match == a.rend())
    return;

  L(FL("find_encloser: from %u matching %d, \"%s\"")
    % pos % (a.rend() - encloser_last_match) % *encloser_last_match);

  // the number 40 is chosen to match GNU diff.  it could safely be
  // increased up to about 60 without overflowing the standard
  // terminal width.
  encloser = string(" ") + (*encloser_last_match).substr(0, 40);
}

void walk_hunk_consumer(vector<long, QA(long)> const & lcs,
                        vector<long, QA(long)> const & lines1,
                        vector<long, QA(long)> const & lines2,
                        hunk_consumer & cons)
{

  size_t a = 0, b = 0;
  if (lcs.begin() == lcs.end())
    {
      // degenerate case: files have nothing in common
      cons.advance_to(0);
      while (a < lines1.size())
        cons.delete_at(a++);
      while (b < lines2.size())
        cons.insert_at(b++);
      cons.flush_hunk(a);
    }
  else
    {
      // normal case: files have something in common
      for (vector<long, QA(long)>::const_iterator i = lcs.begin();
           i != lcs.end(); ++i, ++a, ++b)
        {
          if (idx(lines1, a) == *i && idx(lines2, b) == *i)
            continue;

          cons.advance_to(a);
          while (idx(lines1,a) != *i)
              cons.delete_at(a++);
          while (idx(lines2,b) != *i)
            cons.insert_at(b++);
        }
      if (a < lines1.size())
        {
          cons.advance_to(a);
          while(a < lines1.size())
            cons.delete_at(a++);
        }
      if (b < lines2.size())
        {
          cons.advance_to(a);
          while(b < lines2.size())
            cons.insert_at(b++);
        }
      cons.flush_hunk(a);
    }
}

struct unidiff_hunk_writer : public hunk_consumer
{
  vector<string> hunk;

  virtual void flush_hunk(size_t pos);
  virtual void advance_to(size_t newpos);
  virtual void insert_at(size_t b_pos);
  virtual void delete_at(size_t a_pos);
  virtual ~unidiff_hunk_writer() {}
  unidiff_hunk_writer(vector<string> const & a,
                      vector<string> const & b,
                      size_t ctx,
                      ostream & ost,
                      string const & encloser_pattern,
                      colorizer const & color)
  : hunk_consumer(a, b, ctx, ost, encloser_pattern, color)
  {}
};

void unidiff_hunk_writer::insert_at(size_t b_pos)
{
  b_len++;
  hunk.push_back(color.colorize(string("+") + b[b_pos],
                                    colorizer::add));
}

void unidiff_hunk_writer::delete_at(size_t a_pos)
{
  a_len++;
  hunk.push_back(color.colorize(string("-") + a[a_pos],
                                    colorizer::remove));
}

void unidiff_hunk_writer::flush_hunk(size_t pos)
{
  if (!hunk.empty())
    {
      // insert trailing context
      size_t a_pos = a_begin + a_len;
      for (size_t i = 0; (i < ctx) && (a_pos + i < a.size()); ++i)
        {
          hunk.push_back(string(" ") + a[a_pos + i]);
          a_len++;
          b_len++;
        }

      // write hunk to stream
      stringstream ss;
      if (a_len == 0)
        ss << "@@ -0,0";
      else
        {
          ss << "@@ -" << a_begin+1;
          if (a_len > 1)
            ss << ',' << a_len;
        }
 
      if (b_len == 0)
        ss << " +0,0";
      else
        {
          ss << " +" << b_begin+1;
          if (b_len > 1)
            ss << ',' << b_len;
        }

      {
        string encloser;
        ptrdiff_t first_mod = 0;
        vector<string>::const_iterator i;
        for (i = hunk.begin(); i != hunk.end(); i++)
          if ((*i)[0] != ' ')
            {
              first_mod = i - hunk.begin();
              break;
            }

        find_encloser(a_begin + first_mod, encloser);
        ss << " @@";

        ost << color.colorize(ss.str(), colorizer::separator);
        ost << color.colorize(encloser, colorizer::encloser);
        ost << '\n';
      }
      copy(hunk.begin(), hunk.end(), ostream_iterator<string>(ost, "\n"));
    }

  // reset hunk
  hunk.clear();
  skew += b_len - a_len;
  a_begin = pos;
  b_begin = pos + skew;
  a_len = 0;
  b_len = 0;
}

void unidiff_hunk_writer::advance_to(size_t newpos)
{
  if (a_begin + a_len + (2 * ctx) < newpos || hunk.empty())
    {
      flush_hunk(newpos);

      // insert new leading context
      for (size_t p = max(ctx, newpos) - ctx;
           p < min(a.size(), newpos); ++p)
        {
          hunk.push_back(string(" ") + a[p]);
          a_begin--; a_len++;
          b_begin--; b_len++;
        }
    }
  else
    {
      // pad intermediate context
      while(a_begin + a_len < newpos)
        {
          hunk.push_back(string(" ") + a[a_begin + a_len]);
          a_len++;
          b_len++;
        }
    }
}

struct cxtdiff_hunk_writer : public hunk_consumer
{
  // For context diffs, we have to queue up calls to insert_at/delete_at
  // until we hit an advance_to, so that we can get the tags right: an
  // unpaired insert gets a + in the left margin, an unpaired delete a -,
  // but if they are paired, they both get !.  Hence, we have both the
  // 'inserts' and 'deletes' queues of line numbers, and the 'from_file' and
  // 'to_file' queues of line strings.
  vector<size_t> inserts;
  vector<size_t> deletes;
  vector<string> from_file;
  vector<string> to_file;
  bool have_insertions;
  bool have_deletions;

  virtual void flush_hunk(size_t pos);
  virtual void advance_to(size_t newpos);
  virtual void insert_at(size_t b_pos);
  virtual void delete_at(size_t a_pos);
  void flush_pending_mods();
  virtual ~cxtdiff_hunk_writer() {}
  cxtdiff_hunk_writer(vector<string> const & a,
                      vector<string> const & b,
                      size_t ctx,
                      ostream & ost,
                      string const & encloser_pattern,
                      colorizer const & colorizer)
  : hunk_consumer(a, b, ctx, ost, encloser_pattern, colorizer),
    have_insertions(false), have_deletions(false)
  {}
};

void cxtdiff_hunk_writer::insert_at(size_t b_pos)
{
  inserts.push_back(b_pos);
  have_insertions = true;
}

void cxtdiff_hunk_writer::delete_at(size_t a_pos)
{
  deletes.push_back(a_pos);
  have_deletions = true;
}

void cxtdiff_hunk_writer::flush_hunk(size_t pos)
{
  flush_pending_mods();

  if (have_deletions || have_insertions)
    {
      // insert trailing context
      size_t ctx_start = a_begin + a_len;
      for (size_t i = 0; (i < ctx) && (ctx_start + i < a.size()); ++i)
        {
          from_file.push_back(string("  ") + a[ctx_start + i]);
          a_len++;
        }

      ctx_start = b_begin + b_len;
      for (size_t i = 0; (i < ctx) && (ctx_start + i < b.size()); ++i)
        {
          to_file.push_back(string("  ") + b[ctx_start + i]);
          b_len++;
        }

      {
        string encloser;
        ptrdiff_t first_insert = b_len;
        ptrdiff_t first_delete = a_len;
        vector<string>::const_iterator i;

        if (have_deletions)
          for (i = from_file.begin(); i != from_file.end(); i++)
            if ((*i)[0] != ' ')
              {
                first_delete = i - from_file.begin();
                break;
              }
        if (have_insertions)
          for (i = to_file.begin(); i != to_file.end(); i++)
            if ((*i)[0] != ' ')
              {
                first_insert = i - to_file.begin();
                break;
              }

        find_encloser(a_begin + min(first_insert, first_delete),
                      encloser);

        ost << color.colorize("***************", colorizer::separator)
            << color.colorize(encloser, colorizer::encloser) << '\n';
      }

      ost << "*** " << (a_begin + 1) << ',' << (a_begin + a_len) << " ****\n";
      if (have_deletions)
        copy(from_file.begin(), from_file.end(), ostream_iterator<string>(ost, "\n"));

      ost << "--- " << (b_begin + 1) << ',' << (b_begin + b_len) << " ----\n";
      if (have_insertions)
        copy(to_file.begin(), to_file.end(), ostream_iterator<string>(ost, "\n"));
    }

  // reset hunk
  to_file.clear();
  from_file.clear();
  have_insertions = false;
  have_deletions = false;
  skew += b_len - a_len;
  a_begin = pos;
  b_begin = pos + skew;
  a_len = 0;
  b_len = 0;
}

void cxtdiff_hunk_writer::flush_pending_mods()
{
  // nothing to flush?
  if (inserts.empty() && deletes.empty())
    return;

  string prefix;

  // if we have just insertions to flush, prefix them with "+"; if
  // just deletions, prefix with "-"; if both, prefix with "!"
  colorizer::purpose p = colorizer::normal;
  if (inserts.empty() && !deletes.empty())
  {
    prefix = "-";
    p = colorizer::remove;
  }
  else if (deletes.empty() && !inserts.empty())
  {
    prefix = "+";
    p = colorizer::add;
  }
  else
  {
    prefix = "!";
    p = colorizer::change;
  }

  for (vector<size_t>::const_iterator i = deletes.begin();
       i != deletes.end(); ++i)
    {
      from_file.push_back(color.colorize(prefix + string(" ") + a[*i], p));
      a_len++;
    }
  for (vector<size_t>::const_iterator i = inserts.begin();
       i != inserts.end(); ++i)
    {
      to_file.push_back(color.colorize(prefix + string(" ") + b[*i], p));
      b_len++;
    }

  // clear pending mods
  inserts.clear();
  deletes.clear();
}

void cxtdiff_hunk_writer::advance_to(size_t newpos)
{
  // We must first flush out pending mods because otherwise our calculation
  // of whether we need to generate a new hunk header will be way off.
  // It is correct (i.e. consistent with diff(1)) to reset the +/-/!
  // generation algorithm between sub-components of a single hunk.
  flush_pending_mods();

  if (a_begin + a_len + (2 * ctx) < newpos)
    {
      flush_hunk(newpos);

      // insert new leading context
      if (newpos - ctx < a.size())
        {
          for (size_t i = ctx; i > 0; --i)
            {
              // The original test was (newpos - i < 0), but since newpos
              // is size_t (unsigned), it will never go negative.  Testing
              // that newpos is smaller than i is the same test, really.
              if (newpos < i)
                continue;

              // note that context diffs prefix common text with two
              // spaces, whereas unified diffs use a single space
              from_file.push_back(string("  ") + a[newpos - i]);
              to_file.push_back(string("  ") + a[newpos - i]);
              a_begin--; a_len++;
              b_begin--; b_len++;
            }
        }
    }
  else
    // pad intermediate context
    while (a_begin + a_len < newpos)
      {
        from_file.push_back(string("  ") + a[a_begin + a_len]);
        to_file.push_back(string("  ") + a[a_begin + a_len]);
        a_len++;
        b_len++;
      }
}

void
make_diff(string const & filename1,
          string const & filename2,
          file_id const & id1,
          file_id const & id2,
          data const & data1,
          data const & data2,
          ostream & ost,
          diff_type type,
          string const & pattern,
          colorizer const & color)
{
  if (guess_binary(data1()) || guess_binary(data2()))
    {
      // If a file has been removed, filename2 will be "/dev/null".
      // It doesn't make sense to output that.
      if (filename2 == "/dev/null")
        ost << color.colorize(string("# ") + filename1 + " is binary",
                              colorizer::comment) << "\n";
      else
        ost << color.colorize(string("# ") + filename2 + " is binary",
                              colorizer::comment) << "\n";
      return;
    }

  vector<string> lines1, lines2;
  split_into_lines(data1(), lines1, split_flags::diff_compat);
  split_into_lines(data2(), lines2, split_flags::diff_compat);

  vector<long, QA(long)> left_interned;
  vector<long, QA(long)> right_interned;
  vector<long, QA(long)> lcs;

  interner<long> in;

  left_interned.reserve(lines1.size());
  for (vector<string>::const_iterator i = lines1.begin();
       i != lines1.end(); ++i)
    left_interned.push_back(in.intern(*i));

  right_interned.reserve(lines2.size());
  for (vector<string>::const_iterator i = lines2.begin();
       i != lines2.end(); ++i)
    right_interned.push_back(in.intern(*i));

  lcs.reserve(min(lines1.size(),lines2.size()));
  longest_common_subsequence(left_interned.begin(), left_interned.end(),
                             right_interned.begin(), right_interned.end(),
                             min(lines1.size(), lines2.size()),
                             back_inserter(lcs));

  // The existence of various hacky diff parsers in the world somewhat
  // constrains what output we can use.  Here are some notes on how various
  // tools interpret the header lines of a diff file:
  //
  // interdiff/filterdiff (patchutils):
  //   Attempt to parse a timestamp after each whitespace.  If they succeed,
  //   then they take the filename as everything up to the whitespace they
  //   succeeded at, and the timestamp as everything after.  If they fail,
  //   then they take the filename to be everything up to the first
  //   whitespace.  Have hardcoded that /dev/null and timestamps at the
  //   epoch (in any timezone) indicate a file that did not exist.
  //
  //   filterdiff filters on the first filename line.  interdiff matches on
  //   the first filename line.
  // PatchReader perl library (used by Bugzilla):
  //   Takes the filename to be everything up to the first tab; requires
  //   that there be a tab.  Determines the filename based on the first
  //   filename line.
  // diffstat:
  //   Can handle pretty much everything; tries to read up to the first tab
  //   to get the filename.  Knows that "/dev/null", "", and anything
  //   beginning "/tmp/" are meaningless.  Uses the second filename line.
  // patch:
  //   If there is a tab, considers everything up to that tab to be the
  //   filename.  If there is not a tab, considers everything up to the
  //   first whitespace to be the filename.
  //
  //   Contains comment: 'If the [file]name is "/dev/null", ignore the name
  //   and mark the file as being nonexistent.  The name "/dev/null" appears
  //   in patches regardless of how NULL_DEVICE is spelled.'  Also detects
  //   timestamps at the epoch as indicating that a file does not exist.
  //
  //   Uses the first filename line as the target, unless it is /dev/null or
  //   has an epoch timestamp in which case it uses the second.
  // trac:
  //   Anything up to the first whitespace, or end of line, is considered
  //   filename.  Does not care about timestamp.  Uses the shorter of the
  //   two filenames as the filename (!).
  //
  // Conclusions:
  //   -- You must have a tab, both to prevent PatchReader blowing up, and
  //      to make it possible to have filenames with spaces in them.
  //      (Filenames with tabs in them are always impossible to properly
  //      express; FIXME what should be done if one occurs?)
  //   -- What comes after that tab matters not at all, though it probably
  //      shouldn't look like a timestamp, or have any trailing part that
  //      looks like a timestamp, unless it really is a timestamp.  Simply
  //      having a trailing tab should work fine.
  //   -- If you need to express that some file does not exist, you should
  //      use /dev/null as the path.  patch(1) goes so far as to claim that
  //      this is part of the diff format definition.
  //   -- If you want your patches to actually _work_ with patch(1), then
  //      renames are basically hopeless (you can do them by hand _after_
  //      running patch), adds work so long as the first line says either
  //      the new file's name or "/dev/null", nothing else, and deletes work
  //      if the new file name is "/dev/null", nothing else.
  switch (type)
    {
      case unified_diff:
      {
        ost << color.colorize(string("--- ") + filename1,
                              colorizer::remove)
            << '\t' << id1 << '\n';
        ost << color.colorize(string("+++ ") + filename2,
                              colorizer::add)
            << '\t' << id2 << '\n';

        unidiff_hunk_writer hunks(lines1, lines2, 3, ost, pattern, color);
        walk_hunk_consumer(lcs, left_interned, right_interned, hunks);
        break;
      }
      case context_diff:
      {
        ost << color.colorize(string("*** ") + filename1,
                              colorizer::remove)
            << '\t' << id1 << '\n';
        ost << color.colorize(string("--- ") + filename2,
                              colorizer::add)
            << '\t' << id2 << '\n';

        cxtdiff_hunk_writer hunks(lines1, lines2, 3, ost, pattern, color);
        walk_hunk_consumer(lcs, left_interned, right_interned, hunks);
        break;
      }
      default:
      {
        // should never reach this; the external_diff type is not
        // handled by this function.
        I(false);
      }
    }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
