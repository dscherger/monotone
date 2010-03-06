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
#include "simplestring_xform.hh"
#include "database.hh"
#include "roster.hh"
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
revision_header(revision_id rid, revision_t const & rev, string const & author, 
                date_t const date, branch_name const & branch, 
                bool const branch_changed, utf8 & header)
{
  ostringstream out;
  int const width = 70;

  // FIXME bad suffix
  out << string(width, '-') << '\n'
      << _("Revision: ") << rid << _("       (uncommitted)") << '\n';

  for (edge_map::const_iterator i = rev.edges.begin(); i != rev.edges.end(); ++i)
    {
      revision_id parent = edge_old_revision(*i);
      out << _("Parent: ") << parent << '\n';
    }

  out << _("Author: ") << author << '\n'
      << _("Date: ") << date << '\n';

  if (branch_changed)
    {
      // FIXME bad suffix
      int space = width - branch().length() - 8 - 10;
      if (space < 0) space = 0;
      out << _("Branch: ") << branch << string(space, ' ') << _(" (changed)") << '\n';
    }
  else
    out << _("Branch: ") << branch << '\n';

  out << _("Changelog:") << "\n\n";

  header = utf8(out.str(), origin::internal);
}

static void
revision_summary(revision_t const & rev, utf8 & summary)
{
  // We intentionally do not collapse the final \n into the format
  // strings here, for consistency with newline conventions used by most
  // other format strings.

  ostringstream out;
  revision_id rid;
  calculate_ident(rev, rid);

  for (edge_map::const_iterator i = rev.edges.begin(); i != rev.edges.end(); ++i)
    {
      revision_id parent = edge_old_revision(*i);
      cset const & cs = edge_changes(*i);

      out << '\n';

      // A colon at the end of this string looked nicer, but it made
      // double-click copying from terminals annoying.
      if (!null_id(parent))
        out << _("Changes against parent ") << parent << "\n\n";

      // presumably a merge rev could have an empty edge if one side won
      if (cs.empty())
        out << _("no changes") << '\n';

      for (set<file_path>::const_iterator i = cs.nodes_deleted.begin();
            i != cs.nodes_deleted.end(); ++i)
        out << _("  dropped  ") << *i << '\n';

      for (map<file_path, file_path>::const_iterator
            i = cs.nodes_renamed.begin();
            i != cs.nodes_renamed.end(); ++i)
        out << _("  renamed  ") << i->first
            << _("       to  ") << i->second << '\n';

      for (set<file_path>::const_iterator i = cs.dirs_added.begin();
            i != cs.dirs_added.end(); ++i)
        out << _("  added    ") << *i << '\n';

      for (map<file_path, file_id>::const_iterator i = cs.files_added.begin();
            i != cs.files_added.end(); ++i)
        out << _("  added    ") << i->first << '\n';

      for (map<file_path, pair<file_id, file_id> >::const_iterator
              i = cs.deltas_applied.begin(); i != cs.deltas_applied.end(); ++i)
        out << _("  patched  ") << i->first << '\n';

      for (map<pair<file_path, attr_key>, attr_value >::const_iterator
             i = cs.attrs_set.begin(); i != cs.attrs_set.end(); ++i)
        out << _("  attr on  ") << i->first.first << '\n'
            << _("      set  ") << i->first.second << '\n'
            << _("       to  ") << i->second << '\n';

      // FIXME: naming here could not be more inconsistent
      // the cset calls it attrs_cleared
      // the command is attr drop
      // here it is called unset
      // the revision text uses attr clear 

      for (set<pair<file_path, attr_key> >::const_iterator
             i = cs.attrs_cleared.begin(); i != cs.attrs_cleared.end(); ++i)
        out << _("  attr on  ") << i->first << '\n'
            << _("    unset  ") << i->second << '\n';
    }
  summary = utf8(out.str(), origin::internal);
}

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

