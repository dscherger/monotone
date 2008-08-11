#ifndef __ROSTER_MERGE_HH__
#define __ROSTER_MERGE_HH__

// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <boost/shared_ptr.hpp>

#include "rev_types.hh"
#include "database.hh"
#include "diff_patch.hh"
#include "roster.hh" // needs full definition of roster_t available

// When adding a new conflict type, add all of the following:
//
// - in this file:
// -- A struct definition
// -- A dump template declaration
// -- A vector of the conflicts in roster_merge_result
// -- 'report' and 'resolve' functions in roster_merge_result
// - in roster_merge.cc:
// -- bodies for dump, report, resolve
// -- line in roster_merge_result::has_non_content_conflicts
// -- line in dump_conflicts
// -- 'parse_*_conflicts'
// -- case in parse_resolve_conflicts_str
// -- line in parse_resolve_conflicts_opts
// -- line in roster_merge_result::clear
// -- cases to record conflicts in roster_merge and subroutines
// - in merge.cc:
// -- resolve and report lines in resolve_merge_conflicts
// - in cmd_merging.cc:
// -- line in show_conflicts_core

// Our general strategy is to return a (possibly insane) roster, and a list of
// conflicts encountered in that roster.  Each conflict encountered in merging
// the roster creates an entry in this list.
//
// If the user specifies a --resolve-conflicts option, and it contains a
// resolution for a given conflict, the conflict resolutions are added to
// each conflict object when the option is parsed.

// interactions between conflict types:
//   node rename conflicts never participate in structural conflicts
//     (e.g., merge <rename a foo; rename b bar>, <rename a bar> could be
//     considered to have two conflicts -- 'a' being renamed to both 'foo' and
//     'bar', and 'a' and 'b' both being renamed to 'bar'.  Only the former
//     occurs; 'b' merges cleanly and will be named 'bar' in the resulting
//     manifest.)
//

namespace resolve_conflicts
{
  enum resolution_t {none, content_user, content_internal, ignore_drop, rename, respect_drop, suture};

  enum side_t {left_side, right_side};
}

// renaming the root dir allows these:
//   -- _MTN in root
//   -- missing root directory

// this is a node that cleanly merged to some name, but that name was somehow
// forbidden.  (Currently, the only forbidden name is "_MTN" in the root
// directory.)
struct invalid_name_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name;
};

struct directory_loop_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name;
};

// orphaned nodes always merged their name cleanly, so we simply put that name
// here.  the node in the resulting roster is detached.
struct orphaned_node_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name;
};

// nodes with multiple name conflicts are left detached in the resulting
// roster, with null parent and name fields.
// note that it is possible that the parent node on the left, the right, or
// both, no longer exist in the merged roster.  also note that it is possible
// that on one or both sides, they do exist, but already have an entry with
// the given name.
struct multiple_name_conflict
{
  node_id nid;
  multiple_name_conflict(node_id nid) : nid(nid) {}
  std::pair<node_id, path_component> left, right;
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
struct duplicate_name_conflict
{
  node_id left_nid, right_nid;
  std::pair<node_id, path_component> parent_name;
  std::pair<resolve_conflicts::resolution_t, file_path> left_resolution, right_resolution;

  duplicate_name_conflict ()
  {left_resolution.first = resolve_conflicts::none;
    right_resolution.first = resolve_conflicts::none;};
};

// files with content_drop conflicts are unattached in result roster, with
// parent content hash.
struct content_drop_conflict
{
  node_id nid;
  resolve_conflicts::side_t parent_side; // node is in parent_side roster, not in other roster
  file_id fid;

  // resolution is one of none, ignore_drop, respect_drop. If ignore_drop,
  // provide new name to allow avoiding name conflicts.
  std::pair<resolve_conflicts::resolution_t, file_path> resolution;

  content_drop_conflict () :
    nid(the_null_node), parent_side(resolve_conflicts::left_side) {resolution.first = resolve_conflicts::none;};

  content_drop_conflict(node_id nid, file_id fid, resolve_conflicts::side_t parent_side) :
    nid(nid), parent_side(parent_side), fid(fid) {resolution.first = resolve_conflicts::none;};
};

// files with suture_drop conflicts are unattached in result roster, with
// sutured parent content hash.
struct suture_drop_conflict
{
  // We don't store the file_id in the conflict, to support suturing and
  // conflicting directories in the future.
  //
  // sutured_nid is in sutured_side roster, not in other roster
  // dropped_nids are dropped in other roster
  node_id sutured_nid;
  resolve_conflicts::side_t sutured_side;
  std::set<node_id> dropped_nids;

