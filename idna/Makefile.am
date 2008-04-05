## Process this file with automake to produce Makefile.in
# Copyright (C) 2002, 2003, 2004, 2005, 2006, 2007 Simon Josefsson.
# Copyright (C) 2004 Free Software Foundation, Inc.
#
# This file is part of GNU Libidn.
#
# GNU Libidn is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 2.1 of
# the License, or (at your option) any later version.
#
# GNU Libidn is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with GNU Libidn; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA

# MONOTONE: A bunch of undesirable stuff has been pruned from this
# MONOTONE: Makefile.  Also, the required pieces of gnulib have been
# MONOTONE: incorporated into this makefile rather than having a gl
# MONOTONE: subdirectory, and the method for creating idn-int.h is
# MONOTONE: very different.

DEFS = -DLIBIDN_BUILDING @DEFS@

lib_LIBRARIES = libidn.a
idn_int = idn-int.h

BUILT_SOURCES = $(idn_int)
DISTCLEANFILES = $(idn_int)
include_HEADERS = stringprep.h idna.h punycode.h
nodist_include_HEADERS = $(idn_int)

idn-int.h: config.h
	inttypes_h=$$(grep -q '^#define HAVE_INTTYPES_H' config.h; echo $$?);\
	stdint_h=$$(grep -q '^#define HAVE_STDINT_H' config.h; echo $$?);\
	uint32_t=$$(grep -q '^#define uint32_t' config.h; echo $$?);\
	uint16_t=$$(grep -q '^#define uint16_t' config.h; echo $$?);\
	if [ $$uint32_t -ne 0 ] || [ $$uint16_t -ne 0 ];\
	then if [ $$stdint_h -eq 0 ];\
	     then echo '#include <stdint.h>' > idn-int.h.T;\
	     elif [ $$inttypes_h -eq 0 ];\
	     then echo '#include <inttypes.h>' > idn-int.h.T;\
	     else echo 'Missing inttypes.h and stdint.h' >&2; exit 1;\
	     fi;\
	fi;\
	if [ $$uint32_t -eq 0 ] || [ $$uint16_t -eq 0 ];\
	then grep '^#define uint[0-9]*_t' config.h >> idn-int.h.T;\
	fi;\
	mv -f idn-int.h.T idn-int.h

libidn_a_SOURCES = gunibreak.h gunicomp.h gunidecomp.h \
	nfkc.c toutf8.c version.c \
	stringprep.h.in stringprep.c rfc3454.c profiles.c \
	punycode.h punycode.c \
	idna.h idna.c \
	striconv.h striconv.c strverscmp.h strverscmp.c \
	c-strcase.h c-strcasecmp.c c-ctype.c c-ctype.h

# This is a subset of the tests originally in a separate directory
# of the libidn distribution.  tst_pr29, tst_strerror, and tst_tld
# have been excluded, since the corresponding code has been excluded
# from the library proper.

TESTS = tst_stringprep$(EXEEXT) tst_punycode$(EXEEXT)		\
	tst_idna$(EXEEXT) tst_idna2$(EXEEXT) tst_nfkc$(EXEEXT)
check_PROGRAMS = $(TESTS)

# You might think there was a more elegant way to do this, but no.
tst_stringprep_SOURCES = tst_stringprep.c utils.c utils.h
tst_punycode_SOURCES   = tst_punycode.c utils.c utils.h
tst_idna_SOURCES       = tst_idna.c utils.c utils.h
tst_idna2_SOURCES      = tst_idna2.c utils.c utils.h
tst_nfkc_SOURCES       = tst_nfkc.c utils.c utils.h

tst_stringprep_LDADD   = libidn.a
tst_punycode_LDADD     = libidn.a
tst_idna_LDADD         = libidn.a
tst_idna2_LDADD        = libidn.a
tst_nfkc_LDADD         = libidn.a

# This is necessary because autoreconf doesn't pay attention to
# AC_CONFIG_MACRO_DIR.
ACLOCAL_AMFLAGS = -I ../m4
