// Copyright (C) 2010 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 3.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <string>
#include <ctime>

bool parse_date (const std::string s, const std::string fmt, struct tm *tp)
{
  char *p = strptime(s.c_str(), fmt.c_str(), tb);

  return p != 0;
}
// end of file
