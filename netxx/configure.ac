# Stub configuration template for Monotone third-party library directories.
# Process this file with autoconf to produce a configure script.
# Copyright 2008 Zachary Weinberg  <zackw@panix.com>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# This file will be replaced by the actual third-party library
# configuration script for all directories where that is practical.
# (It may not be practical for dead-upstream or heavily modified
# third-party libraries, e.g. netxx and idna.)  It makes no attempt to
# do anything beyond the bare minimum, just like the associated
# Makefile.am.

AC_PREREQ(2.58)
AC_INIT(netxx, XX, monotone-devel@nongnu.org)
AC_CONFIG_AUX_DIR([..])
AC_CONFIG_MACRO_DIR([../m4])

AM_INIT_AUTOMAKE([1.7.1 foreign no-dist no-define])
AC_CONFIG_SRCDIR([common.h])
AC_CONFIG_FILES([Makefile netxx.pc])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CXX
AC_PROG_RANLIB

AC_LANG_PUSH([C++])
AC_NET_IPV6
if test $enable_ipv6 = no; then
   AC_DEFINE(NETXX_NO_INET6, 1, [Define to suppress IPv6 support.])
fi

PLAT_OBJS=
AC_SUBST(PLAT_OBJS)

AC_CHECK_FUNC(getaddrinfo,
  [PLAT_OBJS="$PLAT_OBJS resolve_getaddrinfo.\$(OBJEXT)"]
 ,
  [PLAT_OBJS="$PLAT_OBJS resolve_gethostbyname.\$(OBJEXT)"
   PLAT_OBJS="$PLAT_OBJS resolve_getservbyname.\$(OBJEXT)"])


AC_CHECK_FUNC(inet_pton,
  [],
  [PLAT_OBJS="$PLAT_OBJS inet_pton.\$(OBJEXT)"
   AC_DEFINE(NETXX_NO_PTON, 1, [Define if inet_pton is not available.])
  ])

AC_CHECK_FUNC(inet_ntop,
  [],
  [PLAT_OBJS="$PLAT_OBJS inet_ntop.\$(OBJEXT)"
   AC_DEFINE(NETXX_NO_NTOP, 1, [Define if inet_ntop is not available.])
  ])

AC_CHECK_TYPES([socklen_t],,,[
 #include <sys/types.h>
 #include <sys/socket.h>
])

AC_OUTPUT
