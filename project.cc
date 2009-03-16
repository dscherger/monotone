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

#include "cert.hh"
#include "database.hh"
#include "file_io.hh"
#include "globish.hh"
#include "policy.hh"
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
  shared_ptr<policy_branch> policy;
  bool passthru;
  explicit policy_info(database & db)
    : policy(policy_branch::empty_policy(db)),
      passthru(true)
  {
  }
  policy_info(shared_ptr<editable_policy> const & ep, database & db)
    : policy(policy_branch::create(ep, db)), passthru(false)
  {
  }
};

project_t::project_t(database & db)
  : db(db)
{
  project_policy.reset(new policy_info(db));
}

project_t::project_t(database & db, lua_hooks & lua, options & opts)
  : db(db)
{
  shared_ptr<editable_policy> ep(new editable_policy(db, set<rsa_keypair_id>()));
  // Empty editable_policy's start with (at least) a self-referencing
  // __policy__ branch. We don't want that.
  while (!ep->get_all_branches().empty())
    {
      ep->remove_branch(ep->get_all_branches().begin()->first);
    }

  bool have_delegation(false);

  for (map<branch_name, hexenc<id> >::const_iterator
         i = opts.policy_revisions.begin();
       i != opts.policy_revisions.end(); ++i)
    {
      data dat("revision_id ["+i->second()+"]\n", origin::internal);
      ep->get_delegation(i->first(), true)->read(dat);
      have_delegation = true;
    }

  std::map<string, data> defs;
  lua.hook_get_projects(defs);
  for (map<string, data>::const_iterator i = defs.begin();
       i != defs.end(); ++i)
    {
      // Don't overwrite something that was overridden
      // from the command line (above).
      if (ep->get_delegation(i->first))
        continue;
      ep->get_delegation(i->first, true)->read(i->second);
      have_delegation = true;
    }
  if (have_delegation)
    project_policy.reset(new policy_info(ep, db));
  else
    project_policy.reset(new policy_info(db));
}

project_t
project_t::empty_project(database & db)
{
  return project_t(db);
}

bool
project_t::get_policy_branch_policy_of(branch_name const & name,
                                       editable_policy & policy_branch_policy,
                                       branch_name & policy_prefix)
{
  shared_ptr<policy_branch> result;
  result = project_policy->policy->walk(name, policy_prefix);
  if (!result)
    return false;
  policy_branch_policy = *result->get_policy();
  return true;
}

bool
project_t::policy_exists(branch_name const & name) const
{
  if (project_policy->passthru)
    return name().empty();

  branch_name got;
  shared_ptr<policy_branch> sub = project_policy->policy->walk(name, got);
  return sub && name == got;
}

void
project_t::get_subpolicies(branch_name const & name,
                           std::set<branch_name> & names) const
{
  if (project_policy->passthru)
    return;

  branch_name got;
  shared_ptr<policy_branch> sub = project_policy->policy->walk(name, got);
  if (sub && got == name)
    {
      shared_ptr<editable_policy const> pol = sub->get_policy();
      editable_policy::const_delegation_map dels = pol->get_all_delegations();
      for (editable_policy::const_delegation_map::const_iterator i = dels.begin();
           i != dels.end(); ++i)
        {
          branch_name n(name);
          n.append(branch_name(i->first, origin::internal));
          names.insert(n);
        }
    }
}

