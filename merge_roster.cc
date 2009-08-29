// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
//               2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "merge_roster.hh"

#include "sanity.hh"
#include "safe_map.hh"
#include "parallel_iter.hh"

#include <sstream>

using std::make_pair;
using std::pair;
using std::set;
using std::string;
using std::ostringstream;

enum side_t {left_side, right_side};

static char const *
image(resolve_conflicts::resolution_t resolution)
{
  switch (resolution)
    {
    case resolve_conflicts::none:
      return "none";
    case resolve_conflicts::content_user:
      return "content_user";
    case resolve_conflicts::content_internal:
      return "content_internal";
    case resolve_conflicts::drop:
      return "drop";
    case resolve_conflicts::keep:
      return "keep";
    case resolve_conflicts::rename:
      return "rename";
    }
  I(false); // keep compiler happy
}

template <> void
dump(invalid_name_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "invalid_name_conflict on node: " << conflict.nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second << "\n";
  out = oss.str();
}

template <> void
dump(directory_loop_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "directory_loop_conflict on node: " << conflict.nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second << "\n";
  out = oss.str();
}

template <> void
dump(orphaned_node_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "orphaned_node_conflict on node: " << conflict.nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second << "\n";
  out = oss.str();
}

template <> void
dump(multiple_name_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "multiple_name_conflict on node: " << conflict.nid << " "
      << "left parent: " << conflict.left.first << " "
      << "basename: " << conflict.left.second << " "
      << "right parent: " << conflict.right.first << " "
      << "basename: " << conflict.right.second << "\n";
  out = oss.str();
}

template <> void
dump(duplicate_name_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "duplicate_name_conflict between left node: " << conflict.left_nid << " "
      << "and right node: " << conflict.right_nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second;

  if (conflict.left_resolution.first != resolve_conflicts::none)
    {
      oss << " left_resolution: " << image(conflict.left_resolution.first);
      oss << " left_name: " << conflict.left_resolution.second;
    }
  if (conflict.right_resolution.first != resolve_conflicts::none)
    {
      oss << " right_resolution: " << image(conflict.right_resolution.first);
      oss << " right_name: " << conflict.right_resolution.second;
    }
  oss << "\n";
  out = oss.str();
}

template <> void
dump(attribute_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "attribute_conflict on node: " << conflict.nid << " "
      << "attr: '" << conflict.key << "' "
      << "left: " << conflict.left.first << " '" << conflict.left.second << "' "
      << "right: " << conflict.right.first << " '" << conflict.right.second << "'\n";
  out = oss.str();
}

template <> void
dump(file_content_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "file_content_conflict on node: " << conflict.nid;

  if (conflict.resolution.first != resolve_conflicts::none)
    {
      oss << " resolution: " << image(conflict.resolution.first);
      oss << " name: " << conflict.resolution.second;
    }
  oss << "\n";
  out = oss.str();
}

void
roster_merge_result::clear()
{
  missing_root_conflict = false;
  invalid_name_conflicts.clear();
  directory_loop_conflicts.clear();

  orphaned_node_conflicts.clear();
  multiple_name_conflicts.clear();
  duplicate_name_conflicts.clear();

  attribute_conflicts.clear();
  file_content_conflicts.clear();

  roster = roster_t();
}

bool
roster_merge_result::is_clean() const
{
  return !has_non_content_conflicts()
    && !has_content_conflicts();
}

bool
roster_merge_result::has_content_conflicts() const
{
  return !file_content_conflicts.empty();
}

bool
roster_merge_result::has_non_content_conflicts() const
{
  return missing_root_conflict
    || !invalid_name_conflicts.empty()
    || !directory_loop_conflicts.empty()
    || !orphaned_node_conflicts.empty()
    || !multiple_name_conflicts.empty()
    || !duplicate_name_conflicts.empty()
    || !attribute_conflicts.empty();
}

int
roster_merge_result::count_unsupported_resolution() const
{
  return (missing_root_conflict ? 1 : 0)
    + invalid_name_conflicts.size()
    + directory_loop_conflicts.size()
    + multiple_name_conflicts.size()
    + attribute_conflicts.size();
}

