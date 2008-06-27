// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#include "base.hh"
#include "vector.hh"
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

#include "cert.hh"
#include "database.hh"
#include "file_io.hh"
#include "globish.hh"
#include "policy.hh"
#include "project.hh"
#include "revision.hh"
#include "transforms.hh"
#include "lua_hooks.hh"
#include "keys.hh"
#include "options.hh"

using std::string;
using std::set;
using std::vector;
using std::map;
using std::multimap;
using std::make_pair;
using boost::shared_ptr;


class policy_info
{
public:
  policy_branch policy;
  bool passthru;
  policy_info(data const & spec,
              branch_prefix const & prefix,
              database & db)
    : policy(spec, prefix, db), passthru(false)
  {
  }
  policy_info(revision_id const & rev,
              branch_prefix const & prefix,
              database & db)
    : policy(rev, prefix, db), passthru(false)
  {
  }
  explicit policy_info(database & db)
    : policy(policy_branch::empty_policy(db)), passthru(true)
  {
  }
};

project_t::project_t(branch_prefix const & project_name,
                     data const & project_spec,
                     database & db)
  : db(db), project_policy(new policy_info(project_spec, project_name, db))
{}

project_t::project_t(branch_prefix const & project_name,
                     revision_id const & policy_rev,
                     database & db)
  : db(db), project_policy(new policy_info(policy_rev, project_name, db))
{}

project_t::project_t(database & db)
  : db(db), project_policy(new policy_info(db))
{}

bool
project_t::get_policy_branch_policy_of(branch_name const & name,
                                       branch_policy & policy_branch_policy,
                                       branch_prefix & policy_prefix)
{
  std::string acc;
  return project_policy->policy.get_nearest_policy(name,
                                                   policy_branch_policy,
                                                   policy_prefix,
                                                   acc);
}

void
project_t::get_branch_list(std::set<branch_name> & names,
                           bool check_heads)
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

          if (check_heads)
            get_branch_heads(branch, heads, false, &inverse_graph_cache);

          if (!check_heads || !heads.empty())
            branches.insert(branch);
        }
    }

  names = branches;
}

void
project_t::get_branch_list(globish const & glob,
                           std::set<branch_name> & names,
                           bool check_heads)
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

      if (check_heads)
        get_branch_heads(branch, heads, false, &inverse_graph_cache);

      if (!check_heads || !heads.empty())
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
    branch_name const & branch;
    not_in_branch(database & db,
                  branch_name const & branch)
      : db(db), branch(branch)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector< revision<cert> > certs;
      db.get_revision_certs(rid,
                            cert_name(branch_cert_name),
                            cert_value(branch()),
                            certs);
      erase_bogus_certs(db, certs);
      return certs.empty();
    }
  };

  struct suspended_in_branch : public is_failure
  {
    database & db;
    branch_name const & branch;
    suspended_in_branch(database & db,
                        branch_name const & branch)
      : db(db), branch(branch)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector< revision<cert> > certs;
      db.get_revision_certs(rid,
                            cert_name(suspend_cert_name),
                            cert_value(branch()),
                            certs);
      erase_bogus_certs(db, certs);
      return !certs.empty();
    }
  };
}

