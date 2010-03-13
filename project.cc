// Copyright (C) 2007 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "vector.hh"
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>

#include "cert.hh"
#include "database.hh"
#include "file_io.hh"
#include "globish.hh"
//#include "policy.hh"
#include "policies/base_policy.hh"
#include "policies/editable_policy.hh"
#include "policies/policy_branch.hh"
//#include "policies/policy.hh"
#include "project.hh"
#include "revision.hh"
#include "transforms.hh"
#include "lua_hooks.hh"
#include "key_store.hh"
#include "keys.hh"
#include "options.hh"
#include "vocab_cast.hh"
#include "simplestring_xform.hh"
#include "lexical_cast.hh"

using std::make_pair;
using std::multimap;
using std::pair;
using std::set;
using std::string;
using std::vector;
using std::map;
using boost::shared_ptr;
using boost::weak_ptr;

using policies::branch;
using policies::policy;

struct policy_key
{
  weak_ptr<policy> parent;
  string delegation_name;
  string serialized_delegation;
};

bool operator<(policy_key const & l, policy_key const & r)
{
  if (l.parent < r.parent)
    return true;
  else if (r.parent < l.parent)
    return false;

  if (l.delegation_name < r.delegation_name)
    return true;
  else if (r.delegation_name < l.delegation_name)
    return false;

  return l.serialized_delegation < r.serialized_delegation;
}

typedef map<policy_key, shared_ptr<policy> > child_policy_map;

// walk the tree of policies, resolving children if needed
void walk_policies(shared_ptr<policy> root,
                   child_policy_map & children,
                   boost::function<void(shared_ptr<policy>, string,
                                        policies::delegation const &)> fn,
                   string current_prefix = "",
                   policies::delegation del = policies::delegation())
{
  fn(root, current_prefix, del);

  policy::del_map const & d(root->list_delegations());
  for (policy::del_map::const_iterator i = d.begin(); i != d.end(); ++i)
    {
      string child_prefix = current_prefix;
      if (!i->first.empty())
        child_prefix += string(".") + i->first;

      policy_key child_key;
      child_key.parent = root;
      child_key.delegation_name = i->first;
      i->second.serialize(child_key.serialized_delegation);

      child_policy_map::iterator c = children.find(child_key);
      if (c == children.end())
        {
          pair<child_policy_map::iterator, bool> x =
            children.insert(make_pair(child_key, i->second.resolve(root)));
          c = x.first;
        }

      walk_policies(c->second, children, fn, child_prefix, i->second);
    }
}

struct branch_info
{
  branch self;
  shared_ptr<policy> owner;
  branch_info(branch const & b, shared_ptr<policy> o)
    : self(b), owner(o)
  { }
};
class branch_lister
{
  map<branch_name, branch_info> & branches;
public:
  branch_lister(map<branch_name, branch_info> & b) : branches(b) { }
  void operator()(shared_ptr<policy> pol, string prefix,
                  policies::delegation const & del)
  {
    map<string, branch> const & x = pol->list_branches();
    for (map<string, branch>::const_iterator i = x.begin(); i != x.end(); ++i)
      {
        if (prefix.empty())
          branches.insert(make_pair(branch_name(prefix, origin::internal),
                                    branch_info(i->second, pol)));
        else if (i->first.empty())
          branches.insert(make_pair(branch_name(i->first, origin::internal),
                                    branch_info(i->second, pol)));
        else
          branches.insert(make_pair(branch_name(prefix + "." + i->first,
                                                origin::internal),
                                    branch_info(i->second, pol)));
      }
  }
};

class policy_lister
{
  branch_name const & base;
  set<branch_name> & policies;
public:
  policy_lister(branch_name const & b, set<branch_name> & p)
    : base(b), policies(p) { }
  void operator()(shared_ptr<policy> pol, string prefix,
                  policies::delegation const & del)
  {
    branch_name n(prefix, origin::internal);
    if (n.has_prefix(n))
      policies.insert(n);
  }
};

