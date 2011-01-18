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
#include "database.hh"
#include "options.hh"
#include "transforms.hh"

#include <map>

using std::make_pair;
using std::map;
using std::pair;
using std::string;

class database;

namespace policies {
  var_domain const policy_domain("policy_roots", origin::internal);
  base_policy::base_policy(options const & opts, database & db):
    _opts(opts), _db(db), _empty(true)
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

    map<var_key, var_value> all_db_vars;
    _db.get_vars(all_db_vars);
    for (map<var_key, var_value>::iterator i = all_db_vars.begin();
         i != all_db_vars.end(); ++i)
      {
        if (i->first.first != policy_domain)
          continue;
        string const delegation_name = i->first.second();
        string const delegation_data = i->second();
        pair<del_map::iterator, bool> r;
        r = delegations.insert(make_pair(delegation_name, delegation()));
        if (r.second)
          {
            r.first->second.deserialize(delegation_data);
            _empty = false;
          }
      }
  }

  void base_policy::write(database & db, policy const & pol)
  {
    transaction_guard guard(db);
    map<var_key, var_value> all_db_vars;
    db.get_vars(all_db_vars);
    for (map<var_key, var_value>::iterator i = all_db_vars.begin();
         i != all_db_vars.end(); ++i)
      {
        if (i->first.first != policy_domain)
          continue;
        db.clear_var(i->first);
      }

    policy::del_map const & delegations = pol.list_delegations();
    for (policy::del_map::const_iterator d = delegations.begin();
         d != delegations.end(); ++d)
      {
        string s;
        d->second.serialize(s);
        var_name const delegation_name(d->first, origin::internal);
        var_value const delegation_data(s, origin::internal);
        db.set_var(make_pair(policy_domain, delegation_name), delegation_data);
      }

    guard.commit();
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
