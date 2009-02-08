// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
// Copyright (C) 2007 Julio M. Merino Vidal <jmmv@NetBSD.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "unit_tests.hh"
#include "cmd.hh"
#include "simplestring_xform.hh"

using std::set;

// by duplicating these definitions from options.cc we avoid dragging
// that file and all its dependencies into the unit tester

option::option_set<options>
operator | (option::option_set<options> const & opts,
	    option::option_set<options> const & (*fun)())
{
  return opts | fun();
}

options::options_type const & options::opts::none()
{
  static options::options_type val;
  return val;
}

CMD_GROUP(__root__, "__root__", "", NULL, "", "");

CMD_GROUP(top, "top", "", CMD_REF(__root__),
          "", "");
CMD(test, "test", "", CMD_REF(top),
    "", "", "", options::opts::none) {}
CMD(test1, "test1", "alias1", CMD_REF(top),
    "", "", "", options::opts::none) {}
CMD(test2, "test2", "alias2", CMD_REF(top),
    "", "", "", options::opts::none) {}
CMD_HIDDEN(test3, "test3", "", CMD_REF(top),
           "", "", "", options::opts::none) {}

CMD_GROUP(testg, "testg", "aliasg", CMD_REF(top),
          "", "");
CMD(testg1, "testg1", "", CMD_REF(testg),
    "", "", "", options::opts::none) {}
CMD(testg2, "testg2", "", CMD_REF(testg),
    "", "", "", options::opts::none) {}
CMD_HIDDEN(testg3, "testg3", "", CMD_REF(testg),
           "", "", "", options::opts::none) {}

static args_vector
mkargs(const char *words)
{
  return split_into_words(arg_type(words, origin::user));
}

UNIT_TEST(make_command_id)
{
  using commands::command_id;
  using commands::make_command_id;

  {
    command_id id = make_command_id("foo");
    UNIT_TEST_CHECK(id.size() == 1);
    UNIT_TEST_CHECK(id[0]() == "foo");
  }

  {
    command_id id = make_command_id("foo bar");
    UNIT_TEST_CHECK(id.size() == 2);
    UNIT_TEST_CHECK(id[0]() == "foo");
    UNIT_TEST_CHECK(id[1]() == "bar");
  }
}

UNIT_TEST(complete_command)
{
  using commands::command_id;
  using commands::complete_command;
  using commands::make_command_id;

  // Single-word identifier, top-level category.
  {
    command_id id = complete_command(mkargs("top"));
    UNIT_TEST_CHECK(id == make_command_id("top"));
  }

  // Single-word identifier.
  {
    command_id id = complete_command(mkargs("testg"));
    UNIT_TEST_CHECK(id == make_command_id("top testg"));
  }

  // Single-word identifier, non-primary name.
  {
    command_id id = complete_command(mkargs("alias1"));
    UNIT_TEST_CHECK(id == make_command_id("top alias1"));
  }

  // Multi-word identifier.
  {
    command_id id = complete_command(mkargs("testg testg1"));
    UNIT_TEST_CHECK(id == make_command_id("top testg testg1"));
  }

  // Multi-word identifier, non-primary names.
  {
    command_id id = complete_command(mkargs("al testg1"));
    UNIT_TEST_CHECK(id == make_command_id("top aliasg testg1"));
  }
}

UNIT_TEST(command_complete_command)
{
  using commands::command_id;
  using commands::make_command_id;

  // Non-existent single-word identifier.
  {
    command_id id = make_command_id("foo");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.empty());
  }

  // Non-existent multi-word identifier.
  {
    command_id id = make_command_id("foo bar");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.empty());
  }

  // Single-word identifier with one match. Exact matches are found
  // before any possible completions.
  {
    command_id id = make_command_id("test");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 1);
    UNIT_TEST_CHECK(*matches.begin() == make_command_id("test"));
  }

  // Single-word identifier with one match, non-primary name.
  {
    command_id id = make_command_id("alias1");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 1);
    UNIT_TEST_CHECK(*matches.begin() == make_command_id("alias1"));
  }

  // Single-word identifier with multiple matches.
  {
    command_id id = make_command_id("tes");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 4);

    set< command_id > expected;
    expected.insert(make_command_id("test"));
    expected.insert(make_command_id("test1"));
    expected.insert(make_command_id("test2"));
    expected.insert(make_command_id("testg"));
    UNIT_TEST_CHECK(matches == expected);
  }

  // Single-word identifier with multiple matches, non-primary name.
  {
    command_id id = make_command_id("alias");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 3);

    set< command_id > expected;
    expected.insert(make_command_id("alias1"));
    expected.insert(make_command_id("alias2"));
    expected.insert(make_command_id("aliasg"));
    UNIT_TEST_CHECK(matches == expected);
  }

  // Multi-word identifier with one match.
  {
    command_id id = make_command_id("testg testg1");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 1);

    set< command_id > expected;
    expected.insert(make_command_id("testg testg1"));
    UNIT_TEST_CHECK(matches == expected);
  }

  // Multi-word identifier with multiple matches.
  {
    command_id id = make_command_id("testg testg");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 2);

    set< command_id > expected;
    expected.insert(make_command_id("testg testg1"));
    expected.insert(make_command_id("testg testg2"));
    UNIT_TEST_CHECK(matches == expected);
  }

  // Multi-word identifier with multiple matches at different levels.
  {
    command_id id = make_command_id("tes testg1");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 4);

    set< command_id > expected;
    expected.insert(make_command_id("test"));
    expected.insert(make_command_id("test1"));
    expected.insert(make_command_id("test2"));
    expected.insert(make_command_id("testg testg1"));
    UNIT_TEST_CHECK(matches == expected);
  }

  // Multi-word identifier with one match and extra words.
  {
    command_id id = make_command_id("testg testg1 foo");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 1);

    set< command_id > expected;
    expected.insert(make_command_id("testg testg1"));
    UNIT_TEST_CHECK(matches == expected);
  }
}

UNIT_TEST(command_find_command)
{
  using commands::command;
  using commands::command_id;
  using commands::make_command_id;

  // Non-existent single-word identifier.
  {
    command_id id = make_command_id("foo");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == NULL);
  }

  // Non-existent multi-word identifier.
  {
    command_id id = make_command_id("foo bar");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == NULL);
  }

  // Single-word identifier that could be completed.
  {
    command_id id = make_command_id("tes");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == NULL);
  }

  // Single-word identifier.
  {
    command_id id = make_command_id("test1");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == CMD_REF(test1));
  }

  // Hidden single-word identifier.
  {
    command_id id = make_command_id("test3");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == CMD_REF(test3));
  }

  // Multi-word identifier that could be completed.
  {
    command_id id = make_command_id("testg testg");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == NULL);
  }

  // Multi-word identifier.
  {
    command_id id = make_command_id("testg testg1");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == CMD_REF(testg1));
  }

  // Hidden multi-word identifier.
  {
    command_id id = make_command_id("testg testg3");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == CMD_REF(testg3));
  }

  // Multi-word identifier with extra words.
  {
    command_id id = make_command_id("testg testg1 foo");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == NULL);
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
