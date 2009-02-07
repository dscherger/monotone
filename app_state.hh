#ifndef __APP_STATE_HH__
#define __APP_STATE_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <boost/shared_ptr.hpp>
#include "botan/rng.h"

#include "options.hh"
#include "lua_hooks.hh"

// This class holds any state that needs to be persistent across multiple
// commands, or be accessible to the lua hooks (which includes anything
// needed by mtn_automate()).

struct database_cache;
class app_state
{
public:
  explicit app_state();
  ~app_state();

  options opts;
  lua_hooks lua;
  bool mtn_automate_allowed;
  boost::shared_ptr<Botan::RandomNumberGenerator> rng;
  boost::shared_ptr<database_cache> dbcache;
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __APP_STATE_HH__
