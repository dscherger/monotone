// Copyright 2008 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.

#include "base.hh"
#include "editable_policy.hh"

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/key_extractors.hpp>

#include <map>
#include <vector>

#include "basic_io.hh"
#include "botan/botan.h"
#include "database.hh"
#include "key_store.hh"
#include "outdated_indicator.hh"
#include "paths.hh"
#include "policy.hh"
#include "revision.hh"
#include "roster.hh"
#include "transforms.hh"

using std::string;
using std::set;
using std::map;
using std::make_pair;
using std::vector;

using boost::shared_ptr;

using boost::multi_index::multi_index_container;
using boost::multi_index::indexed_by;
using boost::multi_index::ordered_non_unique;
using boost::multi_index::tag;
using boost::multi_index::member;


namespace basic_io
{
  namespace syms
  {
    symbol const branch_uid("branch_uid");
    symbol const committer("committer");
    symbol const revision_id("revision_id");
  }
}

bool
operator == (editable_policy::delegation const & lhs,
             editable_policy::delegation const & rhs)
{
  return lhs.rev == rhs.rev
    && lhs.uid == rhs.uid
    && lhs.committers == rhs.committers;
}

namespace {
  branch_uid
  generate_uid()
  {
    // FIXME: I'm sure there's a better way to do this.
    std::string when = date_t::now().as_iso_8601_extended();
    char buf[20];
    Botan::Global_RNG::randomize(reinterpret_cast<Botan::byte*>(buf), 20);
    return branch_uid(when + "--" + encode_hexenc(std::string(buf, 20)));
  }

  template<typename Value>
  struct thing_info
  {
    typedef Value item_type;
    string old_name;
    string new_name;
    //Value old_value;
    shared_ptr<Value> new_value;
  };

  //struct original {};
  struct current {};

  template<typename Value>
  struct thing_holder
  {
    typedef thing_info<Value> info_type;
    typedef multi_index_container<
      info_type,
      indexed_by<
        ordered_non_unique<
          tag<current>,
          member<info_type, string, &info_type::new_name>
          >/*,
        ordered_non_unique<
          tag<original>,
          member<info_type, string, &info_type::old_name>
          >*/
        >
      > container;
    class rename
    {
      string new_name;
    public:
      rename(string const & nn) : new_name(nn) {}
      void operator()(info_type & it) { it.new_name = new_name; }
    };
    //typedef typename container::template index<original>::type by_original;
    typedef typename container::template index<current>::type by_current;
  };
  typedef thing_holder<editable_policy::tag> tag_holder;
  typedef thing_holder<editable_policy::branch> branch_holder;
  typedef thing_holder<editable_policy::delegation> delegation_holder;


  outdated_indicator_factory never_outdated;
}

class editable_policy_impl
{
public:
  database & db;
  outdated_indicator indicator;
  editable_policy_impl(database & db)
    : db(db)
  {}
  editable_policy_impl(editable_policy_impl const & other)
    : db(other.db), old_rev_id(other.old_rev_id),
      old_roster(other.old_roster),
      files(other.files),
      new_revision(other.new_revision)
  {
    for (tag_holder::container::const_iterator i = other.tags.begin();
         i != other.tags.end(); ++i)
      {
        tag_holder::info_type x = *i;
        x.new_value.reset(new editable_policy::tag(*x.new_value));
        tags.insert(x);
      }
    for (branch_holder::container::const_iterator i = other.branches.begin();
         i != other.branches.end(); ++i)
      {
        branch_holder::info_type x = *i;
        x.new_value.reset(new editable_policy::branch(*x.new_value));
        branches.insert(x);
      }
    for (delegation_holder::container::const_iterator i = other.delegations.begin();
         i != other.delegations.end(); ++i)
      {
        delegation_holder::info_type x = *i;
        x.new_value.reset(new editable_policy::delegation(*x.new_value));
        delegations.insert(x);
      }
  }

  revision_id old_rev_id;
  roster_t old_roster;

