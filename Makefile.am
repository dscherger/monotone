# Copyright (C) 2003 Graydon Hoare <graydon@pobox.com>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# Makefile for monotone

AUTOMAKE_OPTIONS = subdir-objects -Wno-portability
ACLOCAL_AMFLAGS  = -I m4

CMD_SOURCES =								\
	cmd.hh cmd.cc cmd_netsync.cc cmd_list.cc cmd_packet.cc		\
	cmd_key_cert.cc cmd_merging.cc cmd_db.cc cmd_diff_log.cc	\
	cmd_ws_commit.cc cmd_othervcs.cc cmd_automate.cc cmd_files.cc	\
	cmd_conflicts.cc

SANITY_CORE_SOURCES =							\
	sanity.cc sanity.hh quick_alloc.hh vector.hh base.hh		\
	simplestring_xform.cc simplestring_xform.hh 			\
	constants.cc constants.hh numeric_vocab.hh 			\
	platform.hh origin_type.hh

LUAEXT_SOURCES = 							\
	vocab.hh vocab.cc vocab_terms.hh vocab_macros.hh vocab_cast.hh  \
	charset.cc charset.hh paths.cc paths.hh				\
	interner.hh hash_map.hh	vocab_hash.hh				\
	luaext_mkstemp.cc luaext_parse_basic_io.cc			\
	luaext_guess_binary.cc luaext_platform.cc luaext_globish.cc	\
	lua.cc lua.hh mkstemp.cc file_io.cc file_io.hh			\
	globish.cc globish.hh basic_io.cc basic_io.hh			\
	char_classifiers.hh

MOST_SOURCES = 								\
	$(SANITY_CORE_SOURCES) $(LUAEXT_SOURCES) platform-wrapped.hh	\
	rev_types.hh mtn-sanity.cc mtn-sanity.hh ui.cc ui.hh		\
	app_state.cc app_state.hh					\
	botan_pipe_cache.hh						\
	commands.cc commands.hh $(CMD_SOURCES)				\
	diff_output.cc diff_output.hh					\
	lua_hooks.cc lua_hooks.hh 					\
	transforms.cc transforms.hh					\
	update.cc update.hh						\
	work.cc migrate_work.cc work.hh					\
	cert.cc cert.hh							\
	project.cc project.hh						\
	outdated_indicator.cc outdated_indicator.hh			\
	database.cc database.hh						\
	key_store.cc key_store.hh					\
	ssh_agent.cc ssh_agent.hh					\
	keys.cc keys.hh							\
	packet.cc packet.hh						\
	rcs_file.cc rcs_file.hh						\
	xdelta.cc xdelta.hh						\
	migration.hh migrate_schema.cc migrate_ancestry.cc		\
	refiner.cc refiner.hh						\
	enumerator.cc enumerator.hh					\
	netsync.cc            						\
	netxx_pipe.cc netxx_pipe.hh					\
	netcmd.cc netcmd.hh						\
	merkle_tree.cc merkle_tree.hh 					\
	lcs.cc lcs.hh							\
	rcs_import.cc rcs_import.hh 					\
	git_change.cc git_change.hh git_export.cc git_export.hh		\
	revision.cc ancestry.cc revision.hh				\
	cset.cc cset.hh							\
	roster.cc roster.hh 						\
	mt_version.cc mt_version.hh					\
	automate.cc							\
	database_check.cc						\
	epoch.cc epoch.hh						\
	gzip.cc gzip.hh							\
	inodeprint.cc inodeprint.hh					\
	selectors.cc selectors.hh					\
	annotate.cc annotate.hh						\
	restrictions.cc	restrictions.hh					\
	hmac.cc hmac.hh							\
	string_queue.hh							\
	merge_roster.cc merge_conflict.cc merge_roster.hh		\
	merge_content.cc merge_3way.cc merge_content.hh			\
	legacy.cc legacy.hh uri.cc uri.hh				\
	graph.cc graph.hh						\
	roster_delta.cc roster_delta.hh					\
	sha1.cc								\
	pcrewrap.cc pcrewrap.hh						\
	rev_height.cc rev_height.hh					\
	asciik.cc asciik.hh						\
	dates.cc dates.hh						\
									\
	lru_writeback_cache.hh hybrid_map.hh lazy_rng.hh		\
									\
	cleanup.hh unit_tests.hh					\
	cycle_detector.hh randomfile.hh adler32.hh			\
	netio.hh gettext.h						\
	package_revision.cc package_full_revision.cc			\
	option.cc option.hh options.cc options.hh options_list.hh	\
	specialized_lexical_cast.cc lexical_cast.hh			\
	parallel_iter.hh safe_map.hh pch.hh current_exception.hh

