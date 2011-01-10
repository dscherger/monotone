// Copyright (C) 2010, 2011 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <deque>
#include <iostream>
#include <map>
#include <sstream>

#include "cmd.hh"
#include "merge_content.hh"
#include "file_io.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "selectors.hh"
#include "transforms.hh"
#include "work.hh"
#include "charset.hh"
#include "app_state.hh"
#include "project.hh"
#include "basic_io.hh"
#include "xdelta.hh"
#include "keys.hh"
#include "key_store.hh"
#include "maybe_workspace_updater.hh"
#include "simplestring_xform.hh"
#include "database.hh"
#include "date_format.hh"
#include "roster.hh"
#include "rev_output.hh"
#include "vocab_cast.hh"

using std::cout;
using std::make_pair;
using std::make_pair;
using std::map;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;

static void
get_old_branch_names(database & db, parent_map const & parents,
                     set<branch_name> & old_branch_names)
{
  for (parent_map::const_iterator i = parents.begin();
       i != parents.end(); ++i)
    {
      vector<cert> branches;
      db.get_revision_certs(parent_id(i), branch_cert_name, branches);
      for (vector<cert>::const_iterator b = branches.begin();
           b != branches.end(); ++b)
        {
          old_branch_names.insert(typecast_vocab<branch_name>(b->value));
        }
    }
}

class message_reader
{
public:
  message_reader(string const & message, size_t offset) :
    message(message), offset(offset) {}

  bool read(string const & text)
  {
    size_t len = text.length();
    if (message.compare(offset, len, text) == 0)
      {
        offset += len;
        return true;
      }
    else
      return false;
  }

  string readline()
  {
    size_t eol = message.find_first_of("\r\n", offset);
    if (eol == string::npos)
      return "";

    size_t len = eol - offset;
    string line = message.substr(offset, len);
    offset = eol+1;

    if (message[eol] == '\r' && message.length() > eol+1 &&
        message[eol+1] == '\n')
      offset++;

    return trim(line);
  }

  bool contains(string const & summary)
  {
    return message.find(summary, offset) != string::npos;
  }

  bool remove(string const & summary)
  {
    size_t pos = message.find(summary, offset);
    I(pos != string::npos);
    if (pos + summary.length() == message.length())
      {
        message.erase(pos);
        return true;
      }
    else
      return false;
  }

  string content()
  {
    return message.substr(offset);
  }

private:
  string message;
  size_t offset;
};

static bool
date_fmt_valid(string date_fmt)
{
  if (date_fmt.empty())
    {
      return true;
    }
  else
    {
      // check that the specified date format can be used to format and
      // parse a date
      date_t now = date_t::now();
      date_t parsed;
      try
        {
          string formatted = now.as_formatted_localtime(date_fmt);
          parsed = date_t::from_formatted_localtime(formatted, date_fmt);
        }
      catch (recoverable_failure const & e)
        {
          L(FL("date check failed: %s") % e.what());
        }

      if (parsed != now)
        {
          L(FL("date check failed: %s != %s") % now % parsed);
          return false;
        }
      else
        {
          return true;
        }
    }
}

static void
get_log_message_interactively(lua_hooks & lua, workspace & work,
                              project_t & project,
                              revision_id const rid, revision_t const & rev,
                              string & author, date_t & date, branch_name & branch,
                              set<branch_name> const & old_branches,
                              string const & date_fmt, utf8 & log_message)
{
  utf8 backup;
  work.load_commit_text(backup);

  E(backup().empty(), origin::user,
    F("A backup from a previously failed commit exists in _MTN/commit.\n"
      "This file must be removed before commit will proceed.\n"
      "You may recover the previous message from this file if necessary."));

  utf8 instructions(
    _("-- Enter a description of this change above --\n"
      "-- You may edit the fields below            --\n"));

  utf8 ignored(
    _("\n-- Modifications below this line are ignored --\n"));

  utf8 cancel(_("*** REMOVE THIS LINE TO CANCEL THE COMMIT ***\n"));

  utf8 const BRANCH(_("Branch:   "));
  utf8 const AUTHOR(_("Author:   "));
  utf8 const DATE(  _("Date:     "));

  bool is_date_fmt_valid = date_fmt_valid(date_fmt);

  utf8 changelog;
  work.read_user_log(changelog);

  // ensure there are two blank lines after the changelog

  changelog = utf8(changelog() + "\n\n", origin::user);

  // build editable fields
  utf8 editable;
  {
    ostringstream oss;

    oss << BRANCH << ' ' << branch << '\n';
    oss << AUTHOR << ' ' << author << '\n';

    if (!is_date_fmt_valid)
      {
        W(F("date format '%s' cannot be parsed; using default instead") % date_fmt);
      }

    if (!is_date_fmt_valid || date_fmt.empty())
      {
        oss << DATE << ' ' << date << '\n';
      }
    else
      {
        oss << DATE << ' ' << date.as_formatted_localtime(date_fmt) << '\n';
      }

    editable = utf8(oss.str().c_str());
  }

  // Build notes
  utf8 notes;
  {
    ostringstream oss;

    if (!old_branches.empty() && old_branches.find(branch) == old_branches.end())
      {
        oss << _("*** THIS REVISION WILL CREATE A NEW BRANCH ***") << "\n\n";
        for (set<branch_name>::const_iterator i = old_branches.begin();
             i != old_branches.end(); ++i)
          oss << _("Old Branch: ") << *i << '\n';
        oss << _("New Branch: ") << branch << "\n\n";
      }
    set<revision_id> heads;
    project.get_branch_heads(branch, heads, false);
    if (!heads.empty())
      {
        for (edge_map::const_iterator e = rev.edges.begin();
             e != rev.edges.end(); ++e)
          {
            if (heads.find(edge_old_revision(e)) == heads.end())
              {
                oss << _("*** THIS REVISION WILL CREATE DIVERGENCE ***") << "\n\n";
                break;
              }
          }
      }

    notes = utf8(oss.str().c_str());
  }

  utf8 summary;
  revision_summary(rev, summary);

  utf8 full_message(changelog() + cancel() + instructions() + editable() + ignored() +
                    notes() + summary(),
                    origin::internal);

  external input_message;
  external output_message;

  utf8_to_system_best_effort(full_message, input_message);

  E(lua.hook_edit_comment(input_message, output_message),
    origin::user,
    F("edit of log message failed"));

  system_to_utf8(output_message, full_message);

  // Everything up to the cancel message is the changelog; trailing blank
  // lines trimmed.
  size_t changelog_end = full_message().find(cancel());
  if (changelog_end == string::npos)
    {
      // try to save the edited changelog, now delimited by the instructions.
      changelog_end = full_message().find(instructions());
      if (changelog_end != string::npos)
        work.write_user_log(utf8(trim_right(full_message().substr(0, changelog_end)) + '\n', origin::user));

      E(false, origin::user, F("Commit cancelled."));
    }

  // save the message in _MTN/commit so it's not lost if something fails
  // below
  work.save_commit_text(full_message);

  string content = trim_right(full_message().substr(0, changelog_end)) + '\n';
  log_message = utf8(content, origin::user);

  message_reader message(full_message(), changelog_end);

  // Parse the editable fields.

  // this can't fail, since we start reading where we found it above.
  message.read(cancel());

  E(message.read(instructions()), origin::user,
    F("Commit failed. Instructions not found."));

  // Branch:

  E(message.read(trim_right(BRANCH())), origin::user,
    F("Commit failed. Branch header not found."));

  string b = message.readline();

  E(!b.empty(), origin::user,
    F("Commit failed. Branch value empty."));

  branch = branch_name(b, origin::user);

  // Author:

  E(message.read(trim_right(AUTHOR())), origin::user,
    F("Commit failed. Author header not found."));

  author = message.readline();

  E(!author.empty(), origin::user,
    F("Commit failed. Author value empty."));

  // Date:

  E(message.read(trim_right(DATE())), origin::user,
    F("Commit failed. Date header not found."));

  string d = message.readline();

  E(!d.empty(), origin::user,
    F("Commit failed. Date value empty."));

  if (!is_date_fmt_valid || date_fmt.empty())
    date = date_t(d);
  else
    date = date_t::from_formatted_localtime(d, date_fmt);

  // rest is ignored

  // remove the backup file now that all values have been extracted
  work.clear_commit_text();
}

