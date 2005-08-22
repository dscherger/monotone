// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this is how you "ask for" the C99 constant constructor macros.  *and*
// you have to do so before any other files accidentally include
// stdint.h. awesome.
#define __STDC_CONSTANT_MACROS

#include <algorithm>
#include <iterator>
#include <iostream>
#include <list>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/dynamic_bitset.hpp>

#include "basic_io.hh"
#include "change_set.hh"
#include "constants.hh"
#include "diff_patch.hh"
#include "file_io.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "smap.hh"
#include "path_component.hh"

#include "pcdv.hh"
#include "database.hh"
#include "revision.hh"

// our analyses in this file happen on one of two families of
// related structures: a path_analysis or a directory_map.
//
// a path_analysis corresponds exactly to a normalized 
// path_rearrangement; they are two ways of writing the
// same information
//
// the path_analysis stores two path_states. each path_state is a map from
// transient identifiers (tids) to items. each item represents a semantic
// element of a filesystem which has a type (file or directory), a name,
// and a parent link (another tid). tids should be unique across a
// path_analysis.

typedef enum { ptype_directory, ptype_file } ptype;
typedef u32 tid;
static tid root_tid = 0;

struct
tid_source
{
  tid ctr;
  tid_source() : ctr(root_tid + 1) {}
  tid next() { I(ctr != UINT32_C(0xffffffff)); return ctr++; }
};

struct
path_item
{
  tid parent;
  ptype ty;
  path_component name;      
  inline path_item() {}
  inline path_item(tid p, ptype t, path_component n);
  inline path_item(path_item const & other);
  inline path_item const & operator=(path_item const & other);
  inline bool operator==(path_item const & other) const;
};


template<typename T> struct identity 
{
  size_t operator()(T const & v) const 
  { 
    return static_cast<size_t>(v);
  }
};

typedef smap<tid, path_item> path_state;
typedef smap<tid, tid> state_renumbering;
typedef std::pair<path_state, path_state> path_analysis;

// nulls and tests

static file_id null_ident;

// a directory_map is a more "normal" representation of a directory tree,
// which you can traverse more conveniently from root to tip
//
//     tid ->  [ name -> (ptype, tid),
//               name -> (ptype, tid),
//               ...                  ]
//
//     tid ->  [ name -> (ptype, tid),
//               name -> (ptype, tid),
//               ...                  ]

// There is a bug in handling directory_nodes; the problem is that it is legal
// to have multiple files named "", and, worse, to have both a file named ""
// and a directory named "".  Currently, we make this a map instead of an
// smap, so whichever ""-named file is added first simply wins, and the rest
// are ignored.  FIXME: teach the code that uses directory_map's not to expect
// there to be any entry at all for ""-named files.
typedef std::map< path_component, std::pair<ptype,tid> > directory_node;

typedef smap<tid, boost::shared_ptr<directory_node> > directory_map;

static path_component
directory_entry_name(directory_node::const_iterator const & i)
{
  return i->first;
}

static ptype
directory_entry_type(directory_node::const_iterator const & i)
{
  return i->second.first;
}

static tid
directory_entry_tid(directory_node::const_iterator const & i)
{
  return i->second.second;
}

void 
change_set::add_file(file_path const & a)
{
  I(rearrangement.added_files.find(a) == rearrangement.added_files.end());
  rearrangement.added_files.insert(a);
}

void 
change_set::add_file(file_path const & a, file_id const & ident)
{
  I(rearrangement.added_files.find(a) == rearrangement.added_files.end());
  I(deltas.find(a) == deltas.end());
  rearrangement.added_files.insert(a);
  deltas.insert(std::make_pair(a, std::make_pair(null_ident, ident)));
}

void 
change_set::apply_delta(file_path const & path, 
                        file_id const & src, 
                        file_id const & dst)
{
  I(deltas.find(path) == deltas.end());
  deltas.insert(std::make_pair(path, std::make_pair(src, dst)));
}

void 
change_set::delete_file(file_path const & d)
{
  I(rearrangement.deleted_files.find(d) == rearrangement.deleted_files.end());
  rearrangement.deleted_files.insert(d);
}

void 
change_set::delete_dir(file_path const & d)
{
  I(rearrangement.deleted_dirs.find(d) == rearrangement.deleted_dirs.end());
  rearrangement.deleted_dirs.insert(d);
}

void 
change_set::rename_file(file_path const & a, file_path const & b)
{
  I(rearrangement.renamed_files.find(a) == rearrangement.renamed_files.end());
  rearrangement.renamed_files.insert(std::make_pair(a,b));
}

void 
change_set::rename_dir(file_path const & a, file_path const & b)
{
  I(rearrangement.renamed_dirs.find(a) == rearrangement.renamed_dirs.end());
  rearrangement.renamed_dirs.insert(std::make_pair(a,b));
}


bool 
change_set::path_rearrangement::operator==(path_rearrangement const & other) const
{
  return deleted_files == other.deleted_files &&
    deleted_dirs == other.deleted_dirs &&
    renamed_files == other.renamed_files &&
    renamed_dirs == other.renamed_dirs &&
    added_files == other.added_files;
}

bool 
change_set::path_rearrangement::empty() const
{
  return deleted_files.empty() &&
    deleted_dirs.empty() &&
    renamed_files.empty() &&
    renamed_dirs.empty() &&
    added_files.empty();
}

bool
change_set::path_rearrangement::has_added_file(file_path const & file) const
{
  return added_files.find(file) != added_files.end();
}

bool
change_set::path_rearrangement::has_deleted_file(file_path const & file) const
{
  return deleted_files.find(file) != deleted_files.end();
}

bool
change_set::path_rearrangement::has_renamed_file_dst(file_path const & file) const
{
  // FIXME: this is inefficient, but improvements would require a different
  // structure for renamed_files (or perhaps a second reverse map). For now
  // we'll assume that few files will be renamed per changeset.
  for (std::map<file_path,file_path>::const_iterator rf = renamed_files.begin();
       rf != renamed_files.end(); ++rf)
    if (rf->second == file)
      return true;
  return false;
}

bool
change_set::path_rearrangement::has_renamed_file_src(file_path const & file) const
{
  return renamed_files.find(file) != renamed_files.end();
}

bool 
change_set::empty() const
{
  return deltas.empty() && rearrangement.empty();
}

bool 
change_set::operator==(change_set const & other) const
{
  return rearrangement == other.rearrangement &&
    deltas == other.deltas;    
}


// simple accessors

inline tid const & 
path_item_parent(path_item const & p) 
{ 
  return p.parent; 
}

inline ptype const & 
path_item_type(path_item const & p) 
{ 
  return p.ty; 
}

inline path_component
path_item_name(path_item const & p) 
{ 
  return p.name; 
}

inline tid
path_state_tid(path_state::const_iterator i)
{
  return i->first;
}

inline path_item const &
path_state_item(path_state::const_iterator i)
{
  return i->second;
}



void
dump(path_state const & st, std::string & out)
{
  for (path_state::const_iterator i = st.begin();
       i != st.end(); ++i)
    {
      std::vector<path_component> tmp_v;
      tmp_v.push_back(path_item_name(path_state_item(i)));
      file_path tmp_fp;
      compose_path(tmp_v, tmp_fp);
      out += (F("tid %d: parent %d, type %s, name %s\n")
              % path_state_tid(i) 
              % path_item_parent(path_state_item(i))
              % (path_item_type(path_state_item(i)) == ptype_directory ? "dir" : "file")
              % tmp_fp).str();
    }
}

void
dump(path_analysis const & analysis, std::string & out)
{
  out = "pre-state:\n";
  std::string tmp;
  dump(analysis.first, tmp);
  out += tmp;
  out += "post-state:\n";
  tmp.clear();
  dump(analysis.second, tmp);
  out += tmp;
}

void
dump(state_renumbering const & r, std::string & out)
{
  for (state_renumbering::const_iterator i = r.begin();
       i != r.end(); ++i)
    out += (F("%d -> %d\n") % i->first % i->second).str();
}

// structure dumping 
/*

static void
dump_renumbering(std::string const & s,
                 state_renumbering const & r)
{
  L(F("BEGIN dumping renumbering '%s'\n") % s);
  for (state_renumbering::const_iterator i = r.begin();
       i != r.end(); ++i)
    {
      L(F("%d -> %d\n") % i->first % i->second);
    }
  L(F("END dumping renumbering '%s'\n") % s);
}

static void
dump_state(std::string const & s,
           path_state const & st)
{
  L(F("BEGIN dumping state '%s'\n") % s);
  for (path_state::const_iterator i = st.begin();
       i != st.end(); ++i)
    {
      std::vector<path_component> tmp_v;
      tmp_v.push_back(path_item_name(path_state_item(i)));
      file_path tmp_fp;
      compose_path(tmp_v, tmp_fp);
      L(F("state '%s': tid %d, parent %d, type %s, name %s\n")
        % s
        % path_state_tid(i) 
        % path_item_parent(path_state_item(i))
        % (path_item_type(path_state_item(i)) == ptype_directory ? "dir" : "file")
        % tmp_fp);
    }
  L(F("END dumping state '%s'\n") % s);
}

static void
dump_analysis(std::string const & s,
              path_analysis const & t)
{
  L(F("BEGIN dumping tree '%s'\n") % s);
  dump_state(s + " first", t.first);
  dump_state(s + " second", t.second);
  L(F("END dumping tree '%s'\n") % s);
}

*/


//  sanity checking 

static void 
check_sets_disjoint(std::set<file_path> const & a,
                    std::set<file_path> const & b)
{
  std::set<file_path> isect;
  std::set_intersection(a.begin(), a.end(),
                        b.begin(), b.end(),
                        std::inserter(isect, isect.begin()));
  if (!global_sanity.relaxed)
    {
      I(isect.empty());
    }
}

change_set::path_rearrangement::path_rearrangement(path_rearrangement const & other)
{
  other.check_sane();
  deleted_files = other.deleted_files;
  deleted_dirs = other.deleted_dirs;
  renamed_files = other.renamed_files;
  renamed_dirs = other.renamed_dirs;
  added_files = other.added_files;
}

change_set::path_rearrangement const &
change_set::path_rearrangement::operator=(path_rearrangement const & other)
{
  other.check_sane();
  deleted_files = other.deleted_files;
  deleted_dirs = other.deleted_dirs;
  renamed_files = other.renamed_files;
  renamed_dirs = other.renamed_dirs;
  added_files = other.added_files;
  return *this;
}

static void
extract_pairs_and_insert(std::map<file_path, file_path> const & in,
                         std::set<file_path> & firsts,
                         std::set<file_path> & seconds)
{
  for (std::map<file_path, file_path>::const_iterator i = in.begin();
       i != in.end(); ++i)
    {
      firsts.insert(i->first);
      seconds.insert(i->second);
    }
}

template <typename A, typename B>
static void
extract_first(std::map<A, B> const & m, std::set<A> & s)
{
  s.clear();
  for (typename std::map<A, B>::const_iterator i = m.begin();
       i != m.end(); ++i)
    {
      s.insert(i->first);
    }
}

static void
extract_killed(path_analysis const & a,
               std::set<file_path> & killed);


static void
check_no_deltas_on_killed_files(path_analysis const & pa,
                                change_set::delta_map const & del)
{
  std::set<file_path> killed;
  std::set<file_path> delta_paths;

  extract_killed(pa, killed);
  extract_first(del, delta_paths);
  check_sets_disjoint(killed, delta_paths);
}

static void
check_delta_entries_not_directories(path_analysis const & pa,
                                    change_set::delta_map const & dels);

void 
analyze_rearrangement(change_set::path_rearrangement const & pr,
                      path_analysis & pa,
                      tid_source & ts);

void
sanity_check_path_analysis(path_analysis const & pr);

void 
change_set::path_rearrangement::check_sane() const
{
  delta_map del;
  this->check_sane(del);
}

void 
change_set::path_rearrangement::check_sane(delta_map const & deltas) const
{
  tid_source ts;
  path_analysis pa;
  analyze_rearrangement(*this, pa, ts);
  sanity_check_path_analysis (pa);

  check_no_deltas_on_killed_files(pa, deltas);
  check_delta_entries_not_directories(pa, deltas);

  // FIXME: extend this as you manage to think of more invariants
  // which are cheap enough to check at this level.
  std::set<file_path> renamed_srcs, renamed_dsts;
  extract_pairs_and_insert(renamed_files, renamed_srcs, renamed_dsts);
  extract_pairs_and_insert(renamed_dirs, renamed_srcs, renamed_dsts);

  // Files cannot be split nor joined by renames.
  I(renamed_files.size() + renamed_dirs.size() == renamed_srcs.size());
  I(renamed_files.size() + renamed_dirs.size() == renamed_dsts.size());

  check_sets_disjoint(deleted_files, deleted_dirs);
  check_sets_disjoint(deleted_files, renamed_srcs);
  check_sets_disjoint(deleted_dirs, renamed_srcs);

  check_sets_disjoint(added_files, renamed_dsts);
}

