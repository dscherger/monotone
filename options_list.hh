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
 * they get put into, and how they get there. There are 6 important macros
 * available here (and only here):
 *
 *   OPTSET(name)
 *     Defines a set of related options, which can easily be allowed for
 *     a particular command or reset together. It is named
 *     'options::opts::name'.
 *
 *     This is used primarily for convenience in specifying options for the
 *     CMD macro. Especially if several options always go together, they
 *     can be grouped into an optset and then only that optset has to be
 *     specified as an argument to the CMD macro when declaring commands
 *     that use all of those options.
 *
 *     This is also used for resettable options; any option that has a
 *     reset flag needs to be directly in an optset that *only* contains
 *     (directly or through OPTSET_REL) the optvar's that should be
 *     re-initialized when the reset flag is given. Because the
 *     SIMPLE_OPTION family all declare an optset containing only the
 *     one option and its one optvar, you don't typically need to care
 *     about this.
 *
 *   OPTSET_REL(parent, child)
 *     Declare a relationship between two optsets, so that if the parent
 *     is reset or allowed for a command the child will also be.
 *
 *     For example "diff" takes all of the options that "automate diff"
 *     takes, plus some additional ones. So there is a line below,
 *     "OPTSET_REL(diff_options, au_diff_options)", and then "diff" takes
 *     options::opts::diff_options and "automate diff" takes
 *     options::opts::au_diff_options. Options that only apply to "diff"
 *     go in the diff_options optset, while options that apply to both
 *     go in the au_diff_options optset.
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
 *   HIDE(option)
 *     Do not show the named option in normal help output. Hidden options
 *     are shown by the --hidden option.
 *
 *     In general, options should be hidden if they are introduced for
 *     testing purposes.
 *
 *   DEPRECATE(option, reason, deprecated_in, will_remove_in)
 *     Do not show the named option in help output (even with --hidden), and
 *     give a warning if it is used. The reason should be
 *     gettext_noopt("some text here") as it is translatable.
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

/*
 * If you want different *default* values for an option based on what command
 * is being run (for example --with-header/--no-with-header for 'diff' and
 * 'automate diff'), use CMD_PRESET_OPTIONS(cmdname) { ... } defined in cmd.hh.
 * It doesn't go in this file, but rather in the file where the command is
 * defined (ideally immediately before the CMD() declaration for that same
 * command, just to be consistent).
 */

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
void set_simple_option(u8 & t, std::string const & arg)
{
  long l = boost::lexical_cast<long>(arg);
  if (l < 0 || l > 255)
    throw bad_arg_internal(F("must be between 0 and 255").str());
  else
    t = (u8)l;
}
template<>
void set_simple_option(std::string & t, std::string const & arg)
{ t = arg; }
void set_simple_option(date_t & t, std::string const & arg)
{
  try { t = date_t(arg); }
  catch (std::exception & e)
    { throw bad_arg_internal(e.what()); }
}
template<>
void set_simple_option(std::vector<string> & t, std::string const & arg)
{ t.push_back(arg); }
template<>
void set_simple_option(std::set<string> & t, std::string const & arg)
{ t.insert(arg); }
template<>
void set_simple_option(enum_string & t, std::string const & arg)
{ t.set(arg); }
template<>
void set_simple_option(enum_string_set & t, std::string const & arg)
{ t.add(arg); }

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
#define SIMPLE_INITIALIZED_OPTION(name, optstring, type, init, description) \
  OPTSET(name)                                                          \
  OPTVAR(name, type, name, init)                                        \
  OPTION(name, name, has_arg<type >(), optstring, description)          \
  SIMPLE_OPTION_BODY(name)

#define SIMPLE_OPTION(name, optstring, type, description)               \
  SIMPLE_INITIALIZED_OPTION(name, optstring, type, , description)

#define GROUPED_SIMPLE_OPTION(group, name, optstring, type, description) \
  OPTSET_REL(group, name)                                               \
  SIMPLE_OPTION(name, optstring, type, description)


// because 'default_' is constructor arguments, and may need to be a list
// This doesn't work if fed through SIMPLE_INITIALIZED_OPTION (it wouldn't
// work with the other 2 SIMPLE_OPTION macros either, but it wouldn't make
// sense in the first place with those).
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