class tag_lister
{
  set<tag_t> & tags;
public:
  tag_lister(set<tag_t> & t) : tags(t) { }
  void operator()(shared_ptr<policy> pol, string prefix,
                  policies::delegation const & del)
  {
    map<string, revision_id> const & x = pol->list_tags();
    for (map<string, revision_id>::const_iterator i = x.begin();
         i != x.end(); ++i)
      {
        string name = prefix;
        if (!name.empty() && !i->first.empty())
          name += ".";
        name += i->first;
        tags.insert(tag_t(i->second,
                          utf8(name, origin::internal),
                          key_id()));
      }
  }
};



// find the policy governing a particular name
class policy_finder
{
  string target;
  policy_chain & info;
public:
  policy_finder(string const & target, policy_chain & info)
    : target(target), info(info)
  {
    info.clear();
  }

  void operator()(shared_ptr<policy> pol, string prefix,
                  policies::delegation const & del)
  {
    if (prefix.empty())
      {
        policy_chain_item i;
        i.policy = pol;
        info.push_back(i);
        return;
      }
    if (target.find(prefix) == 0)
      {
        bool equals = target == prefix;
        bool is_prefix = target.length() > prefix.length() && target[prefix.length()] == '.';
        if (equals || is_prefix)
          {
            policy_chain_item i;
            i.policy = pol;
            i.full_policy_name = prefix;
            i.delegation = del;
            info.push_back(i);
          }
      }
  }
};

class policy_info
{
  shared_ptr<policies::policy> policy;
  child_policy_map child_policies;
public:
  bool passthru;
  explicit policy_info(database & db)
    : policy(),
      passthru(true)
  {
  }
  policy_info(shared_ptr<policies::policy> const & ep, database & db)
    : policy(ep), passthru(false)
  {
  }

  policies::policy const & get_base_policy() const
  {
    I(!passthru);
    return *policy;
  }

  void all_branches(set<branch_name> & branches)
  {
    branches.clear();
    if (!policy)
      return;

    map<branch_name, branch_info> branch_map;
    walk_policies(policy, child_policies, branch_lister(branch_map));
    for (map<branch_name, branch_info>::iterator i = branch_map.begin();
         i != branch_map.end(); ++i)
      {
        branches.insert(i->first);
      }
  }
  void all_branches(set<branch_uid> & branches)
  {
    branches.clear();
    if (!policy)
      return;

    map<branch_name, branch_info> branch_map;
    walk_policies(policy, child_policies, branch_lister(branch_map));
    for (map<branch_name, branch_info>::iterator i = branch_map.begin();
         i != branch_map.end(); ++i)
      {
        branches.insert(i->second.self.get_uid());
      }
  }

  void all_tags(set<tag_t> & tags)
  {
    tags.clear();
    if (!policy)
      return;

    walk_policies(policy, child_policies, tag_lister(tags));
  }

  branch_uid translate_branch(branch_name const & name)
  {
    map<branch_name, branch_info> branch_map;
    walk_policies(policy, child_policies, branch_lister(branch_map));
    map<branch_name, branch_info>::const_iterator i = branch_map.find(name);
    if (i != branch_map.end())
      {
        return i->second.self.get_uid();
      }
    I(false);
  }

  branch_name translate_branch(branch_uid const & uid)
  {
    map<branch_name, branch_info> branch_map;
    walk_policies(policy, child_policies, branch_lister(branch_map));
    for (map<branch_name, branch_info>::iterator i = branch_map.begin();
         i != branch_map.end(); ++i)
      {
        if (i->second.self.get_uid() == uid)
          return i->first;
      }
    I(false);
  }

  void lookup_branch(branch_name const & name,
                     branch_uid & uid, set<key_id> & signers)
  {
    map<branch_name, branch_info> branch_map;
    walk_policies(policy, child_policies, branch_lister(branch_map));
    map<branch_name, branch_info>::const_iterator i = branch_map.find(name);
    if (i != branch_map.end())
      {
        uid = i->second.self.get_uid();
        set<external_key_name> raw_signers = i->second.self.get_signers();
        for (set<external_key_name>::iterator k = raw_signers.begin();
             k != raw_signers.end(); ++k)
          {
            id id;
            if (try_decode_hexenc((*k)(), id))
              signers.insert(key_id(id));
            else
              {
                key_name kn = typecast_vocab<key_name>(*k);
                signers.insert(i->second.owner->get_key_id(kn));
              }
          }
        return ;
      }
    I(false);
  }

