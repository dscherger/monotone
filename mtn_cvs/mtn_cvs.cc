// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

/* this file contains modified parts of monotone.cc, */

#include <config.h>
#include "base.hh"
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
#include <constants.hh>
#include <database.hh>
#include "../botan_pipe_cache.hh"
#include "../simplestring_xform.hh"

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


// Wrapper class which ensures proper setup and teardown of the global ui
// object.  (We do not want to use global con/destructors for this, as they
// execute outside the protection of main.cc's signal handlers.)
struct ui_library
{
  ui_library() { ui.initialize(); }
  ~ui_library() { ui.deinitialize(); }
};

// fake app_state ctor/dtor, we do not use this class at all
app_state::app_state() : lua(this), mtn_automate_allowed(false) {}
app_state::~app_state() {}
lua_hooks::lua_hooks(app_state * app) {}
lua_hooks::~lua_hooks() {}
//database::database(app_state &app) : imp(), lua(app.lua) {}
//database::~database() {}
//ssh_agent::ssh_agent() {}
//ssh_agent::~ssh_agent() {}

CMD_GROUP(__root__, "__root__", "", NULL, "", "");

CMD_GROUP(network, "network", "", CMD_REF(__root__),
          N_("Commands that access the network"),
          "");
CMD_GROUP(informative, "informative", "", CMD_REF(__root__),
          N_("Commands for information retrieval"),
          "");