static void
dump_conflicts(roster_merge_result const & result, string & out)
{
  if (result.missing_root_conflict)
    out += (FL("missing_root_conflict: root directory has been removed\n"))
      .str();

  dump(result.invalid_name_conflicts, out);
  dump(result.directory_loop_conflicts, out);

  dump(result.orphaned_node_conflicts, out);
  dump(result.multiple_name_conflicts, out);
  dump(result.duplicate_name_conflicts, out);

  dump(result.attribute_conflicts, out);
  dump(result.file_content_conflicts, out);
}

template <> void
dump(roster_merge_result const & result, string & out)
{
  dump_conflicts(result, out);

  string roster_part;
  dump(result.roster, roster_part);
  out += "\n\n";
  out += roster_part;
}

void
roster_merge_result::log_conflicts() const
{
  string str;
  dump_conflicts(*this, str);
  L(FL("%s") % str);
}

namespace
{
  // a wins if *(b) > a.  Which is to say that all members of b_marks are
  // ancestors of a.  But all members of b_marks are ancestors of the
  // _b_, so the previous statement is the same as saying that _no_
  // members of b_marks is an _uncommon_ ancestor of _b_.
  bool
  a_wins(set<revision_id> const & b_marks,
         set<revision_id> const & b_uncommon_ancestors)
  {
    for (set<revision_id>::const_iterator i = b_marks.begin();
         i != b_marks.end(); ++i)
      if (b_uncommon_ancestors.find(*i) != b_uncommon_ancestors.end())
        return false;
    return true;
  }

  // returns true if merge was successful ('result' is valid), false otherwise
  // ('conflict_descriptor' is valid).
  template <typename T, typename C> bool
  merge_scalar(T const & left,
               set<revision_id> const & left_marks,
               set<revision_id> const & left_uncommon_ancestors,
               T const & right,
               set<revision_id> const & right_marks,
               set<revision_id> const & right_uncommon_ancestors,
               T & result,
               C & conflict_descriptor)
  {
    if (left == right)
      {
        result = left;
        return true;
      }
    MM(left_marks);
    MM(left_uncommon_ancestors);
    MM(right_marks);
    MM(right_uncommon_ancestors);
    bool left_wins = a_wins(right_marks, right_uncommon_ancestors);
    bool right_wins = a_wins(left_marks, left_uncommon_ancestors);
    // two bools means 4 cases:

    //     this is ambiguous clean merge, which is theoretically impossible.
    I(!(left_wins && right_wins));

    if (left_wins && !right_wins)
      {
        result = left;
        return true;
      }

    if (!left_wins && right_wins)
      {
        result = right;
        return true;
      }

    if (!left_wins && !right_wins)
      {
        conflict_descriptor.left = left;
        conflict_descriptor.right = right;
        return false;
      }
    I(false);
  }

  inline void
  create_node_for(node_t const & n, roster_t & new_roster)
  {
    if (is_dir_t(n))
      new_roster.create_dir_node(n->self);
    else if (is_file_t(n))
      new_roster.create_file_node(file_id(), n->self);
    else
      I(false);
  }

  inline void
  insert_if_unborn(node_t const & n,
                   marking_map const & markings,
                   set<revision_id> const & uncommon_ancestors,
                   roster_t const & parent_roster,
                   roster_t & new_roster)
  {
    revision_id const & birth = safe_get(markings, n->self).birth_revision;
    if (uncommon_ancestors.find(birth) != uncommon_ancestors.end())
      create_node_for(n, new_roster);
    else
      {
        // In this branch we are NOT inserting the node into the new roster as it
        // has been deleted from the other side of the merge.
        // In this case, output a warning if there are changes to the file on the
        // side of the merge where it still exists.
        set<revision_id> const & content_marks = safe_get(markings, n->self).file_content;
        bool found_one_ignored_content = false;
        for (set<revision_id>::const_iterator it = content_marks.begin(); it != content_marks.end(); it++)
          {
            if (uncommon_ancestors.find(*it) != uncommon_ancestors.end())
              {
                if (!found_one_ignored_content)
                  {
                    file_path fp;
                    parent_roster.get_name(n->self, fp);
                    W(F("Content changes to the file '%s'\n"
                        "will be ignored during this merge as the file has been\n"
                        "removed on one side of the merge.  Affected revisions include:") % fp);
                  }
                found_one_ignored_content = true;
                W(F("Revision: %s") % (*it));
              }
          }
      }
  }

