// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// copyright (C) 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <stack>
#include <set>
#include <string>
#include <vector>
#include <sstream>

#include "app_state.hh"
#include "basic_io.hh"
#include "cset.hh"
#include "inodeprint.hh"
#include "roster.hh"
#include "revision.hh"
#include "vocab.hh"
#include "transforms.hh"
#include "parallel_iter.hh"
#include "restrictions.hh"
#include "safe_map.hh"

#include <boost/lexical_cast.hpp>

using std::inserter;
using std::make_pair;
using std::map;
using std::pair;
using std::reverse;
using std::set;
using std::set_union;
using std::stack;
using std::string;
using std::vector;
using boost::lexical_cast;


///////////////////////////////////////////////////////////////////

void
dump(full_attr_map_t const & val, std::string & out)
{
  std::ostringstream oss;
  for (full_attr_map_t::const_iterator i = val.begin(); i != val.end(); ++i)
    oss << "attr key: '" << i->first << "'\n"
        << "  status: " << (i->second.first ? "live" : "dead") << "\n"
        << "   value: '" << i->second.second << "'\n";
  out = oss.str();
}

void
dump(std::set<revision_id> const & revids, std::string & out)
{
  out.clear();
  bool first = true;
  for (std::set<revision_id>::const_iterator i = revids.begin();
       i != revids.end(); ++i)
    {
      if (!first)
        out += ", ";
      first = false;
      out += i->inner()();
    }
}

void
dump(marking_t const & marking, std::string & out)
{
  std::ostringstream oss;
  std::string tmp;
  oss << "birth_revision: " << marking.birth_revision << "\n";
  dump(marking.parent_name, tmp);
  oss << "parent_name: " << tmp << "\n";
  dump(marking.file_content, tmp);
  oss << "file_content: " << tmp << "\n";
  oss << "attrs (number: " << marking.attrs.size() << "):\n";
  for (std::map<attr_key, std::set<revision_id> >::const_iterator
         i = marking.attrs.begin(); i != marking.attrs.end(); ++i)
    {
      dump(i->second, tmp);
      oss << "  " << i->first << ": " << tmp << "\n";
    }
  out = oss.str();
}

void
dump(marking_map const & markings, std::string & out)
{
  std::ostringstream oss;
  for (marking_map::const_iterator i = markings.begin();
       i != markings.end();
       ++i)
    {
      oss << "Marking for " << i->first << ":\n";
      std::string marking_str, indented_marking_str;
      dump(i->second, marking_str);
      prefix_lines_with("    ", marking_str, indented_marking_str);
      oss << indented_marking_str << "\n";
    }
  out = oss.str();
}

namespace 
{
  //
  // We have a few concepts of "nullness" here:
  //
  // - the_null_node is a node_id. It does not correspond to a real node;
  //   it's an id you use for the parent of the root, or of any node which
  //   is detached.
  //
  // - the_null_component is a path_component. It is the *name* of the root
  //   node. Its string representation is "", the empty string. 
  //
  // - The split_path corresponding to the_null_node is [], the empty vector.
  //
  // - The split_path corresponding to the root node is [""], the 1-element
  //   vector containing the_null_component.
  //
  // - The split_path corresponding to foo/bar is ["", "foo", "bar"].
  //
  // - The only legal one-element split_path is [""], referring to the
  //   root node.
  //
  // We do this in order to support the notion of moving the root directory
  // around, or applying attributes to the root directory (though we will
  // not support moving the root at this time, since we haven't worked out
  // all the UI implications yet). 
  //


  const node_id first_node = 1;
  const node_id first_temp_node = widen<node_id, int>(1) << (sizeof(node_id) * 8 - 1);
  inline bool temp_node(node_id n)
  {
    return n & first_temp_node;
  }
}


node::node(node_id i)
  : self(i),    
    parent(the_null_node),
    name(the_null_component)
{
}


node::node()
  : self(the_null_node),
    parent(the_null_node), 
    name(the_null_component)
{
}


dir_node::dir_node(node_id i)
  : node(i)
{
}


dir_node::dir_node()
  : node()
{
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
  I(null_name(child->name));
  safe_insert(children, make_pair(pc, child));
  child->parent = this->self;
  child->name = pc;
}


node_t 
dir_node::detach_child(path_component const & pc)
{
  node_t n = get_child(pc);
  n->parent = the_null_node;
  n->name = the_null_component;
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
}


