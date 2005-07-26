/*

nodes:
~~~~~~

 a node is either a file or a directory. nodes have attributes, an ident, a
 parent-ident, possibly an heir, and a set of sires. directory nodes have a
 map of children and a map of clobbered-children. file nodes have a content
 hash. see below for the definitions of these members.


mfests:
~~~~~~~

 an mfest is an index-set of nodes X and a tree T of nodes starting from a
 root. the index X maps ident numbers to shared pointers into T. there may
 be entries in X which are not in T. T must always be a well-formed tree.

 an mfest has a normal form, in which:
 
  - a node is in X iff it is in T
  - all nodes in the tree have empty heirs and sires
    (see below for definition of these)

 serializing and re-reading an mfest normalizes it (though there is a
 faster, in-place function to normalize them as well).


csets:
~~~~~~

 a cset is a pair of mfests A, B. the mfests in a cset are *not*
 normalized. it is an invariant that idents(A.X) = idents(B.X), but it is
 only sometimes true that idents(A.T) = idents(B.T); some nodes might be
 present in one mfest's tree but absent from the other's (if they were
 added or deleted).


change_consumers:
~~~~~~~~~~~~~~~~~

 a change_consumer -- and the serial form of a change_set -- is the
 way a human wants to read about some work: organized into a set of
 deletes, moves, adds, deltas, and attribute set/clear operations.

 there is ambiguity in a change_consumer regarding the resolution of
 names and the simultineity of operations. specifically, each entry in
 a change_set is either looked up in the pre state or post state mfest
 of a change. a delete is looked up in the pre state. a rename's
 source in the pre state. a rename's destination in the post state. an
 add is looked up in the post state.

 when playing back a change_set, there is a canonical order in which
 entries are emitted: set_heir, delete, rename, add, set_sire,
 apply_delta, clear_attr, set_attr.

 furthermore within each type of entry, the order in which the entries
 are emitted is specified: all entries are emitted in lexicographic
 order, which happens to induce topological order as well (A is always
 emitted before A/B).

 crucially, set_heir and delete are ordered by *source space*; rename,
 add, set_sire, apply_delta, clear_attr, and set_attr are ordered by
 *destination space*. we replay these by walking the tree rooted in
 each mfest.


heirs and sires:
~~~~~~~~~~~~~~~~

 nodes may have heirs or sires. only nodes being deleted in a cset may have
 an heir; only nodes being added in a cset may have a sire. a node may have
 at most one heir. the heir of a node is a target to send future content
 deltas and attributes to; it is a "forwarding address" used in cases where
 two files with separate histories are considered identical in a merge and
 "sutured": one node is deleted, and it marks the other node as an
 heir. the name of the heir is looked up in the new manifest.

 only attribute and content-merging passes care about heirs and sires.
 they do not affect lifecycle decisions during merging.

 an added node A has a node S as sire in cset C iff there is a cset C' in
 which A was being deleted with heir S, and C' = inverse(C). in other
 words, sires exist *only* to preserve information about heirs during cset
 inversion. there are no user-accessible primitives for creating sire
 relationships. a node S may be the sire of many other nodes N1,...,Nk, if
 multiple Nk consider S their heir. the sire relationship is therefore a
 set.


generation numbers:
~~~~~~~~~~~~~~~~~~~
 
 every node in an mfest has a generation number. this exists to
 implement copy-on-write semantics. the cset has a "write_generation"
 number representing the generation which nodes should be updated to
 when written to in the new mfest. when you want a node for writing,
 you request it from the new mfest and the node-finder walks down the tree
 looking. any time it hits an intermediate node which is less than the
 write_generation, it makes a copy of that node and replaces the owning
 pointer before walking into it. this ensures that the path you modify
 is a truly new object, not a pointer into to the old tree.


attach and detach lists:
~~~~~~~~~~~~~~~~~~~~~~~~

 this is a subtle point. read it until it's clear. 

 csets contain an implicit order to their operations: a block of
 set_heir actions, then deletes, renames, adds, set_sires, deltas, and
 finally attribute changes. moreover as we've seen above, within each
 block there is a topological order: renames landing on A/B happen
 before renames landing on A/B/C, for example.

 there is still, however, an ordering hazard when you apply renames
 "in-place", either to an mfest or a filesystem: it's possible that a
 rename-target will land on a name which is *in the process* of being
 moved elsewhere -- by another rename in the cset -- but has not yet
 been moved due to the serial order chosen.  consider this cset:

    rename_file "A/B"
             to "P/Q"

    rename_file "P/Q"
            to  "Y/Z"

 this is the order they will be emitted in. because these names exist
 in the same cset, they are to be executed "simultaneously" in theory;
 but programs execute sequentially, so naively we would perform the
 first rename, then the second.

 suppose we executed them "sequentially". for the first rename we
 would do this:
 
 - find the node A/B in the OLD mfest, its file ident is file_0.
 - find file_0 in the NEW mfest, its parent is parent_0. 
 - detach B from parent_0 in the NEW mfest.
 - find P in the NEW mfest, its ident is parent_1.
 - attach file_0 to parent_1 under the name Q. 

 this last action will fail. it will fail because parent_1 already has
 a child called Q in the second mfest (it's the source of the next
 rename, and it hasn't been moved elsewhere yet). we the readers know
 that by the time the cset is finished applying, Q will have been
 detached from parent_1 in the NEW mfest. but in the meantime we have
 a problem.

 so what we do instead is execute the first "half" of the renames in a
 preliminary pass, then the second "half" of them in a second
 pass. the change_consumer interface has a special
 "finalize_renames()" method which triggers the second half; it is
 called after the last rename is processed. here's how we *actually*
 process the renames:

  - buffer all the renames into a vector of src/dst pairs, in order. 
  - when the user calls finalize_renames():
    - build a temporary vector of idents with the same length as the buffer
    - walk the vector from end to beginning, detaching each node from 
      the NEW tree and saving its ident in the temporary vector
    - walk the vector from beginning to end, attaching each node to 
      the NEW tree, using the saved idents

 */


#define __STDC_CONSTANT_MACROS
#define __STDC_LIMIT_MACROS


#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <stack>
#include <string>
#include <vector>


#include <ext/hash_map>
#include <ext/hash_set>


#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/filesystem/path.hpp>


#include "basic_io.hh"
#include "constants.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "vocab.hh"


using std::deque;
using std::map;
using std::ostream;
using std::pair;
using std::set;
using std::stack;
using std::string;
using std::vector;
using std::inserter;
using std::copy;
using std::make_pair;
using boost::lexical_cast;
using boost::scoped_ptr;
using boost::shared_ptr;
using boost::dynamic_pointer_cast;
using __gnu_cxx::hash_map;
using __gnu_cxx::hash_set;


struct node;
struct dir_node;
struct file_node;
struct dir_entry;
struct dirent_t_cmp;
struct dirent_hash;
struct dirent_eq;


namespace __gnu_cxx
{
  template<>
  struct hash<u64>
  {
    size_t
    operator()(u64 __x) const
    { return static_cast<size_t>(__x); }
  };
}


typedef uint64_t ident_t;
typedef uint64_t gen_t;
typedef shared_ptr<node> node_t;
typedef shared_ptr<dir_node> dir_t;
typedef shared_ptr<file_node> file_t;
typedef shared_ptr<dir_entry> dirent_t;
// dirmap *must* be a sorted map, do not change to a hashed map
typedef map<dirent_t, node_t> dirmap_t; 
typedef hash_set<dirent_t, dirent_hash, dirent_eq> dirset_t;
typedef string attr_name;
typedef string attr_val;


static ident_t
null_ident = 0;
  

static ident_t 
nursery_ident = 1;


static ident_t 
graveyard_ident = 2;


struct
ident_source
{
  ident_t ctr;
  ident_source() : ctr(graveyard_ident + 1) {}
  ident_t next() { I(ctr != UINT64_MAX); return ctr++; }
};


//==================================================================
// dirents and paths
//==================================================================

// directory entries are globally interned components
// of a file_path

struct 
dir_entry
{
  string val;

  explicit dir_entry(string s) 
    : val(s) 
  {}

  bool operator<(dir_entry const & other) const
  {
    return val < other.val;
  }

  bool operator==(dir_entry const & other) const
  {
    return val == other.val;
  }
};


dirent_t
null_dirent(new dir_entry(""));


bool
operator<(dirent_t const & a,
	  dirent_t const & b)
{
  return *a < *b;
}


ostream &
operator<<(ostream & o, dirent_t const & d)
{
  return o << d->val;
}


struct 
dirent_hash
{ 
  size_t operator()(dirent_t const & x) const
  { 
    return __gnu_cxx::__stl_hash_string(x->val.c_str()); 
  }
};


struct 
dirent_eq
{
  bool operator()(dirent_t const & a,
		  dirent_t const & b) const
  {
    return *a == *b;
  }
};


// this helper class represents an "unpacked" view of the path
// through a tree of directory nodes to some specific leaf (either
// dir or file); it is just a faster form of a non-empty file_path

