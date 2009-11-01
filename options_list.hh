// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//               2008-2009 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

/*
 * This is a list of all options that monotone can take, what variables
 * they get put into, and how they get there. There are 4 important macros
 * available here (and only here):
 *
 *   OPTSET(name)
 *     Defines a set of related options, which can easily be allowed for
 *     a particular command or reset together. It is named
 *     'options::opts::name'.
 *
 *   OPTSET_REL(parent, child)
 *     Declare a relationship between two optsets, so that if the parent
 *     is reset or allowed for a command the child will also be.
 *
 *   OPTVAR(optset, type, name, default)
 *     Defines a variable 'type options::name' which is initialized to
 *     'type (default)' and belongs to the named optset. When the optset
 *     is reset, this variable will be reset to 'type (default)'.
 *
 *   OPTION(optset, name, hasarg, optstring, description)
 *     Declare an option named 'options::opts::name', which belongs to the
 *     given optset. 'optstring' can look like "foo", in which case it is
 *     specified as "--foo", or it can look like "foo,f", in which case it
 *     is specified as either "--foo" or "-f". The description is a
 *     translatable help text. 'hasarg' is a bool indicating whether this
 *     option takes an argument.
 *
 *     Some expansions of this macro expect a function body, which looks
 *     like 'void (std::string const & arg)'. This is the 'setter' function
 *     for this option, and is called when the option is parsed. If the
 *     option was declared to not take an argument, 'arg' is empty.
 *     Otherwise, it is the given argument. In any case this function
 *     should set the option variables for this option and throw a
 *     'bad_arg_internal' if this fails. The variables that are set must be
 *     part of the same optset as the option, or they won't get reset
 *     properly. When the function body is needed, 'option_bodies' will
 *     be defined.
 */

// This is a shortcut for an option which has its own variable and optset.
// It will take an argument unless 'type' is 'bool'.
#define OPT(name, string, type, default_, description)               \
  OPTVAR(name, type, name, default_)                                    \
  OPTION(name, name, has_arg<type >(), string, description)

// This is the same, except that the option and variable belong to the
// 'globals' optset. These are global options, not specific to a particular
// command.
#define GOPT(name, string, type, default_, description)                 \
  OPTVAR(globals, type, name, default_)                                 \
  OPTION(globals, name, has_arg<type >(), string, description)

// because 'default_' is constructor arguments, and may need to be a list
// This doesn't work if fed through the OPT / GOPT shorthand versions
#define COMMA ,

OPTSET(globals)

OPTVAR(globals, args_vector, args, )
OPTION(globals, positionals, true, "--", "")
#ifdef option_bodies
{
  args.push_back(arg_type(arg, origin::user));
}
#endif

typedef std::map<branch_name, hexenc<id> > policy_revision_arg_map;
GOPT(policy_revisions, "policy-revision", policy_revision_arg_map, ,
     gettext_noop("prefix@REVISION_ID, use a specific policy revision"))
#ifdef option_bodies
{
  size_t at = arg.find('@');
  if (at == std::string::npos)
    throw bad_arg_internal(F("no '@' found").str());
  branch_name bn(arg.substr(0, at), origin::user);
  hexenc<id> rid(arg.substr(at+1), origin::user);
  policy_revisions.insert(std::make_pair(bn, rid));
}
#endif

OPT(author, "author", utf8, , gettext_noop("override author for commit"))
#ifdef option_bodies
{
  author = utf8(arg, origin::user);
}
#endif

OPT(automate_stdio_size, "automate-stdio-size", size_t, 32768,
     gettext_noop("block size in bytes for \"automate stdio\" output"))
#ifdef option_bodies
{
  automate_stdio_size = boost::lexical_cast<long>(arg);
  if (automate_stdio_size <= 0)
    throw bad_arg_internal(F("cannot be zero or negative").str());
}
#endif

OPTSET(bind_opts)
OPTVAR(bind_opts, std::list<utf8>, bind_uris, )
OPTVAR(bind_opts, bool, bind_stdio, false)
OPTVAR(bind_opts, bool, use_transport_auth, true)

OPTION(bind_opts, bind, true, "bind",
       gettext_noop("address:port to listen on (default :4691)"))
#ifdef option_bodies
{
  bind_uris.push_back(utf8(arg, origin::user));
  bind_stdio = false;
}
#endif
OPTION(bind_opts, no_transport_auth, false, "no-transport-auth",
       gettext_noop("disable transport authentication"))
