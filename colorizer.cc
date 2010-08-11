// Copyright (C) 2010 Thomas Keller <me@thomaskeller.biz>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "colorizer.hh"
#include "platform.hh"

using std::string;
using std::map;
using std::make_pair;

colorizer::colorizer(bool enable)
{
  if (!have_smart_terminal())
    enable = false;

  if (enable)
    {
      colormap.insert(std::make_pair(normal,         ""));
      colormap.insert(std::make_pair(reset,          "\033[m"));
      colormap.insert(std::make_pair(diff_encloser,  "\033[1;34m"));
      colormap.insert(std::make_pair(diff_add,       "\033[32m"));
      colormap.insert(std::make_pair(diff_delete,    "\033[31m"));
      colormap.insert(std::make_pair(diff_change,    "\033[33m"));
      colormap.insert(std::make_pair(diff_comment,   "\033[36m"));
      colormap.insert(std::make_pair(diff_separator, "\033[1m"));
      colormap.insert(std::make_pair(log_revision,   "\033[34m"));
      colormap.insert(std::make_pair(rev_header,     "\033[1m"));
    }
}

string
colorizer::colorize(string const & in, purpose p) const
{
  if (colormap.find(p) == colormap.end())
    return in;
  return colormap.find(p)->second + in + colormap.find(reset)->second;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