void
revert(app_state & app,
       args_vector const & args,
       bool undrop)
{
  roster_t old_roster, new_roster;
  cset preserved;

  E(app.opts.missing || !args.empty() || !app.opts.exclude.empty(),
    origin::user,
    F("you must pass at least one path to 'revert' (perhaps '.')"));

  database db(app);
  workspace work(app);

  parent_map parents;
  work.get_parent_rosters(db, parents);
  E(parents.size() == 1, origin::user,
    F("this command can only be used in a single-parent workspace"));
  old_roster = parent_roster(parents.begin());

  {
    temp_node_id_source nis;
    work.get_current_roster_shape(db, nis, new_roster);
  }

  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.opts.exclude),
                        app.opts.depth,
                        old_roster, new_roster, ignored_file(work),
                        restriction::explicit_includes);

  if (app.opts.missing)
    {
      // --missing is a further filter on the files included by a
      // restriction we first find all missing files included by the
      // specified args and then make a restriction that includes only
      // these missing files.
      set<file_path> missing;
      work.find_missing(new_roster, mask, missing);
      if (missing.empty())
        {
          P(F("no missing files to revert"));
          return;
        }

      std::vector<file_path> missing_files;
      for (set<file_path>::const_iterator i = missing.begin();
           i != missing.end(); i++)
        {
          L(FL("reverting missing file: %s") % *i);
          missing_files.push_back(*i);
        }
      // replace the original mask with a more restricted one
      mask = node_restriction(missing_files,
                              std::vector<file_path>(), app.opts.depth,
                              old_roster, new_roster, ignored_file(work));
    }

  // We want the restricted roster to include all the changes
  // that are to be *kept*. Then, the changes to revert are those
  // from the new roster *back* to the restricted roster

  // The arguments to revert are paths to be reverted *not* paths to be left
  // intact. The restriction formed from these arguments will include the
  // changes to be reverted and excludes the changes to be kept.  To form
  // the correct restricted roster this restriction must be applied to the
  // old and new rosters in reverse order, from new *back* to old.

  roster_t restricted_roster;
  make_restricted_roster(new_roster, old_roster, restricted_roster,
                         mask);

  make_cset(old_roster, restricted_roster, preserved);

  // At this point, all three rosters have accounted for additions,
  // deletions and renames but they all have content hashes from the
  // original old roster. This is fine, when reverting files we want to
  // revert them back to their original content.

  // The preserved cset will be left pending in MTN/revision

  // if/when reverting through the editable_tree interface use
  // make_cset(new_roster, restricted_roster, reverted);
  // to get a cset that gets us back to the restricted roster
  // from the current workspace roster

  node_map const & nodes = restricted_roster.all_nodes();

  for (node_map::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      node_id nid = i->first;
      node_t node = i->second;

      if (restricted_roster.is_root(nid))
        continue;

      if (!mask.includes(restricted_roster, nid))
        continue;

      file_path path;
      restricted_roster.get_name(nid, path);

      if (is_file_t(node))
        {
          file_t f = downcast_to_file_t(node);

          bool revert = true;

          if (file_exists(path))
            {
              file_id ident;
              calculate_ident(path, ident);
              // don't touch unchanged files
              if (ident == f->content)
                {
                  L(FL("skipping unchanged %s") % path);
                  revert = false;
                }
              else
                {
                  revert = !undrop;
                }
            }

          if (revert)
            {
              P(F("reverting %s") % path);
              L(FL("reverting %s to [%s]") % path
                % f->content);

              E(db.file_version_exists(f->content), origin::user,
                F("no file version %s found in database for %s")
                % f->content % path);

              file_data dat;
              L(FL("writing file %s to %s")
                % f->content % path);
              db.get_file_version(f->content, dat);
              write_data(path, dat.inner());
            }
        }
      else
        {
          if (!directory_exists(path))
            {
              P(F("recreating %s/") % path);
              mkdir_p(path);
            }
          else
            {
              L(FL("skipping existing %s/") % path);
            }
        }

      // revert attributes on this node -- this doesn't quite catch all cases:
      // if the execute bits are manually set on some path that doesn't have
      // a dormant mtn:execute the execute bits will not be cleared
      // FIXME: check execute bits against mtn:execute explicitly?

      for (attr_map_t::const_iterator a = node->attrs.begin();
           a != node->attrs.end(); ++a)
        {
          L(FL("reverting %s on %s") % a->first() % path);
          if (a->second.first)
            app.lua.hook_set_attribute(a->first(), path,
                                       a->second.second());
          else
            app.lua.hook_clear_attribute(a->first(), path);
        }
    }

  // Included_work is thrown away which effectively reverts any adds,
  // drops and renames it contains. Drops and rename sources will have
  // been rewritten above but this may leave rename targets laying
  // around.

  revision_t remaining;
  make_revision_for_workspace(parent_id(parents.begin()), preserved, remaining);

  // Race.
  work.put_work_rev(remaining);
  work.maybe_update_inodeprints(db);
}

CMD(revert, "revert", "", CMD_REF(workspace), N_("[PATH]..."),
    N_("Reverts files and/or directories"),
    N_("In order to revert the entire workspace, specify \".\" as the "
       "file name."),
    options::opts::depth | options::opts::exclude | options::opts::missing)
{
  revert(app, args, false);
}

CMD(undrop, "undrop", "", CMD_REF(workspace), N_("PATH..."),
    N_("Reverses a mistaken 'drop'"),
    N_("If the file was deleted from the workspace, this is the same as 'revert'. "
       "Otherwise, it just removes the 'drop' from the manifest."),
    options::opts::none)
{
  revert(app, args, true);
}

static void
walk_revisions(database & db, const revision_id & from_rev,
               const revision_id & to_rev)
{
  revision_id r = from_rev;
  revision_t rev;

  do
    {
      E(!null_id(r), origin::user,
        F("revision %s it not a child of %s, cannot invert")
        % from_rev % to_rev);

      db.get_revision(r, rev);
      E(rev.edges.size() < 2, origin::user,
        F("revision %s has %d parents, cannot invert")
        % r % rev.edges.size());

      E(rev.edges.size() > 0, origin::user,
        F("revision %s it not a child of %s, cannot invert")
        % from_rev % to_rev);
      r = edge_old_revision (rev.edges.begin());
    }
  while (r != to_rev);
}

CMD(disapprove, "disapprove", "", CMD_REF(review),
    N_("[PARENT-REVISION] CHILD-REVISION"),
    N_("Disapproves a particular revision or revision range"),
    "",
    options::opts::branch | options::opts::messages | options::opts::date |
    options::opts::author | options::opts::auto_update)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() < 1 || args.size() > 2)
    throw usage(execid);

  maybe_workspace_updater updater(app, project);

  utf8 log_message("");
  bool log_message_given;
  revision_id child_rev, parent_rev;
  revision_t rev, rev_inverse;
  shared_ptr<cset> cs_inverse(new cset());

  if (args.size() == 1)
    {
      complete(app.opts, app.lua, project, idx(args, 0)(), child_rev);
      db.get_revision(child_rev, rev);

      E(rev.edges.size() == 1, origin::user,
        F("revision %s has %d parents, cannot invert")
        % child_rev % rev.edges.size());

      guess_branch(app.opts, project, child_rev);
      E(!app.opts.branch().empty(), origin::user,
        F("need --branch argument for disapproval"));

      process_commit_message_args(app.opts, log_message_given, log_message,
                                  utf8((FL("disapproval of revision '%s'")
                                        % child_rev).str(),
                                       origin::internal));
    }
  else if (args.size() == 2)
    {
      complete(app.opts, app.lua, project, idx(args, 0)(), parent_rev);
      complete(app.opts, app.lua, project, idx(args, 1)(), child_rev);

      set<revision_id> rev_set;

      rev_set.insert(child_rev);
      rev_set.insert(parent_rev);

      erase_ancestors(db, rev_set);
      if (rev_set.size() > 1)
        {
          set<revision_id> ancestors;
          db.get_common_ancestors (rev_set, ancestors);
          E(ancestors.size() > 0, origin::user,
            F("revisions %s and %s do not share common history, cannot invert")
            % parent_rev % child_rev);
          E(ancestors.size() < 1, origin::user,
            F("revisions share common history"
              ", but %s is not an ancestor of %s, cannot invert")
            % parent_rev % child_rev);
        }

      walk_revisions(db, child_rev, parent_rev);
      db.get_revision(parent_rev, rev);

      E(rev.edges.size() == 1, origin::user,
        F("revision %s has %d parents, cannot invert")
        % child_rev % rev.edges.size());

      guess_branch(app.opts, project, child_rev);
      E(!app.opts.branch().empty(), origin::user,
        F("need --branch argument for disapproval"));

      process_commit_message_args(app.opts, log_message_given, log_message,
                                  utf8((FL("disapproval of revisions "
                                           "'%s'..'%s'")
                                        % parent_rev % child_rev).str(),
                                       origin::internal));
    }

  cache_user_key(app.opts, project, keys, app.lua);

  // for the divergence check, below
  set<revision_id> heads;
  project.get_branch_heads(app.opts.branch, heads,
                           app.opts.ignore_suspend_certs);
  unsigned int old_head_size = heads.size();

  edge_entry const & old_edge (*rev.edges.begin());
  db.get_revision_manifest(edge_old_revision(old_edge),
                               rev_inverse.new_manifest);
  {
    roster_t old_roster, new_roster;
    db.get_roster(edge_old_revision(old_edge), old_roster);
    db.get_roster(child_rev, new_roster);
    make_cset(new_roster, old_roster, *cs_inverse);
  }
  rev_inverse.edges.insert(make_pair(child_rev, cs_inverse));

  {
    transaction_guard guard(db);

    revision_id inv_id;
    revision_data rdat;

    write_revision(rev_inverse, rdat);
    calculate_ident(rdat, inv_id);
    db.put_revision(inv_id, rdat);

    project.put_standard_certs_from_options(app.opts, app.lua, keys,
                                            inv_id, app.opts.branch,
                                            log_message);
    guard.commit();
  }

  project.get_branch_heads(app.opts.branch, heads,
                           app.opts.ignore_suspend_certs);
  if (heads.size() > old_head_size && old_head_size > 0) {
    P(F("note: this revision creates divergence\n"
        "note: you may (or may not) wish to run '%s merge'")
      % prog_name);
  }
  updater.maybe_do_update();
}

