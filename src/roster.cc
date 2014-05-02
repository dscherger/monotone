// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include <set>
#include "vector.hh"
#include <sstream>

#include "basic_io.hh"
#include "cset.hh"
#include "database.hh"
#include "platform-wrapped.hh"
#include "roster.hh"
#include "revision.hh"
#include "vocab.hh"
#include "transforms.hh"
#include "simplestring_xform.hh"
#include "file_io.hh"
#include "parallel_iter.hh"
#include "restrictions.hh"
#include "safe_map.hh"
#include "ui.hh"
#include "vocab_cast.hh"

using std::inserter;
using std::make_pair;
using std::map;
using std::ostringstream;
using std::pair;
using std::reverse;
using std::set;
using std::set_union;
using std::stack;
using std::string;
using std::to_string;
using std::vector;

///////////////////////////////////////////////////////////////////

namespace
{
  namespace syms
  {
    symbol const birth("birth");
    symbol const dormant_attr("dormant_attr");
    symbol const ident("ident");

    symbol const path_mark("path_mark");
    symbol const attr_mark("attr_mark");
  }
}

template <> void
dump(attr_map_t const & val, string & out)
{
  ostringstream oss;
  for (attr_map_t::const_iterator i = val.begin(); i != val.end(); ++i)
    oss << "attr key: '" << i->first << "'\n"
        << "  status: " << (i->second.first ? "live" : "dead") << '\n'
        << "   value: '" << i->second.second << "'\n";
  out = oss.str();
}

template <> void
dump(set<revision_id> const & revids, string & out)
{
  out.clear();
  bool first = true;
  for (set<revision_id>::const_iterator i = revids.begin();
       i != revids.end(); ++i)
    {
      if (!first)
        out += ", ";
      first = false;
      out += encode_hexenc(i->inner()(), i->inner().made_from);
    }
}

template <> void
dump(marking_t const & marking, string & out)
{
  ostringstream oss;
  string tmp;
  oss << "birth_revision: " << marking->birth_revision << '\n';
  dump(marking->parent_name, tmp);
  oss << "parent_name: " << tmp << '\n';
  dump(marking->file_content, tmp);
  oss << "file_content: " << tmp << '\n';
  oss << "attrs (number: " << marking->attrs.size() << "):\n";
  for (map<attr_key, set<revision_id> >::const_iterator
         i = marking->attrs.begin(); i != marking->attrs.end(); ++i)
    {
      dump(i->second, tmp);
      oss << "  " << i->first << ": " << tmp << '\n';
    }
  out = oss.str();
}

template <> void
dump(marking_map const & markings, string & out)
{
  ostringstream oss;
  for (marking_map::const_iterator i = markings.begin();
       i != markings.end();
       ++i)
    {
      oss << "Marking for " << i->first << ":\n";
      string marking_str, indented_marking_str;
      dump(i->second, marking_str);
      prefix_lines_with("    ", marking_str, indented_marking_str);
      oss << indented_marking_str << '\n';
    }
  out = oss.str();
}

//
// We have a few concepts of "nullness" here:
//
// - the_null_node is a node_id. It does not correspond to a real node;
//   it's an id you use for the parent of the root, or of any node which
//   is detached.
//
// - the root node has a real node id, just like any other directory.
//
// - the path_component whose string representation is "", the empty
//   string, is the *name* of the root node.  write it as
//   path_component() and test for it with component.empty().
//
// - similarly, the file_path whose string representation is "" also
//   names the root node.  write it as file_path() and test for it
//   with path.empty().
//
// - there is no file_path or path_component corresponding to the_null_node.
//
// We do this in order to support the notion of moving the root directory
// around, or applying attributes to the root directory.  Note that the
// only supported way to move the root is with the 'pivot_root' operation,
// which atomically turns the root directory into a subdirectory and some
// existing subdirectory into the root directory.  This is an UI constraint,
// not a constraint at this level.

u32 last_created_roster = 0;


marking::marking()
  : cow_version(0)
{ }

marking::marking(marking const & other)
  : cow_version(0),
    birth_revision(other.birth_revision),
    parent_name(other.parent_name),
    file_content(other.file_content),
    attrs(other.attrs)
{
}

marking const & marking::operator=(marking const & other)
{
  cow_version = 0;
  birth_revision = other.birth_revision;
  parent_name = other.parent_name;
  file_content = other.file_content;
  attrs = other.attrs;
  return *this;
}

marking_map::marking_map()
  : cow_version(++last_created_roster)
{ }

marking_map::marking_map(marking_map const & other)
  : cow_version(++last_created_roster),
    _store(other._store)
{
  other.cow_version = ++last_created_roster;
}

marking_map const & marking_map::operator=(marking_map const & other)
{
  cow_version = ++last_created_roster;
  other.cow_version = ++last_created_roster;
  _store = other._store;
  return *this;
}

void marking_map::clear()
{
  cow_version = ++last_created_roster;
  _store.clear();
}

const_marking_t marking_map::get_marking(node_id nid) const
{
  marking_t const & m = _store.get_if_present(nid);
  I(m);
  return m;
}

marking_t const & marking_map::get_marking_for_update(node_id nid)
{
  marking_t const & m = _store.get_unshared_if_present(nid);
  I(m);
  if (cow_version == m->cow_version)
    return m;
  if (m.unique())
    {
      m->cow_version = cow_version;
      return m;
    }
  return _store.set(nid, marking_t(new marking(*m)));
}

bool marking_map::contains(node_id nid) const
{
  return static_cast<bool>(_store.get_if_present(nid));
}

void marking_map::remove_marking(node_id nid)
{
  unsigned pre_sz = _store.size();
  _store.unset(nid);
  I(_store.size() == pre_sz - 1);
}

void marking_map::put_marking(node_id nid, marking_t const & m)
{
  I(_store.set_if_missing(nid, m));
}

void marking_map::put_marking(node_id nid, const_marking_t const & m)
{
  I(_store.set_if_missing(nid, boost::const_pointer_cast<marking>(m)));
}

void marking_map::put_or_replace_marking(node_id nid, const_marking_t const & m)
{
  _store.set(nid, boost::const_pointer_cast<marking>(m));
}

size_t marking_map::size() const
{
  return _store.size();
}

marking_map::const_iterator marking_map::begin() const
{
  return _store.begin();
}

marking_map::const_iterator marking_map::end() const
{
  return _store.end();
}


node::node(node_id i)
  : self(i),
    parent(the_null_node),
    name(),
    type(node_type_none),
    cow_version(0)
{
}


node::node()
  : self(the_null_node),
    parent(the_null_node),
    name(),
    type(node_type_none),
    cow_version(0)
{
}


dir_node::dir_node(node_id i)
  : node(i)
{
  type = node_type_dir;
}


dir_node::dir_node()
  : node()
{
  type = node_type_dir;
}


bool
dir_node::has_child(path_component const & pc) const
{
  return children.find(pc) != children.end();
}

node_t
dir_node::get_child(path_component const & pc) const
{
  return safe_get(children, pc);
}


void
dir_node::attach_child(path_component const & pc, node_t child)
{
  I(null_node(child->parent));
  I(child->name.empty());
  I(cow_version == child->cow_version);
  safe_insert(children, make_pair(pc, child));
  child->parent = this->self;
  child->name = pc;
}


node_t
dir_node::detach_child(path_component const & pc)
{
  node_t n = get_child(pc);
  I(cow_version == n->cow_version);
  n->parent = the_null_node;
  n->name = path_component();
  safe_erase(children, pc);
  return n;
}


node_t
dir_node::clone()
{
  dir_t d = dir_t(new dir_node(self));
  d->parent = parent;
  d->name = name;
  d->attrs = attrs;
  d->children = children;
  return d;
}


file_node::file_node(node_id i, file_id const & f)
  : node(i),
    content(f)
{
  type = node_type_file;
}


file_node::file_node()
  : node()
{
  type = node_type_file;
}


node_t
file_node::clone()
{
  file_t f = file_t(new file_node(self, content));
  f->parent = parent;
  f->name = name;
  f->attrs = attrs;
  return f;
}

template <> void
dump(node_t const & n, string & out)
{
  ostringstream oss;
  string name;
  dump(n->name, name);
  oss << "address: " << n << " (uses: " << n.use_count() << ")\n"
      << "self: " << n->self << '\n'
      << "parent: " << n->parent << '\n'
      << "name: " << name << '\n';
  string attr_map_s;
  dump(n->attrs, attr_map_s);
  oss << "attrs:\n" << attr_map_s;
  oss << "type: ";
  if (is_file_t(n))
    {
      oss << "file\ncontent: "
          << downcast_to_file_t(n)->content
          << '\n';
    }
  else
    {
      oss << "dir\n";
      dir_map const & c = downcast_to_dir_t(n)->children;
      oss << "children: " << c.size() << '\n';
      for (dir_map::const_iterator i = c.begin(); i != c.end(); ++i)
        {
          dump(i->first, name);
          oss << "  " << name << " -> " << i->second << '\n';
        }
    }
  out = oss.str();
}


roster_t::roster_t()
  : cow_version(++last_created_roster)
{
}

roster_t::roster_t(roster_t const & other)
  : cow_version(++last_created_roster)
{
  root_dir = other.root_dir;
  nodes = other.nodes;
  other.cow_version = ++last_created_roster;
}

roster_t &
roster_t::operator=(roster_t const & other)
{
  root_dir = other.root_dir;
  nodes = other.nodes;
  cow_version = ++last_created_roster;
  other.cow_version = ++last_created_roster;
  return *this;
}


dfs_iter::dfs_iter(const_dir_t r, bool t = false)
  : root(r), return_root(root), track_path(t)
{
  if (root && !root->children.empty())
    stk.push(make_pair(root, root->children.begin()));
}

bool
dfs_iter::finished() const
{
  return (!return_root) && stk.empty();
}

string const &
dfs_iter::path() const
{
  I(track_path);
  return curr_path;
}

const_node_t
dfs_iter::operator*() const
{
  I(!finished());
  if (return_root)
    return root;
  else
    {
      I(!stk.empty());
      return stk.top().second->second;
    }
}

void
dfs_iter::advance_top()
{
  int prevsize = 0;
  int nextsize = 0;
  pair<const_dir_t, dir_map::const_iterator> & stack_top(stk.top());
  if (track_path)
    {
      prevsize = stack_top.second->first().size();
    }

  ++stack_top.second;

  if (track_path)
    {
      if (stack_top.second != stack_top.first->children.end())
        nextsize = stack_top.second->first().size();

      int tmpsize = curr_path.size()-prevsize;
      I(tmpsize >= 0);
      curr_path.resize(tmpsize);
      if (nextsize != 0)
        curr_path.insert(curr_path.end(),
                         stack_top.second->first().begin(),
                         stack_top.second->first().end());
    }
}