NETXX_SOURCES =								\
	netxx/accept.cxx netxx/accept.h netxx/address.cxx		\
	netxx/common.h netxx/compat.h netxx/datagram.cxx		\
	netxx/datagramserver.cxx netxx/osutil.cxx netxx/osutil.h	\
	netxx/peer.cxx netxx/probe.cxx netxx/probe_impl.h		\
	netxx/probe_select.cxx netxx/recvfrom.cxx netxx/recvfrom.h	\
	netxx/resolve.h netxx/serverbase.cxx netxx/serverbase.h		\
	netxx/sockaddr.cxx netxx/sockaddr.h netxx/socket.cxx 		\
	netxx/socket.h netxx/sockopt.cxx netxx/stream.cxx		\
	netxx/streambase.cxx netxx/streamserver.cxx			\
	netxx/address.h netxx/datagram.h netxx/datagramserver.h		\
	netxx/netbuf.h netxx/netxx.h netxx/peer.h netxx/probe.h 	\
	netxx/probeinfo.h netxx/sockopt.h netxx/stream.h		\
	netxx/streambase.h netxx/streamserver.h netxx/timeout.h		\
	netxx/types.h

BOOST_SANDBOX_SOURCES =							\
	boost/circular_buffer_adaptor.hpp				\
	boost/circular_buffer_base.hpp					\
	boost/circular_buffer_fwd.hpp					\
	boost/circular_buffer.hpp

UNIX_PLATFORM_SOURCES =							\
	unix/read_password.cc unix/get_system_flavour.cc		\
	unix/process.cc unix/terminal.cc unix/inodeprint.cc		\
	unix/fs.cc unix/make_io_binary.cc unix/os_strerror.cc           \
	unix/cputime.cc unix/ssh_agent_platform.cc			\
	unix/ssh_agent_platform.hh

WIN32_PLATFORM_SOURCES =						\
	win32/read_password.cc win32/get_system_flavour.cc		\
	win32/process.cc win32/terminal.cc win32/inodeprint.cc		\
	win32/fs.cc win32/make_io_binary.cc win32/os_strerror.cc	\
	win32/cputime.cc win32/ssh_agent_platform.cc			\
	win32/ssh_agent_platform.hh

# these files (part of the main program) contain code subject to unit testing

UNIT_TESTEES =								\
	basic_io.cc charset.cc commands.cc cset.cc git_change.cc	\
	globish.cc graph.cc merge_3way.cc merge_roster.cc mkstemp.cc	\
	netcmd.cc netxx_pipe.cc option.cc outdated_indicator.cc		\
	refiner.cc restrictions.cc revision.cc simplestring_xform.cc	\
	transforms.cc uri.cc vocab.cc xdelta.cc

# these files contain the actual unit tests
# yes, we have to list them all explicitly, with directory prefix, or
# automake gets very confused
UNIT_TESTERS =								\
	unit-tests/basic_io.cc unit-tests/charset.cc			\
	unit-tests/commands.cc unit-tests/cset.cc			\
	unit-tests/git_change.cc unit-tests/globish.cc			\
	unit-tests/graph.cc unit-tests/merge_3way.cc			\
	unit-tests/merge_roster.cc unit-tests/mkstemp.cc		\
	unit-tests/netcmd.cc unit-tests/netxx_pipe.cc			\
	unit-tests/option.cc unit-tests/outdated_indicator.cc		\
	unit-tests/refiner.cc unit-tests/restrictions.cc		\
	unit-tests/revision.cc unit-tests/simplestring_xform.cc		\
	unit-tests/transforms.cc unit-tests/uri.cc unit-tests/vocab.cc	\
	unit-tests/xdelta.cc unit-tests/numeric_vocab.cc		\
	unit-tests/sha1.cc unit-tests/string_queue.cc unit_tester.cc

# these files contain unit tests that need access to private interfaces
# of the code under test, so they #include the code under test.  thus,
# to avoid multiple-definition errors, for these files the code under
# test is not part of UNIT_TESTEES.

UNIT_TESTERS_PRIV =							\
	unit-tests/dates.cc unit-tests/packet.cc unit-tests/paths.cc	\
	unit-tests/rev_height.cc unit-tests/roster.cc

