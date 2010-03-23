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
      if (!ros.has_node(dir_name))
        {
          badbit = true;
          return;
        }
      const_node_t n = ros.get_node(dir_name);
      if (!is_dir_t(n))
        {
          badbit = true;
          return;
        }
      const_dir_t dir = downcast_to_dir_t(n);
      i = dir->children.begin();
      end = dir->children.end();
      while (i != end && !is_file_t(i->second))
        {
          ++i;
        }
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
      if (!(bool)*this)
        return;
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
  void policy_from_roster(project_t const & project,
                          roster_t const & the_roster,
                          editable_policy & pol)
  {
    pol.clear();
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
  }
  policy_ptr policy_from_revision(project_t const & project,
                                  policy_ptr owner,
                                  revision_id const & rev)
  {
    roster_t the_roster;
    project.db.get_roster(rev, the_roster);
    policies::editable_policy pol;
    pol.set_parent(owner);
    policy_from_roster(project, the_roster, pol);
    return policy_ptr(new policy(pol));
  }

  policy_branch::policy_branch(project_t const & project,
                               policy_ptr parent_policy,
                               branch const & b)
    : spec_owner(parent_policy), spec(b)
  {
    loaded = reload(project);
  }
  branch const & policy_branch::get_spec() const
  {
    return spec;
  }
  size_t policy_branch::num_heads() const
  {
    return _num_heads;
  }

  namespace {
    void get_heads(project_t const & project,
                   branch const & spec,
                   policy_ptr const & spec_owner,
                   std::set<revision_id> & heads)
    {
      heads.clear();
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
    }
    void get_rosters(project_t const & project,
                     std::set<revision_id> const & heads,
                     parent_map & rosters)
    {
      rosters.clear();
      for (std::set<revision_id>::const_iterator h = heads.begin();
           h != heads.end(); ++h)
        {
          cached_roster cr;
          project.db.get_roster(*h, cr);
          rosters.insert(make_pair(*h, cr));
        }
      if (heads.empty())
        {
          rosters.insert(make_pair(revision_id(),
                                   make_pair(roster_t_cp(new roster_t()),
                                             marking_map_cp(new marking_map()))));
        }
    }
    enum parent_result { parent_clean, parent_semiclean, parent_fail };
    parent_result try_merge_parents(project_t const & project,
                                    parent_map const & parents,
                                    roster_t & roster)
    {
      if (parents.size() > 2)
        return parent_fail;
      if (parents.size() == 1)
        {
          roster = *parents.begin()->second.first;
          return parent_clean;
        }

      
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
      roster = merge_result.roster;

      if (merge_result.is_clean())
        return parent_clean;
      else
        return parent_fail; // eh, don't bother with 'semiclean'
    }
  }

  bool policy_branch::reload(project_t const & project)
  {
    std::set<revision_id> heads;
    get_heads(project, spec, spec_owner, heads);
    _num_heads = heads.size();
    parent_map rosters;
    get_rosters(project, heads, rosters);
    roster_t roster;
    parent_result res = try_merge_parents(project, rosters, roster);
    if (res == parent_fail)
      return false;

    policy_from_roster(project, roster, my_policy);
    my_policy.set_parent(spec_owner);
    return true;
  }
  bool policy_branch::try_get_policy(policy & pol) const
  {
    if (!loaded)
      return false;
    pol = my_policy;
    return true;
  }
  void policy_branch::get_policy(policy & pol, origin::type ty) const
  {
    E(try_get_policy(pol), ty,
      F("cannot sanely combine %d heads of policy")
      % _num_heads);
  }

  namespace {
    class content_putter
    {
      roster_t & roster;
      dir_t dir;
      map<file_id, file_data> & files;
      node_id_source & source;
    public:
      content_putter(roster_t & ros, file_path const & dir_name,
                     map<file_id, file_data> & files,
                     node_id_source & source)
        : roster(ros), files(files), source(source)
      {
        if (!roster.has_node(dir_name))
          {
            node_id n = roster.create_dir_node(source);
            roster.attach_node(n, dir_name);
          }
        node_t n = roster.get_node_for_update(dir_name);
        dir = downcast_to_dir_t(n);
      }
      void set(path_component const & cx, file_data const & dat)
      {
        file_id ident;
        calculate_ident(dat, ident);
        files.insert(make_pair(ident, dat));
        if (dir->has_child(cx))
          {
            file_id current = downcast_to_file_t(dir->get_child(cx))->content;
            if (current != ident)
              roster.set_content(dir->get_child(cx)->self, ident);
          }
        else
          {
            node_id n = roster.create_file_node(ident, source);
            roster.attach_node(n, dir->self, cx);
          }
      }
    };
  }

  void policy_branch::commit(project_t & project,
                             key_store & keys,
                             policy const & p,
                             utf8 const & changelog,
                             origin::type ty)
  {
    E(try_commit(project, keys, p, changelog), ty,
      F("cannot automatically merge %d heads of policy branch")
      % _num_heads);
  }
  bool policy_branch::try_commit(project_t & project,
                                 key_store & keys,
                                 policy const & p,
                                 utf8 const & changelog)
  {
    std::set<revision_id> heads;
    get_heads(project, spec, spec_owner, heads);
    parent_map parents;
    get_rosters(project, heads, parents);

    roster_t new_roster;
    parent_result res = try_merge_parents(project, parents, new_roster);
    if (res != parent_clean)
      return false;

    temp_node_id_source source;

    if (!new_roster.has_root())
      {
        node_id n = new_roster.create_dir_node(source);
        new_roster.attach_node(n, file_path_internal(""));
      }
    map<file_id, file_data> files;

    policy::del_map const & p_delegations = p.list_delegations();
    content_putter del_putter(new_roster,
                              file_path_internal("delegations"),
                              files, source);
    for (policy::del_map::const_iterator i = p_delegations.begin();
         i != p_delegations.end(); ++i)
      {
        string x;
        i->second.serialize(x);
        del_putter.set(path_component(i->first, origin::internal),
                       file_data(x, origin::internal));
      }

    policy::key_map const & p_keys = p.list_keys();
    content_putter key_putter(new_roster,
                              file_path_internal("keys"),
                              files, source);
    for (policy::key_map::const_iterator i = p_keys.begin();
         i != p_keys.end(); ++i)
      {
        string x = encode_hexenc(i->second.inner()(), origin::internal);
        key_putter.set(path_component(i->first(), origin::internal),
                       file_data(x, origin::internal));
      }


    map<string, branch> const & p_branches = p.list_branches();
    content_putter branch_putter(new_roster,
                                 file_path_internal("branches"),
                                 files, source);
    for (map<string, branch>::const_iterator i = p_branches.begin();
         i != p_branches.end(); ++i)
      {
        string x;
        i->second.serialize(x);
        branch_putter.set(path_component(i->first, origin::internal),
                          file_data(x, origin::internal));
      }


    map<string, revision_id> const & p_tags = p.list_tags();
    content_putter tag_putter(new_roster,
                              file_path_internal("tags"),
                              files, source);
    for (map<string, revision_id>::const_iterator i = p_tags.begin();
         i != p_tags.end(); ++i)
      {
        string x = encode_hexenc(i->second.inner()(), origin::internal);
        tag_putter.set(path_component(i->first, origin::internal),
                       file_data(x, origin::internal));
      }


    revision_t rev;
    make_revision(parents, new_roster, rev);
    rev.made_for = made_for_database;
    revision_id revid;
    calculate_ident(rev, revid);

    string author = encode_hexenc(keys.signing_key.inner()(), origin::internal);
    transaction_guard guard(project.db);
    for (map<file_id, file_data>::const_iterator f = files.begin();
         f != files.end(); ++f)
      {
        project.db.put_file(f->first, f->second);
      }
    project.db.put_revision(revid, rev);
    project.put_standard_certs(keys, revid,
                               spec.get_uid(),
                               changelog,
                               date_t::now(),
                               author);
    guard.commit();
    return true;
  }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