#ifdef option_bodies
{
  use_transport_auth = false;
}
#endif
OPTION(bind_opts, bind_stdio, false, "stdio",
       gettext_noop("serve netsync on stdio"))
#ifdef option_bodies
{
  bind_stdio = true;
}
#endif

OPT(max_netsync_version, "max-netsync-version",
    u8, constants::netcmd_current_protocol_version,
    gettext_noop("cause monotone to lie about the maximum netsync "
                 "protocol version that it supports, mostly for debugging"))
#ifdef option_bodies
{
  max_netsync_version = (u8)boost::lexical_cast<u32>(arg);
}
#endif

OPT(min_netsync_version, "min-netsync-version",
    u8, constants::netcmd_minimum_protocol_version,
    gettext_noop("cause monotone to lie about the minimum netsync "
                 "protocol version it supports, useful for debugging or "
                 "if you want to prevent use of older protocol versions"))
#ifdef option_bodies
{
  min_netsync_version = (u8)boost::lexical_cast<u32>(arg);
}
#endif

OPT(remote_stdio_host, "remote-stdio-host",
    utf8, ,
    gettext_noop("sets the host (and optionally the port) for a "
                 "remote netsync action"))
#ifdef option_bodies
{
  remote_stdio_host = utf8(arg, origin::user);
}
#endif

OPT(branch, "branch,b", branch_name, ,
        gettext_noop("select branch cert for operation"))
#ifdef option_bodies
{
  branch = branch_name(arg, origin::user);
}
#endif

OPT(brief, "brief", bool, false,
     gettext_noop("print a brief version of the normal output"))
#ifdef option_bodies
{
  brief = true;
}
#endif

OPT(revs_only, "revs-only", bool, false,
     gettext_noop("annotate using full revision ids only"))
#ifdef option_bodies
{
  revs_only = true;
}
#endif

// Remember COMMA doesn't work with GOPT, use long form.
//GOPT(conf_dir, "confdir", system_path, get_default_confdir() COMMA origin::user,
//     gettext_noop("set location of configuration directory"))
OPTVAR(globals, system_path, conf_dir, get_default_confdir() COMMA origin::user)
OPTION(globals, conf_dir, true, "confdir",
       gettext_noop("set location of configuration directory"))
#ifdef option_bodies
{
  conf_dir = system_path(arg, origin::user);
  if (!key_dir_given)
    key_dir = (conf_dir / "keys");
}
#endif

GOPT(no_default_confdir, "no-default-confdir", bool, false,
     gettext_noop("forbid use of the default confdir"))
#ifdef option_bodies
{
  no_default_confdir = true;
}
#endif

OPT(date, "date", date_t, ,
     gettext_noop("override date/time for commit"))
#ifdef option_bodies
{
  try
    {
      date = date_t(arg);
    }
  catch (std::exception &e)
    {
      throw bad_arg_internal(e.what());
    }
}
#endif

OPT(date_fmt, "date-format", std::string, ,
    gettext_noop("strftime(3) format specification for printing dates"))
#ifdef option_bodies
{
  date_fmt = arg;
}
#endif

OPT(format_dates, "no-format-dates", bool, true,
    gettext_noop("print date certs exactly as stored in the database"))
#ifdef option_bodies
{
  format_dates = false;
}
#endif

OPTVAR(globals, bool, dbname_is_memory, false);
GOPT(dbname, "db,d", system_path, , gettext_noop("set name of database"))
#ifdef option_bodies
{
  dbname = system_path(arg, origin::user);
  dbname_is_memory = (arg == ":memory:");
}
#endif

OPTION(globals, debug, false, "debug",
        gettext_noop("print debug log to stderr while running"))
#ifdef option_bodies
{
  global_sanity.set_debug();
}
#endif

OPT(depth, "depth", long, -1,
     gettext_noop("limit the number of levels of directories to descend"))
#ifdef option_bodies
{
  depth = boost::lexical_cast<long>(arg);
  if (depth < 0)
    throw bad_arg_internal(F("cannot be negative").str());
}
#endif


OPTSET(diff_options)

OPTVAR(diff_options, std::string, external_diff_args, )
OPTION(diff_options, external_diff_args, true, "diff-args",
        gettext_noop("argument to pass external diff hook"))
#ifdef option_bodies
{
  external_diff_args = arg;
}
#endif

OPTVAR(diff_options, bool, reverse, false)
OPTION(diff_options, reverse, false, "reverse",
        gettext_noop("reverse order of diff"))
