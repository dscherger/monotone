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
#include "lexical_cast.hh"
#include "policies/editable_policy.hh"
#include "project.hh"
#include "roster.hh"
#include "transforms.hh"
#include "vocab_cast.hh"

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
        policies.insert(policy_from_revision(project, spec_owner, *i));
      }
  }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
