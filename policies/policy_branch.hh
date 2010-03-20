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

#include "policies/branch.hh"
#include "policies/delegation.hh"
#include "policies/policy.hh"

class project_t;

namespace policies {
  policy_ptr policy_from_revision(project_t const & project,
                                  policy_ptr owner,
                                  revision_id const & rev);
  class policy_branch
  {
  public:
    typedef std::set<policy_ptr> policy_set;
  private:
    policy_ptr spec_owner;
    branch spec;
    policy_set policies;
    void reload(project_t const & project);
  public:
    typedef policy_set::const_iterator iterator;

    policy_branch(project_t const & project,
                  policy_ptr parent_policy,
                  branch const & b);
    //policy_branch(delegation const & d);

    branch const & get_spec() const;

    policy create_initial_revision() const;

    iterator begin() const;
    iterator end() const;
    size_t size() const;

    void commit(policy const & p, utf8 const & changelog,
		iterator parent_1, iterator parent_2);
    inline void commit(policy const & p, utf8 const & changelog,
		       iterator parent)
    {
      commit(p, changelog, parent, end());
    }
    inline void commit(policy const & p, utf8 const & changelog)
    {
      commit(p, changelog, end());
    }
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