CMD(mkdir, "mkdir", "", CMD_REF(workspace), N_("[DIRECTORY...]"),
    N_("Creates directories and adds them to the workspace"),
    "",
    options::opts::no_ignore)
{
  if (args.size() < 1)
    throw usage(execid);

  database db(app);
  workspace work(app);

  set<file_path> paths;
  // spin through args and try to ensure that we won't have any collisions
  // before doing any real filesystem modification.  we'll also verify paths
  // against .mtn-ignore here.
  for (args_vector::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      file_path fp = file_path_external(*i);
      require_path_is_nonexistent
        (fp, F("directory '%s' already exists") % fp);

      // we'll treat this as a user (fatal) error.  it really wouldn't make
      // sense to add a dir to .mtn-ignore and then try to add it to the
      // project with a mkdir statement, but one never can tell...
      E(app.opts.no_ignore || !work.ignore_file(fp),
        origin::user,
        F("ignoring directory '%s' [see .mtn-ignore]") % fp);

      paths.insert(fp);
    }

  // this time, since we've verified that there should be no collisions,
  // we'll just go ahead and do the filesystem additions.
  for (set<file_path>::const_iterator i = paths.begin(); i != paths.end(); ++i)
    mkdir_p(*i);

  work.perform_additions(db, paths, false, !app.opts.no_ignore);
}

void perform_add(app_state & app,
                 database & db,
                 workspace & work,
                 vector<file_path> roots)
{
  set<file_path> paths;
  bool add_recursive = app.opts.recursive;
  if (app.opts.unknown)
    {
      path_restriction mask(roots, args_to_paths(app.opts.exclude),
                            app.opts.depth, ignored_file(work));
      set<file_path> ignored;

      // if no starting paths have been specified use the workspace root
      if (roots.empty())
        roots.push_back(file_path());

      work.find_unknown_and_ignored(db, mask, roots, paths, ignored);

      work.perform_additions(db, ignored,
                                 add_recursive, !app.opts.no_ignore);
    }
  else
    paths = set<file_path>(roots.begin(), roots.end());

  work.perform_additions(db, paths, add_recursive, !app.opts.no_ignore);
}

CMD(add, "add", "", CMD_REF(workspace), N_("[PATH]..."),
    N_("Adds files to the workspace"),
    "",
    options::opts::unknown | options::opts::no_ignore |
    options::opts::recursive)
{
  if (!app.opts.unknown && (args.size() < 1))
    throw usage(execid);

  database db(app);
  workspace work(app);

  vector<file_path> roots = args_to_paths(args);

  perform_add(app, db, work, roots);
}

void perform_drop(app_state & app,
                  database & db,
                  workspace & work,
                  vector<file_path> roots)
{
  set<file_path> paths;
  if (app.opts.missing)
    {
      temp_node_id_source nis;
      roster_t current_roster_shape;
      work.get_current_roster_shape(db, nis, current_roster_shape);
      node_restriction mask(roots,
                            args_to_paths(app.opts.exclude),
                            app.opts.depth,
                            current_roster_shape, ignored_file(work));
      work.find_missing(current_roster_shape, mask, paths);
    }
  else
    {
      paths = set<file_path>(roots.begin(), roots.end());
    }

  work.perform_deletions(db, paths,
                             app.opts.recursive, app.opts.bookkeep_only);
}
CMD(drop, "drop", "rm", CMD_REF(workspace), N_("[PATH]..."),
    N_("Drops files from the workspace"),
    "",
    options::opts::bookkeep_only | options::opts::missing | options::opts::recursive)
{
  if (!app.opts.missing && (args.size() < 1))
    throw usage(execid);

  database db(app);
  workspace work(app);

  perform_drop(app, db, work, args_to_paths(args));
}


CMD(rename, "rename", "mv", CMD_REF(workspace),
    N_("SRC DEST\n"
       "SRC1 [SRC2 [...]] DEST_DIR"),
    N_("Renames entries in the workspace"),
    "",
    options::opts::bookkeep_only)
{
  if (args.size() < 2)
    throw usage(execid);

  database db(app);
  workspace work(app);

  utf8 dstr = args.back();
  file_path dst_path = file_path_external(dstr);

  set<file_path> src_paths;
  for (size_t i = 0; i < args.size()-1; i++)
    {
      file_path s = file_path_external(idx(args, i));
      src_paths.insert(s);
    }

  //this catches the case where the user specifies a directory 'by convention'
  //that doesn't exist.  the code in perform_rename already handles the proper
  //cases for more than one source item.
  if (src_paths.size() == 1 && dstr()[dstr().size() -1] == '/')
    if (get_path_status(*src_paths.begin()) != path::directory)
      E(get_path_status(dst_path) == path::directory, origin::user,
        F(_("The specified target directory %s/ doesn't exist.")) % dst_path);

  work.perform_rename(db, src_paths, dst_path, app.opts.bookkeep_only);
}


CMD(pivot_root, "pivot_root", "", CMD_REF(workspace), N_("NEW_ROOT PUT_OLD"),
    N_("Renames the root directory"),
    N_("After this command, the directory that currently "
       "has the name NEW_ROOT "
       "will be the root directory, and the directory "
       "that is currently the root "
       "directory will have name PUT_OLD.\n"
       "Use of --bookkeep-only is NOT recommended."),
    options::opts::bookkeep_only | options::opts::move_conflicting_paths)
{
  if (args.size() != 2)
    throw usage(execid);

  database db(app);
  workspace work(app);
  file_path new_root = file_path_external(idx(args, 0));
  file_path put_old = file_path_external(idx(args, 1));
  work.perform_pivot_root(db, new_root, put_old,
                          app.opts.bookkeep_only,
                          app.opts.move_conflicting_paths);
}

CMD(status, "status", "", CMD_REF(informative), N_("[PATH]..."),
    N_("Shows workspace's status information"),
    "",
    options::opts::depth | options::opts::exclude)
{
  roster_t new_roster;
  parent_map old_rosters;
  revision_t rev;
  temp_node_id_source nis;

  database db(app);
  project_t project(db);
  workspace work(app);

  string date_fmt = get_date_format(app.opts, app.lua, date_time_long);

  work.get_parent_rosters(db, old_rosters);
  work.get_current_roster_shape(db, nis, new_roster);

  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.opts.exclude),
                        app.opts.depth,
                        old_rosters, new_roster, ignored_file(work));

  work.update_current_roster_from_filesystem(new_roster, mask);
  make_restricted_revision(old_rosters, new_roster, mask, rev);

  vector<bisect::entry> info;
  work.get_bisect_info(info);

  if (!info.empty())
    {
      bisect::entry start = *info.begin();
      I(start.first == bisect::start);

      if (old_rosters.size() == 1)
        {
          revision_id current_id = parent_id(*old_rosters.begin());
          if (start.second != current_id)
            P(F("bisection from revision %s in progress") % start.second);
        }
    }

  revision_id rid;
  string author;
  key_store keys(app);
  key_identity_info key;

  try
    {
      get_user_key(app.opts, app.lua, db, keys, project, key.id, cache_disable);
      project.complete_key_identity_from_id(keys, app.lua, key);

      if (!app.lua.hook_get_author(app.opts.branch, key, author))
        author = key.official_name();
    }
  catch (recoverable_failure & rf)
    {
      // If we can't figure out which key would be used for commit, that's no reason
      // to make status fail.
      if (rf.caused_by() == origin::user)
        {
          author = "???";
        }
      else
        throw;
    }

  calculate_ident(rev, rid);

  set<branch_name> old_branches;
  get_old_branch_names(db, old_rosters, old_branches);

  utf8 changelog;
  work.read_user_log(changelog);

  utf8 header;
  utf8 summary;

  revision_header(rid, rev, author, date_t::now(), app.opts.branch, changelog,
                  date_fmt, header);
  revision_summary(rev, summary);

  external header_external;
  external summary_external;

  utf8_to_system_best_effort(header, header_external);
  utf8_to_system_best_effort(summary, summary_external);

  cout << header_external;

  if (!old_branches.empty() &&
      old_branches.find(app.opts.branch) == old_branches.end())
    {
      cout << string(70, '-') << '\n'
           << _("*** THIS REVISION WILL CREATE A NEW BRANCH ***") << "\n\n";
      for (set<branch_name>::const_iterator i = old_branches.begin();
           i != old_branches.end(); ++i)
        cout << _("Old Branch: ") << *i << '\n';
      cout << _("New Branch: ") << app.opts.branch << "\n\n";
    }
  set<revision_id> heads;
  project.get_branch_heads(app.opts.branch, heads, false);
  if (!heads.empty())
    {
      for (edge_map::const_iterator e = rev.edges.begin();
           e != rev.edges.end(); ++e)
        {
          if (heads.find(edge_old_revision(e)) == heads.end())
            {
              cout << _("*** THIS REVISION WILL CREATE DIVERGENCE ***") << "\n\n";
              break;
            }
        }
    }

  cout << summary_external;
}

