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
#include "editable_policy.hh"

namespace policies {
  editable_policy::editable_policy() { }
  editable_policy::editable_policy(policy const & p)
    :  policy(p)
  { }

  void editable_policy::set_parent(boost::weak_ptr<policy> const & parent)
  {
    this->parent = parent;
  }

  void editable_policy::set_key(key_name const & name,
                                key_id const & value)
  {
    keys[name] = value;
  }
  void editable_policy::remove_key(key_name const & name)
  {
    keys.erase(name);
  }

  void editable_policy::set_branch(std::string const & name,
                                   branch const & value)
  {
    branches[name] = value;
  }
  void editable_policy::remove_branch(std::string const & name)
  {
    branches.erase(name);
  }

  void editable_policy::set_tag(std::string const & name,
                                revision_id const & value)
  {
    tags[name] = value;
  }
  void editable_policy::remove_tag(std::string const & name)
  {
    tags.erase(name);
  }

  void editable_policy::set_delegation(std::string const & name,
                                       delegation const & value)
  {
    delegations[name] = value;
  }
  void editable_policy::remove_delegation(std::string const & name)
  {
    delegations.erase(name);
  }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
