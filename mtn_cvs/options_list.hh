
COPT(branch_name, "branch,b", string, N_("select branch cert for operation"));
COPT(revision, "revision,r", string, N_("select revision id for operation"));
COPT(since, "since", string, N_("set history start for CVS pull"));
COPT(full, "full", string, N_("ignore already pulled CVS revisions"));

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
