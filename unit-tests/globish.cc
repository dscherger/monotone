// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//               2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "unit_tests.hh"
#include "globish.hh"
#include "option.hh"

using std::string;
using std::vector;

UNIT_TEST(syntax)
{
  struct tcase
  {
    char const * in;
    char const * out;
  };
  tcase const good[] = {
    { "a",   "a" },
    { "\\a", "a" },
    { "[a]", "a" },
    { "[!a]", "[!a]" },
    { "[^a]", "[!a]" },
    { "[\\!a]", "[\\!a]" },
    { "[\\^a]", "[\\^a]" },
    { "[ab]", "[ab]" },
    { "[a-b]", "[ab]" },
    { "[a-c]", "[abc]" },
    { "[ac-]", "[\\-ac]" },
    { "[-ac]", "[\\-ac]" },
    { "[+-/]", "[+\\,\\-./]" },

    { "\xC2\xA1", "\xC2\xA1" }, // U+00A1 in UTF8

    { "*",   "*" },
    { "\\*", "\\*" },
    { "[*]", "\\*" },
    { "?",   "?" },
    { "\\?", "\\?" },
    { "[?]", "\\?" },
    { ",",   "\\," },
    { "\\,", "\\," },
    { "[,]", "\\," },
    { "\\{", "\\{" },
    { "[{]", "\\{" },
    { "[}]", "\\}" },
    { "\\[", "\\[" },
    { "\\]", "\\]" },
    { "\\\\", "\\\\" },

    { "**",      "*" },
    { "*?",      "?*" },
    { "*???*?*", "????*" },
    { "*a?*?b*", "*a??*b*" },

    { "{a,b,c}d", "{a,b,c}d" },
    { "foo{a,{b,c},?*}d", "foo{a,{b,c},?*}d" },
    { "\\a\\b\\|\\{\\*", "ab|\\{\\*" },
    { ".+$^{}", ".+$\\^{}" },
    { "\\.\\+\\$\\^\\(\\)", ".+$\\^()" },
    { 0, 0 }
  };

  char const * const bad[] = {
    "[",
    "[!",
    "[\\",
    "[\\]",
    "[foo",
    "[!foo",
    "foo]",
    "[\003]",
    "[a-a]",
    "[f-a]",
    "[]",
    "[\xC2\xA1]",
    "[\xC2\xA1\xC2\xA2]",
    "[\xC2\xA1-\xC2\xA2]",
    "[-\xC2\xA1]",
    "[[]",
    "[]",

    "\003",
    "foo\\",
    "{foo",
    "{foo,bar{baz,quux}",
    "foo}",
    "foo,bar{baz,quux}}",
    "{{{{{{{{{{a,b},c},d},e},f},g},h},i},j},k}",
    0
  };
  char const dummy[] = "";

  for (tcase const * p = good; p->in; p++)
    {
      globish g(p->in, origin::internal);
      string s;
      dump(g, s);
      L(FL("globish syntax: %s -> %s [expect %s]") % p->in % s % p->out);
      UNIT_TEST_CHECK(s == p->out);
    }

  for (char const * const * p = bad; *p; p++)
    {
      L(FL("globish syntax: invalid %s") % *p);
      UNIT_TEST_CHECK_THROW(I(globish(*p, origin::user).matches(dummy)), recoverable_failure);
      UNIT_TEST_CHECK_THROW(I(globish(*p, origin::internal).matches(dummy)), unrecoverable_failure);
    }
}

UNIT_TEST(from_vector)
{
  vector<arg_type> v;
  v.push_back(arg_type("a", origin::internal));
  v.push_back(arg_type("b", origin::internal));
  v.push_back(arg_type("c", origin::internal));
  globish combined(v);
  string s;
  dump(combined, s);
  UNIT_TEST_CHECK(s == "{a,b,c}");
}