  void find_governing_policy(std::string const & of_what,
                             policy_chain & info)
  {
    walk_policies(policy, child_policies,
                  policy_finder(of_what, info));
  }

  void list_policies(branch_name const & base,
                     set<branch_name> & children)
  {
    walk_policies(policy, child_policies,
                  policy_lister(base, children));
  }
};

bool
operator<(key_identity_info const & left,
          key_identity_info const & right)
{
  if (left.id < right.id)
    return true;
  else if (left.id != right.id)
    return false;
  else if (left.official_name < right.official_name)
    return true;
  else if (left.official_name != right.official_name)
    return false;
  else
    return left.given_name < right.given_name;
}
std::ostream &
operator<<(std::ostream & os,
           key_identity_info const & identity)
{
  os<<"{id="<<identity.id<<"; given_name="<<identity.given_name
    <<"; official_name="<<identity.official_name<<"}";
  return os;
}


project_t::project_t(database & db)
  : db(db)
{
  project_policy.reset(new policy_info(db));
}

project_t::project_t(database & db, lua_hooks & lua, options & opts)
  : db(db)
{
  shared_ptr<policies::base_policy> bp(new policies::base_policy(db, opts, lua));
  if (bp->empty())
    project_policy.reset(new policy_info(db));
  else
    project_policy.reset(new policy_info(bp, db));
}

project_t
project_t::empty_project(database & db)
{
  return project_t(db);
}

policies::policy const & project_t::get_base_policy() const
{
  return project_policy->get_base_policy();
}

bool
project_t::policy_exists(branch_name const & name) const
{
  if (project_policy->passthru)
    return name().empty();

  policy_chain info;
  find_governing_policy(name(), info);
  if (info.empty())
    return false;
  return info.back().full_policy_name == name();
}

void
project_t::get_subpolicies(branch_name const & name,
                           std::set<branch_name> & names) const
{
  if (project_policy->passthru)
    return;

  project_policy->list_policies(name, names);
}


