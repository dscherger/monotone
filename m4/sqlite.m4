# This is a separate macro primarily to trick autoconf into not looking
# for pkg-config.
AC_DEFUN([MTN_FIND_SQLITE],
[  PKG_PROG_PKG_CONFIG

   # We manually test the variables here because we want them to work
   # even if pkg-config isn't installed.  The use of + instead of :+ is
   # deliberate; the user should be able to tell us that the empty string
   # is the correct set of flags.  (PKG_CHECK_MODULES gets this wrong!)
   if test -n "${SQLITE_CFLAGS+set}" || test -n "${SQLITE_LIBS+set}"; then
     found_libsqlite=yes
   else
     PKG_CHECK_MODULES([SQLITE], [sqlite3],
                       [found_libsqlite=yes], [found_libsqlite=no])
   fi

   if test $found_libsqlite = no; then
     AC_MSG_RESULT([no; guessing])
     AC_CHECK_LIB([sqlite3], [sqlite3_exec], 
                  [SQLITE_LIBS=-lsqlite3], 
                  [SQLITE_LIBS=])
     SQLITE_CFLAGS=
   fi

   # Wherever we got the settings from, make sure they work.
   SQLITE_CFLAGS="`echo :$SQLITE_CFLAGS | sed -e 's/^:@<:@	 @:>@*//; s/@<:@	 @:>@*$//'`"
   SQLITE_LIBS="`echo :$SQLITE_LIBS | sed -e 's/^:@<:@	 @:>@*//; s/@<:@	 @:>@*$//'`"

   AC_MSG_NOTICE([using sqlite compile flags: "$SQLITE_CFLAGS"])
   AC_MSG_NOTICE([using sqlite link flags: "$SQLITE_LIBS"])

   AC_CACHE_CHECK([whether the sqlite library is usable], ac_cv_lib_sqlite_works,
    [save_LIBS="$LIBS"
     save_CFLAGS="$CFLAGS"
     LIBS="$LIBS $SQLITE_LIBS"
     CFLAGS="$CFLAGS $SQLITE_CFLAGS"
     CPPFLAGS="$CFLAGS $SQLITE_CFLAGS"
     AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [
extern "C"
{
  #include <sqlite3.h>
}
      ],
      [

#if SQLITE_VERSION_NUMBER != 3005009
#error Sqlite version mismatch
#endif

      int v = sqlite3_libversion_number();
      ])],
      [ac_cv_lib_sqlite_works=yes], [ac_cv_lib_sqlite_works=no])
     LIBS="$save_LIBS"
     CFLAGS="$save_CFLAGS"])
   if test $ac_cv_lib_sqlite_works = no; then
      AC_MSG_ERROR([Your sqlite library is not usable.])
   fi

])
