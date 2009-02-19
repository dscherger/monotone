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
