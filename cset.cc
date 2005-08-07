/*

Nodes:
~~~~~~

 A node is either a file or a directory. Nodes have a name and
 attributes. Directory nodes have a map of children. File nodes have a
 content hash. See below for the definitions of these members.


Mfests:
~~~~~~~

 An mfest is a structure containing a directory node, with a sanity
 checking routine to confirm that the tree is acyclic. Since mfests
 implement copy-on-write (see below; essentially you have to be rather
 careful about touching their insides as a result) we will probably keep
 mfests opaque outside this module.


Csets:
~~~~~~

 A cset is a change_consumer (see below) and also an object which
 can be analyzed to *feed* a change_consumer. Structurally, it is:

    - a pair of mfests "old" and "new" 
    - an "incubator" set of unborn nodes
    - a "graveyard" set of killed nodes
    - a pair of maps relating copied nodes in "new" to 
      their corresponding pre-copy node in "old"
    - a pair of maps connecting nodes to their parents
      in "old" and "new" (these cannot be stored in the
      nodes themselves because much of the graph may be
      shared)

 We should have that:

    {OLD union incubator} == {NEW union graveyard}

 As well, the parents in each map should always match up with the child
 relationships stored in the nodes, and the new->old and old->new maps
 should agree.
 

Change_consumers:
~~~~~~~~~~~~~~~~~

 A change_consumer -- and the serial form of a change_set -- is the
 way a human wants to read about some work: organized into a set of
 deletes, renames, adds, deltas, and attribute set/clear operations.

 There is ambiguity in a change_consumer regarding the resolution of
 names and the simultineity of operations. Specifically, each entry in
 a change_set is either looked up in the pre-state or post-state mfest
 of a change. A delete is looked up in the pre-state. a rename's
 source in the pre-state. A rename's destination in the post-state. An
 add is looked up in the post-state.

 When playing back a change_set, there is a canonical order in which
 entries are emitted: deleted files, deleted dirs, renamed nodes,
 added dirs, added files, applied deltas, attrs cleared, attrs set.

 Furthermore within each type of entry, the order in which the entries
 are emitted is specified: deletes are emitted in reverse-lexicographic
 order (B, A/B, A) and all other entries are emitted in lexicographic
 order (A, A/B, B).

 Crucially, delete is ordered by *source space*; rename, add, apply_delta,
 clear_attr, and set_attr are ordered by *destination space*. We replay
 these by walking the tree rooted in each mfest.


Copy-on-write:
~~~~~~~~~~~~~~

 When copying an mfest, only a shallow copy of the root shared_ptr is made;
 When writing to the copy (say inside a cset), you must check the
 shared_ptr you're writing to (and all its parents) to see that they are
 shared_ptr::unique, and if not then you must make a stepwise copy down to
 the child in question. This means that often an mfest contains pointers
 into other mfests; the new mfest in a cset usually contains pointers into
 the old one. The cset::get_writable_*() family of helpers should do the
 right thing, as far as preparing a node in the new_mfest for writing.


Attach and detach lists:
~~~~~~~~~~~~~~~~~~~~~~~~

 This is a subtle point. Read it until it's clear. 

 Csets contain an implicit order to their operations: a block of delete
 actions, then renames, adds, deltas, and finally attribute
 changes. Moreover as we've seen above, within each block there is a
 topological order: renames landing on A/B happen before renames landing on
 A/B/C, for example.

 There is still, however, an ordering hazard when you apply renames
 "in-place", either to an mfest or a filesystem: it's possible that a
 rename-target will land on a name which is *in the process* of being
 moved elsewhere -- by another rename in the cset -- but has not yet
 been moved due to the serial order chosen. Consider this cset:

    rename_file "A/B"
             to "P/Q"

    rename_file "P/Q"
            to  "Y/Z"

 This is the lexicographic order they will be emitted in. Because these
 names exist in the same cset, they are to be executed "simultaneously" in
 theory; but programs execute sequentially, so naively we would perform the
 first rename, then the second.

 Suppose we executed them "sequentially". For the first rename we
 would do this:
 
 - find the node "A/B" in the OLD mfest, call it file_0.
 - find file_0 in the NEW mfest, call its parent parent_0. 
 - detach "B" from parent_0 in the NEW mfest.
 - find "P" in the NEW mfest, call it parent_1.
 - attach file_0 to parent_1 under the name "Q". 

 This last action will fail. It will fail because parent_1 already has
 a child called Q in the second mfest (it's the source of the next
 rename, and it hasn't been moved elsewhere yet). We the readers know
 that by the time the cset is finished applying, Q will have been
 detached from parent_1 in the NEW mfest. But in the meantime we have
 a problem.

 So what we do instead is execute the first "half" of the renames in a
 preliminary pass, then the second "half" of them in a second pass. The
 change_consumer interface has a special "finalize_renames()" method which
 triggers the second half; it is called after the last rename is
 processed. Here's how we *actually* process the renames:

  - buffer all the renames into a vector of src/dst pairs, in order. 
  - when the user calls finalize_renames():
    - build a temporary vector of nodes with the same length as the buffer
    - walk the vector from end to beginning, detaching each node from 
      the NEW tree and saving it in the temporary vector ("bottom-up")
    - walk the vector from beginning to end, attaching each node to 
      the NEW tree ("top-down")

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
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
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
using boost::shared_ptr;
using boost::dynamic_pointer_cast;
using boost::weak_ptr;
using __gnu_cxx::hash_map;
using __gnu_cxx::hash_set;


//==================================================================
// dirents and paths
//==================================================================

struct
dirent_t
{
  // A dirent is a special "kind of pointer": it's one which has an
  // operator< that obeys the lexicographic order of the strings it points
  // to, and which considers itself "equal" to a dirent_t based on equality
  // of the pointed-to string.
  //
  // Storage for the stable strings is *never reclaimed*; they just
  // accumulate in the dirent_t::stable_strings set forever through the
  // life of the program. They're expected to be small and very frequently
  // reused.

  struct 
  hash
  { 
    size_t operator()(string const * const & x) const
    { 
      return __gnu_cxx::__stl_hash_string(x->c_str());
    }
  };
  
  struct 
  eq
  {
    bool operator()(string const * const & x,
                    string const * const & y) const
    {
      return *x == *y;
    }
  };

  string const * stable_string;
  typedef hash_set<string const *, hash, eq> stable_string_set;
  static stable_string_set stable_strings;

  string const & val() const
  {
    return *stable_string;
  }

  void set(string const & s)
  {
    I(s != "..");
    I(s != ".");
    I(s.find('/') == string::npos);

    stable_string = NULL;
    stable_string_set::const_iterator i = stable_strings.find(&s);
    if (i != stable_strings.end())
      stable_string = *i;
    else
      {
        stable_string = new string(s);
        stable_strings.insert(stable_string);
      }
  }

  dirent_t()
  {
    set("");
  }
  
  dirent_t(string const & s)
  {
    set(s);
  }

  dirent_t(dirent_t const & other)
    : stable_string(other.stable_string)
  {}
};


dirent_t::stable_string_set 
dirent_t::stable_strings;


bool
operator<(dirent_t const & a, 
          dirent_t const & b)
{
  return *(a.stable_string) < *(b.stable_string);
}


bool
operator==(dirent_t const & a,
           dirent_t const & b)
{
  return *(a.stable_string) == *(b.stable_string);
}


bool
operator!=(dirent_t const & a,
           dirent_t const & b)
{
  return *(a.stable_string) != *(b.stable_string);
}


ostream &
operator<<(ostream & o,
           dirent_t const & d)
{
  return o << *(d.stable_string);
}


struct node;
struct dir_node;
struct file_node;

typedef shared_ptr<node> node_t;
typedef weak_ptr<node> weak_node_t;
typedef shared_ptr<dir_node> dir_t;
typedef shared_ptr<file_node> file_t;

// dirmap *must* be a sorted map, do not change to a hashed map
typedef map<dirent_t, node_t> dirmap_t; 
typedef string attr_name;
typedef string attr_val;

// this helper class represents an "unpacked" view of the path
// through a tree of directory nodes to some specific leaf (either
// dir or file); it is just a faster form of a non-empty file_path

struct 
path_vec_t
{
  vector<dirent_t> dir;
  dirent_t leaf;

  static path_vec_t from_file_path(file_path const & f)
  {
    fs::path fp(f());
    path_vec_t pv;
    for (fs::path::iterator i = fp.begin(); 
	 i != fp.end(); ++i)
      {
	pv.dir.push_back(dirent_t(*i));
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
	fp /= i->val();
      }
    fp /= leaf.val();
    return file_path(fp.string());
  }

};

bool
operator<(path_vec_t const & pva,
	  path_vec_t const & pvb)
{
  vector<dirent_t> a(pva.dir);
  vector<dirent_t> b(pvb.dir);
  a.push_back(pva.leaf);
  b.push_back(pvb.leaf);
  return lexicographical_compare(a.begin(), a.end(),
				 b.begin(), b.end());
  
}


//==================================================================
// nodes
//==================================================================

struct 
node
{
  map<attr_name, attr_val> attrs;

  bool has_attrs() 
  {
    return !attrs.empty();
  }

  bool has_attr(attr_name const & name) 
  { 
    return attrs.find(name) != attrs.end();
  }

  attr_val const & get_attr(attr_name const & name) 
  { 
    map<attr_name, attr_val>::const_iterator i = attrs.find(name);
    I(i != attrs.end());
    return i->second;
  }

  void set_attr(attr_name const & name,
		attr_val const & val) 
  {
    attrs[name] = val;
  }

  void clear_attr(attr_name const & name) 
  {
    attrs.erase(name);
  }

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
  f->attrs = attrs;
  f->content = content;
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
  vector<dirent_t> prefix;

  dfs_iter(dir_t root)
  {
    if (!root->entries.empty())
      stk.push(make_pair(root, root->entries.begin()));
  }

  bool finished()
  {
    return stk.empty();
  }

  void cwd(dir_t & d)
  {
    I(!stk.empty());
    d = stk.top().first;
  }

  node_t operator*()
  {
    I(!stk.empty());
    return stk.top().second->second;
  }

  void path(path_vec_t & pv) 
  {
    I(!finished());
    pv.dir = prefix;
    leaf(pv.leaf);
  }
  
  void leaf(dirent_t & d)
  {
    I(!finished());
    d = stk.top().second->first;
  }

  void operator++()
  {
    if (stk.empty())
      return;

    node_t ntmp = stk.top().second->second;
    if (is_dir_t(ntmp))
      {
        prefix.push_back(stk.top().second->first);
	dir_t dtmp = downcast_to_dir_t(ntmp);
	stk.push(make_pair(dtmp, dtmp->entries.begin()));
      }
    else
      ++(stk.top().second);

    while (!stk.empty() 
	   && stk.top().second == stk.top().first->entries.end())
      {
	stk.pop();
	if (!prefix.empty())
	  prefix.pop_back();
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
  dirmap_t::const_iterator i = entries.find(p);
  I(i != entries.end());
  return i->second;
}


node_t 
dir_node::shallow_copy() const
{
  dir_t d = dir_t(new dir_node());
  d->attrs = attrs;
  d->entries = entries;
  return d;
}


namespace __gnu_cxx
{
  template <>
  struct hash<node_t>  
  { 
    size_t 
    operator()(node_t const & x) const
    { 
      return reinterpret_cast<size_t>(x.get());
    }
  };

  template <>
  struct hash<node *>  
  { 
    size_t 
    operator()(node * const & x) const
    { 
      return reinterpret_cast<size_t>(x);
    }
  };

  template <>
  struct hash<weak_node_t>  
  { 
    size_t 
    operator()(weak_node_t const & x) const
    { 
      return reinterpret_cast<size_t>(x.lock().get());
    }
  };
}

bool
operator==(weak_node_t const & a,
           weak_node_t const & b)
{
  return a.lock().get() == b.lock().get();
}


//==================================================================
// mfests
//==================================================================


struct 
mfest
{
  dir_t root;

  mfest() : root(new dir_node()) 
  {}
  
  mfest(mfest const & other);
  void reset(mfest const & other);
  void check_sane() const;

  // Informative parts.
  bool file_exists(file_path fp) const;
  bool dir_exists(file_path dp) const;

  // Imperative parts (fault on lookup failures).
  void lookup(path_vec_t const & pth, 
	      dir_t & parent, node_t & leaf);
};


void 
mfest::lookup(path_vec_t const & pth, dir_t & parent, node_t & leaf)
{
  parent = root;
  for (vector<dirent_t>::const_iterator i = pth.dir.begin(); 
       i != pth.dir.end(); ++i)
    {
      parent = downcast_to_dir_t(parent->get_entry(*i));
    }
  leaf = parent->get_entry(pth.leaf);
}


mfest::mfest(mfest const & other)
{
  this->reset(other);
}


void
mfest::reset(mfest const & other)
{
  root = other.root;
  check_sane();
}


void
mfest::check_sane() const
{
  // Check for absence of cycles.
  hash_set<node *> seen;  
  for(bfs_iter i(root); !i.finished(); ++i)
    {
      node *n = (*i).get();
      I(seen.find(n) == seen.end());
      seen.insert(n);
    }
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


bool 
operator==(mfest const & ma,
           mfest const & mb)
{
  // This function could use bfs_iter, but we want it to go *very fast*, so
  // it actually short-circuits any time it sees identical node pointers
  // rather than structurally comparing their subtrees.

  if (ma.root.get() == mb.root.get())
    return true;

  deque<node_t> qa, qb;
  qa.push_back(ma.root);
  qb.push_back(mb.root);

  while (! (qa.empty() || qb.empty()))
    {

      node_t a = qa.front();
      node_t b = qb.front();

      qa.pop_front();
      qb.pop_front();

      // Short-circuit comparisons when we actually
      // have the same pointers here.

      if (a.get() == b.get())
        continue;

      if (a->attrs != b->attrs)
        return false;

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
          
          for (dirmap_t::const_iterator 
                 i = da->entries.begin(),
                 j = db->entries.begin();
                 i != da->entries.end(); ++i, ++j)
            {
              I(j != db->entries.end());
              if (i->first != j->first)
                return false;
              qa.push_back(i->second);
              qb.push_back(j->second);
            }
        }
      else
        return false;
    }

  if (! (qa.empty() && qb.empty()))
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

  void delete_dir(file_path const & pth);
  void delete_file(file_path const & pth,
                   file_id const & ident);
  void rename_node(file_path const & src, 
		   file_path const & dst);

  void add_dir(file_path const & dp);
  void add_file(file_path const & fp,
                file_id const & ident);

  void apply_delta(file_path const & path, 
		   file_id const & src, 
		   file_id const & dst);

  void set_attr(file_path const & path, 
		attr_name const & name, 
		attr_val const & val);

  void clear_attr(file_path const & path, 
		  attr_name const & name);

  virtual void finalize_renames() {}

  // This part is just a lower-level form of the API above, to avoid
  // composing or parsing file_paths in their string-y form.

  virtual void delete_file(path_vec_t const & path,
                           file_id const & ident) = 0;
  virtual void delete_dir(path_vec_t const & path) = 0;

  virtual void rename_node(path_vec_t const & src, 
			   path_vec_t const & dst) = 0;
  
  virtual void add_dir(path_vec_t const & dp) = 0;
  virtual void add_file(path_vec_t const & fp,
                        file_id const & ident) = 0;

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
change_consumer::delete_dir(file_path const & dp)
{
  L(F("delete_dir('%s')\n") % dp);
  this->delete_dir(path_vec_t::from_file_path(dp));
}

void 
change_consumer::delete_file(file_path const & fp,
                             file_id const & ident)
{
  I(!ident.inner()().empty());
  L(F("delete_file('%s', '%s')\n") % fp % ident);
  this->delete_file(path_vec_t::from_file_path(fp), ident);
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
change_consumer::add_file(file_path const & fp,
                          file_id const & ident)
{
  L(F("add_file('%s', '%s')\n") % fp % ident);
  I(!ident.inner()().empty());
  this->add_file(path_vec_t::from_file_path(fp), ident);
}


void 
change_consumer::apply_delta(file_path const & path, 
			     file_id const & src, 
			     file_id const & dst)
{
  L(F("apply_delta('%s', [%s], [%s])\n") % path % src % dst);
  I(!src.inner()().empty());
  I(!dst.inner()().empty());
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

  virtual node_t detach(path_vec_t const & path) = 0;
  virtual void attach(path_vec_t const & path, node_t id) = 0;

  // Renames are directed into a temporary buffer, which 
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
  vector<pair<path_vec_t, node_t> > tmp;
  
  for (vector< pair<path_vec_t, path_vec_t> >::reverse_iterator i = pending_renames.rbegin(); 
       i != pending_renames.rend(); ++i)
    {
      tmp.push_back(make_pair(i->second, detach(i->first)));
    }

  for (vector< pair<path_vec_t, node_t> >::const_iterator i = tmp.begin(); 
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

  hash_set<node_t> incubator;
  mfest old_mfest;
  mfest new_mfest;
  hash_set<node_t> graveyard;

  // Fields and accessors related to the "inverse"
  // child->{parent,name} maps.

  hash_map<weak_node_t, pair<weak_node_t, dirent_t> > old_parents;
  hash_map<weak_node_t, pair<weak_node_t, dirent_t> > new_parents;

  void get_old_path(node_t const & leaf, path_vec_t & pth) const;
  void get_new_path(node_t const & leaf, path_vec_t & pth) const;

  void set_old_parent(node_t const & child, 
		      dir_t const & parent, 
		      dirent_t name_in_parent);
  void set_new_parent(node_t const & child, 
		      dir_t const & parent, 
		      dirent_t name_in_parent);
  void get_old_parent(node_t const & child, 
		      dir_t & parent, 
		      dirent_t & name_in_parent) const;
  void get_new_parent(node_t const & child, 
		      dir_t & parent, 
		      dirent_t & name_in_parent) const;

  // Fields and accessors related to the new <-> old bijection.

  hash_map<weak_node_t, weak_node_t> old_to_new;
  hash_map<weak_node_t, weak_node_t> new_to_old;

  void update_old_new_mapping(weak_node_t old, 
                              weak_node_t modified_new);
  void get_corresponding_new_weak_node(weak_node_t old_or_unborn, 
                                       weak_node_t & new_or_killed) const;
  void get_corresponding_old_weak_node(weak_node_t new_or_killed, 
                                       weak_node_t & old_or_unborn) const;
  void get_corresponding_new_node(weak_node_t old_or_unborn, 
                                  node_t & new_or_killed) const;
  void get_corresponding_old_node(weak_node_t new_or_killed, 
                                  node_t & old_or_unborn) const;

  // Accessors related to copy-on-write.
  node_t unique_entry_in_new_dir(dir_t new_dir, dirent_t entry);
  node_t get_writable_node(path_vec_t const & pth);
  dir_t get_writable_dir_containing(path_vec_t const & pth);
  dir_t get_writable_dir(path_vec_t const & pth);
  file_t get_writable_file(path_vec_t const & pth);

  cset()
  {
  }

  cset(mfest const & base)
  {
    this->reset(base);
  }

  virtual ~cset() {}

  void reset(mfest const & m);
  void check_sane() const;

  bool is_unborn(node_t const & n) const;
  bool is_live(node_t const & n) const;
  bool is_killed(node_t const & n) const;

  void replay_changes(change_consumer & cc) const;
  void replay_inverse_changes(change_consumer & cc) const;

  virtual void finalize_renames();

  virtual void delete_dir(path_vec_t const & dp);
  virtual void delete_file(path_vec_t const & dp,
                           file_id const & ident);

  virtual node_t detach(path_vec_t const & path);
  virtual void attach(path_vec_t const & path, node_t id);

  virtual void add_dir(path_vec_t const & dp);
  virtual void add_file(path_vec_t const & fp,
                        file_id const & ident);

  virtual void apply_delta(path_vec_t const & path, 
			   file_id const & src, 
			   file_id const & dst);

  virtual void set_attr(path_vec_t const & path, 
			attr_name const & name, 
			attr_val const & val);

  virtual void clear_attr(path_vec_t const & path, 
			  attr_name const & name);

};


void 
cset::reset(mfest const & m)
{
  incubator.clear();
  graveyard.clear();

  new_to_old.clear();
  old_to_new.clear();

  old_mfest.reset(m);
  new_mfest.reset(m);

  old_parents.clear();
  new_parents.clear();

  for (dfs_iter i(old_mfest.root); !i.finished(); ++i)
    {
      dir_t parent;
      dirent_t entry;
      i.cwd(parent);
      i.leaf(entry);
      set_old_parent(*i, parent, entry);
    }
}

bool 
cset::is_unborn(node_t const & n) const
{
  return incubator.find(n) != incubator.end();
}

bool 
cset::is_live(node_t const & n) const
{
  return !(is_unborn(n) || is_killed(n));
}

bool 
cset::is_killed(node_t const & n) const
{
  return graveyard.find(n) != graveyard.end();
}

static void
confirm_weak_maps_agree(hash_map<weak_node_t, weak_node_t> const & a,
                        hash_map<weak_node_t, weak_node_t> const & b)
{
  for (hash_map<weak_node_t, weak_node_t>::const_iterator i = a.begin();
       i != a.end(); ++i)
    {
      I(!i->first.expired());
      I(!i->second.expired());
      hash_map<weak_node_t, weak_node_t>::const_iterator j = b.find(i->second);
      I(j != b.end());      
      I(!j->first.expired());
      I(!j->second.expired());
      I(j->first.lock().get() == i->second.lock().get());
      I(j->second.lock().get() == i->first.lock().get());
    }
}


void 
cset::check_sane() const
{
  old_mfest.check_sane();
  new_mfest.check_sane();
  
  confirm_weak_maps_agree(new_to_old, old_to_new);
  confirm_weak_maps_agree(old_to_new, new_to_old);

  I(is_live(old_mfest.root));
  I(is_live(new_mfest.root));

  for (dfs_iter i(old_mfest.root); !i.finished(); ++i)
    {
      I(is_live(*i));
      dir_t parent, indexed_parent;
      dirent_t entry, indexed_entry;
      i.cwd(parent);
      i.leaf(entry);
      get_old_parent(*i, indexed_parent, indexed_entry);
      I(entry == indexed_entry);
      I(parent.get() == indexed_parent.get());
    }

  for (dfs_iter i(new_mfest.root); !i.finished(); ++i)
    {
      I(is_live(*i));
      dir_t parent, indexed_parent;
      dirent_t entry, indexed_entry;
      i.cwd(parent);
      i.leaf(entry);
      get_new_parent(*i, indexed_parent, indexed_entry);
      I(entry == indexed_entry);
      I(parent.get() == indexed_parent.get());
    }

  for (hash_set<node_t>::const_iterator i = graveyard.begin(); 
       i != graveyard.end(); ++i)
    {
      I(!is_unborn(*i));
      I(new_to_old.find(weak_node_t(*i)) != new_to_old.end());
    }

  for (hash_set<node_t>::const_iterator i = incubator.begin(); 
       i != incubator.end(); ++i)
    {
      I(!is_killed(*i));
      I(old_to_new.find(weak_node_t(*i)) != old_to_new.end());
    }  
}

void 
cset::get_old_path(node_t const & leaf, path_vec_t & pth) const
{
  pth.dir.clear();
  dir_t parent;  
  get_old_parent(leaf, parent, pth.leaf);

  for (node_t child = parent; 
       child.get() != old_mfest.root.get();
       child = parent)
    {
      dirent_t child_entry;
      get_old_parent(child, parent, child_entry);
      pth.dir.push_back(child_entry);
    }

  reverse(pth.dir.begin(), pth.dir.end());  
}

void 
cset::get_new_path(node_t const & leaf, path_vec_t & pth) const
{
  pth.dir.clear();
  dir_t parent;  
  get_new_parent(leaf, parent, pth.leaf);

  for (node_t child = parent; 
       child.get() != new_mfest.root.get();
       child = parent)
    {
      dirent_t child_entry;
      get_new_parent(child, parent, child_entry);
      pth.dir.push_back(child_entry);
    }

  reverse(pth.dir.begin(), pth.dir.end());  
}

void 
cset::set_old_parent(node_t const & child, 
		     dir_t const & parent, 
		     dirent_t name_in_parent)
{
  // L(F("set_old_parent(child=0x%x, parent=0x%x, name='%s')\n")
  //     % child.get()
  //     % parent.get()
  //     % name_in_parent);

  old_parents[weak_node_t(child)] = make_pair(weak_node_t(parent),
					      name_in_parent);
}

void 
cset::set_new_parent(node_t const & child, 
		     dir_t const & parent, 
		     dirent_t name_in_parent)
{
  // L(F("set_new_parent(child=0x%x, parent=0x%x, name='%s')\n")
  //     % child.get()
  //     % parent.get()
  //     % name_in_parent);

  new_parents[weak_node_t(child)] = make_pair(weak_node_t(parent),
					      name_in_parent);
}

void 
cset::get_old_parent(node_t const & child, 
		     dir_t & parent, 
		     dirent_t & name_in_parent) const
{
  // L(F("get_old_parent(0x%x)\n") % child.get());
 
 hash_map<weak_node_t, pair<weak_node_t, dirent_t> >::const_iterator i = 
    old_parents.find(weak_node_t(child));

  I(i != old_parents.end());
  I(!i->second.first.expired());
  parent = downcast_to_dir_t(i->second.first.lock());
  name_in_parent = i->second.second;

  // L(F("get_old_parent(0x%x) -> parent=0x%x name='%s' \n") 
  //     % child.get()
  //     % parent.get()
  //     % name_in_parent);
}

void 
cset::get_new_parent(node_t const & child, 
		     dir_t & parent, 
		     dirent_t & name_in_parent) const
{
  // L(F("get_new_parent(0x%x)\n") % child.get());

  hash_map<weak_node_t, pair<weak_node_t, dirent_t> >::const_iterator i = 
    new_parents.find(weak_node_t(child));


  if (i != new_parents.end())
    {
      I(!i->second.first.expired());
      parent = downcast_to_dir_t(i->second.first.lock());
      name_in_parent = i->second.second;
    }
  else
    get_old_parent(child, parent, name_in_parent);

  // L(F("get_new_parent(0x%x) -> parent=0x%x name='%s' \n") 
  //     % child.get()
  //     % parent.get()
  //     % name_in_parent);
}


void
cset::update_old_new_mapping(weak_node_t old, weak_node_t modified_new)
{
  // We must call this when we might have changed the "new" node associated
  // with an "old" (or unborn) node; namely when we've done a
  // copy-for-write.

  // L(F("re-associating old 0x%x -> new 0x%x\n") 
  //     % old.lock().get() 
  //     % modified_new.lock().get());

  weak_node_t existing_new;
  get_corresponding_new_weak_node(old, existing_new);
  new_to_old.erase(existing_new);
  old_to_new[old] = modified_new;
  new_to_old[modified_new] = old;
}


node_t
cset::unique_entry_in_new_dir(dir_t new_dir, dirent_t entry)
{
  // NB: We do not use "get_foo_entry" here because that reproduces the
  // shared_ptr, and we're using "unique" to check that the shared_ptr
  // is non-duplicated.
  
  dirmap_t::const_iterator e = new_dir->entries.find(entry);
  I(e != new_dir->entries.end());
  node_t result;
  if (e->second.unique())
    result = e->second;
  else
    {
      weak_node_t old_weak_node;
      get_corresponding_old_weak_node(e->second, old_weak_node);
      result = e->second->shallow_copy();
      I(result.unique());
      update_old_new_mapping(old_weak_node, weak_node_t(result));
      new_dir->entries[entry] = result;
      set_new_parent(result, new_dir, entry);
    }

  // The node should now have exactly 2 references: 
  // 'new_dir->entries[entry]' and 'result'.

  I(result.use_count() == 2);
  return result;  
}
                                  

node_t
cset::get_writable_node(path_vec_t const & pth)
{
  dir_t d = get_writable_dir_containing(pth);
  return unique_entry_in_new_dir(d, pth.leaf);
}


dir_t
cset::get_writable_dir_containing(path_vec_t const & pth)
{
  // We want to return a dir_t which:
  //
  //   - is present at the provided path, in the tree rooted at
  //     new_mfest.root
  //
  //   - is shared_ptr::unique
  //
  //   - has only shared_ptr::unique parents all the way up to
  //     new_mfest.root
  //
  // We can do this "top down": we simply have to duplicate non-unique
  // nodes as we go. We know that in any existing unique dir D we're safe
  // to replace any existing slot S with a clone because D is itself
  // unique; nobody else is going to see us damaging it.

  if (!new_mfest.root.unique())
    new_mfest.root = downcast_to_dir_t(new_mfest.root->shallow_copy());

  I(new_mfest.root.unique());
  
  dir_t d = new_mfest.root;
  for (vector<dirent_t>::const_iterator i = pth.dir.begin();
       i != pth.dir.end(); ++i)
    {
      d = downcast_to_dir_t(unique_entry_in_new_dir(d,*i));
    }
  return d;
}


dir_t
cset::get_writable_dir(path_vec_t const & pth)
{
  return downcast_to_dir_t(get_writable_node(pth));
}


file_t
cset::get_writable_file(path_vec_t const & pth)
{
  return downcast_to_file_t(get_writable_node(pth));
}

void
cset::get_corresponding_new_weak_node(weak_node_t old_or_unborn, 
                                      weak_node_t & n) const
{

  // L(F("get_corresponding_new_weak_node(0x%x)\n")
  //     % old_or_unborn.lock().get());

  hash_map<weak_node_t, weak_node_t>::const_iterator i 
    = old_to_new.find(old_or_unborn);

  if (i == old_to_new.end())
    n = old_or_unborn;
  else
    n = i->second;

  // L(F("get_corresponding_new_weak_node(0x%x) -> 0x%x\n")
  //     % old_or_unborn.lock().get()
  //     % n.lock().get());

  I(!n.expired());
}


void
cset::get_corresponding_old_weak_node(weak_node_t new_or_killed, 
                                      weak_node_t & o) const
{
  // L(F("get_corresponding_old_weak_node(0x%x)\n")
  //     % new_or_killed.lock().get());

  hash_map<weak_node_t, weak_node_t>::const_iterator i 
    = new_to_old.find(new_or_killed);

  if (i == old_to_new.end())
    o = new_or_killed;
  else
    o = i->second;

  // L(F("get_corresponding_old_weak_node(0x%x) -> 0x%x\n")
  //     % new_or_killed.lock().get()
  //     % o.lock().get());

  I(!o.expired());
}


void
cset::get_corresponding_new_node(weak_node_t old_or_unborn, 
                                 node_t & n) const
{
  weak_node_t tmp;
  get_corresponding_new_weak_node(old_or_unborn, tmp);
  n = tmp.lock();
}


void
cset::get_corresponding_old_node(weak_node_t new_or_killed, 
                                 node_t & n) const
{
  weak_node_t tmp;
  get_corresponding_old_weak_node(new_or_killed, tmp);
  n = tmp.lock();
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
cset::delete_dir(path_vec_t const & dp)
{
  node_t n = this->detach(dp);
  I(is_dir_t(n));

  // NB: we do not permit deleting a non-empty directory; the
  // semantics are too complex when coupled with renames (which
  // might escape the deleted dir). If you are a client and you
  // want to delete a dir, you have to delete everything inside
  // the dir first.
  I(downcast_to_dir_t(n)->entries.empty());

  graveyard.insert(n);

  if (global_sanity.debug)
    {
      check_sane();
    }  
}

void 
cset::delete_file(path_vec_t const & dp,
                  file_id const & ident)
{
  I(!ident.inner()().empty());
  node_t n = this->detach(dp);
  I(is_file_t(n));
  I(downcast_to_file_t(n)->content == ident);
  graveyard.insert(n);

  if (global_sanity.debug)
    {
      check_sane();
    }  
}


node_t 
cset::detach(path_vec_t const & path)
{
  dir_t old_parent;
  node_t old_node, new_node;
  path_vec_t new_path;

  old_mfest.lookup(path, old_parent, old_node);
  get_corresponding_new_node(old_node, new_node);
  get_new_path(new_node, new_path);

  dir_t new_parent = get_writable_dir_containing(new_path);
  new_node = unique_entry_in_new_dir(new_parent, new_path.leaf);
  new_parent->drop_child(new_path.leaf);
  new_parents.erase(new_node);
  return new_node;

  // NB: it is important *not* to check sanity here, as we might have
  // entered a transient state where detached grandchildren are 
  // not pointing to the graveyard because they're about to be
  // re-attached.
}


void 
cset::attach(path_vec_t const & path, node_t n)
{
  dir_t new_dir = get_writable_dir_containing(path);
  new_dir->add_child(path.leaf, n);
  set_new_parent(n, new_dir, path.leaf);

  // NB: it is important *not* to check sanity here, as we might still
  // be in a transient state where detached grandchildren are not
  // pointing to the graveyard because they're about to be
  // re-attached.
}


void 
cset::add_dir(path_vec_t const & dp)
{
  dir_t old_dir(new dir_node());
  incubator.insert(old_dir);

  dir_t new_dir(downcast_to_dir_t(old_dir->shallow_copy()));
  dir_t new_parent = get_writable_dir_containing(dp);
  new_parent->add_child(dp.leaf, new_dir);
  set_new_parent(new_dir, new_parent, dp.leaf);

  old_to_new[weak_node_t(old_dir)] = weak_node_t(new_dir);
  new_to_old[weak_node_t(new_dir)] = weak_node_t(old_dir);

  // L(F("added dir mapping old 0x%x -> new 0x%x\n") 
  //     % old_dir.get() 
  //     % new_dir.get());

  if (global_sanity.debug)
    {
      check_sane();
    }
}


void 
cset::add_file(path_vec_t const & fp,
               file_id const & ident)
{
  I(!ident.inner()().empty());
  file_t old_file(new file_node());
  incubator.insert(old_file);

  file_t new_file(downcast_to_file_t(old_file->shallow_copy()));
  new_file->content = ident;
  dir_t new_parent = get_writable_dir_containing(fp);
  new_parent->add_child(fp.leaf, new_file);
  set_new_parent(new_file, new_parent, fp.leaf);

  old_to_new[weak_node_t(old_file)] = weak_node_t(new_file);
  new_to_old[weak_node_t(new_file)] = weak_node_t(old_file);

  //   L(F("added file mapping old 0x%x -> new 0x%x\n") 
  //     % old_file.get() 
  //     % new_file.get());

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
  node_t old_node;
  file_t old_file, new_file;
  
  I(!s.inner()().empty());
  I(!d.inner()().empty());

  new_file = get_writable_file(path);
  get_corresponding_old_node(weak_node_t(new_file), old_node);
  old_file = downcast_to_file_t(old_node);

  I(old_file->content == s);
  new_file->content = d;

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
  get_writable_node(path)->set_attr(name, val);

  if (global_sanity.debug)
    {
      check_sane();
    }
}


void 
cset::clear_attr(path_vec_t const & path, 
		 string const & name)
{
  get_writable_node(path)->clear_attr(name);

  if (global_sanity.debug)
    {
      check_sane();
    }
}


struct replay_record
{

  struct live_paths
  {
    path_vec_t src;
    path_vec_t dst;
  };

  struct changed_attr
  {
    live_paths paths;
    attr_name name;
    attr_val src_val;
    attr_val dst_val;
  };

  struct applied_delta
  {
    live_paths paths;
    file_id src_id;
    file_id dst_id;
  };

  struct path_with_content
  {
    path_vec_t path;
    file_id content;
    bool operator<(path_with_content const & other) const
    {
      return path < other.path;
    }
  };

  set<path_with_content> files_deleted;
  set<path_vec_t> dirs_deleted;
  vector<live_paths> nodes_renamed;
  set<path_vec_t> dirs_added;
  set<path_with_content> files_added;
  vector<applied_delta> deltas_applied;
  vector<changed_attr> attrs_changed;

  // In the forward direction, renames are sorted by destination
  // space; so in the inverse record, renames should be sorted by
  // source space.
  struct rename_inverse_sorter
  {
    bool operator()(live_paths const & a,
		    live_paths const & b)
    {
      return a.src < b.src;
    }
  };

  // The same is true of deltas.
  struct delta_inverse_sorter
  {
    bool operator()(applied_delta const & a,
		    applied_delta const & b)
    {
      return a.paths.src < b.paths.src;
    }
  };

  // The same is true of attributes.
  struct attr_inverse_sorter
  {
    bool operator()(changed_attr const & a,
		    changed_attr const & b)
    {
      return a.paths.src < b.paths.src;
    }
  };

  void sort_for_inverse_direction()
  {
    sort(nodes_renamed.begin(), nodes_renamed.end(), 
	 rename_inverse_sorter());
    sort(deltas_applied.begin(), deltas_applied.end(),
	 delta_inverse_sorter());
    sort(attrs_changed.begin(), attrs_changed.end(),
	 attr_inverse_sorter());
  }
  
};


static void
play_back_replay_record(replay_record const & rr,
			change_consumer & cc)
{
  for (set<replay_record::path_with_content>::reverse_iterator i 
         = rr.files_deleted.rbegin(); i != rr.files_deleted.rend(); ++i)
    cc.delete_file(i->path, i->content);
  
  for (set<path_vec_t>::reverse_iterator i = rr.dirs_deleted.rbegin();
       i != rr.dirs_deleted.rend(); ++i)
    cc.delete_dir(*i);


  for (vector<replay_record::live_paths>::const_iterator i 
	 = rr.nodes_renamed.begin(); i != rr.nodes_renamed.end(); ++i)
    cc.rename_node(i->src, i->dst);

  cc.finalize_renames();


  for (set<path_vec_t>::const_iterator i = rr.dirs_added.begin();
       i != rr.dirs_added.end(); ++i)
    cc.add_dir(*i);

  for (set<replay_record::path_with_content>::const_iterator i 
         = rr.files_added.begin(); i != rr.files_added.end(); ++i)
    cc.add_file(i->path, i->content);

  for (vector<replay_record::applied_delta>::const_iterator i 
	 = rr.deltas_applied.begin(); i != rr.deltas_applied.end(); ++i)
    cc.apply_delta(i->paths.dst, i->src_id, i->dst_id);

  // attr pass #1: emit all the cleared attrs
  for (vector<replay_record::changed_attr>::const_iterator i = rr.attrs_changed.begin();
       i != rr.attrs_changed.end(); ++i)
    {
      if (i->dst_val.empty())
	cc.clear_attr(i->paths.dst, i->name);
    }

  // attr pass #2: emit all the set attrs
  for (vector<replay_record::changed_attr>::const_iterator i = rr.attrs_changed.begin();
       i != rr.attrs_changed.end(); ++i)
    {
      if (!i->dst_val.empty())
	cc.set_attr(i->paths.dst, i->name, i->dst_val);
    }
}


static void
play_back_replay_record_inverse(replay_record const & rr,
				change_consumer & cc)
{

  for (set<replay_record::path_with_content>::reverse_iterator i 
         = rr.files_added.rbegin(); i != rr.files_added.rend(); ++i)
    cc.delete_file(i->path, i->content);

  for (set<path_vec_t>::reverse_iterator i = rr.dirs_added.rbegin();
       i != rr.dirs_added.rend(); ++i)
    cc.delete_dir(*i);


  for (vector<replay_record::live_paths>::const_iterator i 
	 = rr.nodes_renamed.begin(); i != rr.nodes_renamed.end(); ++i)
    cc.rename_node(i->dst, i->src);

  cc.finalize_renames();


  for (set<path_vec_t>::const_iterator i = rr.dirs_deleted.begin();
       i != rr.dirs_deleted.end(); ++i)
    cc.add_dir(*i);

  for (set<replay_record::path_with_content>::const_iterator i 
         = rr.files_deleted.begin(); i != rr.files_deleted.end(); ++i)
    cc.add_file(i->path, i->content);
  

  for (vector<replay_record::applied_delta>::const_iterator i 
	 = rr.deltas_applied.begin(); i != rr.deltas_applied.end(); ++i)
    cc.apply_delta(i->paths.src, i->dst_id, i->src_id);

  // attr pass #1: emit all the cleared attrs
  for (vector<replay_record::changed_attr>::const_iterator i = rr.attrs_changed.begin();
       i != rr.attrs_changed.end(); ++i)
    {
      if (i->src_val.empty())
	cc.clear_attr(i->paths.src, i->name);
    }

  // attr pass #2: emit all the set attrs
  for (vector<replay_record::changed_attr>::const_iterator i = rr.attrs_changed.begin();
       i != rr.attrs_changed.end(); ++i)
    {
      if (!i->src_val.empty())
	cc.set_attr(i->paths.src, i->name, i->src_val);
    }
}


static void
build_replay_record(cset const & cs,
		    replay_record & rr)
{
  // we do two passes accumulating nodes: the first pass accumulates
  // nodes in the topological order they appear in the src map, the
  // second accumulates nodes in the toplogical order they appear in
  // the dst map. in both cases we append any interesting nodes to
  // vectors which we then replay as blocks of related changes.
  
  for (dfs_iter i(cs.old_mfest.root); !i.finished(); ++i)
    {
      // in this pass we accumulate nodes_deleted. the "self" node is a
      // member of the "src" directory tree.
      node_t self = *i, other;
      cs.is_live(self);

      cs.get_corresponding_new_node(self, other);
      
      if (cs.is_killed(other))
	{
	  path_vec_t pth;
	  cs.get_old_path(self, pth);

	  if (is_dir_t(self))
	    rr.dirs_deleted.insert(pth);
	  else
            {
              replay_record::path_with_content pc;
              pc.path = pth;
              pc.content = downcast_to_file_t(self)->content;
              rr.files_deleted.insert(pc);
            }
	}
    }


  for (dfs_iter i(cs.new_mfest.root); !i.finished(); ++i)
    {
      // in this pass we accumulate nodes_renamed, files_added, dirs_added,
      // deltas_applied, attrs_cleared, and attrs_set.
      //
      // the "self" node is a member of the "dst" directory tree.
      node_t self = *i, other;

      I(cs.is_live(self));

      cs.get_corresponding_old_node(self, other);

      if (cs.is_unborn(other))
	{
	  path_vec_t pth;
	  cs.get_new_path(self, pth);
	  if (is_dir_t(self))
	    {
	      I(is_dir_t(other));
	      rr.dirs_added.insert(pth);
	    }
	  else
	    {
	      I(is_file_t(self));
	      I(is_file_t(other));
              replay_record::path_with_content pc;
              pc.path = pth;
              pc.content = downcast_to_file_t(self)->content;
	      rr.files_added.insert(pc);
	    }
	} 
      else if (cs.is_live(other))
	{
	  dirent_t self_entry, other_entry;
	  dir_t self_parent, other_parent;
	  cs.get_old_parent(other, other_parent, other_entry);
	  cs.get_new_parent(self, self_parent, self_entry);

	  if ((other_entry != self_entry)
	      || (other_parent.get() != self_parent.get()))
	    {
	      replay_record::live_paths paths;
	      cs.get_old_path(other, paths.src);
	      cs.get_new_path(self, paths.dst);
	      rr.nodes_renamed.push_back(paths);
	    }
	}

      // content deltas
      if (is_file_t(self)
          && cs.is_live(self)
          && cs.is_live(other))
	{
	  I(is_file_t(other));
	  file_t f_other = downcast_to_file_t(other);
	  file_t f_self = downcast_to_file_t(self);
	  if (!(f_other->content == f_self->content))
	    {
	      replay_record::applied_delta del;
	      cs.get_old_path(other, del.paths.src);
	      cs.get_new_path(self, del.paths.dst);
	      del.src_id = f_other->content;
	      del.dst_id = f_self->content;
              I(!del.src_id.inner()().empty());
              I(!del.dst_id.inner()().empty());
	      rr.deltas_applied.push_back(del);
	    }
	}
      
      // attrs which were changed
      if (self->attrs != other->attrs)
	{
	  replay_record::live_paths pths;
	  cs.get_old_path(other, pths.src);
	  cs.get_new_path(self, pths.dst);

	  for (map<attr_name, attr_val>::const_iterator a = self->attrs.begin();
	       a != self->attrs.end(); ++a)
	    {
	      attr_val oldval;

	      map<attr_name, attr_val>::const_iterator b = other->attrs.find(a->first);
	      if (b != other->attrs.end())
		oldval = b->second;

	      if (oldval != a->second)
		{
		  replay_record::changed_attr ab;
		  ab.paths = pths;
		  ab.name = a->first;
		  ab.src_val = oldval;
		  ab.dst_val = a->second;
		  rr.attrs_changed.push_back(ab);
		}
	    }

	  for (map<attr_name, attr_val>::const_iterator b = other->attrs.begin();
	       b != other->attrs.end(); ++b)
	    {
	      attr_val newval;

	      map<attr_name, attr_val>::const_iterator a = self->attrs.find(b->first);
	      if (a != other->attrs.end())
		newval = a->second;

	      if (newval != b->second)
		{
		  replay_record::changed_attr ab;
		  ab.paths = pths;
		  ab.name = b->first;
		  ab.src_val = b->second;
		  ab.dst_val = newval;
		  rr.attrs_changed.push_back(ab);
		}
	    }
	}
    }
}


void 
cset::replay_changes(change_consumer & cc) const
{
  replay_record rr;
  build_replay_record(*this, rr);
  play_back_replay_record(rr, cc);
}


void 
cset::replay_inverse_changes(change_consumer & cc) const
{
  replay_record rr;
  build_replay_record(*this, rr);
  rr.sort_for_inverse_direction();
  play_back_replay_record_inverse(rr, cc);
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
    std::string const delete_file("delete_file");
    std::string const delete_dir("delete_dir");
    std::string const rename_node("rename");
    std::string const add_file("add_file");
    std::string const add_dir("add_dir");
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
      if (parser.symp(syms::delete_file)) 
        { 
          parser.sym();
          parser.str(t1);
	  parser.esym(syms::content);
	  parser.hex(t2);
          cc.delete_file(file_path(t1),
                         file_id(t2));
        }
      else if (parser.symp(syms::delete_dir)) 
        { 
          parser.sym();
          parser.str(t1);
          cc.delete_dir(file_path(t1));
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
	  parser.esym(syms::content);
	  parser.hex(t2);
          cc.add_file(file_path(t1),
                      file_id(t2));
        }
      else if (parser.symp(syms::add_dir)) 
        { 
          parser.sym();
          parser.str(t1);
          cc.add_dir(file_path(t1));
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
  virtual void delete_file(path_vec_t const & fp,
                           file_id const & content);
  virtual void delete_dir(path_vec_t const & fp);
  virtual void rename_node(path_vec_t const & src, 
			   path_vec_t const & dst);
  virtual void add_dir(path_vec_t const & dp);
  virtual void add_file(path_vec_t const & fp,
                        file_id const & ident);
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
cset_printer::delete_file(path_vec_t const & fp,
                          file_id const & ident)
{
  basic_io::stanza st;
  I(!ident.inner()().empty());
  st.push_str_pair(syms::delete_file, fp.to_file_path()());
  st.push_hex_pair(syms::content, ident.inner()());
  printer.print_stanza(st);
}


void 
cset_printer::delete_dir(path_vec_t const & dp)
{
  basic_io::stanza st;
  st.push_str_pair(syms::delete_dir, dp.to_file_path()());
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
cset_printer::add_file(path_vec_t const & fp,
                       file_id const & ident)
{
  basic_io::stanza st;
  I(!ident.inner()().empty());
  st.push_str_pair(syms::add_file, fp.to_file_path()());
  st.push_hex_pair(syms::content, ident.inner()());
  printer.print_stanza(st);
}


void 
cset_printer::apply_delta(path_vec_t const & path, 
                          file_id const & src, 
                          file_id const & dst)
{
  basic_io::stanza st;
  I(!src.inner()().empty());
  I(!dst.inner()().empty());
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
	  cs.add_file(file_path(pth), file_id(ident));
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
      i.path(pv);

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
      for (map<attr_name, attr_val>::const_iterator j = curr->attrs.begin();
	   j != curr->attrs.end(); ++j)
	{
	  I(!j->second.empty());
	  // L(F("printing attr %s : %s = %s\n") % fp % j->first % j->second);
	  st.push_str_triple(syms::attr, j->first, j->second);
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

/* 

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
        if ((*i)->has_parent())
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
    hash_set<node_t, cset::hash> parents;
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

*/


static void
spin_mfest(mfest const & m)
{
  data tmp;
  mfest m1, m2;
  m1.reset(m);
  for (unsigned i = 0; i < 5; ++i)
    {
      L(F("spinning mfest (pass %d)\n") % i);
      write_mfest(m1, tmp);
      L(F("wrote mfest: [[%s]]\n") % tmp);
      read_mfest(tmp, m2);
      BOOST_CHECK(m1 == m2);
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

file_id null_file_id;

static void 
basic_cset_test()
{
  try
    {

      cset c;
      change_consumer & cs = c; 
      cs.add_dir(file_path("usr"));
      cs.add_dir(file_path("usr/bin"));
      cs.add_file(file_path("usr/bin/cat"),
                  file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));

      cs.add_dir(file_path("usr/local"));
      cs.add_dir(file_path("usr/local/bin"));
      cs.add_file(file_path("usr/local/bin/dog"),
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



void 
add_cset_tests(test_suite * suite)
{
  I(suite);
  //suite->add(BOOST_TEST_CASE(&automaton_cset_test));
  suite->add(BOOST_TEST_CASE(&basic_cset_test));
}


#endif // BUILD_UNIT_TESTS