void
project_t::get_branch_list(set<branch_name> & names,
                           bool check_heads)
{
  if (!project_policy->passthru)
    {
      project_policy->all_branches(names);
      return;
    }
  if (indicator.outdated())
    {
      vector<string> got;
      indicator = db.get_branches(got);
      branches.clear();
      multimap<revision_id, revision_id> inverse_graph_cache;

      for (vector<string>::iterator i = got.begin();
           i != got.end(); ++i)
        {
          // check that the branch has at least one non-suspended head
          const branch_name branch(*i, origin::database);
          set<revision_id> heads;

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
                           set<branch_name> & names,
                           bool check_heads)
{
  if (!project_policy->passthru)
    {
      set<branch_name> all_names;
      project_policy->all_branches(all_names);

      for (set<branch_name>::const_iterator i = all_names.begin();
           i != all_names.end(); ++i)
        {
          if (glob.matches((*i)()))
            names.insert(*i);
        }
      return;
    }

  vector<string> got;
  db.get_branches(glob, got);
  names.clear();
  multimap<revision_id, revision_id> inverse_graph_cache;

  for (vector<string>::iterator i = got.begin();
       i != got.end(); ++i)
    {
      // check that the branch has at least one non-suspended head
      const branch_name branch(*i, origin::database);
      set<revision_id> heads;

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
          branch_ids.insert(typecast_vocab<branch_uid>(*i));
        }
      return;
    }
  project_policy->all_branches(branch_ids);
}

branch_uid
project_t::translate_branch(branch_name const & name)
{
  if (project_policy->passthru)
    return typecast_vocab<branch_uid>(name);
  else
    return project_policy->translate_branch(name);
}

branch_name
project_t::translate_branch(branch_uid const & uid)
{
  if (project_policy->passthru)
    return typecast_vocab<branch_name>(uid);
  else
    return project_policy->translate_branch(uid);
}

namespace
{
  struct not_in_branch : public is_failure
  {
    project_t & project;
    branch_uid const & branch;
    bool is_managed;
    set<key_id> trusted_signers;
    not_in_branch(project_t & project,
                  branch_uid const & branch)
      : project(project),
        branch(branch),
        is_managed(false)
    {}
    not_in_branch(project_t & project,
                  branch_uid const & branch,
                  set<key_id> const & signers)
      : project(project),
        branch(branch),
        is_managed(true),
        trusted_signers(signers)
    {}
    bool is_trusted(set<key_id> const & signers,
                    id const & rid,
                    cert_name const & name,
                    cert_value const & value)
    {
      for (set<key_id>::const_iterator i = signers.begin();
	   i != signers.end(); ++i)
	{
	  set<key_id>::const_iterator t = trusted_signers.find(*i);
	  if (t != trusted_signers.end())
	    return true;
	}
      return false;
    }
    virtual bool operator()(revision_id const & rid)
    {
      vector<cert> certs;
      project.db.get_revision_certs(rid,
                                    cert_name(branch_cert_name),
                                    typecast_vocab<cert_value>(branch),
                                    certs);
      if (is_managed)
        project.db.erase_bogus_certs(certs,
                                     bind(&not_in_branch::is_trusted,
                                          this, _1, _2, _3, _4));
      else
        project.db.erase_bogus_certs(project, certs);
      return certs.empty();
    }
  };

  struct suspended_in_branch : public is_failure
  {
    project_t & project;
    branch_uid const & branch;
    bool is_managed;
    set<key_id> trusted_signers;
    suspended_in_branch(project_t & project,
                        branch_uid const & branch)
      : project(project), branch(branch), is_managed(false)
    {}
    suspended_in_branch(project_t & project,
                        branch_uid const & branch,
                        set<key_id> const & signers)
      : project(project),
        branch(branch),
        is_managed(true),
        trusted_signers(signers)
    {}
    bool is_trusted(set<key_id> const & signers,
                    id const & rid,
                    cert_name const & name,
                    cert_value const & value)
    {
      for (set<key_id>::const_iterator i = signers.begin();
	   i != signers.end(); ++i)
	{
	  set<key_id>::const_iterator t = trusted_signers.find(*i);
	  if (t != trusted_signers.end())
	    return true;
	}
      return false;
    }
    virtual bool operator()(revision_id const & rid)
    {
      vector<cert> certs;
      project.db.get_revision_certs(rid,
                                    cert_name(suspend_cert_name),
                                    typecast_vocab<cert_value>(branch),
                                    certs);
      if (is_managed)
        project.db.erase_bogus_certs(certs,
                                     bind(&suspended_in_branch::is_trusted,
                                          this, _1, _2, _3, _4));
      else
        project.db.erase_bogus_certs(project, certs);
      return !certs.empty();
    }
  };
}

void
project_t::get_branch_heads(branch_name const & name,
                            set<revision_id> & heads,
                            bool ignore_suspend_certs,
                            multimap<revision_id, revision_id> * inverse_graph_cache_ptr)
{
  pair<branch_name, suspended_indicator>
    cache_index(name, ignore_suspend_certs);
  pair<outdated_indicator, set<revision_id> > &
    branch = branch_heads[cache_index];

  if (branch.first.outdated())
    {
      L(FL("getting heads of branch %s") % name);

      branch_uid uid;
      set<key_id> signers;
      if (project_policy->passthru)
        uid = typecast_vocab<branch_uid>(name);
      else
        {
          project_policy->lookup_branch(name, uid, signers);
        }

      branch.first = db.get_revisions_with_cert(cert_name(branch_cert_name),
                                                typecast_vocab<cert_value>(uid),
                                                branch.second);

      shared_ptr<not_in_branch> p;
      if (project_policy->passthru)
        p.reset(new not_in_branch(*this, uid));
      else
        p.reset(new not_in_branch(*this, uid, signers));

      erase_ancestors_and_failures(db, branch.second, *p,
                                   inverse_graph_cache_ptr);

      if (!ignore_suspend_certs)
        {
          shared_ptr<suspended_in_branch> s;
          if (project_policy->passthru)
            s.reset(new suspended_in_branch(*this, uid));
          else
            s.reset(new suspended_in_branch(*this, uid, signers));
          set<revision_id>::iterator it = branch.second.begin();
          while (it != branch.second.end())
            {
              if ((*s)(*it))
                branch.second.erase(it++);
              else
                it++;
            }
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
      branch_uid bid = typecast_vocab<branch_uid>(branch);
      vector<cert> certs;
      db.get_revision_certs(id, branch_cert_name,
                            typecast_vocab<cert_value>(bid), certs);

      int num = certs.size();

      db.erase_bogus_certs(*this, certs);

      L(FL("found %d (%d valid) %s branch certs on revision %s")
        % num
        % certs.size()
        % branch
        % id);

      return !certs.empty();
    }
  else
    {
      branch_uid uid;
      set<key_id> signers;
      project_policy->lookup_branch(branch, uid, signers);

      not_in_branch p(*this, uid, signers);
      return !p(id);
    }
}

void
project_t::put_revision_in_branch(key_store & keys,
                                  revision_id const & id,
                                  branch_name const & branch)
{
  branch_uid bid;
  if (project_policy->passthru)
    bid = typecast_vocab<branch_uid>(branch);
  else
    bid = translate_branch(branch);
  put_cert(keys, id, branch_cert_name, typecast_vocab<cert_value>(bid));
}

bool
project_t::revision_is_suspended_in_branch(revision_id const & id,
                                 branch_name const & branch)
{
  branch_uid bid;
  if (project_policy->passthru)
    bid = typecast_vocab<branch_uid>(branch);
  else
    bid = translate_branch(branch);
  vector<cert> certs;
  db.get_revision_certs(id, suspend_cert_name,
                        typecast_vocab<cert_value>(branch), certs);

  int num = certs.size();

  db.erase_bogus_certs(*this, certs);

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
    bid = typecast_vocab<branch_uid>(branch);
  else
    bid = translate_branch(branch);
  put_cert(keys, id, suspend_cert_name, typecast_vocab<cert_value>(bid));
}


outdated_indicator
project_t::get_revision_cert_hashes(revision_id const & rid,
                                    vector<id> & hashes)
{
  return db.get_revision_certs(rid, hashes);
}

outdated_indicator
project_t::get_revision_certs(revision_id const & id,
                              vector<cert> & certs)
{
  return db.get_revision_certs(id, certs);
}

outdated_indicator
project_t::get_revision_certs_by_name(revision_id const & id,
                                      cert_name const & name,
                                      vector<cert> & certs)
{
  outdated_indicator i = db.get_revision_certs(id, name, certs);
  db.erase_bogus_certs(*this, certs);
  return i;
}

outdated_indicator
project_t::get_revision_branches(revision_id const & id,
                                 set<branch_name> & branches)
{
  vector<cert> certs;
  outdated_indicator i = get_revision_certs_by_name(id, branch_cert_name, certs);
  branches.clear();
  for (vector<cert>::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      if (project_policy->passthru)
        branches.insert(typecast_vocab<branch_name>(i->value));
      else
        {
          std::set<branch_uid> branchids;
          get_branch_list(branchids);
          branch_uid bid = typecast_vocab<branch_uid>(i->value);
          if (branchids.find(bid) != branchids.end())
            branches.insert(translate_branch(bid));
        }
    }
  return i;
}


outdated_indicator
project_t::get_branch_certs(branch_name const & branch,
                            vector<pair<id, cert> > & certs)
{
  branch_uid bid;
  if (project_policy->passthru)
    bid = typecast_vocab<branch_uid>(branch);
  else
    bid = translate_branch(branch);

  return db.get_revision_certs(branch_cert_name,
                               typecast_vocab<cert_value>(bid), certs);
}

tag_t::tag_t(revision_id const & ident,
             utf8 const & name,
             key_id const & key)
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
  if (project_policy->passthru)
    {
      std::vector<cert> certs;
      outdated_indicator i = db.get_revision_certs(tag_cert_name, certs);
      db.erase_bogus_certs(*this, certs);
      tags.clear();
      for (std::vector<cert>::const_iterator i = certs.begin();
           i != certs.end(); ++i)
        tags.insert(tag_t(revision_id(i->ident),
                      typecast_vocab<utf8>(i->value),
                      i->key));

      return i;
    }
  else
    {
      project_policy->all_tags(tags);
      return outdated_indicator();
    }
}

void
project_t::find_governing_policy(string const & of_what,
                                 policy_chain & info) const
{
  I(!project_policy->passthru);
  project_policy->find_governing_policy(of_what, info);
}

void
project_t::put_tag(key_store & keys,
                   revision_id const & id,
                   string const & name)
{
  if (project_policy->passthru)
    put_cert(keys, id, tag_cert_name, cert_value(name, origin::user));
  else
    {
      policy_chain info;
      project_policy->find_governing_policy(name, info);
      E(!info.empty(), origin::user,
        F("Cannot find policy for tag '%s'") % name);
      E(info.back().delegation.is_branch_type(), origin::user,
        F("Cannot edit '%s', it is delegated to a specific revision") % name);
      policies::policy_branch br(info.back().delegation.get_branch_spec());

      I(br.begin() != br.end());
      policies::editable_policy ep(**br.begin());

      ep.set_tag(name.substr(info.back().full_policy_name.size() + 1), id);

      br.commit(ep, utf8((F("Set tag %s") % name).str(),
                         origin::internal));
    }
}



void
project_t::put_standard_certs(key_store & keys,
                              revision_id const & id,
                              branch_name const & branch,
                              utf8 const & changelog,
                              date_t const & time,
                              string const & author)
{
  branch_uid uid;
  if (project_policy->passthru)
    uid = typecast_vocab<branch_uid>(branch);
  else
    uid = translate_branch(branch);
  put_standard_certs(keys, id, uid, changelog, time, author);
}

void
project_t::put_standard_certs(key_store & keys,
                              revision_id const & id,
                              branch_uid const & branch,
                              utf8 const & changelog,
                              date_t const & time,
                              string const & author)
{
  I(!branch().empty());
  I(!changelog().empty());
  I(time.valid());
  I(!author.empty());

  put_cert(keys, id, branch_cert_name,
           typecast_vocab<cert_value>(branch));
  put_cert(keys, id, changelog_cert_name,
           typecast_vocab<cert_value>(changelog));
  put_cert(keys, id, date_cert_name,
           cert_value(time.as_iso_8601_extended(), origin::internal));
  put_cert(keys, id, author_cert_name,
           cert_value(author, origin::user));
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
      key_identity_info key;
      get_user_key(opts, lua, db, keys, *this, key.id);
      complete_key_identity(lua, key);

      if (!lua.hook_get_author(branch, key, author))
        {
          author = key.official_name();
        }
    }

  put_standard_certs(keys, id, branch, changelog, date, author);
}

