// Copyright (C) 2008 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "policies/policy_branch.hh"

#include "database.hh"
#include "dates.hh"
#include "key_store.hh"
#include "lexical_cast.hh"
#include "merge_roster.hh"
#include "policies/editable_policy.hh"
#include "project.hh"
#include "revision.hh"
#include "roster.hh"
#include "transforms.hh"
#include "vocab_cast.hh"

using std::make_pair;
using std::map;
using std::pair;
using std::string;

namespace {
  class item_lister
  {
    database & db;
    dir_map::const_iterator i, end;
    bool badbit;
  public:
    typedef pair<string, data> value_type;
  private:
    value_type value;
  public:
    item_lister(roster_t const & ros,
                file_path const & dir_name,
                database & db)
      : db(db), badbit(false)
    {
      node_t n = ros.get_node(dir_name);
      if (!is_dir_t(n))
        {
          badbit = true;
          return;
        }
      dir_t dir = downcast_to_dir_t(n);
      i = dir->children.begin();
      while (i != end && !is_file_t(i->second))
        {
          ++i;
        }
      end = dir->children.end();
      if ((bool)*this)
        {
          get(value.first, value.second);
        }
    }
    bool bad() const { return badbit; }
    operator bool() const
    {
      return !badbit && i != end;
    }
    item_lister const & operator++()
    {
      I(i != end);
      do {
        ++i;
      } while (i != end && !is_file_t(i->second));
      get(value.first, value.second);
      return *this;
    }
    void get(string & name, data & contents) const
    {
      name = i->first();
      file_t f = downcast_to_file_t(i->second);
      file_data fdat;
      db.get_file_version(f->content, fdat);
      contents = fdat.inner();
    }
    value_type const & operator*() const
    {
      return value;
    }
    value_type const * operator->() const
    {
      return &value;
    }
  };
}

namespace policies {
  policy_ptr policy_from_revision(project_t const & project,
                                  policy_ptr owner,
                                  revision_id const & rev)
  {
    roster_t the_roster;
    project.db.get_roster(rev, the_roster);
    policies::editable_policy pol;
    pol.set_parent(owner);

    for (item_lister i(the_roster,
                       file_path_internal("branches"),
                       project.db);
         i; ++i)
      {
        policies::branch b;
        b.deserialize(i->second());
        pol.set_branch(i->first, b);
      }

    for (item_lister i(the_roster,
                       file_path_internal("delegations"),
                       project.db);
         i; ++i)
      {
        policies::delegation d;
        d.deserialize(i->second());
        pol.set_delegation(i->first, d);
      }

    for (item_lister i(the_roster,
                       file_path_internal("tags"),
                       project.db);
         i; ++i)
      {
        string s = boost::lexical_cast<string>(i->second);
        revision_id rid = decode_hexenc_as<revision_id>(s, origin::internal);
        pol.set_tag(i->first, rid);
      }

    for (item_lister i(the_roster,
                       file_path_internal("keys"),
                       project.db);
         i; ++i)
      {
        string s = boost::lexical_cast<string>(i->second);
        key_id id = decode_hexenc_as<key_id>(s, origin::internal);
        pol.set_key(key_name(i->first, origin::internal), id);
      }

    return policy_ptr(new policies::policy(pol));
  }
  policy_branch::policy_branch(project_t const & project,
                               policy_ptr parent_policy,
                               branch const & b)
    : spec_owner(parent_policy), spec(b)
  {
    reload(project);
  }
  branch const & policy_branch::get_spec() const
  {
    return spec;
  }

  policy_branch::iterator policy_branch::begin() const
  {
    return policies.begin();
  }
  policy_branch::iterator policy_branch::end() const
  {
    return policies.end();
  }
  size_t policy_branch::size() const
  {
    return policies.size();
  }

