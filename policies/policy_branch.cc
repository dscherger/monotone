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
#include "policies/policy_branch.hh"

namespace policies {
  policy_branch::policy_branch(branch const & b)
    : spec(b)
  {
    reload();
  }
  branch const & policy_branch::get_spec() const
  {
    return spec;
  }

  policy_branch::iterator policy_branch::begin() const
  {
    return policies.begin();
  }
  policy_branch::iterator policy_branch::end() const
  {
    return policies.end();
  }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
