AUTOMAKE_OPTIONS=subdir-objects 1.7.1
ACLOCAL_AMFLAGS = -I m4

noinst_LIBRARIES = libidna.a
libidna_a_SOURCES =							\
	gunibreak.h gunicomp.h gunidecomp.h idna.c idna.h idn-int.h	\
	nfkc.c profiles.c punycode.c punycode.h rfc3454.c stringprep.c	\
	stringprep.h toutf8.c version.c

libidna_a_CPPFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir) $(LIB3RDPARTY_CXXFLAGS)
libidna_a_CFLAGS = $(AM_CFLAGS) -I$(top_srcdir)/.. $(LIB3RDPARTY_CFLAGS)
libidna_a_CXXFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir)/.. $(LIB3RDPARTY_CXXFLAGS)