change_set::change_set(change_set const & other)
{
  other.check_sane();
  rearrangement = other.rearrangement;
  deltas = other.deltas;
}

change_set const &change_set::operator=(change_set const & other)
{
  other.check_sane();
  rearrangement = other.rearrangement;
  deltas = other.deltas;
  return *this;
}

void 
change_set::check_sane() const
{
  // FIXME: extend this as you manage to think of more invariants
  // which are cheap enough to check at this level.
  MM(*this);

  rearrangement.check_sane(this->deltas);

  for (std::set<file_path>::const_iterator i = rearrangement.added_files.begin(); 
       i != rearrangement.added_files.end(); ++i)
    {
      delta_map::const_iterator j = deltas.find(*i);
      if (!global_sanity.relaxed)
        {
          I(j != deltas.end());
          I(null_id(delta_entry_src(j)));
          I(!null_id(delta_entry_dst(j)));
        }
    }

  for (delta_map::const_iterator i = deltas.begin(); 
       i != deltas.end(); ++i)
    {
      if (!global_sanity.relaxed)
        {
          I(!null_name(delta_entry_path(i)));
          I(!null_id(delta_entry_dst(i)));
          I(!(delta_entry_src(i) == delta_entry_dst(i)));
          if (null_id(delta_entry_src(i)))
            I(rearrangement.added_files.find(delta_entry_path(i))
              != rearrangement.added_files.end());
        }
    }

}

inline static void
sanity_check_path_item(path_item const & pi)
{
}

static void
confirm_proper_tree(path_state const & ps)
{
  if (ps.empty())
    return;

  I(ps.find(root_tid) == ps.end()); // Note that this find() also ensures
                                    // sortedness of ps.

  tid min_tid = ps.begin()->first;
  tid max_tid = ps.rbegin()->first;
  size_t tid_range = max_tid - min_tid + 1;
  
  boost::dynamic_bitset<> confirmed(tid_range);
  boost::dynamic_bitset<> ancbits(tid_range);
  std::vector<tid> ancs; // a set is more efficient, at least in normal
                      // trees where the number of ancestors is
                      // significantly less than tid_range
  tid curr;
  path_item item;

  for (path_state::const_iterator i = ps.begin(); i != ps.end(); ++i)
    {
      ancs.clear();
      ancbits.reset();
      curr = i->first;
      item = i->second;

      while (confirmed.test(curr - min_tid) == false)
        {             
          sanity_check_path_item(item);
          I(ancbits.test(curr-min_tid) == false);
          ancs.push_back(curr);
          ancbits.set(curr-min_tid);
          if (path_item_parent(item) == root_tid)
            break;
          else
            {
              curr = path_item_parent(item);
              path_state::const_iterator j = ps.find(curr);
              I(j != ps.end());

              // if we're null, our parent must also be null
              if (null_name(item.name))
                I(null_name(path_state_item(j).name));

              item = path_state_item(j);
              I(path_item_type(item) == ptype_directory);
            }
        }
      for (std::vector<tid>::const_iterator a = ancs.begin(); a != ancs.end(); a++)
        {
          confirmed.set(*a - min_tid);
        }
    }
}

static void
confirm_unique_entries_in_directories(path_state const & ps)
{
  std::vector<std::pair<tid,path_component> > entries;
  for (path_state::const_iterator i = ps.begin(); i != ps.end(); ++i)
    {
      if (null_name(path_item_name(i->second)))
        {
          I(path_item_parent(i->second) == root_tid);
          continue;
        }
          
      std::pair<tid,path_component> p = std::make_pair(path_item_parent(i->second), 
                                                       path_item_name(i->second));
      entries.push_back(p);
    }

  // Now we check that entries is unique
  if (entries.empty())
      return;

  std::sort(entries.begin(), entries.end());

  std::vector<std::pair<tid,path_component> >::const_iterator leader, lagged;
  leader = entries.begin();
  lagged = entries.begin();

  I(leader != entries.end());
  ++leader;
  while (leader != entries.end())
  {
    I(*leader != *lagged);
    ++leader;
    ++lagged;
  }
}

static void
sanity_check_path_state(path_state const & ps)
{
  MM(ps);
  confirm_proper_tree(ps);
  confirm_unique_entries_in_directories(ps);
}

path_item::path_item(tid p, ptype t, path_component n) 
  : parent(p), ty(t), name(n) 
{
  sanity_check_path_item(*this);
}

path_item::path_item(path_item const & other) 
  : parent(other.parent), ty(other.ty), name(other.name) 
{
  sanity_check_path_item(*this);
}

path_item const & path_item::operator=(path_item const & other)
{
  parent = other.parent;
  ty = other.ty;
  name = other.name;
  sanity_check_path_item(*this);
  return *this;
}

bool path_item::operator==(path_item const & other) const
{
  return this->parent == other.parent &&
    this->ty == other.ty &&
    this->name == other.name;
}


static void
check_states_agree(path_state const & p1,
                   path_state const & p2)
{
  path_analysis pa;
  pa.first = p1;
  pa.second = p2;
  // dump_analysis("agreement", pa);
  for (path_state::const_iterator i = p1.begin(); i != p1.end(); ++i)
    {
      path_state::const_iterator j = p2.find(i->first);
      I(j != p2.end());
      I(path_item_type(i->second) == path_item_type(j->second));
      //       I(! (null_name(path_item_name(i->second))
      //           &&
      //           null_name(path_item_name(j->second))));
    }
}

void
sanity_check_path_analysis(path_analysis const & pr)
{
  sanity_check_path_state(pr.first);
  sanity_check_path_state(pr.second);
  check_states_agree(pr.first, pr.second);
  check_states_agree(pr.second, pr.first);
}


// construction helpers

static boost::shared_ptr<directory_node>
new_dnode()
{
  return boost::shared_ptr<directory_node>(new directory_node());
}

static boost::shared_ptr<directory_node>
dnode(directory_map & dir, tid t)
{
  boost::shared_ptr<directory_node> node;
  directory_map::const_iterator dirent = dir.find(t);
  if (dirent == dir.end())
    {      
      node = new_dnode();
      dir.insert(std::make_pair(t, node));
    }
  else
    node = dirent->second;
  return node;
}

static void
get_full_path(path_state const & state,
              tid t,
              std::vector<path_component> & pth)
{
  std::vector<path_component> tmp;
  while(t != root_tid)
    {
      path_state::const_iterator i = state.find(t);
      I(i != state.end());
      tmp.push_back(path_item_name(i->second));
      t = path_item_parent(i->second);
    }
  pth.clear();
  std::copy(tmp.rbegin(), tmp.rend(), inserter(pth, pth.begin()));
}

static void
get_full_path(path_state const & state,
              tid t,
              file_path & pth)
{
  std::vector<path_component> tmp;
  get_full_path(state, t, tmp);
  // L(F("got %d-entry path for tid %d\n") % tmp.size() % t);
  compose_path(tmp, pth);
}

static void
clear_rearrangement(change_set::path_rearrangement & pr)
{
  pr.deleted_files.clear();
  pr.deleted_dirs.clear();
  pr.renamed_files.clear();
  pr.renamed_dirs.clear();
  pr.added_files.clear();
}

static void
clear_change_set(change_set & cs)
{
  clear_rearrangement(cs.rearrangement);
  cs.deltas.clear();
}

static void 
compose_rearrangement(path_analysis const & pa,
                      change_set::path_rearrangement & pr)
{
  clear_rearrangement(pr);

  for (path_state::const_iterator i = pa.first.begin();
       i != pa.first.end(); ++i)
    {      
      tid curr(path_state_tid(i));
      std::vector<path_component> old_name, new_name;
      file_path old_path, new_path;
     
      path_state::const_iterator j = pa.second.find(curr);
      I(j != pa.second.end());
      path_item old_item(path_state_item(i));
      path_item new_item(path_state_item(j));

      // compose names
      if (!null_name(path_item_name(old_item)))
        {
          get_full_path(pa.first, curr, old_name);
          compose_path(old_name, old_path);
        }

      if (!null_name(path_item_name(new_item)))      
        {
          get_full_path(pa.second, curr, new_name);
          compose_path(new_name, new_path);
        }

      if (old_path == new_path)
        {
          /*
          L(F("skipping preserved %s %d : '%s'\n")
            % (path_item_type(old_item) == ptype_directory ? "directory" : "file")
            % curr % old_path);
          */
          continue;
        }
      
      /*
      L(F("analyzing %s %d : '%s' -> '%s'\n")
        % (path_item_type(old_item) == ptype_directory ? "directory" : "file")
        % curr % old_path % new_path);
      */
      
      if (null_name(path_item_name(old_item)))
        {
          // an addition (which must be a file, not a directory)
          I(! null_name(path_item_name(new_item)));
          I(path_item_type(new_item) != ptype_directory);
          pr.added_files.insert(new_path);
        }
      else if (null_name(path_item_name(new_item)))
        {
          // a deletion
          I(! null_name(path_item_name(old_item)));
          switch (path_item_type(new_item))
            {
            case ptype_directory:
              pr.deleted_dirs.insert(old_path);
              break;
            case ptype_file:
              pr.deleted_files.insert(old_path);
              break;
            }     
        }
      else
        {
          // a generic rename
          switch (path_item_type(new_item))
            {
            case ptype_directory:
              pr.renamed_dirs.insert(std::make_pair(old_path, new_path));
              break;
            case ptype_file:
              pr.renamed_files.insert(std::make_pair(old_path, new_path));
              break;
            }
        }
    }
}



static bool
lookup_path(std::vector<path_component> const & pth,
            directory_map const & dir,
            tid & t)
{
  t = root_tid;
  for (std::vector<path_component>::const_iterator i = pth.begin();
       i != pth.end(); ++i)
    {
      directory_map::const_iterator dirent = dir.find(t);
      if (dirent != dir.end())
        {
          boost::shared_ptr<directory_node> node = dirent->second;
          directory_node::const_iterator entry = node->find(*i);
          if (entry == node->end())
            return false;
          t = directory_entry_tid(entry);
        }
      else
        return false;
    }
  return true;
}

static bool
lookup_path(file_path const & pth,
            directory_map const & dir,
            tid & t)
{
  std::vector<path_component> vec;
  split_path(pth, vec);
  return lookup_path(vec, dir, t);
}

static tid
ensure_entry(directory_map & dmap,
             path_state & state,             
             tid dir_tid,
             ptype entry_ty,
             path_component entry,
             tid_source & ts)
{
  I(! null_name(entry));

  if (dir_tid != root_tid)
    {
      path_state::const_iterator parent = state.find(dir_tid);      
      I( parent != state.end());

      // if our parent is null, we immediately become null too, and attach to
      // the root node (where all null entries reside)
      if (null_name(path_item_name(path_state_item(parent))))
        {
          tid new_tid = ts.next();
          state.insert(std::make_pair(new_tid, path_item(root_tid, entry_ty, make_null_component())));
          return new_tid;
        }        
    }

  boost::shared_ptr<directory_node> node = dnode(dmap, dir_tid);
  directory_node::const_iterator node_entry = node->find(entry);

  if (node_entry != node->end())
    {
      I(node_entry->second.first == entry_ty);
      return node_entry->second.second;
    }
  else
    {
      tid new_tid = ts.next();
      state.insert(std::make_pair(new_tid, path_item(dir_tid, entry_ty, entry)));
      node->insert(std::make_pair(entry, std::make_pair(entry_ty, new_tid)));
      return new_tid;
    }
}

static tid
ensure_dir_in_map (std::vector<path_component> pth,
                   directory_map & dmap,
                   path_state & state,
                   tid_source & ts)
{
  tid dir_tid = root_tid;
  for (std::vector<path_component>::const_iterator p = pth.begin();
       p != pth.end(); ++p)
    {
      dir_tid = ensure_entry(dmap, state, dir_tid, 
                             ptype_directory, *p, ts);
    }
  return dir_tid;
}

static tid
ensure_dir_in_map (file_path const & path,
                   directory_map & dmap,
                   path_state & state,
                   tid_source & ts)
{
  std::vector<path_component> components;
  split_path(path, components);
  return ensure_dir_in_map(components, dmap, state, ts);
}

static tid
ensure_file_in_map (file_path const & path,
                    directory_map & dmap,
                    path_state & state,
                    tid_source & ts)
{
  std::vector<path_component> prefix;  
  path_component leaf_path;
  split_path(path, prefix, leaf_path);
  
  I(! null_name(leaf_path));
  tid dir_tid = ensure_dir_in_map(prefix, dmap, state, ts);
  return ensure_entry(dmap, state, dir_tid, ptype_file, leaf_path, ts);
}

