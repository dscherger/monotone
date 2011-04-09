// Copyright (C) 2005 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __DATE_FORMAT_HH__
#define __DATE_FORMAT_HH__

#include "options.hh"
#include "lua_hooks.hh"
#include "vocab.hh"

inline std::string
get_date_format(options const & opts,
                lua_hooks & lua,
                date_format_spec spec)
{
  std::string date_fmt;
  if (!opts.no_format_dates)
    {
      if (!opts.date_fmt.empty())
        date_fmt = opts.date_fmt;
      else
        lua.hook_get_date_format_spec(spec, date_fmt);
    }
  return date_fmt;
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