static void
checkout_common(app_state & app,
                args_vector const & args)
{
  revision_id revid;
  system_path dir;

  database db(app);
  project_t project(db);
  transaction_guard guard(db, false);

  if (app.opts.revision.empty())
    {
      // use branch head revision
      E(!app.opts.branch().empty(), origin::user,
        F("use --revision or --branch to specify what to checkout"));

      set<revision_id> heads;
      project.get_branch_heads(app.opts.branch, heads,
                               app.opts.ignore_suspend_certs);
      E(!heads.empty(), origin::user,
        F("branch '%s' is empty") % app.opts.branch);
      if (heads.size() > 1)
        {
          P(F("branch %s has multiple heads:") % app.opts.branch);
          for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
            P(i18n_format("  %s")
              % describe_revision(app.opts, app.lua, project, *i));
          P(F("choose one with '%s checkout -r<id>'") % prog_name);
          E(false, origin::user,
            F("branch %s has multiple heads") % app.opts.branch);
        }
      revid = *(heads.begin());
    }
  else if (app.opts.revision.size() == 1)
    {
      // use specified revision
      complete(app.opts, app.lua, project, idx(app.opts.revision, 0)(), revid);

      guess_branch(app.opts, project, revid);

      I(!app.opts.branch().empty());

      E(project.revision_is_in_branch(revid, app.opts.branch),
        origin::user,
        F("revision %s is not a member of branch %s")
        % revid % app.opts.branch);
    }

  // we do this part of the checking down here, because it is legitimate to
  // do
  //  $ mtn co -r h:net.venge.monotone
  // and have mtn guess the branch, and then use that branch name as the
  // default directory.  But in this case the branch name will not be set
  // until after the guess_branch() call above:
  {
    bool checkout_dot = false;

    if (args.empty())
      {
        // No checkout dir specified, use branch name for dir.
        E(!app.opts.branch().empty(), origin::user,
          F("you must specify a destination directory"));
        dir = system_path(app.opts.branch(), origin::user);
      }
    else
      {
        // Checkout to specified dir.
        dir = system_path(idx(args, 0));
        if (idx(args, 0) == utf8("."))
          checkout_dot = true;
      }

    if (!checkout_dot)
      require_path_is_nonexistent
        (dir, F("checkout directory '%s' already exists") % dir);
  }

  workspace::create_workspace(app.opts, app.lua, dir);
  workspace work(app);

  roster_t empty_roster, current_roster;

  L(FL("checking out revision %s to directory %s")
    % revid % dir);
  db.get_roster(revid, current_roster);

  revision_t workrev;
  make_revision_for_workspace(revid, cset(), workrev);
  work.put_work_rev(workrev);

  cset checkout;
  make_cset(empty_roster, current_roster, checkout);

  content_merge_checkout_adaptor wca(db);
  work.perform_content_update(empty_roster, current_roster, checkout, wca, false,
                              app.opts.move_conflicting_paths);

  work.maybe_update_inodeprints(db);
  guard.commit();
}

CMD(checkout, "checkout", "co", CMD_REF(tree), N_("[DIRECTORY]"),
    N_("Checks out a revision from the database into a directory"),
    N_("If a revision is given, that's the one that will be checked out.  "
       "Otherwise, it will be the head of the branch (given or implicit).  "
       "If no directory is given, the branch name will be used as directory."),
    options::opts::branch | options::opts::revision |
    options::opts::move_conflicting_paths)
{
  if (args.size() > 1 || app.opts.revision.size() > 1)
    throw usage(execid);

  checkout_common(app, args);
}

CMD_AUTOMATE(checkout, N_("[DIRECTORY]"),
    N_("Checks out a revision from the database into a directory"),
    N_("If a revision is given, that's the one that will be checked out.  "
       "Otherwise, it will be the head of the branch (given or implicit).  "
       "If no directory is given, the branch name will be used as directory."),
    options::opts::branch | options::opts::revision |
    options::opts::move_conflicting_paths)
{
  E(args.size() < 2, origin::user,
    F("wrong argument count"));

  E(app.opts.revision.size() < 2, origin::user,
    F("wrong revision count"));

  checkout_common(app, args);
}

CMD_GROUP(attr, "attr", "", CMD_REF(workspace),
          N_("Manages file attributes"),
          N_("This command is used to set, get or drop file attributes."));

// WARNING: this function is used by both attr_drop and AUTOMATE drop_attribute
// don't change anything that affects the automate interface contract

static void
drop_attr(app_state & app, args_vector const & args)
{
  database db(app);
  workspace work(app);

  roster_t old_roster;
  temp_node_id_source nis;

  work.get_current_roster_shape(db, nis, old_roster);

  file_path path = file_path_external(idx(args, 0));

  E(old_roster.has_node(path), origin::user,
    F("Unknown path '%s'") % path);

  roster_t new_roster = old_roster;
  node_t node = new_roster.get_node_for_update(path);

  // Clear all attrs (or a specific attr).
  if (args.size() == 1)
    {
      for (attr_map_t::iterator i = node->attrs.begin();
           i != node->attrs.end(); ++i)
        i->second = make_pair(false, "");
    }
  else
    {
      I(args.size() == 2);
      attr_key a_key = typecast_vocab<attr_key>(idx(args, 1));
      E(node->attrs.find(a_key) != node->attrs.end(), origin::user,
        F("Path '%s' does not have attribute '%s'")
        % path % a_key);
      node->attrs[a_key] = make_pair(false, "");
    }

  cset cs;
  make_cset(old_roster, new_roster, cs);

  content_merge_empty_adaptor empty;
  work.perform_content_update(old_roster, new_roster, cs, empty);

  parent_map parents;
  work.get_parent_rosters(db, parents);

  revision_t new_work;
  make_revision_for_workspace(parents, new_roster, new_work);
  work.put_work_rev(new_work);
}

CMD(attr_drop, "drop", "", CMD_REF(attr), N_("PATH [ATTR]"),
    N_("Removes attributes from a file"),
    N_("If no attribute is specified, this command removes all attributes "
       "attached to the file given in PATH.  Otherwise only removes the "
       "attribute specified in ATTR."),
    options::opts::none)
{
  if (args.size() != 1 && args.size() != 2)
    throw usage(execid);

  drop_attr(app, args);
}

CMD(attr_get, "get", "", CMD_REF(attr), N_("PATH [ATTR]"),
    N_("Gets the values of a file's attributes"),
    N_("If no attribute is specified, this command prints all attributes "
       "attached to the file given in PATH.  Otherwise it only prints the "
       "attribute specified in ATTR."),
    options::opts::none)
{
  if (args.size() != 1 && args.size() != 2)
    throw usage(execid);

  roster_t new_roster;
  temp_node_id_source nis;

  database db(app);
  workspace work(app);
  work.get_current_roster_shape(db, nis, new_roster);

  file_path path = file_path_external(idx(args, 0));

  E(new_roster.has_node(path), origin::user, F("Unknown path '%s'") % path);
  const_node_t node = new_roster.get_node(path);

  if (args.size() == 1)
    {
      bool has_any_live_attrs = false;
      for (attr_map_t::const_iterator i = node->attrs.begin();
           i != node->attrs.end(); ++i)
        if (i->second.first)
          {
            cout << path << " : "
                 << i->first << '='
                 << i->second.second << '\n';
            has_any_live_attrs = true;
          }
      if (!has_any_live_attrs)
        cout << F("No attributes for '%s'") % path << '\n';
    }
  else
    {
      I(args.size() == 2);
      attr_key a_key = typecast_vocab<attr_key>(idx(args, 1));
      attr_map_t::const_iterator i = node->attrs.find(a_key);
      if (i != node->attrs.end() && i->second.first)
        cout << path << " : "
             << i->first << '='
             << i->second.second << '\n';
      else
        cout << (F("No attribute '%s' on path '%s'")
                 % a_key % path) << '\n';
    }
}

