/*

 an mfest is a collection of nodes, indexed by ident number and
 also held in a tree of nodes. the bucket of identified nodes is
 possibly larger than the tree of nodes.

 a change_set is a pair of mfests. every entry in each mfest exists in
 the other, though some might be detached from the file tree and just
 known by ident.

 a change_consumer -- and the serial form of a change_set -- is the
 way a human wants to read about some work: organized into a set of
 deletes, moves, and adds.

 there is ambiguity in a change_consumer though, regarding the
 resolution of names and the simultineity of operations. specifically,
 each entry in a change_set is either looked up in the pre state or
 post state mfest of a change. a delete is looked up in the pre
 state. a rename's source in the pre state. a rename's destination in
 the post state. an add is looked up in the post state.

 when playing back a change_set, there is a canonical order in which
 entries are emitted: {delete,rename,add}{file,dir}.

 furthermore within each type of entry, the order in which the entries
 are emitted is specified: all entries are emitted in lexicographic
 order, which happens to induce topological order as well (A is always
 emitted before A/B).

 crucially, deletes are ordered by *source space* and adds and renames
 are ordered by *destination space*. we replay these by walking the
 tree rooted in each mfest.

*/


/*

 aliases, splits, joins, nursery, graveyard:
 
 each node has a set of alias nodes.

 if N is a node and A is an alias node of N in the old_manifest of a
 cset C, we say that A split from N in C.  if A is an alias of N in
 the new_manifest of C, we say that A joined N in C.

 if N is not an alias, but actually present in a manifest, we say that N
 is *live* in that manifest. 

   - if N is live in the old_manifest of C, we say N is live-in in C. 
   - if N is live in the new_manifest of C, we say N is live-out in C.

 two special nodes exist *always* in every manifest: 

     1. the nursery
     2. the graveyard

   - if A was split from the nursery in C, we say that A was added in C. 
   - if A was joined to the graveyard in C, we say that A was deleted in C.

 for any nodes M, N in C, we must have a proper lifecycle nesting. that is:

   - if M splits from N in C then N must be live-in in C
   - if M joins to N in C, then N must be live-out in C

 this is normally trivially satisfied by splitting and joining from the 
 nursery and graveyard, respectively. 

 splits exist only in order to serve as the inverse of joins. joins
 exist for only one purpose: when merging two trees which have
 different (non-identified) files occupying the same name, and the
 user decides they *want* to identify the files (eg. 2-way merge). in
 this case we want to join one file to the other, to give future edits
 on the joiner a target to apply to.

 yes? hmm.. we want to avoid this:
 ---------------------------------

 bob makes a tree and syncs it with jane.
 bob adds file foo
 bob syncs with sid
 jane adds file foo
 jane syncs with bob
 jane merges with bob. jane's foo is not the same as bob's foo. jane's foo wins.
 sid makes a change to bob's foo.
 sid syncs with jane.
 sid's change is silently eaten. 


 
 a point: every file *does* have a GUID. the (rev,path) pair it was added in.
 or at least it ought to. the problem is you can add a file in two (rev,path)
 pairs, and then later decide you *wanted* them to behave -- for the sake of
 merging -- as though they are the same file. iow, behave like they had a 
 common ancestor immediately before the states they were both added in.

 so how do joins help here? simply this: when you are working on a merge 
 between A and B, the first step is to decide which files will be live
 in the merge. to do this you take all the files which are live in A and
 live in B. then for each file you've decided will be live, you pull out
 a subgraph for it, containing all the nodes in which it's alive. then you
 add to this subgraph all the nodes *joined* to the live one. that's the
 central trick: you will be doing graph contraction on the joiner as well,
 so that you do not lose any of its deltas.

 further restriction: foo/bar can only ever join foo/bar, and this can
 only happen in a merge in which one of the incoming edges is
 providing foo/bar already; it's actually written (unambiguously) in
 the joiner's changeset as 'join "foo/bar"' (inverse: 'split
 "foo/bar"'). so, scratch previous idea: adds and deletes are still
 expressed as being renamed to or from the nursery or graveyard, not
 split/joined with them.

 
 further: suppose in A->C we have "join foo/bar", where there's a B->C
 edge containing a foo/bar to join with. all well and good. then
 suppose we append D="rename foo/bar foo/baz" to C. then the B->C->D
 edge has "rename foo/bar foo/baz", and the A->C->D edge has ... 
 "join foo/baz"? that makes no sense. 

 we know a join is essentially a "delete with a forwarding address";
 so like a delete it has to take effect *before* anything else in a
 cset. which means it is written in the pre-space of the cset. the
 problem is that the name it refers to is clearly a name in the
 post-space of the delta: the name it's colliding with!

 perhaps "join" is not the right word. perhaps we want something more
 like "doom foo/bar; rename foo/bar foo/baz" in the composite cset: an
 acknowledgment that the node named by foo/bar at the *beginning* of
 the cset is doomed, even if it gets moved around thereafter: the
 proviso of a doomed node eixsting in a revision is that there's a
 non-doomed node occupying the same node. a doomed node is not yet
 dead, but it adheres to some other node until that other node dies.

 subsequently if you doom the node foo/baz (which requires some other
 incoming node in a merge, using that name) then the composite cset
 is "doom foo/bar; doom foo/baz; rename foo/bar foo/baz" and you have
 *two* doomed nodes in the doomed set of the third node. this should
 continue to work until such time as the live node carrying the doomed
 ones actually dies.

 actually that reads terribly. it would be better to have "delete_foo"
 have a secondary clause, "with_heir", where the heir is a name in the
 post-space of the cset. this way the heir can get moved around; and
 the natural inverse is "add_foo" ... "with_sire", where the sire is 
 in the pre-space of the cset. that feels more likely to work.

 */