static void
ensure_entries_exist (path_state const & self_state,
                      directory_map & other_dmap,
                      path_state & other_state,
                      tid_source & ts)
{
  for (path_state::const_iterator i = self_state.begin(); 
       i != self_state.end(); ++i)
    {
      if (other_state.find(path_state_tid(i)) != other_state.end())
        continue;

      if (null_name(path_item_name(path_state_item(i))))
        continue;

      file_path full;
      get_full_path(self_state, path_state_tid(i), full);
      switch (path_item_type(path_state_item(i)))
        {
        case ptype_directory:
          ensure_dir_in_map(full, other_dmap, other_state, ts);
          break;

        case ptype_file:
          ensure_file_in_map(full, other_dmap, other_state, ts);
          break;
        }
    }
}


static void
apply_state_renumbering(state_renumbering const & renumbering,
                        path_state & state)
{
  sanity_check_path_state(state);  
  path_state tmp(state);
  state.clear();

  for (path_state::const_iterator i = tmp.begin(); i != tmp.end(); ++i)
    {
      path_item item = path_state_item(i);
      tid t = path_state_tid(i);

      state_renumbering::const_iterator j = renumbering.find(t);
      if (j != renumbering.end())
        t = j->second;

      j = renumbering.find(item.parent);
      if (j != renumbering.end())
        item.parent = j->second;

      state.insert(std::make_pair(t, item));
    }
  sanity_check_path_state(state);
}

static void
apply_state_renumbering(state_renumbering const & renumbering,
                        path_analysis & pa)
{
  apply_state_renumbering(renumbering, pa.first);
  apply_state_renumbering(renumbering, pa.second);
}
                        

// this takes a path in the path space defined by input_dir and rebuilds it
// in the path space defined by output_space, including any changes to
// parents in the path (rather than directly to the path leaf name).  it
// therefore *always* succeeds; sometimes it does nothing if there's no
// affected parent, but you always get a rebuilt path in the output space.

static void
reconstruct_path(file_path const & input,
                 directory_map const & input_dir,
                 path_state const & output_space,
                 file_path & output)
{
  std::vector<path_component> vec;
  std::vector<path_component> rebuilt;

  // L(F("reconstructing path '%s' under analysis\n") % input);
  
  split_path(input, vec);

  tid t = root_tid;
  std::vector<path_component>::const_iterator pth = vec.begin();
  while (pth != vec.end())
    {     
      directory_map::const_iterator dirent = input_dir.find(t);
      if (dirent == input_dir.end())
        break;
      
      boost::shared_ptr<directory_node> node = dirent->second;
      directory_node::const_iterator entry = node->find(*pth);
      if (entry == node->end())
        break;

      {
        // check to see if this is the image of an added or deleted entry
        // (i.e. null name in output space), if so it terminates our
        // search.
        path_state::const_iterator i = output_space.find(directory_entry_tid(entry));
        I(i != output_space.end());
        if (null_name(path_item_name(path_state_item(i))))
          {
            // L(F("input path element '%s' is null in output space, mapping truncated\n") % *pth);
            break;
          }
      }
 
      // L(F("resolved entry '%s' in reconstruction\n") % *pth);
      ++pth;
      t = directory_entry_tid(entry);

      if (directory_entry_type(entry) != ptype_directory)
        break;
    }
      
  get_full_path(output_space, t, rebuilt);
  
  while(pth != vec.end())
    {
      // L(F("copying tail entry '%s' in reconstruction\n") % *pth);
      rebuilt.push_back(*pth);
      ++pth;
    }

  compose_path(rebuilt, output);
  // L(F("reconstructed path '%s' as '%s'\n") % input % output);
}


static void
build_directory_map(path_state const & state,
                    directory_map & dir)
{
  sanity_check_path_state(state);
  dir.clear();
  // L(F("building directory map for %d entries\n") % state.size());
  for (path_state::const_iterator i = state.begin(); i != state.end(); ++i)
    {
      tid curr = path_state_tid(i);
      path_item item = path_state_item(i);
      tid parent = path_item_parent(item);
      path_component name = path_item_name(item);
      ptype type = path_item_type(item);            
      //       L(F("adding entry %s (%s %d) to directory node %d\n") 
      //        % name % (type == ptype_directory ? "dir" : "file") % curr % parent);
      dnode(dir, parent)->insert(std::make_pair(name,std::make_pair(type, curr)));

      // also, make sure to add current node if it's a directory, even if
      // there are no entries in it
      if (type == ptype_directory)
        dnode(dir, curr);        
    }
}


void 
analyze_rearrangement(change_set::path_rearrangement const & pr,
                      path_analysis & pa,
                      tid_source & ts)
{
  directory_map first_map, second_map;
  state_renumbering renumbering;
  std::set<tid> damaged_in_second;

  pa.first.clear();
  pa.second.clear();

  for (std::set<file_path>::const_iterator f = pr.deleted_files.begin();
       f != pr.deleted_files.end(); ++f)
    {
      tid x = ensure_file_in_map(*f, first_map, pa.first, ts);
      pa.second.insert(std::make_pair(x, path_item(root_tid, ptype_file, make_null_component())));
    }

  for (std::set<file_path>::const_iterator d = pr.deleted_dirs.begin();
       d != pr.deleted_dirs.end(); ++d)
    {
      tid x = ensure_dir_in_map(*d, first_map, pa.first, ts);
      pa.second.insert(std::make_pair(x, path_item(root_tid, ptype_directory, make_null_component())));
    }

  for (std::map<file_path,file_path>::const_iterator rf = pr.renamed_files.begin();
       rf != pr.renamed_files.end(); ++rf)
    {
      tid a = ensure_file_in_map(rf->first, first_map, pa.first, ts);
      tid b = ensure_file_in_map(rf->second, second_map, pa.second, ts);
      I(renumbering.find(a) == renumbering.end());
      renumbering.insert(std::make_pair(b,a));
      damaged_in_second.insert(b);
    }

  for (std::map<file_path,file_path>::const_iterator rd = pr.renamed_dirs.begin();
       rd != pr.renamed_dirs.end(); ++rd)
    {
      tid a = ensure_dir_in_map(rd->first, first_map, pa.first, ts);
      tid b = ensure_dir_in_map(rd->second, second_map, pa.second, ts);
      I(renumbering.find(a) == renumbering.end());
      renumbering.insert(std::make_pair(b,a));
      damaged_in_second.insert(b);
    }

  for (std::set<file_path>::const_iterator a = pr.added_files.begin();
       a != pr.added_files.end(); ++a)
    {
      tid x = ensure_file_in_map(*a, second_map, pa.second, ts);
      pa.first.insert(std::make_pair(x, path_item(root_tid, ptype_file, make_null_component())));
      damaged_in_second.insert(x);
    }

  // we now have two states which probably have a number of entries in
  // common. we know already of an interesting set of entries they have in
  // common: all the renamed_foo entries. for each such renamed_foo(a,b)
  // entry, we made an entry in our state_renumbering of the form b->a,
  // while building the states.

  // dump_analysis("analyzed", pa);
  // dump_renumbering("first", renumbering);
  apply_state_renumbering(renumbering, pa.second);
  build_directory_map(pa.first, first_map);
  build_directory_map(pa.second, second_map);
  renumbering.clear();
  // dump_analysis("renumbered once", pa);
  
  // that only gets us half way, though:
  //
  // - every object which was explicitly moved (thus stayed alive) has been
  //   renumbered in re.second to have the same tid as in re.first
  //
  // - every object which was merely mentionned in passing -- say due to
  //   being an intermediate directory in a path -- and was not moved, still 
  //   has differing tids in re.first and re.second (or worse, may only
  //   even have an *entry* in one of them)
  //
  // the second point here is what we need to correct: if a path didn't
  // move, wasn't destroyed, and wasn't added, we want it to have the same
  // tid. but that's a relatively easy condition to check; we've been
  // keeping sets of all the objects which were damaged on each side of
  // this business anyways.


  // pass #1 makes sure that all the entries in each state *exist* within
  // the other state, even if they have the wrong numbers

  ensure_entries_exist (pa.first, second_map, pa.second, ts);
  ensure_entries_exist (pa.second, first_map, pa.first, ts);

  // pass #2 identifies common un-damaged elements from 2->1 and inserts
  // renumberings

  for (path_state::const_iterator i = pa.second.begin(); 
       i != pa.second.end(); ++i)
    {
      tid first_tid, second_tid;
      second_tid = path_state_tid(i);
      file_path full;
      if (pa.first.find(second_tid) != pa.first.end())
        continue;
      get_full_path(pa.second, second_tid, full);
      if (damaged_in_second.find(second_tid) != damaged_in_second.end())
        continue;
      if (null_name(path_item_name(path_state_item(i))))
        continue;
      I(lookup_path(full, first_map, first_tid));
      renumbering.insert(std::make_pair(second_tid, first_tid));
    }

  // dump_renumbering("second", renumbering);
  apply_state_renumbering(renumbering, pa.second);
  // dump_analysis("renumbered again", pa);

  // that should be the whole deal; if we don't have consensus at this
  // point we have done something wrong.
  sanity_check_path_analysis (pa);
}

void
normalize_path_rearrangement(change_set::path_rearrangement & norm)
{
  path_analysis tmp;
  tid_source ts;

  analyze_rearrangement(norm, tmp, ts);
  clear_rearrangement(norm);
  compose_rearrangement(tmp, norm);
}

void
normalize_change_set(change_set & norm)
{  
  normalize_path_rearrangement(norm.rearrangement);
  change_set::delta_map tmp = norm.deltas;
  for (change_set::delta_map::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      if (delta_entry_src(i) == delta_entry_dst(i))
        norm.deltas.erase(delta_entry_path(i));
    }
}


// begin stuff related to concatenation

static void 
index_entries(path_state const & state, 
              std::map<file_path, tid> & files, 
              std::map<file_path, tid> & dirs)
{
  for (path_state::const_iterator i = state.begin(); 
       i != state.end(); ++i)
    {
      file_path full;
      path_item item = path_state_item(i);
      get_full_path(state, path_state_tid(i), full);

      if (null_name(path_item_name(item))) 
        continue;

      switch (path_item_type(item))
        {
        case ptype_directory:
          dirs.insert(std::make_pair(full, path_state_tid(i)));
          break;

        case ptype_file:
          files.insert(std::make_pair(full, path_state_tid(i)));
          break;
        }
    }  
}

// this takes every (p1,t1) entry in b and, if (p1,t2) it exists in a, 
// inserts (t1,t2) in the rename set. in other words, it constructs the
// renumbering from b->a
static void 
extend_renumbering_from_path_identities(std::map<file_path, tid> const & a,
                                        std::map<file_path, tid> const & b,
                                        state_renumbering & renumbering)
{
  for (std::map<file_path, tid>::const_iterator i = b.begin();
       i != b.end(); ++i)
    {
      I(! null_name(i->first));
      std::map<file_path, tid>::const_iterator j = a.find(i->first);
      if (j == a.end())
        continue;
      I(renumbering.find(i->second) == renumbering.end());
      renumbering.insert(std::make_pair(i->second, j->second));
    }
}

static void
extend_state(path_state const & src, 
             path_state & dst)
{
  std::vector< std::pair<tid, path_item> > tmp;
  for (path_state::const_iterator i = src.begin();
       i != src.end(); ++i)
    {
      if (dst.find(path_state_tid(i)) == dst.end())
        tmp.push_back(*i);
    }

  for (std::vector<std::pair<tid, path_item> >::const_iterator i = tmp.begin();
      i != tmp.end(); i++)
    dst.insert(*i);
}

static void
ensure_tids_disjoint(path_analysis const & a, 
                     path_analysis const & b)
{
  for (path_state::const_iterator i = a.first.begin();
       i != a.first.end(); ++i)
    {
      I(b.first.find(path_state_tid(i)) == b.first.end());
    }  
  for (path_state::const_iterator i = b.first.begin();
       i != b.first.end(); ++i)
    {
      I(a.first.find(path_state_tid(i)) == a.first.end());
    }  
}

static void
extract_killed(path_analysis const & a,
               std::set<file_path> & killed)

