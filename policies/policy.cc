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
#include "policies/policy.hh"

#include "keys.hh"

namespace policies {
  policy::policy()
  {
  }
  policy::~policy()
  {
  }

  key_name policy::get_key_name(key_id const & ident) const
  {
  }

  key_id policy::get_key_id(key_name const & ident) const
  {
    key_map::const_iterator k = keys.find(ident());
    if (k != keys.end())
      {
        key_id out;
        key_hash_code(k->second.first, k->second.second, out);
        return out;
      }
    boost::shared_ptr<policy> p = parent.lock();
    if (p)
      return p->get_key_id(ident);
    else
      return key_id();
  }

  policy::del_map const & policy::list_delegations() const
  {
    return delegations;
  }

  map<string, branch> const & policy::list_branches() const
  {
    return branches;
  }

  map<string, revision_id> const & policy::list_tags() const
  {
    return tags;
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