static void
get_log_message_interactively(lua_hooks & lua, workspace & work,
                              revision_id const rid, revision_t const & rev,
                              string & author, date_t & date, branch_name & branch,
                              bool const branch_changed,
                              utf8 & log_message)
{
  external instructions(
    _("Ensure the values for Author, Date and Branch are correct, then enter\n"
      "a description of this change following the Changelog line. Any other\n"
      "modifications to the lines below or to the summary of changes will\n"
      "cause the commit to fail.\n"));

  utf8 header;
  utf8 message;
  utf8 summary;

  revision_header(rid, rev, author, date, branch, branch_changed, header);
  work.read_user_log(message);
  revision_summary(rev, summary);

  string text = message();
  if (text.empty() || text.substr(text.length()-1) != "\n")
    {
      text += "\n";
      message = utf8(text, origin::user);
    }

  utf8 full_message(instructions() + header() + message() + summary(), origin::internal);
  
  external input_message;
  external output_message;

  utf8_to_system_best_effort(full_message, input_message);

  E(lua.hook_edit_comment(input_message, output_message),
    origin::user,
    F("edit of log message failed"));

  system_to_utf8(output_message, full_message);

  string raw(full_message());

  // Check the message carefully to make sure the user didn't edit somewhere
  // outside of the author, date, branch or changelog values. The section
  // between the "Changelog: " line from the header and the "Changes against
  // parent ..." line from the summary is where they should be adding
  // lines. Ideally, there is a blank line following "Changelog:"
  // (preceeding the changelog message) and another blank line preceeding
  // "Changes against parent ..." (following the changelog message) but both
  // of these are optional.

  E(raw.find(instructions()) == 0,
    origin::user,
    F("Modifications outside of Author, Date, Branch or Changelog.\n"
      "Commit failed (missing instructions)."));

  if (!summary().empty())
    {
      // ignore the initial blank line when looking for the summary
      size_t pos = raw.find(summary().substr(1));

      // ignore the trailing blank line from the header as well
      E(pos >= instructions().length() + header().length() - 1,
        origin::user,
        F("Modifications outside of Author, Date, Branch or Changelog.\n"
          "Commit failed (missing summary)."));
      raw.resize(pos); // remove the change summary
    }

  raw = raw.substr(instructions().length()); // remove the instructions

  // ensure the first 3 or 4 lines from the header still match
  size_t pos = header().find("Author: ");
  E(header().substr(0, pos) == raw.substr(0, pos),
    origin::user,
    F("Modifications outside of Author, Date, Branch or Changelog.\n"
      "Commit failed (missing revision or parent header)."));

  raw = raw.substr(pos); // remove the leading unchanged header lines

  vector<string> lines;
  split_into_lines(raw, lines);

  E(lines.size() >= 4,
    origin::user,
    F("Modifications outside of Author, Date, Branch or Changelog.\n"
      "Commit failed (missing lines)."));

  vector<string>::const_iterator line = lines.begin();
  E(line->find(_("Author: ")) == 0,
    origin::user,
    F("Modifications outside of Author, Date, Branch or Changelog.\n"
      "Commit failed (missing author)."));

  author = trim(line->substr(8));
  
  ++line;
  E(line->find(_("Date: ")) == 0,
    origin::user,
    F("Modifications outside of Author, Date, Branch or Changelog.\n"
      "Commit failed (missing date)."));

  date = trim(line->substr(6));

  ++line;
  E(line->find(_("Branch: ")) == 0,
    origin::user,
    F("Modifications outside of Author, Date, Branch or Changelog.\n"
      "Commit failed (missing branch)."));

  // FIXME: this suffix and the associated length calculations are bad
  if (branch_changed && line->rfind(_("(changed)")) == line->length() - 9)
    branch = branch_name(trim(line->substr(8, line->length() - 17)), 
                         origin::user);
  else
    branch = branch_name(trim(line->substr(8)), origin::user);

  ++line;
  E(*line == _("Changelog:"),
    origin::user,
    F("Modifications outside of Author, Date, Branch or Changelog.\n"
      "Commit failed (missing changelog)."));

  // now pointing at the optional blank line after Changelog
  ++line;
  join_lines(line, lines.end(), raw);

  raw = trim(raw) + "\n";

  log_message = utf8(raw, origin::user);
}