  bool
  would_make_dir_loop(roster_t const & r, node_id nid, node_id parent)
  {
    // parent may not be fully attached yet; that's okay.  that just means
    // we'll run into a node with a null parent somewhere before we hit the
    // actual root; whether we hit the actual root or not, hitting a node
    // with a null parent will tell us that this particular attachment won't
    // create a loop.
    for (node_id curr = parent; !null_node(curr); curr = r.get_node(curr)->parent)
      {
        if (curr == nid)
          return true;
      }
    return false;
  }

  void
  assign_name(roster_merge_result & result, node_id nid,
              node_id parent, path_component name, side_t side)
  {
    // this function is reponsible for detecting structural conflicts.  by the
    // time we've gotten here, we have a node that's unambiguously decided on
    // a name; but it might be that that name does not exist (because the
    // parent dir is gone), or that it's already taken (by another node), or
    // that putting this node there would create a directory loop.  In all
    // such cases, rather than actually attach the node, we write a conflict
    // structure and leave it detached.

    // the root dir is somewhat special.  it can't be orphaned, and it can't
    // make a dir loop.  it can, however, have a name collision.
    if (null_node(parent))
      {
        I(name.empty());
        if (result.roster.has_root())
          {
            // see comments below about name collisions.
            duplicate_name_conflict c;
            // some other node has already been attached at the root location
            // so write a conflict structure with this node on the indicated
            // side of the merge and the attached node on the other side of
            // the merge. detach the previously attached node and leave both
            // conflicted nodes detached.
            switch (side)
              {
              case left_side:
                c.left_nid = nid;
                c.right_nid = result.roster.root()->self;
                break;
              case right_side:
                c.left_nid = result.roster.root()->self;
                c.right_nid = nid;
                break;
              }
            c.parent_name = make_pair(parent, name);
            result.roster.detach_node(file_path());
            result.duplicate_name_conflicts.push_back(c);
            return;
          }
      }
    else
      {
        // orphan:
        if (!result.roster.has_node(parent))
          {
            orphaned_node_conflict c;
            c.nid = nid;
            c.parent_name = make_pair(parent, name);
            result.orphaned_node_conflicts.push_back(c);
            return;
          }

        dir_t p = downcast_to_dir_t(result.roster.get_node(parent));

        // duplicate name conflict:
        // see the comment in roster_merge.hh for the analysis showing that at
        // most two nodes can participate in a duplicate name conflict.  this code
        // exploits that; after this code runs, there will be no node at the given
        // location in the tree, which means that in principle, if there were a
        // third node that _also_ wanted to go here, when we got around to
        // attaching it we'd have no way to realize it should be a conflict.  but
        // that never happens, so we don't have to keep a lookaside set of
        // "poisoned locations" or anything.
        if (p->has_child(name))
          {
            duplicate_name_conflict c;
            // some other node has already been attached at the named location
            // so write a conflict structure with this node on the indicated
            // side of the merge and the attached node on the other side of
            // the merge. detach the previously attached node and leave both
            // conflicted nodes detached.
            switch (side)
              {
              case left_side:
                c.left_nid = nid;
                c.right_nid = p->get_child(name)->self;
                break;
              case right_side:
                c.left_nid = p->get_child(name)->self;
                c.right_nid = nid;
                break;
              }
            c.parent_name = make_pair(parent, name);
            p->detach_child(name);
            result.duplicate_name_conflicts.push_back(c);
            return;
          }

        if (would_make_dir_loop(result.roster, nid, parent))
          {
            directory_loop_conflict c;
            c.nid = nid;
            c.parent_name = make_pair(parent, name);
            result.directory_loop_conflicts.push_back(c);
            return;
          }
      }
    // hey, we actually made it.  attach the node!
    result.roster.attach_node(nid, parent, name);
  }

