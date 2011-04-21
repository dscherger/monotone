// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../../../src/simplestring_xform.hh"

#include "../../../src/vocab.hh"
#include <set>

using std::set;
using std::string;
using std::vector;

UNIT_TEST(caseconv)
{
  UNIT_TEST_CHECK(uppercase("hello") == "HELLO");
  UNIT_TEST_CHECK(uppercase("heLlO") == "HELLO");
  UNIT_TEST_CHECK(lowercase("POODLE DAY") == "poodle day");
  UNIT_TEST_CHECK(lowercase("PooDLe DaY") == "poodle day");
  UNIT_TEST_CHECK(uppercase("!@#$%^&*()") == "!@#$%^&*()");
  UNIT_TEST_CHECK(lowercase("!@#$%^&*()") == "!@#$%^&*()");
}

UNIT_TEST(join_lines)
{
  vector<string> strs;
  string joined;

  strs.clear();
  join_lines(strs, joined);
  UNIT_TEST_CHECK(joined == "");

  strs.push_back("hi");
  join_lines(strs, joined);
  UNIT_TEST_CHECK(joined == "hi\n");

  strs.push_back("there");
  join_lines(strs, joined);
  UNIT_TEST_CHECK(joined == "hi\nthere\n");

  strs.push_back("user");
  join_lines(strs, joined);
  UNIT_TEST_CHECK(joined == "hi\nthere\nuser\n");
}

UNIT_TEST(join_words)
{
  vector< utf8 > v;
  set< utf8 > s;

  v.clear();
  UNIT_TEST_CHECK(join_words(v)() == "");

  v.clear();
  v.push_back(utf8("a"));
  UNIT_TEST_CHECK(join_words(v)() == "a");
  UNIT_TEST_CHECK(join_words(v, ", ")() == "a");

  s.clear();
  s.insert(utf8("a"));
  UNIT_TEST_CHECK(join_words(s)() == "a");
  UNIT_TEST_CHECK(join_words(s, ", ")() == "a");

  v.clear();
  v.push_back(utf8("a"));
  v.push_back(utf8("b"));
  UNIT_TEST_CHECK(join_words(v)() == "a b");
  UNIT_TEST_CHECK(join_words(v, ", ")() == "a, b");

  s.clear();
  s.insert(utf8("b"));
  s.insert(utf8("a"));
  UNIT_TEST_CHECK(join_words(s)() == "a b");
  UNIT_TEST_CHECK(join_words(s, ", ")() == "a, b");

  v.clear();
  v.push_back(utf8("a"));
  v.push_back(utf8("b"));
  v.push_back(utf8("c"));
  UNIT_TEST_CHECK(join_words(v)() == "a b c");
  UNIT_TEST_CHECK(join_words(v, ", ")() == "a, b, c");

  s.clear();
  s.insert(utf8("b"));
  s.insert(utf8("a"));
  s.insert(utf8("c"));
  UNIT_TEST_CHECK(join_words(s)() == "a b c");
  UNIT_TEST_CHECK(join_words(s, ", ")() == "a, b, c");
}

UNIT_TEST(split_into_words)
{
  vector< utf8 > words;

  words = split_into_words(utf8(""));
  UNIT_TEST_CHECK(words.empty());

  words = split_into_words(utf8("foo"));
  UNIT_TEST_CHECK(words.size() == 1);
  UNIT_TEST_CHECK(words[0]() == "foo");

  words = split_into_words(utf8("foo bar"));
  UNIT_TEST_CHECK(words.size() == 2);
  UNIT_TEST_CHECK(words[0]() == "foo");
  UNIT_TEST_CHECK(words[1]() == "bar");

  // describe() in commands.cc assumes this behavior.  If it ever changes,
  // remember to modify that function accordingly!
  words = split_into_words(utf8("foo  bar"));
  UNIT_TEST_CHECK(words.size() == 3);
  UNIT_TEST_CHECK(words[0]() == "foo");
  UNIT_TEST_CHECK(words[1]() == "");
  UNIT_TEST_CHECK(words[2]() == "bar");
}

UNIT_TEST(trimming)
{
  UNIT_TEST_CHECK(trim_right(":foobar:", ":") == ":foobar");
  UNIT_TEST_CHECK(trim_left(":foobar:", ":") == "foobar:");
  UNIT_TEST_CHECK(trim(":foobar:", ":") == "foobar");

  UNIT_TEST_CHECK(trim("\n  leading space") == "leading space");
  UNIT_TEST_CHECK(trim("trailing space  \n") == "trailing space");
  UNIT_TEST_CHECK(trim("\t\n both \r \n\r\n") == "both");

  // strings with nothing but whitespace should trim to nothing
  UNIT_TEST_CHECK(trim_left("   \r\n\r\n\t\t\n\n\r\n   ") == "");
  UNIT_TEST_CHECK(trim_right("   \r\n\r\n\t\t\n\n\r\n   ") == "");
  UNIT_TEST_CHECK(trim("   \r\n\r\n\t\t\n\n\r\n   ") == "");

  UNIT_TEST_CHECK(remove_ws("  I like going\tfor walks\n  ")
              == "Ilikegoingforwalks");

}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
