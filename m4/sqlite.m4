AC_DEFUN([MTN_FIND_SQLITE],
[  # We manually test the variables here because we want them to work
   # even if pkg-config isn't installed.  The use of + instead of :+ is
   # deliberate; the user should be able to tell us that the empty string
   # is the correct set of flags.  (PKG_CHECK_MODULES gets this wrong!)
   if test -n "${sqlite3_CFLAGS+set}" || test -n "${sqlite3_LIBS+set}"; then
     found_sqlite3=yes
   else
     PKG_CHECK_MODULES([sqlite3], [sqlite3],
                       [found_sqlite3=yes], [found_sqlite3=no])
   fi

   if test $found_sqlite3 = no; then
     AC_MSG_RESULT([no; guessing])
     sqlite3_CFLAGS=
     sqlite3_LIBS=-lsqlite3
   fi

   # AC_MSG_NOTICE([using sqlite3 compile flags: "$sqlite3_CFLAGS"])
   # AC_MSG_NOTICE([using sqlite3 link flags: "$sqlite3_LIBS"])

   AC_LANG_ASSERT([C])
   AC_CACHE_CHECK([whether the sqlite3 library is usable],
   	          ac_cv_lib_sqlite3_works,
    [save_LIBS="$LIBS"
     save_CPPFLAGS="$CPPFLAGS"
     LIBS="$LIBS $sqlite3_LIBS"
     CPPFLAGS="$CPPFLAGS $sqlite3_CFLAGS"
     AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [#include <sqlite3.h>],
[sqlite3 *st;

#if SQLITE_VERSION_NUMBER < 3003000
#error Sqlite3 too old
#endif

sqlite3_open("testfile.db", &st);
sqlite3_close(st);])],
      [ac_cv_lib_sqlite3_works=yes], [ac_cv_lib_sqlite3_works=no])
     LIBS="$save_LIBS"
     CPPFLAGS="$save_CPPFLAGS"])
   if test $ac_cv_lib_sqlite3_works = no; then
      AC_MSG_ERROR([Your sqlite3 library is not usable.])
   fi
])
