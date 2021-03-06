// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
// Copyright (C) 2008-2016 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "base.hh"

#include <iterator>
#include <fstream>
#include <sstream>
#include <clocale>
#include <cstdlib>

#include <sqlite3.h>
#include <botan/botan.h>

#include "app_state.hh"
#include "database.hh"
#include "botan_pipe_cache.hh"
#include "commands.hh"
#include "sanity.hh"
#include "file_io.hh"
#include "charset.hh"
#include "ui.hh"
#include "mt_version.hh"
#include "option.hh"
#include "paths.hh"
#include "simplestring_xform.hh"
#include "platform.hh"
#include "work.hh"

using std::string;
using std::ios_base;
using std::ostringstream;
using std::set;
using std::string;
using std::vector;
using std::ios_base;

// main option processing and exception handling code

// options are split into two categories.  the first covers global options,
// which globally affect program behaviour.  the second covers options
// specific to one or more commands.  these command-specific options are
// defined in a single group, with the intent that any command-specific
// option means the same thing for any command that uses it.
//
// "ui" is a global object, through which all messages to the user go.
// see ui.hh for it
//
// "cmds" is a static table in commands.cc which associates top-level
// commands, given on the command-line, to various version control tasks.
//
// "app_state" is a non-static object type which contains all the
// application state (filesystem, database, network, lua interpreter,
// etc). you can make more than one of these, and feed them to a command in
// the command table.

// this file defines cpp_main, which does option processing and sub-command
// dispatching, and provides the outermost exception catch clauses.  it is
// called by main, in unix/main.cc or win32/main.cc; that function is
// responsible for trapping fatal conditions reported by the operating
// system (signals, win32 structured exceptions, etc).

// this program should *never* unexpectedly terminate without dumping some
// diagnostics.  if the fatal condition is an invariant check or anything
// else that produces a C++ exception caught in this file, the debug logs
// will be dumped out.  if the fatal condition is only caught in the lower-
// level handlers in main.cc, at least we'll get a friendly error message.

// define the global objects needed by botan_pipe_cache.hh
pipe_cache_cleanup * global_pipe_cleanup_object;
Botan::Pipe * unfiltered_pipe;
static unsigned char unfiltered_pipe_cleanup_mem[sizeof(cached_botan_pipe)];

void
mtn_terminate_handler()
{
  ui.fatal(F("std::terminate() - "
             "exception thrown while handling another exception"));
  exit(3);
}

