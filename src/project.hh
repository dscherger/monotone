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

#include "cert.hh"
#include "outdated_indicator.hh"
#include "vocab.hh"

class arg_type;
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

struct key_identity_info
{
  key_id id;
  key_name given_name; // name given when creating the key
  key_name official_name; // name returned by hooks or (once implented) policy
};
bool
operator<(key_identity_info const & left,
          key_identity_info const & right);
std::ostream &
operator<<(std::ostream & os,
           key_identity_info const & identity);

class tag_t
{
public:
  revision_id const ident;
  utf8 const name;
  key_id key;
  tag_t(revision_id const & ident, utf8 const & name, key_id const & key);
};
bool operator < (tag_t const & a, tag_t const & b);

typedef bool suspended_indicator;

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
  // These are caches of what's in the database. They are updated when
  // they're noticed to be out of date, which will always be during a
  // logically read-only operation.
  mutable std::map < std::pair<branch_name, suspended_indicator>,
          std::pair<outdated_indicator, std::set<revision_id> >
          > branch_heads;
  mutable std::set<branch_name> branches;
  mutable outdated_indicator indicator;

public:
  project_t(database & db);

  void get_branch_list(std::set<branch_name> & names,
                       bool check_heads = false) const;
  void get_branch_list(globish const & glob, std::set<branch_name> & names,
                       bool check_heads = false) const;
  void get_branch_heads(branch_name const & name, std::set<revision_id> & heads,
                        bool ignore_suspend_certs,
                        std::multimap<revision_id, revision_id> *inverse_graph_cache_ptr = NULL) const;

  outdated_indicator get_tags(std::set<tag_t> & tags) const;
  void put_tag(key_store & keys, revision_id const & id, std::string const & name);

  bool revision_is_in_branch(revision_id const & id, branch_name const & branch) const;
  void put_revision_in_branch(key_store & keys,
                              revision_id const & id,
                              branch_name const & branch);

  bool revision_is_suspended_in_branch(revision_id const & id, branch_name const & branch) const;
  void suspend_revision_in_branch(key_store & keys,
                                  revision_id const & id,
                                  branch_name const & branch);

  outdated_indicator get_revision_cert_hashes(revision_id const & rid,
                                              std::vector<id> & hashes) const;
  outdated_indicator get_revision_certs(revision_id const & id,
                                        std::vector<cert> & certs) const;
  outdated_indicator get_revision_certs_by_name(revision_id const & id,
                                                cert_name const & name,
                                                std::vector<cert> & certs) const;
  outdated_indicator get_revision_branches(revision_id const & id,
                                           std::set<branch_name> & branches) const;
  outdated_indicator get_branch_certs(branch_name const & branch,
                                      std::vector<std::pair<id, cert> > & certs) const;

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

private:
  // lookup the key ID associated with a particular key name
  void lookup_key_by_name(key_store * const keys,
                          lua_hooks & lua,
                          key_name const & name,
                          key_id & id) const;
  // get the name given when creating the key
  void get_given_name_of_key(key_store * const keys,
                             key_id const & id,
                             key_name & name) const;
  void complete_key_identity_from_id(key_store * const keys,
                                     lua_hooks & lua,
                                     key_identity_info & info) const;
  void get_key_identity(key_store * const keys,
                        lua_hooks & lua,
                        external_key_name const & input,
                        key_identity_info & output) const;
public:
  void complete_key_identity_from_id(key_store & keys,
                                     lua_hooks & lua,
                                     key_identity_info & info) const;
  void complete_key_identity_from_id(lua_hooks & lua,
                                     key_identity_info & info) const;
  void get_key_identity(key_store & keys,
                        lua_hooks & lua,
                        external_key_name const & input,
                        key_identity_info & output) const;
  void get_key_identity(lua_hooks & lua,
                        external_key_name const & input,
                        key_identity_info & output) const;
};

std::string
describe_revision(options const & opts, lua_hooks & lua,
                  project_t & project, revision_id const & id);

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
