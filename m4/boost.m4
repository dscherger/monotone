# Copyright (C) 2006 Zack Weinberg <zackw@panix.com>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# Check for suitably new version of boost.
AC_DEFUN([BOOST_VERSION_CHECK],
[AC_LANG_ASSERT([C++])
 AC_CACHE_CHECK([boost version 1.33.0 or newer],
                 ac_cv_boost_version_least_1_33_0,
 [
  AC_COMPILE_IFELSE([AC_LANG_SOURCE(
  [#include <boost/version.hpp>
  #if BOOST_VERSION >= 103300
  int main() { return 0; }
  #else
  #error boost version is too old
  #endif
  ],
  ac_cv_boost_version_least_1_33_0=yes,
  ac_cv_boost_version_least_1_33_0=no)
 ])])
  if test x$ac_cv_boost_version_least_1_33_0 = xno; then
        AC_MSG_ERROR([boost 1.33.0 or newer required])
  fi
])

# We currently don't need any checks for boost version-specific bugs,
# but we have in the past and may again.  They go in this macro.
AC_DEFUN([BOOST_VERSION_SPECIFIC_BUGS],
[AC_LANG_ASSERT([C++])
])