void
project_t::get_branch_list(std::set<branch_name> & names,
                           bool check_heads)
{
  if (!project_policy->passthru)
    {
      policy_branch::branchmap branches = project_policy->policy->branches();
      for (policy_branch::branchmap::const_iterator i = branches.begin();
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
          const branch_name branch(*i, origin::database);
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
      policy_branch::branchmap branches = project_policy->policy->branches();
      for (policy_branch::branchmap::const_iterator i = branches.begin();
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
      const branch_name branch(*i, origin::database);
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
          branch_ids.insert(typecast_vocab<branch_uid>(*i));
        }
      return;
    }
  policy_branch::branchmap branches = project_policy->policy->branches();
  for (policy_branch::branchmap::const_iterator i = branches.begin();
       i != branches.end(); ++i)
    {
      branch_ids.insert(i->second.uid);
    }
}

branch_uid
project_t::translate_branch(branch_name const & name)
{
  if (project_policy->passthru)
    return typecast_vocab<branch_uid>(name);
  policy_branch::branchmap branches = project_policy->policy->branches();
  policy_branch::branchmap::iterator i = branches.find(name);
  I(i != branches.end());
  return i->second.uid;
}

branch_name
project_t::translate_branch(branch_uid const & uid)
{
  if (project_policy->passthru)
    return typecast_vocab<branch_name>(uid);
  policy_branch::branchmap branches = project_policy->policy->branches();
  for (policy_branch::branchmap::const_iterator i = branches.begin();
       i != branches.end(); ++i)
    {
      if (i->second.uid == uid)
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
                            typecast_vocab<cert_value>(branch),
                            certs);
      db.erase_bogus_certs(certs);
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
                            typecast_vocab<cert_value>(branch),
                            certs);
      db.erase_bogus_certs(certs);
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
                                                typecast_vocab<cert_value>(name),
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
          shared_ptr<editable_policy::branch const> bp;
          bp  = project_policy->policy->maybe_get_branch_policy(name);
          E(bp, name.made_from,
            F("Cannot find policy for branch %s.") % name);

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
      branch_uid bid = typecast_vocab<branch_uid>(branch);
      vector<revision<cert> > certs;
  db.get_revision_certs(id, branch_cert_name,
                        typecast_vocab<cert_value>(bid), certs);

      int num = certs.size();

  db.erase_bogus_certs(certs);

      L(FL("found %d (%d valid) %s branch certs on revision %s")
        % num
        % certs.size()
        % branch
        % id);

      return !certs.empty();
    }
  else
    {
      shared_ptr<editable_policy::branch const> bp;
      bp  = project_policy->policy->maybe_get_branch_policy(branch);
      E(bp, branch.made_from,
        F("Cannot find policy for branch %s.") % branch);
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
  vector<revision<cert> > certs;
  db.get_revision_certs(id, suspend_cert_name,
                        typecast_vocab<cert_value>(branch), certs);

  int num = certs.size();

  db.erase_bogus_certs(certs);

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
  db.erase_bogus_certs(certs);
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
        branches.insert(typecast_vocab<branch_name>(i->inner().value));
      else
        {
          std::set<branch_uid> branchids;
          get_branch_list(branchids);
          branch_uid bid = typecast_vocab<branch_uid>(i->inner().value);
          if (branchids.find(bid) != branchids.end())
            branches.insert(translate_branch(bid));
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
    bid = typecast_vocab<branch_uid>(branch);
  else
    bid = translate_branch(branch);

  return db.get_revision_certs(branch_cert_name,
                               typecast_vocab<cert_value>(bid), certs);
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
  if (project_policy->passthru)
    {
      std::vector<revision<cert> > certs;
      outdated_indicator i = db.get_revision_certs(tag_cert_name, certs);
      db.erase_bogus_certs(certs);
      tags.clear();
      for (std::vector<revision<cert> >::const_iterator i = certs.begin();
           i != certs.end(); ++i)
        tags.insert(tag_t(revision_id(i->inner().ident),
                      typecast_vocab<utf8>(i->inner().value),
                      i->inner().key));

      return i;
    }
  else
    {
      policy_branch::tagmap got = project_policy->policy->tags();
      for (policy_branch::tagmap::const_iterator i = got.begin();
           i != got.end(); ++i)
        {
          tags.insert(tag_t(i->second.rev,
                            typecast_vocab<utf8>(i->first),
                            rsa_keypair_id()));
        }
      return outdated_indicator();
    }
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
      shared_ptr<policy_branch> policy_br;
      branch_name tag_name(name, origin::internal);
      branch_name policy_name;
      policy_br = project_policy->policy->walk(tag_name, policy_name);
      E(policy_br && policy_br != project_policy->policy,
        origin::internal,
        F("Cannot find a parent policy for tag %s") % tag_name);
      tag_name.strip_prefix(policy_name);
      editable_policy ep(*policy_br->get_policy());
      ep.get_tag(tag_name(), true)->rev = id;
      ep.commit(*this, keys, utf8((F("Set tag %s to %s") % tag_name % id).str(),
                           tag_name.made_from));
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
      rsa_keypair_id key;
      get_user_key(opts, lua, db, keys, key);

      if (!lua.hook_get_author(branch, key, author))
        author = key();
    }

  put_standard_certs(keys, id, branch, changelog, date, author);
}

bool
project_t::put_cert(key_store & keys,
                    revision_id const & id,
                    cert_name const & name,
                    cert_value const & value)
{
  I(!keys.signing_key().empty());

  cert t(id, name, value, keys.signing_key);
  string signed_text;
  cert_signable_text(t, signed_text);
  load_key_pair(keys, t.key);
  keys.make_signature(db, t.key, signed_text, t.sig);

  revision<cert> cc(t);
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

// These should maybe be converted to member functions.

string
describe_revision(project_t & project, revision_id const & id)
{
  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);

  string description;

  description += encode_hexenc(id.inner()(), id.inner().made_from);

  // append authors and date of this revision
  vector< revision<cert> > tmp;
  project.get_revision_certs_by_name(id, author_name, tmp);
  for (vector< revision<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      description += " ";
      description += i->inner().value();
    }
  project.get_revision_certs_by_name(id, date_name, tmp);
  for (vector< revision<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      description += " ";
      description += i->inner().value();
    }

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
