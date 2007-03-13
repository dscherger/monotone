// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

/* this file contains modified parts of monotone.cc, */

#include <config.h>
#include "options.hh"
#include <i18n.h>
#include <sanity.hh>
#include <charset.hh>
#include <sstream>
#include <cleanup.hh>
#include <boost/filesystem/path.hpp>
#include <commands.hh>
#include <iostream>
#include <ui.hh>
#include <botan/init.h>
#include <botan/allocate.h>
#include <cmd.hh>
#include "mtncvs_state.hh"
#include <cvs_sync.hh>

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


// Wrapper class to ensure Botan is properly initialized and deinitialized.
struct botan_library
{
  botan_library() { 
    Botan::InitializerOptions options("thread_safe=0 selftest=0 seed_rng=1 "
                                      "use_engines=0 secure_memory=1 "
                                      "fips140=0");
    Botan::LibraryInitializer::initialize(options);
  }
  ~botan_library() {
    Botan::LibraryInitializer::deinitialize();
  }
};

// Similarly, for the global ui object.  (We do not want to use global
// con/destructors for this, as they execute outside the protection of
// main.cc's signal handlers.)
struct ui_library
{
  ui_library() {
    ui.initialize();
  }
  ~ui_library() {
    ui.deinitialize();
  }
};

// fake app_state ctor/dtor, we do not use this class at all
app_state::app_state() : db(system_path()), keys(this), work(db,lua), branch_is_sticky(), project(*this) {}
void app_state::process_options() {}
app_state::~app_state() {}
lua_hooks::lua_hooks() {}
lua_hooks::~lua_hooks() {}
key_store::key_store(app_state*) {}
database::database(system_path const&) : roster_cache(constants::db_roster_cache_sz,roster_writeback_manager(*this)) {}
database::~database() {}
outdated_indicator_factory::outdated_indicator_factory() {}
outdated_indicator_factory::~outdated_indicator_factory() {}
outdated_indicator::outdated_indicator() {}
project_t::project_t(app_state&a) : app(a) {}
ssh_agent::ssh_agent() {}
ssh_agent::~ssh_agent() {}

// missing: compression level (-z), cvs-branch (-r), since (-D)
CMD(pull, N_("network"), N_("[CVS-REPOSITORY CVS-MODULE [CVS-BRANCH]]"),
    N_("(re-)import a module from a remote cvs repository"), 
    options::opts::branch | options::opts::since | options::opts::full)
{
  if (args.size() == 1 || args.size() > 3) throw usage(name);

  std::string repository,module,branch;
  if (args.size() >= 2)
  { repository = idx(args, 0)();
    module = idx(args, 1)();
    if (args.size()==3) 
      branch=idx(args, 2)();
  }
  mtncvs_state &myapp=mtncvs_state::upcast(app);
//myapp.dump();
      
  cvs_sync::pull(repository,module,branch,myapp);
}

CMD(push, N_("network"), N_("[CVS-REPOSITORY CVS-MODULE [CVS-BRANCH]]"),
    N_("commit changes in local database to a remote cvs repository"), 
    options::opts::branch | options::opts::revision | options::opts::first)
{
  if (args.size() == 1 || args.size() > 3) throw usage(name);

  std::string repository,module,branch;
  if (args.size() >= 2)
  { repository = idx(args, 0)();
    module = idx(args, 1)();
    if (args.size()==3) 
      branch=idx(args, 2)();
  }
  mtncvs_state &myapp=mtncvs_state::upcast(app);
  cvs_sync::push(repository,module,branch,myapp);
}

CMD(takeover, N_("working copy"), N_("[CVS-MODULE]"), 
      N_("put a CVS working directory under monotone's control"), 
      options::opts::branch)
{
  if (args.size() > 1) throw usage(name);
  std::string module;
  if (args.size() == 1) module = idx(args, 0)();
  mtncvs_state &myapp=mtncvs_state::upcast(app);
  N(!myapp.opts.branch_name().empty(), F("no destination branch specified\n"));
  cvs_sync::takeover(myapp, module);
}

CMD(test, N_("debug"), "", 
      N_("attempt to parse certs"), 
      options::opts::revision)
{
  if (args.size()) throw usage(name);
  mtncvs_state &myapp=mtncvs_state::upcast(app);
  cvs_sync::test(myapp);
}

#include <package_revision.h>

using std::cout;
using std::cerr;
using std::endl;
using std::ostringstream;
using std::string;

void
get_version(string & out)
{
  out = (F("%s (base revision: %s)")
         % PACKAGE_STRING % string(package_revision_constant)).str();
}

void
print_version()
{
  string s;
  get_version(s);
  cout << s << endl;
}

void
get_full_version(std::string & out)
{ out="mtn_cvs version 0.1 ("+std::string(package_revision_constant)+")";
}

//namespace po = boost::program_options;
using std::cout;
using std::endl;
using std::string;
using std::ios_base;
using std::ostringstream;
using std::set;
using std::string;
using std::vector;
using std::ios_base;
using boost::shared_ptr;

// This is in a sepaarte procedure so it can be called from code that's called
// before cpp_main(), such as program option object creation code.  It's made
// so it can be called multiple times as well.
void localize_monotone()
{
  static int init = 0;
  if (!init)
    {
      setlocale(LC_ALL, "");
//      bindtextdomain(PACKAGE, LOCALEDIR);
//      textdomain(PACKAGE);
      init = 1;
    }
}

