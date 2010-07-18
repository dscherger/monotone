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

#include <iostream>

using std::string;
using std::vector;
using std::set;
using std::ostream;
using std::cout;
using boost::lexical_cast;

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
        workspace::check_format();
        workspace::get_options(app.opts);
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

  class cmd_ptr_compare
  {
  public:
    bool operator()(command const * const a, command const * const b) const
    {
      return a->primary_name()() < b->primary_name()();
    }
  };

  static void explain_children(command::children_set const & children,
                               bool show_hidden_commands,
                               ostream & out)
  {
    I(!children.empty());

    vector< command const * > sorted;

    size_t colabstract = 0;
    for (command::children_set::const_iterator i = children.begin();
         i != children.end(); i++)
      {
        command const * child = *i;

        if (child->hidden() && !show_hidden_commands)
          continue;

        size_t len = display_width(join_words(child->names(), ", ")) +
            display_width(utf8("    "));
        if (colabstract < len)
          colabstract = len;

        sorted.push_back(child);
      }

    sort(sorted.begin(), sorted.end(), cmd_ptr_compare());

    for (vector< command const * >::const_iterator i = sorted.begin();
         i != sorted.end(); i++)
      {
        command const * child = *i;
        describe(join_words(child->names(), ", ")(), child->abstract(),
                 join_words(child->subcommands(show_hidden_commands), ", ")(),
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

  static void explain_cmd_usage(command_id const & ident,
                                bool show_hidden_commands,
                                ostream & out)
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
        explain_children(cmd->children(), show_hidden_commands, out);
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

  void explain_usage(command_id const & ident,
                     bool show_hidden_commands,
                     ostream & out)
  {
    command const * cmd = find_command(ident);

    if (ident.empty())
      {
        out << format_text(F("Command groups:")) << "\n\n";
        explain_children(CMD_REF(__root__)->children(),
                         show_hidden_commands,
                         out);
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
      explain_cmd_usage(ident, show_hidden_commands, out);
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
    options::opts::show_hidden_commands)
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
           "{ N | E | I | double-throw | exception | signal }",
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
  else if (idx(args,0)() == "double-throw")
    {
      // This code is rather picky, for example I(false) in the destructor
      // won't always work like it should; see http://bugs.debian.org/516862
      class throwing_dtor
      {
      public:
        throwing_dtor() {}
        ~throwing_dtor()
        {
          throw std::exception();
        }
      };
      throwing_dtor td;
      throw std::exception();
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

static string
man_italic(string const & content)
{
  return "\\fI" + content + "\\fP";
}

static string
man_bold(string const & content)
{
  return "\\fB" + content + "\\fP";
}

static string
man_definition(vector<string> const & labels, string const & content, int width = -1)
{
  string out;
  out += ".IP \"" + (*labels.begin()) + "\"";

  if (width != -1)
    out += " " + lexical_cast<string>(width);
  out += "\n";

  if (labels.size() > 1)
    {
      out += ".PD 0\n";
      for (vector<string>::const_iterator i = labels.begin() + 1;
           i < labels.end(); ++i)
        {
          out += ".IP \"" + (*i) + "\"\n";
        }
      out += ".PD\n";
    }
  out += content;
  if (content.rfind('\n') != (content.size() - 1))
     out += "\n";
  return out;
}

static string
man_definition(string const & label, string const & content, int width = -1)
{
  vector<string> labels;
  labels.push_back(label);
  return man_definition(labels, content, width);
}

static string
man_indent(string const & content)
{
  return ".RS\n" + content + ".RE\n";
}

static string
man_subsection(string const & content)
{
  return ".SS \"" + content + "\"\n";
}

static string
man_section(string const & content)
{
  return ".SH \"" + uppercase(content) + "\"\n";
}

static string
man_title(string const & title)
{
  return ".TH \"" + title + "\" 1 "+
         "\"" + date_t::now().as_formatted_localtime("%Y-%m-%d") + "\" " +
         "\"" + PACKAGE_STRING + "\"\n";
}

static string
get_options_string(options::options_type const & optset, options & opts, int width = -1)
{
  vector<string> names;
  vector<string> descriptions;
  unsigned int maxnamelen;

  optset.instantiate(&opts).get_usage_strings(names, descriptions, maxnamelen);

  string out;
  vector<string>::const_iterator name;
  vector<string>::const_iterator desc;
  for (name = names.begin(), desc = descriptions.begin();
       name != names.end(); ++name, ++desc)
    {
      if (name->empty())
        continue;
      out += man_definition(*name, *desc, width);
    }
  return out;
}

static string
get_command_tree(options & opts, commands::command const * cmd)
{
  string out;

  commands::command::children_set subcmds = cmd->children();
  for (commands::command::children_set::const_iterator iter = subcmds.begin();
       iter != subcmds.end(); iter++)
    {
      commands::command * subcmd = *iter;
      if (subcmd->hidden() && !opts.show_hidden_commands)
        continue;

      if (!subcmd->is_leaf())
        {
          if (subcmd->parent() == CMD_REF(__root__))
            {
              out += man_subsection(
                  (F("command group '%s'") % subcmd->primary_name()).str()
              );
              out += subcmd->desc() + "\n";
            }

          // we ignore cascaded groups and go right down to the
          // individual commands
          out += get_command_tree(opts, subcmd);
        }
      else
        {
          // there are no top level commands, so this must be an
          // empty group - skip it
          if (subcmd->parent() == CMD_REF(__root__))
            continue;

          // this builds a list of already formatted command calls
          // which are used as label for the specific command section
          vector<string> cmd_calls;

          //
          // newline characters in the parameter section mark
          // alternative call syntaxes which we expand here, i.e.
          // a command "do-foo" with an alias of "foo" and an argument
          // list of "BAR\nBAR BAZ" will be expanded to
          //
          //  do-foo BAR
          //  do-foo BAR BAZ
          //  foo BAR
          //  foo BAR BAZ
          //
          vector<string> params;
          if (!subcmd->params().empty())
            split_into_lines(subcmd->params(), params);

          vector<utf8> main_ident = subcmd->ident();
          typedef set<vector<string> > ident_set;
          ident_set idents;

          commands::command::names_set allnames = subcmd->names();
          for (set<utf8>::const_iterator i = allnames.begin();
               i != allnames.end(); ++i)
            {
              vector<string> full_ident;
              for (vector<utf8>::const_iterator j = main_ident.begin() + 1;
                    j < main_ident.end() - 1;  ++j)
                {
                  full_ident.push_back((*j)());
                }
              full_ident.push_back((*i)());
              idents.insert(full_ident);
            }

          for (ident_set::const_iterator i = idents.begin();
               i != idents.end();  ++i)
            {
              string call, name;
              // cannot use join_words here, since this only
              // works on containers
              join_lines(*i, name, " ");

              if (params.size() == 0)
                {
                  call = man_bold(name);
                  cmd_calls.push_back(call);
                  continue;
                }

              for (vector<string>::const_iterator j = params.begin();
                   j < params.end(); ++j)
                {
                  call = man_bold(name) + " " + *j;
                  cmd_calls.push_back(call);
                }
            }

          string cmd_desc;
          cmd_desc += subcmd->desc() + "\n";

          // this prints an indented list of available command options
          options::options_type cmd_options =
            commands::command_options(main_ident);
          if (!cmd_options.empty())
            {
              cmd_desc += man_indent(get_options_string(cmd_options, opts, 4));
            }

          // compile everything into a man definition
          out += man_definition(cmd_calls, cmd_desc, 4);
        }
    }
    return out;
}

CMD_HIDDEN(manpage, "manpage", "", CMD_REF(informative), "",
    N_("Dumps monotone's command tree in a (g)roff compatible format"),
    "",
    options::opts::show_hidden_commands)
{
  cout << man_title("monotone");
  cout << man_section(_("Name"));

  cout << _("monotone - a distributed version control system") << "\n";
  cout << man_section(_("Synopsis"));
  cout << man_bold(prog_name) << " "
            << man_italic(_("[options...] command [arguments...]"))
            << "\n";

  cout << man_section(_("Description"));
  cout << _("monotone is a highly reliable, very customizable distributed "
            "version control system that provides lightweight branches, "
            "history-sensitive merging and a flexible trust setup. "
            "monotone has an easy-to-learn command set and comes with a rich "
            "interface for scripting purposes and thorough documentation.")
       << "\n\n";
  cout << (F("For more information on monotone, visit %s.")
            % man_bold("http://www.monotone.ca")).str()
       << "\n\n";
  cout << (F("The complete documentation, including a tutorial for a quick start "
             "with the system, can be found online on %s.")
            % man_bold("http://www.monotone.ca/docs")).str() << "\n";

  cout << man_section(_("Global Options"));
  cout << get_options_string(options::opts::globals(), app.opts, 25) << "\n";

  cout << man_section(_("Commands"));
  cout << get_command_tree(app.opts, CMD_REF(__root__));

  cout << man_section(_("See Also"));
  cout << (F("info %s and the documentation on %s")
                % prog_name % man_bold("http://monotone.ca/docs")).str() << "\n";

  cout << man_section("Bugs");
  cout << (F("Please report bugs to %s.")
                % man_bold("http://savannah.nongnu.org/bugs/?group=monotone")).str()<< "\n";

  cout << man_section("Authors");
  cout << _("monotone was written originally by Graydon Hoare "
            "<graydon@pobox.com> in 2004 and has since then received "
            "numerous contributions from many individuals. "
            "A complete list of authors can be found in AUTHORS.")
       << "\n\n";
  cout << _("Nowadays, monotone is maintained by a collective of enthusiastic "
            "programmers, known as the monotone developement team.") << "\n";

  cout << man_section("Copyright");
  cout << (F("monotone and this man page is Copyright (c) 2004 - %s by "
             "the monotone development team.")
             % date_t::now().as_formatted_localtime("%Y")).str() << "\n";
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
