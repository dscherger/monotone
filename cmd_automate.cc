// Copyright (C) 2002, 2008 Graydon Hoare <graydon@pobox.com>
//               2010 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iostream>
#include <sstream>
#include <map>
#include <unistd.h>

#include "cmd.hh"
#include "app_state.hh"
#include "automate_ostream.hh"
#include "automate_reader.hh"
#include "ui.hh"
#include "lua.hh"
#include "lua_hooks.hh"
#include "database.hh"
#include "work.hh"

using std::istream;
using std::make_pair;
using std::map;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;

CMD_GROUP(automate, "automate", "au", CMD_REF(automation),
          N_("Interface for scripted execution"),
          "");

namespace commands {
  automate::automate(string const & name,
                     bool stdio_ok,
                     bool hidden,
                     string const & params,
                     string const & abstract,
                     string const & desc,
                     options::options_type const & opts) :
    command(name, "", CMD_REF(automate), false, hidden, params, abstract,
            // We set use_workspace_options true, because all automate
            // commands need a database, and they expect to get the database
            // name from the workspace options, even if they don't need a
            // workspace for anything else.
            desc, true, opts, false),
    stdio_ok(stdio_ok)
  {
  }

  void
  automate::exec(app_state & app,
                 command_id const & execid,
                 args_vector const & args,
                 std::ostream & output) const
  {
    make_io_binary();
    exec_from_automate(app, execid, args, output);
  }

  void
  automate::exec(app_state & app,
                 command_id const & execid,
                 args_vector const & args) const
  {
    exec(app, execid, args, std::cout);
  }

  bool
  automate::can_run_from_stdio() const
  {
    return stdio_ok;
  }
}

// This number is only raised once, during the process of releasing a new
// version of monotone, by the release manager. For more details, see
// point (2) in notes/release-checklist.txt
static string const interface_version = "12.0";

// This number determines the format version of the stdio packet format.
// The original format which came without a version notification was "1".
static string const stdio_format_version = "2";

// Name: interface_version
// Arguments: none
// Added in: 0.0
// Purpose: Prints version of automation interface.  Major number increments
//   whenever a backwards incompatible change is made; minor number increments
//   whenever any change is made (but is reset when major number
//   increments).
// Output format: "<decimal number>.<decimal number>\n".  Always matches
//   "[0-9]+\.[0-9]+\n".
// Error conditions: None.
CMD_AUTOMATE(interface_version, "",
             N_("Prints the automation interface's version"),
             "",
             options::opts::none)
{
  E(args.empty(), origin::user,
    F("no arguments needed"));

  output << interface_version << '\n';
}

// these headers are outputted before any other output for stdio and remote_stdio
void commands::get_stdio_headers(std::vector<std::pair<std::string,std::string> > & headers)
{
    headers.push_back(make_pair("format-version", stdio_format_version));
}

