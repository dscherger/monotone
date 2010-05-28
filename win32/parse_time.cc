// Copyright (C) 2010 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 3.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <string>

bool parse_date (const std::string s, const std::string fmt, struct tm *tp)
{
  // Apparently the Win32 API does not provide a date parsing function.
  //
  // So far, parse_date is only used in the changelog processing to
  // allow the user to change the date cert. So we just disable that
  // on Win32; see cmd_ws_commit.cc get_log_message_interactively.
  return false;
}
// end of file
