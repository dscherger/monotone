#ifndef __CSET_HH__
#define __CSET_HH__

// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <set>
#include "paths.hh"
#include "rev_types.hh"

// Virtual interface to a tree-of-files which you can edit destructively;
// this may be the filesystem or an in-memory representation (a roster /
// mfest). The operations maintain both the roster and the marking_map (if
// any).

struct editable_tree
{
  // Detaching existing nodes (for renaming or deleting)
  virtual node_id detach_node(file_path const & src) = 0;
  virtual void drop_detached_node(node_id nid) = 0;

  // Attaching new nodes (via creation, as the tail end of renaming, suturing, or splitting)
  virtual node_id create_dir_node() = 0;
  virtual node_id create_file_node(file_id const & content,
                                   std::pair<node_id, node_id> const ancestors) = 0;
  virtual node_id get_node(file_path const &pth) = 0;
  virtual void attach_node(node_id nid, file_path const & dst) = 0;

  // Modifying elements in-place
  virtual void apply_delta(file_path const & pth,
                           file_id const & old_id,
                           file_id const & new_id) = 0;
  virtual void clear_attr(file_path const & pth,
                          attr_key const & name) = 0;
  virtual void set_attr(file_path const & pth,
                        attr_key const & name,
                        attr_value const & val) = 0;

  virtual void commit() = 0;

  virtual ~editable_tree() {}
};


// In-memory representation of a change set.

struct cset
{
  // Deletions.
  std::set<file_path> nodes_deleted;

  // Additions.
  std::set<file_path> dirs_added;
  std::map<file_path, file_id> files_added;

  // Sutures.
  struct sutured_t
  {
    // If the suture is resolving a merge conflict, then one ancestor is
    // from the left side of the merge, and the other ancestor is from the
    // other side of the merge. However, each changeset only shows one of
    // these ancestors; there are two changesets for a merged revision. Only
    // first_ancestor is non-null in this case.
    //
    // If the suture is a user command, then both ancestors are from the
    // same revision, and both are non-null.
    file_path first_ancestor;
    file_path second_ancestor;
    file_id sutured_id;

    sutured_t(file_path the_first, file_path the_second, file_id the_sutured_id) :
      first_ancestor (the_first), second_ancestor (the_second), sutured_id (the_sutured_id) {};
  };
  std::map<file_path, sutured_t> nodes_sutured;

  // Pure renames.
  std::map<file_path, file_path> nodes_renamed;

  // Pure deltas.
  std::map<file_path, std::pair<file_id, file_id> > deltas_applied;

  // Attribute changes.
  std::set<std::pair<file_path, attr_key> > attrs_cleared;
  std::map<std::pair<file_path, attr_key>, attr_value> attrs_set;

  bool operator==(cset const & other) const
  {
    return nodes_deleted == other.nodes_deleted
      && dirs_added == other.dirs_added
      && files_added == other.files_added
      && nodes_renamed == other.nodes_renamed
      && deltas_applied == other.deltas_applied
      && attrs_cleared == other.attrs_cleared
      && attrs_set == other.attrs_set
      ;
  }

  // Apply changeset to roster and marking map in tree.
  void apply_to(editable_tree & t) const;

  bool empty() const;
  void clear();
};

inline file_path const &
delta_entry_path(std::map<file_path, std::pair<file_id, file_id> >::const_iterator i)
{
  return i->first;
}

inline file_id const &
delta_entry_src(std::map<file_path, std::pair<file_id, file_id> >::const_iterator i)
{
  return i->second.first;
}

inline file_id const &
delta_entry_dst(std::map<file_path, std::pair<file_id, file_id> >::const_iterator i)
{
  return i->second.second;
}

void
print_cset(basic_io::printer & printer,
           cset const & cs);

void
write_cset(cset const & cs, data & dat);

void
parse_cset(basic_io::parser & parser,
           cset & cs);

void
read_cset(data const & dat, cset & cs);

template <> void
dump(cset const & cs, std::string & out);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __CSET_HH__
