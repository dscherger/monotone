# Copyright (C) 2003 Graydon Hoare <graydon@pobox.com>
#               2014 Markus Wanner <markus@bluegap.ch>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.58)
AC_INIT([monotone], [1.2dev], [https://code.monotone.ca/p/monotone/issues/], ,
	[http://www.monotone.ca])
AM_INIT_AUTOMAKE([1.9 tar-ustar dist-bzip2 no-dist-gzip std-options])
AC_CONFIG_SRCDIR([src/app_state.cc])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile doc/Makefile])
AC_LANG([C++])

dnl We want to take advantage of automake 1.11's optional "quiet build"
dnl mode if it's available, and we want to set up our own custom rules
dnl to be quiet too, but this means we need $(AM_DEFAULT_VERBOSITY) to
dnl be defined even when an older automake is in use.
m4_ifdef([AM_SILENT_RULES],
  [AM_SILENT_RULES],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST([AM_DEFAULT_VERBOSITY])])

dnl AM_INIT_AUTOMAKE only provides $(MKDIR_P) in 1.10 and later.
dnl No, AC_PROG_MKDIR_P does not AC_SUBST its result variable, at least
dnl not in 2.61.  Comments in programs.m4 suggest that it was meant to
dnl but not properly implemented.
AC_PROG_MKDIR_P
if test x"$MKDIR_P" = x && test x"$mkdir_p" != x; then
  MKDIR_P="$mkdir_p"
fi
AC_SUBST([MKDIR_P])

BUILD_DATE=`date '+%F'`
AC_SUBST([BUILD_DATE])
AC_DEFINE_UNQUOTED([BUILD_DATE], ["$BUILD_DATE"],
	[The date of the build, used in the manual page and other places.])

# Operating system categorization.  Don't add more entries to this
# case statement if you can possibly avoid it.
AC_CANONICAL_HOST
AC_MSG_CHECKING([category of operating system])

WIN32_PLATFORM=false
os_err_t="int"
case "$host" in
  *-mingw32 | *-msys | *-pc-pw32 | *-pc-mks | *-winnt )
    AC_MSG_RESULT([Windows])
    WIN32_PLATFORM="true"
    os_err_t="unsigned int"
    ac_executable_extensions=".exe"
    
    # Inno setup configuration
    AC_CONFIG_FILES([monotone.iss:innosetup/monotone.iss.in])
    AC_PATH_PROG(ISCC, [ISCC]) 
  ;;

  *-ibm-aix* )
    AC_MSG_RESULT([AIX])
    LDFLAGS="$LDFLAGS -Wl,-bexpfull"
  ;;

  * )
    AC_MSG_RESULT([Unix])
  ;;
esac
AM_CONDITIONAL(WIN32_PLATFORM, $WIN32_PLATFORM)
AC_DEFINE_UNQUOTED(os_err_t, $os_err_t,
           [Define as the best type to hold an OS error code.])


# Checks for programs.
AC_PROG_CXX
AC_PROG_CXX_WARNINGS
AC_PROG_CXX_PCH

dnl needed for 'txt2c'
dnl There ought to be an AC_PROG_CXX_FOR_BUILD, but there isn't.
AC_ARG_VAR([CXX_FOR_BUILD],
       [C++ compiler command for programs to run on build machine])
if test $cross_compiling = yes; then
  CXX_FOR_BUILD='c++'
else
  CXX_FOR_BUILD="$CXX"
fi

AC_PROG_RANLIB

MTN_GNU_GETTEXT

# Do library checks as early as possible so we bail out quickly if
# one is missing.

AC_SEARCH_LIBS([deflate], [z], , AC_MSG_FAILURE([zlib is required]))

BOOST_VERSION_CHECK
BOOST_VERSION_SPECIFIC_BUGS

MTN_FIND_BOTAN
MTN_FIND_IDNA
MTN_FIND_LUA
MTN_FIND_PCRE
MTN_FIND_SQLITE

MTN_NETXX_DEPENDENCIES

# Checks for header files.
dnl It's hard these days to get AC_CHECK_HEADERS to check for just one
dnl header which should compile fine with no dependencies.
dnl Using AC_INCLUDES_DEFAULT triggers a whole bunch of tests that are
dnl pointless nowadays.  This is the best thing I can come up with.
dnl (A line with nothing but a # on it is ignored by the preprocessor.)

AC_CHECK_HEADERS([cxxabi.h fcntl.h netinet/in.h],,, [#])

# check for language features and compiler bugs
AX_CXX_COMPILE_STDCXX_11(, mandatory)
AC_CXX_TEMPLATE_STATIC_CONST_BUG
AC_CXX_SYNC_WITH_STDIO_WORKS
AC_CXX_MKTIME_64BIT_WORKS

# Checks for typedefs and structures.
MTN_NUMERIC_VOCAB

dnl we don't use the stock AC_TYPE_* macros because of the AC_INCLUDES_DEFAULT
dnl problem described above.

AC_CHECK_TYPE([off_t],
  [],
  [AC_DEFINE([off_t], [long int],
         [Define to `long int' if <sys/types.h> does not define.])],
  [#undef __STRICT_ANSI__
   #include <sys/types.h>
  ])

AC_CHECK_TYPE([pid_t],
  [],
  [AC_DEFINE([pid_t], [int],
         [Define to `int' if <sys/types.h> does not define.])],
  [#include <sys/types.h>])

AC_CHECK_MEMBERS([struct stat.st_ctim.tv_nsec,
          struct stat.st_mtim.tv_nsec,
          struct stat.st_ctimespec.tv_nsec,
          struct stat.st_mtimespec.tv_nsec,
          struct stat.st_ctimensec,
          struct stat.st_mtimensec],
          [], [], [#include <sys/stat.h>])
AC_CHECK_MEMBERS([struct dirent.d_type], [], [], [#include <dirent.h>])

# Checks for library functions.
# We don't do anything especially clever with mmap so we don't need the
# complicated check for it.

AC_CHECK_FUNCS([__cxa_current_exception_type __cxa_demangle dirfd \
        fstatat mkdtemp mmap putenv setenv strptime unsetenv])

AC_OUTPUT