void
dfs_iter::operator++()
{
  I(!finished());

  if (return_root)
    {
      return_root = false;
      if (!stk.empty())
        curr_path = stk.top().second->first();
      return;
    }

  // we're not finished, so we need to set up so operator* will return the
  // right thing.
  node_t ntmp = stk.top().second->second;
  if (is_dir_t(ntmp))
    {
      dir_t dtmp = downcast_to_dir_t(ntmp);
      stk.push(make_pair(dtmp, dtmp->children.begin()));

      if (track_path)
        {
          if (!curr_path.empty())
            curr_path += "/";
          if (!dtmp->children.empty())
            curr_path += dtmp->children.begin()->first();
        }
    }
  else
    {
      advance_top();
    }

  while (!stk.empty()
         && stk.top().second == stk.top().first->children.end())
    {
      stk.pop();
      if (!stk.empty())
        {
          if (track_path)
            {
              curr_path.resize(curr_path.size()-1);
            }
          advance_top();
        }
    }
}

bool
roster_t::has_root() const
{
  return static_cast<bool>(root_dir);
}


inline bool
same_type(const_node_t a, const_node_t b)
{
  return is_file_t(a) == is_file_t(b);
}


bool
shallow_equal(const_node_t a, const_node_t b,
              bool shallow_compare_dir_children,
              bool compare_file_contents)
{
  if (a->self != b->self)
    return false;

  if (a->parent != b->parent)
    return false;

  if (a->name != b->name)
    return false;

  if (a->attrs != b->attrs)
    return false;

  if (! same_type(a,b))
    return false;

  if (is_file_t(a))
    {
      if (compare_file_contents)
        {
          const_file_t fa = downcast_to_file_t(a);
          const_file_t fb = downcast_to_file_t(b);
          if (!(fa->content == fb->content))
            return false;
        }
    }
  else
    {
      const_dir_t da = downcast_to_dir_t(a);
      const_dir_t db = downcast_to_dir_t(b);

      if (shallow_compare_dir_children)
        {
          if (da->children.size() != db->children.size())
            return false;

          dir_map::const_iterator
            i = da->children.begin(),
            j = db->children.begin();

          while (i != da->children.end() && j != db->children.end())
            {
              if (i->first != j->first)
                return false;
              if (i->second->self != j->second->self)
                return false;
              ++i;
              ++j;
            }
          I(i == da->children.end() && j == db->children.end());
        }
    }
  return true;
}


// FIXME_ROSTERS: why does this do two loops? why does it pass 'true' for
// shallow_compare_dir_children to shallow_equal? -- njs
bool
roster_t::operator==(roster_t const & other) const
{
  node_map::const_iterator i = nodes.begin(), j = other.nodes.begin();
  while (i != nodes.end() && j != other.nodes.end())
    {
      if (i->first != j->first)
        return false;
      if (!shallow_equal(i->second, j->second, true))
        return false;
      ++i;
      ++j;
    }

  if (i != nodes.end() || j != other.nodes.end())
    return false;

  dfs_iter p(root_dir), q(other.root_dir);
  while (! (p.finished() || q.finished()))
    {
      if (!shallow_equal(*p, *q, true))
        return false;
      ++p;
      ++q;
    }

  if (!(p.finished() && q.finished()))
    return false;

  return true;
}

// This is exactly the same as roster_t::operator== (and the same FIXME
// above applies) except that it does not compare file contents.
bool
equal_shapes(roster_t const & a, roster_t const & b)
{
  node_map::const_iterator i = a.nodes.begin(), j = b.nodes.begin();
  while (i != a.nodes.end() && j != b.nodes.end())
    {
      if (i->first != j->first)
        return false;
      if (!shallow_equal(i->second, j->second, true, false))
        return false;
      ++i;
      ++j;
    }

  if (i != a.nodes.end() || j != b.nodes.end())
    return false;

  dfs_iter p(a.root_dir), q(b.root_dir);
  while (! (p.finished() || q.finished()))
    {
      if (!shallow_equal(*p, *q, true, false))
        return false;
      ++p;
      ++q;
    }

  if (!(p.finished() && q.finished()))
    return false;

  return true;
}

node_t roster_t::get_node_internal(file_path const & p) const
{
  MM(*this);
  MM(p);
  I(has_root());
  if (p.empty())
    return root_dir;

  dir_t nd = root_dir;
  string const & pi = p.as_internal();
  string::size_type start = 0, stop;
  for (;;)
    {
      stop = pi.find('/', start);
      path_component pc(pi, start, (stop == string::npos
                                    ? stop : stop - start));
      dir_map::const_iterator child = nd->children.find(pc);

      I(child != nd->children.end());
      if (stop == string::npos)
        return child->second;

      start = stop + 1;
      nd = downcast_to_dir_t(child->second);
    }
}

const_node_t
roster_t::get_node(file_path const & p) const
{
  return get_node_internal(p);
}

node_t
roster_t::get_node_for_update(file_path const & p)
{
  node_t n = get_node_internal(p);
  unshare(n);
  return n;
}

bool
roster_t::has_node(node_id n) const
{
  return static_cast<bool>(nodes.get_if_present(n));
}

bool
roster_t::is_root(node_id n) const
{
  return has_root() && root_dir->self == n;
}

bool
roster_t::is_attached(node_id n) const
{
  if (!has_root())
    return false;
  if (n == root_dir->self)
    return true;
  if (!has_node(n))
    return false;

  const_node_t node = get_node(n);

  return !null_node(node->parent);
}

bool
roster_t::has_node(file_path const & p) const
{
  MM(*this);
  MM(p);

  if (!has_root())
    return false;
  if (p.empty())
    return true;

  dir_t nd = root_dir;
  string const & pi = p.as_internal();
  string::size_type start = 0, stop;
  for (;;)
    {
      stop = pi.find('/', start);
      path_component pc(pi, start, (stop == string::npos
                                    ? stop : stop - start));
      dir_map::const_iterator child = nd->children.find(pc);

      if (child == nd->children.end())
        return false;
      if (stop == string::npos)
        return true;
      if (!is_dir_t(child->second))
        return false;

      start = stop + 1;
      nd = downcast_to_dir_t(child->second);
    }
}

const_node_t
roster_t::get_node(node_id nid) const
{
  node_t const &n(nodes.get_if_present(nid));
  I(n);
  return n;
}

node_t
roster_t::get_node_for_update(node_id nid)
{
  node_t n(nodes.get_if_present(nid));
  I(n);
  unshare(n);
  return n;
}


void
roster_t::get_name(node_id nid, file_path & p) const
{
  I(!null_node(nid));

  stack<const_node_t> sp;
  size_t size = 0;

  while (nid != root_dir->self)
    {
      const_node_t n = get_node(nid);
      sp.push(n);
      size += n->name().length() + 1;
      nid = n->parent;
    }

  if (size == 0)
    {
      p.clear();
      return;
    }

  I(!bookkeeping_path::internal_string_is_bookkeeping_path(typecast_vocab<utf8>(sp.top()->name)));

  string tmp;
  tmp.reserve(size);

  for (;;)
    {
      tmp += sp.top()->name();
      sp.pop();
      if (sp.empty())
        break;
      tmp += "/";
    }

  p = file_path(tmp, 0, string::npos);  // short circuit constructor
}

void
roster_t::unshare(node_t & n, bool is_in_node_map)
{
  if (cow_version == n->cow_version)
    return;
  // we can't get at the (possibly shared) pointer in the node_map,
  // so if we were given the only pointer then we know the node
  // isn't in any other rosters
  if (n.unique())
    {
      n->cow_version = cow_version;
      return;
    }
  // here we could theoretically walk up the tree to see if
  // the node or any of its parents have too many references,
  // but I'm guessing that the avoided copies won't be worth
  // the extra search time

  node_t old = n;
  n = n->clone();
  n->cow_version = cow_version;
  if (is_in_node_map)
    nodes.set(n->self, n);
  if (!null_node(n->parent))
    {
      node_t p = nodes.get_if_present(n->parent);
      I(p);
      unshare(p);
      downcast_to_dir_t(p)->children[n->name] = n;
    }
  if (root_dir && root_dir->self == n->self)
    {
      root_dir = downcast_to_dir_t(n);
    }
}

void
roster_t::replace_node_id(node_id from, node_id to)
{
  I(!null_node(from));
  I(!null_node(to));
  node_t n = nodes.get_if_present(from);
  I(n);
  nodes.unset(from);

  unshare(n, false);

  I(nodes.set_if_missing(to, n));
  n->self = to;
  if (is_dir_t(n))
    {
      dir_t d = downcast_to_dir_t(n);
      for (dir_map::iterator i = d->children.begin(); i != d->children.end(); ++i)
        {
          I(i->second->parent == from);
          i->second->parent = to;
        }
    }
}


// this records the old location into the old_locations member, to prevent the
// same node from being re-attached at the same place.
node_id
roster_t::detach_node(file_path const & p)
{
  file_path dirname;
  path_component basename;
  p.dirname_basename(dirname, basename);

  I(has_root());
  if (basename.empty())
    {
      // detaching the root dir
      I(dirname.empty());
      node_id root_id = root_dir->self;
      safe_insert(old_locations, make_pair(root_id, make_pair(root_dir->parent,
                                                              root_dir->name)));
      // clear ("reset") the root_dir shared_pointer
      root_dir.reset();
      I(!has_root());
      return root_id;
    }

  node_t pp = get_node_for_update(dirname);
  dir_t parent = downcast_to_dir_t(pp);
  node_t c = parent->get_child(basename);
  unshare(c);
  node_id nid = parent->detach_child(basename)->self;
  safe_insert(old_locations,
              make_pair(nid, make_pair(parent->self, basename)));
  I(!null_node(nid));
  return nid;
}

void
roster_t::detach_node(node_id nid)
{
  node_t n = get_node_for_update(nid);
  if (null_node(n->parent))
    {
      // detaching the root dir
      I(n->name.empty());
      safe_insert(old_locations,
                  make_pair(nid, make_pair(n->parent, n->name)));
      root_dir.reset();
      I(!has_root());
    }
  else
    {
      path_component name = n->name;
      node_t p = get_node_for_update(n->parent);
      dir_t parent = downcast_to_dir_t(p);
      I(parent->detach_child(name) == n);
      safe_insert(old_locations,
                  make_pair(nid, make_pair(n->parent, name)));
    }
}

