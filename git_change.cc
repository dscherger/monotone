// Copyright (C) 2009 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "git_change.hh"
#include "parallel_iter.hh"
#include "roster.hh"

#include <stack>

using std::make_pair;
using std::map;
using std::stack;
using std::string;
using std::vector;

void
get_change(roster_t const & left, roster_t const & right,
           git_change & change)
{
  typedef full_attr_map_t::const_iterator attr_iterator;
  static attr_key exe_attr("mtn:execute");

  parallel::iter<node_map> i(left.all_nodes(), right.all_nodes());
  while (i.next())
    {
      MM(i);
      switch (i.state())
        {
        case parallel::invalid:
          I(false);

        case parallel::in_left:
          // deleted
          if (is_file_t(i.left_data()))
            {
              file_path path;
              left.get_name(i.left_key(), path);
              change.deletions.push_back(path);
            }
          break;

        case parallel::in_right:
          // added
          if (is_file_t(i.right_data()))
            {
              file_t file = downcast_to_file_t(i.right_data());

              attr_iterator exe = file->attrs.find(exe_attr);

              string mode = "100644";
              if (exe != file->attrs.end() &&
                  exe->second.first && // live attr
                  exe->second.second() == "true")
                mode = "100755";

              file_path path;
              right.get_name(i.right_key(), path);
              change.additions.push_back(git_add(path,
                                                 file->content,
                                                 mode));
            }
          break;

        case parallel::in_both:
          // moved/renamed/patched/attribute changes
          if (is_file_t(i.left_data()))
            {
              file_t left_file = downcast_to_file_t(i.left_data());
              file_t right_file = downcast_to_file_t(i.right_data());

              attr_iterator left_attr = left_file->attrs.find(exe_attr);
              attr_iterator right_attr = right_file->attrs.find(exe_attr);

              string left_mode = "100644";
              string right_mode = "100644";

              if (left_attr != left_file->attrs.end() &&
                  left_attr->second.first && // live attr
                  left_attr->second.second() == "true")
                left_mode = "100755";

              if (right_attr != right_file->attrs.end() &&
                  right_attr->second.first && // live attr
                  right_attr->second.second() == "true")
                right_mode = "100755";

              file_path left_path, right_path;
              left.get_name(i.left_key(), left_path);
              right.get_name(i.right_key(), right_path);

              if (left_path != right_path)
                change.renames.push_back(make_pair(left_path,
                                                   right_path));

              // git handles content changes as additions
              if (left_file->content != right_file->content ||
                  left_mode != right_mode)
                change.additions.push_back(git_add(right_path,
                                                   right_file->content,
                                                   right_mode));
            }
          break;
        }
    }
}

// re-order renames so that they occur in the correct order
// i.e. rename a->b + rename b->c will be re-ordered as
//      rename b->c + rename a->b
// this will also insert temporary names to resolve circular
// renames and name swaps:
// i.e. rename a->b + rename b->a will be re-ordered as
//      rename a->tmp + rename b->a + rename tmp->b
void
reorder_renames(vector<git_rename> const & renames,
                vector<git_rename> & reordered_renames)
{
  typedef map<file_path, file_path> map_type;

  map_type rename_map;

  for (rename_iterator i = renames.begin(); i != renames.end(); ++i)
    rename_map.insert(*i);

  while (!rename_map.empty())
    {
      map_type::iterator i = rename_map.begin();
      I(i != rename_map.end());
      git_rename base(*i);
      rename_map.erase(i);

      map_type::iterator next = rename_map.find(base.second);
      stack<git_rename> rename_stack;

      // stack renames so their order can be reversed
      while (next != rename_map.end())
        {
          git_rename rename(*next);
          rename_stack.push(rename);
          rename_map.erase(next);
          next = rename_map.find(rename.second);
        }

      // break rename loops
      if (!rename_stack.empty())
        {
          git_rename const & top = rename_stack.top();
          // if there is a loop push another rename onto the stack that
          // renames the old base to a temporary and adjust the base
          // rename to account for this
          if (base.first == top.second)
            {
              // the temporary path introduced here is pretty weak in
              // terms of random filenames but should suffice for the
              // already rare situations where any of this is required.
              string path = top.second.as_internal();
              path += ".tmp.break-rename-loop";
              file_path tmp = file_path_internal(path);
              rename_stack.push(git_rename(base.first, tmp));
              base.first = tmp;
            }
        }

      // insert the stacked renames in reverse order
      while (!rename_stack.empty())
        {
          git_rename rename = rename_stack.top();
          rename_stack.pop();
          reordered_renames.push_back(rename);
        }

      reordered_renames.push_back(base);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
