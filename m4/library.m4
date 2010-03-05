# Copyright (C) 2008 Zack Weinberg <zackw@panix.com>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# Encapsulation of all the logic required to probe for monotone's
# dependent libraries.

# This is more complicated than it ought to be because (a) we can
# assume that each of these libraries installs either a .pc file or a
# *-config executable, but which one may vary across versions and
# installations, and (b) we can't count on the .pc file, or the
# *-config executable, to have the same name in all versions or
# installations!

# All is not lost, however; we _can_ count on the .pc file or *-config
# executable to have a basename that matches the Perl regular
# expression /^(lib)?\Q${libname}\E(-?[0-9.]+)?(\.pc|-config)$/i,
# where ${libname} is the short name of the library.  Thus, we can
# enumerate all the possibilities for any given library.  This is easy
# for *-config executables (iterate over $PATH) and slightly trickier
# for .pc files (iterate over PKG_CONFIG_PATH, but also we have to
# determine pkg-config's built-in path, which is nontrivial)

# Internal-use-only subroutine.
AC_DEFUN([MTN_FULL_PKG_CONFIG_PATH],
[AC_REQUIRE([PKG_PROG_PKG_CONFIG])
# The dummy "pkg-config" package is guaranteed to exist.
if test -n "$PKG_CONFIG"; then
  mtn__full_pkg_config_path=`$PKG_CONFIG --debug pkg-config 2>&1 |
    sed -ne "/^Scanning directory '/{; s///; s/'$//; p;}" | 
    tr "$as_nl" ':' | sed 's/:$//'`
  #AC_MSG_NOTICE([detected pkg-config path: $mtn__full_pkg_config_path])
fi
])

# MTN_CHECK_MODULE(libname, version, validator)
#
# Probe for LIBNAME, trying first pkg-config, then a *-config program,
# and failing that, a total guess (no special -I or -L switches,
# -llibname).  If VERSION is not empty, it is a >= constraint passed
# to pkg-config (but not otherwise enforced).
#
# Whatever we come up with, validate it by attempting to
# compile and link VALIDATOR, which should be an AC_LANG_PROGRAM invocation
# that tries to include library headers and call functions.  VALIDATOR may
# perform compile-time version checks, but may not perform runtime tests.
#
# Like PKG_CHECK_MODULES, will set substitution variables named
# "libname_CFLAGS" and "libname_LIBS".

