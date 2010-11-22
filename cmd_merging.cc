// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//               2008, 2010 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <cstring>
#include <iostream>
#include <iomanip>

#include "basic_io.hh"
#include "cmd.hh"
#include "diff_output.hh"
#include "merge_content.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "merge_roster.hh"
#include "transforms.hh"
#include "update.hh"
#include "work.hh"
#include "safe_map.hh"
#include "app_state.hh"
#include "maybe_workspace_updater.hh"
#include "project.hh"
#include "simplestring_xform.hh"
#include "keys.hh"
#include "key_store.hh"
#include "database.hh"
#include "vocab_cast.hh"

using std::cout;
using std::make_pair;
using std::map;
using std::set;
using std::string;
using std::vector;
using std::strlen;

using boost::shared_ptr;

static void
add_dormant_attrs(const_node_t parent, node_t child)
{
  for (attr_map_t::const_iterator i = parent->attrs.begin();
       i != parent->attrs.end(); ++i)
    {
      // if the child does not have the associated attr add a dormant one
      if (child->attrs.find(i->first) == child->attrs.end())
        safe_insert(child->attrs,
                    make_pair(i->first, make_pair(false, attr_value())));
    }
}

static void
three_way_merge(revision_id const & ancestor_rid, roster_t const & ancestor_roster,
                revision_id const & left_rid, roster_t const & left_roster,
                revision_id const & right_rid, roster_t const & right_roster,
                roster_merge_result & result,
                marking_map & left_markings,
                marking_map & right_markings)
{
  MM(ancestor_roster);
  MM(left_roster);
  MM(right_roster);

  MM(ancestor_rid);
  MM(left_rid);
  MM(right_rid);

  // for this to work correctly attrs that exist in the ancestor *must*
  // exist in both children, since attrs are never deleted they are only
  // marked as dormant. however, since this may be any arbitrary arrangement
  // of three revisions it is possible that attrs do exist in the parent and
  // not in the children. in this case the attrs must be added to the
  // children as dormant so that roster_merge works correctly.

  roster_t left_with_attrs(left_roster);
  roster_t right_with_attrs(right_roster);

  MM(left_with_attrs);
  MM(right_with_attrs);

  node_map const & nodes = ancestor_roster.all_nodes();

  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      if (left_with_attrs.has_node(i->first))
        add_dormant_attrs(i->second, left_with_attrs.get_node_for_update(i->first));
      if (right_with_attrs.has_node(i->first))
        add_dormant_attrs(i->second, right_with_attrs.get_node_for_update(i->first));
    }

  // Mark up the ANCESTOR
  marking_map ancestor_markings; MM(ancestor_markings);
  mark_roster_with_no_parents(ancestor_rid, ancestor_roster, ancestor_markings);

  // Mark up the LEFT roster
  left_markings.clear();
  MM(left_markings);
  mark_roster_with_one_parent(ancestor_roster, ancestor_markings,
                              left_rid, left_with_attrs, left_markings);

  // Mark up the RIGHT roster
  right_markings.clear();
  MM(right_markings);
  mark_roster_with_one_parent(ancestor_roster, ancestor_markings,
                              right_rid, right_with_attrs, right_markings);

  // Make the synthetic graph, by creating uncommon ancestor sets
  std::set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;
  safe_insert(left_uncommon_ancestors, left_rid);
  safe_insert(right_uncommon_ancestors, right_rid);

  P(F("[left]  %s") % left_rid);
  P(F("[right] %s") % right_rid);

  // And do the merge
  roster_merge(left_with_attrs, left_markings, left_uncommon_ancestors,
               right_with_attrs, right_markings, right_uncommon_ancestors,
               result);
}

static bool
pick_branch_for_update(options & opts,
                       project_t & project, revision_id chosen_rid)
{
  bool switched_branch = false;

  // figure out which branches the target is in
  set<branch_name> branches;
  project.get_revision_branches(chosen_rid, branches);

  if (!opts.ignore_suspend_certs)
    {
      vector<cert> suspend_certs;
      project.db.get_revision_certs(chosen_rid, suspend_cert_name, suspend_certs);

      for (vector<cert>::const_iterator i = suspend_certs.begin();
           i != suspend_certs.end(); i++)
        {
          branch_uid const the_uid = typecast_vocab<branch_uid>(i->value);
          branch_name susp_branch = project.translate_branch(the_uid);
          set<branch_name>::iterator pos = branches.find(susp_branch);
          if (pos != branches.end())
            {
              branches.erase(pos);
            }
        }
    }

  if (branches.find(opts.branch) != branches.end())
    {
      L(FL("using existing branch %s") % opts.branch());
    }
  else
    {
      P(F("target revision is not in current branch"));
      if (branches.size() > 1)
        {
          // multiple non-matching branchnames
          string branch_list;
          for (set<branch_name>::const_iterator i = branches.begin();
               i != branches.end(); i++)
            branch_list += "\n  " + (*i)();
          E(false, origin::user,
            F("target revision is in multiple branches:%s\n\n"
              "try again with explicit --branch") % branch_list);
        }
      else if (branches.size() == 1)
        {
          // one non-matching, inform and update
          opts.branch = *(branches.begin());
          switched_branch = true;
        }
      else
        {
          W(F("target revision not in any branch\n"
              "next commit will use branch %s")
            % opts.branch);
        }
    }
  return switched_branch;
}

