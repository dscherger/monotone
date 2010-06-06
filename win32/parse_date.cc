// Copyright (C) 2010 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 3.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

void parse_date(const std::string s, const std::string fmt, struct tm *tp)
{
  // Apparently the Win32 API does not provide a date parsing function.
  //
  // So far, parse_date is only used in the changelog processing to
  // allow the user to change the date cert. So we just disable that
  // on Win32; see cmd_ws_commit.cc get_log_message_interactively.
  E(false, origin::system, F("date parsing not available on win32"));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
