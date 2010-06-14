// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//               2008-2010 Stephen Leake <stephen_leake@stephe-leake.org>
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
 *
 *
 *   Option Strings
 *
 *     Options can have a long name, a short name, and a 'reset' name. The
 *     long and short names run the function body in the '#ifdef
 *     option_bodies'. The 'reset' name is slightly different, it makes the
 *     optset that the option belongs to get reset. This means that if you
 *     have several related options in the same optset, you probably *don't*
 *     want to specify a reset name for any of them.
 *
 *     If you want an option to belong to an optset and also be resettable,
 *     you can use OPTSET_REL to make the desired optset include the option's
 *     personal optset.
 *
 *     The option string format is "long,s/reset". "--long" and "-s" are
 *     the long and short names, and set the option. "--reset" is the
 *     reset name, and resets the option. An option *must* have a long
 *     and/or short name, but isn't required to have a reset name. So
 *     "/foo" is invalid, but "foo,f", "foo/no-foo", "f/no-f", and
 *     "foo,f/no-foo" are all allowed.
 */

// This is a shortcut for an option which has its own variable and optset.
// It will take an argument unless 'type' is 'bool'.
#define OPT(name, string, type, default_, description)                  \
  OPTSET(name)                                                          \
  OPTVAR(name, type, name, default_)                                    \
  OPTION(name, name, has_arg<type >(), string, description)

// This is the same, except that the option and variable belong to the
// 'globals' optset. These are global options, not specific to a particular
// command.
#define GOPT(name, string, type, default_, description)                 \
  OPTSET(name)                                                          \
  OPTSET_REL(globals, name)                                             \
  OPTVAR(name, type, name, default_)                                    \
  OPTION(name, name, has_arg<type >(), string, description)

#ifdef option_bodies
template<typename T>
void set_simple_option(T & t, std::string const & arg)
{ t = T(arg, origin::user); }
template<typename T>
void set_simple_option(std::vector<T> & t, std::string const & arg)
{ t.push_back(T(arg, origin::user)); }
template<typename T>
void set_simple_option(std::set<T> & t, std::string const & arg)
{ t.insert(T(arg, origin::user)); }
template<>
void set_simple_option(bool & t, std::string const & arg)
{ t = true; }
template<>
void set_simple_option(std::string & t, std::string const & arg)
{ t = arg; }
template<>
void set_simple_option(std::vector<string> & t, std::string const & arg)
{ t.push_back(arg); }
template<>
void set_simple_option(std::set<string> & t, std::string const & arg)
{ t.insert(arg); }
# define SIMPLE_OPTION_BODY(name) { set_simple_option(name, arg); }
#else
# define SIMPLE_OPTION_BODY(name)
#endif
/*
 * This is a 'magic' option, and Does The Right Thing based on its data type:
 *  * If it's a bool, it will be true if the option is given and false if
 *    not given (or reset), and will not take an argument.
 *  * If it's a string or vocab type, it will be set to the argument if
 *    given, and set to the empty value (default constructor) if reset
 *    or not given.
 *  * If it's a container (vector/set) of strings or vocab types, each
 *    time the option is given the argument will be added to the collection
 *    (with push_back or insert), and the collection will be empty if the
 *    option is not given or is reset.
 */
#define SIMPLE_OPTION(name, optstring, type, description)               \
  OPTSET(name)                                                          \
  OPTVAR(name, type, name, )                                            \
  OPTION(name, name, has_arg<type >(), optstring, description)  \
  SIMPLE_OPTION_BODY(name)

// Like SIMPLE_OPTION, but the declared option is a member of the globals
#define GLOBAL_SIMPLE_OPTION(name, optstring, type, description) \
  OPTSET_REL(globals, name) \
  SIMPLE_OPTION(name, optstring, type, description)

// because 'default_' is constructor arguments, and may need to be a list
// This doesn't work if fed through the OPT / GOPT shorthand versions
#define COMMA ,

OPTSET(globals)

// this is a magic option
OPTVAR(globals, args_vector, args, )
OPTION(globals, positionals, true, "--", "")
#ifdef option_bodies
{
  args.push_back(arg_type(arg, origin::user));
}
#endif
// this is a more magic option
OPTION(globals, xargs, true, "xargs,@",
       gettext_noop("insert command line arguments taken from the given file"))