# these files do not contain unit tests, but are required for unit testing
# note that some are and some aren't part of the main program
UNIT_TEST_SUPPORT =							\
	constants.cc file_io.cc gzip.cc hmac.cc lcs.cc merkle_tree.cc   \
	randomizer.cc randomizer.hh roster_delta.cc roster_tests.hh	\
	sanity.cc specialized_lexical_cast.cc

# primaries

bin_PROGRAMS = mtn
noinst_PROGRAMS = txt2c
EXTRA_PROGRAMS =

bin_SCRIPTS = mtnopt

mtn_SOURCES = $(MOST_SOURCES) monotone.cc
nodist_mtn_SOURCES = std_hooks.cc schema.cc

unit_tester_SOURCES = $(UNIT_TESTERS) $(UNIT_TESTERS_PRIV) \
	$(UNIT_TESTEES) $(UNIT_TEST_SUPPORT)

tester_SOURCES = tester.cc transforms.cc gzip.cc \
	$(SANITY_CORE_SOURCES) $(LUAEXT_SOURCES) pcrewrap.cc
nodist_tester_SOURCES = testlib.cc

txt2c_SOURCES = txt2c.cc

noinst_LIBRARIES = libplatform.a lib3rdparty.a
libplatform_a_SOURCES = platform.hh tester-plaf.hh
lib3rdparty_a_SOURCES = $(BOOST_SANDBOX_SOURCES) \
			$(NETXX_SOURCES)

htmldir = $(datadir)/doc/monotone
html_DATA = monotone.html

# flags

# sets DEFS for just this object (autoconf's -DHAVE_CONFIG_H is useless)
# the variable prevents this from overriding automake's unix/fs.o rule
DEFS =
UNIX=unix/
$(UNIX)fs.$(OBJEXT) : DEFS += -DLOCALEDIR="\"$(localedir)\""

if BUILD_PCH

PCH_SOURCE = $(srcdir)/pch.hh
# the .gch file only will be seen if it's in the same dir as the .hh file,
# so need to copy it for separate build directories
PCH_BUILD = pch-build.hh
PCH_FLAGS = -Winvalid-pch -include $(PCH_BUILD)
PCH_FILE = $(PCH_BUILD).gch
sinclude $(PCH_FILE).dep

*.o *.lo *.obj: $(PCH_FILE)

$(PCH_BUILD): $(PCH_SOURCE)
	cp $(PCH_SOURCE) $(PCH_BUILD)

$(PCH_FILE): $(PCH_BUILD)
	$(MAKE) PCH_FLAGS= pch-file

pch-file:
	$(CXXCOMPILE) -x c++-header $(PCH_BUILD) -o $(PCH_FILE) \
	-MT $(PCH_FILE)  -MD -MP -MF "./$(PCH_FILE).Tdep" \
	&& mv "./$(PCH_FILE).Tdep" "./$(PCH_FILE).dep" \
	|| rm "./$(PCH_FILE).Tdep"
endif
AM_CXXFLAGS = $(PCH_FLAGS)

SUFFIXES = .gch .merged.po .gmo

AM_CPPFLAGS = $(intl_CFLAGS) $(pcre_CFLAGS) $(botan_CFLAGS)	\
	      $(lua_CFLAGS) $(sqlite_CFLAGS) $(idn_CFLAGS)

LDADD = libplatform.a lib3rdparty.a $(intl_LIBS) $(pcre_LIBS)	\
	$(botan_LIBS) $(lua_LIBS) $(sqlite_LIBS) $(idn_LIBS)

if WIN32_PLATFORM
  libplatform_a_SOURCES += $(WIN32_PLATFORM_SOURCES)
  mtn_SOURCES += win32/main.cc
  tester_SOURCES += win32/tester-plaf.cc
  check_net_SOURCES = win32/tester-check-net.cc
  AM_CPPFLAGS += -DWIN32
  LDADD += -lshfolder -lws2_32 -liconv -liphlpapi
else
  libplatform_a_SOURCES += $(UNIX_PLATFORM_SOURCES)
  mtn_SOURCES += unix/main.cc
  tester_SOURCES += unix/tester-plaf.cc
  check_net_SOURCES = unix/tester-check-net.cc
endif

if MISSING_INET_PTON
  lib3rdparty_a_SOURCES += netxx/inet_pton.cxx netxx/inet_pton.h
  AM_CPPFLAGS += -DNETXX_NO_PTON
endif

if MISSING_INET_NTOP
  lib3rdparty_a_SOURCES += netxx/inet_ntop.cxx netxx/inet_ntop.h
  AM_CPPFLAGS += -DNETXX_NO_NTOP