  map<file_id, file_data> files;
  revision_data new_revision;

  tag_holder::container tags;
  branch_holder::container branches;
  delegation_holder::container delegations;
};

editable_policy::editable_policy(database & db,
                                 set<rsa_keypair_id> const & admins)
  : impl(new editable_policy_impl(db)), uid(generate_uid())
{
  branch_holder::info_type self;
  self.new_name = "__policy__";
  self.new_value.reset(new branch());
  self.new_value->uid = uid;
  self.new_value->committers = admins;
  impl->branches.insert(self);
  impl->indicator = never_outdated.get_indicator();
}

namespace {
  template<typename T> void
  load_itemtype(T & cont,
                roster_t const & ros,
                file_path const & dir_name,
                database & db)
  {
    if (!ros.has_node(dir_name))
      return;
    node_t n = ros.get_node(dir_name);
    if (!is_dir_t(n))
      return;
    dir_t dir = downcast_to_dir_t(n);
    for (dir_map::const_iterator i = dir->children.begin();
         i != dir->children.end(); ++i)
      {
        if (!is_file_t(i->second))
          continue;
        file_t item = downcast_to_file_t(i->second);
        file_data fdat;
        db.get_file_version(item->content, fdat);
        typename T::value_type info;
        info.old_name = i->first();
        info.new_name = i->first();
        info.new_value.reset(new typename T::value_type::item_type());
        info.new_value->read(fdat.inner());
        cont.insert(info);
      }
  }
  void
  load_policy(shared_ptr<editable_policy_impl> impl)
  {
    load_itemtype(impl->tags,
                  impl->old_roster,
                  file_path_internal("tags"),
                  impl->db);
    load_itemtype(impl->branches,
                  impl->old_roster,
                  file_path_internal("branches"),
                  impl->db);
    load_itemtype(impl->delegations,
                  impl->old_roster,
                  file_path_internal("delegations"),
                  impl->db);
  }
}

editable_policy::editable_policy(database & db,
                                 revision_id const & rev)
  : impl(new editable_policy_impl(db))
{
  init(rev);
}

editable_policy::editable_policy(database & db,
                                 editable_policy::delegation const & del)
  : impl(new editable_policy_impl(db))
{
  if (null_id(del.rev))
    {
      branch br;
      br.uid = del.uid;
      br.committers = del.committers;
      init(br);
    }
  else
    init(del.rev);
}

editable_policy::editable_policy()
{ }

editable_policy::editable_policy(editable_policy const & other)
  : impl(new editable_policy_impl(*other.impl)), uid(other.uid)
{
}

editable_policy const &
editable_policy::operator = (editable_policy const & other)
{
  impl.reset(new editable_policy_impl(*other.impl));
  uid = other.uid;
  return *this;
}

bool
editable_policy::outdated() const
{
  return impl->indicator.outdated();
}

void
editable_policy::init(revision_id const & rev)
{
  vector<revision<cert> > certs;
  impl->db.get_revision_certs(rev, branch_cert_name, certs);
  erase_bogus_certs(impl->db, certs);
  if (certs.size() == 1)
    {
      uid = branch_uid(idx(certs,0).inner().value());
    }

  impl->old_rev_id = rev;
  impl->db.get_roster(rev, impl->old_roster);
  load_policy(impl);
  impl->indicator = never_outdated.get_indicator();
}

void
editable_policy::init(editable_policy::branch const & br)
{
  uid = br.uid;
  set<revision_id> heads;
  impl->indicator = get_branch_heads(br, false, impl->db, heads, NULL);
  E(heads.size() <= 1,
    F("Policy branch %s has too many heads") % uid);
  if (heads.empty())
    {
      W(F("Cannot find policy %s, loading an empty policy") % br.uid);
      branch_holder::info_type self;
      self.new_name = "__policy__";
      self.new_value.reset(new branch(br));
      impl->branches.insert(self);
    }
  else
    {
      impl->old_rev_id = *heads.begin();
      impl->db.get_roster(impl->old_rev_id, impl->old_roster);
      load_policy(impl);
    }
}


