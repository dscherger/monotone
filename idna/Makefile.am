AUTOMAKE_OPTIONS=subdir-objects no-dist foreign 1.7.1

lib_LIBRARIES = libidna.a
include_HEADERS = idna.h punycode.h stringprep.h

libidna_a_SOURCES =							\
	gunibreak.h gunicomp.h gunidecomp.h idna.c idn-int.h nfkc.c	\
	profiles.c punycode.c rfc3454.c stringprep.c toutf8.c		\
	version.c

libidna_a_CPPFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir) $(LIB3RDPARTY_CXXFLAGS)
libidna_a_CFLAGS = $(AM_CFLAGS) -I$(top_srcdir)/.. $(LIB3RDPARTY_CFLAGS)
libidna_a_CXXFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir)/.. $(LIB3RDPARTY_CXXFLAGS)