void
roster_t::drop_detached_node(node_id nid)
{
  // ensure the node is already detached
  const_node_t n = get_node(nid);
  I(null_node(n->parent));
  I(n->name.empty());
  // if it's a dir, make sure it's empty
  if (is_dir_t(n))
    I(downcast_to_dir_t(n)->children.empty());
  // all right, kill it
  nodes.unset(nid);

  // Resolving a duplicate name conflict via drop one side requires dropping
  // nodes that were never attached. So we erase the key without checking
  // whether it was present.
  old_locations.erase(nid);
}


// this creates a node in a detached state, but it does _not_ insert an entry
// for it into the old_locations member, because there is no old_location to
// forbid
node_id
roster_t::create_dir_node(node_id_source & nis)
{
  node_id nid = nis.next();
  create_dir_node(nid);
  return nid;
}
void
roster_t::create_dir_node(node_id nid)
{
  dir_t d = dir_t(new dir_node());
  d->self = nid;
  d->cow_version = cow_version;
  nodes.set(nid, d);
}


// this creates a node in a detached state, but it does _not_ insert an entry
// for it into the old_locations member, because there is no old_location to
// forbid
node_id
roster_t::create_file_node(file_id const & content, node_id_source & nis)
{
  node_id nid = nis.next();
  create_file_node(content, nid);
  return nid;
}

void
roster_t::create_file_node(file_id const & content, node_id nid)
{
  file_t f = file_t(new file_node());
  f->self = nid;
  f->content = content;
  f->cow_version = cow_version;
  nodes.set(nid, f);
}

void
roster_t::attach_node(node_id nid, file_path const & p)
{
  MM(p);
  if (p.empty())
    // attaching the root node
    attach_node(nid, the_null_node, path_component());
  else
    {
      file_path dir;
      path_component base;
      p.dirname_basename(dir, base);
      attach_node(nid, get_node(dir)->self, base);
    }
}

void
roster_t::attach_node(node_id nid, node_id parent, path_component name)
{
  node_t n = get_node_for_update(nid);

  I(!null_node(n->self));
  // ensure the node is already detached (as best one can)
  I(null_node(n->parent));
  I(n->name.empty());

  // this iterator might point to old_locations.end(), because old_locations
  // only includes entries for renames, not new nodes
  map<node_id, pair<node_id, path_component> >::iterator
    i = old_locations.find(nid);

  if (null_node(parent) || name.empty())
    {
      I(null_node(parent) && name.empty());
      I(null_node(n->parent));
      I(n->name.empty());
      I(!has_root());
      root_dir = downcast_to_dir_t(n);
      I(i == old_locations.end() || i->second != make_pair(root_dir->parent,
                                                           root_dir->name));
    }
  else
    {
      node_t p = get_node_for_update(parent);
      dir_t parent_n = downcast_to_dir_t(p);
      parent_n->attach_child(name, n);
      I(i == old_locations.end() || i->second != make_pair(n->parent, n->name));
    }

  if (i != old_locations.end())
    old_locations.erase(i);
}

void
roster_t::apply_delta(file_path const & pth,
                      file_id const & old_id,
                      file_id const & new_id)
{
  node_t n = get_node_for_update(pth);
  file_t f = downcast_to_file_t(n);
  I(f->content == old_id);
  I(!null_node(f->self));
  I(!(f->content == new_id));
  f->content = new_id;
}

void
roster_t::set_content(node_id nid, file_id const & new_id)
{
  node_t n = get_node_for_update(nid);
  file_t f = downcast_to_file_t(n);
  I(!(f->content == new_id));
  f->content = new_id;
}


void
roster_t::clear_attr(file_path const & path,
                     attr_key const & key)
{
  set_attr(path, key, make_pair(false, attr_value()));
}

void
roster_t::erase_attr(node_id nid,
                     attr_key const & name)
{
  node_t n = get_node_for_update(nid);
  safe_erase(n->attrs, name);
}

void
roster_t::set_attr(file_path const & path,
                   attr_key const & key,
                   attr_value const & val)
{
  set_attr(path, key, make_pair(true, val));
}


void
roster_t::set_attr(file_path const & pth,
                   attr_key const & name,
                   pair<bool, attr_value> const & val)
{
  node_t n = get_node_for_update(pth);
  I(val.first || val.second().empty());
  I(!null_node(n->self));
  attr_map_t::iterator i = n->attrs.find(name);
  if (i == n->attrs.end())
    i = safe_insert(n->attrs, make_pair(name,
                                        make_pair(false, attr_value())));
  I(i->second != val);
  i->second = val;
}

// same as above, but allowing <unknown> -> <dead> transition
void
roster_t::set_attr_unknown_to_dead_ok(node_id nid,
                                      attr_key const & name,
                                      pair<bool, attr_value> const & val)
{
  node_t n = get_node_for_update(nid);
  I(val.first || val.second().empty());
  attr_map_t::iterator i = n->attrs.find(name);
  if (i != n->attrs.end())
    I(i->second != val);
  n->attrs[name] = val;
}

bool
roster_t::get_attr(file_path const & pth,
                   attr_key const & name,
                   attr_value & val) const
{
  I(has_node(pth));

  const_node_t n = get_node(pth);
  attr_map_t::const_iterator i = n->attrs.find(name);
  if (i != n->attrs.end() && i->second.first)
    {
      val = i->second.second;
      return true;
    }
  return false;
}


template <> void
dump(roster_t const & val, string & out)
{
  ostringstream oss;
  if (val.root_dir)
    oss << "Root node: " << val.root_dir->self << '\n'
        << "   at " << val.root_dir << ", uses: " << val.root_dir.use_count() << '\n';
  else
    oss << "root dir is NULL\n";
  for (node_map::const_iterator i = val.nodes.begin(); i != val.nodes.end(); ++i)
    {
      oss << "\nNode " << i->first << '\n';
      string node_s;
      dump(i->second, node_s);
      oss << node_s;
    }
  out = oss.str();
}

template <> void
dump(std::map<node_id, std::pair<node_id, path_component> > const & val, string & out)
{
  ostringstream oss;
  for (std::map<node_id, std::pair<node_id, path_component> >::const_iterator i = val.begin();
       i != val.end();
       ++i)
    {
      oss << "Node " << i->first;
      oss << " node " << i->second.first;
      oss << " path " << i->second.second << "\n";
    }
  out = oss.str();
}

void
roster_t::check_sane(bool temp_nodes_ok) const
{
  MM(*this);
  MM(old_locations);

  node_id parent_id(the_null_node);
  const_dir_t parent_dir;
  I(old_locations.empty()); // if fail, some renamed node is still present and detached
  I(has_root());
  size_t maxdepth = nodes.size();
  bool is_first = true;
  for (dfs_iter i(root_dir); !i.finished(); ++i)
    {
      const_node_t const &n(*i);
      if (is_first)
        {
          I(n->name.empty() && null_node(n->parent));
          is_first = false;
        }
      else
        {
          I(!n->name.empty() && !null_node(n->parent));

          if (n->parent != parent_id)
            {
              parent_id = n->parent;
              parent_dir = downcast_to_dir_t(get_node(parent_id));
            }
          I(parent_dir->get_child(n->name) == n);
        }
      for (attr_map_t::const_iterator a = n->attrs.begin(); a != n->attrs.end(); ++a)
        {
          I(a->second.first || a->second.second().empty());
        }
      if (is_file_t(n))
        {
          I(!null_id(downcast_to_file_t(n)->content));
        }
      node_id nid(n->self);
      I(!null_node(nid));
      if (!temp_nodes_ok)
        {
          I(!temp_node(nid));
        }
      I(n == get_node(nid));
      I(maxdepth-- > 0);
    }
  I(maxdepth == 0); // if fails, some newly created node is not attached
}

void
roster_t::check_sane_against(marking_map const & markings, bool temp_nodes_ok) const
{
  check_sane(temp_nodes_ok);

  node_map::const_iterator ri;
  marking_map::const_iterator mi;

  for (ri = nodes.begin(), mi = markings.begin();
       ri != nodes.end() && mi != markings.end();
       ++ri, ++mi)
    {
      I(!null_id(mi->second->birth_revision));
      I(!mi->second->parent_name.empty());

      if (is_file_t(ri->second))
        I(!mi->second->file_content.empty());
      else
        I(mi->second->file_content.empty());

      attr_map_t::const_iterator rai;
      map<attr_key, set<revision_id> >::const_iterator mai;
      for (rai = ri->second->attrs.begin(), mai = mi->second->attrs.begin();
           rai != ri->second->attrs.end() && mai != mi->second->attrs.end();
           ++rai, ++mai)
        {
          I(rai->first == mai->first);
          I(!mai->second.empty());
        }
      I(rai == ri->second->attrs.end() && mai == mi->second->attrs.end());
      // TODO: attrs
    }

  I(ri == nodes.end() && mi == markings.end());
}


temp_node_id_source::temp_node_id_source()
  : curr(first_temp_node)
{}

node_id
temp_node_id_source::next()
{
    node_id n = curr++;
    I(temp_node(n));
    return n;
}

editable_roster_base::editable_roster_base(roster_t & r, node_id_source & nis)
  : r(r), nis(nis)
{}

node_id
editable_roster_base::detach_node(file_path const & src)
{
  // L(FL("detach_node('%s')") % src);
  return r.detach_node(src);
}

void
editable_roster_base::drop_detached_node(node_id nid)
{
  // L(FL("drop_detached_node(%d)") % nid);
  r.drop_detached_node(nid);
}

node_id
editable_roster_base::create_dir_node()
{
  // L(FL("create_dir_node()"));
  node_id n = r.create_dir_node(nis);
  // L(FL("create_dir_node() -> %d") % n);
  return n;
}

node_id
editable_roster_base::create_file_node(file_id const & content)
{
  // L(FL("create_file_node('%s')") % content);
  node_id n = r.create_file_node(content, nis);
  // L(FL("create_file_node('%s') -> %d") % content % n);
  return n;
}

void
editable_roster_base::attach_node(node_id nid, file_path const & dst)
{
  // L(FL("attach_node(%d, '%s')") % nid % dst);
  MM(dst);
  MM(this->r);
  r.attach_node(nid, dst);
}