// Name: bandtest
// Arguments: { info | warning | error | fatal | ticker }
// Added in: FIXME
// Purpose: Emulates certain kinds of diagnostic / UI messages for debugging
//          and testing purposes
// Output format: None
// Error conditions: None.
CMD_AUTOMATE_HIDDEN(bandtest, "{ info | warning | error | ticker }",
             N_("Emulates certain kinds of diagnostic / UI messages "
                "for debugging and testing purposes, such as stdio"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  std::string type = args.at(0)();
  if (type.compare("info") == 0)
    P(F("this is an informational message"));
  else if (type.compare("warning") == 0)
    W(F("this is a warning"));
  else if (type.compare("error") == 0)
    E(false, origin::user, F("this is an error message"));
  else if (type.compare("ticker") == 0)
    {
      ticker first("fake ticker (not fixed)", "f1", 3);
      ticker second("fake ticker (fixed)", "f2", 5);

      int max = 20;
      second.set_total(max);

      for (int i=0; i<max; i++)
        {
          first+=3;
          ++second;
          usleep(100000); // 100ms
        }
    }
  else
    I(false);
}


static void out_of_band_to_automate_streambuf(char channel, std::string const& text, void *opaque)
{
  reinterpret_cast<automate_ostream*>(opaque)->write_out_of_band(channel, text);
}

// Name: stdio
// Arguments: none
// Added in: 1.0
// Purpose: Allow multiple automate commands to be run from one instance
//   of monotone.
//
// Input format: The input is a series of lines of the form
//   'l'<size>':'<string>[<size>':'<string>...]'e', with characters
//   after the 'e' of one command, but before the 'l' of the next ignored.
//   This space is reserved, and should not contain characters other
//   than '\n'.
//   Example:
//     l6:leavese
//     l7:parents40:0e3171212f34839c2e3263e7282cdeea22fc5378e
//
// Output format: <command number>:<err code>:<stream>:<size>:<output>
//   <command number> is a decimal number specifying which command
//   this output is from. It is 0 for the first command, and increases
//   by one each time.
//   <err code> is 0 for success, 1 for a syntax error, and 2 for any
//   other error.
//   <stream> is 'l' if this is the last piece of output for this command,
//   and 'm' if there is more output to come. Otherwise, 'e', 'p' and 'w'
//   notify the caller about errors, informational messages and warnings.
//   A special type 't' outputs progress information for long-term actions.
//   <size> is the number of bytes in the output.
//   <output> is the output of the command.
//   Example:
//     0:0:l:205:0e3171212f34839c2e3263e7282cdeea22fc5378
//     1f4ef73c3e056883c6a5ff66728dd764557db5e6
//     2133c52680aa2492b18ed902bdef7e083464c0b8
//     23501f8afd1f9ee037019765309b0f8428567f8a
//     2c295fcf5fe20301557b9b3a5b4d437b5ab8ec8c
//     1:0:l:41:7706a422ccad41621c958affa999b1a1dd644e79
//
// Error conditions: Errors encountered by the commands run only set
//   the error code in the output for that command. Malformed input
//   results in exit with a non-zero return value and an error message.
CMD_AUTOMATE_NO_STDIO(stdio, "",
                      N_("Automates several commands in one run"),
                      "",
                      options::opts::automate_stdio_size)
{
  E(args.empty(), origin::user,
    F("no arguments needed"));

  database db(app);

  // initialize the database early so any calling process is notified
  // immediately if a version discrepancy exists
  db.ensure_open();

  // disable user prompts, f.e. for password decryption
  app.opts.non_interactive = true;
  options original_opts = app.opts;

  automate_ostream os(output, app.opts.automate_stdio_size);
  automate_reader ar(std::cin);

  std::vector<std::pair<std::string, std::string> > headers;
  commands::get_stdio_headers(headers);
  os.write_headers(headers);

  vector<pair<string, string> > params;
  vector<string> cmdline;
  global_sanity.set_out_of_band_handler(&out_of_band_to_automate_streambuf, &os);

  while (true)
    {
      automate const * acmd = 0;
      command_id id;
      args_vector args;

      // FIXME: what follows is largely duplicated
      // in network/automate_session.cc::do_work()
      //
      // stdio decoding errors should be noted with errno 1,
      // errno 2 is reserved for errors from the commands itself
      try
        {
          if (!ar.get_command(params, cmdline))
            break;

          vector<string>::iterator i = cmdline.begin();
          for (; i != cmdline.end(); ++i)
            {
              args.push_back(arg_type(*i, origin::user));
              id.push_back(utf8(*i, origin::user));
            }

          set< command_id > matches =
            CMD_REF(automate)->complete_command(id);

          if (matches.empty())
            {
              E(false, origin::user,
                F("no completions for this command"));
            }
          else if (matches.size() > 1)
            {
              E(false, origin::user,
                F("multiple completions possible for this command"));
            }

          id = *matches.begin();

          I(args.size() >= id.size());
          for (command_id::size_type i = 0; i < id.size(); i++)
            args.erase(args.begin());

          command const * cmd = CMD_REF(automate)->find_command(id);
          I(cmd != NULL);

          acmd = dynamic_cast< automate const * >(cmd);
          I(acmd != NULL);

          E(acmd->can_run_from_stdio(), origin::network,
            F("sorry, that can't be run remotely or over stdio"));

          if (cmd->use_workspace_options())
            {
              // Re-read the ws options file, rather than just copying
              // the options from the previous apts.opts object, because
              // the file may have changed due to user activity.
              workspace::check_format();
              workspace::get_options(app.opts);
            }

          options::options_type opts;
          opts = options::opts::globals() | cmd->opts();
          opts.instantiate(&app.opts).from_key_value_pairs(params);

          // set a fixed ticker type regardless what the user wants to
          // see, because anything else would screw the stdio-encoded output
          ui.set_tick_write_stdio();
        }
      // FIXME: we need to re-package and rethrow this special exception
      // since it is not based on informative_failure
      catch (option::option_error & e)
        {
          os.write_out_of_band('e', e.what());
          os.end_cmd(1);
          ar.reset();
          continue;
        }
      catch (recoverable_failure & f)
        {
          os.write_out_of_band('e', f.what());
          os.end_cmd(1);
          ar.reset();
          continue;
        }

      try
        {
          acmd->exec_from_automate(app, id, args, os);
          os.end_cmd(0);

          // restore app.opts
          app.opts = original_opts;
        }
      catch (recoverable_failure & f)
        {
          os.write_out_of_band('e', f.what());
          os.end_cmd(2);
        }
    }
    global_sanity.set_out_of_band_handler();
}

LUAEXT(change_workspace, )
{
  const system_path ws(luaL_checkstring(LS, -1), origin::user);
  app_state* app_p = get_app_state(LS);

  try
    {
      go_to_workspace(ws);
    }
  catch (recoverable_failure & f)
    {
      // need a variant of P that doesn't require F?
      P(F(f.what()));
    }

  // go_to_workspace doesn't check that it is a workspace, nor set workspace::found!
  if (path_exists(ws / bookkeeping_root_component / ".") )
    {
      workspace::found = true;
      return 1;
    }
  else
    {
      P(F("directory %s is not a workspace") % ws);
      return 0;
    }
}

LUAEXT(mtn_automate, )
{
  args_vector args;
  std::stringstream output;
  bool result = true;
  std::stringstream & os = output;

  try
    {
      app_state* app_p = get_app_state(LS);
      I(app_p != NULL);
      I(app_p->lua.check_lua_state(LS));
      E(app_p->mtn_automate_allowed, origin::user,
          F("It is illegal to call the mtn_automate() lua extension,\n"
            "unless from a command function defined by register_command()."));

      // don't allow recursive calls
      app_p->mtn_automate_allowed = false;

      int n = lua_gettop(LS);

      E(n > 0, origin::user,
        F("Bad input to mtn_automate() lua extension: command name is missing"));

      L(FL("Starting call to mtn_automate lua hook"));

      for (int i=1; i<=n; i++)
        {
          arg_type next_arg(luaL_checkstring(LS, i), origin::user);
          L(FL("arg: %s")%next_arg());
          args.push_back(next_arg);
        }

      // disable user prompts, f.e. for password decryption
      app_p->opts.non_interactive = true;

      options::options_type opts;
      opts = options::opts::all_options() - options::opts::globals();
      opts.instantiate(&app_p->opts).reset();

      // the arguments for a command are read from app.opts.args which
      // is already cleaned from all options. this variable, however, still
      // contains the original arguments with which the user function was
      // called. Since we're already in lua context, it makes no sense to
      // preserve them for the outside world, so we're just clearing them.
      app_p->opts.args.clear();

      commands::command_id id;
      for (args_vector::const_iterator iter = args.begin();
           iter != args.end(); iter++)
        id.push_back(*iter);

      E(!id.empty(), origin::user, F("no command found"));

      set< commands::command_id > matches =
        CMD_REF(automate)->complete_command(id);

      if (matches.empty())
        {
          E(false, origin::user, F("no completions for this command"));
        }
      else if (matches.size() > 1)
        {
          E(false, origin::user,
            F("multiple completions possible for this command"));
        }

      id = *matches.begin();

      I(args.size() >= id.size());
      for (commands::command_id::size_type i = 0; i < id.size(); i++)
        args.erase(args.begin());

      commands::command const * cmd = CMD_REF(automate)->find_command(id);
      I(cmd != NULL);
      opts = options::opts::globals() | cmd->opts();

      if (cmd->use_workspace_options())
        {
          // Re-read the ws options file, rather than just copying
          // the options from the previous apts.opts object, because
          // the file may have changed due to user activity.
          workspace::check_format();
          workspace::get_options(app_p->opts);
        }

      opts.instantiate(&app_p->opts).from_command_line(args, false);
      args_vector & parsed_args = app_p->opts.args;

      commands::automate const * acmd
        = dynamic_cast< commands::automate const * >(cmd);
      I(acmd);
      acmd->exec(*app_p, id, app_p->opts.args, os);

      // allow further calls
      app_p->mtn_automate_allowed = true;
    }
  catch(recoverable_failure & f)
    {
      // informative failures are passed back to the caller
      result = false;
      L(FL("Informative failure caught inside lua call to mtn_automate: %s") % f.what());
      os.flush();
      output.flush();
      output.str().clear();
      os << f.what();
    }
  catch (std::logic_error & e)
    {
      // invariant failures are permanent
      result = false;
      ui.fatal(e.what());
      lua_pushstring(LS, e.what());
      lua_error(LS);
    }

  os.flush();

  lua_pushboolean(LS, result);
  lua_pushlstring(LS, output.str().data(), output.str().size());
  return 2;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