// also used from maybe_workspace_updater.cc
void
update(app_state & app,
       args_vector const & args)
{
  database db(app);
  workspace work(app);
  project_t project(db, app.lua, app.opts);

  // Figure out where we are
  parent_map parents;
  work.get_parent_rosters(db, parents);

  E(parents.size() == 1, origin::user,
    F("this command can only be used in a single-parent workspace"));

  revision_id old_rid = parent_id(parents.begin());
  E(!null_id(old_rid), origin::user,
    F("this workspace is a new project; cannot update"));

  // Figure out where we're going
  E(!app.opts.branch().empty(), origin::user,
    F("cannot determine branch for update"));
  MM(app.opts.branch);

  revision_id chosen_rid;
  if (app.opts.revision.empty())
    {
      P(F("updating along branch '%s'") % app.opts.branch);
      set<revision_id> candidates;
      pick_update_candidates(app.lua, project, candidates, old_rid,
                             app.opts.branch,
                             app.opts.ignore_suspend_certs);
      E(!candidates.empty(), origin::user,
        F("your request matches no descendents of the current revision\n"
          "in fact, it doesn't even match the current revision\n"
          "maybe you want something like --revision=h:%s")
        % app.opts.branch);
      if (candidates.size() != 1)
        {
          P(F("multiple update candidates:"));
          for (set<revision_id>::const_iterator i = candidates.begin();
               i != candidates.end(); ++i)
            P(i18n_format("  %s")
              % describe_revision(app.opts, app.lua, project, *i));
          P(F("choose one with '%s update -r<id>'") % prog_name);
          E(false, origin::user,
            F("multiple update candidates remain after selection"));
        }
      chosen_rid = *(candidates.begin());
    }
  else
    {
      complete(app.opts, app.lua, project, app.opts.revision[0](), chosen_rid);
    }
  I(!null_id(chosen_rid));

  // do this notification before checking to see if we can bail out early,
  // because when you are at one of several heads, and you hit update, you
  // want to know that merging would let you update further.
  notify_if_multiple_heads(project,
                           app.opts.branch, app.opts.ignore_suspend_certs);

  if (old_rid == chosen_rid)
    {
      P(F("already up to date at %s") % old_rid);
      // do still switch the workspace branch, in case they have used
      // update to switch branches.
      work.set_options(app.opts, app.lua, true);
      return;
    }

  P(F("selected update target %s") % chosen_rid);

  // Fiddle around with branches, in an attempt to guess what the user
  // wants.
  bool switched_branch = pick_branch_for_update(app.opts, project, chosen_rid);
  if (switched_branch)
    P(F("switching to branch %s") % app.opts.branch());

  // Okay, we have a target, we have a branch, let's do this merge!

  // We have:
  //
  //    old  --> working
  //     |         |
  //     V         V
  //  chosen --> merged
  //
  // - old is the revision specified in _MTN/revision
  // - working is based on old and includes the workspace's changes
  // - chosen is the revision we're updating to and will end up in _MTN/revision
  // - merged is the merge of working and chosen, that will become the new
  //   workspace
  //
  // we apply the working to merged cset to the workspace
  // and write the cset from chosen to merged changeset in _MTN/work

  temp_node_id_source nis;

  // Get the OLD and WORKING rosters
  roster_t_cp old_roster
    = parent_cached_roster(parents.begin()).first;
  MM(*old_roster);

  shared_ptr<roster_t> working_roster = shared_ptr<roster_t>(new roster_t());

  MM(*working_roster);
  work.get_current_roster_shape(db, nis, *working_roster);
  work.update_current_roster_from_filesystem(*working_roster);

  revision_t working_rev;
  revision_id working_rid;
  make_revision_for_workspace(parents, *working_roster, working_rev);
  calculate_ident(working_rev, working_rid);

  // Get the CHOSEN roster
  roster_t chosen_roster; MM(chosen_roster);
  db.get_roster(chosen_rid, chosen_roster);


  // And finally do the merge
  roster_merge_result result;
  marking_map left_markings, right_markings;
  three_way_merge(old_rid, *old_roster,
                  working_rid, *working_roster,
                  chosen_rid, chosen_roster,
                  result, left_markings, right_markings);

  roster_t & merged_roster = result.roster;

  map<file_id, file_path> paths;
  get_content_paths(*working_roster, paths);

  content_merge_workspace_adaptor wca(db, old_rid, old_roster,
                                      left_markings, right_markings, paths);
  wca.cache_roster(working_rid, working_roster);
  resolve_merge_conflicts(app.lua, app.opts, *working_roster, chosen_roster,
                          result, wca, false);

  // Make sure it worked...
  I(result.is_clean());
  merged_roster.check_sane(true);

  // Now finally modify the workspace
  cset update;
  make_cset(*working_roster, merged_roster, update);
  work.perform_content_update(*working_roster, merged_roster, update, wca, true,
                              app.opts.move_conflicting_paths);

  revision_t remaining;
  make_revision_for_workspace(chosen_rid, chosen_roster,
                              merged_roster, remaining);

  // small race condition here... FIXME: what is it?
  work.put_update_id(old_rid);
  work.put_work_rev(remaining);
  work.maybe_update_inodeprints(db);
  work.set_options(app.opts, app.lua, true);

  if (switched_branch)
    P(F("switched branch; next commit will use branch %s") % app.opts.branch());
  P(F("updated to base revision %s") % chosen_rid);
}

CMD(update, "update", "", CMD_REF(workspace), "",
    N_("Updates the workspace"),
    N_("This command modifies your workspace to be based off of a "
       "different revision, preserving uncommitted changes as it does so.  "
       "If a revision is given, update the workspace to that revision.  "
       "If not, update the workspace to the head of the branch."),
    options::opts::branch | options::opts::revision |
    options::opts::move_conflicting_paths)
{
  if (!args.empty())
    throw usage(execid);

  if (app.opts.revision.size() > 1)
    throw usage(execid);

  update(app, args);
}

CMD_AUTOMATE(update, "",
             N_("Updates the workspace"),
             "",
             options::opts::branch | options::opts::revision |
             options::opts::move_conflicting_paths)
{
  E(args.empty(), origin::user,
    F("wrong argument count"));

  E(app.opts.revision.size() <= 1, origin::user,
    F("at most one revision selector may be specified"));

  update(app, args);
}

// Subroutine of CMD(merge) and CMD(explicit_merge).  Merge LEFT with RIGHT,
// placing results onto BRANCH.  Note that interactive_merge_and_store may
// bomb out, and therefore so may this.
static void
merge_two(options & opts, lua_hooks & lua, project_t & project,
          key_store & keys,
          revision_id const & left, revision_id const & right,
          branch_name const & branch, string const & caller,
          std::ostream & output, bool automate)
{
  // The following mess constructs a neatly formatted log message that looks
  // like this:
  //    CALLER of 'LEFT'
  //          and 'RIGHT'
  //    to branch 'BRANCH'
  // where the last line is left out if we're merging onto the current branch.
  // We use a stringstream because boost::format does not support %-*s.
  using std::ostringstream;
  using std::setw;
  using std::max;

  ostringstream log;
  utf8 log_message("");
  bool log_message_given;
  size_t fieldwidth = max(caller.size() + strlen(" of '"), strlen("and '"));

  if (branch != opts.branch)
    fieldwidth = max(fieldwidth, strlen("to branch '"));

  log << setw(fieldwidth - strlen(" of '")) << caller << " of '" << left
      << "'\n" << setw(fieldwidth) << "and '" << right
      << "'\n";

  if (branch != opts.branch)
    log << setw(fieldwidth) << "to branch '" << branch << "'\n";

  process_commit_message_args(opts, log_message_given, log_message,
                              utf8(log.str(), origin::internal));

  // Now it's time for the real work.
  if (automate)
    {
      output << left << " " << right << " ";
    }
  else
    {
      P(F("[left]  %s") % left);
      P(F("[right] %s") % right);
    }

  revision_id merged;
  transaction_guard guard(project.db);
  interactive_merge_and_store(lua, project.db, opts, left, right, merged);

  project.put_standard_certs_from_options(opts, lua, keys, merged, branch, log_message);

  guard.commit();
  if (automate)
    output << merged << "\n";
  else
    P(F("[merged] %s") % merged);
}