struct 
path_vec_t
{
  static dirset_t dset;
  vector<dirent_t> dir;
  dirent_t leaf;

  path_vec_t()
  {
  }

  static dirent_t intern_component(string const & s)
  {
    dirent_t tmp(new dir_entry(s));
    dirset_t::const_iterator i = dset.find(tmp);
    if (i == dset.end())
      {
        dset.insert(tmp);
      }
    else
      {
        tmp = *i;
      }
    return tmp;
  }

  static path_vec_t from_file_path(file_path const & f)
  {
    fs::path fp(f());
    path_vec_t pv;
    for (fs::path::iterator i = fp.begin(); 
	 i != fp.end(); ++i)
      {
	dirent_t tmp = intern_component(*i);
	pv.dir.push_back(tmp);
      }
    I(!pv.dir.empty());
    pv.leaf = pv.dir.back();
    pv.dir.pop_back();
    return pv;
  }

  void operator/=(dirent_t d)
  {
    dir.push_back(leaf);
    leaf = d;
  }

  file_path to_file_path() const
  {
    fs::path fp;
    for (vector<dirent_t>::const_iterator i = dir.begin();
	 i != dir.end(); ++i)
      {
	fp /= (*i)->val;
      }
    fp /= leaf->val;
    return file_path(fp.string());
  }

};


//==================================================================
// nodes
//==================================================================

// static bucket of shared pointers
dirset_t
path_vec_t::dset;


struct 
node
{
  gen_t generation;
  ident_t ident;
  ident_t parent;
  dirent_t name;

  // very few nodes have any attributes, heirs, or sires.  a node with
  // any of these is "fancy", and we keep fancy parts in a secondary
  // struture, under a scoped pointer, to save memory. it only gets a
  // value if someone assigns to one of the fancy parts.
  
  struct fancy_part
  {  
    ident_t sire;
    ident_t heir;
    map<attr_name, attr_val> attrs;
  };

  scoped_ptr<fancy_part> fancy;
  void ensure_fancy()
  {
    if (!fancy)
      {
	fancy.reset(new fancy_part());
	fancy->sire = null_ident;
	fancy->heir = null_ident;	
      }
  }

  bool has_sire() const 
  { 
    return fancy && (fancy->sire != null_ident);
  }

  bool has_heir() const 
  { 
    return fancy && (fancy->heir != null_ident); 
  }

  ident_t get_sire() const 
  { 
    I(fancy); 
    return fancy->sire; 
  }

  ident_t get_heir() const 
  { 
    I(fancy);
    return fancy->heir;
  }

  void set_sire(ident_t s) 
  { 
    ensure_fancy();
    fancy->sire = s; 
  }  

  void set_heir(ident_t h) 
  { 
    ensure_fancy();
    fancy->heir = h;
  }

  bool has_attrs() 
  {
    return fancy && !fancy->attrs.empty();
  }

  bool has_attr(attr_name const & name) 
  { 
    return fancy && (fancy->attrs.find(name) !=
		     fancy->attrs.end()); 
  }

  attr_val const & get_attr(attr_name const & name) 
  { 
    I(fancy);
    map<attr_name, attr_val>::const_iterator i = fancy->attrs.find(name);
    I(i != fancy->attrs.end());
    return i->second;
  }

  void set_attr(attr_name const & name,
		attr_val const & val) 
  {
    ensure_fancy();
    fancy->attrs[name] = val;
  }

  void clear_attr(attr_name const & name) 
  {
    ensure_fancy();
    fancy->attrs.erase(name);
  }

  bool unborn() const 
  { 
    return parent == nursery_ident; 
  }

  bool live() const 
  { 
    return ! (unborn() || killed()); 
  }

  bool killed() const 
  { 
    return parent == graveyard_ident; 
  }

  node() 
    : generation(0),
      ident(null_ident),
      parent(null_ident),
      name(null_dirent)
  {}

  virtual node_t shallow_copy() const = 0;
  virtual ~node() {}  
};


struct 
file_node 
  : public node
{
  file_id content;
  
  virtual node_t shallow_copy() const;
  virtual ~file_node() {}
};


node_t 
file_node::shallow_copy() const
{
  file_t f = file_t(new file_node());
  f->generation = 0;
  f->ident = ident;
  f->parent = parent;
  f->name = name;

  if (fancy)
    f->fancy.reset(new fancy_part(*fancy));

  f->content = content;

  // L(F("shallow-copied file node %d='%s'\n") % ident % name);

  return f;
}


struct
dir_node 
  : public node
{
  dirmap_t entries;

  // informative
  bool contains_entry(dirent_t p) const;

  // imperative (fault on lookup failures)
  node_t get_entry(dirent_t p) const;
  void add_child(dirent_t name, node_t n);
  void drop_child(dirent_t c);

  virtual node_t shallow_copy() const;
  virtual ~dir_node() {}
};


void 
dir_node::add_child(dirent_t name, node_t n)
{
  I(!contains_entry(name));
  // L(F("parent %d adding child %d = '%s'\n") % ident % n->ident % name->val);
  n->name = name;
  n->parent = ident;
  entries.insert(make_pair(name,n));
}


void 
dir_node::drop_child(dirent_t c)
{
  I(contains_entry(c));
  entries.erase(c);
}


static inline bool 
is_dir_t(node_t n)
{
  dir_t d = dynamic_pointer_cast<dir_node, node>(n);
  return static_cast<bool>(d);
}


static inline bool 
is_file_t(node_t n)
{
  file_t f = dynamic_pointer_cast<file_node, node>(n);
  return static_cast<bool>(f);
}


static inline dir_t 
downcast_to_dir_t(node_t const n)
{
  dir_t d = dynamic_pointer_cast<dir_node, node>(n);
  I(static_cast<bool>(d));
  return d;
}


static inline file_t 
downcast_to_file_t(node_t const n)
{
  file_t f = dynamic_pointer_cast<file_node, node>(n);
  I(static_cast<bool>(f));
  return f;
}


struct 
dfs_iter
{
  // NB: the dfs_iter struct *does not return* the node it's
  // constructed on, as part of its iteration 

  stack< pair<dir_t, dirmap_t::const_iterator> > stk;

  dfs_iter(dir_t root)
  {
    if (!root->entries.empty())
      stk.push(make_pair(root, root->entries.begin()));
  }

  bool finished()
  {
    return stk.empty();
  }

  dir_t cwd()
  {
    I(!stk.empty());
    return stk.top().first;
  }

  node_t operator*()
  {
    I(!stk.empty());
    //     L(F("dfs_iter dereferencing node in '%s'\n") 
    //       % stk.top().first->name);
    return stk.top().second->second;
  }

  void operator++()
  {
    if (stk.empty())
      return;

    node_t ntmp = stk.top().second->second;
    if (is_dir_t(ntmp))
      {
	dir_t dtmp = downcast_to_dir_t(ntmp);
	stk.push(make_pair(dtmp, dtmp->entries.begin()));
      }
    else
      ++(stk.top().second);

    while (!stk.empty() 
	   && stk.top().second == stk.top().first->entries.end())
      {
	stk.pop();
	if (!stk.empty())
	  ++stk.top().second;
      }
  }
};


struct 
bfs_iter
{
  deque<node_t> q;
  bfs_iter(node_t root)
  {
    //     if (is_dir_t(root))
    //       L(F("bfs_iter starting with node %d (%d children)\n") 
    // 	% root->ident % downcast_to_dir_t(root)->entries.size());
    //     else
    //       L(F("bfs_iter starting with node %d\n") 
    // 	% root->ident);
    q.push_back(root);
  }

  bool finished()
  {
    return q.empty();
  }

  node_t operator*()
  {
    I(!q.empty());
    return q.front();
  }

  void operator++()
  {
    if (q.empty())
      return;

    if (is_dir_t(q.front()))
      {
	dir_t tmp = downcast_to_dir_t(q.front());
	for (dirmap_t::const_iterator i = tmp->entries.begin();
	     i != tmp->entries.end(); ++i)
	  {
            // 	    L(F("bfs_iter adding node %d, child of %d\n") 
            // 	      % i->second->ident % tmp->ident);
	    q.push_back(i->second);
	  }
      }
    q.pop_front();
  }
};


bool 
dir_node::contains_entry(dirent_t p) const
{
  map<dirent_t,node_t>::const_iterator i = entries.find(p);
  if (i == entries.end())
    return false;
  return true;
}


node_t 
dir_node::get_entry(dirent_t p) const
{
  // L(F("dir_node::get_entry(\"%s\")\n") % p->val);
  dirmap_t::const_iterator i = entries.find(p);
  I(i != entries.end());
  return i->second;
}


node_t 
dir_node::shallow_copy() const
{
  dir_t d = dir_t(new dir_node());
  d->generation = 0;
  d->ident = ident;
  d->parent = parent;
  d->name = name;

  if (fancy)
    d->fancy.reset(new fancy_part(*fancy));

  d->entries = entries;

  // L(F("shallow-copied directory node %d='%s'\n") % ident % name);

  return d;
}

static node_t
deep_copy(node_t n)
{
  if (is_file_t(n))
    {
      return n->shallow_copy();
    }
  else
    {
      I(is_dir_t(n));
      deque<dir_t> work;
      dir_t new_root = downcast_to_dir_t(n->shallow_copy());
      work.push_back(new_root);
      
      while (!work.empty())
	{
	  dir_t curr = work.front();
	  work.pop_front();
	  dirmap_t new_dirmap;
	  for (dirmap_t::const_iterator i = curr->entries.begin();
	       i != curr->entries.end(); ++i)
	    {
	      node_t tmp = i->second->shallow_copy();
	      new_dirmap.insert(make_pair(i->first, tmp));
	      if (is_dir_t(tmp))
		work.push_back(downcast_to_dir_t(tmp));
	    }
	  curr->entries = new_dirmap;
	}
      return new_root;
    }
}


//==================================================================
// mfests
//==================================================================

typedef hash_map<ident_t, node_t> node_map_t;


