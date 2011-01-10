// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../../../src/merge_content.hh"

#include "../../../src/lexical_cast.hh"
#include "../../../src/paths.hh"
#include "../randomfile.hh"
#include "../../../src/simplestring_xform.hh"

using std::cerr;
using std::cout;
using std::stringstream;
using std::string;
using std::vector;

using boost::lexical_cast;

static void dump_incorrect_merge(vector<string> const & expected,
                                 vector<string> const & got,
                                 string const & prefix)
{
  size_t mx = expected.size();
  if (mx < got.size())
    mx = got.size();
  for (size_t i = 0; i < mx; ++i)
    {
      cerr << "bad merge: " << i << " [" << prefix << "]\t";

      if (i < expected.size())
        cerr << '[' << expected[i] << "]\t";
      else
        cerr << "[--nil--]\t";

      if (i < got.size())
        cerr << '[' << got[i] << "]\t";
      else
        cerr << "[--nil--]\t";

      cerr << '\n';
    }
}

// high tech randomizing test
UNIT_TEST(randomizing_merge)
{
  randomizer rng;
  for (int i = 0; i < 30; ++i)
    {
      vector<string> anc, d1, d2, m1, m2, gm;

      file_randomizer::build_random_fork(anc, d1, d2, gm, (10 + 2 * i), rng);

      UNIT_TEST_CHECK(merge3(anc, d1, d2, m1));
      if (gm != m1)
        dump_incorrect_merge (gm, m1, "random_merge 1");
      UNIT_TEST_CHECK(gm == m1);

      UNIT_TEST_CHECK(merge3(anc, d2, d1, m2));
      if (gm != m2)
        dump_incorrect_merge (gm, m2, "random_merge 2");
      UNIT_TEST_CHECK(gm == m2);
    }
}


// old boring tests
UNIT_TEST(merge_prepend)
{
  UNIT_TEST_CHECKPOINT("prepend test");
  vector<string> anc, d1, d2, m1, m2, gm;
  for (int i = 10; i < 20; ++i)
    {
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  for (int i = 0; i < 10; ++i)
    {
      anc.push_back(lexical_cast<string>(i));
      d1.push_back(lexical_cast<string>(i));
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  UNIT_TEST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_prepend 1");
  UNIT_TEST_CHECK(gm == m1);


  UNIT_TEST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_prepend 2");
  UNIT_TEST_CHECK(gm == m2);
}

UNIT_TEST(merge_append)
{
  UNIT_TEST_CHECKPOINT("append test");
  vector<string> anc, d1, d2, m1, m2, gm;
  for (int i = 0; i < 10; ++i)
      anc.push_back(lexical_cast<string>(i));

  d1 = anc;
  d2 = anc;
  gm = anc;

  for (int i = 10; i < 20; ++i)
    {
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  UNIT_TEST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_append 1");
  UNIT_TEST_CHECK(gm == m1);

  UNIT_TEST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_append 2");
  UNIT_TEST_CHECK(gm == m2);


}

UNIT_TEST(merge_additions)
{
  UNIT_TEST_CHECKPOINT("additions test");
  string ancestor("I like oatmeal\nI like orange juice\nI like toast");
  string desc1("I like oatmeal\nI don't like spam\nI like orange juice\nI like toast");
  string confl("I like oatmeal\nI don't like tuna\nI like orange juice\nI like toast");
  string desc2("I like oatmeal\nI like orange juice\nI don't like tuna\nI like toast");
  string good_merge("I like oatmeal\nI don't like spam\nI like orange juice\nI don't like tuna\nI like toast");
  vector<string> anc, d1, cf, d2, m1, m2, gm;

  split_into_lines(ancestor, anc);
  split_into_lines(desc1, d1);
  split_into_lines(confl, cf);
  split_into_lines(desc2, d2);
  split_into_lines(good_merge, gm);

  UNIT_TEST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_addition 1");
  UNIT_TEST_CHECK(gm == m1);

  UNIT_TEST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_addition 2");
  UNIT_TEST_CHECK(gm == m2);

  UNIT_TEST_CHECK(!merge3(anc, d1, cf, m1));
}

UNIT_TEST(merge_deletions)
{
  string ancestor("I like oatmeal\nI like orange juice\nI like toast");
  string desc2("I like oatmeal\nI like toast");

  vector<string> anc, d1, d2, m1, m2, gm;

  split_into_lines(ancestor, anc);
  split_into_lines(desc2, d2);
  d1 = anc;
  gm = d2;

  UNIT_TEST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_deletion 1");
  UNIT_TEST_CHECK(gm == m1);

  UNIT_TEST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_deletion 2");
  UNIT_TEST_CHECK(gm == m2);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