void
editable_policy::tag::write(data & dat)
{
  basic_io::printer printer;
  basic_io::stanza st;
  hexenc<id> enc;
  encode_hexenc(rev.inner(), enc);
  st.push_hex_pair(basic_io::syms::revision_id, enc);
  printer.print_stanza(st);
  dat = data(printer.buf);
}

void
editable_policy::tag::read(data const & dat)
{
  basic_io::input_source src(dat(), "tag");
  basic_io::tokenizer tok(src);
  basic_io::parser pa(tok);
  while (pa.symp())
    {
      if (pa.symp(basic_io::syms::revision_id))
        {
          pa.sym();
          string rid;
          pa.hex(rid);
          rev = revision_id(decode_hexenc(rid));
        }
      else
        {
          E(false, F("bad tag spec"));
        }
    }
  I(src.lookahead == EOF);
}

void
editable_policy::branch::write(data & dat)
{
  basic_io::printer printer;
  basic_io::stanza st;
  st.push_str_pair(basic_io::syms::branch_uid, uid());
  for (std::set<rsa_keypair_id>::const_iterator i = committers.begin();
       i != committers.end(); ++i)
    {
      st.push_str_pair(basic_io::syms::committer, (*i)());
    }
  printer.print_stanza(st);
  dat = data(printer.buf);
}

void
editable_policy::branch::read(data const & dat)
{
  basic_io::input_source src(dat(), "policy spec");
  basic_io::tokenizer tok(src);
  basic_io::parser pa(tok);

  while (pa.symp())
    {
      if(pa.symp(basic_io::syms::branch_uid))
        {
          pa.sym();
          string branch;
          pa.str(branch);
          uid = branch_uid(branch);
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
          E(false, F("Unable to understand delegation"));
        }
    }
}

void
editable_policy::delegation::write(data & dat)
{
  basic_io::printer printer;
  basic_io::stanza st;

  // must have only one or the other
  I(uid().empty() != null_id(rev));
  if (null_id(rev))
    {
      st.push_str_pair(basic_io::syms::branch_uid, uid());
      for (std::set<rsa_keypair_id>::const_iterator i = committers.begin();
           i != committers.end(); ++i)
        {
          st.push_str_pair(basic_io::syms::committer, (*i)());
        }
    }
  else
    {
      hexenc<id> enc;
      encode_hexenc(rev.inner(), enc);
      st.push_hex_pair(basic_io::syms::revision_id, enc);
    }

  printer.print_stanza(st);
  dat = data(printer.buf);
}

void
editable_policy::delegation::read(data const & dat)
{
  bool seen_revid = false;
  bool seen_branchspec = false;

  basic_io::input_source src(dat(), "policy spec");
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
          uid = branch_uid(branch);
        }
      else if (pa.symp(basic_io::syms::committer))
        {
          seen_branchspec = true;
          pa.sym();
          string key;
          pa.str(key);
          committers.insert(rsa_keypair_id(key));
        }
      else if (pa.symp(basic_io::syms::revision_id))
        {
          seen_revid = true;
          pa.sym();
          string rid;
          pa.hex(rid);
          rev = revision_id(decode_hexenc(rid));
        }
      else
        {
          E(false, F("Unable to understand delegation"));
        }
    }

  I(src.lookahead == EOF);

  E(seen_revid || seen_branchspec,
    F("Delegation file seems to be empty"));

  E(seen_revid != seen_branchspec,
    F("Delegation file contains both a revision id and a branch spec"));
}

