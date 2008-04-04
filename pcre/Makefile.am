## Process this file with automake to produce Makefile.in.

## MONOTONE: All irrelevant functionality has been removed from this file.
## MONOTONE: See README.monotone for details.

# The library to install.
lib_LIBRARIES = libpcre.a
libpcre_a_SOURCES = pcre_chartables.c pcre_compile.c pcre_config.c	\
  pcre_dfa_exec.c pcre_exec.c pcre_fullinfo.c pcre_get.c		\
  pcre_globals.c pcre_info.c pcre_internal.h pcre_maketables.c		\
  pcre_printint.src pcre_newline.c pcre_ord2utf8.c pcre_refcount.c	\
  pcre_study.c pcre_tables.c pcre_try_flipped.c pcre_ucp_searchfuncs.c	\
  pcre_valid_utf8.c pcre_version.c pcre_xclass.c ucp.h ucpinternal.h	\
  ucptable.h

# We have .pc files for pkg-config users.
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libpcre.pc
bin_SCRIPTS = pcre-config

# These are the header files we'll install.
include_HEADERS = pcre.h

# Unit tests you want to run when people type 'make check'.
# Each unit test is a binary plus a script that runs that binary in various
# ways. We install these test binaries in case folks find it helpful.

TESTS = RunTest RunGrepTest
noinst_SCRIPTS = RunTest RunGrepTest
bin_PROGRAMS = pcretest pcregrep

pcretest_SOURCES = pcretest.c pcreposix.c pcreposix.h
pcretest_LDADD = libpcre.a

pcregrep_SOURCES = pcregrep.c
pcregrep_LDADD = libpcre.a

# Additional files to delete on 'make clean'.
CLEANFILES = testsavedregex teststderr testtry testNinput
