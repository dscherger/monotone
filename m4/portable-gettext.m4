dnl @synopsis PG_NLS_FRAMEWORK
dnl
dnl @summary provide --disable-nls to the user
dnl
dnl This macro sets $enable_nls to "yes" if user didn't disable NLS support
dnl and provides ENABLE_NLS in config.h
dnl
dnl @category UserOptions
dnl @author Patrick Georgi <patrick@georgi-clan.de>
dnl @version 2007-03-10
dnl @license MIT

AC_DEFUN([PG_NLS_FRAMEWORK],[
   AC_ARG_ENABLE([nls],
     AS_HELP_STRING(
        [--disable-nls],
        [Disable support for translated messages]
     ), , enable_nls=yes)

if test "x$enable_nls" = "xyes"; then
   AC_DEFINE(ENABLE_NLS, 1,
     [Set to 1 if support for translated messages should be compiled in])
fi
])

dnl @synopsis PG_ICONV
dnl
dnl @summary checks for iconv() at various places and in different variants
dnl
dnl This macro sets $ac_cv_func_iconv_exists to "yes" if it can find iconv()
dnl and also sets LIBS (at least it should) and provides HAVE_ICONV to config.h
dnl 
dnl Furthermore it checks if the compiler and library want you to provide a
dnl const char* and sets ICONV_CONST based on that.
dnl
dnl @category InstalledPackages
dnl @author Patrick Georgi <patrick@georgi-clan.de>
dnl @version 2007-03-10
dnl @license MIT

AC_DEFUN([PG_ICONV],
[
if test "x$enable_nls" = "xyes"; then
  AC_CACHE_VAL(
                  ac_cv_func_iconv_exists,
    [AC_SEARCH_LIBS(iconv, [iconv intl],
       ac_cv_func_iconv_exists=yes,
       ac_cv_func_iconv_exists=no
    )]
  )dnl
  if test "x$ac_cv_func_iconv_exists" = "xyes"; then
    AC_DEFINE(HAVE_ICONV, 1, [Defines if iconv() is available])
    AC_CACHE_CHECK([if iconv() needs const argument],
                    ac_cv_func_iconv_const,
     [AC_TRY_COMPILE(
[#include <iconv.h>],
[char* test; iconv(0,&test,0,0,0);],
ac_cv_func_iconv_const=no,
          [AC_TRY_COMPILE(
[#include <iconv.h>],
[const char* test; iconv(0,&test,0,0,0);],
ac_cv_func_iconv_const=yes
          )]
      )
     ])
     if test "x$ac_cv_func_iconv_const" = "xyes"; then
        AC_DEFINE(ICONV_CONST, const, [ Defines if iconv needs const argument ])
     fi
  fi
fi
])

dnl @synopsis PG_GETTEXT
dnl
dnl @summary looks for various gettext() related functions
dnl
dnl
dnl @category InstalledPackages
dnl @author Patrick Georgi <patrick@georgi-clan.de>
dnl @version 2007-03-10
dnl @license MIT

AC_DEFUN([PG_GETTEXT],
[
if test "x$enable_nls" = "xyes"; then
  AC_CACHE_VAL(
                  ac_cv_func_gettext_exists,
    [AC_SEARCH_LIBS(gettext, [gettext iconv intl],
       ac_cv_func_gettext_exists=yes,
       ac_cv_func_gettext_exists=no
    )]
  )dnl
  if test "x$ac_cv_func_gettext_exists" = "xyes"; then
    AC_DEFINE(HAVE_GETTEXT, 1, [Defines if gettext() is available])
  fi
  AC_CACHE_VAL(
                  ac_cv_func_dcgettext_exists,
    [AC_SEARCH_LIBS(dcgettext, [gettext iconv intl],
       ac_cv_func_dcgettext_exists=yes,
       ac_cv_func_dcgettext_exists=no
    )]
  )dnl
  if test "x$ac_cv_func_dcgettext_exists" = "xyes"; then
    AC_DEFINE(HAVE_DCGETTEXT, 1, [Defines if dcgettext() is available])
  fi
fi
])
