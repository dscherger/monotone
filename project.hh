// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#ifndef __PROJECT_HH__
#define __PROJECT_HH__

#include <map>
#include <set>

#include "cert.hh"
#include "outdated_indicator.hh"
#include "vocab.hh"

class database;
class key_store;
class options;
class lua_hooks;

class branch_policy;

class tag_t
{
public:
  revision_id const ident;
  utf8 const name;
  rsa_keypair_id const key;
  tag_t(revision_id const & ident, utf8 const & name, rsa_keypair_id const & key);
};
bool operator < (tag_t const & a, tag_t const & b);

typedef bool suspended_indicator;

class policy_info;

class project_set;

class project_t
{
  // In the hypothetical future situation where one monotone process is
  // using more than one database at a time, it will be essential to use a
  // project object only with the database it was created from.  To make it
  // easy to get this right, we expose the database handle inside the
  // project object, and make it a style rule that you pass around a
  // database _or_ you pass around a project; not both.
public:
  database & db;

private:
  boost::shared_ptr<policy_info> project_policy;
  std::map<std::pair<branch_name, suspended_indicator>,
           std::pair<outdated_indicator, std::set<revision_id> >
           > branch_heads;
  std::set<branch_name> branches;
  outdated_indicator indicator;

  explicit project_t(database & db);
  friend class project_set;
public:
  project_t(branch_prefix const & project_name,
            data const & project_spec,
            database & db);
  project_t(branch_prefix const & project_name,
            revision_id const & policy_rev,
            database & db);
  project_t(database & db, lua_hooks & lua, options & opts);

  bool get_policy_branch_policy_of(branch_name const & name,
                                   branch_policy & policy_branch_policy,
                                   branch_prefix & policy_prefix);
  bool policy_exists(branch_prefix const & name) const;
  void get_subpolicies(branch_prefix const & name,
                       std::set<branch_prefix> & names) const;

  void get_branch_list(std::set<branch_name> & names,
                       bool check_heads = false);
  void get_branch_list(globish const & glob, std::set<branch_name> & names,
                       bool check_heads = false);

  // used by 'ls epochs'
  void get_branch_list(std::set<branch_uid> & ids);
  branch_uid translate_branch(branch_name const & branch);
  branch_name translate_branch(branch_uid const & branch);

  void get_branch_heads(branch_name const & name, std::set<revision_id> & heads,
                        bool ignore_suspend_certs,
                        std::multimap<revision_id, revision_id> *inverse_graph_cache_ptr = NULL);

  outdated_indicator get_tags(std::set<tag_t> & tags);
  void put_tag(key_store & keys, revision_id const & id, std::string const & name);

  bool revision_is_in_branch(revision_id const & id, branch_name const & branch);
  void put_revision_in_branch(key_store & keys,
                              revision_id const & id,
                              branch_name const & branch);

  bool revision_is_suspended_in_branch(revision_id const & id,
                                       branch_name const & branch);
  void suspend_revision_in_branch(key_store & keys,
                                  revision_id const & id,
                                  branch_name const & branch);

  outdated_indicator get_revision_cert_hashes(revision_id const & rid,
                                              std::vector<id> & hashes);
  outdated_indicator get_revision_certs(revision_id const & id,
                                        std::vector<revision<cert> > & certs);
  // This kind of assumes that we'll eventually have certs be for a specific
  // project. There's a fairly good chance that that won't happen, which would
  // mean that this can go away.
  outdated_indicator get_revision_certs_by_name(revision_id const & id,
                                                cert_name const & name,
                                                std::vector<revision<cert> > & certs);
  // What branches in *this* project does the given revision belong to?
  outdated_indicator get_revision_branches(revision_id const & id,
                                           std::set<branch_name> & branches);
  outdated_indicator get_branch_certs(branch_name const & branch,
                                      std::vector<revision<cert> > & certs);

  void put_standard_certs(key_store & keys,
                          revision_id const & id,
                          branch_name const & branch,
                          utf8 const & changelog,
                          date_t const & time,
                          std::string const & author);
  void put_standard_certs_from_options(options const & opts,
                                       lua_hooks & lua,
                                       key_store & keys,
                                       revision_id const & id,
                                       branch_name const & branch,
                                       utf8 const & changelog);

  void put_cert(key_store & keys,
                revision_id const & id,
                cert_name const & name,
                cert_value const & value);
};

class project_set
{
public:
  database & db;
  typedef std::map<branch_prefix, project_t> project_map;

private:
  project_map projects;

public:
  project_set(database & db,
              lua_hooks & lua,
              options & opts);

  project_map const & all_projects() const;

  // Get the project containing a named branch.
  project_t & get_project_of_branch(branch_name const & branch);
  project_t * maybe_get_project_of_branch(branch_name const & branch);

  // All branches in all projects.
  void get_branch_list(std::set<branch_name> & names,
                       bool check_heads = false);
  // Subset of branches from all projects.
  void get_branch_list(globish const & glob,
                       std::set<branch_name> & names,
                       bool check_heads = false);

  // used by epoch handling
  void get_branch_uids(std::set<branch_uid> & ids);
  branch_uid translate_branch(branch_name const & branch);
  branch_name translate_branch(branch_uid const & branch);

  // What tags exist across all projects?
  outdated_indicator get_tags(std::set<tag_t> & tags);
  // Because tags aren't yet per-project.
  void put_tag(key_store & keys, revision_id const & id, std::string const & name);

  // What branches in *any* project does the given revision belong to?
  outdated_indicator get_revision_branches(revision_id const & id,
                                           std::set<branch_name> & branches);

  outdated_indicator get_revision_certs_by_name(revision_id const & id,
                                                cert_name const & name,
                                                std::vector<revision<cert> > & certs);

  outdated_indicator get_revision_certs(revision_id const & id,
                                        std::vector<revision<cert> > & certs);
};

#endif


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