typedef std::map<branch_name, hexenc<id> > policy_revision_arg_map;
OPTVAR(globals, policy_revision_arg_map, policy_revisions, )
OPTION(globals, policy_revisions, true, "policy-revision",
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
// this is a more magic option
OPTION(globals, xargs, true, "xargs,@",
       gettext_noop("insert command line arguments taken from the given file"))
#ifdef option_bodies
{
}
#endif


SIMPLE_OPTION(author, "author", utf8, gettext_noop("override author for commit"))

SIMPLE_OPTION(automate_stdio_size, "automate-stdio-size",
              restricted_long<1>,
              gettext_noop("block size in bytes for \"automate stdio\" output"))

SIMPLE_OPTION(auto_update, "update/no-update", bool,
              gettext_noop("automatically update the workspace, if it is clean and the base "
                           "revision is a head of an affected branch"))

OPTSET(bind_opts)
GROUPED_SIMPLE_OPTION(bind_opts, bind_uris, "bind", std::vector<utf8>,
                      gettext_noop("address:port to listen on (default :4691)"))
HIDE(no_transport_auth)
GROUPED_SIMPLE_OPTION(bind_opts, no_transport_auth, "no-transport-auth", bool,
                      gettext_noop("disable transport authentication"))
HIDE(bind_stdio)
GROUPED_SIMPLE_OPTION(bind_opts, bind_stdio, "stdio", bool,
                      gettext_noop("serve netsync on stdio"))

HIDE(max_netsync_version)
SIMPLE_OPTION(max_netsync_version, "max-netsync-version", u8,
              gettext_noop("cause monotone to lie about the maximum netsync "
                           "protocol version that it supports, mostly for debugging"))
HIDE(min_netsync_version)
SIMPLE_OPTION(min_netsync_version, "min-netsync-version", u8,
              gettext_noop("cause monotone to lie about the minimum netsync "
                           "protocol version it supports, useful for debugging or "
                           "if you want to prevent use of older protocol versions"))

SIMPLE_OPTION(remote_stdio_host, "remote-stdio-host", arg_type,
    gettext_noop("sets the host (and optionally the port) for a "
                 "remote netsync action"))

SIMPLE_OPTION(branch, "branch,b", branch_name,
              gettext_noop("select branch cert for operation"))

SIMPLE_OPTION(brief, "brief/no-brief", bool,
              gettext_noop("print a brief version of the normal output"))

SIMPLE_OPTION(revs_only, "revs-only", bool,
              gettext_noop("annotate using full revision ids only"))

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

GROUPED_SIMPLE_OPTION(globals, no_default_confdir, "no-default-confdir/allow-default-confdir", bool,
                     gettext_noop("forbid use of the default confdir"))

SIMPLE_OPTION(date, "date", date_t,
              gettext_noop("override date/time for commit"))

OPTSET(date_formats)
OPTSET_REL(globals, date_formats)
OPTVAR(date_formats, std::string, date_fmt, )
OPTION(date_formats, date_fmt, true, "date-format",
       gettext_noop("strftime(3) format specification for printing dates"))
#ifdef option_bodies
{
  date_fmt = arg;
  no_format_dates = false;
}
#endif
GROUPED_SIMPLE_OPTION(date_formats, no_format_dates,
                      "no-format-dates", bool,
                      gettext_noop("print date certs exactly as stored in the database"))


OPTVAR(globals, db_type, dbname_type, )
OPTVAR(globals, std::string, dbname_alias, )
OPTVAR(globals, system_path, dbname, )
OPTION(globals, dbname, true, "db,d", gettext_noop("set name of database"))
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

HIDE(roster_cache_performance_log)
GROUPED_SIMPLE_OPTION(globals, roster_cache_performance_log, "roster-cache-performance-log",
                     system_path,
                     gettext_noop("log roster cache statistic to the given file"))

SIMPLE_OPTION(depth, "depth", restricted_long<0>,
              gettext_noop("limit the number of levels of directories to descend"))


OPTSET(diff_options)
OPTSET(au_diff_options)
OPTSET_REL(diff_options, au_diff_options)

GROUPED_SIMPLE_OPTION(diff_options, external_diff_args, "diff-args", std::string,
        gettext_noop("argument to pass external diff hook"))
GROUPED_SIMPLE_OPTION(au_diff_options, reverse, "reverse", bool,
        gettext_noop("reverse order of diff"))
GROUPED_SIMPLE_OPTION(diff_options, no_show_encloser, "no-show-encloser/show-encloser", bool,
     gettext_noop("do not show the function containing each block of changes"))
OPTSET_REL(au_diff_options, with_header)
SIMPLE_OPTION(with_header, "with-header/without-header", bool,
              gettext_noop("show the matching cset in the diff header"))

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


SIMPLE_OPTION(diffs, "diffs/no-diffs", bool, gettext_noop("print diffs along with logs"))

SIMPLE_OPTION(attrs_to_drop, "drop-attr", std::set<std::string>,
              gettext_noop("when rosterifying, drop attrs entries with the given key"))

SIMPLE_OPTION(dryrun, "dry-run/no-dry-run", bool,
              gettext_noop("don't perform the operation, just show what would have happened"))

SIMPLE_OPTION(drop_bad_certs, "drop-bad-certs", bool,
              gettext_noop("drop certs signed by keys we don't know about"))

GROUPED_SIMPLE_OPTION(globals, dump, "dump", system_path,
        gettext_noop("file to dump debugging log to, on failure"))

SIMPLE_OPTION(exclude, "exclude", args_vector,
              gettext_noop("leave out anything described by its argument"))
SIMPLE_OPTION(include, "include", args_vector,
        gettext_noop("include anything described by its argument"))

SIMPLE_OPTION(bookkeep_only, "bookkeep-only", bool,
        gettext_noop("only update monotone's internal bookkeeping, not the filesystem"))

SIMPLE_OPTION(move_conflicting_paths,
              "move-conflicting-paths/no-move-conflicting-paths",
              bool,
              gettext_noop("move conflicting, unversioned paths into _MTN/resolutions "
                           "before proceeding with any workspace change"))

OPTSET_REL(globals, ssh_sign)
SIMPLE_INITIALIZED_OPTION(ssh_sign, "ssh-sign", enum_string, "yes,no,only,check",
     gettext_noop("controls use of ssh-agent.  valid arguments are: "
                  "'yes' to use ssh-agent to make signatures if possible, "
                  "'no' to force use of monotone's internal code, "
                  "'only' to force use of ssh-agent, "
                  "'check' to sign with both and compare"))

SIMPLE_OPTION(force_duplicate_key, "force-duplicate-key", bool,
              gettext_noop("force genkey to not error out when the named key "
                           "already exists"))


GROUPED_SIMPLE_OPTION(globals, help, "help,h", bool, gettext_noop("display help message"))

SIMPLE_OPTION(show_hidden_commands, "hidden/no-hidden", bool,
              gettext_noop("show hidden commands and options"))

GROUPED_SIMPLE_OPTION(globals, ignore_suspend_certs, "ignore-suspend-certs/no-ignore-suspend-certs", bool,
                     gettext_noop("do not ignore revisions marked as suspended"))

GROUPED_SIMPLE_OPTION(globals, non_interactive, "non-interactive/interactive", bool,
                     gettext_noop("do not prompt the user for input"))

GROUPED_SIMPLE_OPTION(globals, key, "key,k/use-default-key", external_key_name,
       gettext_noop("sets the key for signatures, using either the key "
                    "name or the key hash"))

OPTSET_REL(globals, key_dir)
SIMPLE_INITIALIZED_OPTION(key_dir, "keydir", system_path,
                          system_path(get_default_keydir(), origin::internal),
                          gettext_noop("set location of key store"))

SIMPLE_OPTION(keys_to_push, "key-to-push", std::vector<external_key_name>,
        gettext_noop("push the specified key even if it hasn't signed anything"))

SIMPLE_OPTION(last, "last", restricted_long<1>,
              gettext_noop("limit log output to the last number of entries"))

GROUPED_SIMPLE_OPTION(globals, log, "log", system_path,
                     gettext_noop("file to write the log to"))

OPTSET(messages)
GROUPED_SIMPLE_OPTION(messages, message, "message,m", std::vector<std::string>,
        gettext_noop("set commit changelog message"))
GROUPED_SIMPLE_OPTION(messages, msgfile, "message-file", utf8,
        gettext_noop("set filename containing commit changelog message"))
HIDE(no_prefix)
GROUPED_SIMPLE_OPTION(messages, no_prefix, "no-prefix", bool,
        gettext_noop("no prefix to message"))

SIMPLE_OPTION(missing, "missing", bool,
              gettext_noop("perform the operations for files missing from workspace"))

SIMPLE_OPTION(next, "next", restricted_long<1>,
              gettext_noop("limit log output to the next number of entries"))

SIMPLE_OPTION(no_files, "no-files/files", bool,
              gettext_noop("exclude files when printing logs"))

SIMPLE_OPTION(no_graph, "no-graph/graph", bool,
              gettext_noop("do not use ASCII graph to display ancestry"))

SIMPLE_OPTION(no_ignore, "no-respect-ignore/respect-ignore", bool,
              gettext_noop("do not ignore any files"))

SIMPLE_OPTION(no_merges, "no-merges/merges", bool,
              gettext_noop("exclude merges when printing logs"))

#ifdef WIN32
# define NORC_TEXT gettext_noop("do not load %APPDATA%\\monotone\\monotonerc or " \
                                "_MTN\\monotonerc lua files")
