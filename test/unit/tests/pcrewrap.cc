// Copyright (C) 2010 Thomas Keller <me@thomaskeller.biz>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../../../src/pcrewrap.hh"

using std::string;
using std::vector;
using pcre::regex;

UNIT_TEST(simple_match)
{
  regex rx1("^\\d+$", origin::internal);
  UNIT_TEST_CHECK(rx1.match("012345", origin::internal));
  UNIT_TEST_CHECK(!rx1.match("abc", origin::internal));

  regex rx2("abc", origin::internal, pcre::CASELESS);
  UNIT_TEST_CHECK(rx2.match("abc", origin::internal));
  UNIT_TEST_CHECK(rx2.match("ABC", origin::internal));

  regex rx3("(.*)", origin::internal);
  UNIT_TEST_CHECK(rx3.match("", origin::internal));
  UNIT_TEST_CHECK(!rx3.match("", origin::internal, pcre::NOTEMPTY));

  // FIXME: test more options...
}

UNIT_TEST(extract_captures)
{
  regex rx1("(ab(c|d))", origin::internal);
  vector<string> matches;

  UNIT_TEST_CHECK(rx1.match("abc", origin::internal, matches));
  UNIT_TEST_CHECK(matches.size() == 3);
  UNIT_TEST_CHECK(matches[0] == "abc");
  UNIT_TEST_CHECK(matches[1] == "abc");
  UNIT_TEST_CHECK(matches[2] == "c");

  UNIT_TEST_CHECK(!rx1.match("abe", origin::internal, matches));
  UNIT_TEST_CHECK(matches.size() == 0);

  regex rx2("a(b(c)?)d", origin::internal);

  UNIT_TEST_CHECK(rx2.match("abd", origin::internal, matches));
  UNIT_TEST_CHECK(matches.size() == 3);
  UNIT_TEST_CHECK(matches[0] == "abd");
  UNIT_TEST_CHECK(matches[1] == "b");
  UNIT_TEST_CHECK(matches[2].empty());

  UNIT_TEST_CHECK(rx2.match("abcd", origin::internal, matches));
  UNIT_TEST_CHECK(matches.size() == 3);
  UNIT_TEST_CHECK(matches[0] == "abcd");
  UNIT_TEST_CHECK(matches[1] == "bc");
  UNIT_TEST_CHECK(matches[2] == "c");

  regex rx3("(abc)(d)?", origin::internal);
  UNIT_TEST_CHECK(rx3.match("abc", origin::internal, matches));
  UNIT_TEST_CHECK(matches.size() == 3);
  UNIT_TEST_CHECK(matches[0] == "abc");
  UNIT_TEST_CHECK(matches[1] == "abc");
  UNIT_TEST_CHECK(matches[2].empty());
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