AC_DEFUN([MTN_CHECK_MODULE],
[AC_REQUIRE([MTN_FULL_PKG_CONFIG_PATH])
AC_REQUIRE([AC_PROG_EGREP])
AC_REQUIRE([AC_PROG_AWK])

# Probe for the $1 library.
_notfound=true
_verreq=m4_if([$2],[],[],[" >= $2"])

# First test for user overrides.  This should work even if pkg-config
# isn't installed.  The use of + instead of :+ is deliberate; the user
# should be able to tell us that no flags are required.
# (PKG_CHECK_MODULES gets this wrong!)
if test x${$1[]_CFLAGS+set} = xset || test x${$1[]_LIBS+set} = xset; then
  AC_MSG_CHECKING([for $1])
  AC_MSG_RESULT([user specified])
  _notfound=false
fi

if $_notfound; then
# Second, try a "naive" search for the unqualified package name.  This
# also ensures that the ARG_VARs for the desired library are sane.
PKG_CHECK_MODULES([$1], [$1$_verreq], 
 [_notfound=false],
 [AC_MSG_RESULT([$1.pc not found])])
fi

# Third, try looking for alternative names known to pkg-config for
# the library.
if $_notfound; then
  _save_IFS="$IFS"
  IFS=":"
  set fnord $mtn__full_pkg_config_path
  shift
  IFS="$_save_IFS"

  for pkgcfgdir; do
    echo $pkgcfgdir/*$1*.pc
  done | tr ' ' "$as_nl" | 
  $EGREP '/(lib)?$1(-?@<:@0-9.@:>@+)?\.pc$' > conftest.candpc

  echo "$as_me: candidate .pc files are:" >&AS_MESSAGE_LOG_FD
  sed 's/^/| /' conftest.candpc >&AS_MESSAGE_LOG_FD
  for f in `cat conftest.candpc`; do
    c=`AS_BASENAME([$f])`
    c=`expr X"$c" : 'X\(.*\)\.pc'`
    AC_MSG_CHECKING([for $1 using $c.pc])

    pkg_failed=no
    _PKG_CONFIG([$1][_CFLAGS], [cflags], [$c$_verreq])
    _PKG_CONFIG([$1][_LIBS], [libs], [$c$_verreq])
    if test $pkg_failed = no; then
       $1[]_CFLAGS=$pkg_cv_[]$1[]_CFLAGS
       $1[]_LIBS=$pkg_cv_[]$1[]_LIBS
       AC_MSG_RESULT([yes])
       _notfound=false
       break
    else
       AC_MSG_RESULT([no])
    fi
  done
fi

# Fourth, try -config binaries.
if $_notfound; then
  _save_IFS="$IFS"
  IFS=":"
  set fnord $PATH
  shift
  IFS="$_save_IFS"

  for pathdir; do
    echo $pathdir/*$1*-config
  done | tr ' ' "$as_nl" |
  $EGREP '/(lib)?$1(-?@<:@0-9.@:>@+)?-config$' > conftest.candcfg

  echo "$as_me: candidate -config programs are:" >&AS_MESSAGE_LOG_FD
  sed 's/^/| /' conftest.candcfg >&AS_MESSAGE_LOG_FD
  for c in `cat conftest.candcfg`; do
    n=`AS_BASENAME([$c])`
    AC_MSG_CHECKING([for $1 using $n])
    if _ccflg=`$c --cflags 2>&AS_MESSAGE_LOG_FD` &&
       _clibs=`$c --libs 2>&AS_MESSAGE_LOG_FD`
    then
      $1[]_CFLAGS="$_ccflg"
      $1[]_LIBS="$_clibs"
      AC_MSG_RESULT([yes])
      _notfound=false
      break
    else
      AC_MSG_RESULT([no])
    fi
  done
fi

if $_notfound; then
  AC_MSG_CHECKING([for $1])
  AC_MSG_RESULT([not found; guessing])
  $1[]_CFLAGS=
  $1[]_LIBS=-l$1
fi

m4_if([$3],[],[], [
AC_MSG_CHECKING([whether $1 is usable])
save_LIBS="$LIBS"
save_CPPFLAGS="$CPPFLAGS"
LIBS="$LIBS ${$1[]_LIBS}"
CPPFLAGS="$CPPFLAGS ${$1[]_CFLAGS}"
AC_LINK_IFELSE([$3], [_libusable=yes], [_libusable=no])
LIBS="$save_LIBS"
CPPFLAGS="$save_CPPFLAGS"
AC_MSG_RESULT($_libusable)
if test $_libusable = no; then
  AC_MSG_NOTICE([*** $1[]_CFLAGS=${$1[]_CFLAGS}])
  AC_MSG_NOTICE([*** $1[]_LIBS=${$1[]_LIBS}])
  AC_MSG_FAILURE([Must be able to compile and link programs against $1.])
fi
])

rm -f conftest.candcfg conftest.candpc
])

# Checks for specific libraries that can be probed this way.

AC_DEFUN([MTN_FIND_BOTAN],
[MTN_CHECK_MODULE([botan], [1.6.3],
  [AC_LANG_PROGRAM(
    [#include <botan/botan.h>
     #if BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,6,3)
     #error too old
     #endif
     #if BOTAN_VERSION_CODE == BOTAN_VERSION_CODE_FOR(1,7,14)
     #error version 1.7.14 is not usable for monotone
     #endif],
    [Botan::LibraryInitializer li;])
  ])
])

AC_DEFUN([MTN_FIND_IDNA],
[MTN_CHECK_MODULE([idn], ,
  [AC_LANG_PROGRAM(
    [#include <idna.h>],
    [const char *e = idna_strerror(IDNA_SUCCESS);])
  ])
])

AC_DEFUN([MTN_FIND_LUA],
[MTN_CHECK_MODULE([lua], [5.1],
  [AC_LANG_PROGRAM(
    [#ifdef __cplusplus
     #include <lua.hpp>
     #else
     #include <lua.h>
     #include <lualib.h>
     #include <lauxlib.h>
     #endif
     #if LUA_VERSION_NUM < 501
     #error out of date
     #endif],
    [lua_State *st = luaL_newstate();])
  ])
])

AC_DEFUN([MTN_FIND_PCRE],
[MTN_CHECK_MODULE([pcre], [7.4],
  [AC_LANG_PROGRAM(
    [#include <pcre.h>
     #if PCRE_MAJOR < 7 || (PCRE_MAJOR == 7 && PCRE_MINOR < 4)
     #error out of date
     #endif],
    [const char *e;
     int dummy;
     int o;
     /* Make sure some definitions are present. */
     dummy = PCRE_NEWLINE_CR;
     dummy = PCRE_DUPNAMES;
     pcre *re = pcre_compile("foo", 0, &e, &o, 0);])
  ])
])

AC_DEFUN([MTN_FIND_SQLITE],
[MTN_CHECK_MODULE([sqlite3], [3.3],
  [AC_LANG_PROGRAM(
    [#include <sqlite3.h>
     #if SQLITE_VERSION_NUMBER < 3003000
     #error out of date
     #endif],
    [sqlite3 *st;
     sqlite3_open("testfile.db", &st);
     sqlite3_close(st);])
  ])
])