// WARNING: this function is used by both attr_set and AUTOMATE set_attribute
// don't change anything that affects the automate interface contract

static void
set_attr(app_state & app, args_vector const & args)
{
  database db(app);
  workspace work(app);

  roster_t old_roster;
  temp_node_id_source nis;

  work.get_current_roster_shape(db, nis, old_roster);

  file_path path = file_path_external(idx(args, 0));

  E(old_roster.has_node(path), origin::user,
    F("Unknown path '%s'") % path);

  roster_t new_roster = old_roster;
  node_t node = new_roster.get_node_for_update(path);

  attr_key a_key = typecast_vocab<attr_key>(idx(args, 1));
  attr_value a_value = typecast_vocab<attr_value>(idx(args, 2));

  node->attrs[a_key] = make_pair(true, a_value);

  cset cs;
  make_cset(old_roster, new_roster, cs);

  content_merge_empty_adaptor empty;
  work.perform_content_update(old_roster, new_roster, cs, empty);

  parent_map parents;
  work.get_parent_rosters(db, parents);

  revision_t new_work;
  make_revision_for_workspace(parents, new_roster, new_work);
  work.put_work_rev(new_work);
}

CMD(attr_set, "set", "", CMD_REF(attr), N_("PATH ATTR VALUE"),
    N_("Sets an attribute on a file"),
    N_("Sets the attribute given on ATTR to the value specified in VALUE "
       "for the file mentioned in PATH."),
    options::opts::none)
{
  if (args.size() != 3)
    throw usage(execid);

  set_attr(app, args);
}

// Name: get_attributes
// Arguments:
//   1: file / directory name
// Added in: 1.0
// Renamed from attributes to get_attributes in: 5.0
// Purpose: Prints all attributes for the specified path
// Output format: basic_io formatted output, each attribute has its own stanza:
//
// 'format_version'
//         used in case this format ever needs to change.
//         format: ('format_version', the string "1" currently)
//         occurs: exactly once
// 'attr'
//         represents an attribute entry
//         format: ('attr', name, value), ('state', [unchanged|changed|added|dropped])
//         occurs: zero or more times
//
// Error conditions: If the path has no attributes, prints only the
//                   format version, if the file is unknown, escalates
CMD_AUTOMATE(get_attributes, N_("PATH"),
             N_("Prints all attributes for the specified path"),
             "",
             options::opts::none)
{
  E(!args.empty(), origin::user,
    F("wrong argument count"));

  database db(app);
  workspace work(app);

  // retrieve the path
  file_path path = file_path_external(idx(args,0));

  roster_t base, current;
  parent_map parents;
  temp_node_id_source nis;

  // get the base and the current roster of this workspace
  work.get_current_roster_shape(db, nis, current);
  work.get_parent_rosters(db, parents);
  E(parents.size() == 1, origin::user,
    F("this command can only be used in a single-parent workspace"));
  base = parent_roster(parents.begin());

  E(current.has_node(path), origin::user,
    F("Unknown path '%s'") % path);

  // create the printer
  basic_io::printer pr;

  // the current node holds all current attributes (unchanged and new ones)
  const_node_t n = current.get_node(path);
  for (attr_map_t::const_iterator i = n->attrs.begin();
       i != n->attrs.end(); ++i)
  {
    std::string value(i->second.second());
    std::string state;

    // if if the first value of the value pair is false this marks a
    // dropped attribute
    if (!i->second.first)
      {
        // if the attribute is dropped, we should have a base roster
        // with that node. we need to check that for the attribute as well
        // because if it is dropped there as well it was already deleted
        // in any previous revision
        I(base.has_node(path));

        const_node_t prev_node = base.get_node(path);

        // find the attribute in there
        attr_map_t::const_iterator j = prev_node->attrs.find(i->first);
        I(j != prev_node->attrs.end());

        // was this dropped before? then ignore it
        if (!j->second.first) { continue; }

        state = "dropped";
        // output the previous (dropped) value later
        value = j->second.second();
      }
    // this marks either a new or an existing attribute
    else
      {
        if (base.has_node(path))
          {
            const_node_t prev_node = base.get_node(path);
            attr_map_t::const_iterator j =
              prev_node->attrs.find(i->first);

            // the attribute is new if it either hasn't been found
            // in the previous roster or has been deleted there
            if (j == prev_node->attrs.end() || !j->second.first)
              {
                state = "added";
              }
            // check if the attribute's value has been changed
            else if (i->second.second() != j->second.second())
              {
                state = "changed";
              }
            else
              {
                state = "unchanged";
              }
          }
        // its added since the whole node has been just added
        else
          {
            state = "added";
          }
      }

    basic_io::stanza st;
    st.push_str_triple(basic_io::syms::attr, i->first(), value);
    st.push_str_pair(symbol("state"), state);
    pr.print_stanza(st);
  }

  // print the output
  output.write(pr.buf.data(), pr.buf.size());
}

// Name: set_attribute
// Arguments:
//   1: file / directory name
//   2: attribute key
//   3: attribute value
// Added in: 5.0
// Purpose: Edits the workspace revision and sets an attribute on a certain path
//
// Error conditions: If PATH is unknown in the new roster, prints an error and
//                   exits with status 1.
CMD_AUTOMATE(set_attribute, N_("PATH KEY VALUE"),
             N_("Sets an attribute on a certain path"),
             "",
             options::opts::none)
{
  E(args.size() == 3, origin::user,
    F("wrong argument count"));

  set_attr(app, args);
}