void
editable_roster_base::apply_delta(file_path const & pth,
                                  file_id const & old_id,
                                  file_id const & new_id)
{
  // L(FL("apply_delta('%s', '%s', '%s')") % pth % old_id % new_id);
  r.apply_delta(pth, old_id, new_id);
}

void
editable_roster_base::clear_attr(file_path const & path,
                                 attr_key const & key)
{
  // L(FL("clear_attr('%s', '%s')") % path % key);
  r.clear_attr(path, key);
}

void
editable_roster_base::set_attr(file_path const & path,
                               attr_key const & key,
                               attr_value const & val)
{
  // L(FL("set_attr('%s', '%s', '%s')") % path % key % val);
  r.set_attr(path, key, val);
}

void
editable_roster_base::commit()
{
}

namespace
{
  class editable_roster_for_merge
    : public editable_roster_base
  {
  public:
    set<node_id> new_nodes;
    editable_roster_for_merge(roster_t & r, node_id_source & nis)
      : editable_roster_base(r, nis)
    {}
    virtual node_id create_dir_node()
    {
      node_id nid = this->editable_roster_base::create_dir_node();
      new_nodes.insert(nid);
      return nid;
    }
    virtual node_id create_file_node(file_id const & content)
    {
      node_id nid = this->editable_roster_base::create_file_node(content);
      new_nodes.insert(nid);
      return nid;
    }
  };


  void union_new_nodes(roster_t & a, set<node_id> & a_new,
                       roster_t & b, set<node_id> & b_new,
                       node_id_source & nis)
  {
    // We must not replace a node whose id is in both a_new and b_new
    // with a new temp id that is already in either set.  b_new is
    // destructively modified, so record the union of both sets now.
    set<node_id> all_new_nids;
    std::set_union(a_new.begin(), a_new.end(),
                   b_new.begin(), b_new.end(),
                   std::inserter(all_new_nids, all_new_nids.begin()));

    // First identify nodes that are new in A but not in B, or new in both.
    for (set<node_id>::const_iterator i = a_new.begin(); i != a_new.end(); ++i)
      {
        node_id const aid = *i;
        file_path p;
        // SPEEDUP?: climb out only so far as is necessary to find a shared
        // id?  possibly faster (since usually will get a hit immediately),
        // but may not be worth the effort (since it doesn't take that long to
        // get out in any case)
        a.get_name(aid, p);
        node_id bid = b.get_node(p)->self;
        if (b_new.find(bid) != b_new.end())
          {
            I(temp_node(bid));
            node_id new_nid;
            do
              new_nid = nis.next();
            while (all_new_nids.find(new_nid) != all_new_nids.end());

            a.replace_node_id(aid, new_nid);
            b.replace_node_id(bid, new_nid);
            b_new.erase(bid);
          }
        else
          {
            a.replace_node_id(aid, bid);
          }
      }

    // Now identify nodes that are new in B but not A.
    for (set<node_id>::const_iterator i = b_new.begin(); i != b_new.end(); i++)
      {
        node_id const bid = *i;
        file_path p;
        // SPEEDUP?: climb out only so far as is necessary to find a shared
        // id?  possibly faster (since usually will get a hit immediately),
        // but may not be worth the effort (since it doesn't take that long to
        // get out in any case)
        b.get_name(bid, p);
        node_id aid = a.get_node(p)->self;
        I(a_new.find(aid) == a_new.end());
        b.replace_node_id(bid, aid);
      }
  }

  void
  union_corpses(roster_t & left, roster_t & right)
  {
    // left and right should be equal, except that each may have some attr
    // corpses that the other does not
    node_map::const_iterator left_i = left.all_nodes().begin();
    node_map::const_iterator right_i = right.all_nodes().begin();
    while (left_i != left.all_nodes().end() || right_i != right.all_nodes().end())
      {
        I(left_i->second->self == right_i->second->self);
        parallel::iter<attr_map_t> j(left_i->second->attrs,
                                     right_i->second->attrs);
        // we batch up the modifications until the end, so as not to be
        // changing things around under the parallel::iter's feet
        set<attr_key> left_missing, right_missing;
        while (j.next())
          {
            MM(j);
            switch (j.state())
              {
              case parallel::invalid:
                I(false);

              case parallel::in_left:
                // this is a corpse
                I(!j.left_data().first);
                right_missing.insert(j.left_key());
                break;

              case parallel::in_right:
                // this is a corpse
                I(!j.right_data().first);
                left_missing.insert(j.right_key());
                break;

              case parallel::in_both:
                break;
              }
          }
        node_t left_n = left_i->second;
        for (set<attr_key>::const_iterator j = left_missing.begin();
             j != left_missing.end(); ++j)
          {
            left.unshare(left_n);
            safe_insert(left_n->attrs,
                        make_pair(*j, make_pair(false, attr_value())));
          }
        node_t right_n = right_i->second;
        for (set<attr_key>::const_iterator j = right_missing.begin();
             j != right_missing.end(); ++j)
          {
            right.unshare(right_n);
            safe_insert(right_n->attrs,
                        make_pair(*j, make_pair(false, attr_value())));
          }
        ++left_i;
        ++right_i;
      }
    I(left_i == left.all_nodes().end());
    I(right_i == right.all_nodes().end());
  }

  // After this, left should == right, and there should be no temporary ids.
  // Destroys sets, because that's handy (it has to scan over both, but it can
  // skip some double-scanning)
  void
  unify_rosters(roster_t & left, set<node_id> & left_new,
                roster_t & right, set<node_id> & right_new,
                node_id_source & nis)
  {
    // Our basic problem is this: when interpreting a revision with multiple
    // csets, those csets all give enough information for us to get the same
    // manifest, and even a bit more than that.  However, there is some
    // information that is not exposed at the manifest level, and csets alone
    // do not give us all we need.  This function is responsible taking the
    // two rosters that we get from pure cset application, and fixing them up
    // so that they are wholly identical.

    // The first thing that is missing is identification of files.  If one
    // cset says "add_file" and the other says nothing, then the add_file is
    // not really an add_file.  One of our rosters will have a temp id for
    // this file, and the other will not.  In this case, we replace the temp
    // id with the other side's permanent id.  However, if both csets say
    // "add_file", then this really is a new id; both rosters will have temp
    // ids, and we replace both of them with a newly allocated id.  After
    // this, the two rosters will have identical node_ids at every path.
    union_new_nodes(left, left_new, right, right_new, nis);

    // The other thing we need to fix up is attr corpses.  Live attrs are made
    // identical by the csets; but if, say, on one side of a fork an attr is
    // added and then deleted, then one of our incoming merge rosters will
    // have a corpse for that attr, and the other will not.  We need to make
    // sure at both of them end up with the corpse.  This function fixes up
    // that.
    union_corpses(left, right);
  }

  template <typename T> void
  mark_unmerged_scalar(set<revision_id> const & parent_marks,
                       T const & parent_val,
                       revision_id const & new_rid,
                       T const & new_val,
                       set<revision_id> & new_marks)
  {
    I(new_marks.empty());
    if (parent_val == new_val)
      new_marks = parent_marks;
    else
      new_marks.insert(new_rid);
  }

  // This function implements the case.
  //   a   b1
  //    \ /
  //     b2
  void
  mark_won_merge(set<revision_id> const & a_marks,
                 set<revision_id> const & a_uncommon_ancestors,
                 set<revision_id> const & b1_marks,
                 revision_id const & new_rid,
                 set<revision_id> & new_marks)
  {
    for (set<revision_id>::const_iterator i = a_marks.begin();
         i != a_marks.end(); ++i)
      {
        if (a_uncommon_ancestors.find(*i) != a_uncommon_ancestors.end())
          {
            // at least one element of *(a) is not an ancestor of b1
            new_marks.clear();
            new_marks.insert(new_rid);
            return;
          }
      }
    // all elements of *(a) are ancestors of b1; this was a clean merge to b,
    // so copy forward the marks.
    new_marks = b1_marks;
  }

  template <typename T> void
  mark_merged_scalar(set<revision_id> const & left_marks,
                     set<revision_id> const & left_uncommon_ancestors,
                     T const & left_val,
                     set<revision_id> const & right_marks,
                     set<revision_id> const & right_uncommon_ancestors,
                     T const & right_val,
                     revision_id const & new_rid,
                     T const & new_val,
                     set<revision_id> & new_marks)
  {
    I(new_marks.empty());

    // let's not depend on T::operator!= being defined, only on T::operator==
    // being defined.
    bool diff_from_left = !(new_val == left_val);
    bool diff_from_right = !(new_val == right_val);

    // some quick sanity checks
    for (set<revision_id>::const_iterator i = left_marks.begin();
         i != left_marks.end(); ++i)
      I(right_uncommon_ancestors.find(*i) == right_uncommon_ancestors.end());
    for (set<revision_id>::const_iterator i = right_marks.begin();
         i != right_marks.end(); ++i)
      I(left_uncommon_ancestors.find(*i) == left_uncommon_ancestors.end());

    if (diff_from_left && diff_from_right)
      new_marks.insert(new_rid);

    else if (diff_from_left && !diff_from_right)
      mark_won_merge(left_marks, left_uncommon_ancestors, right_marks,
                     new_rid, new_marks);

    else if (!diff_from_left && diff_from_right)
      mark_won_merge(right_marks, right_uncommon_ancestors, left_marks,
                     new_rid, new_marks);

    else
      {
        // this is the case
        //   a   a
        //    \ /
        //     a
        // so we simply union the mark sets.  This is technically not
        // quite the canonical multi-*-merge thing to do; in the case
        //     a1*
        //    / \      (blah blah; avoid multi-line-comment warning)
        //   b   a2
        //   |   |
        //   a3* |
        //    \ /
        //     a4
        // we will set *(a4) = {a1, a3}, even though the minimal
        // common ancestor set is {a3}.  we could fix this by running
        // erase_ancestors.  However, there isn't really any point;
        // the only operation performed on *(a4) is to test *(a4) > R
        // for some revision R.  The truth-value of this test cannot
        // be affected by added new revisions to *(a4) that are
        // ancestors of revisions that are already in *(a4).
        set_union(left_marks.begin(), left_marks.end(),
                  right_marks.begin(), right_marks.end(),
                  inserter(new_marks, new_marks.begin()));
      }
  }