{
  killed.clear();
  directory_map first_map, second_map;

  build_directory_map(a.first, first_map);
  build_directory_map(a.second, second_map);

  for (directory_map::const_iterator i = first_map.begin();
       i != first_map.end(); ++i)
    {
      tid dir_tid = i->first;
      directory_map::const_iterator j = second_map.find(dir_tid);
      I(j != second_map.end());

      // a path P = DIR/LEAF is "killed" by a path_analysis iff the
      // directory node named DIR in the post-state contains LEAF in the
      // pre-state, and does not contain LEAF in the post-state

      boost::shared_ptr<directory_node> first_node = i->second;
      boost::shared_ptr<directory_node> second_node = j->second;

      for (directory_node::const_iterator p = first_node->begin();
           p != first_node->end(); ++p)
        {
          path_component first_name = directory_entry_name(p);
          directory_node::const_iterator q = second_node->find(first_name);
          if (q == second_node->end())
            {
              // found a killed entry
              std::vector<path_component> killed_name;
              file_path killed_path;
              get_full_path(a.second, dir_tid, killed_name);
              killed_name.push_back(first_name);
              compose_path(killed_name, killed_path);
              killed.insert(killed_path);
            }
        }
    }
}

static void
check_delta_entries_not_directories(path_analysis const & pa,
                                    change_set::delta_map const & dels)
{
  directory_map dmap;
  build_directory_map(pa.second, dmap);
  for (change_set::delta_map::const_iterator i = dels.begin();
       i != dels.end(); ++i)
    {
      tid delta_tid;
      if (lookup_path(delta_entry_path(i), dmap, delta_tid))
        {
          path_state::const_iterator j = pa.second.find(delta_tid);
          I(j != pa.second.end());
          I(path_item_type(path_state_item(j)) == ptype_file);
        }
    }
}

static void
concatenate_disjoint_analyses(path_analysis const & a,
                              path_analysis const & b,
                              std::set<file_path> const & a_killed,
                              path_analysis & concatenated)
{
  std::map<file_path, tid> a_second_files, a_second_dirs;
  std::map<file_path, tid> b_first_files, b_first_dirs;
  path_analysis a_tmp(a), b_tmp(b);
  state_renumbering renumbering;
  
  // the trick here is that a.second and b.first supposedly refer to the
  // same state-of-the-world, so all we need to do is:
  //
  // - confirm that both analyses have disjoint tids
  // - work out which tids in b to identify with tids in a
  // - renumber b
  //
  // - copy a.first -> concatenated.first
  // - insert all elements of b.first not already in concatenated.first
  // - copy b.second -> concatenated.second
  // - insert all elements of a.second not already in concatenated.second

  ensure_tids_disjoint(a_tmp, b_tmp);

  index_entries(a_tmp.second, a_second_files, a_second_dirs);
  index_entries(b_tmp.first, b_first_files, b_first_dirs);

  {
    std::set<file_path> 
      a_second_file_set, a_second_dir_set, 
      b_first_file_set, b_first_dir_set;
    
    extract_first(a_second_files, a_second_file_set);
    extract_first(a_second_dirs, a_second_dir_set);
    extract_first(b_first_files, b_first_file_set);
    extract_first(b_first_dirs, b_first_dir_set);
    
    // check that there are no entry-type mismatches
    check_sets_disjoint(a_second_file_set, b_first_dir_set);
    check_sets_disjoint(a_second_dir_set, b_first_file_set);

    // check that there's no use of killed entries
    check_sets_disjoint(a_killed, b_first_dir_set);
    check_sets_disjoint(a_killed, b_first_file_set);
  }

  extend_renumbering_from_path_identities(a_second_files, b_first_files, renumbering);
  extend_renumbering_from_path_identities(a_second_dirs, b_first_dirs, renumbering);

  //   dump_analysis("state A", a_tmp);
  //   dump_analysis("state B", b_tmp);
  //   dump_renumbering("concatenation", renumbering);
  apply_state_renumbering(renumbering, b_tmp);

  concatenated.first = a_tmp.first;
  concatenated.second = b_tmp.second;

  extend_state(b_tmp.first, concatenated.first);
  extend_state(a_tmp.second, concatenated.second);

  sanity_check_path_analysis(concatenated);
}

void
concatenate_rearrangements(change_set::path_rearrangement const & a,
                           change_set::path_rearrangement const & b,
                           change_set::path_rearrangement & concatenated)
{
  a.check_sane();
  b.check_sane();
  concatenated = change_set::path_rearrangement();
  
  tid_source ts;
  path_analysis a_analysis, b_analysis, concatenated_analysis;

  analyze_rearrangement(a, a_analysis, ts);
  analyze_rearrangement(b, b_analysis, ts);

  std::set<file_path> a_killed;
  extract_killed(a_analysis, a_killed);

  concatenate_disjoint_analyses(a_analysis, 
                                b_analysis,
                                a_killed,
                                concatenated_analysis);

  compose_rearrangement(concatenated_analysis, 
                        concatenated);

  concatenated.check_sane();
}

void
concatenate_change_sets(change_set const & a,
                        change_set const & b,
                        change_set & concatenated)
{
  MM(a);
  MM(b);
  MM(concatenated);
  a.check_sane();
  b.check_sane();

  L(F("concatenating change sets\n"));

  tid_source ts;
  path_analysis a_analysis, b_analysis, concatenated_analysis;

  analyze_rearrangement(a.rearrangement, a_analysis, ts);
  analyze_rearrangement(b.rearrangement, b_analysis, ts);

  std::set<file_path> a_killed;
  extract_killed(a_analysis, a_killed);

  concatenate_disjoint_analyses(a_analysis, 
                                b_analysis,
                                a_killed,
                                concatenated_analysis);

  compose_rearrangement(concatenated_analysis, 
                        concatenated.rearrangement);

  // now process the deltas

  concatenated.deltas.clear();
  directory_map a_dst_map, b_src_map;
  L(F("concatenating %d and %d deltas\n")
    % a.deltas.size() % b.deltas.size());
  build_directory_map(a_analysis.second, a_dst_map);
  build_directory_map(b_analysis.first, b_src_map);

  // first rename a's deltas under the rearrangement of b
  for (change_set::delta_map::const_iterator del = a.deltas.begin();
       del != a.deltas.end(); ++del)
    {
      file_path new_pth;
      L(F("processing delta on %s\n") % delta_entry_path(del));

      // work out the name of entry in b.first
      reconstruct_path(delta_entry_path(del), b_src_map, b_analysis.second, new_pth);
      L(F("delta on %s in first changeset renamed to %s\n")
        % delta_entry_path(del) % new_pth);

      if (b.rearrangement.has_deleted_file(delta_entry_path(del)))
        // the delta should be removed if the file is going to be deleted
        L(F("discarding delta [%s]->[%s] for deleted file '%s'\n")
          % delta_entry_src(del) % delta_entry_dst(del) % delta_entry_path(del));
      else
        concatenated.deltas.insert(std::make_pair(new_pth,
                                                  std::make_pair(delta_entry_src(del),
                                                                 delta_entry_dst(del))));
    }

  // next fuse any deltas id1->id2 and id2->id3 to id1->id3
  for (change_set::delta_map::const_iterator del = b.deltas.begin();
       del != b.deltas.end(); ++del)
    {

      file_path del_pth = delta_entry_path(del);
      change_set::delta_map::const_iterator existing = 
        concatenated.deltas.find(del_pth);
      if (existing != concatenated.deltas.end())
        {
          L(F("fusing deltas on %s : %s -> %s and %s -> %s\n")
            % del_pth
            % delta_entry_src(existing) 
            % delta_entry_dst(existing)
            % delta_entry_src(del)
            % delta_entry_dst(del));
          I(delta_entry_dst(existing) == delta_entry_src(del));
          std::pair<file_id, file_id> fused = std::make_pair(delta_entry_src(existing),
                                                             delta_entry_dst(del));      
          concatenated.deltas.erase(del_pth);
          concatenated.deltas.insert(std::make_pair((del_pth), fused));
        }
      else
        {
          L(F("delta on %s in second changeset copied forward\n") % del_pth);
          // in general don't want deltas on deleted files. however if a
          // file has been deleted then re-added, then a delta is valid
          // (it applies to the newly-added file)
          if (!b.rearrangement.has_deleted_file(del_pth)
              || b.rearrangement.has_added_file(del_pth)
              || b.rearrangement.has_renamed_file_dst(del_pth))
            concatenated.deltas.insert(*del);
        }
    }
  
  normalize_change_set(concatenated);
  concatenated.check_sane();
  
  L(F("finished concatenation\n")); 
}

// end stuff related to concatenation


// begin stuff related to merging

static bool
find_item(tid t, path_state const & ps, 
          path_item & item)
{
  path_state::const_iterator i = ps.find(t);
  if (i == ps.end())
    return false;
  item = path_state_item(i);
  return true;
}

struct itempaths
{
  file_path anc;
  file_path left;
  file_path right;
  file_path merged;
  file_id ahash;
  file_id lhash;
  file_id rhash;
  file_id mhash;
  bool clean;

  itempaths(file_path const & l, file_path const & r, file_path const & m,
            file_id const & lh, file_id const & rh, file_id const & mh):
    anc(file_path()), left(l), right(r), merged(m),
    ahash(file_id()), lhash(lh), rhash(rh), mhash(mh),
    clean(!(mh == file_id()))
  {}
  itempaths()
  {}
};

void dump(itempaths const & obj, std::string & out)
{
  std::string o;
  dump(obj.anc, o);
  out = "Ancestor: " + o;
  dump(obj.left, o);
  out += "\nLeft: " + o;
  dump(obj.right, o);
  out += "\nRight: " + o;
  dump(obj.merged, o);
  out += "\nMerged: " + o;
}

void dump(std::vector<itempaths> const & obj, std::string & out)
{
  out.clear();
  std::string o;
  for (std::vector<itempaths>::const_iterator i = obj.begin();
       i != obj.end(); ++i)
    {
      dump(*i, o);
      out += o + "\n";
    }
}

static void
merge_deltas(itempaths const & paths, 
             std::map<file_path, file_id> & merge_finalists,
             file_id & finalist, 
             merge_provider & merger)
{
  std::map<file_path, file_id>::const_iterator
    i = merge_finalists.find(paths.merged);
  if (i != merge_finalists.end())
    {
      L(F("reusing merge resolution '%s' : '%s' -> '%s'\n")
        % paths.merged % paths.ahash % i->second);
      finalist = i->second;
    }
  else
    {
      if (null_id(paths.ahash))
        {
          N(merger.try_to_merge_files(paths.left, paths.right, paths.merged,
                                      paths.lhash, paths.rhash, finalist),
            F("merge of '%s' : '%s' vs. '%s' (no common ancestor) failed")
            % paths.merged % paths.lhash % paths.rhash);
        }
      else
        {
          N(merger.try_to_merge_files(paths.anc, paths.left,
                                      paths.right, paths.merged, 
                                      paths.ahash, paths.lhash, paths.rhash,
                                      finalist),
            F("merge of '%s' : '%s' -> '%s' vs '%s' failed") 
            % paths.merged % paths.ahash % paths.lhash % paths.rhash);
        }

      L(F("merge of '%s' : '%s' -> '%s' vs '%s' resolved to '%s'\n") 
        % paths.merged % paths.ahash % paths.lhash % paths.rhash % finalist);

      merge_finalists.insert(std::make_pair(paths.merged, finalist));
    }
}

static void
project_missing_deltas(std::vector<itempaths> const & pathset,
                       change_set & l_merged,
                       change_set & r_merged,
                       merge_provider & merger,
                       std::map<file_path, file_id> & merge_finalists)
{
// b_merged needs deltas for
// merged != file_path() && rhash != mhash
// if !clean, then run the merger
  for (std::vector<itempaths>::const_iterator i = pathset.begin();
       i != pathset.end(); ++i)
    {
      itempaths const & paths(*i);
      if (paths.merged == file_path())
        continue;
      if (paths.clean)
        {
          L(F("File '%s' clean merged to '%s' by hash")
            % paths.merged % paths.mhash);
          if (!(paths.lhash == paths.mhash))
            l_merged.apply_delta(paths.merged, paths.lhash, paths.mhash);
          if (!(paths.rhash == paths.mhash))
            r_merged.apply_delta(paths.merged, paths.rhash, paths.mhash);
        }
      else
        {
          file_id finalist;
          merge_deltas(paths, 
                       merge_finalists,
                       finalist, merger);
          L(F("resolved merge to '%s' : '%s'\n")
            % paths.merged % finalist);

          if (!(finalist == paths.lhash))
            l_merged.apply_delta(paths.merged,
                                 paths.lhash,
                                 finalist);
          if (!(finalist == paths.rhash))
            r_merged.apply_delta(paths.merged,
                                 paths.rhash,
                                 finalist);
        }
    }
}

