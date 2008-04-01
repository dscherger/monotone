lib_LIBRARIES = libnetxx.a
include_HEADERS = 
libnetxx_a_SOURCES =							\
	accept.cxx accept.h address.cxx address.h common.h compat.h	\
	datagram.cxx datagram.h datagramserver.cxx datagramserver.h	\
	inet_ntop.cxx inet_ntop.h inet_pton.cxx inet_pton.h netbuf.h	\
	netxx.h osutil.cxx osutil.h peer.cxx peer.h probe.cxx probe.h	\
	probe_impl.h probeinfo.h probe_select.cxx recvfrom.cxx		\
	recvfrom.h resolve_getaddrinfo.cxx resolve_gethostbyname.cxx	\
	resolve_getservbyname.cxx resolve.h serverbase.cxx serverbase.h	\
	sockaddr.cxx sockaddr.h socket.cxx socket.h sockopt.cxx		\
	sockopt.h streambase.cxx streambase.h stream.cxx stream.h	\
	streamserver.cxx streamserver.h timeout.h types.h

if MISSING_INET_PTON
  libnetxx_a_SOURCES += inet_pton.cxx inet_pton.h
endif

if MISSING_INET_NTOP
  libnetxx_a_SOURCES += inet_ntop.cxx inet_ntop.h
endif

if MISSING_GETADDRINFO
  libnetxx_a_SOURCES += resolve_gethostbyname.cxx resolve_getservbyname.cxx
else
  libnetxx_a_SOURCES += resolve_getaddrinfo.cxx
endif

# This is necessary because autoreconf doesn't pay attention to
# AC_CONFIG_MACRO_DIR.
ACLOCAL_AMFLAGS = -I ../m4