typedef std::pair<revision_id, revision_id> revpair;
typedef set<revision_id>::const_iterator rid_set_iter;

// Subroutine of 'merge' and 'automate show_conflicts'; find first pair of
// heads to merge.
static revpair
find_heads_to_merge(database & db, set<revision_id> const heads)
{
  I(heads.size() >= 2);

  if (heads.size() == 2)
    {
      rid_set_iter i = heads.begin();
      revision_id left = *i++;
      revision_id right = *i++;
      return revpair(left, right);
    };

  map<revision_id, revpair> heads_for_ancestor;
  set<revision_id> ancestors;

  // For every pair of heads, determine their merge ancestor, and
  // remember the ancestor->head mapping.
  for (rid_set_iter i = heads.begin(); i != heads.end(); ++i)
    for (rid_set_iter j = i; j != heads.end(); ++j)
      {
        // It is not possible to initialize j to i+1 (set iterators
        // expose neither operator+ nor a nondestructive next() method)
        if (j == i)
          continue;

        revision_id ancestor;
        find_common_ancestor_for_merge(db, *i, *j, ancestor);

        // More than one pair might have the same ancestor (e.g. if we
        // have three heads all with the same parent); as this table
        // will be recalculated on every pass, we just take the first
        // one we find.
        if (ancestors.insert(ancestor).second)
          safe_insert(heads_for_ancestor, std::make_pair(ancestor, revpair(*i, *j)));
      }

  // Erasing ancestors from ANCESTORS will now produce a set of merge
  // ancestors each of which is not itself an ancestor of any other
  // merge ancestor.
  erase_ancestors(db, ancestors);
  I(!ancestors.empty());

  // Take the first ancestor from the above set.
  return heads_for_ancestor[*ancestors.begin()];
}

// should merge support --message, --message-file?  It seems somewhat weird,
// since a single 'merge' command may perform arbitrarily many actual merges.
// (Possibility: append the --message/--message-file text to the synthetic
// log message constructed in merge_two().)
CMD(merge, "merge", "", CMD_REF(tree), "",
    N_("Merges unmerged heads of a branch"),
    "",
    options::opts::branch | options::opts::date | options::opts::author |
    options::opts::messages | options::opts::resolve_conflicts_opts |
    options::opts::auto_update)
{
  database db(app);
  key_store keys(app);
  project_t project(db, app.lua, app.opts);

  maybe_workspace_updater updater(app, project);

  if (!args.empty())
    throw usage(execid);

  E(!app.opts.branch().empty(), origin::user,
    F("please specify a branch, with --branch=BRANCH"));

  set<revision_id> heads;
  project.get_branch_heads(app.opts.branch, heads,
                           app.opts.ignore_suspend_certs);

  E(!heads.empty(), origin::user,
    F("branch '%s' is empty") % app.opts.branch);
  if (heads.size() == 1)
    {
      P(F("branch '%s' is already merged") % app.opts.branch);
      return;
    }

  P(FP("%d head on branch '%s'", "%d heads on branch '%s'", heads.size())
      % heads.size() % app.opts.branch);

  // avoid failure after lots of work
  cache_user_key(app.opts, project, keys, app.lua);

  size_t pass = 1, todo = heads.size() - 1;

  if (app.opts.resolve_conflicts)
    {
      // conflicts and resolutions only apply to first merge, so only do that one.
      todo = 1;
    }

  // If there are more than two heads to be merged, on each iteration we
  // merge a pair whose least common ancestor is not an ancestor of any
  // other pair's least common ancestor.  For example, if the history graph
  // looks like this:
  //
  //            X
  //           / \.                      (periods to prevent multi-line
  //          Y   C                       comment warnings)
  //         / \.
  //        A   B
  //
  // A and B will be merged first, and then the result will be merged with C.
  while (pass <= todo)
    {
      P(F("merge %d / %d:") % pass % todo);
      P(F("calculating best pair of heads to merge next"));

      revpair p = find_heads_to_merge(db, heads);

      merge_two(app.opts, app.lua, project, keys,
                p.first, p.second, app.opts.branch, string("merge"),
                std::cout, false);

      project.get_branch_heads(app.opts.branch, heads,
                               app.opts.ignore_suspend_certs);
      pass++;
    }

  if (heads.size() > 1)
    P(F("note: branch '%s' still has %d heads; run merge again") % app.opts.branch % heads.size());

  updater.maybe_do_update();
}

