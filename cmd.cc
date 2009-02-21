// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//               2007 Julio M. Merino Vidal <jmmv@NetBSD.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "cmd.hh"
#include "lua.hh"
#include "app_state.hh"
#include "work.hh"
#include "ui.hh"
#include "mt_version.hh"
#include "charset.hh"
#include "simplestring_xform.hh"
#include "vocab_cast.hh"

#ifndef _WIN32
#include <signal.h>
#endif

using std::string;
using std::vector;
using std::ostream;

//
// Definition of top-level commands, used to classify the real commands
// in logical groups.
//
// These top level commands, while part of the final identifiers and defined
// as regular command groups, are handled separately.  The user should not
// see them except through the help command.
//
// XXX This is to easily maintain compatibilty with older versions.  But
// maybe this should be revised, because exposing the top level category
// (being optional, of course), may not be a bad idea.
//

CMD_GROUP(__root__, "__root__", "", NULL, "", "");

CMD_GROUP_NO_COMPLETE(automation, "automation", "", CMD_REF(__root__),
                      N_("Commands that aid in scripted execution"),
                      "");
CMD_GROUP(database, "database", "", CMD_REF(__root__),
          N_("Commands that manipulate the database"),
          "");
CMD_GROUP(debug, "debug", "", CMD_REF(__root__),
          N_("Commands that aid in program debugging"),
          "");
CMD_GROUP(informative, "informative", "", CMD_REF(__root__),
          N_("Commands for information retrieval"),
          "");
CMD_GROUP(key_and_cert, "key_and_cert", "", CMD_REF(__root__),
          N_("Commands to manage keys and certificates"),
          "");
CMD_GROUP(network, "network", "", CMD_REF(__root__),
          N_("Commands that access the network"),
          "");
CMD_GROUP(packet_io, "packet_io", "", CMD_REF(__root__),
          N_("Commands for packet reading and writing"),
          "");
CMD_GROUP(vcs, "vcs", "", CMD_REF(__root__),
          N_("Commands for interaction with other version control systems"),
          "");
CMD_GROUP(review, "review", "", CMD_REF(__root__),
          N_("Commands to review revisions"),
          "");
CMD_GROUP(tree, "tree", "", CMD_REF(__root__),
          N_("Commands to manipulate the tree"),
          "");
CMD_GROUP(variables, "variables", "", CMD_REF(__root__),
          N_("Commands to manage persistent variables"),
          "");
CMD_GROUP(workspace, "workspace", "", CMD_REF(__root__),
          N_("Commands that deal with the workspace"),
          "");
CMD_GROUP(user, "user", "", CMD_REF(__root__),
          N_("Commands defined by the user"),
          "");

namespace commands {

  // monotone.cc calls this function after option processing.
  void process(app_state & app, command_id const & ident,
               args_vector const & args)
  {
    command const * cmd = CMD_REF(__root__)->find_command(ident);

    string visibleid = join_words(vector< utf8 >(ident.begin() + 1,
                                                 ident.end()))();

    I(cmd->is_leaf() || cmd->is_group());
    E(!(cmd->is_group() && cmd->parent() == CMD_REF(__root__)),
      origin::user,
      F("command '%s' is invalid; it is a group") % join_words(ident));

    E(!(!cmd->is_leaf() && args.empty()), origin::user,
      F("no subcommand specified for '%s'") % visibleid);

    E(!(!cmd->is_leaf() && !args.empty()), origin::user,
      F("could not match '%s' to a subcommand of '%s'") %
      join_words(args) % visibleid);

    L(FL("executing command '%s'") % visibleid);

    // at this point we process the data from _MTN/options if
    // the command needs it.
    if (cmd->use_workspace_options())
      {
        workspace::check_ws_format();
        workspace::get_ws_options(app.opts);
      }

    cmd->exec(app, ident, args);
  }

  // Prints the abstract description of the given command or command group
  // properly indented.  The tag starts at column two.  The description has
  // to start, at the very least, two spaces after the tag's end position;
  // this is given by the colabstract parameter.
  static void describe(const string & tag, const string & abstract,
                       const string & subcommands, size_t colabstract,
                       ostream & out)
  {
    I(colabstract > 0);

    size_t col = 0;
    out << "  " << tag << " ";
    col += display_width(utf8(tag + "   ", origin::internal));

    out << string(colabstract - col, ' ');
    col = colabstract;
    string desc(abstract);
    if (!subcommands.empty())
      {
        desc += " (" + subcommands + ')';
      }
    out << format_text(desc, colabstract, col) << '\n';
  }

  static void explain_children(command::children_set const & children,
                               ostream & out)
  {
    I(!children.empty());

    vector< command const * > sorted;

    size_t colabstract = 0;
    for (command::children_set::const_iterator i = children.begin();
         i != children.end(); i++)
      {
        command const * child = *i;

        if (child->hidden())
          continue;

        size_t len = display_width(join_words(child->names(), ", ")) +
            display_width(utf8("    "));
        if (colabstract < len)
          colabstract = len;

        sorted.push_back(child);
      }

    sort(sorted.begin(), sorted.end(), std::greater< command const * >());

    for (vector< command const * >::const_iterator i = sorted.begin();
         i != sorted.end(); i++)
      {
        command const * child = *i;
        describe(join_words(child->names(), ", ")(), child->abstract(),
                 join_words(child->subcommands(), ", ")(),
                 colabstract, out);
      }
  }

