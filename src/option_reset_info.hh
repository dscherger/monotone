// Copyright (C) 2010 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#ifndef __OPTION_RESET_INFO_HH__
#define __OPTION_RESET_INFO_HH__

#include "option.hh"

namespace commands
{
  class command;
}

struct option_reset_info
{
  args_vector default_args;
  args_vector cmdline_args;
  commands::command const * cmd;
};

#endif // __OPTION_RESET_INFO_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
