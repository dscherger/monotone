AC_DEFUN([MTN_FIND_PCRE],
[  # We manually test the variables here because we want them to work
   # even if pkg-config isn't installed.  The use of + instead of :+ is
   # deliberate; the user should be able to tell us that the empty string
   # is the correct set of flags.  (PKG_CHECK_MODULES gets this wrong!)
   if test -n "${pcre_CFLAGS+set}" || test -n "${pcre_LIBS+set}"; then
     found_libpcre=yes
   else
     PKG_CHECK_MODULES([pcre], [libpcre],
                       [found_libpcre=yes], [found_libpcre=no])
   fi

   if test $found_libpcre = no; then
     # try pcre-config, in case we're on a system with no pkg-config
     AC_MSG_CHECKING([for pcre using pcre-config])
     if test -n "`type -p pcre-config`"; then
       pcre_CFLAGS="`pcre-config --cflags`"
       pcre_LIBS="`pcre-config --libs`"
       found_libpcre=yes
       AC_MSG_RESULT([yes])
     fi
   fi

   if test $found_libpcre = no; then
     AC_MSG_RESULT([no; guessing])
     pcre_CFLAGS=
     pcre_LIBS=-lpcre
   fi

   # AC_MSG_NOTICE([using pcre compile flags: "$pcre_CFLAGS"])
   # AC_MSG_NOTICE([using pcre link flags: "$pcre_LIBS"])

   # Wherever we got the settings from, make sure they work.
   AC_CACHE_CHECK([whether the pcre library is usable], ac_cv_lib_pcre_works,
    [save_LIBS="$LIBS"
     save_CPPFLAGS="$CPPFLAGS"
     LIBS="$LIBS $pcre_LIBS"
     CPPFLAGS="$CPPFLAGS $pcre_CFLAGS"
     AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [#include <pcre.h>],
      [const char *e;
       int dummy;
       int o;
       /* Make sure some definitions are present. */
       dummy = PCRE_NEWLINE_CR;
       dummy = PCRE_DUPNAMES;
       pcre *re = pcre_compile("foo", 0, &e, &o, 0);])],
      [ac_cv_lib_pcre_works=yes], [ac_cv_lib_pcre_works=no])
     LIBS="$save_LIBS"
     CPPFLAGS="$save_CPPFLAGS"])
   if test $ac_cv_lib_pcre_works = no; then
      AC_MSG_ERROR([Your pcre library is not usable.])
   fi

   # This is deliberately not cached.
   AC_MSG_CHECKING([whether the pcre library is new enough])
   sed -n -e 's/#define REQUIRED_PCRE_MAJOR[ 	]*/#define REQUIRED_PCRE_MAJOR /p' \
          -e 's/#define REQUIRED_PCRE_MINOR[ 	]*/#define REQUIRED_PCRE_MINOR /p' \
          $srcdir/pcrewrap.hh > conftest.h
   save_CPPFLAGS="$CPPFLAGS"
   CPPFLAGS="$CPPFLAGS $pcre_CFLAGS"
   AC_PREPROC_IFELSE([
#include "conftest.h"
#include <pcre.h>
#if PCRE_MAJOR < REQUIRED_PCRE_MAJOR || \
    (PCRE_MAJOR == REQUIRED_PCRE_MAJOR && PCRE_MINOR < REQUIRED_PCRE_MINOR)
#error out of date
#endif],
   [pcre_version_match=yes],
   [pcre_version_match=no])
   AC_MSG_RESULT($pcre_version_match)
   CPPFLAGS="$save_CPPFLAGS"

   if test $pcre_version_match = no; then
      AC_MSG_ERROR([Your pcre library is too old, please upgrade it.])
   fi
])
