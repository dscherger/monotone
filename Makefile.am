AUTOMAKE_OPTIONS=subdir-objects 1.7.1
ACLOCAL_AMFLAGS = -I m4

CMD_SOURCES =								\
	cmd.hh cmd_netsync.cc cmd_list.cc cmd_packet.cc	cmd_key_cert.cc \
	cmd_merging.cc cmd_db.cc cmd_diff_log.cc cmd_ws_commit.cc 	\
	cmd_othervcs.cc cmd_automate.cc cmd_files.cc cmd_conflicts.cc

SANITY_CORE_SOURCES =							\
	sanity.cc sanity.hh quick_alloc.hh vector.hh base.hh		\
	simplestring_xform.cc simplestring_xform.hh 			\
	constants.cc constants.hh numeric_vocab.hh 			\
	platform.hh numeric_vocab.cc

LUAEXT_SOURCES = 							\
	vocab.hh vocab.cc vocab_terms.hh vocab_macros.hh vocab_cast.hh  \
	charset.cc charset.hh paths.cc paths.hh				\
	interner.hh hash_map.hh	vocab_hash.hh				\
	luaext_mkstemp.cc luaext_parse_basic_io.cc			\
	luaext_guess_binary.cc luaext_platform.cc luaext_globish.cc	\
	lua.cc lua.hh mkstemp.cc mkstemp.hh file_io.cc file_io.hh	\
	globish.cc globish.hh basic_io.cc basic_io.hh			\
	char_classifiers.hh

MOST_SOURCES = 								\
	$(SANITY_CORE_SOURCES) $(LUAEXT_SOURCES) platform-wrapped.hh	\
	rev_types.hh mtn-sanity.cc mtn-sanity.hh ui.cc ui.hh		\
	app_state.cc app_state.hh					\
	botan_pipe_cache.hh						\
	commands.cc commands.hh $(CMD_SOURCES)				\
	diff_patch.cc diff_patch.hh					\
	lua_hooks.cc lua_hooks.hh 					\
	transforms.cc transforms.hh					\
	update.cc update.hh						\
	work.cc work_migration.cc work.hh				\
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
	schema_migration.cc schema_migration.hh				\
	refiner.cc refiner.hh						\
	enumerator.cc enumerator.hh					\
	netsync.cc            						\
	netxx_pipe.cc netxx_pipe.hh					\
	netcmd.cc netcmd.hh						\
	merkle_tree.cc merkle_tree.hh 					\
	lcs.cc lcs.hh							\
	rcs_import.cc rcs_import.hh 					\
	revision.cc revision.hh						\
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
	string_queue.cc string_queue.hh					\
	roster_merge.cc roster_merge.hh					\
	merge.cc merge.hh						\
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
	randomizer.cc randomizer.hh					\
	netio.hh gettext.h						\
	package_revision.cc package_revision.h				\
	package_full_revision.cc package_full_revision.h		\
	option.cc option.hh options.cc options.hh options_list.hh	\
	specialized_lexical_cast.cc lexical_cast.hh			\
	i18n.h parallel_iter.hh safe_map.hh pch.hh current_exception.hh

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

# these files contain unit tests
UNIT_TEST_SOURCES =							\
	basic_io.cc charset.cc commands.cc crypto_tests.cc cset.cc	\
	dates.cc diff_patch.cc globish.cc graph.cc mkstemp.cc netcmd.cc	\
	netxx_pipe.cc numeric_vocab.cc option.cc outdated_indicator.cc	\
	packet.cc paths.cc refiner.cc restrictions.cc rev_height.cc	\
	revision.cc roster.cc roster_merge.cc simplestring_xform.cc	\
	string_queue.cc transforms.cc unit_tests.cc uri.cc vocab.cc	\
	xdelta.cc

# these files do not contain unit tests, but are required for unit testing
# and must be recompiled for that purpose
UNIT_TEST_SRC_SUPPORT =							\
	roster_delta.cc

