// Copyright (C) 2008 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __POLICIES_POLICY_HH__
#define __POLICIES_POLICY_HH__

#include <map>
#include <boost/weak_ptr.hpp>

#include "vocab.hh"

#include "policies/branch.hh"
#include "policies/delegation.hh"

namespace policies {
  class policy
  {
  public:
    typedef std::map<std::string, delegation> del_map;
    typedef std::map<std::string, std::pair<key_name, rsa_pub_key> > key_map;
  protected:
    std::map<std::string, branch> branches;
    del_map delegations;

    key_map keys;
    std::map<std::string, revision_id> tags;

    boost::weak_ptr<policy> parent;

    policy();

  public:
    virtual ~policy();

    // keys
    key_name get_key_name(key_id const & ident) const;
    key_id get_key_id(key_name const & ident) const;

    // delegations
    del_map const & list_delegations() const;

    // branches
    std::map<std::string, branch> const & list_branches() const;
    
    // tags
    std::map<std::string, revision_id> const & list_tags() const;
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
