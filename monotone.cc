// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "config.h"

#include "popt/popt.h"
#include <cstdio>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include <iterator>
#include <iostream>
#include <fstream>
#include <sstream>
#include <locale.h>

#include <stdlib.h>

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/path.hpp>

#include "botan/botan.h"

#include "i18n.h"

#include "app_state.hh"
#include "commands.hh"
#include "sanity.hh"
#include "cleanup.hh"
#include "file_io.hh"
#include "charset.hh"
#include "ui.hh"
#include "mt_version.hh"
#include "options.hh"
#include "paths.hh"

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
    {"branch", 'b', POPT_ARG_STRING, &argstr, OPT_BRANCH_NAME, gettext_noop("select branch cert for operation"), NULL},
    {"revision", 'r', POPT_ARG_STRING, &argstr, OPT_REVISION, gettext_noop("select revision id for operation"), NULL},
    {"message", 'm', POPT_ARG_STRING, &argstr, OPT_MESSAGE, gettext_noop("set commit changelog message"), NULL},
    {"message-file", 0, POPT_ARG_STRING, &argstr, OPT_MSGFILE, gettext_noop("set filename containing commit changelog message"), NULL},
    {"date", 0, POPT_ARG_STRING, &argstr, OPT_DATE, gettext_noop("override date/time for commit"), NULL},
    {"author", 0, POPT_ARG_STRING, &argstr, OPT_AUTHOR, gettext_noop("override author for commit"), NULL},
    {"depth", 0, POPT_ARG_LONG, &arglong, OPT_DEPTH, gettext_noop("limit the number of levels of directories to descend"), NULL},
    {"last", 0, POPT_ARG_LONG, &arglong, OPT_LAST, gettext_noop("limit log output to the last number of entries"), NULL},
    {"next", 0, POPT_ARG_LONG, &arglong, OPT_NEXT, gettext_noop("limit log output to the next number of entries"), NULL},
    {"pid-file", 0, POPT_ARG_STRING, &argstr, OPT_PIDFILE, gettext_noop("record process id of server"), NULL},
    {"brief", 0, POPT_ARG_NONE, NULL, OPT_BRIEF, gettext_noop("print a brief version of the normal output"), NULL},
    {"diffs", 0, POPT_ARG_NONE, NULL, OPT_DIFFS, gettext_noop("print diffs along with logs"), NULL},
    {"no-merges", 0, POPT_ARG_NONE, NULL, OPT_NO_MERGES, gettext_noop("exclude merges when printing logs"), NULL},
    {"set-default", 0, POPT_ARG_NONE, NULL, OPT_SET_DEFAULT, gettext_noop("use the current arguments as the future default"), NULL},
    {"exclude", 0, POPT_ARG_STRING, &argstr, OPT_EXCLUDE, gettext_noop("leave out anything described by its argument"), NULL},
    {"unified", 0, POPT_ARG_NONE, NULL, OPT_UNIFIED_DIFF, gettext_noop("use unified diff format"), NULL},
    {"context", 0, POPT_ARG_NONE, NULL, OPT_CONTEXT_DIFF, gettext_noop("use context diff format"), NULL},
    {"external", 0, POPT_ARG_NONE, NULL, OPT_EXTERNAL_DIFF, gettext_noop("use external diff hook for generating diffs"), NULL},
    {"diff-args", 0, POPT_ARG_STRING, &argstr, OPT_EXTERNAL_DIFF_ARGS, gettext_noop("argument to pass external diff hook"), NULL},
    {"execute", 'e', POPT_ARG_NONE, NULL, OPT_EXECUTE, gettext_noop("perform the associated file operation"), NULL},
    {"bind", 0, POPT_ARG_STRING, &argstr, OPT_BIND, gettext_noop("address:port to listen on (default :4691)"), NULL},
    {"missing", 0, POPT_ARG_NONE, NULL, OPT_MISSING, gettext_noop("perform the operations for files missing from workspace"), NULL},
    {"unknown", 0, POPT_ARG_NONE, NULL, OPT_UNKNOWN, gettext_noop("perform the operations for unknown files from workspace"), NULL},
    {"key-to-push", 0, POPT_ARG_STRING, &argstr, OPT_KEY_TO_PUSH, gettext_noop("push the specified key even if it hasn't signed anything"), NULL},
    {"stdio", 0, POPT_ARG_NONE, NULL, OPT_STDIO, gettext_noop("serve netsync on stdio"), NULL},
    {"no-transport-auth", 0, POPT_ARG_NONE, NULL, OPT_NO_TRANSPORT_AUTH, gettext_noop("disable transport authentication"), NULL},
    {"drop-attr", 0, POPT_ARG_STRING, &argstr, OPT_DROP_ATTR, gettext_noop("when rosterifying, drop attrs entries with the given key"), NULL},
    {"no-files", 0, POPT_ARG_NONE, NULL, OPT_NO_FILES, gettext_noop("exclude files when printing logs"), NULL},
    {"recursive", 'R', POPT_ARG_NONE, NULL, OPT_RECURSIVE, gettext_noop("also operate on the contents of any listed directories"), NULL},
    { NULL, 0, 0, NULL, 0, NULL, NULL }
  };