// read command-line options and return the command name
string read_options(options & opts, vector<string> args)
{
  option::concrete_option_set optset =
    options::opts::all_options().instantiate(&opts);
  optset.from_command_line(args);

  // consume the command, and perform completion if necessary
  string cmd;
  if (!opts.args.empty())
    cmd = commands::complete_command(idx(opts.args, 0)());

  // reparse options, now that we know what command-specific
  // options are allowed.

  options::options_type cmdopts = commands::command_options(opts.args);
  optset.reset();

  optset = (options::opts::globals() | cmdopts).instantiate(&opts);
  optset.from_command_line(args, false);

  if (!opts.args.empty())
    opts.args.erase(opts.args.begin());

  return cmd;
}

int
cpp_main(int argc, char ** argv)
{
  int ret = 0;

//  atexit(&dumper);

  // go-go gadget i18n
  localize_monotone();

  // set up global ui object - must occur before anything that might try to
  // issue a diagnostic
  ui_library acquire_ui;

  // we want to catch any early informative_failures due to charset
  // conversion etc
  try
  {
  // Set up the global sanity object.  No destructor is needed and
  // therefore no wrapper object is needed either.
  global_sanity.initialize(argc, argv, setlocale(LC_ALL, 0));

  // Set up secure memory allocation etc
  botan_library acquire_botan;

  // Record where we are.  This has to happen before any use of
  // boost::filesystem.
  save_initial_path();

  // decode all argv values into a UTF-8 array
  vector<string> args;
  for (int i = 1; i < argc; ++i)
    {
      external ex(argv[i]);
      utf8 ut;
      system_to_utf8(ex, ut);
      args.push_back(ut());
    }

  // find base name of executable, convert to utf8, and save it in the
  // global ui object
  {
    string prog_name = fs::path(argv[0]).leaf();
    if (prog_name.rfind(".exe") == prog_name.size() - 4)
      prog_name = prog_name.substr(0, prog_name.size() - 4);
    utf8 prog_name_u;
    system_to_utf8(external(prog_name), prog_name_u);
    ui.prog_name = prog_name_u();
    I(!ui.prog_name.empty());
  }

  mtncvs_state app;
  try
    {
      string cmd = read_options(app.opts, args);

      if (app.opts.version_given)
        {
          print_version();
          return 0;
        }

      // stop here if they asked for help
      if (app.opts.help)
        {
          throw usage(cmd);     // cmd may be empty, and that's fine.
        }

      // at this point we allow a workspace (meaning search for it
      // and if found read _MTN/options, but don't use the data quite
      // yet, and read all the monotonercs).  Processing the data
      // from _MTN/options happens later.
      // Certain commands may subsequently require a workspace or fail
      // if we didn't find one at this point.
//      app.allow_workspace();

//      if (!app.found_workspace && global_sanity.filename.empty())
//        global_sanity.filename = (app.get_confdir() / "dump").as_external();

      // main options processed, now invoke the
      // sub-command w/ remaining args
      if (cmd.empty())
        {
          throw usage("");
        }
      else
        {
          vector<utf8> args(app.opts.args.begin(), app.opts.args.end());
          return commands::process(app.downcast(), cmd, args);
        }
    }
  catch (option::option_error const & e)
    {
      N(false, i18n_format("%s") % e.what());
    }
  catch (usage & u)
    {
      // we send --help output to stdout, so that "mtn --help | less" works
      // but we send error-triggered usage information to stderr, so that if
      // you screw up in a script, you don't just get usage information sent
      // merrily down your pipes.
      std::ostream & usage_stream = (app.opts.help ? cout : cerr);

      usage_stream << F("Usage: %s [OPTION...] command [ARG...]") % ui.prog_name << "\n\n";
      usage_stream << options::opts::globals().instantiate(&app.opts).get_usage_str() << "\n";

      // Make sure to hide documentation that's not part of
      // the current command.
      options::options_type cmd_options = commands::toplevel_command_options(u.which);
      if (!cmd_options.empty())
        {
          usage_stream << F("Options specific to '%s %s':") % ui.prog_name % u.which << "\n\n";
          usage_stream << cmd_options.instantiate(&app.opts).get_usage_str() << "\n";
        }

      commands::explain_usage(u.which, usage_stream);
      if (app.opts.help)
        return 0;
      else
        return 2;

    }
  }
  catch (informative_failure & inf)
    {
      ui.inform(inf.what());
      return 1;
    }
  catch (ios_base::failure const & ex)
    {
      // an error has already been printed
      return 1;
    }
  catch (std::exception const & ex)
    {
      ui.fatal_exception (ex);
      return 3;
    }
  catch (...)
    {
      ui.fatal_exception ();
      return 3;
    }

  // control cannot reach this point
  ui.fatal("impossible: reached end of cpp_main");
  return 3;
}

#if 0
int
main(int argc, char **argv)
{
  try
    {
//      ui.set_prog_name("mtn_cvs");
      return cpp_main(argc,argv);
//      return main_with_many_flavours_of_exception(argc, argv);
    }
  catch (std::exception const & e)
    {
      ui.fatal(string(e.what()) + "\n");
      return 3;
    }
}
#endif