namespace {
  template<typename T>
  void extract_changes(T const & cont,
                       roster_t const & old_roster,
                       cset & changes,
                       map<file_id, file_data> & files,
                       string const & name)
  {
    file_path dir = file_path_internal(name);
    bool have_dir = old_roster.has_node(dir);
    for (typename T::const_iterator i = cont.begin();
         i != cont.end(); ++i)
      {
        file_path old_path;
        if (!i->old_name.empty())
          old_path = dir / path_component(i->old_name);
        file_path new_path;
        if (!i->new_name.empty())
          {
            new_path = dir / path_component(i->new_name);
            if (!have_dir)
              {
                have_dir = true;
                changes.dirs_added.insert(dir);
              }
          }
        file_data new_fdat;
        file_id new_fid;
        file_id old_fid;
        if (!i->new_name.empty())
          {
            if (!i->old_name.empty())
              {
                node_t n = old_roster.get_node(old_path);
                file_t f = downcast_to_file_t(n);
                old_fid = f->content;
              }
            data dat;
            i->new_value->write(dat);
            new_fdat = file_data(dat);
            calculate_ident(new_fdat, new_fid);
            files.insert(make_pair(new_fid, new_fdat));
          }
        
        if (i->new_name.empty())
          changes.nodes_deleted.insert(old_path);
        else if (i->old_name.empty())
          changes.files_added.insert(make_pair(new_path, new_fid));
        else
          {
            if (i->old_name != i->new_name)
              changes.nodes_renamed.insert(make_pair(old_path, new_path));
            if (new_fid != old_fid)
              changes.deltas_applied.insert(make_pair(new_path,
                                                      make_pair(old_fid,
                                                                new_fid)));
          }
      }
  }
}

revision_id
editable_policy::calculate_id()
{
  impl->files.clear();
  cset changes;
  if (!impl->old_roster.has_root())
    changes.dirs_added.insert(file_path_internal(""));

  extract_changes(impl->tags, impl->old_roster,
                  changes, impl->files, "tags");
  extract_changes(impl->branches, impl->old_roster,
                  changes, impl->files, "branches");
  extract_changes(impl->delegations, impl->old_roster,
                  changes, impl->files, "delegations");

  revision_t rev;
  make_revision(impl->old_rev_id, impl->old_roster,
                changes, rev);
  write_revision(rev, impl->new_revision);
  revision_id rid;
  calculate_ident(impl->new_revision, rid);
  return rid;
}

revision_id
editable_policy::commit(key_store & keys,
                        utf8 const & changelog,
                        string author)
{
  revision_id new_id = calculate_id();
  transaction_guard guard(impl->db);

  for (map<file_id, file_data>::const_iterator i = impl->files.begin();
       i != impl->files.end(); ++i)
    {
      if (!impl->db.file_version_exists(i->first))
        impl->db.put_file(i->first, i->second);//FIXME
    }
  impl->db.put_revision(new_id, impl->new_revision);

  if (author.empty())
    author = keys.signing_key();
  cert_revision_date_time(impl->db, keys, new_id, date_t::now());
  cert_revision_changelog(impl->db, keys, new_id, changelog);
  cert_revision_author(impl->db, keys, new_id, author);
  cert_revision_in_branch(impl->db, keys, new_id, uid);

  guard.commit();
  return new_id;
}

void
editable_policy::get_spec(data & dat)
{
  shared_ptr<branch> spec = get_branch("__policy__");
  spec->write(dat);
}


void
editable_policy::remove_delegation(string const & name)
{
  delegation_holder::by_current::iterator i = impl->delegations.find(name);
  if (i != impl->delegations.end())
    impl->delegations.erase(i);
}

void
editable_policy::remove_branch(string const & name)
{
  branch_holder::by_current::iterator i = impl->branches.find(name);
  if (i != impl->branches.end())
    impl->branches.erase(i);
}

void
editable_policy::remove_tag(string const & name)
{
  tag_holder::by_current::iterator i = impl->tags.find(name);
  if (i != impl->tags.end())
    impl->tags.erase(i);
}


void
editable_policy::rename_delegation(string const & from,
                                   string const & to)
{
  delegation_holder::by_current::iterator i = impl->delegations.find(from);
  if (i != impl->delegations.end())
    {
      if (impl->delegations.find(to) != impl->delegations.end())
        {
          impl->delegations.modify(i, delegation_holder::rename(to));
        }
    }
}

