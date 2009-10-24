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
//#include <i18n.h>
#include <sanity.hh>
#include <charset.hh>
#include <sstream>
#include <cleanup.hh>
//#include <boost/filesystem/path.hpp>
#include "../commands.hh"
#include <iostream>
#include "../ui.hh"
#include <botan/init.h>
#include <botan/allocate.h>
#include "../cmd.hh"
#include "mtncvs_state.hh"
#include "cvs_sync.hh"
#include "../constants.hh"
#include "../database.hh"
#include "../mt_version.hh"
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
/*struct ui_library
{
  ui_library() { ui.initialize(); }
  ~ui_library() { ui.deinitialize(); }
};*/

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
  E(!myapp.opts.branchname().empty(), origin::user, F("no destination branch specified\n"));
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

using std::cout;
using std::cerr;
using std::endl;
using std::ostringstream;
using std::string;

void
get_version(string & out)
{
  out = (F("%s (base revision: %s)")
         % PACKAGE_STRING % string(package_full_revision_constant)).str();
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
{ out="mtn_cvs version 0.1 ("+std::string(package_full_revision_constant)+")";
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

/*string
get_usage_str(options::options_type const & optset, options & opts)
{
  vector<string> names;
  vector<string> descriptions;
  unsigned int maxnamelen;

  optset.instantiate(&opts).get_usage_strings(names, descriptions, maxnamelen);
  return format_usage_strings(names, descriptions, maxnamelen);
}*/

void
mtn_terminate_handler()
{
  ui.fatal(F("std::terminate() - exception thrown while handling another exception"));
  exit(3);
}

int
cpp_main(int argc, char ** argv)
{
  int ret = 0;

//  atexit(&dumper);

  // go-go gadget i18n
  setlocale(LC_ALL, "");
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

      // check the botan library version we got linked against.
      u32 linked_botan_version = BOTAN_VERSION_CODE_FOR(
        Botan::version_major(), Botan::version_minor(),
        Botan::version_patch());

      // Botan 1.7.14 has an incompatible API change, which got reverted
      // again in 1.7.15. Thus we do not care to support 1.7.14.
      E(linked_botan_version != BOTAN_VERSION_CODE_FOR(1,7,14), origin::system,
        F("Monotone does not support Botan 1.7.14."));

#if BOTAN_VERSION_CODE <= BOTAN_VERSION_CODE_FOR(1,7,6)
      E(linked_botan_version >= BOTAN_VERSION_CODE_FOR(1,6,3), origin::system,
        F("This monotone binary requires Botan 1.6.3 or newer."));
      E(linked_botan_version <= BOTAN_VERSION_CODE_FOR(1,7,6), origin::system,
        F("This monotone binary does not work with Botan newer than 1.7.6."));
#elif BOTAN_VERSION_CODE <= BOTAN_VERSION_CODE_FOR(1,7,22)
      E(linked_botan_version > BOTAN_VERSION_CODE_FOR(1,7,6), origin::system,
        F("This monotone binary requires Botan 1.7.7 or newer."));
      // While compiling against 1.7.22 or newer is recommended, because
      // it enables new features of Botan, the monotone binary compiled
      // against Botan 1.7.21 and before should still work with newer Botan
      // versions, including all of the stable branch 1.8.x.
      E(linked_botan_version < BOTAN_VERSION_CODE_FOR(1,9,0), origin::system,
        F("This monotone binary does not work with Botan 1.9.x."));
#else
      E(linked_botan_version > BOTAN_VERSION_CODE_FOR(1,7,22), origin::system,
        F("This monotone binary requires Botan 1.7.22 or newer."));
      E(linked_botan_version < BOTAN_VERSION_CODE_FOR(1,9,0), origin::system,
        F("This monotone binary does not work with Botan 1.9.x."));
#endif

#if 0
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
#endif

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
  catch (usage & u)
    {
          ui.inform_usage(u, app.opts);
          return app.opts.help ? 0 : 2;
#if 0
      // we send --help output to stdout, so that "mtn --help | less" works
      // but we send error-triggered usage information to stderr, so that if
      // you screw up in a script, you don't just get usage information sent
      // merrily down your pipes.
      std::ostream & usage_stream = (app.opts.help ? cout : cerr);

          string visibleid;
          if (!u.which.empty())
            visibleid = join_words(vector< utf8 >(u.which.begin() + 1,
                                                  u.which.end()))();

      usage_stream << F("Usage: %s [OPTION...] command [ARG...]") % prog_name << "\n\n";
      usage_stream << get_usage_str(options::opts::globals(), app.opts);

      // Make sure to hide documentation that's not part of
      // the current command.
      options::options_type cmd_options = commands::command_options(u.which);
      if (!cmd_options.empty())
        {
          usage_stream << F("Options specific to '%s %s':") % prog_name % visibleid << "\n\n";
          usage_stream << get_usage_str(cmd_options, app.opts);
        }

      commands::explain_usage(u.which, /*app.opts.show_hidden_commands*/ true, usage_stream);
      if (app.opts.help)
        return 0;
      else
        return 2;
#endif
    }
  }
  catch (option::option_error const & e)
    {
      E(false, origin::user, i18n_format("%s") % e.what());
    }
  catch (recoverable_failure & inf)
    {
      ui.inform(inf.what());
      return 1;
    }
  catch (unrecoverable_failure & inf)
    {
      if (inf.caused_by() == origin::database)
        ui.fatal_db(inf.what());
      else
        ui.fatal(inf.what());
      return 3;
    }
  catch (ios_base::failure const & ex)
    {
      // an error has already been printed
      return 1;
    }
  catch (std::bad_alloc)
    {
      ui.inform(_("error: memory exhausted"));
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

//#include "../work.hh"

using std::ostream;

// taken from cmds.cc
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
/*    if (cmd->use_workspace_options())
      {
        workspace::check_format();
        workspace::get_options(app.opts);
      }*/

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

    sort(sorted.begin(), sorted.end(), std::greater< command const * >());

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
}

