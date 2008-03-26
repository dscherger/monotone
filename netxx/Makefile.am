AUTOMAKE_OPTIONS=subdir-objects 1.7.1
ACLOCAL_AMFLAGS = -I m4

noinst_LIBRARIES = libnetxx.a
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

libnetxx_a_CPPFLAGS =
libnetxx_a_CFLAGS = $(AM_CFLAGS) $(LIB3RDPARTY_CFLAGS)
libnetxx_a_CXXFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir) $(LIB3RDPARTY_CXXFLAGS)

if MISSING_INET_PTON
  libnetxx_a_SOURCES += netxx/inet_pton.cxx netxx/inet_pton.h
  libnetxx_a_CPPFLAGS += -DNETXX_NO_PTON
endif

if MISSING_INET_NTOP
  libnetxx_a_SOURCES += netxx/inet_ntop.cxx netxx/inet_ntop.h
  libnetxx_a_CPPFLAGS += -DNETXX_NO_NTOP
endif

if MISSING_GETADDRINFO
  libnetxx_a_SOURCES += resolve_gethostbyname.cxx resolve_getservbyname.cxx
else
  libnetxx_a_SOURCES += resolve_getaddrinfo.cxx
endif

if MISSING_INET6
  libnetxx_a_CPPFLAGS += -DNETXX_NO_INET6
endif

