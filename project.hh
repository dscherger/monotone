// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#ifndef __PROJECT_HH__
#define __PROJECT_HH__

#include <map>
#include <set>

#include "cert.hh"
#include "outdated_indicator.hh"
#include "vocab.hh"

class app_state;

class tag_t
{
public:
  revision_id ident;
  utf8 name;
  rsa_keypair_id key;
  tag_t(revision_id const & ident, utf8 const & name, rsa_keypair_id const & key);
};
bool operator < (tag_t const & a, tag_t const & b);

typedef bool suspended_indicator;

class policy_info;

class project_t
{
  boost::shared_ptr<policy_info> project_policy;
  app_state & app;
  std::map<std::pair<branch_name, suspended_indicator>, std::pair<outdated_indicator, std::set<revision_id> > > branch_heads;
  std::set<branch_name> branches;
  outdated_indicator indicator;

public:
  project_t(std::string const & project_name,
            system_path const & spec_file,
            app_state & app);
  explicit project_t(app_state & app);

  void get_branch_list(std::set<branch_name> & names, bool allow_suspend_certs = true);
  void get_branch_list(globish const & glob, std::set<branch_name> & names,
                        bool allow_suspend_certs = true);
  void get_branch_heads(branch_name const & name, std::set<revision_id> & heads,
                        std::multimap<revision_id, revision_id> *inverse_graph_cache_ptr = NULL);

  outdated_indicator get_tags(std::set<tag_t> & tags);
  void put_tag(revision_id const & id, std::string const & name);

  bool revision_is_in_branch(revision_id const & id, branch_name const & branch);
  void put_revision_in_branch(revision_id const & id,
                              branch_name const & branch);

  bool revision_is_suspended_in_branch(revision_id const & id, branch_name const & branch);
  void suspend_revision_in_branch(revision_id const & id,
                              branch_name const & branch);

  outdated_indicator get_revision_cert_hashes(revision_id const & rid,
                                              std::vector<hexenc<id> > & hashes);
  outdated_indicator get_revision_certs(revision_id const & id,
                                        std::vector<revision<cert> > & certs);
  outdated_indicator get_revision_certs_by_name(revision_id const & id,
                                                cert_name const & name,
                                                std::vector<revision<cert> > & certs);
  outdated_indicator get_revision_branches(revision_id const & id,
                                           std::set<branch_name> & branches);
  outdated_indicator get_branch_certs(branch_name const & branch,
                                      std::vector<revision<cert> > & certs);

  void put_standard_certs(revision_id const & id,
                          branch_name const & branch,
                          utf8 const & changelog,
                          date_t const & time,
                          utf8 const & author);
  void put_standard_certs_from_options(revision_id const & id,
                                       branch_name const & branch,
                                       utf8 const & changelog);

  void put_cert(revision_id const & id,
                cert_name const & name,
                cert_value const & value);
};

#endif


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