#ifdef option_bodies
{
  reverse = true;
}
#endif
OPTVAR(diff_options, diff_type, diff_format, unified_diff)
OPTION(diff_options, diff_context, false, "context",
        gettext_noop("use context diff format"))
#ifdef option_bodies
{
  diff_format = context_diff;
}
#endif
OPTION(diff_options, diff_external, false, "external",
        gettext_noop("use external diff hook for generating diffs"))
#ifdef option_bodies
{
  diff_format = external_diff;
}
#endif
OPTION(diff_options, diff_unified, false, "unified",
        gettext_noop("use unified diff format"))
#ifdef option_bodies
{
  diff_format = unified_diff;
}
#endif
OPTVAR(diff_options, bool, no_show_encloser, false)
OPTION(diff_options, no_show_encloser, false, "no-show-encloser",
     gettext_noop("do not show the function containing each block of changes"))
#ifdef option_bodies
{
  no_show_encloser = true;
}
#endif

OPTVAR(diff_options, bool, without_header, false);
OPTVAR(diff_options, bool, with_header, false);
OPTION(diff_options, without_header, false, "without-header",
       gettext_noop("show the matching cset in the diff header"))
#ifdef option_bodies
{
  with_header = false;
  without_header = true;
}
#endif
OPTION(diff_options, with_header, false, "with-header",
       gettext_noop("do not show the matching cset in the diff header"))
#ifdef option_bodies
{
  with_header = true;
  without_header = false;
}
#endif

OPT(diffs, "diffs", bool, false, gettext_noop("print diffs along with logs"))
#ifdef option_bodies
{
  diffs = true;
}
#endif

OPTVAR(drop_attr, std::set<std::string>, attrs_to_drop, )
OPTION(drop_attr, drop_attr, true, "drop-attr",
        gettext_noop("when rosterifying, drop attrs entries with the given key"))
#ifdef option_bodies
{
  attrs_to_drop.insert(arg);
}
#endif

OPT(dryrun, "dry-run", bool, false,
     gettext_noop("don't perform the operation, just show what would have happened"))
#ifdef option_bodies
{
  dryrun = true;
}
#endif

OPT(drop_bad_certs, "drop-bad-certs", bool, false,
    gettext_noop("drop certs signed by keys we don't know about"))
#ifdef option_bodies
{
  drop_bad_certs = true;
}
#endif

OPTION(globals, dump, true, "dump",
        gettext_noop("file to dump debugging log to, on failure"))
#ifdef option_bodies
{
  global_sanity.set_dump_path(system_path(arg, origin::user).as_external());
}
#endif

OPTVAR(exclude, args_vector, exclude_patterns, )
OPTION(exclude, exclude, true, "exclude",
        gettext_noop("leave out anything described by its argument"))
#ifdef option_bodies
{
  exclude_patterns.push_back(arg_type(arg, origin::user));
}
#endif

OPT(bookkeep_only, "bookkeep-only", bool, false,
        gettext_noop("only update monotone's internal bookkeeping, not the filesystem"))
#ifdef option_bodies
{
  bookkeep_only = true;
}
#endif

OPT(move_conflicting_paths, "move-conflicting-paths", bool, false,
        gettext_noop("move conflicting, unversioned paths into _MTN/conflicts "
                     "before proceeding with any workspace change"))
#ifdef option_bodies
{
  move_conflicting_paths = true;
}
#endif

GOPT(ssh_sign, "ssh-sign", std::string, "yes",
     gettext_noop("controls use of ssh-agent.  valid arguments are: "
                  "'yes' to use ssh-agent to make signatures if possible, "
                  "'no' to force use of monotone's internal code, "
                  "'only' to force use of ssh-agent, "
                  "'check' to sign with both and compare"))
#ifdef option_bodies
{
  if (arg.empty())
    throw bad_arg_internal(F("--ssh-sign requires a value "
                             "['yes', 'no', 'only', or 'check']").str());
  if (arg != "yes"
      && arg != "no"
      && arg != "check"
      && arg != "only") // XXX what does "only" do? not documented
    throw bad_arg_internal(F("--ssh-sign must be set to 'yes', 'no', "
                             "'only', or 'check'").str());

  ssh_sign = arg;
}
#endif

OPT(force_duplicate_key, "force-duplicate-key", bool, false,
    gettext_noop("force genkey to not error out when the named key "
                 "already exists"))
