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

#include "branch_name.hh"
#include "lua_hooks.hh"
#include "options.hh"
#include "transforms.hh"

#include <map>

using std::make_pair;
using std::map;
using std::pair;
using std::string;

class database;

namespace policies {
  base_policy::base_policy(options const & opts, lua_hooks & lua):
    _opts(opts), _lua(lua), _empty(true)
  {
    reload();
  }
  bool base_policy::empty() const
  {
    return _empty;
  }

  void base_policy::reload()
  {
    delegations.clear();
    typedef map<branch_name, hexenc<id> > override_map;
    for (override_map::const_iterator i = _opts.policy_revisions.begin();
         i != _opts.policy_revisions.end(); ++i)
      {
        id r;
        decode_hexenc(i->second, r);
        delegations.insert(make_pair(i->first(), delegation(revision_id(r))));
        _empty = false;
      }

    typedef map<string, data> hook_map;
    hook_map hm;
    _lua.hook_get_projects(hm);
    for (hook_map::const_iterator i = hm.begin(); i != hm.end(); ++i)
      {
        if (delegations.find(i->first) == delegations.end())
          {
            pair<del_map::iterator, bool> r;
            r = delegations.insert(make_pair(i->first, delegation()));
            r.first->second.deserialize(i->second());
            _empty = false;
          }
      }
  }

  void base_policy::write(lua_hooks & lua, policy const & pol)
  {
    map<string, data> hm;

    policy::del_map const & delegations = pol.list_delegations();
    for (policy::del_map::const_iterator d = delegations.begin();
         d != delegations.end(); ++d)
      {
        string s;
        d->second.serialize(s);
        hm.insert(make_pair(d->first, data(s, origin::internal)));
      }

    lua.hook_write_projects(hm);
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
