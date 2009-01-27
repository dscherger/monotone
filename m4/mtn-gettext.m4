# This is a custom set of probe macros for xgettext and the gettext
# library (either in libc or as -lintl [-liconv]).  The differences
# from the standard version are as follows:
#
#  - Library tests work correctly under AC_LANG([C++]).
#  - Library tests have far fewer dependencies.
#  - Does not require use of autopoint.
#  - Does not use a Makefile in the po/ directory.
#    (Instead, sets AM_CONDITIONALs; must be paired with appropriate
#     Makefile.am logic.)
#  - Does not support bundled libintl/libiconv.
#  - Always checks for ngettext().
#  - Always checks for the --flag option to xgettext.
#  - Never checks for support for <inttypes.h> format string macros.
#  - If you give an explicit --enable-nls on the command line and the
#    library functions are not available, configure will error out.
#  - Does not use POTFILES.in.  (Does use the LINGUAS file, as this is
#    a sensible way of suppressing out-of-date translations.)

AC_DEFUN([MTN_GNU_GETTEXT], [
AC_ARG_VAR([LINGUAS], [List of languages to install message translations for, if available.  (Default is to install everything available.)])
AC_ARG_VAR([intl_CFLAGS], [override C compiler flags for GNU libintl])
AC_ARG_VAR([intl_LIBS], [override linker flags for GNU libintl])

AC_ARG_ENABLE([nls],
  [AS_HELP_STRING([--disable-nls],
		  [do not use user's native language for program messages])],
  [USE_NLS=$enableval],
  [USE_NLS=auto])

MTN_PROG_GNU_GETTEXT
MTN_LIB_GNU_GETTEXT
MTN_VARS_GNU_GETTEXT])

# Probe for gettext and related programs.
# This happens whether or not NLS is disabled, because it's needed for
# 'make dist'.
AC_DEFUN([MTN_PROG_GNU_GETTEXT], [
  dnl The first test excludes Solaris msgfmt and early GNU msgfmt versions.
  dnl The second test excludes FreeBSD msgfmt.
  AC_CACHE_CHECK([for GNU msgfmt], [ac_cv_path_gnu_msgfmt],
    [AC_PATH_PROGS_FEATURE_CHECK([gnu_msgfmt], [msgfmt gmsgfmt],
      [[if "$ac_path_gnu_msgfmt" --statistics /dev/null >/dev/null 2>&1; then
	  if "$ac_path_gnu_msgfmt" --statistics /dev/null 2>&1 >/dev/null |
		 grep usage >/dev/null; then
	    : # no good
	  else
	    ac_cv_path_gnu_msgfmt="$ac_path_gnu_msgfmt"
	    ac_path_gnu_msgfmt_found=:
	  fi
	fi]],
      [ac_cv_path_gnu_msgfmt="not found"])])
  AC_SUBST([MSGFMT], [$ac_cv_path_gnu_msgfmt])

  AC_CACHE_CHECK([for GNU msgmerge], [ac_cv_path_gnu_msgmerge],
     [AC_PATH_PROGS_FEATURE_CHECK([gnu_msgmerge], [msgmerge gmsgmerge],
	[[if "$ac_path_gnu_msgmerge" --update -q /dev/null /dev/null \
		 >/dev/null 2>&1; then
	    ac_cv_path_gnu_msgmerge="$ac_path_gnu_msgmerge"
	    ac_path_gnu_msgmerge_found=:
	  fi]],
	[ac_cv_path_gnu_msgmerge="not found"])])
  AC_SUBST([MSGMERGE], [$ac_cv_path_gnu_msgmerge])

  dnl The first test excludes Solaris xgettext and early GNU xgettext versions.
  dnl The second test excludes FreeBSD xgettext.
  AC_CACHE_CHECK([for GNU xgettext], [ac_cv_path_gnu_xgettext],
    [AC_PATH_PROGS_FEATURE_CHECK([gnu_xgettext], [xgettext gxgettext],
       [[if "$ac_path_gnu_xgettext" --omit-header --copyright-holder= \
		/dev/null >/dev/null 2>&1; then
	   if "$ac_path_gnu_xgettext" --omit-header --copyright-holder= \
		/dev/null 2>&1 >/dev/null | grep usage >/dev/null; then
	     : # no good
	   else
	     ac_cv_path_gnu_xgettext="$ac_path_gnu_xgettext"
	     ac_path_gnu_xgettext_found=:
	   fi
	 fi]],
       [ac_cv_path_gnu_xgettext="not found"])])
  AC_SUBST([XGETTEXT], [$ac_cv_path_gnu_xgettext])

  dnl Remove leftover from FreeBSD xgettext call.
  rm -f messages.po

  if test x"$XGETTEXT" != x"not found"; then
    AC_CACHE_CHECK([whether $XGETTEXT supports --flag],
		   [ac_cv_prog_xgettext_flag_option],
      [echo 'int main(void) { return 0; }' >> conftest.c
      if "$XGETTEXT" --flag printf:1:c-format -o conftest.po conftest.c \
	 >/dev/null 2>&1
      then ac_cv_prog_xgettext_flag_option=yes
      else ac_cv_prog_xgettext_flag_option=no
      fi])

    # The variable names here are chosen for compatibility with
    # intltool-update; see po/Makevars.
    if test $ac_cv_prog_xgettext_flag_option = yes; then
      XGETTEXT_OPTS='$(XGETTEXT_OPTIONS)'
    else
      XGETTEXT_OPTS='$(XGETTEXT_OPTIONS_NO_FLAG)'
    fi
  else
    XGETTEXT_OPTS=
  fi
  AC_SUBST([XGETTEXT_OPTS])

  if test x"$MSGFMT" = x"not found" ||
     test x"$MSGMERGE" = x"not found" ||
     test x"$XGETTEXT" = x"not found"; then
    REBUILD_NLS=false
  else
    REBUILD_NLS=true
  fi
  AM_CONDITIONAL([REBUILD_NLS], [$REBUILD_NLS])
])

# Probe for NLS functions in libc, then in libintl, and throw
# in libiconv if needed.  These tests are skipped when --disable-nls is
# given.  If --enable-nls was given explicitly, we bomb out if we can't
# find the libraries.
AC_DEFUN([MTN_LIB_GNU_GETTEXT], [
if test x"$USE_NLS" != xno; then
  dnl This is not the same test that gettext.m4 does, so we do not use
  dnl the same cache variables.  In particular it does not look for any
  dnl _nl_* symbols.
  AC_CACHE_CHECK([for libraries containing GNU gettext and ngettext],
		 [mtn_cv_lib_gnu_gettext_ngettext],
   [# If the user specified the libraries we're supposed to use, test
    # those and no others.
    if test x"${intl_LIBS+set}" = xset; then
      set fnord "${intl_LIBS}"
    else
      set fnord '' '-lintl' '-lintl -liconv'
    fi
    shift

    mtn_cv_lib_gnu_gettext_ngettext="not found"
    save_LIBS="$LIBS"
    save_CFLAGS="$CFLAGS"
    CFLAGS="$save_CFLAGS $intl_CFLAGS"
    for try in "$@"; do
      LIBS="$save_LIBS $try"
      AC_LINK_IFELSE([AC_LANG_PROGRAM([#include <libintl.h>], [[
	const char *a = textdomain("");
	const char *b = bindtextdomain("", "");
	const char *c = gettext("");
	const char *d = ngettext("", "", 0);
	return a && b && c && d;
	]])],
	[mtn_cv_lib_gnu_gettext_ngettext="${try:-none required}"
	 break])
    done
    CFLAGS="$save_CFLAGS"
    LIBS="$save_LIBS"
   ])

   if test x"$mtn_cv_lib_gnu_gettext_ngettext" = x"not found"; then
     if test x"$USE_NLS" = xyes; then
       AC_MSG_FAILURE([no usable NLS libraries found])
     else
       intl_LIBS=""
       USE_NLS=no
     fi
   else
     USE_NLS=yes
     intl_LIBS="$mtn_cv_lib_gnu_gettext_ngettext"
     if test x"$intl_LIBS" = x"none required"; then
       intl_LIBS=""
     fi
   fi
fi
if test $USE_NLS = yes; then
  AC_DEFINE(ENABLE_NLS, 1,
    [Define to 1 if program messages should use the user's native language.])
fi
AM_CONDITIONAL(USE_NLS, [test x$USE_NLS = xyes])
])

# Logic to set Makefile variables that depend on what language translations
# are available.
AC_DEFUN([MTN_VARS_GNU_GETTEXT], [
  GOOD_LINGUAS=
  if test -f "$srcdir/po/LINGUAS"; then
    GOOD_LINGUAS=`sed -e '/^#/d' $srcdir/po/LINGUAS | tr "$as_nl" " "`
  fi

  if test x"$LINGUAS" = x; then
    INST_LINGUAS="$GOOD_LINGUAS"
  else
    INST_LINGUAS=
    for candidate in $GOOD_LINGUAS; do
      for desired in $LINGUAS; do
	 case "$desired" in "$candidate"*)
	   INST_LINGUAS="$INST_LINGUAS $candidate"
	   break ;;
	 esac
      done
    done
  fi
  AC_SUBST(INST_LINGUAS)
])

# pre autoconf 2.62 support; fallback code lifted directly from autoconf 2.62
m4_ifndef([AC_PATH_PROGS_FEATURE_CHECK], [
m4_define([_AC_PATH_PROGS_FEATURE_CHECK],
[if test -z "$$1"; then
  ac_path_$1_found=false
  # Loop through the user's path and test for each of PROGNAME-LIST
  _AS_PATH_WALK([$5],
  [for ac_prog in $2; do
    for ac_exec_ext in '' $ac_executable_extensions; do
      ac_path_$1="$as_dir/$ac_prog$ac_exec_ext"
      AS_EXECUTABLE_P(["$ac_path_$1"]) || continue
$3
      $ac_path_$1_found && break 3
    done
  done])dnl
  if test -z "$ac_cv_path_$1"; then
    m4_default([$4],
      [AC_MSG_ERROR([no acceptable m4_bpatsubst([$2], [ .*]) could be dnl
found in m4_default([$5], [\$PATH])])])
  fi
else
  ac_cv_path_$1=$$1
fi
])
m4_define([AC_PATH_PROGS_FEATURE_CHECK],
[_$0([$1], [$2], [$3], m4_default([$4], [:]), [$5])dnl
])
])
