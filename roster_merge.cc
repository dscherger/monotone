// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>

#include "vocab.hh"
#include "roster_merge.hh"
#include "parallel_iter.hh"
#include "safe_map.hh"

bool
roster_merge_result::is_clean()
{
  return node_name_conflicts.empty()
    && file_content_conflicts.empty()
    && node_attr_conflicts.empty()
    && orphaned_node_conflicts.empty()
    && rename_target_conflicts.empty();
}

void
roster_merge_result::clear()
{
  node_attr_conflicts.clear();
  file_content_conflicts.clear();
  node_attr_conflicts.clear();
  orphaned_node_conflicts.clear();
  rename_target_conflicts.clear();
  roster = roster_t();
}

namespace 
{
  // a wins if *(b) > a.  Which is to say that all members of b_marks are
  // ancestors of a.  But all members of b_marks are ancestors of the
  // _b_, so the previous statement is the same as saying that _no_
  // members of b_marks is an _uncommon_ ancestor of _b_.
  bool
  a_wins(std::set<revision_id> const & b_marks,
         std::set<revision_id> const & b_uncommon_ancestors)
  {
    for (std::set<revision_id>::const_iterator i = b_marks.begin();
         i != b_marks.end(); ++i)
      if (b_uncommon_ancestors.find(*i) != b_uncommon_ancestors.end())
        return false;
    return true;
  }

  // returns true if merge was successful ('result' is valid), false otherwise
  // ('conflict_descriptor' is valid).
  template <typename T, typename C> bool
  merge_scalar(T const & left,
               std::set<revision_id> const & left_marks,
               std::set<revision_id> const & left_uncommon_ancestors,
               T const & right,
               std::set<revision_id> const & right_marks,
               std::set<revision_id> const & right_uncommon_ancestors,
               T & result,
               C & conflict_descriptor)
  {
    if (left == right)
      {
        result = left;
        return true;
      }
    bool left_wins = a_wins(right_marks, right_uncommon_ancestors);
    bool right_wins = a_wins(left_marks, left_uncommon_ancestors);
    // two bools means 4 cases:
    //   left_wins && right_wins
    //     this is ambiguous clean merge, which is theoretically impossible.
    I(!(left_wins && right_wins));
    //   left_wins && !right_wins
    if (left_wins && !right_wins)
      {
        result = left;
        return true;
      }
    //   !left_wins && right_wins
    if (!left_wins && right_wins)
      {
        result = right;
        return true;
      }
    //   !left_wins && !right_wins
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
                   marking_map const & marking,
                   std::set<revision_id> const & uncommon_ancestors,
                   roster_t & new_roster)
  {
    revision_id const & birth = safe_get(marking, n->self).birth_revision;
    if (uncommon_ancestors.find(birth) != uncommon_ancestors.end())
      create_node_for(n, new_roster);
  }
  
  void
  copy_node_forward(node_t const & n, roster_t & new_roster,
                    node_t const & old_n)
  {
    I(n->self == old_n->self);
    n->attrs = old_n->attrs;
    if (is_file_t(n))
      downcast_to_file_t(n)->content = downcast_to_file_t(old_n)->content;
    // FIXME: this could hit a conflict!  (orphan or rename-target)
    new_roster.attach_node(n->self, old_n->parent, old_n->name);
  }
  
} // end anonymous namespace

void
roster_merge(roster_t const & left_parent,
             marking_map const & left_marking,
             std::set<revision_id> const & left_uncommon_ancestors,
             roster_t const & right_parent,
             marking_map const & right_marking,
             std::set<revision_id> const & right_uncommon_ancestors,
             roster_merge_result & result)
{

  result.clear();
  MM(left_parent);
  MM(left_marking);
  MM(right_parent);
  MM(right_marking);
  MM(result.roster);
  
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
                             left_marking, left_uncommon_ancestors,
                             result.roster);
            break;

          case parallel::in_right:
            insert_if_unborn(i.right_data(),
                             right_marking, right_uncommon_ancestors,
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
    marking_map::const_iterator left_mi = left_marking.begin();
    marking_map::const_iterator right_mi = right_marking.begin();
    while (i.next())
      {
        switch (i.state())
          {
          case parallel::invalid:
            I(false);

          case parallel::in_left:
            copy_node_forward(new_i->second, result.roster, i.left_data());
            ++left_mi;
            break;

          case parallel::in_right:
            copy_node_forward(new_i->second, result.roster, i.right_data());
            ++right_mi;
            break;

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
                std::pair<node_id, path_component> new_name;
                node_name_conflict conflict(new_n->self);
                if (merge_scalar(std::make_pair(left_n->parent, left_n->name),
                                 left_marking.parent_name,
                                 left_uncommon_ancestors,
                                 std::make_pair(right_n->parent, right_n->name),
                                 right_marking.parent_name,
                                 right_uncommon_ancestors,
                                 new_name, conflict))
                  {
                    // FIXME: this could hit a conflict! (orphan or rename-target)
                    result.roster.attach_node(new_n->self,
                                              new_name.first, new_name.second);
                  }
                else
                  {
                    // unsuccessful merge; leave node detached and save
                    // conflict object
                    result.node_name_conflicts.push_back(conflict);
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
                full_attr_map_t::const_iterator left_ai = left_n->attrs.begin();
                full_attr_map_t::const_iterator right_ai = right_n->attrs.begin();
                parallel::iter<full_attr_map_t> attr_i(left_n->attrs,
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
                      safe_insert(new_n->attrs, attr_i.left_value());
                      break;
                    case parallel::in_both:
                      std::pair<bool, attr_value> new_value;
                      node_attr_conflict conflict(new_n->self);
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
                                      std::make_pair(attr_i.left_key(),
                                                     new_value));
                        }
                      else
                        {
                          // unsuccessful merge
                          // leave out the attr entry entirely, and save the
                          // conflict
                          result.node_attr_conflicts.push_back(conflict);
                        }
                      break;
                    }
                  
                }
              }
            }
            ++left_mi;
            ++right_mi;
            break;
          }
        ++new_i;
      }
  }

  // FIXME: looped nodes here
}