bool
project_t::put_cert(key_store & keys,
                    revision_id const & id,
                    cert_name const & name,
                    cert_value const & value)
{
  I(keys.have_signing_key());

  cert t(id, name, value, keys.signing_key);
  string signed_text;
  t.signable_text(signed_text);
  load_key_pair(keys, t.key);
  keys.make_signature(db, t.key, signed_text, t.sig);

  cert cc(t);
  return db.put_revision_cert(cc);
}

void
project_t::put_revision_comment(key_store & keys,
                                revision_id const & id,
                                utf8 const & comment)
{
  put_cert(keys, id, comment_cert_name, typecast_vocab<cert_value>(comment));
}

void
project_t::put_revision_testresult(key_store & keys,
                                   revision_id const & id,
                                   string const & results)
{
  bool passed;
  if (lowercase(results) == "true" ||
      lowercase(results) == "yes" ||
      lowercase(results) == "pass" ||
      results == "1")
    passed = true;
  else if (lowercase(results) == "false" ||
           lowercase(results) == "no" ||
           lowercase(results) == "fail" ||
           results == "0")
    passed = false;
  else
    E(false, origin::user,
      F("could not interpret test result string '%s'; "
        "valid strings are: 1, 0, yes, no, true, false, pass, fail")
      % results);

  put_cert(keys, id, testresult_cert_name,
           cert_value(boost::lexical_cast<string>(passed), origin::internal));
}

