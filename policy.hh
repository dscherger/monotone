// Copyright 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL v2 or later

#ifndef __POLICY_HH__
#define __POLICY_HH__

#include <map>
#include <set>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "branch_name.hh"
#include "database.hh"
#include "editable_policy.hh"
#include "vocab.hh"

outdated_indicator
get_branch_heads(editable_policy::branch const & br,
                 bool ignore_suspend_certs,
                 database & db,
                 std::set<revision_id> & heads,
                 std::multimap<revision_id, revision_id>
                 * inverse_graph_cache_ptr);

bool
revision_is_in_branch(editable_policy::branch const & br,
                      revision_id const & rid,
                      database & db);

class policy_branch : public boost::enable_shared_from_this<policy_branch>
{
  database & db;
  boost::shared_ptr<editable_policy const> policy;
  boost::shared_ptr<editable_policy::delegation const> delayed;
  typedef std::map<branch_name, boost::shared_ptr<policy_branch> > delegation_map;
  delegation_map delegations;

  // Load from the db.
  bool init();
  // Process the loaded policy info.
  void init_lower();

  policy_branch(database & db);
public:
  static boost::shared_ptr<policy_branch> empty_policy(database & db);

private:
  policy_branch(editable_policy::delegation const & del,
                database & db);

  policy_branch(boost::shared_ptr<editable_policy const> pol,
                database & db);
public:
  static boost::shared_ptr<policy_branch>
  create(editable_policy::delegation const & del,
         database & db);
  static boost::shared_ptr<policy_branch>
  create(boost::shared_ptr<editable_policy const> pol,
         database & db);


  boost::shared_ptr<editable_policy const> get_policy();

  boost::shared_ptr<policy_branch> walk(branch_name target,
                                        branch_name & result);

  typedef std::map<branch_name, editable_policy::branch const> branchmap;

  branchmap branches();

  typedef std::map<branch_name, editable_policy::tag const> tagmap;
  tagmap tags();
private:
  void
  branches(branch_name const & prefix, branchmap & branchlist);
  void
  tags(branch_name const & prefix, tagmap & taglist);
public:

  boost::shared_ptr<editable_policy::branch const>
  maybe_get_branch_policy(branch_name const & name);
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
