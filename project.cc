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

branch_heads_key::branch_heads_key(branch_uid const & uid,
                                   bool ignore_suspends,
                                   std::set<key_id> const & keys,
                                   bool have_signers)
  : uid(uid),
    ignore_suspends(ignore_suspends),
    keys(keys),
    have_signers(have_signers)
{ }
bool operator<(branch_heads_key const & a,
               branch_heads_key const & b)
{
  if (a.uid < b.uid)
    return true;
  else if (b.uid < a.uid)
    return false;
  else if (a.ignore_suspends < b.ignore_suspends)
    return true;
  else if (b.ignore_suspends < a.ignore_suspends)
    return false;
  else if (a.have_signers < b.have_signers)
    return true;
  else if (b.have_signers < a.have_signers)
    return false;
  else
    return a.keys < b.keys;
}

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

typedef boost::function<void(shared_ptr<policy>, branch_name const &,
                             policies::delegation const &)> policy_walker;

// walk the tree of policies, resolving children if needed
void walk_policies_internal(project_t const & project,
                            shared_ptr<policy> root,
                            child_policy_map & children,
                            policy_walker fn,
                            branch_name current_prefix,
                            policies::delegation del)
{
  L(FL("Walking policies; root = %d, current prefix = '%s'")
    % (bool)root % current_prefix);

  fn(root, current_prefix, del);

  if (!root)
    return;

  policy::del_map const & d(root->list_delegations());
  for (policy::del_map::const_iterator i = d.begin(); i != d.end(); ++i)
    {
      branch_name child_prefix = current_prefix;
      if (!i->first.empty())
        child_prefix.append(branch_name(i->first, origin::internal));

      policy_key child_key;
      child_key.parent = root;
      child_key.delegation_name = i->first;
      i->second.serialize(child_key.serialized_delegation);

      child_policy_map::iterator c = children.find(child_key);
      if (c == children.end())
        {
          L(FL("loaded new policy '%s'") % child_prefix);
          pair<child_policy_map::iterator, bool> x =
            children.insert(make_pair(child_key,
                                      i->second.resolve(project, root)));
          c = x.first;
        }
      else if (!c->second || c->second->outdated())
        {
          if (c->second)
            L(FL("policy '%s' did not resolve; reloading") % child_prefix);
          else
            L(FL("policy '%s' marked outdated; reloading (will reload all children)")
              % child_prefix);
          c->second = i->second.resolve(project, root);
        }

      walk_policies_internal(project, c->second, children, fn, child_prefix, i->second);
    }
  L(FL("Done walking under prefix '%s'")
    % current_prefix);
}
void walk_policies(project_t const & project,
                   shared_ptr<policy> root_policy,
                   child_policy_map & children,
                   policy_walker fn)
{
  if (root_policy->outdated())
    {
      L(FL("root policy is marked outdated; reloading (will reload all children)..."));
      shared_ptr<policies::base_policy> bp
        = boost::dynamic_pointer_cast<policies::base_policy>(root_policy);
      bp->reload();
    }

  walk_policies_internal(project, root_policy, children, fn,
                         branch_name(), policies::delegation());

  set<policy_key> dead_policies;
  for (child_policy_map::const_iterator i = children.begin();
       i != children.end(); ++i)
    {
      policies::policy_ptr p = i->first.parent.lock();
      if (!p || p->outdated())
        dead_policies.insert(i->first);
    }
  for (set<policy_key>::const_iterator i = dead_policies.begin();
       i != dead_policies.end(); ++i)
    {
      children.erase(*i);
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
  void operator()(shared_ptr<policy> pol, branch_name const & prefix,
                  policies::delegation const & del)
  {
    if (!pol)
      return;

    map<string, branch> const & x = pol->list_branches();
    for (map<string, branch>::const_iterator i = x.begin(); i != x.end(); ++i)
      {
        if (i->first == "__self__")
          branches.insert(make_pair(prefix,
                                    branch_info(i->second, pol)));
        else
          branches.insert(make_pair(prefix / branch_name(i->first,
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
  void operator()(shared_ptr<policy> pol, branch_name const &prefix,
                  policies::delegation const & del)
  {
    if (prefix.has_prefix(base))
      policies.insert(prefix);
  }
};

class tag_lister
{
  set<tag_t> & tags;
public:
  tag_lister(set<tag_t> & t) : tags(t) { }
  void operator()(shared_ptr<policy> pol, branch_name const & prefix,
                  policies::delegation const & del)
  {
    if (!pol)
      return;

    map<string, revision_id> const & x = pol->list_tags();
    for (map<string, revision_id>::const_iterator i = x.begin();
         i != x.end(); ++i)
      {
        branch_name name = prefix / branch_name(i->first, origin::internal);
        tags.insert(tag_t(i->second,
                          utf8(name(), origin::internal),
                          key_id()));
      }
  }
};

class key_lister
{
public:
  typedef std::multimap<key_id, std::pair<branch_name, key_name> > name_map;
private:
  name_map & names;
public:
  key_lister(name_map & n) : names(n) { }
  void operator()(shared_ptr<policy> pol, branch_name const & prefix,
                  policies::delegation const & del)
  {
    if (!pol)
      return;

    typedef policies::policy::key_map key_map;

    key_map const & km = pol->list_keys();
    for (key_map::const_iterator i = km.begin(); i != km.end(); ++i)
      {
        names.insert(make_pair(i->second, make_pair(prefix, i->first)));
      }
  }
};



// find the policy governing a particular name
class policy_finder
{
  branch_name target;
  policy_chain & info;
public:
  policy_finder(branch_name const & target, policy_chain & info)
    : target(target), info(info)
  {
    info.clear();
  }

  void operator()(shared_ptr<policy> pol, branch_name const & prefix,
                  policies::delegation const & del)
  {
    if (prefix.empty())
      {
        policy_chain_item i;
        i.policy = pol;
        info.push_back(i);
        return;
      }
    if (target.has_prefix(prefix))
      {
        if (target.has_prefix(prefix))
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
  policy_info()
    : policy(), passthru(true)
  {
    L(FL("Using empty passthru policy"));
  }
  policy_info(bool passthru, shared_ptr<policies::policy> const & ep)
    : policy(ep), passthru(passthru)
  {
    L(FL("Using policy with passthru = %d") % passthru);
  }

  policies::policy const & get_base_policy() const
  {
    I(policy);
    return *policy;
  }

  void all_branches(project_t const & project, set<branch_name> & branches)
  {
    branches.clear();
    if (!policy)
      return;

    map<branch_name, branch_info> branch_map;
    walk_policies(project, policy, child_policies, branch_lister(branch_map));
    for (map<branch_name, branch_info>::iterator i = branch_map.begin();
         i != branch_map.end(); ++i)
      {
        branches.insert(i->first);
      }
  }
  void all_branches(project_t const & project, set<branch_uid> & branches)
  {
    branches.clear();
    if (!policy)
      return;

    map<branch_name, branch_info> branch_map;
    walk_policies(project, policy, child_policies, branch_lister(branch_map));
    for (map<branch_name, branch_info>::iterator i = branch_map.begin();
         i != branch_map.end(); ++i)
      {
        branches.insert(i->second.self.get_uid());
      }
  }

  void all_tags(project_t const & project, set<tag_t> & tags)
  {
    tags.clear();
    if (!policy)
      return;

    walk_policies(project, policy, child_policies, tag_lister(tags));
  }

  bool try_translate_branch(project_t const & project,
                            branch_name const & name,
                            branch_uid & uid)
  {
    L(FL("Translating branch '%s'") % name);
    map<branch_name, branch_info> branch_map;
    walk_policies(project, policy, child_policies, branch_lister(branch_map));
    map<branch_name, branch_info>::const_iterator i = branch_map.find(name);
    if (i != branch_map.end())
      {
        uid = i->second.self.get_uid();
        return true;
      }

    L(FL("branch '%s' does not exist") % name);
    branch_name postfix("__policy__", origin::internal);
    if (name.has_postfix(postfix))
      {
        branch_name pol_name = name.without_postfix(postfix);
        L(FL("branch '%s' is named like a policy; checking for policy named '%s'")
          % name % pol_name);

        policy_chain info;
        find_governing_policy(project, pol_name, info);
        if (!info.empty())
          {
            if (info.back().full_policy_name == pol_name)
              {
                policies::delegation del = info.back().delegation;
                if (del.is_branch_type())
                  {
                    uid = del.get_branch_spec().get_uid();
                    return true;
                  }
              }
          }
      }
    return false;
  }

  branch_uid translate_branch(project_t const & project, branch_name const & name)
  {
    branch_uid uid;
    E(try_translate_branch(project, name, uid), origin::no_fault,
      F("branch '%s' does not exist; please inform %s of what "
        "command you ran to get this message so we can replace it with "
        "a better one")
      % name % PACKAGE_BUGREPORT);
    return uid;
  }

  branch_name translate_branch(project_t const & project, branch_uid const & uid)
  {
    map<branch_name, branch_info> branch_map;
    walk_policies(project, policy, child_policies, branch_lister(branch_map));
    for (map<branch_name, branch_info>::iterator i = branch_map.begin();
         i != branch_map.end(); ++i)
      {
        if (i->second.self.get_uid() == uid)
          return i->first;
      }
    I(false);
  }

  void lookup_branch(project_t const & project,
                     branch_name const & name,
                     branch_uid & uid, set<key_id> & signers)
  {
    L(FL("Looking up branch '%s'") % name);
    {
      map<branch_name, branch_info> branch_map;
      walk_policies(project, policy, child_policies, branch_lister(branch_map));
      map<branch_name, branch_info>::const_iterator i = branch_map.find(name);
      if (i != branch_map.end())
        {
          uid = i->second.self.get_uid();
          L(FL("found uid '%s' for branch '%s'") % uid % name);
          set<external_key_name> const & raw_signers = i->second.self.get_signers();
          for (set<external_key_name>::const_iterator k = raw_signers.begin();
               k != raw_signers.end(); ++k)
            {
              id id;
              if (try_decode_hexenc((*k)(), id))
                {
                  L(FL("branch has signer '%s'") % *k);
                  signers.insert(key_id(id));
                }
              else
                {
                  key_name kn = typecast_vocab<key_name>(*k);
                  key_id id = i->second.owner->get_key_id(kn);
                  L(FL("branch has signer '%s' (%s)") % kn % id);
                  signers.insert(id);
                }
            }
          return;
        }
    }
    L(FL("branch '%s' does not exist") % name);

    branch_name postfix("__policy__", origin::internal);
    if (name.has_postfix(postfix))
      {
        branch_name pol_name = name.without_postfix(postfix);
        L(FL("branch '%s' is named like a policy; checking for policy named '%s'")
          % name % pol_name);

        policy_chain info;
        find_governing_policy(project, pol_name, info);
        do // cleaner than lots of nested if statements
          {
            if (info.empty())
              break;
            if (info.back().full_policy_name != pol_name)
              break;

            policies::delegation del = info.back().delegation;
            if (!del.is_branch_type())
              break;

            policies::policy_ptr parent;
            if (info.size() >= 2)
              parent = idx(info, info.size()-2).policy;

            uid = del.get_branch_spec().get_uid();
            set<external_key_name> const & raw_signers = del.get_branch_spec().get_signers();
            for (set<external_key_name>::const_iterator k = raw_signers.begin();
                 k != raw_signers.end(); ++k)
              {
                id id;
                if (try_decode_hexenc((*k)(), id))
                  signers.insert(key_id(id));
                else
                  {
                    E(parent, origin::user,
                      F("toplevel delegation to '%s' uses key name")
                      % name);
                    key_name kn = typecast_vocab<key_name>(*k);
                    signers.insert(parent->get_key_id(kn));
                  }
              }
            return;
          }
        while (false);
      }

    E(false, origin::no_fault,
      F("branch '%s' does not exist; please inform %s of what "
        "command you ran to get this message so we can replace it with "
        "a better one")
      % name % PACKAGE_BUGREPORT);
  }

  void find_governing_policy(project_t const & project,
                             branch_name const & of_what,
                             policy_chain & info)
  {
    walk_policies(project, policy, child_policies,
                  policy_finder(of_what, info));
  }

  void list_policies(project_t const & project,
                     branch_name const & base,
                     set<branch_name> & children)
  {
    walk_policies(project, policy, child_policies,
                  policy_lister(base, children));
  }
  bool lookup_key_name(project_t const & project,
                       key_id const & ident,
                       branch_name const & where,
                       key_name & official_name)
  {
    L(FL("looking for key %s under prefix '%s'...") % ident % where);
    key_lister::name_map names;
    walk_policies(project, policy, child_policies, key_lister(names));

    typedef key_lister::name_map::const_iterator it;
    pair<it, it> range = names.equal_range(ident);

    int prefix_length = -1;
    bool have_dup = false;
    set<key_name> matched_names;
    for (it i = range.first; i != range.second; ++i)
      {
        int my_prefix_length = 0;
        if (!where.empty() && where.has_prefix(i->second.first))
          my_prefix_length = i->second.first.size();
        if (my_prefix_length >= prefix_length)
          {
            if (my_prefix_length > prefix_length)
              {
                matched_names.clear();
              }
            prefix_length = my_prefix_length;
            
            branch_name name_as_branch = typecast_vocab<branch_name>(i->second.second);
            official_name = typecast_vocab<key_name>(i->second.first / name_as_branch);
            matched_names.insert(official_name);
          }
      }
    if (matched_names.size() > 1)
      {
        W(F("key %s has multiple names") % ident);
        for (set<key_name>::const_iterator k = matched_names.begin();
             k != matched_names.end(); ++k)
          {
            W(F("    name: %s") % *k);
          }
      }
    return prefix_length >= 0;
  }
  void find_keys_named(project_t const & project,
                       key_name const & name,
                       branch_name const & where,
                       map<key_name, key_id> & results)
  {
    key_lister::name_map names;
    walk_policies(project, policy, child_policies, key_lister(names));

    results.clear();
    int prefix_length = -1;

    typedef key_lister::name_map::const_iterator it;
    for (it i = names.begin(); i != names.end(); ++i)
      {
        branch_name name_as_branch =
          typecast_vocab<branch_name>(i->second.second);
        key_name official_name =
          typecast_vocab<key_name>(i->second.first / name_as_branch);
        if (official_name == name)
          {
            // fully-qualified exact match
            results.clear();
            results[official_name] = i->first;
            return;
          }
        if (i->second.second != name)
          continue;
        int my_prefix_length = 0;
        if (!where.empty() && where.has_prefix(i->second.first))
          my_prefix_length = i->second.first.size();
        // This is used to interpret key names provided by the user.
        // It shouldn't accidentially match on keys that aren't recognized by
        // the current policy.
        if (my_prefix_length == 0 && !where.empty())
          continue;
        if (my_prefix_length >= prefix_length)
          {
            if (my_prefix_length > prefix_length)
              results.clear();
            prefix_length = my_prefix_length;
            results[official_name] = i->first;
          }
      }
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

branch_name const empty_branch_name;
project_t::project_t(database & db)
  : db(db), branch_option(empty_branch_name)
{
  project_policy.reset(new policy_info());
}

project_t::project_t(database & db, lua_hooks & lua, options & opts)
  : db(db), branch_option(opts.branch)
{
  shared_ptr<policies::base_policy> bp(new policies::base_policy(opts, lua));
  project_policy.reset(new policy_info(bp->empty(), bp));
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
  find_governing_policy(name, info);
  if (info.empty())
    return false;
  return info.back().full_policy_name == name;
}

void
project_t::get_subpolicies(branch_name const & name,
                           std::set<branch_name> & names) const
{
  if (project_policy->passthru)
    return;

  project_policy->list_policies(*this, name, names);
  names.erase(name);
}


void
project_t::get_branch_list(set<branch_name> & names,
                           bool check_heads) const
{
  if (!project_policy->passthru)
    {
      project_policy->all_branches(*this, names);
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
                           bool check_heads) const
{
  if (!project_policy->passthru)
    {
      set<branch_name> all_names;
      project_policy->all_branches(*this, all_names);

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
project_t::get_branch_list(std::set<branch_uid> & branch_ids) const
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
  project_policy->all_branches(*this, branch_ids);
}

branch_uid
project_t::translate_branch(branch_name const & name) const
{
  if (project_policy->passthru)
    return typecast_vocab<branch_uid>(name);
  else
    return project_policy->translate_branch(*this, name);
}

branch_name
project_t::translate_branch(branch_uid const & uid) const
{
  if (project_policy->passthru)
    return typecast_vocab<branch_name>(uid);
  else
    return project_policy->translate_branch(*this, uid);
}

namespace
{
  struct not_in_branch : public is_failure
  {
    project_t const & project;
    branch_uid const & branch;
    bool is_managed;
    set<key_id> trusted_signers;
    not_in_branch(project_t const & project,
                  branch_uid const & branch)
      : project(project),
        branch(branch),
        is_managed(false)
    {}
    not_in_branch(project_t const & project,
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
    project_t const & project;
    branch_uid const & branch;
    bool is_managed;
    set<key_id> trusted_signers;
    suspended_in_branch(project_t const & project,
                        branch_uid const & branch)
      : project(project), branch(branch), is_managed(false)
    {}
    suspended_in_branch(project_t const & project,
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

  void do_get_branch_heads(pair<outdated_indicator, set<revision_id> > & branch,
                           project_t const & project,
                           branch_uid const & uid,
                           set<key_id> const * const signers,
                           bool ignore_suspend_certs,
                           multimap<revision_id, revision_id> * inverse_graph_cache_ptr)
  {
    if (!branch.first.outdated())
      return;

    L(FL("getting heads of branch %s") % uid);
    
    set<revision_id> leaves;
    branch.first = project.db.get_branch_leaves(typecast_vocab<cert_value>(uid),
                                                leaves);

    shared_ptr<not_in_branch> p;
    if (!signers)
      p.reset(new not_in_branch(project, uid));
    else
      p.reset(new not_in_branch(project, uid, *signers)); 
    
    bool have_failure = false;
    for (set<revision_id>::iterator l = leaves.begin();
         l != leaves.end(); ++l)
      {
        if ((*p)(*l))
        {
          have_failure = true;
          break;
        }
      }
    
    if (!have_failure)
      {
        branch.second = leaves;
      }
    else
      {
        branch.first = project.db.get_revisions_with_cert(cert_name(branch_cert_name),
                                                          typecast_vocab<cert_value>(uid),
                                                          branch.second);   
      }

    erase_ancestors_and_failures(project.db, branch.second, *p,

                                 inverse_graph_cache_ptr);



    if (!ignore_suspend_certs)
      {
        shared_ptr<suspended_in_branch> s;
        if (!signers)
          s.reset(new suspended_in_branch(project, uid));
        else
          s.reset(new suspended_in_branch(project, uid, *signers));
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
      % uid % branch.second.size());
  }
}

outdated_indicator
project_t::get_branch_heads(branch_uid const & uid,
                            std::set<key_id> const & signers,
                            std::set<revision_id> & heads,
                            bool ignore_suspend_certs,
                            std::multimap<revision_id, revision_id>
                            *inverse_graph_cache_ptr) const
{
  branch_heads_key cache_index(uid, ignore_suspend_certs, signers, true);

  pair<outdated_indicator, set<revision_id> > &
    branch = branch_heads[cache_index];

  do_get_branch_heads(branch, *this, uid, &signers,
                      ignore_suspend_certs,
                      inverse_graph_cache_ptr);

  heads = branch.second;
  return branch.first;
}

bool project_t::branch_exists(branch_name const & name) const
{
  if (project_policy->passthru)
    return true;
  branch_uid uid;
  return project_policy->try_translate_branch(*this, name, uid);
}

void
project_t::get_branch_heads(branch_name const & name,
                            set<revision_id> & heads,
                            bool ignore_suspend_certs,
                            multimap<revision_id, revision_id>
                                *inverse_graph_cache_ptr) const
{
  branch_uid uid;
  set<key_id> signers;
  set<key_id> *sign_ptr = 0;
  if (project_policy->passthru)
    uid = typecast_vocab<branch_uid>(name);
  else
    {
      project_policy->lookup_branch(*this, name, uid, signers);
      sign_ptr = &signers;
    }

  branch_heads_key cache_index(uid, ignore_suspend_certs, signers, sign_ptr);

  pair<outdated_indicator, set<revision_id> > &
    branch = branch_heads[cache_index];

  do_get_branch_heads(branch, *this, uid, sign_ptr,
                      ignore_suspend_certs,
                      inverse_graph_cache_ptr);

  heads = branch.second;
}

bool
project_t::revision_is_in_branch(revision_id const & id,
                                 branch_name const & branch) const
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
      project_policy->lookup_branch(*this, branch, uid, signers);

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
                                           branch_name const & branch) const
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
                                    vector<id> & hashes) const
{
  return db.get_revision_certs(rid, hashes);
}

outdated_indicator
project_t::get_revision_certs(revision_id const & id,
                              vector<cert> & certs) const
{
  return db.get_revision_certs(id, certs);
}

outdated_indicator
project_t::get_revision_certs_by_name(revision_id const & id,
                                      cert_name const & name,
                                      vector<cert> & certs) const
{
  outdated_indicator i = db.get_revision_certs(id, name, certs);
  db.erase_bogus_certs(*this, certs);
  return i;
}

outdated_indicator
project_t::get_revision_branches(revision_id const & id,
                                 set<branch_name> & branches) const
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
                            vector<pair<id, cert> > & certs) const
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
project_t::get_tags(set<tag_t> & tags) const
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
      project_policy->all_tags(*this, tags);
      return outdated_indicator();
    }
}

void
project_t::find_governing_policy(branch_name const & of_what,
                                 policy_chain & info) const
{
  I(!project_policy->passthru);
  project_policy->find_governing_policy(*this, of_what, info);
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
      project_policy->find_governing_policy(*this,
                                            branch_name(name, origin::user),
                                            info);
      E(!info.empty(), origin::user,
        F("Cannot find policy for tag '%s'") % name);
      E(info.back().delegation.is_branch_type(), origin::user,
        F("Cannot edit '%s', it is delegated to a specific revision") % name);
      policies::policy_branch br(*this,
                                 info.back().policy,
                                 info.back().delegation.get_branch_spec());

      policies::editable_policy ep;
      br.get_policy(ep, origin::user);

      ep.set_tag(name.substr(info.back().full_policy_name.size() + 1), id);

      br.commit(*this, keys, ep,
                utf8((F("Set tag %s") % name).str(),
                     origin::internal),
                origin::user);
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
      complete_key_identity_from_id(0, lua, branch, key);

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
                              branch_name const & where,
                              key_name const & name,
                              key_id & id) const
{
  if (!project_policy->passthru)
    {
      set<key_id> my_matching_keys;
      vector<key_id> storekeys;
      keys->get_key_ids(storekeys);
      for (vector<key_id>::const_iterator i = storekeys.begin();
           i != storekeys.end(); ++i)
        {
          key_name i_name;
          keypair kp;
          keys->get_key_pair(*i, i_name, kp);

          if (i_name == name)
            my_matching_keys.insert(*i);
        }
      E(my_matching_keys.size() < 2, origin::user,
        F("you have %d keys named '%s'") %
        my_matching_keys.size() % name);
      if (my_matching_keys.size() == 1)
        {
          id = *my_matching_keys.begin();
          return;
        }

      map<key_name, key_id> results;
      project_policy->find_keys_named(*this, name, where, results);
      E(results.size() <= 1, origin::user,
        F("there are %d keys named '%s'") % results.size() % name);
      E(results.size() > 0, origin::user,
        F("there are no keys named '%s'") % name);
      id = results.begin()->second;
      return;
    }

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
          bool found;
          if (project_policy->passthru)
            found = lua.hook_get_local_key_name(identity);
          else
            found = project_policy->lookup_key_name(*this,
                                                    identity.id,
                                                    where,
                                                    identity.official_name);
          if (found)
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
          bool found;
          if (project_policy->passthru)
            found = lua.hook_get_local_key_name(identity);
          else
            found = project_policy->lookup_key_name(*this,
                                                    identity.id,
                                                    where,
                                                    identity.official_name);
          if (found)
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
project_t::get_given_name_of_key(key_store * const keys,
                                 key_id const & id,
                                 key_name & name) const
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
      E(false, id.inner().made_from,
        F("key %s does not exist") % id);
    }
}

void
project_t::complete_key_identity_from_id(key_store * const keys,
                                         lua_hooks & lua,
                                         branch_name const & where,
                                         key_identity_info & info) const
{
  MM(info.id);
  MM(info.official_name);
  MM(info.given_name);
  I(!info.id.inner()().empty());
  get_given_name_of_key(keys, info.id, info.given_name);

  if (project_policy->passthru)
    lua.hook_get_local_key_name(info);
  else
    project_policy->lookup_key_name(*this,
                                    info.id,
                                    where,
                                    info.official_name);
}

void
project_t::complete_key_identity_from_id(key_store & keys,
                                         lua_hooks & lua,
                                         key_identity_info & info) const
{
  complete_key_identity_from_id(&keys, lua, branch_option, info);
}

void
project_t::complete_key_identity_from_id(lua_hooks & lua,
                                         key_identity_info & info) const
{
  complete_key_identity_from_id(0, lua, branch_option, info);
}

void
project_t::get_key_identity(key_store * const keys,
                            lua_hooks & lua,
                            external_key_name const & input,
                            key_identity_info & output) const
{
  try
    {
      string in2 = decode_hexenc(input(), origin::no_fault);
      id ident(in2, origin::no_fault);
      // set this separately so we can ensure that the key_id() calls
      // above throw recoverable_failure instead of unrecoverable_failure
      ident.made_from = input.made_from;
      output.id = key_id(ident);
      complete_key_identity_from_id(keys, lua, branch_option, output);
      return;
    }
  catch (recoverable_failure &)
    {
      output.official_name = typecast_vocab<key_name>(input);
      lookup_key_by_name(keys, lua, branch_option, output.official_name, output.id);
      get_given_name_of_key(keys, output.id, output.given_name);
      return;
    }
}

void
project_t::get_key_identity(key_store & keys,
                            lua_hooks & lua,
                            external_key_name const & input,
                            key_identity_info & output) const
{
  get_key_identity(&keys, lua, input, output);
}

void
project_t::get_key_identity(lua_hooks & lua,
                            external_key_name const & input,
                            key_identity_info & output) const
{
  get_key_identity(0, lua, input, output);
}


// These should maybe be converted to member functions.

string
describe_revision(options const & opts, lua_hooks & lua,
                  project_t & project, revision_id const & id)
{
  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);

  string description;

  description += encode_hexenc(id.inner()(), id.inner().made_from);

  string date_fmt;
  if (opts.format_dates)
    {
      if (!opts.date_fmt.empty())
        date_fmt = opts.date_fmt;
      else
        lua.hook_get_date_format_spec(date_time_short, date_fmt);
    }

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
          dates += date_t(i->value()).as_formatted_localtime(date_fmt);
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
