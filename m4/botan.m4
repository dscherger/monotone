# We accept botan versions 1.6.3 and newer, including most of the
# (unstable) 1.7.x branch, preferrably 1.8.x. The next development
# branch 1.9.x is not supported for now.

AC_DEFUN([MTN_FIND_BOTAN],
[
  AC_MSG_CHECKING([for Botan])
  if test -n "`type -p botan-config`"; then
    BOTAN_VERSION="`botan-config --version`"
    BOTAN_CPPFLAGS="`botan-config --cflags`"
    BOTAN_LIBS="`botan-config --libs`"

    found_botan=yes

    # make sure we have to do with a supported botan version
    save_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $BOTAN_CPPFLAGS"
    AC_PREPROC_IFELSE([
#include <botan/version.h>

#if BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,6,3)
#error "Botan is too old"
#endif

#if BOTAN_VERSION_CODE == BOTAN_VERSION_CODE_FOR(1,7,14)
#error "Botan 1.7.14 is unusable for monotone"
#endif],
    [botan_version_match=yes],
    [botan_version_match=no])
    if test $botan_version_match = no; then
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([Your botan library is too old ($BOTAN_VERSION).])
    fi

    # check against unknown versions from the future and warn
    AC_PREPROC_IFELSE([
#include <botan/version.h>

#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,9,0)
#error "Botan from the future"
#endif],
    [botan_version_match=yes],
    [botan_version_match=no])

    CPPFLAGS="$save_CPPFLAGS"
    AC_MSG_RESULT([yes (version $BOTAN_VERSION)])

    if test $botan_version_match = no; then
      AC_MSG_WARN([Your botan library version ($BOTAN_VERSION) is newer than expected. Monotone might not build cleanly.])
    fi

    # AC_MSG_NOTICE([using botan compile flags: "$BOTAN_CPPFLAGS"])
    # AC_MSG_NOTICE([using botan link flags: "$BOTAN_LIBS"])

    AC_SUBST(BOTAN_LIBS)
    AC_SUBST(BOTAN_CPPFLAGS)
  else
    found_botan=no
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([Botan cannot be found.])
  fi
])