static void
index_nodes(dir_t d, node_map_t & nodes)
{
  for (bfs_iter i(d); !i.finished(); ++i)
    nodes.insert(make_pair((*i)->ident, *i));
}


struct 
mfest
{
  ident_t max_ident;
  node_map_t nodes;
  dir_t root;

  dir_t make_dir();
  file_t make_file();

  mfest() : 
    max_ident(graveyard_ident+1), 
    root(new dir_node()) 
  {
    index_nodes(root, nodes);
  }

  mfest(mfest const & other);
  void reset(mfest const & other);
  void check_sane() const;

  // informative
  bool file_exists(file_path fp) const;
  bool dir_exists(file_path dp) const;
  void get_path(node_t const & n, path_vec_t & path) const;

  // imperative (fault on lookup failures)
  node_t get_node(ident_t i) const;
  dir_t get_dir_node(ident_t i) const;
  file_t get_file_node(ident_t i) const;

  node_t get_node(path_vec_t const & n) const;
  dir_t get_dir_node(path_vec_t const & d) const;
  file_t get_file_node(path_vec_t const & f) const;

  node_t get_node(ident_t i, gen_t write_generation);
  dir_t get_dir_node(ident_t i, gen_t write_generation);
  file_t get_file_node(ident_t i, gen_t write_generation);

  node_t get_node(path_vec_t const & n, gen_t write_generation);
  dir_t get_dir_node(path_vec_t const & d, gen_t write_generation);
  file_t get_file_node(path_vec_t const & f, gen_t write_generation);

  dir_t get_containing_dir_node(path_vec_t const & d) const;  
  dir_t get_containing_dir_node(path_vec_t const & d,
				gen_t write_generation);  

  bool operator==(mfest const & other) const;
};


mfest::mfest(mfest const & other)
{
  this->reset(other);
}


void
mfest::reset(mfest const & other)
{
  nodes.clear();
  root = other.root;
  max_ident = other.max_ident;
  index_nodes(root, nodes);
}


dir_t 
mfest::make_dir()
{
  dir_t n(new dir_node());
  n->ident = ++max_ident;
  n->parent = nursery_ident;
  //   L(F("produced new dir %d\n") % n->ident);
  nodes.insert(make_pair(n->ident, n));
  return n;
}


file_t 
mfest::make_file()
{
  file_t n(new file_node());
  n->ident = ++max_ident;
  n->parent = nursery_ident;
  //   L(F("produced new file %d\n") % n->ident);
  nodes.insert(make_pair(n->ident, n));
  return n;
}


void
mfest::check_sane() const
{

  hash_set<ident_t> seen;
  
  // first go from the top of the tree to the bottom checking each
  // directory for absence of cycle-forming edges and agreement
  // between names.

  //   L(F("mfest sanity check beginning...\n"));
  for(bfs_iter i(root); !i.finished(); ++i)
    {
      //       if ((*i)->live())
      // 	{
      // 	  path_vec_t v;
      // 	  get_path(*i, v);
      // 	  L(F("tree iter visiting live node %d = '%s'\n") 
      // 	    % (*i)->ident
      // 	    % v.to_file_path());
      // 	}
      //       else
      // 	{
      // 	  L(F("tree iter visiting %s node %d\n") 
      // 	    % ((*i)->unborn() ? "unborn" : "killed")
      // 	    % (*i)->ident);
      // 	}
      I(seen.find((*i)->ident) == seen.end());
      seen.insert((*i)->ident);
    }
  
  // now go through the node map and make sure that we both found
  // every node, and also that the nodes have the same belief about
  // their ident that the node map has

  for (node_map_t::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      //       if (i->second->live())
      //         {
      //           path_vec_t v;
      //           get_path(i->second, v);
      //           L(F("set iter visiting live node %d = '%s'\n") 
      //             % i->first
      //             % v.to_file_path());
      //         }
      //       else
      //         {
      //           L(F("set iter visiting %s node %d\n") 
      //             % (i->second->unborn() ? "unborn" : "killed")
      //             % i->first);
      //         }
      I(i->first == i->second->ident);
      if (i->second->live())
	{
	  I(seen.find(i->first) != seen.end());
	}
    }    
  // L(F("mfest sanity check done"));
}


bool 
mfest::file_exists(file_path fp) const
{
  path_vec_t v = path_vec_t::from_file_path(fp);
  dir_t d = root;
  vector<dirent_t>::const_iterator i = v.dir.begin(), j = v.dir.end();
  while(i != j)
    {
      if (!d->contains_entry(*i))
	return false;
      
      node_t n = d->get_entry(*i);

      if (!is_dir_t(n))
	return false;

      d = downcast_to_dir_t(n);
    }
  return d->contains_entry(v.leaf) 
    && is_file_t(d->get_entry(v.leaf));
}


bool 
mfest::dir_exists(file_path dp) const
{
  path_vec_t v = path_vec_t::from_file_path(dp);
  dir_t d = root;
  vector<dirent_t>::const_iterator i = v.dir.begin(), j = v.dir.end();
  while(i != j)
    {
      if (!d->contains_entry(*i))
	return false;
      
      node_t n = d->get_entry(*i);

      if (!is_dir_t(n))
	return false;

      d = downcast_to_dir_t(n);
    }
  return d->contains_entry(v.leaf) 
    && is_dir_t(d->get_entry(v.leaf));
}


node_t 
mfest::get_node(ident_t i) const
{
  // L(F("mfest::get_node(%s)\n") % i);
  node_map_t::const_iterator j = nodes.find(i);
  I(j != nodes.end());
  return j->second;
}


dir_t 
mfest::get_dir_node(ident_t i) const
{
  return downcast_to_dir_t(get_node(i));
}


file_t 
mfest::get_file_node(ident_t i) const
{
  return downcast_to_file_t(get_node(i));
}


dir_t 
mfest::get_dir_node(path_vec_t const & d) const
{
  return downcast_to_dir_t(get_node(d));
}


file_t 
mfest::get_file_node(path_vec_t const & f) const
{
  return downcast_to_file_t(get_node(f));
}


dir_t 
mfest::get_containing_dir_node(path_vec_t const & v) const
{
  dir_t d = root;
   
  for (vector<dirent_t>::const_iterator i = v.dir.begin();  
       i != v.dir.end(); ++i)
    {
      d = downcast_to_dir_t(d->get_entry(*i));
    }
  return d;
}


node_t 
mfest::get_node(path_vec_t const & n) const
{
  return get_containing_dir_node(n)->get_entry(n.leaf);
}


void
mfest::get_path(node_t const & n, 
		path_vec_t & path) const
{
  I(n->live());
  path.dir.clear();
  path.leaf = n->name;
  ident_t i = n->parent;
  while(i != root->ident)
    {
      node_t tmp = get_node(i);
      path.dir.push_back(tmp->name);
      i = tmp->parent;
    }
  reverse(path.dir.begin(), path.dir.end());
}


// these versions implement copy-on-write, updating the
// tree to point to nodes meeting write_generation

static void
ensure_node_meets_generation(mfest & m,
			     dir_t & d,
			     node_t & n,
			     gen_t write_generation)
{
  if (n->generation < write_generation)
    {
      n = n->shallow_copy();
      n->generation = write_generation;

      m.nodes.erase(n->ident);
      m.nodes.insert(make_pair(n->ident, n));

      d->entries.erase(n->name);
      d->entries.insert(make_pair(n->name, n));
    }
}


dir_t 
mfest::get_containing_dir_node(path_vec_t const & v,
			       gen_t write_generation)
{  
  if (root->generation < write_generation)
    {
      // L(F("upgrading root to write generation %d\n") % write_generation);
      root = downcast_to_dir_t(root->shallow_copy());
      root->generation = write_generation;
      nodes.erase(root->ident);
      nodes.insert(make_pair(root->ident, root));
    }

  dir_t d = root;
  for (vector<dirent_t>::const_iterator i = v.dir.begin();
       i != v.dir.end(); ++i)
    {
      node_t n = d->get_entry(*i);
      ensure_node_meets_generation(*this, d, n, write_generation);
      d = downcast_to_dir_t(n);
    }
  return d;
}


node_t 
mfest::get_node(path_vec_t const & pth,
		gen_t write_generation)
{
  dir_t d = get_containing_dir_node(pth, write_generation);
  node_t n = d->get_entry(pth.leaf);
  ensure_node_meets_generation(*this, d, n, write_generation);
  return n;
}


dir_t 
mfest::get_dir_node(path_vec_t const & pth,
		    gen_t write_generation)
{
  return downcast_to_dir_t(get_node(pth, write_generation));
}


file_t 
mfest::get_file_node(path_vec_t const & pth,
		     gen_t write_generation)
{
  return downcast_to_file_t(get_node(pth, write_generation));
}


node_t 
mfest::get_node(ident_t i, gen_t write_generation)
{
  path_vec_t pth;
  if (i == root->ident)
    return root;
  node_t n = get_node(i);
  get_path(n, pth);
  return get_node(pth, write_generation);
}


dir_t 
mfest::get_dir_node(ident_t i, gen_t write_generation)
{
  return downcast_to_dir_t(get_node(i, write_generation));
}


file_t 
mfest::get_file_node(ident_t i, gen_t write_generation)
{
  return downcast_to_file_t(get_node(i, write_generation));
}


