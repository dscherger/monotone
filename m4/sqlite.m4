AC_DEFUN([MTN_FIND_SQLITE],
[  PKG_PROG_PKG_CONFIG

   # We manually test the variables here because we want them to work
   # even if pkg-config isn't installed.  The use of + instead of :+ is
   # deliberate; the user should be able to tell us that the empty string
   # is the correct set of flags.  (PKG_CHECK_MODULES gets this wrong!)
   if test -n "${SQLITE3_CFLAGS+set}" || test -n "${SQLITE3_LIBS+set}"; then
     found_sqlite3=yes
   else
     PKG_CHECK_MODULES([SQLITE3], [sqlite3],
                       [found_sqlite3=yes], [found_sqlite3=no])
   fi

   if test $found_sqlite3 = no; then
     AC_MSG_RESULT([no; guessing])
     AC_CHECK_LIB([sqlite3], [sqlite_open], 
                  [SQLITE3_LIBS=-lsqlite3])
     SQLITE3_CFLAGS=
   fi

   SQLITE3_CPPFLAGS="$SQLITE3_CFLAGS"

    # AC_MSG_NOTICE([using sqlite3 compile flags: "$SQLITE3_CPPFLAGS"])
    # AC_MSG_NOTICE([using sqlite3 link flags: "$SQLITE3_LIBS"])

   AC_SUBST(SQLITE3_CPPFLAGS)
   AC_SUBST(SQLITE3_LIBS)

   AC_CACHE_CHECK([whether the sqlite3 library is usable], ac_cv_lib_sqlite3_works,
    [save_LIBS="$LIBS"
     save_CPPFLAGS="$CPPFLAGS"
     LIBS="$LIBS $SQLITE3_LIBS"
     CPPFLAGS="$CPPFLAGS $SQLITE3_CPPFLAGS"
     AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [
extern "C"
{
  #include <sqlite3.h>
}
      ],
      [
sqlite3 *st;

#if SQLITE_VERSION_NUMBER < 3003000
#error Sqlite3 version mismatch
#endif

sqlite3_open("testfile.db", &st);
sqlite3_close(st);
      ])],
      [ac_cv_lib_sqlite3_works=yes], [ac_cv_lib_sqlite3_works=no])
     LIBS="$save_LIBS"
     CPPFLAGS="$save_CPPFLAGS"])
   if test $ac_cv_lib_sqlite3_works = no; then
      AC_MSG_ERROR([Your sqlite3 library is not usable.])
   fi

])