void
project_t::lookup_key_by_name(key_store * const keys,
                              lua_hooks & lua,
                              key_name const & name,
                              key_id & id)
{
  set<key_id> ks_match_by_local_name;
  set<key_id> db_match_by_local_name;
  set<key_id> ks_match_by_given_name;

  if (keys)
    {
      vector<key_id> storekeys;
      keys->get_key_ids(storekeys);
      for (vector<key_id>::const_iterator i = storekeys.begin();
           i != storekeys.end(); ++i)
        {
          key_name i_name;
          keypair kp;
          keys->get_key_pair(*i, i_name, kp);

          if (i_name == name)
            ks_match_by_given_name.insert(*i);

          key_identity_info identity;
          identity.id = *i;
          identity.given_name = i_name;
          if (lua.hook_get_local_key_name(identity))
            {
              if (identity.official_name == name)
                ks_match_by_local_name.insert(*i);
            }
        }
    }
  if (db.database_specified())
    {
      vector<key_id> dbkeys;
      db.get_key_ids(dbkeys);
      for (vector<key_id>::const_iterator i = dbkeys.begin();
           i != dbkeys.end(); ++i)
        {
          key_name i_name;
          rsa_pub_key pub;
          db.get_pubkey(*i, i_name, pub);

          key_identity_info identity;
          identity.id = *i;
          identity.given_name = i_name;
          if (lua.hook_get_local_key_name(identity))
            {
              if (identity.official_name == name)
                db_match_by_local_name.insert(*i);
            }
        }
    }

  E(ks_match_by_local_name.size() < 2, origin::user,
    F("you have %d keys named '%s'") %
    ks_match_by_local_name.size() % name);
  if (ks_match_by_local_name.size() == 1)
    {
      id = *ks_match_by_local_name.begin();
      return;
    }
  E(db_match_by_local_name.size() < 2, origin::user,
    F("there are %d keys named '%s'") %
    db_match_by_local_name.size() % name);
  if (db_match_by_local_name.size() == 1)
    {
      id = *db_match_by_local_name.begin();
      return;
    }
  E(ks_match_by_given_name.size() < 2, origin::user,
    F("you have %d keys named '%s'") %
    ks_match_by_local_name.size() % name);
  if (ks_match_by_given_name.size() == 1)
    {
      id = *ks_match_by_given_name.begin();
      return;
    }
  E(false, origin::user,
    F("there is no key named '%s'") % name);
}

