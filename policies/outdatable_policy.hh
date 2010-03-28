// Copyright (C) 2008 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __POLICIES_OUTDATABLE_POLICY__
#define __POLICIES_OUTDATABLE_POLICY__

#include "outdated_indicator.hh"
#include "policies/policy.hh"

class outdated_indicator;

namespace policies {
  class outdatable_policy : public policy
  {
    outdated_indicator ind;
  public:
    outdatable_policy();
    explicit outdatable_policy(policy const & p);
    outdatable_policy const & operator=(policy const & p);

    bool outdated() const;
    void set_indicator(outdated_indicator const & new_ind);
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