file_node::file_node()
  : node()
{
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

void
dump(node_t const & n, std::string & out)
{
  std::ostringstream oss;
  std::string name;
  dump(n->name, name);
  oss << "address: " << n << " (uses: " << n.use_count() << ")\n"
      << "self: " << n->self << "\n"
      << "parent: " << n->parent << "\n"
      << "name: " << name << "\n";
  std::string attr_map_s;
  dump(n->attrs, attr_map_s);
  oss << "attrs:\n" << attr_map_s;
  oss << "type: ";
  if (is_file_t(n))
    oss << "file\n"
        << "content: " << downcast_to_file_t(n)->content << "\n";
  else
    {
      oss << "dir\n";
      dir_map const & c = downcast_to_dir_t(n)->children;
      oss << "children: " << c.size() << "\n";
      for (dir_map::const_iterator i = c.begin(); i != c.end(); ++i)
        {
          dump(i->first, name);
          oss << "  " << name << " -> " << i->second << "\n";
        }
    }
  out = oss.str();
}

// helper
void
roster_t::do_deep_copy_from(roster_t const & other)
{
  MM(*this);
  MM(other);
  I(!root_dir);
  I(nodes.empty());
  for (node_map::const_iterator i = other.nodes.begin(); i != other.nodes.end();
       ++i)
    safe_insert(nodes, std::make_pair(i->first, i->second->clone()));
  for (node_map::iterator i = nodes.begin(); i != nodes.end(); ++i)
    if (is_dir_t(i->second))
      {
        dir_map & children = downcast_to_dir_t(i->second)->children;
        for (dir_map::iterator j = children.begin(); j != children.end(); ++j)
          j->second = safe_get(nodes, j->second->self);
      }
  if (other.root_dir)
    root_dir = downcast_to_dir_t(safe_get(nodes, other.root_dir->self));
}

roster_t::roster_t(roster_t const & other)
{
  do_deep_copy_from(other);
}

roster_t &
roster_t::operator=(roster_t const & other)
{
  root_dir.reset();
  nodes.clear();
  do_deep_copy_from(other);
  return *this;
}

void
dirname_basename(split_path const & sp,
                 split_path & dirname, path_component & basename)
{
  I(!sp.empty());
  // L(F("dirname_basename('%s' [%d components],...)\n") % file_path(sp) % sp.size());
  split_path::const_iterator penultimate = sp.begin() + (sp.size()-1);
  dirname = split_path(sp.begin(), penultimate);
  basename = *penultimate;
  if (dirname.empty())
    {
      // L(F("basename %d vs. null component %d\n") % basename % the_null_component);
      I(null_name(basename));
    }
}


struct
dfs_iter
{
  
  dir_t root;
  bool return_root;
  stack< pair<dir_t, dir_map::const_iterator> > stk;
  split_path dirname;


  dfs_iter(dir_t r) 
    : root(r), return_root(root)
  {
    if (root && !root->children.empty())
      stk.push(make_pair(root, root->children.begin()));
  }


  bool finished() const
  {
    return (!return_root) && stk.empty();
  }


  node_t operator*() const
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


  void operator++()
  {
    I(!finished());

    if (return_root)
      {
        return_root = false;
        return;
      }

    // we're not finished, so we need to set up so operator* will return the
    // right thing.
    node_t ntmp = stk.top().second->second;
    if (is_dir_t(ntmp))
      {
        dirname.push_back(stk.top().second->first);
        dir_t dtmp = downcast_to_dir_t(ntmp);
        stk.push(make_pair(dtmp, dtmp->children.begin()));
      }
    else
      ++(stk.top().second);

    while (!stk.empty()
           && stk.top().second == stk.top().first->children.end())
      {
        stk.pop();
        if (!dirname.empty())
          dirname.pop_back();
        if (!stk.empty())
          ++stk.top().second;
      }
  }
};


bool
roster_t::has_root() const
{
  return static_cast<bool>(root_dir);
}


inline bool
same_type(node_t a, node_t b)
{
  return is_file_t(a) == is_file_t(b);
}


inline bool
shallow_equal(node_t a, node_t b, 
              bool shallow_compare_dir_children)
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
      file_t fa = downcast_to_file_t(a);
      file_t fb = downcast_to_file_t(b);
      if (!(fa->content == fb->content))
        return false;     
    }
  else
    {
      dir_t da = downcast_to_dir_t(a);
      dir_t db = downcast_to_dir_t(b);

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


// FIXME_ROSTERS: why does this do two loops?  why does it pass 'true' to
// shallow_equal?
// -- njs
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


node_t
roster_t::get_node(split_path const & sp) const
{
  split_path dirname;
  path_component basename;
  dirname_basename(sp, dirname, basename);

  MM(sp);
  MM(*this);

  I(has_root());

  if (dirname.empty())
    {
      I(null_name(basename));
      return root_dir;
    }

  dir_t d = root_dir;  
  for (split_path::const_iterator i = dirname.begin()+1; i != dirname.end(); ++i)
    d = downcast_to_dir_t(d->get_child(*i));
  return d->get_child(basename);
}

bool
roster_t::has_node(node_id n) const
{
  return nodes.find(n) != nodes.end();
}

bool
roster_t::is_root(node_id n) const
{
  return has_root() && root_dir->self == n;
}

bool
roster_t::has_node(split_path const & sp) const
{
  split_path dirname;
  path_component basename;
  dirname_basename(sp, dirname, basename);

  if (dirname.empty())
    {
      I(null_name(basename));
      return has_root();
    }

  // If we have no root, we *definitely* don't have a non-root path
  if (!has_root())
    return false;
    
  dir_t d = root_dir;  
  for (split_path::const_iterator i = dirname.begin()+1; i != dirname.end(); ++i)
    {
      if (d->children.find(*i) == d->children.end())
        return false;
      d = downcast_to_dir_t(d->get_child(*i));
    }
  return d->children.find(basename) != d->children.end();
}



node_t
roster_t::get_node(node_id nid) const
{
  return safe_get(nodes, nid);
}


void
roster_t::get_name(node_id nid, split_path & sp) const
{
  I(!null_node(nid));
  sp.clear();
  while (!null_node(nid))
    {
      node_t n = get_node(nid);
      sp.push_back(n->name);
      nid = n->parent;
    }
  reverse(sp.begin(), sp.end());
}


void 
roster_t::replace_node_id(node_id from, node_id to)
{
  I(!null_node(from));
  I(!null_node(to));
  node_t n = get_node(from);
  safe_erase(nodes, from);
  safe_insert(nodes, make_pair(to, n));
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
roster_t::detach_node(split_path const & pth)
{
  split_path dirname;
  path_component basename;
  dirname_basename(pth, dirname, basename);

  if (dirname.empty())
    {
      // detaching the root dir
      {
        // detaching the root dir is currently forbidden.
        I(false);
      }
      I(null_name(basename));
      node_id root_id = root_dir->self;
      safe_insert(old_locations,
                  make_pair(root_id, make_pair(root_dir->parent, root_dir->name)));
      // clear ("reset") the root_dir shared_pointer
      root_dir.reset();
      return root_id;
    }

  dir_t parent = downcast_to_dir_t(get_node(dirname));
  node_id nid = parent->detach_child(basename)->self;
  safe_insert(old_locations,
              make_pair(nid, make_pair(parent->self, basename)));
  I(!null_node(nid));
  return nid;
}


void
roster_t::drop_detached_node(node_id nid)
{
  // ensure the node is already detached
  node_t n = get_node(nid);
  I(null_node(n->parent));
  I(null_name(n->name));
  // if it's a dir, make sure it's empty
  if (is_dir_t(n))
    I(downcast_to_dir_t(n)->children.empty());
  // all right, kill it
  safe_erase(nodes, nid);
  // can use safe_erase here, because while not every detached node appears in
  // old_locations, all those that used to be in the tree do.  and you should
  // only ever be dropping nodes that were detached, not nodes that you just
  // created and that have never been attached.
  safe_erase(old_locations, nid);
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
  safe_insert(nodes, make_pair(nid, d));
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
  safe_insert(nodes, make_pair(nid, f));
}

void
roster_t::attach_node(node_id nid, split_path const & dst)
{
  split_path dirname;
  path_component basename;
  dirname_basename(dst, dirname, basename);

  MM(dst);

  if (dirname.empty())
    // attaching the root node
    attach_node(nid, the_null_node, basename);
  else
    attach_node(nid, get_node(dirname)->self, basename);
}

void
roster_t::attach_node(node_id nid, node_id parent, path_component name)
{
  node_t n = get_node(nid);
  
  I(!null_node(n->self));
  // ensure the node is already detached (as best one can)
  I(null_node(n->parent));
  I(null_name(n->name));

  // this iterator might point to old_locations.end(), because old_locations
  // only includes entries for renames, not new nodes
  std::map<node_id, std::pair<node_id, path_component> >::iterator
    i = old_locations.find(nid);

  if (null_node(parent) || null_name(name))
    {
      I(null_node(parent) && null_name(name));
      I(null_node(n->parent));
      I(null_name(n->name));
      root_dir = downcast_to_dir_t(n);
      I(i == old_locations.end() || i->second != make_pair(root_dir->parent,
                                                           root_dir->name));
    }
  else
    {
      dir_t parent_n = downcast_to_dir_t(get_node(parent));
      parent_n->attach_child(name, n);
      I(i == old_locations.end() || i->second != make_pair(n->parent, n->name));
    }
  
  if (i != old_locations.end())
    old_locations.erase(i);
}

void
roster_t::apply_delta(split_path const & pth,
                      file_id const & old_id,
                      file_id const & new_id)
{
  file_t f = downcast_to_file_t(get_node(pth));
  I(f->content == old_id);
  I(!null_node(f->self));
  I(!(f->content == new_id));
  f->content = new_id;
}


void
roster_t::clear_attr(split_path const & pth,
                     attr_key const & name)
{
  set_attr(pth, name, make_pair(false, attr_value()));
}


void
roster_t::set_attr(split_path const & pth,
                   attr_key const & name,
                   attr_value const & val)
{
  set_attr(pth, name, make_pair(true, val));
}


void
roster_t::set_attr(split_path const & pth,
                   attr_key const & name,
                   pair<bool, attr_value> const & val)
{
  I(val.first || val.second().empty());
  node_t n = get_node(pth);
  I(!null_node(n->self));
  full_attr_map_t::iterator i = n->attrs.find(name);
  if (i == n->attrs.end())
    i = safe_insert(n->attrs, make_pair(name,
                                        make_pair(false, attr_value())));
  I(i->second != val);
  i->second = val;
}

void
dump(roster_t const & val, std::string & out)
{
  std::ostringstream oss;
  if (val.root_dir)
    oss << "Root node: " << val.root_dir->self << "\n"
        << "   at " << val.root_dir << ", uses: " << val.root_dir.use_count() << "\n";
  else
    oss << "root dir is NULL\n";
  for (node_map::const_iterator i = val.nodes.begin(); i != val.nodes.end(); ++i)
    {
      oss << "\nNode " << i->first << "\n";
      std::string node_s;
      dump(i->second, node_s);
      oss << node_s;
    }
  out = oss.str();
}

void
roster_t::check_sane(bool temp_nodes_ok) const
{
  I(has_root());
  node_map::const_iterator ri;

  I(old_locations.empty());

  for (ri = nodes.begin();
       ri != nodes.end();
       ++ri)
    {
      node_id nid = ri->first;
      I(!null_node(nid));
      if (!temp_nodes_ok)
        I(!temp_node(nid));
      node_t n = ri->second;
      I(n->self == nid);
      if (is_dir_t(n))
        {
          if (null_name(n->name) || null_node(n->parent))
            I(null_name(n->name) && null_node(n->parent));
          else
            I(!null_name(n->name) && !null_node(n->parent));
        }
      else
        {
          I(!null_name(n->name) && !null_node(n->parent));
          I(!null_id(downcast_to_file_t(n)->content));
        }
      for (full_attr_map_t::const_iterator i = n->attrs.begin(); i != n->attrs.end(); ++i)
        I(i->second.first || i->second.second().empty());
      if (n != root_dir)
        {
          I(!null_node(n->parent));
          I(downcast_to_dir_t(get_node(n->parent))->get_child(n->name) == n);
        }

    }

  I(has_root());
  size_t maxdepth = nodes.size(); 
  for (dfs_iter i(root_dir); !i.finished(); ++i)
    {
      I(*i == get_node((*i)->self));
      I(maxdepth-- > 0);
    }
  I(maxdepth == 0);
}

void
roster_t::check_sane_against(marking_map const & markings) const
{

  check_sane();

  node_map::const_iterator ri;
  marking_map::const_iterator mi;

  for (ri = nodes.begin(), mi = markings.begin();
       ri != nodes.end() && mi != markings.end();
       ++ri, ++mi)
    {
      I(!null_id(mi->second.birth_revision));
      I(!mi->second.parent_name.empty());

      if (is_file_t(ri->second))
        I(!mi->second.file_content.empty());
      else
        I(mi->second.file_content.empty());

      full_attr_map_t::const_iterator rai;
      std::map<attr_key, std::set<revision_id> >::const_iterator mai;
      for (rai = ri->second->attrs.begin(), mai = mi->second.attrs.begin();
           rai != ri->second->attrs.end() && mai != mi->second.attrs.end();
           ++rai, ++mai)
        {
          I(rai->first == mai->first);
          I(!mai->second.empty());
        }
      I(rai == ri->second->attrs.end() && mai == mi->second.attrs.end());
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
editable_roster_base::detach_node(split_path const & src)
{
  // L(F("detach_node('%s')") % file_path(src)); 
  return r.detach_node(src);
}

void 
editable_roster_base::drop_detached_node(node_id nid)
{
  // L(F("drop_detached_node(%d)") % nid); 
  r.drop_detached_node(nid);
}

node_id 
editable_roster_base::create_dir_node()
{
  // L(F("create_dir_node()\n")); 
  node_id n = r.create_dir_node(nis);
  // L(F("create_dir_node() -> %d\n") % n); 
  return n;
}

node_id 
editable_roster_base::create_file_node(file_id const & content)
{
  // L(F("create_file_node('%s')\n") % content); 
  node_id n = r.create_file_node(content, nis);
  // L(F("create_file_node('%s') -> %d\n") % content % n); 
  return n;
}

void 
editable_roster_base::attach_node(node_id nid, split_path const & dst)
{
  // L(F("attach_node(%d, '%s')") % nid % file_path(dst));
  MM(dst);
  MM(this->r);
  r.attach_node(nid, dst);
}

void 
editable_roster_base::apply_delta(split_path const & pth, 
                                  file_id const & old_id, 
                                  file_id const & new_id)
{
  // L(F("clear_attr('%s', '%s', '%s')") % file_path(pth) % old_id % new_id);
  r.apply_delta(pth, old_id, new_id);
}

void 
editable_roster_base::clear_attr(split_path const & pth,
                                 attr_key const & name)
{
  // L(F("clear_attr('%s', '%s')") % file_path(pth) % name);
  r.clear_attr(pth, name);
}

void 
editable_roster_base::set_attr(split_path const & pth,
                               attr_key const & name,
                               attr_value const & val)
{
  // L(F("set_attr('%s', '%s', '%s')") % file_path(pth) % name % val);
  r.set_attr(pth, name, val);
}

namespace 
{
  struct true_node_id_source 
    : public node_id_source
  {
    true_node_id_source(app_state & app) : app(app) {}
    virtual node_id next()
    {
      node_id n = app.db.next_node_id();
      I(!temp_node(n));
      return n;
    }
    app_state & app;
  };


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


  // This handles all the stuff in a_new.
  void unify_roster_oneway(roster_t & a, set<node_id> & a_new,
                           roster_t & b, set<node_id> & b_new,
                           set<node_id> & new_ids,
                           node_id_source & nis)
  {
    for (set<node_id>::const_iterator i = a_new.begin(); i != a_new.end(); ++i)
      {
        node_id const aid = *i;
        split_path sp;
        // SPEEDUP?: climb out only so far as is necessary to find a shared
        // id?  possibly faster (since usually will get a hit immediately),
        // but may not be worth the effort (since it doesn't take that long to
        // get out in any case)
        a.get_name(aid, sp);
        node_id bid = b.get_node(sp)->self;
        if (temp_node(bid))
          {
            node_id new_nid = nis.next();
            a.replace_node_id(aid, new_nid);
            b.replace_node_id(bid, new_nid);
            new_ids.insert(new_nid);
            b_new.erase(bid);
          }
        else
          {
            a.replace_node_id(aid, bid);
          }
      }
  }


  // After this, left should == right, and there should be no temporary ids.
  // Destroys sets, because that's handy (it has to scan over both, but it can
  // skip some double-scanning)
  void
  unify_rosters(roster_t & left, set<node_id> & left_new,
                roster_t & right, set<node_id> & right_new,
                // these new_ids all come from the given node_id_source
                set<node_id> & new_ids,
                node_id_source & nis)
  {
    unify_roster_oneway(left, left_new, right, right_new, new_ids, nis);
    unify_roster_oneway(right, right_new, left, left_new, new_ids, nis);
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
  mark_new_node(revision_id const & new_rid, node_t n, marking_t & new_marking)
  {
    new_marking.birth_revision = new_rid;
    I(new_marking.parent_name.empty());
    new_marking.parent_name.insert(new_rid);
    I(new_marking.file_content.empty());
    if (is_file_t(n))
      new_marking.file_content.insert(new_rid);
    I(new_marking.attrs.empty());
    set<revision_id> singleton;
    singleton.insert(new_rid);
    for (full_attr_map_t::const_iterator i = n->attrs.begin();
         i != n->attrs.end(); ++i)
      new_marking.attrs.insert(make_pair(i->first, singleton));
  }

  void
  mark_unmerged_node(marking_t const & parent_marking, node_t parent_n,
                     revision_id const & new_rid, node_t n,
                     marking_t & new_marking)
  {
    // SPEEDUP?: the common case here is that the parent and child nodes are
    // exactly identical, in which case the markings are also exactly
    // identical.  There might be a win in first doing an overall
    // comparison/copy, in case it can be better optimized as a block
    // comparison and a block copy...

    I(same_type(parent_n, n) && parent_n->self == n->self);

    new_marking.birth_revision = parent_marking.birth_revision;

    mark_unmerged_scalar(parent_marking.parent_name,
                         std::make_pair(parent_n->parent, parent_n->name),
                         new_rid,
                         std::make_pair(n->parent, n->name),
                         new_marking.parent_name);

    if (is_file_t(n))
      mark_unmerged_scalar(parent_marking.file_content,
                           downcast_to_file_t(parent_n)->content,
                           new_rid,
                           downcast_to_file_t(n)->content,
                           new_marking.file_content);

    for (full_attr_map_t::const_iterator i = n->attrs.begin();
           i != n->attrs.end(); ++i)
      {
        set<revision_id> & new_marks = new_marking.attrs[i->first];
        I(new_marks.empty());
        full_attr_map_t::const_iterator j = parent_n->attrs.find(i->first);
        if (j == parent_n->attrs.end())
          new_marks.insert(new_rid);
        else
          mark_unmerged_scalar(safe_get(parent_marking.attrs, i->first),
                               j->second,
                               new_rid, i->second, new_marks);
      }
  }

  void
  mark_merged_node(marking_t const & left_marking,
                   set<revision_id> left_uncommon_ancestors,
                   node_t ln,
                   marking_t const & right_marking,
                   set<revision_id> right_uncommon_ancestors,
                   node_t rn,
                   revision_id const & new_rid,
                   node_t n,
                   marking_t & new_marking)
  {
    I(same_type(ln, n) && same_type(rn, n));
    I(left_marking.birth_revision == right_marking.birth_revision);
    new_marking.birth_revision = left_marking.birth_revision;

    // name
    mark_merged_scalar(left_marking.parent_name, left_uncommon_ancestors,
                       std::make_pair(ln->parent, ln->name),
                       right_marking.parent_name, right_uncommon_ancestors,
                       std::make_pair(rn->parent, rn->name),
                       new_rid,
                       std::make_pair(n->parent, n->name),
                       new_marking.parent_name);
    // content
    if (is_file_t(n))
      {
        file_t f = downcast_to_file_t(n);
        file_t lf = downcast_to_file_t(ln);
        file_t rf = downcast_to_file_t(rn);
        mark_merged_scalar(left_marking.file_content, left_uncommon_ancestors,
                           lf->content,
                           right_marking.file_content, right_uncommon_ancestors,
                           rf->content,
                           new_rid, f->content, new_marking.file_content);
      }
    // attrs
    for (full_attr_map_t::const_iterator i = n->attrs.begin();
         i != n->attrs.end(); ++i)
      {
        attr_key const & key = i->first;
        full_attr_map_t::const_iterator li = ln->attrs.find(key);
        full_attr_map_t::const_iterator ri = rn->attrs.find(key);
        I(new_marking.attrs.find(key) == new_marking.attrs.end());
        // [], when used to refer to a non-existent element, default
        // constructs that element and returns a reference to it.  We make use
        // of this here.
        set<revision_id> & new_marks = new_marking.attrs[key];

        if (li == ln->attrs.end() && ri == rn->attrs.end())
          // this is a brand new attribute, never before seen
          safe_insert(new_marks, new_rid);

        else if (li != ln->attrs.end() && ri == rn->attrs.end())
          // only the left side has seen this attr before
          mark_unmerged_scalar(safe_get(left_marking.attrs, key),
                               li->second,
                               new_rid, i->second, new_marks);
        
        else if (li == ln->attrs.end() && ri != rn->attrs.end())
          // only the right side has seen this attr before
          mark_unmerged_scalar(safe_get(right_marking.attrs, key),
                               ri->second,
                               new_rid, i->second, new_marks);
        
        else
          // both sides have seen this attr before
          mark_merged_scalar(safe_get(left_marking.attrs, key),
                             left_uncommon_ancestors,
                             li->second,
                             safe_get(right_marking.attrs, key),
                             right_uncommon_ancestors,
                             ri->second,
                             new_rid, i->second, new_marks);
      }

    // some extra sanity checking -- attributes are not allowed to be deleted,
    // so we double check that they haven't.
    // SPEEDUP?: this code could probably be made more efficient -- but very
    // rarely will any node have more than, say, one attribute, so it probably
    // doesn't matter.
    for (full_attr_map_t::const_iterator i = ln->attrs.begin();
         i != ln->attrs.end(); ++i)
      I(n->attrs.find(i->first) != n->attrs.end());
    for (full_attr_map_t::const_iterator i = rn->attrs.begin();
         i != rn->attrs.end(); ++i)
      I(n->attrs.find(i->first) != n->attrs.end());
  }


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
    for (node_map::const_iterator i = merge.all_nodes().begin();
         i != merge.all_nodes().end(); ++i)
      {
        node_t const & n = i->second;
        // SPEEDUP?: instead of using find repeatedly, iterate everything in
        // parallel
        map<node_id, node_t>::const_iterator lni = left_roster.all_nodes().find(i->first);
        map<node_id, node_t>::const_iterator rni = right_roster.all_nodes().find(i->first);

        bool exists_in_left = (lni != left_roster.all_nodes().end());
        bool exists_in_right = (rni != right_roster.all_nodes().end());

        marking_t new_marking;

        if (!exists_in_left && !exists_in_right)
          mark_new_node(new_rid, n, new_marking);

        else if (!exists_in_left && exists_in_right)
          {
            node_t const & right_node = rni->second;
            marking_t const & right_marking = safe_get(right_markings, n->self);
            // must be unborn on the left (as opposed to dead)
            I(right_uncommon_ancestors.find(right_marking.birth_revision)
              != right_uncommon_ancestors.end());
            mark_unmerged_node(right_marking, right_node,
                               new_rid, n, new_marking);
          }
        else if (exists_in_left && !exists_in_right)
          {
            node_t const & left_node = lni->second;
            marking_t const & left_marking = safe_get(left_markings, n->self);
            // must be unborn on the right (as opposed to dead)
            I(left_uncommon_ancestors.find(left_marking.birth_revision)
              != left_uncommon_ancestors.end());
            mark_unmerged_node(left_marking, left_node,
                               new_rid, n, new_marking);
          }
        else
          {
            node_t const & left_node = lni->second;
            node_t const & right_node = rni->second;
            mark_merged_node(safe_get(left_markings, n->self),
                             left_uncommon_ancestors, left_node,
                             safe_get(right_markings, n->self),
                             right_uncommon_ancestors, right_node,
                             new_rid, n, new_marking);
          }

        safe_insert(new_markings, make_pair(i->first, new_marking));
      }
  }             
  
  
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

    virtual node_id detach_node(split_path const & src)
    {
      node_id nid = this->editable_roster_base::detach_node(src);
      marking_map::iterator marking = markings.find(nid);
      I(marking != markings.end());
      marking->second.parent_name.clear();
      marking->second.parent_name.insert(rid);
      return nid;
    }

    virtual void drop_detached_node(node_id nid)
    {
      this->editable_roster_base::drop_detached_node(nid);
      safe_erase(markings, nid);
    }

    virtual node_id create_dir_node()
    {
      return handle_new(this->editable_roster_base::create_dir_node());
    }

    virtual node_id create_file_node(file_id const & content)
    {
      return handle_new(this->editable_roster_base::create_file_node(content));
    }

    virtual void apply_delta(split_path const & pth,
                             file_id const & old_id, file_id const & new_id)
    {
      this->editable_roster_base::apply_delta(pth, old_id, new_id);
      node_id nid = r.get_node(pth)->self;
      marking_map::iterator marking = markings.find(nid);
      I(marking != markings.end());
      marking->second.file_content.clear();
      marking->second.file_content.insert(rid);
    }

    virtual void clear_attr(split_path const & pth, attr_key const & name)
    {
      this->editable_roster_base::clear_attr(pth, name);
      handle_attr(pth, name);
    }

    virtual void set_attr(split_path const & pth, attr_key const & name,
                          attr_value const & val)
    {
      this->editable_roster_base::set_attr(pth, name, val);
      handle_attr(pth, name);
    }

    node_id handle_new(node_id nid)
    {
      node_t n = r.get_node(nid);
      marking_t new_marking;
      mark_new_node(rid, n, new_marking);
      safe_insert(markings, make_pair(nid, new_marking));
      return nid;
    }

    void handle_attr(split_path const & pth, attr_key const & name)
    {
      node_id nid = r.get_node(pth)->self;
      marking_map::iterator marking = markings.find(nid);
      std::map<attr_key, std::set<revision_id> >::iterator am = marking->second.attrs.find(name);
      if (am == marking->second.attrs.end())
        {
          marking->second.attrs.insert(make_pair(name, set<revision_id>()));
          am = marking->second.attrs.find(name);
        }
      
      I(am != marking->second.attrs.end());
      am->second.clear();
      am->second.insert(rid);
    }
    
  private:
    revision_id const & rid;
    // markings starts out as the parent's markings
    marking_map & markings;
  };


  // yes, this function takes 14 arguments.  I'm very sorry.
  void
  make_roster_for_merge(revision_id const & left_rid,
                        roster_t const & left_roster,
                        marking_map const & left_markings,
                        cset const & left_cs,
                        std::set<revision_id> left_uncommon_ancestors,

                        revision_id const & right_rid,
                        roster_t const & right_roster,
                        marking_map const & right_markings,
                        cset const & right_cs,
                        std::set<revision_id> right_uncommon_ancestors,

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
    {
      temp_node_id_source temp_nis;
      // SPEEDUP?: the copies on the next two lines are probably the main
      // bottleneck in this code
      new_roster = left_roster;
      roster_t from_right_r(right_roster);

      editable_roster_for_merge from_left_er(new_roster, temp_nis);
      editable_roster_for_merge from_right_er(from_right_r, temp_nis);

      left_cs.apply_to(from_left_er);
      right_cs.apply_to(from_right_er);

      set<node_id> new_ids;
      unify_rosters(new_roster, from_left_er.new_nodes,
                    from_right_r, from_right_er.new_nodes,
                    new_ids, nis);
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


  // WARNING: this function is not tested directly (no unit tests).  Do not
  // put real logic in it.
  void
  make_roster_for_merge(revision_id const & left_rid, cset const & left_cs,
                        revision_id const & right_rid, cset const & right_cs,
                        revision_id const & new_rid,
                        roster_t & new_roster, marking_map & new_markings,
                        app_state & app)
  {
    I(!null_id(left_rid) && !null_id(right_rid));
    roster_t left_roster, right_roster;
    marking_map left_marking, right_marking;
    app.db.get_roster(left_rid, left_roster, left_marking);
    app.db.get_roster(right_rid, right_roster, right_marking);
    true_node_id_source tnis = true_node_id_source(app);

    set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;
    app.db.get_uncommon_ancestors(left_rid, right_rid,
                                  left_uncommon_ancestors,
                                  right_uncommon_ancestors);
    make_roster_for_merge(left_rid, left_roster, left_marking, left_cs,
                          left_uncommon_ancestors,
                          right_rid, right_roster, right_marking, right_cs,
                          right_uncommon_ancestors,
                          new_rid,
                          new_roster, new_markings,
                          tnis);
  }

  // Warning: this function expects the parent's roster and markings in the
  // 'new_roster' and 'new_markings' parameters, and they are modified
  // destructively!
  void
  make_roster_for_nonmerge(cset const & cs,
                           revision_id const & new_rid,
                           roster_t & new_roster, marking_map & new_markings,
                           node_id_source & nis)
  {
    editable_roster_for_nonmerge er(new_roster, nis, new_rid, new_markings);
    cs.apply_to(er);
  }

  // WARNING: this function is not tested directly (no unit tests).  Do not
  // put real logic in it.
  void
  make_roster_for_nonmerge(revision_id const & parent_rid,
                           cset const & parent_cs,
                           revision_id const & new_rid,
                           roster_t & new_roster, marking_map & new_markings,
                           app_state & app)
  {
    app.db.get_roster(parent_rid, new_roster, new_markings);
    true_node_id_source nis(app);
    make_roster_for_nonmerge(parent_cs, new_rid, new_roster, new_markings, nis);
  }
}

void
make_roster_for_base_plus_cset(revision_id const & base, cset const & cs,
                               revision_id const & new_rid,
                               roster_t & new_roster, marking_map & new_markings,
                               app_state & app)
{
  MM(base);
  MM(cs);
  app.db.get_roster(base, new_roster, new_markings);
  temp_node_id_source nis;
  editable_roster_for_nonmerge er(new_roster, nis, new_rid, new_markings);
  cs.apply_to(er);
}

// WARNING: this function is not tested directly (no unit tests).  Do not put
// real logic in it.
void
make_roster_for_revision(revision_set const & rev, revision_id const & new_rid,
                         roster_t & new_roster, marking_map & new_markings,
                         app_state & app)
{
  MM(rev);
  MM(new_rid);
  MM(new_roster);
  MM(new_markings);
  if (rev.edges.size() == 1)
    make_roster_for_nonmerge(edge_old_revision(rev.edges.begin()),
                             edge_changes(rev.edges.begin()),
                             new_rid, new_roster, new_markings, app);
  else if (rev.edges.size() == 2)
    {
      edge_map::const_iterator i = rev.edges.begin();
      revision_id const & left_rid = edge_old_revision(i);
      cset const & left_cs = edge_changes(i);
      ++i;
      revision_id const & right_rid = edge_old_revision(i);
      cset const & right_cs = edge_changes(i);
      make_roster_for_merge(left_rid, left_cs, right_rid, right_cs,
                            new_rid, new_roster, new_markings, app);
    }
  else
    I(false);

  new_roster.check_sane_against(new_markings);
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
    split_path sp;
    from.get_name(nid, sp);
    safe_insert(cs.nodes_deleted, sp);
  }


  void delta_only_in_to(roster_t const & to, node_id nid, node_t n,
                        cset & cs)
  {
    split_path sp;
    to.get_name(nid, sp);
    if (is_file_t(n))
      {
        safe_insert(cs.files_added,
                    make_pair(sp, downcast_to_file_t(n)->content));
      }
    else
      {
        safe_insert(cs.dirs_added, sp);
      }
    for (full_attr_map_t::const_iterator i = n->attrs.begin(); 
         i != n->attrs.end(); ++i)
      if (i->second.first)
        safe_insert(cs.attrs_set,
                    make_pair(make_pair(sp, i->first), i->second.second));
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

    split_path from_sp, to_sp;
    from.get_name(nid, from_sp);
    to.get_name(nid, to_sp);

    // Compare name and path.
    if (from_n->name != to_n->name || from_n->parent != to_n->parent)
      safe_insert(cs.nodes_renamed, make_pair(from_sp, to_sp));

    // Compare file content.
    if (is_file_t(from_n))
      {
        file_t from_f = downcast_to_file_t(from_n);
        file_t to_f = downcast_to_file_t(to_n);
        if (!(from_f->content == to_f->content))
          {
            safe_insert(cs.deltas_applied,
                        make_pair(to_sp, make_pair(from_f->content,
                                                   to_f->content)));
          }
      }

    // Compare attrs.
    {
      parallel::iter<full_attr_map_t> i(from_n->attrs, to_n->attrs);
      while (i.next())
        {
          MM(i);
          if ((i.state() == parallel::in_left
               || (i.state() == parallel::in_both && !i.right_data().first))
              && i.left_data().first)
            {
              safe_insert(cs.attrs_cleared,
                          make_pair(to_sp, i.left_key()));
            }
          else if ((i.state() == parallel::in_right
                    || (i.state() == parallel::in_both && !i.left_data().first))
                   && i.right_data().first)
            {
              safe_insert(cs.attrs_set,
                          make_pair(make_pair(to_sp, i.right_key()),
                                    i.right_data().second));
            }
          else if (i.state() == parallel::in_both
                   && i.right_data().first 
                   && i.left_data().first
                   && i.right_data().second != i.left_data().second)
            {
              safe_insert(cs.attrs_set,
                          make_pair(make_pair(to_sp, i.right_key()),
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
          delta_only_in_from(from, i.left_key(), i.left_data(), cs);
          break;
 
        case parallel::in_right:
          delta_only_in_to(to, i.right_key(), i.right_data(), cs);
          break;

        case parallel::in_both:
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
      split_path sp;
      a.get_name(i->first, sp);
      if (!b.has_node(sp))
        return false;
      node_t b_n = b.get_node(sp);
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
      if (!(safe_get(a_markings, i->first) == safe_get(b_markings, b_n->self)))
        return false;
    }
  return true;
}


void make_restricted_csets(roster_t const & from, roster_t const & to,
                           cset & included, cset & excluded,
                           restriction const & mask)
{
  included.clear();
  excluded.clear();
  L(F("building restricted csets\n"));
  parallel::iter<node_map> i(from.all_nodes(), to.all_nodes());
  while (i.next())
    {
      MM(i);
      switch (i.state())
        {
        case parallel::invalid:
          I(false);

        case parallel::in_left:
          if (mask.includes(from, i.left_key()))
            {
              delta_only_in_from(from, i.left_key(), i.left_data(), included);
              L(F("included left %d\n") % i.left_key());
            }
          else
            {
              delta_only_in_from(from, i.left_key(), i.left_data(), excluded);
              L(F("excluded left %d\n") % i.left_key());
            }
          break;
 
        case parallel::in_right:
          if (mask.includes(to, i.right_key()))
            {
              delta_only_in_to(to, i.right_key(), i.right_data(), included);
              L(F("included right %d\n") % i.right_key());
            }
          else
            {
              delta_only_in_to(to, i.right_key(), i.right_data(), excluded);
              L(F("excluded right %d\n") % i.right_key());
            }
          break;

        case parallel::in_both:
          if (mask.includes(from, i.left_key()) || mask.includes(to, i.right_key()))
            {
              delta_in_both(i.left_key(), from, i.left_data(), to, i.right_data(), included);
              L(F("in both %d %d\n") % i.left_key() % i.right_key());
            }
          else
            {
              delta_in_both(i.left_key(), from, i.left_data(), to, i.right_data(), excluded);
              L(F("in both %d %d\n") % i.left_key() % i.right_key());
            }
          break;
        }
    }
}


void
select_nodes_modified_by_cset(cset const & cs,
                              roster_t const & old_roster,
                              roster_t const & new_roster,
                              std::set<node_id> & nodes_modified)
{
  nodes_modified.clear();

  path_set modified_prestate_nodes;
  path_set modified_poststate_nodes;

  // Pre-state damage

  copy(cs.nodes_deleted.begin(), cs.nodes_deleted.end(), 
       inserter(modified_prestate_nodes, modified_prestate_nodes.begin()));
  
  for (std::map<split_path, split_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    modified_prestate_nodes.insert(i->first);

  // Post-state damage

  copy(cs.dirs_added.begin(), cs.dirs_added.end(), 
       inserter(modified_poststate_nodes, modified_poststate_nodes.begin()));

  for (std::map<split_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    modified_poststate_nodes.insert(i->first);

  for (std::map<split_path, split_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    modified_poststate_nodes.insert(i->second);

  for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    modified_poststate_nodes.insert(i->first);

  for (std::set<std::pair<split_path, attr_key> >::const_iterator i = cs.attrs_cleared.begin();
       i != cs.attrs_cleared.end(); ++i)
    modified_poststate_nodes.insert(i->first);

  for (std::map<std::pair<split_path, attr_key>, attr_value>::const_iterator i = cs.attrs_set.begin();
       i != cs.attrs_set.end(); ++i)
    modified_poststate_nodes.insert(i->first.first);

  // Finale

  for (path_set::const_iterator i = modified_prestate_nodes.begin();
       i != modified_prestate_nodes.end(); ++i)
    {
      I(old_roster.has_node(*i));
      nodes_modified.insert(old_roster.get_node(*i)->self);
    }

  for (path_set::const_iterator i = modified_poststate_nodes.begin();
       i != modified_poststate_nodes.end(); ++i)
    {
      I(new_roster.has_node(*i));
      nodes_modified.insert(new_roster.get_node(*i)->self);
    }

}

////////////////////////////////////////////////////////////////////
//   getting rosters from the working copy
////////////////////////////////////////////////////////////////////

// TODO: doesn't that mean they should go in work.cc ? 
// perhaps do that after propagating back to n.v.m.experiment.rosters
// or to mainline so that diffs are more informative

inline static bool
inodeprint_unchanged(inodeprint_map const & ipm, file_path const & path) 
{
  inodeprint_map::const_iterator old_ip = ipm.find(path);
  if (old_ip != ipm.end())
    {
      hexenc<inodeprint> ip;
      if (inodeprint_file(path, ip) && ip == old_ip->second)
          return true; // unchanged
      else
          return false; // changed or unavailable
    }
  else
    return false; // unavailable
}

// TODO: unchanged, changed, missing might be better as set<node_id>

// note that this does not take a restriction because it is used only by
// automate_inventory which operates on the entire, unrestricted, working
// directory.

void 
classify_roster_paths(roster_t const & ros,
                      path_set & unchanged,
                      path_set & changed,
                      path_set & missing,
                      app_state & app)
{
  temp_node_id_source nis;
  inodeprint_map ipm;

  if (in_inodeprints_mode())
    {
      data dat;
      read_inodeprints(dat);
      read_inodeprint_map(dat, ipm);
    }

  // this code is speed critical, hence the use of inode fingerprints so be
  // careful when making changes in here and preferably do some timing tests

  if (!ros.has_root())
    return;

  node_map const & nodes = ros.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;
      node_t node = i->second;

      split_path sp;
      ros.get_name(nid, sp);

      file_path fp(sp);

      if (is_dir_t(node) || inodeprint_unchanged(ipm, fp))
        {
          // dirs don't have content changes
          unchanged.insert(sp);
        }
      else 
        {
          file_t file = downcast_to_file_t(node);
          file_id fid;
          if (ident_existing_file(fp, fid, app.lua))
            {
              if (file->content == fid)
                unchanged.insert(sp);
              else
                changed.insert(sp);
            }
          else
            {
              missing.insert(sp);
            }
        }
    }
}

void 
update_current_roster_from_filesystem(roster_t & ros, 
                                      restriction const & mask,
                                      app_state & app)
{
  temp_node_id_source nis;
  inodeprint_map ipm;

  if (in_inodeprints_mode())
    {
      data dat;
      read_inodeprints(dat);
      read_inodeprint_map(dat, ipm);
    }

  size_t missing_files = 0;

  // this code is speed critical, hence the use of inode fingerprints so be
  // careful when making changes in here and preferably do some timing tests

  if (!ros.has_root())
    return;

  node_map const & nodes = ros.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;
      node_t node = i->second;

      // Only analyze files further, not dirs.
      if (! is_file_t(node))
        continue;

      // Only analyze restriction-included files.
      if (!mask.includes(ros, nid))
        continue;

      split_path sp;
      ros.get_name(nid, sp);
      file_path fp(sp);

      // Only analyze changed files (or all files if inodeprints mode
      // is disabled).
      if (inodeprint_unchanged(ipm, fp))
        continue;

      file_t file = downcast_to_file_t(node);
      if (!ident_existing_file(fp, file->content, app.lua))
        {
          W(F("missing %s") % (fp));
          missing_files++;
        }
    }

  N(missing_files == 0, 
    F("%d missing files\n"
      "to restore consistency, on each missing file run either\n"
      "'monotone drop FILE' to remove it permanently, or\n"
      "'monotone revert FILE' to restore it\n")
    % missing_files);
}

void 
update_current_roster_from_filesystem(roster_t & ros, 
                                      app_state & app)
{
  restriction tmp;
  update_current_roster_from_filesystem(ros, tmp, app);
}

void
roster_t::extract_path_set(path_set & paths) const
{
  paths.clear();
  if (has_root())
    {
      for (dfs_iter i(root_dir); !i.finished(); ++i)
        {
          node_t curr = *i;
          split_path pth;
          get_name(curr->self, pth);
          if (pth.size() == 1)
            I(null_name(idx(pth,0)));
          else
            paths.insert(pth);
        }
    }
}


////////////////////////////////////////////////////////////////////
//   I/O routines
////////////////////////////////////////////////////////////////////


namespace
{
  namespace syms
  {
    // roster symbols
    string const dir("dir");
    string const file("file");
    string const content("content");
    string const attr("attr");
    
    // 'local' roster and marking symbols
    string const ident("ident");
    string const birth("birth");
    string const dormant_attr("dormant_attr");

    string const path_mark("path_mark");
    string const content_mark("content_mark");
    string const attr_mark("attr_mark");
  }
}


static void
push_marking(basic_io::stanza & st,
             node_t curr,
             marking_t const & mark)
{

  I(!null_id(mark.birth_revision));
  st.push_hex_pair(syms::birth, mark.birth_revision.inner()());

  for (set<revision_id>::const_iterator i = mark.parent_name.begin();
       i != mark.parent_name.end(); ++i)
    st.push_hex_pair(syms::path_mark, i->inner()());

  if (is_file_t(curr))
    {
      for (set<revision_id>::const_iterator i = mark.file_content.begin();
           i != mark.file_content.end(); ++i)
        st.push_hex_pair(syms::content_mark, i->inner()());
    }
  else
    I(mark.file_content.empty());
  
  for (full_attr_map_t::const_iterator i = curr->attrs.begin();
       i != curr->attrs.end(); ++i)
    {
      map<attr_key, std::set<revision_id> >::const_iterator am = mark.attrs.find(i->first);
      I(am != mark.attrs.end());
      for (set<revision_id>::const_iterator j = am->second.begin();
           j != am->second.end(); ++j)
        st.push_hex_triple(syms::attr_mark, i->first(), j->inner()());
    }
}


void
parse_marking(basic_io::parser & pa, 
              node_t n, 
              marking_t & marking)
{
  while (pa.symp())
    {
      string rev;
      if (pa.symp(syms::birth))
        {
          pa.sym();
          pa.hex(rev);
          marking.birth_revision = revision_id(rev);
        }
      else if (pa.symp(syms::path_mark))
        {
          pa.sym();
          pa.hex(rev);
          safe_insert(marking.parent_name, revision_id(rev));
        }
      else if (pa.symp(syms::content_mark))
        {
          pa.sym();
          pa.hex(rev);
          safe_insert(marking.file_content, revision_id(rev));
        }
      else if (pa.symp(syms::attr_mark))
        {
          string k;
          pa.sym();
          pa.str(k);
          pa.hex(rev);
          attr_key key = attr_key(k);
          I(n->attrs.find(key) != n->attrs.end());
          safe_insert(marking.attrs[key], revision_id(rev));
        }
      else break;
    }
}

// SPEEDUP?: hand-writing a parser for manifests was a measurable speed win,
// and the original parser was much simpler than basic_io.  After benchmarking
// consider replacing the roster disk format with something that can be
// processed more efficiently.

void 
roster_t::print_to(basic_io::printer & pr,
                   marking_map const & mm,
                   bool print_local_parts) const
{
  I(has_root());
  for (dfs_iter i(root_dir); !i.finished(); ++i)
    {
      node_t curr = *i;
      split_path pth;
      get_name(curr->self, pth);

      file_path fp = file_path(pth);

      basic_io::stanza st;
      if (is_dir_t(curr))
        {
          // L(F("printing dir %s\n") % fp);
          st.push_file_pair(syms::dir, fp);
        }
      else
        {
          file_t ftmp = downcast_to_file_t(curr);
          st.push_file_pair(syms::file, fp);
          st.push_hex_pair(syms::content, ftmp->content.inner()());
          // L(F("printing file %s\n") % fp);
        }

      if (print_local_parts)
        {
          I(curr->self != the_null_node);
          st.push_str_pair(syms::ident, lexical_cast<string>(curr->self));
        }

      // Push the non-dormant part of the attr map
      for (full_attr_map_t::const_iterator j = curr->attrs.begin();
           j != curr->attrs.end(); ++j)
        {
          if (j->second.first)
            {
              I(!j->second.second().empty());
              // L(F("printing attr %s : %s = %s\n") % fp % j->first % j->second);
              st.push_str_triple(syms::attr, j->first(), j->second.second());
            }
        }

      if (print_local_parts)
        {
          // Push the dormant part of the attr map
          for (full_attr_map_t::const_iterator j = curr->attrs.begin();
               j != curr->attrs.end(); ++j)
            {
              if (!j->second.first)
                {
                  I(j->second.second().empty());
                  st.push_str_pair(syms::dormant_attr, j->first());
                }
            }

          marking_map::const_iterator m = mm.find(curr->self);
          I(m != mm.end());
          push_marking(st, curr, m->second);
        }

      pr.print_stanza(st);
    }
}


void 
roster_t::parse_from(basic_io::parser & pa,
                     marking_map & mm)
{
  // Instantiate some lookaside caches to ensure this roster reuses
  // string storage across ATOMIC elements.
  id::symtab id_syms;
  path_component::symtab path_syms;
  attr_key::symtab attr_key_syms;
  attr_value::symtab attr_value_syms;


  // We *always* parse the local part of a roster, because we do not
  // actually send the non-local part over the network; the only times
  // we serialize a manifest (non-local roster) is when we're printing
  // it out for a user, or when we're hashing it for a manifest ID.
  nodes.clear();
  root_dir.reset();
  mm.clear();

  while(pa.symp())
    {
      string pth, ident, rev;
      node_t n;

      if (pa.symp(syms::file))
        {
          string content;
          pa.sym();
          pa.str(pth);
          pa.esym(syms::content);
          pa.hex(content);
          pa.esym(syms::ident);
          pa.str(ident);
          n = file_t(new file_node(lexical_cast<node_id>(ident),
                                   file_id(content)));
        }
      else if (pa.symp(syms::dir))
        {
          pa.sym();
          pa.str(pth);
          pa.esym(syms::ident);
          pa.str(ident);
          n = dir_t(new dir_node(lexical_cast<node_id>(ident)));
        }
      else 
        break;

      I(static_cast<bool>(n));

      safe_insert(nodes, make_pair(n->self, n));
      if (is_dir_t(n) && pth.empty())
        {
          I(! has_root());
          root_dir = downcast_to_dir_t(n);
        }
      else
        {
          I(!pth.empty());
          attach_node(n->self, internal_string_to_split_path(pth));
        }

      // Non-dormant attrs
      while(pa.symp(syms::attr))
        {
          pa.sym();
          string k, v;
          pa.str(k);
          pa.str(v);
          safe_insert(n->attrs, make_pair(attr_key(k),
                                          make_pair(true, attr_value(v))));
        }

      // Dormant attrs
      while(pa.symp(syms::dormant_attr))
        {
          pa.sym();
          string k;
          pa.str(k);
          safe_insert(n->attrs, make_pair(attr_key(k),
                                          make_pair(false, attr_value())));
        }

      {
        marking_t marking;
        parse_marking(pa, n, marking);
        safe_insert(mm, make_pair(n->self, marking));
      }
    }
}


void 
read_roster_and_marking(data const & dat,
                        roster_t & ros,
                        marking_map & mm)
{
  basic_io::input_source src(dat(), "roster");
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
                         bool print_local_parts)
{
  if (print_local_parts)
    ros.check_sane_against(mm);
  else
    ros.check_sane(true);
  std::ostringstream oss;
  basic_io::printer pr(oss);
  ros.print_to(pr, mm, print_local_parts);
  dat = data(oss.str());
}


void
write_roster_and_marking(roster_t const & ros,
                         marking_map const & mm,
                         data & dat)
{
  write_roster_and_marking(ros, mm, dat, true);
}


void
write_manifest_of_roster(roster_t const & ros,
                         data & dat)
{
  marking_map mm;
  write_roster_and_marking(ros, mm, dat, false);  
}


////////////////////////////////////////////////////////////////////
//   testing
////////////////////////////////////////////////////////////////////

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "sanity.hh"
#include "constants.hh"

#include <string>
#include <boost/lexical_cast.hpp>

using std::string;
using boost::lexical_cast;

static void
make_fake_marking_for(roster_t const & r, marking_map & mm)
{
  mm.clear();
  revision_id rid(std::string("0123456789abcdef0123456789abcdef01234567"));
  for (node_map::const_iterator i = r.all_nodes().begin(); i != r.all_nodes().end();
       ++i)
    {
      marking_t fake_marks;
      mark_new_node(rid, i->second, fake_marks);
      mm.insert(std::make_pair(i->first, fake_marks));
    }
}

static void
do_testing_on_one_roster(roster_t const & r)
{
  if (!r.has_root())
    {
      I(r.all_nodes().size() == 0);
      // not much testing to be done on an empty roster -- can't iterate over
      // it or read/write it.
      return;
    }

  MM(r);
  // test dfs_iter by making sure it returns the same number of items as there
  // are items in all_nodes()
  int n; MM(n);
  n = r.all_nodes().size();
  int dfs_counted = 0; MM(dfs_counted);
  split_path root_name;
  file_path().split(root_name);
  for (dfs_iter i(downcast_to_dir_t(r.get_node(root_name))); !i.finished(); ++i)
    ++dfs_counted;
  I(n == dfs_counted);

  // do a read/write spin
  data r_dat; MM(r_dat);
  marking_map fm;
  make_fake_marking_for(r, fm);
  write_roster_and_marking(r, fm, r_dat);
  roster_t r2; MM(r2);
  marking_map fm2;
  read_roster_and_marking(r_dat, r2, fm2);
  I(r == r2);
  I(fm == fm2);
  data r2_dat; MM(r2_dat);
  write_roster_and_marking(r2, fm2, r2_dat);
  I(r_dat == r2_dat);
}

static void
do_testing_on_two_equivalent_csets(cset const & a, cset const & b)
{
  // we do all this reading/writing/comparing of both strings and objects to
  // cross-check the reading, writing, and comparison logic against each
  // other.  (if, say, there is a field in cset that == forgets to check but
  // that write remembers to include, this should catch it).
  MM(a);
  MM(b);
  I(a == b);

  data a_dat, b_dat, a2_dat, b2_dat;
  MM(a_dat);
  MM(b_dat);
  MM(a2_dat);
  MM(b2_dat);

  write_cset(a, a_dat);
  write_cset(b, b_dat);
  I(a_dat == b_dat);
  cset a2, b2;
  MM(a2);
  MM(b2);
  read_cset(a_dat, a2);
  read_cset(b_dat, b2);
  I(a2 == a);
  I(b2 == b);
  I(b2 == a);
  I(a2 == b);
  I(a2 == b2);
  write_cset(a2, a2_dat);
  write_cset(b2, b2_dat);
  I(a_dat == a2_dat);
  I(b_dat == b2_dat);
}

static void
apply_cset_and_do_testing(roster_t & r, cset const & cs, node_id_source & nis)
{
  MM(r);
  MM(cs);
  roster_t original = r;
  MM(original);
  I(original == r);
  
  editable_roster_base e(r, nis);
  cs.apply_to(e);

  cset derived;
  MM(derived);
  make_cset(original, r, derived);

  do_testing_on_two_equivalent_csets(cs, derived);
  do_testing_on_one_roster(r);
}

static void
tests_on_two_rosters(roster_t const & a, roster_t const & b, node_id_source & nis)
{
  MM(a);
  MM(b);

  do_testing_on_one_roster(a);
  do_testing_on_one_roster(b);

  cset a_to_b; MM(a_to_b);
  cset b_to_a; MM(b_to_a);
  make_cset(a, b, a_to_b);
  make_cset(b, a, b_to_a);
  roster_t a2(b); MM(a2);
  roster_t b2(a); MM(b2);
  // we can't use a cset to entirely empty out a roster, so don't bother doing
  // the apply_to tests towards an empty roster
  // (NOTE: if you notice this special case in a time when root dirs can be
  // renamed or deleted, remove it, it will no longer be necessary.
  if (!a.all_nodes().empty())
    {
      editable_roster_base eb(a2, nis);
      b_to_a.apply_to(eb);
    }
  else
    a2 = a;
  if (!b.all_nodes().empty())
    {
      editable_roster_base ea(b2, nis);
      a_to_b.apply_to(ea);
    }
  else
    b2 = b;
  // We'd like to assert that a2 == a and b2 == b, but we can't, because they
  // will have new ids assigned.
  // But they _will_ have the same manifests, assuming things are working
  // correctly.
  data a_dat; MM(a_dat);
  data a2_dat; MM(a2_dat);
  data b_dat; MM(b_dat);
  data b2_dat; MM(b2_dat);
  if (a.has_root())
    write_manifest_of_roster(a, a_dat);
  if (a2.has_root())
    write_manifest_of_roster(a2, a2_dat);
  if (b.has_root())
    write_manifest_of_roster(b, b_dat);
  if (b2.has_root())
    write_manifest_of_roster(b2, b2_dat);
  I(a_dat == a2_dat);
  I(b_dat == b2_dat);

  cset a2_to_b2; MM(a2_to_b2);
  cset b2_to_a2; MM(b2_to_a2);
  make_cset(a2, b2, a2_to_b2);
  make_cset(b2, a2, b2_to_a2);
  do_testing_on_two_equivalent_csets(a_to_b, a2_to_b2);
  do_testing_on_two_equivalent_csets(b_to_a, b2_to_a2);
}

template<typename M>
typename M::const_iterator 
random_element(M const & m)
{
  size_t i = rand() % m.size();
  typename M::const_iterator j = m.begin();
  while (i > 0)
    {
      I(j != m.end());
      --i; 
      ++j;
    }
  return j;
}

bool flip(unsigned n = 2)
{
  return (rand() % n) == 0;
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

path_component new_component()
{
  split_path pieces;
  file_path_internal(new_word()).split(pieces);
  return pieces.back();
}


attr_key pick_attr(full_attr_map_t const & attrs)
{
  return random_element(attrs)->first;
}

attr_key pick_attr(attr_map_t const & attrs)
{
  return random_element(attrs)->first;
}

bool parent_of(split_path const & p,
               split_path const & c)
{
  bool is_parent = false;

  if (p.size() <= c.size())
    {
      split_path::const_iterator c_anchor = 
        search(c.begin(), c.end(),
               p.begin(), p.end());
        
      is_parent = (c_anchor == c.begin());
    }

  //     L(F("path '%s' is%s parent of '%s'")
  //       % file_path(p)
  //       % (is_parent ? "" : " not")
  //       % file_path(c));
    
  return is_parent;      
}

struct
change_automaton
{

  change_automaton()
  {
    srand(0x12345678);
  }

  void perform_random_action(roster_t & r, node_id_source & nis)
  {
    cset c;
    while (c.empty())
      {
        if (r.all_nodes().empty())
          {
            // Must add, couldn't find anything to work with
            split_path root;
            root.push_back(the_null_component);
            c.dirs_added.insert(root);
          }
        else
          {
            node_t n = random_element(r.all_nodes())->second;
            split_path pth;
            r.get_name(n->self, pth);
            // L(F("considering acting on '%s'\n") % file_path(pth));

            switch (rand() % 7)
              {
              default:
              case 0:
              case 1:
              case 2:
                if (is_file_t(n) || (pth.size() > 1 && flip()))
                  // Add a sibling of an existing entry.
                  pth[pth.size() - 1] = new_component();
                
                else 
                  // Add a child of an existing entry.
                  pth.push_back(new_component());
                
                if (flip())
                  {
                    // L(F("adding dir '%s'\n") % file_path(pth));
                    safe_insert(c.dirs_added, pth);
                  }
                else
                  {
                    // L(F("adding file '%s'\n") % file_path(pth));
                    safe_insert(c.files_added, make_pair(pth, new_ident()));
                  }
                break;

              case 3:
                if (is_file_t(n))
                  {
                    safe_insert(c.deltas_applied, 
                                make_pair
                                (pth, make_pair(downcast_to_file_t(n)->content,
                                                new_ident())));
                  }
                break;

              case 4:
                {
                  node_t n2 = random_element(r.all_nodes())->second;
                  split_path pth2;
                  r.get_name(n2->self, pth2);

                  if (n == n2)
                    continue;
                  
                  if (is_file_t(n2) || (pth2.size() > 1 && flip()))
                    {
                      // L(F("renaming to a sibling of an existing entry '%s'\n") % file_path(pth2));
                      // Move to a sibling of an existing entry.
                      pth2[pth2.size() - 1] = new_component();
                    }
                  
                  else
                    {
                      // L(F("renaming to a child of an existing entry '%s'\n") % file_path(pth2));
                      // Move to a child of an existing entry.
                      pth2.push_back(new_component());
                    }
                  
                  if (!parent_of(pth, pth2))
                    {
                      // L(F("renaming '%s' -> '%s\n") % file_path(pth) % file_path(pth2));
                      safe_insert(c.nodes_renamed, make_pair(pth, pth2));
                    }
                }
                break;
                
              case 5:
                if (!null_node(n->parent) && 
                    (is_file_t(n) || downcast_to_dir_t(n)->children.empty()))
                  {
                    // L(F("deleting '%s'\n") % file_path(pth));
                    safe_insert(c.nodes_deleted, pth);
                  }
                break;
                
              case 6:
                if (!n->attrs.empty() && flip())
                  {
                    attr_key k = pick_attr(n->attrs);
                    if (safe_get(n->attrs, k).first)
                      {
                        if (flip())
                          {
                            // L(F("clearing attr on '%s'\n") % file_path(pth));
                            safe_insert(c.attrs_cleared, make_pair(pth, k));
                          }
                        else
                          {
                            // L(F("changing attr on '%s'\n) % file_path(pth));
                            safe_insert(c.attrs_set, make_pair(make_pair(pth, k), new_word()));
                          }
                      }
                    else
                      {
                        // L(F("setting previously set attr on '%s'\n") % file_path(pth));
                        safe_insert(c.attrs_set, make_pair(make_pair(pth, k), new_word()));
                      }
                  }
                else
                  {
                    // L(F("setting attr on '%s'\n") % file_path(pth));
                    safe_insert(c.attrs_set, make_pair(make_pair(pth, new_word()), new_word()));
                  }
                break;                
              }
          }
      }
    // now do it
    apply_cset_and_do_testing(r, c, nis);
  }
};

struct testing_node_id_source 
  : public node_id_source
{
  testing_node_id_source() : curr(first_node) {}
  virtual node_id next()
  {
    // L(F("creating node %x\n") % curr);
    node_id n = curr++;
    I(!temp_node(n));
    return n;
  }
  node_id curr;
};

static void
dump(int const & i, std::string & out)
{
  out = lexical_cast<std::string>(i) + "\n";
}

static void
automaton_roster_test()
{
  roster_t r;
  change_automaton aut;
  testing_node_id_source nis;

  roster_t empty, prev;

  for (int i = 0; i < 2000; ++i)
    {
      MM(i);
      if (i % 100 == 0)
        P(F("performing random action %d\n") % i);
      // test operator==
      I(r == r);
      aut.perform_random_action(r, nis);
      if (i == 0)
        prev = r;
      else
        {
          // test operator==
          I(!(prev == r));
        }
      // some randomly made up magic numbers, just to make sure we do tests on
      // rosters that have a number of changes between them, not just a single
      // change.
      if (i == 4 || i == 50 || i == 100 || i == 200 || i == 205
          || i == 500 || i == 640 || i == 1200 || i == 1900 || i == 1910)
        {
          tests_on_two_rosters(prev, r, nis);
          tests_on_two_rosters(empty, r, nis);
          prev = r;
        }
    }
}

// some of our raising operations leave our state corrupted.  so rather than
// trying to do all the illegal things in one pass, we re-run this function a
// bunch of times, and each time we do only one of these potentially
// corrupting tests.  Test numbers are in the range [0, total).

#define MAYBE(code) if (total == to_run) { L(F(#code)); code; return; } ++total

static void
check_sane_roster_do_tests(int to_run, int& total)
{
  total = 0;
  testing_node_id_source nis;
  roster_t r;
  MM(r);
  
  // roster must have a root dir
  MAYBE(BOOST_CHECK_THROW(r.check_sane(false), std::logic_error));
  MAYBE(BOOST_CHECK_THROW(r.check_sane(true), std::logic_error));

  split_path sp_, sp_foo, sp_foo_bar, sp_foo_baz;
  file_path().split(sp_);
  file_path_internal("foo").split(sp_foo);
  file_path_internal("foo/bar").split(sp_foo_bar);
  file_path_internal("foo/baz").split(sp_foo_baz);
  node_id nid_f = r.create_file_node(file_id(std::string("0000000000000000000000000000000000000000")),
                                     nis);
  // root must be a directory, not a file
  MAYBE(BOOST_CHECK_THROW(r.attach_node(nid_f, sp_), std::logic_error));

  node_id root_dir = r.create_dir_node(nis);
  r.attach_node(root_dir, sp_);
  // has a root dir, but a detached file
  MAYBE(BOOST_CHECK_THROW(r.check_sane(false), std::logic_error));
  MAYBE(BOOST_CHECK_THROW(r.check_sane(true), std::logic_error));

  r.attach_node(nid_f, sp_foo);
  // now should be sane
  BOOST_CHECK_NOT_THROW(r.check_sane(false), std::logic_error);
  BOOST_CHECK_NOT_THROW(r.check_sane(true), std::logic_error);

  node_id nid_d = r.create_dir_node(nis);
  // if "foo" exists, can't attach another node at "foo"
  MAYBE(BOOST_CHECK_THROW(r.attach_node(nid_d, sp_foo), std::logic_error));
  // if "foo" is a file, can't attach a node at "foo/bar"
  MAYBE(BOOST_CHECK_THROW(r.attach_node(nid_d, sp_foo_bar), std::logic_error));

  BOOST_CHECK(r.detach_node(sp_foo) == nid_f);
  r.attach_node(nid_d, sp_foo);
  r.attach_node(nid_f, sp_foo_bar);
  BOOST_CHECK_NOT_THROW(r.check_sane(false), std::logic_error);
  BOOST_CHECK_NOT_THROW(r.check_sane(true), std::logic_error);

  temp_node_id_source nis_tmp;
  node_id nid_tmp = r.create_dir_node(nis_tmp);
  // has a detached node
  MAYBE(BOOST_CHECK_THROW(r.check_sane(false), std::logic_error));
  MAYBE(BOOST_CHECK_THROW(r.check_sane(true), std::logic_error));
  r.attach_node(nid_tmp, sp_foo_baz);
  // now has no detached nodes, but one temp node
  MAYBE(BOOST_CHECK_THROW(r.check_sane(false), std::logic_error));
  BOOST_CHECK_NOT_THROW(r.check_sane(true), std::logic_error);
}

#undef MAYBE

static void
check_sane_roster_test()
{
  int total;
  check_sane_roster_do_tests(-1, total);
  for (int to_run = 0; to_run < total; ++to_run)
    {
      L(F("check_sane_roster_test: loop = %i (of %i)") % to_run % (total - 1));
      int tmp;
      check_sane_roster_do_tests(to_run, tmp);
    }
}

static void
check_sane_roster_loop_test()
{
  testing_node_id_source nis;
  roster_t r; MM(r);
  split_path root, foo_bar;
  file_path().split(root);
  file_path_internal("foo/bar").split(foo_bar);
  r.attach_node(r.create_dir_node(nis), root);
  node_id nid_foo = r.create_dir_node(nis);
  node_id nid_bar = r.create_dir_node(nis);
  r.attach_node(nid_foo, nid_bar, foo_bar[1]);
  r.attach_node(nid_bar, nid_foo, foo_bar[2]);
  BOOST_CHECK_THROW(r.check_sane(true), std::logic_error);
}

static void
check_sane_roster_screwy_dir_map()
{
  testing_node_id_source nis;
  roster_t r; MM(r);
  split_path root, foo;
  file_path().split(root);
  file_path_internal("foo").split(foo);
  r.attach_node(r.create_dir_node(nis), root);
  roster_t other; MM(other);
  node_id other_nid = other.create_dir_node(nis);
  dir_t root_n = downcast_to_dir_t(r.get_node(root));
  root_n->children.insert(make_pair(*(foo.end()-1), other.get_node(other_nid)));
  BOOST_CHECK_THROW(r.check_sane(), std::logic_error);
  // well, but that one was easy, actually, because a dir traversal will hit
  // more nodes than actually exist... so let's make it harder, by making sure
  // that a dir traversal will hit exactly as many nodes as actually exist.
  node_id distractor_nid = r.create_dir_node(nis);
  BOOST_CHECK_THROW(r.check_sane(), std::logic_error);
  // and even harder, by making that node superficially valid too
  dir_t distractor_n = downcast_to_dir_t(r.get_node(distractor_nid));
  distractor_n->parent = distractor_nid;
  distractor_n->name = *(foo.end()-1);
  distractor_n->children.insert(make_pair(distractor_n->name, distractor_n));
  BOOST_CHECK_THROW(r.check_sane(), std::logic_error);
}

static void
bad_attr_test()
{
  testing_node_id_source nis;
  roster_t r; MM(r);
  split_path root;
  file_path().split(root);
  r.attach_node(r.create_dir_node(nis), root);
  BOOST_CHECK_THROW(r.set_attr(root, attr_key("test_key1"),
                               std::make_pair(false, attr_value("invalid"))),
                    std::logic_error);
  BOOST_CHECK_NOT_THROW(r.check_sane(true), std::logic_error);
  safe_insert(r.get_node(root)->attrs,
              make_pair(attr_key("test_key2"),
                        make_pair(false, attr_value("invalid"))));
  BOOST_CHECK_THROW(r.check_sane(true), std::logic_error);
}

////////////////////////////////////////////////////////////////////////
// exhaustive marking tests
////////////////////////////////////////////////////////////////////////

// The marking/roster generation code is extremely critical.  It is the very
// core of monotone's versioning technology, very complex, and bugs can result
// in corrupt and nonsensical histories (not to mention erroneous merges and
// the like).  Furthermore, the code that implements it is littered with
// case-by-case analysis, where copy-paste errors could easily occur.  So the
// purpose of this section is to systematically and exhaustively test every
// possible case.
//
// Our underlying merger, *-merge, works on scalars, case-by-case.  The cases are:
//   0 parent:
//       a*
//   1 parent:
//       a     a
//       |     |
//       a     b*
//   2 parents:
//       a   a  a   a  a   b  a   b
//        \ /    \ /    \ /    \ /
//         a      b*     c*     a?
// 
// Each node has a number of scalars associated with it:
//   * basename+parent
//   * file content (iff a file)
//   * attributes
//
// So for each scalar, we want to test each way it can appear in each of the
// above shapes.  This is made more complex by lifecycles.  We can achieve a 0
// parent node as:
//   * a node in a 0-parent roster (root revision)
//   * a newly added node in a 1-parent roster
//   * a newly added node in a 2-parent roster
// a 1 parent node as:
//   * a pre-existing node in a 1-parent roster
//   * a node in a 2-parent roster that only existed in one of the parents
// a 2 parent node as:
//   * a pre-existing node in a 2-parent roster
//
// Because the basename+parent and file_content scalars have lifetimes that
// exactly match the lifetime of the node they are on, those are all the cases
// for these scalars.  However, attrs make things a bit more complicated,
// because they can be added.  An attr can have 0 parents:
//   * in any of the above cases, with an attribute newly added on the node
// And one parent:
//   * in any of the cases above with one node parent and the attr pre-existing
//   * in a 2-parent node where the attr exists in only one of the parents
//   
// Plus, just to be sure, in the merge cases we check both the given example
// and the mirror-reversed one, since the code implementing this could
// conceivably mark merge(A, B) right but get merge(B, A) wrong.  And for the
// scalars that can appear on either files or dirs, we check both.

// The following somewhat elaborate code implements all these checks.  The
// most important background assumption to know, is that it always assumes
// (and this assumption is hard-coded in various places) that it is looking at
// one of the following topologies:
//
//     old
//
//     old
//      |
//     new
//
//     old
//     / \.
// left   right
//     \ /
//     new
//
// There is various tricksiness in making sure that the root directory always
// has the right birth_revision, that nodes are created with good birth
// revisions and sane markings on the scalars we are not interested in, etc.
// This code is ugly and messy and could use refactoring, but it seems to
// work.

////////////////
// These are some basic utility pieces handy for the exhaustive mark tests

namespace
{
  template <typename T> std::set<T>
  singleton(T const & t)
  {
    std::set<T> s;
    s.insert(t);
    return s;
  }

  template <typename T> std::set<T>
  doubleton(T const & t1, T const & t2)
  {
    std::set<T> s;
    s.insert(t1);
    s.insert(t2);
    return s;
  }

  revision_id old_rid(string("0000000000000000000000000000000000000000"));
  revision_id left_rid(string("1111111111111111111111111111111111111111"));
  revision_id right_rid(string("2222222222222222222222222222222222222222"));
  revision_id new_rid(string("4444444444444444444444444444444444444444"));

  split_path
  split(std::string const & s)
  {
    split_path sp;
    file_path_internal(s).split(sp);
    return sp;
  }

////////////////
// These classes encapsulate information about all the different scalars
// that *-merge applies to.

  typedef enum { scalar_a, scalar_b, scalar_c,
                 scalar_none, scalar_none_2 } scalar_val;

  void
  dump(scalar_val val, std::string & out)
  {
    switch (val)
      {
      case scalar_a: out = "scalar_a"; break;
      case scalar_b: out = "scalar_b"; break;
      case scalar_c: out = "scalar_c"; break;
      case scalar_none: out = "scalar_none"; break;
      case scalar_none_2: out = "scalar_none_2"; break;
      }
    out += "\n";
  }

  struct a_scalar
  {
    virtual void set(revision_id const & scalar_origin_rid,
                     scalar_val val, std::set<revision_id> const & this_scalar_mark,
                     roster_t & roster, marking_map & markings)
      = 0;
    virtual ~a_scalar() {};

    node_id_source & nis;
    node_id const root_nid;
    node_id const obj_under_test_nid;
    a_scalar(node_id_source & nis)
      : nis(nis), root_nid(nis.next()), obj_under_test_nid(nis.next())
    {}

    void setup(roster_t & roster, marking_map & markings)
    {
      roster.create_dir_node(root_nid);
      roster.attach_node(root_nid, split(""));
      marking_t marking;
      marking.birth_revision = old_rid;
      marking.parent_name.insert(old_rid);
      safe_insert(markings, make_pair(root_nid, marking));
    }

    virtual std::string my_type() const = 0;

    virtual void dump(std::string & out) const
    {
      std::ostringstream oss;
      oss << "type: " << my_type() << "\n"
          << "root_nid: " << root_nid << "\n"
          << "obj_under_test_nid: " << obj_under_test_nid << "\n";
      out = oss.str();
    }
  };

  void
  dump(a_scalar const & s, std::string & out)
  {
    s.dump(out);
  }

  struct file_maker
  {
    static void make_obj(revision_id const & scalar_origin_rid, node_id nid,
                         roster_t & roster, marking_map & markings)
    {
      make_file(scalar_origin_rid, nid,
                file_id(string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")),
                roster, markings);
    }
    static void make_file(revision_id const & scalar_origin_rid, node_id nid,
                          file_id const & fid,
                          roster_t & roster, marking_map & markings)
    {
      roster.create_file_node(fid, nid);
      marking_t marking;
      marking.birth_revision = scalar_origin_rid;
      marking.parent_name = marking.file_content = singleton(scalar_origin_rid);
      safe_insert(markings, make_pair(nid, marking));
    }
  };
  
  struct dir_maker
  {
    static void make_obj(revision_id const & scalar_origin_rid, node_id nid,
                         roster_t & roster, marking_map & markings)
    {
      roster.create_dir_node(nid);
      marking_t marking;
      marking.birth_revision = scalar_origin_rid;
      marking.parent_name = singleton(scalar_origin_rid);
      safe_insert(markings, make_pair(nid, marking));
    }
  };
  
  struct file_content_scalar : public a_scalar
  {
    virtual std::string my_type() const { return "file_content_scalar"; }
    
    std::map<scalar_val, file_id> values;
    file_content_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values,
                  make_pair(scalar_a,
                            file_id(string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"))));
      safe_insert(values,
                  make_pair(scalar_b,
                            file_id(string("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"))));
      safe_insert(values,
                  make_pair(scalar_c,
                            file_id(string("cccccccccccccccccccccccccccccccccccccccc"))));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      if (val != scalar_none)
        {
          file_maker::make_file(scalar_origin_rid, obj_under_test_nid,
                                safe_get(values, val),
                                roster, markings);
          roster.attach_node(obj_under_test_nid, split("foo"));
          markings[obj_under_test_nid].file_content = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  template <typename T>
  struct X_basename_scalar : public a_scalar
  {
    virtual std::string my_type() const { return "X_basename_scalar"; }

    std::map<scalar_val, split_path> values;
    X_basename_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, split("a")));
      safe_insert(values, make_pair(scalar_b, split("b")));
      safe_insert(values, make_pair(scalar_c, split("c")));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, safe_get(values, val));
          markings[obj_under_test_nid].parent_name = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  template <typename T>
  struct X_parent_scalar : public a_scalar
  {
    virtual std::string my_type() const { return "X_parent_scalar"; }

    std::map<scalar_val, split_path> values;
    node_id const a_nid, b_nid, c_nid;
    X_parent_scalar(node_id_source & nis)
      : a_scalar(nis), a_nid(nis.next()), b_nid(nis.next()), c_nid(nis.next())
    {
      safe_insert(values, make_pair(scalar_a, split("dir_a/foo")));
      safe_insert(values, make_pair(scalar_b, split("dir_b/foo")));
      safe_insert(values, make_pair(scalar_c, split("dir_c/foo")));
    }
    void
    setup_dirs(roster_t & roster, marking_map & markings)
    {
      roster.create_dir_node(a_nid);
      roster.attach_node(a_nid, split("dir_a"));
      roster.create_dir_node(b_nid);
      roster.attach_node(b_nid, split("dir_b"));
      roster.create_dir_node(c_nid);
      roster.attach_node(c_nid, split("dir_c"));
      marking_t marking;
      marking.birth_revision = old_rid;
      marking.parent_name.insert(old_rid);
      safe_insert(markings, make_pair(a_nid, marking));
      safe_insert(markings, make_pair(b_nid, marking));
      safe_insert(markings, make_pair(c_nid, marking));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      setup_dirs(roster, markings);
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, safe_get(values, val));
          markings[obj_under_test_nid].parent_name = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  // this scalar represents an attr whose node already exists, and we put an
  // attr on it.
  template <typename T>
  struct X_attr_existing_node_scalar : public a_scalar
  {
    virtual std::string my_type() const { return "X_attr_scalar"; }

    std::map<scalar_val, pair<bool, attr_value> > values;
    X_attr_existing_node_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, make_pair(true, attr_value("a"))));
      safe_insert(values, make_pair(scalar_b, make_pair(true, attr_value("b"))));
      safe_insert(values, make_pair(scalar_c, make_pair(true, attr_value("c"))));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      // _not_ scalar_origin_rid, because our object exists everywhere, regardless of
      // when the attr shows up
      T::make_obj(old_rid, obj_under_test_nid, roster, markings);
      roster.attach_node(obj_under_test_nid, split("foo"));
      if (val != scalar_none)
        {
          safe_insert(roster.get_node(obj_under_test_nid)->attrs,
                      make_pair(attr_key("test_key"), safe_get(values, val)));
          markings[obj_under_test_nid].attrs[attr_key("test_key")] = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };
  
  // this scalar represents an attr whose node does not exist; we create the
  // node when we create the attr.
  template <typename T>
  struct X_attr_new_node_scalar : public a_scalar
  {
    virtual std::string my_type() const { return "X_attr_scalar"; }

    std::map<scalar_val, pair<bool, attr_value> > values;
    X_attr_new_node_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, make_pair(true, attr_value("a"))));
      safe_insert(values, make_pair(scalar_b, make_pair(true, attr_value("b"))));
      safe_insert(values, make_pair(scalar_c, make_pair(true, attr_value("c"))));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, split("foo"));
          safe_insert(roster.get_node(obj_under_test_nid)->attrs,
                      make_pair(attr_key("test_key"), safe_get(values, val)));
          markings[obj_under_test_nid].attrs[attr_key("test_key")] = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  typedef std::vector<boost::shared_ptr<a_scalar> > scalars;
  scalars
  all_scalars(node_id_source & nis)
  {
    scalars ss;
    ss.push_back(boost::shared_ptr<a_scalar>(new file_content_scalar(nis)));
    ss.push_back(boost::shared_ptr<a_scalar>(new X_basename_scalar<file_maker>(nis)));
    ss.push_back(boost::shared_ptr<a_scalar>(new X_basename_scalar<dir_maker>(nis)));
    ss.push_back(boost::shared_ptr<a_scalar>(new X_parent_scalar<file_maker>(nis)));
    ss.push_back(boost::shared_ptr<a_scalar>(new X_parent_scalar<dir_maker>(nis)));
    ss.push_back(boost::shared_ptr<a_scalar>(new X_attr_existing_node_scalar<file_maker>(nis)));
    ss.push_back(boost::shared_ptr<a_scalar>(new X_attr_existing_node_scalar<dir_maker>(nis)));
    ss.push_back(boost::shared_ptr<a_scalar>(new X_attr_new_node_scalar<file_maker>(nis)));
    ss.push_back(boost::shared_ptr<a_scalar>(new X_attr_new_node_scalar<dir_maker>(nis)));
    return ss;
  }
}

////////////////
// These functions encapsulate the logic for running a particular mark
// scenario with a particular scalar with 0, 1, or 2 roster parents.

static void
run_with_0_roster_parents(a_scalar & s, revision_id scalar_origin_rid,
                          scalar_val new_val,
                          std::set<revision_id> const & new_mark_set,
                          node_id_source & nis)
{
  MM(s);
  MM(scalar_origin_rid);
  MM(new_val);
  MM(new_mark_set);
  roster_t expected_roster; MM(expected_roster);
  marking_map expected_markings; MM(expected_markings);

  s.set(scalar_origin_rid, new_val, new_mark_set, expected_roster, expected_markings);

  roster_t empty_roster;
  cset cs; MM(cs);
  make_cset(empty_roster, expected_roster, cs);
  
  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  // this function takes the old parent roster/marking and modifies them; in
  // our case, the parent roster/marking are empty, and so are our
  // roster/marking, so we don't need to do anything special.
  make_roster_for_nonmerge(cs, old_rid, new_roster, new_markings, nis);

  I(equal_up_to_renumbering(expected_roster, expected_markings,
                            new_roster, new_markings));
}

static void
run_with_1_roster_parent(a_scalar & s,
                         revision_id scalar_origin_rid,
                         scalar_val parent_val,
                         std::set<revision_id> const & parent_mark_set,
                         scalar_val new_val,
                         std::set<revision_id> const & new_mark_set,
                         node_id_source & nis)
{
  MM(s);
  MM(scalar_origin_rid);
  MM(parent_val);
  MM(parent_mark_set);
  MM(new_val);
  MM(new_mark_set);
  roster_t parent_roster; MM(parent_roster);
  marking_map parent_markings; MM(parent_markings);
  roster_t expected_roster; MM(expected_roster);
  marking_map expected_markings; MM(expected_markings);

  s.set(scalar_origin_rid, parent_val, parent_mark_set, parent_roster, parent_markings);
  s.set(scalar_origin_rid, new_val, new_mark_set, expected_roster, expected_markings);

  cset cs; MM(cs);
  make_cset(parent_roster, expected_roster, cs);
  
  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  new_roster = parent_roster;
  new_markings = parent_markings;
  make_roster_for_nonmerge(cs, new_rid, new_roster, new_markings, nis);

  I(equal_up_to_renumbering(expected_roster, expected_markings,
                            new_roster, new_markings));
}

static void
run_with_2_roster_parents(a_scalar & s,
                          revision_id scalar_origin_rid,
                          scalar_val left_val,
                          std::set<revision_id> const & left_mark_set,
                          scalar_val right_val,
                          std::set<revision_id> const & right_mark_set,
                          scalar_val new_val,
                          std::set<revision_id> const & new_mark_set,
                          node_id_source & nis)
{
  MM(s);
  MM(scalar_origin_rid);
  MM(left_val);
  MM(left_mark_set);
  MM(right_val);
  MM(right_mark_set);
  MM(new_val);
  MM(new_mark_set);
  roster_t left_roster; MM(left_roster);
  roster_t right_roster; MM(right_roster);
  roster_t expected_roster; MM(expected_roster);
  marking_map left_markings; MM(left_markings);
  marking_map right_markings; MM(right_markings);
  marking_map expected_markings; MM(expected_markings);

  s.set(scalar_origin_rid, left_val, left_mark_set, left_roster, left_markings);
  s.set(scalar_origin_rid, right_val, right_mark_set, right_roster, right_markings);
  s.set(scalar_origin_rid, new_val, new_mark_set, expected_roster, expected_markings);

  cset left_cs; MM(left_cs);
  cset right_cs; MM(right_cs);
  make_cset(left_roster, expected_roster, left_cs);
  make_cset(right_roster, expected_roster, right_cs);

  std::set<revision_id> left_uncommon_ancestors; MM(left_uncommon_ancestors);
  left_uncommon_ancestors.insert(left_rid);
  std::set<revision_id> right_uncommon_ancestors; MM(right_uncommon_ancestors);
  right_uncommon_ancestors.insert(right_rid);

  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  make_roster_for_merge(left_rid, left_roster, left_markings, left_cs,
                        left_uncommon_ancestors,
                        right_rid, right_roster, right_markings, right_cs,
                        right_uncommon_ancestors,
                        new_rid, new_roster, new_markings,
                        nis);

  I(equal_up_to_renumbering(expected_roster, expected_markings,
                            new_roster, new_markings));
}

////////////////
// These functions encapsulate all the different ways to get a 0 parent node,
// a 1 parent node, and a 2 parent node.

////////////////
// These functions encapsulate all the different ways to get a 0 parent
// scalar, a 1 parent scalar, and a 2 parent scalar.

// FIXME: have clients just use s.nis instead of passing it separately...?

static void
run_a_2_scalar_parent_mark_scenario_exact(revision_id const & scalar_origin_rid,
                                          scalar_val left_val,
                                          std::set<revision_id> const & left_mark_set,
                                          scalar_val right_val,
                                          std::set<revision_id> const & right_mark_set,
                                          scalar_val new_val,
                                          std::set<revision_id> const & new_mark_set)
{
  testing_node_id_source nis;
  scalars ss = all_scalars(nis);
  for (scalars::const_iterator i = ss.begin(); i != ss.end(); ++i)
    {
      run_with_2_roster_parents(**i, scalar_origin_rid,
                                left_val, left_mark_set,
                                right_val, right_mark_set,
                                new_val, new_mark_set,
                                nis);
    }
}

static revision_id
flip_revision_id(revision_id const & rid)
{
  if (rid == old_rid || rid == new_rid)
    return rid;
  else if (rid == left_rid)
    return right_rid;
  else if (rid == right_rid)
    return left_rid;
  else
    I(false);
}

static set<revision_id>
flip_revision_set(set<revision_id> const & rids)
{
  set<revision_id> flipped_rids;
  for (set<revision_id>::const_iterator i = rids.begin(); i != rids.end(); ++i)
    flipped_rids.insert(flip_revision_id(*i));
  return flipped_rids;
}

static void
run_a_2_scalar_parent_mark_scenario(revision_id const & scalar_origin_rid,
                                    scalar_val left_val,
                                    std::set<revision_id> const & left_mark_set,
                                    scalar_val right_val,
                                    std::set<revision_id> const & right_mark_set,
                                    scalar_val new_val,
                                    std::set<revision_id> const & new_mark_set)
{
  // run both what we're given...
  run_a_2_scalar_parent_mark_scenario_exact(scalar_origin_rid,
                                            left_val, left_mark_set,
                                            right_val, right_mark_set,
                                            new_val, new_mark_set);
  // ...and its symmetric reflection.  but we have to flip the mark set,
  // because the exact stuff has hard-coded the names of the various
  // revisions and their uncommon ancestor sets.
  {
    std::set<revision_id> flipped_left_mark_set = flip_revision_set(left_mark_set);
    std::set<revision_id> flipped_right_mark_set = flip_revision_set(right_mark_set);
    std::set<revision_id> flipped_new_mark_set = flip_revision_set(new_mark_set);

    run_a_2_scalar_parent_mark_scenario_exact(flip_revision_id(scalar_origin_rid),
                                              right_val, flipped_right_mark_set,
                                              left_val, flipped_left_mark_set,
                                              new_val, flipped_new_mark_set);
  }
}

static void
run_a_2_scalar_parent_mark_scenario(scalar_val left_val,
                                    std::set<revision_id> const & left_mark_set,
                                    scalar_val right_val,
                                    std::set<revision_id> const & right_mark_set,
                                    scalar_val new_val,
                                    std::set<revision_id> const & new_mark_set)
{
  run_a_2_scalar_parent_mark_scenario(old_rid,
                                      left_val, left_mark_set,
                                      right_val, right_mark_set,
                                      new_val, new_mark_set);
}

static void
run_a_1_scalar_parent_mark_scenario(scalar_val parent_val,
                                    std::set<revision_id> const & parent_mark_set,
                                    scalar_val new_val,
                                    std::set<revision_id> const & new_mark_set)
{
  {
    testing_node_id_source nis;
    scalars ss = all_scalars(nis);
    for (scalars::const_iterator i = ss.begin(); i != ss.end(); ++i)
      run_with_1_roster_parent(**i, old_rid,
                               parent_val, parent_mark_set,
                               new_val, new_mark_set,
                               nis);
  }
  // this is an asymmetric, test, so run it via the code that will test it
  // both ways
  run_a_2_scalar_parent_mark_scenario(left_rid,
                                      parent_val, parent_mark_set,
                                      scalar_none, std::set<revision_id>(),
                                      new_val, new_mark_set);
}

static void
run_a_0_scalar_parent_mark_scenario()
{
  {
    testing_node_id_source nis;
    scalars ss = all_scalars(nis);
    for (scalars::const_iterator i = ss.begin(); i != ss.end(); ++i)
      {
        run_with_0_roster_parents(**i, old_rid, scalar_a, singleton(old_rid), nis);
        run_with_1_roster_parent(**i, new_rid,
                                 scalar_none, std::set<revision_id>(),
                                 scalar_a, singleton(new_rid),
                                 nis);
        run_with_2_roster_parents(**i, new_rid,
                                  scalar_none, std::set<revision_id>(),
                                  scalar_none, std::set<revision_id>(),
                                  scalar_a, singleton(new_rid),
                                  nis);
      }
  }
}

////////////////
// These functions contain the actual list of *-merge cases that we would like
// to test.

static void
test_all_0_scalar_parent_mark_scenarios()
{
  L(F("TEST: begin checking 0-parent marking"));
  // a*
  run_a_0_scalar_parent_mark_scenario();
  L(F("TEST: end checking 0-parent marking"));
}

static void
test_all_1_scalar_parent_mark_scenarios()
{
  L(F("TEST: begin checking 1-parent marking"));
  //  a
  //  |
  //  a
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid));
  //  a*
  //  |
  //  a
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_a, singleton(left_rid));
  // a*  a*
  //  \ /
  //   a
  //   |
  //   a
  run_a_1_scalar_parent_mark_scenario(scalar_a, doubleton(left_rid, right_rid),
                                      scalar_a, doubleton(left_rid, right_rid));
  //  a
  //  |
  //  b*
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(new_rid));
  //  a*
  //  |
  //  b*
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(new_rid));
  // a*  a*
  //  \ /
  //   a
  //   |
  //   b*
  run_a_1_scalar_parent_mark_scenario(scalar_a, doubleton(left_rid, right_rid),
                                      scalar_b, singleton(new_rid));
  L(F("TEST: end checking 1-parent marking"));
}

static void
test_all_2_scalar_parent_mark_scenarios()
{
  L(F("TEST: begin checking 2-parent marking"));
  ///////////////////////////////////////////////////////////////////
  // a   a
  //  \ /
  //   a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid));
  // a   a*
  //  \ /
  //   a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_a, doubleton(old_rid, right_rid));
  // a*  a*
  //  \ /
  //   a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_a, doubleton(left_rid, right_rid));

  ///////////////////////////////////////////////////////////////////
  // a   a
  //  \ /
  //   b*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid),
                                      scalar_b, singleton(new_rid));
  // a   a*
  //  \ /
  //   b*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_b, singleton(new_rid));
  // a*  a*
  //  \ /
  //   b*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_b, singleton(new_rid));

  ///////////////////////////////////////////////////////////////////
  //  a*  b*
  //   \ /
  //    c*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_c, singleton(new_rid));
  //  a   b*
  //   \ /
  //    c*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_c, singleton(new_rid));
  // this case cannot actually arise, because if *(a) = *(b) then val(a) =
  // val(b).  but hey.
  //  a   b
  //   \ /
  //    c*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(old_rid),
                                      scalar_c, singleton(new_rid));

  ///////////////////////////////////////////////////////////////////
  //  a*  b*
  //   \ /
  //    a*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_a, singleton(new_rid));
  //  a   b*
  //   \ /
  //    a*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_a, singleton(new_rid));
  //  a*  b
  //   \ /
  //    a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(old_rid),
                                      scalar_a, singleton(left_rid));

  // FIXME: be nice to test:
  //  a*  a*  b
  //   \ /   /
  //    a   /
  //     \ /
  //      a
  L(F("TEST: end checking 2-parent marking"));
}

// there is _one_ remaining case that the above tests miss, because they
// couple scalar lifetimes and node lifetimes.  Maybe they shouldn't do that,
// but anyway... until someone decides to refactor, we need this.  The basic
// issue is that for content and name scalars, the scalar lifetime and the
// node lifetime are identical.  For attrs, this isn't necessarily true.  This
// is why we have two different attr scalars.  Let's say that "." means a node
// that doesn't exist, and "+" means a node that exists but has no roster.
// The first scalar checks cases like
//     +
//     |
//     a
//
//   +   +
//    \ /
//     a*
//     
//   a*  +
//    \ /
//     a
// and the second one checks cases like
//     .
//     |
//     a
//
//   .   .
//    \ /
//     a*
//     
//   a*  .
//    \ /
//     a
// Between them, they cover _almost_ all possibilities.  The one that they
// miss is:
//   .   +
//    \ /
//     a*
// (and its reflection).
// That is what this test checks.
// Sorry it's so code-duplication-iferous.  Refactors would be good...

namespace
{
  // this scalar represents an attr whose node may or may not already exist
  template <typename T>
  struct X_attr_mixed_scalar : public a_scalar
  {
    virtual std::string my_type() const { return "X_attr_scalar"; }

    std::map<scalar_val, pair<bool, attr_value> > values;
    X_attr_mixed_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, make_pair(true, attr_value("a"))));
      safe_insert(values, make_pair(scalar_b, make_pair(true, attr_value("b"))));
      safe_insert(values, make_pair(scalar_c, make_pair(true, attr_value("c"))));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      // scalar_none is . in the above notation
      // and scalar_none_2 is +
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, split("foo"));
        }
      if (val != scalar_none && val != scalar_none_2)
        {
          safe_insert(roster.get_node(obj_under_test_nid)->attrs,
                      make_pair(attr_key("test_key"), safe_get(values, val)));
          markings[obj_under_test_nid].attrs[attr_key("test_key")] = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };
}

static void
test_residual_attr_mark_scenario()
{
  L(F("TEST: begin checking residual attr marking case"));
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<file_maker> s(nis);
    run_with_2_roster_parents(s, left_rid,
                              scalar_none_2, std::set<revision_id>(),
                              scalar_none, std::set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<dir_maker> s(nis);
    run_with_2_roster_parents(s, left_rid,
                              scalar_none_2, std::set<revision_id>(),
                              scalar_none, std::set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<file_maker> s(nis);
    run_with_2_roster_parents(s, right_rid,
                              scalar_none, std::set<revision_id>(),
                              scalar_none_2, std::set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<dir_maker> s(nis);
    run_with_2_roster_parents(s, right_rid,
                              scalar_none, std::set<revision_id>(),
                              scalar_none_2, std::set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  L(F("TEST: end checking residual attr marking case"));
}

static void
test_all_mark_scenarios()
{
  test_all_0_scalar_parent_mark_scenarios();
  test_all_1_scalar_parent_mark_scenarios();
  test_all_2_scalar_parent_mark_scenarios();
  test_residual_attr_mark_scenario();
}

////////////////////////////////////////////////////////////////////////
// end of exhaustive tests
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// lifecyle tests
////////////////////////////////////////////////////////////////////////

// nodes can't survive dying on one side of a merge
static void
test_die_die_die_merge()
{
  roster_t left_roster; MM(left_roster);
  marking_map left_markings; MM(left_markings);
  roster_t right_roster; MM(right_roster);
  marking_map right_markings; MM(right_markings);
  testing_node_id_source nis;

  // left roster is empty except for the root
  left_roster.attach_node(left_roster.create_dir_node(nis), split(""));
  marking_t an_old_marking;
  an_old_marking.birth_revision = old_rid;
  an_old_marking.parent_name = singleton(old_rid);
  safe_insert(left_markings, make_pair(left_roster.get_node(split(""))->self,
                                       an_old_marking));
  // right roster is identical, except for a dir created in the old rev
  right_roster = left_roster;
  right_markings = left_markings;
  right_roster.attach_node(right_roster.create_dir_node(nis), split("foo"));
  safe_insert(right_markings, make_pair(right_roster.get_node(split("foo"))->self,
                                        an_old_marking));

  left_roster.check_sane_against(left_markings);
  right_roster.check_sane_against(right_markings);

  cset left_cs; MM(left_cs);
  // we add the node
  left_cs.dirs_added.insert(split("foo"));
  // we do nothing
  cset right_cs; MM(right_cs);

  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);

  // because the dir was created in the old rev, the left side has logically
  // seen it and killed it, so it needs to be dead in the result.
  BOOST_CHECK_THROW(
     make_roster_for_merge(left_rid, left_roster, left_markings, left_cs,
                           singleton(left_rid),
                           right_rid, right_roster, right_markings, right_cs,
                           singleton(right_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     std::logic_error);
  BOOST_CHECK_THROW(
     make_roster_for_merge(right_rid, right_roster, right_markings, right_cs,
                           singleton(right_rid),
                           left_rid, left_roster, left_markings, left_cs,
                           singleton(left_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     std::logic_error);
}
// nodes can't change type file->dir or dir->file
//    make_cset fails
//    merging a file and a dir with the same nid and no mention of what should
//      happen to them fails

static void
test_same_nid_diff_type()
{
  testing_node_id_source nis;

  roster_t dir_roster; MM(dir_roster);
  marking_map dir_markings; MM(dir_markings);
  dir_roster.attach_node(dir_roster.create_dir_node(nis), split(""));
  marking_t marking;
  marking.birth_revision = old_rid;
  marking.parent_name = singleton(old_rid);
  safe_insert(dir_markings, make_pair(dir_roster.get_node(split(""))->self,
                                      marking));

  roster_t file_roster; MM(file_roster);
  marking_map file_markings; MM(file_markings);
  file_roster = dir_roster;
  file_markings = dir_markings;

  // okay, they both have the root dir
  node_id nid = nis.next();
  dir_roster.create_dir_node(nid);
  dir_roster.attach_node(nid, split("foo"));
  safe_insert(dir_markings, make_pair(nid, marking));

  file_roster.create_file_node(new_ident(), nid);
  file_roster.attach_node(nid, split("foo"));
  marking.file_content = singleton(old_rid);
  safe_insert(file_markings, make_pair(nid, marking));

  dir_roster.check_sane_against(dir_markings);
  file_roster.check_sane_against(file_markings);

  cset cs; MM(cs);
  BOOST_CHECK_THROW(make_cset(dir_roster, file_roster, cs), std::logic_error);
  BOOST_CHECK_THROW(make_cset(file_roster, dir_roster, cs), std::logic_error);

  cset left_cs; MM(left_cs);
  cset right_cs; MM(right_cs);
  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  BOOST_CHECK_THROW(
     make_roster_for_merge(left_rid, dir_roster, dir_markings, left_cs,
                           singleton(left_rid),
                           right_rid, file_roster, file_markings, right_cs,
                           singleton(right_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     std::logic_error);
  BOOST_CHECK_THROW(
     make_roster_for_merge(left_rid, file_roster, file_markings, left_cs,
                           singleton(left_rid),
                           right_rid, dir_roster, dir_markings, right_cs,
                           singleton(right_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     std::logic_error);
  
}


static void
write_roster_test()
{
  L(F("TEST: write_roster_test"));
  roster_t r; MM(r);
  marking_map mm; MM(mm);

  testing_node_id_source nis;
  split_path root, foo, xx, fo, foo_bar, foo_ang, foo_zoo;
  file_path().split(root);
  file_path_internal("foo").split(foo);
  file_path_internal("foo/ang").split(foo_ang);
  file_path_internal("foo/bar").split(foo_bar);
  file_path_internal("foo/zoo").split(foo_zoo);
  file_path_internal("fo").split(fo);
  file_path_internal("xx").split(xx);

  file_id f1(string("1111111111111111111111111111111111111111"));
  revision_id rid(string("1234123412341234123412341234123412341234"));
  node_id nid;

  // if adding new nodes, add them at the end to keep the node_id order

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, root);
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, foo);
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, xx);
  r.set_attr(xx, attr_key("say"), attr_value("hello"));
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, fo);
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  // check that files aren't ordered separately to dirs & vice versa
  nid = nis.next();
  r.create_file_node(f1, nid);
  r.attach_node(nid, foo_bar);
  r.set_attr(foo_bar, attr_key("fascist"), attr_value("tidiness"));
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, foo_ang);
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, foo_zoo);
  r.set_attr(foo_zoo, attr_key("regime"), attr_value("new"));
  r.clear_attr(foo_zoo, attr_key("regime"));
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  { 
    // manifest first
    data mdat; MM(mdat);
    write_manifest_of_roster(r, mdat);

    data expected("dir \"\"\n"
                  "\n"
                  "dir \"fo\"\n"
                  "\n"
                  "dir \"foo\"\n"
                  "\n"
                  "dir \"foo/ang\"\n"
                  "\n"
                  "   file \"foo/bar\"\n"
                  "content [1111111111111111111111111111111111111111]\n"
                  "   attr \"fascist\" \"tidiness\"\n"
                  "\n"
                  "dir \"foo/zoo\"\n"
                  "\n"
                  " dir \"xx\"\n"
                  "attr \"say\" \"hello\"\n"
                 );
    MM(expected);

    BOOST_CHECK_NOT_THROW( I(expected == mdat), std::logic_error);
  }

  { 
    // full roster with local parts
    data rdat; MM(rdat);
    write_roster_and_marking(r, mm, rdat);

    // node_id order is a hassle.
    // root 1, foo 2, xx 3, fo 4, foo_bar 5, foo_ang 6, foo_zoo 7
    data expected("      dir \"\"\n"
                  "    ident \"1\"\n"
                  "    birth [1234123412341234123412341234123412341234]\n"
                  "path_mark [1234123412341234123412341234123412341234]\n"
                  "\n"
                  "      dir \"fo\"\n"
                  "    ident \"4\"\n"
                  "    birth [1234123412341234123412341234123412341234]\n"
                  "path_mark [1234123412341234123412341234123412341234]\n"
                  "\n"
                  "      dir \"foo\"\n"
                  "    ident \"2\"\n"
                  "    birth [1234123412341234123412341234123412341234]\n"
                  "path_mark [1234123412341234123412341234123412341234]\n"
                  "\n"
                  "      dir \"foo/ang\"\n"
                  "    ident \"6\"\n"
                  "    birth [1234123412341234123412341234123412341234]\n"
                  "path_mark [1234123412341234123412341234123412341234]\n"
                  "\n"
                  "        file \"foo/bar\"\n"
                  "     content [1111111111111111111111111111111111111111]\n"
                  "       ident \"5\"\n"
                  "        attr \"fascist\" \"tidiness\"\n"
                  "       birth [1234123412341234123412341234123412341234]\n"
                  "   path_mark [1234123412341234123412341234123412341234]\n"
                  "content_mark [1234123412341234123412341234123412341234]\n"
                  "   attr_mark \"fascist\" [1234123412341234123412341234123412341234]\n"
                  "\n"
                  "         dir \"foo/zoo\"\n"
                  "       ident \"7\"\n"
                  "dormant_attr \"regime\"\n"
                  "       birth [1234123412341234123412341234123412341234]\n"
                  "   path_mark [1234123412341234123412341234123412341234]\n"
                  "   attr_mark \"regime\" [1234123412341234123412341234123412341234]\n"
                  "\n"
                  "      dir \"xx\"\n"
                  "    ident \"3\"\n"
                  "     attr \"say\" \"hello\"\n"
                  "    birth [1234123412341234123412341234123412341234]\n"
                  "path_mark [1234123412341234123412341234123412341234]\n"
                  "attr_mark \"say\" [1234123412341234123412341234123412341234]\n"
                 );
    MM(expected);

    BOOST_CHECK_NOT_THROW( I(expected == rdat), std::logic_error);
  }
}

static void
check_sane_against_test()
{
  testing_node_id_source nis;
  split_path root, foo, bar;
  file_path().split(root);
  file_path_internal("foo").split(foo);
  file_path_internal("bar").split(bar);

  file_id f1(string("1111111111111111111111111111111111111111"));
  revision_id rid(string("1234123412341234123412341234123412341234"));
  node_id nid;

  {
    L(F("TEST: check_sane_against_test, no extra nodes in rosters"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, bar);
    // missing the marking

    BOOST_CHECK_THROW(r.check_sane_against(mm), std::logic_error);
  }

  {
    L(F("TEST: check_sane_against_test, no extra nodes in markings"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, bar);
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    r.detach_node(bar);

    BOOST_CHECK_THROW(r.check_sane_against(mm), std::logic_error);
  }

  {
    L(F("TEST: check_sane_against_test, missing birth rev"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].birth_revision = revision_id();

    BOOST_CHECK_THROW(r.check_sane_against(mm), std::logic_error);
  }

  {
    L(F("TEST: check_sane_against_test, missing path mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].parent_name.clear();

    BOOST_CHECK_THROW(r.check_sane_against(mm), std::logic_error);
  }

  {
    L(F("TEST: check_sane_against_test, missing content mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_file_node(f1, nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].file_content.clear();

    BOOST_CHECK_THROW(r.check_sane_against(mm), std::logic_error);
  }

  {
    L(F("TEST: check_sane_against_test, extra content mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].file_content.insert(rid);

    BOOST_CHECK_THROW(r.check_sane_against(mm), std::logic_error);
  }

  {
    L(F("TEST: check_sane_against_test, missing attr mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    // NB: mark and _then_ add attr
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    r.set_attr(root, attr_key("my_key"), attr_value("my_value"));

    BOOST_CHECK_THROW(r.check_sane_against(mm), std::logic_error);
  }

  {
    L(F("TEST: check_sane_against_test, empty attr mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    r.set_attr(root, attr_key("my_key"), attr_value("my_value"));
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].attrs[attr_key("my_key")].clear();

    BOOST_CHECK_THROW(r.check_sane_against(mm), std::logic_error);
  }

  {
    L(F("TEST: check_sane_against_test, extra attr mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    r.set_attr(root, attr_key("my_key"), attr_value("my_value"));
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].attrs[attr_key("my_second_key")].insert(rid);

    BOOST_CHECK_THROW(r.check_sane_against(mm), std::logic_error);
  }
}

static void
check_post_roster_unification_ok(roster_t const & left,
                                 roster_t const & right)
{
  MM(left);
  MM(right);
  I(left == right);
  left.check_sane();
  right.check_sane();
}

static void
create_some_new_temp_nodes(temp_node_id_source & nis,
                           roster_t & left_ros,
                           set<node_id> & left_new_nodes,
                           roster_t & right_ros,
                           set<node_id> & right_new_nodes)
{
  size_t n_nodes = 10 + (rand() % 30);
  editable_roster_base left_er(left_ros, nis);
  editable_roster_base right_er(right_ros, nis);

  // Stick in a root if there isn't one.
  if (!left_ros.has_root())
    {
      I(!right_ros.has_root());
      split_path root;
      root.push_back(the_null_component);

      node_id left_nid = left_er.create_dir_node();
      left_new_nodes.insert(left_nid);
      left_er.attach_node(left_nid, root);

      node_id right_nid = right_er.create_dir_node();
      right_new_nodes.insert(right_nid);
      right_er.attach_node(right_nid, root);
    }

  // Now throw in a bunch of others
  for (size_t i = 0; i < n_nodes; ++i)
    {
      node_t left_n = random_element(left_ros.all_nodes())->second;

      node_id left_nid, right_nid;
      if (flip())
        {
          left_nid = left_er.create_dir_node();
          right_nid = right_er.create_dir_node();
        }
      else
        {
          file_id fid = new_ident();
          left_nid = left_er.create_file_node(fid);
          right_nid = right_er.create_file_node(fid);
        }
      
      left_new_nodes.insert(left_nid);
      right_new_nodes.insert(right_nid);

      split_path pth;
      left_ros.get_name(left_n->self, pth);

      I(right_ros.has_node(pth));

      if (is_file_t(left_n) || (pth.size() > 1 && flip()))
        // Add a sibling of an existing entry.
        pth[pth.size() - 1] = new_component();      
      else 
        // Add a child of an existing entry.
        pth.push_back(new_component());
      
      left_er.attach_node(left_nid, pth);
      right_er.attach_node(right_nid, pth);
    }  
}

static void
test_unify_rosters_randomized()
{
  L(F("TEST: begin checking unification of rosters (randomly)"));
  temp_node_id_source tmp_nis;
  testing_node_id_source test_nis;  
  roster_t left, right;
  for (size_t i = 0; i < 30; ++i)
    {
      set<node_id> left_new, right_new, resolved_new;
      create_some_new_temp_nodes(tmp_nis, left, left_new, right, right_new);
      create_some_new_temp_nodes(tmp_nis, right, right_new, left, left_new);
      unify_rosters(left, left_new, right, right_new, resolved_new, test_nis);
      check_post_roster_unification_ok(left, right);
    }
  L(F("TEST: end checking unification of rosters (randomly)"));
}

static void
test_unify_rosters_end_to_end()
{
  L(F("TEST: begin checking unification of rosters (end to end)"));
  revision_id has_rid = left_rid;
  revision_id has_not_rid = right_rid;
  file_id my_fid(std::string("9012901290129012901290129012901290129012"));

  testing_node_id_source nis;

  roster_t has_not_roster; MM(has_not_roster);
  marking_map has_not_markings; MM(has_not_markings);
  {
    has_not_roster.attach_node(has_not_roster.create_dir_node(nis), split(""));
    marking_t root_marking;
    root_marking.birth_revision = old_rid;
    root_marking.parent_name = singleton(old_rid);
    safe_insert(has_not_markings, make_pair(has_not_roster.root()->self,
                                            root_marking));
  }

  roster_t has_roster = has_not_roster; MM(has_roster);
  marking_map has_markings = has_not_markings; MM(has_markings);
  {
    has_roster.attach_node(has_roster.create_file_node(my_fid, nis),
                           split("foo"));
    marking_t file_marking;
    file_marking.birth_revision = has_rid;
    file_marking.parent_name = file_marking.file_content = singleton(has_rid);
    safe_insert(has_markings, make_pair(has_roster.get_node(split("foo"))->self,
                                        file_marking));
  }

  cset add_cs; MM(add_cs);
  safe_insert(add_cs.files_added, make_pair(split("foo"), my_fid));
  cset no_add_cs; MM(no_add_cs);
  
  // added in left, then merged
  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(has_rid, has_roster, has_markings, no_add_cs,
                          singleton(has_rid),
                          has_not_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_not_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(split("foo"))->self
      == has_roster.get_node(split("foo"))->self);
  }
  // added in right, then merged
  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(has_not_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_not_rid),
                          has_rid, has_roster, has_markings, no_add_cs,
                          singleton(has_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(split("foo"))->self
      == has_roster.get_node(split("foo"))->self);
  }
  // added in merge
  // this is a little "clever", it uses the same has_not_roster twice, but the
  // second time it passes the has_rid, to make it a possible graph.
  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(has_not_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_not_rid),
                          has_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(split("foo"))->self
      != has_roster.get_node(split("foo"))->self);
  }
  L(F("TEST: end checking unification of rosters (end to end)"));
}


void
add_roster_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&check_sane_roster_screwy_dir_map));
  suite->add(BOOST_TEST_CASE(&test_die_die_die_merge));
  suite->add(BOOST_TEST_CASE(&test_same_nid_diff_type));
  suite->add(BOOST_TEST_CASE(&test_unify_rosters_end_to_end));
  suite->add(BOOST_TEST_CASE(&test_unify_rosters_randomized));
  suite->add(BOOST_TEST_CASE(&test_all_mark_scenarios));
  suite->add(BOOST_TEST_CASE(&bad_attr_test));
  suite->add(BOOST_TEST_CASE(&check_sane_roster_loop_test));
  suite->add(BOOST_TEST_CASE(&check_sane_roster_test));
  suite->add(BOOST_TEST_CASE(&write_roster_test));
  suite->add(BOOST_TEST_CASE(&check_sane_against_test));
  suite->add(BOOST_TEST_CASE(&automaton_roster_test));
}


#endif // BUILD_UNIT_TESTS