void
project_t::get_branch_heads(branch_name const & name,
                            std::set<revision_id> & heads,
                            bool ignore_suspend_certs,
                            multimap<revision_id, revision_id> * inverse_graph_cache_ptr)
{
  std::pair<branch_name, suspended_indicator>
    cache_index(name, ignore_suspend_certs);
  std::pair<outdated_indicator, std::set<revision_id> > &
    branch = branch_heads[cache_index];

  if (branch.first.outdated())
    {
      L(FL("getting heads of branch %s") % name);

      if (project_policy->passthru)
        {

          branch.first = db.get_revisions_with_cert(cert_name(branch_cert_name),
                                                cert_value(name()),
                                                branch.second);

          not_in_branch p(db, name);
          erase_ancestors_and_failures(db, branch.second, p,
                                       inverse_graph_cache_ptr);

          if (!ignore_suspend_certs)
            {
              suspended_in_branch s(db, name);
              std::set<revision_id>::iterator it = branch.second.begin();
              while (it != branch.second.end())
                {
                  if (s(*it))
                    branch.second.erase(it++);
                  else
                    it++;
                }
            }
        }
      else
        {
          shared_ptr<branch_policy> bp;
          bp  = project_policy->policy.maybe_get_branch_policy(name);
          E(bp, F("Cannot find policy for branch %s.") % name);

          branch.first = ::get_branch_heads(*bp, ignore_suspend_certs, db,
                                            branch.second,
                                            inverse_graph_cache_ptr);
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
  if (project_policy->passthru)
    {
      branch_uid bid = branch_uid(branch());
      vector<revision<cert> > certs;
      db.get_revision_certs(id, branch_cert_name, cert_value(bid()), certs);

      int num = certs.size();

      erase_bogus_certs(db, certs);

      L(FL("found %d (%d valid) %s branch certs on revision %s")
        % num
        % certs.size()
        % branch
        % id);

      return !certs.empty();
    }
  else
    {
      shared_ptr<branch_policy> bp;
      bp  = project_policy->policy.maybe_get_branch_policy(branch);
      E(bp, F("Cannot find policy for branch %s.") % branch);
      return ::revision_is_in_branch(*bp, id, db);
    }
}

void
project_t::put_revision_in_branch(key_store & keys,
                                  revision_id const & id,
                                  branch_name const & branch)
{
  branch_uid bid;
  if (project_policy->passthru)
    bid = branch_uid(branch());
  else
    bid = translate_branch(branch);
  cert_revision_in_branch(db, keys, id, bid);
}

bool
project_t::revision_is_suspended_in_branch(revision_id const & id,
                                 branch_name const & branch)
{
  branch_uid bid;
  if (project_policy->passthru)
    bid = branch_uid(branch());
  else
    bid = translate_branch(branch);
  vector<revision<cert> > certs;
  db.get_revision_certs(id, suspend_cert_name, cert_value(branch()), certs);

  int num = certs.size();

  erase_bogus_certs(db, certs);

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
  branch_uid bid;
  if (project_policy->passthru)
    bid = branch_uid(branch());
  else
    bid = translate_branch(branch);
  cert_revision_suspended_in_branch(db, keys, id, bid);
}


outdated_indicator
project_t::get_revision_cert_hashes(revision_id const & rid,
                                    std::vector<id> & hashes)
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
  erase_bogus_certs(db, certs);
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
      if (project_policy->passthru)
        branches.insert(branch_name(i->inner().value()));
      else
        {
          std::set<branch_uid> branchids;
          get_branch_list(branchids);
          if (branchids.find(branch_uid(i->inner().value())) != branchids.end())
            branches.insert(translate_branch(branch_uid(i->inner().value())));
        }
    }
  return i;
}


