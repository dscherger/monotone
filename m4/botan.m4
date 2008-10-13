# Currently we accept botan version 1.7.8 and newer, limited to the
# development branch 1.7, emitting a warning if the found botan is
# newer than 1.7.18.

AC_DEFUN([MTN_FIND_BOTAN],
[
  AC_MSG_CHECKING([for Botan])
  if test -n "`type -p botan-config`"; then
    BOTAN_VERSION="`botan-config --version`"
    BOTAN_CPPFLAGS="`botan-config --cflags`"

    # botan-config has the annoying habit of telling us to use
    # -L switches for directories that the compiler will search
    # automatically.
    BOTAN_LIBS="`botan-config --libs | \
	       sed -e 's:-L */usr/lib/*::' -e 's:-R */usr/lib/*::' \
	           -e 's:-L */lib/*::' -e 's:-R */lib/*::'`"

    found_botan=yes

    # make sure we have to do with botan version 1.7
    save_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $BOTAN_CPPFLAGS"
    AC_PREPROC_IFELSE([
#include <botan/version.h>

#if BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,7,8)
#error "Botan is too old"
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

#if BOTAN_VERSION_CODE > BOTAN_VERSION_CODE_FOR(1,7,18)
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

