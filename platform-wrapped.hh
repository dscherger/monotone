// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __PLATFORM_WRAPPED_HH__
#define __PLATFORM_WRAPPED_HH__

#include "paths.hh"
#include "platform.hh"
#include "vocab.hh"

inline void change_current_working_dir(any_path const & to)
{
  change_current_working_dir(to.as_external());
}

inline path::status get_path_status(any_path const & path)
{
  std::string p(path.as_external());
  return get_path_status(p.empty()?".":p);
}

inline void rename_clobberingly(any_path const & from, any_path const & to)
{
  rename_clobberingly(from.as_external(), to.as_external());
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