bool 
equal_up_to_renumbering(mfest const & ma,
                        mfest const & mb)
{
  // NB: this function compares mfests for structural equality over the
  // abstract filesystem; it ignores differences which may exist between
  // the mfests' node sets, ident numbers, and any sire/heir relationships
  // (which are only relevant in the context of csets)

  bfs_iter pa(ma.root), pb(mb.root);

  while (!(pa.finished() || pb.finished()))
    {

      node_t a = *pa;
      node_t b = *pb;
      
      if ((a->name != b->name))
        return false;
      
      if (a->fancy || b->fancy)
        {
          if (!(a->fancy && b->fancy))
            return false;
          if (a->fancy->attrs != b->fancy->attrs)
            return false;
        }

      if (is_file_t(a) && is_file_t(b))
        {
          file_t fa = downcast_to_file_t(a);
          file_t fb = downcast_to_file_t(b);
          if (! (fa->content == fb->content))
            return false;
        }

      else if (is_dir_t(a) && is_dir_t(b))
        {
          dir_t da = downcast_to_dir_t(a);
          dir_t db = downcast_to_dir_t(b);
          if (da->entries.size() != da->entries.size())
            return false;
        }
      else
        return false;

      ++pa;
      ++pb;
    }

  if (! (pa.finished() && pb.finished()))
    return false;

  return true;
}


//==================================================================
// change_consumers
//==================================================================

struct
change_consumer
{

  virtual ~change_consumer() {}

  void set_heir(file_path const & dying,
		file_path const & heir);
  
  void delete_node(file_path const & pth);
  void rename_node(file_path const & src, 
		   file_path const & dst);

  void add_dir(file_path const & dp);
  void add_file(file_path const & fp);

  void set_sire(file_path const & newborn,
		file_path const & sire);

  void apply_delta(file_path const & path, 
		   file_id const & src, 
		   file_id const & dst);

  void set_attr(file_path const & path, 
		attr_name const & name, 
		attr_val const & val);

  void clear_attr(file_path const & path, 
		  attr_name const & name);

  virtual void finalize_renames() {}

  // this part is just a lower-level form of the API above, to avoid
  // composing or parsing file_paths in their string-y form

  virtual void set_heir(path_vec_t const & dying,
			path_vec_t const & heir) = 0;

  virtual void delete_node(path_vec_t const & path) = 0;

  virtual void rename_node(path_vec_t const & src, 
			   path_vec_t const & dst) = 0;
  
  virtual void add_dir(path_vec_t const & dp) = 0;
  virtual void add_file(path_vec_t const & fp) = 0;

  virtual void set_sire(path_vec_t const & newborn,
			path_vec_t const & sire) = 0;

  virtual void apply_delta(path_vec_t const & path, 
			   file_id const & src, 
			   file_id const & dst) = 0;

  virtual void set_attr(path_vec_t const & path, 
			attr_name const & name, 
			attr_val const & val) = 0;

  virtual void clear_attr(path_vec_t const & path, 
			  attr_name const & name) = 0;
};


void 
change_consumer::set_heir(file_path const & dying,
			  file_path const & heir)
{
  L(F("set_heir('%s', '%s')\n") % dying % heir);
  this->set_heir(path_vec_t::from_file_path(dying), 
		 path_vec_t::from_file_path(heir));
}


void 
change_consumer::delete_node(file_path const & dp)
{
  L(F("delete_node('%s')\n") % dp);
  this->delete_node(path_vec_t::from_file_path(dp));
}


void 
change_consumer::rename_node(file_path const & a,
			     file_path const & b)
{
  L(F("rename_node('%s', '%s')\n") % a % b);
  this->rename_node(path_vec_t::from_file_path(a),
		    path_vec_t::from_file_path(b));
}


void 
change_consumer::add_dir(file_path const & dp)
{
  L(F("add_dir('%s')\n") % dp);
  this->add_dir(path_vec_t::from_file_path(dp));
}


void 
change_consumer::add_file(file_path const & fp)
{
  L(F("add_file('%s')\n") % fp);
  this->add_file(path_vec_t::from_file_path(fp));
}


void 
change_consumer::set_sire(file_path const & newborn,
			  file_path const & sire)
{
  L(F("set_sire('%s', '%s')\n") % newborn % sire);
  this->set_sire(path_vec_t::from_file_path(newborn), 
		 path_vec_t::from_file_path(sire));
}


void 
change_consumer::apply_delta(file_path const & path, 
			     file_id const & src, 
			     file_id const & dst)
{
  L(F("apply_delta('%s', [%s], [%s])\n") % path % src % dst);
  this->apply_delta(path_vec_t::from_file_path(path), src, dst);
}


void 
change_consumer::set_attr(file_path const & path, 
			  attr_name const & name, 
			  attr_val const & val)
{
  L(F("set_attr('%s', '%s', '%s')\n") % path % name % val);
  this->set_attr(path_vec_t::from_file_path(path), name, val);
}


void 
change_consumer::clear_attr(file_path const & path, 
			    attr_name const & name)
{
  L(F("clear_attr('%s', '%s')\n") % path % name);
  this->clear_attr(path_vec_t::from_file_path(path), name);
}


//==================================================================
// attach_detach_change_consumers
//==================================================================

struct 
attach_detach_change_consumer
  : public change_consumer
{
  virtual ~attach_detach_change_consumer() {}

  virtual ident_t detach(path_vec_t const & path) = 0;
  virtual void attach(path_vec_t const & path, ident_t id) = 0;

  // renames are directed into a temporary buffer, which 
  // then executes them in two steps using the lower-level API
  // "attach" and "detach".

  vector< pair<path_vec_t, path_vec_t> > pending_renames;

  virtual void rename_node(path_vec_t const & src, 
			   path_vec_t const & dst);
  virtual void finalize_renames();

};

void 
attach_detach_change_consumer::rename_node(path_vec_t const & src, 
					   path_vec_t const & dst)
{
  pending_renames.push_back(make_pair(src, dst));
}


void 
attach_detach_change_consumer::finalize_renames()
{
  vector<pair<path_vec_t, ident_t> > tmp;
  
  for (vector< pair<path_vec_t, path_vec_t> >::reverse_iterator i = pending_renames.rbegin(); 
       i != pending_renames.rend(); ++i)
    {
      ident_t t = detach(i->first);
      tmp.push_back(make_pair(i->second, t));
    }

  for (vector< pair<path_vec_t, ident_t> >::const_iterator i = tmp.begin(); 
       i != tmp.end(); ++i)
    {
      attach(i->first, i->second);
    }
}



//==================================================================
// csets
//==================================================================


struct 
cset 
  : public attach_detach_change_consumer
{ 
  mfest old_mfest;
  mfest new_mfest;
  gen_t write_generation;

  cset()
  {
    write_generation = 1;
  }

  cset(mfest const & base)
  {
    this->reset(base);
  }

  virtual ~cset() {}

  void reset(mfest const & m);
  void check_sane() const;

  void replay_changes(change_consumer & cc) const;
  void replay_inverse_changes(change_consumer & cc) const;

  virtual void finalize_renames();

  virtual void set_heir(path_vec_t const & dying,
			path_vec_t const & heir);
  
  virtual void delete_node(path_vec_t const & dp);

  virtual ident_t detach(path_vec_t const & path);
  virtual void attach(path_vec_t const & path, ident_t id);

  virtual void add_dir(path_vec_t const & dp);
  virtual void add_file(path_vec_t const & fp);

  virtual void set_sire(path_vec_t const & newborn,
			path_vec_t const & sire);

  virtual void apply_delta(path_vec_t const & path, 
			   file_id const & src, 
			   file_id const & dst);

  virtual void set_attr(path_vec_t const & path, 
			attr_name const & name, 
			attr_val const & val);

  virtual void clear_attr(path_vec_t const & path, 
			  attr_name const & name);
};


static gen_t 
find_max_write_generation(dir_t d)
{
  gen_t m = d->generation;
  for (dfs_iter i(d); !i.finished(); ++i)
    if ((*i)->generation > m)
      m = (*i)->generation;  
  return m;
}


void 
cset::reset(mfest const & m)
{
  old_mfest.reset(m);
  new_mfest.reset(m);
  write_generation = find_max_write_generation(m.root);
  I(write_generation != UINT64_MAX);
  ++write_generation;
  // L(F("cset write generation is %d\n") % write_generation);
}


static void
check_hash_inclusion(mfest const & a,
		     mfest const & b)
{
  for (node_map_t::const_iterator i = a.nodes.begin();
	 i != a.nodes.end(); ++i)
    {
      node_map_t::const_iterator j = b.nodes.find(i->first);
      I(j != b.nodes.end());
    }  
}


static void
check_mfests_agree(mfest const & a,
                   mfest const & b)
{
  check_hash_inclusion(a,b);
  check_hash_inclusion(b,a);
}
		     

void 
cset::check_sane() const
{
  //   L(F("cset::check_sane checking prestate manifest sanity...\n"));
  old_mfest.check_sane();
  //   L(F("cset::check_sane checking poststate manifest sanity...\n"));
  new_mfest.check_sane();
  //   L(F("cset::check_sane checking pre/post agreement...\n"));
  check_mfests_agree(old_mfest, new_mfest);
  //   L(F("cset::check_sane OK\n"));
}

void 
cset::finalize_renames()
{
  attach_detach_change_consumer::finalize_renames();

  if (global_sanity.debug)
    {
      check_sane();
    }
}

void 
cset::delete_node(path_vec_t const & dp)
{
  this->detach(dp);

  if (global_sanity.debug)
    {
      check_sane();
    }  
}


void 
cset::set_heir(path_vec_t const & dying,
	       path_vec_t const & heir)
{
  node_t n = new_mfest.get_node(dying, write_generation);
  node_t h = new_mfest.get_node(heir);
  n->ensure_fancy();
  n->fancy->heir = h->ident;

  if (global_sanity.debug)
    {
      check_sane();
    }
}


