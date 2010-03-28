// Copyright (C) 2010 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __POLICIES_POLICY_BRANCH_HH__
#define __POLICIES_POLICY_BRANCH_HH__

#include <boost/shared_ptr.hpp>

#include "origin_type.hh"
#include "outdated_indicator.hh"
#include "policies/branch.hh"
#include "policies/delegation.hh"
#include "policies/editable_policy.hh"
#include "policies/outdatable_policy.hh"


class key_store;
class project_t;

namespace policies {
  policy_ptr policy_from_revision(project_t const & project,
                                  policy_ptr owner,
                                  revision_id const & rev);
  class policy_branch
  {
    policy_ptr spec_owner;
    branch spec;
    size_t _num_heads;
    editable_policy my_policy;
    bool loaded;
    outdated_indicator _indicator;
    bool reload(project_t const & project);
  public:
    policy_branch(project_t const & project,
                  policy_ptr parent_policy,
                  branch const & b);

    branch const & get_spec() const;

    size_t num_heads() const;
    // return false if we can't get a coherent policy, due to
    // having multiple heads and they can't be auto-merged
    bool try_get_policy(outdatable_policy & pol) const;
    // wraper that will E() for you
    void get_policy(outdatable_policy & pol, origin::type ty) const;
    void get_policy(policy & pol, origin::type ty) const;
    // return false if the commit fails, due to
    // having multiple heads that can't be auto-merged
    bool try_commit(project_t & project, key_store & keys,
                    policy const & pol,
                    utf8 const & message,
                    revision_id & result);
    // wrapper that will E() for you
    revision_id commit(project_t & project, key_store & keys,
                       policy const & pol,
                       utf8 const & message,
                       origin::type ty);
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
