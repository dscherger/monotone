#ifdef option_bodies
#include "../transforms.hh"
#endif

#define OPT(name, string, type, default_, description)			\
  OPTVAR(name, type, name, default_)					\
  OPTION(name, name, has_arg<type>(), string, description)

#define GOPT(name, string, type, default_, description)			\
  OPTVAR(globals, type, name, default_)					\
  OPTION(globals, name, has_arg<type>(), string, description)

OPTSET(globals)

OPTVAR(globals, args_vector, args, )
OPTION(globals, positionals, true, "--", "")
#ifdef option_bodies
{
  args.push_back(arg_type(arg, origin::user));
}
#endif

OPTVAR(branch, branch_name, branchname, )
OPTION(branch, branch, true, "branch,b", gettext_noop("select branch cert for operation"))
#ifdef option_bodies
{
  branchname = branch_name(arg, origin::user);
}
#endif

OPT(since, "since", std::string, , gettext_noop("set history start for CVS pull"))
#ifdef option_bodies
{
  since = std::string(arg);
}
#endif

OPT(full, "full", bool, false, gettext_noop("ignore already pulled CVS revisions"))
#ifdef option_bodies
{
  full = true;
}
#endif

OPT(no_time, "no-time", bool, false, gettext_noop("do not send Checkin-time command on push"))
#ifdef option_bodies
{
  no_time = true;
}
#endif

OPT(first, "first", bool, false, gettext_noop("take first child if choice necessary"))
#ifdef option_bodies
{
  first = true;
}
#endif

OPTVAR(revision, std::vector<revision_id>, revisions, )
OPTION(revision, revision, true, "revision,r", 
      gettext_noop("select revision id(s) for operation"))
#ifdef option_bodies
{
  revisions.push_back(revision_id(decode_hexenc(arg, origin::user),origin::user));
}
#endif

GOPT(version, "version,V", bool, false,
     gettext_noop("print version number, then exit"))
#ifdef option_bodies
{
  version = true;
}
#endif

GOPT(help, "help,h", bool, false, gettext_noop("display help message"))
#ifdef option_bodies
{
  help = true;
}
#endif

OPTION(globals, debug, false, "debug",
        gettext_noop("print debug log to stderr while running"))
#ifdef option_bodies
{
  global_sanity.set_debug();
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

GOPT(reallyquiet, "reallyquiet", bool, false,
gettext_noop("suppress warning, verbose, informational and progress messages"))
#ifdef option_bodies
{
  reallyquiet = true;
  global_sanity.set_reallyquiet();
  ui.set_tick_write_nothing();
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

GOPT(mtn_binary, "mtn", std::string, , gettext_noop("monotone binary name"))
#ifdef option_bodies
{
  mtn_binary = std::string(arg);
}
#endif

GOPT(domain, "domain", std::string, "cvs", gettext_noop("synchronization domain"))
#ifdef option_bodies
{
  domain = std::string(arg);
}
#endif

OPTVAR(globals, std::vector<std::string>, mtn_options, )
OPTION(globals, mtn_option, true, "mtn-option", gettext_noop("pass option to monotone"))
#ifdef option_bodies
{
  mtn_options.push_back(std::string(arg));
}
#endif

OPTION(globals, dump, true, "dump",
        gettext_noop("file to dump debugging log to, on failure"))
#ifdef option_bodies
{
  global_sanity.set_dump_path(system_path(arg, origin::user).as_external());
}
#endif

#define TRANSOPT_sub(name,hasarg,optstring,desc) \
  OPTION(globals,name,hasarg,optstring,desc)

#ifdef option_bodies
#define TRANSOPT3(name,optstring,desc) TRANSOPT_sub(name,true,optstring,desc) \
{ mtn_options.push_back(std::string("--" #name "=" +arg)); }
#else
#define TRANSOPT3(name,optstring,desc) TRANSOPT_sub(name,true,optstring,desc) 
#endif

#define TRANSOPT(name,desc) TRANSOPT3(name,#name,desc)

#ifdef option_bodies
#define TRANSOPT_BOOL(name,desc) TRANSOPT_sub(name,false,#name,desc) \
{ mtn_options.push_back(std::string("--" #name)); }
#else
#define TRANSOPT_BOOL(name,desc) TRANSOPT_sub(name,false,#name,desc) 
#endif

// these options are passed transparently
TRANSOPT3(db, "db,d", gettext_noop("passed: set name of database"));
TRANSOPT(rcfile, gettext_noop("passed: load extra rc file"));
TRANSOPT_BOOL(nostd, gettext_noop("passed: do not load standard lua hooks"));
TRANSOPT(keydir, gettext_noop("passed: set location of key store"));
TRANSOPT3(key, "key,k", gettext_noop("passed: set key for signatures"));
TRANSOPT_BOOL(norc, gettext_noop("passed: do not load ~/.monotone/monotonerc or _MTN/monotonerc lua files"));
TRANSOPT(root, gettext_noop("passed: limit search for workspace to specified root"));
TRANSOPT(confdir, gettext_noop("passed: set location of configuration directory"));

#undef TRANSOPT3
#undef TRANSOPT_BOOL
#undef TRANSOPT

