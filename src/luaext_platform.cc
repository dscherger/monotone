// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "lua.hh"

#include <signal.h>
#include <cstdlib>

#include "platform.hh"
#include "sanity.hh"

using std::string;
using std::malloc;
using std::free;

LUAEXT(get_ostype, )
{
  std::string str;
  get_system_flavour(str);
  lua_pushstring(LS, str.c_str());
  return 1;
}

LUAEXT(existsonpath, )
{
  const char * exe = luaL_checkstring(LS, -1);
  lua_pushnumber(LS, existsonpath(exe));
  return 1;
}

LUAEXT(is_executable, )
{
  const char * path = luaL_checkstring(LS, -1);
  lua_pushboolean(LS, is_executable(path));
  return 1;
}

LUAEXT(set_executable, )
{
  const char * path = luaL_checkstring(LS, -1);
  lua_pushnumber(LS, set_executable(path));
  return 1;
}

LUAEXT(clear_executable, )
{
  const char * path = luaL_checkstring(LS, -1);
  lua_pushnumber(LS, clear_executable(path));
  return 1;
}

LUAEXT(spawn, )
{
  int n = lua_gettop(LS);
  const char * path = luaL_checkstring(LS, 1);
  char ** argv = (char **)malloc((n + 1) * sizeof(char *));
  int i;
  pid_t ret;
  if (argv == NULL)
    return 0;
  argv[0] = (char *)path;
  for (i = 1; i < n; i++) argv[i] = (char *)luaL_checkstring(LS, i + 1);
  argv[i] = NULL;
  ret = process_spawn(argv);
  free(argv);
  lua_pushnumber(LS, ret);
  return 1;
}

LUAEXT(spawn_redirected, )
{
  int n = lua_gettop(LS);
  char const * infile = luaL_checkstring(LS, 1);
  char const * outfile = luaL_checkstring(LS, 2);
  char const * errfile = luaL_checkstring(LS, 3);
  const char * path = luaL_checkstring(LS, 4);
  n -= 3;
  char ** argv = (char **)malloc((n + 1) * sizeof(char *));
  int i;
  pid_t ret;
  if (argv == NULL)
    return 0;
  argv[0] = (char *)path;
  for (i = 1; i < n; i++) argv[i] = (char *)luaL_checkstring(LS,  i + 4);
  argv[i] = NULL;
  ret = process_spawn_redirected(infile, outfile, errfile, argv);
  free(argv);
  lua_pushnumber(LS, ret);
  return 1;
}

// borrowed from lua/liolib.cc
// Note that making C functions that return FILE* in Lua is tricky
// There is a Lua FAQ entitled:
// "Why does my library-created file segfault on :close() but work otherwise?"

#define topfile(LS)     ((FILE **)luaL_checkudata(LS, 1, LUA_FILEHANDLE))

static int io_fclose (lua_State * LS)
{
  FILE ** p = topfile(LS);
  int ok = (fclose(*p) == 0);
  *p = NULL;
  lua_pushboolean(LS, ok);
  return 1;
}

static FILE ** newfile (lua_State * LS)
{
  FILE ** pf = (FILE **)lua_newuserdata(LS, sizeof(FILE *));
  *pf = NULL;  /* file handle is currently `closed' */
  luaL_getmetatable(LS, LUA_FILEHANDLE);
  lua_setmetatable(LS, -2);

  lua_pushcfunction(LS, io_fclose);
  lua_setfield(LS, LUA_ENVIRONINDEX, "__close");

  return pf;
}

LUAEXT(spawn_pipe, )
{
  int n = lua_gettop(LS);
  char ** argv = (char **)malloc((n + 1) * sizeof(char *));
  int i;
  pid_t pid;
  if (argv == NULL)
    return 0;
  if (n < 1)
    return 0;
  for (i = 0; i < n; i++) argv[i] = (char *)luaL_checkstring(LS,  i + 1);
  argv[i] = NULL;

  int infd;
  FILE ** inpf = newfile(LS);
  int outfd;
  FILE ** outpf = newfile(LS);

  pid = process_spawn_pipe(argv, inpf, outpf);
  free(argv);

  lua_pushnumber(LS, pid);

  return 3;
}

LUAEXT(wait, )
{
  pid_t pid = static_cast<pid_t>(luaL_checknumber(LS, -1));
  int res;
  int ret;
  ret = process_wait(pid, &res);
  lua_pushnumber(LS, res);
  lua_pushnumber(LS, ret);
  return 2;
}

LUAEXT(kill, )
{
  int n = lua_gettop(LS);
  pid_t pid = static_cast<pid_t>(luaL_checknumber(LS, -2));
  int sig;
  if (n > 1)
    sig = static_cast<int>(luaL_checknumber(LS, -1));
  else
    sig = SIGTERM;
  lua_pushnumber(LS, process_kill(pid, sig));
  return 1;
}

LUAEXT(sleep, )
{
  int seconds = static_cast<int>(luaL_checknumber(LS, -1));
  lua_pushnumber(LS, process_sleep(seconds));
  return 1;
}

LUAEXT(get_pid, )
{
  pid_t pid = get_process_id();
  lua_pushnumber(LS, pid);
  return 1;
}

// fs extensions

LUAEXT(mkdir, )
{
  try
    {
      char const * dirname = luaL_checkstring(LS, -1);
      do_mkdir(dirname);
      lua_pushboolean(LS, true);
      return 1;
    }
  catch(recoverable_failure & e)
    {
      lua_pushnil(LS);
      return 1;
    }
}

LUAEXT(exists, )
{
  try
    {
      char const * name = luaL_checkstring(LS, -1);
      switch (get_path_status(name))
        {
        case path::nonexistent:  lua_pushboolean(LS, false); break;
        case path::file:
        case path::directory:    lua_pushboolean(LS, true); break;
        }
    }
  catch(recoverable_failure & e)
    {
      lua_pushnil(LS);
    }
  return 1;
}

LUAEXT(isdir, )
{
  try
    {
      char const * name = luaL_checkstring(LS, -1);
      switch (get_path_status(name))
        {
        case path::nonexistent:
        case path::file:         lua_pushboolean(LS, false); break;
        case path::directory:    lua_pushboolean(LS, true); break;
        }
    }
  catch(recoverable_failure & e)
    {
      lua_pushnil(LS);
    }
  return 1;
}

namespace
{
  struct build_table : public dirent_consumer
  {
    build_table(lua_State * st) : st(st), n(1)
    {
      lua_newtable(st);
    }
    virtual void consume(const char * s)
    {
      lua_pushstring(st, s);
      lua_rawseti(st, -2, n);
      n++;
    }
  private:
    lua_State * st;
    unsigned int n;
  };
}

LUAEXT(read_directory, )
{
  int top = lua_gettop(LS);
  try
    {
      string path(luaL_checkstring(LS, -1));
      build_table tbl(LS);

      read_directory(path, tbl, tbl, tbl);
    }
  catch(recoverable_failure &)
    {
      // discard the table and any pending path element
      lua_settop(LS, top);
      lua_pushnil(LS);
    }
  catch (...)
    {
      lua_settop(LS, top);
      throw;
    }
  return 1;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