// missing: compression level (-z), cvs-branch (-r), since (-D)
CMD(pull, "pull", "", CMD_REF(network), 
    N_("[CVS-REPOSITORY CVS-MODULE [CVS-BRANCH]]"),
    N_("(re-)import a module from a remote cvs repository"), "",
    options::opts::branch | options::opts::since | options::opts::full)
{
  if (args.size() == 1 || args.size() > 3) throw usage(execid);

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

CMD(push, "push", "", CMD_REF(network), 
    N_("[CVS-REPOSITORY CVS-MODULE [CVS-BRANCH]]"),
    N_("commit changes in local database to a remote cvs repository"), "",
    options::opts::branch | options::opts::revision | options::opts::first | options::opts::no_time)
{
  if (args.size() == 1 || args.size() > 3) throw usage(execid);

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

CMD_GROUP(workspace, "workspace", "", CMD_REF(__root__),
          N_("Commands that deal with the workspace"),
          "");

CMD(takeover, "takeover", "", CMD_REF(workspace), 
      N_("[CVS-MODULE]"), 
      N_("put a CVS working directory under monotone's control"), "",
      options::opts::branch)
{
  if (args.size() > 1) throw usage(execid);
  std::string module;
  if (args.size() == 1) module = idx(args, 0)();
  mtncvs_state &myapp=mtncvs_state::upcast(app);
  N(!myapp.opts.branchname().empty(), F("no destination branch specified\n"));
  cvs_sync::takeover(myapp, module);
}

CMD_GROUP(debug, "debug", "", CMD_REF(__root__),
          N_("Commands that aid in program debugging"),
          "");

CMD(test, "test", "", CMD_REF(debug), "", 
      N_("attempt to parse certs"), "",
      options::opts::revision)
{
  if (args.size()) throw usage(execid);
  mtncvs_state &myapp=mtncvs_state::upcast(app);
  cvs_sync::test(myapp);
}

CMD(last_sync, "last_sync", "", CMD_REF(debug), "", 
      N_("find last synced revision"), "",
      options::opts::none)
{
  mtncvs_state &myapp=mtncvs_state::upcast(app);
  std::cout << cvs_sync::last_sync(myapp) << '\n';
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

// define the global objects needed by botan_pipe_cache.hh
pipe_cache_cleanup * global_pipe_cleanup_object;
Botan::Pipe * unfiltered_pipe;
static unsigned char unfiltered_pipe_cleanup_mem[sizeof(cached_botan_pipe)];

option::concrete_option_set
read_global_options(options & opts, args_vector & args)
{
  option::concrete_option_set optset =
    options::opts::all_options().instantiate(&opts);
  optset.from_command_line(args);
  
  return optset;
}

// read command-line options and return the command name
commands::command_id  read_options(options & opts, option::concrete_option_set & optset, args_vector & args)
{
	  commands::command_id cmd;

	  if (!opts.args.empty())
	    {
	      // There are some arguments remaining in the command line.  Try first
	      // to see if they are a command.
	      cmd = commands::complete_command(opts.args);
	      I(!cmd.empty());

	      // Reparse options now that we know what command-specific options
	      // are allowed.
	      options::options_type cmdopts = commands::command_options(cmd);
	      optset.reset();
	      optset = (options::opts::globals() | cmdopts).instantiate(&opts);
	      optset.from_command_line(args, false);

	      // Remove the command name from the arguments.  Rember that the group
	      // is not taken into account.
	      I(opts.args.size() >= cmd.size() - 1);

	      for (args_vector::size_type i = 1; i < cmd.size(); i++)
	        {
	          I(cmd[i]().find(opts.args[0]()) == 0);
	          opts.args.erase(opts.args.begin());
	        }
	    }

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
  Botan::LibraryInitializer acquire_botan("thread_safe=0 selftest=0 "
          "seed_rng=1 use_engines=0 "
          "secure_memory=1 fips140=0");

  // and caching for botan pipes
  pipe_cache_cleanup acquire_botan_pipe_caching;
  unfiltered_pipe = new Botan::Pipe;
  new (unfiltered_pipe_cleanup_mem) cached_botan_pipe(unfiltered_pipe);
  
  // Record where we are.  This has to happen before any use of
  // boost::filesystem.
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

  // find base name of executable, convert to utf8, and save it in the
  // global ui object
  {
    utf8 argv0_u;
    system_to_utf8(external(argv[0]), argv0_u);
    string prog_name = system_path(argv0_u).basename()();
    if (prog_name.rfind(".exe") == prog_name.size() - 4)
      prog_name = prog_name.substr(0, prog_name.size() - 4);
    ui.prog_name = prog_name;
    I(!ui.prog_name.empty());
  }

  mtncvs_state app;
  try
    {
      // read global options first
      // command specific options will be read below
      args_vector opt_args(args);
      option::concrete_option_set optset = read_global_options(app.opts, opt_args);

      if (app.opts.version_given)
        {
          print_version();
          return 0;
        }

      // at this point we allow a workspace (meaning search for it,
      // and if found, change directory to it
      // Certain commands may subsequently require a workspace or fail
      // if we didn't find one at this point.
//      workspace::found = find_and_go_to_workspace(app.opts.root);

      // Load all available monotonercs.  If we found a workspace above,
      // we'll pick up _MTN/monotonerc as well as the user's monotonerc.
//      app.lua.load_rcfiles(app.opts);

      // now grab any command specific options and parse the command
       // this needs to happen after the monotonercs have been read
       commands::command_id cmd = read_options(app.opts, optset, opt_args);

      // stop here if they asked for help
      if (app.opts.help)
        {
          throw usage(cmd);     // cmd may be empty, and that's fine.
        }

      // main options processed, now invoke the
      // sub-command w/ remaining args
      if (cmd.empty())
        {
          throw usage(commands::command_id());
        }
      else
        {
          commands::process(app.downcast(), cmd, app.opts.args);
          // The command will raise any problems itself through
          // exceptions.  If we reach this point, it is because it
          // worked correctly.
          return 0;
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

          string visibleid;
          if (!u.which.empty())
            visibleid = join_words(vector< utf8 >(u.which.begin() + 1,
                                                  u.which.end()))();

      usage_stream << F("Usage: %s [OPTION...] command [ARG...]") % ui.prog_name << "\n\n";
      usage_stream << options::opts::globals().instantiate(&app.opts).get_usage_str() << "\n";

      // Make sure to hide documentation that's not part of
      // the current command.
      options::options_type cmd_options = commands::command_options(u.which);
      if (!cmd_options.empty())
        {
          usage_stream << F("Options specific to '%s %s':") % ui.prog_name % visibleid << "\n\n";
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
