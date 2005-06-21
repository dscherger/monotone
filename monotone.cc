// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <config.h>

#include "popt/popt.h"
#include <cstdio>
#include <iterator>
#include <iostream>
#include <fstream>
#include <sstream>

#include <stdlib.h>
#ifdef WIN32
#include <libintl.h>
#endif

#include "app_state.hh"
#include "commands.hh"
#include "sanity.hh"
#include "cleanup.hh"
#include "file_io.hh"
#include "transforms.hh"
#include "ui.hh"
#include "mt_version.hh"
#include "options.hh"

// main option processing and exception handling code

using namespace std;

char * argstr = NULL;
long arglong = 0;

// Options are split between two tables.  The first one is command-specific
// options (hence the `c' in `coptions').  The second is the global one
// with options that aren't tied to specific commands.
//
// the intent is to ensure that any command specific options mean the same
// thing to all commands that use them

struct poptOption coptions[] =
  {
    {"branch", 'b', POPT_ARG_STRING, &argstr, OPT_BRANCH_NAME, "select branch cert for operation", NULL},
    {"revision", 'r', POPT_ARG_STRING, &argstr, OPT_REVISION, "select revision id for operation", NULL},
    {"message", 'm', POPT_ARG_STRING, &argstr, OPT_MESSAGE, "set commit changelog message", NULL},
    {"message-file", 0, POPT_ARG_STRING, &argstr, OPT_MSGFILE, "set filename containing commit changelog message", NULL},
    {"date", 0, POPT_ARG_STRING, &argstr, OPT_DATE, "override date/time for commit", NULL},
    {"author", 0, POPT_ARG_STRING, &argstr, OPT_AUTHOR, "override author for commit", NULL},
    {"depth", 0, POPT_ARG_LONG, &arglong, OPT_DEPTH, "limit the number of levels of directories to descend", NULL},
    {"last", 0, POPT_ARG_LONG, &arglong, OPT_LAST, "limit the log output to the given number of entries", NULL},
    {"pid-file", 0, POPT_ARG_STRING, &argstr, OPT_PIDFILE, "record process id of server", NULL},
    {"since", 0, POPT_ARG_STRING, &argstr, OPT_SINCE, "set history start for CVS sync", NULL},
    {"brief", 0, POPT_ARG_NONE, NULL, OPT_BRIEF, "print a brief version of the normal output", NULL},
    {"diffs", 0, POPT_ARG_NONE, NULL, OPT_DIFFS, "print diffs along with logs", NULL},
    {"no-merges", 0, POPT_ARG_NONE, NULL, OPT_NO_MERGES, "skip merges when printing logs", NULL},
    { NULL, 0, 0, NULL, 0, NULL, NULL }
  };

struct poptOption options[] =
  {
    // Use the coptions table as well.
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, coptions, 0, NULL, NULL },

    {"debug", 0, POPT_ARG_NONE, NULL, OPT_DEBUG, "print debug log to stderr while running", NULL},
    {"dump", 0, POPT_ARG_STRING, &argstr, OPT_DUMP, "file to dump debugging log to, on failure", NULL},
    {"quiet", 0, POPT_ARG_NONE, NULL, OPT_QUIET, "suppress log and progress messages", NULL},
    {"help", 0, POPT_ARG_NONE, NULL, OPT_HELP, "display help message", NULL},
    {"version", 0, POPT_ARG_NONE, NULL, OPT_VERSION, "print version number, then exit", NULL},
    {"full-version", 0, POPT_ARG_NONE, NULL, OPT_FULL_VERSION, "print detailed version number, then exit", NULL},
    {"xargs", '@', POPT_ARG_STRING, &argstr, OPT_ARGFILE, "insert command line arguments taken from the given file", NULL},
    {"ticker", 0, POPT_ARG_STRING, &argstr, OPT_TICKER, "set ticker style (count|dot|none) [count]", NULL},
    {"nostd", 0, POPT_ARG_NONE, NULL, OPT_NOSTD, "do not load standard lua hooks", NULL},
    {"norc", 0, POPT_ARG_NONE, NULL, OPT_NORC, "do not load ~/.monotone/monotonerc or MT/monotonerc lua files", NULL},
    {"rcfile", 0, POPT_ARG_STRING, &argstr, OPT_RCFILE, "load extra rc file", NULL},
    {"key", 'k', POPT_ARG_STRING, &argstr, OPT_KEY_NAME, "set key for signatures", NULL},
    {"db", 'd', POPT_ARG_STRING, &argstr, OPT_DB_NAME, "set name of database", NULL},
    {"root", 0, POPT_ARG_STRING, &argstr, OPT_ROOT, "limit search for working copy to specified root", NULL},
    {"verbose", 0, POPT_ARG_NONE, NULL, OPT_VERBOSE, "verbose completion output", NULL},
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

static bool clean_shutdown;

