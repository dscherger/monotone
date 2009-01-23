AC_DEFUN([MTN_FIND_IDNA],
[  # We manually test the variables here because we want them to work
   # even if pkg-config isn't installed.  The use of + instead of :+ is
   # deliberate; the user should be able to tell us that the empty string
   # is the correct set of flags.  (PKG_CHECK_MODULES gets this wrong!)
   if test -n "${libidn_CFLAGS+set}" || test -n "${libidn_LIBS+set}"; then
     found_libidn=yes
   else
     PKG_CHECK_MODULES([libidn], [libidn],
                       [found_libidn=yes], [found_libidn=no])
   fi

   if test $found_libidn = no; then
     AC_MSG_RESULT([no; guessing])
     libidn_CFLAGS=
     libidn_LIBS=-lidn
   fi

   # AC_MSG_NOTICE([using libidn compile flags: "$libidn_CFLAGS"])
   # AC_MSG_NOTICE([using libidn link flags: "$libidn_LIBS"])

   AC_LANG_ASSERT([C])
   AC_CACHE_CHECK([whether the libidn library is usable],
                  ac_cv_lib_libidn_works,
    [save_LIBS="$LIBS"
     save_CPPFLAGS="$CPPFLAGS"
     LIBS="$LIBS $libidn_LIBS"
     CPPFLAGS="$CPPFLAGS $libidn_CFLAGS"
     AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [#include <idna.h>],
      [const char * error = idna_strerror(IDNA_SUCCESS);])],
      [ac_cv_lib_libidn_works=yes], [ac_cv_lib_libidn_works=no])
     LIBS="$save_LIBS"
     CPPFLAGS="$save_CPPFLAGS"])
   if test $ac_cv_lib_libidn_works = no; then
      AC_MSG_ERROR([Your libidn library is not usable.])
   fi
])