void
calculate_itempaths(tree_state const & a,
                    tree_state const & l,
                    tree_state const & r,
                    tree_state const & m,
                    std::vector<itempaths> & paths,
                    interner<item_status::scalar> & itx)
{
  std::map<item_id, itempaths> ip;
  std::vector<std::pair<item_id, file_path> > ps;
  std::map<item_id, std::set<item_status::scalar> > sm;
  ps = a.current();
  for (std::vector<std::pair<item_id, file_path> >::const_iterator
         i = ps.begin(); i != ps.end(); ++i)
    {
      std::pair<std::map<item_id, itempaths>::iterator, bool> r;
      r = ip.insert(std::make_pair(i->first, itempaths()));
      r.first->second.anc = i->second;
    }
  ps = l.current();
  for (std::vector<std::pair<item_id, file_path> >::const_iterator
         i = ps.begin(); i != ps.end(); ++i)
    {
      std::pair<std::map<item_id, itempaths>::iterator, bool> r;
      r = ip.insert(std::make_pair(i->first, itempaths()));
      r.first->second.left = (*i).second;
    }
  ps = r.current();
  for (std::vector<std::pair<item_id, file_path> >::const_iterator
         i = ps.begin(); i != ps.end(); ++i)
    {
      std::pair<std::map<item_id, itempaths>::iterator, bool> r;
      r = ip.insert(std::make_pair(i->first, itempaths()));
      r.first->second.right = (*i).second;
    }
  ps = m.current();
  for (std::vector<std::pair<item_id, file_path> >::const_iterator
         i = ps.begin(); i != ps.end(); ++i)
    {
      std::pair<std::map<item_id, itempaths>::iterator, bool> r;
      r = ip.insert(std::make_pair(i->first, itempaths()));
      r.first->second.merged = (*i).second;
    }


  sm = a.current_scalars();
  for (std::map<item_id, std::set<item_status::scalar> >::const_iterator
         i = sm.begin(); i != sm.end(); ++i)
    {
      std::pair<std::map<item_id, itempaths>::iterator, bool> r;
      r = ip.insert(std::make_pair(i->first, itempaths()));
      I((*i).second.size() == 1);
      r.first->second.ahash = file_id(itx.lookup(*(*i).second.begin()));
    }

  sm = l.current_scalars();
  for (std::map<item_id, std::set<item_status::scalar> >::const_iterator
         i = sm.begin(); i != sm.end(); ++i)
    {
      std::pair<std::map<item_id, itempaths>::iterator, bool> r;
      r = ip.insert(std::make_pair(i->first, itempaths()));
      I((*i).second.size() == 1);
      r.first->second.lhash = file_id(itx.lookup(*(*i).second.begin()));
    }

  sm = r.current_scalars();
  for (std::map<item_id, std::set<item_status::scalar> >::const_iterator
         i = sm.begin(); i != sm.end(); ++i)
    {
      std::pair<std::map<item_id, itempaths>::iterator, bool> r;
      r = ip.insert(std::make_pair(i->first, itempaths()));
      I((*i).second.size() == 1);
      r.first->second.rhash = file_id(itx.lookup(*(*i).second.begin()));
    }

  sm = m.current_scalars();
  for (std::map<item_id, std::set<item_status::scalar> >::const_iterator
         i = sm.begin(); i != sm.end(); ++i)
    {
      std::pair<std::map<item_id, itempaths>::iterator, bool> r;
      r = ip.insert(std::make_pair(i->first, itempaths()));
      if ((*i).second.size() == 1)
        {
          r.first->second.clean = true;
          r.first->second.mhash = file_id(itx.lookup(*(*i).second.begin()));
        }
      else
        r.first->second.clean = false;
    }

  paths.clear();
  paths.reserve(ip.size());
  for (std::map<item_id, itempaths>::const_iterator i = ip.begin();
       i != ip.end(); ++i)
    paths.push_back(i->second);
}

tree_state
merge_trees(std::vector<tree_state> const & treevec,
            std::vector<change_set> const & chvec,
            interner<item_status::scalar> & itx,
            std::string revision)
{
  std::vector<change_set::path_rearrangement> revec;
  std::map<file_path, item_status::scalar> sc;
  for (std::vector<change_set>::const_iterator i = chvec.begin();
       i != chvec.end(); ++i)
    {
      revec.push_back(i->rearrangement);
      for (change_set::delta_map::const_iterator
             j = i->deltas.begin();
           j != i->deltas.end(); ++j)
        {
          sc.insert(make_pair(delta_entry_path(j),
                              itx.intern(delta_entry_dst(j).inner()())));
        }
    }
  tree_state newtree(tree_state::merge_with_rearrangement(treevec, revec,
                                                          revision));
  return newtree.set_scalars(revision, sc);
}

tree_state
merge_trees(tree_state l, tree_state r)
{
  std::vector<path_conflict> conf(l.conflict(r));
  MM(conf);
  std::set<path_conflict::resolution> res;
  for (std::vector<path_conflict>::const_iterator i = conf.begin();
       i != conf.end(); ++i)
    {
      E(i->type != path_conflict::split,
        F("Cannot handle filename conflicts yet."));
      if (i->type == path_conflict::collision)
        W(F("Filename collision, suturing..."));
    }
  std::vector<tree_state> lr;
  lr.push_back(l);
  lr.push_back(r);
  tree_state m = tree_state::merge_with_resolution(lr, res, "abccb");
  N(m.conflict(m).empty(), F("Provided filename resolution is inconsistent."));
  return m;
}

void
process_filetree_history(revision_id const & anc,
                         revision_id const & left,
                         revision_id const & right,
                         std::vector<itempaths> & paths,
                         change_set::path_rearrangement & lm_re,
                         change_set::path_rearrangement & rm_re,
                         app_state & app)
{
  typedef std::multimap<revision_id, revision_id>::iterator gi;
  typedef std::map<revision_id, std::pair<int, std::set<revision_id> > >::iterator ai;

  interner<item_status::scalar> itx;

  // process history

  std::multimap<revision_id, revision_id> graph, rgraph;
  app.db.get_revision_ancestry(graph);
  std::deque<revision_id> todo, roots;
//  for (gi i = graph.begin(); i != graph.end(); ++i)
//    rgraph.insert(std::make_pair(i->second, i->first));
  // only process as far back as the lcad. This saves time, and older history
  // has no effect on the merge.
  revision_id lcad;
  find_common_ancestor_for_merge(left, right, lcad, app);
  todo.push_back(lcad);
  std::set<revision_id> done;
  while(todo.size())
    {
      revision_id c(todo.back());
      todo.pop_back();
      unsigned int s = done.size();
      done.insert(c);
      if (s == done.size())
        continue;
      gi pb = graph.lower_bound(c);
      gi pe = graph.upper_bound(c);
      for (gi i = pb; i != pe; ++i)
        {
          todo.push_back(i->second);
          rgraph.insert(std::make_pair(i->second, i->first));
        }
    }


  // rev -> {# of parents remaining, children}
  std::map<revision_id, std::pair<int, std::set<revision_id> > > about;
  todo.push_back(left);
  todo.push_back(right);
  todo.push_back(anc);
  about.insert(make_pair(left, make_pair(0, std::set<revision_id>())));
  about.insert(make_pair(right, make_pair(0, std::set<revision_id>())));
  about.insert(make_pair(anc, make_pair(0, std::set<revision_id>())));
  while(todo.size())
    {
      revision_id c(todo.back());
      todo.pop_back();
      gi pb = rgraph.lower_bound(c);
      gi pe = rgraph.upper_bound(c);
      int n = 0;
      for (gi i = pb; i != pe; ++i)
        {
          if (null_id(i->second))
            continue;
          std::set<revision_id> s;
          s.insert(c);
          std::pair<ai, bool> r;
          r = about.insert(make_pair(i->second, make_pair(0, s)));
          if (r.second)
            todo.push_back(i->second);
          else
            r.first->second.second.insert(c);
          ++n;
        }
      ai me = about.find(c);
      I(me != about.end());
      me->second.first = n;
      if (n == 0)
        roots.push_back(c);
    }
  std::map<revision_id, tree_state> trees;
  tree_state emptytree(tree_state::new_tree());
  while(roots.size())
    {
      revision_set rs;
      app.db.get_revision(roots.front(), rs);
      std::vector<tree_state> treevec;
      std::vector<change_set> chvec;
      std::map<file_path, item_status::scalar> sc;
      for (edge_map::const_iterator i = rs.edges.begin();
           i != rs.edges.end(); ++i)
        {
          tree_state from(emptytree);
          if (edge_old_revision(i) == revision_id())
            from = emptytree;
          else
            {
              std::map<revision_id, tree_state>::iterator
                j = trees.find(edge_old_revision(i));
              // if it doesn't exist, then it's from a rev that's being ignored
              // due to old age.
              if (j == trees.end())
                continue;
              from = j->second;
            }
          treevec.push_back(from);
          chvec.push_back(edge_changes(i));
        }
      if (treevec.empty())
        {
          // this can happen since we ignore prehistoric ancestors.
          // but we still need a change_set
          manifest_map man;
          app.db.get_manifest(rs.new_manifest, man);
          change_set cs;
          build_pure_addition_change_set(man, cs);
          treevec.push_back(emptytree);
          chvec.push_back(cs);
        }
      trees.insert(make_pair(roots.front(),
                             merge_trees(treevec, chvec, itx,
                                         roots.front().inner()())));

      ai i = about.find(roots.front());
      I(i != about.end());
      std::set<revision_id> const & cs(i->second.second);
      for (std::set<revision_id>::const_iterator j = cs.begin();
           j != cs.end(); j++)
        {
          ai k = about.find(*j);
          I(k != about.end());
          if (--(k->second.first) == 0)
            roots.push_back(*j);
        }
      roots.pop_front();
    }

  // find the interesting revisions
  std::map<revision_id, tree_state>::const_iterator
    i = trees.find(anc),
    j = trees.find(left),
    k = trees.find(right);
  I(i != trees.end());
  I(j != trees.end());
  I(k != trees.end());
  tree_state a = i->second;
  tree_state l = j->second;
  tree_state r = k->second;

  // do the merge
  tree_state m = merge_trees(l, r);

  // calculate outputs
  calculate_itempaths(a, l, r, m, paths, itx);
  l.get_changes_for_merge(m, lm_re);
  r.get_changes_for_merge(m, rm_re);
}

void
check_merge(change_set const & anc_a, change_set & a_merged,
            change_set const & anc_b, change_set & b_merged)
{
  L(F("Checking merge..."));
  a_merged.check_sane();
  b_merged.check_sane();

  {
    // confirmation step
    change_set a_check, b_check;
    MM(a_check);
    MM(b_check);
    //     dump_change_set("a", a);
    //     dump_change_set("a_merged", a_merged);
    //     dump_change_set("b", b);
    //     dump_change_set("b_merged", b_merged);
    concatenate_change_sets(anc_a, a_merged, a_check);
    concatenate_change_sets(anc_b, b_merged, b_check);
    //     dump_change_set("a_check", a_check);
    //     dump_change_set("b_check", b_check);
    I(a_check == b_check);
  }

  normalize_change_set(a_merged);
  normalize_change_set(b_merged);

  a_merged.check_sane();
  b_merged.check_sane(); 
}

void
merge_revisions(revision_id const & anc,
                revision_id const & a,
                revision_id const & b,
                change_set & a_merged,
                change_set & b_merged,
                merge_provider & merger,
                app_state & app)
{
  L(F("merging revisions\n"));

  std::vector<itempaths> paths;
  MM(paths);

  std::map<file_path, file_id> merge_finalists;

  change_set anc_a, anc_b, anc_bwithchanges;
  MM(anc_a);
  MM(anc_b);
  if (null_id(anc))
    {
      manifest_map a_man, b_man;
      revision_set a_rev, b_rev;
      MM(a_man);
      MM(b_man);
      app.db.get_revision(a, a_rev);
      app.db.get_revision(b, b_rev);
      app.db.get_manifest(a_rev.new_manifest, a_man);
      app.db.get_manifest(b_rev.new_manifest, b_man);
      build_pure_addition_change_set(a_man, anc_a);
      build_pure_addition_change_set(b_man, anc_b);

      for (std::set<file_path>::const_iterator
             i = anc_a.rearrangement.added_files.begin();
           i != anc_a.rearrangement.added_files.end(); ++i)
        {
          manifest_map::const_iterator j = a_man.find(*i);
          I(j != a_man.end());
          file_id a_id = manifest_entry_id(j);
          if (!anc_b.rearrangement.has_added_file(*i))
            {
              b_merged.add_file(*i);
              paths.push_back(itempaths(*i, file_path(), *i,
                                        a_id, file_id(), a_id));
            }
          else
            {
              manifest_map::const_iterator k = b_man.find(*i);
              I(k != b_man.end());
              file_id b_id = manifest_entry_id(k);
              file_id m_id = ((a_id == b_id)?a_id:file_id());
              paths.push_back(itempaths(*i, *i, *i,
                                        a_id, b_id, m_id));
            }
        }

      for (std::set<file_path>::const_iterator
             i = anc_b.rearrangement.added_files.begin();
           i != anc_b.rearrangement.added_files.end(); ++i)
        if (!anc_a.rearrangement.has_added_file(*i))
          {
            manifest_map::const_iterator k = b_man.find(*i);
            I(k != b_man.end());
            file_id b_id = manifest_entry_id(k);
            a_merged.add_file(*i);
            paths.push_back(itempaths(file_path(), *i, *i,
                                      file_id(), b_id, b_id));
          }
    }
  else
    {
      process_filetree_history(anc, a, b, paths,
                               a_merged.rearrangement,
                               b_merged.rearrangement,
                               app);
      if (!(anc == a))
        calculate_arbitrary_change_set(anc, a, app, anc_a);
      if (!(anc == b))
        calculate_arbitrary_change_set(anc, b, app, anc_b);
    }

  MM(a_merged);
  MM(b_merged);
  project_missing_deltas(paths, a_merged, b_merged,
                         merger, merge_finalists);

  check_merge(anc_a, a_merged, anc_b, b_merged);
  L(F("finished merge\n"));
}