struct poptOption options[] =
  {
    // Use the coptions table as well.
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, coptions, 0, NULL, NULL },

    {"debug", 0, POPT_ARG_NONE, NULL, OPT_DEBUG, gettext_noop("print debug log to stderr while running"), NULL},
    {"dump", 0, POPT_ARG_STRING, &argstr, OPT_DUMP, gettext_noop("file to dump debugging log to, on failure"), NULL},
    {"log", 0, POPT_ARG_STRING, &argstr, OPT_LOG, gettext_noop("file to write the log to"), NULL},
    {"quiet", 0, POPT_ARG_NONE, NULL, OPT_QUIET, gettext_noop("suppress verbose, informational and progress messages"), NULL},
    {"reallyquiet", 0, POPT_ARG_NONE, NULL, OPT_REALLYQUIET, gettext_noop("suppress warning, verbose, informational and progress messages"), NULL},
    {"help", 'h', POPT_ARG_NONE, NULL, OPT_HELP, gettext_noop("display help message"), NULL},
    {"version", 0, POPT_ARG_NONE, NULL, OPT_VERSION, gettext_noop("print version number, then exit"), NULL},
    {"full-version", 0, POPT_ARG_NONE, NULL, OPT_FULL_VERSION, gettext_noop("print detailed version number, then exit"), NULL},
    {"xargs", '@', POPT_ARG_STRING, &argstr, OPT_ARGFILE, gettext_noop("insert command line arguments taken from the given file"), NULL},
    {"ticker", 0, POPT_ARG_STRING, &argstr, OPT_TICKER, gettext_noop("set ticker style (count|dot|none)"), NULL},
    {"nostd", 0, POPT_ARG_NONE, NULL, OPT_NOSTD, gettext_noop("do not load standard lua hooks"), NULL},
    {"norc", 0, POPT_ARG_NONE, NULL, OPT_NORC, gettext_noop("do not load ~/.monotone/monotonerc or _MTN/monotonerc lua files"), NULL},
    {"rcfile", 0, POPT_ARG_STRING, &argstr, OPT_RCFILE, gettext_noop("load extra rc file"), NULL},
    {"key", 'k', POPT_ARG_STRING, &argstr, OPT_KEY_NAME, gettext_noop("set key for signatures"), NULL},
    {"db", 'd', POPT_ARG_STRING, &argstr, OPT_DB_NAME, gettext_noop("set name of database"), NULL},
    {"root", 0, POPT_ARG_STRING, &argstr, OPT_ROOT, gettext_noop("limit search for workspace to specified root"), NULL},
    {"verbose", 0, POPT_ARG_NONE, NULL, OPT_VERBOSE, gettext_noop("verbose completion output"), NULL},
    {"keydir", 0, POPT_ARG_STRING, &argstr, OPT_KEY_DIR, gettext_noop("set location of key store"), NULL},
    {"confdir", 0, POPT_ARG_STRING, &argstr, OPT_CONF_DIR, gettext_noop("set location of configuration directory"), NULL},
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
  
  Botan::Init::deinitialize();
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
  int ret = 0;

  atexit(&dumper);

  // go-go gadget i18n

  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
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
  get_full_version(full_version_string);
  MM(full_version_string);

  // Set up secure memory allocation etc
  Botan::Init::initialize();
  Botan::set_default_allocator("malloc");
  
  // decode all argv values into a UTF-8 array

  save_initial_path();
  utf8_argv uv(argc, argv);

  // find base name of executable

  string prog_path = fs::path(uv.argv[0]).leaf();
  if (prog_path.rfind(".exe") == prog_path.size() - 4)
    prog_path = prog_path.substr(0, prog_path.size() - 4);
  utf8 prog_name(prog_path);

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

  poptSetOtherOptionHelp(ctx(), _("[OPTION...] command [ARGS...]\n"));

  try
    {
      app_state app;

      app.set_prog_name(prog_name);

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
              ui.set_tick_writer(new tick_write_nothing);
              break;

            case OPT_REALLYQUIET:
              global_sanity.set_reallyquiet();
              ui.set_tick_writer(new tick_write_nothing);
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
              app.add_rcfile(string(argstr));
              break;

            case OPT_DUMP:
              global_sanity.filename = system_path(argstr);
              break;

            case OPT_LOG:
              ui.redirect_log_to(system_path(argstr));
              break;

            case OPT_DB_NAME:
              app.set_database(system_path(argstr));
              break;

            case OPT_KEY_DIR:
              app.set_key_dir(system_path(argstr));
              break;

            case OPT_CONF_DIR:
              app.set_confdir(system_path(argstr));
              break;

            case OPT_TICKER:
              if (string(argstr) == "none" || global_sanity.quiet)
                ui.set_tick_writer(new tick_write_nothing);
              else if (string(argstr) == "dot")
                ui.set_tick_writer(new tick_write_dot);
              else if (string(argstr) == "count")
                ui.set_tick_writer(new tick_write_count);
              else
                requested_help = true;
              break;

            case OPT_KEY_NAME:
              app.set_signing_key(string(argstr));
              break;

            case OPT_BRANCH_NAME:
              app.set_branch(string(argstr));
              app.set_is_explicit_option(OPT_BRANCH_NAME);
              break;

            case OPT_VERSION:
              print_version();
              global_sanity.clean_shutdown = true;
              return 0;

            case OPT_FULL_VERSION:
              print_full_version();
              global_sanity.clean_shutdown = true;
              return 0;

            case OPT_REVISION:
              app.add_revision(string(argstr));
              break;

            case OPT_MESSAGE:
              app.set_message(string(argstr));
              app.set_is_explicit_option(OPT_MESSAGE);
              break;

            case OPT_MSGFILE:
              app.set_message_file(string(argstr));
              app.set_is_explicit_option(OPT_MSGFILE);
              break;

            case OPT_DATE:
              app.set_date(string(argstr));
              break;

            case OPT_AUTHOR:
              app.set_author(string(argstr));
              break;

            case OPT_ROOT:
              app.set_root(system_path(argstr));
              break;

            case OPT_LAST:
              app.set_last(arglong);
              break;

            case OPT_NEXT:
              app.set_next(arglong);
              break;

            case OPT_DEPTH:
              app.set_depth(arglong);
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

            case OPT_SET_DEFAULT:
              app.set_default = true;
              break;

            case OPT_EXCLUDE:
              app.add_exclude(utf8(string(argstr)));
              break;

            case OPT_PIDFILE:
              app.set_pidfile(system_path(argstr));
              break;

            case OPT_ARGFILE:
              my_poptStuffArgFile(ctx(), utf8(string(argstr)));
              break;

            case OPT_UNIFIED_DIFF:
              app.set_diff_format(unified_diff);
              break;

            case OPT_CONTEXT_DIFF:
              app.set_diff_format(context_diff);
              break;

            case OPT_EXTERNAL_DIFF:
              app.set_diff_format(external_diff);
              break;
              
            case OPT_EXTERNAL_DIFF_ARGS:
              app.set_diff_args(utf8(string(argstr)));
              break;

            case OPT_EXECUTE:
              app.execute = true;
              break;

            case OPT_STDIO:
              app.bind_stdio = true;
              break;

            case OPT_NO_TRANSPORT_AUTH:
              app.use_transport_auth = false;
              break;

            case OPT_BIND:
              {
                std::string arg(argstr);
                std::string addr_part, port_part;
                size_t l_colon = arg.find(':');
                size_t r_colon = arg.rfind(':');
                
                // not an ipv6 address, as that would have at least two colons
                if (l_colon == r_colon)
                  {
                    addr_part = (r_colon == std::string::npos ? arg : arg.substr(0, r_colon));
                    port_part = (r_colon == std::string::npos ? "" :  arg.substr(r_colon+1, arg.size() - r_colon));
                  }
                else
                  { 
                    // IPv6 addresses have a port specified in the style: [2001:388:0:13::]:80
                    size_t squareb = arg.rfind(']');
                    if ((arg.find('[') == 0) && (squareb != std::string::npos))
                      {
                        if (squareb < r_colon)
                          port_part = (r_colon == std::string::npos ? "" :  arg.substr(r_colon+1, arg.size() - r_colon));
                        else
                          port_part = "";
                        addr_part = (squareb == std::string::npos ? arg.substr(1, arg.size()) : arg.substr(1, squareb-1));
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
              app.set_is_explicit_option(OPT_BIND);
              break;

            case OPT_MISSING:
              app.missing = true;
              break;

            case OPT_UNKNOWN:
              app.unknown = true;
              break;

            case OPT_KEY_TO_PUSH:
              {
                app.add_key_to_push(string(argstr));
              }
              break;

            case OPT_DROP_ATTR:
              app.attrs_to_drop.insert(string(argstr));
              break;

            case OPT_NO_FILES:
              app.no_files = true;
              break;

            case OPT_RECURSIVE:
              app.set_recursive();
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

      // at this point we allow a workspace (meaning search for it
      // and if found read _MTN/options, but don't use the data quite
      // yet, and read all the monotonercs).  Processing the data
      // from _MTN/options happens later.
      // Certain commands may subsequently require a workspace or fail
      // if we didn't find one at this point.

      app.allow_workspace();

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