CMD(revert, "revert", "", CMD_REF(workspace), N_("[PATH]..."),
    N_("Reverts files and/or directories"),
    N_("In order to revert the entire workspace, specify \".\" as the "
       "file name."),
    options::opts::depth | options::opts::exclude | options::opts::missing)
{
  roster_t old_roster, new_roster;
  cset preserved;

  E(app.opts.missing || !args.empty() || !app.opts.exclude_patterns.empty(),
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
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        old_roster, new_roster, ignored_file(work));

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

          bool changed = true;

          if (file_exists(path))
            {
              file_id ident;
              calculate_ident(path, ident);
              // don't touch unchanged files
              if (ident == f->content)
                {
                  L(FL("skipping unchanged %s") % path);
                  changed = false;;
                }
            }

          if (changed)
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

CMD(disapprove, "disapprove", "", CMD_REF(review), N_("REVISION"),
    N_("Disapproves a particular revision"),
    "",
    options::opts::branch | options::opts::messages | options::opts::date |
    options::opts::author)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 1)
    throw usage(execid);

  utf8 log_message("");
  bool log_message_given;
  revision_id r;
  revision_t rev, rev_inverse;
  shared_ptr<cset> cs_inverse(new cset());
  complete(app.opts, app.lua, project, idx(args, 0)(), r);
  db.get_revision(r, rev);

  E(rev.edges.size() == 1, origin::user,
    F("revision %s has %d changesets, cannot invert")
      % r % rev.edges.size());

  guess_branch(app.opts, project, r);
  E(!app.opts.branch().empty(), origin::user,
    F("need --branch argument for disapproval"));

  process_commit_message_args(app.opts, log_message_given, log_message,
                              utf8((FL("disapproval of revision '%s'")
                                    % r).str(),
                                   origin::internal));

  cache_user_key(app.opts, app.lua, db, keys, project);

  edge_entry const & old_edge (*rev.edges.begin());
  db.get_revision_manifest(edge_old_revision(old_edge),
                               rev_inverse.new_manifest);
  {
    roster_t old_roster, new_roster;
    db.get_roster(edge_old_revision(old_edge), old_roster);
    db.get_roster(r, new_roster);
    make_cset(new_roster, old_roster, *cs_inverse);
  }
  rev_inverse.edges.insert(make_pair(r, cs_inverse));

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

  set<file_path> paths;
  bool add_recursive = app.opts.recursive;
  if (app.opts.unknown)
    {
      path_restriction mask(roots, args_to_paths(app.opts.exclude_patterns),
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

CMD(drop, "drop", "rm", CMD_REF(workspace), N_("[PATH]..."),
    N_("Drops files from the workspace"),
    "",
    options::opts::bookkeep_only | options::opts::missing | options::opts::recursive)
{
  if (!app.opts.missing && (args.size() < 1))
    throw usage(execid);

  database db(app);
  workspace work(app);

  set<file_path> paths;
  if (app.opts.missing)
    {
      temp_node_id_source nis;
      roster_t current_roster_shape;
      work.get_current_roster_shape(db, nis, current_roster_shape);
      node_restriction mask(args_to_paths(args),
                            args_to_paths(app.opts.exclude_patterns),
                            app.opts.depth,
                            current_roster_shape, ignored_file(work));
      work.find_missing(current_roster_shape, mask, paths);
    }
  else
    {
      vector<file_path> roots = args_to_paths(args);
      paths = set<file_path>(roots.begin(), roots.end());
    }

  work.perform_deletions(db, paths,
                             app.opts.recursive, app.opts.bookkeep_only);
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
  work.get_parent_rosters(db, old_rosters);
  work.get_current_roster_shape(db, nis, new_roster);

  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
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

  get_user_key(app.opts, app.lua, db, keys, project, key.id);
  project.complete_key_identity(app.lua, key);

  if (!app.lua.hook_get_author(app.opts.branch, key, author))
    author = key.official_name();

  utf8 header;
  utf8 message;
  utf8 summary;

  calculate_ident(rev, rid);

  set<branch_name> old_branches;
  get_old_branch_names(db, old_rosters, old_branches);
  bool branch_changed =
    old_branches.find(app.opts.branch) == old_branches.end();

  revision_header(rid, rev, author, date_t::now(), 
                  app.opts.branch, branch_changed, header);

  work.read_user_log(message);

  string text = message();
  if (text.empty() || text.substr(text.length()-1) != "\n")
    {
      text += "\n";
      message = utf8(text, origin::user);
    }

  revision_summary(rev, summary);

  external header_external;
  external message_external;
  external summary_external;

  utf8_to_system_best_effort(header, header_external);
  utf8_to_system_best_effort(message, message_external);
  utf8_to_system_best_effort(summary, summary_external);

  cout << header_external 
       << message_external
       << summary_external;
}

CMD(checkout, "checkout", "co", CMD_REF(tree), N_("[DIRECTORY]"),
    N_("Checks out a revision from the database into a directory"),
    N_("If a revision is given, that's the one that will be checked out.  "
       "Otherwise, it will be the head of the branch (given or implicit).  "
       "If no directory is given, the branch name will be used as directory."),
    options::opts::branch | options::opts::revision |
    options::opts::move_conflicting_paths)
{
  revision_id revid;
  system_path dir;

  database db(app);
  project_t project(db);
  transaction_guard guard(db, false);

  if (args.size() > 1 || app.opts.revision_selectors.size() > 1)
    throw usage(execid);

  if (app.opts.revision_selectors.empty())
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
              % describe_revision(project, *i));
          P(F("choose one with '%s checkout -r<id>'") % prog_name);
          E(false, origin::user,
            F("branch %s has multiple heads") % app.opts.branch);
        }
      revid = *(heads.begin());
    }
  else if (app.opts.revision_selectors.size() == 1)
    {
      // use specified revision
      complete(app.opts, app.lua, project, idx(app.opts.revision_selectors, 0)(), revid);

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
  node_t node = new_roster.get_node(path);

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
  node_t node = new_roster.get_node(path);

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
  node_t node = new_roster.get_node(path);

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
  node_t n = current.get_node(path);
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

        node_t prev_node = base.get_node(path);

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
            node_t prev_node = base.get_node(path);
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

CMD(commit, "commit", "ci", CMD_REF(workspace), N_("[PATH]..."),
    N_("Commits workspace changes to the database"),
    "",
    options::opts::branch | options::opts::message | options::opts::msgfile
    | options::opts::date | options::opts::author | options::opts::depth
    | options::opts::exclude)
{
  database db(app);
  key_store keys(app);
  workspace work(app);
  project_t project(db);

  utf8 log_message("");
  bool log_message_given;
  revision_t restricted_rev;
  parent_map old_rosters;
  roster_t new_roster;
  temp_node_id_source nis;
  cset excluded;

  work.get_parent_rosters(db, old_rosters);
  work.get_current_roster_shape(db, nis, new_roster);

  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        old_rosters, new_roster, ignored_file(work));

  work.update_current_roster_from_filesystem(new_roster, mask);
  make_restricted_revision(old_rosters, new_roster, mask, restricted_rev,
                           excluded, join_words(execid));
  restricted_rev.check_sane();
  E(restricted_rev.is_nontrivial(), origin::user, F("no changes to commit"));

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

  if (global_sanity.debug_p())
    {
      L(FL("new manifest '%s'\n"
           "new revision '%s'\n")
        % restricted_rev.new_manifest
        % restricted_rev_id);
    }

  process_commit_message_args(app.opts, log_message_given, log_message);

  E(!(log_message_given && work.has_contents_user_log() && app.opts.msgfile() != "_MTN/log"), origin::user,
    F("_MTN/log is non-empty and log message "
      "was specified on command line\n"
      "perhaps move or delete _MTN/log,\n"
      "or remove --message/--message-file from the command line?"));

  date_t date;
  date_t now = date_t::now();
  string author = app.opts.author();

  if (app.opts.date_given)
    date = app.opts.date;
  else
    date = now;

  if (author.empty())
    {
      key_identity_info key;
      get_user_key(app.opts, app.lua, db, keys, project, key.id);
      project.complete_key_identity(app.lua, key);

      if (!app.lua.hook_get_author(app.opts.branch, key, author))
        author = key.official_name();
    }

  if (!log_message_given)
    {
      set<branch_name> old_branches;
      get_old_branch_names(db, old_rosters, old_branches);
      bool branch_changed =
        old_branches.find(app.opts.branch) == old_branches.end();

      // This call handles _MTN/log.
      get_log_message_interactively(app.lua, work, 
                                    restricted_rev_id, restricted_rev,
                                    author, date, app.opts.branch, 
                                    branch_changed,
                                    log_message);

      // We only check for empty log messages when the user entered them
      // interactively.  Consensus was that if someone wanted to explicitly
      // type --message="", then there wasn't any reason to stop them.
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
  string reason, new_manifest_text;

  revision_data new_rev;
  write_revision(restricted_rev, new_rev);

  app.lua.hook_validate_commit_message(log_message, new_rev, app.opts.branch,
                                       message_validated, reason);
  E(message_validated, origin::user,
    F("log message rejected by hook: %s") % reason);

  cache_user_key(app.opts, app.lua, db, keys, project);

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
      date = date_t::now();

    project.put_standard_certs(keys,
                               restricted_rev_id,
                               app.opts.branch,
                               log_message,
                               date,
                               author);
    guard.commit();
  }

  // the workspace should remember the branch we just committed to.
  work.set_options(app.opts, true);

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

CMD_NO_WORKSPACE(setup, "setup", "", CMD_REF(tree), N_("[DIRECTORY]"),
    N_("Sets up a new workspace directory"),
    N_("If no directory is specified, uses the current directory."),
    options::opts::branch)
{
  if (args.size() > 1)
    throw usage(execid);
  E(!app.opts.branch().empty(), origin::user,
    F("need --branch argument for setup"));

  database db(app);
  db.ensure_open();

  string dir;
  if (args.size() == 1)
    dir = idx(args,0)();
  else
    dir = ".";

  workspace::create_workspace(app.opts, app.lua, system_path(dir, origin::user));
  workspace work(app);

  revision_t rev;
  make_revision_for_workspace(revision_id(), cset(), rev);
  work.put_work_rev(rev);
}

CMD_NO_WORKSPACE(import, "import", "", CMD_REF(tree), N_("DIRECTORY"),
  N_("Imports the contents of a directory into a branch"),
  "",
  options::opts::branch | options::opts::revision |
  options::opts::message | options::opts::msgfile |
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

  if (app.opts.revision_selectors.size() == 1)
    {
      // use specified revision
      complete(app.opts, app.lua, project, idx(app.opts.revision_selectors, 0)(), ident);

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
              % describe_revision(project, *i));
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

  workspace::create_workspace(app.opts, app.lua, dir);
  workspace work(app);

  try
    {
      revision_t rev;
      make_revision_for_workspace(ident, cset(), rev);
      work.put_work_rev(rev);

      // prepare stuff for 'add' and so on.
      args_vector empty_args;
      options save_opts;
      // add --unknown
      save_opts.exclude_patterns = app.opts.exclude_patterns;
      app.opts.exclude_patterns = args_vector();
      app.opts.unknown = true;
      app.opts.recursive = true;
      process(app, make_command_id("workspace add"), empty_args);
      app.opts.recursive = false;
      app.opts.unknown = false;
      app.opts.exclude_patterns = save_opts.exclude_patterns;

      // drop --missing
      save_opts.no_ignore = app.opts.no_ignore;
      app.opts.missing = true;
      process(app, make_command_id("workspace drop"), empty_args);
      app.opts.missing = false;
      app.opts.no_ignore = save_opts.no_ignore;

      // commit
      if (!app.opts.dryrun)
        process(app, make_command_id("workspace commit"), empty_args);
    }
  catch (...)
    {
      // clean up before rethrowing
      delete_dir_recursive(bookkeeping_root);
      throw;
    }

  // clean up
  delete_dir_recursive(bookkeeping_root);
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

  workspace work(app, false);
  work.migrate_format();
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
  P(F("reset back to %s") % describe_revision(project, starting_id));

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
bisect_select(project_t & project,
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
        % describe_revision(project, first_bad));

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
  if (app.opts.revision_selectors.empty())
    marked_ids.insert(current_id);
  else
    for (args_vector::const_iterator i = app.opts.revision_selectors.begin();
         i != app.opts.revision_selectors.end(); i++)
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
        % describe_revision(project, current_id));
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
  bisect_select(project, info, current_id, selected_id);
  if (null_id(selected_id))
    return;

  P(F("updating to %s") % describe_revision(project, selected_id));

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
    N_("Lists the total number of revisions in the search set; "
       "the number of revisions that have been determined to be good or bad; "
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
  bisect_select(project, info, current_id, selected_id);

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