#ifdef option_bodies
{
}
#endif


SIMPLE_OPTION(author, "author", utf8, gettext_noop("override author for commit"))

OPT(automate_stdio_size, "automate-stdio-size", size_t, 32768,
     gettext_noop("block size in bytes for \"automate stdio\" output"))
#ifdef option_bodies
{
  automate_stdio_size = boost::lexical_cast<long>(arg);
  if (automate_stdio_size <= 0)
    throw bad_arg_internal(F("cannot be zero or negative").str());
}
#endif

SIMPLE_OPTION(auto_update, "update/no-update", bool,
              gettext_noop("automatically update the workspace, if it is clean and the base "
                           "revision is a head of an affected branch"))

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

SIMPLE_OPTION(remote_stdio_host, "remote-stdio-host", utf8,
    gettext_noop("sets the host (and optionally the port) for a "
                 "remote netsync action"))

SIMPLE_OPTION(branch, "branch,b", branch_name, gettext_noop("select branch cert for operation"))

SIMPLE_OPTION(brief, "brief/no-brief", bool, gettext_noop("print a brief version of the normal output"))

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

GLOBAL_SIMPLE_OPTION(no_default_confdir, "no-default-confdir/allow-default-confdir", bool,
                     gettext_noop("forbid use of the default confdir"))

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

GLOBAL_SIMPLE_OPTION(date_fmt, "date-format/default-date-format", std::string,
                     gettext_noop("strftime(3) format specification for printing dates"))

GOPT(format_dates, "no-format-dates", bool, true,
     gettext_noop("print date certs exactly as stored in the database"))
#ifdef option_bodies
{
  format_dates = false;
}
#endif


OPTVAR(globals, db_type, dbname_type, );
OPTVAR(globals, std::string, dbname_alias, );
GOPT(dbname, "db,d", system_path, , gettext_noop("set name of database"))
#ifdef option_bodies
{
  if (arg == memory_db_identifier)
    {
      dbname_type = memory_db;
    }
  else if (arg.size() > 0 && arg.substr(0, 1) == ":")
    {
      dbname_alias = arg;
      dbname_type = managed_db;
    }
  else
    {
      dbname = system_path(arg, origin::user);
      dbname_type = unmanaged_db;
    }
}
#endif

GLOBAL_SIMPLE_OPTION(roster_cache_performance_log, "roster-cache-performance-log",
                     system_path,
                     gettext_noop("log roster cache statistic to the given file"))

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
OPTSET(au_diff_options)
OPTSET_REL(diff_options, au_diff_options)

OPTVAR(diff_options, std::string, external_diff_args, )
OPTION(diff_options, external_diff_args, true, "diff-args",
        gettext_noop("argument to pass external diff hook"))
#ifdef option_bodies
{
  external_diff_args = arg;
}
#endif

