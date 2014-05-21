// Copyright (C) 2005 Derek Scherger <derek@echologic.com>
//                    Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../base.hh"

#include <windows.h>
#include <io.h>
#include <cstdlib>

#include "../platform.hh"

bool have_smart_terminal()
{
  std::string term;
  if (const char* term_cstr = getenv("TERM"))
    term = term_cstr;
  else
    term = "";

  // cmd.exe does not set TERM, but isatty returns true. Cygwin and MinGW
  // MSYS shells set a TERM but isatty returns false.
  if (isatty(2))
    {
      return true;
    }
  else
    {
      if (term == "" || term == "dumb")
        return false;
      else
        return true;
    }
}

unsigned int terminal_width()
{
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE)
    {
      CONSOLE_SCREEN_BUFFER_INFO ci;
      if (GetConsoleScreenBufferInfo(h, &ci) != 0)
        {
          return static_cast<unsigned int>(ci.dwSize.X);
        }
    }

  return 0;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