  static command const *
  find_command(command_id const & ident)
  {
    command const * cmd = CMD_REF(__root__)->find_command(ident);

    // This function is only used internally with an identifier returned
    // by complete_command.  Therefore, it must always exist.
    I(cmd != NULL);

    return cmd;
  }

  static void explain_cmd_usage(command_id const & ident, ostream & out)
  {
    I(ident.size() >= 1);

    vector< string > lines;
    command const * cmd = find_command(ident);

    string visibleid = join_words(vector< utf8 >(ident.begin() + 1,
                                                 ident.end()))();

    // Print command parameters.
    string params = cmd->params();
    split_into_lines(params, lines);

    if (visibleid.empty())
      out << format_text(F("Commands in group '%s':") %
                         join_words(ident)())
          << "\n\n";
    else
      {
        if (!cmd->children().empty())
          out << format_text(F("Subcommands of '%s %s':") %
                             prog_name % visibleid)
              << "\n\n";
        else if (!lines.empty())
          out << format_text(F("Syntax specific to '%s %s':") %
                             prog_name % visibleid)
              << "\n\n";
      }

    // lines might be empty, but only when specific syntax is to be
    // displayed, not in the other cases.
    if (!lines.empty())
      {
        for (vector<string>::const_iterator j = lines.begin();
             j != lines.end(); ++j)
          out << "  " << visibleid << ' ' << *j << '\n';
        out << '\n';
      }

    // Explain children, if any.
    if (!cmd->is_leaf())
      {
        explain_children(cmd->children(), out);
        out << '\n';
      }

    // Print command description.
    if (visibleid.empty())
      out << format_text(F("Purpose of group '%s':") %
                         join_words(ident)())
          << "\n\n";
    else
      out << format_text(F("Description for '%s %s':") %
                         prog_name % visibleid)
          << "\n\n";
    out << format_text(cmd->desc(), 2) << "\n\n";

    // Print all available aliases.
    if (cmd->names().size() > 1)
      {
        command::names_set othernames = cmd->names();
        othernames.erase(ident[ident.size() - 1]);
        out << format_text(F("Aliases: %s.") %
                           join_words(othernames, ", ")(), 2)
            << '\n';
      }
  }

  void explain_usage(command_id const & ident, ostream & out)
  {
    command const * cmd = find_command(ident);

    if (ident.empty())
      {
        out << format_text(F("Command groups:")) << "\n\n";
        explain_children(CMD_REF(__root__)->children(), out);
        out << '\n'
            << format_text(F("For information on a specific command, type "
                           "'mtn help <command_name> [subcommand_name ...]'."))
            << "\n\n"
            << format_text(F("To see more details about the commands of a "
                           "particular group, type 'mtn help <group_name>'."))
            << "\n\n"
            << format_text(F("Note that you can always abbreviate a command "
                           "name as long as it does not conflict with other "
                           "names."))
            << "\n";
      }
    else
      explain_cmd_usage(ident, out);
  }

  options::options_type command_options(command_id const & ident)
  {
    command const * cmd = find_command(ident);
    return cmd->opts();
  }

  // Lua-defined user commands.
  class cmd_lua : public command
  {
    lua_State *st;
    std::string const f_name;
  public:
    cmd_lua(std::string const & primary_name,
                   std::string const & params,
                   std::string const & abstract,
                   std::string const & desc,
                   lua_State *L_st,
                   std::string const & func_name) :
         command(primary_name, "", CMD_REF(user), false, false, params,
                 abstract, desc, true,
                 options::options_type() | options::opts::none, true),
                 st(L_st), f_name(func_name)
    {
      // because user commands are inserted after the normal
      // initialisation process
      CMD_REF(user)->children().insert(this);
    }

    void exec(app_state & app, command_id const & execid,
              args_vector const & args) const
    {
      I(st);
      I(app.lua.check_lua_state(st));

      app_state* app_p = get_app_state(st);
      I(app_p == & app);

      Lua ll(st);
      ll.func(f_name);

      for (args_vector::const_iterator it = args.begin();
           it != args.end(); ++it)
        ll.push_str((*it)());

      app.mtn_automate_allowed = true;

      ll.call(args.size(),0);

      app.mtn_automate_allowed = false;

      E(ll.ok(), origin::user,
        F("Call to user command %s (lua command: %s) failed.")
        % primary_name() % f_name);
    }
  };
}

