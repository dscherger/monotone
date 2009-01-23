# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.58)
AC_INIT([monotone], [0.43dev], [monotone-devel@nongnu.org])
AM_INIT_AUTOMAKE([1.9 tar-ustar std-options])
AC_CONFIG_SRCDIR([app_state.cc])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([util/mtnopt])

# Checks for programs.
dnl Note: AC_PROG_CC is implied by AM_PROG_CC_C_O and in any case has
dnl already been done by AM_INIT_AUTOMAKE.
AM_PROG_CC_C_O
AC_CC_FEXCEPTIONS

# ??? Shouldn't be necessary (anymore).
test "z$GCC" = "zyes" && CFLAGS="$CFLAGS -fno-strict-aliasing"
test "z$GXX" = "zyes" && CXXFLAGS="$CXXFLAGS -fno-strict-aliasing"

AC_PROG_CXX
AC_PROG_CXXCPP
AC_PROG_CXX_WARNINGS
AC_PROG_CXX_PCH

AC_PROG_RANLIB

AM_GNU_GETTEXT([external])
AC_PROG_XGETTEXT_FLAG_OPTION
AM_GNU_GETTEXT_VERSION(0.11.5)
AM_ICONV

# Stick some logic into config.status, after the logic that
# AM_GNU_GETTEXT sticks into config.status, to annul the UPDATEPOFILES
# variable in po/Makefile.  This prevents auto-regeneration of the
# .po files when the sources change, which the translators do not want.
AC_CONFIG_COMMANDS([default-2], [[
for ac_file in $CONFIG_FILES; do
  # Support "outfile[:infile[:infile...]]"
  case "$ac_file" in
    *:*) ac_file=`echo "$ac_file"|sed 's%:.*%%'` ;;
  esac
  # PO directories have a Makefile.in generated from Makefile.in.in.
  case "$ac_file" in */Makefile.in)
    target="`echo x$ac_file | sed -e 's/.in$//' -e 's/^x//'`"
    test -n "$as_me" && echo "$as_me: fixing $target" || echo "fixing $target"
    mtn_tmp="${target}.tmp"
    sed -e 's/^MSGMERGE =.*$/MSGMERGE = false/' \
            -e 's/^MSGMERGE_UPDATE =.*$/MSGMERGE_UPDATE = true/' \
               "$target" > "$mtn_tmp"
    mv -f "$mtn_tmp" "$target" ;;
  esac
done
]], [[]])

# Do library checks as early as possible so we bail out quickly if
# one is missing.

AC_SEARCH_LIBS([deflate], [z], , AC_MSG_FAILURE([zlib is required]))

MTN_FIND_LUA
MTN_FIND_PCRE
MTN_FIND_SQLITE
MTN_FIND_IDNA

AC_LANG([C++])
MTN_FIND_BOTAN
BOOST_VERSION_CHECK
BOOST_VERSION_SPECIFIC_BUGS
MTN_NETXX_DEPENDENCIES

AC_LANG([C])
# C language features

AC_CACHE_CHECK([whether this is Windows],
       ac_win32, [
       AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#ifdef WIN32
                       #include <windows.h>
                       #endif]],
                      [[HANDLE h; DWORD d;]])],
                      [ac_win32=yes],
                      [ac_win32=no])
])
if test "$ac_win32" = "yes"; then
   AM_CONDITIONAL(WIN32_PLATFORM, true)
   AC_DEFINE(os_err_t, unsigned int, [OS-specific error type])
else
   AM_CONDITIONAL(WIN32_PLATFORM, false)
   AC_DEFINE(os_err_t, int, [OS-specific error type])
fi

AC_CACHE_CHECK([whether this is AIX],
       ac_aix, [
       AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#ifndef _AIX
                       #error "This is not AIX"
                       #endif]],
                      [[int i;]])],
                      [ac_aix=yes],
                      [ac_aix=no])
])
if test "$ac_aix" = "yes"; then
   AM_CONDITIONAL(AIX_PLATFORM, true)
   LDFLAGS="$LDFLAGS -Wl,-bexpfull"
else
   AM_CONDITIONAL(AIX_PLATFORM, false)
fi

# Checks for header files.
dnl It's hard these days to get AC_CHECK_HEADERS to check for just one
dnl header which should compile fine with no dependencies.
dnl Using AC_INCLUDES_DEFAULT triggers a whole bunch of tests that are
dnl pointless nowadays.  This is the best thing I can come up with.
dnl (A line with nothing but a # on it is ignored by the preprocessor.)

AC_CHECK_HEADERS([fcntl.h netinet/in.h],,, [#])

AC_LANG([C++])
# This header may not be visible to the C compiler.
AC_CHECK_HEADERS([cxxabi.h],,, [#])

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
AC_FUNC_LOCALE_WORKS
AC_FUNC_ICONV_TRANSLIT

AC_CHECK_FUNCS([__cxa_current_exception_type __cxa_demangle dirfd \
                fstatat mkdtemp mmap putenv setenv strptime unsetenv])

# allow compiling with different flags/optimisation
# for monotone versus libs, useful for testing.
# only makes sense if CFLAGS="" or CXXFLAGS=""
AC_SUBST(MONOTONE_CFLAGS)
AC_SUBST(MONOTONE_CXXFLAGS)
AC_SUBST(LIB3RDPARTY_CFLAGS)
AC_SUBST(LIB3RDPARTY_CXXFLAGS)

AC_OUTPUT
