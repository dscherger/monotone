# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.58)
AC_INIT([monotone], [0.43dev], [monotone-devel@nongnu.org])
AM_INIT_AUTOMAKE([1.9 tar-ustar std-options])
AC_CONFIG_SRCDIR([app_state.cc])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile po/Makefile.in util/mtnopt])
AC_LANG([C++])

# Operating system categorization.  Don't add more entries to this
# case statement if you can possibly avoid it.
AC_CANONICAL_HOST
AC_MSG_CHECKING([category of operating system])

case "$host" in 
  # Unclear what should be done with Interix and Cygwin.
  # The old test would have succeeded on both so I'm including both.
  *-mingw32 | *-pc-pw32 | *-pc-mks | *-winnt | *-cygwin | *-interix )
    AC_MSG_RESULT([Windows])

    AM_CONDITIONAL(WIN32_PLATFORM, true)
    AC_DEFINE(os_err_t, unsigned int, 
             [Define as the best type to hold an OS error code.])
  ;;

  *-aix* )
    AC_MSG_RESULT([AIX])

    AM_CONDITIONAL(WIN32_PLATFORM, false)
    AC_DEFINE(os_err_t, int, 
             [Define as the best type to hold an OS error code.])
    LDFLAGS="$LDFLAGS -Wl,-bexpfull"
  ;;

  * )
    AC_MSG_RESULT([Unix])

    AM_CONDITIONAL(WIN32_PLATFORM, false)
    AC_DEFINE(os_err_t, int, 
             [Define as the best type to hold an OS error code.])
  ;;
esac

# Checks for programs.
AC_PROG_CXX
AC_PROG_CXX_WARNINGS
AC_PROG_CXX_PCH

AC_PROG_RANLIB

dnl Note: the gettext checks do not work if AC_LANG(C++) is in effect,
dnl due to the C++ compiler objecting to the very sloppy code that is
dnl used to generate link-time references to gettext functions.  This
dnl is the only remaining use of the C compiler in the entire package.
dnl Note 2: if you *don't* put AM_ICONV in here then AM_GNU_GETTEXT
dnl thinks it needs to stick -liconv in LIBS, even when iconv is in libc!

AC_LANG([C])
AM_ICONV
AM_GNU_GETTEXT([external], [need-ngettext])
AM_GNU_GETTEXT_VERSION(0.11.5)
AC_PROG_XGETTEXT_FLAG_OPTION
AC_LANG([C++])

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
             [Define to `long int' if <sys/types.h does not define.])],
  [#include <sys/types.h>])

AC_CHECK_TYPE([pid_t],
  [],
  [AC_DEFINE([pid_t], [int],
             [Define to `int' if <sys/types.h does not define.])],
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