void 
cset::set_sire(path_vec_t const & newborn,
	       path_vec_t const & sire)
{
  node_t n = new_mfest.get_node(newborn, write_generation);
  node_t s = new_mfest.get_node(sire);
  n->ensure_fancy();
  n->fancy->sire = s->ident;

  if (global_sanity.debug)
    {
      check_sane();
    }
}


ident_t 
cset::detach(path_vec_t const & path)
{
  node_t src = old_mfest.get_node(path);
  node_t dst = new_mfest.get_node(src->ident, write_generation);
  dir_t parent_of_dst = new_mfest.get_dir_node(dst->parent, write_generation);

  parent_of_dst->drop_child(dst->name);
  dst->parent = graveyard_ident; 

  // NB: it is important *not* to check sanity here, as we might have
  // entered a transient state where detached grandchildren are 
  // not pointing to the graveyard because they're about to be
  // re-attached.

  return dst->ident;
}


void 
cset::attach(path_vec_t const & path, ident_t id)
{
  node_t n = new_mfest.get_node(id);
  dir_t d = new_mfest.get_containing_dir_node(path, write_generation);
  d->add_child(path.leaf, n);

  // NB: it is important *not* to check sanity here, as we might still
  // be in a transient state where detached grandchildren are not
  // pointing to the graveyard because they're about to be
  // re-attached.
}


void 
cset::add_dir(path_vec_t const & dp)
{
  dir_t new_dst_parent = new_mfest.get_containing_dir_node(dp, write_generation);
  dir_t new_dir = old_mfest.make_dir();
  if (new_dir->ident > new_mfest.max_ident)
    new_mfest.max_ident = new_dir->ident;
  node_t new_dir_in_new_mfest = new_dir->shallow_copy();

  new_dst_parent->add_child(dp.leaf, new_dir_in_new_mfest);
  new_mfest.nodes.insert(make_pair(new_dir->ident, new_dir_in_new_mfest));

  if (global_sanity.debug)
    {
      check_sane();
    }
}


void 
cset::add_file(path_vec_t const & fp)
{
  dir_t new_dst_parent = new_mfest.get_containing_dir_node(fp, write_generation);
  file_t new_file = old_mfest.make_file();
  if (new_file->ident > new_mfest.max_ident)
    new_mfest.max_ident = new_file->ident;
  node_t new_file_in_new_mfest = new_file->shallow_copy();

  new_dst_parent->add_child(fp.leaf, new_file_in_new_mfest);
  new_mfest.nodes.insert(make_pair(new_file->ident, new_file_in_new_mfest));

  if (global_sanity.debug)
    {
      check_sane();
    }
}


void 
cset::apply_delta(path_vec_t const & path, 
		  file_id const & s, 
		  file_id const & d)
{
  file_t dst = new_mfest.get_file_node(path, write_generation);
  file_t src = old_mfest.get_file_node(dst->ident);

  I(src->content == s);
  dst->content = d;

  if (global_sanity.debug)
    {
      check_sane();
    }
}


void 
cset::set_attr(path_vec_t const & path, 
	       attr_name const & name, 
	       attr_val const & val)
{
  node_t n = new_mfest.get_node(path, write_generation);
  n->set_attr(name, val);
}


void 
cset::clear_attr(path_vec_t const & path, 
		 string const & name)
{
  node_t n = new_mfest.get_node(path, write_generation);
  n->clear_attr(name);
}


typedef vector<pair<node_t, node_t> > node_pair_vec;
typedef vector<pair<file_t, file_t> > file_pair_vec;
typedef vector<pair<pair<node_t, node_t>, 
		    shared_ptr<set<attr_name> > > > 
node_attr_name_vec;


struct replay_record
{
  node_pair_vec heirs_set;
  node_pair_vec dirs_deleted;
  node_pair_vec files_deleted;
  node_pair_vec nodes_renamed;
  node_pair_vec dirs_added;
  node_pair_vec files_added;
  node_pair_vec sires_set;
  file_pair_vec deltas_applied;
  node_attr_name_vec attrs_changed;
};


static void
play_back_replay_record(replay_record const & rr,
			mfest const & src,
			mfest const & dst,
			change_consumer & cc)
{
  path_vec_t v1, v2;

  for (node_pair_vec::const_iterator i = rr.heirs_set.begin();
       i != rr.heirs_set.end(); ++i)
    {
      src.get_path(i->first, v1);
      dst.get_path(i->second, v2);
      cc.set_heir(v1, v2);
    }

  for (node_pair_vec::const_iterator i = rr.files_deleted.begin();
       i != rr.files_deleted.end(); ++i)
    {
      src.get_path(i->first, v1);
      cc.delete_node(v1);
    }

  for (node_pair_vec::const_iterator i = rr.dirs_deleted.begin();
       i != rr.dirs_deleted.end(); ++i)
    {
      src.get_path(i->first, v1);
      cc.delete_node(v1);
    }

  for (node_pair_vec::const_iterator i = rr.nodes_renamed.begin();
       i != rr.nodes_renamed.end(); ++i)
    {
      src.get_path(i->first, v1);
      dst.get_path(i->second, v2);
      cc.rename_node(v1, v2);
    }

  cc.finalize_renames();

  for (node_pair_vec::const_iterator i = rr.dirs_added.begin();
       i != rr.dirs_added.end(); ++i)
    {
      dst.get_path(i->second, v2);
      cc.add_dir(v2);
    }

  for (node_pair_vec::const_iterator i = rr.files_added.begin();
       i != rr.files_added.end(); ++i)
    {
      dst.get_path(i->second, v2);
      cc.add_file(v2);
    }

  for (node_pair_vec::const_iterator i = rr.sires_set.begin();
       i != rr.sires_set.end(); ++i)
    {
      src.get_path(i->first, v1);
      dst.get_path(i->second, v2);
      cc.set_sire(v2, v1);
    }

  for (file_pair_vec::const_iterator i = rr.deltas_applied.begin();
       i != rr.deltas_applied.end(); ++i)
    {
      dst.get_path(i->second, v2);
      cc.apply_delta(v2, 
		     i->first->content, 
		     i->second->content);
    }

  // attr pass #1: emit all the cleared attrs
  for (node_attr_name_vec::const_iterator i = rr.attrs_changed.begin();
       i != rr.attrs_changed.end(); ++i)
    {
      node_t a = i->first.first;
      node_t b = i->first.second;
      shared_ptr< set<attr_name> > attr_names = i->second;
      for (set<attr_name>::const_iterator j = attr_names->begin();
	   j != attr_names->end(); ++j)
	{
	  if (a->has_attr(*j) && !b->has_attr(*j))
	    {
	      dst.get_path(b, v2);
	      cc.clear_attr(v2, *j);
	    }
	}
    }

  // attr pass #2: emit all the set attrs
  for (node_attr_name_vec::const_iterator i = rr.attrs_changed.begin();
       i != rr.attrs_changed.end(); ++i)
    {
      node_t a = i->first.first;
      node_t b = i->first.second;
      shared_ptr< set<attr_name> > attr_names = i->second;
      for (set<attr_name>::const_iterator j = attr_names->begin();
	   j != attr_names->end(); ++j)
	{
	  if (b->has_attr(*j))
	    {
	      if ((!a->has_attr(*j))
		  || (a->has_attr(*j) &&
		      a->get_attr(*j) != b->get_attr(*j)))
		{
		  dst.get_path(b, v2);
		  cc.set_attr(v2, *j, b->get_attr(*j));
		}
	    }
	}
    }
}


