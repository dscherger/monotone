# Copyright (C) 2014 Markus Wanner <markus@bluegap.ch>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# Checks for crippled versions of mktime() as provided by Apple on
# 64-bit systems.

AC_DEFUN([AC_CXX_MKTIME_64BIT_WORKS],
[
  AC_CACHE_CHECK([whether mktime is 64-bit capable],
    ac_cv_cxx_mktime_64bit, [
    AC_RUN_IFELSE([AC_LANG_SOURCE([
#include <cstdlib>
#include <ctime>

int main() {
  /* not an issue if not 64-bit */
  if (sizeof(struct tm) <= 4)
	return EXIT_SUCCESS;

  setenv("TZ", "UTC", 1);

  /* equals INT32_MIN */
  struct tm tb_lower = { 52, 45, 20, 13, 11, 1, 0, 0, -1 };
  time_t i_lower = std::mktime(&tb_lower);

  /* step over the boundary */
  tb_lower.tm_sec -= 1;

  time_t o_lower = std::mktime(&tb_lower);
  if (o_lower == -1)
    return EXIT_FAILURE;
  else if ((o_lower - i_lower) != -1)
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}
	])], ac_cv_cxx_mktime_64bit=yes, ac_cv_cxx_mktime_64bit=no,
	     ac_cv_cxx_mktime_64bit=no)
  ])
  if test x$ac_cv_cxx_mktime_64bit = xno; then
    HAVE_MKTIME_64BIT=0
  else
    HAVE_MKTIME_64BIT=1
    AC_DEFINE(HAVE_MKTIME_64BIT,1,
              [define if mktime is 64-bit capable])
  fi
  AC_SUBST(HAVE_MKTIME_64BIT)
])
