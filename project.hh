// Copyright (C) 2007 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __PROJECT_HH__
#define __PROJECT_HH__

#include <map>
#include <set>

#include "branch_name.hh"
#include "cert.hh"
#include "editable_policy.hh"
#include "outdated_indicator.hh"
#include "vocab.hh"

class database;
class key_store;
class options;
class lua_hooks;
struct globish;
struct date_t;

// "Special" certs have syntax and semantics essential to correct operation.
// They add structure to the ancestry graph.

#define branch_cert_name cert_name("branch")
#define suspend_cert_name cert_name("suspend")
#define tag_cert_name cert_name("tag")

// "Standard" certs are largely for user information, but their values have
// conventional syntax and semantics defined by the system, and the
// revision-trust hook can use them to impose further structure on the
// ancestry graph.

#define date_cert_name cert_name("date")
#define author_cert_name cert_name("author")
#define changelog_cert_name cert_name("changelog")
#define comment_cert_name cert_name("comment")
#define testresult_cert_name cert_name("testresult")

class tag_t
{
public:
  revision_id const ident;
  utf8 const name;
  rsa_keypair_id const key;
  tag_t(revision_id const & ident,
        utf8 const & name,
        rsa_keypair_id const & key);
};
bool operator < (tag_t const & a, tag_t const & b);

typedef bool suspended_indicator;

class policy_info;

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
public:
  project_t(database & db, lua_hooks & lua, options & opts);
  // Used by migration code.
  static project_t empty_project(database & db);

  bool get_policy_branch_policy_of(branch_name const & name,
                                   editable_policy & policy_branch_policy,
                                   branch_name & policy_prefix);
  bool policy_exists(branch_name const & name) const;
  void get_subpolicies(branch_name const & name,
                       std::set<branch_name> & names) const;

  void get_branch_list(std::set<branch_name> & names,
                       bool check_heads = false);
  void get_branch_list(globish const & glob, std::set<branch_name> & names,
                       bool check_heads = false);

  // used by 'ls epochs'
  void get_branch_list(std::set<branch_uid> & ids);
  branch_uid translate_branch(branch_name const & branch);
  branch_name translate_branch(branch_uid const & branch);

  void get_branch_heads(branch_name const & name,
                        std::set<revision_id> & heads,
                        bool ignore_suspend_certs,
                        std::multimap<revision_id, revision_id> *inverse_graph_cache_ptr = NULL);

  outdated_indicator get_tags(std::set<tag_t> & tags);
  void put_tag(key_store & keys,
               revision_id const & id,
               std::string const & name);

  bool revision_is_in_branch(revision_id const & id,
                             branch_name const & branch);
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
                                        std::vector<cert> & certs);
  // This kind of assumes that we'll eventually have certs be for a specific
  // project. There's a fairly good chance that that won't happen, which would
  // mean that this can go away.
  outdated_indicator get_revision_certs_by_name(revision_id const & id,
                                                cert_name const & name,
                                                std::vector<cert> & certs);
  // What branches in *this* project does the given revision belong to?
  outdated_indicator get_revision_branches(revision_id const & id,
                                           std::set<branch_name> & branches);
  outdated_indicator get_branch_certs(branch_name const & branch,
                                      std::vector<cert> & certs);

  void put_standard_certs(key_store & keys,
                          revision_id const & id,
                          branch_name const & branch,
                          utf8 const & changelog,
                          date_t const & time,
                          std::string const & author);
  void put_standard_certs(key_store & keys,
                          revision_id const & id,
                          branch_uid const & branch,
                          utf8 const & changelog,
                          date_t const & time,
                          std::string const & author);
  void put_standard_certs_from_options(options const & opts,
                                       lua_hooks & lua,
                                       key_store & keys,
                                       revision_id const & id,
                                       branch_name const & branch,
                                       utf8 const & changelog);

  bool put_cert(key_store & keys,
                revision_id const & id,
                cert_name const & name,
                cert_value const & value);

  // "standard certs"
  void put_revision_testresult(key_store & keys,
                               revision_id const & id,
                               std::string const & results);
  void put_revision_comment(key_store & keys,
                            revision_id const & id,
                            utf8 const & comment);
};

std::string
describe_revision(project_t & project, revision_id const & id);

void
notify_if_multiple_heads(project_t & project, branch_name const & branchname,
                         bool ignore_suspend_certs);

void
guess_branch(options & opts, project_t & project, revision_id const & rev,
             branch_name & branchname);
void
guess_branch(options & opts, project_t & project, revision_id const & rev);

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
