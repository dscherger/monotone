# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.58)
AC_INIT([monotone], [0.43dev], [monotone-devel@nongnu.org])
AM_INIT_AUTOMAKE([1.9 tar-ustar std-options])
AC_CONFIG_SRCDIR([app_state.cc])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile util/mtnopt])
AC_LANG([C++])

dnl AM_INIT_AUTOMAKE only provides $(MKDIR_P) in 1.10 and later.
dnl No, AC_PROG_MKDIR_P does not AC_SUBST its result variable, at least
dnl not in 2.61.  Comments in programs.m4 suggest that it was meant to
dnl but not properly implemented.
if test x"$MKDIR_P" = x; then
  if test x"$mkdir_p" != x; then
    MKDIR_P="$mkdir_p"
  else
    AC_PROG_MKDIR_P
  fi
fi
AC_SUBST([MKDIR_P])

# Operating system categorization.  Don't add more entries to this
# case statement if you can possibly avoid it.
AC_CANONICAL_HOST
AC_MSG_CHECKING([category of operating system])

WIN32_PLATFORM=false
os_err_t="int"
case "$host" in
  *-mingw32 | *-pc-pw32 | *-pc-mks | *-winnt )
    AC_MSG_RESULT([Windows])
    WIN32_PLATFORM="true"
    os_err_t="unsigned int"
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
AC_CXX_TYPEOF
AC_CXX_EXTERN_TEMPLATE
AC_CXX_GNUCXX_HASHMAP
AC_CXX_STLPORT_HASHMAP
AC_CXX_TR1_UNORDERED_MAP
AC_CXX_TR1_UNORDERED_MAP_CONST_CORRECT
AC_CXX_SYNC_WITH_STDIO_WORKS
AC_CXX_TEMPLATE_STATIC_CONST_BUG

# Checks for typedefs and structures.
MTN_NUMERIC_VOCAB

dnl we don't use the stock AC_TYPE_* macros because of the AC_INCLUDES_DEFAULT
dnl problem described above.

AC_CHECK_TYPE([off_t],
  [],
  [AC_DEFINE([off_t], [long int],
	     [Define to `long int' if <sys/types.h> does not define.])],
  [#include <sys/types.h>])

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
