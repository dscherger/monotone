# Process this file with autoconf to produce a configure script.
# Copyright 2008 Zachary Weinberg  <zackw@panix.com>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# This is a drastically trimmed down version of the configure.ac from
# libidn 1.4, with additional tweakage for monotone's purposes.

AC_PREREQ(2.58)
AC_INIT([libidn (monotone)], 1.4, monotone-devel@nongnu.org)
AC_CONFIG_AUX_DIR([..])
AC_CONFIG_MACRO_DIR([../m4])

AM_INIT_AUTOMAKE([1.7.1 foreign no-dist no-define])
AC_CONFIG_SRCDIR([idna.h])
AC_CONFIG_FILES([Makefile stringprep.h libidn.pc])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_PROG_RANLIB

AM_ICONV
AM_LANGINFO_CODESET

AC_TYPE_UINT16_T
AC_TYPE_UINT32_T

AC_OUTPUT
