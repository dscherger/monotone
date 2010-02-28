// Copyright (C) 2010 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __POLICIES_EDITABLE_POLICY_HH__
#define __POLICIES_EDITABLE_POLICY_HH__

#include "policies/policy.hh"

namespace policies {
  class editable_policy : public policy
  {
  public:
    explicit editable_policy(policy const & p);

    void set_key_name(key_id const & ident, key_name const & name);
    void remove_key(key_id const & ident);

    void set_branch(std::string const & name, branch const & value);
    void remove_branch(std::string const & name);

    void set_tag(std::string const & name, revision_id const & value);
    void remove_tag(std::string const & name);

    void set_delegation(std::string const & name, delegation const & value);
    void remove_delegation(std::string const & name);

    virtual bool outdated() const { return false; }
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