void
project_t::get_canonical_name_of_key(key_store * const keys,
                                     key_id const & id,
                                     key_name & name)
{
  if (keys && keys->key_pair_exists(id))
    {
      keypair kp;
      keys->get_key_pair(id, name, kp);
    }
  else if (db.database_specified() && db.public_key_exists(id))
    {
      rsa_pub_key pub;
      db.get_pubkey(id, name, pub);
    }
  else
    {
      E(false, origin::internal,
        F("key %s does not exist") % id);
    }
}

void
project_t::complete_key_identity(key_store * const keys,
                                 lua_hooks & lua,
                                 key_identity_info & info)
{
  MM(info.id);
  MM(info.official_name);
  MM(info.given_name);
  if (!info.id.inner()().empty())
    {
      get_canonical_name_of_key(keys, info.id, info.given_name);
      lua.hook_get_local_key_name(info);
    }
  else if (!info.official_name().empty())
    {
      lookup_key_by_name(keys, lua, info.official_name, info.id);
      get_canonical_name_of_key(keys, info.id, info.given_name);
    }
  //else if (!info.given_name().empty())
  //  {
  //    lookup_key_by_name(keys, info.given_name, info.id);
  //    get_name_of_key(keys, info.id, info.official_name);
  //  }
  else
    I(false);
}

