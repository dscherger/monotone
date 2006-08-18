
COPT(branch, "branch", string, N_("select branch cert for operation"));
#if 0
    {"revision", 'r', POPT_ARG_STRING, &argstr, MTNCVSOPT_REVISION, gettext_noop("select revision id for operation"), NULL},
    {"since", 0, POPT_ARG_STRING, &argstr, MTNCVSOPT_SINCE, N_("set history start for CVS pull"), NULL},
    {"full", 0, POPT_ARG_NONE, &argstr, MTNCVSOPT_FULL, N_("ignore already pulled CVS revisions"), NULL},
    { NULL, 0, 0, NULL, 0, NULL, NULL }
  };
#endif

GOPT(debug, "debug", nil, N_("print debug log to stderr while running"));

#if 0
struct poptOption options[] =
  {
    // Use the coptions table as well.
    { NULL, 0, POPT_ARG_INCLUDE_TABLE, coptions, 0, NULL, NULL },

    {"help", 'h', POPT_ARG_NONE, NULL, MTNCVSOPT_HELP, gettext_noop("display help message"), NULL},
    {"version", 0, POPT_ARG_NONE, NULL, MTNCVSOPT_VERSION, gettext_noop("print version number, then exit"), NULL},
    {"mtn", 'd', POPT_ARG_STRING, &argstr, MTNCVSOPT_BINARY, gettext_noop("monotone binary name"), NULL},
    {"mtn-option", 0, POPT_ARG_STRING, &argstr, MTNCVSOPT_MTN_OPTION, gettext_noop("pass option to monotone"), NULL},
// these options are passed transparently
    {"db", 'd', POPT_ARG_STRING, &argstr, MTNCVSOPT_DB, gettext_noop("passed: database location"), NULL},
    {"rcfile", 0, POPT_ARG_STRING, &argstr, MTNCVSOPT_RCFILE, gettext_noop("passed: config file"), NULL},
    {"nostd", 0, POPT_ARG_NONE, NULL, MTNCVSOPT_NOSTD, gettext_noop("passed: do not read standard hooks"), NULL},
    {"keydir", 0, POPT_ARG_STRING, &argstr, MTNCVSOPT_KEYDIR, gettext_noop("passed: key directory"), NULL},
    {"key", 0, POPT_ARG_STRING, &argstr, MTNCVSOPT_KEY, gettext_noop("passed: key"), NULL},
    { NULL, 0, 0, NULL, 0, NULL, NULL }
  };
#endif