# these files do not contain unit tests; they are required for unit
# testing, but can be used "as is" from the main build.  (many of
# these _should_ have unit tests, but they haven't been written yet.)
UNIT_TEST_OBJ_SUPPORT =							\
	mtn-app_state.$(OBJEXT) mtn-cert.$(OBJEXT)			\
	mtn-constants.$(OBJEXT) mtn-database.$(OBJEXT)			\
	mtn-epoch.$(OBJEXT) mtn-file_io.$(OBJEXT) mtn-gzip.$(OBJEXT)	\
	mtn-hmac.$(OBJEXT) mtn-inodeprint.$(OBJEXT)			\
	mtn-key_store.$(OBJEXT) mtn-keys.$(OBJEXT) mtn-lcs.$(OBJEXT)	\
	mtn-legacy.$(OBJEXT) mtn-lua.$(OBJEXT) mtn-lua_hooks.$(OBJEXT)	\
	mtn-merkle_tree.$(OBJEXT) mtn-mt_version.$(OBJEXT)		\
	mtn-mtn-sanity.$(OBJEXT) mtn-options.$(OBJEXT)			\
	mtn-package_full_revision.$(OBJEXT)				\
	mtn-package_revision.$(OBJEXT) mtn-pcrewrap.$(OBJEXT)		\
	mtn-project.$(OBJEXT) mtn-randomizer.$(OBJEXT)			\
	mtn-sanity.$(OBJEXT) mtn-schema.$(OBJEXT)			\
	mtn-schema_migration.$(OBJEXT)					\
	mtn-specialized_lexical_cast.$(OBJEXT) mtn-ssh_agent.$(OBJEXT)	\
	mtn-std_hooks.$(OBJEXT) mtn-ui.$(OBJEXT) mtn-work.$(OBJEXT)	\
	mtn-work_migration.$(OBJEXT)

# primaries

bin_PROGRAMS = mtn
noinst_PROGRAMS = txt2c
EXTRA_PROGRAMS =

dist_bin_SCRIPTS = mtnopt

mtn_SOURCES = $(MOST_SOURCES) monotone.cc
nodist_mtn_SOURCES = std_hooks.cc schema.cc

unit_tester_SOURCES = $(UNIT_TEST_SOURCES) $(UNIT_TEST_SRC_SUPPORT)
unit_tester_LDADD = $(UNIT_TEST_OBJ_SUPPORT)

tester_SOURCES = tester.cc transforms.cc gzip.cc
nodist_tester_SOURCES = testlib.cc
tester_LDADD = $(addprefix mtn-, $(patsubst %.cc, %.$(OBJEXT), \
		 $(filter %.cc, $(SANITY_CORE_SOURCES) $(LUAEXT_SOURCES) \
		 	  	pcrewrap.cc)))

txt2c_SOURCES = txt2c.cc

noinst_LIBRARIES = libplatform.a lib3rdparty.a
libplatform_a_SOURCES = platform.hh tester-plaf.hh
lib3rdparty_a_SOURCES = $(BOOST_SANDBOX_SOURCES) \
			$(NETXX_SOURCES)
lib3rdparty_a_CPPFLAGS = # may be augmented in conditionals

htmldir = $(datadir)/doc/monotone
html_DATA = monotone.html

# flags

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

SUFFIXES = .gch

EXTERNAL_CPPFLAGS = $(pcre_CFLAGS) $(botan_CFLAGS) \
		    $(lua_CFLAGS) $(sqlite_CFLAGS) $(idn_CFLAGS)
EXTERNAL_LIBS = $(BOOSTLIBS) $(LIBICONV) $(LIBINTL) \
	        $(pcre_LIBS) $(botan_LIBS) $(lua_LIBS) $(sqlite_LIBS) \
	        $(idn_LIBS)

mtn_CPPFLAGS = $(EXTERNAL_CPPFLAGS)
mtn_CXXFLAGS = $(PCH_FLAGS)
mtn_LDADD = libplatform.a lib3rdparty.a $(EXTERNAL_LIBS)

unit_tester_CPPFLAGS = -DBUILD_UNIT_TESTS $(EXTERNAL_CPPFLAGS)
unit_tester_CXXFLAGS = $(PCH_FLAGS)
unit_tester_LDADD += libplatform.a lib3rdparty.a $(EXTERNAL_LIBS)

tester_CPPFLAGS = $(EXTERNAL_CPPFLAGS)
tester_LDADD += libplatform.a lib3rdparty.a $(EXTERNAL_LIBS)

if WIN32_PLATFORM
  libplatform_a_SOURCES += $(WIN32_PLATFORM_SOURCES)
  mtn_SOURCES += win32/main.cc
  mtn_LDADD += -lshfolder -lws2_32 -lintl -liconv -liphlpapi
  unit_tester_LDADD += -lshfolder -lws2_32 -lintl -liconv -liphlpapi
  lib3rdparty_a_CPPFLAGS += -DWIN32
  tester_SOURCES += win32/tester-plaf.cc
  check_net_SOURCES = win32/tester-check-net.cc
else
  libplatform_a_SOURCES += $(UNIX_PLATFORM_SOURCES)
  mtn_SOURCES += unix/main.cc
  tester_SOURCES += unix/tester-plaf.cc
  check_net_SOURCES = unix/tester-check-net.cc
