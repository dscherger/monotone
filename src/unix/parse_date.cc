// Copyright (C) 2010 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 3.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../base.hh"
#include "../sanity.hh"
#include "../origin_type.hh"

#include <ctime>

void parse_date(const std::string s, const std::string fmt, struct tm *tp)
{
  char *p = strptime(s.c_str(), fmt.c_str(), tp);

  E(p, origin::user,// failed to match all of the fromat string
    F("unable to parse date '%s' with format '%s'") % s % fmt);

  E(*p == 0, origin::user, // extraneous characters in input string
    F("invalid date '%s' not matched by format '%s'") % s % fmt);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