#else
# define NORC_TEXT gettext_noop("do not load ~/.monotone/monotonerc or " \
                                "_MTN/monotonerc lua files")
#endif
GROUPED_SIMPLE_OPTION(globals, norc, "no-standard-rcfiles/standard-rcfiles", bool, NORC_TEXT)
#undef NORC_TEXT

GROUPED_SIMPLE_OPTION(globals, nostd, "no-builtin-rcfile/builtin-rcfile", bool,
                     gettext_noop("do not load the built-in lua file with the default hooks"))

DEPRECATE(old_norc, gettext_noop("please use --no-standard-rcfiles instead"), 1.0, 2.0)
OPTION(globals, old_norc, false, "norc",
       gettext_noop("old version of --no-standard-rcfiles"))
#ifdef option_bodies
{ norc = true; }
#endif
DEPRECATE(old_nostd, gettext_noop("please use --no-builtin-rcfile instead"), 1.0, 2.0)
OPTION(globals, old_nostd, false, "nostd",
       gettext_noop("old version of --no-builtin-rcfile"))
#ifdef option_bodies
{ nostd = true; }
#endif

GROUPED_SIMPLE_OPTION(globals, extra_rcfiles, "rcfile/clear-rcfiles", args_vector,
                     gettext_noop("load extra lua file"))