void 
dumper() 
{
  if (!clean_shutdown)
    global_sanity.dump_buffer();
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

int 
cpp_main(int argc, char ** argv)
{

  clean_shutdown = false;
  int ret = 0;

  atexit(&dumper);

  // go-go gadget i18n

  setlocale(LC_CTYPE, "");
  setlocale(LC_MESSAGES, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);


  // we want to catch any early informative_failures due to charset
  // conversion etc
  try
  {

  {
    std::ostringstream cmdline_ss;
    for (int i = 0; i < argc; ++i)
      {
        if (i)
          cmdline_ss << ", ";
        cmdline_ss << "'" << argv[i] << "'";
      }
    L(F("command line: %s\n") % cmdline_ss.str());
  }

  L(F("set locale: LC_CTYPE=%s, LC_MESSAGES=%s\n")
    % (setlocale(LC_CTYPE, NULL) == NULL ? "n/a" : setlocale(LC_CTYPE, NULL))
    % (setlocale(LC_MESSAGES, NULL) == NULL ? "n/a" : setlocale(LC_CTYPE, NULL)));

  // decode all argv values into a UTF-8 array

  save_initial_path();
  utf8_argv uv(argc, argv);

  // prepare for arg parsing

  cleanup_ptr<poptContext, void> 
    ctx(poptGetContext(NULL, argc, (char const **) uv.argv, options, 0),
        &my_poptFreeContext);

  set<int> local_options;
  for (poptOption *opt = coptions; opt->val; opt++)
    local_options.insert(opt->val);

  // process main program options

  int opt;
  bool requested_help = false;
  set<int> used_local_options;

  poptSetOtherOptionHelp(ctx(), "[OPTION...] command [ARGS...]\n");

  try
    {
      app_state app;

      while ((opt = poptGetNextOpt(ctx())) > 0)
        {
          if (local_options.find(opt) != local_options.end())
            used_local_options.insert(opt);

          switch(opt)
            {
            case OPT_DEBUG:
              global_sanity.set_debug();
              break;

            case OPT_QUIET:
              global_sanity.set_quiet();
              break;

            case OPT_NOSTD:
              app.set_stdhooks(false);
              break;

            case OPT_NORC:
              app.set_rcfiles(false);
              break;

            case OPT_VERBOSE:
              app.set_verbose(true);
              break;

            case OPT_RCFILE:
              app.add_rcfile(absolutify_for_command_line(tilde_expand(string(argstr))));
              break;

            case OPT_DUMP:
              global_sanity.filename = absolutify(tilde_expand(string(argstr)));
              break;

            case OPT_DB_NAME:
              app.set_database(absolutify(tilde_expand(string(argstr))));
              break;

            case OPT_TICKER:
              if (string(argstr) == "dot")
                ui.set_tick_writer(new tick_write_dot);
              else if (string(argstr) == "count")
                ui.set_tick_writer(new tick_write_count);
              else if (string(argstr) == "none")
                ui.set_tick_writer(new tick_write_nothing);
              else
                requested_help = true;
              break;

            case OPT_KEY_NAME:
              app.set_signing_key(string(argstr));
              break;

            case OPT_BRANCH_NAME:
              app.set_branch(string(argstr));
              break;

            case OPT_VERSION:
              print_version();
              clean_shutdown = true;
              return 0;

            case OPT_FULL_VERSION:
              print_full_version();
              clean_shutdown = true;
              return 0;

            case OPT_REVISION:
              app.add_revision(string(argstr));
              break;

            case OPT_MESSAGE:
              app.set_message(string(argstr));
              break;

            case OPT_MSGFILE:
              app.set_message_file(absolutify_for_command_line(tilde_expand(string(argstr))));
              break;

            case OPT_DATE:
              app.set_date(string(argstr));
              break;

            case OPT_AUTHOR:
              app.set_author(string(argstr));
              break;

            case OPT_ROOT:
              app.set_root(string(argstr));
              break;

            case OPT_LAST:
              app.set_last(arglong);
              break;

            case OPT_DEPTH:
              app.set_depth(arglong);
              break;

            case OPT_SINCE:
              app.set_since(string(argstr));
              break;

            case OPT_BRIEF:
              global_sanity.set_brief();
              break;

            case OPT_DIFFS:
              app.diffs = true;
              break;

            case OPT_NO_MERGES:
              app.no_merges = true;
              break;

            case OPT_PIDFILE:
              app.set_pidfile(absolutify(tilde_expand(string(argstr))));
              break;

            case OPT_ARGFILE:
              my_poptStuffArgFile(ctx(), utf8(string(argstr)));
              break;

            case OPT_HELP:
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

      // at this point we allow a working copy (meaning search for it
      // and if found read MT/options) but don't require it. certain
      // commands may subsequently require a working copy or fail

      app.allow_working_copy();

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
              L(F("Removed 'hidden' from option # %d\n") % o->argInfo);
              count++;
            }
          else
            {
              o->argInfo |= POPT_ARGFLAG_DOC_HIDDEN;
              L(F("Added 'hidden' to option # %d\n") % o->argInfo);
            }
        }
      free((void *)options[0].descrip); options[0].descrip = NULL;
      if (count != 0)
        {
          ostringstream sstr;
          sstr << "Options specific to 'monotone " << u.which << "':";
          options[0].descrip = strdup(sstr.str().c_str());

          options[0].argInfo |= POPT_ARGFLAG_DOC_HIDDEN;
          L(F("Added 'hidden' to option # %d\n") % options[0].argInfo);
        }

      poptPrintHelp(ctx(), stdout, 0);
      cout << endl;
      commands::explain_usage(u.which, cout);
      clean_shutdown = true;
      return 0;
    }
  }
  catch (informative_failure & inf)
  {
    ui.inform(inf.what);
    clean_shutdown = true;
    return 1;
  }

  clean_shutdown = true;
  return ret;
}
