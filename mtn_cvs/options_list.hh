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

OPT(revision, "revision,r", std::string, "", N_("select revision id for operation"))
#ifdef option_bodies
{
  // revision = true;
}
#endif

#if 0
COPT(revision, "revision,r", string, N_("select revision id for operation"));

GOPT(debug, "debug", nil, N_("print debug log to stderr while running"));
GOPT(help, "help,h", nil, N_("display help message"));
GOPT(version, "version,V", nil, N_("print version number, then exit"));
GOPT(mtn, "mtn", string, N_("monotone binary name"));
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