void
editable_policy::rename_branch(string const & from,
                               string const & to)
{
  branch_holder::by_current::iterator i = impl->branches.find(from);
  if (i != impl->branches.end())
    {
      if (impl->branches.find(to) != impl->branches.end())
        {
          impl->branches.modify(i, branch_holder::rename(to));
        }
    }
}

void
editable_policy::rename_tag(string const & from,
                            string const & to)
{
  tag_holder::by_current::iterator i = impl->tags.find(from);
  if (i != impl->tags.end())
    {
      if (impl->tags.find(to) != impl->tags.end())
        {
          impl->tags.modify(i, tag_holder::rename(to));
        }
    }
}


editable_policy::delegation_t
editable_policy::get_delegation(string const & name, bool create)
{
  delegation_holder::by_current::iterator i = impl->delegations.find(name);
  if (i != impl->delegations.end())
    return i->new_value;
  if (!create)
    return shared_ptr<delegation>();
  delegation_holder::info_type item;
  item.new_name = name;
  item.new_value.reset(new delegation());
  impl->delegations.insert(item);
  return item.new_value;
}

editable_policy::branch_t
editable_policy::get_branch(string const & name, bool create)
{
  branch_holder::by_current::iterator i = impl->branches.find(name);
  if (i != impl->branches.end())
    return i->new_value;
  if (!create)
    return shared_ptr<branch>();
  branch_holder::info_type item;
  item.new_name = name;
  item.new_value.reset(new branch());
  item.new_value->uid = generate_uid();
  impl->branches.insert(item);
  return item.new_value;
}

editable_policy::tag_t
editable_policy::get_tag(string const & name, bool create)
{
  tag_holder::by_current::iterator i = impl->tags.find(name);
  if (i != impl->tags.end())
    return i->new_value;
  if (!create)
    return shared_ptr<tag>();
  tag_holder::info_type item;
  item.new_name = name;
  item.new_value.reset(new tag());
  impl->tags.insert(item);
  return item.new_value;
}


editable_policy::delegation_map
editable_policy::get_all_delegations()
{
  delegation_map ret;
  for (delegation_holder::by_current::iterator i = impl->delegations.begin();
       i != impl->delegations.end(); ++i)
    {
      ret.insert(make_pair(i->new_name, i->new_value));
    }
  return ret;
}

editable_policy::const_delegation_map
editable_policy::get_all_delegations() const
{
  const_delegation_map ret;
  for (delegation_holder::by_current::iterator i = impl->delegations.begin();
       i != impl->delegations.end(); ++i)
    {
      ret.insert(make_pair(i->new_name, const_delegation_t(i->new_value)));
    }
  return ret;
}

editable_policy::branch_map
editable_policy::get_all_branches()
{
  branch_map ret;
  for (branch_holder::by_current::iterator i = impl->branches.begin();
       i != impl->branches.end(); ++i)
    {
      ret.insert(make_pair(i->new_name, i->new_value));
    }
  return ret;
}

editable_policy::const_branch_map
editable_policy::get_all_branches() const
{
  const_branch_map ret;
  for (branch_holder::by_current::iterator i = impl->branches.begin();
       i != impl->branches.end(); ++i)
    {
      ret.insert(make_pair(i->new_name, const_branch_t(i->new_value)));
    }
  return ret;
}

editable_policy::tag_map
editable_policy::get_all_tags()
{
  tag_map ret;
  for (tag_holder::by_current::iterator i = impl->tags.begin();
       i != impl->tags.end(); ++i)
    {
      ret.insert(make_pair(i->new_name, i->new_value));
    }
  return ret;
}

editable_policy::const_tag_map
editable_policy::get_all_tags() const
{
  const_tag_map ret;
  for (tag_holder::by_current::iterator i = impl->tags.begin();
       i != impl->tags.end(); ++i)
    {
      ret.insert(make_pair(i->new_name, const_tag_t(i->new_value)));
    }
  return ret;
}