UNIT_TEST(simple_matches)
{
  UNIT_TEST_CHECK(globish("abc", origin::internal).matches("abc"));
  UNIT_TEST_CHECK(!globish("abc", origin::internal).matches("aac"));

  UNIT_TEST_CHECK(globish("a[bc]d", origin::internal).matches("abd"));
  UNIT_TEST_CHECK(globish("a[bc]d", origin::internal).matches("acd"));
  UNIT_TEST_CHECK(!globish("a[bc]d", origin::internal).matches("and"));
  UNIT_TEST_CHECK(!globish("a[bc]d", origin::internal).matches("ad"));
  UNIT_TEST_CHECK(!globish("a[bc]d", origin::internal).matches("abbd"));

  UNIT_TEST_CHECK(globish("a[!bc]d", origin::internal).matches("and"));
  UNIT_TEST_CHECK(globish("a[!bc]d", origin::internal).matches("a#d"));
  UNIT_TEST_CHECK(!globish("a[!bc]d", origin::internal).matches("abd"));
  UNIT_TEST_CHECK(!globish("a[!bc]d", origin::internal).matches("acd"));
  UNIT_TEST_CHECK(!globish("a[!bc]d", origin::internal).matches("ad"));
  UNIT_TEST_CHECK(!globish("a[!bc]d", origin::internal).matches("abbd"));

  UNIT_TEST_CHECK(globish("a?c", origin::internal).matches("abc"));
  UNIT_TEST_CHECK(globish("a?c", origin::internal).matches("aac"));
  UNIT_TEST_CHECK(globish("a?c", origin::internal).matches("a%c"));
  UNIT_TEST_CHECK(!globish("a?c", origin::internal).matches("a%d"));
  UNIT_TEST_CHECK(!globish("a?c", origin::internal).matches("d%d"));
  UNIT_TEST_CHECK(!globish("a?c", origin::internal).matches("d%c"));
  UNIT_TEST_CHECK(!globish("a?c", origin::internal).matches("a%%d"));

  UNIT_TEST_CHECK(globish("a*c", origin::internal).matches("ac"));
  UNIT_TEST_CHECK(globish("a*c", origin::internal).matches("abc"));
  UNIT_TEST_CHECK(globish("a*c", origin::internal).matches("abac"));
  UNIT_TEST_CHECK(globish("a*c", origin::internal).matches("abbcc"));
  UNIT_TEST_CHECK(globish("a*c", origin::internal).matches("abcbbc"));
  UNIT_TEST_CHECK(!globish("a*c", origin::internal).matches("abcbb"));
  UNIT_TEST_CHECK(!globish("a*c", origin::internal).matches("abcb"));
  UNIT_TEST_CHECK(!globish("a*c", origin::internal).matches("aba"));
  UNIT_TEST_CHECK(!globish("a*c", origin::internal).matches("ab"));

  UNIT_TEST_CHECK(globish("*.bak", origin::internal).matches(".bak"));
  UNIT_TEST_CHECK(globish("*.bak", origin::internal).matches("a.bak"));
  UNIT_TEST_CHECK(globish("*.bak", origin::internal).matches("foo.bak"));
  UNIT_TEST_CHECK(globish("*.bak", origin::internal).matches(".bak.bak"));
  UNIT_TEST_CHECK(globish("*.bak", origin::internal).matches("fwibble.bak.bak"));

  UNIT_TEST_CHECK(globish("a*b*[cd]", origin::internal).matches("abc"));
  UNIT_TEST_CHECK(globish("a*b*[cd]", origin::internal).matches("abcd"));
  UNIT_TEST_CHECK(globish("a*b*[cd]", origin::internal).matches("aabrd"));
  UNIT_TEST_CHECK(globish("a*b*[cd]", origin::internal).matches("abbbbbbbccd"));
  UNIT_TEST_CHECK(!globish("a*b*[cd]", origin::internal).matches("ab"));
  UNIT_TEST_CHECK(!globish("a*b*[cd]", origin::internal).matches("abde"));
  UNIT_TEST_CHECK(!globish("a*b*[cd]", origin::internal).matches("aaaaaaab"));
  UNIT_TEST_CHECK(!globish("a*b*[cd]", origin::internal).matches("axxxxd"));
  UNIT_TEST_CHECK(!globish("a*b*[cd]", origin::internal).matches("adb"));
}

UNIT_TEST(complex_matches)
{  {
    globish_matcher m(globish("{a,b}?*\\*|", origin::internal),
                      globish("*c*", origin::internal));
    UNIT_TEST_CHECK(m("aq*|"));
    UNIT_TEST_CHECK(m("bq*|"));
    UNIT_TEST_CHECK(!m("bc*|"));
    UNIT_TEST_CHECK(!m("bq|"));
    UNIT_TEST_CHECK(!m("b*|"));
    UNIT_TEST_CHECK(!m(""));
  }
  {
    globish_matcher m(globish("{a,\\\\,b*}", origin::internal),
                      globish("*c*", origin::internal));
    UNIT_TEST_CHECK(m("a"));
    UNIT_TEST_CHECK(!m("ab"));
    UNIT_TEST_CHECK(m("\\"));
    UNIT_TEST_CHECK(!m("\\\\"));
    UNIT_TEST_CHECK(m("b"));
    UNIT_TEST_CHECK(m("bfoobar"));
    UNIT_TEST_CHECK(!m("bfoobarcfoobar"));
  }
  {
    globish_matcher m(globish("*", origin::internal),
                      globish("", origin::internal));
    UNIT_TEST_CHECK(m("foo"));
    UNIT_TEST_CHECK(m(""));
  }
  {
    globish_matcher m(globish("{foo}", origin::internal),
                      globish("", origin::internal));
    UNIT_TEST_CHECK(m("foo"));
    UNIT_TEST_CHECK(!m("bar"));
  }
}