//   This is a special merge operator, but very useful for people
//   maintaining "slightly disparate but related" trees. It does a one-way
//   merge; less powerful than putting things in the same branch and also
//   more flexible.
//
//   1. Check to see if src and dst branches are merged, if not abort, if so
//   call heads N1 and N2 respectively.
//
//   2. (FIXME: not yet present) Run the hook propagate ("src-branch",
//   "dst-branch", N1, N2) which gives the user a chance to massage N1 into
//   a state which is likely to "merge nicely" with N2, eg. edit pathnames,
//   omit optional files of no interest.
//
//   3. Do a normal 2 or 3-way merge on N1 and N2, depending on the
//   existence of common ancestors.
//
//   4. Save the results as the delta (N2,M), the ancestry edges (N1,M)
//   and (N2,M), and the cert (N2,dst).
//
//   There are also special cases we have to check for where no merge is
//   actually necessary, because there hasn't been any divergence since the
//   last time propagate was run.
//
//   If dir is not the empty string, rename the root of N1 to have the name
//   'dir' in the merged tree. (ie, it has name "basename(dir)", and its
//   parent node is "N2.get_node(dirname(dir))")
void perform_merge_into_dir(app_state & app,
                            commands::command_id const & execid,
                            args_vector const & args)
{
  database db(app);
  key_store keys(app);
  project_t project(db, app.lua, app.opts);
  set<revision_id> src_heads, dst_heads;

  if (args.size() != 3)
    throw usage(execid);

  maybe_workspace_updater updater(app, project);

  project.get_branch_heads(typecast_vocab<branch_name>(idx(args, 0)), src_heads,
                           app.opts.ignore_suspend_certs);
  project.get_branch_heads(typecast_vocab<branch_name>(idx(args, 1)), dst_heads,
                           app.opts.ignore_suspend_certs);

  E(src_heads.size() != 0, origin::user,
    F("branch '%s' is empty") % idx(args, 0)());
  E(src_heads.size() == 1, origin::user,
    F("branch '%s' is not merged") % idx(args, 0)());

  E(dst_heads.size() != 0, origin::user,
    F("branch '%s' is empty") % idx(args, 1)());
  E(dst_heads.size() == 1, origin::user,
    F("branch '%s' is not merged") % idx(args, 1)());

  set<revision_id>::const_iterator src_i = src_heads.begin();
  set<revision_id>::const_iterator dst_i = dst_heads.begin();

  if (*src_i == *dst_i || is_ancestor(db, *src_i, *dst_i))
    {
      P(F("branch '%s' is up-to-date with respect to branch '%s'")
          % idx(args, 1)() % idx(args, 0)());
      P(F("no action taken"));
      return;
    }

  cache_user_key(app.opts, project, keys, app.lua);

  P(F("propagating %s -> %s") % idx(args,0) % idx(args,1));
  P(F("[left]  %s") % *src_i);
  P(F("[right] %s") % *dst_i);

  // check for special cases
  if (is_ancestor(db, *dst_i, *src_i))
    {
      P(F("no merge necessary; putting %s in branch '%s'")
        % *src_i % idx(args, 1)());
      transaction_guard guard(db);
      project.put_revision_in_branch(keys, *src_i,
                                     typecast_vocab<branch_name>(idx(args, 1)));
      guard.commit();
    }
  else
    {
      revision_id merged;
      transaction_guard guard(db);

      {
        revision_id const & left_rid(*src_i), & right_rid(*dst_i);
        roster_t left_roster, right_roster;
        MM(left_roster);
        MM(right_roster);
        marking_map left_marking_map, right_marking_map;
        set<revision_id>
          left_uncommon_ancestors,
          right_uncommon_ancestors;

        db.get_roster(left_rid, left_roster, left_marking_map);
        db.get_roster(right_rid, right_roster, right_marking_map);
        db.get_uncommon_ancestors(left_rid, right_rid,
                                  left_uncommon_ancestors,
                                  right_uncommon_ancestors);

        if (!idx(args,2)().empty())
          {
            dir_t moved_root = left_roster.root();
            file_path pth = file_path_external(idx(args, 2));
            file_path dir;
            path_component base;
            MM(dir);
            pth.dirname_basename(dir, base);

            E(right_roster.has_node(dir), origin::user,
              F("Path %s not found in destination tree.") % pth);
            const_node_t parent = right_roster.get_node(dir);
            moved_root->parent = parent->self;
            moved_root->name = base;

            marking_t i = left_marking_map.get_marking_for_update(moved_root->self);
            i->parent_name.clear();
            i->parent_name.insert(left_rid);
          }

        roster_merge_result result;
        roster_merge(left_roster,
                     left_marking_map,
                     left_uncommon_ancestors,
                     right_roster,
                     right_marking_map,
                     right_uncommon_ancestors,
                     result);

        content_merge_database_adaptor
          dba(db, left_rid, right_rid, left_marking_map, right_marking_map);

        bool resolutions_given;

        parse_resolve_conflicts_opts
          (app.opts, left_rid, left_roster, right_rid, right_roster, result, resolutions_given);

        resolve_merge_conflicts(app.lua, app.opts, left_roster, right_roster,
                                result, dba, resolutions_given);

        {
          dir_t moved_root = left_roster.root();
          moved_root->parent = the_null_node;
          moved_root->name = path_component();
        }

        // Write new files into the db.
        store_roster_merge_result(db, left_roster, right_roster, result,
                                  left_rid, right_rid, merged);
      }

      bool log_message_given;
      utf8 log_message;
      utf8 log_prefix = utf8((FL("propagate from branch '%s' (head %s)\n"
                               "            to branch '%s' (head %s)\n")
                              % idx(args, 0)
                              % *src_i
                              % idx(args, 1)
                              % *dst_i).str(),
                             origin::internal);

      process_commit_message_args(app.opts, log_message_given, log_message, log_prefix);

      project.put_standard_certs_from_options(app.opts, app.lua,
                                              keys,
                                              merged,
                                              typecast_vocab<branch_name>(idx(args, 1)),
                                              log_message);

      guard.commit();
      P(F("[merged] %s") % merged);
    }

  updater.maybe_do_update();
}

CMD(propagate, "propagate", "", CMD_REF(tree),
    N_("SOURCE-BRANCH DEST-BRANCH"),
    N_("Merges from one branch to another asymmetrically"),
    "",
    options::opts::date | options::opts::author | options::opts::messages |
    options::opts::resolve_conflicts_opts)
{
  if (args.size() != 2)
    throw usage(execid);
  args_vector a = args;
  a.push_back(arg_type());
  perform_merge_into_dir(app, make_command_id("tree merge_into_dir"), a);
}

CMD(merge_into_dir, "merge_into_dir", "", CMD_REF(tree),
    N_("SOURCE-BRANCH DEST-BRANCH DIR"),
    N_("Merges one branch into a subdirectory in another branch"),
    "",
    options::opts::date | options::opts::author | options::opts::messages |
    options::opts::resolve_conflicts_opts | options::opts::auto_update)
{
  perform_merge_into_dir(app, execid, args);
}

