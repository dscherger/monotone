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
#include "cmd.hh"
#include "simplestring_xform.hh"

using std::map;
using std::set;
using std::string;
using std::vector;

// This file defines the logic behind the CMD() family of macros and handles
// command completion.  Note that commands::process is in cmd.cc mainly for
// better encapsulation of functions not needed in the unit tester.

namespace commands
{
  const char * safe_gettext(const char * msgid)
  {
    if (strlen(msgid) == 0)
      return msgid;

    return _(msgid);
  }

  // This must be a pointer.
  // It's used by the constructor of other static objects in different
  // files (cmd_*.cc), and since they're in different files, there's no
  // guarantee about what order they'll be initialized in. So have this
  // be something that doesn't get automatic initialization, and initialize
  // it ourselves the first time we use it.
  typedef map< command *, command * > relation_map;
  static relation_map * cmds_relation_map = NULL;

  static void init_children(void)
  {
    static bool children_inited = false;

    if (!children_inited)
      {
        children_inited = true;

        for (relation_map::iterator iter = cmds_relation_map->begin();
             iter != cmds_relation_map->end(); iter++)
          {
            if ((*iter).second != NULL)
              (*iter).second->children().insert((*iter).first);
          }
      }
  }

  //
  // Implementation of the commands::command class.
  //
  command::command(std::string const & primary_name,
                   std::string const & other_names,
                   command * parent,
                   bool is_group,
                   bool hidden,
                   std::string const & params,
                   std::string const & abstract,
                   std::string const & desc,
                   bool use_workspace_options,
                   options::options_type const & opts,
                   bool _allow_completion)
    : m_primary_name(utf8(primary_name, origin::internal)),
      m_parent(parent),
      m_is_group(is_group),
      m_hidden(hidden),
      m_params(utf8(params, origin::internal)),
      m_abstract(utf8(abstract, origin::internal)),
      m_desc(utf8(desc, origin::internal)),
      m_use_workspace_options(use_workspace_options),
      m_opts(opts),
      m_allow_completion(_allow_completion)
  {
    // A warning about the parent pointer: commands are defined as global
    // variables, so they are initialized during program startup.  As they
    // are spread over different compilation units, we have no idea of the
    // order in which they will be initialized.  Therefore, accessing
    // *parent from here is dangerous.
    //
    // This is the reason for the cmds_relation_map.  We cannot set up
    // the m_children set until a late stage during program execution.

    if (cmds_relation_map == NULL)
      cmds_relation_map = new relation_map();
    (*cmds_relation_map)[this] = m_parent;

    m_names.insert(m_primary_name);

    vector< utf8 > onv = split_into_words(utf8(other_names, origin::internal));
    m_names.insert(onv.begin(), onv.end());
  }

  command::~command(void)
  {
  }

  bool
  command::allow_completion() const
  {
    return m_allow_completion &&
      (m_parent?m_parent->allow_completion():true);
  }

  command_id
  command::ident(void) const
  {
    I(this != CMD_REF(__root__));

    command_id i;

    if (parent() != CMD_REF(__root__))
      i = parent()->ident();
    i.push_back(primary_name());

    I(!i.empty());
    return i;
  }

  const utf8 &
  command::primary_name(void) const
  {
    return m_primary_name;
  }

  const command::names_set &
  command::names(void) const
  {
    return m_names;
  }

  void
  command::add_alias(const utf8 &new_name)
  {
    m_names.insert(new_name);
  }


  command *
  command::parent(void) const
  {
    return m_parent;
  }

  bool
  command::is_group(void) const
  {
    return m_is_group;
  }

  bool
  command::hidden(void) const
  {
    return m_hidden;
  }

  std::string
  command::params() const
  {
    return safe_gettext(m_params().c_str());
  }

  std::string
  command::abstract() const
  {
    return safe_gettext(m_abstract().c_str());
  }

  std::string
  command::desc() const
  {
    return abstract() + ".\n" + safe_gettext(m_desc().c_str());
  }

  command::names_set
  command::subcommands(void) const
  {
    names_set set;
    init_children();
    for (children_set::const_iterator i = m_children.begin();
      i != m_children.end(); i++)
      {
        if ((*i)->hidden())
          continue;
        names_set const & other = (*i)->names();
        set.insert(other.begin(), other.end());
      }
    return set;
  }

  options::options_type const &
  command::opts(void) const
  {
    return m_opts;
  }

  bool
  command::use_workspace_options(void) const
  {
    return m_use_workspace_options;
  }

  command::children_set &
  command::children(void)
  {
    init_children();
    return m_children;
  }

  command::children_set const &
  command::children(void) const
  {
    init_children();
    return m_children;
  }

  bool
  command::is_leaf(void) const
  {
    return children().empty();
  }

  bool
  command::operator<(command const & cmd) const
  {
    // *twitch*
    return (parent()->primary_name() < cmd.parent()->primary_name() ||
            ((parent() == cmd.parent()) &&
             primary_name() < cmd.primary_name()));
  }

  bool
  command::has_name(utf8 const & name) const
  {
    return names().find(name) != names().end();
  }

  command const *
  command::find_command(command_id const & id) const
  {
    command const * cmd;

    if (id.empty())
      cmd = this;
    else
      {
        utf8 component = *(id.begin());
        command const * match = find_child_by_name(component);

        if (match != NULL)
          {
            command_id remaining(id.begin() + 1, id.end());
            I(remaining.size() == id.size() - 1);
            cmd = match->find_command(remaining);
          }
        else
          cmd = NULL;
      }

    return cmd;
  }

