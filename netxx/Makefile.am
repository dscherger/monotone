lib_LIBRARIES = libnetxx.a
nobase_include_HEADERS =						\
	netxx/address.h netxx/datagram.h netxx/datagramserver.h		\
	netxx/netbuf.h netxx/peer.h netxx/probe.h netxx/probe_impl.h	\
	netxx/probeinfo.h netxx/socket.h netxx/sockopt.h		\
	netxx/streambase.h netxx/stream.h netxx/streamserver.h		\
	netxx/timeout.h netxx/types.h

libnetxx_a_SOURCES =							\
	accept.cxx address.cxx datagram.cxx datagramserver.cxx		\
	osutil.cxx peer.cxx probe.cxx probe_select.cxx recvfrom.cxx	\
	serverbase.cxx sockaddr.cxx socket.cxx sockopt.cxx stream.cxx	\
	streambase.cxx streamserver.cxx					\
	accept.h common.h compat.h osutil.h recvfrom.h resolve.h	\
	serverbase.h sockaddr.h

# conditionally included
EXTRA_libnetxx_a_SOURCES =						\
	inet_pton.cxx inet_pton.h					\
	inet_ntop.cxx inet_ntop.h					\
	resolve_gethostbyname.cxx resolve_getservbyname.cxx		\
	resolve_getaddrinfo.cxx

libnetxx_a_LIBADD = $(PLAT_OBJS)
libnetxx_a_DEPENDENCIES = $(PLAT_OBJS)

# We have .pc files for pkg-config users.
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = netxx.pc

# This is necessary because autoreconf doesn't pay attention to
# AC_CONFIG_MACRO_DIR.
ACLOCAL_AMFLAGS = -I ../m4