OPTVAR(au_diff_options, bool, reverse, false)
OPTION(au_diff_options, reverse, false, "reverse",
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

OPTVAR(au_diff_options, bool, without_header, false);
OPTVAR(au_diff_options, bool, with_header, false);
OPTION(au_diff_options, without_header, false, "without-header",
       gettext_noop("show the matching cset in the diff header"))
#ifdef option_bodies
{
  with_header = false;
  without_header = true;
}
#endif
OPTION(au_diff_options, with_header, false, "with-header",
       gettext_noop("do not show the matching cset in the diff header"))
#ifdef option_bodies
{
  with_header = true;
  without_header = false;
}
#endif

SIMPLE_OPTION(diffs, "diffs/no-diffs", bool, gettext_noop("print diffs along with logs"))

OPTSET(drop_attr)
OPTVAR(drop_attr, std::set<std::string>, attrs_to_drop, )
OPTION(drop_attr, drop_attr, true, "drop-attr",
        gettext_noop("when rosterifying, drop attrs entries with the given key"))
#ifdef option_bodies
{
  attrs_to_drop.insert(arg);
}
#endif

SIMPLE_OPTION(dryrun, "dry-run/no-dry-run", bool,
              gettext_noop("don't perform the operation, just show what would have happened"))

SIMPLE_OPTION(drop_bad_certs, "drop-bad-certs", bool,
              gettext_noop("drop certs signed by keys we don't know about"))

GLOBAL_SIMPLE_OPTION(dump, "dump", system_path,
        gettext_noop("file to dump debugging log to, on failure"))

OPTSET(exclude)
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

SIMPLE_OPTION(move_conflicting_paths, "move-conflicting-paths", bool,
              gettext_noop("move conflicting, unversioned paths into _MTN/resolutions "
                           "before proceeding with any workspace change"))

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

SIMPLE_OPTION(force_duplicate_key, "force-duplicate-key", bool,
              gettext_noop("force genkey to not error out when the named key "
                           "already exists"))


GLOBAL_SIMPLE_OPTION(help, "help,h", bool, gettext_noop("display help message"))

SIMPLE_OPTION(show_hidden_commands, "hidden/no-hidden", bool,
              gettext_noop("show hidden commands"))

OPTSET(include)
OPTVAR(include, args_vector, include_patterns, )
OPTION(include, include, true, "include",
        gettext_noop("include anything described by its argument"))
#ifdef option_bodies
{
  include_patterns.push_back(arg_type(arg, origin::user));
}
#endif

GLOBAL_SIMPLE_OPTION(ignore_suspend_certs, "ignore-suspend-certs/no-ignore-suspend-certs", bool,
                     gettext_noop("do not ignore revisions marked as suspended"))

GLOBAL_SIMPLE_OPTION(non_interactive, "non-interactive/interactive", bool,
                     gettext_noop("do not prompt the user for input"))

OPTSET(key)
OPTVAR(key, external_key_name, signing_key, )
OPTION(globals, key, true, "key,k/use-default-key",
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

OPTSET(key_to_push)
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

GLOBAL_SIMPLE_OPTION(log, "log", system_path, gettext_noop("file to write the log to"))

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

SIMPLE_OPTION(missing, "missing", bool,
              gettext_noop("perform the operations for files missing from workspace"))

OPT(next, "next", long, -1,
     gettext_noop("limit log output to the next number of entries"))
#ifdef option_bodies
{
  next = boost::lexical_cast<long>(arg);
  if (next <= 0)
    throw bad_arg_internal(F("cannot be zero or negative").str());
}
#endif

SIMPLE_OPTION(no_files, "no-files/files", bool,
              gettext_noop("exclude files when printing logs"))

SIMPLE_OPTION(no_graph, "no-graph/graph", bool,
              gettext_noop("do not use ASCII graph to display ancestry"))

SIMPLE_OPTION(no_ignore, "no-respect-ignore/respect-ignore", bool,
              gettext_noop("do not ignore any files"))

SIMPLE_OPTION(no_merges, "no-merges/merges", bool,
              gettext_noop("exclude merges when printing logs"))

GLOBAL_SIMPLE_OPTION(norc, "norc/yesrc", bool,
                     gettext_noop("do not load ~/.monotone/monotonerc or _MTN/monotonerc lua files"))

GLOBAL_SIMPLE_OPTION(nostd, "nostd/stdhooks", bool,
                     gettext_noop("do not load standard lua hooks"))

SIMPLE_OPTION(pidfile, "pid-file/no-pid-file", system_path,
              gettext_noop("record process id of server"))

GLOBAL_SIMPLE_OPTION(extra_rcfiles, "rcfile/clear-rcfiles", args_vector,
                     gettext_noop("load extra rc file"))

OPTSET(verbosity)
OPTSET_REL(globals, verbosity)
OPTVAR(verbosity, int, verbosity, 0)
OPTION(verbosity, set_verbosity, true, "verbosity",
       gettext_noop("set verbosity level: 0 is default; 1 is verbose; "
                    "-1 is hide tickers and progress messages; -2 is also hide warnings"))
#ifdef option_bodies
{
  verbosity = boost::lexical_cast<long>(arg);
}
#endif
OPTION(verbosity, inc_verbosity, false, "v",
       gettext_noop("increase verbosity level by one"))
#ifdef option_bodies
{
  ++verbosity;
}
#endif

OPTSET(full)
OPTION(full, full, false, "full",
       gettext_noop("print detailed information"))
#ifdef option_bodies
{
  if (verbosity < 1)
    verbosity = 1;
}
#endif

OPTSET(verbose)
OPTSET_REL(verbosity, verbose)
OPTION(verbose, verbose, false, "verbose/no-verbose",
       gettext_noop("verbose completion output"))
#ifdef option_bodies
{
  if (verbosity < 1)
    verbosity = 1;
}
#endif

OPTION(verbosity, quiet, false, "quiet",
     gettext_noop("suppress verbose, informational and progress messages"))
#ifdef option_bodies
{
  if (verbosity > -1)
    verbosity = -1;
}
#endif

OPTION(verbosity, reallyquiet, false, "reallyquiet",
     gettext_noop("suppress warning, verbose, informational and progress messages"))
#ifdef option_bodies
{
  verbosity = -2;
}
#endif

GOPT(timestamps, "timestamps", bool, false,
     gettext_noop("show timestamps in front of errors, warnings and progress messages"))
#ifdef option_bodies
{
  timestamps = true;
}
#endif

SIMPLE_OPTION(recursive, "recursive,R/no-recursive", bool,
              gettext_noop("also operate on the contents of any listed directories"))

OPTSET(revision)
OPTVAR(revision, args_vector, revision_selectors, )
OPTION(revision, revision, true, "revision,r",
     gettext_noop("select revision id for operation"))
#ifdef option_bodies
{
  revision_selectors.push_back(arg_type(arg, origin::user));
}
#endif

GLOBAL_SIMPLE_OPTION(root, "root", std::string,
                     gettext_noop("limit search for workspace to specified root"))

GLOBAL_SIMPLE_OPTION(no_workspace, "no-workspace/allow-workspace", bool,
                     gettext_noop("don't look for a workspace"))

SIMPLE_OPTION(set_default, "set-default/no-set-default", bool,
              gettext_noop("use the current netsync arguments and options "
                           "as the future default"))

GOPT(ticker, "ticker", std::string, ,
     gettext_noop("set ticker style (count|dot|none)"))
#ifdef option_bodies
{
  ticker = arg;
  if (ticker != "none" &&
      ticker != "dot" &&
      ticker != "count")
    throw bad_arg_internal(F("argument must be 'none', 'dot', or 'count'").str());
}
#endif

SIMPLE_OPTION(from, "from/clear-from", args_vector,
              gettext_noop("revision(s) to start logging at"))

SIMPLE_OPTION(to, "to/clear-to", args_vector,
              gettext_noop("revision(s) to stop logging at"))

SIMPLE_OPTION(unknown, "unknown", bool,
              gettext_noop("perform the operations for unknown files from workspace"))

GLOBAL_SIMPLE_OPTION(version, "version", bool,
                     gettext_noop("print version number, then exit"))


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

SIMPLE_OPTION(use_one_changelog, "use-one-changelog", bool,
              gettext_noop("use only one changelog cert for the git commit message"))

SIMPLE_OPTION(authors_file, "authors-file", system_path,
              gettext_noop("file mapping author names from original to new values"))

SIMPLE_OPTION(branches_file, "branches-file", system_path,
              gettext_noop("file mapping branch names from original to new values "))

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

SIMPLE_OPTION(log_revids, "log-revids/no-log-revids", bool,
              gettext_noop("include revision ids in commit logs"))

SIMPLE_OPTION(log_certs, "log-certs/no-log-certs", bool,
              gettext_noop("include standard cert values in commit logs"))

SIMPLE_OPTION(import_marks, "import-marks", system_path,
              gettext_noop("load the internal marks table before exporting revisions"))

SIMPLE_OPTION(export_marks, "export-marks", system_path,
              gettext_noop("save the internal marks table after exporting revisions"))

// clean up after ourselves
#undef OPT
#undef GOPT
#undef SIMPLE_OPTION
#undef SIMPLE_OPTION_BODY
#undef GLOBAL_SIMPLE_OPTION
#undef COMMA

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