  void
  mark_new_node(revision_id const & new_rid, const_node_t n, marking_map & mm)
  {
    marking_t new_marking(new marking());
    new_marking->birth_revision = new_rid;
    I(new_marking->parent_name.empty());
    new_marking->parent_name.insert(new_rid);
    I(new_marking->file_content.empty());
    if (is_file_t(n))
      new_marking->file_content.insert(new_rid);
    I(new_marking->attrs.empty());
    set<revision_id> singleton;
    singleton.insert(new_rid);
    for (attr_map_t::const_iterator i = n->attrs.begin();
         i != n->attrs.end(); ++i)
      new_marking->attrs.insert(make_pair(i->first, singleton));
    mm.put_marking(n->self, new_marking);
  }

  void
  mark_unmerged_node(const_marking_t const & parent_marking,
                     const_node_t parent_n,
                     revision_id const & new_rid, const_node_t n,
                     marking_map & mm)
  {
    // Our new marking map is initialized as a copy of the parent map.
    // So, if nothing's changed, there's nothing to do. Unless this
    // is a merge, and the parent marking that was copied happens to
    // be the other parent than the one this node came from.
    if (n == parent_n || shallow_equal(n, parent_n, true))
      {
        if (mm.contains(n->self))
          {
            return;
          }
        else
          {
            mm.put_marking(n->self, parent_marking);
            return;
          }
      }

    I(same_type(parent_n, n) && parent_n->self == n->self);

    marking_t new_marking(new marking());

    new_marking->birth_revision = parent_marking->birth_revision;

    mark_unmerged_scalar(parent_marking->parent_name,
                         make_pair(parent_n->parent, parent_n->name),
                         new_rid,
                         make_pair(n->parent, n->name),
                         new_marking->parent_name);

    if (is_file_t(n))
      mark_unmerged_scalar(parent_marking->file_content,
                           downcast_to_file_t(parent_n)->content,
                           new_rid,
                           downcast_to_file_t(n)->content,
                           new_marking->file_content);

    for (attr_map_t::const_iterator i = n->attrs.begin();
           i != n->attrs.end(); ++i)
      {
        set<revision_id> & new_marks = new_marking->attrs[i->first];
        I(new_marks.empty());
        attr_map_t::const_iterator j = parent_n->attrs.find(i->first);
        if (j == parent_n->attrs.end())
          new_marks.insert(new_rid);
        else
          mark_unmerged_scalar(safe_get(parent_marking->attrs, i->first),
                               j->second,
                               new_rid, i->second, new_marks);
      }

    mm.put_or_replace_marking(n->self, new_marking);
  }

  void
  mark_merged_node(const_marking_t const & left_marking,
                   set<revision_id> const & left_uncommon_ancestors,
                   const_node_t ln,
                   const_marking_t const & right_marking,
                   set<revision_id> const & right_uncommon_ancestors,
                   const_node_t rn,
                   revision_id const & new_rid,
                   const_node_t n,
                   marking_map & mm)
  {
    bool same_nodes = ((ln == rn || shallow_equal(ln, rn, true)) &&
                       (ln == n || shallow_equal(ln, n, true)));
    if (same_nodes)
      {
        bool same_markings = left_marking == right_marking
          || *left_marking == *right_marking;
        if (same_markings)
          {
            // The child marking will be the same as both parent markings,
            // so just leave it as whichever it was copied from.
            return;
          }
      }

    I(same_type(ln, n) && same_type(rn, n));
    I(left_marking->birth_revision == right_marking->birth_revision);
    marking_t new_marking = mm.get_marking_for_update(n->self);
    new_marking->birth_revision = left_marking->birth_revision;
    MM(n->self);

    // name
    new_marking->parent_name.clear();
    mark_merged_scalar(left_marking->parent_name, left_uncommon_ancestors,
                       make_pair(ln->parent, ln->name),
                       right_marking->parent_name, right_uncommon_ancestors,
                       make_pair(rn->parent, rn->name),
                       new_rid,
                       make_pair(n->parent, n->name),
                       new_marking->parent_name);
    // content
    if (is_file_t(n))
      {
        const_file_t f = downcast_to_file_t(n);
        const_file_t lf = downcast_to_file_t(ln);
        const_file_t rf = downcast_to_file_t(rn);
        new_marking->file_content.clear();
        mark_merged_scalar(left_marking->file_content, left_uncommon_ancestors,
                           lf->content,
                           right_marking->file_content, right_uncommon_ancestors,
                           rf->content,
                           new_rid, f->content, new_marking->file_content);
      }
    // attrs
    new_marking->attrs.clear();
    for (attr_map_t::const_iterator i = n->attrs.begin();
         i != n->attrs.end(); ++i)
      {
        attr_key const & key = i->first;
        MM(key);
        attr_map_t::const_iterator li = ln->attrs.find(key);
        attr_map_t::const_iterator ri = rn->attrs.find(key);
        I(new_marking->attrs.find(key) == new_marking->attrs.end());
        // [], when used to refer to a non-existent element, default
        // constructs that element and returns a reference to it.  We make use
        // of this here.
        set<revision_id> & new_marks = new_marking->attrs[key];

        if (li == ln->attrs.end() && ri == rn->attrs.end())
          // this is a brand new attribute, never before seen
          safe_insert(new_marks, new_rid);

        else if (li != ln->attrs.end() && ri == rn->attrs.end())
          // only the left side has seen this attr before
          mark_unmerged_scalar(safe_get(left_marking->attrs, key),
                               li->second,
                               new_rid, i->second, new_marks);

        else if (li == ln->attrs.end() && ri != rn->attrs.end())
          // only the right side has seen this attr before
          mark_unmerged_scalar(safe_get(right_marking->attrs, key),
                               ri->second,
                               new_rid, i->second, new_marks);

        else
          // both sides have seen this attr before
          mark_merged_scalar(safe_get(left_marking->attrs, key),
                             left_uncommon_ancestors,
                             li->second,
                             safe_get(right_marking->attrs, key),
                             right_uncommon_ancestors,
                             ri->second,
                             new_rid, i->second, new_marks);
      }

    // some extra sanity checking -- attributes are not allowed to be deleted,
    // so we double check that they haven't.
    // SPEEDUP?: this code could probably be made more efficient -- but very
    // rarely will any node have more than, say, one attribute, so it probably
    // doesn't matter.
    for (attr_map_t::const_iterator i = ln->attrs.begin();
         i != ln->attrs.end(); ++i)
      I(n->attrs.find(i->first) != n->attrs.end());
    for (attr_map_t::const_iterator i = rn->attrs.begin();
         i != rn->attrs.end(); ++i)
      I(n->attrs.find(i->first) != n->attrs.end());
  }

  void drop_extra_markings(roster_t const & ros, marking_map & mm)
  {
    if (mm.size() > ros.all_nodes().size())
      {
        std::set<node_id> to_drop;

        marking_map::const_iterator mi = mm.begin(), me = mm.end();
        node_map::const_iterator ri = ros.all_nodes().begin(), re = ros.all_nodes().end();

        for (; mi != me; ++mi)
          {
            if (ri == re)
              {
                to_drop.insert(mi->first);
              }
            else
              {
                if (ri->first < mi->first)
                  ++ri;
                I(ri == re || ri->first >= mi->first);
                if (ri == re || ri->first > mi->first)
                  to_drop.insert(mi->first);
              }
          }
        for (std::set<node_id>::const_iterator i = to_drop.begin();
             i != to_drop.end(); ++i)
          {
            mm.remove_marking(*i);
          }
      }
    I(mm.size() == ros.all_nodes().size());
  }

} // anonymous namespace


// This function is also responsible for verifying ancestry invariants --
// those invariants on a roster that involve the structure of the roster's
// parents, rather than just the structure of the roster itself.
void
mark_merge_roster(roster_t const & left_roster,
                  marking_map const & left_markings,
                  set<revision_id> const & left_uncommon_ancestors,
                  roster_t const & right_roster,
                  marking_map const & right_markings,
                  set<revision_id> const & right_uncommon_ancestors,
                  revision_id const & new_rid,
                  roster_t const & merge,
                  marking_map & new_markings)
{
  {
    int left_err = left_markings.size() - merge.all_nodes().size();
    int right_err = right_markings.size() - merge.all_nodes().size();
    if (left_err * left_err > right_err * right_err)
      new_markings = right_markings;
    else
      new_markings = left_markings;
  }

  for (node_map::const_iterator i = merge.all_nodes().begin();
       i != merge.all_nodes().end(); ++i)
    {
      node_t const & n = i->second;
      node_t const &left_node = left_roster.all_nodes().get_if_present(i->first);
      node_t const &right_node = right_roster.all_nodes().get_if_present(i->first);

      bool exists_in_left = static_cast<bool>(left_node);
      bool exists_in_right = static_cast<bool>(right_node);

      if (!exists_in_left && !exists_in_right)
        mark_new_node(new_rid, n, new_markings);

      else if (!exists_in_left && exists_in_right)
        {
          const_marking_t const & right_marking = right_markings.get_marking(n->self);
          // must be unborn on the left (as opposed to dead)
          I(right_uncommon_ancestors.find(right_marking->birth_revision)
            != right_uncommon_ancestors.end());
          mark_unmerged_node(right_marking, right_node,
                             new_rid, n, new_markings);
        }
      else if (exists_in_left && !exists_in_right)
        {
          const_marking_t const & left_marking = left_markings.get_marking(n->self);
          // must be unborn on the right (as opposed to dead)
          I(left_uncommon_ancestors.find(left_marking->birth_revision)
            != left_uncommon_ancestors.end());
          mark_unmerged_node(left_marking, left_node,
                             new_rid, n, new_markings);
        }
      else
        {
          mark_merged_node(left_markings.get_marking(n->self),
                           left_uncommon_ancestors, left_node,
                           right_markings.get_marking(n->self),
                           right_uncommon_ancestors, right_node,
                           new_rid, n, new_markings);
        }
    }

  drop_extra_markings(merge, new_markings);
}

namespace {

