// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#include "base.hh"
#include "vector.hh"
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

#include "basic_io.hh"
#include "cert.hh"
#include "database.hh"
#include "file_io.hh"
#include "globish.hh"
#include "project.hh"
#include "revision.hh"
#include "transforms.hh"

using std::string;
using std::set;
using std::vector;
using std::map;
using std::multimap;
using std::make_pair;
using boost::shared_ptr;

namespace basic_io
{
  namespace syms
  {
    symbol const branch_uid("branch_uid");
    symbol const committer("committer");
    symbol const policy("policy_branch_id");
    symbol const administrator("administrator");
  }
}

struct branch_policy
{
  branch_name const visible_name;
  branch_uid const branch_cert_value;
  set<rsa_keypair_id> const committers;

  branch_policy(branch_name const & name,
                branch_uid const & value,
                set<rsa_keypair_id> const & keys)
    : visible_name(name),
      branch_cert_value(value),
      committers(keys)
  { }
};

class policy_revision;

class policy_branch
{
  branch_prefix prefix;
  branch_uid my_branch_cert_value;
  set<rsa_keypair_id> my_committers;

  database & db;
  shared_ptr<policy_revision> rev;
  void init(data const & spec)
  {
    basic_io::input_source src(spec(), "policy spec");
    basic_io::tokenizer tok(src);
    basic_io::parser pa(tok);

    while (pa.symp())
      {
        if(pa.symp(basic_io::syms::policy))
          {
            pa.sym();
            string branch;
            pa.str(branch);
            my_branch_cert_value = branch_uid(branch);
          }
        else if (pa.symp(basic_io::syms::administrator))
          {
            pa.sym();
            string key;
            pa.str(key);
            my_committers.insert(rsa_keypair_id(key));
          }
        else
          {
            N(false, F("Unable to understand policy spec file"));
          }
      }

    I(src.lookahead == EOF);
  }
public:
  policy_branch(data const & spec,
                branch_prefix const & prefix,
                database & db)
    : prefix(prefix), db(db)
  {
    init(spec);
  }
  policy_branch(system_path const & spec_file,
                branch_prefix const & prefix,
                database & db)
    : prefix(prefix), db(db)
  {
    require_path_is_file(spec_file,
                         F("policy spec file %s does not exist") % spec_file,
                         F("policy spec file %s is a directory") % spec_file);
    data spec;
    read_data(spec_file, spec);
    init(spec);
  }
  shared_ptr<policy_revision> get_policy();
  map<branch_name, branch_policy> branches();
};