outdated_indicator
project_t::get_branch_certs(branch_name const & branch,
                            std::vector<revision<cert> > & certs)
{
  branch_uid bid;
  if (project_policy->passthru)
    bid = branch_uid(branch());
  else
    bid = translate_branch(branch);

  return db.get_revision_certs(branch_cert_name, cert_value(branch()), certs);
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
  erase_bogus_certs(db, certs);
  tags.clear();
  for (std::vector<revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    tags.insert(tag_t(revision_id(i->inner().ident),
                      utf8(i->inner().value()), i->inner().key));

  return i;
}

void
project_t::put_tag(key_store & keys,
                   revision_id const & id,
                   string const & name)
{
  cert_revision_tag(db, keys, id, name);
}


void
project_t::put_standard_certs(key_store & keys,
                              revision_id const & id,
                              branch_name const & branch,
                              utf8 const & changelog,
                              date_t const & time,
                              string const & author)
{
  I(!branch().empty());
  I(!changelog().empty());
  I(time.valid());
  I(!author.empty());

  branch_uid bid;
  if (project_policy->passthru)
    bid = branch_uid(branch());
  else
    bid = translate_branch(branch);
  cert_revision_in_branch(db, keys, id, bid);
  cert_revision_changelog(db, keys, id, changelog);
  cert_revision_date_time(db, keys, id, time);
  cert_revision_author(db, keys, id, author);
}

void
project_t::put_standard_certs_from_options(options const & opts,
                                           lua_hooks & lua,
                                           key_store & keys,
                                           revision_id const & id,
                                           branch_name const & branch,
                                           utf8 const & changelog)
{
  date_t date;
  if (opts.date_given)
    date = opts.date;
  else
    date = date_t::now();

  string author = opts.author();
  if (author.empty())
    {
      rsa_keypair_id key;
      get_user_key(opts, lua, db, keys, key);

      if (!lua.hook_get_author(branch, key, author))
        author = key();
    }

  put_standard_certs(keys, id, branch, changelog, date, author);
}

void
project_t::put_cert(key_store & keys,
                    revision_id const & id,
                    cert_name const & name,
                    cert_value const & value)
{
  put_simple_revision_cert(db, keys, id, name, value);
}

////////////////////////////////////////////////////////////////////////

project_set::project_set(database & db,
                         lua_hooks & lua,
                         options & opts)
  : db(db)
{
  map<string, data> project_definitions;
  lua.hook_get_projects(project_definitions);
  for (map<string, data>::const_iterator i = project_definitions.begin();
       i != project_definitions.end(); ++i)
    {
      if (opts.policy_revisions.find(branch_prefix(i->first))
          == opts.policy_revisions.end())
        {
          projects.insert(make_pair(branch_prefix(i->first),
                                    project_t(branch_prefix(i->first),
                                              i->second,
                                              db)));
        }
    }
  for (map<branch_prefix, hexenc<id> >::const_iterator
         i = opts.policy_revisions.begin();
       i != opts.policy_revisions.end(); ++i)
    {
      id rid;
      decode_hexenc(i->second, rid);
      projects.insert(make_pair(i->first,
                                project_t(i->first,
                                          revision_id(rid),
                                          db)));
    }
  if (projects.empty())
    {
      projects.insert(std::make_pair("", project_t(db)));
    }
}

project_t &
project_set::get_project(branch_prefix const & name)
{
  project_t * const project = maybe_get_project(name);
  I(project != NULL);
  return *project;
}

project_t *
project_set::maybe_get_project(branch_prefix const & name)
{
  map<branch_prefix, project_t>::iterator i = projects.find(name);
  if (i != projects.end())
    return &i->second;
  else
    return NULL;
}

project_t &
project_set::get_project_of_branch(branch_name const & branch)
{
  MM(branch);
  project_t * const project = maybe_get_project_of_branch(branch);
  I(project != NULL);
  return *project;
}

project_t *
project_set::maybe_get_project_of_branch(branch_name const & branch)
{
  for (map<branch_prefix, project_t>::iterator i = projects.begin();
       i != projects.end(); ++i)
    {
      if (i->first() == "")
        return &i->second;
      std::string pre = i->first() + ".";
      if (branch().substr(0, pre.size()) == pre)
        return &i->second;
    }
  return NULL;
}

void
project_set::get_branch_list(std::set<branch_name> & names,
                             bool check_heads)
{
  names.clear();
  for (project_map::iterator i = projects.begin();
       i != projects.end(); ++i)
    {
      std::set<branch_name> some_names;
      i->second.get_branch_list(some_names, check_heads);
      std::copy(some_names.begin(), some_names.end(),
                std::inserter(names, names.end()));
    }
}

void
project_set::get_branch_list(globish const & glob,
                             std::set<branch_name> & names,
                             bool check_heads)
{
  names.clear();
  for (project_map::iterator i = projects.begin();
       i != projects.end(); ++i)
    {
      std::set<branch_name> some_names;
      i->second.get_branch_list(glob, some_names, check_heads);
      std::copy(some_names.begin(), some_names.end(),
                std::inserter(names, names.end()));
    }
}

void
project_set::get_branch_uids(std::set<branch_uid> & uids)
{
  uids.clear();
  for (project_map::iterator i = projects.begin();
       i != projects.end(); ++i)
    {
      std::set<branch_uid> some_uids;
      i->second.get_branch_list(some_uids);
      std::copy(some_uids.begin(), some_uids.end(),
                std::inserter(uids, uids.end()));
    }
}

branch_uid
project_set::translate_branch(branch_name const & branch)
{
  return get_project_of_branch(branch).translate_branch(branch);
}

branch_name
project_set::translate_branch(branch_uid const & branch)
{
  for (project_map::iterator i = projects.begin();
       i != projects.end(); ++i)
    {
      std::set<branch_uid> uids;
      i->second.get_branch_list(uids);
      if (uids.find(branch) != uids.end())
        {
          return i->second.translate_branch(branch);
        }
    }
  E(false, F("Cannot find a name for the branch with uid '%s'") % branch);
}

outdated_indicator
project_set::get_tags(std::set<tag_t> & tags)
{
  I(!projects.empty());
  return projects.begin()->second.get_tags(tags);
}

void
project_set::put_tag(key_store & keys,
                     revision_id const & id,
                     string const & name)
{
  cert_revision_tag(db, keys, id, name);
}

outdated_indicator
project_set::get_revision_branches(revision_id const & id,
                                   std::set<branch_name> & branches)
{
  std::vector<revision<cert> > certs;
  outdated_indicator i = get_revision_certs_by_name(id, branch_cert_name, certs);
  branches.clear();
  for (std::vector<revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      branch_uid uid(i->inner().value());

      for (project_map::iterator i = projects.begin();
           i != projects.end(); ++i)
        {
          std::set<branch_uid> branchids;
          i->second.get_branch_list(branchids);
          if (branchids.find(uid) != branchids.end())
            {
              branches.insert(i->second.translate_branch(uid));
              break;
            }
        }
    }
  return i;
}

outdated_indicator
project_set::get_revision_certs_by_name(revision_id const & id,
                                        cert_name const & name,
                                        std::vector<revision<cert> > & certs)
{
  outdated_indicator i = db.get_revision_certs(id, name, certs);
  erase_bogus_certs(db, certs);
  return i;
}

outdated_indicator
project_set::get_revision_certs(revision_id const & id,
                                std::vector<revision<cert> > & certs)
{
  return db.get_revision_certs(id, certs);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

