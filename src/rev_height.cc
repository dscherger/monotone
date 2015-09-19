// Copyright (C) 2006 Thomas Moschny <thomas.moschny@gmx.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include <algorithm>
#include <sstream>
#include <utility>

#include "sanity.hh"
#include "rev_height.hh"

using std::make_pair;
using std::min;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::string;

/*
 * Implementation note: hv, holding the raw revision height, is
 * formally a string, but in fact is an array of u32 integers stored
 * in big endian byte order. The same format is used for storing
 * revision heights in the database. This has the advantage that we
 * can use memcmp() for comparing them, which will be the most common
 * operation for revision heights.
 *
 * One could also use vector<u32>. While this would be cleaner, it
 * would force us to convert back and forth to the database format
 * every now and then, and additionally inhibit the use of memcmp().
 *
 */

// Internal manipulations
size_t const width = sizeof(u32);

static u32 read_at(string const & d, size_t pos)
{
  u32 value = 0;
  size_t first = width * pos;

  for (size_t i = first; i < first + width;)
    {
      value <<= 8;
      value += d.at(i++) & 0xff;
    }

  return value;
}

static void write_at(string & d, size_t pos, u32 value)
{
  size_t first = width * pos;
  for (size_t i = first + width ; i > first;)
    {
      d.at(--i) = value & 0xff;
      value >>= 8;
    }
}

static void append(string & d, u32 value)
{
  d.resize(d.size() + width);   // make room
  write_at(d, d.size() / width - 1, value);
}

// Creating derived heights
rev_height rev_height::child_height(u32 nr) const
{
  string child = d;

  if (nr == 0)
    {
      size_t pos = child.size() / width - 1;
      u32 tmp = read_at(child, pos);
      I(tmp < std::numeric_limits<u32>::max());
      write_at(child, pos, tmp + 1);
    }
  else
    {
      append(child, nr - 1);
      append(child, 0);
    }
  return rev_height(child);
}

rev_height rev_height::root_height()
{
  string root;
  append(root, 0);
  return rev_height(root);
}

s64 rev_height::diff_add_rest(size_t pos) const
{
  s64 sum = 0;
  for (size_t i = pos; i < d.size() / width; i += 2)
    sum += read_at(d, i) + 1;
  L(FL("        diff_add_rest returns %d") % sum);
  return sum;
}

// Tries to calculate the difference between two revisions directly from the
// rev_height, if possible. Returns <false, 0> if this isn't possible.
pair<bool, s64> rev_height::distance_to(rev_height const & rhs) const
{
  // Numbers at even indices account for height (i.e. number of commits in
  // that "branch") while numbers at odd positions enumerate children. Note
  // that $PREFIX.$BRANCH_ID.0 is one revision deeper in the tree than just
  // $PREFIX. Independent of the BRANCH_ID. Therefore, to calculate
  // differences, we're only interested in the even numbers plus the depth.
  I(d.size() >= width);
  I(rhs.d.size() >= width);
  I(d.size() % (2 * width) == width);
  I(rhs.d.size() % (2 * width) == width);

  size_t l_size = d.size() / width,
    r_size = rhs.d.size() / width;

  for (size_t i = 0; ; i += 2)
    {
      bool l_cont = (i < l_size),
        r_cont = (i < r_size);

      if (!l_cont && !r_cont)
        return make_pair(true, 0);
      else if (l_cont && r_cont)
        {
          u32 left = read_at(d, i),
            right = read_at(rhs.d, i);

          if (left == right)
            continue;
          else if (left > right)
            {
              if (i + 2 < r_size)
                // diverging branches, cannot calculate difference directly.
                return make_pair(false, 0);
              else
                return make_pair(true,
                                 left - right + diff_add_rest(i + 2));
            }
          else
            {
              if (i + 2 < l_size)
                // diverging branches, cannot calculate difference directly.
                return make_pair(false, 0);
              else
                return make_pair(true,
                                 -(right - left + rhs.diff_add_rest(i + 2)));
            }
        }
      else if (!r_cont)
        return make_pair(true, diff_add_rest(i));
      else
        {
          I(!l_cont);
          return make_pair(true, -(rhs.diff_add_rest(i)));
        }
    }

  // Should never be reached.
  I(false);
}

// Human-readable output
ostream & operator <<(ostream & os, rev_height const & h)
{
  bool first(true);
  string const & d(h());

  for (size_t i = 0; i < d.size() / width; ++i)
    {
      if (!first)
        os << '.';
      os << read_at(d, i);
      first = false;
    }
  return os;
}

void dump(rev_height const & h, string & out)
{
  ostringstream os;
  os << h;
  out = os.str();
}



// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