  command *
  command::find_command(command_id const & id)
  {
    command * cmd;

    if (id.empty())
      cmd = this;
    else
      {
        utf8 component = *(id.begin());
        command * match = find_child_by_name(component);

        if (match != NULL)
          {
            command_id remaining(id.begin() + 1, id.end());
            I(remaining.size() == id.size() - 1);
            cmd = match->find_command(remaining);
          }
        else
          cmd = NULL;
      }

    return cmd;
  }

  map< command_id, command * >
  command::find_completions(utf8 const & prefix, command_id const & completed,
                            bool completion_ok)
    const
  {
    map< command_id, command * > matches;

    I(!prefix().empty());

    for (children_set::const_iterator iter = children().begin();
         iter != children().end(); iter++)
      {
        command * child = *iter;

        for (names_set::const_iterator iter2 = child->names().begin();
             iter2 != child->names().end(); iter2++)
          {
            command_id caux = completed;
            caux.push_back(*iter2);

            // If one of the command names was an exact match,
            // do not try to find other possible completions.
            // This would  eventually hinder us to ever call a command
            // whose name is also the prefix for another command in the
            // same group (f.e. mtn automate cert and mtn automate certs)
            if (prefix == *iter2)
              {
                // since the command children are not sorted, we
                // need to ensure that no other partial completed
                // commands matched
                matches.clear();
                matches[caux] = child;
                return matches;
              }

            if (!child->hidden() &&
                     prefix().length() < (*iter2)().length() &&
                     allow_completion() && completion_ok)
              {
                string temp((*iter2)(), 0, prefix().length());
                utf8 p(temp, origin::internal);
                if (prefix == p)
                  matches[caux] = child;
              }
          }
      }

    return matches;
  }

  set< command_id >
  command::complete_command(command_id const & id,
                            command_id completed,
                            bool completion_ok) const
  {
    I(this != CMD_REF(__root__) || !id.empty());
    I(!id.empty());

    set< command_id > matches;

    utf8 component = *(id.begin());
    command_id remaining(id.begin() + 1, id.end());

    map< command_id, command * >
      m2 = find_completions(component,
                            completed,
                            allow_completion() && completion_ok);
    for (map< command_id, command * >::const_iterator iter = m2.begin();
         iter != m2.end(); iter++)
      {
        command_id const & i2 = (*iter).first;
        command * child = (*iter).second;

        if (child->is_leaf() || remaining.empty())
          matches.insert(i2);
        else
          {
            I(remaining.size() == id.size() - 1);
            command_id caux = completed;
            caux.push_back(i2[i2.size() - 1]);
            set< command_id > maux = child->complete_command(remaining, caux);
            if (maux.empty())
              matches.insert(i2);
            else
              matches.insert(maux.begin(), maux.end());
          }
      }

    return matches;
  }

  command *
  command::find_child_by_name(utf8 const & name) const
  {
    I(!name().empty());

    command * cmd = NULL;

    for (children_set::const_iterator iter = children().begin();
         iter != children().end() && cmd == NULL; iter++)
      {
        command * child = *iter;

        if (child->has_name(name))
          cmd = child;
      }

    return cmd;
  }
};

namespace std
{
  template <>
  struct greater<commands::command *>
  {
    bool operator()(commands::command const * a, commands::command const * b)
    {
      return *a < *b;
    }
  };
};

namespace commands
{
  command_id
  complete_command(args_vector const & args)
  {
    // Handle categories early; no completion allowed.
    if (CMD_REF(__root__)->find_command(make_command_id(args[0]())) != NULL)
      return make_command_id(args[0]());

    command_id id;
    for (args_vector::const_iterator iter = args.begin();
         iter != args.end(); iter++)
      id.push_back(*iter);

    set< command_id > matches;

    command::children_set const & cs = CMD_REF(__root__)->children();
    for (command::children_set::const_iterator iter = cs.begin();
         iter != cs.end(); iter++)
      {
        command const * child = *iter;

        set< command_id > m2 = child->complete_command(id, child->ident());
        matches.insert(m2.begin(), m2.end());
      }

    if (matches.size() >= 2)
      {
        // If there is an exact match at the lowest level, pick it.  Needed
        // to automatically resolve ambiguities between, e.g., 'drop' and
        // 'dropkey'.
        command_id tmp;

        for (set< command_id >::const_iterator iter = matches.begin();
             iter != matches.end() && tmp.empty(); iter++)
          {
            command_id const & id = *iter;
            I(id.size() >= 2);
            if (id[id.size() - 1]() == args[id.size() - 2]())
              tmp = id;
          }

        if (!tmp.empty())
          {
            matches.clear();
            matches.insert(tmp);
          }
      }

    if (matches.empty())
      {
        E(false, origin::user,
          F("unknown command '%s'") % join_words(id));
      }
    else if (matches.size() == 1)
      {
        id = *matches.begin();
      }
    else
      {
        I(matches.size() > 1);
        string err =
          (F("'%s' is ambiguous; possible completions are:") %
             join_words(id)()).str();
        for (set< command_id >::const_iterator iter = matches.begin();
             iter != matches.end(); iter++)
          err += '\n' + join_words(*iter)();
        E(false, origin::user, i18n_format(err));
      }

    I(!id.empty());
    return id;
  }

  command_id make_command_id(std::string const & path)
  {
    return split_into_words(utf8(path, origin::user));
  }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

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
#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
