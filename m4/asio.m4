dnl Grab-bag of checks related to asio.

# Check for suitably new version of asio.
AC_DEFUN([ASIO_VERSION_CHECK],
[AC_LANG_ASSERT([C++])
 AC_CACHE_CHECK([for asio version 1.2.0 or newer],
                 ac_cv_asio_version_least_1_2_0,
 [
  AC_COMPILE_IFELSE(
  [#include <asio/version.hpp>
  #if ASIO_VERSION >= 100200
  int main() { return 0; }
  #else
  #error asio version is too old
  #endif
  ],
  ac_cv_asio_version_least_1_2_0=yes,
  ac_cv_asio_version_least_1_2_0=no)
 ])
  if test x$ac_cv_asio_version_least_1_2_0 = xno; then
    AC_MSG_FAILURE([asio 1.2.0 or newer required])
  fi
])

# We currently don't need any checks for asio version-specific bugs,
# but we may in the future.  They go in this macro.
AC_DEFUN([ASIO_VERSION_SPECIFIC_BUGS],
[AC_LANG_ASSERT([C++])
])