LUAEXT(alias_command, )
{
  const char *old_cmd = luaL_checkstring(LS, -2);
  const char *new_cmd = luaL_checkstring(LS, -1);
  E(old_cmd && new_cmd, origin::user,
    F("%s called with an invalid parameter") % "alias_command");

  args_vector args;
  args.push_back(arg_type(old_cmd, origin::user));
  commands::command_id id = commands::complete_command(args);
  commands::command *old_cmd_p = CMD_REF(__root__)->find_command(id);

  old_cmd_p->add_alias(utf8(new_cmd));

  lua_pushboolean(LS, true);
  return 1;
}


LUAEXT(register_command, )
{
  const char *cmd_name = luaL_checkstring(LS, -5);
  const char *cmd_params = luaL_checkstring(LS, -4);
  const char *cmd_abstract = luaL_checkstring(LS, -3);
  const char *cmd_desc = luaL_checkstring(LS, -2);
  const char *cmd_func = luaL_checkstring(LS, -1);

  E(cmd_name && cmd_params && cmd_abstract && cmd_desc && cmd_func,
    origin::user,
    F("%s called with an invalid parameter") % "register_command");

  // leak this - commands can't be removed anyway
  new commands::cmd_lua(cmd_name, cmd_params, cmd_abstract, cmd_desc,
                        LS, cmd_func);

  lua_pushboolean(LS, true);
  return 1;
}

// Miscellaneous commands and related functions for which there is no
// better file.

CMD_NO_WORKSPACE(help, "help", "", CMD_REF(informative),
                 N_("command [ARGS...]"),
    N_("Displays help about commands and options"),
    "",
    options::opts::none)
{
  if (args.size() < 1)
    {
      app.opts.help = true;
      throw usage(command_id());
    }

  command_id id = commands::complete_command(args);
  app.opts.help = true;
  throw usage(id);
}

CMD_NO_WORKSPACE(version, "version", "", CMD_REF(informative), "",
    N_("Shows the program version"),
    "",
    options::opts::full)
{
  E(args.empty(), origin::user,
    F("no arguments allowed"));

  if (app.opts.full)
    print_full_version();
  else
    print_version();
}

CMD_HIDDEN(crash, "crash", "", CMD_REF(debug),
           "{ N | E | I | exception | signal }",
           N_("Triggers the specified kind of crash"),
           "",
           options::opts::none)
{
  if (args.size() != 1)
    throw usage(execid);
  bool spoon_exists(false);
  if (idx(args,0)() == "N")
    E(spoon_exists, origin::user, i18n_format("There is no spoon."));
  else if (idx(args,0)() == "E")
    E(spoon_exists, origin::system, i18n_format("There is no spoon."));
  else if (idx(args,0)() == "I")
    {
      I(spoon_exists);
    }
#define maybe_throw(ex) if(idx(args,0)()==#ex) throw ex("There is no spoon.")
#define maybe_throw_bare(ex) if(idx(args,0)()==#ex) throw ex()
  else maybe_throw_bare(std::bad_alloc);
  else maybe_throw_bare(std::bad_cast);
  else maybe_throw_bare(std::bad_typeid);
  else maybe_throw_bare(std::bad_exception);
  else maybe_throw_bare(std::exception);
  else maybe_throw(std::domain_error);
  else maybe_throw(std::invalid_argument);
  else maybe_throw(std::length_error);
  else maybe_throw(std::out_of_range);
  else maybe_throw(std::range_error);
  else maybe_throw(std::overflow_error);
  else maybe_throw(std::underflow_error);
  else maybe_throw(std::logic_error);
  else maybe_throw(std::runtime_error);
  else
    {
#ifndef _WIN32
      try
        {
          int signo = boost::lexical_cast<int>(idx(args,0)());
          if (0 < signo && signo <= 15)
            {
              raise(signo);
              // control should not get here...
              I(!"crash: raise returned");
            }
        }
      catch (boost::bad_lexical_cast&)
        { // fall through and throw usage
        }
#endif
      throw usage(execid);
    }
#undef maybe_throw
#undef maybe_throw_bare
}

// There isn't really a better place for this function.

void
process_commit_message_args(options const & opts,
                            bool & given,
                            utf8 & log_message,
                            utf8 const & message_prefix)
{
  // can't have both a --message and a --message-file ...
  E(!opts.message_given || !opts.msgfile_given, origin::user,
    F("--message and --message-file are mutually exclusive"));

  if (opts.message_given)
    {
      string msg;
      join_lines(opts.message, msg);
      log_message = utf8(msg, origin::user);
      if (!opts.no_prefix && message_prefix().length() != 0)
        log_message = utf8(message_prefix() + "\n\n" + log_message(),
                           origin::user);
      given = true;
    }
  else if (opts.msgfile_given)
    {
      data dat;
      read_data_for_command_line(opts.msgfile, dat);
      external dat2 = typecast_vocab<external>(dat);
      system_to_utf8(dat2, log_message);
      if (!opts.no_prefix && message_prefix().length() != 0)
        log_message = utf8(message_prefix() + "\n\n" + log_message(),
                           origin::user);
      given = true;
    }
  else if (message_prefix().length() != 0)
    {
      log_message = message_prefix;
      given = true;
    }
  else
    given = false;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
