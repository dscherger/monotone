// Copyright (C) 2008 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __POLICIES_BRANCH_HH__
#define __POLICIES_BRANCH_HH__

#include <set>
#include <boost/shared_ptr.hpp>

#include "vocab.hh"

class external_key_name;

namespace policies {

  class branch
  {
    branch_uid uid;
    std::set<external_key_name> signers;
  public:
    branch();
    branch(branch_uid const & uid,
           std::set<external_key_name> const & admins);
    static branch create(std::set<external_key_name> const & admins);

    branch_uid const & get_uid() const;
    std::set<external_key_name> const & get_signers() const;
    void serialize(std::string & out) const;
    void deserialize(std::string const & in);
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
