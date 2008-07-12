// Copyright 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL v2 or later

#ifndef __POLICY_HH__
#define __POLICY_HH__

#include <map>
#include <set>

#include <boost/shared_ptr.hpp>

#include "database.hh"
#include "vocab.hh"


class branch_policy
{
  branch_name _visible_name;
  branch_uid _branch_cert_value;
  std::set<rsa_keypair_id> _committers;
public:
  branch_name const & visible_name;
  branch_uid const & branch_cert_value;
  std::set<rsa_keypair_id> const & committers;

  branch_policy()
    : visible_name(_visible_name),
      branch_cert_value(_branch_cert_value),
      committers(_committers)
  { }
  branch_policy(branch_name const & name,
                branch_uid const & value,
                std::set<rsa_keypair_id> const & keys)
    : _visible_name(name),
      _branch_cert_value(value),
      _committers(keys),
      visible_name(_visible_name),
      branch_cert_value(_branch_cert_value),
      committers(_committers)
  { }
  branch_policy(branch_policy const & rhs)
    : _visible_name(rhs._visible_name),
      _branch_cert_value(rhs._branch_cert_value),
      _committers(rhs._committers),
      visible_name(_visible_name),
      branch_cert_value(_branch_cert_value),
      committers(_committers)
  { }
  branch_policy const &
  operator=(branch_policy const & rhs)
  {
    _visible_name = rhs._visible_name;
    _branch_cert_value = rhs._branch_cert_value;
    _committers = rhs._committers;
    return *this;
  }
};

outdated_indicator
get_branch_heads(branch_policy const & pol,
                 bool ignore_suspend_certs,
                 database & db,
                 std::set<revision_id> & heads,
                 std::multimap<revision_id, revision_id>
                 * inverse_graph_cache_ptr);

bool
revision_is_in_branch(branch_policy const & pol,
                      revision_id const & rid,
                      database & db);

class policy_revision;

class policy_branch
{
  branch_prefix prefix;
  branch_uid my_branch_cert_value;
  std::set<rsa_keypair_id> my_committers;

  database & db;
  boost::shared_ptr<policy_revision> rev;
  void init(data const & spec);

  policy_branch(database & db);
public:
  static policy_branch empty_policy(database & db);
  policy_branch(data const & spec,
                branch_prefix const & prefix,
                database & db);
  policy_branch(revision_id const & rid,
                branch_prefix const & prefix,
                database & db);
  policy_branch(std::map<branch_prefix, data> const & delegations,
                branch_prefix const & my_prefix,
                database & db);
  boost::shared_ptr<policy_revision> get_policy();
  std::map<branch_name, branch_policy> branches();

  boost::shared_ptr<branch_policy>
  maybe_get_branch_policy(branch_name const & name);

  policy_revision const *
  get_nearest_policy(branch_name const & name,
                     branch_policy & policy_policy,
                     branch_prefix & policy_prefix,
                     std::string const & accumulated_prefix);
};

class policy_revision
{
  std::map<branch_name, branch_policy> branches;
  std::map<branch_prefix, policy_branch> delegations;
public:
  policy_revision(database & db,
                  revision_id const & rev,
                  branch_prefix const & prefix);
  policy_revision(std::map<branch_prefix, data> const & del,
                  database & db);
  std::map<branch_name, branch_policy> all_branches();

  void get_delegation_names(std::set<branch_prefix> & names) const;

  policy_revision const *
  get_nearest_policy(branch_name const & name,
                     branch_policy & policy_policy,
                     branch_prefix & policy_prefix,
                     std::string const & accumulated_prefix);
};



#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
