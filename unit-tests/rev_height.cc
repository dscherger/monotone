// Copyright (C) 2006 Thomas Moschny <thomas.moschny@gmx.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "unit_tests.hh"
#include "randomizer.hh"

#include "../rev_height.cc"

UNIT_TEST(count_up)
{
  rev_height h = rev_height::root_height().child_height(1);

  I(h().size() / width == 3);
  I(read_at(h(), 0) == 0);
  I(read_at(h(), 1) == 0);
  I(read_at(h(), 2) == 0);
  UNIT_TEST_CHECK_THROW(read_at(h(), 3), std::out_of_range);

  for (u32 n = 1; n < 10000; n++)
    {
      h = h.child_height(0);
      I(read_at(h(), 0) == 0);
      I(read_at(h(), 1) == 0);
      I(read_at(h(), 2) == n);
    }
}

UNIT_TEST(children)
{
  rev_height h;
  I(!h.valid());
  h = rev_height::root_height();
  I(h.valid());
  MM(h);

  randomizer rng;
  for (u32 generations = 0; generations < 200; generations++)
    {
      L(FL("gen %d: %s") % generations % h);

      // generate between five and ten children each time
      u32 children = rng.uniform(5) + 5;
      u32 survivor_no;

      // take the first child 50% of the time, a randomly chosen second or
      // subsequent child the rest of the time.
      if (rng.flip())
        survivor_no = 0;
      else
        survivor_no = 1 + rng.uniform(children - 2);

      L(FL("gen %d: %d children, survivor %d")
        % generations % children % survivor_no);

      u32 parent_len = h().size() / width;
      rev_height survivor;
      MM(survivor);

      for (u32 c = 0; c < children; c++)
        {
          rev_height child = h.child_height(c);
          MM(child);
          I(child.valid());
          if (c == 0)
            {
              I(child().size() / width == parent_len);
              I(read_at(child(), parent_len - 1)
                == read_at(h(), parent_len - 1) + 1);
            }
          else
            {
              I(child().size() / width == parent_len + 2);
              I(read_at(child(), parent_len - 1)
                == read_at(h(), parent_len - 1));
              I(read_at(child(), parent_len) == c - 1);
              I(read_at(child(), parent_len + 1) == 0);
            }
          if (c == survivor_no)
            survivor = child;
        }
      I(survivor.valid());
      h = survivor;
    }
}

UNIT_TEST(comparisons)
{
  rev_height root(rev_height::root_height());
  rev_height left(root.child_height(0));
  rev_height right(root.child_height(1));

  I(root < left);
  I(root < right);
  I(right < left);
  for (u32 i = 0; i < 1000; i++)
    {
      rev_height rchild(right.child_height(0));
      I(right < rchild);
      I(rchild < left);
      right = rchild;
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