  void policy_branch::reload(project_t const & project)
  {
    policies.clear();
    std::set<revision_id> heads;
    std::set<key_id> keys;
    std::set<external_key_name> const & key_names = spec.get_signers();
    for (std::set<external_key_name>::const_iterator i = key_names.begin();
         i != key_names.end(); ++i)
      {
        id ident;
        if (try_decode_hexenc((*i)(), ident))
          {
            keys.insert(key_id(ident));
          }
        else
          {
            key_name name = typecast_vocab<key_name>(*i);
            keys.insert(spec_owner->get_key_id(name));
          }
      }
    project.get_branch_heads(spec.get_uid(),
                             keys,
                             heads,
                             false);

    for (std::set<revision_id>::const_iterator i = heads.begin();
         i != heads.end(); ++i)
      {
        policies.insert(make_pair(*i, policy_from_revision(project,
                                                           spec_owner,
                                                           *i)));
      }
  }

  void policy_branch::commit(project_t & project,
                             key_store & keys,
                             policy const & p,
                             utf8 const & changelog,
                             policy_branch::iterator parent_1,
                             policy_branch::iterator parent_2)
  {
    parent_map parents;
    if (parent_1 != end())
      {
        cached_roster cr;
        project.db.get_roster(parent_1->first, cr);
        parents.insert(make_pair(parent_1->first, cr));
      }
    if (parent_2 != end())
      {
        cached_roster cr;
        project.db.get_roster(parent_2->first, cr);
        parents.insert(make_pair(parent_2->first, cr));
      }
    roster_t new_roster;
    if (parents.empty())
      {
      }
    else if (parents.size() == 1)
      {
        new_roster = *parents.begin()->second.first;
      }
    else
      {
        parent_map::const_iterator left = parents.begin();
        parent_map::const_iterator right = left; ++right;


        revision_id const & left_rid = left->first;
        revision_id const & right_rid = right->first;
        roster_t const & left_roster = *left->second.first;
        roster_t const & right_roster = *right->second.first;
        marking_map const & left_mm = *left->second.second;
        marking_map const & right_mm = *right->second.second;

        std::set<revision_id> left_ancestors, right_ancestors;
        project.db.get_uncommon_ancestors(left_rid, right_rid,
                                          left_ancestors,
                                          right_ancestors);
        roster_merge_result merge_result;
        roster_merge(left_roster, left_mm, left_ancestors,
                     right_roster, right_mm, right_ancestors,
                     merge_result);
        // should really check this after applying our changes
        // (or check that the only conflicts are contents conflicts
        // on items that we'll be overwriting)
        E(merge_result.is_clean(), origin::user,
          F("Cannot automatically merge policy branch"));
        new_roster = merge_result.roster;
      }

    temp_node_id_source source;

    policy::del_map const & p_delegations = p.list_delegations();
    file_path delegation_path = file_path_internal("delegations");
    if (!new_roster.has_node(delegation_path))
      {
        node_id n = new_roster.create_dir_node(source);
        new_roster.attach_node(n, delegation_path);
      }
    for (policy::del_map::const_iterator i = p_delegations.begin();
         i != p_delegations.end(); ++i)
      {
        string x;
        i->second.serialize(x);
        file_data dat(x, origin::internal);
        file_id contents;
        calculate_ident(dat, contents);
        path_component name(i->first, origin::internal);
        if (new_roster.has_node(delegation_path / name))
          {
            node_id n = new_roster.get_node(delegation_path / name)->self;
            new_roster.set_content(n, contents);
          }
        else
          {
            node_id n = new_roster.create_file_node(contents, source);
            new_roster.attach_node(n, delegation_path / name);
          }
      }

    policy::key_map const & p_keys = p.list_keys();
    map<string, branch> const & p_branches = p.list_branches();
    map<string, revision_id> const & p_tags = p.list_tags();


    revision_t rev;
    make_revision(parents, new_roster, rev);
    revision_id revid;
    calculate_ident(rev, revid);

    string author = decode_hexenc(keys.signing_key.inner()(), origin::internal);
    transaction_guard guard(project.db);
    project.db.put_revision(revid, rev);
    project.put_standard_certs(keys, revid,
                               spec.get_uid(),
                               changelog,
                               date_t::now(),
                               author);
    guard.commit();
  }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