// Name: drop_attribute
// Arguments:
//   1: file / directory name
//   2: attribute key (optional)
// Added in: 5.0
// Purpose: Edits the workspace revision and drops an attribute or all
//          attributes of the specified path
//
// Error conditions: If PATH is unknown in the new roster or the specified
//                   attribute key is unknown, prints an error and exits with
//                   status 1.
CMD_AUTOMATE(drop_attribute, N_("PATH [KEY]"),
             N_("Drops an attribute or all of them from a certain path"),
             "",
             options::opts::none)
{
  E(args.size() ==1 || args.size() == 2, origin::user,
    F("wrong argument count"));

  drop_attr(app, args);
}
void perform_commit(app_state & app,
                    database & db,
                    workspace & work,
                    project_t & project,
                    commands::command_id const & execid,
                    vector<file_path> const & paths)
{
  key_store keys(app);

  utf8 log_message("");
  bool log_message_given;
  revision_t restricted_rev;
  parent_map old_rosters;
  roster_t new_roster;
  temp_node_id_source nis;
  cset excluded;

  string date_fmt = get_date_format(app.opts, app.lua, date_time_long);

  work.get_parent_rosters(db, old_rosters);
  work.get_current_roster_shape(db, nis, new_roster);

  node_restriction mask(paths,
                        args_to_paths(app.opts.exclude),
                        app.opts.depth,
                        old_rosters, new_roster, ignored_file(work));

  work.update_current_roster_from_filesystem(new_roster, mask);
  make_restricted_revision(old_rosters, new_roster, mask, restricted_rev,
                           excluded, join_words(execid));
  restricted_rev.check_sane();
  E(restricted_rev.is_nontrivial(), origin::user, F("no changes to commit"));

  set<branch_name> old_branches;
  get_old_branch_names(db, old_rosters, old_branches);

  revision_id restricted_rev_id;
  calculate_ident(restricted_rev, restricted_rev_id);

  // We need the 'if' because guess_branch will try to override any branch
  // picked up from _MTN/options.
  if (app.opts.branch().empty())
    {
      branch_name branchname, bn_candidate;
      for (edge_map::iterator i = restricted_rev.edges.begin();
           i != restricted_rev.edges.end();
           i++)
        {
          // this will prefer --branch if it was set
          guess_branch(app.opts, project, edge_old_revision(i),
                       bn_candidate);
          E(branchname() == "" || branchname == bn_candidate, origin::user,
            F("parent revisions of this commit are in different branches:\n"
              "'%s' and '%s'.\n"
              "please specify a branch name for the commit, with --branch.")
            % branchname % bn_candidate);
          branchname = bn_candidate;
        }

      app.opts.branch = branchname;
    }

  // now that we have an (unedited) branch name, let the hook decide if the
  // changes should get committed at all
  revision_data rev_data;
  write_revision(restricted_rev, rev_data);

  bool changes_validated;
  string reason;

  app.lua.hook_validate_changes(rev_data, app.opts.branch,
                                changes_validated, reason);

  E(changes_validated, origin::user,
    F("changes rejected by hook: %s") % reason);

  if (global_sanity.debug_p())
    {
      L(FL("new manifest '%s'\n"
           "new revision '%s'\n")
        % restricted_rev.new_manifest
        % restricted_rev_id);
    }

  process_commit_message_args(app.opts, log_message_given, log_message);

  E(!(log_message_given && work.has_contents_user_log() &&
      app.opts.msgfile() != "_MTN/log"), origin::user,
    F("_MTN/log is non-empty and log message "
      "was specified on command line\n"
      "perhaps move or delete _MTN/log,\n"
      "or remove --message/--message-file from the command line?"));

  date_t date;
  date_t now = date_t::now();
  string author = app.opts.author();

  if (app.opts.date_given)
    {
      date = app.opts.date;
      L(FL("using specified commit date %s") % date);
    }
  else
    {
      date = now;
      L(FL("using current commit date %s") % date);
    }

  if (author.empty())
    {
      key_identity_info key;
      get_user_key(app.opts, app.lua, db, keys, project, key.id, cache_disable);
      project.complete_key_identity_from_id(keys, app.lua, key);

      if (!app.lua.hook_get_author(app.opts.branch, key, author))
        author = key.official_name();
    }

  if (!log_message_given)
    {
      // This call handles _MTN/log.
      get_log_message_interactively(app.lua, work, project,
                                    restricted_rev_id, restricted_rev,
                                    author, date, app.opts.branch, old_branches,
                                    date_fmt, log_message);

      // We only check for empty log messages when the user entered them
      // interactively.  Consensus was that if someone wanted to explicitly
      // type --message="", then there wasn't any reason to stop them.
      // FIXME: perhaps there should be no changelog cert in this case.

      E(log_message().find_first_not_of("\n\r\t ") != string::npos,
        origin::user,
        F("empty log message; commit canceled"));

      // We save interactively entered log messages to _MTN/log, so if
      // something goes wrong, the next commit will pop up their old
      // log message by default. We only do this for interactively
      // entered messages, because otherwise 'monotone commit -mfoo'
      // giving an error, means that after you correct that error and
      // hit up-arrow to try again, you get an "_MTN/log non-empty and
      // message given on command line" error... which is annoying.

      work.write_user_log(log_message);
    }

  // If the hook doesn't exist, allow the message to be used.
  bool message_validated;
  reason.clear();

  app.lua.hook_validate_commit_message(log_message, rev_data, app.opts.branch,
                                       message_validated, reason);
  E(message_validated, origin::user,
    F("log message rejected by hook: %s") % reason);

  cache_user_key(app.opts, project, keys, app.lua);

  // for the divergence check, below
  set<revision_id> heads;
  project.get_branch_heads(app.opts.branch, heads,
                           app.opts.ignore_suspend_certs);
  unsigned int old_head_size = heads.size();

  P(F("beginning commit on branch '%s'") % app.opts.branch);

  {
    transaction_guard guard(db);

    if (db.revision_exists(restricted_rev_id))
      W(F("revision %s already in database")
        % restricted_rev_id);
    else
      {
        if (global_sanity.debug_p())
          L(FL("inserting new revision %s")
            % restricted_rev_id);

        for (edge_map::const_iterator edge = restricted_rev.edges.begin();
             edge != restricted_rev.edges.end();
             edge++)
          {
            // process file deltas or new files
            cset const & cs = edge_changes(edge);

            for (map<file_path, pair<file_id, file_id> >::const_iterator
                   i = cs.deltas_applied.begin();
                 i != cs.deltas_applied.end(); ++i)
              {
                file_path path = i->first;

                file_id old_content = i->second.first;
                file_id new_content = i->second.second;

                if (db.file_version_exists(new_content))
                  {
                    if (global_sanity.debug_p())
                      L(FL("skipping file delta %s, already in database")
                        % delta_entry_dst(i));
                  }
                else if (db.file_version_exists(old_content))
                  {
                    if (global_sanity.debug_p())
                      L(FL("inserting delta %s -> %s")
                        % old_content % new_content);

                    file_data old_data;
                    data new_data;
                    db.get_file_version(old_content, old_data);
                    read_data(path, new_data);
                    // sanity check
                    file_id tid;
                    calculate_ident(file_data(new_data), tid);
                    E(tid == new_content, origin::system,
                      F("file '%s' modified during commit, aborting")
                      % path);
                    delta del;
                    diff(old_data.inner(), new_data, del);
                    db.put_file_version(old_content,
                                            new_content,
                                            file_delta(del));
                  }
                else
                  // If we don't err out here, the database will later.
                  E(false, origin::no_fault,
                    F("Your database is missing version %s of file '%s'")
                    % old_content % path);
              }

            for (map<file_path, file_id>::const_iterator
                   i = cs.files_added.begin();
                 i != cs.files_added.end(); ++i)
              {
                file_path path = i->first;
                file_id new_content = i->second;

                if (global_sanity.debug_p())
                  L(FL("inserting full version %s") % new_content);
                data new_data;
                read_data(path, new_data);
                // sanity check
                file_id tid;
                calculate_ident(file_data(new_data), tid);
                E(tid == new_content, origin::user,
                  F("file '%s' modified during commit, aborting")
                  % path);
                db.put_file(new_content, file_data(new_data));
              }
          }

        revision_data rdat;
        write_revision(restricted_rev, rdat);
        db.put_revision(restricted_rev_id, rdat);
      }

    // if no --date option was specified and the user didn't edit the date
    // update it to reflect the current time.

    if (date == now && !app.opts.date_given)
      {
        date = date_t::now();
        L(FL("updating commit date %s") % date);
      }

    project.put_standard_certs(keys,
                               restricted_rev_id,
                               app.opts.branch,
                               log_message,
                               date,
                               author);
    guard.commit();
  }

  // the workspace should remember the branch we just committed to.
  work.set_options(app.opts, app.lua, true);

  // the work revision is now whatever changes remain on top of the revision
  // we just checked in.
  revision_t remaining;
  make_revision_for_workspace(restricted_rev_id, excluded, remaining);

  // small race condition here...
  work.put_work_rev(remaining);
  P(F("committed revision %s") % restricted_rev_id);

  work.blank_user_log();

  project.get_branch_heads(app.opts.branch, heads,
                           app.opts.ignore_suspend_certs);
  if (heads.size() > old_head_size && old_head_size > 0) {
    P(F("note: this revision creates divergence\n"
        "note: you may (or may not) wish to run '%s merge'")
      % prog_name);
  }

  work.maybe_update_inodeprints(db);

  {
    // Tell lua what happened. Yes, we might lose some information
    // here, but it's just an indicator for lua, eg. to post stuff to
    // a mailing list. If the user *really* cares about cert validity,
    // multiple certs with same name, etc. they can inquire further,
    // later.
    map<cert_name, cert_value> certs;
    vector<cert> ctmp;
    project.get_revision_certs(restricted_rev_id, ctmp);
    for (vector<cert>::const_iterator i = ctmp.begin();
         i != ctmp.end(); ++i)
      certs.insert(make_pair(i->name, i->value));

    revision_data rdat;
    db.get_revision(restricted_rev_id, rdat);
    app.lua.hook_note_commit(restricted_rev_id, rdat, certs);
  }
}

CMD_PRESET_OPTIONS(commit)
{
  // Dates are never parseable on Win32 (see win32/parse_date.cc),
  // so don't warn about that, just use the default format.
#ifdef WIN32
  opts.no_format_dates = true;
#else
  opts.no_format_dates = false;
#endif
}

CMD(commit, "commit", "ci", CMD_REF(workspace), N_("[PATH]..."),
    N_("Commits workspace changes to the database"),
    "",
    options::opts::branch | options::opts::messages |
    options::opts::date | options::opts::author | options::opts::depth |
    options::opts::exclude)
{
  database db(app);
  workspace work(app);
  project_t project(db);
  perform_commit(app, db, work, project, execid, args_to_paths(args));
}

CMD_NO_WORKSPACE(setup, "setup", "", CMD_REF(tree), N_("[DIRECTORY]"),
    N_("Sets up a new workspace directory"),
    N_("If no directory is specified, uses the current directory."),
    options::opts::branch)
{
  if (args.size() > 1)
    throw usage(execid);

  E(!app.opts.branch().empty(), origin::user,
    F("need --branch argument for setup"));

  string dir;
  if (args.size() == 1)
      dir = idx(args,0)();
  else
      dir = ".";

  system_path workspace_dir(dir, origin::user);
  system_path _MTN_dir(workspace_dir / bookkeeping_root_component);

  require_path_is_nonexistent
    (_MTN_dir, F("bookkeeping directory already exists in '%s'")
     % workspace_dir);

  // only try to remove the complete workspace directory
  // if we're about to create it anyways
  directory_cleanup_helper remove_on_fail(
    directory_exists(workspace_dir) ? _MTN_dir : workspace_dir
  );

  database_path_helper helper(app.lua);
  helper.maybe_set_default_alias(app.opts);

  database db(app);
  db.create_if_not_exists();
  db.ensure_open();

  workspace::create_workspace(app.opts, app.lua, workspace_dir);

  workspace work(app);
  revision_t rev;
  make_revision_for_workspace(revision_id(), cset(), rev);
  work.put_work_rev(rev);

  remove_on_fail.commit();
}