class policy_revision
{
  map<branch_name, branch_policy> branches;
  map<branch_prefix, policy_branch> delegations;
public:
  policy_revision(database & db,
                  revision_id const & rev,
                  branch_prefix const & prefix)
  {
    roster_t roster;
    db.get_roster(rev, roster);

    file_path branch_dir = file_path_internal("branches");
    file_path delegation_dir = file_path_internal("delegations");

    if (roster.has_node(branch_dir))
      {
        dir_t branch_node = downcast_to_dir_t(roster.get_node(branch_dir));
        for (dir_map::const_iterator i = branch_node->children.begin();
             i != branch_node->children.end(); ++i)
          {
            branch_name branch;
            if (i->first() != "__main__")
              {
                branch = branch_name(prefix() + "." + i->first());
              }
            else
              {
                branch = branch_name(prefix());
              }
            file_id ident = downcast_to_file_t(i->second)->content;
            file_data spec;
            db.get_file_version(ident, spec);

            branch_uid branch_cert_value;
            set<rsa_keypair_id> committers;

            basic_io::input_source src(spec.inner()(), "branch spec");
            basic_io::tokenizer tok(src);
            basic_io::parser pa(tok);

            while (pa.symp())
              {
                if (pa.symp(basic_io::syms::branch_uid))
                  {
                    pa.sym();
                    string branch;
                    pa.str(branch);
                    branch_cert_value = branch_uid(branch);
                  }
                else if (pa.symp(basic_io::syms::committer))
                  {
                    pa.sym();
                    string key;
                    pa.str(key);
                    committers.insert(rsa_keypair_id(key));
                  }
                else
                  {
                    N(false,
                      F("Unable to understand branch spec file for %s in revision %s")
                      % i->first() % rev);
                  }
              }                

            branches.insert(make_pair(branch,
                                      branch_policy(branch,
                                                    branch_cert_value,
                                                    committers)));
          }
      }
    if (roster.has_node(delegation_dir))
      {
        dir_t delegation_node = downcast_to_dir_t(roster.get_node(delegation_dir));
        for (dir_map::const_iterator i = delegation_node->children.begin();
             i != delegation_node->children.end(); ++i)
          {
            branch_prefix subprefix(prefix() + "." + i->first());
            file_id ident = downcast_to_file_t(i->second)->content;
            file_data spec;
            db.get_file_version(ident, spec);

            delegations.insert(make_pair(subprefix,
                                         policy_branch(spec.inner(),
                                                       subprefix,
                                                       db)));
          }
      }
  }
  map<branch_name, branch_policy> all_branches()
  {
    typedef map<branch_name, branch_policy> branch_policies;
    typedef map<branch_prefix, policy_branch> policy_branches;
    branch_policies out = branches;
    for (policy_branches::iterator i = delegations.begin();
         i != delegations.end(); ++i)
      {
        branch_policies del = i->second.branches();
        for (branch_policies::const_iterator i = del.begin();
             i != del.end(); ++i)
          {
            out.insert(*i);
          }
      }
    return out;
  }
};

namespace
{
  struct not_in_policy_branch : public is_failure
  {
    database & db;
    base64<cert_value > const & branch_encoded;
    set<rsa_keypair_id> const & trusted_signers;
    bool is_trusted(set<rsa_keypair_id> const & signers,
                    hexenc<id> const & rid,
                    cert_name const & name,
                    cert_value const & value)
    {
      for (set<rsa_keypair_id>::const_iterator i = signers.begin();
           i != signers.end(); ++i)
        {
          set<rsa_keypair_id>::const_iterator t = trusted_signers.find(*i);
          if (t != trusted_signers.end())
            return true;
        }
      return false;
    }
    not_in_policy_branch(database & db,
                         base64<cert_value> const & branch_encoded,
                         set<rsa_keypair_id> const & trusted)
      : db(db), branch_encoded(branch_encoded), trusted_signers(trusted)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector< revision<cert> > certs;
      db.get_revision_certs(rid,
                            cert_name(branch_cert_name),
                            branch_encoded,
                            certs);
      erase_bogus_certs(certs,
                        boost::bind(&not_in_policy_branch::is_trusted,
                                    this, _1, _2, _3, _4),
                        db);
      return certs.empty();
    }
  };

  revision_id policy_branch_head(branch_uid const & name,
                                 set<rsa_keypair_id> const & trusted_signers,
                                 database & db)
  {
     L(FL("getting heads of policy branch %s") % name);
     base64<cert_value> branch_encoded;
     encode_base64(cert_value(name()), branch_encoded);
     set<revision_id> heads;

     db.get_revisions_with_cert(cert_name(branch_cert_name),
                                branch_encoded,
                                heads);

     not_in_policy_branch p(db, branch_encoded, trusted_signers);
     erase_ancestors_and_failures(heads, p, db, NULL);

     E(heads.size() == 1,
       F("policy branch %s has %d heads, should have 1 head")
       % name % heads.size());

     return *heads.begin();
  }
}


shared_ptr<policy_revision> policy_branch::get_policy()
{
  if (!rev)
    {
      revision_id rid;
      rid = policy_branch_head(my_branch_cert_value, my_committers, db);
      rev.reset(new policy_revision(db, rid, prefix));
    }
  return rev;
}
map<branch_name, branch_policy> policy_branch::branches()
{
  shared_ptr<policy_revision> policy = get_policy();
  return policy->all_branches();
}