int
cpp_main(int argc, char ** argv)
{
  // go-go gadget i18n
  char const * localename = setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, get_locale_dir().c_str());
  textdomain(PACKAGE);

  // set up global ui object - must occur before anything that might try to
  // issue a diagnostic
  ui_library acquire_ui;

  std::set_terminate(&mtn_terminate_handler);

  // we want to catch any early informative_failures due to charset
  // conversion etc
  try
    {
      // Set up the global sanity object.  No destructor is needed and
      // therefore no wrapper object is needed either.  This has the
      // side effect of making the 'prog_name' global usable.
      global_sanity.initialize(argc, argv, localename);

      // Set up secure memory allocation etc
      Botan::LibraryInitializer acquire_botan("thread_safe=0 selftest=0 "
                                              "seed_rng=1 use_engines=0 "
                                              "secure_memory=1 fips140=0");

      // and caching for botan pipes
      pipe_cache_cleanup acquire_botan_pipe_caching;
      unfiltered_pipe = new Botan::Pipe;
      new (unfiltered_pipe_cleanup_mem) cached_botan_pipe(unfiltered_pipe);

      class _DbCacheEmptier {
      public:
        _DbCacheEmptier() { }
        ~_DbCacheEmptier() { database::reset_cache(); }
      } db_cache_emptier;

      // Record where we are.  This has to happen before any use of
      // paths.hh objects.
      save_initial_path();

      // decode all argv values into a UTF-8 array
      args_vector args;
      for (int i = 1; i < argc; ++i)
        {
          external ex(argv[i]);
          utf8 ut;
          system_to_utf8(ex, ut);
          args.push_back(arg_type(ut));
        }

#ifdef SUPPORT_SQLITE_BEFORE_3003014
      E(sqlite3_libversion_number() >= 3003008, origin::system,
        F("this monotone binary requires at least SQLite 3.3.8 to run"));
#else
      E(sqlite3_libversion_number() >= 3003014, origin::system,
        F("this monotone binary requires at least SQLite 3.3.14 to run"));
#endif

      // check the botan library version we got linked against.
      u32 linked_botan_version = BOTAN_VERSION_CODE_FOR(
        Botan::version_major(), Botan::version_minor(),
        Botan::version_patch());

      // Botan 1.7.14 has an incompatible API change, which got reverted
      // again in 1.7.15. Thus we do not care to support 1.7.14.
      E(linked_botan_version != BOTAN_VERSION_CODE_FOR(1,7,14), origin::system,
        F("monotone does not support Botan 1.7.14"));

      // In Botan 1.9.9, the DataSink_Stream cannot be instantiated per
      // se. As 1.10.1 is already out, let's simply disable support for
      // that specific (testing) version of botan.
      E(linked_botan_version != BOTAN_VERSION_CODE_FOR(1,9,9), origin::system,
        F("monotone does not support Botan 1.9.9"));

#if BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,7,7)
      // motonote binary compiled against botan younger than 1.7.7
      E(linked_botan_version >= BOTAN_VERSION_CODE_FOR(1,6,3), origin::system,
        F("this monotone binary requires Botan 1.6.3 or newer"));
      E(linked_botan_version < BOTAN_VERSION_CODE_FOR(1,7,7), origin::system,
        F("this monotone binary does not work with Botan 1.7.7 or newer"));

#elif BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,7,22)
      // motonote binary compiled against botan 1.7.7 - 1.7.21
      E(linked_botan_version >= BOTAN_VERSION_CODE_FOR(1,7,7), origin::system,
        F("this monotone binary requires Botan 1.7.7 or newer"));
      // While compiling against 1.7.22 or newer is recommended, because it
      // enables new features of Botan, the monotone binary compiled against
      // Botan 1.7.21 and before should still work with newer Botan version,
      // including all of the stable branch 1.8.x, up to and including
      // 1.9.3.
      E(linked_botan_version < BOTAN_VERSION_CODE_FOR(1,9,4), origin::system,
        F("this monotone binary does not work with Botan 1.9.4 or newer"));

#elif BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,9,4)
      // motonote binary compiled against botan 1.7.22 - 1.9.3
      E(linked_botan_version >= BOTAN_VERSION_CODE_FOR(1,7,22), origin::system,
        F("this monotone binary requires Botan 1.7.22 or newer"));
      E(linked_botan_version < BOTAN_VERSION_CODE_FOR(1,9,4), origin::system,
        F("this monotone binary does not work with Botan 1.9.4 or newer"));

#elif BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,9,11)
      // motonote binary compiled against botan 1.9.4 - 1.9.10
#pragma message ( "The resulting monotone binary won't be able to run with any stable release of botan." )
      E(linked_botan_version >= BOTAN_VERSION_CODE_FOR(1,9,4), origin::system,
        F("this monotone binary requires Botan 1.9.4 or newer"));
      E(linked_botan_version < BOTAN_VERSION_CODE_FOR(1,9,11), origin::system,
        F("this monotone binary does not work with Botan 1.9.11 or newer"));

#else
      // motonote binary compiled against botan 1.9.11 and newer
      E(linked_botan_version >= BOTAN_VERSION_CODE_FOR(1,9,11), origin::system,
        F("this monotone binary requires Botan 1.9.11 or newer"));
#endif

      app_state app;
      try
        {
          // read global options first
          // command specific options will be read below
          app.reset_info.cmdline_args = args;

          options::opts::all_options().instantiate(&app.opts)
            .from_command_line(app.reset_info.cmdline_args,
                               option::concrete_option_set::preparse);

          if (app.opts.version_given)
            {
              print_version();
              return 0;
            }

          // at this point we allow a workspace (meaning search for it,
          // and if found, change directory to it
          // Certain commands may subsequently require a workspace or fail
          // if we didn't find one at this point.
          workspace::found = app.opts.no_workspace ? false
            : find_and_go_to_workspace(app.opts.root);

          // Load all available monotonercs.  If we found a workspace above,
          // we'll pick up _MTN/monotonerc as well as the user's monotonerc.
          app.lua.load_rcfiles(app.opts);

          // figure out what command is being run
          // this needs to be after the hooks are loaded, because new
          // command names may have been added with the alias_command()
          // lua extension function
          commands::command_id cmd_id;
          if (!app.opts.args.empty())
            cmd_id = commands::complete_command(app.opts.args);

          // check if the user specified default arguments for this command
          if (!cmd_id.empty())
            app.lua.hook_get_default_command_options
              (cmd_id, app.reset_info.default_args);

          if (app.opts.log_given)
            ui.redirect_log_to(app.opts.log);

          global_sanity.set_verbosity(app.opts.verbosity, true);
          if (app.opts.dump_given)
            global_sanity.set_dump_path(app.opts.dump.as_external());
          else if (workspace::found)
            {
              bookkeeping_path dump_path = workspace::get_local_dump_path();

              // The 'false' means that, e.g., if we're running checkout,
              // then it's okay for dumps to go into our starting working
              // dir's _MTN rather than the new workspace dir's _MTN.
              global_sanity.set_dump_path(system_path(dump_path, false)
                                          .as_external());
            }
          else if (app.opts.conf_dir_given || !app.opts.no_default_confdir)
            global_sanity.set_dump_path((app.opts.conf_dir / "dump")
                                        .as_external());

          app.lua.hook_note_mtn_startup(args);

          // query terminal capabilities
          initialize_terminal();

          // stop here if they asked for help
          if (app.opts.help)
            {
              if (have_smart_terminal() && initialize_pager() != 0)
                {
                  L(FL("Failed to initialize the pager."));
                  throw ui.fatal_exception();
                }
              throw usage(cmd_id);
            }

          // main options processed, now invoke the
          // sub-command w/ remaining args
          if (cmd_id.empty())
            throw usage(commands::command_id());

          // as soon as a command requires a workspace, this is set to true
          workspace::used = false;

          commands::process(app, cmd_id, app.opts.args);

          workspace::maybe_set_options(app.opts, app.lua);

          // The command will raise any problems itself through
          // exceptions.  If we reach this point, it is because it
          // worked correctly.
          return 0;
        }
      catch (usage & u)
        {
          ui.inform_usage(u, app.opts);
          return app.opts.help ? 0 : 2;
        }
      // Note that we try to catch the following two exceptions here
      // already, with thi app_state still in scope.
      catch (std::exception const & ex)
        {
          return ui.fatal_exception(ex);
        }
      catch (...)
        {
          return ui.fatal_exception();
        }
    }
  // While these catch handlers only apply to the outer try block and don't
  // have no app_state in scope.
  catch (std::exception const & ex)
    {
      return ui.fatal_exception(ex);
    }
  catch (...)
    {
      return ui.fatal_exception();
    }

  // control cannot reach this point
  ui.fatal("impossible: reached end of cpp_main");
  return 3;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