CMD_NO_WORKSPACE(import, "import", "", CMD_REF(tree), N_("DIRECTORY"),
  N_("Imports the contents of a directory into a branch"),
  "",
  options::opts::branch | options::opts::revision |
  options::opts::messages |
  options::opts::dryrun |
  options::opts::no_ignore | options::opts::exclude |
  options::opts::author | options::opts::date)
{
  revision_id ident;
  system_path dir;
  database db(app);
  project_t project(db);

  E(args.size() == 1, origin::user,
    F("you must specify a directory to import"));

  if (app.opts.revision.size() == 1)
    {
      // use specified revision
      complete(app.opts, app.lua, project, idx(app.opts.revision, 0)(), ident);

      guess_branch(app.opts, project, ident);

      I(!app.opts.branch().empty());

      E(project.revision_is_in_branch(ident, app.opts.branch),
        origin::user,
        F("revision %s is not a member of branch %s")
        % ident % app.opts.branch);
    }
  else
    {
      // use branch head revision
      E(!app.opts.branch().empty(), origin::user,
        F("use --revision or --branch to specify the parent revision for the import"));

      set<revision_id> heads;
      project.get_branch_heads(app.opts.branch, heads,
                               app.opts.ignore_suspend_certs);
      if (heads.size() > 1)
        {
          P(F("branch %s has multiple heads:") % app.opts.branch);
          for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
            P(i18n_format("  %s")
              % describe_revision(app.opts, app.lua, project, *i));
          P(F("choose one with '%s import -r<id>'") % prog_name);
          E(false, origin::user,
            F("branch %s has multiple heads") % app.opts.branch);
        }
      if (!heads.empty())
        ident = *(heads.begin());
    }

  dir = system_path(idx(args, 0));
  require_path_is_directory
    (dir,
     F("import directory '%s' doesn't exists") % dir,
     F("import directory '%s' is a file") % dir);

  system_path _MTN_dir = dir / path_component("_MTN");

  require_path_is_nonexistent
    (_MTN_dir, F("bookkeeping directory already exists in '%s'") % dir);

  directory_cleanup_helper remove_on_fail(_MTN_dir);

  workspace::create_workspace(app.opts, app.lua, dir);
  workspace work(app);

  revision_t rev;
  make_revision_for_workspace(ident, cset(), rev);
  work.put_work_rev(rev);

  // prepare stuff for 'add' and so on.
  options save_opts;
  // add --unknown
  save_opts.exclude = app.opts.exclude;
  app.opts.exclude = args_vector();
  app.opts.unknown = true;
  app.opts.recursive = true;
  perform_add(app, db, work, vector<file_path>());
  app.opts.recursive = false;
  app.opts.unknown = false;
  app.opts.exclude = save_opts.exclude;

  // drop --missing
  save_opts.no_ignore = app.opts.no_ignore;
  app.opts.missing = true;
  perform_drop(app, db, work, vector<file_path>());
  app.opts.missing = false;
  app.opts.no_ignore = save_opts.no_ignore;

  // commit
  if (!app.opts.dryrun)
    {
        perform_commit(app, db, work, project,
                       make_command_id("workspace commit"),
                       vector<file_path>());
      remove_on_fail.commit();
    }
  else
    {
      // since the _MTN directory gets removed, don't try to write out
      // _MTN/options at the end
      workspace::used = false;
    }
}

CMD_NO_WORKSPACE(migrate_workspace, "migrate_workspace", "", CMD_REF(tree),
  N_("[DIRECTORY]"),
  N_("Migrates a workspace directory's metadata to the latest format"),
  N_("If no directory is given, defaults to the current workspace."),
  options::opts::none)
{
  if (args.size() > 1)
    throw usage(execid);

  if (args.size() == 1)
    {
      go_to_workspace(system_path(idx(args, 0)));
      workspace::found = true;
    }

  workspace work(app);
  work.migrate_format();

  // FIXME: it seems to be a bit backwards to use the workspace object
  // but reset its usage flag afterwards, but migrate_workspace is a
  // different case: we don't want that this command touches
  // _MTN/options for any other use case than possibly migrating its
  // format and the workspace_migration test enforces that
  workspace::used = false;
}

CMD(refresh_inodeprints, "refresh_inodeprints", "", CMD_REF(tree), "",
    N_("Refreshes the inodeprint cache"),
    "",
    options::opts::none)
{
  database db(app);
  workspace work(app);
  work.enable_inodeprints();
  work.maybe_update_inodeprints(db);
}

CMD_GROUP(bisect, "bisect", "", CMD_REF(informative),
          N_("Search revisions to find where a change first appeared"),
          N_("These commands subdivide a set of revisions into good, bad "
             "and untested subsets and successively narrow the untested set "
             "to find the first revision that introduced some change."));

CMD(reset, "reset", "", CMD_REF(bisect), "",
    N_("Reset the current bisection search"),
    N_("Update the workspace back to the revision from which the bisection "
       "was started and remove all current search information, allowing a new "
       "search to be started."),
    options::opts::none)
{
  if (args.size() != 0)
    throw usage(execid);

  database db(app);
  workspace work(app);
  project_t project(db);

  vector<bisect::entry> info;
  work.get_bisect_info(info);

  E(!info.empty(), origin::user, F("no bisection in progress"));

  parent_map parents;
  work.get_parent_rosters(db, parents);
  E(parents.size() == 1, origin::user,
    F("this command can only be used in a single-parent workspace"));

  revision_id current_id = parent_id(*parents.begin());

  temp_node_id_source nis;
  roster_t current_roster;
  work.get_current_roster_shape(db, nis, current_roster);
  work.update_current_roster_from_filesystem(current_roster);

  E(parent_roster(parents.begin()) == current_roster, origin::user,
    F("this command can only be used in a workspace with no pending changes"));

  bisect::entry start = *info.begin();
  I(start.first == bisect::start);

  revision_id starting_id = start.second;
  P(F("reset back to %s") % describe_revision(app.opts, app.lua, project, starting_id));

  roster_t starting_roster;
  db.get_roster(starting_id, starting_roster);

  cset update;
  make_cset(current_roster, starting_roster, update);

  content_merge_checkout_adaptor adaptor(db);
  work.perform_content_update(current_roster, starting_roster, update, adaptor);

  revision_t starting_rev;
  cset empty;
  make_revision_for_workspace(starting_id, empty, starting_rev);

  work.put_work_rev(starting_rev);
  work.maybe_update_inodeprints(db);

  // note that the various bisect commands didn't change the workspace
  // branch so this should not need to reset it.

  work.remove_bisect_info();
}