  void
  copy_node_forward(roster_merge_result & result, node_t const & n,
                    node_t const & old_n, side_t const & side)
  {
    I(n->self == old_n->self);
    n->attrs = old_n->attrs;
    if (is_file_t(n))
      downcast_to_file_t(n)->content = downcast_to_file_t(old_n)->content;
    assign_name(result, n->self, old_n->parent, old_n->name, side);
  }

} // end anonymous namespace

void
roster_merge(roster_t const & left_parent,
             marking_map const & left_markings,
             set<revision_id> const & left_uncommon_ancestors,
             roster_t const & right_parent,
             marking_map const & right_markings,
             set<revision_id> const & right_uncommon_ancestors,
             roster_merge_result & result)
{
  L(FL("Performing a roster_merge"));

  result.clear();
  MM(left_parent);
  MM(left_markings);
  MM(right_parent);
  MM(right_markings);
  MM(result);

  // First handle lifecycles, by die-die-die merge -- our result will contain
  // everything that is alive in both parents, or alive in one and unborn in
  // the other, exactly.
  {
    parallel::iter<node_map> i(left_parent.all_nodes(), right_parent.all_nodes());
    while (i.next())
      {
        switch (i.state())
          {
          case parallel::invalid:
            I(false);

          case parallel::in_left:
            insert_if_unborn(i.left_data(),
                             left_markings, left_uncommon_ancestors, left_parent,
                             result.roster);
            break;

          case parallel::in_right:
            insert_if_unborn(i.right_data(),
                             right_markings, right_uncommon_ancestors, right_parent,
                             result.roster);
            break;

          case parallel::in_both:
            create_node_for(i.left_data(), result.roster);
            break;
          }
      }
  }

  // okay, our roster now contains a bunch of empty, detached nodes.  fill
  // them in one at a time with *-merge.
  {
    node_map::const_iterator left_i, right_i;
    parallel::iter<node_map> i(left_parent.all_nodes(), right_parent.all_nodes());
    node_map::const_iterator new_i = result.roster.all_nodes().begin();
    marking_map::const_iterator left_mi = left_markings.begin();
    marking_map::const_iterator right_mi = right_markings.begin();
    while (i.next())
      {
        switch (i.state())
          {
          case parallel::invalid:
            I(false);

          case parallel::in_left:
            {
              node_t const & left_n = i.left_data();
              // we skip nodes that aren't in the result roster (were
              // deleted in the lifecycles step above)
              if (result.roster.has_node(left_n->self))
                {
                  // attach this node from the left roster. this may cause
                  // a name collision with the previously attached node from
                  // the other side of the merge.
                  copy_node_forward(result, new_i->second, left_n, left_side);
                  ++new_i;
                }
              ++left_mi;
              break;
            }

          case parallel::in_right:
            {
              node_t const & right_n = i.right_data();
              // we skip nodes that aren't in the result roster
              if (result.roster.has_node(right_n->self))
                {
                  // attach this node from the right roster. this may cause
                  // a name collision with the previously attached node from
                  // the other side of the merge.
                  copy_node_forward(result, new_i->second, right_n, right_side);
                  ++new_i;
                }
              ++right_mi;
              break;
            }

          case parallel::in_both:
            {
              I(new_i->first == i.left_key());
              I(left_mi->first == i.left_key());
              I(right_mi->first == i.right_key());
              node_t const & left_n = i.left_data();
              marking_t const & left_marking = left_mi->second;
              node_t const & right_n = i.right_data();
              marking_t const & right_marking = right_mi->second;
              node_t const & new_n = new_i->second;
              // merge name
              {
                pair<node_id, path_component> left_name, right_name, new_name;
                multiple_name_conflict conflict(new_n->self);
                left_name = make_pair(left_n->parent, left_n->name);
                right_name = make_pair(right_n->parent, right_n->name);
                if (merge_scalar(left_name,
                                 left_marking.parent_name,
                                 left_uncommon_ancestors,
                                 right_name,
                                 right_marking.parent_name,
                                 right_uncommon_ancestors,
                                 new_name, conflict))
                  {
                    side_t winning_side;

                    if (new_name == left_name)
                      winning_side = left_side;
                    else if (new_name == right_name)
                      winning_side = right_side;
                    else
                      I(false);

                    // attach this node from the winning side of the merge. if
                    // there is a name collision the previously attached node
                    // (which is blocking this one) must come from the other
                    // side of the merge.
                    assign_name(result, new_n->self,
                                new_name.first, new_name.second, winning_side);

                  }
                else
                  {
                    // unsuccessful merge; leave node detached and save
                    // conflict object
                    result.multiple_name_conflicts.push_back(conflict);
                  }
              }
              // if a file, merge content
              if (is_file_t(new_n))
                {
                  file_content_conflict conflict(new_n->self);
                  if (merge_scalar(downcast_to_file_t(left_n)->content,
                                   left_marking.file_content,
                                   left_uncommon_ancestors,
                                   downcast_to_file_t(right_n)->content,
                                   right_marking.file_content,
                                   right_uncommon_ancestors,
                                   downcast_to_file_t(new_n)->content,
                                   conflict))
                    {
                      // successful merge
                    }
                  else
                    {
                      downcast_to_file_t(new_n)->content = file_id();
                      result.file_content_conflicts.push_back(conflict);
                    }
                }
              // merge attributes
              {
                attr_map_t::const_iterator left_ai = left_n->attrs.begin();
                attr_map_t::const_iterator right_ai = right_n->attrs.begin();
                parallel::iter<attr_map_t> attr_i(left_n->attrs,
                                                  right_n->attrs);
                while(attr_i.next())
                {
                  switch (attr_i.state())
                    {
                    case parallel::invalid:
                      I(false);
                    case parallel::in_left:
                      safe_insert(new_n->attrs, attr_i.left_value());
                      break;
                    case parallel::in_right:
                      safe_insert(new_n->attrs, attr_i.right_value());
                      break;
                    case parallel::in_both:
                      pair<bool, attr_value> new_value;
                      attribute_conflict conflict(new_n->self);
                      conflict.key = attr_i.left_key();
                      I(conflict.key == attr_i.right_key());
                      if (merge_scalar(attr_i.left_data(),
                                       safe_get(left_marking.attrs,
                                                attr_i.left_key()),
                                       left_uncommon_ancestors,
                                       attr_i.right_data(),
                                       safe_get(right_marking.attrs,
                                                attr_i.right_key()),
                                       right_uncommon_ancestors,
                                       new_value,
                                       conflict))
                        {
                          // successful merge
                          safe_insert(new_n->attrs,
                                      make_pair(attr_i.left_key(),
                                                     new_value));
                        }
                      else
                        {
                          // unsuccessful merge
                          // leave out the attr entry entirely, and save the
                          // conflict
                          result.attribute_conflicts.push_back(conflict);
                        }
                      break;
                    }

                }
              }
            }
            ++left_mi;
            ++right_mi;
            ++new_i;
            break;
          }
      }
    I(left_mi == left_markings.end());
    I(right_mi == right_markings.end());
    I(new_i == result.roster.all_nodes().end());
  }

  // now check for the possible global problems
  if (!result.roster.has_root())
    result.missing_root_conflict = true;
  else
    {
      // we can't have an illegal _MTN dir unless we have a root node in the
      // first place...
      dir_t result_root = result.roster.root();

      if (result_root->has_child(bookkeeping_root_component))
        {
          invalid_name_conflict conflict;
          node_t n = result_root->get_child(bookkeeping_root_component);
          conflict.nid = n->self;
          conflict.parent_name.first = n->parent;
          conflict.parent_name.second = n->name;
          I(n->name == bookkeeping_root_component);

          result.roster.detach_node(n->self);
          result.invalid_name_conflicts.push_back(conflict);
        }
    }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
