// Copyright (C) 2008 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "policies/base_policy.hh"

#include "transforms.hh"

#include <map>

using std::make_pair;
using std::map;

class database;
class lua_hooks;
class options;

namespace policies {
  base_policy::base_policy(database & db, options const & opts, lua_hooks & lua):
    _empty(true)
  {
    load(db, opts, lua);
  }
  bool base_policy::empty() const
  {
    return _empty;
  }
  void base_policy::load(database & db, options const & opts, lua_hooks & lua)
  {
    typedef map<branch_name, hexenc<id> > override_map;
    for (override_map::const_iterator i = opts.policy_revisions.begin();
         i != opts.policy_revisions.end(); ++i)
      {
        id r = decode_hexenc(i->second);
        delegations.insert(make_pair(i->first(), delegation(revision_id(r))));
        _empty = false;
      }

    typedef map<string, data> hook_map;
    hook_map hm;
    lua.hook_get_projects(hm);
    for (hm::const_iterator i = hm.begin(); i != hm.end(); ++i)
      {
        if (delegations.find(i->first) == delegations.end())
          {
            del_map::iterator d = delegations.insert(make_pair(i->first, delegation()));
            d->second.deserialize(i->second());
            _empty = false;
          }
      }
  }
}

#endif
// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
