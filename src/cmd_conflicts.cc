// Copyright (C) 2008 - 2010, 2012 - 2014 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iostream>

#include "app_state.hh"
#include "cmd.hh"
#include "database.hh"
#include "merge_roster.hh"

using std::make_shared;
using std::set;
using std::shared_ptr;
using std::vector;

CMD_GROUP(conflicts, "conflicts", "", CMD_REF(tree),
          N_("Commands for conflict resolutions"),
          "");

struct conflicts_t
{
  roster_merge_result result;
  revision_id ancestor_rid, left_rid, right_rid;
  shared_ptr<roster_t> ancestor_roster;
  shared_ptr<roster_t> left_roster;
  shared_ptr<roster_t> right_roster;
  marking_map left_marking, right_marking;

  conflicts_t(database & db, bookkeeping_path const & file):
    left_roster(make_shared<roster_t>()),
    right_roster(make_shared<roster_t>())
  {
    result.clear(); // default constructor doesn't do this.

    result.read_conflict_file(db, file, ancestor_rid, left_rid, right_rid,
                              *left_roster, left_marking,
                              *right_roster, right_marking);
  };

  void write (database & db, lua_hooks & lua, bookkeeping_path const & file)
    {
      result.write_conflict_file
        (db, lua, file, left_rid, right_rid,
         left_roster, left_marking, right_roster, right_marking);
    };
};

typedef enum {first, remaining} show_conflicts_case_t;

static void
show_resolution(resolve_conflicts::file_resolution_t resolution,
                char const * const prefix)
{

  if (resolution.resolution != resolve_conflicts::none)
    {
      P(F(string(prefix).append(image(resolution)).c_str()));
    }
}