CMD(merge_into_workspace, "merge_into_workspace", "", CMD_REF(tree),
    N_("OTHER-REVISION"),
    N_("Merges a revision into the current workspace's base revision"),
    N_("Merges OTHER-REVISION into the current workspace's base revision, "
       "and update the current workspace with the result.  There can be no "
       "pending changes in the current workspace.  Both OTHER-REVISION and "
       "the workspace's base revision will be recorded as parents on commit.  "
       "The workspace's selected branch is not changed."),
    options::opts::move_conflicting_paths)
{
  revision_id left_id, right_id;
  cached_roster left, right;
  shared_ptr<roster_t> working_roster = shared_ptr<roster_t>(new roster_t());

  if (args.size() != 1)
    throw usage(execid);

  database db(app);
  workspace work(app);
  project_t project(db, app.lua, app.opts);

  // Get the current state of the workspace.

  // This command cannot be applied to a workspace with more than one parent
  // (revs can have no more than two parents).
  revision_id working_rid;

  {
    parent_map parents;
    work.get_parent_rosters(db, parents);
    E(parents.size() == 1, origin::user,
      F("this command can only be used in a single-parent workspace"));

    temp_node_id_source nis;
    work.get_current_roster_shape(db, nis, *working_roster);
    work.update_current_roster_from_filesystem(*working_roster);

    E(parent_roster(parents.begin()) == *working_roster, origin::user,
      F("'%s' can only be used in a workspace with no pending changes") %
        join_words(execid)());

    left_id = parent_id(parents.begin());
    left = parent_cached_roster(parents.begin());

    revision_t working_rev;
    make_revision_for_workspace(parents, *working_roster, working_rev);
    calculate_ident(working_rev, working_rid);
  }

  complete(app.opts, app.lua, project, idx(args, 0)(), right_id);
  db.get_roster(right_id, right);
  E(!(left_id == right_id), origin::user,
    F("workspace is already at revision %s") % left_id);

  E(!is_ancestor(db, right_id, left_id), origin::user,
    F("revision %s is already an ancestor of your workspace") % right_id);
  E(!is_ancestor(db, left_id, right_id), origin::user,
    F("revision %s is a descendant of the workspace parent,\n"
      "did you mean 'mtn update -r %s'?") % right_id % right_id);

  P(F("[left]  %s") % left_id);
  P(F("[right] %s") % right_id);

  set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;
  db.get_uncommon_ancestors(left_id, right_id,
                                left_uncommon_ancestors,
                                right_uncommon_ancestors);

  roster_merge_result merge_result;
  MM(merge_result);
  roster_merge(*left.first, *left.second, left_uncommon_ancestors,
               *right.first, *right.second, right_uncommon_ancestors,
               merge_result);

  revision_id lca_id;
  cached_roster lca;
  find_common_ancestor_for_merge(db, left_id, right_id, lca_id);
  db.get_roster(lca_id, lca);

  map<file_id, file_path> paths;
  get_content_paths(*working_roster, paths);

  content_merge_workspace_adaptor wca(db, lca_id, lca.first,
                                      *left.second, *right.second, paths);
  wca.cache_roster(working_rid, working_roster);
  resolve_merge_conflicts(app.lua, app.opts, *left.first, *right.first, merge_result, wca, false);

  // Make sure it worked...
  I(merge_result.is_clean());
  merge_result.roster.check_sane(true);

  // Construct the workspace revision.
  parent_map parents;
  safe_insert(parents, std::make_pair(left_id, left));
  safe_insert(parents, std::make_pair(right_id, right));

  revision_t merged_rev;
  make_revision_for_workspace(parents, merge_result.roster, merged_rev);

  // Note: the csets in merged_rev are _not_ suitable for submission to
  // perform_content_update, because content changes have been dropped.
  cset update;
  make_cset(*left.first, merge_result.roster, update);

  // small race condition here...
  work.perform_content_update(*left.first, merge_result.roster, update, wca, true,
                              app.opts.move_conflicting_paths);
  work.put_work_rev(merged_rev);
  work.maybe_update_inodeprints(db);

  P(F("updated to result of merge\n"
      " [left] %s\n"
      "[right] %s\n")
    % left_id
    % right_id);
}

CMD(explicit_merge, "explicit_merge", "", CMD_REF(tree),
    N_("LEFT-REVISION RIGHT-REVISION DEST-BRANCH"),
    N_("Merges two explicitly given revisions"),
    N_("The results of the merge are placed on the branch specified by "
       "DEST-BRANCH."),
    options::opts::date | options::opts::author |
    options::opts::messages | options::opts::resolve_conflicts_opts |
    options::opts::auto_update)
{
  database db(app);
  key_store keys(app);
  project_t project(db, app.lua, app.opts);
  revision_id left, right;
  branch_name branch;

  if (args.size() != 3)
    throw usage(execid);

  maybe_workspace_updater updater(app, project);

  complete(app.opts, app.lua, project, idx(args, 0)(), left);
  complete(app.opts, app.lua, project, idx(args, 1)(), right);
  branch = typecast_vocab<branch_name>(idx(args, 2));

  E(!(left == right), origin::user,
    F("%s and %s are the same revision, aborting")
    % left % right);
  E(!is_ancestor(db, left, right), origin::user,
    F("%s is already an ancestor of %s")
    % left % right);
  E(!is_ancestor(db, right, left), origin::user,
    F("%s is already an ancestor of %s")
    % right % left);

  // avoid failure after lots of work
  cache_user_key(app.opts, project, keys, app.lua);
  merge_two(app.opts, app.lua, project, keys,
            left, right, branch, string("explicit merge"),
            std::cout, false);

  updater.maybe_do_update();
}

namespace
{
  namespace syms
  {
    symbol const ancestor("ancestor");
    symbol const left("left");
    symbol const right("right");
  }
}