static void
play_back_replay_record_inverse(replay_record const & rr,
				mfest const & src,
				mfest const & dst,
				change_consumer & cc)
{
  path_vec_t v1, v2;

  for (node_pair_vec::const_iterator i = rr.sires_set.begin();
       i != rr.sires_set.end(); ++i)
    {
      // the forward cset goes from src->dst, and had 
      // set_sire(newborn, sire) with newborn in dst and sire in src
      //
      // the inverse cset goes from src<-dst, and has
      // set_heir(dying, heir) with dying in dst and heir in src

      src.get_path(i->first, v1);
      dst.get_path(i->second, v2);
      cc.set_heir(v2, v1);
    }

  for (node_pair_vec::const_iterator i = rr.files_added.begin();
       i != rr.files_added.end(); ++i)
    {
      // the forward cset goes from src->dst, and had 
      // add_file(newborn) with newborn in dst
      //
      // the inverse cset goes from src<-dst, and has
      // delete_node(dying) with dying in dst

      dst.get_path(i->second, v2);
      cc.delete_node(v2);
    }

  for (node_pair_vec::const_iterator i = rr.dirs_added.begin();
       i != rr.dirs_added.end(); ++i)
    {
      // the forward cset goes from src->dst, and had 
      // add_dir(newborn) with newborn in dst
      //
      // the inverse cset goes from src<-dst, and has
      // delete_node(dying) with dying in dst

      dst.get_path(i->second, v2);
      cc.delete_node(v2);
    }

  for (node_pair_vec::const_iterator i = rr.nodes_renamed.begin();
       i != rr.nodes_renamed.end(); ++i)
    {
      // the forward cset goes from src->dst, and had 
      // rename_node(a, b) with a in src in b in dst
      //
      // the inverse cset goes from src<-dst, and has
      // rename_node(b, a) with a in src in b in dst

      src.get_path(i->first, v1);
      dst.get_path(i->second, v2);
      cc.rename_node(v2, v1);
    }

  cc.finalize_renames();


  for (node_pair_vec::const_iterator i = rr.dirs_deleted.begin();
       i != rr.dirs_deleted.end(); ++i)
    {
      // the forward cset goes from src->dst, and had 
      // delete_node(dying) with dying in src
      //
      // the inverse cset goes from src<-dst, and has
      // add_dir(newborn) with newborn in src

      dst.get_path(i->first, v1);
      cc.add_dir(v1);
    }

  for (node_pair_vec::const_iterator i = rr.files_deleted.begin();
       i != rr.files_deleted.end(); ++i)
    {
      // the forward cset goes from src->dst, and had 
      // delete_node(dying) with dying in src
      //
      // the inverse cset goes from src<-dst, and has
      // add_file(newborn) with newborn in src

      dst.get_path(i->first, v1);
      cc.add_file(v1);
    }

  for (node_pair_vec::const_iterator i = rr.heirs_set.begin();
       i != rr.heirs_set.end(); ++i)
    {
      // the forward cset goes from src->dst, and had 
      // set_heir(dying, heir) with dying in src and heir in dst
      //
      // the inverse cset goes from src<-dst, and has
      // set_sire(newborn, sire) with newborn in src and sire in dst

      src.get_path(i->first, v1);
      dst.get_path(i->second, v2);
      cc.set_sire(v1, v2);
    }

  for (file_pair_vec::const_iterator i = rr.deltas_applied.begin();
       i != rr.deltas_applied.end(); ++i)
    {
      // the forward cset goes from src->dst, and had 
      // apply_delta(pth, a, b) with a in src, and pth and b in dst
      //
      // the inverse cset goes from src<-dst, and has
      // apply_delta(pth, a, b) with a in dst, and pth and b in src

      src.get_path(i->first, v1);
      cc.apply_delta(v1, 
		     i->second->content,
		     i->first->content);
    }


  // attr pass #1: emit all the cleared attrs
  for (node_attr_name_vec::const_iterator i = rr.attrs_changed.begin();
       i != rr.attrs_changed.end(); ++i)
    {
      // the forward cset goes from src->dst, nodes a->b, and had 
      // clear_attr(b, attr) with b in dst
      //
      // the inverse cset goes from src<-dst, nodes a<-b, and has
      // clear_attr(a, attr) with a in src

      node_t a = i->first.first;
      node_t b = i->first.second;
      shared_ptr< set<attr_name> > attr_names = i->second;
      for (set<attr_name>::const_iterator j = attr_names->begin();
	   j != attr_names->end(); ++j)
	{
	  if (b->has_attr(*j) && !a->has_attr(*j))
	    {
	      dst.get_path(a, v1);
	      cc.clear_attr(v1, *j);
	    }
	}
    }

  // attr pass #2: emit all the set attrs
  for (node_attr_name_vec::const_iterator i = rr.attrs_changed.begin();
       i != rr.attrs_changed.end(); ++i)
    {
      // the forward cset goes from src->dst, nodes a->b, and had 
      // set_attr(b, attr, val) with b in dst
      //
      // the inverse cset goes from src<-dst, nodes a<-b, and has
      // set_attr(a, attr, val) with a in src

      node_t a = i->first.first;
      node_t b = i->first.second;
      shared_ptr< set<attr_name> > attr_names = i->second;
      for (set<attr_name>::const_iterator j = attr_names->begin();
	   j != attr_names->end(); ++j)
	{
	  if (a->has_attr(*j))
	    {
	      if ((!b->has_attr(*j))
		  || (b->has_attr(*j) &&
		      b->get_attr(*j) != a->get_attr(*j)))
		{
		  dst.get_path(a, v1);
		  cc.set_attr(v1, *j, a->get_attr(*j));
		}
	    }
	}
    }
}


static void
build_replay_record(mfest const & src,
		    mfest const & dst,
		    replay_record & rr)
{
  // we do two passes accumulating nodes: the first pass accumulates
  // nodes in the topological order they appear in the src map, the
  // second accumulates nodes in the toplogical order they appear in
  // the dst map. in both cases we append any interesting nodes to
  // vectors which we then replay as blocks of related changes.
  
  for (dfs_iter i(src.root); !i.finished(); ++i)
    {
      // in this pass we accumulate heirs_set and nodes_deleted.
      // the "self" node is a member of the "src" directory tree.
      node_t self = *i;
      node_t other = dst.get_node(self->ident);

      I(self->live());
      
      if (other->killed())
	{
	  if (is_dir_t(self))
	    rr.dirs_deleted.push_back(make_pair(self, other));
	  else
	    rr.files_deleted.push_back(make_pair(self, other));
	    
	  if (self->has_heir())
	    {
	      node_t heir = dst.get_node(self->fancy->heir);
	      rr.heirs_set.push_back(make_pair(self, heir));
	    }
	}
    }

  for (dfs_iter i(dst.root); !i.finished(); ++i)
    {
      // in this pass we accumulate nodes_renamed, files_added,
      // dirs_added, sires_set, deltas_applied, attrs_cleared, 
      // and attrs_set.
      //
      // the "self" node is a member of the "dst" directory tree.
      node_t self = *i;

      I(self->live());

      node_t other = src.get_node(self->ident);
      if (other->unborn())
	{
	  if (is_dir_t(self))
	    {
	      I(is_dir_t(other));
	      rr.dirs_added.push_back(make_pair(other, self));
	    }
	  else
	    {
	      I(is_file_t(self));
	      I(is_file_t(other));
	      rr.files_added.push_back(make_pair(other, self));
	    }
	  if (self->has_sire())
	    {
	      node_t sire = src.get_node(self->fancy->sire);
	      rr.sires_set.push_back(make_pair(self, sire));
	    }
	} 
      else if (other->live())
	{
	  if ((other->name->val != self->name->val)
	      || (other->parent != self->parent))
	    {
	      rr.nodes_renamed.push_back(make_pair(other, self));
	    }
	}

      // content deltas
      if (is_file_t(self))
	{
	  I(is_file_t(other));
	  file_t f_other = downcast_to_file_t(other);
	  file_t f_self = downcast_to_file_t(self);
	  if (!(f_other->content == f_self->content))
	    {
	      rr.deltas_applied.push_back(make_pair(f_other, f_self));
	    }
	}
      
      // attrs which were changed
      if (other->has_attrs() || self->has_attrs())
	{
	  map<attr_name, attr_val> self_attrs;
	  map<attr_name, attr_val> other_attrs;

	  if (self->has_attrs())
	    self_attrs = self->fancy->attrs;

	  if (other->has_attrs())
	    other_attrs = other->fancy->attrs;

	  shared_ptr< set<attr_name> > attr_set = shared_ptr< set<attr_name> >(new set<attr_name>);

	  for (map<attr_name, attr_val>::const_iterator a = self_attrs.begin();
	       a != self_attrs.end(); ++a)
	    {
	      if (other_attrs[a->first] != a->second)
		attr_set->insert(a->first);
	    }

	  for (map<attr_name, attr_val>::const_iterator b = other_attrs.begin();
	       b != other_attrs.end(); ++b)
	    {
	      if (self_attrs[b->first] != b->second)
		attr_set->insert(b->first);
	    }

	  if (!attr_set->empty())
	    rr.attrs_changed.push_back(make_pair(make_pair(self, other), attr_set));
	}
    }
}


void 
cset::replay_changes(change_consumer & cc) const
{
  replay_record rr;
  check_sane();
  build_replay_record(old_mfest, new_mfest, rr);
  play_back_replay_record(rr, old_mfest, new_mfest, cc);
}


void 
cset::replay_inverse_changes(change_consumer & cc) const
{
  replay_record rr;
  check_sane();
  build_replay_record(old_mfest, new_mfest, rr);
  play_back_replay_record_inverse(rr, old_mfest, new_mfest, cc);
}


void
concatenate_changesets(cset const & a,
		       cset const & b,
		       cset & c)
{
  a.check_sane();
  b.check_sane();
  c.reset(a.new_mfest);
  b.replay_changes(c);
  c.check_sane();
}



// merging changes is done on a file-by-file basis (within a
// worst-case subgraph established at the revision level).
//
// - start with revisions (and thus mfests) A and B, which we want to
// merge. there's an LCAD C, which is the worst-case common ancestor.
//
// - extract the subgraph S bound by A+B at the bottom and C at the top.
//
// - take the files in A and the files in B, and try to identify them
// via their history structure. there is a possibility for
// mis-identification here if there were mutual adds of "the same
// file" on both edges; the essential thing to achieve is that A and B
// each have only *one* entry for each identified file. there cannot
// be two. if there are two, due to post-add identification along two
// different history lines (suturing), ask the user to serve as a
// tie-breaker.
//
// - for each "aspect" X (name, parent (+ lifecycle-state),
// data-content, any attributes) of each identified file F in A union
// B, check to see if the aspect is identical in A and B. if so, it's not
// affected by the merge, no problem.
//
// if it differs, copy the graph S into G[F.X] and perform a sequence
// of contractions on G[F.X] by checking each graph node until there
// is a full pass over the graph w/o contractions. the contraction
// rules are:
//
//   - contract    X -> X         ==>    X
//   - contract    Y <- X -> Y    ==>    X
//   - contract    X -> Y -> Z    ==>    X -> Z
//     (where Y has no other children)
//
// - if we have contracted A[F.X] and B[F.X] into the same node M[F.X]
// in G[F.X] (or more likely, if one node is contracted into a parent
// of the other, such that there is only one head), then we have
// merged F.X for free: there were no conflicts on it.
// 
// - otherwise, with a contracted version of G[F.X], we let C[F.X] =
// LCAD(G[F.X]) -- an aspect-specific common-ancestor -- and ask the
// user for help on A[F.X], C[F.X], B[F.X]. if they refuse, we fail.
//