  class editable_roster_for_nonmerge
    : public editable_roster_base
  {
  public:
    editable_roster_for_nonmerge(roster_t & r, node_id_source & nis,
                                 revision_id const & rid,
                                 marking_map & markings)
      : editable_roster_base(r, nis),
        rid(rid), markings(markings)
    {}

    virtual node_id detach_node(file_path const & src)
    {
      node_id nid = this->editable_roster_base::detach_node(src);
      marking_t marking = markings.get_marking_for_update(nid);
      marking->parent_name.clear();
      marking->parent_name.insert(rid);
      return nid;
    }

    virtual void drop_detached_node(node_id nid)
    {
      this->editable_roster_base::drop_detached_node(nid);
      markings.remove_marking(nid);
    }

    virtual node_id create_dir_node()
    {
      return handle_new(this->editable_roster_base::create_dir_node());
    }

    virtual node_id create_file_node(file_id const & content)
    {
      return handle_new(this->editable_roster_base::create_file_node(content));
    }

    virtual void apply_delta(file_path const & pth,
                             file_id const & old_id, file_id const & new_id)
    {
      this->editable_roster_base::apply_delta(pth, old_id, new_id);
      node_id nid = r.get_node(pth)->self;
      marking_t marking = markings.get_marking_for_update(nid);
      marking->file_content.clear();
      marking->file_content.insert(rid);
    }

    virtual void clear_attr(file_path const & path, attr_key const & key)
    {
      this->editable_roster_base::clear_attr(path, key);
      handle_attr(path, key);
    }

    virtual void set_attr(file_path const & path, attr_key const & key,
                          attr_value const & val)
    {
      this->editable_roster_base::set_attr(path, key, val);
      handle_attr(path, key);
    }

    node_id handle_new(node_id nid)
    {
      const_node_t n = r.get_node(nid);
      mark_new_node(rid, n, markings);
      return nid;
    }

    void handle_attr(file_path const & pth, attr_key const & name)
    {
      node_id nid = r.get_node(pth)->self;
      marking_t marking = markings.get_marking_for_update(nid);
      map<attr_key, set<revision_id> >::iterator am = marking->attrs.find(name);
      if (am == marking->attrs.end())
        {
          marking->attrs.insert(make_pair(name, set<revision_id>()));
          am = marking->attrs.find(name);
        }

      I(am != marking->attrs.end());
      am->second.clear();
      am->second.insert(rid);
    }

  private:
    revision_id const & rid;
    // markings starts out as the parent's markings
    marking_map & markings;
  };

} // anonymous namespace

// Interface note: make_roster_for_merge and make_roster_for_nonmerge
// each exist in two variants:
//
// 1. A variant that does all of the actual work, taking every single
//    relevant base-level data object as a separate argument.  This
//    variant is called directly by the unit tests, and also by variant 2.
//    It is defined in this file.
//
// 2. A variant that takes a revision object, a revision ID, a database,
//    and a node_id_source.  This variant uses those four things to look
//    up all of the low-level data required by variant 1, then calls
//    variant 1 to get the real work done.  This is the one called by
//    (one variant of) make_roster_for_revision.
//    It, and make_roster_for_revision, is defined in ancestry.cc.

// yes, this function takes 14 arguments.  I'm very sorry.
void
make_roster_for_merge(revision_id const & left_rid,
                      roster_t const & left_roster,
                      marking_map const & left_markings,
                      cset const & left_cs,
                      set<revision_id> const & left_uncommon_ancestors,

                      revision_id const & right_rid,
                      roster_t const & right_roster,
                      marking_map const & right_markings,
                      cset const & right_cs,
                      set<revision_id> const & right_uncommon_ancestors,

                      revision_id const & new_rid,
                      roster_t & new_roster,
                      marking_map & new_markings,
                      node_id_source & nis)
{
  I(!null_id(left_rid) && !null_id(right_rid));
  I(left_uncommon_ancestors.find(left_rid) != left_uncommon_ancestors.end());
  I(left_uncommon_ancestors.find(right_rid) == left_uncommon_ancestors.end());
  I(right_uncommon_ancestors.find(right_rid) != right_uncommon_ancestors.end());
  I(right_uncommon_ancestors.find(left_rid) == right_uncommon_ancestors.end());
  MM(left_rid);
  MM(left_roster);
  MM(left_markings);
  MM(left_cs);
  MM(left_uncommon_ancestors);
  MM(right_rid);
  MM(right_roster);
  MM(right_markings);
  MM(right_cs);
  MM(right_uncommon_ancestors);
  MM(new_rid);
  MM(new_roster);
  MM(new_markings);
  {
    temp_node_id_source temp_nis;
    new_roster = left_roster;
    roster_t from_right_r(right_roster);
    MM(from_right_r);

    editable_roster_for_merge from_left_er(new_roster, temp_nis);
    editable_roster_for_merge from_right_er(from_right_r, temp_nis);

    left_cs.apply_to(from_left_er);
    right_cs.apply_to(from_right_er);

    unify_rosters(new_roster, from_left_er.new_nodes,
                  from_right_r, from_right_er.new_nodes,
                  nis);

    // Kluge: If both csets have no content changes, and the node_id_source
    // passed to this function is a temp_node_id_source, then we are being
    // called from get_current_roster_shape, and we should not attempt to
    // verify that these rosters match as far as content IDs.
    if (left_cs.deltas_applied.empty()
        && right_cs.deltas_applied.empty()
        && typeid(nis) == typeid(temp_node_id_source))
      I(equal_shapes(new_roster, from_right_r));
    else
      I(new_roster == from_right_r);
  }

  // SPEEDUP?: instead of constructing new marking from scratch, track which
  // nodes were modified, and scan only them
  // load one of the parent markings directly into the new marking map
  new_markings.clear();
  mark_merge_roster(left_roster, left_markings, left_uncommon_ancestors,
                    right_roster, right_markings, right_uncommon_ancestors,
                    new_rid, new_roster, new_markings);
}



// Warning: this function expects the parent's roster and markings in the
// 'new_roster' and 'new_markings' parameters, and they are modified
// destructively!
// This function performs an almost identical task to
// mark_roster_with_one_parent; however, for efficiency, it is implemented
// in a different, destructive way.
void
make_roster_for_nonmerge(cset const & cs,
                         revision_id const & new_rid,
                         roster_t & new_roster, marking_map & new_markings,
                         node_id_source & nis)
{
  editable_roster_for_nonmerge er(new_roster, nis, new_rid, new_markings);
  cs.apply_to(er);
}


void
mark_roster_with_no_parents(revision_id const & rid,
                            roster_t const & roster,
                            marking_map & markings)
{
  roster_t mock_parent;
  marking_map mock_parent_markings;
  mark_roster_with_one_parent(mock_parent, mock_parent_markings,
                              rid, roster, markings);
}

void
mark_roster_with_one_parent(roster_t const & parent,
                            marking_map const & parent_markings,
                            revision_id const & child_rid,
                            roster_t const & child,
                            marking_map & child_markings)
{
  MM(parent);
  MM(parent_markings);
  MM(child_rid);
  MM(child);
  MM(child_markings);

  I(!null_id(child_rid));
  child_markings = parent_markings;

  for (node_map::const_iterator i = child.all_nodes().begin();
       i != child.all_nodes().end(); ++i)
    {
      marking_t new_marking;
      if (parent.has_node(i->first))
        mark_unmerged_node(parent_markings.get_marking(i->first),
                           parent.get_node(i->first),
                           child_rid, i->second, child_markings);
      else
        mark_new_node(child_rid, i->second, child_markings);
    }
  drop_extra_markings(child, child_markings);

  child.check_sane_against(child_markings, true);
}


////////////////////////////////////////////////////////////////////
//   Calculation of a cset
////////////////////////////////////////////////////////////////////


namespace
{

  void delta_only_in_from(roster_t const & from,
                          node_id nid, node_t n,
                          cset & cs)
  {
    file_path pth;
    from.get_name(nid, pth);
    safe_insert(cs.nodes_deleted, pth);
  }


  void delta_only_in_to(roster_t const & to, node_id nid, node_t n,
                        cset & cs)
  {
    file_path pth;
    to.get_name(nid, pth);
    if (is_file_t(n))
      {
        safe_insert(cs.files_added,
                    make_pair(pth, downcast_to_file_t(n)->content));
      }
    else
      {
        safe_insert(cs.dirs_added, pth);
      }
    for (attr_map_t::const_iterator i = n->attrs.begin();
         i != n->attrs.end(); ++i)
      if (i->second.first)
        safe_insert(cs.attrs_set,
                    make_pair(make_pair(pth, i->first), i->second.second));
  }

  void delta_in_both(node_id nid,
                     roster_t const & from, node_t from_n,
                     roster_t const & to, node_t to_n,
                     cset & cs)
  {
    I(same_type(from_n, to_n));
    I(from_n->self == to_n->self);

    if (shallow_equal(from_n, to_n, false))
      return;

    file_path from_p, to_p;
    from.get_name(nid, from_p);
    to.get_name(nid, to_p);

    // Compare name and path.
    if (from_n->name != to_n->name || from_n->parent != to_n->parent)
      safe_insert(cs.nodes_renamed, make_pair(from_p, to_p));

    // Compare file content.
    if (is_file_t(from_n))
      {
        file_t from_f = downcast_to_file_t(from_n);
        file_t to_f = downcast_to_file_t(to_n);
        if (!(from_f->content == to_f->content))
          {
            safe_insert(cs.deltas_applied,
                        make_pair(to_p, make_pair(from_f->content,
                                                   to_f->content)));
          }
      }

    // Compare attrs.
    {
      parallel::iter<attr_map_t> i(from_n->attrs, to_n->attrs);
      while (i.next())
        {
          MM(i);
          if ((i.state() == parallel::in_left
               || (i.state() == parallel::in_both && !i.right_data().first))
              && i.left_data().first)
            {
              safe_insert(cs.attrs_cleared,
                          make_pair(to_p, i.left_key()));
            }
          else if ((i.state() == parallel::in_right
                    || (i.state() == parallel::in_both && !i.left_data().first))
                   && i.right_data().first)
            {
              safe_insert(cs.attrs_set,
                          make_pair(make_pair(to_p, i.right_key()),
                                    i.right_data().second));
            }
          else if (i.state() == parallel::in_both
                   && i.right_data().first
                   && i.left_data().first
                   && i.right_data().second != i.left_data().second)
            {
              safe_insert(cs.attrs_set,
                          make_pair(make_pair(to_p, i.right_key()),
                                    i.right_data().second));
            }
        }
    }
  }
}

void
make_cset(roster_t const & from, roster_t const & to, cset & cs)
{
  cs.clear();
  parallel::iter<node_map> i(from.all_nodes(), to.all_nodes());
  while (i.next())
    {
      MM(i);
      switch (i.state())
        {
        case parallel::invalid:
          I(false);

        case parallel::in_left:
          // deleted
          delta_only_in_from(from, i.left_key(), i.left_data(), cs);
          break;

        case parallel::in_right:
          // added
          delta_only_in_to(to, i.right_key(), i.right_data(), cs);
          break;

        case parallel::in_both:
          // moved/renamed/patched/attribute changes
          delta_in_both(i.left_key(), from, i.left_data(), to, i.right_data(), cs);
          break;
        }
    }
}


