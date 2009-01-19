# This is a separate macro primarily to trick autoconf into not looking
# for pkg-config.
AC_DEFUN([MTN_FIND_LUA],
[  PKG_PROG_PKG_CONFIG

   # We manually test the variables here because we want them to work
   # even if pkg-config isn't installed.  The use of + instead of :+ is
   # deliberate; the user should be able to tell us that the empty string
   # is the correct set of flags.  (PKG_CHECK_MODULES gets this wrong!)
   if test -n "${LUA_CPPFLAGS+set}" || test -n "${LUA_LIBS+set}"; then
     found_liblua=yes
   else
     PKG_CHECK_MODULES([LUA], [lua5.1],
                       [found_liblua=yes], [found_liblua=no])

     # if that has not been found, we also try lua-5.1, as used on FreeBSD
     # for example.
     if test $found_liblua = no; then
       PKG_CHECK_MODULES([LUA], [lua-5.1],
                         [found_liblua=yes], [found_liblua=no])
     fi

     if test $found_liblua = yes; then
       # PKG_CHECK_MODULES adds LUA_CFLAGS, but we want LUA_CPPFLAGS
       LUA_CPPFLAGS="$LUA_CFLAGS"
     fi
   fi

   if test $found_liblua = no; then
     AC_MSG_CHECKING([for lua 5.1])
     # try lua-config5x, in case we're on a system with no pkg-config
     if test -n "`type -p lua-config51`"; then
       LUA_CPPFLAGS="`lua-config51 --include`"
       LUA_LIBS="`lua-config51 --libs`"
       found_liblua=yes
       AC_MSG_RESULT([yes])
     fi
   fi

   if test $found_liblua = no; then
     AC_MSG_RESULT([no; guessing])
     AC_CHECK_LIB([lua5.1], [lua_load], 
                  [LUA_LIBS=-llua5.1], 
                  [LUA_LIBS=-llua])
     LUA_CPPFLAGS=
   fi

   # Wherever we got the settings from, make sure they work.
   LUA_CPPFLAGS="`echo :$LUA_CPPFLAGS | sed -e 's/^:@<:@	 @:>@*//; s/@<:@	 @:>@*$//'`"
   LUA_LIBS="`echo :$LUA_LIBS | sed -e 's/^:@<:@	 @:>@*//; s/@<:@	 @:>@*$//'`"

   # AC_MSG_NOTICE([using lua compile flags: "$LUA_CPPFLAGS"])
   # AC_MSG_NOTICE([using lua link flags: "$LUA_LIBS"])

   AC_CACHE_CHECK([whether the lua library is usable], ac_cv_lib_lua_works,
    [save_LIBS="$LIBS"
     save_CPPFLAGS="$CPPFLAGS"
     LIBS="$LIBS $LUA_LIBS"
     CPPFLAGS="$CPPFLAGS $LUA_CPPFLAGS"
     AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [
extern "C"
{
  #include <lua.h>
  #include <lualib.h>
  #include <lauxlib.h>
}
      ],
      [
lua_State *st;

#if LUA_VERSION_NUM != 501
#error Lua version mismatch
#endif

st = luaL_newstate();
      ])],
      [ac_cv_lib_lua_works=yes], [ac_cv_lib_lua_works=no])
     LIBS="$save_LIBS"
     CPPFLAGS="$save_CPPFLAGS"])
   if test $ac_cv_lib_lua_works = no; then
      AC_MSG_ERROR([Your lua library is not usable.])
   fi

])