static void
bisect_select(options const & opts, lua_hooks & lua,
              project_t & project,
              vector<bisect::entry> const & info,
              revision_id const & current_id,
              revision_id & selected_id)
{
  graph_loader loader(project.db);
  set<revision_id> good, bad, skipped;

  E(!info.empty(), origin::user,
    F("no bisection in progress"));

  for (vector<bisect::entry>::const_iterator i = info.begin();
       i != info.end(); ++i)
    {
      switch (i->first)
        {
        case bisect::start:
          // ignored the for the purposes of bisection
          // used only by reset after bisection is complete
          break;
        case bisect::good:
          good.insert(i->second);
          break;
        case bisect::bad:
          bad.insert(i->second);
          break;
        case bisect::skipped:
          skipped.insert(i->second);
          break;
        case bisect::update:
          // this value is not persisted, it is only used by the bisect
          // update command to rerun a selection and update based on current
          // bisect information
          I(false);
          break;
        }
    }

  if (good.empty() && !bad.empty())
    {
      P(F("bisecting revisions; %d good; %d bad; %d skipped; specify good revisions to start search")
        % good.size() % bad.size() % skipped.size());
      return;
    }
  else if (!good.empty() && bad.empty())
    {
      P(F("bisecting revisions; %d good; %d bad; %d skipped; specify bad revisions to start search")
        % good.size() % bad.size() % skipped.size());
      return;
    }

  I(!good.empty());
  I(!bad.empty());

  // the initial set of revisions to be searched is the intersection between
  // the good revisions and their descendants and the bad revisions and
  // their ancestors. this clamps the search set between these two sets of
  // revisions.

  // NOTE: this also presupposes that the search is looking for a good->bad
  // transition rather than a bad->good transition.

  set<revision_id> good_descendants(good), bad_ancestors(bad);
  loader.load_descendants(good_descendants);
  loader.load_ancestors(bad_ancestors);

  set<revision_id> search;
  set_intersection(good_descendants.begin(), good_descendants.end(),
                   bad_ancestors.begin(), bad_ancestors.end(),
                   inserter(search, search.end()));

  // the searchable set of revisions excludes those explicitly skipped

  set<revision_id> searchable;
  set_difference(search.begin(), search.end(),
                 skipped.begin(), skipped.end(),
                 inserter(searchable, searchable.begin()));

  // partition the searchable set into three subsets
  // - known good revisions
  // - remaining revisions
  // - known bad revisions

  set<revision_id> good_ancestors(good), bad_descendants(bad);
  loader.load_ancestors(good_ancestors);
  loader.load_descendants(bad_descendants);

  set<revision_id> known_good;
  set_intersection(searchable.begin(), searchable.end(),
                   good_ancestors.begin(), good_ancestors.end(),
                   inserter(known_good, known_good.end()));

  set<revision_id> known_bad;
  set_intersection(searchable.begin(), searchable.end(),
                   bad_descendants.begin(), bad_descendants.end(),
                   inserter(known_bad, known_bad.end()));

  // remove known good and known bad revisions from the searchable set

  set<revision_id> removed;
  set_union(known_good.begin(), known_good.end(),
            known_bad.begin(), known_bad.end(),
            inserter(removed, removed.begin()));

  set<revision_id> remaining;
  set_difference(searchable.begin(), searchable.end(),
                 removed.begin(), removed.end(),
                 inserter(remaining, remaining.end()));

  P(F("bisecting %d revisions; %d good; %d bad; %d skipped; %d remaining")
    % search.size() % known_good.size() % known_bad.size() % skipped.size()
    % remaining.size());

  // remove the current revision from the remaining set so it cannot be
  // chosen as the next update target. this may remove the top bad revision
  // and end the search.
  remaining.erase(current_id);

  if (remaining.empty())
    {
      // when no revisions remain to be tested the bisection ends on the bad
      // revision that is the ancestor of all other bad revisions.

      vector<revision_id> bad_sorted;
      toposort(project.db, bad, bad_sorted);
      revision_id first_bad = *bad_sorted.begin();

      P(F("bisection finished at revision %s")
        % describe_revision(opts, lua, project, first_bad));

      // if the workspace is not already at the ending revision return it as
      // the selected revision so that an update back to this revision
      // happens

      if (current_id != first_bad)
        selected_id = first_bad;
      return;
    }

  // bisection is done by toposorting the remaining revs and using the
  // midpoint of the result as the next revision to test

  vector<revision_id> candidates;
  toposort(project.db, remaining, candidates);

  selected_id = candidates[candidates.size()/2];
}

std::ostream &
operator<<(std::ostream & os,
           bisect::type const type)
{
  switch (type)
    {
    case bisect::start:
      os << "start";
      break;
    case bisect::good:
      os << "good";
      break;
    case bisect::bad:
      os << "bad";
      break;
    case bisect::skipped:
      os << "skip";
      break;
    case bisect::update:
      // this value is not persisted, it is only used by the bisect
      // update command to rerun a selection and update based on current
      // bisect information
      I(false);
    break;
  }
  return os;
}

static void
bisect_update(app_state & app, bisect::type type)
{
  database db(app);
  workspace work(app);
  project_t project(db);

  parent_map parents;
  work.get_parent_rosters(db, parents);
  E(parents.size() == 1, origin::user,
    F("this command can only be used in a single-parent workspace"));

  revision_id current_id = parent_id(*parents.begin());

  temp_node_id_source nis;
  roster_t current_roster;
  work.get_current_roster_shape(db, nis, current_roster);
  work.update_current_roster_from_filesystem(current_roster);

  E(parent_roster(parents.begin()) == current_roster, origin::user,
    F("this command can only be used in a workspace with no pending changes"));

  set<revision_id> marked_ids;

  // mark the current or specified revisions as good, bad or skipped
  if (app.opts.revision.empty())
    marked_ids.insert(current_id);
  else
    for (args_vector::const_iterator i = app.opts.revision.begin();
         i != app.opts.revision.end(); i++)
      {
        set<revision_id> rids;
        MM(rids);
        MM(*i);
        complete(app.opts, app.lua, project, (*i)(), rids);
        marked_ids.insert(rids.begin(), rids.end());
      }

  vector<bisect::entry> info;
  work.get_bisect_info(info);

  if (info.empty())
    {
      info.push_back(make_pair(bisect::start, current_id));
      P(F("bisection started at revision %s")
        % describe_revision(app.opts, app.lua, project, current_id));
    }

  if (type != bisect::update)
    {
      // don't allow conflicting or redundant settings
      for (vector<bisect::entry>::const_iterator i = info.begin();
           i != info.end(); ++i)
        {
          if (i->first == bisect::start)
            continue;
          if (marked_ids.find(i->second) != marked_ids.end())
            {
              if (type == i->first)
                {
                  W(F("ignored redundant bisect %s on revision %s")
                    % type % i->second);
                  marked_ids.erase(i->second);
                }
              else
                E(false, origin::user, F("conflicting bisect %s/%s on revision %s")
                  % type % i->first % i->second);
            }
        }

      // push back all marked revs with the appropriate type
      for (set<revision_id>::const_iterator i = marked_ids.begin();
           i != marked_ids.end(); ++i)
        info.push_back(make_pair(type, *i));

      work.put_bisect_info(info);
    }

  revision_id selected_id;
  bisect_select(app.opts, app.lua, project, info, current_id, selected_id);
  if (null_id(selected_id))
    return;

  P(F("updating to %s") % describe_revision(app.opts, app.lua, project, selected_id));

  roster_t selected_roster;
  db.get_roster(selected_id, selected_roster);

  cset update;
  make_cset(current_roster, selected_roster, update);

  content_merge_checkout_adaptor adaptor(db);
  work.perform_content_update(current_roster, selected_roster, update, adaptor,
                              true, app.opts.move_conflicting_paths);

  revision_t selected_rev;
  cset empty;
  make_revision_for_workspace(selected_id, empty, selected_rev);

  work.put_work_rev(selected_rev);
  work.maybe_update_inodeprints(db);

  // this may have updated to a revision not in the branch specified by
  // the workspace branch option. however it cannot update the workspace
  // branch option because the new revision may be in multiple branches.
}

CMD(bisect_status, "status", "", CMD_REF(bisect), "",
    N_("Reports on the current status of the bisection search"),
    N_("Lists the total number of revisions in the search set, "
       "the number of revisions that have been determined to be good or bad, "
       "the number of revisions that have been skipped "
       "and the number of revisions remaining to be tested."),
    options::opts::none)
{
  if (args.size() != 0)
    throw usage(execid);

  database db(app);
  workspace work(app);
  project_t project(db);

  parent_map parents;
  work.get_parent_rosters(db, parents);
  E(parents.size() == 1, origin::user,
    F("this command can only be used in a single-parent workspace"));

  revision_id current_id = parent_id(*parents.begin());

  vector<bisect::entry> info;
  work.get_bisect_info(info);

  revision_id selected_id;
  bisect_select(app.opts, app.lua, project, info, current_id, selected_id);

  if (current_id != selected_id)
    {
      W(F("next revision for bisection testing is %s\n") % selected_id);
      W(F("however this workspace is currently at %s\n") % current_id);
      W(F("run 'bisect update' to update to this revision before testing"));
    }
}

CMD(bisect_update, "update", "", CMD_REF(bisect), "",
    N_("Updates the workspace to the next revision to be tested by bisection"),
    N_("This command can be used if updates by good, bad or skip commands "
       "fail due to blocked paths or other problems."),
    options::opts::move_conflicting_paths)
{
  if (args.size() != 0)
    throw usage(execid);
  bisect_update(app, bisect::update);
}

CMD(bisect_skip, "skip", "", CMD_REF(bisect), "",
    N_("Excludes the current revision or specified revisions from the search"),
    N_("Skipped revisions are removed from the set being searched. Revisions "
       "that cannot be tested for some reason should be skipped."),
    options::opts::revision | options::opts::move_conflicting_paths)
{
  if (args.size() != 0)
    throw usage(execid);
  bisect_update(app, bisect::skipped);
}

CMD(bisect_bad, "bad", "", CMD_REF(bisect), "",
    N_("Marks the current revision or specified revisions as bad"),
    N_("Known bad revisions are removed from the set being searched."),
    options::opts::revision | options::opts::move_conflicting_paths)
{
  if (args.size() != 0)
    throw usage(execid);
  bisect_update(app, bisect::bad);
}

CMD(bisect_good, "good", "", CMD_REF(bisect), "",
    N_("Marks the current revision or specified revisions as good"),
    N_("Known good revisions are removed from the set being searched."),
    options::opts::revision | options::opts::move_conflicting_paths)
{
  if (args.size() != 0)
    throw usage(execid);
  bisect_update(app, bisect::good);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