static void
show_conflicts(database & db, conflicts_t conflicts,
               show_conflicts_case_t show_case)
{
  // Go thru the conflicts we know how to resolve in the same order
  // merge.cc resolve_merge_conflicts outputs them.
  for (orphaned_node_conflict const & conflict
         : conflicts.result.orphaned_node_conflicts)
    {
      if (conflict.resolution.resolution == resolve_conflicts::none)
        {
          file_path name;
          if (conflicts.left_roster->has_node(conflict.nid))
            conflicts.left_roster->get_name(conflict.nid, name);
          else
            conflicts.right_roster->get_name(conflict.nid, name);

          P(F("orphaned node '%s'") % name);

          switch (show_case)
            {
            case first:
              P(F("possible resolutions:"));
              P(F("resolve_first drop"));
              P(F("resolve_first rename \"file_name\""));
              return;

            case remaining:
              break;
            }
        }
    }

  for (vector<dropped_modified_conflict>::iterator i = conflicts.result.dropped_modified_conflicts.begin();
       i != conflicts.result.dropped_modified_conflicts.end();
       ++i)
    {
      dropped_modified_conflict & conflict = *i;

      if ((conflict.left_nid != the_null_node &&
           conflict.left_resolution.resolution == resolve_conflicts::none) ||
          (conflict.right_nid != the_null_node &&
           conflict.right_resolution.resolution == resolve_conflicts::none))
        {
          file_path modified_name;

          switch (conflict.dropped_side)
            {
            case resolve_conflicts::left_side:
              conflicts.right_roster->get_name(conflict.right_nid, modified_name);
              break;

            case resolve_conflicts::right_side:
              conflicts.left_roster->get_name(conflict.left_nid, modified_name);
              break;
            }

          P(F("conflict: file '%s'") % modified_name);
          if (conflict.orphaned)
            {
              switch (conflict.dropped_side)
                {
                case resolve_conflicts::left_side:
                  P(F("orphaned on the left"));
                  P(F("modified on the right"));
                  break;

                case resolve_conflicts::right_side:
                  P(F("modified on the left"));
                  P(F("orphaned on the right"));
                }
            }
          else
            {
              switch (conflict.dropped_side)
                {
                case resolve_conflicts::left_side:
                  if (conflict.left_nid == the_null_node)
                    P(F("dropped on the left"));
                  else
                    {
                      // we can't distinguish duplicate name from recreated
                      P(F("dropped and recreated on the left"));
                    }

                  P(F("modified on the right"));
                  break;

                case resolve_conflicts::right_side:
                  P(F("modified on the left"));

                  if (conflict.right_nid == the_null_node)
                    P(F("dropped on the right"));
                  else
                    {
                      P(F("dropped and recreated on the right"));
                    }
                }
            }

          show_resolution(conflict.left_resolution, "left_");
          show_resolution(conflict.right_resolution, "right_");

          if (show_case == remaining) return;

          if (conflict.left_nid == the_null_node || conflict.right_nid == the_null_node)
            {
              // only one file involved; only need one resolution
              P(F("possible resolutions:"));
              P(F("resolve_first drop"));
              P(F("resolve_first rename"));
              P(F("resolve_first user_rename \"new_content_name\" \"new_file_name\""));

              if (!conflict.orphaned)
                {
                  P(F("resolve_first keep"));
                  P(F("resolve_first user \"name\""));
                }
              return;
            }
          else
            {
              // recreated or repeated duplicate name; need two resolutions

              P(F("possible resolutions:"));

              if (conflict.left_nid != the_null_node &&
                  conflict.left_resolution.resolution == resolve_conflicts::none)
                {
                  P(F("resolve_first_left drop"));
                  P(F("resolve_first_left rename"));
                  P(F("resolve_first_left user_rename \"new_content_name\" \"new_file_name\""));

                  if (!conflict.orphaned &&
                      conflict.right_resolution.resolution != resolve_conflicts::keep &&
                      conflict.right_resolution.resolution != resolve_conflicts::content_user)
                    {
                      P(F("resolve_first_left keep"));
                      P(F("resolve_first_left user \"name\""));
                    }
                }

              if (conflict.right_nid != the_null_node &&
                  conflict.right_resolution.resolution == resolve_conflicts::none)
                {
                  P(F("resolve_first_right drop"));
                  P(F("resolve_first_right rename"));
                  P(F("resolve_first_right user_rename \"new_content_name\" \"new_file_name\""));
                  if (!conflict.orphaned &&
                      conflict.left_resolution.resolution != resolve_conflicts::keep &&
                      conflict.left_resolution.resolution != resolve_conflicts::content_user)
                    {
                      P(F("resolve_first_right keep"));
                      P(F("resolve_first_right user \"name\""));
                    }
                }
              return;
            }
        }
    }

  for (vector<duplicate_name_conflict>::iterator i = conflicts.result.duplicate_name_conflicts.begin();
       i != conflicts.result.duplicate_name_conflicts.end();
       ++i)
    {
      duplicate_name_conflict & conflict = *i;

      if (conflict.left_resolution.resolution == resolve_conflicts::none ||
          conflict.right_resolution.resolution == resolve_conflicts::none)
        {
          file_path left_name;
          conflicts.left_roster->get_name(conflict.left_nid, left_name);
          P(F("duplicate_name %s") % left_name);

          switch (show_case)
            {
            case first:
              P(F("possible resolutions:"));

              if (conflict.left_resolution.resolution == resolve_conflicts::none)
                {
                  P(F("resolve_first_left drop"));
                  P(F("resolve_first_left keep"));
                  P(F("resolve_first_left rename \"name\""));
                  P(F("resolve_first_left user \"name\""));
                }

              if (conflict.right_resolution.resolution == resolve_conflicts::none)
                {
                  P(F("resolve_first_right drop"));
                  P(F("resolve_first_right keep"));
                  P(F("resolve_first_right rename \"name\""));
                  P(F("resolve_first_right user \"name\""));
                }
              return;

            case remaining:
              break;
            }
        }
    }

  for (vector<file_content_conflict>::iterator i = conflicts.result.file_content_conflicts.begin();
       i != conflicts.result.file_content_conflicts.end();
       ++i)
    {
      file_content_conflict & conflict = *i;

      if (conflict.resolution.resolution == resolve_conflicts::none)
        {
          file_path name;
          conflicts.left_roster->get_name(conflict.nid, name);
          P(F("content %s") % name);

          switch (show_case)
            {
            case first:
              P(F("possible resolutions:"));
              P(F("resolve_first interactive \"file_name\""));
              P(F("resolve_first user \"file_name\""));
              return;

            case remaining:
              break;
            }
        }
    }

  switch (show_case)
    {
    case first:
      {
        int const count = conflicts.result.count_unsupported_resolution();
        if (count > 0)
            P(FP("warning: %d conflict with no supported resolutions.",
                 "warning: %d conflicts with no supported resolutions.",
                 count) % count);
        else
          P(F("all conflicts resolved"));
      }
      break;

    case remaining:
      {
        int const count = conflicts.result.count_unsupported_resolution();
        if (count > 0)
          {
            P(FP("warning: %d conflict with no supported resolutions.",
                 "warning: %d conflicts with no supported resolutions.",
                 count) % count);

            content_merge_database_adaptor adaptor
              (db, conflicts.left_rid, conflicts.right_rid, conflicts.left_marking, conflicts.right_marking,
               set<revision_id> (), set<revision_id> ()); // uncommon_ancestors only used in automate

            conflicts.result.report_missing_root_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
            conflicts.result.report_invalid_name_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
            conflicts.result.report_directory_loop_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
            conflicts.result.report_orphaned_node_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
            conflicts.result.report_multiple_name_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
            conflicts.result.report_dropped_modified_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
            conflicts.result.report_attribute_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
          }
      }
      break;
    }

} // show_conflicts