SIMPLE_OPTION(pidfile, "pid-file/no-pid-file", system_path,
              gettext_noop("record process id of server"))

OPTSET(verbosity)
OPTSET_REL(globals, verbosity)
OPTVAR(verbosity, int, verbosity, 0)

OPTION(verbosity, quiet, false, "quiet,q",
     gettext_noop("decrease verbosity (undo previous -v, then disable informational output, then disable warnings)"))
#ifdef option_bodies
{
  --verbosity;
  if (verbosity < -2)
    verbosity = -2;
}
#endif
OPTION(verbosity, verbose, false, "verbose,v",
       gettext_noop("increase verbosity (undo previous -q, and then enable debug output)"))
#ifdef option_bodies
{
  ++verbosity;
  if (verbosity > 1)
    verbosity = 1;
}
#endif

DEPRECATE(debug,
          gettext_noop("please us -v (or -v -v -v if there are previous -q options)"),
          1.0, 2.0)
OPTION(globals, debug, false, "debug",
       gettext_noop("print debug log to stderr while running"))
#ifdef option_bodies
{
  verbosity = 1;
}
#endif

DEPRECATE(reallyquiet, gettext_noop("please use -q -q"), 1.0, 2.0)
OPTION(verbosity, reallyquiet, false, "reallyquiet",
     gettext_noop("suppress warning, verbose, informational and progress messages"))
#ifdef option_bodies
{
  verbosity = -2;
}
#endif

SIMPLE_OPTION(full, "full/concise", bool,
       gettext_noop("print detailed information"))

