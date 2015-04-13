// Copyright (C) 2010 Thomas Keller <me@thomaskeller.biz>
//               2015 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __COLORIZER_HH__
#define __COLORIZER_HH__

#include <map>
#include <tuple>

#include "lua_hooks.hh"
#include "vocab.hh"

struct colorizer {

  typedef enum { normal = 0,
                 reset,

                 add,
                 branch,
                 change,
                 comment,
                 encloser,
                 graph,
                 hint,
                 important,
                 remove,
                 rename,
                 rev_header,
                 rev_id,
                 separator,
                } purpose;

  colorizer(bool enable, lua_hooks & lh);

  std::string
  colorize(std::string const & in, purpose p = normal) const;

private:
  std::map<purpose, std::tuple<std::string, std::string, std::string> >
    colormap;
  lua_hooks & lua;
  bool enabled;

  std::pair<purpose, std::tuple<std::string, std::string, std::string> >
  map_output_color(purpose const p);

  std::string fg_to_code(std::string const color) const;
  std::string bg_to_code(std::string const color) const;
  std::string style_to_code(std::string const style) const;

  std::string get_format(purpose const p) const;

  std::string purpose_to_name(purpose const p) const;
};

#endif // __COLORIZER_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
