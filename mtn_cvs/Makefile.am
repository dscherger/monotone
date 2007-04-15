AM_CXXFLAGS=-I. -I.. -DLIBMTN_COMPILE

noinst_LIBRARIES=libmtn.a

libmtn_a_SOURCES=../netxx_pipe.cc ../sanity.cc ../ui.cc \
	../transforms.cc ../vocab.cc ../paths.cc ../charset.cc \
	../simplestring_xform.cc ../constants.cc ../xdelta.cc \
	../commands.cc ../basic_io.cc botan/md5.cpp piece_table.cc \
	../mtn-sanity.cc ../package_revision.c ../option.cc \
	../file_io.cc ../ssh_agent.cc

bin_PROGRAMS = mtn_cvs

mtn_cvs_SOURCES = options.cc cvs_sync.cc cvs_client.cc mtn_cvs.cc \
		mtn_pipe.cc mtn_automate.cc mtncvs_state.cc \
		cvs_revision_nr.cc cvs_edge.cc ../unix/main.cc
AM_LDFLAGS = -L. -L..
mtn_cvs_LDADD = -lmtn -lplatform -l3rdparty $(BOOSTLIBS) $(LIBICONV) $(LIBINTL)

pipetest: mtn_pipe.cc
	$(CXX) $(CXXFLAGS) $(AM_CXXFLAGS) $(AM_LDFLAGS) $(LIBS) \
			 -o $@ -DTEST_MAIN $^ $(mtn_cvs_LDADD)

testlinks:
	cd tests ; ln -sf ../../tests/common ../../tests/min_hooks.lua \
		../../tests/test_hooks.lua ../../tests/test_keys .

TESTS = run_lua_tests
TESTS_ENVIRONMENT=AUTOTEST_PATH="."

run_lua_tests: Makefile
	echo '#!/bin/sh' > $@ ; \
	echo 'PATH=$(top_builddir):$$PATH' >> $@ ; \
	echo 'exec $(top_builddir)/tester $(srcdir)/testsuite.lua "$$@"' >> $@ ; \
	chmod 755 $@

