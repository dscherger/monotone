// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __REV_TYPES_HH__
#define __REV_TYPES_HH__

// This file contains forward declarations and typedefs for all of the basic
// types associated with revision handling.  It should be included instead
// of (any or all of) basic_io.hh, cset.hh, graph.hh, paths.hh, revision.hh,
// roster.hh, and database.hh when all that is necessary is these
// declarations.

#include <memory>
#include "vector.hh"

#include "vocab.hh"
#include "numeric_vocab.hh"
#include "hybrid_map.hh"
#include "cow_trie.hh"

// full definitions in basic_io.hh
namespace basic_io
{
  struct printer;
  struct parser;
  struct stanza;
}

// full definitions in cset.hh
typedef u32 node_id;
class cset;
class editable_tree;

const node_id first_temp_node = 1U << (sizeof(node_id) * 8 - 1);
inline bool temp_node(node_id n)
{
  return n & first_temp_node;
}

// full definitions in graph.hh
struct rev_graph;
struct reconstruction_graph;
typedef std::vector<id> reconstruction_path;
typedef std::multimap<revision_id, revision_id> rev_ancestry_map;

// full definitions in paths.hh
class any_path;
class bookkeeping_path;
class file_path;
class system_path;
class path_component;

// full definitions in revision.hh
class revision_t;
typedef std::map<revision_id, std::shared_ptr<cset> > edge_map;
typedef edge_map::value_type edge_entry;

// full definitions in rev_height.hh
class rev_height;

// full definitions in roster.hh
struct node_id_source;
struct node;
struct dir_node;
struct file_node;
struct marking;
class roster_t;
class editable_roster_base;

typedef std::shared_ptr<node> node_t;
typedef std::shared_ptr<file_node> file_t;
typedef std::shared_ptr<dir_node> dir_t;

typedef std::shared_ptr<node const> const_node_t;
typedef std::shared_ptr<file_node const> const_file_t;
typedef std::shared_ptr<dir_node const> const_dir_t;

typedef std::shared_ptr<marking> marking_t;
typedef std::shared_ptr<marking const> const_marking_t;
class marking_map;

typedef std::map<path_component, node_t> dir_map;
//typedef hybrid_map<node_id, node_t> node_map;
typedef cow_trie<node_id, node_t, 8> node_map;

// (true, "val") or (false, "") are both valid attr values (for proper
// merging, we have to widen the attr_value type to include a first-class
// "undefined" value).
typedef std::map<attr_key, std::pair<bool, attr_value> > attr_map_t;

// full definitions in database.hh
class database;
class conditional_transaction_guard;
class transaction_guard;

typedef std::shared_ptr<roster_t const> roster_t_cp;
typedef std::shared_ptr<marking_map const> marking_map_cp;
typedef std::pair<roster_t_cp, marking_map_cp> cached_roster;

typedef std::map<revision_id, cached_roster> parent_map;
typedef parent_map::value_type parent_entry;

#endif // __REV_TYPES_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