enum side_t {left, right, neither};
static char const * const conflict_resolution_not_supported_msg = N_("'%s' is not a supported conflict resolution for %s");

// Call Lua merge3 hook to merge left_fid, right_fid, store result in
// result_path
static bool
do_interactive_merge(database & db,
                     lua_hooks & lua,
                     conflicts_t & conflicts,
                     node_id const nid,
                     file_id const & ancestor_fid,
                     file_id const & left_fid,
                     file_id const & right_fid,
                     bookkeeping_path const & result_path)
{
  file_path ancestor_path, left_path, right_path;

  if (!conflicts.ancestor_roster)
    {
      conflicts.ancestor_roster = make_shared<roster_t>();
      *conflicts.ancestor_roster = db.get_roster(conflicts.ancestor_rid);
    }

  conflicts.ancestor_roster->get_name(nid, ancestor_path);
  conflicts.left_roster->get_name(nid, left_path);
  conflicts.right_roster->get_name(nid, right_path);

  data merged_unpacked;
  file_data
    left_data = db.get_file_version(left_fid),
    ancestor_data = db.get_file_version(ancestor_fid),
    right_data = db.get_file_version(right_fid);

  if (lua.hook_merge3(ancestor_path, left_path, right_path, file_path(),
                      ancestor_data.inner(), left_data.inner(),
                      right_data.inner(), merged_unpacked))
    {
      write_data(result_path, merged_unpacked);
      return true;
    }

  return false;
} // do_interactive_merge

static void
set_resolution(resolve_conflicts::file_resolution_t &       resolution,
               resolve_conflicts::file_resolution_t const & other_resolution,
               args_vector const &                          args)
{
  if ("drop" == idx(args, 0)())
    {
      E(args.size() == 1, origin::user, F("too many arguments"));
      resolution.resolution = resolve_conflicts::drop;
    }
  else if ("keep" == idx(args, 0)())
    {
      E(args.size() == 1, origin::user, F("too many arguments"));
      E(other_resolution.resolution == resolve_conflicts::none ||
        other_resolution.resolution == resolve_conflicts::drop ||
        other_resolution.resolution == resolve_conflicts::rename ||
        other_resolution.resolution == resolve_conflicts::content_user_rename,
        origin::user,
        F("other resolution is %s; specify 'drop', 'rename', or 'user_rename'") %
        image(other_resolution.resolution));
      resolution.resolution = resolve_conflicts::keep;
    }
  else if ("rename" == idx(args, 0)())
    {
      E(args.size() == 2, origin::user, F("wrong number of arguments"));
      resolution.resolution  = resolve_conflicts::rename;
      resolution.rename = file_path_external(idx(args,1));
    }
  else if ("user" == idx(args, 0)())
    {
      E(args.size() == 2, origin::user, F("wrong number of arguments"));
      E(other_resolution.resolution == resolve_conflicts::none ||
        other_resolution.resolution == resolve_conflicts::drop ||
        other_resolution.resolution == resolve_conflicts::rename ||
        other_resolution.resolution == resolve_conflicts::content_user_rename,
        origin::user,
        F("other resolution is %s; specify 'drop', 'rename', or 'user_rename'") %
        image(other_resolution.resolution));

      resolution.resolution  = resolve_conflicts::content_user;
      resolution.content = new_optimal_path(idx(args,1)(), false);
    }
  else if ("user_rename" == idx(args,0)())
    {
      E(args.size() == 3, origin::user, F("wrong number of arguments"));

      resolution.resolution  = resolve_conflicts::content_user_rename;
      resolution.content = new_optimal_path(idx(args,1)(), false);
      resolution.rename = file_path_external(idx(args,2));
    }
  else
    E(false, origin::user,
      F(conflict_resolution_not_supported_msg) % idx(args,0) % "duplicate_name");

} // set_resolution