#ifdef option_bodies
{
  force_duplicate_key = true;
}
#endif

OPT(full, "full", bool, false,
     gettext_noop("print detailed information"))
#ifdef option_bodies
{
  full = true;
}
#endif

GOPT(help, "help,h", bool, false, gettext_noop("display help message"))
#ifdef option_bodies
{
  help = true;
}
#endif

OPT(show_hidden_commands, "hidden", bool, false,
     gettext_noop("show hidden commands"))
#ifdef option_bodies
{
  show_hidden_commands = true;
}
#endif

OPTVAR(include, args_vector, include_patterns, )
OPTION(include, include, true, "include",
        gettext_noop("include anything described by its argument"))
#ifdef option_bodies
{
  include_patterns.push_back(arg_type(arg, origin::user));
}
#endif

GOPT(ignore_suspend_certs, "ignore-suspend-certs", bool, false,
     gettext_noop("do not ignore revisions marked as suspended"))
#ifdef option_bodies
{
  ignore_suspend_certs = true;
}
#endif


OPTVAR(key, external_key_name, signing_key, )
OPTION(globals, key, true, "key,k",
       gettext_noop("sets the key for signatures, using either the key "
                    "name or the key hash"))
#ifdef option_bodies
{
  signing_key = external_key_name(arg, origin::user);
}
#endif

// Remember COMMA doesn't work with GOPT, use long form.
//GOPT(key_dir, "keydir", system_path, get_default_keydir() COMMA origin::user,
//     gettext_noop("set location of key store"))
OPTVAR(globals, system_path, key_dir, get_default_keydir() COMMA origin::user)
OPTION(globals, key_dir, true, "keydir", gettext_noop("set location of key store"))
#ifdef option_bodies
{
  key_dir = system_path(arg, origin::user);
}
#endif

OPTVAR(key_to_push, std::vector<external_key_name>, keys_to_push, )
OPTION(key_to_push, key_to_push, true, "key-to-push",
        gettext_noop("push the specified key even if it hasn't signed anything"))
#ifdef option_bodies
{
  keys_to_push.push_back(external_key_name(arg, origin::user));
}
#endif

OPT(last, "last", long, -1,
     gettext_noop("limit log output to the last number of entries"))
#ifdef option_bodies
{
  last = boost::lexical_cast<long>(arg);
  if (last <= 0)
    throw bad_arg_internal(F("cannot be zero or negative").str());
}
#endif

OPTION(globals, log, true, "log", gettext_noop("file to write the log to"))
#ifdef option_bodies
{
  ui.redirect_log_to(system_path(arg, origin::user));
}
#endif

OPTSET(messages)
OPTVAR(messages, std::vector<std::string>, message, )
OPTVAR(messages, utf8, msgfile, )
OPTVAR(messages, bool, no_prefix, false)
OPTION(messages, message, true, "message,m",
        gettext_noop("set commit changelog message"))
#ifdef option_bodies
{
  message.push_back(arg);
}
#endif
OPTION(messages, msgfile, true, "message-file",
        gettext_noop("set filename containing commit changelog message"))
#ifdef option_bodies
{
  msgfile = utf8(arg, origin::user);
}
#endif
OPTION(messages, no_prefix, false, "no-prefix",
        gettext_noop("no prefix to message"))
#ifdef option_bodies
{
  no_prefix = true;
}
#endif

OPT(missing, "missing", bool, false,
     gettext_noop("perform the operations for files missing from workspace"))
#ifdef option_bodies
{
  missing = true;
}
#endif

OPT(next, "next", long, -1,
     gettext_noop("limit log output to the next number of entries"))
#ifdef option_bodies
{
  next = boost::lexical_cast<long>(arg);
  if (next <= 0)
    throw bad_arg_internal(F("cannot be zero or negative").str());
}
#endif

OPT(no_files, "no-files", bool, false,
     gettext_noop("exclude files when printing logs"))
#ifdef option_bodies
{
  no_files = true;
}
#endif

OPT(no_graph, "no-graph", bool, false,
     gettext_noop("do not use ASCII graph to display ancestry"))
#ifdef option_bodies
{
  no_graph = true;
}
#endif

OPT(no_ignore, "no-respect-ignore", bool, false,
     gettext_noop("do not ignore any files"))
#ifdef option_bodies
{
  no_ignore = true;
}
#endif

OPT(no_merges, "no-merges", bool, false,
     gettext_noop("exclude merges when printing logs"))
