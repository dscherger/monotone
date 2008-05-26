CMD_SOURCES =								\
	cmd.hh cmd_netsync.cc cmd_list.cc cmd_packet.cc	cmd_key_cert.cc \
	cmd_merging.cc cmd_db.cc cmd_diff_log.cc cmd_ws_commit.cc 	\
	cmd_othervcs.cc cmd_automate.cc cmd_files.cc

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
	sha1.hh sha1.cc sha1_engine.hh                                  \
	pcrewrap.cc pcrewrap.hh						\
	rev_height.cc rev_height.hh					\
	asciik.cc asciik.hh						\
	dates.cc dates.hh						\
									\
	lru_writeback_cache.hh hybrid_map.hh				\
									\
	cleanup.hh unit_tests.hh					\
	cycle_detector.hh randomfile.hh adler32.hh			\
	randomizer.cc randomizer.hh					\
	netio.hh smap.hh gettext.h					\
	package_revision.c package_revision.h				\
	package_full_revision.c package_full_revision.h			\
	option.cc option.hh options.cc options.hh options_list.hh	\
	specialized_lexical_cast.cc lexical_cast.hh			\
	i18n.h parallel_iter.hh safe_map.hh pch.hh current_exception.hh

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
	dates.cc diff_patch.cc globish.cc graph.cc netcmd.cc		\
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
	app_state.$(OBJEXT) cert.$(OBJEXT) constants.$(OBJEXT)		\
	database.$(OBJEXT) epoch.$(OBJEXT) file_io.$(OBJEXT)		\
	gzip.$(OBJEXT) hmac.$(OBJEXT) inodeprint.$(OBJEXT)		\
	key_store.$(OBJEXT) keys.$(OBJEXT) lcs.$(OBJEXT)		\
	legacy.$(OBJEXT) lua.$(OBJEXT) lua_hooks.$(OBJEXT)		\
	merkle_tree.$(OBJEXT) mt_version.$(OBJEXT)			\
	mtn-sanity.$(OBJEXT) options.$(OBJEXT)				\
	package_full_revision.$(OBJEXT) package_revision.$(OBJEXT)	\
	pcrewrap.$(OBJEXT) project.$(OBJEXT) randomizer.$(OBJEXT)	\
	sanity.$(OBJEXT) schema.$(OBJEXT) schema_migration.$(OBJEXT)	\
	specialized_lexical_cast.$(OBJEXT) ssh_agent.$(OBJEXT)		\
	std_hooks.$(OBJEXT) ui.$(OBJEXT) work.$(OBJEXT)			\
	work_migration.$(OBJEXT)

# primaries
bin_PROGRAMS = mtn
bin_SCRIPTS = mtnopt

mtn_SOURCES = $(MOST_SOURCES) monotone.cc
nodist_mtn_SOURCES = std_hooks.c schema.c
mtn_LDADD = libplatform.a $(LIBICONV) $(LIBINTL)

# build tools
noinst_PROGRAMS = txt2c

txt2c_SOURCES = txt2c.cc

# testing tools
check_PROGRAMS = unit_tester tester check_net

unit_tester_SOURCES = $(UNIT_TEST_SOURCES) $(UNIT_TEST_SRC_SUPPORT)
unit_tester_CPPFLAGS = -DBUILD_UNIT_TESTS
unit_tester_LDADD = $(UNIT_TEST_OBJ_SUPPORT) \
	libplatform.a $(LIBICONV) $(LIBINTL)

tester_SOURCES = tester.cc transforms.cc gzip.cc pcrewrap.cc \
		 $(SANITY_CORE_SOURCES) $(LUAEXT_SOURCES)
nodist_tester_SOURCES = testlib.c
tester_LDADD = libplatform.a $(LIBICONV) $(LIBINTL)

# platform goo
noinst_LIBRARIES = libplatform.a
libplatform_a_SOURCES = platform.hh tester-plaf.hh

if WIN32_PLATFORM
  libplatform_a_SOURCES += $(WIN32_PLATFORM_SOURCES)
  mtn_SOURCES += win32/main.cc
  tester_SOURCES += win32/tester-plaf.cc
  mtn_LDADD += -lshfolder -lws2_32 -lintl -liconv -liphlpapi
  unit_tester_LDADD += -lshfolder -lws2_32 -lintl -liconv -liphlpapi
  check_net_SOURCES = win32/tester-check-net.c
else
  libplatform_a_SOURCES += $(UNIX_PLATFORM_SOURCES)
  mtn_SOURCES += unix/main.cc
  tester_SOURCES += unix/tester-plaf.cc
  check_net_SOURCES = unix/tester-check-net.c
endif

# i18n
SUBDIRS = po
unix/fs.o : CPPFLAGS += -DLOCALEDIR=\"$(localedir)\"

# testsuite stuff (could this possibly be more ugly?)  To get
# parallelism, we cannot use Automake's TESTS feature at all.

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

check-local: tester_tests.status unit_tests.status lua_tests.status
	PACKAGE_BUGREPORT=$(PACKAGE_BUGREPORT) \
	  $(SHELL) $(srcdir)/util/summarize-tests $^

# We want the tests re-run even if the .status files already exist.
# .PHONY does not work for that (bad interaction with pattern rules),
# but the FORCE hack does.
.PHONY: check-local FORCE
FORCE:
.PRECIOUS: run_tester_tests run_unit_tests run_lua_tests

mostlyclean-local:
	rm -rf tester_dir unit-tests html

DISTCLEANFILES = mt-stdint.h xgettext.opts			\
	run_tester_tests run_unit_tests run_lua_tests		\
	tester_tests.status unit_tests.status lua_tests.status

# we generate some source files to copy data into the executable
# note that the only things that should go in BUILT_SOURCES are things
# that need to be generated early on 'make all'; this is _not_ true of
# these generated source files

# package_revision.txt and package_full_revision_dist.txt are shipped
# in the distribution, so don't delete them here
CLEANFILES = std_hooks.c testlib.c schema.c \
	package_revision.c package_full_revision.c \
	package_revision_raw.txt \
	package_full_revision.txt package_full_revision_raw.txt 

# FIXME: should use stamp files.
%.c: %.sql txt2c
	./txt2c $(TXT2CFLAGS) --no-static $< $(*F) >$@.tmp
	cmp -s $@.tmp $@ || mv -f $@.tmp $@
	rm -f $@.tmp

%.c: %.lua txt2c
	./txt2c $(TXT2CFLAGS) --no-static $< $(*F) >$@.tmp
	cmp -s $@.tmp $@ || mv -f $@.tmp $@
	rm -f $@.tmp

%.c: %.txt txt2c
	./txt2c $(TXT2CFLAGS) --no-static $< $(*F) >$@.tmp
	cmp -s $@.tmp $@ || mv -f $@.tmp $@
	rm -f $@.tmp

# Support for scripts
%: util/%
	cp $< $@

# This construct causes --strip-trailing to be applied only when
# generating package_revision.c.
TXT2CFLAGS =
package_revision.c : TXT2CFLAGS = --strip-trailing

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

# This is necessary because autoreconf doesn't pay attention to
# AC_CONFIG_MACRO_DIR.
ACLOCAL_AMFLAGS = -I ../m4
