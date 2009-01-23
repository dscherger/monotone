AC_DEFUN([MTN_FIND_LUA],
[  # We manually test the variables here because we want them to work
   # even if pkg-config isn't installed.  The use of + instead of :+ is
   # deliberate; the user should be able to tell us that the empty string
   # is the correct set of flags.  (PKG_CHECK_MODULES gets this wrong!)
   if test -n "${lua_CFLAGS+set}" || test -n "${lua_LIBS+set}"; then
     found_liblua=yes
   else
     PKG_CHECK_MODULES([lua], [lua5.1],
                       [found_liblua=yes], [found_liblua=no])

     # if that has not been found, we also try lua-5.1, as used on FreeBSD
     # for example.
     if test $found_liblua = no; then
       PKG_CHECK_MODULES([lua], [lua-5.1],
                         [found_liblua=yes], [found_liblua=no])
     fi
   fi

   if test $found_liblua = no; then
     AC_MSG_CHECKING([for lua 5.1])
     # try lua-config5x, in case we're on a system with no pkg-config
     if test -n "`type -p lua-config51`"; then
       lua_CFLAGS="`lua-config51 --include`"
       lua_LIBS="`lua-config51 --libs`"
       found_liblua=yes
       AC_MSG_RESULT([yes])
     fi
   fi

   if test $found_liblua = no; then
     AC_MSG_RESULT([no; guessing])
     lua_CFLAGS=
     AC_CHECK_LIB([lua5.1], [lua_load], 
                  [lua_LIBS=-llua5.1], 
                  [lua_LIBS=-llua])
   fi

   # AC_MSG_NOTICE([using lua compile flags: "$lua_CFLAGS"])
   # AC_MSG_NOTICE([using lua link flags: "$lua_LIBS"])

   AC_CACHE_CHECK([whether the lua library is usable], ac_cv_lib_lua_works,
    [save_LIBS="$LIBS"
     save_CPPFLAGS="$CPPFLAGS"
     LIBS="$LIBS $lua_LIBS"
     CPPFLAGS="$CPPFLAGS $lua_CFLAGS"
     AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [
  #include <lua.h>
  #include <lualib.h>
  #include <lauxlib.h>
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
