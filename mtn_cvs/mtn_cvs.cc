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
#include <popt/popt.h>
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

char * argstr = NULL;
long arglong = 0;

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
    Botan::Init::initialize();
    Botan::set_default_allocator("malloc");
  }
  ~botan_library() {
    Botan::Init::deinitialize();
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
app_state::app_state() : db(system_path()), keys(this) {}
void app_state::process_options() {}
app_state::~app_state() {}
lua_hooks::lua_hooks() {}
lua_hooks::~lua_hooks() {}
key_store::key_store(app_state*) {}
database::database(system_path const&) {}
database::~database() {}

// missing: compression level (-z), cvs-branch (-r), since (-D)
CMD(pull, N_("network"), N_("[CVS-REPOSITORY CVS-MODULE [CVS-BRANCH]]"),
    N_("(re-)import a module from a remote cvs repository"), 
    option::branch_name % option::since % option::full)
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
  N(!myapp.branch().empty(), F("no destination branch specified\n"));
      
  cvs_sync::pull(repository,module,branch,myapp);
}

CMD(push, N_("network"), N_("[CVS-REPOSITORY CVS-MODULE [CVS-BRANCH]]"),
    N_("commit changes in local database to a remote cvs repository"), 
    option::branch_name % option::revision)
{
  if (args.size() == 1 || args.size() > 3) throw usage(name);

  std::string repository,module,branch;
  if (args.size() >= 2)
  { repository = idx(args, 0)();
    module = idx(args, 1)();
    if (args.size()==3) 
      branch=idx(args, 2)();
  }
//  cvs_sync::push(repository,module,branch,myapp);
}

CMD(takeover, N_("working copy"), N_("[CVS-MODULE]"), 
      N_("put a CVS working directory under monotone's control"), 
      option::branch_name)
{
  if (args.size() > 1) throw usage(name);
  std::string module;
  if (args.size() == 1) module = idx(args, 0)();
  N(!app.branch_name().empty(), F("no destination branch specified\n"));
//  cvs_sync::takeover(myapp, module);
}

void
get_full_version(std::string & out)
{ out="mtn_cvs version 0.1";
}

namespace po = boost::program_options;
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

