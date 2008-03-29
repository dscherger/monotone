AUTOMAKE_OPTIONS=subdir-objects 1.7.1
ACLOCAL_AMFLAGS = -I m4

noinst_LIBRARIES = libpcre.a
libpcre_a_SOURCES =							\
	pcre_chartables.c pcre_compile.c pcre_config.c pcre_config.h	\
	pcre_dfa_exec.c pcre_exec.c pcre_fullinfo.c pcre_get.c		\
	pcre_globals.c pcre.h pcre_info.c pcre_internal.h		\
	pcre_maketables.c pcre_newline.c pcre_ord2utf8.c		\
	pcre_refcount.c pcre_study.c pcre_tables.c pcre_try_flipped.c	\
	pcre_ucp_searchfuncs.c pcre_valid_utf8.c pcre_version.c		\
	pcre_xclass.c ucp.h ucpinternal.h ucptable.h

libpcre_a_CPPFLAGS =
libpcre_a_CFLAGS = $(AM_CFLAGS) $(LIB3RDPARTY_CFLAGS)
libpcre_a_CXXFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir) $(LIB3RDPARTY_CXXFLAGS)

