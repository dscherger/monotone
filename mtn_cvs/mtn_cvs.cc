// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

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

char * argstr = NULL;
long arglong = 0;

enum 
{ MTNCVSOPT_BRANCH_NAME, MTNCVSOPT_REVISION, MTNCVSOPT_DEBUG, MTNCVSOPT_HELP, MTNCVSOPT_VERSION,
  MTNCVSOPT_DB_NAME, MTNCVSOPT_MTN_OPTION, MTNCVSOPT_FULL, MTNCVSOPT_SINCE };

// Options are split between two tables.  The first one is command-specific
// options (hence the `c' in `coptions').  The second is the global one
// with options that aren't tied to specific commands.
//
// the intent is to ensure that any command specific options mean the same
// thing to all commands that use them

struct poptOption coptions[] =
  {
    {"branch", 'b', POPT_ARG_STRING, &argstr, MTNCVSOPT_BRANCH_NAME, gettext_noop("select branch cert for operation"), NULL},
    {"revision", 'r', POPT_ARG_STRING, &argstr, MTNCVSOPT_REVISION, gettext_noop("select revision id for operation"), NULL},
    {"since", 0, POPT_ARG_STRING, &argstr, MTNCVSOPT_SINCE, N_("set history start for CVS pull"), NULL},
    {"full", 0, POPT_ARG_NONE, &argstr, MTNCVSOPT_FULL, N_("ignore already pulled CVS revisions"), NULL},
    { NULL, 0, 0, NULL, 0, NULL, NULL }
  };

struct poptOption options[] =
  {
    // Use the coptions table as well.
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, coptions, 0, NULL, NULL },

    {"debug", 0, POPT_ARG_NONE, NULL, MTNCVSOPT_DEBUG, gettext_noop("print debug log to stderr while running"), NULL},
    {"help", 'h', POPT_ARG_NONE, NULL, MTNCVSOPT_HELP, gettext_noop("display help message"), NULL},
    {"version", 0, POPT_ARG_NONE, NULL, MTNCVSOPT_VERSION, gettext_noop("print version number, then exit"), NULL},
//    {"key", 'k', POPT_ARG_STRING, &argstr, MTNCVSOPT_KEY_NAME, gettext_noop("set key for signatures"), NULL},
    {"db", 'd', POPT_ARG_STRING, &argstr, MTNCVSOPT_DB_NAME, gettext_noop("set name of database"), NULL},
    {"mtn-option", 0, POPT_ARG_STRING, &argstr, MTNCVSOPT_MTN_OPTION, gettext_noop("pass option to monotone"), NULL},
//    {"root", 0, POPT_ARG_STRING, &argstr, MTNCVSOPT_ROOT, gettext_noop("limit search for workspace to specified root"), NULL},
    { NULL, 0, 0, NULL, 0, NULL, NULL }
  };

// there are 3 variables which serve as roots for our system.
//
// "global_sanity" is a global object, which contains the error logging
// system, which is constructed once and used by any nana logging actions.
// see cleanup.hh for it
//
// "cmds" is a static table in commands.cc which associates top-level
// commands, given on the command-line, to various version control tasks.
//
// "app_state" is a non-static object type which contains all the
// application state (filesystem, database, network, lua interpreter,
// etc). you can make more than one of these, and feed them to a command in
// the command table.

// our main function is run inside a boost execution monitor. this monitor
// portably sets up handlers for various fatal conditions (signals, win32
// structured exceptions, etc) and provides a uniform reporting interface
// to any exceptions it catches. we augment this with a helper atexit()
// which will also dump our internal logs when an explicit clean shutdown
// flag is not set.
//
// in other words, this program should *never* unexpectedly terminate
// without dumping some diagnostics.

void 
dumper() 
{
  if (!global_sanity.clean_shutdown)
    global_sanity.dump_buffer();
  
//  Botan::Init::deinitialize();
}


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

#if 0
// Read arguments from a file.  The special file '-' means stdin.
// Returned value must be free()'d, after arg parsing has completed.
static void
my_poptStuffArgFile(poptContext con, utf8 const & filename)
{
  utf8 argstr;
  {
    data dat;
    read_data_for_command_line(filename, dat);
    external ext(dat());
    system_to_utf8(ext, argstr);
  }

  const char **argv = 0;
  int argc = 0;
  int rc;

  // Parse the string.  It's OK if there are no arguments.
  rc = poptParseArgvString(argstr().c_str(), &argc, &argv);
  N(rc >= 0 || rc == POPT_ERROR_NOARG,
    F("problem parsing arguments from file %s: %s")
    % filename % poptStrerror(rc));

  if (rc != POPT_ERROR_NOARG)
    {
      // poptStuffArgs does not take an argc argument, but rather requires that
      // the argv array be null-terminated.
      I(argv[argc] == NULL);
      N((rc = poptStuffArgs(con, argv)) >= 0,
        F("weird error when stuffing arguments read from %s: %s\n")
        % filename % poptStrerror(rc));
    }

  free(argv);
}
#endif

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
  mtncvs_state &myapp=static_cast<mtncvs_state&>(app);
  N(!myapp.branch_name().empty(), F("no destination branch specified\n"));
      
//  cvs_sync::pull(repository,module,branch,app);
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
//  cvs_sync::push(repository,module,branch,app);
}

CMD(takeover, N_("working copy"), N_("[CVS-MODULE]"), 
      N_("put a CVS working directory under monotone's control"), 
      MTNCVSOPT_BRANCH_NAME)
{
  if (args.size() > 1) throw usage(name);
  string module;
  if (args.size() == 1) module = idx(args, 0)();
  N(!app.branch_name().empty(), F("no destination branch specified\n"));
//  cvs_sync::takeover(app, module);
}

int 
cpp_main(int argc, char ** argv)
{
  int ret = 0;

  atexit(&dumper);

  // go-go gadget i18n

  setlocale(LC_ALL, "");
//  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);


  // we want to catch any early informative_failures due to charset
  // conversion etc
  try
  {

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
  utf8_argv uv(argc, argv);

  // find base name of executable

  string prog_path = fs::path(uv.argv[0]).leaf();
  prog_path = prog_path.substr(0, prog_path.find(".exe", 0));
  utf8 prog_name(prog_path);

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

            case MTNCVSOPT_HELP:
            default:
              requested_help = true;
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
          ret = commands::process(app, cmd, args);
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