////////////////////////////////////////////////////////////////////////

class policy_info
{
public:
  policy_branch policy;
  bool passthru;
  policy_info(system_path const & spec_file,
              branch_prefix const & prefix,
              database & db)
    : policy(spec_file, prefix, db), passthru(false)
  {
  }
  explicit policy_info(database & db)
    : policy(data(""), branch_prefix(""), db), passthru(true)
  {
  }
};

project_t::project_t(branch_prefix const & project_name,
                     system_path const & spec_file,
                     database & db)
  : project_policy(new policy_info(spec_file, project_name, db)), db(db)
{}

project_t::project_t(database & db)
  : project_policy(new policy_info(db)), db(db)
{}

void
project_t::get_branch_list(std::set<branch_name> & names, bool check_certs_valid)
{
  if (!project_policy->passthru)
    {
      map<branch_name, branch_policy> branches = project_policy->policy.branches();
      for (map<branch_name, branch_policy>::const_iterator i = branches.begin();
           i != branches.end(); ++i)
        {
          names.insert(i->first);
        }
      return;
    }
  if (indicator.outdated())
    {
      std::vector<std::string> got;
      indicator = db.get_branches(got);
      branches.clear();
      multimap<revision_id, revision_id> inverse_graph_cache;
  
      for (std::vector<std::string>::iterator i = got.begin();
           i != got.end(); ++i)
        {
          // check that the branch has at least one non-suspended head
          const branch_name branch(*i);
          std::set<revision_id> heads;

          if (check_certs_valid)
            get_branch_heads(branch, heads, &inverse_graph_cache);
          
          if (!check_certs_valid || !heads.empty())
            branches.insert(branch);
        }
    }

  names = branches;
}

void
project_t::get_branch_list(globish const & glob,
                           std::set<branch_name> & names,
                           bool check_certs_valid)
{
  if (!project_policy->passthru)
    {
      map<branch_name, branch_policy> branches = project_policy->policy.branches();
      for (map<branch_name, branch_policy>::const_iterator i = branches.begin();
           i != branches.end(); ++i)
        {
          if (glob.matches(i->first()))
            names.insert(i->first);
        }
      return;
    }

  std::vector<std::string> got;
  db.get_branches(glob, got);
  names.clear();
  multimap<revision_id, revision_id> inverse_graph_cache;
  
  for (std::vector<std::string>::iterator i = got.begin();
       i != got.end(); ++i)
    {
      // check that the branch has at least one non-suspended head
      const branch_name branch(*i);
      std::set<revision_id> heads;

      if (check_certs_valid)
        get_branch_heads(branch, heads, &inverse_graph_cache);

      if (!check_certs_valid || !heads.empty())
        names.insert(branch);
    }
}

void
project_t::get_branch_list(std::set<branch_uid> & branch_ids)
{
  branch_ids.clear();
  if (project_policy->passthru)
    {
      std::set<branch_name> names;
      get_branch_list(names, false);
      for (std::set<branch_name>::const_iterator i = names.begin();
           i != names.end(); ++i)
        {
          branch_ids.insert(branch_uid((*i)()));
        }
      return;
    }
  typedef map<branch_name, branch_policy> branchlist;
  branchlist branches = project_policy->policy.branches();
  for (branchlist::const_iterator i = branches.begin();
       i != branches.end(); ++i)
    {
      branch_ids.insert(i->second.branch_cert_value);
    }
}

branch_uid
project_t::translate_branch(branch_name const & name)
{
  if (project_policy->passthru)
    return branch_uid(name());
  typedef map<branch_name, branch_policy> branchlist;
  branchlist branches = project_policy->policy.branches();
  branchlist::iterator i = branches.find(name);
  I(i != branches.end());
  return i->second.branch_cert_value;
}

