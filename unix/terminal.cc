// Copyright (C) 2005 Derek Scherger <derek@echologic.com>
//                    Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "platform.hh"

bool have_smart_terminal()
{
  std::string term;
  if (const char* term_cstr = getenv("TERM"))
    term = term_cstr;
  else
    term = "";

  if (term == "" || term == "dumb" || !isatty(2))
    return false;
  else
    return true;
}

unsigned int terminal_width()
{
  struct winsize ws;
  int ret = ioctl(2, TIOCGWINSZ, &ws);
  if (ret < 0)
    {
      // FIXME: it would be nice to log something here
      // but we are called by the tick printing code, and trying to print
      // things while in the middle of printing a tick line is a great way to
      // break things.
      return 0;
    }
  return ws.ws_col;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
