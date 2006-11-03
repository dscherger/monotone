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

OPT(since, "since", bool, false, N_("set history start for CVS pull"))
#ifdef option_bodies
{
  since = true;
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

GOPT(mtn, "mtn", std::string, , gettext_noop("monotone binary name"))
#ifdef option_bodies
{
  mtn = arg;
}
#endif

#if 0
GOPT(mtn_option, "mtn-option", string, N_("pass option to monotone"));
// these options are passed transparently
GOPT(db, "db,d", string, N_("passed: database location"));
GOPT(rcfile, "rcfile", string, N_("passed: config file"));
GOPT(nostd, "nostd", nil, N_("passed: do not read standard hooks"));
GOPT(keydir, "keydir", string, N_("passed: key directory"));
GOPT(key, "key,k", string, N_("passed: key"));
GOPT(norc, "norc", nil, N_("passed: norc"));
GOPT(root, "root", string, N_("passed: root"));
GOPT(confdir, "confdir", string, N_("passed: confdir"));
#endif