  // resolution is one of none, ignore_drop. If ignore_drop,
  // provide new name to allow avoiding name conflicts.
  std::pair<resolve_conflicts::resolution_t, file_path> resolution;

  suture_drop_conflict () :
    sutured_nid(the_null_node),
    sutured_side(resolve_conflicts::left_side)
  {resolution.first = resolve_conflicts::none;};

  suture_drop_conflict(node_id sutured_nid,
                       resolve_conflicts::side_t sutured_side,
                       std::set<node_id> dropped_nids) :
    sutured_nid(sutured_nid),
    sutured_side(sutured_side),
    dropped_nids(dropped_nids)
  {resolution.first = resolve_conflicts::none;};
};

// files with suture_suture conflicts are left attached in result roster
// (unless unattached for another reason), with sutured parent content hash.
struct suture_suture_conflict
{
  // We don't store the file_id in the conflict, to support suturing and
  // conflicting directories in the future.
  //
  // sutured_nid is in sutured_side roster, not in other roster
  // common_parents are parents of suture_nid common to both parent rosters
  // conflict_nids are in other roster, have subset of common_parents
  // extra_nids are in other roster, have some parents in common_parents, other parents not in common_parents
  node_id sutured_nid;
  resolve_conflicts::side_t sutured_side;
  std::set<node_id> common_parents;
  std::set<node_id> conflict_nids;
  std::set<node_id> extra_nids;

  // There is no resolution; user must suture the nodes in the other parent
  // to match the sutured parent, or undo the sutures in the sutured parent
  // to match the other parent.

  suture_suture_conflict () :
    sutured_nid(the_null_node),
    sutured_side(resolve_conflicts::left_side) {};

  suture_suture_conflict(node_id sutured_nid,
                         resolve_conflicts::side_t sutured_side,
                         std::set<node_id> common_parents,
                         std::set<node_id> conflict_nids,
                         std::set<node_id> extra_nids) :
    sutured_nid(sutured_nid),
    sutured_side(sutured_side),
    common_parents(common_parents),
    conflict_nids(conflict_nids),
    extra_nids(extra_nids) {};
};

// nodes with attribute conflicts are left attached in the resulting tree (unless
// detached for some other reason), but with the given attribute left out of
// their full_attr_map_t.  Note that this doesn't actually leave the resulting
// roster insane (FIXME: we could put an invalid attr value in instead, like a
// pair (false, "foo") (since the second value can only be non-null if the
// first is 'true').  Should we do this?)
struct attribute_conflict
{
  node_id nid;
  attribute_conflict(node_id nid) : nid(nid) {}
  attr_key key; // attr_name?
  std::pair<bool, attr_value> left, right;
};

// files with content conflicts are left attached in resulting tree (unless
// detached for some other reason), but with a null content hash.
struct file_content_conflict
{
  node_id left_nid, right_nid, result_nid; // node ids can be different due to suture
  file_id left, right;

  std::pair<resolve_conflicts::resolution_t, file_path> resolution;

  file_content_conflict () :
    left_nid(the_null_node), right_nid(the_null_node), result_nid(the_null_node),
    resolution(std::make_pair(resolve_conflicts::none, file_path())) {};

  file_content_conflict(node_id left_nid, node_id right_nid, node_id result_nid) :
    left_nid(left_nid), right_nid(right_nid), result_nid(result_nid),
    resolution(std::make_pair(resolve_conflicts::none, file_path())) {};

  void get_ancestor_roster(content_merge_adaptor & adaptor,
                           node_id & ancestor_nid,
                           revision_id & rid,
                           boost::shared_ptr<roster_t const> & ancestor_roster) const;
};


template <> void dump(invalid_name_conflict const & conflict, std::string & out);
template <> void dump(directory_loop_conflict const & conflict, std::string & out);

template <> void dump(orphaned_node_conflict const & conflict, std::string & out);
template <> void dump(multiple_name_conflict const & conflict, std::string & out);
template <> void dump(duplicate_name_conflict const & conflict, std::string & out);

template <> void dump(attribute_conflict const & conflict, std::string & out);
template <> void dump(file_content_conflict const & conflict, std::string & out);
template <> void dump(content_drop_conflict const & conflict, std::string & out);
template <> void dump(suture_drop_conflict const & conflict, std::string & out);

struct roster_merge_result
{
  // three main types of conflicts
  // - structural conflicts  (which have the following subtypes)
  //   - missing root directory
  //   - invalid name conflicts
  //   - duplicate name conflicts
  //   - orphaned node conflicts
  //   - multiple name conflicts
  //   - directory loop conflicts
  //   - content_drop conflicts
  //   - suture_drop conflicts
  //   - suture_suture conflicts
  // - attribute conflicts
  // - file content conflicts