static void
show_conflicts_core (database & db,
                     lua_hooks & lua,
                     revision_id const & l_id,
                     revision_id const & r_id,
                     bool const basic_io,
                     bool automate,
                     std::ostream & output)
{
  // Note that left and right are in the order specified on the command line.
  // They are not in lexical order as they are with other merge commands so
  // they may appear swapped here. The user may have done that deliberately,
  // especially via automate, so we don't sort them here.

  basic_io::stanza st;

  if (basic_io)
    {
      st.push_binary_pair(syms::left, l_id.inner());
      st.push_binary_pair(syms::right, r_id.inner());
    }
  else
    {
      P(F("[left]  %s") % l_id);
      P(F("[right] %s") % r_id);
    }

  if (is_ancestor(db, l_id, r_id))
    {
      if (basic_io)
        {
          basic_io::printer pr;
          pr.print_stanza(st);
          output.write(pr.buf.data(), pr.buf.size());
        }
      else
        P(F("%s is an ancestor of %s; no merge is needed.")
          % l_id % r_id);

      return;
    }

  if (is_ancestor(db, r_id, l_id))
    {
      if (basic_io)
        {
          basic_io::printer pr;
          pr.print_stanza(st);
          output.write(pr.buf.data(), pr.buf.size());
        }
      else
        P(F("%s is an ancestor of %s; no merge is needed.")
          % r_id % l_id);

      return;
    }

  shared_ptr<roster_t> l_roster = shared_ptr<roster_t>(new roster_t());
  shared_ptr<roster_t> r_roster = shared_ptr<roster_t>(new roster_t());
  marking_map l_marking, r_marking;
  db.get_roster(l_id, *l_roster, l_marking);
  db.get_roster(r_id, *r_roster, r_marking);
  set<revision_id> l_uncommon_ancestors, r_uncommon_ancestors;
  db.get_uncommon_ancestors(l_id, r_id, l_uncommon_ancestors, r_uncommon_ancestors);
  roster_merge_result result;
  roster_merge(*l_roster, l_marking, l_uncommon_ancestors,
               *r_roster, r_marking, r_uncommon_ancestors,
               result);

  if (result.is_clean())
    {
      if (basic_io)
        {
          basic_io::printer pr;
          pr.print_stanza(st);
          output.write(pr.buf.data(), pr.buf.size());
        }

      if (!automate)
        P(F("0 conflicts"));
    }
  else
    {
      content_merge_database_adaptor adaptor(db, l_id, r_id,
                                             l_marking, r_marking);

      {
        basic_io::printer pr;
        st.push_binary_pair(syms::ancestor, adaptor.lca.inner());
        pr.print_stanza(st);
        output.write(pr.buf.data(), pr.buf.size());
      }

      // The basic_io routines in roster_merge.cc access these rosters via
      // the adaptor.
      adaptor.cache_roster (l_id, l_roster);
      adaptor.cache_roster (r_id, r_roster);

      result.report_missing_root_conflicts(*l_roster, *r_roster, adaptor, basic_io, output);
      result.report_invalid_name_conflicts(*l_roster, *r_roster, adaptor, basic_io, output);
      result.report_directory_loop_conflicts(*l_roster, *r_roster, adaptor, basic_io, output);

      result.report_orphaned_node_conflicts(*l_roster, *r_roster, adaptor, basic_io, output);
      result.report_multiple_name_conflicts(*l_roster, *r_roster, adaptor, basic_io, output);
      result.report_duplicate_name_conflicts(*l_roster, *r_roster, adaptor, basic_io, output);

      result.report_attribute_conflicts(*l_roster, *r_roster, adaptor, basic_io, output);
      result.report_file_content_conflicts(lua, *l_roster, *r_roster, adaptor, basic_io, output);

      if (!automate)
        {
          int const supported = result.count_supported_resolution();
          int const unsupported = result.count_unsupported_resolution();

          P(FP("%d conflict with supported resolutions.",
               "%d conflicts with supported resolutions.",
               supported) % supported);

          if (unsupported > 0)
            P(FP("warning: %d conflict with no supported resolutions.",
                 "warning: %d conflicts with no supported resolutions.",
                 unsupported) % unsupported);
        }
    }
}

CMD(show_conflicts, "show_conflicts", "", CMD_REF(informative), N_("REV REV"),
    N_("Shows what conflicts need resolution between two revisions"),
    N_("The conflicts are calculated based on the two revisions given in "
       "the REV parameters."),
    options::opts::none)
{
  database db(app);
  project_t project(db, app.lua, app.opts);

  if (args.size() != 2)
    throw usage(execid);
  revision_id l_id, r_id;
  complete(app.opts, app.lua, project, idx(args,0)(), l_id);
  complete(app.opts, app.lua, project, idx(args,1)(), r_id);

  show_conflicts_core(db, app.lua, l_id, r_id,
                      false, // basic_io
                      false, // automate
                      std::cout);
}

static void get_conflicts_rids(args_vector const & args,
                               database & db,
                               project_t & project,
                               app_state & app,
                               revision_id & left_rid,
                               revision_id & right_rid)
{
  if (args.empty())
    {
      // get ids from heads
      E(!app.opts.branch().empty(), origin::user,
        F("please specify a branch, with --branch=BRANCH"));

      set<revision_id> heads;
      project.get_branch_heads(app.opts.branch, heads,
                               app.opts.ignore_suspend_certs);

      E(heads.size() >= 2, origin::user,
        F("branch '%s' has only 1 head; must be at least 2 for conflicts") % app.opts.branch);

      revpair p = find_heads_to_merge (db, heads);
      left_rid = p.first;
      right_rid = p.second;
    }
  else if (args.size() == 2)
    {
      // get ids from args
      complete(app.opts, app.lua, project, idx(args,0)(), left_rid);
      complete(app.opts, app.lua, project, idx(args,1)(), right_rid);
    }
  else
    E(false, origin::user, F("wrong argument count"));
}

// Name: show_conflicts
// Arguments:
//   Two revision ids (optional, determined from the workspace if not given; there must be exactly two heads)
// Added in: 8.0
// Changed in: 9.0 (see monotone.texi for details)
// Purpose: Prints the conflicts between two revisions, to aid in merging them.
//
// Output format: see monotone.texi
//
// Error conditions:
//
//   If the revision IDs are unknown or invalid prints an error message to
//   stderr and exits with status 1.
//
//   If revision ids are not given, and the current workspace does not have
//   two heads, prints an error message to stderr and exits with status 1.
//
CMD_AUTOMATE(show_conflicts, N_("[LEFT_REVID RIGHT_REVID]"),
             N_("Shows the conflicts between two revisions"),
             N_("If no arguments are given, LEFT_REVID and RIGHT_REVID default to the "
                "first two heads that would be chosen by the 'merge' command."),
             options::opts::branch | options::opts::ignore_suspend_certs)
{
  database    db(app);
  project_t project(db, app.lua, app.opts);
  revision_id l_id, r_id;

  get_conflicts_rids(args, db, project, app, l_id, r_id);
  show_conflicts_core(db, app.lua, l_id, r_id,
                      true, // basic_io
                      true, // automate
                      output);
}

