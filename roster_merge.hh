#ifndef __ROSTER_MERGE_HH__
#define __ROSTER_MERGE_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"
#include "roster.hh"

// our general strategy is to return a (possibly insane) roster, and a list of
// conflicts encountered in that roster.  Each conflict encountered in merging
// the roster creates an entry in this list.

// nodes with name conflicts are left detached in the resulting roster, with
// null parent and name fields.
// note that it is possible that the parent node on the left, the right, or
// both, no longer exist in the merged roster.  also note that it is possible
// that on one or both sides, they do exist, but already have an entry with
// the given name.
struct node_name_conflict
{
  node_id nid;
  node_name_conflict(node_id nid) : nid(nid) {}
  std::pair<node_id, path_component> left, right;
};

// files with content conflicts are left attached in resulting tree (unless
// detached for some other reason), but with a null content hash.
struct file_content_conflict
{
  node_id nid;
  file_content_conflict(node_id nid) : nid(nid) {}
  file_id left, right;
};

// nodes with attrs conflicts are left attached in the resulting tree (unless
// detached for some other reason), but with the given attribute left out of
// their full_attr_map_t.  Note that this doesn't actually leave the resulting
// roster insane (FIXME: we could put an invalid attr value in instead, like a
// pair (false, "foo") (since the second value can only be non-null if the
// first is 'true').  Should we do this?)
struct node_attr_conflict
{
  node_id nid;
  node_attr_conflict(node_id nid) : nid(nid) {}
  attr_key key;
  std::pair<bool, attr_value> left, right;
};

// interactions between conflict types:
//   node rename conflicts never participate in structural conflicts
//     (e.g., merge <rename a foo; rename b bar>, <rename a bar> could be
//     considered to have two conflicts -- 'a' being renamed to both 'foo' and
//     'bar', and 'a' and 'b' both being renamed to 'bar'.  Only the former
//     occurs; 'b' merges cleanly and will be named 'bar' in the resulting
//     manifest.)
//   

// orphaned nodes always merged their name cleanly, so we simply put that name
// here.  the node in the resulting roster is detached.
struct orphaned_node_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name;
};

// this is when two distinct nodes want to have the same name.  these nodes
// always each merged their names cleanly.  the nodes in the resulting roster
// are both detached.
// only two nodes are possible, because we
//   -- only merge two rosters at a time
//   -- merge (parent, basename) as a single scalar.  If we merged them
//      separately, then it would be possible to have one side of a merge
//      rename a bunch of files in different directories to all have the same
//      basename, and the other side of the merge to move them all into the
//      same directory.
// a clean *-merge of a scalar always takes on the value of one parent or
// another, and the requirement here is that each node have a unique (parent,
// basename) tuple, and since our requirement matches our *-merge scalar,
// we're okay.
struct rename_target_conflict
{
  node_id nid1, nid2;
  std::pair<node_id, path_component> parent_name;
};

struct directory_loop_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name;
};

// renaming the root dir allows these:
//   -- MT in root
//   -- missing root directory

// this is a node that cleanly merged to some name, but that name was somehow
// forbidden.  (Currently, the only forbidden name is "MT" in the root
// directory.)
struct illegal_name_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name;
};

struct roster_merge_result
{
  std::vector<node_name_conflict> node_name_conflicts;
  std::vector<file_content_conflict> file_content_conflicts;
  std::vector<node_attr_conflict> node_attr_conflicts;
  std::vector<orphaned_node_conflict> orphaned_node_conflicts;
  std::vector<rename_target_conflict> rename_target_conflicts;
  std::vector<directory_loop_conflict> directory_loop_conflicts;
  std::vector<illegal_name_conflict> illegal_name_conflicts;
  bool missing_root_dir;
  // this roster is sane if is_clean() returns true
  roster_t roster;
  bool is_clean();
  void clear();
};

void
roster_merge(roster_t const & left_parent,
             marking_map const & left_marking,
             std::set<revision_id> const & left_uncommon_ancestors,
             roster_t const & right_parent,
             marking_map const & right_marking,
             std::set<revision_id> const & right_uncommon_ancestors,
             roster_merge_result & result);


#endif
