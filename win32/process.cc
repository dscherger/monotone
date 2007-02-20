// copyright (C) 2005 Jon Bright <jon@siliconcircus.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <sstream>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "sanity.hh"
#include "platform.hh"

static std::string
munge_inner_argument(std::string arg)
{
  std::string result;
  bool has_space = false;
  unsigned quotes = 0;
  bool space_outside_quote = false;

  for (std::string::const_iterator it = arg.begin();
       it != arg.end(); ++it)
    {
      switch (*it)
	{
	case ' ':
	  has_space = true;
	  if (quotes % 2 == 0)
	    space_outside_quote = true;
	  break;
	case '"':
	  ++quotes;
	  break;
	}
    }

  I(quotes % 2 == 0);

  // quote start of argument if needed
  if (has_space && space_outside_quote)
    result += "\"";

  // copy argument
  if (quotes == 0)
    result += arg;
  else
    {
      // escape inner quotes
      for (std::string::const_iterator it = arg.begin();
	   it != arg.end(); ++it)
	{
	  if (*it == '"' && it != arg.begin() && it != arg.end() - 1)
	    result += '\\';
	  result += *it;
	}
    }

  // quote end of argument
  if (has_space && space_outside_quote)
    result += "\"";

  return result;
}

static std::string
munge_argument(std::string arg)
{
  // handle DOS-style '/file:c:\path to\file.txt' by splitting at the colon
  // and handling the last part as a standard argument, then reassembling
  // for the cmdline.
  if (arg.empty())
    return "\"\"";
  else if (arg[0] == '/')
    {
      std::string result;
      std::string::size_type dos_cmd = arg.find(':');
      if (dos_cmd != std::string::size_type)
	{
	  result += arg.substr(0, dos_cmd + 1);
	  result += munge_inner_argument(std::string(dos_cmd + 1));
	}
      else
	result += arg;
      return result;
    }
  else
    return munge_inner_argument(arg);
}

std::string
munge_argv_into_cmdline(char const * const argv[])
{
  std::string cmdline;

  for (int i = 0; argv[i]; ++i)
    {
      cmdline += munge_argument(std::string(argv[i]));
      cmdline += " ";
    }

  return cmdline;
}

int
existsonpath(char const * exe)
{
  if (SearchPath(NULL, exe, ".exe", 0, NULL, NULL)==0)
    return -1;
  return 0;
}

bool
is_executable(char const * path)
{
  return false; /* Basically meaningless on win32 */
}

int
make_executable(char const * path)
{
  return 0; /* Basically meaningless on win32 */
}

pid_t
process_spawn(char const * const argv[])
{
  std::vector<char> realexe;
  realexe.resize(strlen(argv[0]) + 1 + MAXPATH);

  L(FL("searching for exe: %s\n") % realexe);
  char const * filepart;
  if (SearchPath(NULL, argv[0], ".exe", realexe.size(), &*realexe.begin(),
		 &filepart) == 0)
    {
      os_err_t errnum = GetLastError();
      L(FL("SearchPath failed, err=%s (%d)\n") % os_strerror(errnum) % errnum);
      return -1;
    }

  std::string cmd = munge_argv_into_cmdline(argv);
  L(FL("spawning command: '%s' '%s'\n") % &*realexe.begin() % cmd);

  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  memset(&si, 0, sizeof(si));
  si.cb = sizeof(STARTUPINFO);

  /* We don't need to set any of the STARTUPINFO members */
  if (CreateProcess(realexe, const_cast<char *>(cmd.c_str()), NULL, NULL, TRUE,
		    0, NULL, NULL, &si, &pi) == 0)
    {
      os_err_t errnum = GetLastError();
      L(FL("CreateProcess failed, err=%s (%d)\n") % os_strerror(errnum) % errnum);
      return -1;
    }
  CloseHandle(pi.hThread);
  return static_cast<pid_t>(pi.hProcess);
}

struct redir
{
  struct bad_redir {};
  HANDLE saved;
  int what;
  redir(int which, char const * file);
  ~redir();
};

redir::redir(int which, char const * filename)
  : what(which)
{
  if (!filename || *filename == '\0')
    {
      what = -1;
      return;
    }
  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = 0;
  sa.bInheritHandle = true;

  HANDLE file = CreateFile(filename,
			   (which == 0 ? GENERIC_READ : GENERIC_WRITE),
			   FILE_SHARE_READ,
			   &sa,
			   (which == 0 ? OPEN_EXISTING : CREATE_ALWAYS),
			   FILE_ATTRIBUTE_NORMAL,
			   NULL);
  switch (which)
    {
    case 0:
      saved = GetStdHandle(STD_INPUT_HANDLE);
      SetStdHandle(STD_INPUT_HANDLE, file);
      break;
    case 1:
      saved = GetStdHandle(STD_OUTPUT_HANDLE);
      SetStdHandle(STD_OUTPUT_HANDLE, file);
      break;
    case 2:
      saved = GetStdHandle(STD_ERROR_HANDLE);
      SetStdHandle(STD_ERROR_HANDLE, file);
      break;
    }
}

redir::~redir()
{
  switch(what)
    {
    case 0:
      CloseHandle(GetStdHandle(STD_INPUT_HANDLE));
      SetStdHandle(STD_INPUT_HANDLE, saved);
      break;
    case 1:
      CloseHandle(GetStdHandle(STD_OUTPUT_HANDLE));
      SetStdHandle(STD_OUTPUT_HANDLE, saved);
      break;
    case 2:
      CloseHandle(GetStdHandle(STD_ERROR_HANDLE));
      SetStdHandle(STD_ERROR_HANDLE, saved);
      break;
    }
}

pid_t
process_spawn_redirected(char const * in,
			 char const * out,
			 char const * err,
			 char const * const argv[])
{
  try
    {
      redir i(0, in);
      redir o(1, out);
      redir e(2, err);
      return process_spawn(argv);
    }
  catch (redir::bad_redir)
    {
      return -1;
    }
}

int
process_wait(pid_t pid, int * res, int timeout)
{
  HANDLE hProcess = static_cast<DWORD>(pid);
  DWORD time = INFINITE;
  if (timeout != -1)
    time = timeout * 1000;
  DWORD r = WaitForSingleObject(hProcess, time);
  if (r == WAIT_TIMEOUT)
    return -1;
  if (r == WAIT_FAILED)
    {
      CloseHandle(hProcess); /* May well not work, but won't harm */
      return -1;
    }
  I(res);
  if (GetExitCodeProcess(hProcess, static_cast<DWORD *>(res)) == 0)
    *res = -1;
  CloseHandle(hProcess); /* Let the process die */
  return 0;
}

int
process_kill(pid_t pid, int signal)
{
  HANDLE hProcess = static_cast<HANDLE>(pid);
  if (TerminateProcess(hProcess, 1) == 0)
    return -1;
  return 0;
}

int
process_sleep(unsigned int seconds)
{
  Sleep(seconds * 1000);
  return 0;
}

pid_t
get_process_id()
{
  return GetCurrentProcessId();
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
