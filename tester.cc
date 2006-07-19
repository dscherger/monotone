
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "lua.hh"
#include "tester.h"
#include "platform.hh"
#include "sanity.hh"

#include <cstdio>

#include <exception>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/lexical_cast.hpp>

#include <map>
#include <utility>

namespace fs = boost::filesystem;

using std::string;
using std::map;
using std::make_pair;
using boost::lexical_cast;

// Lua uses the c i/o functions, so we need to too.
struct tester_sanity : public sanity
{
  void inform_log(std::string const &msg)
  {fprintf(stdout, "%s", msg.c_str());}
  void inform_message(std::string const &msg)
  {fprintf(stdout, "%s", msg.c_str());};
  void inform_warning(std::string const &msg)
  {fprintf(stderr, "warning: %s", msg.c_str());};
  void inform_error(std::string const &msg)
  {fprintf(stderr, "error: %s", msg.c_str());};
};
tester_sanity real_sanity;
sanity & global_sanity(real_sanity);

namespace redirect
{
  enum what {in, out, err};
}

#ifdef WIN32
#include <windows.h>
namespace redirect {typedef HANDLE savetype;}
HANDLE set_redirect(redirect::what what, string where)
{
  HANDLE file;
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = 0;
  sa.bInheritHandle = true;
  if (what == redirect::in)
    {
      file = CreateFile(where.c_str(),
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        &sa,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    }
  else
    {
      file = CreateFile(where.c_str(),
                        GENERIC_WRITE,
                        FILE_SHARE_READ,
                        &sa,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    }
  HANDLE old;
  switch(what)
  {
  case redirect::in:
    old = GetStdHandle(STD_INPUT_HANDLE);
    SetStdHandle(STD_INPUT_HANDLE, file);
    break;
  case redirect::out:
    old = GetStdHandle(STD_OUTPUT_HANDLE);
    SetStdHandle(STD_OUTPUT_HANDLE, file);
    break;
  case redirect::err:
    old = GetStdHandle(STD_ERROR_HANDLE);
    SetStdHandle(STD_ERROR_HANDLE, file);
    break;
  }
  return old;
}
void clear_redirect(redirect::what what, HANDLE saved)
{
  switch(what)
  {
  case redirect::in:
    CloseHandle(GetStdHandle(STD_INPUT_HANDLE));
    SetStdHandle(STD_INPUT_HANDLE, saved);
    break;
  case redirect::out:
    CloseHandle(GetStdHandle(STD_OUTPUT_HANDLE));
    SetStdHandle(STD_OUTPUT_HANDLE, saved);
    break;
  case redirect::err:
    CloseHandle(GetStdHandle(STD_ERROR_HANDLE));
    SetStdHandle(STD_ERROR_HANDLE, saved);
    break;
  }
}
bool make_accessible(string const &name)
{
  DWORD attrs = GetFileAttributes(name.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES)
    return false;
  bool ok = SetFileAttributes(name.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
  return ok;
}
#else
#include <unistd.h>
#include <sys/stat.h>
namespace redirect {typedef int savetype;}
int set_redirect(redirect::what what, string where)
{
  int from;
  char const *mode;
  switch(what)
  {
  case redirect::in:
    from = 0;
    mode = "r";
    break;
  case redirect::out:
    from = 1;
    mode = "w";
    break;
  case redirect::err:
    from = 2;
    mode = "w";
    break;
  default:
    from = -1;
    mode = "";
    break;
  };
  int saved = dup(from);
  FILE *f = fopen(where.c_str(), mode);
  if (!f)
    return -1;
  dup2(fileno(f), from);
  fclose(f);
  return saved;
}
void clear_redirect(redirect::what what, int saved)
{
  int from;
  switch(what)
  {
  case redirect::in:
    from = 0;
    break;
  case redirect::out:
    from = 1;
    break;
  case redirect::err:
    from = 2;
    break;
  default:
    from = -1;
    break;
  };
  dup2(saved, from);
  close(saved);
}
bool make_accessible(string const &name)
{
  struct stat st;
  bool ok = (stat(name.c_str(), &st) == 0);
  if (!ok)
    return false;
  return (chmod(name.c_str(), st.st_mode | S_IREAD | S_IWRITE | S_IEXEC) == 0);
}
#endif
namespace redirect
{
  struct saveblock
  {
    savetype in;
    savetype out;
    savetype err;
  };
}

#include <cstdlib>
map<string, string> orig_env_vars;
void save_env() { orig_env_vars.clear(); }
#if defined(WIN32) && !defined(__MINGW32__)
void restore_env()
{
  for (map<string,string>::const_iterator i = orig_env_vars.begin();
       i != orig_env_vars.end(); ++i)
    {
      _putenv_s(i->first.c_str(), i->second.c_str());
    }
  orig_env_vars.clear();
}
void set_env(string const &var, string const &val)
{
  char const *old = getenv(var.c_str());
  if (old)
    orig_env_vars.insert(make_pair(var, string(old)));
  else
    orig_env_vars.insert(make_pair(var, ""));
  _putenv_s(var.c_str(), val.c_str());
}
#else
void putenv2(string const &var, string const &val)
{
  string tempstr = var + "=" + val;
  char const *s = tempstr.c_str();
  size_t len = var.size() + val.size() + 2;
  char *cp = new char[len];
  memcpy(cp, s, len);
  putenv(cp);
}
void restore_env()
{
  for (map<string,string>::const_iterator i = orig_env_vars.begin();
       i != orig_env_vars.end(); ++i)
    {
      putenv2(i->first, i->second);
    }
  orig_env_vars.clear();
}
void set_env(string const &var, string const &val)
{
  char const *old = getenv(var.c_str());
  if (old)
    orig_env_vars.insert(make_pair(var, string(old)));
  else
    orig_env_vars.insert(make_pair(var, ""));
  putenv2(var, val);
}
#endif


fs::path source_dir;
fs::path run_dir;

static int panic_thrower(lua_State * st)
{
  throw oops("lua error");
}

void do_remove_recursive(fs::path const &rem)
{
  if (!fs::exists(rem))
    return;
  make_accessible(rem.native_file_string());
  if (fs::is_directory(rem))
    {
      for (fs::directory_iterator i(rem); i != fs::directory_iterator(); ++i)
        do_remove_recursive(*i);
    }
  fs::remove(rem);
}

void do_make_tree_accessible(fs::path const &f)
{
  if (!fs::exists(f))
    return;
  make_accessible(f.native_file_string());
  if (fs::is_directory(f))
    {
      for (fs::directory_iterator i(f); i != fs::directory_iterator(); ++i)
        do_make_tree_accessible(*i);
    }
}

void do_copy_recursive(fs::path const &from, fs::path to)
{
  if (!fs::exists(from))
    throw fs::filesystem_error("Source for copy does not exist", from, 0);
  if (fs::exists(to))
    {
      if (fs::is_directory(to))
        to = to / from.leaf();
      else
        do_remove_recursive(to);
    }
  if (fs::is_directory(from))
    {
      fs::create_directory(to);
      for (fs::directory_iterator i(from); i != fs::directory_iterator(); ++i)
        do_copy_recursive(*i, to / i->leaf());
    }
  else
    fs::copy_file(from, to);
}

LUAEXT(posix_umask, )
{
#ifdef WIN32
  lua_pushnil(L);
  return 1;
#else
  int from = luaL_checknumber(L, -1);
  mode_t mask = 64*((from / 100) % 10) + 8*((from / 10) % 10) + (from % 10);
  mode_t oldmask = umask(mask);
  int res = 100*(oldmask/64) + 10*((oldmask/8) % 8) + (oldmask % 8);
  lua_pushnumber(L, res);
  return 1;
#endif
}

LUAEXT(go_to_test_dir, )
{
  try
    {
      char const * testname = luaL_checkstring(L, -1);
      fs::path tname(testname);
      fs::path testdir = run_dir / tname.leaf();
      if (fs::exists(testdir))
        do_remove_recursive(testdir);
      fs::create_directory(testdir);
      change_current_working_dir(testdir.native_file_string());
      lua_pushstring(L, testdir.native_file_string().c_str());
      lua_pushstring(L, tname.leaf().c_str());
      return 2;
    }
  catch(fs::filesystem_error & e)
    {
      lua_pushnil(L);
      return 1;
    }
}

LUAEXT(chdir, )
{
  try
    {
      fs::path dir(luaL_checkstring(L, -1), fs::native);
      string from = fs::current_path().native_file_string();
      if (!dir.is_complete())
        dir = fs::current_path() / dir;
      if (!fs::exists(dir) || !fs::is_directory(dir))
        {
          lua_pushnil(L);
          return 1;
        }
      change_current_working_dir(dir.native_file_string());
      lua_pushstring(L, from.c_str());
      return 1;
    }
  catch(fs::filesystem_error & e)
    {
      lua_pushnil(L);
      return 1;
    }
}

LUAEXT(clean_test_dir, )
{
  try
    {
      char const * testname = luaL_checkstring(L, -1);
      fs::path tname(testname, fs::native);
      fs::path testdir = run_dir / tname.leaf();
      change_current_working_dir(run_dir.native_file_string());
      do_remove_recursive(testdir);
      lua_pushboolean(L, true);
      return 1;
    }
  catch(fs::filesystem_error & e)
    {
      lua_pushnil(L);
      return 1;
    }
}

LUAEXT(remove_recursive, )
{
  try
    {
      fs::path dir(luaL_checkstring(L, -1));
      do_remove_recursive(dir);
      lua_pushboolean(L, true);
      return 1;
    }
  catch(fs::filesystem_error & e)
    {
      lua_pushboolean(L, false);
      lua_pushstring(L, e.what());
      return 2;
    }
}

LUAEXT(make_tree_accessible, )
{
  try
    {
      fs::path dir(luaL_checkstring(L, -1));
      do_make_tree_accessible(dir);
      lua_pushboolean(L, true);
      return 1;
    }
  catch(fs::filesystem_error & e)
    {
      lua_pushboolean(L, false);
      lua_pushstring(L, e.what());
      return 2;
    }
}

LUAEXT(copy_recursive, )
{
  try
    {
      fs::path from(luaL_checkstring(L, -2));
      fs::path to(luaL_checkstring(L, -1));
      do_copy_recursive(from, to);
      lua_pushboolean(L, true);
      return 1;
    }
  catch(fs::filesystem_error & e)
    {
      lua_pushboolean(L, false);
      lua_pushstring(L, e.what());
      return 2;
    }
}

LUAEXT(leave_test_dir, )
{
  try
    {
      change_current_working_dir(run_dir.native_file_string());
      lua_pushboolean(L, true);
      return 1;
    }
  catch(fs::filesystem_error & e)
    {
      lua_pushnil(L);
      return 1;
    }
}

LUAEXT(mkdir, )
{
  try
    {
      char const * dirname = luaL_checkstring(L, -1);
      fs::path dir(dirname, fs::native);
      fs::create_directory(dir);
      lua_pushboolean(L, true);
      return 1;
    }
  catch(fs::filesystem_error & e)
    {
      lua_pushnil(L);
      return 1;
    }
}

LUAEXT(mtime, )
{
  try
    {
      char const * file = luaL_checkstring(L, -1);
      fs::path fn(file);
      std::time_t t = fs::last_write_time(file);
      if (t == std::time_t(-1))
        lua_pushnil(L);
      else
        lua_pushnumber(L, t);
      return 1;
    }
  catch(fs::filesystem_error & e)
    {
      lua_pushnil(L);
      return 1;
    }
}

LUAEXT(exists, )
{
  try
    {
      char const * name = luaL_checkstring(L, -1);
      fs::path p(name, fs::native);
      lua_pushboolean(L, fs::exists(p));
    }
  catch(fs::filesystem_error & e)
    {
      lua_pushnil(L);
    }
  return 1;
}

LUAEXT(isdir, )
{
  try
    {
      char const * name = luaL_checkstring(L, -1);
      fs::path p;
      p = fs::path(name, fs::native);
      lua_pushboolean(L, fs::exists(p) && fs::is_directory(p));
    }
  catch(fs::filesystem_error & e)
    {
      lua_pushnil(L);
    }
  return 1;
}

LUAEXT(get_source_dir, )
{
  lua_pushstring(L, source_dir.native_file_string().c_str());
  return 1;
}

extern "C" static int clear_redirect(lua_State *L)
{
  typedef redirect::saveblock rsb;
  rsb const *sb = static_cast<rsb const*>(lua_topointer(L, 1));
  clear_redirect(redirect::in, sb->in);
  clear_redirect(redirect::out, sb->out);
  clear_redirect(redirect::err, sb->err);
  return 0;
}

LUAEXT(set_redirect, )
{
  char const * infile = luaL_checkstring(L, -3);
  char const * outfile = luaL_checkstring(L, -2);
  char const * errfile = luaL_checkstring(L, -1);

  typedef redirect::saveblock rsb;
  rsb *sb = static_cast<rsb*> (lua_newuserdata(L, sizeof(rsb)));
  sb->in = set_redirect(redirect::in, infile);
  sb->out = set_redirect(redirect::out, outfile);
  sb->err = set_redirect(redirect::err, errfile);
  lua_newtable(L);
  lua_pushstring(L, "__index");
  lua_pushvalue(L, -2);
  lua_settable(L, -3);

  lua_pushstring(L, "restore");
  lua_pushcfunction(L,clear_redirect);
  lua_settable(L, -3);
  lua_setmetatable(L, -2);
  
  return 1;
}

LUAEXT(get_ostype, )
{
  string str;
  get_system_flavour(str);
  lua_pushstring(L, str.c_str());
  return 1;
}

LUAEXT(save_env, )
{
  save_env();
  return 0;
}

LUAEXT(restore_env, )
{
  restore_env();
  return 0;
}

LUAEXT(set_env, )
{
  char const * var = luaL_checkstring(L, -2);
  char const * val = luaL_checkstring(L, -1);
  set_env(var, val);
  return 0;
}

LUAEXT(timed_wait, )
{
  pid_t pid = static_cast<pid_t>(luaL_checknumber(L, -2));
  int time = static_cast<int>(luaL_checknumber(L, -1));
  int res;
  int ret;
  ret = process_wait(pid, &res, time);
  lua_pushnumber(L, res);
  lua_pushnumber(L, ret);
  return 2;
}

int main(int argc, char **argv)
{
  int retcode = 2;
  lua_State *st = 0;
  try{
//  global_sanity.set_debug();
  string testfile;
  string firstdir;
  bool needhelp = false;
  for (int i = 1; i < argc; ++i)
    if (string(argv[i]) == "--help" || string(argv[i]) == "-h")
      needhelp = true;
  if (argc > 1 && !needhelp)
    {
      fs::initial_path();
      fs::path::default_name_check(fs::native);
      try
        {
          std::string name = argv[1];
          fs::path file = fs::complete(fs::path(name, fs::native));
          testfile = file.native_file_string();
          source_dir = file.branch_path();
        }
      catch(fs::filesystem_error & e)
        {
          E(false, F("Error during initialization: %s") % e.what());
        }
      firstdir = fs::initial_path().native_file_string();
      run_dir = fs::initial_path() / "tester_dir";
      fs::create_directory(run_dir);
      change_current_working_dir(run_dir.native_file_string());
    }
  else
    {
      P(F("Usage: %s test-file [arguments]\n") % argv[0]);
      P(F("\t-h         print this message\n"));
      P(F("\t-l         print test names only; don't run them\n"));
      P(F("\t-d         don't clean the scratch directories\n"));
      P(F("\tnum        run a specific test\n"));
      P(F("\tnum..num   run tests in a range\n"));
      P(F("\t           if num is negative, count back from the end\n"));
      P(F("\tregex      run tests with matching names\n"));
      return 1;
    }
  st = lua_open();
  lua_atpanic (st, &panic_thrower);
  luaopen_base(st);
  luaopen_io(st);
  luaopen_string(st);
  luaopen_math(st);
  luaopen_table(st);
  luaopen_debug(st);
  add_functions(st);
  
  lua_pushstring(st, "initial_dir");
  lua_pushstring(st, firstdir.c_str());
  lua_settable(st, LUA_GLOBALSINDEX);

  try
    {
      run_string(st, tester_constant, "tester builtin functions");
      //printf("Loading test file %s\n", testfile.c_str());
      run_file(st, testfile);
      Lua ll(st);
      ll.func("run_tests");
      ll.push_table();
      for (int i = 2; i < argc; ++i)
        {
          ll.push_int(i-1);
          ll.push_str(argv[i]);
          ll.set_table();
        }
      ll.call(1,1)
        .extract_int(retcode);
    }
  catch (std::exception &e)
    {
      P(F("Error: %s") % e.what());
    }
  } catch (informative_failure & e) {
    P(F("Error: %s\n") % e.what.c_str());
    retcode = 1;
  } catch (std::logic_error & e) {
    P(F("Invariant failure: %s\n") % e.what());
    retcode = 3;
  }
  if (st)
    lua_close(st);
  return retcode;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
