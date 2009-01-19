AC_DEFUN([MTN_FIND_IDNA],
[  PKG_PROG_PKG_CONFIG

   # We manually test the variables here because we want them to work
   # even if pkg-config isn't installed.  The use of + instead of :+ is
   # deliberate; the user should be able to tell us that the empty string
   # is the correct set of flags.  (PKG_CHECK_MODULES gets this wrong!)
   if test -n "${LIBIDN_CPPFLAGS+set}" || test -n "${LIBIDN_LIBS+set}"; then
     found_libidn=yes
   else
     PKG_CHECK_MODULES([LIBIDN], [libidn],
                       [found_libidn=yes], [found_libidn=no])

     if test $found_libidn = yes; then
       # PKG_CHECK_MODULES adds LIBIDN_CFLAGS, but we want LIBIDN_CPPFLAGS
       LIBIDN_CPPFLAGS="$LIBIDN_CFLAGS"
     fi
   fi

   if test $found_libidn = no; then
     AC_MSG_RESULT([no; guessing])
     AC_CHECK_LIB([libidn], [idna_strerror], 
                  [LIBIDN_LIBS=-lidn])
     LIBIDN_CPPFLAGS=
   fi

    # AC_MSG_NOTICE([using libidn compile flags: "$LIBIDN_CPPFLAGS"])
    # AC_MSG_NOTICE([using libidn link flags: "$LIBIDN_LIBS"])

   AC_SUBST(LIBIDN_CPPFLAGS)
   AC_SUBST(LIBIDN_LIBS)

   AC_CACHE_CHECK([whether the libidn library is usable], ac_cv_lib_libidn_works,
    [save_LIBS="$LIBS"
     save_CPPFLAGS="$CPPFLAGS"
     LIBS="$LIBS $LIBIDN_LIBS"
     CPPFLAGS="$CPPFLAGS $LIBIDN_CPPFLAGS"
     AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [
extern "C"
{
  #include <idna.h>
}
      ],
      [
const char * error = idna_strerror(IDNA_SUCCESS);
      ])],
      [ac_cv_lib_libidn_works=yes], [ac_cv_lib_libidn_works=no])
     LIBS="$save_LIBS"
     CPPFLAGS="$save_CPPFLAGS"])
   if test $ac_cv_lib_libidn_works = no; then
      AC_MSG_ERROR([Your libidn library is not usable.])
   fi

])

