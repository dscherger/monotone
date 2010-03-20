// Copyright (C) 2008 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __POLICIES_DELEGATION_HH__
#define __POLICIES_DELEGATION_HH__

#include <set>
#include <boost/shared_ptr.hpp>

#include "vocab.hh"

#include "policies/branch.hh"

class app_state;
class external_key_name;
class project_t;

namespace policies {
  class policy;

  class delegation
  {
    enum delegation_type { revision_type, branch_type };
    delegation_type type;
    revision_id revid;
    branch branch_desc;
  public:
    delegation();
    explicit delegation(revision_id const & r);
    explicit delegation(branch const & b);
    static delegation create(app_state & app,
                             std::set<external_key_name> const & admins);

    bool is_branch_type() const;
    branch const & get_branch_spec() const;

    void serialize(std::string & out) const;
    void deserialize(std::string const & in);

    boost::shared_ptr<policy> resolve(project_t const & project,
                                      boost::shared_ptr<policy> parent) const;
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