#ifdef option_bodies
{
  no_merges = true;
}
#endif

GOPT(norc, "norc", bool, false,
gettext_noop("do not load ~/.monotone/monotonerc or _MTN/monotonerc lua files"))
#ifdef option_bodies
{
  norc = true;
}
#endif

GOPT(nostd, "nostd", bool, false,
     gettext_noop("do not load standard lua hooks"))
#ifdef option_bodies
{
  nostd = true;
}
#endif

OPT(pidfile, "pid-file", system_path, ,
     gettext_noop("record process id of server"))
#ifdef option_bodies
{
  pidfile = system_path(arg, origin::user);
}
#endif

GOPT(quiet, "quiet", bool, false,
     gettext_noop("suppress verbose, informational and progress messages"))
#ifdef option_bodies
{
  quiet = true;
  global_sanity.set_quiet();
  ui.set_tick_write_nothing();
}
#endif

GOPT(extra_rcfiles, "rcfile", args_vector, ,
     gettext_noop("load extra rc file"))
#ifdef option_bodies
{
  extra_rcfiles.push_back(arg_type(arg, origin::user));
}
#endif

GOPT(reallyquiet, "reallyquiet", bool, false,
gettext_noop("suppress warning, verbose, informational and progress messages"))
#ifdef option_bodies
{
  reallyquiet = true;
  global_sanity.set_reallyquiet();
  ui.set_tick_write_nothing();
}
#endif

OPT(recursive, "recursive,R", bool, false,
     gettext_noop("also operate on the contents of any listed directories"))
#ifdef option_bodies
{
  recursive = true;
}
#endif

OPTVAR(revision, args_vector, revision_selectors, )
OPTION(revision, revision, true, "revision,r",
     gettext_noop("select revision id for operation"))
#ifdef option_bodies
{
  revision_selectors.push_back(arg_type(arg, origin::user));
}
#endif

GOPT(root, "root", std::string, ,
     gettext_noop("limit search for workspace to specified root"))
#ifdef option_bodies
{
  root = arg;
}
#endif

GOPT(no_workspace, "no-workspace", bool, false,
     gettext_noop("don't look for a workspace"))
#ifdef option_bodies
{
  no_workspace = true;
}
#endif

OPT(set_default, "set-default", bool, false,
     gettext_noop("use the current netsync arguments and options "
                  "as the future default"))
#ifdef option_bodies
{
  set_default = true;
}
#endif

GOPT(ticker, "ticker", std::string, ,
     gettext_noop("set ticker style (count|dot|none)"))
#ifdef option_bodies
{
  ticker = arg;
  if (ticker == "none" || global_sanity.quiet_p())
    ui.set_tick_write_nothing();
  else if (ticker == "dot")
    ui.set_tick_write_dot();
  else if (ticker == "count")
    ui.set_tick_write_count();
  else
    throw bad_arg_internal(F("argument must be 'none', 'dot', or 'count'").str());
}
#endif

OPT(from, "from", args_vector, , gettext_noop("revision(s) to start logging at"))
#ifdef option_bodies
{
  from.push_back(arg_type(arg, origin::user));
}
#endif

OPT(to, "to", args_vector, , gettext_noop("revision(s) to stop logging at"))
#ifdef option_bodies
{
  to.push_back(arg_type(arg, origin::user));
}
#endif

OPT(unknown, "unknown", bool, false,
     gettext_noop("perform the operations for unknown files from workspace"))
#ifdef option_bodies
{
  unknown = true;
}

#endif

OPT(verbose, "verbose", bool, false,
     gettext_noop("verbose completion output"))
#ifdef option_bodies
{
  verbose = true;
}
#endif

GOPT(version, "version", bool, false,
     gettext_noop("print version number, then exit"))
#ifdef option_bodies
{
  version = true;
}
#endif

OPTION(globals, xargs, true, "xargs,@",
       gettext_noop("insert command line arguments taken from the given file"))
#ifdef option_bodies
{
}
#endif

OPTSET(automate_inventory_opts)
OPTVAR(automate_inventory_opts, bool, no_ignored, false)
OPTVAR(automate_inventory_opts, bool, no_unknown, false)
OPTVAR(automate_inventory_opts, bool, no_unchanged, false)
OPTVAR(automate_inventory_opts, bool, no_corresponding_renames, false)

OPTION(automate_inventory_opts, no_ignored, false, "no-ignored",
       gettext_noop("don't output ignored files"))
#ifdef option_bodies
{
  no_ignored = true;
}
#endif

