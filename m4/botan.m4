# Currently we accept botan version 1.7.8 and newer, limited to the
# development branch 1.7, emitting a warning if the found botan is
# newer than 1.7.15.

AC_DEFUN([MTN_FIND_BOTAN],
[
  AC_MSG_CHECKING([for Botan version 1.7.8 or newer])
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
    save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $BOTAN_CPPFLAGS"
    AC_PREPROC_IFELSE([
#include <botan/build.h>

#ifndef BOTAN_VERSION_MAJOR
#error Botan didn't define version macros?!?
#endif

#if BOTAN_VERSION_MAJOR != 1
#error Botan major version mismatch.
#endif],
    [botan_version_match=yes],
    [botan_version_match=no])
    if test $botan_version_match = no; then
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([Your botan library version not match ($BOTAN_VERSION).])
    fi

    # prevent from building against older, no longer supported versions
    AC_PREPROC_IFELSE([
#include <botan/build.h>

#if BOTAN_VERSION_PATCH < 8
#error Botan is too old
#endif],
    [botan_version_match=yes],
    [botan_version_match=no])
    if test $botan_version_match = no; then
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([Your botan library is too old ($BOTAN_VERSION).])
    fi

    # check against unknown versions from the future and warn
    AC_PREPROC_IFELSE([
#include <botan/build.h>

#if BOTAN_VERSION_PATCH > 15
#error Botan from the future
#endif],
    [botan_version_match=yes],
    [botan_version_match=no])
    if test $botan_version_match = no; then
      AC_MSG_WARN([Your botan library version ($BOTAN_VERSION) is newer than expected. Monotone might not build cleanly.])
    fi

    CFLAGS="$save_CFLAGS"
    AC_MSG_RESULT([yes])
    AC_SUBST(BOTAN_LIBS)
    AC_SUBST(BOTAN_CFLAGS)
  else
    found_botan=no
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([Botan cannot be found.])
  fi
])