branch_name
project_t::translate_branch(branch_uid const & uid)
{
  if (project_policy->passthru)
    return branch_name(uid());
  typedef map<branch_name, branch_policy> branchlist;
  branchlist branches = project_policy->policy.branches();
  for (branchlist::const_iterator i = branches.begin();
       i != branches.end(); ++i)
    {
      if (i->second.branch_cert_value == uid)
        return i->first;
    }
  I(false);
}

namespace
{
  struct not_in_branch : public is_failure
  {
    database & db;
    base64<cert_value > const & branch_encoded;
    not_in_branch(database & db,
                  base64<cert_value> const & branch_encoded)
      : db(db), branch_encoded(branch_encoded)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector< revision<cert> > certs;
      db.get_revision_certs(rid,
                            cert_name(branch_cert_name),
                            branch_encoded,
                            certs);
      erase_bogus_certs(certs, db);
      return certs.empty();
    }
  };

  struct suspended_in_branch : public is_failure
  {
    database & db;
    base64<cert_value > const & branch_encoded;
    suspended_in_branch(database & db,
                        base64<cert_value> const & branch_encoded)
      : db(db), branch_encoded(branch_encoded)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector< revision<cert> > certs;
      db.get_revision_certs(rid,
                            cert_name(suspend_cert_name),
                            branch_encoded,
                            certs);
      erase_bogus_certs(certs, db);
      return !certs.empty();
    }
  };
}

void
project_t::get_branch_heads(branch_name const & name, std::set<revision_id> & heads,
                            multimap<revision_id, revision_id> *inverse_graph_cache_ptr)
{
  std::pair<branch_name, suspended_indicator> cache_index(name,
    db.get_opt_ignore_suspend_certs());
  std::pair<outdated_indicator, std::set<revision_id> > & branch = branch_heads[cache_index];
  if (branch.first.outdated())
    {
      L(FL("getting heads of branch %s") % name);
      base64<cert_value> branch_encoded;
      encode_base64(cert_value(name()), branch_encoded);

      outdated_indicator stamp;
      branch.first = db.get_revisions_with_cert(cert_name(branch_cert_name),
                                                    branch_encoded,
                                                    branch.second);

      not_in_branch p(db, branch_encoded);
      erase_ancestors_and_failures(branch.second, p, db, inverse_graph_cache_ptr);
      
      if (!db.get_opt_ignore_suspend_certs())
        {
          suspended_in_branch s(db, branch_encoded);
          std::set<revision_id>::iterator it = branch.second.begin();
          while (it != branch.second.end())
            if (s(*it))
              branch.second.erase(it++);
            else
              it++;
        }
      
      L(FL("found heads of branch %s (%s heads)")
        % name % branch.second.size());
    }
  heads = branch.second;
}

bool
project_t::revision_is_in_branch(revision_id const & id,
                                 branch_name const & branch)
{
  base64<cert_value> branch_encoded;
  encode_base64(cert_value(branch()), branch_encoded);

  vector<revision<cert> > certs;
  db.get_revision_certs(id, branch_cert_name, branch_encoded, certs);

  int num = certs.size();

  erase_bogus_certs(certs, db);

  L(FL("found %d (%d valid) %s branch certs on revision %s")
    % num
    % certs.size()
    % branch
    % id);

  return !certs.empty();
}

void
project_t::put_revision_in_branch(key_store & keys,
                                  revision_id const & id,
                                  branch_name const & branch)
{
  cert_revision_in_branch(id, branch, db, keys);
}

bool
project_t::revision_is_suspended_in_branch(revision_id const & id,
                                 branch_name const & branch)
{
  base64<cert_value> branch_encoded;
  encode_base64(cert_value(branch()), branch_encoded);

  vector<revision<cert> > certs;
  db.get_revision_certs(id, suspend_cert_name, branch_encoded, certs);

  int num = certs.size();

  erase_bogus_certs(certs, db);

  L(FL("found %d (%d valid) %s suspend certs on revision %s")
    % num
    % certs.size()
    % branch
    % id);

  return !certs.empty();
}