endif

if MISSING_GETADDRINFO
  lib3rdparty_a_SOURCES += netxx/resolve_gethostbyname.cxx netxx/resolve_getservbyname.cxx
else
  lib3rdparty_a_SOURCES += netxx/resolve_getaddrinfo.cxx
endif

if MISSING_INET6
  AM_CPPFLAGS += -DNETXX_NO_INET6
endif

# extra files

PDF_FIGURES= figures/branch-heads.pdf figures/cert.pdf			\
	figures/difference-between-versions.pdf				\
	figures/file-id-manifest-id.pdf figures/fork.pdf		\
	figures/general-workflow.pdf figures/linear-history.pdf		\
	figures/local-workflow.pdf figures/manifest.pdf			\
	figures/merge.pdf figures/network-workflow.pdf			\
	figures/parent-child-hashes.pdf					\
	figures/parent-child-names-hashes.pdf figures/parent-child.pdf	\
	figures/revision-chaining.pdf figures/revision.pdf		\
	figures/statement.pdf figures/three-versions.pdf		\
	figures/two-branches.pdf

PNG_FIGURES=$(addprefix $(top_builddir)/figures/,$(notdir $(PDF_FIGURES:.pdf=.png)))
EPS_FIGURES=$(addprefix $(top_builddir)/figures/,$(notdir $(PDF_FIGURES:.pdf=.eps)))
TEXI_FRAGMENTS = pcrepattern.texi pcresyntax.texi

