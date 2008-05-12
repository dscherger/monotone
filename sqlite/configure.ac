# Configuration template for Monotone embedded sqlite.
# Process this file with autoconf to produce a configure script.
# Copyright 2008 Zachary Weinberg  <zackw@panix.com>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# This is a stopgap configuration script for Monotone's embedded copy
# of sqlite.  Once we upgrade our embedded sqlite to a more recent
# version, we will be able to use the upstream configure script instead.

AC_PREREQ(2.58)
AC_INIT([sqlite (monotone)], 3.4.2, monotone-devel@nongnu.org)
AC_CONFIG_AUX_DIR([..])
AC_CONFIG_MACRO_DIR([../m4])

AM_INIT_AUTOMAKE([1.7.1 foreign no-dist no-define])
AC_CONFIG_SRCDIR([sqlite3.h])
AC_CONFIG_FILES([Makefile sqlite3.pc])

AC_PROG_CC
AC_PROG_RANLIB

# Let the user specify whether he wants large file support or not in sqlite.
AC_ARG_ENABLE([large-file],
   AS_HELP_STRING(
      [--disable-large-file],
      [Disable support for repositories larger than 2GB.]
   ), , enable_large_file=yes
)

if test "x$enable_large_file" = "xno"; then
   AC_DEFINE(SQLITE_DISABLE_LFS, 1,
     [Define to disable support for database files larger than 2GB.])
fi

AC_CHECK_FUNC([pread],
  [AC_CHECK_FUNC([pwrite],
    [AC_DEFINE(USE_PREAD, 1, 
      [Define if pread() and pwrite() should be used.])])])

AC_SEARCH_LIBS([fdatasync], [rt],
 [AC_DEFINE(HAVE_FDATASYNC, 1, 
   [Define if fdatasync() is available in either libc or librt.])])

AC_SEARCH_LIBS([usleep], [rt],
 [AC_DEFINE(HAVE_USLEEP, 1, 
   [Define if fdatasync() is available in either libc or librt.])])

# Monotone-specific configuration defines.
AC_DEFINE(TEMP_STORE, 1, [Enable temporary storage])
AC_DEFINE(NDEBUG, 1, [Disable debugging])
AC_DEFINE(SQLITE_OMIT_CURSOR, 1, [Disable cursors])
AC_DEFINE(SQLITE_OMIT_LOAD_EXTENSION, 1, [Disable dynamic extensions])
AC_DEFINE(THREADSAFE, 0, [Disable thread safety])
AC_DEFINE(SQLITE_THREAD_OVERRIDE_LOCK, -1, [Disable more thread safety])

AC_OUTPUT