void
transplant_change_set(revision_id const & from,
                      revision_id const & to,
                      change_set const & cs,
                      change_set & to_res,
                      change_set & cs_res,
                      merge_provider & merger,
                      app_state & app)
{
  manifest_map from_man;
  revision_set from_rev;
  change_set from_cs, from_to_cs;

  app.db.get_revision(from, from_rev);
  app.db.get_manifest(from_rev.new_manifest, from_man);
  build_pure_addition_change_set(from_man, from_cs);

  calculate_arbitrary_change_set(from, to, app, from_to_cs);

  tree_state emptytree(tree_state::new_tree());
  interner<item_status::scalar> itx;
  std::vector<tree_state> treevec;
  std::vector<change_set> chvec;
  treevec.push_back(emptytree);
  chvec.push_back(from_cs);
  tree_state anc = merge_trees(treevec, chvec, itx, "from");
  idx(treevec, 0) = anc;
  idx(chvec, 0) = from_to_cs;
  tree_state left = merge_trees(treevec, chvec, itx, "to");
//  idx(treevec, 0) = from;
  idx(chvec, 0) = cs;
  tree_state changes = merge_trees(treevec, chvec, itx, "changes");

  // merge
  tree_state result = merge_trees(left, changes);

  // calculate outputs
  std::vector<itempaths> paths;
  calculate_itempaths(anc, left, changes, result, paths, itx);
  left.get_changes_for_merge(result, to_res.rearrangement);
  changes.get_changes_for_merge(result, cs_res.rearrangement);
  std::map<file_path, file_id> merge_finalists;
  project_missing_deltas(paths, to_res, cs_res,
                         merger, merge_finalists);
  check_merge(from_to_cs, to_res, cs, cs_res);
}

// end stuff related to merging

void 
invert_change_set(change_set const & a2b,
                  manifest_map const & a_map,
                  change_set & b2a)
{
  MM(a2b);
  MM(a_map);
  MM(b2a);
  a2b.check_sane();
  tid_source ts;
  path_analysis a2b_analysis, b2a_analysis;

  analyze_rearrangement(a2b.rearrangement, a2b_analysis, ts);

  L(F("inverting change set\n"));
  b2a_analysis.first = a2b_analysis.second;
  b2a_analysis.second = a2b_analysis.first;
  compose_rearrangement(b2a_analysis, b2a.rearrangement);

  b2a.deltas.clear();

  std::set<file_path> moved_deltas;

  // existing deltas are in "b space"
  for (path_state::const_iterator b = b2a_analysis.first.begin();
       b != b2a_analysis.first.end(); ++b)
    {
      path_state::const_iterator a = b2a_analysis.second.find(path_state_tid(b));
      I(a != b2a_analysis.second.end());
      if (path_item_type(path_state_item(b)) == ptype_file)
        {
          file_path b_pth, a_pth;
          get_full_path(b2a_analysis.first, path_state_tid(b), b_pth);

          if (null_name(path_item_name(path_state_item(b))) &&
              ! null_name(path_item_name(path_state_item(a))))
            {
              // b->a represents an add in "a space"
              get_full_path(b2a_analysis.second, path_state_tid(a), a_pth);
              manifest_map::const_iterator i = a_map.find(a_pth);
              I(i != a_map.end());
              b2a.deltas.insert(std::make_pair(a_pth, 
                                               std::make_pair(file_id(), 
                                                              manifest_entry_id(i))));
              L(F("converted 'delete %s' to 'add as %s' in inverse\n")
                % a_pth 
                % manifest_entry_id(i));
            }
          else if (! null_name(path_item_name(path_state_item(b))) &&
                   null_name(path_item_name(path_state_item(a))))
            {
              // b->a represents a del from "b space"
              get_full_path(b2a_analysis.first, path_state_tid(b), b_pth);
              L(F("converted add %s to delete in inverse\n") % b_pth );
            }
          else
            {
              get_full_path(b2a_analysis.first, path_state_tid(b), b_pth);
              get_full_path(b2a_analysis.second, path_state_tid(a), a_pth);
              change_set::delta_map::const_iterator del = a2b.deltas.find(b_pth);
               if (del == a2b.deltas.end())
                continue;
              file_id src_id(delta_entry_src(del)), dst_id(delta_entry_dst(del));
              L(F("converting delta %s -> %s on %s\n")
                % src_id % dst_id % b_pth);
              L(F("inverse is delta %s -> %s on %s\n")
                % dst_id % src_id % a_pth);
              b2a.deltas.insert(std::make_pair(a_pth, std::make_pair(dst_id, src_id)));
              moved_deltas.insert(b_pth);
            }
        }
    }

  // some deltas might not have been renamed, however. these we just invert the
  // direction on
  for (change_set::delta_map::const_iterator del = a2b.deltas.begin();
       del != a2b.deltas.end(); ++del)
    {
      // check to make sure this isn't the image of an add (now a delete)
      if (null_id(delta_entry_src(del)))
        continue;
      // check to make sure this isn't one of the already-moved deltas
      if (moved_deltas.find(delta_entry_path(del)) != moved_deltas.end())
        continue;
      // we shouldn't have created a delta earlier, if this file really is
      // untouched...
      I(b2a.deltas.find(delta_entry_path(del)) == b2a.deltas.end());
      b2a.deltas.insert(std::make_pair(delta_entry_path(del),
                                       std::make_pair(delta_entry_dst(del),
                                                      delta_entry_src(del))));
    }
  normalize_change_set(b2a);
  b2a.check_sane();
}

void 
move_files_to_tmp_bottom_up(tid t,
                            local_path const & temporary_root,
                            path_state const & state,
                            directory_map const & dmap)
{
  directory_map::const_iterator dirent = dmap.find(t);
  if (dirent != dmap.end())
    {
      boost::shared_ptr<directory_node> node = dirent->second;  
      for (directory_node::const_iterator entry = node->begin();
           entry != node->end(); ++entry)
        {
          tid child = directory_entry_tid(entry);
          file_path path;
          path_item item;
              
          find_item(child, state, item);

          if (null_name(path_item_name(item)))
            continue;

          // recursively move all sub-entries
          if (path_item_type(item) == ptype_directory)
            move_files_to_tmp_bottom_up(child, temporary_root, state, dmap);

          get_full_path(state, child, path);
          
          local_path src(path());
          local_path dst((mkpath(temporary_root()) 
                          / mkpath(boost::lexical_cast<std::string>(child))).string());
          
          P(F("moving %s -> %s\n") % src % dst);
          switch (path_item_type(item))
            {
            case ptype_file:
              if (file_exists(src))
                move_file(src, dst);
              break;
            case ptype_directory:
              if (directory_exists(src))
                move_dir(src, dst);
              break;
            }
        }
    }
}

void 
move_files_from_tmp_top_down(tid t,
                             local_path const & temporary_root,
                             path_state const & state,
                             directory_map const & dmap)
{
  directory_map::const_iterator dirent = dmap.find(t);
  if (dirent != dmap.end())
    {
      boost::shared_ptr<directory_node> node = dirent->second;  
      for (directory_node::const_iterator entry = node->begin();
           entry != node->end(); ++entry)
        {
          tid child = directory_entry_tid(entry);
          file_path path;
          path_item item;
              
          find_item(child, state, item);

          if (null_name(path_item_name(item)))
            continue;

          get_full_path(state, child, path);
          
          local_path src((mkpath(temporary_root()) 
                          / mkpath(boost::lexical_cast<std::string>(child))).string());
          local_path dst(path());
          
          switch (path_item_type(item))
            {
            case ptype_file:
              if (file_exists(src))
                {
                  P(F("moving file %s -> %s\n") % src % dst);
                  make_dir_for(path);
                  move_file(src, dst);
                }
              break;
            case ptype_directory:
              if (directory_exists(src))
                {
                  P(F("moving dir %s -> %s\n") % src % dst);
                  make_dir_for(path);
                  move_dir(src, dst);
                }
              break;
            }

          // recursively move all sub-entries
          if (path_item_type(item) == ptype_directory)
            move_files_from_tmp_top_down(child, temporary_root, state, dmap);
        }
    }
}


void
apply_rearrangement_to_filesystem(change_set::path_rearrangement const & re,
                                  local_path const & temporary_root)
{
  re.check_sane();
  tid_source ts;
  path_analysis analysis;
  directory_map first_dmap, second_dmap;

  analyze_rearrangement(re, analysis, ts);
  build_directory_map(analysis.first, first_dmap);
  build_directory_map(analysis.second, second_dmap);

  if (analysis.first.empty())
    return;

  move_files_to_tmp_bottom_up(root_tid, temporary_root,
                              analysis.first, first_dmap);

  move_files_from_tmp_top_down(root_tid, temporary_root,
                               analysis.second, second_dmap);
}

// application stuff

void
build_pure_addition_change_set(manifest_map const & man,
                               change_set & cs)
{
  for (manifest_map::const_iterator i = man.begin(); i != man.end(); ++i)
    cs.add_file(manifest_entry_path(i), manifest_entry_id(i));
  cs.check_sane();
}

// this function takes the rearrangement sitting in cs and "completes" the
// changeset by filling in all the deltas

void 
complete_change_set(manifest_map const & m_old,
                    manifest_map const & m_new,
                    change_set & cs)
{
  cs.rearrangement.check_sane();
  tid_source ts;
  path_analysis analysis;
  directory_map first_dmap, second_dmap;

  analyze_rearrangement(cs.rearrangement, analysis, ts);
  build_directory_map(analysis.first, first_dmap);
  build_directory_map(analysis.second, second_dmap);

  std::set<file_path> paths;
  extract_path_set(m_new, paths);

  for (std::set<file_path>::const_iterator i = cs.rearrangement.added_files.begin();
       i != cs.rearrangement.added_files.end(); ++i)
    {
      manifest_map::const_iterator j = m_new.find(*i);
      I(j != m_new.end());
      cs.deltas.insert(std::make_pair(*i,
                                      std::make_pair(null_ident,
                                                     manifest_entry_id(j))));
      paths.erase(*i);
    }

  for (std::set<file_path>::const_iterator i = paths.begin();
       i != paths.end(); ++i)
    {
      file_path old_path;
      reconstruct_path(*i, second_dmap, analysis.first, old_path);
      manifest_map::const_iterator j = m_old.find(old_path);
      manifest_map::const_iterator k = m_new.find(*i);
      I(j != m_old.end());
      I(k != m_new.end());
      if (!(manifest_entry_id(j) == manifest_entry_id(k)))
        cs.deltas.insert(std::make_pair(*i, std::make_pair(manifest_entry_id(j),
                                                           manifest_entry_id(k))));
    }

  cs.check_sane();    
}


void
apply_change_set(manifest_map const & old_man,
                 change_set const & cs,
                 manifest_map & new_man)
{
  cs.check_sane();
  change_set a, b;
  build_pure_addition_change_set(old_man, a);
  concatenate_change_sets(a, cs, b);

  // If the composed change_set still has renames or deletions in it, then
  // they referred to things that weren't in the original manifest, and this
  // change_set should never have been applied to this manifest in the first
  // place.
  I(b.rearrangement.deleted_files.empty());
  I(b.rearrangement.renamed_files.empty());
  // Furthermore, all deltas should be add deltas
  for (change_set::delta_map::const_iterator i = b.deltas.begin();
      i != b.deltas.end(); ++i)
    {
      I(null_id(delta_entry_src(i)));
      I(b.rearrangement.added_files.find(delta_entry_path(i))
        != b.rearrangement.added_files.end());
    }

  new_man.clear();
  for (std::set<file_path>::const_iterator i = b.rearrangement.added_files.begin();
       i != b.rearrangement.added_files.end(); ++i)
    {
      change_set::delta_map::const_iterator d = b.deltas.find(*i);
      I(d != b.deltas.end());
      new_man.insert(std::make_pair(*i, delta_entry_dst(d)));
    }
}