static void
tokenize_for_command_line(string const & from, vector<string> & to)
{
  // Unfortunately, the tokenizer in basic_io is too format-specific
  to.clear();
  enum quote_type {none, one, two};
  string cur;
  quote_type type = none;
  bool have_tok(false);
  
  for (string::const_iterator i = from.begin(); i != from.end(); ++i)
    {
      if (*i == '\'')
        {
          if (type == none)
            type = one;
          else if (type == one)
            type = none;
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else if (*i == '"')
        {
          if (type == none)
            type = two;
          else if (type == two)
            type = none;
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else if (*i == '\\')
        {
          if (type != one)
            ++i;
          N(i != from.end(), F("Invalid escape in --xargs file"));
          cur += *i;
          have_tok = true;
        }
      else if (string(" \n\t").find(*i) != string::npos)
        {
          if (type == none)
            {
              if (have_tok)
                to.push_back(cur);
              cur.clear();
              have_tok = false;
            }
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else
        {
          cur += *i;
          have_tok = true;
        }
    }
  if (have_tok)
    to.push_back(cur);
}

int 
cpp_main(int argc, char ** argv)
{
  int ret = 0;

//  atexit(&dumper);

  // go-go gadget i18n

  setlocale(LC_ALL, "");
//  bindtextdomain(PACKAGE, LOCALEDIR);
//  textdomain(PACKAGE);

  // set up global ui object - must occur before anything that might try to
  // issue a diagnostic
  ui_library acquire_ui;

  // we want to catch any early informative_failures due to charset
  // conversion etc
  try
  {
  bool requested_help=false;

  // Set up the global sanity object.  No destructor is needed and
  // therefore no wrapper object is needed either.
  global_sanity.initialize(argc, argv, setlocale(LC_ALL, 0));

  // Set up secure memory allocation etc
  botan_library acquire_botan;

  // set up some marked strings, so even if our logbuf overflows, we'll get
  // this data in a crash.
  std::string cmdline_string;
  {
    std::ostringstream cmdline_ss;
    for (int i = 0; i < argc; ++i)
      {
        if (i)
          cmdline_ss << ", ";
        cmdline_ss << "'" << argv[i] << "'";
      }
    cmdline_string = cmdline_ss.str();
  }
  MM(cmdline_string);
  L(FL("command line: %s\n") % cmdline_string);

  std::string locale_string = (setlocale(LC_ALL, NULL) == NULL ? "n/a" : setlocale(LC_ALL, NULL));
  MM(locale_string);
  L(FL("set locale: LC_ALL=%s\n") % locale_string);

  std::string full_version_string;
  get_full_version(full_version_string);
  MM(full_version_string);

  // Set up secure memory allocation etc
  Botan::Init::initialize();
  Botan::set_default_allocator("malloc");
  
  // decode all argv values into a UTF-8 array
  save_initial_path();
  std::vector<std::string> args;
  utf8 progname;
  for (int i = 0; i < argc; ++i)
    {
      external ex(argv[i]);
      utf8 ut;
      system_to_utf8(ex, ut);
      if (i)
        args.push_back(ut());
      else
        progname = ut;
    }

  // find base name of executable
  std::string prog_path = fs::path(progname()).leaf();
  if (prog_path.rfind(".exe") == prog_path.size() - 4)
    prog_path = prog_path.substr(0, prog_path.size() - 4);
  utf8 prog_name(prog_path);

  mtncvs_state app;
  try
    {

//      app.set_prog_name(prog_name);

      // set up for parsing.  we add a hidden argument that collections all
      // positional arguments, which we process ourselves in a moment.
      po::options_description all_options;
      all_options.add(option::global_options);
      all_options.add(option::specific_options);
      all_options.add_options()
        ("all_positional_args", po::value< vector<string> >());
      po::positional_options_description all_positional_args;
      all_positional_args.add("all_positional_args", -1);

      // Check the command line for -@/--xargs
      {
        po::parsed_options parsed = po::command_line_parser(args)
          .style(po::command_line_style::default_style &
                 ~po::command_line_style::allow_guessing)
          .options(all_options)
          .run();
        po::variables_map vm;
        po::store(parsed, vm);
        po::notify(vm);
#if 0
        if (option::argfile.given(vm))
          {
            vector<string> files = option::argfile.get(vm);
            for (vector<string>::iterator f = files.begin();
                 f != files.end(); ++f)
              {
                data dat;
                read_data_for_command_line(*f, dat);
                vector<string> fargs;
                tokenize_for_command_line(dat(), fargs);
                for (vector<string>::const_iterator i = fargs.begin();
                     i != fargs.end(); ++i)
                  {
                    args.push_back(*i);
                  }
              }
          }
#endif
      }

      po::parsed_options parsed = po::command_line_parser(args)
        .style(po::command_line_style::default_style &
               ~po::command_line_style::allow_guessing)
        .options(all_options)
        .positional(all_positional_args)
        .run();
      po::variables_map vm;
      po::store(parsed, vm);
      po::notify(vm);

      // consume the command, and perform completion if necessary
      std::string cmd;
      vector<string> positional_args;
      if (vm.count("all_positional_args"))
        {
          positional_args = vm["all_positional_args"].as< vector<string> >();
          cmd = commands::complete_command(idx(positional_args, 0));
          positional_args.erase(positional_args.begin());
        }

      // build an options_description specific to this cmd.
      po::options_description cmd_options_desc = commands::command_options(cmd);

      po::options_description all_for_this_cmd;
      all_for_this_cmd.add(option::global_options);
      all_for_this_cmd.add(cmd_options_desc);

      // reparse arguments using specific options.
      parsed = po::command_line_parser(args)
        .style(po::command_line_style::default_style &
               ~po::command_line_style::allow_guessing)
        .options(all_for_this_cmd)
        .run();
      po::store(parsed, vm);
      po::notify(vm);

      if (option::debug.given(vm))
        {
          global_sanity.set_debug();
        }

      if (option::full.given(vm)) app.full=true;
            
      if (option::since.given(vm)) app.since=string(option::since.get(vm));

      if (option::branch_name.given(vm))
      { L(FL("branch %s") % argstr);
        app.branch=option::branch_name.get(vm);
      }

      if (option::help.given(vm)) requested_help = true;
            
      if (option::mtn.given(vm)) app.mtn_binary = option::mtn.get(vm);
            
      if (option::db.given(vm)) 
        app.mtn_options.push_back(string("--db=")+option::db.get(vm));

      if (option::rcfile.given(vm)) 
        app.mtn_options.push_back(string("--rcfile=")+option::rcfile.get(vm));

      if (option::nostd.given(vm)) 
        app.mtn_options.push_back(string("--nostd"));

      if (option::norc.given(vm)) 
        app.mtn_options.push_back(string("--norc"));

      if (option::keydir.given(vm)) 
        app.mtn_options.push_back(string("--keydir=")+option::keydir.get(vm));

      if (option::root.given(vm)) 
        app.mtn_options.push_back(string("--root=")+option::root.get(vm));

      if (option::confdir.given(vm)) 
        app.mtn_options.push_back(string("--confdir=")+option::confdir.get(vm));

      if (option::key.given(vm)) 
        app.mtn_options.push_back(string("--key=")+option::key.get(vm));

      // stop here if they asked for help
      if (requested_help)
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
          vector<utf8> args(positional_args.begin(), positional_args.end());
          return commands::process(app.downcast(), cmd, args);
        }
    }
  catch (po::ambiguous_option const & e)
    {
      std::string msg = (F("%s:\n") % e.what()).str();
      vector<string>::const_iterator it = e.alternatives.begin();
      for (; it != e.alternatives.end(); ++it)
        msg += *it + "\n";
      N(false, i18n_format(msg));
    }
  catch (po::error const & e)
    {
      N(false, F("%s") % e.what());
    }
  catch (usage & u)
    {
      // Make sure to hide documentation that's not part of
      // the current command.

      po::options_description cmd_options_desc = commands::command_options(u.which);
      unsigned count = cmd_options_desc.options().size();

      cout << F("Usage: %s [OPTION...] command [ARG...]") % prog_name << "\n\n";
      cout << option::global_options << "\n";

      if (count > 0)
        {
          cout << F("Options specific to '%s %s':") % prog_name % u.which << "\n\n";
          cout << cmd_options_desc << "\n";
        }

      commands::explain_usage(u.which, cout);
      if (requested_help)
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

int
main(int argc, char **argv)
{
  try
    {
      ui.set_prog_name("mtn_cvs");
      return cpp_main(argc,argv);
//      return main_with_many_flavours_of_exception(argc, argv);
    }
  catch (std::exception const & e)
    {
      ui.fatal(string(e.what()) + "\n");
      return 3;
    }
}