// TODO: implement this algorithm!


namespace
{
  namespace syms
  {
    // mfest symbols
    std::string const dir("dir");
    std::string const file("file");
    std::string const content("content");

    // cset symbols
    std::string const set_heir("set_heir");
    std::string const delete_node("delete");
    std::string const rename_node("rename");
    std::string const add_file("add_file");
    std::string const add_dir("add_dir");
    std::string const set_sire("set_sire");
    std::string const patch("patch");
    std::string const from("from");
    std::string const to("to");
    std::string const clear("clear");
    std::string const set("set");
    std::string const attr("attr");
    std::string const value("value");
  }
}


static void 
parse_cset(basic_io::parser & parser,
	   change_consumer & cc)
{
  while (parser.symp())
    {
      std::string t1, t2, t3;
      if (parser.symp(syms::set_heir)) 
        { 
          parser.sym();
          parser.str(t1);
	  parser.esym(syms::to);
          parser.str(t2);
          cc.set_heir(file_path(t1),
		      file_path(t2));
        }
      else if (parser.symp(syms::delete_node)) 
        { 
          parser.sym();
          parser.str(t1);
          cc.delete_node(file_path(t1));
        }
      else if (parser.symp(syms::rename_node)) 
        { 
          parser.sym();
          parser.str(t1);
          parser.esym(syms::to);
          parser.str(t2);
          cc.rename_node(file_path(t1),
                         file_path(t2));
        }
      else if (parser.symp(syms::add_file)) 
        { 
          parser.sym();
          parser.str(t1);
          cc.add_file(file_path(t1));
        }
      else if (parser.symp(syms::add_dir)) 
        { 
          parser.sym();
          parser.str(t1);
          cc.add_dir(file_path(t1));
        }
      else if (parser.symp(syms::set_sire))
        { 
          parser.sym();
          parser.str(t1);
	  parser.esym(syms::from);
          parser.str(t2);
          cc.set_sire(file_path(t1),
		      file_path(t2));
        }
      else if (parser.symp(syms::patch))
	{
	  parser.sym();
	  parser.str(t1);
	  parser.esym(syms::from);
	  parser.hex(t2);
	  parser.esym(syms::to);
	  parser.hex(t3);
	  cc.apply_delta(file_path(t1), file_id(t2), file_id(t3));
	}
      else if (parser.symp(syms::clear))
	{
	  parser.sym();
	  parser.str(t1);
	  parser.esym(syms::attr);
	  parser.str(t2);
	  cc.clear_attr(file_path(t1), attr_name(t2));
	}
      else if (parser.symp(syms::set))
	{
	  parser.sym();
	  parser.str(t1);
	  parser.esym(syms::attr);
	  parser.str(t2);
	  parser.esym(syms::value);
	  parser.str(t3);
	  cc.set_attr(file_path(t1), 
		      attr_name(t2),
		      attr_val(t3));
	}
      else
        break;
    }
}


struct 
cset_printer 
  : public change_consumer
{
  basic_io::printer & printer;
  cset_printer(basic_io::printer & p) : printer(p) {}
  virtual void set_heir(path_vec_t const & dying,
			path_vec_t const & heir);
  virtual void delete_node(path_vec_t const & fp);
  virtual void rename_node(path_vec_t const & src, 
			   path_vec_t const & dst);
  virtual void add_dir(path_vec_t const & dp);
  virtual void add_file(path_vec_t const & fp);
  virtual void set_sire(path_vec_t const & newborn,
			path_vec_t const & sire);
  virtual void apply_delta(path_vec_t const & path,
			   file_id const & src,
			   file_id const & dst);
  virtual void clear_attr(path_vec_t const & path,
			  attr_name const & attr);
  virtual void set_attr(path_vec_t const & path,
			attr_name const & attr,
			attr_val const & val);
};


void 
cset_printer::set_heir(path_vec_t const & dying, 
		       path_vec_t const & heir)
{
      basic_io::stanza st;
      st.push_str_pair(syms::set_heir, dying.to_file_path()());
      st.push_str_pair(syms::to, heir.to_file_path()());
      printer.print_stanza(st);
}


void 
cset_printer::delete_node(path_vec_t const & dp)
{
      basic_io::stanza st;
      st.push_str_pair(syms::delete_node, dp.to_file_path()());
      printer.print_stanza(st);
}


void 
cset_printer::rename_node(path_vec_t const & src, 
			  path_vec_t const & dst)
{
      basic_io::stanza st;
      st.push_str_pair(syms::rename_node, src.to_file_path()());
      st.push_str_pair(syms::to, dst.to_file_path()());
      printer.print_stanza(st);
}


void 
cset_printer::add_dir(path_vec_t const & dp)
{
      basic_io::stanza st;
      st.push_str_pair(syms::add_dir, dp.to_file_path()());
      printer.print_stanza(st);
}


void 
cset_printer::add_file(path_vec_t const & fp)
{
      basic_io::stanza st;
      st.push_str_pair(syms::add_file, fp.to_file_path()());
      printer.print_stanza(st);
}


void 
cset_printer::set_sire(path_vec_t const & newborn, 
		       path_vec_t const & sire)
{
      basic_io::stanza st;
      st.push_str_pair(syms::set_sire, newborn.to_file_path()());
      st.push_str_pair(syms::from, sire.to_file_path()());
      printer.print_stanza(st);
}


void 
cset_printer::apply_delta(path_vec_t const & path, 
			   file_id const & src, 
			   file_id const & dst)
{
      basic_io::stanza st;
      st.push_str_pair(syms::patch, path.to_file_path()());
      st.push_hex_pair(syms::from, src.inner()());
      st.push_hex_pair(syms::to, dst.inner()());
      printer.print_stanza(st);
}


void 
cset_printer::clear_attr(path_vec_t const & path, 
			 attr_name const & attr)
{
      basic_io::stanza st;
      st.push_str_pair(syms::set, path.to_file_path()());
      st.push_str_pair(syms::attr, attr);
      printer.print_stanza(st);
}


void 
cset_printer::set_attr(path_vec_t const & path, 
		       attr_name const & attr,
		       attr_val const & val)
{
      basic_io::stanza st;
      st.push_str_pair(syms::set, path.to_file_path()());
      st.push_str_pair(syms::attr, attr);
      st.push_str_pair(syms::value, val);
      printer.print_stanza(st);
}


void
read_cset(data const & dat,
	  cset & cs)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "cset");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_cset(pars, cs);
  I(src.lookahead == EOF);
  cs.check_sane();
}


void
write_cset(cset const & cs,
	   data & dat)
{
  cs.check_sane();
  std::ostringstream oss;
  basic_io::printer pr(oss);
  cset_printer cpr(pr);
  cs.replay_changes(cpr);
  dat = data(oss.str());  
}

void
parse_mfest(basic_io::parser & p, 
	    mfest & m)
{
  cset c;
  change_consumer & cs = c;
  std::string pth, ident, n, v;
  while (p.symp())
    {
      if (p.symp(syms::dir))
	{
	  p.sym();
	  p.str(pth);
          // L(F("read dir %s\n") % pth);
	  cs.add_dir(file_path(pth));
	}
      else if (p.symp(syms::file))
	{
	  p.sym();
	  p.str(pth);
	  p.esym(syms::content);
          p.hex(ident);
          // L(F("read file %s\n") % pth);
	  cs.add_file(file_path(pth));
          cs.apply_delta(file_path(pth), file_id(), file_id(ident));
	}
      else break;

      // if we got here, we read either a dir or file
      while (p.symp(syms::attr))
	{
	  p.sym();
	  p.str(n);
	  p.str(v);
          // L(F("read attr %s : %s = %s\n") % pth % n % v);
	  cs.set_attr(file_path(pth), n, v);
	}
    }

  // now store the results into the provided mfest
  m.reset(c.new_mfest);
}


void
print_mfest(basic_io::printer & p,
	    mfest const & m)
{
  for (dfs_iter i(m.root); !i.finished(); ++i)
    {
      node_t curr = *i;
      path_vec_t pv;
      m.get_path(curr, pv);

      file_path fp = pv.to_file_path();

      basic_io::stanza st;
      if (is_dir_t(curr))
        {
          // L(F("printing dir %s\n") % fp);
          st.push_str_pair(syms::dir, fp());
        }
      else
	{
	  file_t ftmp = downcast_to_file_t(curr);
	  st.push_str_pair(syms::file, fp());
	  st.push_hex_pair(syms::content, ftmp->content.inner()());
          // L(F("printing file %s\n") % fp);
	}
      if (curr->has_attrs())
	{
	  for (map<attr_name, attr_val>::const_iterator j = curr->fancy->attrs.begin();
	       j != curr->fancy->attrs.end(); ++j)
            {
              // L(F("printing attr %s : %s = %s\n") % fp % j->first % j->second);
              st.push_str_triple(syms::attr, j->first, j->second);
            }
	}
      p.print_stanza(st);
    }  
}


void
read_mfest(data const & dat,
	   mfest & mf)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "mfest");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_mfest(pars, mf);
  I(src.lookahead == EOF);
  mf.check_sane();
}