info_TEXINFOS = monotone.texi
EXTRA_DIST =								\
	HACKING INSTALL README.changesets README.visualc8 UPGRADE	\
	audit-includes do-editor-vars.sh doxygen.cfg			\
	$(wildcard $(srcdir)/m4/*.m4) monotone.html			\
	texinfo.css schema.sql unix/README util/mtnopt.in win32/README	\
	win32/monotone.iss win32/modpath.iss win32/monotone.bmp		\
									\
	$(PDF_FIGURES) $(PNG_FIGURES) $(TEXI_FRAGMENTS)			\
	figures/monotone-logo.svg figures/oo-figures.sxd		\
	figures/pdfcrop.pl figures/README				\
									\
	std_hooks.lua testlib.lua lua-testsuite.lua tests		\
	tester-testsuite.lua tester-tests unit-testsuite.lua		\
									\
	package_revision.txt package_full_revision_dist.txt		\
									\
	contrib cygwin examples mac notes visualc

MAKEINFOFLAGS=-I $(top_builddir)

monotone.info: monotone.texi version.texi std_hooks.lua $(TEXI_FRAGMENTS)

monotone.pdf: monotone.texi version.texi std_hooks.lua $(TEXI_FRAGMENTS) \
              $(PDF_FIGURES)

monotone.dvi: monotone.texi version.texi std_hooks.lua $(TEXI_FRAGMENTS) \
              $(EPS_FIGURES)

#%.eps: %.epsi
#	mv -f $< $@
#
#%.epsi: %.ps
#	ps2epsi $< $@

%.eps : %.ps
	ps2eps <$< >$@

$(top_builddir)/figures/%.ps: $(srcdir)/figures/%.pdf
	mkdir -p $(top_builddir)/figures
#	acroread -toPostScript -pairs $< $@
	pdftops $< $@


# Message translation support
# INST_LINGUAS is set by configure, based on the po/LINGUAS file and
# the user's LINGUAS env. variable.  Additional settings are in
# po/Makevars.
#
# Only the foo.po-update target will overwrite a file in the source
# po/ directory.

include $(srcdir)/po/Makevars
ALL_POFILES := $(wildcard $(srcdir)/po/*.po)
ALL_LINGUAS := $(basename $(notdir $(ALL_POFILES)))
ALL_GMOFILES := $(addsuffix .gmo, $(ALL_LINGUAS))
CLEAN_POFILES := $(ALL_GMOFILES) $(addsuffix .merged.po, $(ALL_LINGUAS)) \
		 $(PACKAGE).pot

EXTRA_DIST += $(ALL_POFILES) po/LINGUAS po/README

if USE_NLS
# for installation
GMOFILES := $(addsuffix .gmo, $(INST_LINGUAS))

all-local: $(GMOFILES)

install-data-local: all-local
	@linguas='$(INST_LINGUAS)'; set -ex; for lang in $$linguas; do \
	  dir=$(DESTDIR)$(localedir)/$$lang/LC_MESSAGES; \
	  $(MKDIR_P) $$dir; \
	  $(INSTALL_DATA) $$lang.gmo $$dir/$(PACKAGE).mo; \
	done

uninstall-local:
	@linguas='$(INST_LINGUAS)'; set -ex; for lang in $$linguas; do \
	  rm -f $(DESTDIR)$(localedir)/$$lang/LC_MESSAGES/$(PACKAGE).mo; \
	  rmdir $(DESTDIR)$(localedir)/$$lang/LC_MESSAGES || :; \
	  rmdir $(DESTDIR)$(localedir)/$$lang || :; \
	done
endif

if REBUILD_NLS
%.gmo: %.merged.po
	$(V_msgfmt)$(MSGFMT) -c --statistics -o $@ $<

%.merged.po: $(srcdir)/po/%.po $(PACKAGE).pot
	$(V_msgmerge)$(MSGMERGE) $^ -q -o $@

# for translators' use
UPDATEPOFILES = $(addsuffix .po-update, $(ALL_LINGUAS))

%.po-update: $(srcdir)/po/%.po $(PACKAGE).pot
	$(MSGMERGE) --update $^

update-po: $(UPDATEPOFILES)

# for the template file
POTFILES = $(filter-out package_%, $(MOST_SOURCES)) \
	$(UNIX_PLATFORM_SOURCES) $(WIN32_PLATFORM_SOURCES) \
	monotone.cc std_hooks.lua

$(PACKAGE).pot: $(POTFILES)
	$(V_xgettext)$(XGETTEXT) -o$@ -D$(top_srcdir) -cTRANSLATORS: \
	  --copyright-holder='$(COPYRIGHT_HOLDER)' \
	  --msgid-bugs-address='$(MSGID_BUGS_ADDRESS)' \
	  $(XGETTEXT_PKG_OPTS) $(XGETTEXT_OPTS) \
	  $(POTFILES)

dist-hook: $(ALL_GMOFILES)
	cp $^ $(distdir)/po
# also, kill off any backup files that got pulled in by one of the
# recursive subdirectory includes in EXTRA_DIST
# note use of slightly non-portable "-delete" action
	find $(distdir) \( -name '*.bak' -o -name '*.orig' -o -name '*.rej' \
	    -o -name '*~' -o -name '#*#' -o -name '*.swp' \) -delete

else

%.gmo: $(srcdir)/po/%.gmo
	cp $< $@

dist-hook:
	@echo "*** Cannot 'make dist' without xgettext" >&2; exit 1

endif

# build an OS X installer
# requires sudo since packagemaker just looks at directory permissions.
PKG_INST_ROOT = $(PWD)/$(top_builddir)/pkg_inst_root
monotone-$(PACKAGE_VERSION).pkg: monotone.pdf html
	sudo rm -rf $(PKG_INST_ROOT)
	rm -rf "$@"
	$(MAKE) install-strip DESTDIR=$(PKG_INST_ROOT)
	mkdir -p $(PKG_INST_ROOT)/Developer/Documentation/monotone
	cp monotone.pdf $(PKG_INST_ROOT)/Developer/Documentation/monotone/
	cp -r html $(PKG_INST_ROOT)/Developer/Documentation/monotone/html
	plutil -convert xml1 -o monotone-xml.pmproj.in $(srcdir)/mac/monotone.pmproj
	sed "s/__MONOTONE_VERSION__/$(PACKAGE_VERSION)/g" \
		< monotone-xml.pmproj.in > monotone-xml.pmproj
	cp $(srcdir)/mac/monotone-pkg-logo.gif $(srcdir)/mac/package-intro.rtf .
	cp $(srcdir)/COPYING COPYING.txt
	chmod -R o-w,g-w,a+rX $(PKG_INST_ROOT)
	sudo chown -R root:wheel $(PKG_INST_ROOT)/usr
	sudo chown -R root:admin $(PKG_INST_ROOT)/Developer
	/Developer/Applications/Utilities/PackageMaker.app/Contents/MacOS/PackageMaker \
		-build -proj monotone-xml.pmproj -p "$@"

# build an OS X distribution disk image
dmg: monotone-$(PACKAGE_VERSION).dmg

monotone-$(PACKAGE_VERSION).dmg: monotone-$(PACKAGE_VERSION).pkg
	rm -rf $(top_builddir)/dmg_root "$@"
	mkdir $(top_builddir)/dmg_root
	cp -R $^ $(top_builddir)/dmg_root/
	chmod -R a+rX $(top_builddir)/dmg_root
	hdiutil create -srcfolder "$(top_builddir)/dmg_root" \
		"$@" -format UDZO -scrub -imagekey zlib-level=9 \
		-volname monotone-$(PACKAGE_VERSION)

# testsuite stuff (could this possibly be more ugly?)  To get
# parallelism, we cannot use Automake's TESTS feature at all.  The
# shell script embedded in the 'check-local' rule is partially
# borrowed from automake 1.9's check.am

check-local: tester_tests.status unit_tests.status lua_tests.status
	@all=0; failed=0; error=0;					       \
	for f in $^; do							       \
	  all=`expr $$all + 1`;						       \
	  if test -f $$f; then						       \
	    s=`cat $$f`;						       \
	    if test "$$s" = 0; then					       \
	      :;							       \
	    elif test "$$s" = 1; then					       \
	      failed=`expr $$failed + 1`;				       \
	    else							       \
	      error=`expr $$error + 1`;					       \
	    fi;								       \
	  else								       \
	    error=`expr $$error + 1`;					       \
	  fi;								       \
	done;								       \
	if test "$$failed" = 0 && test "$$error" = 0; then		       \
	  exit=0;							       \
	  report="";							       \
	  banner="All $$all test suites passed";			       \
	else								       \
	  exit=1;							       \
	  report="Please report to $(PACKAGE_BUGREPORT)";		       \
	  if test "$$error" = 0; then					       \
	    banner="$$failed of $$all test suites failed";		       \
	  elif test "$$failed" = 0; then				       \
	    banner="$$error of $$all test suites malfunctioned";	       \
	  else								       \
	    banner="$$failed of $$all test suites failed"		       \
	    banner="$$banner and $$error malfunctioned";		       \
	  fi;								       \
	fi;								       \
	rl=`echo $$report | wc -c`;					       \
	bl=`echo $$banner | wc -c`;					       \
	if test $$rl -gt $$bl; then					       \
	  dashes=`echo "$$report" | sed s/./=/g`;			       \
	else								       \
	  dashes=`echo "$$banner" | sed s/./=/g`;			       \
	fi;								       \
	echo $$dashes;							       \
	echo $$banner;							       \
	test -z "$$report" || echo "$$report";				       \
	exit $$exit

run_%_tests: Makefile
	$(AM_V_GEN){ echo '#!/bin/sh';					  \
	  echo 'export LANG=C';						  \
	  echo 'PATH=$(top_builddir):$$PATH';				  \
	  echo '$(top_builddir)/tester $(srcdir)/$*-testsuite.lua "$$@"'; \
	  echo 'echo $$? > $*_tests.status';				  \
	  echo 'exit 0'; } > $@ && \
	chmod 755 $@

# The leading + causes Make to treat this as a recursive invocation,
# allowing it to participate in the jobserver protocol.
%_tests.status: run_%_tests %-testsuite.lua tester$(EXEEXT) FORCE
	+$(AM_V_at)./run_$*_tests

unit_tests.status : unit_tester$(EXEEXT)
lua_tests.status : mtn$(EXEEXT) check_net$(EXEEXT)
check_PROGRAMS = unit_tester tester check_net

# We want the tests re-run even if the .status files already exist.
# .PHONY does not work for that (bad interaction with pattern rules),
# but the FORCE hack does.
.PHONY: check-local FORCE
FORCE:
.PRECIOUS: run_tester_tests run_unit_tests run_lua_tests

mostlyclean-local:
	rm -rf tester_dir html
	set -ex; for d in unit-tests/*/__driver__.lua; do \
	  rm -f $$d; \
	  rmdir $${d%/*} || :; \
	done

DISTCLEANFILES = mt-stdint.h xgettext.opts pch-build.hh.gch.dep	\
	run_tester_tests run_unit_tests run_lua_tests		\
	tester_tests.status unit_tests.status lua_tests.status

# we generate some source files to copy data into the executable
# note that the only things that should go in BUILT_SOURCES are things
# that need to be generated early on 'make all'; this is _not_ true of
# generated source files, but it is true for the PCH if used.

# package_revision.txt and package_full_revision_dist.txt are shipped
# in the distribution, so don't delete them here
CLEAN_SOURCES = std_hooks.cc testlib.cc schema.cc \
	package_revision.cc package_full_revision.cc \
	package_revision_raw.txt \
	package_full_revision.txt package_full_revision_raw.txt

BUILT_SOURCES = $(PCH_FILE) $(PCH_BUILD)

CLEANFILES = $(bin_SCRIPTS) $(BUILT_SOURCES) $(CLEAN_SOURCES) \
	     $(EPS_FIGURES) $(CLEAN_POFILES)

# automake provides no nice way to build a helper program to execute
# on the build machine, so we need our own rule.  almost all the
# standard flags variables are inappropriate.

txt2c$(EXEEXT): txt2c.cc
	$(V_bcxx)$(CXX_FOR_BUILD) $(CXXFLAGS) -o $@ $^

# files generated using txt2c
std_hooks.cc: std_hooks.lua txt2c$(EXEEXT)
	$(V_txt2c)./txt2c std_hooks $< $@

testlib.cc: testlib.lua txt2c$(EXEEXT)
	$(V_txt2c)./txt2c testlib $< $@

schema.cc: schema.sql txt2c$(EXEEXT)
	$(V_txt2c)./txt2c schema $< $@

package_revision.cc: package_revision.txt txt2c$(EXEEXT)
	$(V_txt2c)./txt2c --strip-trailing package_revision $< $@

package_full_revision.cc: package_full_revision.txt txt2c$(EXEEXT)
	$(V_txt2c)./txt2c package_full_revision $< $@


# Support for scripts
do_subst = sed -e 's,[@]PACKAGE_VERSION[@],$(PACKAGE_VERSION),'

%: util/%.in
	$(V_subst)$(do_subst) < $< > $@ && chmod +x $@

# This is phony, so that we always try to rebuild it.  If it succeeds
# in calculating changes, it produces its target; otherwise, its
# target does not exist.
.PHONY: package_revision_raw.txt
# if both attempts fail, then we need to remove the empty file that >
# creates, and also make sure that the shell command exits
# successfully; the rm -f ensures both
package_revision_raw.txt:
	$(AM_V_GEN)REAL_BLDDIR=$$PWD/$(top_builddir); \
	(cd $(srcdir) && $$REAL_BLDDIR/mtn --root=. automate get_base_revision_id) 2>/dev/null >$@ \
	|| (cd $(srcdir) && mtn --root=. automate get_base_revision_id) 2>/dev/null >$@ \
	|| rm -f $@
package_revision.txt: package_revision_raw.txt
	$(AM_V_GEN)set -e; if [ -f $< ]; then \
	  cp $< $@; \
	fi; \
	if [ ! -f $@ -a -f $(srcdir)/$@ ]; then \
	  cp $(srcdir)/$@ $@; \
	fi; \
	[ -f $@ ] || echo "unknown" > $@

# To avoid a dependency loop here, what we do is:
#           calculate changes
#                  |
#      package_full_revision_raw.txt
#              |                \  <-- add "generated from cached data" text
#              |      package_full_revision_dist.txt
#              |           /
#          package_full_revision.txt

# See above comment.
.PHONY: package_full_revision_raw.txt
package_full_revision_raw.txt:
	$(AM_V_GEN)REAL_BLDDIR=$$PWD/$(top_builddir); \
	(cd $(srcdir) && $$REAL_BLDDIR/mtn --root=. automate get_current_revision) 2>/dev/null >$@ \
	|| (cd $(srcdir) && mtn --root=. automate get_current_revision) 2>/dev/null >$@ \
	|| rm -f $@
# See above comment -- the file this depends on may or may not exist.
# This file, however, must always exist, so that 'make dist' can
# include it in the tarball it generates.  So there are three cases:
#   -- if the raw file does not exist, then
#      -- if the dist file exists, then we leave it alone
#      -- if the dist file does not exist, then we say "unknown"
#   -- if the raw file does exist, then we regenerate the dist file
package_full_revision_dist.txt: package_full_revision_raw.txt
	$(AM_V_GEN)set -e; if [ -f $< ]; then \
	  cp -f $< $@ \
	  && (echo ''; \
	      echo '  Generated from data cached in the distribution;'; \
	      echo '  further changes may have been made.') >> $@; \
	fi; \
	if [ ! -f $@ -a -f $(srcdir)/$@ ]; then \
	  cp $(srcdir)/$@ $@; \
	fi; \
	[ -f $@ ] || echo "unknown" > $@
# The raw file may not exist, but the dist file definitely does; we
# just take the first dependency that exists.
package_full_revision.txt: package_full_revision_raw.txt package_full_revision_dist.txt
	$(AM_V_GEN)rm -f $@; \
	for SRC in $^; do \
	    ([ -f $$SRC -a ! -f $@ ] && cp -f $$SRC $@) || true; \
	done

# This is a magic directive copy-and-pasted, then modified, from the
# automake 1.9 manual, section 13.4, "Checking the distribution".
# Normally, 'distcheck' does a clean build, and then afterwards runs
# 'distclean', and 'distclean' is supposed to remove everything that
# the build created.  However, we have some targets
# (package_revision.txt, package_full_revision_dist.txt) that we
# distribute, but then always attempt to rebuild optimistically, and
# then if that fails fall back on the distributed versions.  This
# means that 'distclean' should _not_ remove those files, since they
# are distributed, yet building the package will generate those files,
# thus automake thinks that 'distclean' _should_ remove those files,
# and 'distcheck' gets cranky if we don't.  So basically what this
# line does is tell 'distcheck' to shut up and ignore those two files.
distcleancheck_listfiles = find . -type f -a ! -name package_revision.txt -a ! -name package_full_revision_dist.txt

# the distcheck-hook checks for errors in the use of base.hh
distcheck-hook:
	cd $(srcdir) && $(SHELL) audit-includes \
		$(sort $(mtn_SOURCES) $(unit_tester_SOURCES) $(tester_SOURCES) \
                       $(UNIX_PLATFORM_SOURCES) $(WIN32_PLATFORM_SOURCES) \
                       win32/main.cc unix/main.cc)

# automake doesn't build html docs

CHECK_SRCDIR_EQ_BUILDDIR=[ "`cd \"$(srcdir)\"; pwd`" = "`pwd`" -a -f ./monotone.texi ]

.PHONY: apidocs
apidocs:
	doxygen doxygen.cfg

monotone.html: monotone.texi version.texi $(TEXI_FRAGMENTS) \
	       std_hooks.lua texinfo.css
	$(V_makeinfo)set -e; if $(CHECK_SRCDIR_EQ_BUILDDIR); then :; else \
		cp -r $(srcdir)/figures .; \
		cp $(srcdir)/texinfo.css .; \
	fi; \
	makeinfo -I $(srcdir) --no-split --no-headers --output $@ --html $<; \
	cp $@ $@.bak; \
	sed -e 's,<head>,<head><link type="text/css" rel="stylesheet" href="texinfo.css" />,' $@.bak >$@; \
	rm -f $@.bak

html: monotone.texi version.texi std_hooks.lua texinfo.css
	$(V_makeinfo)set -e; if $(CHECK_SRCDIR_EQ_BUILDDIR); then :; else \
		cp -r $(srcdir)/figures .; \
	fi; \
	mkdir -p $@; \
	cp -r $(srcdir)/figures $@; \
	cp $(srcdir)/texinfo.css $@; \
	makeinfo -I $(srcdir) --number-sections --html --output $@ $<; \
	for f in $@/*.html; do \
		cp $$f $$f.bak; \
		sed -e 's,<head>,<head><link type="text/css" rel="stylesheet" href="texinfo.css" />,' $$f.bak >$$f; \
		rm -f $$f.bak; \
	done

# Verbosity goo.
V_bcxx       = $(V_bcxx_$(V))
V_bcxx_      = $(V_bcxx_$(AM_DEFAULT_VERBOSITY))
V_bcxx_0     = @echo "  BCXX  " $@;

V_txt2c      = $(V_txt2c_$(V))
V_txt2c_     = $(V_txt2c_$(AM_DEFAULT_VERBOSITY))
V_txt2c_0    = @echo "  TXT2C " $@;

V_subst      = $(V_subst_$(V))
V_subst_     = $(V_subst_$(AM_DEFAULT_VERBOSITY))
V_subst_0    = @echo "  SUBST " $@;

V_xgettext   = $(V_xgettext_$(V))
V_xgettext_  = $(V_xgettext_$(AM_DEFAULT_VERBOSITY))
V_xgettext_0 = @echo "XGETTEXT" $@;

V_msgfmt     = $(V_msgfmt_$(V))
V_msgfmt_    = $(V_msgfmt_$(AM_DEFAULT_VERBOSITY))
V_msgfmt_0   = @echo "  MSGFMT" $@;

V_msgmerge   = $(V_msgmerge_$(V))
V_msgmerge_  = $(V_msgmerge_$(AM_DEFAULT_VERBOSITY))
V_msgmerge_0 = @echo "MSGMERGE" $@;

V_makeinfo   = $(V_makeinfo_$(V))
V_makeinfo_  = $(V_makeinfo_$(AM_DEFAULT_VERBOSITY))
V_makeinfo_0 = @echo "MAKEINFO" $@;
