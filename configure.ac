# Top-level configuration template for Monotone.
# Process this file with autoconf to produce a configure script.
# Copyright 2008 Zachary Weinberg  <zackw@panix.com>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# We do not do any checks here, only record the configure options and
# create the Makefile which will pass them all down to the
# subdirectories.  Note that we do not use AC_CONFIG_SUBDIRS, because
# we need to control the arguments to the sub-configures; instead we
# invoke them from the top-level Makefile.

# FIXME: The AC_INIT line has to be kept in sync with monotone/configure.ac.
AC_PREREQ(2.58)
AC_INIT(monotone, 0.39, monotone-devel@nongnu.org)
AC_CONFIG_AUX_DIR([.])
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_SRCDIR([monotone/monotone.cc])

CONFIGARGS="$ac_configure_args"
AC_SUBST(CONFIGARGS)

# Gettext is invoked here, and the po directory lives at top level,
# primarily because autopoint is a horrid monster.
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.11.5])

# --with-system-foo support goes here
LIBRARIES="botan idna lua netxx pcre sqlite"
AC_SUBST(LIBRARIES)

# A bunch of Makefile rules need to know whether or not to prepend "../"
# to $(srcdir) when in a subdirectory.  It is much easier to figure this
# out here.
case "$srcdir" in
 . | ./) subsrcdir=".." ;;
     /*) subsrcdir="$srcdir" ;;
      *) subsrcdir="../$srcdir" ;;
esac
AC_SUBST(subsrcdir)

# It is much easier to create the build subdirectories in the same
# breath as the top level Makefile than to deal with Make's bizarro
# behavior when directories are goals.
AC_CONFIG_FILES([Makefile])
AC_CONFIG_COMMANDS([build-dirs],
	           [mkdir -p $needed_dirs],
		   [needed_dirs="$LIBRARIES monotone"])
AC_OUTPUT
