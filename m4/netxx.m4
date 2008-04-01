# checks done primarily for the benefit of netxx

# Check for IPv6.  Let the user enable or disable it manually using a
# three-state (yes|no|auto) --enable argument.
AC_DEFUN([AC_NET_IPV6],
[AC_LANG_ASSERT([C++])
 AC_ARG_ENABLE(ipv6,
   AS_HELP_STRING([--enable-ipv6],[enable IPv6 support (default=auto)]), ,
   enable_ipv6=auto)
 if test x"${enable_ipv6}" = xauto || test x"${enable_ipv6}" = xyes; then
   AC_CHECK_TYPE([sockaddr_in6],
      [enable_ipv6=yes],
      [if test x"${enable_ipv6}" = xyes; then
         AC_MSG_FAILURE([IPv6 explicitly requested but it could not be found])
       fi
       enable_ipv6=no],
		    [#ifdef WIN32
                     #include <winsock2.h>
                     #else
                     #include <sys/types.h>
                     #include <sys/socket.h>
                     #include <netinet/in.h>
                     #include <arpa/inet.h>
                     #endif])
 fi
])

AC_DEFUN([MTN_NETXX_DEPENDENCIES],
[AC_NET_IPV6
 if test $enable_ipv6 = yes; then
   AC_DEFINE(USE_IPV6, 1, [Define if IPv6 support should be included.])
 fi
 AC_SEARCH_LIBS([gethostbyname], [nsl])
 AC_SEARCH_LIBS([accept], [socket])
 AC_SEARCH_LIBS([inet_aton], [resolv])
])
