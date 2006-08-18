// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

/* this file contains modified parts of monotone.cc, */

#include <config.h>
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

#if 0
enum 
{ MTNCVSOPT_DUMMY, 
  MTNCVSOPT_BRANCH_NAME, MTNCVSOPT_REVISION, MTNCVSOPT_DEBUG, MTNCVSOPT_HELP, 
  MTNCVSOPT_VERSION, MTNCVSOPT_MTN_OPTION, MTNCVSOPT_FULL, MTNCVSOPT_SINCE,
  MTNCVSOPT_BINARY, 
  
  MTNCVSOPT_DB, MTNCVSOPT_RCFILE, MTNCVSOPT_NOSTD, MTNCVSOPT_KEYDIR,
  MTNCVSOPT_KEY };
#endif

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

#if 0

struct 
utf8_argv
{
  int argc;
  char **argv;

  explicit utf8_argv(int ac, char **av)
    : argc(ac),
      argv(static_cast<char **>(malloc(ac * sizeof(char *))))
  {
    I(argv != NULL);
    for (int i = 0; i < argc; ++i)
      {
        external ext(av[i]);
        utf8 utf;
        system_to_utf8(ext, utf);
        argv[i] = static_cast<char *>(malloc(utf().size() + 1));
        I(argv[i] != NULL);
        memcpy(argv[i], utf().data(), utf().size());
        argv[i][utf().size()] = static_cast<char>(0);
    }
  }

  ~utf8_argv() 
  {
    if (argv != NULL)
      {
        for (int i = 0; i < argc; ++i)
          if (argv[i] != NULL)
            free(argv[i]);
        free(argv);
      }    
  }
};

// Stupid type system tricks: to use a cleanup_ptr, we need to know the return
// type of the cleanup function.  But popt silently changed the return type of
// poptFreeContext at some point, I guess because they thought it would be
// "backwards compatible".  We don't actually _use_ the return value of
// poptFreeContext, so this little wrapper works.
static void
my_poptFreeContext(poptContext con)
{
  poptFreeContext(con);
}

using namespace std;

static string
coption_string(int o)
{
  char buf[2] = { 0,0 };
  for(struct poptOption *opt = coptions; opt->val; opt++)
    if (o == opt->val)
      {
        buf[0] = opt->shortName;
        return opt->longName
          ? string("--") + string(opt->longName)
          : string("-") + string(buf);
      }
  return string();
}
#endif

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
    MTNCVSOPT_BRANCH_NAME % MTNCVSOPT_SINCE % MTNCVSOPT_FULL)
{
  if (args.size() == 1 || args.size() > 3) throw usage(name);

  string repository,module,branch;
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
    MTNCVSOPT_BRANCH_NAME % MTNCVSOPT_REVISION)
{
  if (args.size() == 1 || args.size() > 3) throw usage(name);

  string repository,module,branch;
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
      MTNCVSOPT_BRANCH_NAME)
{
  if (args.size() > 1) throw usage(name);
  string module;
  if (args.size() == 1) module = idx(args, 0)();
  N(!app.branch_name().empty(), F("no destination branch specified\n"));
//  cvs_sync::takeover(myapp, module);
}