static void
set_first_conflict(database & db,
                   lua_hooks & lua,
                   conflicts_t & conflicts,
                   args_vector const & args,
                   side_t side)
{
  E(args.size() > 0, origin::user, F("wrong number of arguments"));

  if (side != neither)
    {
      for (vector<dropped_modified_conflict>::iterator i = conflicts.result.dropped_modified_conflicts.begin();
           i != conflicts.result.dropped_modified_conflicts.end();
           ++i)
        {
          dropped_modified_conflict & conflict = *i;

          // here we only allow two resolutions; single resolutions are handled below

          switch (side)
            {
            case left:
              if (conflict.left_resolution.resolution == resolve_conflicts::none)
                {
                  E(conflict.left_nid != the_null_node, origin::user,
                    F("must specify resolve_first (not _left or _right)"));

                  if ("keep" == idx(args,0)())
                    E(!conflict.orphaned, origin::user, F("orphaned files must be renamed"));

                  set_resolution(conflict.left_resolution, conflict.right_resolution, args);
                  return;
                }
              break;
            case right:
              if (conflict.right_resolution.resolution == resolve_conflicts::none)
                {
                  E(conflict.right_nid != the_null_node, origin::user,
                    F("must specify resolve_first (not _left or _right)"));

                  if ("keep" == idx(args,0)())
                    E(!conflict.orphaned, origin::user, F("orphaned files must be renamed"));

                  set_resolution(conflict.right_resolution, conflict.left_resolution, args);
                  return;
                }
              break;
            case neither:
              // can't get here
              break;
            }
        }

      for (vector<duplicate_name_conflict>::iterator i = conflicts.result.duplicate_name_conflicts.begin();
           i != conflicts.result.duplicate_name_conflicts.end();
           ++i)
        {
          duplicate_name_conflict & conflict = *i;

          switch (side)
            {
            case left:
              if (conflict.left_resolution.resolution == resolve_conflicts::none)
                {
                  set_resolution(conflict.left_resolution, conflict.right_resolution, args);
                  return;
                }
              break;

            case right:
              if (conflict.right_resolution.resolution == resolve_conflicts::none)
                {
                  set_resolution(conflict.right_resolution, conflict.left_resolution, args);
                  return;
                }
              break;

            case neither:
              I(false);
            }
        }
    }

  if (side == neither)
    {
      for (vector<orphaned_node_conflict>::iterator i = conflicts.result.orphaned_node_conflicts.begin();
           i != conflicts.result.orphaned_node_conflicts.end();
           ++i)
        {
          orphaned_node_conflict & conflict = *i;

          if (conflict.resolution.resolution == resolve_conflicts::none)
            {
              if ("drop" == idx(args,0)())
                {
                  E(args.size() == 1, origin::user, F("wrong number of arguments"));

                  conflict.resolution.resolution  = resolve_conflicts::drop;
                }
              else if ("rename" == idx(args,0)())
                {
                  E(args.size() == 2, origin::user, F("wrong number of arguments"));

                  conflict.resolution.resolution  = resolve_conflicts::rename;
                  conflict.resolution.rename = file_path_external(idx(args,1));
                }
              else
                {
                  E(false, origin::user,
                    F(conflict_resolution_not_supported_msg) % idx(args,0) % "orphaned_node");
                }
              return;
            }
        }

      for (vector<dropped_modified_conflict>::iterator i = conflicts.result.dropped_modified_conflicts.begin();
           i != conflicts.result.dropped_modified_conflicts.end();
           ++i)
        {
          dropped_modified_conflict & conflict = *i;

          // Here we only allow single resolutions; two resolutions are handled above

          switch (conflict.dropped_side)
            {
            case resolve_conflicts::left_side:
              E(conflict.left_nid == the_null_node, origin::user,
                F("must specify 'resolve_first_left' or 'resolve_first_right' (not just 'resolve_first')"));

              // the left side stays dropped; we either drop, keep or replace the right side
              if (conflict.right_resolution.resolution == resolve_conflicts::none)
                {
                  if ("drop" == idx(args,0)())
                    {
                      E(args.size() == 1, origin::user, F("wrong number of arguments"));

                      conflict.right_resolution.resolution = resolve_conflicts::drop;
                    }
                  else if ("keep" == idx(args,0)())
                    {
                      E(args.size() == 1, origin::user, F("wrong number of arguments"));
                      E(!conflict.orphaned, origin::user, F("orphaned files must be renamed"));

                      conflict.right_resolution.resolution = resolve_conflicts::keep;
                    }
                  else if ("user" == idx(args,0)())
                    {
                      E(args.size() == 2, origin::user, F("wrong number of arguments"));
                      E(!conflict.orphaned, origin::user, F("orphaned files must be renamed"));

                      conflict.right_resolution.resolution  = resolve_conflicts::content_user;
                      conflict.right_resolution.content = new_optimal_path(idx(args,1)(), false);
                    }
                  else if ("rename" == idx(args,0)())
                    {
                      E(args.size() == 2, origin::user, F("wrong number of arguments"));

                      conflict.right_resolution.resolution  = resolve_conflicts::rename;
                      conflict.right_resolution.rename = file_path_external(idx(args,1));
                    }
                  else if ("user_rename" == idx(args,0)())
                    {
                      E(args.size() == 3, origin::user, F("wrong number of arguments"));

                      conflict.right_resolution.resolution  = resolve_conflicts::content_user_rename;
                      conflict.right_resolution.content = new_optimal_path(idx(args,1)(), false);
                      conflict.right_resolution.rename = file_path_external(idx(args,2));
                    }
                  else
                    {
                      E(false, origin::user,
                        F(conflict_resolution_not_supported_msg) % idx(args,0) % "dropped_modified");
                    }
                  return;
                }
              break;

            case resolve_conflicts::right_side:
              E(conflict.right_nid == the_null_node, origin::user,
                F("must specify 'resolve_first_left' or 'resolve_first_right' (not just 'resolve_first')"));

              // the right side stays dropped; we either drop, keep or replace the left side
              if (conflict.left_resolution.resolution == resolve_conflicts::none)
                {
                  if ("drop" == idx(args,0)())
                    {
                      E(args.size() == 1, origin::user, F("wrong number of arguments"));

                      conflict.left_resolution.resolution = resolve_conflicts::drop;
                    }
                  else if ("keep" == idx(args,0)())
                    {
                      E(args.size() == 1, origin::user, F("wrong number of arguments"));
                      E(!conflict.orphaned, origin::user, F("orphaned files must be renamed"));

                      conflict.left_resolution.resolution  = resolve_conflicts::keep;
                    }
                  else if ("user" == idx(args,0)())
                    {
                      E(args.size() == 2, origin::user, F("wrong number of arguments"));
                      E(!conflict.orphaned, origin::user, F("orphaned files must be renamed"));

                      conflict.left_resolution.resolution  = resolve_conflicts::content_user;
                      conflict.left_resolution.content = new_optimal_path(idx(args,1)(), false);
                    }
                  else if ("rename" == idx(args,0)())
                    {
                      E(args.size() == 2, origin::user, F("wrong number of arguments"));

                      conflict.left_resolution.resolution  = resolve_conflicts::rename;
                      conflict.left_resolution.rename = file_path_external(idx(args,1));
                    }
                  else if ("user_rename" == idx(args,0)())
                    {
                      E(args.size() == 3, origin::user, F("wrong number of arguments"));

                      conflict.left_resolution.resolution  = resolve_conflicts::content_user_rename;
                      conflict.left_resolution.content = new_optimal_path(idx(args,1)(), false);
                      conflict.left_resolution.rename = file_path_external(idx(args,2));
                    }
                  else
                    {
                      E(false, origin::user,
                        F(conflict_resolution_not_supported_msg) % idx(args,0) % "dropped_modified");
                    }
                  return;
                }
              break;
            }
        }

      for (vector<file_content_conflict>::iterator i = conflicts.result.file_content_conflicts.begin();
           i != conflicts.result.file_content_conflicts.end();
           ++i)
        {
          file_content_conflict & conflict = *i;

          if (conflict.resolution.resolution == resolve_conflicts::none)
            {
              if ("interactive" == idx(args,0)())
                {
                  bookkeeping_path result_path;

                  switch (args.size())
                    {
                    case 1:
                      // use default path for resolution file
                      {
                        file_path left_path;
                        conflicts.left_roster->get_name(conflict.nid, left_path);
                        result_path = bookkeeping_resolutions_dir / left_path;
                      }
                      break;

                    case 2:
                      // user path for resolution file
                      {
                        string normalized;
                        normalize_external_path(idx(args,1)(),
                                                normalized,
                                                false); // to_workspace_root
                        result_path = bookkeeping_path(normalized, origin::user);
                      }
                      break;

                    default:
                      E(false, origin::user, F("wrong number of arguments"));
                    }

                  if (do_interactive_merge(db, lua, conflicts, conflict.nid,
                                           conflict.ancestor, conflict.left,
                                           conflict.right, result_path))
                    {
                      conflict.resolution.resolution
                        = resolve_conflicts::content_user;
                      conflict.resolution.content
                        = make_shared<bookkeeping_path>(result_path);
                      P(F("interactive merge result saved in '%s'")
                        % result_path.as_internal());
                    }
                  else
                    P(F("interactive merge failed."));
                }
              else if ("user" == idx(args,0)())
                {
                  E(args.size() == 2, origin::user, F("wrong number of arguments"));

                  conflict.resolution.resolution  = resolve_conflicts::content_user;
                  conflict.resolution.content = new_optimal_path(idx(args,1)(), false);
                }
              else
                {
                  // We don't allow the user to specify 'resolved_internal'; that
                  // is only done by automate show_conflicts.
                  E(false, origin::user,
                    F(conflict_resolution_not_supported_msg) % idx(args,0) % "file_content");
                }
              return;
            }
        }
    }

  switch (side)
    {
    case left:
      E(false, origin::user, F("no resolvable yet unresolved left side conflicts"));
      break;

    case right:
      E(false, origin::user, F("no resolvable yet unresolved right side conflicts"));
      break;

    case neither:
      E(false, origin::user, F("no resolvable yet unresolved single-file conflicts"));
      break;
    }

} // set_first_conflict


