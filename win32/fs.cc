// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <errno.h>
#include <windows.h>
#include <shlobj.h>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "vocab.hh"
#include "sanity.hh"
#include "platform.hh"

std::string
win32_strerror(DWORD errnum)
{
  LPTSTR tstr;

  if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM,
                    0, errnum, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    reinterpret_cast<LPTSTR>(&tstr), 0,
                    static_cast<va_list*>(0)) == 0)
    return (F("unknown error code %d") % errnum).str();

  std::string str = tstr;
  LocalFree(tstr);
  return str;
}

std::string
get_current_working_dir()
{
  char buffer[4096];
  E(getcwd(buffer, 4096),
    F("cannot get working directory: %s") % strerror(errno));
  std::string str(buffer);
  if (str[str.size() - 1] == '\\')
    str = str.substr(0, str.size() - 1);
  return str;
}

void
change_current_working_dir(any_path const & to)
{
  E(!chdir(to.as_external().c_str()),
    F("cannot change to directory %s: %s") % to % strerror(errno));
}

system_path
get_default_confdir()
{
  std::string base;
  char * appdata;
  appdata = getenv("APPDATA");
  if (appdata != NULL)
    base = appdata;
  else
    {
      TCHAR szPath[MAX_PATH];
      if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, szPath)))
        base = szPath;
    }
  N(!base.empty(), F("could not determine configuration path"));
  return system_path(base) / "monotone";
}

// FIXME: BUG: this probably mangles character sets
// (as in, we're treating system-provided data as utf8, but it's probably in
// the filesystem charset)
utf8
get_homedir()
{
  // Windows is fun!
  // There has been much discussion about the correct way to do this, and a
  // couple of methods have been tried (look at previous versions of this
  // file for the discussion).  For consistency, we now calculate the user's
  // home path using the same technique that Qt's QDir::homePath() uses on
  // Windows.
  char * home;
  home = getenv("HOME");
  if (home != NULL)
    {
      L(FL("Home directory from HOME\n"));
      return std::string(home);
    }
  // Otherwise, try USERPROFILE.  We could also use SHGetFolderPath() to get
  // at USERPROFILE without requiring it to be set as an environment
  // variable, but Qt doesn't, so we won't either.
  char * userprofile = getenv("USERPROFILE");
  if (userprofile != NULL)
    {
      L(FL("Home directory from USERPROFILE\n"));
      return std::string(userprofile);
    }
  // Try concatenating HOMEDRIVE and HOMEPATH
  char * homedrive = getenv("HOMEDRIVE");
  char * homepath = getenv("HOMEPATH");
  if (homedrive != NULL && homepath != NULL)
    {
      L(FL("Home directory from HOMEDRIVE+HOMEPATH\n"));
      return std::string(homedrive) + std::string(homepath);
    }
  char * systemdrive = getenv("SystemDrive");
  if (systemdrive != NULL)
    {
      L(FL("Home directory from SystemDrive\n"));
      return std::string(systemdrive);
    }
  return std::string("C:");
}

utf8
tilde_expand(utf8 const & in)
{
  if (in().empty() || in()[0] != '~')
    return in;
  fs::path tmp(in(), fs::native);
  fs::path::iterator i = tmp.begin();
  if (i != tmp.end())
    {
      fs::path res;
      if (*i == "~" || i->size() > 1 && i->at(0) == '~')
        {
          fs::path restmp(get_homedir()(), fs::native);
          res /= restmp;
          ++i;
        }
      while (i != tmp.end())
        res /= *i++;
      return res.string();
    }

  return tmp.string();
}

path::status
get_path_status(any_path const & path)
{
  fs::path p(path.as_external(), fs::native);
  if (!fs::exists(p))
    return path::nonexistent;
  else if (fs::is_directory(p))
    return path::directory;
  else
    return path::file;
}

static bool
rename_clobberingly_impl(const char* from, const char* to)
{
  // MoveFileEx is only available on NT-based systems.  We will revert to a
  // more compatible DeleteFile/MoveFile pair as a compatibility fall-back.
  typedef BOOL (*MoveFileExFun)(LPCTSTR, LPCTSTR, DWORD);
  static MoveFileExFun MoveFileEx = 0;
  static bool MoveFileExAvailable = false;
  if (MoveFileEx == 0) {
    HMODULE hModule = LoadLibrary("kernel32");
    if (hModule)
      MoveFileEx = reinterpret_cast<MoveFileExFun>
        (GetProcAddress(hModule, "MoveFileExA"));
    if (MoveFileEx) {
      L(FL("using MoveFileEx for renames"));
      MoveFileExAvailable = true;
    } else
      L(FL("using DeleteFile/MoveFile fallback for renames"));
  }

  if (MoveFileExAvailable) {
    if (MoveFileEx(from, to, MOVEFILE_REPLACE_EXISTING))
      return true;
    else if (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED) {
      MoveFileExAvailable = false;
      L(FL("MoveFileEx failed with CALL_NOT_IMPLEMENTED, using fallback"));
    }
  } else {
    // This is not even remotely atomic, but what can you do?
    DeleteFile(to);
    if (MoveFile(from, to))
      return true;
  }
  return false;
}

void
rename_clobberingly(any_path const & from, any_path const & to)
{
  const char* szFrom = from.as_external().c_str();
  const char* szTo = to.as_external().c_str();
  static const int renameAttempts = 16;
  DWORD sleepTime = 1;
  DWORD lastError = 0;

  // If a clobbering rename attempt fails, we wait and try again, up to an
  // (arbitrary) maximum of 16 attempts.  This is a gross hack to work
  // around the common problem where another process (e.g. a virus checker)
  // will exclusive open a file you've just touched.
  for (int i = 0; i < renameAttempts; ++i) {
    if (rename_clobberingly_impl(szFrom, szTo))
      return;
    lastError = GetLastError();
    L(FL("attempted rename of '%s' to '%s' failed: %d")
      % szFrom % szTo % lastError);
    Sleep(sleepTime);
    if (sleepTime < 250)
      sleepTime *= 2;
  }
  E(false, F("renaming '%s' to '%s' failed: %s (%d)") % from % to
           % win32_strerror(lastError) % lastError);
}

