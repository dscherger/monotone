# Copyright (C) 2003 Patrick Mauritz <oxygene@studentenbude.ath.cx>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

AC_DEFUN([AC_HAVE_INADDR_NONE],
[AC_CACHE_CHECK([whether INADDR_NONE is defined], ac_cv_have_inaddr_none,
 [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
],[
unsigned long foo = INADDR_NONE;
])],
  [ac_cv_have_inaddr_none=yes],
  [ac_cv_have_inaddr_none=no])])
 if test x$ac_cv_have_inaddr_none != xyes; then
   AC_DEFINE(INADDR_NONE, 0xffffffff,
 [Define to value of INADDR_NONE if not provided by your system header files.])
 fi])