void
project_t::complete_key_identity(key_store & keys,
                                 lua_hooks & lua,
                                 key_identity_info & info)
{
  complete_key_identity(&keys, lua, info);
}

void
project_t::complete_key_identity(lua_hooks & lua,
                                 key_identity_info & info)
{
  complete_key_identity(0, lua, info);
}

void
project_t::get_key_identity(key_store * const keys,
                            lua_hooks & lua,
                            external_key_name const & input,
                            key_identity_info & output)
{
  try
    {
      string in2 = decode_hexenc(input(), origin::no_fault);
      id ident(in2, origin::no_fault);
      // set this separately so we can ensure that the key_id() calls
      // above throw recoverable_failure instead of unrecoverable_failure
      ident.made_from = input.made_from;
      output.id = key_id(ident);
    }
  catch (recoverable_failure &)
    {
      output.official_name = typecast_vocab<key_name>(input);
    }
  complete_key_identity(keys, lua, output);
}

void
project_t::get_key_identity(key_store & keys,
                            lua_hooks & lua,
                            external_key_name const & input,
                            key_identity_info & output)
{
  get_key_identity(&keys, lua, input, output);
}

void
project_t::get_key_identity(lua_hooks & lua,
                            external_key_name const & input,
                            key_identity_info & output)
{
  get_key_identity(0, lua, input, output);
}

void
project_t::get_key_identity(key_store & keys,
                            lua_hooks & lua,
                            arg_type const & input,
                            key_identity_info & output)
{
  get_key_identity(&keys, lua, typecast_vocab<external_key_name>(input), output);
}

void
project_t::get_key_identity(lua_hooks & lua,
                            arg_type const & input,
                            key_identity_info & output)
{
  get_key_identity(0, lua, typecast_vocab<external_key_name>(input), output);
}


// These should maybe be converted to member functions.

string
describe_revision(project_t & project, revision_id const & id)
{
  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);

  string description;

  description += encode_hexenc(id.inner()(), id.inner().made_from);

  // append authors and date of this revision
  vector<cert> certs;
  project.get_revision_certs(id, certs);
  string authors;
  string dates;
  for (vector<cert>::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      if (i->name == author_cert_name)
        {
          authors += " ";
          authors += i->value();
        }
      else if (i->name == date_cert_name)
        {
          dates += " ";
          dates += i->value();
        }
    }

  description += authors;
  description += dates;
  return description;
}

void
notify_if_multiple_heads(project_t & project,
                         branch_name const & branchname,
                         bool ignore_suspend_certs)
{
  set<revision_id> heads;
  project.get_branch_heads(branchname, heads, ignore_suspend_certs);
  if (heads.size() > 1) {
    string prefixedline;
    prefix_lines_with(_("note: "),
                      _("branch '%s' has multiple heads\n"
                        "perhaps consider '%s merge'"),
                      prefixedline);
    P(i18n_format(prefixedline) % branchname % prog_name);
  }
}

// Guess which branch is appropriate for a commit below IDENT.
// OPTS may override.  Branch name is returned in BRANCHNAME.
// Does not modify branch state in OPTS.
void
guess_branch(options & opts, project_t & project,
             revision_id const & ident, branch_name & branchname)
{
  if (opts.branch_given && !opts.branch().empty())
    branchname = opts.branch;
  else
    {
      E(!ident.inner()().empty(), origin::user,
        F("no branch found for empty revision, "
          "please provide a branch name"));

      set<branch_name> branches;
      project.get_revision_branches(ident, branches);

      E(!branches.empty(), origin::user,
        F("no branch certs found for revision %s, "
          "please provide a branch name") % ident);

      E(branches.size() == 1, origin::user,
        F("multiple branch certs found for revision %s, "
          "please provide a branch name") % ident);

      set<branch_name>::iterator i = branches.begin();
      I(i != branches.end());
      branchname = *i;
    }
}

// As above, but set the branch name in the options
// if it wasn't already set.
void
guess_branch(options & opts, project_t & project, revision_id const & ident)
{
  branch_name branchname;
  guess_branch(opts, project, ident, branchname);
  opts.branch = branchname;
}
// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
