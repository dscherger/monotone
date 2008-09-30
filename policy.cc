// Copyright 2008 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL v2 or later

#include "base.hh"

#include <vector>

#include "basic_io.hh"
#include "policy.hh"
#include "project.hh"
#include "revision.hh"
#include "roster.hh"
#include "transforms.hh"

using boost::shared_ptr;

using std::make_pair;
using std::map;
using std::multimap;
using std::set;
using std::string;
using std::vector;

namespace basic_io
{
  namespace syms
  {
    symbol const branch_uid("branch_uid");
    symbol const committer("committer");
    symbol const revision_id("revision_id");
  }
}

policy_branch::policy_branch(database & db)
  : db(db)
{
}

shared_ptr<policy_branch>
policy_branch::empty_policy(database & db)
{
  return shared_ptr<policy_branch>(new policy_branch(db));
}

policy_branch::policy_branch(editable_policy::delegation const & del,
			     database & db)
  : db(db), delayed(new editable_policy::delegation(del))
{
}

policy_branch::policy_branch(shared_ptr<editable_policy const> pol,
                             database & db)
  : db(db), policy(pol)
{
  init_lower();
}

shared_ptr<policy_branch>
policy_branch::create(editable_policy::delegation const & del,
                      database & db)
{
  return shared_ptr<policy_branch>(new policy_branch(del, db));
}

shared_ptr<policy_branch>
policy_branch::create(shared_ptr<editable_policy const> pol,
                      database & db)
{
  return shared_ptr<policy_branch>(new policy_branch(pol, db));
}

bool
policy_branch::init()
{
  if (policy)
    return true;
  if (!delayed)
    return false;

  policy.reset(new editable_policy(db, *delayed));
  delayed.reset();

  init_lower();
  return true;
}
void
policy_branch::init_lower()
{
  editable_policy::const_delegation_map dels = policy->get_all_delegations();
  for (editable_policy::const_delegation_map::const_iterator i = dels.begin();
       i != dels.end(); ++i)
    {
      delegations.insert(make_pair(i->first, create(*i->second, db)));
    }
}

shared_ptr<editable_policy const>
policy_branch::get_policy()
{
  I(init());
  return policy;
}

shared_ptr<editable_policy::branch const>
policy_branch::maybe_get_branch_policy(branch_name const & name)
{
  I(init());
  branchmap bm = branches();
  branchmap::const_iterator i = bm.find(name);
  if (i != bm.end())
    return shared_ptr<editable_policy::branch const>(new editable_policy::branch(i->second));
  else
    return shared_ptr<editable_policy::branch const>();
}

template<typename T>
class longer
{
public:
  bool operator()(T const & l, T const & r)
  {
    return l.size() > r.size();
  }
};

boost::shared_ptr<policy_branch>
policy_branch::walk(branch_name target,
                    branch_name & result)
{
  I(init());
  typedef map<branch_name, shared_ptr<policy_branch>, longer<branch_name> > match_map;
  match_map matches;
  for (delegation_map::const_iterator i = delegations.begin();
       i != delegations.end(); ++i)
    {
      if (target.has_prefix(branch_name(i->first)))
        {
          matches.insert(*i);
        }
    }
  if (matches.empty())
    {
      return shared_from_this();
    }
  else
    {
      result.append(matches.begin()->first);
      target.strip_prefix(matches.begin()->first);
      return matches.begin()->second->walk(target, result);;
    }
}

namespace
{
  struct not_in_managed_branch : public is_failure
  {
    database & db;
    cert_value branch;
    set<rsa_keypair_id> const & trusted_signers;
    bool is_trusted(set<rsa_keypair_id> const & signers,
		    revision_id const & rid,
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
    not_in_managed_branch(database & db,
			  cert_value const & branch,
			  set<rsa_keypair_id> const & trusted)
      : db(db), branch(branch), trusted_signers(trusted)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector< revision<cert> > certs;
      db.get_revision_certs(rid,
			    cert_name(branch_cert_name),
			    branch,
			    certs);
      erase_bogus_certs(db,
			boost::bind(&not_in_managed_branch::is_trusted,
				    this, _1, _2, _3, _4),
			certs);
      return certs.empty();
    }
  };

