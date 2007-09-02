#ifndef __WORK_CONFLICTS_HH__
#define __WORK_CONFLICTS_HH__

// Copyright (C) 2007 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

//     this []  // not in roster_merge_result
//    other []
// ancestor []
//
// temporary "a/temp/dir"  // not in roster_merge_result
// 
//    two_names "where/it/is"
//  this_wanted "dir" "basename"
// other_wanted "dir" "basename"
//
// file_content "where/it/is"
//         this "where/this/is"    // these are implicit temporaries
//        other "where/other/is"   // and aren't in roster_merge_result
//     ancestor "where/ancestor/is"
//
//   node_attr "where/file/is"
//         key "key"
//  this_value "value1"     // or unset, somehow...
// other_value "value2"     // or unset, somehow...
//
// orphaned_node "where/it/is"
//        wanted "old_dir_name" "basename"  // not exactly in roster_merge_result
//
// rename_target "where/one/is" "where/the/other/is"
//        wanted "dir" "basename"
//
// directory_loop "where/it/is"
//        wanted "dir" "basename"
//
// illegal_name "where/it/is"
//       wanted "dir" "basename"
//
// missing_root_dir "true"

#include <string>
#include "roster_merge.hh"

struct workspace_file_content_conflict
{
  node_id nid, left, right, ancestor;
};

struct workspace_orphaned_node_conflict
{
  node_id nid;
  std::string old_dir_name;
  path_component wanted_basename;
};

// we split rename target conflicts into two halves, and recognize the two
// halves by them having the same parent_name bit here.
struct workspace_rename_target_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name;
};

struct work_conflicts
{
  // Note: 'this' is 'left', 'other' is 'right'
  revision_id left, right, ancestor;
  set<node_id> temporaries;
  std::vector<node_name_conflict> node_name_conflicts;
  std::vector<workspace_file_content_conflict> file_content_conflicts;
  std::vector<node_attr_conflict> node_attr_conflicts;
  std::vector<workspace_orphaned_node_conflict> orphaned_node_conflicts;
  std::vector<workspace_rename_target_conflict> rename_target_conflicts;
  std::vector<directory_loop_conflict> directory_loop_conflicts;
  std::vector<illegal_name_conflict> illegal_name_conflicts;
  bool missing_root_dir;
};

void
convert_roster_merge_to_work_conflicts(roster_merge_result const & result,
                                       work_conflicts & conflicts);

void
write_work_conflicts(work_conflicts const & conflicts,
                     roster_t const & workspace_shape,
                     work_conflicts_data & out);

void
read_work_conflicts(work_conflicts_data const & in,
                    roster_t const & workspace_shape,
                    work_conflicts & conflicts);

#endif