// we assume our input is sane
bool
equal_up_to_renumbering(roster_t const & a, marking_map const & a_markings,
                        roster_t const & b, marking_map const & b_markings)
{
  if (a.all_nodes().size() != b.all_nodes().size())
    return false;

  for (node_map::const_iterator i = a.all_nodes().begin();
       i != a.all_nodes().end(); ++i)
    {
      file_path p;
      a.get_name(i->first, p);
      if (!b.has_node(p))
        return false;
      const_node_t b_n = b.get_node(p);
      // we already know names are the same
      if (!same_type(i->second, b_n))
        return false;
      if (i->second->attrs != b_n->attrs)
        return false;
      if (is_file_t(i->second))
        {
          if (!(downcast_to_file_t(i->second)->content
                == downcast_to_file_t(b_n)->content))
            return false;
        }
      // nodes match, check the markings too
      const_marking_t am = a_markings.get_marking(i->first);
      const_marking_t bm = b_markings.get_marking(b_n->self);
      if (!(am == bm) && !(*am == *bm))
        {
          return false;
        }
    }
  return true;
}

static void
select_restricted_nodes(roster_t const & from, roster_t const & to,
                        node_restriction const & mask,
                        map<node_id, node_t> & selected)
{
  selected.clear();
  parallel::iter<node_map> i(from.all_nodes(), to.all_nodes());
  while (i.next())
    {
      MM(i);

      switch (i.state())
        {
        case parallel::invalid:
          I(false);

        case parallel::in_left:
          // deleted
          if (!mask.includes(from, i.left_key()))
            selected.insert(make_pair(i.left_key(), i.left_data()));
          break;

        case parallel::in_right:
          // added
          if (mask.includes(to, i.right_key()))
            selected.insert(make_pair(i.right_key(), i.right_data()));
          break;

        case parallel::in_both:
          // moved/renamed/patched/attribute changes
          if (mask.includes(from, i.left_key()) ||
              mask.includes(to, i.right_key()))
            selected.insert(make_pair(i.right_key(), i.right_data()));
          else
            selected.insert(make_pair(i.left_key(), i.left_data()));
          break;
        }
    }
}

void
make_restricted_roster(roster_t const & from, roster_t const & to,
                       roster_t & restricted,
                       node_restriction const & mask)
{
  MM(from);
  MM(to);
  MM(restricted);

  I(restricted.all_nodes().empty());

  map<node_id, node_t> selected;

  select_restricted_nodes(from, to, mask, selected);

  int problems = 0;

  while (!selected.empty())
    {
      map<node_id, node_t>::const_iterator n = selected.begin();

      L(FL("selected node %d %s parent %d")
            % n->second->self
            % n->second->name
            % n->second->parent);

      bool missing_parent = false;

      while (!null_node(n->second->parent) &&
             !restricted.has_node(n->second->parent))
        {
          // we can't add this node until its parent has been added

          L(FL("deferred node %d %s parent %d")
            % n->second->self
            % n->second->name
            % n->second->parent);

          map<node_id, node_t>::const_iterator
            p = selected.find(n->second->parent);

          if (p != selected.end())
            {
              n = p; // see if we can add the parent
              I(is_dir_t(n->second));
            }
          else
            {
              missing_parent = true;
              break;
            }
        }

      if (!missing_parent)
        {
          L(FL("adding node %d %s parent %d")
            % n->second->self
            % n->second->name
            % n->second->parent);

          if (is_file_t(n->second))
            {
              file_t const f = downcast_to_file_t(n->second);
              restricted.create_file_node(f->content, f->self);
            }
          else
            restricted.create_dir_node(n->second->self);

          node_t added = restricted.get_node_for_update(n->second->self);
          added->attrs = n->second->attrs;

          restricted.attach_node(n->second->self, n->second->parent, n->second->name);
        }
      else if (from.has_node(n->second->parent) && !to.has_node(n->second->parent))
        {
          // included a delete that must be excluded
          file_path self, parent;
          from.get_name(n->second->self, self);
          from.get_name(n->second->parent, parent);
          W(F("restriction includes deletion of '%s' "
              "but excludes deletion of '%s'")
            % parent % self);
          problems++;
        }
      else if (!from.has_node(n->second->parent) && to.has_node(n->second->parent))
        {
          // excluded an add that must be included
          file_path self, parent;
          to.get_name(n->second->self, self);
          to.get_name(n->second->parent, parent);
          W(F("restriction excludes addition of '%s' "
              "but includes addition of '%s'")
            % parent % self);
          problems++;
        }
      else
        I(false); // something we missed?!?

      selected.erase(n->first);
    }


  // we cannot call restricted.check_sane(true) unconditionally because the
  // restricted roster is very possibly *not* sane. for example, if we run
  // the following in a new unversioned directory the from, to and
  // restricted rosters will all be empty and thus not sane.
  //
  // mtn setup .
  // mtn status
  //
  // several tests do this and it seems entirely reasonable. we first check
  // that the restricted roster is not empty and only then require it to be
  // sane.

  if (!restricted.all_nodes().empty() && !restricted.has_root())
   {
     W(F("restriction excludes addition of root directory"));
     problems++;
   }

  E(problems == 0, origin::user, F("invalid restriction"));

  if (!restricted.all_nodes().empty())
    restricted.check_sane(true);

}