CMD(store, "store", "", CMD_REF(conflicts),
    "[LEFT_REVID RIGHT_REVID]",
    N_("Store the conflicts from merging two revisions"),
    N_("If no arguments are given, LEFT_REVID and RIGHT_REVID default to the "
       "first two heads that would be chosen by the 'merge' command. If "
       "--conflicts-file is not given, '_MTN/conflicts' is used."),
    options::opts::branch | options::opts::conflicts_opts)
{
  database    db(app);
  project_t   project(db, app.lua, app.opts);
  revision_id left_id, right_id;

  workspace::require_workspace(F("conflicts file must be under _MTN"));

  get_conflicts_rids(args, db, project, app, left_id, right_id);

  std::ostringstream output;
  show_conflicts_core(db, app.lua, left_id, right_id,
                      true, // basic_io
                      false, // automate
                      output);

  data dat(output.str(), origin::internal);
  write_data(app.opts.conflicts_file, dat);
  P(F("stored in '%s'") % app.opts.conflicts_file);
}

CMD_AUTOMATE(file_merge, N_("LEFT_REVID LEFT_FILENAME RIGHT_REVID RIGHT_FILENAME"),
             N_("Prints the results of the internal line merger, given two child revisions and file names"),
             "",
             options::opts::none)
{
  // We would have liked to take arguments of ancestor, left, right revision
  // and file ids; those are provided by show_conflicts and would save
  // computing the common ancestor and searching for file names. But we need
  // the file names to get the manual merge and file encoding attributes,
  // and there is no way to go from file id to file name. And there is no
  // way to specify the ancestor id for a merge adaptor; why should we trust
  // the user?

  E(args.size() == 4, origin::user,
    F("wrong argument count"));

  database  db(app);
  project_t project(db, app.lua, app.opts);

  revision_id left_rid;
  complete(app.opts, app.lua, project, idx(args,0)(), left_rid);
  file_path const left_path = file_path_external(idx(args,1));

  revision_id right_rid;
  complete(app.opts, app.lua, project, idx(args,2)(), right_rid);
  file_path const right_path = file_path_external(idx(args,3));

  roster_t left_roster;
  roster_t right_roster;
  marking_map left_marking, right_marking;
  db.get_roster(left_rid, left_roster, left_marking);
  db.get_roster(right_rid, right_roster, right_marking);

  content_merge_database_adaptor adaptor(db, left_rid, right_rid,
                                         left_marking, right_marking);

  const_file_t left_n = downcast_to_file_t(left_roster.get_node(left_path));
  const_file_t right_n = downcast_to_file_t(right_roster.get_node(right_path));

  revision_id ancestor_rid;
  file_path ancestor_path;
  file_id ancestor_fid;
  shared_ptr<roster_t const> ancestor_roster;
  adaptor.get_ancestral_roster(left_n->self, ancestor_rid, ancestor_roster);
  ancestor_roster->get_file_details(left_n->self, ancestor_fid, ancestor_path);

  content_merger cm(app.lua, *ancestor_roster, left_roster, right_roster, adaptor);
  file_data ancestor_data, left_data, right_data, merge_data;

  E(cm.attempt_auto_merge(ancestor_path, left_path, right_path,
                          ancestor_fid, left_n->content, right_n->content,
                          left_data, right_data, merge_data),
    origin::user,
    F("internal line merger failed"));

  output << merge_data;
}

CMD(pluck, "pluck", "", CMD_REF(workspace), N_("[PATH...]"),
    N_("Applies changes made at arbitrary places in history"),
    N_("This command takes changes made at any point in history, and "
       "edits your current workspace to include those changes.  The end result "
       "is identical to 'mtn diff -r FROM -r TO | patch -p0', except that "
       "this command uses monotone's merger, and thus intelligently handles "
       "renames, conflicts, and so on.\n"
       "If one revision is given, applies the changes made in that revision "
       "compared to its parent.\n"
       "If two revisions are given, applies the changes made to get from the "
       "first revision to the second."),
    options::opts::revision | options::opts::depth | options::opts::exclude |
    options::opts::move_conflicting_paths)
{
  database db(app);
  workspace work(app);
  project_t project(db, app.lua, app.opts);

  // Work out our arguments
  revision_id from_rid, to_rid;
  if (app.opts.revision.size() == 1)
    {
      complete(app.opts, app.lua, project, idx(app.opts.revision, 0)(), to_rid);
      std::set<revision_id> parents;
      db.get_revision_parents(to_rid, parents);
      E(parents.size() == 1, origin::user,
        F("revision %s is a merge\n"
          "to apply the changes relative to one of its parents, use:\n"
          "  %s pluck -r PARENT -r %s")
        % to_rid
        % prog_name
        % to_rid);
      from_rid = *parents.begin();
    }
  else if (app.opts.revision.size() == 2)
    {
      complete(app.opts, app.lua, project, idx(app.opts.revision, 0)(), from_rid);
      complete(app.opts, app.lua, project, idx(app.opts.revision, 1)(), to_rid);
    }
  else
    throw usage(execid);

  E(!(from_rid == to_rid), origin::user, F("no changes to apply"));

  // notionally, we have the situation
  //
  // from --> working
  //   |         |
  //   V         V
  //   to --> merged
  //
  // - from is the revision we start plucking from
  // - to is the revision we stop plucking at
  // - working is the current contents of the workspace
  // - merged is the result of the plucking, and achieved by running a
  //   merge in the fictional graph seen above
  //
  // To perform the merge, we use the real from roster, and the real working
  // roster, but synthesize a temporary 'to' roster.  This ensures that the
  // 'from', 'working' and 'base' rosters all use the same nid namespace,
  // while any additions that happened between 'from' and 'to' should be
  // considered as new nodes, even if the file that was added is in fact in
  // 'working' already -- so 'to' needs its own namespace.  (Among other
  // things, it is impossible with our merge formalism to have the above
  // graph with a node that exists in 'to' and 'working', but not 'from'.)
  //
  // finally, we take the cset from working -> merged, and apply that to the
  //   workspace
  // and take the cset from the workspace's base, and write that to _MTN/work

  // The node id source we'll use for the 'working' and 'to' rosters.
  temp_node_id_source nis;

  // Get the FROM roster
  shared_ptr<roster_t> from_roster = shared_ptr<roster_t>(new roster_t());
  MM(*from_roster);
  db.get_roster(from_rid, *from_roster);

  // Get the WORKING roster
  shared_ptr<roster_t> working_roster = shared_ptr<roster_t>(new roster_t());
  MM(*working_roster);
  work.get_current_roster_shape(db, nis, *working_roster);

  work.update_current_roster_from_filesystem(*working_roster);

  // Get the FROM->TO cset...
  cset from_to_to; MM(from_to_to);
  cset from_to_to_excluded; MM(from_to_to_excluded);
  {
    roster_t to_true_roster;
    db.get_roster(to_rid, to_true_roster);
    node_restriction mask(args_to_paths(args),
                          args_to_paths(app.opts.exclude),
                          app.opts.depth,
                          *from_roster, to_true_roster,
                          ignored_file(work));

    roster_t restricted_roster;
    make_restricted_roster(*from_roster, to_true_roster,
                           restricted_roster, mask);

    make_cset(*from_roster, restricted_roster, from_to_to);
    make_cset(restricted_roster, to_true_roster, from_to_to_excluded);
  }
  E(!from_to_to.empty(), origin::user, F("no changes to be applied"));
  // ...and use it to create the TO roster
  shared_ptr<roster_t> to_roster = shared_ptr<roster_t>(new roster_t());
  MM(*to_roster);
  {
    *to_roster = *from_roster;
    editable_roster_base editable_to_roster(*to_roster, nis);
    from_to_to.apply_to(editable_to_roster);
  }

  parent_map parents;
  work.get_parent_rosters(db, parents);

  revision_t working_rev;
  revision_id working_rid;
  make_revision_for_workspace(parents, *working_roster, working_rev);
  calculate_ident(working_rev, working_rid);

  // Now do the merge
  roster_merge_result result;
  marking_map left_markings, right_markings;
  three_way_merge(from_rid, *from_roster,
                  working_rid, *working_roster,
                  to_rid, *to_roster,
                  result, left_markings, right_markings);

  roster_t & merged_roster = result.roster;

  map<file_id, file_path> paths;
  get_content_paths(*working_roster, paths);

  content_merge_workspace_adaptor wca(db, from_rid, from_roster,
                                      left_markings, right_markings, paths);

  wca.cache_roster(working_rid, working_roster);
  // cache the synthetic to_roster under the to_rid so that the real
  // to_roster is not fetched from the db which does not have temporary nids
  wca.cache_roster(to_rid, to_roster);

  resolve_merge_conflicts(app.lua, app.opts, *working_roster, *to_roster,
                          result, wca, false);

  I(result.is_clean());
  // temporary node ids may appear
  merged_roster.check_sane(true);

  // we apply the working to merged cset to the workspace
  cset update;
  MM(update);
  make_cset(*working_roster, merged_roster, update);
  E(!update.empty(), origin::no_fault, F("no changes were applied"));
  work.perform_content_update(*working_roster, merged_roster, update, wca, true,
                              app.opts.move_conflicting_paths);

  P(F("applied changes to workspace"));

  // and record any remaining changes in _MTN/revision
  revision_t remaining;
  MM(remaining);
  make_revision_for_workspace(parents, merged_roster, remaining);

  // small race condition here...
  work.put_work_rev(remaining);

  // add a note to the user log file about what we did
  {
    utf8 log;
    work.read_user_log(log);
    std::string log_str = log();
    if (!log_str.empty())
      log_str += "\n";
    if (from_to_to_excluded.empty())
      log_str += (FL("applied changes from %s\n"
                     "             through %s\n")
                  % from_rid
                  % to_rid).str();
    else
      log_str += (FL("applied partial changes from %s\n"
                     "                     through %s\n")
                  % from_rid
                  % to_rid).str();
    work.write_user_log(utf8(log_str, origin::internal));
  }
}

