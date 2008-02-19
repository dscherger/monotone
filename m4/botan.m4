# Set up to use either a bundled or a system-provided version of Botan.
#
# If --with-system-botan is specified and the library cannot be found or is
# unsuitable, the configure script will error out rather than falling back
# to the bundled version.  This is to avoid surprising a user who expected
# their system libbotan to be used.
#
# Currently we accept botan version 1.7.2 and newer, limited to major
# version 1, as for a 2.0 release, probability of an API change in too high.

AC_DEFUN([MTN_LIB_BOTAN],
[AC_ARG_WITH([system-botan],
    AC_HELP_STRING([--with-system-botan],
     [use a system-provided copy of Botan instead of the default bundled
      copy. (To use a specific installed version, use the environment
      variables BOTAN_CPPFLAGS and/or BOTAN_LIBS.)]),
    [case "$withval" in
      ""|yes) with_system_botan=yes ;;
      no)     with_system_botan=no  ;;
      *)      AC_MSG_ERROR([--with(out)-system-botan takes no argument]) ;;
    esac],
   [with_system_botan=no])
 if test "$with_system_botan" = yes; then
   MTN_FIND_BOTAN
 else
   AC_DEFINE([BOTAN_STATIC],[1],[Define if using bundled botan])
   AC_MSG_NOTICE([using the bundled copy of Botan])
 fi
 AM_CONDITIONAL([INCLUDED_BOTAN], [test $with_system_botan = no])
 AC_SUBST([BOTAN_CPPFLAGS])
 AC_SUBST([BOTAN_LIBS])
])

AC_DEFUN([MTN_FIND_BOTAN],
[
  AC_MSG_CHECKING([for Botan version 1.7.2 or newer])
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

    # make sure we have to do with botan major version 1.
    save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $BOTAN_CPPFLAGS"
    AC_PREPROC_IFELSE([
#include <botan/build.h>

#ifndef BOTAN_VERSION_MAJOR
#error Botan didn't define version macros?!?
#endif

#if BOTAN_VERSION_MAJOR > 1
#error Botan major version from the future.
#endif],
    [botan_version_match=yes],
    [botan_version_match=no])
    if test $botan_version_match = no; then
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([system-provided Botan is newer than expected ($BOTAN_VERSION).  Correct your settings or use --with-system-botan=no.])
    fi

    # then also check minor and patch version
    AC_PREPROC_IFELSE([
#include <botan/build.h>

#if BOTAN_VERSION_MAJOR != 1
#error Botan major version mismatch
#endif

#if BOTAN_VERSION_MINOR < 7
#error Botan minor version mismatch
#endif

#if BOTAN_VERSION_MINOR == 7 && BOTAN_VERSION_PATCH < 2
#error Botan patch version mismatch
#endif],
    [botan_version_match=yes],
    [botan_version_match=no])
    if test $botan_version_match = no; then
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([system-provided Botan is too old ($BOTAN_VERSION).  Correct your settings or use --with-system-botan=no.])
    else
      AC_MSG_RESULT([yes])
    fi
    CFLAGS="$save_CFLAGS"
  else
    found_botan=no
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([Botan cannot be found.  Correct your settings or use --with-system-botan=no.])
  fi
])

