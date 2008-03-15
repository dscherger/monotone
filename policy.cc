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

policy_branch
policy_branch::empty_policy(database & db)
{
  return policy_branch(db);
}

policy_branch::policy_branch(data const & spec,
			     branch_prefix const & prefix,
			     database & db)
  : prefix(prefix), db(db)
{
  init(spec);
}

shared_ptr<branch_policy>
policy_branch::maybe_get_branch_policy(branch_name const & name)
{
  map<branch_name, branch_policy> bm = branches();
  map<branch_name, branch_policy>::const_iterator i = bm.find(name);
  if (i != bm.end())
    return shared_ptr<branch_policy>(new branch_policy(i->second));
  else
    return shared_ptr<branch_policy>();
}

policy_branch::policy_branch(revision_id const & rid,
                             branch_prefix const & prefix,
                             database & db)
  : prefix(prefix), db(db)
{
  rev.reset(new policy_revision(db, rid, prefix));
}

void
policy_branch::init(data const & spec)
{
  bool seen_revid = false;
  bool seen_branchspec = false;
  revision_id rev_id;

  basic_io::input_source src(spec(), "policy spec");
  basic_io::tokenizer tok(src);
  basic_io::parser pa(tok);

  while (pa.symp())
    {
      if(pa.symp(basic_io::syms::branch_uid))
        {
          seen_branchspec = true;
          pa.sym();
          string branch;
          pa.str(branch);
          my_branch_cert_value = branch_uid(branch);
        }
      else if (pa.symp(basic_io::syms::committer))
        {
          seen_branchspec = true;
          pa.sym();
          string key;
          pa.str(key);
          my_committers.insert(rsa_keypair_id(key));
        }
      else if (pa.symp(basic_io::syms::revision_id))
        {
          seen_revid = true;
          pa.sym();
          string rid;
          pa.hex(rid);
          rev_id = revision_id(rid);
        }
      else
        {
          N(false, F("Unable to understand policy spec file for %s") % prefix);
        }
    }

  I(src.lookahead == EOF);

  E(seen_revid || seen_branchspec,
    F("Policy spec file for %s seems to be empty") % prefix);

  E(seen_revid != seen_branchspec,
    F("Policy spec file for %s contains both a revision id and a branch spec")
    % prefix);

  if (!null_id(rev_id))
    {
      rev.reset(new policy_revision(db, rev_id, prefix));
    }
}


policy_revision::policy_revision(database & db,
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

map<branch_name, branch_policy>
policy_revision::all_branches()
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

namespace
{
  struct not_in_managed_branch : public is_failure
  {
    database & db;
    cert_value branch;
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
get_branch_heads(branch_policy const & pol,
		 bool ignore_suspend_certs,
		 database & db,
		 std::set<revision_id> & heads,
		 multimap<revision_id, revision_id>
		 * inverse_graph_cache_ptr)
{
  outdated_indicator ret;

  ret = db.get_revisions_with_cert(cert_name(branch_cert_name),
				   cert_value(pol.branch_cert_value()), heads);

  not_in_managed_branch p(db, cert_value(pol.branch_cert_value()),
                          pol.committers);
  erase_ancestors_and_failures(db, heads, p, inverse_graph_cache_ptr);

  if (!ignore_suspend_certs)
    {
      suspended_in_managed_branch s(db, cert_value(pol.branch_cert_value()),
                                    pol.committers);
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
revision_is_in_branch(branch_policy const & pol,
                      revision_id const & rid,
                      database & db)
{
  not_in_managed_branch p(db,
                          cert_value(pol.branch_cert_value()),
                          pol.committers);
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


shared_ptr<policy_revision> policy_branch::get_policy()
{
  if (!rev)
    {
      revision_id rid;
      if (maybe_get_policy_branch_head(my_branch_cert_value,
                                       my_committers, db, rid))
        {
          rev.reset(new policy_revision(db, rid, prefix));
        }
    }
  return rev;
}
map<branch_name, branch_policy> policy_branch::branches()
{
  shared_ptr<policy_revision> policy = get_policy();
  if (policy)
    {
      return policy->all_branches();
    }
  else
    return map<branch_name, branch_policy>();
}


bool
policy_branch::get_nearest_policy(branch_name const & name,
                                  branch_policy & policy_policy,
                                  branch_prefix & policy_prefix,
                                  std::string const & accumulated_prefix)
{
  shared_ptr<policy_revision> policy = get_policy();
  if (!policy)
    return false;

  policy_policy = branch_policy(branch_name(),
                                my_branch_cert_value,
                                my_committers);
  return policy->get_nearest_policy(name, policy_policy, policy_prefix,
                                    accumulated_prefix);
}

bool
policy_revision::get_nearest_policy(branch_name const & name,
                                    branch_policy & policy_policy,
                                    branch_prefix & policy_prefix,
                                    std::string const & accumulated_prefix)
{
  for (std::map<branch_prefix, policy_branch>::iterator
         i = delegations.begin(); i != delegations.end(); ++i)
    {
      std::string mypref(accumulated_prefix + "." + i->first());
      if (name().find(mypref) == 0)
        {
          return i->second.get_nearest_policy(name,
                                              policy_policy,
                                              policy_prefix,
                                              mypref);
        }
    }
  policy_prefix = branch_prefix(accumulated_prefix);
  return true;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
