AUTOMAKE_OPTIONS=subdir-objects no-dist 1.7.1

lib_LIBRARIES = libsqlite.a
include_HEADERS = sqlite3.h sqlite3ext.h
libsqlite_a_SOURCES =							\
	alter.c analyze.c attach.c auth.c btree.c btree.h btreeInt.h	\
	build.c callback.c complete.c date.c delete.c expr.c func.c	\
	hash.c hash.h insert.c keywordhash.h legacy.c loadext.c main.c	\
	malloc.c opcodes.c opcodes.h os.c os_common.h os.h os_os2.c	\
	os_os2.h os_unix.c os_win.c pager.c pager.h parse.c parse.h	\
	pragma.c prepare.c printf.c random.c select.c sqliteInt.h	\
	sqliteLimit.h table.c tokenize.c trigger.c update.c utf.c	\
	util.c vacuum.c vdbeapi.c vdbeaux.c vdbeblob.c vdbe.c		\
	vdbefifo.c vdbe.h vdbeInt.h vdbemem.c vtab.c where.c

libsqlite_a_CPPFLAGS =
libsqlite_a_CFLAGS = $(AM_CFLAGS) $(LIB3RDPARTY_CFLAGS)
libsqlite_a_CXXFLAGS = $(AM_CXXFLAGS) -I$(top_srcdir) $(LIB3RDPARTY_CXXFLAGS)