#define __STDC_CONSTANT_MACROS

#include <algorithm>
#include <deque>
#include <map>
#include <stack>
#include <string>
#include <vector>

#include <ext/hash_map>
#include <ext/hash_set>

#include <boost/shared_ptr.hpp>
#include <boost/filesystem/path.hpp>

#include "basic_io.hh"
#include "sanity.hh"
#include "vocab.hh"

using std::deque;
using std::map;
using std::pair;
using std::stack;
using std::string;
using std::vector;
using std::inserter;
using std::copy;
using std::make_pair;
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

typedef uint32_t ident_t;
typedef shared_ptr<node> node_t;
typedef shared_ptr<dir_node> dir_t;
typedef shared_ptr<file_node> file_t;
typedef shared_ptr<dir_entry> dirent_t;
// dirmap *must* be a sorted map, do not change to a hashed map
typedef map<dirent_t, node_t, dirent_t_cmp> dirmap_t; 
typedef hash_set<dirent_t, dirent_hash, dirent_eq> dirset_t;
typedef string attr_name;
typedef string attr_val;

static ident_t 
nursery_ident = 0;

static ident_t 
graveyard_ident = 1;

struct
ident_source
{
  ident_t ctr;
  ident_source() : ctr(graveyard_ident + 1) {}
  ident_t next() { I(ctr != UINT32_C(0xffffffff)); return ctr++; }
};

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



