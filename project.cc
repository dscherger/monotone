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

#include "cert.hh"
#include "database.hh"
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
using boost::lexical_cast;

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
{}

void
project_t::get_branch_list(set<branch_name> & names,
                           bool check_heads) const
{
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

namespace
{
  struct not_in_branch : public is_failure
  {
    project_t const & project;
    branch_name const & branch;
    not_in_branch(project_t const & project,
                  branch_name const & branch)
      : project(project), branch(branch)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector<cert> certs;
      project.db.get_revision_certs(rid,
                                    cert_name(branch_cert_name),
                                    typecast_vocab<cert_value>(branch),
                                    certs);
      project.db.erase_bogus_certs(project, certs);
      return certs.empty();
    }
  };

  struct suspended_in_branch : public is_failure
  {
    project_t const & project;
    branch_name const & branch;
    suspended_in_branch(project_t const & project,
                        branch_name const & branch)
      : project(project), branch(branch)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector<cert> certs;
      project.db.get_revision_certs(rid,
                                    cert_name(suspend_cert_name),
                                    typecast_vocab<cert_value>(branch),
                                    certs);
      project.db.erase_bogus_certs(project, certs);
      return !certs.empty();
    }
  };
}

void
project_t::get_branch_heads(branch_name const & name,
                            set<revision_id> & heads,
                            bool ignore_suspend_certs,
                            multimap<revision_id, revision_id> * inverse_graph_cache_ptr) const
{
  pair<branch_name, suspended_indicator>
    cache_index(name, ignore_suspend_certs);
  pair<outdated_indicator, set<revision_id> > &
    branch = branch_heads[cache_index];
  outdated_indicator & indicator = branch.first;
  set<revision_id> & my_heads = branch.second;
  if (indicator.outdated())
    {
      L(FL("getting heads of branch %s") % name);

      set<revision_id> leaves;
      indicator = db.get_branch_leaves(typecast_vocab<cert_value>(name),
                                       leaves);

      not_in_branch p(*this, name);

      bool have_failure = false;
      for (set<revision_id>::iterator l = leaves.begin();
           l != leaves.end(); ++l)
        {
          if (p(*l))
            {
              have_failure = true;
              break;
            }
        }

      if (!have_failure)
        {
          my_heads = leaves;
        }
      else
        { // bah, do it the slow way
          indicator = db.get_revisions_with_cert(cert_name(branch_cert_name),
                                                 typecast_vocab<cert_value>(name),
                                                 my_heads);
          erase_ancestors_and_failures(db, my_heads, p,
                                       inverse_graph_cache_ptr);

        }

      if (!ignore_suspend_certs)
        {
          suspended_in_branch s(*this, name);
          set<revision_id>::iterator it = my_heads.begin();
          while (it != my_heads.end())
            if (s(*it))
              my_heads.erase(it++);
            else
              it++;
        }

      L(FL("found heads of branch %s (%s heads)")
        % name % my_heads.size());
    }
  heads = my_heads;
}

bool
project_t::revision_is_in_branch(revision_id const & id,
                                 branch_name const & branch) const
{
  vector<cert> certs;
  db.get_revision_certs(id, branch_cert_name,
                        typecast_vocab<cert_value>(branch), certs);

  int num = certs.size();

  db.erase_bogus_certs(*this, certs);

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
  put_cert(keys, id, branch_cert_name, typecast_vocab<cert_value>(branch));
}

bool
project_t::revision_is_suspended_in_branch(revision_id const & id,
                                           branch_name const & branch) const
{
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
  put_cert(keys, id, suspend_cert_name, typecast_vocab<cert_value>(branch));
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
    branches.insert(typecast_vocab<branch_name>(i->value));

  return i;
}

outdated_indicator
project_t::get_branch_certs(branch_name const & branch,
                            vector<pair<id, cert> > & certs) const
{
  return db.get_revision_certs(branch_cert_name,
                               typecast_vocab<cert_value>(branch), certs);
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
  vector<cert> certs;
  outdated_indicator i = db.get_revision_certs(tag_cert_name, certs);
  db.erase_bogus_certs(*this, certs);
  tags.clear();
  for (vector<cert>::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    tags.insert(tag_t(revision_id(i->ident),
                      typecast_vocab<utf8>(i->value),
                      i->key));

  return i;
}

void
project_t::put_tag(key_store & keys,
                   revision_id const & id,
                   string const & name)
{
  put_cert(keys, id, tag_cert_name, cert_value(name, origin::user));
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
      complete_key_identity_from_id(lua, key);

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
           cert_value(lexical_cast<string>(passed), origin::internal));
}

void
project_t::lookup_key_by_name(key_store * const keys,
                              lua_hooks & lua,
                              key_name const & name,
                              key_id & id) const
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
      E(false, origin::internal,
        F("key %s does not exist") % id);
    }
}

void
project_t::complete_key_identity_from_id(key_store * const keys,
                                         lua_hooks & lua,
                                         key_identity_info & info) const
{
  MM(info.id);
  MM(info.official_name);
  MM(info.given_name);
  I(!info.id.inner()().empty());
  get_given_name_of_key(keys, info.id, info.given_name);
  lua.hook_get_local_key_name(info);
}

void
project_t::complete_key_identity_from_id(key_store & keys,
                                         lua_hooks & lua,
                                         key_identity_info & info) const
{
  complete_key_identity_from_id(&keys, lua, info);
}

void
project_t::complete_key_identity_from_id(lua_hooks & lua,
                                         key_identity_info & info) const
{
  complete_key_identity_from_id(0, lua, info);
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
      complete_key_identity_from_id(keys, lua, output);
      return;
    }
  catch (recoverable_failure &)
    {
      output.official_name = typecast_vocab<key_name>(input);
      lookup_key_by_name(keys, lua, output.official_name, output.id);
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