/// commands

// CMD(store) is in cmd_merging.cc, since it needs access to
// show_conflicts_core, and doesn't need conflicts_t.

CMD_PRESET_OPTIONS(show_first)
{
  opts.pager = have_smart_terminal();
}
CMD(show_first, "show_first", "", CMD_REF(conflicts),
    "",
    N_("Show the first unresolved conflict in the conflicts file, and possible resolutions"),
    "",
    options::opts::conflicts_opts | options::opts::pager)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  E(args.size() == 0, origin::user, F("wrong number of arguments"));
  show_conflicts(db, conflicts, first);
}

CMD_PRESET_OPTIONS(show_remaining)
{
  opts.pager = have_smart_terminal();
}
CMD(show_remaining, "show_remaining", "", CMD_REF(conflicts),
    "",
    N_("Show the remaining unresolved conflicts in the conflicts file"),
    "",
    options::opts::conflicts_opts | options::opts::pager)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  E(args.size() == 0, origin::user, F("wrong number of arguments"));
  show_conflicts(db, conflicts, remaining);
}

CMD(resolve_first, "resolve_first", "", CMD_REF(conflicts),
    N_("RESOLUTION"),
    N_("Set the resolution for the first unresolved single-file conflict."),
    "Use 'mtn conflicts show_first' to see possible resolutions.",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  set_first_conflict(db, app.lua, conflicts, args, neither);

  conflicts.write (db, app.lua, app.opts.conflicts_file);
}

CMD(resolve_first_left, "resolve_first_left", "", CMD_REF(conflicts),
    N_("RESOLUTION"),
    N_("Set the left resolution for the first unresolved two-file conflict"),
    "",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  set_first_conflict(db, app.lua, conflicts, args, left);

  conflicts.write (db, app.lua, app.opts.conflicts_file);
}

CMD(resolve_first_right, "resolve_first_right", "", CMD_REF(conflicts),
    N_("RESOLUTION"),
    N_("Set the right resolution for the first unresolved two-file conflict"),
    "",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  set_first_conflict(db, app.lua, conflicts, args, right);

  conflicts.write (db, app.lua, app.opts.conflicts_file);
}

CMD(clean, "clean", "", CMD_REF(conflicts),
    "",
    N_("Delete any bookkeeping files related to conflict resolution"),
    "",
    options::opts::none)
{
  if (path_exists(bookkeeping_conflicts_file))
    delete_file(bookkeeping_conflicts_file);

  if (path_exists(bookkeeping_resolutions_dir))
    delete_dir_recursive(bookkeeping_resolutions_dir);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