CMD(heads, "heads", "", CMD_REF(tree), "",
    N_("Shows unmerged head revisions of a branch"),
    "",
    options::opts::branch)
{
  set<revision_id> heads;
  if (!args.empty())
    throw usage(execid);

  E(!app.opts.branch().empty(), origin::user,
    F("please specify a branch, with --branch=BRANCH"));

  database db(app);
  project_t project(db, app.lua, app.opts);

  project.get_branch_heads(app.opts.branch, heads,
                           app.opts.ignore_suspend_certs);

  if (heads.empty())
    P(F("branch '%s' is empty") % app.opts.branch);
  else if (heads.size() == 1)
    P(F("branch '%s' is currently merged:") % app.opts.branch);
  else
    P(F("branch '%s' is currently unmerged:") % app.opts.branch);

  for (set<revision_id>::const_iterator i = heads.begin();
       i != heads.end(); ++i)
    cout << describe_revision(app.opts, app.lua, project, *i) << '\n';
}

CMD(get_roster, "get_roster", "", CMD_REF(debug), N_("[REVID]"),
    N_("Dumps the roster associated with a given identifier"),
    N_("If no REVID is given, the workspace is used."),
    options::opts::none)
{
  database db(app);
  roster_t roster;
  marking_map mm;

  if (args.empty())
    {
      parent_map parents;
      temp_node_id_source nis;
      revision_id rid(fake_id());

      workspace work(app);
      work.get_parent_rosters(db, parents);
      work.get_current_roster_shape(db, nis, roster);
      work.update_current_roster_from_filesystem(roster);

      if (parents.empty())
        {
          mark_roster_with_no_parents(rid, roster, mm);
        }
      else if (parents.size() == 1)
        {
          roster_t parent = parent_roster(parents.begin());
          marking_map parent_mm = parent_marking(parents.begin());
          mark_roster_with_one_parent(parent, parent_mm, rid, roster, mm);
        }
      else
        {
          parent_map::const_iterator i = parents.begin();
          revision_id left_id = parent_id(i);
          roster_t const & left_roster = parent_roster(i);
          marking_map const & left_markings = parent_marking(i);

          i++;
          revision_id right_id = parent_id(i);
          roster_t const & right_roster = parent_roster(i);
          marking_map const & right_markings = parent_marking(i);

          i++; I(i == parents.end());

          set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;
          db.get_uncommon_ancestors(left_id, right_id,
                                        left_uncommon_ancestors,
                                        right_uncommon_ancestors);

          mark_merge_roster(left_roster, left_markings,
                            left_uncommon_ancestors,
                            right_roster, right_markings,
                            right_uncommon_ancestors,
                            rid, roster, mm);
        }
    }
  else if (args.size() == 1)
    {
      database db(app);
      project_t project(db, app.lua, app.opts);
      revision_id rid;
      complete(app.opts, app.lua, project, idx(args, 0)(), rid);
      I(!null_id(rid));
      db.get_roster(rid, roster, mm);
    }
  else
    throw usage(execid);

  roster_data dat;
  write_roster_and_marking(roster, mm, dat);
  cout << dat;
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