  bool missing_root_dir;
  std::vector<invalid_name_conflict> invalid_name_conflicts;
  std::vector<directory_loop_conflict> directory_loop_conflicts;

  std::vector<orphaned_node_conflict> orphaned_node_conflicts;
  std::vector<multiple_name_conflict> multiple_name_conflicts;
  std::vector<duplicate_name_conflict> duplicate_name_conflicts;
  std::vector<content_drop_conflict> content_drop_conflicts;
  std::vector<suture_drop_conflict> suture_drop_conflicts;
  std::vector<suture_suture_conflict> suture_suture_conflicts;

  std::vector<attribute_conflict> attribute_conflicts;
  std::vector<file_content_conflict> file_content_conflicts;


  // this roster is sane if is_clean() returns true
  roster_t roster;
  bool is_clean() const;
  bool has_content_conflicts() const;
  bool has_non_content_conflicts() const;
  void log_conflicts() const;

  void report_missing_root_conflicts(roster_t const & left,
                                     roster_t const & right,
                                     content_merge_adaptor & adaptor,
                                     bool const basic_io,
                                     std::ostream & output) const;
  void report_invalid_name_conflicts(roster_t const & left,
                                     roster_t const & right,
                                     content_merge_adaptor & adaptor,
                                     bool const basic_io,
                                     std::ostream & output) const;
  void report_directory_loop_conflicts(roster_t const & left,
                                       roster_t const & right,
                                       content_merge_adaptor & adaptor,
                                       bool const basic_io,
                                       std::ostream & output) const;

  void report_orphaned_node_conflicts(roster_t const & left,
                                      roster_t const & right,
                                      content_merge_adaptor & adaptor,
                                      bool const basic_io,
                                      std::ostream & output) const;
  void report_multiple_name_conflicts(roster_t const & left,
                                      roster_t const & right,
                                      content_merge_adaptor & adaptor,
                                      bool const basic_io,
                                      std::ostream & output) const;

  void report_duplicate_name_conflicts(roster_t const & left,
                                       roster_t const & right,
                                       content_merge_adaptor & adaptor,
                                       bool const basic_io,
                                       std::ostream & output) const;
  void resolve_duplicate_name_conflicts(lua_hooks & lua,
                                        temp_node_id_source nis,
                                        roster_t const & left_roster,
                                        roster_t const & right_roster,
                                        content_merge_adaptor & adaptor);

  void report_content_drop_conflicts(roster_t const & left_roster,
                                     roster_t const & right_roster,
                                     bool const basic_io,
                                     std::ostream & output) const;
  void resolve_content_drop_conflicts(roster_t const & left_roster,
                                      roster_t const & right_roster);

  void report_suture_drop_conflicts(roster_t const & left_roster,
                                    roster_t const & right_roster,
                                    bool const basic_io,
                                    std::ostream & output) const;
  void resolve_suture_drop_conflicts(roster_t const & left_roster,
                                     roster_t const & right_roster);

  void report_attribute_conflicts(roster_t const & left,
                                  roster_t const & right,
                                  content_merge_adaptor & adaptor,
                                  bool const basic_io,
                                  std::ostream & output) const;

  // not 'const' because this sets resolution to 'resolved_internal' if the
  // internal merger would succeed.
  void report_file_content_conflicts(lua_hooks & lua,
                                     roster_t const & left,
                                     roster_t const & right,
                                     content_merge_adaptor & adaptor,
                                     bool const basic_io,
                                     std::ostream & output);

  void resolve_file_content_conflicts(lua_hooks & lua,
                                      roster_t const & left_roster,
                                      roster_t const & right_roster,
                                      content_merge_adaptor & adaptor);

  void clear();
};

template <> void dump(roster_merge_result const & result, std::string & out);

void
roster_merge(roster_t const & left_parent,
             marking_map const & left_markings,
             std::set<revision_id> const & left_uncommon_ancestors,
             roster_t const & right_parent,
             marking_map const & right_markings,
             std::set<revision_id> const & right_uncommon_ancestors,
             temp_node_id_source nis,
             roster_merge_result & result);

void
parse_resolve_conflicts_opts (options const & opts,
                              roster_t const & left_roster,
                              roster_t const & right_roster,
                              roster_merge_result & result,
                              bool & resolutions_given);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