struct 
dirent_t_cmp
{
  bool operator()(dirent_t const & a,
		  dirent_t const & b) const
  {
    return *a < *b;
  }
};

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

  path_vec_t(file_path const & f)
  {
    fs::path fp(f());
    for (fs::path::iterator i = fp.begin(); 
	 i != fp.end(); ++i)
      {
	dirent_t tmp(new dir_entry(*i));
	dirset_t::const_iterator j = dset.find(tmp);
	if (j == dset.end())
	  {
	    dset.insert(tmp);
	  }
	else
	  {
	    tmp = *j;
	  }
	dir.push_back(tmp);
      }
    I(!dir.empty());
    leaf = dir.back();
    dir.pop_back();
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

// static bucket of shared pointers
dirset_t
path_vec_t::dset;

struct 
node
{
  ident_t ident;
  ident_t parent;
  dirent_t name;
  map<attr_name, attr_val> attributes;

  bool unborn() const { return parent == nursery_ident; }
  bool live() const { return ! (unborn() || killed()); }
  bool killed() const { return parent == graveyard_ident; }

  node() : name(new dir_entry("")) {}
  virtual void flush_clobbered() = 0;
  virtual node_t clone() const = 0;
  virtual ~node() {}  
};

struct 
file_node 
  : public node
{
  file_id content;
  
  virtual void flush_clobbered() {}
  virtual node_t clone() const;
  virtual ~file_node() {}
};

node_t 
file_node::clone() const
{
  file_t f = file_t(new file_node());
  f->ident = ident;
  f->parent = parent;
  f->name = name;
  f->content = content;
  copy(attributes.begin(), attributes.end(),
       inserter(f->attributes, f->attributes.begin()));
  return f;
}


struct
dir_node 
  : public node
{
  dirmap_t entries;
  dirmap_t clobbered_entries;

  // informative
  bool contains_entry(dirent_t p) const;
  bool contains_clobbered_entry(dirent_t p) const;
  bool contains_dir(dirent_t p) const;
  bool contains_file(dirent_t p) const;

  // imperative (fault on lookup failures)
  node_t get_entry(dirent_t p) const;
  dir_t get_dir_entry(dirent_t p) const;
  file_t get_file_entry(dirent_t p) const;

  node_t get_clobbered_entry(dirent_t p) const;
  dir_t get_clobbered_dir_entry(dirent_t p) const;
  file_t get_clobbered_file_entry(dirent_t p) const;

  void add_child(dirent_t name, node_t n, bool clobber = false);
  void drop_dir(dirent_t c, bool clobbered_ok = false);
  void drop_file(dirent_t c, bool clobbered_ok = false);

  virtual void flush_clobbered();
  virtual node_t clone() const;
  virtual ~dir_node() {}
};


void 
dir_node::flush_clobbered()
{
  dirmap_t::const_iterator 
    i = clobbered_entries.begin(),
    j = clobbered_entries.end();
  while(i != j)
    {
      i->second->flush_clobbered();
    }
  clobbered_entries.clear();
}

void 
dir_node::add_child(dirent_t name, node_t n, bool clobber)
{
  // this is a bit odd: it is possible that, while processing renames
  // (only), we have actually clobbered a directory entry name in the
  // destination mfest of a cset. for example if we have "rename_file
  // A/X B/Y", where the file in question has ident T. we can find the
  // current parent P of T in the second mfest, but we can't tell what
  // name to drop from P in order to get T properly detached and moved
  // over to its new home in Q=B.
  //
  // if we tell P to drop X, we risk dropping an entry which is not T;
  // it is possible some other entry has already clobbered that name
  // in P. so when an entry is added with clobbering turned on (i.e. when
  // we are performing a rename) the new entry moves any old entry to 
  // the clobber map, where future drops (with clobbers accepted) will
  // short-circuit on it.
  //
  // note that in general (in particular, with respect to resolving
  // the parent directory of the target of an add or rename) there is
  // no risk of clobbers because we emit renames in topological order
  // in the target space: by the time we are processing renames
  // landing on foo/bar/baz, we have already resolved which node
  // "owns" the name bar in foo.

  if (clobber && contains_entry(name))
    {
      I(!contains_clobbered_entry(name));
      clobbered_entries.insert(make_pair(name,get_entry(name)));
      entries.erase(name);
    }
  I(!contains_entry(name));
  L(F("parent %d adding child %d = '%s'\n") % ident % n->ident % name->val);
  n->name = name;
  n->parent = ident;
  entries.insert(make_pair(name,n));
}

void 
dir_node::drop_dir(dirent_t c, bool clobbered_ok)
{
  if (clobbered_ok && contains_clobbered_entry(c))
    {
      dir_t tmp = get_clobbered_dir_entry(c);
      clobbered_entries.erase(c);
    }
  else
    {
      dir_t tmp = get_dir_entry(c);
      entries.erase(c);
    }
}

void 
dir_node::drop_file(dirent_t c, bool clobbered_ok)
{
  if (clobbered_ok && contains_clobbered_entry(c))
    {
      file_t tmp = get_clobbered_file_entry(c);
      clobbered_entries.erase(c);
    }
  else
    {
      file_t tmp = get_file_entry(c);
      entries.erase(c);
    }
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

static bool
equal_nodes(node_t const & n1, 
	    node_t const & n2,
	    bool compare_children = false)
{
  deque<node_t> q1;
  deque<node_t> q2;

  q1.push_back(n1);
  q2.push_back(n2);

  while(!q1.empty())
    {

      if (q2.empty())
	return false;

      node_t a = q1.front();
      node_t b = q2.front();
      q1.pop_front();
      q2.pop_front();

      if (!(a->ident == b->ident))
	return false;

      if (!(a->parent == b->parent))
	return false;

      if (! (*(a->name) == *(b->name)))
	return false;

      if (!(a->attributes == b->attributes))
	return false;

      if (is_file_t(a))
	{
	  if (! is_file_t(b))
	    return false;

	  file_t af = downcast_to_file_t(a);
	  file_t bf = downcast_to_file_t(b);

	  if (!(af->content == bf->content))
	    return false;
	}
      else
	{
	  I(is_dir_t(a));

	  if (! is_dir_t(b))
	    return false;

	  dir_t ad = downcast_to_dir_t(a);
	  dir_t bd = downcast_to_dir_t(b);

	  if (ad->entries.size() != bd->entries.size())
	    return false;

	  dirmap_t::const_iterator i = ad->entries.begin();
	  dirmap_t::const_iterator j = bd->entries.begin();

	  while (i != ad->entries.end())
	    {
	      I(j != bd->entries.end());

	      if (! (*(i->first) == *(j->first)))
		return false;

	      if (compare_children)
		{
		  q1.push_back(i->second);
		  q2.push_back(j->second);
		}
	      else
		{
		  if (i->second->ident != i->second->ident)
		    return false;
		}
	      ++i;
	      ++j;
	    }
	}
    }
  return true;
}

bool 
dir_node::contains_entry(dirent_t p) const
{
  map<dirent_t,node_t>::const_iterator i = entries.find(p);
  if (i == entries.end())
    return false;
  return true;
}

bool 
dir_node::contains_clobbered_entry(dirent_t p) const
{
  map<dirent_t,node_t>::const_iterator i = clobbered_entries.find(p);
  if (i == clobbered_entries.end())
    return false;
  return true;
}

bool 
dir_node::contains_dir(dirent_t p) const
{
  dirmap_t::const_iterator i = entries.find(p);
  if (i == entries.end())
    return false;
  return is_dir_t(i->second);
}

bool 
dir_node::contains_file(dirent_t p) const
{
  dirmap_t::const_iterator i = entries.find(p);
  if (i == entries.end())
    return false;
  return is_file_t(i->second);
}

node_t 
dir_node::get_entry(dirent_t p) const
{
  L(F("dir_node::get_entry(\"%s\")\n") % p->val);
  dirmap_t::const_iterator i = entries.find(p);
  I(i != entries.end());
  return i->second;
}

dir_t 
dir_node::get_dir_entry(dirent_t p) const
{
  return downcast_to_dir_t(get_entry(p));
}

file_t 
dir_node::get_file_entry(dirent_t p) const
{
  return downcast_to_file_t(get_entry(p));
}

node_t 
dir_node::get_clobbered_entry(dirent_t p) const
{
  dirmap_t::const_iterator i = clobbered_entries.find(p);
  I(i != clobbered_entries.end());
  return i->second;
}

dir_t 
dir_node::get_clobbered_dir_entry(dirent_t p) const
{
  return downcast_to_dir_t(get_clobbered_entry(p));
}

file_t 
dir_node::get_clobbered_file_entry(dirent_t p) const
{
  return downcast_to_file_t(get_clobbered_entry(p));
}

node_t 
dir_node::clone() const
{
  dir_t d = dir_t(new dir_node());
  d->ident = ident;
  d->parent = parent;
  d->name = name;
  copy(attributes.begin(), attributes.end(),
       inserter(d->attributes, d->attributes.begin()));
  for (map<dirent_t,node_t>::const_iterator i = entries.begin();
       i != entries.end(); ++i)
    {
      d->entries.insert(make_pair(i->first, i->second->clone()));
    }
  for (map<dirent_t,node_t>::const_iterator i = clobbered_entries.begin();
       i != clobbered_entries.end(); ++i)
    {
      d->clobbered_entries.insert(make_pair(i->first, i->second->clone()));
    }
  return d;
}


typedef hash_map<ident_t, node_t> node_map_t;

static void
insert_into_map(dir_t d,
		node_map_t & nodes)
{
  deque<node_t> work;
  work.push_back(d);
  while (!work.empty())
    {
      node_t n = work.front();
      work.pop_front();

      nodes.insert(make_pair(n->ident, n));

      if (is_dir_t(n))
	{
	  dir_t tmp = downcast_to_dir_t(n);
	  for (dirmap_t::const_iterator i = tmp->entries.begin();
	       i != tmp->entries.end(); ++i)
	    {
	      work.push_back(i->second);
	    }
	}
    }
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
    root(make_dir()) 
  {
    insert_into_map(root, nodes);
  }

  mfest(mfest const & other);
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

  dir_t get_containing_dir_node(path_vec_t const & d) const;  
  bool operator==(mfest const & other) const;
};


mfest::mfest(mfest const & other) :
  max_ident(other.max_ident),
  root(downcast_to_dir_t(other.root->clone()))  
{
  insert_into_map(root, nodes);
}

dir_t 
mfest::make_dir()
{
  dir_t n(new dir_node());
  n->ident = ++max_ident;
  n->parent = nursery_ident;
  nodes.insert(make_pair(n->ident, n));
  return n;
}

file_t 
mfest::make_file()
{
  file_t n(new file_node());
  n->ident = ++max_ident;
  n->parent = nursery_ident;
  nodes.insert(make_pair(n->ident, n));
  return n;
}

void
mfest::check_sane() const
{

  deque<ident_t> work;
  hash_set<ident_t> seen;
  work.push_back(root->ident);

  // first go from the top of the tree to the bottom checking each
  // directory for absence of cycle-forming edges and agreement
  // between names.

  while (!work.empty())
    {
      ident_t tmp = work.front();
      work.pop_front();

      I(seen.find(tmp) == seen.end());
      seen.insert(tmp);

      node_t tnode = get_node(tmp);
      if (is_dir_t(tnode))
	{
	  dir_t tdir = downcast_to_dir_t(tnode);
	  for (dirmap_t::const_iterator i = tdir->entries.begin();
	       i != tdir->entries.end(); ++i)
	    {
	      node_t child = i->second;
	      I(i->first == child->name);
	      I(child->parent == tdir->ident);
	      work.push_back(i->second->ident);
	    }
	}
    }
  
  // now go through the node map and make sure that we both found
  // every node, and also that the nodes have the same belief about
  // their ident that the node map has

  for (node_map_t::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      if (i->second->parent != nursery_ident 
	  && i->second->parent != graveyard_ident)
	{
	  I(seen.find(i->first) != seen.end());
	}
      I(i->first == i->second->ident);
    }    
}

bool 
mfest::file_exists(file_path fp) const
{
  path_vec_t v(fp);
  dir_t d = root;
  vector<dirent_t>::const_iterator i = v.dir.begin(), j = v.dir.end();
  while(i != j)
    {
      if (!d->contains_dir(*i))
	return false;

      d = d->get_dir_entry(*i++);
    }
  return d->contains_file(v.leaf);
}

bool 
mfest::dir_exists(file_path dp) const
{
  path_vec_t v(dp);
  dir_t d = root;
  vector<dirent_t>::const_iterator i = v.dir.begin(), j = v.dir.end();
  while(i != j)
    {
      if (!d->contains_dir(*i))
	return false;

      d = d->get_dir_entry(*i++);
    }
  return d->contains_dir(v.leaf);
}

node_t 
mfest::get_node(ident_t i) const
{
  L(F("mfest::get_node(%s)\n") % i);
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
  vector<dirent_t>::const_iterator i = v.dir.begin(), j = v.dir.end();
  while(i != j)
    {
      d = d->get_dir_entry(*i++);
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

bool 
mfest::operator==(mfest const & other) const
{
  if (max_ident != other.max_ident)
    return false;

  if (!equal_nodes(root, other.root, true))
    return false;

  for (node_map_t::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      node_map_t::const_iterator j = other.nodes.find(i->first);

      if (j == other.nodes.end())
	return false;

      if (!equal_nodes(i->second, j->second, false))
	return false;
    }
  return true;
}




struct
change_consumer
{
  void delete_dir(file_path const & dp);
  void delete_file(file_path const & fp);
  void rename_dir(file_path const & src, file_path const & dst);
  void rename_file(file_path const & src, file_path const & dst);
  void add_dir(file_path const & dp);
  void add_file(file_path const & fp);
  void apply_delta(file_path const & path, 
		   file_id const & src, 
		   file_id const & dst);

  virtual void delete_dir(path_vec_t const & dp) = 0;
  virtual void delete_file(path_vec_t const & fp) = 0;
  virtual void rename_dir(path_vec_t const & src, path_vec_t const & dst) = 0;
  virtual void rename_file(path_vec_t const & src, path_vec_t const & dst) = 0;
  virtual void add_dir(path_vec_t const & dp) = 0;
  virtual void add_file(path_vec_t const & fp) = 0;
  virtual void apply_delta(path_vec_t const & path, 
			   file_id const & src, 
			   file_id const & dst) = 0;
};

void 
change_consumer::delete_dir(file_path const & dp)
{
  L(F("delete_dir('%s')\n") % dp);
  this->delete_dir(path_vec_t(dp));
}

void 
change_consumer::delete_file(file_path const & fp)
{
  L(F("delete_file('%s')\n") % fp);
  this->delete_file(path_vec_t(fp));  
}

void 
change_consumer::rename_dir(file_path const & src, 
			    file_path const & dst)
{
  L(F("rename_dir('%s', '%s')\n") % src % dst);
  this->rename_dir(path_vec_t(src), path_vec_t(dst));   
}

void 
change_consumer::rename_file(file_path const & src, 
			     file_path const & dst)
{
  L(F("rename_file('%s', '%s')\n") % src % dst);
  this->rename_file(path_vec_t(src), path_vec_t(dst));
}

void 
change_consumer::add_dir(file_path const & dp)
{
  L(F("add_dir('%s')\n") % dp);
  this->add_dir(path_vec_t(dp));
}

void 
change_consumer::add_file(file_path const & fp)
{
  L(F("add_file('%s')\n") % fp);
  this->add_file(path_vec_t(fp));
}

void 
change_consumer::apply_delta(file_path const & path, 
			     file_id const & src, 
			     file_id const & dst)
{
  L(F("apply_delta('%s', [%s], [%s])\n") % path % src % dst);
  this->apply_delta(path_vec_t(path), src, dst);
}


struct 
cset 
  : public change_consumer
{ 
  mfest old_mfest;
  mfest new_mfest;

  cset(mfest const & base) :
    old_mfest(base), 
    new_mfest(base)
  {}

  cset() 
  {}

  virtual ~cset() {}

  void reset(mfest const & m);
  void check_sane() const;

  void replay_changes(change_consumer & cc) const;
  void replay_inverse_changes(change_consumer & cc) const;

  virtual void delete_dir(path_vec_t const & dp);
  virtual void delete_file(path_vec_t const & fp);
  virtual void rename_dir(path_vec_t const & src, path_vec_t const & dst);
  virtual void rename_file(path_vec_t const & src, path_vec_t const & dst);
  virtual void add_dir(path_vec_t const & dp);
  virtual void add_file(path_vec_t const & fp);
  virtual void apply_delta(path_vec_t const & path, 
			   file_id const & src, 
			   file_id const & dst);

  bool operator==(cset const & other) const
  {
    if (! (old_mfest == other.old_mfest))
      return false;
    if (! (new_mfest == other.new_mfest))
      return false;
    return true;
  }

};


void 
cset::reset(mfest const & m)
{
  old_mfest = m;
  new_mfest = m;
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
  old_mfest.check_sane();
  new_mfest.check_sane();
  check_mfests_agree(old_mfest, new_mfest);
}

void 
cset::delete_dir(path_vec_t const & dp)
{
  dir_t src = old_mfest.get_dir_node(dp);
  dir_t dst = new_mfest.get_dir_node(src->ident);
  dir_t parent_of_dst = new_mfest.get_dir_node(dst->parent);
  parent_of_dst->drop_dir(dp.leaf);
  dst->parent = graveyard_ident;
  if (global_sanity.debug)
    {
      check_sane();
    }
}

void 
cset::delete_file(path_vec_t const & fp)
{
  file_t src = old_mfest.get_file_node(fp);
  file_t dst = new_mfest.get_file_node(src->ident);
  dir_t parent_of_dst = new_mfest.get_dir_node(dst->parent);
  parent_of_dst->drop_file(fp.leaf);
  dst->parent = graveyard_ident;
  if (global_sanity.debug)
    {
      check_sane();
    }
}

void 
cset::rename_dir(path_vec_t const & s, 
		 path_vec_t const & d)
{
  dir_t src = old_mfest.get_dir_node(s);
  dir_t dst = new_mfest.get_dir_node(src->ident);
  dir_t old_dst_parent = new_mfest.get_dir_node(dst->parent);
  dir_t new_dst_parent = new_mfest.get_containing_dir_node(d);
  old_dst_parent->drop_dir(dst->name, true);
  new_dst_parent->add_child(d.leaf, dst, true);
  if (global_sanity.debug)
    {
      check_sane();
    }
}

void 
cset::rename_file(path_vec_t const & s, 
		  path_vec_t const & d)
{
  file_t src = old_mfest.get_file_node(s);
  file_t dst = new_mfest.get_file_node(src->ident);
  dir_t old_dst_parent = new_mfest.get_dir_node(dst->parent);
  dir_t new_dst_parent = new_mfest.get_containing_dir_node(d);
  old_dst_parent->drop_file(dst->name, true);
  new_dst_parent->add_child(d.leaf, dst, true);
  if (global_sanity.debug)
    {
      check_sane();
    }
}

void cset::add_dir(path_vec_t const & dp)
{
  dir_t new_dst_parent = new_mfest.get_containing_dir_node(dp);
  dir_t new_dir = old_mfest.make_dir();
  dir_t new_clone = downcast_to_dir_t(new_dir->clone());
  new_dst_parent->add_child(dp.leaf, new_clone);
  new_mfest.nodes.insert(make_pair(new_dir->ident, new_clone));
  if (global_sanity.debug)
    {
      check_sane();
    }
}

void cset::add_file(path_vec_t const & fp)
{
  dir_t new_dst_parent = new_mfest.get_containing_dir_node(fp);
  file_t new_file = old_mfest.make_file();
  file_t new_clone = downcast_to_file_t(new_file->clone());
  new_dst_parent->add_child(fp.leaf, new_clone);
  new_mfest.nodes.insert(make_pair(new_file->ident, new_clone));
  if (global_sanity.debug)
    {
      check_sane();
    }
}

void cset::apply_delta(path_vec_t const & path, 
		       file_id const & s, 
		       file_id const & d)
{
  file_t dst = new_mfest.get_file_node(path);
  file_t src = old_mfest.get_file_node(dst->ident);
  src->content = s;
  dst->content = d;
  if (global_sanity.debug)
    {
      check_sane();
    }
}

static void 
play_changes(change_consumer & cc, 
	     mfest const & a,
	     mfest const & b);

void 
cset::replay_changes(change_consumer & cc) const
{
  check_sane();
  play_changes(cc, old_mfest, new_mfest);
}

void 
cset::replay_inverse_changes(change_consumer & cc) const
{
  check_sane();
  play_changes(cc, new_mfest, old_mfest);
}

enum
replay_mode_t
  {
    replay_delete_dir,
    replay_delete_file,
    replay_rename_dir,
    replay_rename_file,
    replay_add_dir,
    replay_add_file,
    replay_apply_delta
  };


static void
play_changes_from_dir(change_consumer & cc, 
		      mfest const & src,
		      mfest const & dst,
		      replay_mode_t mode,
		      dir_t cwd)
{

  typedef pair<dir_t, dirmap_t::const_iterator> state;
  stack<state> stk;

  stk.push(make_pair(cwd, cwd->entries.begin()));

  while (! stk.empty())
    {
      bool pushed = false;

      for (state & curr = stk.top(); 
	   curr.second != curr.first->entries.end()  && !pushed; 
	   ++curr.second)
	{

	  cwd = curr.first;
	  L(F("replaying %s = %d\n") % cwd->name->val % cwd->ident);
	  node_t self = curr.second->second, other;
	  path_vec_t v1, v2;

	  switch (mode)
	    { 
	    case replay_delete_dir:
	      // cwd is a directory in src space	  
	      other = dst.get_node(self->ident);
	      if (is_dir_t(self) 
		  && self->live() 
		  && other->killed())
		{
		  I(is_dir_t(other));
		  src.get_path(self, v1);
		  cc.delete_dir(v1);
		}
	      break;

	    case replay_delete_file:
	      // cwd is a directory in src space
	      other = dst.get_node(self->ident);
	      if (is_file_t(self) 
		  && self->live()
		  && other->killed())
		{
		  I(is_file_t(other));
		  src.get_path(self, v1);
		  cc.delete_file(v1);
		}
	      break;

	    case replay_rename_dir:
	      // cwd is a directory in dst space
	      other = src.get_node(self->ident);
	      if (is_dir_t(self) 
		  && self->live()
		  && other->live()
		  && ((other->name->val != self->name->val)
		      || (other->parent != self->parent)))
		{
		  I(is_dir_t(other));
		  src.get_path(other, v1);
		  dst.get_path(self, v2);
		  cc.rename_dir(v1, v2);
		}
	      break;

	    case replay_rename_file:
	      // cwd is a directory in dst space
	      other = src.get_node(self->ident);
	      if (is_file_t(self)
		  && self->live()
		  && other->live()
		  && ((other->name->val != self->name->val)
		      || (other->parent != self->parent)))
		{
		  I(is_file_t(other));
		  src.get_path(other, v1);
		  dst.get_path(self, v2);
		  cc.rename_file(v1, v2);
		}
	      break;

	    case replay_add_dir:
	      // cwd is a directory in dst space	
	      other = src.get_node(self->ident);
	      if (is_dir_t(self)
		  && other->unborn()
		  && self->live())
		{
		  I(is_dir_t(other));
		  dst.get_path(self, v2);
		  cc.add_dir(v2);
		}
	      break;

	    case replay_add_file:
	      // cwd is a directory in dst space
	      other = src.get_node(self->ident);
	      if (is_file_t(self)
		  && other->unborn()
		  && self->live())
		{
		  I(is_file_t(other));
		  dst.get_path(self, v2);
		  cc.add_file(v2);
		}
	      break;

	    case replay_apply_delta:
	      // cwd is a directory in dst space
	      other = src.get_node(self->ident);
	      if (is_file_t(self))
		{
		  file_t self_f = downcast_to_file_t(self);
		  file_t other_f = downcast_to_file_t(other);
		  dst.get_path(self, v2);
	      
		  if (other->unborn() 
		      && self->live())
		    {
		      // deltas accompanying additions
		      cc.apply_delta(v2, file_id(), self_f->content);
		    }
		  else if (self->live() 
			   && other->live() 
			   && !(self_f->content == other_f->content))
		    {
		      // normal deltas
		      cc.apply_delta(v2, 
				     other_f->content,
				     self_f->content);
		    }
		}
	      break;
	    }

	  if (is_dir_t(self))
	    {
	      dir_t dtmp = downcast_to_dir_t(self);
	      stk.push(make_pair(dtmp, dtmp->entries.begin())); 
	      pushed = true;
	      L(F("pushed %s = %d\n") % self->name->val % self->ident);
	    }
	}
      
      if (!pushed)
	stk.pop();
    }
}

static void 
play_changes(change_consumer & cc, 
	     mfest const & a,
	     mfest const & b)
{  
  play_changes_from_dir(cc, a, b, replay_delete_dir, a.root);
  play_changes_from_dir(cc, a, b, replay_delete_file, a.root);

  play_changes_from_dir(cc, a, b, replay_rename_dir, b.root);
  play_changes_from_dir(cc, a, b, replay_rename_file, b.root);

  play_changes_from_dir(cc, a, b, replay_add_dir, b.root);
  play_changes_from_dir(cc, a, b, replay_add_file, b.root);

  play_changes_from_dir(cc, a, b, replay_apply_delta, b.root);
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
  c.old_mfest.root->flush_clobbered();
  c.new_mfest.root->flush_clobbered();
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
    std::string const patch("patch");
    std::string const from("from");
    std::string const to("to");
    std::string const add_file("add_file");
    std::string const add_dir("add_dir");
    std::string const delete_file("delete_file");
    std::string const delete_dir("delete_dir");
    std::string const rename_file("rename_file");
    std::string const rename_dir("rename_dir");
  }
}


static void 
parse_cset(basic_io::parser & parser,
	   change_consumer & cc)
{
  while (parser.symp())
    {
      std::string t1, t2, t3;
      if (parser.symp(syms::add_file)) 
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
      else if (parser.symp(syms::delete_file)) 
        { 
          parser.sym();
          parser.str(t1);
          cc.delete_file(file_path(t1));
        }
      else if (parser.symp(syms::delete_dir)) 
        { 
          parser.sym();
          parser.str(t1);
          cc.delete_dir(file_path(t1));
        }
      else if (parser.symp(syms::rename_file)) 
        { 
          parser.sym();
          parser.str(t1);
          parser.esym(syms::to);
          parser.str(t2);
          cc.rename_file(file_path(t1),
                         file_path(t2));
        }
      else if (parser.symp(syms::rename_dir)) 
        { 
          parser.sym();
          parser.str(t1);
          parser.esym(syms::to);
          parser.str(t2);
          cc.rename_dir(file_path(t1),
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
  virtual void delete_dir(path_vec_t const & dp);
  virtual void delete_file(path_vec_t const & fp);
  virtual void rename_dir(path_vec_t const & src, 
			  path_vec_t const & dst);
  virtual void rename_file(path_vec_t const & src, 
			   path_vec_t const & dst);
  virtual void add_dir(path_vec_t const & dp);
  virtual void add_file(path_vec_t const & fp);
  virtual void apply_delta(path_vec_t const & path, 
			   file_id const & src, 
			   file_id const & dst);
};

void 
cset_printer::delete_dir(path_vec_t const & dp)
{
      basic_io::stanza st;
      st.push_str_pair(syms::delete_dir, dp.to_file_path()());
      printer.print_stanza(st);
}

void 
cset_printer::delete_file(path_vec_t const & fp)
{
      basic_io::stanza st;
      st.push_str_pair(syms::delete_file, fp.to_file_path()());
      printer.print_stanza(st);
}

void 
cset_printer::rename_dir(path_vec_t const & src, 
			 path_vec_t const & dst)
{
      basic_io::stanza st;
      st.push_str_pair(syms::rename_dir, src.to_file_path()());
      st.push_str_pair(syms::to, dst.to_file_path()());
      printer.print_stanza(st);
}

void 
cset_printer::rename_file(path_vec_t const & src, 
			   path_vec_t const & dst)
{
      basic_io::stanza st;
      st.push_str_pair(syms::rename_file, src.to_file_path()());
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




#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "sanity.hh"
#include "transforms.hh"

// TODO: copy more tests from change_set.cc into here, adapt to the
// new circumstances

file_id null_file_id;

static void
spin_cset(cset const & cs)
{
  data tmp1;
  cset cs1;
  write_cset(cs, tmp1);
  read_cset(tmp1, cs1);
  for (int i = 0; i < 5; ++i)
    {
      data tmp2;
      cset cs2;
      write_cset(cs1, tmp2);
      BOOST_CHECK(tmp1 == tmp2);
      read_cset(tmp2, cs2);
      BOOST_CHECK(cs1 == cs2);
      cs1 = cs2;
    }
}

static void 
basic_cset_test()
{
  try
    {

      cset cs;
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

      spin_cset(cs);
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

void 
add_cset_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&basic_cset_test));
}


#endif // BUILD_UNIT_TESTS