SIMPLE_OPTION(formatted, "formatted/plain", bool,
              gettext_noop("automatically run the output through nroff (default if the output is a terminal)"))


GROUPED_SIMPLE_OPTION(globals, timestamps, "timestamps", bool,
                      gettext_noop("show timestamps in front of errors, warnings and progress messages"))

SIMPLE_OPTION(recursive, "recursive,R/no-recursive", bool,
              gettext_noop("also operate on the contents of any listed directories"))

SIMPLE_OPTION(revision, "revision,r",args_vector,
     gettext_noop("select revision id for operation"))

GROUPED_SIMPLE_OPTION(globals, root, "root", std::string,
                      gettext_noop("limit search for workspace to specified root"))

GROUPED_SIMPLE_OPTION(globals, no_workspace, "no-workspace/allow-workspace", bool,
                      gettext_noop("don't look for a workspace"))

SIMPLE_OPTION(set_default, "set-default/no-set-default", bool,
              gettext_noop("use the current netsync arguments and options "
                           "as the future default"))

OPTSET_REL(globals, ticker)
SIMPLE_INITIALIZED_OPTION(ticker, "ticker", enum_string, "count,dot,none",
                          gettext_noop("set ticker style (count|dot|none)"))

SIMPLE_OPTION(from, "from/clear-from", args_vector,
              gettext_noop("revision(s) to start logging at"))

SIMPLE_OPTION(to, "to/clear-to", args_vector,
              gettext_noop("revision(s) to stop logging at"))

SIMPLE_OPTION(unknown, "unknown/no-unknown", bool,
              gettext_noop("perform the operations for unknown files from workspace"))

GROUPED_SIMPLE_OPTION(globals, version, "version", bool,
                      gettext_noop("print version number, then exit"))


OPTSET(automate_inventory_opts)

OPTSET_REL(automate_inventory_opts, no_ignored)
SIMPLE_OPTION(no_ignored, "no-ignored/ignored", bool,
              gettext_noop("don't output ignored files"))
OPTSET_REL(automate_inventory_opts, no_unknown)
SIMPLE_OPTION(no_unknown, "no-unknown/unknown",bool,
              gettext_noop("don't output unknown files"))
OPTSET_REL(automate_inventory_opts, no_unchanged)
SIMPLE_OPTION(no_unchanged, "no-unchanged/unchanged", bool,
              gettext_noop("don't output unchanged files"))
OPTSET_REL(automate_inventory_opts, no_corresponding_renames)
SIMPLE_OPTION(no_corresponding_renames, "no-corresponding-renames/corresponding-renames", bool,
              gettext_noop("don't output corresponding renames if restricted on such nodes"))


OPTSET(resolve_conflicts_opts)
OPTVAR(resolve_conflicts_opts, bookkeeping_path,
       resolve_conflicts_file, "_MTN/conflicts")

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
  resolve_conflicts = true;
}
#endif

OPTSET_REL(resolve_conflicts_opts, resolve_conflicts)
SIMPLE_OPTION(resolve_conflicts, "resolve-conflicts/no-resolve-conflicts", bool,
       gettext_noop("specify conflict resolutions in a file, instead of interactively"))

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

SIMPLE_INITIALIZED_OPTION(refs, "refs", enum_string_set, "revs,roots,leaves",
                          gettext_noop("include git refs for 'revs', 'roots' or 'leaves'"))

SIMPLE_OPTION(log_revids, "log-revids/no-log-revids", bool,
              gettext_noop("include revision ids in commit logs"))

SIMPLE_OPTION(log_certs, "log-certs/no-log-certs", bool,
              gettext_noop("include standard cert values in commit logs"))

SIMPLE_OPTION(import_marks, "import-marks", system_path,
              gettext_noop("load the internal marks table before exporting revisions"))

SIMPLE_OPTION(export_marks, "export-marks", system_path,
              gettext_noop("save the internal marks table after exporting revisions"))

// clean up after ourselves
#undef SIMPLE_OPTION
#undef SIMPLE_OPTION_BODY
#undef GROUPED_SIMPLE_OPTION
#undef SIMPLE_INITIALIZED_OPTION
#undef COMMA

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