static inline bool
apply_path_rearrangement_can_fastpath(change_set::path_rearrangement const & pr)
{
  return pr.added_files.empty()
    && pr.renamed_files.empty()
    && pr.renamed_dirs.empty()
    && pr.deleted_dirs.empty();
}

static inline void
apply_path_rearrangement_fastpath(change_set::path_rearrangement const & pr,
                                  path_set & ps)
{
  pr.check_sane();
  // fast path for simple drop-or-nothing file operations
  for (std::set<file_path>::const_iterator i = pr.deleted_files.begin();
       i != pr.deleted_files.end(); ++i)
    {
      I(ps.find(*i) != ps.end());
      ps.erase(*i);
    }
}

static inline void
apply_path_rearrangement_slowpath(path_set const & old_ps,
                                  change_set::path_rearrangement const & pr,
                                  path_set & new_ps)
{
  pr.check_sane();
  change_set::path_rearrangement a, b;
  a.added_files = old_ps;
  concatenate_rearrangements(a, pr, b);
  new_ps = b.added_files;
}

void
apply_path_rearrangement(path_set const & old_ps,
                         change_set::path_rearrangement const & pr,
                         path_set & new_ps)
{
  if (apply_path_rearrangement_can_fastpath(pr))
    {
      new_ps = old_ps;
      apply_path_rearrangement_fastpath(pr, new_ps);
    }
  else
    {
      apply_path_rearrangement_slowpath(old_ps, pr, new_ps);
    }
}

// destructive version
void
apply_path_rearrangement(change_set::path_rearrangement const & pr,
                         path_set & ps)
{
  if (apply_path_rearrangement_can_fastpath(pr))
    {
      apply_path_rearrangement_fastpath(pr, ps);
    }
  else
    {
      path_set tmp = ps;
      apply_path_rearrangement_slowpath(ps, pr, tmp);
      ps = tmp;
    }
}

// quick, optimistic and destructive version
file_path
apply_change_set_inverse(change_set const & cs,
                         file_path const & file_in_second)
{
  cs.check_sane();
  tid_source ts;
  path_analysis analysis;
  directory_map second_dmap;
  file_path file_in_first;

  analyze_rearrangement(cs.rearrangement, analysis, ts);
  build_directory_map(analysis.second, second_dmap);
  reconstruct_path(file_in_second, second_dmap, analysis.first, file_in_first);
  return file_in_first;
}

// quick, optimistic and destructive version 
void
apply_change_set(change_set const & cs,
                 manifest_map & man)
{
  cs.check_sane();
  if (cs.rearrangement.added_files.empty() 
      && cs.rearrangement.renamed_files.empty() 
      && cs.rearrangement.renamed_dirs.empty()
      && cs.rearrangement.deleted_dirs.empty())
    {
      // fast path for simple drop/delta file operations
      for (std::set<file_path>::const_iterator i = cs.rearrangement.deleted_files.begin();
           i != cs.rearrangement.deleted_files.end(); ++i)
        {
          man.erase(*i);
        }
      for (change_set::delta_map::const_iterator i = cs.deltas.begin(); 
           i != cs.deltas.end(); ++i)
        {
          if (!null_id(delta_entry_dst(i)))
            man[delta_entry_path(i)] = delta_entry_dst(i);
        }
    }
  else
    {
      // fall back to the slow way
      manifest_map tmp;
      apply_change_set(man, cs, tmp);
      man = tmp;
    }
}


// i/o stuff

namespace
{
  namespace syms
  {
    std::string const patch("patch");
    std::string const from("from");
    std::string const to("to");
    std::string const add_file("add_file");
    std::string const delete_file("delete_file");
    std::string const delete_dir("delete_dir");
    std::string const rename_file("rename_file");
    std::string const rename_dir("rename_dir");
  }
}

static void 
parse_path_rearrangement(basic_io::parser & parser,
                         change_set & cs)
{
  while (parser.symp())
    {
      std::string t1, t2;
      if (parser.symp(syms::add_file)) 
        { 
          parser.sym();
          parser.str(t1);
          cs.add_file(file_path(t1));
        }
      else if (parser.symp(syms::delete_file)) 
        { 
          parser.sym();
          parser.str(t1);
          cs.delete_file(file_path(t1));
        }
      else if (parser.symp(syms::delete_dir)) 
        { 
          parser.sym();
          parser.str(t1);
          cs.delete_dir(file_path(t1));
        }
      else if (parser.symp(syms::rename_file)) 
        { 
          parser.sym();
          parser.str(t1);
          parser.esym(syms::to);
          parser.str(t2);
          cs.rename_file(file_path(t1),
                         file_path(t2));
        }
      else if (parser.symp(syms::rename_dir)) 
        { 
          parser.sym();
          parser.str(t1);
          parser.esym(syms::to);
          parser.str(t2);
          cs.rename_dir(file_path(t1),
                        file_path(t2));
        }
      else
        break;
    }
  cs.rearrangement.check_sane();
}


void 
print_insane_path_rearrangement(basic_io::printer & printer,
                                change_set::path_rearrangement const & pr)
{

  for (std::set<file_path>::const_iterator i = pr.deleted_files.begin();
       i != pr.deleted_files.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::delete_file, (*i)());
      printer.print_stanza(st);
    }

  for (std::set<file_path>::const_iterator i = pr.deleted_dirs.begin();
       i != pr.deleted_dirs.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::delete_dir, (*i)());
      printer.print_stanza(st);
    }

  for (std::map<file_path,file_path>::const_iterator i = pr.renamed_files.begin();
       i != pr.renamed_files.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::rename_file, i->first());
      st.push_str_pair(syms::to, i->second());
      printer.print_stanza(st);
    }

  for (std::map<file_path,file_path>::const_iterator i = pr.renamed_dirs.begin();
       i != pr.renamed_dirs.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::rename_dir, i->first());
      st.push_str_pair(syms::to, i->second());
      printer.print_stanza(st);
    }

  for (std::set<file_path>::const_iterator i = pr.added_files.begin();
       i != pr.added_files.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::add_file, (*i)());
      printer.print_stanza(st);
    }
}

void 
print_path_rearrangement(basic_io::printer & printer,
                         change_set::path_rearrangement const & pr)
{
  pr.check_sane();
  print_insane_path_rearrangement(printer, pr);
}

void 
parse_change_set(basic_io::parser & parser,
                 change_set & cs)
{
  clear_change_set(cs);

  parse_path_rearrangement(parser, cs);    

  while (parser.symp(syms::patch))
    {
      std::string path, src, dst;
      parser.sym();
      parser.str(path);
      parser.esym(syms::from);
      parser.hex(src);
      parser.esym(syms::to);
      parser.hex(dst);
      cs.deltas.insert(std::make_pair(file_path(path),
                                      std::make_pair(file_id(src),
                                                     file_id(dst))));
    }
  cs.check_sane();
}

void 
print_insane_change_set(basic_io::printer & printer,
                        change_set const & cs)
{
  print_insane_path_rearrangement(printer, cs.rearrangement);
  
  for (change_set::delta_map::const_iterator i = cs.deltas.begin();
       i != cs.deltas.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::patch, i->first());
      st.push_hex_pair(syms::from, i->second.first.inner()());
      st.push_hex_pair(syms::to, i->second.second.inner()());
      printer.print_stanza(st);
    }
}

void 
print_change_set(basic_io::printer & printer,
                 change_set const & cs)
{
  cs.check_sane();
  print_insane_change_set(printer, cs);
}

void
read_path_rearrangement(data const & dat,
                        change_set::path_rearrangement & re)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "path_rearrangement");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  change_set cs;
  parse_path_rearrangement(pars, cs);
  re = cs.rearrangement;
  I(src.lookahead == EOF);
  re.check_sane();
}

void
read_change_set(data const & dat,
                change_set & cs)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "change_set");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_change_set(pars, cs);
  I(src.lookahead == EOF);
  cs.check_sane();
}

void
write_insane_change_set(change_set const & cs,
                        data & dat)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  print_insane_change_set(pr, cs);
  dat = data(oss.str());  
}

void
write_change_set(change_set const & cs,
                 data & dat)
{
  cs.check_sane();
  write_insane_change_set(cs, dat);
}

void
write_insane_path_rearrangement(change_set::path_rearrangement const & re,
                                data & dat)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  print_insane_path_rearrangement(pr, re);
  dat = data(oss.str());  
}

void
write_path_rearrangement(change_set::path_rearrangement const & re,
                         data & dat)
{
  re.check_sane();
  write_insane_path_rearrangement(re, dat); 
}

void
dump(change_set const & cs, std::string & out)
{
  data tmp;
  write_insane_change_set(cs, tmp);
  out = tmp();
}

void
dump(change_set::path_rearrangement const & pr, std::string & out)
{
  data tmp;
  write_insane_path_rearrangement(pr, tmp);
  out = tmp();
}

void
dump(std::vector<change_set::path_rearrangement> const & obj,
     std::string & out)
{
  out.clear();
  std::string tmp;
  for (std::vector<change_set::path_rearrangement>::const_iterator
         i = obj.begin(); i != obj.end(); ++i)
    {
      dump(*i, tmp);
      out += tmp + "\n\n";
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "sanity.hh"
#include "transforms.hh"

static void dump_change_set(std::string const & ctx,
                            change_set const & cs)
{
  data tmp;
  write_change_set(cs, tmp);
  L(F("[begin changeset %s]\n") % ctx);
  std::vector<std::string> lines;
  split_into_lines(tmp(), lines);
  for (std::vector<std::string>::const_iterator i = lines.begin();
       i != lines.end(); ++i)
    L(F("%s") % *i);
  L(F("[end changeset %s]\n") % ctx);
}

static void
spin_change_set(change_set const & cs)
{
  data tmp1;
  change_set cs1;
  write_change_set(cs, tmp1);
  dump_change_set("normalized", cs);
  read_change_set(tmp1, cs1);
  for (int i = 0; i < 5; ++i)
    {
      data tmp2;
      change_set cs2;
      write_change_set(cs1, tmp2);
      BOOST_CHECK(tmp1 == tmp2);
      read_change_set(tmp2, cs2);
      BOOST_CHECK(cs1.rearrangement == cs2.rearrangement);
      BOOST_CHECK(cs1.deltas == cs2.deltas);
      cs1 = cs2;      
    }
}

static void 
basic_change_set_test()
{
  try
    {
      
      change_set cs;
      cs.delete_file(file_path("usr/lib/zombie"));
      cs.add_file(file_path("usr/bin/cat"),
                  file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));
      cs.add_file(file_path("usr/local/bin/dog"),
                  file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));
      cs.rename_file(file_path("usr/local/bin/dog"), file_path("usr/bin/dog"));
      cs.rename_file(file_path("usr/bin/cat"), file_path("usr/local/bin/chicken"));
      cs.add_file(file_path("usr/lib/libc.so"),
                  file_id(hexenc<id>("435e816c30263c9184f94e7c4d5aec78ea7c028a")));
      // FIXME: this should be valid, but our directory semantics are broken.  Re-add
      // tests for things like this when fixing directory semantics!  (see bug tracker)
      // cs.rename_dir(file_path("usr/lib"), file_path("usr/local/lib"));
      cs.rename_dir(file_path("some/dir"), file_path("some/other/dir"));
      cs.apply_delta(file_path("usr/local/bin/chicken"), 
                     file_id(hexenc<id>("c6a4a6196bb4a744207e1a6e90273369b8c2e925")),
                     file_id(hexenc<id>("fe18ec0c55cbc72e4e51c58dc13af515a2f3a892")));
      spin_change_set(cs);
    }
  catch (informative_failure & exn)
    {
      L(F("informative failure: %s\n") % exn.what);
    }
  catch (std::runtime_error & exn)
    {
      L(F("runtime error: %s\n") % exn.what());
    }
}

