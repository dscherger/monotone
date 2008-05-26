lib_LIBRARIES = libsqlite3.a
include_HEADERS = sqlite3.h sqlite3ext.h
libsqlite3_a_SOURCES =							\
	alter.c analyze.c attach.c auth.c btree.c btree.h btreeInt.h	\
	build.c callback.c complete.c date.c delete.c expr.c func.c	\
	hash.c hash.h insert.c keywordhash.h legacy.c loadext.c main.c	\
	malloc.c opcodes.c opcodes.h os.c os_common.h os.h os_os2.c	\
	os_os2.h os_unix.c os_win.c pager.c pager.h parse.c parse.h	\
	pragma.c prepare.c printf.c random.c select.c sqliteInt.h	\
	sqliteLimit.h table.c tokenize.c trigger.c update.c utf.c	\
	util.c vacuum.c vdbeapi.c vdbeaux.c vdbeblob.c vdbe.c		\
	vdbefifo.c vdbe.h vdbeInt.h vdbemem.c vtab.c where.c

# We have .pc files for pkg-config users.
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = sqlite3.pc