int 
cpp_main(int argc, char ** argv)
{
  int ret = 0;

  atexit(&dumper);

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
//  get_full_version(full_version_string);
//  MM(full_version_string);

  // Set up secure memory allocation etc
  Botan::Init::initialize();
  Botan::set_default_allocator("malloc");
  
  // decode all argv values into a UTF-8 array
  save_initial_path();
  vector<string> args;
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
  string prog_path = fs::path(uv.argv[0]).leaf();
  prog_path = prog_path.substr(0, prog_path.find(".exe", 0));
  utf8 prog_name(prog_path);

  app_state app;
  try
    {

      app.set_prog_name(prog_name);

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
      string cmd;
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

      if (option::quiet.given(vm))
        {
          global_sanity.set_quiet();
          ui.set_tick_writer(new tick_write_nothing);
        }

      if (option::reallyquiet.given(vm))
        {
          global_sanity.set_reallyquiet();
          ui.set_tick_writer(new tick_write_nothing);
        }

      if (option::nostd.given(vm))
        {
          app.set_stdhooks(false);
        }

      if (option::norc.given(vm))
        {
          app.set_rcfiles(false);
        }

      if (option::verbose.given(vm))
        {
          app.set_verbose(true);
        }

      if (option::rcfile.given(vm))
        {
          vector<string> files = option::rcfile.get(vm);
          for (vector<string>::const_iterator i = files.begin();
               i != files.end(); ++i)
            app.add_rcfile(*i);
        }

      if (option::dump.given(vm))
        {
          global_sanity.filename = system_path(option::dump.get(vm)).as_external();
        }

      if (option::log.given(vm))
        {
          ui.redirect_log_to(system_path(option::log.get(vm)));
        }

      if (option::db_name.given(vm))
        {
          app.set_database(system_path(option::db_name.get(vm)));
        }

      if (option::key_dir.given(vm))
        {
          app.set_key_dir(system_path(option::key_dir.get(vm)));
        }

      if (option::conf_dir.given(vm))
        {
          app.set_confdir(system_path(option::conf_dir.get(vm)));
        }

      if (option::ticker.given(vm))
        {
          string ticker = option::ticker.get(vm);
          if (ticker == "none" || global_sanity.quiet)
            ui.set_tick_writer(new tick_write_nothing);
          else if (ticker == "dot")
            ui.set_tick_writer(new tick_write_dot);
          else if (ticker == "count")
            ui.set_tick_writer(new tick_write_count);
          else
            app.requested_help = true;
        }

      if (option::key_name.given(vm))
        {
          app.set_signing_key(option::key_name.get(vm));
        }

      if (option::branch_name.given(vm))
        {
          app.set_branch(option::branch_name.get(vm));
          app.set_is_explicit_option(option::branch_name());
        }

      if (option::version.given(vm))
        {
          print_version();
          return 0;
        }

      if (option::full_version.given(vm))
        {
          print_full_version();
          return 0;
        }

      if (option::revision.given(vm))
        {
          vector<string> revs = option::revision.get(vm);
          for (vector<string>::const_iterator i = revs.begin();
               i != revs.end(); ++i)
            app.add_revision(*i);
        }

      if (option::message.given(vm))
        {
          app.set_message(option::message.get(vm));
          app.set_is_explicit_option(option::message());
        }

      if (option::msgfile.given(vm))
        {
          app.set_message_file(option::msgfile.get(vm));
          app.set_is_explicit_option(option::msgfile());
        }

      if (option::date.given(vm))
        {
          app.set_date(option::date.get(vm));
        }

      if (option::author.given(vm))
        {
          app.set_author(option::author.get(vm));
        }

      if (option::root.given(vm))
        {
          app.set_root(system_path(option::root.get(vm)));
        }

      if (option::last.given(vm))
        {
          app.set_last(option::last.get(vm));
        }

      if (option::next.given(vm))
        {
          app.set_next(option::next.get(vm));
        }

      if (option::depth.given(vm))
        {
          app.set_depth(option::depth.get(vm));
        }

      if (option::brief.given(vm))
        {
          global_sanity.set_brief();
        }

      if (option::diffs.given(vm))
        {
          app.diffs = true;
        }

      if (option::no_merges.given(vm))
        {
          app.no_merges = true;
        }

      if (option::set_default.given(vm))
        {
          app.set_default = true;
        }

      if (option::stdio.given(vm))
        {
          app.bind_stdio = true;
        }

      if (option::no_transport_auth.given(vm))
        {
          app.use_transport_auth = false;
        }

      if (option::exclude.given(vm))
        {
          vector<string> excls = option::exclude.get(vm);
          for (vector<string>::const_iterator i = excls.begin();
               i != excls.end(); ++i)
            app.add_exclude(utf8(*i));
        }

      if (option::pidfile.given(vm))
        {
          app.set_pidfile(system_path(option::pidfile.get(vm)));
        }

      if (option::unified_diff.given(vm))
        {
          app.set_diff_format(unified_diff);
        }

      if (option::context_diff.given(vm))
        {
          app.set_diff_format(context_diff);
        }

      if (option::external_diff.given(vm))
        {
          app.set_diff_format(external_diff);
        }

      if (option::external_diff_args.given(vm))
        {
          app.set_diff_args(utf8(option::external_diff_args.get(vm)));
        }

      if (option::no_show_encloser.given(vm))
        {
          app.diff_show_encloser = false;
        }

      if (option::execute.given(vm))
        {

          app.execute = true;
        }

      if (option::bind.given(vm))
        {
          {
            string arg = option::bind.get(vm);
            string addr_part, port_part;
            size_t l_colon = arg.find(':');
            size_t r_colon = arg.rfind(':');

            // not an ipv6 address, as that would have at least two colons
            if (l_colon == r_colon)
              {
                addr_part = (r_colon == string::npos ? arg : arg.substr(0, r_colon));
                port_part = (r_colon == string::npos ? "" :  arg.substr(r_colon+1, arg.size() - r_colon));
              }
            else
              {
                // IPv6 addresses have a port specified in the style: [2001:388:0:13::]:80
                size_t squareb = arg.rfind(']');
                if ((arg.find('[') == 0) && (squareb != string::npos))
                  {
                    if (squareb < r_colon)
                      port_part = (r_colon == string::npos ? "" :  arg.substr(r_colon+1, arg.size() - r_colon));
                    else
                      port_part = "";
                    addr_part = (squareb == string::npos ? arg.substr(1, arg.size()) : arg.substr(1, squareb-1));
                  }
                else
                  {
                    addr_part = arg;
                    port_part = "";
                  }
              }
            app.bind_stdio = false;
            app.bind_address = utf8(addr_part);
            app.bind_port = utf8(port_part);
          }
          app.set_is_explicit_option(option::bind());
        }

      if (option::missing.given(vm))
        {
          app.missing = true;
        }

      if (option::unknown.given(vm))
        {
          app.unknown = true;
        }

      if (option::key_to_push.given(vm))
        {
          vector<string> kp = option::key_to_push.get(vm);
          for (vector<string>::const_iterator i = kp.begin();
               i != kp.end(); ++i)
            app.add_key_to_push(*i);
        }

      if (option::drop_attr.given(vm))
        {
          vector<string> da = option::drop_attr.get(vm);
          for (vector<string>::const_iterator i = da.begin();
               i != da.end(); ++i)
            app.attrs_to_drop.insert(*i);
        }

      if (option::no_files.given(vm))
        {
          app.no_files = true;
        }

      if (option::recursive.given(vm))
        {
          app.set_recursive();
        }

      if (option::help.given(vm))
        {
          app.requested_help = true;
        }

      if (option::automate_stdio_size.given(vm))
        {
          app.set_automate_stdio_size(option::automate_stdio_size.get(vm));
        }

      // stop here if they asked for help
      if (app.requested_help)
        {
          throw usage(cmd);     // cmd may be empty, and that's fine.
        }

      // at this point we allow a workspace (meaning search for it
      // and if found read _MTN/options, but don't use the data quite
      // yet, and read all the monotonercs).  Processing the data
      // from _MTN/options happens later.
      // Certain commands may subsequently require a workspace or fail
      // if we didn't find one at this point.
      app.allow_workspace();

      if (!app.found_workspace && global_sanity.filename.empty())
        global_sanity.filename = (app.get_confdir() / "dump").as_external();

      // main options processed, now invoke the
      // sub-command w/ remaining args
      if (cmd.empty())
        {
          throw usage("");
        }
      else
        {
          vector<utf8> args(positional_args.begin(), positional_args.end());
          return commands::process(app, cmd, args);
        }
    }
  catch (po::ambiguous_option const & e)
    {
      string msg = (F("%s:\n") % e.what()).str();
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
      if (app.requested_help)
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
  // prepare for arg parsing

  cleanup_ptr<poptContext, void> 
    ctx(poptGetContext(NULL, argc, (char const **) uv.argv, options, 0),
        &my_poptFreeContext);

  set<int> local_options;
  for (poptOption *opt = coptions; opt->val; opt++)
    local_options.insert(opt->val);

  // process main program options

  int opt=-1;
  bool requested_help = false;
  set<int> used_local_options;

  poptSetOtherOptionHelp(ctx(), _("[OPTION...] command [ARGS...]\n"));

  try
    {
      mtncvs_state app;

//      app.set_prog_name(prog_name);

      while ((opt = poptGetNextOpt(ctx())) > 0)
        {
          if (local_options.find(opt) != local_options.end())
            used_local_options.insert(opt);

          switch(opt)
            {
            case MTNCVSOPT_DEBUG:
              global_sanity.set_debug();
              break;
            
            case MTNCVSOPT_FULL:
              app.full=true;
              break;
            
            case MTNCVSOPT_SINCE:
              app.since=string(argstr);
              break;

            case MTNCVSOPT_BRANCH_NAME:
              L(FL("branch %s") % argstr);
              app.branch=string(argstr);
              break;

            case MTNCVSOPT_HELP:
            default:
              requested_help = true;
              break;
            
            case MTNCVSOPT_BINARY:
              app.mtn_binary = utf8(argstr);
              break;
            
            case MTNCVSOPT_DB:
              app.mtn_options.push_back(string("--db=")+argstr);
              break;
            case MTNCVSOPT_RCFILE:
              app.mtn_options.push_back(string("--rcfile=")+argstr);
              break;
            case MTNCVSOPT_NOSTD:
              app.mtn_options.push_back(utf8("--nostd"));
              break;
            case MTNCVSOPT_KEYDIR:
              app.mtn_options.push_back(string("--keydir=")+argstr);
              break;
            case MTNCVSOPT_KEY:
              app.mtn_options.push_back(string("--key=")+argstr);
              break;
            }
        }

      // verify that there are no errors in the command line

      N(opt == -1,
        F("syntax error near the \"%s\" option: %s") %
          poptBadOption(ctx(), POPT_BADOPTION_NOALIAS) % poptStrerror(opt));

      // complete the command if necessary

      string cmd;
      if (poptPeekArg(ctx()))
        {
          cmd = commands::complete_command(poptGetArg(ctx()));
        }

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

      // main options processed, now invoke the 
      // sub-command w/ remaining args

      if (cmd.empty())
        {
          throw usage("");
        }
      else
        {
          // Make sure the local options used are really used by the
          // given command.
          set<int> command_options = commands::command_options(cmd);
          for (set<int>::const_iterator i = used_local_options.begin();
               i != used_local_options.end(); ++i)
            N(command_options.find(*i) != command_options.end(),
              F("monotone %s doesn't use the option %s")
              % cmd % coption_string(*i));

          vector<utf8> args;
          while(poptPeekArg(ctx()))
            {
              args.push_back(utf8(string(poptGetArg(ctx()))));
            }
          ret = commands::process(app.downcast(), cmd, args);
        }
    }
  catch (usage & u)
    {
      // Make sure to hide documentation that's not part of
      // the current command.
      set<int> command_options = commands::command_options(u.which);
      int count = 0;
      for (poptOption *o = coptions; o->val != 0; o++)
        {
          if (command_options.find(o->val) != command_options.end())
            {
              o->argInfo &= ~POPT_ARGFLAG_DOC_HIDDEN;
              L(FL("Removed 'hidden' from option # %d\n") % o->argInfo);
              count++;
            }
          else
            {
              o->argInfo |= POPT_ARGFLAG_DOC_HIDDEN;
              L(FL("Added 'hidden' to option # %d\n") % o->argInfo);
            }
        }
      free((void *)options[0].descrip); options[0].descrip = NULL;
      if (count != 0)
        {
          ostringstream sstr;
          sstr << F("Options specific to '%s %s':") 
            % prog_name % u.which;
          options[0].descrip = strdup(sstr.str().c_str());

          options[0].argInfo |= POPT_ARGFLAG_DOC_HIDDEN;
          L(FL("Added 'hidden' to option # %d\n") % options[0].argInfo);
        }

      poptPrintHelp(ctx(), stdout, 0);
      cout << endl;
      commands::explain_usage(u.which, cout);
      global_sanity.clean_shutdown = true;
      return 2;
    }
  }
  catch (informative_failure & inf)
  {
    ui.inform(inf.what);
    global_sanity.clean_shutdown = true;
    return 1;
  }
  catch (std::ios_base::failure const & ex)
  {
    global_sanity.clean_shutdown = true;
    return 1;
  }

  global_sanity.clean_shutdown = true;
  return ret;
}
#endif

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

