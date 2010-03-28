// Copyright (C) 2008 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __POLICIES_BASE_POLICY_HH__
#define __POLICIES_BASE_POLICY_HH__

#include "policies/policy.hh"

class database;
class lua_hooks;
class options;

namespace policies {
  // The top-level policy, defined by command-line options and lua hooks.
  class base_policy : public policy
  {
    options const & _opts;
    lua_hooks & _lua;
    bool _empty;
  public:
    base_policy(options const & opts, lua_hooks & lua);
    bool empty() const;
    void reload();
    inline bool outdated() const { return false; }

    // Use lua hooks to write out the given policy.
    static void write(lua_hooks & lua, policy const & pol);
  };
}

#endif
// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