endif

if MISSING_INET_PTON
  lib3rdparty_a_SOURCES += netxx/inet_pton.cxx netxx/inet_pton.h
  lib3rdparty_a_CPPFLAGS += -DNETXX_NO_PTON
endif

if MISSING_INET_NTOP
  lib3rdparty_a_SOURCES += netxx/inet_ntop.cxx netxx/inet_ntop.h
  lib3rdparty_a_CPPFLAGS += -DNETXX_NO_NTOP
endif

if MISSING_GETADDRINFO
  lib3rdparty_a_SOURCES += netxx/resolve_gethostbyname.cxx netxx/resolve_getservbyname.cxx
else
  lib3rdparty_a_SOURCES += netxx/resolve_getaddrinfo.cxx
endif

if MISSING_INET6
  lib3rdparty_a_CPPFLAGS += -DNETXX_NO_INET6
endif

# i18n support

SUBDIRS = po
datadir = @datadir@
localedir = $(datadir)/locale
DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@

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
EXTRA_DIST = $(PDF_FIGURES) $(PNG_FIGURES) INSTALL README.changesets	\
	UPGRADE monotone.spec monotone.html texinfo.css schema.sql	\
	std_hooks.lua testlib.lua lua-testsuite.lua $(srcdir)/tests	\
	tester-testsuite.lua $(srcdir)/tester-tests			\
	unit-testsuite.lua package.m4 package_revision.txt		\
	package_full_revision_dist.txt $(wildcard $(srcdir)/m4/*.m4)	\
	contrib/README contrib/monoprof.sh				\
	contrib/monotone-notify.pl contrib/ciabot_monotone.py		\
	contrib/monotone.el contrib/monotone-nav.el			\
	contrib/monotone.bash_completion				\
	contrib/monotone.zsh_completion contrib/mtbrowse.sh		\
	contrib/colorize contrib/color-logs.sh contrib/color-logs.conf	\
	contrib/get_stdio.pl contrib/ciabot_monotone_hookversion.py	\
	contrib/Monotone.pm $(wildcard $(srcdir)/contrib/*.lua)		\
	$(wildcard $(srcdir)/contrib/command/*.lua)			\
	contrib/command/README $(wildcard $(srcdir)/examples/*)		\
	win32/monotone.iss win32/modpath.iss $(wildcard			\
	$(srcdir)/debian/*) mac/monotone-pkg-logo.gif			\
	mac/monotone.pmproj mac/package-intro.rtf audit-includes	\
	$(TEXI_FRAGMENTS)

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
	{ echo '#!/bin/sh';						  \
	  echo 'PATH=$(top_builddir):$$PATH';				  \
	  echo '$(top_builddir)/tester $(srcdir)/$*-testsuite.lua "$$@"'; \
	  echo 'echo $$? > $*_tests.status';				  \
	  echo 'exit 0'; } > $@
	chmod 755 $@

# The leading + causes Make to treat this as a recursive invocation,
# allowing it to participate in the jobserver protocol.
%_tests.status: run_%_tests %-testsuite.lua tester$(EXEEXT) FORCE
	+./run_$*_tests

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
	rm -rf tester_dir unit-tests html

DISTCLEANFILES = mt-stdint.h xgettext.opts pch-build.hh.gch.dep	\
	run_tester_tests run_unit_tests run_lua_tests		\
	tester_tests.status unit_tests.status lua_tests.status  \
	mtnopt

# note: these targets are all in srcdir; they are *not* built during
# the VPATH build part of distcheck (with non-writable source
# directory) but only during the initial dist stage.

$(srcdir)/package.m4: $(top_srcdir)/configure.ac
	{                                      \
	  echo '# Signature of the current package.'; \
	  echo 'm4_define([AT_PACKAGE_NAME],      [@PACKAGE_NAME@])'; \
	  echo 'm4_define([AT_PACKAGE_TARNAME],   [@PACKAGE_TARNAME@])'; \
	  echo 'm4_define([AT_PACKAGE_VERSION],   [@PACKAGE_VERSION@])'; \
	  echo 'm4_define([AT_PACKAGE_STRING],    [@PACKAGE_STRING@])'; \
	  echo 'm4_define([AT_PACKAGE_BUGREPORT], [@PACKAGE_BUGREPORT@])'; \
	} >$(srcdir)/package.m4

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

CLEANFILES = $(BUILT_SOURCES) $(CLEAN_SOURCES) $(EPS_FIGURES)

txt2c: txt2c.cc
	$(CXX) $(CXXFLAGS) -o $@ $<
	chmod 0755 $@$(EXEEXT)

# FIXME: should use stamp files.
%.cc: %.sql txt2c
	./txt2c $(TXT2CFLAGS) --no-static $< $(*F) >$@.tmp
	cmp -s $@.tmp $@ || mv -f $@.tmp $@
	rm -f $@.tmp

%.cc: %.lua txt2c
	./txt2c $(TXT2CFLAGS) --no-static $< $(*F) >$@.tmp
	cmp -s $@.tmp $@ || mv -f $@.tmp $@
	rm -f $@.tmp

%.cc: %.txt txt2c
	./txt2c $(TXT2CFLAGS) --no-static $< $(*F) >$@.tmp
	cmp -s $@.tmp $@ || mv -f $@.tmp $@
	rm -f $@.tmp

# Support for scripts
%: util/%
	cp $< $@

# This construct causes --strip-trailing to be applied only when
# generating package_revision.cc.
TXT2CFLAGS =
package_revision.cc : TXT2CFLAGS = --strip-trailing

# This is phony, so that we always try to rebuild it.  If it succeeds
# in calculating changes, it produces its target; otherwise, its
# target does not exist.
.PHONY: package_revision_raw.txt
# if both attempts fail, then we need to remove the empty file that >
# creates, and also make sure that the shell command exits
# successfully; the rm -f ensures both
package_revision_raw.txt:
	REAL_BLDDIR=$$PWD/$(top_builddir); \
	(cd $(srcdir) && $$REAL_BLDDIR/mtn --root=. automate get_base_revision_id) 2>/dev/null >$@ \
	|| (cd $(srcdir) && mtn --root=. automate get_base_revision_id) 2>/dev/null >$@ \
	|| rm -f $@
package_revision.txt: package_revision_raw.txt
	if [ -f $< ]; then \
	  cp $< $@; \
	fi
	if [ ! -f $@ -a -f $(srcdir)/$@ ]; then \
	  cp $(srcdir)/$@ $@; \
	fi
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
	REAL_BLDDIR=$$PWD/$(top_builddir); \
	(cd $(srcdir) && $$REAL_BLDDIR/mtn --root=. automate get_revision) 2>/dev/null >$@ \
	|| (cd $(srcdir) && mtn --root=. automate get_revision) 2>/dev/null >$@ \
	|| rm -f $@
# See above comment -- the file this depends on may or may not exist.
# This file, however, must always exist, so that 'make dist' can
# include it in the tarball it generates.  So there are three cases:
#   -- if the raw file does not exist, then
#      -- if the dist file exists, then we leave it alone
#      -- if the dist file does not exist, then we say "unknown"
#   -- if the raw file does exist, then we regenerate the dist file
package_full_revision_dist.txt: package_full_revision_raw.txt
	if [ -f $< ]; then \
	  cp -f $< $@ \
	  && (echo ''; \
	      echo '  Generated from data cached in the distribution;'; \
	      echo '  further changes may have been made.') >> $@; \
	fi
	if [ ! -f $@ -a -f $(srcdir)/$@ ]; then \
	  cp $(srcdir)/$@ $@; \
	fi
	[ -f $@ ] || echo "unknown" > $@
# The raw file may not exist, but the dist file definitely does; we
# just take the first dependency that exists.
package_full_revision.txt: package_full_revision_raw.txt package_full_revision_dist.txt
	rm -f $@
	for SRC in $^; do ([ -f $$SRC -a ! -f $@ ] && cp -f $$SRC $@) || true; done

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
	if $(CHECK_SRCDIR_EQ_BUILDDIR); then :; else \
		cp -r $(srcdir)/figures .; \
		cp $(srcdir)/texinfo.css .; \
	fi
	makeinfo -I $(srcdir) --no-split --no-headers --output $@ --html $<
	cp $@ $@.bak
	sed -e 's,<head>,<head><link type="text/css" rel="stylesheet" href="texinfo.css" />,' $@.bak >$@
	rm -f $@.bak

html: monotone.texi version.texi std_hooks.lua texinfo.css
	if $(CHECK_SRCDIR_EQ_BUILDDIR); then :; else \
		cp -r $(srcdir)/figures .; \
	fi
	mkdir -p $@
	cp -r $(srcdir)/figures $@
	cp $(srcdir)/texinfo.css $@
	makeinfo -I $(srcdir) --number-sections --html --output $@ $<
	for f in $@/*.html; do \
		cp $$f $$f.bak; \
		sed -e 's,<head>,<head><link type="text/css" rel="stylesheet" href="texinfo.css" />,' $$f.bak >$$f; \
		rm -f $$f.bak; \
	done