UNIT_TEST(nested_matches)
{  {
    globish g("a.{i.{x,y},j}", origin::internal);
    UNIT_TEST_CHECK(g.matches("a.i.x"));
    UNIT_TEST_CHECK(g.matches("a.i.y"));
    UNIT_TEST_CHECK(g.matches("a.j"));
    UNIT_TEST_CHECK(!g.matches("q"));
    UNIT_TEST_CHECK(!g.matches("a.q"));
    UNIT_TEST_CHECK(!g.matches("a.j.q"));
    UNIT_TEST_CHECK(!g.matches("a.i.q"));
    UNIT_TEST_CHECK(!g.matches("a.i.x.q"));
  }
  {
    globish g("a.b{,.c}", origin::internal);
    UNIT_TEST_CHECK(g.matches("a.b"));
    UNIT_TEST_CHECK(g.matches("a.b.c"));
    UNIT_TEST_CHECK(!g.matches("a.b."));
    UNIT_TEST_CHECK(!g.matches("a.b.\\,"));
    UNIT_TEST_CHECK(!g.matches("a.b.\\,.c"));
  }
  {
    globish g("a.b{.c,}", origin::internal);
    UNIT_TEST_CHECK(g.matches("a.b"));
    UNIT_TEST_CHECK(g.matches("a.b.c"));
    UNIT_TEST_CHECK(!g.matches("a.b.c\\,"));
  }
  {
    globish g("a.b{.c,,.d}", origin::internal);
    UNIT_TEST_CHECK(g.matches("a.b"));
    UNIT_TEST_CHECK(g.matches("a.b.c"));
    UNIT_TEST_CHECK(g.matches("a.b.d"));
    UNIT_TEST_CHECK(!g.matches("a.b."));
    UNIT_TEST_CHECK(!g.matches("a.b.c\\,"));
    UNIT_TEST_CHECK(!g.matches("a.b.c\\,\\,"));
    UNIT_TEST_CHECK(!g.matches("a.b.c\\,\\,.d"));
    UNIT_TEST_CHECK(!g.matches("a.b.c\\,.d"));
    UNIT_TEST_CHECK(!g.matches("a.b.c.d"));
  }
  {
    globish g("a.b{.c,}.d", origin::internal);
    UNIT_TEST_CHECK(g.matches("a.b.d"));
    UNIT_TEST_CHECK(g.matches("a.b.c.d"));
    UNIT_TEST_CHECK(!g.matches("a.b.c\\,.d"));
  }
  {
    globish g("a.b{,.c}.d", origin::internal);
    UNIT_TEST_CHECK(g.matches("a.b.d"));
    UNIT_TEST_CHECK(g.matches("a.b.c.d"));
    UNIT_TEST_CHECK(!g.matches("a.b.c\\,.d"));
  }
  {
    globish g("a.b{.c,,.e}.d", origin::internal);
    UNIT_TEST_CHECK(g.matches("a.b.d"));
    UNIT_TEST_CHECK(g.matches("a.b.c.d"));
    UNIT_TEST_CHECK(g.matches("a.b.e.d"));
  }
  {
    globish g("{a.,}b", origin::internal);
    UNIT_TEST_CHECK(g.matches("a.b"));
    UNIT_TEST_CHECK(g.matches("b"));
    UNIT_TEST_CHECK(!g.matches("a.\\,b"));
  }
  {
    globish g("{,a.}b", origin::internal);
    UNIT_TEST_CHECK(g.matches("b"));
    UNIT_TEST_CHECK(g.matches("a.b"));
    UNIT_TEST_CHECK(!g.matches("\\,a.b"));
  }
  {
    globish g("{a.,,c.}b", origin::internal);
    UNIT_TEST_CHECK(g.matches("a.b"));
    UNIT_TEST_CHECK(g.matches("c.b"));
    UNIT_TEST_CHECK(g.matches("b"));
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