  struct suspended_in_managed_branch : public is_failure
  {
    database & db;
    cert_value branch;
    set<rsa_keypair_id> const & trusted_signers;
    bool is_trusted(set<rsa_keypair_id> const & signers,
		    revision_id const & rid,
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
    suspended_in_managed_branch(database & db,
				cert_value const & branch,
				set<rsa_keypair_id> const & trusted)
      : db(db), branch(branch), trusted_signers(trusted)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector< revision<cert> > certs;
      db.get_revision_certs(rid,
			    cert_name(suspend_cert_name),
			    branch,
			    certs);
      erase_bogus_certs(db,
			boost::bind(&suspended_in_managed_branch::is_trusted,
				    this, _1, _2, _3, _4),
			certs);
      return !certs.empty();
    }
  };
}

outdated_indicator
get_branch_heads(editable_policy::branch const & br,
		 bool ignore_suspend_certs,
		 database & db,
		 std::set<revision_id> & heads,
		 multimap<revision_id, revision_id>
		 * inverse_graph_cache_ptr)
{
  cert_value const certval(br.uid());
  outdated_indicator ret;

  ret = db.get_revisions_with_cert(cert_name(branch_cert_name),
				   certval, heads);

  not_in_managed_branch p(db, certval, br.committers);
  erase_ancestors_and_failures(db, heads, p, inverse_graph_cache_ptr);

  if (!ignore_suspend_certs)
    {
      suspended_in_managed_branch s(db, certval, br.committers);
      std::set<revision_id>::iterator it = heads.begin();
      while (it != heads.end())
	{
	  if (s(*it))
	    heads.erase(it++);
	  else
	    it++;
	}
    }
  return ret;
}

bool
revision_is_in_branch(editable_policy::branch const & br,
                      revision_id const & rid,
                      database & db)
{
  not_in_managed_branch p(db,
                          cert_value(br.uid()),
                          br.committers);
  return !p(rid);
}


bool maybe_get_policy_branch_head(branch_uid const & name,
				  set<rsa_keypair_id> const & trusted_signers,
				  database & db,
				  revision_id & rid)
{
  L(FL("getting heads of policy branch %s") % name);
  set<revision_id> heads;

  db.get_revisions_with_cert(cert_name(branch_cert_name),
			     cert_value(name()),
			     heads);

  not_in_managed_branch p(db, cert_value(name()), trusted_signers);
  erase_ancestors_and_failures(db, heads, p, NULL);

  if (heads.size() != 1)
    {
      W(F("Policy branch %s has %d heads, should have 1 head.")
	% name % heads.size());
      W(F("Some branches may not be available."));
      return false;
    }
  else
    {
      rid = *heads.begin();
      return true;
    }
}


policy_branch::branchmap
policy_branch::branches()
{
  branchmap out;
  branches(branch_name(), out);
  return out;
}
void
policy_branch::branches(branch_name const & prefix,
                        policy_branch::branchmap & branchlist)
{
  I(init());
  editable_policy::const_branch_map br = policy->get_all_branches();
  for (editable_policy::const_branch_map::const_iterator i = br.begin();
       i != br.end(); ++i)
    {
      branch_name name = prefix;
      name.append(branch_name(i->first));
      branchlist.insert(make_pair(name, *i->second));
    }
  for (delegation_map::const_iterator i = delegations.begin();
       i != delegations.end(); ++i)
    {
      branch_name new_prefix = prefix;
      new_prefix.append(branch_name(i->first));
      i->second->branches(new_prefix, branchlist);
    }
}
policy_branch::tagmap
policy_branch::tags()
{
  tagmap out;
  tags(branch_name(), out);
  return out;
}
void
policy_branch::tags(branch_name const & prefix,
                    policy_branch::tagmap & taglist)
{
  I(init());
  editable_policy::const_tag_map br = policy->get_all_tags();
  for (editable_policy::const_tag_map::const_iterator i = br.begin();
       i != br.end(); ++i)
    {
      branch_name name = prefix;
      name.append(branch_name(i->first));
      taglist.insert(make_pair(name, *i->second));
    }
  for (delegation_map::const_iterator i = delegations.begin();
       i != delegations.end(); ++i)
    {
      branch_name new_prefix = prefix;
      new_prefix.append(branch_name(i->first));
      i->second->tags(new_prefix, taglist);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
