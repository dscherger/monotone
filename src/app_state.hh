// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
// Copyright (C) 2007-2016 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __APP_STATE_HH__
#define __APP_STATE_HH__

#include <memory>
#include <vector>

#include "options.hh"
#include "options_applicator.hh"
#include "option_reset_info.hh"
#include "lua_hooks.hh"

// This class holds any state that needs to be persistent across multiple
// commands, or be accessible to the lua hooks (which includes anything
// needed by mtn_automate()).

class app_state
{
public:
  explicit app_state();
  ~app_state();

  options opts;
  option_reset_info reset_info;
  lua_hooks lua;
  bool mtn_automate_allowed;

  void push_opt_applicator(options_applicator && applicator);

private:
  std::vector<options_applicator> opt_applicators;
};

#endif // __APP_STATE_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