void
select_nodes_modified_by_cset(cset const & cs,
                              roster_t const & old_roster,
                              roster_t const & new_roster,
                              set<node_id> & nodes_modified)
{
  nodes_modified.clear();

  set<file_path> modified_prestate_nodes;
  set<file_path> modified_poststate_nodes;

  // Pre-state damage

  copy(cs.nodes_deleted.begin(), cs.nodes_deleted.end(),
       inserter(modified_prestate_nodes, modified_prestate_nodes.begin()));

  for (map<file_path, file_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    modified_prestate_nodes.insert(i->first);

  // Post-state damage

  copy(cs.dirs_added.begin(), cs.dirs_added.end(),
       inserter(modified_poststate_nodes, modified_poststate_nodes.begin()));

  for (map<file_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    modified_poststate_nodes.insert(i->first);

  for (map<file_path, file_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    modified_poststate_nodes.insert(i->second);

  for (map<file_path, pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    modified_poststate_nodes.insert(i->first);

  for (set<pair<file_path, attr_key> >::const_iterator i = cs.attrs_cleared.begin();
       i != cs.attrs_cleared.end(); ++i)
    modified_poststate_nodes.insert(i->first);

  for (map<pair<file_path, attr_key>, attr_value>::const_iterator i = cs.attrs_set.begin();
       i != cs.attrs_set.end(); ++i)
    modified_poststate_nodes.insert(i->first.first);

  // Finale

  for (set<file_path>::const_iterator i = modified_prestate_nodes.begin();
       i != modified_prestate_nodes.end(); ++i)
    {
      I(old_roster.has_node(*i));
      nodes_modified.insert(old_roster.get_node(*i)->self);
    }

  for (set<file_path>::const_iterator i = modified_poststate_nodes.begin();
       i != modified_poststate_nodes.end(); ++i)
    {
      I(new_roster.has_node(*i));
      nodes_modified.insert(new_roster.get_node(*i)->self);
    }

}

void
roster_t::get_file_details(node_id nid,
                           file_id & fid,
                           file_path & pth) const
{
  I(has_node(nid));
  const_file_t f = downcast_to_file_t(get_node(nid));
  fid = f->content;
  get_name(nid, pth);
}

void
roster_t::extract_path_set(set<file_path> & paths) const
{
  paths.clear();
  if (has_root())
    {
      for (dfs_iter i(root_dir, true); !i.finished(); ++i)
        {
          file_path pth = file_path_internal(i.path());
          if (!pth.empty())
            paths.insert(pth);
        }
    }
}

// ??? make more similar to the above (member function, use dfs_iter)
void
get_content_paths(roster_t const & roster, map<file_id, file_path> & paths)
{
  node_map const & nodes = roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      const_node_t node = roster.get_node(i->first);
      if (is_file_t(node))
        {
          file_path p;
          roster.get_name(i->first, p);
          const_file_t file = downcast_to_file_t(node);
          paths.insert(make_pair(file->content, p));
        }
    }
}

////////////////////////////////////////////////////////////////////
//   I/O routines
////////////////////////////////////////////////////////////////////

void
append_with_escaped_quotes(string & collection, string const & item)
{
  size_t mark = 0;
  size_t cursor = item.find('"', mark);
  while (cursor != string::npos)
    {
      collection.append(item, mark, cursor - mark);
      collection.append(1, '\\');
      mark = cursor;
      if (mark == item.size())
        {
          cursor = string::npos;
        }
      else
        {
          cursor = item.find('"', mark + 1);
        }
    }
  collection.append(item, mark, item.size() - mark + 1);
}

void
push_marking(string & contents,
             bool is_file,
             const_marking_t const & mark,
             int symbol_length)
{

  I(!null_id(mark->birth_revision));

  contents.append(symbol_length - 5, ' ');
  contents.append("birth [");
  contents.append(encode_hexenc(mark->birth_revision.inner()(), origin::internal));
  contents.append("]\n");

  for (set<revision_id>::const_iterator i = mark->parent_name.begin();
       i != mark->parent_name.end(); ++i)
    {
      contents.append(symbol_length - 9, ' ');
      contents.append("path_mark [");
      contents.append(encode_hexenc(i->inner()(), origin::internal));
      contents.append("]\n");
    }

  if (is_file)
    {
      for (set<revision_id>::const_iterator i = mark->file_content.begin();
           i != mark->file_content.end(); ++i)
        {
          contents.append("content_mark [");// always the longest symbol
          contents.append(encode_hexenc(i->inner()(), origin::internal));
          contents.append("]\n");
        }
    }
  else
    I(mark->file_content.empty());

  for (map<attr_key, set<revision_id> >::const_iterator i = mark->attrs.begin();
       i != mark->attrs.end(); ++i)
    {
      for (set<revision_id>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        {
          contents.append(symbol_length - 9, ' ');
          contents.append("attr_mark \"");
          append_with_escaped_quotes(contents, i->first());
          contents.append("\" [");
          contents.append(encode_hexenc(j->inner()(), origin::internal));
          contents.append("]\n");
        }
    }
}


void
parse_marking(basic_io::parser & pa,
              marking_t & marking)
{
  while (pa.symp())
    {
      string rev;
      if (pa.symp(syms::birth))
        {
          pa.sym();
          pa.hex(rev);
          marking->birth_revision =
            decode_hexenc_as<revision_id>(rev, pa.tok.in.made_from);
        }
      else if (pa.symp(syms::path_mark))
        {
          pa.sym();
          pa.hex(rev);
          safe_insert(marking->parent_name,
                      decode_hexenc_as<revision_id>(rev, pa.tok.in.made_from));
        }
      else if (pa.symp(basic_io::syms::content_mark))
        {
          pa.sym();
          pa.hex(rev);
          safe_insert(marking->file_content,
                      decode_hexenc_as<revision_id>(rev, pa.tok.in.made_from));
        }
      else if (pa.symp(syms::attr_mark))
        {
          string k;
          pa.sym();
          pa.str(k);
          pa.hex(rev);
          attr_key key = attr_key(k, pa.tok.in.made_from);
          safe_insert(marking->attrs[key],
                      decode_hexenc_as<revision_id>(rev, pa.tok.in.made_from));
        }
      else break;
    }
}

// SPEEDUP?: hand-writing a parser for manifests was a measurable speed win,
// and the original parser was much simpler than basic_io.  After benchmarking
// consider replacing the roster disk format with something that can be
// processed more efficiently.

void
roster_t::print_to(data & dat,
                   marking_map const & mm,
                   bool print_local_parts) const
{
  string contents;
  I(has_root());

  // approximate byte counts
  // a file is name + content (+ birth + path-mark + content-mark + ident)
  //   2 sym + name + hash (+ 4 sym + 3 hash + 1 num)
  //   24 + name + 43 (+48 + 129 + 10) = 67 + name (+ 187) = ~100 (+ 187)
  // a dir is name (+ birth + path-mark + ident)
  //   1 sym + name (+ 3 sym + 2 hash + 1 num)
  //   12 + name (+ 36 + 86 + 10) = 12 + name (+ 132) = ~52 (+ 132)
  // an attr is name/value (+ name/mark)
  //   1 sym + 2 name (+ 1 sym + 1 name + 1 hash)
  //   12 + 2 name (+ 12 + 43 + name) = ~40 (+ ~70)
  // in the monotone tree, there are about 2% as many attrs as nodes

  if (print_local_parts)
    {
      contents.reserve(nodes.size() * (290 + 0.02 * 110) * 1.1);
    }
  else
    {
      contents.reserve(nodes.size() * (100 + 0.02 * 40) * 1.1);
    }

  // symbols are:
  //   birth        (all local)
  //   dormant_attr (any local)
  //   ident        (all local)
  //   path_mark    (all local)
  //   attr_mark    (any local)
  //   dir          (all dir)
  //   file         (all file)
  //   content      (all file)
  //   attr         (any)
  //   content_mark (all local file)

  // local  file : symbol length 12
  // local  dir  : symbol length 9 or 12 (if dormant_attr)
  // public file : symbol length 7
  // public dir  : symbol length 3 or 4 (if attr)

  contents += "format_version \"1\"\n";

  for (dfs_iter i(root_dir, true); !i.finished(); ++i)
    {
      contents += "\n";

      const_node_t curr = *i;

      int symbol_length = 0;

      if (is_dir_t(curr))
        {
          if (print_local_parts)
            {
              symbol_length = 9;
              // unless we have a dormant attr
              for (attr_map_t::const_iterator j = curr->attrs.begin();
                   j != curr->attrs.end(); ++j)
                {
                  if (!j->second.first)
                    {
                      symbol_length = 12;
                      break;
                    }
                }
            }
          else
            {
              symbol_length = 3;
              // unless we have a live attr
              for (attr_map_t::const_iterator j = curr->attrs.begin();
                   j != curr->attrs.end(); ++j)
                {
                  if (j->second.first)
                    {
                      symbol_length = 4;
                      break;
                    }
                }
            }
          contents.append(symbol_length - 3, ' ');
          contents.append("dir \"");
          append_with_escaped_quotes(contents, i.path());
          contents.append("\"\n");
        }
      else
        {
          if (print_local_parts)
            {
              symbol_length = 12;
            }
          else
            {
              symbol_length = 7;
            }
          const_file_t ftmp = downcast_to_file_t(curr);

          contents.append(symbol_length - 4, ' ');
          contents.append("file \"");
          append_with_escaped_quotes(contents, i.path());
          contents.append("\"\n");

          contents.append(symbol_length - 7, ' ');
          contents.append("content [");
          contents.append(encode_hexenc(ftmp->content.inner()(), origin::internal));
          contents.append("]\n");
        }

      if (print_local_parts)
        {
          I(curr->self != the_null_node);
          contents.append(symbol_length - 5, ' ');
          contents.append("ident \"");
          contents.append(to_string(curr->self));
          contents.append("\"\n");
        }

      // Push the non-dormant part of the attr map
      for (attr_map_t::const_iterator j = curr->attrs.begin();
           j != curr->attrs.end(); ++j)
        {
          if (j->second.first)
            {
              // L(FL("printing attr %s : %s = %s") % fp % j->first % j->second);

              contents.append(symbol_length - 4, ' ');
              contents.append("attr \"");
              append_with_escaped_quotes(contents, j->first());
              contents.append("\" \"");
              append_with_escaped_quotes(contents, j->second.second());
              contents.append("\"\n");
            }
        }

      if (print_local_parts)
        {
          // Push the dormant part of the attr map
          for (attr_map_t::const_iterator j = curr->attrs.begin();
               j != curr->attrs.end(); ++j)
            {
              if (!j->second.first)
                {
                  I(j->second.second().empty());

                  contents.append("dormant_attr \""); // always the longest sym
                  append_with_escaped_quotes(contents, j->first());
                  contents.append("\"\n");
                }
            }

          const_marking_t m = mm.get_marking(curr->self);
          push_marking(contents, is_file_t(curr), m, symbol_length);
        }
    }
  dat = data(contents, origin::internal);
}

inline size_t
read_num(string const & s)
{
  size_t n = 0;

  for (string::const_iterator i = s.begin(); i != s.end(); i++)
    {
      I(*i >= '0' && *i <= '9');
      n *= 10;
      n += static_cast<size_t>(*i - '0');
    }
  return n;
}

void
roster_t::parse_from(basic_io::parser & pa,
                     marking_map & mm)
{
  // Instantiate some lookaside caches to ensure this roster reuses
  // string storage across ATOMIC elements.
  id::symtab id_syms;
  utf8::symtab path_syms;
  attr_key::symtab attr_key_syms;
  attr_value::symtab attr_value_syms;


  // We *always* parse the local part of a roster, because we do not
  // actually send the non-local part over the network; the only times
  // we serialize a manifest (non-local roster) is when we're printing
  // it out for a user, or when we're hashing it for a manifest ID.
  nodes.clear();
  root_dir.reset();
  mm.clear();

  {
    pa.esym(basic_io::syms::format_version);
    string vers;
    pa.str(vers);
    I(vers == "1");
  }

  while(pa.symp())
    {
      string pth, ident, rev;
      node_t n;

      if (pa.symp(basic_io::syms::file))
        {
          string content;
          pa.sym();
          pa.str(pth);
          pa.esym(basic_io::syms::content);
          pa.hex(content);
          pa.esym(syms::ident);
          pa.str(ident);
          n = file_t(new file_node(read_num(ident),
                                   decode_hexenc_as<file_id>(content,
                                                             pa.tok.in.made_from)));
        }
      else if (pa.symp(basic_io::syms::dir))
        {
          pa.sym();
          pa.str(pth);
          pa.esym(syms::ident);
          pa.str(ident);
          n = dir_t(new dir_node(read_num(ident)));
        }
      else
        break;

      I(static_cast<bool>(n));

      n->cow_version = cow_version;
      I(nodes.set_if_missing(n->self, n));

      if (is_dir_t(n) && pth.empty())
        {
          I(! has_root());
          root_dir = downcast_to_dir_t(n);
        }
      else
        {
          I(!pth.empty());
          attach_node(n->self, file_path_internal(pth));
        }

      // Non-dormant attrs
      while(pa.symp(basic_io::syms::attr))
        {
          pa.sym();
          string k, v;
          pa.str(k);
          pa.str(v);
          safe_insert(n->attrs, make_pair(attr_key(k, pa.tok.in.made_from),
                                          make_pair(true,
                                                    attr_value(v, pa.tok.in.made_from))));
        }

      // Dormant attrs
      while(pa.symp(syms::dormant_attr))
        {
          pa.sym();
          string k;
          pa.str(k);
          safe_insert(n->attrs, make_pair(attr_key(k, pa.tok.in.made_from),
                                          make_pair(false, attr_value())));
        }

      {
        marking_t m(new marking());
        parse_marking(pa, m);
        mm.put_marking(n->self, m);
      }
    }
}


void
read_roster_and_marking(roster_data const & dat,
                        roster_t & ros,
                        marking_map & mm)
{
  basic_io::input_source src(dat.inner()(), "roster");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  ros.parse_from(pars, mm);
  I(src.lookahead == EOF);
  ros.check_sane_against(mm);
}


static void
write_roster_and_marking(roster_t const & ros,
                         marking_map const & mm,
                         data & dat,
                         bool print_local_parts,
                         bool do_sanity_check)
{
  if (do_sanity_check)
    {
      if (print_local_parts)
        ros.check_sane_against(mm);
      else
        ros.check_sane(true);
    }
  ros.print_to(dat, mm, print_local_parts);
}


void
write_roster_and_marking(roster_t const & ros,
                         marking_map const & mm,
                         roster_data & dat)
{
  data tmp;
  write_roster_and_marking(ros, mm, tmp, true, true);
  dat = roster_data(tmp);
}


void
write_manifest_of_roster(roster_t const & ros,
                         manifest_data & dat,
                         bool do_sanity_check)
{
  data tmp;
  marking_map mm;
  write_roster_and_marking(ros, mm, tmp, false, do_sanity_check);
  dat = manifest_data(tmp);
}

void calculate_ident(roster_t const & ros,
                     manifest_id & ident,
                     bool do_sanity_check)
{
  manifest_data tmp;
  if (!ros.all_nodes().empty())
    {
      write_manifest_of_roster(ros, tmp, do_sanity_check);
    }
  calculate_ident(tmp, ident);
}

////////////////////////////////////////////////////////////////////
//   testing
////////////////////////////////////////////////////////////////////


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