static void
invert_change_test()
{
  L(F("STARTING invert_change_test\n"));
  change_set cs;
  manifest_map a;

  a.insert(std::make_pair(file_path("usr/lib/zombie"),
                          file_id(hexenc<id>("92ceb3cd922db36e48d5c30764e0f5488cdfca28"))));
  cs.delete_file(file_path("usr/lib/zombie"));
  cs.add_file(file_path("usr/bin/cat"),
              file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));
  cs.add_file(file_path("usr/local/dog"),
              file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));
  a.insert(std::make_pair(file_path("usr/foo"),
                          file_id(hexenc<id>("9a4d3ae90b0cc26758e17e1f80229a13f57cad6e"))));
  cs.rename_file(file_path("usr/foo"), file_path("usr/bar"));
  cs.apply_delta(file_path("usr/bar"),
                 file_id(hexenc<id>("9a4d3ae90b0cc26758e17e1f80229a13f57cad6e")),
                 file_id(hexenc<id>("fe18ec0c55cbc72e4e51c58dc13af515a2f3a892")));
  a.insert(std::make_pair(file_path("usr/quuux"),
                          file_id(hexenc<id>("fe18ec0c55cbc72e4e51c58dc13af515a2f3a892"))));
  cs.apply_delta(file_path("usr/quuux"),
                 file_id(hexenc<id>("fe18ec0c55cbc72e4e51c58dc13af515a2f3a892")),
                 file_id(hexenc<id>("c6a4a6196bb4a744207e1a6e90273369b8c2e925")));

  manifest_map b;
  apply_change_set(a, cs, b);

  dump_change_set("invert_change_test, cs", cs);
  change_set cs2, cs3;
  invert_change_set(cs, a, cs2);
  dump_change_set("invert_change_test, cs2", cs2);
  invert_change_set(cs2, b, cs3);
  dump_change_set("invert_change_test, cs3", cs3);
  BOOST_CHECK(cs.rearrangement == cs3.rearrangement);
  BOOST_CHECK(cs.deltas == cs3.deltas);
  L(F("ENDING invert_change_test\n"));
}

static void 
neutralize_change_test()
{
  try
    {
      
      change_set cs1, cs2, csa;
      cs1.add_file(file_path("usr/lib/zombie"),
                   file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));
      cs1.rename_file(file_path("usr/lib/apple"),
                      file_path("usr/lib/orange"));
      cs1.rename_dir(file_path("usr/lib/moose"),
                     file_path("usr/lib/squirrel"));

      dump_change_set("neutralize target", cs1);

      cs2.delete_file(file_path("usr/lib/zombie"));
      cs2.rename_file(file_path("usr/lib/orange"),
                      file_path("usr/lib/apple"));
      cs2.rename_dir(file_path("usr/lib/squirrel"),
                     file_path("usr/lib/moose"));

      dump_change_set("neutralizer", cs2);
      
      concatenate_change_sets(cs1, cs2, csa);

      dump_change_set("neutralized", csa);

      tid_source ts;
      path_analysis analysis;
      analyze_rearrangement(csa.rearrangement, analysis, ts);

      BOOST_CHECK(analysis.first.empty());
      BOOST_CHECK(analysis.second.empty());
    }
  catch (informative_failure & exn)
    {
      L(F("informative failure: %s\n") % exn.what);
    }
  catch (std::runtime_error & exn)
    {
      L(F("runtime error: %s\n") % exn.what());
    }
}

static void 
non_interfering_change_test()
{
  try
    {
      
      change_set cs1, cs2, csa;
      cs1.delete_file(file_path("usr/lib/zombie"));
      cs1.rename_file(file_path("usr/lib/orange"),
                      file_path("usr/lib/apple"));
      cs1.rename_dir(file_path("usr/lib/squirrel"),
                     file_path("usr/lib/moose"));

      dump_change_set("non-interference A", cs1);

      cs2.add_file(file_path("usr/lib/zombie"),
                   file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));
      cs2.rename_file(file_path("usr/lib/pear"),
                      file_path("usr/lib/orange"));
      cs2.rename_dir(file_path("usr/lib/spy"),
                     file_path("usr/lib/squirrel"));
      
      dump_change_set("non-interference B", cs2);

      concatenate_change_sets(cs1, cs2, csa);

      dump_change_set("non-interference combined", csa);

      tid_source ts;
      path_analysis analysis;
      analyze_rearrangement(csa.rearrangement, analysis, ts);

      BOOST_CHECK(analysis.first.size() == 8);
      BOOST_CHECK(analysis.second.size() == 8);
    }
  catch (informative_failure & exn)
    {
      L(F("informative failure: %s\n") % exn.what);
    }
  catch (std::runtime_error & exn)
    {
      L(F("runtime error: %s\n") % exn.what());
    }
}

static const file_id fid_null;
static const file_id fid1 = file_id(hexenc<id>("aaaa3831e5eb74e6cd50b94f9e99e6a14d98d702"));
static const file_id fid2 = file_id(hexenc<id>("bbbb3831e5eb74e6cd50b94f9e99e6a14d98d702"));
static const file_id fid3 = file_id(hexenc<id>("cccc3831e5eb74e6cd50b94f9e99e6a14d98d702"));

typedef enum { in_a, in_b } which_t;
struct bad_concatenate_change_test
{
  change_set a;
  change_set b;
  change_set combined;
  change_set concat;
  bool do_combine;
  std::string ident;
  bad_concatenate_change_test(char const *file, int line) : 
    do_combine(false),
    ident((F("%s:%d") % file % line).str())
  {    
    L(F("BEGINNING concatenation test %s\n") % ident);
  }

  ~bad_concatenate_change_test()
  {
    L(F("FINISHING concatenation test %s\n") % ident);
  }

  change_set & getit(which_t which)
  {
    if (which == in_a)
      return a;
    return b;
  }
  // Call combine() if you want to make sure that the things that are bad when
  // concatenated are also bad when all stuck together into a single
  // changeset.
  void combine() { do_combine = true; }
  void add_file(which_t which, std::string const & path, file_id fid = fid1)
  {
    getit(which).add_file(file_path(path), fid);
    if (do_combine)
      combined.add_file(file_path(path), fid);
  }
  void apply_delta(which_t which, std::string const & path,
                   file_id from_fid,
                   file_id to_fid)
  {
    getit(which).apply_delta(file_path(path), from_fid, to_fid);
    if (do_combine)
      combined.apply_delta(file_path(path), from_fid, to_fid);
  }
  void delete_file(which_t which, std::string const & path)
  {
    getit(which).delete_file(file_path(path));
    if (do_combine)
      combined.delete_file(file_path(path));
  }
  void delete_dir(which_t which, std::string const & path)
  {
    getit(which).delete_dir(file_path(path));
    if (do_combine)
      combined.delete_dir(file_path(path));
  }
  void rename_file(which_t which,
                   std::string const & path1, std::string const & path2)
  {
    getit(which).rename_file(file_path(path1), file_path(path2));
    if (do_combine)
      combined.rename_file(file_path(path1), file_path(path2));
  }
  void rename_dir(which_t which,
                  std::string const & path1, std::string const & path2)
  {
    getit(which).rename_dir(file_path(path1), file_path(path2));
    if (do_combine)
      combined.rename_dir(file_path(path1), file_path(path2));
  }
  void run()
  {
    L(F("RUNNING bad_concatenate_change_test %s\n") % ident);
    try
      {
        dump_change_set("a", a);
        dump_change_set("b", b);
      }
    catch (std::logic_error e)
      {
        L(F("skipping change_set printing, one or both are not sane\n"));
      }
    BOOST_CHECK_THROW(concatenate_change_sets(a, b, concat),
                      std::logic_error);
    try { dump_change_set("concat", concat); }
    catch (std::logic_error e) { L(F("concat change_set is insane\n")); }
    if (do_combine)
      {
        L(F("Checking combined change set\n"));
        change_set empty_cs, combined_concat;
        BOOST_CHECK_THROW(concatenate_change_sets(combined,
                                                  empty_cs,
                                                  combined_concat),
                          std::logic_error);
        try { dump_change_set("combined_concat", combined_concat); }
        catch (std::logic_error e) { L(F("combined_concat is insane\n")); }
      }
  }
  void run_both()
  {
    run();
    L(F("RUNNING bad_concatenate_change_test %s again backwards\n") % ident);
    BOOST_CHECK_THROW(concatenate_change_sets(a, b, concat),
                      std::logic_error);
  }
};

// We also do a number of just "bad change set" tests here, leaving one of
// them empty; this is because our main line of defense against bad
// change_sets, check_sane_history, does its checking by doing
// concatenations, so it's doing concatenations that we want to be sure does
// sanity checking.
static void
bad_concatenate_change_tests()
{
  // Files/directories can't be dropped on top of each other:
  BOOST_CHECKPOINT("on top");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target");
    t.add_file(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.rename_file(in_a, "foo", "target");
    t.rename_file(in_b, "bar", "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.rename_dir(in_a, "foo", "target");
    t.rename_dir(in_b, "bar", "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.rename_file(in_a, "foo", "target");
    t.rename_dir(in_b, "bar", "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target");
    t.rename_file(in_b, "foo", "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target");
    t.rename_dir(in_b, "foo", "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target/subfile");
    t.add_file(in_b, "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target/subfile");
    t.rename_file(in_b, "foo", "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target/subfile");
    t.rename_dir(in_b, "foo", "target");
    t.run_both();
  }
  // You can only delete something once
  BOOST_CHECKPOINT("delete once");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.delete_file(in_a, "target");
    t.delete_file(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_file(in_a, "target");
    t.delete_dir(in_b, "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.delete_dir(in_a, "target");
    t.delete_dir(in_b, "target");
    t.run();
  }
  // You can't delete something that's not there anymore
  BOOST_CHECKPOINT("delete after rename");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_file(in_a, "target");
    t.rename_file(in_b, "target", "foo");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_dir(in_a, "target");
    t.rename_file(in_b, "target", "foo");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_dir(in_a, "target");
    t.rename_dir(in_b, "target", "foo");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_file(in_a, "target");
    t.rename_dir(in_b, "target", "foo");
    t.run_both();
  }
  // Files/directories can't be split in two
  BOOST_CHECKPOINT("splitting files/dirs");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_file(in_a, "target", "foo");
    t.rename_file(in_b, "target", "bar");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_dir(in_a, "target", "foo");
    t.rename_dir(in_b, "target", "bar");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.rename_dir(in_a, "target", "foo");
    t.rename_file(in_b, "target", "bar");
    t.run_both();
  }
  // Files and directories are different
  BOOST_CHECKPOINT("files != dirs");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target");
    t.delete_dir(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target/subfile");
    t.delete_file(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target/subfile");
    t.rename_file(in_b, "target", "foo");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_file(in_a, "foo", "target");
    t.delete_dir(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.apply_delta(in_a, "target", fid1, fid2);
    t.delete_dir(in_b, "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_dir(in_a, "foo", "target");
    t.delete_file(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.rename_dir(in_a, "foo", "target");
    t.apply_delta(in_b, "target", fid1, fid2);
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.apply_delta(in_a, "target", fid1, fid2);
    t.rename_dir(in_b, "target", "bar");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_file(in_a, "foo", "target");
    t.rename_dir(in_b, "target", "bar");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_dir(in_a, "foo", "target");
    t.rename_file(in_b, "target", "bar");
    t.run();
  }
  // Directories can't be patched, and patches can't be directoried...
  BOOST_CHECKPOINT("can't patch dirs or vice versa");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target/subfile");
    t.apply_delta(in_b, "target", fid_null, fid1);
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target/subfile");
    t.apply_delta(in_b, "target", fid1, fid2);
    t.run_both();
  }
  // Deltas must be consistent
  BOOST_CHECKPOINT("consistent deltas");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.apply_delta(in_a, "target", fid1, fid2);
    t.apply_delta(in_b, "target", fid3, fid1);
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target", fid1);
    t.apply_delta(in_b, "target", fid2, fid3);
    t.run();
  }
  // Can't have a null source id if it's not an add
  BOOST_CHECKPOINT("null id on non-add");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.apply_delta(in_a, "target", fid_null, fid1);
    t.run();
  }
  // Can't have drop + delta with no add
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_file(in_a, "target");
    t.apply_delta(in_b, "target", fid1, fid2);
    t.run();
  }
  // Can't have a null destination id, ever, with or without a delete_file
  BOOST_CHECKPOINT("no null destinations");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.delete_file(in_a, "target");
    t.apply_delta(in_a, "target", fid1, fid_null);
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.apply_delta(in_a, "target", fid1, fid_null);
    t.run();
  }
  // Can't have a patch with src == dst
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.apply_delta(in_a, "target", fid1, fid1);
    t.run();
  }
}

// FIXME: Things that should be added, but can't be trivially because they
// assert too early:
//   anything repeated -- multiple adds, multiple deletes, multiple deltas
//   including <rename a b, rename a c> in one changeset, for both files and dirs
// (probably should put these in strings, and do BOOST_CHECK_THROWS in the
// parser?)

// FIXME: also need tests for the invariants in apply_manifest (and any
// invariants that should be there but aren't, of course)

void 
add_change_set_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&basic_change_set_test));
  suite->add(BOOST_TEST_CASE(&neutralize_change_test));
  suite->add(BOOST_TEST_CASE(&non_interfering_change_test));
  suite->add(BOOST_TEST_CASE(&bad_concatenate_change_tests));
  suite->add(BOOST_TEST_CASE(&invert_change_test));
}


#endif // BUILD_UNIT_TESTS
