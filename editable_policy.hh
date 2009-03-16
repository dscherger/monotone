#ifndef __EDITABLE_POLICY_HH__
#define __EDITABLE_POLICY_HH__

// Copyright 2008 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.

// This files defines higher-level editing operations on
// policy-branch revisions.

#include <map>
#include <set>
#include <boost/shared_ptr.hpp>

#include "vocab.hh"

class database;
class key_store;
class lua_hooks;
class project_t;

class editable_policy_impl;
class editable_policy
{
  boost::shared_ptr<editable_policy_impl> impl;
public:
  branch_uid uid;
  bool outdated() const;

  class tag
  {
  public:
    revision_id rev;
    void write(data & dat);
    void read(data const & dat);
  };
  typedef boost::shared_ptr<tag> tag_t;
  typedef boost::shared_ptr<tag const> const_tag_t;
  class branch
  {
  public:
    branch_uid uid;
    std::set<rsa_keypair_id> committers;
    void write(data & dat);
    void read(data const & dat);
  };
  typedef boost::shared_ptr<branch> branch_t;
  typedef boost::shared_ptr<branch const> const_branch_t;
  class delegation // union { branch, revision_id }
  {
  public:
    revision_id rev;
    branch_uid uid;
    std::set<rsa_keypair_id> committers;
    void write(data & dat);
    void read(data const & dat);
  };
  typedef boost::shared_ptr<delegation> delegation_t;
  typedef boost::shared_ptr<delegation const> const_delegation_t;

  // Create a new policy.
  editable_policy(database & db,
                  std::set<rsa_keypair_id> const & admins);
  // Edit an existing policy. If the existing policy is not in
  // exactly one branch, you will have to populate the uid field
  // before calling commit().
  editable_policy(database & db, revision_id const & rev);
  // Edit an existing policy branch. This will fail if the branch
  // doesn't have exactly one head.
  editable_policy(database & db, delegation const & del);
  editable_policy();
  editable_policy(editable_policy const & other);
  editable_policy const & operator = (editable_policy const & other);

private:
  void init(revision_id const & rev);
  void init(branch const & br);
public:

  revision_id commit(project_t & project,
                     key_store & keys,
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


  delegation_t
  get_delegation(std::string const & name, bool create = false);

  branch_t
  get_branch(std::string const & name, bool create = false);

  tag_t
  get_tag(std::string const & name, bool create = false);

  typedef std::map<std::string, delegation_t> delegation_map;
  typedef std::map<std::string, const_delegation_t> const_delegation_map;
  delegation_map get_all_delegations();
  const_delegation_map get_all_delegations() const;
  typedef std::map<std::string, branch_t> branch_map;
  typedef std::map<std::string, const_branch_t> const_branch_map;
  branch_map get_all_branches();
  const_branch_map get_all_branches() const;
  typedef std::map<std::string, tag_t> tag_map;
  typedef std::map<std::string, const_tag_t> const_tag_map;
  tag_map get_all_tags();
  const_tag_map get_all_tags() const;
};
bool
operator == (editable_policy::delegation const & lhs,
             editable_policy::delegation const & rhs);

#endif
