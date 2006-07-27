AM_CXXFLAGS=-I.. -DLIBMTN_COMPILE

noinst_LIBRARIES=libmtn.a

libmtn_a_SOURCES=../netxx_pipe.cc ../sanity.cc ../ui.cc \
	../transforms.cc ../vocab.cc ../paths.cc ../charset.cc \
	../simplestring_xform.cc ../constants.cc ../xdelta.cc \
	../commands.cc ../basic_io.cc md5.cpp piece_table.cc \
	../mtn-sanity.cc

bin_PROGRAMS = mtn_cvs

mtn_cvs_SOURCES = cvs_sync.cc cvs_client.cc mtn_cvs.cc mtn_pipe.cc
AM_LDFLAGS = -L. -L..
mtn_cvs_LDADD = -lmtn -lplatform -l3rdparty

pipetest: mtn_pipe.cc
	$(CXX) $(CXXFLAGS) $(AM_CXXFLAGS) $(AM_LDFLAGS) $(LIBS) \
			 -o $@ -DTEST_MAIN $^ $(mtn_cvs_LDADD)