void
write_mfest(mfest const & mf,
	    data & dat)
{
  mf.check_sane();
  std::ostringstream oss;
  basic_io::printer pr(oss);
  print_mfest(pr, mf);
  dat = data(oss.str());  
}



#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "sanity.hh"
#include "transforms.hh"

// TODO: copy more tests from change_set.cc into here, adapt to the
// new circumstances

file_id null_file_id;


struct
change_automaton
{

  change_automaton()
  {
    srand(0x12345678);
  }

  string new_word()
  {
    static string wordchars = "abcdefghijlkmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static unsigned tick = 0;
    string tmp;
    do 
      {
        tmp += wordchars[rand() % wordchars.size()];
      }
    while (tmp.size() < 10 && !flip(10));
    return tmp + lexical_cast<string>(tick++);
  }

  file_id new_ident()
  {
    static string tab = "0123456789abcdef";
    string tmp;
    tmp.reserve(constants::idlen);
    for (unsigned i = 0; i < constants::idlen; ++i)
      tmp += tab[rand() % tab.size()];
    return file_id(tmp);
  }

  dirent_t new_entry()
  {
    return path_vec_t::intern_component(new_word());
  }

  bool flip(unsigned n = 2)
  {
    return (rand() % n) == 0;
  }

  attr_name pick_attr(node_t n)
  {
    I(n->has_attrs());
    vector<attr_name> tmp;
    for (map<attr_name, attr_val>::const_iterator i = n->fancy->attrs.begin();
         i != n->fancy->attrs.end(); ++i)
      {
        tmp.push_back(i->first);
      }
    return tmp[rand() % tmp.size()];
  }

  enum action
    {
      add_a_node = 0,
      delete_a_node = 1,
      rename_a_node = 2,
      apply_a_delta = 3,
      set_an_attribute = 4,
      clear_an_attribute = 5,
      
      number_of_actions = 6
    };
  
  void inspect(mfest const & m,
               bool & has_nonroot_nodes,
               bool & has_attrs)
  {

    has_nonroot_nodes = false;
    has_attrs = false;

    for (bfs_iter i(m.root); !i.finished(); ++i)
      {
        if ((*i)->ident != m.root->ident)
          has_nonroot_nodes = true;

        if ((*i)->has_attrs())
          has_attrs = true;
        
        if (has_nonroot_nodes 
            && has_attrs)
          return;
      }
  }
  
  void perform_random_action(cset & c)
  {
    set<ident_t> parents;
    path_vec_t pv_a, pv_b;
    file_path fp_a, fp_b;

    bool has_nonroot_nodes;
    bool has_attrs;

    change_consumer & cs = c;

    inspect(c.new_mfest, 
            has_nonroot_nodes, 
            has_attrs);

    vector<node_t> nodes;
    get_nodes(c.new_mfest, nodes);

    bool did_something = false;
    while (! did_something)
      {    
        switch (static_cast<enum action>(rand() % static_cast<unsigned>(number_of_actions)))
          {

          case add_a_node:
            {
              node_t n = random_node(c.new_mfest, nodes, pv_a, parents);
              if (is_dir_t(n) && flip())
                {
                  // add a child of an existing entry
                  pv_a /= new_entry();
                }
              else
                {
                  // add a sibling of an existing entry
                  pv_a.leaf = new_entry();
                }
          
              fp_a = pv_a.to_file_path();

              if (flip())
                cs.add_dir(fp_a);
              else
                {
                  cs.add_file(fp_a);
                  cs.apply_delta(fp_a, null_file_id, new_ident());            
                }
              did_something = true;
            }
            break;
        
          case delete_a_node:
            {
              if (has_nonroot_nodes)
                {
                  node_t n = random_node(c.new_mfest, nodes, pv_a, parents);
                  
                  if (n->live() 
                      && (n->ident != c.new_mfest.root->ident)
                      && (is_file_t(n) || (is_dir_t(n) 
                                           && downcast_to_dir_t(n)->entries.empty())))
                    {
                      fp_a = pv_a.to_file_path();
                      cs.delete_node(fp_a);
                      did_something = true;
                    }
                }
              break;
          
            }

          case rename_a_node:
            {              
              if (has_nonroot_nodes)
                {
                  node_t src = random_node(c.old_mfest, nodes, pv_a, parents);
                  node_t dst = random_node(c.new_mfest, nodes, pv_b, parents);
                  
                  // we want to be sure we're not moving src into one of 
                  // its own children; this is the same as saying that
                  // src isn't in dst's parents
                  
                  if (src->live() 
                      && dst->live()
                      && (parents.find(src->ident) == parents.end()))
                    {
                      if (is_dir_t(dst))
                        {                          
                          pv_b /= new_entry();
                        }
                      else
                        {
                          pv_b.leaf = new_entry();
                        }
                      fp_a = pv_a.to_file_path();
                      fp_b = pv_b.to_file_path();
                      cs.rename_node(fp_a, fp_b);
		      cs.finalize_renames();
                      did_something = true;
                    }
                }
            }
            break;

          case apply_a_delta:
            {
              if (has_nonroot_nodes)
                {
                  node_t f = random_node(c.new_mfest, nodes, pv_a, parents);              
                  if (f->live() && is_file_t(f))
                    {
                      cs.apply_delta(pv_a.to_file_path(),
                                     downcast_to_file_t(f)->content, 
                                     new_ident());
                      did_something = true;
                    }
                }
            }
            break;

          case set_an_attribute:
            {
              if (has_nonroot_nodes)
                {
                  node_t n = random_node(c.new_mfest, nodes, pv_a, parents);
                  if (n->ident != c.new_mfest.root->ident)
                    {
                      fp_a = pv_a.to_file_path();
                      cs.set_attr(fp_a, new_word(), new_word());
                      did_something = true;
                    }
                }
            }
            break;
        
          case clear_an_attribute:
            {
              if (has_nonroot_nodes && has_attrs)
                {
                  node_t n = random_node(c.new_mfest, nodes, pv_a, parents);
                  if (n->ident != c.new_mfest.root->ident)
                    {
                      fp_a = pv_a.to_file_path();
                      if (n->has_attrs())
                        {
                          cs.clear_attr(fp_a, pick_attr(n));
                          did_something = true;
                        }
                    }
                }
            }
            break;

          case number_of_actions:
            break;
                
          }
      }
  }
    
  void get_nodes(mfest const & m, vector<node_t> & nodes)
  {
    nodes.clear();
    for (bfs_iter i(m.root); !i.finished(); ++i)
      nodes.push_back(*i);
  }

  node_t random_node(mfest const & m, 
                     vector<node_t> & nodes,
                     path_vec_t & pv,
                     set<ident_t> & parents)
  {
    parents.clear();

    node_t result = nodes[rand() % nodes.size()];
    
    for (node_t tmp = result; tmp->ident != m.root->ident; tmp = m.get_node(tmp->parent))
      {
        parents.insert(tmp->ident);
      }

    parents.insert(m.root->ident);

    m.get_path(result, pv);

    return result;
  }

                   
};



static void
spin_mfest(mfest const & m)
{
  data tmp;
  mfest m1, m2;
  m1.reset(m);
  for (unsigned i = 0; i < 5; ++i)
    {
      L(F("spinning %d-entry mfest (pass %d)\n") % m1.nodes.size() % i);
      write_mfest(m1, tmp);
      L(F("wrote mfest: [[%s]]\n") % tmp);
      read_mfest(tmp, m2);
      BOOST_CHECK(equal_up_to_renumbering(m1, m2));
      m1.reset(m2);
    }
}


static void
spin_cset(cset const & cs)
{
  data tmp1;
  cset cs1;
  spin_mfest(cs.old_mfest);
  spin_mfest(cs.new_mfest);
  write_cset(cs, tmp1);
  read_cset(tmp1, cs1);
  for (int i = 0; i < 5; ++i)
    {
      data tmp2;
      cset cs2;
      L(F("spinning cset (pass %d)\n") % i);
      write_cset(cs1, tmp2);
      BOOST_CHECK(tmp1 == tmp2);
      read_cset(tmp2, cs2);
      spin_mfest(cs2.old_mfest);
      spin_mfest(cs2.new_mfest);
      cs1 = cs2;
    }  
}


static void 
basic_cset_test()
{
  try
    {

      cset c;
      change_consumer & cs = c; 
      cs.add_dir(file_path("usr"));
      cs.add_dir(file_path("usr/bin"));
      cs.add_file(file_path("usr/bin/cat"));
      cs.apply_delta(file_path("usr/bin/cat"),
		     null_file_id, 
		     file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));

      cs.add_dir(file_path("usr/local"));
      cs.add_dir(file_path("usr/local/bin"));
      cs.add_file(file_path("usr/local/bin/dog"));
      cs.apply_delta(file_path("usr/local/bin/dog"),
		     null_file_id, 
		     file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));

      spin_cset(c);
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
automaton_cset_test()
{
  mfest m1;

  change_automaton aut;
  for (int i = 0; i < 1000; ++i)
    {
      cset cs1(m1);
      aut.perform_random_action(cs1);

      cset cs2(cs1.new_mfest);
      aut.perform_random_action(cs2);

      cset cs3(cs2.new_mfest);
      aut.perform_random_action(cs3);

      cset csA, csB;

      concatenate_changesets(cs1, cs2, csA);
      concatenate_changesets(csA, cs3, csB);

      m1.reset(csB.new_mfest);
    }
}


void 
add_cset_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&automaton_cset_test));
  suite->add(BOOST_TEST_CASE(&basic_cset_test));
}


#endif // BUILD_UNIT_TESTS
