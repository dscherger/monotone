#ifndef __EDITABLE_POLICY_HH__
#define __EDITABLE_POLICY_HH__

// Copyright 2008 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.

// This files defines higher-level editing operations on
// policy-branch revisions.

#include <set>
#include <boost/shared_ptr.hpp>

#include "vocab.hh"

class branch_policy;
class database;
class key_store;
class lua_hooks;

class editable_policy_impl;
class editable_policy
{
  boost::shared_ptr<editable_policy_impl> impl;
public:
  branch_uid uid;

  class tag
  {
  public:
    revision_id rev;
    void write(data & dat);
    void read(data const & dat);
  };
  class branch
  {
  public:
    branch_uid uid;
    std::set<rsa_keypair_id> committers;
    void write(data & dat);
    void read(data const & dat);
  };
  class delegation
  {
  public:
    revision_id rev;
    branch_uid uid;
    std::set<rsa_keypair_id> committers;
    void write(data & dat);
    void read(data const & dat);
  };

  // Create a new policy.
  editable_policy(database & db,
                  std::set<rsa_keypair_id> const & admins);
  // Edit an existing policy. If the existing policy is not in
  // exactly one branch, you will have to populate the uid field
  // before calling commit().
  editable_policy(database & db, revision_id const & rev);
  // Edit an existing policy branch. This will fail if the branch
  // doesn't have exactly one head.
  editable_policy(database & db, branch_policy const & policy_policy);

  revision_id commit(key_store & keys,
                     lua_hooks & lua,
                     utf8 const & changelog,
                     std::string author = "");
  revision_id calculate_id();
  void get_spec(data & dat);


  void remove_delegation(std::string const & name);
  void remove_branch(std::string const & name);
  void remove_tag(std::string const & name);

  void rename_delegation(std::string const & from, std::string const & to);
  void rename_branch(std::string const & from, std::string const & to);
  void rename_tag(std::string const & from, std::string const & to);


  boost::shared_ptr<delegation>
  get_delegation(std::string const & name, bool create = false);

  boost::shared_ptr<branch>
  get_branch(std::string const & name, bool create = false);

  boost::shared_ptr<tag>
  get_tag(std::string const & name, bool create = false);
};

#endif
