AUTOMAKE_OPTIONS=subdir-objects no-dist 1.7.1

lib_LIBRARIES = liblua.a
include_HEADERS = lua.h luaconf.h lualib.h lauxlib.h
liblua_a_SOURCES =							\
	lapi.cc lapi.h lauxlib.cc lbaselib.cc lcode.cc lcode.h		\
	ldblib.cc ldebug.cc ldebug.h ldo.cc ldo.h ldump.cc lfunc.cc	\
	lfunc.h lgc.cc lgc.h linit.cc liolib.cc llex.cc llex.h		\
	llimits.h lmathlib.cc lmem.cc lmem.h loadlib.cc lobject.cc	\
	lobject.h lopcodes.cc lopcodes.h loslib.cc lparser.cc		\
	lparser.h lstate.cc lstate.h lstring.cc lstring.h lstrlib.cc	\
	ltable.cc ltable.h ltablib.cc ltests.cc ltm.cc ltm.h		\
	lundump.cc lundump.h lvm.cc lvm.h lzio.cc lzio.h

liblua_a_CPPFLAGS =
liblua_a_CFLAGS = $(AM_CFLAGS) $(LIB3RDPARTY_CFLAGS)
liblua_a_CXXFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir) $(LIB3RDPARTY_CXXFLAGS)

