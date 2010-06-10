// Copyright (C) 2010 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "policies/outdatable_policy.hh"

namespace policies {
  outdatable_policy::outdatable_policy() { }
  outdatable_policy::outdatable_policy(policy const & p)
    : policy(p)
  { }
  outdatable_policy const & outdatable_policy::operator=(policy const & pol)
  {
    this->policy::operator=(pol);
    ind = outdated_indicator();
    return *this;
  }

  bool outdatable_policy::outdated() const
  {
    return ind.outdated();
  }
  void outdatable_policy::set_indicator(outdated_indicator const & new_ind)
  {
    ind = new_ind;
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