OPTION(automate_inventory_opts, no_unknown, false, "no-unknown",
       gettext_noop("don't output unknown files"))
#ifdef option_bodies
{
  no_unknown = true;
}
#endif

OPTION(automate_inventory_opts, no_unchanged, false, "no-unchanged",
       gettext_noop("don't output unchanged files"))
#ifdef option_bodies
{
  no_unchanged = true;
}
#endif

OPTION(automate_inventory_opts, no_corresponding_renames, false, "no-corresponding-renames",
       gettext_noop("don't output corresponding renames if restricted on such nodes"))
#ifdef option_bodies
{
  no_corresponding_renames = true;
}
#endif

OPTSET(resolve_conflicts_opts)
OPTVAR(resolve_conflicts_opts, bookkeeping_path, resolve_conflicts_file, )
OPTVAR(resolve_conflicts_opts, bool, resolve_conflicts, )

OPTION(resolve_conflicts_opts, resolve_conflicts_file, true, "resolve-conflicts-file",
       gettext_noop("use file to resolve conflicts"))
#ifdef option_bodies
{
  // we can't call  bookkeeping_path::external_string_is_bookkeeping_path
  // here, because we haven't found the workspace yet.
  E(bookkeeping_path::internal_string_is_bookkeeping_path(utf8(arg, origin::user)),
    origin::user,
    F("conflicts file must be under _MTN"));
  resolve_conflicts_file = bookkeeping_path(arg, origin::user);
}
#endif

OPTION(resolve_conflicts_opts, resolve_conflicts, false, "resolve-conflicts",
       gettext_noop("use _MTN/conflicts to resolve conflicts"))
#ifdef option_bodies
{
  E(!resolve_conflicts_file_given, origin::user,
    F("only one of --resolve-conflicts or --resolve-conflicts-file may be given"));
  resolve_conflicts_file = bookkeeping_path("_MTN/conflicts");
}
#endif

OPTSET(conflicts_opts)
OPTVAR(conflicts_opts, bookkeeping_path, conflicts_file, bookkeeping_path("_MTN/conflicts"))

OPTION(conflicts_opts, conflicts_file, true, "conflicts-file",
       gettext_noop("file in which to store conflicts"))
#ifdef option_bodies
{
  // we can't call bookkeeping_path::external_string_is_bookkeeping_path
  // here, because we haven't found the workspace yet.
  E(bookkeeping_path::internal_string_is_bookkeeping_path(utf8(arg, origin::user)),
    origin::user,
    F("conflicts file must be under _MTN"));
  conflicts_file = bookkeeping_path(arg, origin::user);
}
#endif

OPT(use_one_changelog, "use-one-changelog", bool, false,
    gettext_noop("use only one changelog cert for the git commit message"))
#ifdef option_bodies
{
  use_one_changelog = true;
}
#endif

OPT(authors_file, "authors-file", system_path, ,
    gettext_noop("file mapping author names from original to new values"))
#ifdef option_bodies
{
  authors_file = system_path(arg, origin::user);
}
#endif

OPT(branches_file, "branches-file", system_path, ,
    gettext_noop("file mapping branch names from original to new values "))
#ifdef option_bodies
{
  branches_file = system_path(arg, origin::user);
}
#endif

OPT(refs, "refs", std::set<std::string>, ,
    gettext_noop("include git refs for 'revs', 'roots' or 'leaves'"))
#ifdef option_bodies
{
  if (arg == "revs" || arg == "roots" || arg == "leaves")
    refs.insert(arg);
  else
    throw bad_arg_internal
      (F("git ref type must be 'revs', 'roots', or 'leaves'").str());
}
#endif

OPT(log_revids, "log-revids", bool, false,
    gettext_noop("include revision ids in commit logs"))
#ifdef option_bodies
{
  log_revids = true;
}
#endif

OPT(log_certs, "log-certs", bool, false,
    gettext_noop("include standard cert values in commit logs"))
#ifdef option_bodies
{
  log_certs = true;
}
#endif

OPT(import_marks, "import-marks", system_path, ,
    gettext_noop("load the internal marks table before exporting revisions"))
#ifdef option_bodies
{
  import_marks = system_path(arg, origin::user);
}
#endif

OPT(export_marks, "export-marks", system_path, ,
    gettext_noop("save the internal marks table after exporting revisions"))
#ifdef option_bodies
{
  export_marks = system_path(arg, origin::user);
}
#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
