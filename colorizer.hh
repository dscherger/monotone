// Copyright (C) 2010 Thomas Keller <me@thomaskeller.biz>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __COLORIZER_HH__
#define __COLORIZER_HH__

#include "vocab.hh"
#include <map>

struct colorizer {

  typedef enum { normal = 0,
                 reset,
                 diff_encloser,
                 diff_add,
                 diff_delete,
                 diff_change,
                 diff_comment,
                 diff_separator,
                 log_revision,
                 rev_header
                 } purpose;

  colorizer(bool enable);

  std::string
  colorize(std::string const & in, purpose p = normal) const;

private:
  std::map<purpose, std::string> colormap;
};

#endif // __COLORIZER_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
