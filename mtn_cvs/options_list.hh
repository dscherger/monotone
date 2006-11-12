#define OPT(name, string, type, default_, description)			\
  OPTVAR(name, type, name, default_)					\
  OPTION(name, name, has_arg<type>(), string, description)

#define GOPT(name, string, type, default_, description)			\
  OPTVAR(globals, type, name, default_)					\
  OPTION(globals, name, has_arg<type>(), string, description)

OPTSET(globals)

OPTVAR(globals, std::vector<utf8>, args, )

OPTVAR(branch, utf8, branch_name, )
OPTION(branch, branch, true, "branch,b", N_("select branch cert for operation"))
#ifdef option_bodies
{
  branch_name = utf8(arg);
}
#endif

OPT(since, "since", utf8, , N_("set history start for CVS pull"))
#ifdef option_bodies
{
  since = arg;
}
#endif

OPT(full, "full", bool, false, N_("ignore already pulled CVS revisions"))
#ifdef option_bodies
{
  full = true;
}
#endif

OPTVAR(revision, std::vector<revision_id>, revisions, )
OPTION(revision, revision, true, "revision,r", 
      N_("select revision id(s) for operation"))
#ifdef option_bodies
{
  revisions.push_back(revision_id(arg));
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

GOPT(mtn_binary, "mtn", utf8, , gettext_noop("monotone binary name"))
#ifdef option_bodies
{
  mtn_binary = arg;
}
#endif

GOPT(domain, "domain", utf8, "cvs", gettext_noop("synchronization domain"))
#ifdef option_bodies
{
  domain = arg;
}
#endif

OPTVAR(globals, std::vector<utf8>, mtn_options, )
OPTION(globals, mtn_option, true, "mtn-option", N_("pass option to monotone"))
#ifdef option_bodies
{
  mtn_options.push_back(arg);
}
#endif

#define TRANSOPT_sub(name,hasarg,optstring,desc) \
  OPTION(globals,name,hasarg,optstring,desc)

#ifdef option_bodies
#define TRANSOPT3(name,optstring,desc) TRANSOPT_sub(name,true,optstring,desc) \
{ mtn_options.push_back("--" #name "=" +arg); }
#else
#define TRANSOPT3(name,optstring,desc) TRANSOPT_sub(name,true,optstring,desc) 
#endif

#define TRANSOPT(name,desc) TRANSOPT3(name,#name,desc)

#ifdef option_bodies
#define TRANSOPT_BOOL(name,desc) TRANSOPT_sub(name,false,#name,desc) \
{ mtn_options.push_back(utf8("--" #name)); }
#else
#define TRANSOPT_BOOL(name,desc) TRANSOPT_sub(name,false,#name,desc) 
#endif

// these options are passed transparently
TRANSOPT3(db, "db,d", N_("passed: database location"));
TRANSOPT(rcfile, N_("passed: config file"));
TRANSOPT_BOOL(nostd, N_("passed: do not read standard hooks"));
TRANSOPT(keydir, N_("passed: key directory"));
TRANSOPT3(key, "key,k", N_("passed: key"));
TRANSOPT_BOOL(norc, N_("passed: norc"));
TRANSOPT(root, N_("passed: root"));
TRANSOPT(confdir, N_("passed: confdir"));

#undef TRANSOPT3
#undef TRANSOPT_BOOL
#undef TRANSOPT

#if 0
GOPT(db, "db,d", string, N_("passed: database location"));
GOPT(rcfile, "rcfile", string, N_("passed: config file"));
GOPT(nostd, "nostd", nil, N_("passed: do not read standard hooks"));
GOPT(keydir, "keydir", string, N_("passed: key directory"));
GOPT(key, "key,k", string, N_("passed: key"));
GOPT(norc, "norc", nil, N_("passed: norc"));
GOPT(root, "root", string, N_("passed: root"));
GOPT(confdir, "confdir", string, N_("passed: confdir"));
#endif