void
project_t::suspend_revision_in_branch(key_store & keys,
                                      revision_id const & id,
                                      branch_name const & branch)
{
  cert_revision_suspended_in_branch(id, branch, db, keys);
}


outdated_indicator
project_t::get_revision_cert_hashes(revision_id const & rid,
                                    std::vector<hexenc<id> > & hashes)
{
  return db.get_revision_certs(rid, hashes);
}

outdated_indicator
project_t::get_revision_certs(revision_id const & id,
                              std::vector<revision<cert> > & certs)
{
  return db.get_revision_certs(id, certs);
}

outdated_indicator
project_t::get_revision_certs_by_name(revision_id const & id,
                                      cert_name const & name,
                                      std::vector<revision<cert> > & certs)
{
  outdated_indicator i = db.get_revision_certs(id, name, certs);
  erase_bogus_certs(certs, db);
  return i;
}

outdated_indicator
project_t::get_revision_branches(revision_id const & id,
                                 std::set<branch_name> & branches)
{
  std::vector<revision<cert> > certs;
  outdated_indicator i = get_revision_certs_by_name(id, branch_cert_name, certs);
  branches.clear();
  for (std::vector<revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value b;
      decode_base64(i->inner().value, b);
      branches.insert(branch_name(b()));
    }
  return i;
}

outdated_indicator
project_t::get_branch_certs(branch_name const & branch,
                            std::vector<revision<cert> > & certs)
{
  base64<cert_value> branch_encoded;
  encode_base64(cert_value(branch()), branch_encoded);

  return db.get_revision_certs(branch_cert_name, branch_encoded, certs);
}

tag_t::tag_t(revision_id const & ident,
             utf8 const & name,
             rsa_keypair_id const & key)
  : ident(ident), name(name), key(key)
{}

bool
operator < (tag_t const & a, tag_t const & b)
{
  if (a.name < b.name)
    return true;
  else if (a.name == b.name)
    {
      if (a.ident < b.ident)
        return true;
      else if (a.ident == b.ident)
        {
          if (a.key < b.key)
            return true;
        }
    }
  return false;
}

outdated_indicator
project_t::get_tags(set<tag_t> & tags)
{
  std::vector<revision<cert> > certs;
  outdated_indicator i = db.get_revision_certs(tag_cert_name, certs);
  erase_bogus_certs(certs, db);
  tags.clear();
  for (std::vector<revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value value;
      decode_base64(i->inner().value, value);
      tags.insert(tag_t(revision_id(i->inner().ident), utf8(value()), i->inner().key));
    }
  return i;
}

void
project_t::put_tag(key_store & keys,
                   revision_id const & id,
                   string const & name)
{
  cert_revision_tag(id, name, db, keys);
}


void
project_t::put_standard_certs(key_store & keys,
                              revision_id const & id,
                              branch_name const & branch,
                              utf8 const & changelog,
                              date_t const & time,
                              utf8 const & author)
{
  cert_revision_in_branch(id, branch, db, keys);
  cert_revision_changelog(id, changelog, db, keys);
  cert_revision_date_time(id, time, db, keys);
  if (!author().empty())
    cert_revision_author(id, author(), db, keys);
  else
    cert_revision_author_default(id, db, keys);
}

void
project_t::put_standard_certs_from_options(key_store & keys,
                                           revision_id const & id,
                                           branch_name const & branch,
                                           utf8 const & changelog)
{
  put_standard_certs(keys, id,
                     branch,
                     changelog,
                     db.get_opt_date_or_cur_date(),
                     db.get_opt_author());
}
void
project_t::put_cert(key_store & keys,
                    revision_id const & id,
                    cert_name const & name,
                    cert_value const & value)
{
  put_simple_revision_cert(id, name, value, db, keys);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

