// Copyright (C) 2009, 2010 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <map>
#include <iostream>
#include <sstream>
#include <queue>

#include "asciik.hh"
#include "charset.hh"
#include "cmd.hh"
#include "diff_colorizer.hh"
#include "diff_output.hh"
#include "file_io.hh"
#include "parallel_iter.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "rev_height.hh"
#include "rev_output.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "app_state.hh"
#include "project.hh"
#include "database.hh"
#include "work.hh"
#include "roster.hh"

using std::cout;
using std::make_pair;
using std::map;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;
using std::priority_queue;

static void
get_data(database & db,
         file_path const & path, file_id const & id,
         bool const from_db, data & unpacked)
{
  if (from_db)
    {
      file_data dat;
      db.get_file_version(id, dat);
      unpacked = dat.inner();
    }
  else
    {
      read_data(path, unpacked);
    }
}

static void
dump_diff(lua_hooks & lua,
          file_path const & left_path, file_path const & right_path,
          file_id const left_id, file_id const right_id,
          data const & left_data, data const & right_data,
          diff_type const diff_format,
          bool external_diff_args_given,
          string external_diff_args,
          string const & encloser,
          diff_colorizer const & colorizer,
          ostream & output)
{
  if (diff_format == external_diff)
    {
      bool is_binary = false;
      if (guess_binary(left_data()) || guess_binary(right_data()))
        is_binary = true;

      file_path path = right_path;
      if (path.empty()) // use the left path for deletes
        path = left_path;

      lua.hook_external_diff(path,
                             left_data,
                             right_data,
                             is_binary,
                             external_diff_args_given,
                             external_diff_args,
                             encode_hexenc(left_id.inner()(),
                                           left_id.inner().made_from),
                             encode_hexenc(right_id.inner()(),
                                           right_id.inner().made_from));
    }
  else
    {
      // 60 is somewhat arbitrary, but less than 80
      string patch_sep = string(60, '=');
      output << patch_sep << '\n';

      // see the big comment in diff_output.cc about what paths should be
      string left = left_path.as_internal();
      if (left.empty())
        left = "/dev/null";

      string right = right_path.as_internal();
      if (right.empty())
        right = "/dev/null";

      make_diff(left, right,
                left_id, right_id,
                left_data, right_data,
                output, diff_format,
                encloser, colorizer);
    }

}

static void
dump_diffs(lua_hooks & lua,
           database & db,
           roster_t const & left_roster,
           roster_t const & right_roster,
           std::ostream & output,
           diff_type diff_format,
           bool external_diff_args_given,
           string external_diff_args,
           bool left_from_db,
           bool right_from_db,
           bool show_encloser,
           diff_colorizer const & colorizer)
{
  parallel::iter<node_map> i(left_roster.all_nodes(), right_roster.all_nodes());
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
              file_path left_path, right_path;
              left_roster.get_name(i.left_key(), left_path);
              // right_path is null

              file_id left_id, right_id;
              left_id = downcast_to_file_t(i.left_data())->content;
              // right_id is null

              data left_data, right_data;
              get_data(db, left_path, left_id, left_from_db, left_data);
              // right_data is null

              string encloser("");
              if (show_encloser)
                lua.hook_get_encloser_pattern(left_path, encloser);

              dump_diff(lua,
                        left_path, right_path,
                        left_id, right_id,
                        left_data, right_data,
                        diff_format, external_diff_args_given, external_diff_args,
                        encloser, colorizer, output);
            }
          break;

        case parallel::in_right:
          // added
          if (is_file_t(i.right_data()))
            {
              file_path left_path, right_path;
              // left_path is null
              right_roster.get_name(i.right_key(), right_path);

              file_id left_id, right_id;
              // left_id is null
              right_id = downcast_to_file_t(i.right_data())->content;

              data left_data, right_data;
              // left_data is null
              get_data(db, right_path, right_id, right_from_db, right_data);

              string encloser("");
              if (show_encloser)
                lua.hook_get_encloser_pattern(right_path, encloser);

              dump_diff(lua,
                        left_path, right_path,
                        left_id, right_id,
                        left_data, right_data,
                        diff_format, external_diff_args_given, external_diff_args,
                        encloser, colorizer, output);
            }
          break;

        case parallel::in_both:
          // moved/renamed/patched/attribute changes
          if (is_file_t(i.left_data()))
            {

              file_id left_id, right_id;
              left_id = downcast_to_file_t(i.left_data())->content;
              right_id = downcast_to_file_t(i.right_data())->content;

              if (left_id == right_id)
                continue;

              file_path left_path, right_path;
              left_roster.get_name(i.left_key(), left_path);
              right_roster.get_name(i.right_key(), right_path);

              data left_data, right_data;
              get_data(db, left_path, left_id, left_from_db, left_data);
              get_data(db, right_path, right_id, right_from_db, right_data);

              string encloser("");
              if (show_encloser)
                lua.hook_get_encloser_pattern(right_path, encloser);

              dump_diff(lua,
                        left_path, right_path,
                        left_id, right_id,
                        left_data, right_data,
                        diff_format, external_diff_args_given, external_diff_args,
                        encloser, colorizer, output);
            }
          break;
        }
    }
}

// common functionality for diff and automate content_diff to determine
// revisions and rosters which should be diffed
// FIXME needs app_state in order to create workspace objects (sometimes)
static void
prepare_diff(app_state & app,
             database & db,
             roster_t & old_roster,
             roster_t & new_roster,
             args_vector args,
             bool & old_from_db,
             bool & new_from_db,
             std::string & revheader)
{
  temp_node_id_source nis;
  ostringstream header;

  // initialize before transaction so we have a database to work with.
  project_t project(db);

  E(app.opts.revision_selectors.size() <= 2, origin::user,
    F("more than two revisions given"));

  E(!app.opts.reverse || app.opts.revision_selectors.size() == 1, origin::user,
    F("--reverse only allowed with exactly one revision"));

  if (app.opts.revision_selectors.empty())
    {
      roster_t left_roster, restricted_roster, right_roster;
      revision_id old_rid;
      parent_map parents;
      workspace work(app);

      work.get_parent_rosters(db, parents);

      // With no arguments, which parent should we diff against?
      E(parents.size() == 1, origin::user,
        F("this workspace has more than one parent\n"
          "(specify a revision to diff against with --revision)"));

      old_rid = parent_id(parents.begin());
      left_roster = parent_roster(parents.begin());
      work.get_current_roster_shape(db, nis, right_roster);

      node_restriction mask(args_to_paths(args),
                            args_to_paths(app.opts.exclude_patterns),
                            app.opts.depth,
                            left_roster, right_roster, ignored_file(work));

      work.update_current_roster_from_filesystem(right_roster, mask);

      make_restricted_roster(left_roster, right_roster, restricted_roster,
                             mask);

      old_roster = left_roster;
      new_roster = restricted_roster;

      old_from_db = true;
      new_from_db = false;

      header << "# old_revision [" << old_rid << "]\n";
    }
  else if (app.opts.revision_selectors.size() == 1)
    {
      roster_t left_roster, restricted_roster, right_roster;
      revision_id r_old_id;
      workspace work(app);

      complete(app.opts, app.lua, project, idx(app.opts.revision_selectors, 0)(), r_old_id);

      db.get_roster(r_old_id, left_roster);
      work.get_current_roster_shape(db, nis, right_roster);

      node_restriction mask(args_to_paths(args),
                            args_to_paths(app.opts.exclude_patterns),
                            app.opts.depth,
                            left_roster, right_roster, ignored_file(work));

      work.update_current_roster_from_filesystem(right_roster, mask);

      make_restricted_roster(left_roster, right_roster, restricted_roster,
                             mask);

      if (app.opts.reverse)
        {
          old_roster = restricted_roster;
          new_roster = left_roster;
          old_from_db = false;
          new_from_db = true;
        }
      else
        {
          old_roster = left_roster;
          new_roster = restricted_roster;
          old_from_db = true;
          new_from_db = false;
        }

      header << "# old_revision [" << r_old_id << "]\n";
    }
  else if (app.opts.revision_selectors.size() == 2)
    {
      roster_t left_roster, restricted_roster, right_roster;
      revision_id r_old_id, r_new_id;

      complete(app.opts, app.lua, project, idx(app.opts.revision_selectors, 0)(), r_old_id);
      complete(app.opts, app.lua, project, idx(app.opts.revision_selectors, 1)(), r_new_id);

      db.get_roster(r_old_id, left_roster);
      db.get_roster(r_new_id, right_roster);

      // FIXME: this is *possibly* a UI bug, insofar as we
      // look at the restriction name(s) you provided on the command
      // line in the context of new and old, *not* the working copy.
      // One way of "fixing" this is to map the filenames on the command
      // line to node_ids, and then restrict based on those. This
      // might be more intuitive; on the other hand it would make it
      // impossible to restrict to paths which are dead in the working
      // copy but live between old and new. So ... no rush to "fix" it;
      // discuss implications first.
      //
      // Let the discussion begin...
      //
      // - "map filenames on the command line to node_ids" needs to be done
      //   in the context of some roster, possibly the working copy base or
      //   the current working copy (or both)
      // - diff with two --revision's may be done with no working copy
      // - some form of "peg" revision syntax for paths that would allow
      //   for each path to specify which revision it is relevant to is
      //   probably the "right" way to go eventually. something like file@rev
      //   (which fails for paths with @'s in them) or possibly //rev/file
      //   since versioned paths are required to be relative.

      node_restriction mask(args_to_paths(args),
                            args_to_paths(app.opts.exclude_patterns),
                            app.opts.depth,
                            left_roster, right_roster);

      make_restricted_roster(left_roster, right_roster, restricted_roster,
                             mask);

      old_roster = left_roster;
      new_roster = restricted_roster;

      old_from_db = true;
      new_from_db = true;
    }
  else
    {
      I(false);
    }

  revheader = header.str();
}

void dump_header(std::string const & revs,
                 roster_t const & old_roster,
                 roster_t const & new_roster,
                 std::ostream & out,
                 bool show_if_empty)
{
  cset changes;
  make_cset(old_roster, new_roster, changes);

  data summary;
  write_cset(changes, summary);
  if (summary().empty() && !show_if_empty)
    return;

  vector<string> lines;
  split_into_lines(summary(), lines);
  out << "#\n";
  if (!summary().empty())
    {
      out << revs << "#\n";
      for (vector<string>::iterator i = lines.begin();
           i != lines.end(); ++i)
        out << "# " << *i << '\n';
    }
  else
    {
      out << "# " << _("no changes") << '\n';
    }
  out << "#\n";
}

CMD(diff, "diff", "di", CMD_REF(informative), N_("[PATH]..."),
    N_("Shows current differences"),
    N_("Compares the current tree with the files in the repository and "
       "prints the differences on the standard output.\n"
       "If one revision is given, the diff between the workspace and "
       "that revision is shown.  If two revisions are given, the diff "
       "between them is given.  If no format is specified, unified is "
       "used by default."),
    options::opts::revision | options::opts::depth | options::opts::exclude |
    options::opts::diff_options | options::opts::colorize)
{
  if (app.opts.external_diff_args_given)
    E(app.opts.diff_format == external_diff, origin::user,
      F("--diff-args requires --external\n"
        "try adding --external or removing --diff-args?"));

  roster_t old_roster, new_roster;
  std::string revs;
  bool old_from_db;
  bool new_from_db;
  database db(app);

  prepare_diff(app, db, old_roster, new_roster, args, old_from_db, new_from_db, revs);

  if (!app.opts.without_header)
    {
      dump_header(revs, old_roster, new_roster, cout, true);
    }

  dump_diffs(app.lua, db, old_roster, new_roster, cout,
             app.opts.diff_format,
             app.opts.external_diff_args_given,
             app.opts.external_diff_args,
             old_from_db, new_from_db,
             !app.opts.no_show_encloser,
             diff_colorizer(app.opts.colorize));
}


// Name: content_diff
// Arguments:
//   (optional) one or more files to include
// Added in: 4.0
// Purpose: Availability of mtn diff as automate command.
//
// Output format: Like mtn diff, but with the header part omitted by default.
// If no content changes happened, the output is empty. All file operations
// beside mtn add are omitted, as they don't change the content of the file.
CMD_AUTOMATE(content_diff, N_("[FILE [...]]"),
             N_("Calculates diffs of files"),
             "",
             options::opts::with_header | options::opts::without_header |
             options::opts::revision | options::opts::depth |
             options::opts::exclude | options::opts::reverse)
{
  roster_t old_roster, new_roster;
  string dummy_header;
  bool old_from_db;
  bool new_from_db;
  database db(app);

  prepare_diff(app, db, old_roster, new_roster, args, old_from_db, new_from_db,
               dummy_header);


  if (app.opts.with_header)
    {
      dump_header(dummy_header, old_roster, new_roster, output, false);
    }

  dump_diffs(app.lua, db, old_roster, new_roster, output,
             app.opts.diff_format,
             app.opts.external_diff_args_given, app.opts.external_diff_args,
             old_from_db, new_from_db,
             !app.opts.no_show_encloser,
             diff_colorizer(false));    // never colorize the diff output
}


static void
log_certs(vector<cert> const & certs, ostream & os, cert_name const & name,
          string const date_fmt = "")
{
  bool first = true;

  for (vector<cert>::const_iterator i = certs.begin(); i != certs.end(); ++i)
    {
      if (i->name == name)
        {
          if (first)
            os << " ";
          else
            os << ",";

          if (date_fmt.empty())
            os << i->value;
          else
            {
              I(name == date_cert_name);
              os << date_t(i->value()).as_formatted_localtime(date_fmt);
            }

          first = false;
        }
    }
}

enum log_direction { log_forward, log_reverse };

struct rev_cmp
{
  log_direction direction;
  rev_cmp(log_direction const & direction) : direction(direction) {}
  bool operator() (pair<rev_height, revision_id> const & x,
                   pair<rev_height, revision_id> const & y) const
  {
    switch (direction)
      {
      case log_forward:
        return x.first > y.first; // optional with --next N
      case log_reverse:
        return x.first < y.first; // default and with --last N
      default:
        I(false);
      }
  }
};

typedef priority_queue<pair<rev_height, revision_id>,
                       vector<pair<rev_height, revision_id> >,
                       rev_cmp> frontier_t;

void
log_print_rev (app_state &      app,
               database &       db,
               project_t &      project,
               revision_id      rid,
               revision_t &     rev,
               string           date_fmt,
               node_restriction mask,
               ostream &        out)
{
  cert_name const author_name(author_cert_name);
  cert_name const date_name(date_cert_name);
  cert_name const branch_name(branch_cert_name);
  cert_name const tag_name(tag_cert_name);
  cert_name const changelog_name(changelog_cert_name);
  cert_name const comment_name(comment_cert_name);
  vector<cert> certs;
  project.get_revision_certs(rid, certs);

  if (app.opts.brief)
    {
      out << rid;
      log_certs(certs, out, author_name);
      if (app.opts.no_graph)
        log_certs(certs, out, date_name, date_fmt);
      else
        {
          out << '\n';
          log_certs(certs, out, date_name, date_fmt);
        }
      log_certs(certs, out, branch_name);
      out << '\n';
    }
  else
    {
      utf8 header;
      revision_header(rid, rev, certs, date_fmt, header);

      external header_external;
      utf8_to_system_best_effort(header, header_external);
      out << header_external;

      if (!app.opts.no_files)
        {
          utf8 summary;
          revision_summary(rev, summary);
          external summary_external;
          utf8_to_system_best_effort(summary, summary_external);
          out << summary_external;
        }
    }

  if (app.opts.diffs)
    {
      // if the current roster was loaded above this should hit the
      // cache and not cost much... logging diffs isn't superfast
      // regardless.
      roster_t current_roster;
      db.get_roster(rid, current_roster);

      for (edge_map::const_iterator e = rev.edges.begin();
           e != rev.edges.end(); ++e)
        {
          roster_t parent_roster, restricted_roster;

          db.get_roster(edge_old_revision(e), parent_roster);

          // always show forward diffs from the parent roster to
          // the current roster regardless of the log direction
          make_restricted_roster(parent_roster, current_roster,
                                 restricted_roster, mask);

          dump_diffs(app.lua, db, parent_roster, restricted_roster,
                     out, app.opts.diff_format,
                     app.opts.external_diff_args_given,
                     app.opts.external_diff_args,
                     true, true,
                     !app.opts.no_show_encloser,
                     diff_colorizer(app.opts.colorize));
        }
    }
}

void
log_common (app_state & app,
            args_vector args,
            bool automate,
            std::ostream & output)
{
  database db(app);
  project_t project(db);

  string date_fmt;
  if (app.opts.format_dates)
    {
      if (!app.opts.date_fmt.empty())
        date_fmt = app.opts.date_fmt;
      else
        app.lua.hook_get_date_format_spec(date_time_long, date_fmt);
    }

  long last = app.opts.last;
  long next = app.opts.next;

  log_direction direction = log_reverse;

  E(last == -1 || next == -1, origin::user,
    F("only one of --last/--next allowed"));

  if (next >= 0)
    direction = log_forward;

  graph_loader loader(db);

  rev_cmp cmp(direction);
  frontier_t frontier(cmp);
  revision_id first_rid; // for mapping paths to node ids when restricted

  // start at revisions specified and implied by --from selectors

  set<revision_id> starting_revs;
  if (app.opts.from.empty() && app.opts.revision_selectors.empty())
    {
      // only set default --from revs if no --revision selectors were specified
      workspace work(app, F("try passing a --from revision to start at"));

      revision_t rev;
      work.get_work_rev(rev);
      for (edge_map::const_iterator i = rev.edges.begin();
           i != rev.edges.end(); i++)
        {
          revision_id rid = edge_old_revision(i);
          E(db.revision_exists(rid), origin::user,
            F("workspace parent revision '%s' not found - "
              "did you specify a wrong database?") % rid);
          starting_revs.insert(rid);
          if (i == rev.edges.begin())
            first_rid = rid;
        }
    }
  else if (!app.opts.from.empty())
    {
      for (args_vector::const_iterator i = app.opts.from.begin();
           i != app.opts.from.end(); i++)
        {
          set<revision_id> rids;
          MM(rids);
          MM(*i);
          complete(app.opts, app.lua, project, (*i)(), rids);
          starting_revs.insert(rids.begin(), rids.end());
          if (i == app.opts.from.begin())
            first_rid = *rids.begin();
        }
    }

  L(FL("%d starting revisions") % starting_revs.size());

  // stop at revisions specified and implied by --to selectors

  set<revision_id> ending_revs;
  if (!app.opts.to.empty())
    {
      for (args_vector::const_iterator i = app.opts.to.begin();
           i != app.opts.to.end(); i++)
        {
          set<revision_id> rids;
          MM(rids);
          MM(*i);
          complete(app.opts, app.lua, project, (*i)(), rids);
          ending_revs.insert(rids.begin(), rids.end());
        }

      if (direction == log_forward)
        {
          loader.load_descendants(ending_revs);
        }
      else if (direction == log_reverse)
        {
          loader.load_ancestors(ending_revs);
        }
      else
        I(false);
    }

  L(FL("%d ending revisions") % ending_revs.size());

  // select revisions specified by --revision selectors

  set<revision_id> selected_revs;
  if (!app.opts.revision_selectors.empty())
    {
      for (args_vector::const_iterator i = app.opts.revision_selectors.begin();
           i != app.opts.revision_selectors.end(); i++)
        {
          set<revision_id> rids;
          MM(rids);
          MM(*i);
          complete(app.opts, app.lua, project, (*i)(), rids);

          // only select revs outside of the ending set
          set_difference(rids.begin(), rids.end(),
                         ending_revs.begin(), ending_revs.end(),
                         inserter(selected_revs, selected_revs.end()));
          if (null_id(first_rid) && i == app.opts.revision_selectors.begin())
            first_rid = *rids.begin();
        }
    }

  L(FL("%d selected revisions") % selected_revs.size());

  // the first restriction mask only includes the actual selected nodes
  // of the user, so he doesn't get revisions reported in which not the
  // selected node, but only one of its parents changed
  // the second restriction mask includes the parent nodes implicitely,
  // so we can use it to make a restricted roster with it later on
  node_restriction mask;
  node_restriction mask_diff;

  if (!args.empty() || !app.opts.exclude_patterns.empty())
    {
      // User wants to trace only specific files
      if (app.opts.from.empty())
        {
          workspace work(app);
          roster_t new_roster;
          parent_map parents;
          temp_node_id_source nis;

          work.get_parent_rosters(db, parents);
          work.get_current_roster_shape(db, nis, new_roster);

          mask = node_restriction(args_to_paths(args),
                                  args_to_paths(app.opts.exclude_patterns),
                                  app.opts.depth, parents, new_roster,
                                  ignored_file(work),
                                  restriction::explicit_includes);

          if (app.opts.diffs)
            {
              mask_diff = node_restriction(args_to_paths(args),
                                           args_to_paths(app.opts.exclude_patterns),
                                           app.opts.depth, parents, new_roster,
                                           ignored_file(work),
                                           restriction::implicit_includes);
            }
        }
      else
        {
          // FIXME_RESTRICTIONS: should this add paths from the rosters of
          // all selected revs?
          I(!null_id(first_rid));
          roster_t roster;
          db.get_roster(first_rid, roster);

          mask = node_restriction(args_to_paths(args),
                                  args_to_paths(app.opts.exclude_patterns),
                                  app.opts.depth, roster,
                                  path_always_false<file_path>(),
                                  restriction::explicit_includes);

          if (app.opts.diffs)
            {
              mask_diff = node_restriction(args_to_paths(args),
                                           args_to_paths(app.opts.exclude_patterns),
                                           app.opts.depth, roster,
                                           path_always_false<file_path>(),
                                           restriction::explicit_includes);
            }
        }
    }

  // if --revision was specified without --from log only the selected revs
  bool log_selected(!app.opts.revision_selectors.empty() &&
                    app.opts.from.empty());

  if (log_selected)
    {
      for (set<revision_id>::const_iterator i = selected_revs.begin();
           i != selected_revs.end(); ++i)
        {
          rev_height height;
          db.get_rev_height(*i, height);
          frontier.push(make_pair(height, *i));
        }
      L(FL("log %d selected revisions") % selected_revs.size());
    }
  else
    {
      for (set<revision_id>::const_iterator i = starting_revs.begin();
           i != starting_revs.end(); ++i)
        {
          rev_height height;
          db.get_rev_height(*i, height);
          frontier.push(make_pair(height, *i));
        }
      L(FL("log %d starting revisions") % starting_revs.size());
    }


  // we can use the markings if we walk backwards for a restricted log
  bool use_markings(direction == log_reverse && !mask.empty());

  set<revision_id> seen;
  revision_t rev;
  // this is instantiated even when not used, but it's lightweight
  asciik graph(output);
  while(!frontier.empty() && last != 0 && next != 0)
    {
      revision_id const & rid = frontier.top().second;

      bool print_this = mask.empty();

      if (null_id(rid) || seen.find(rid) != seen.end())
        {
          frontier.pop();
          continue;
        }

      seen.insert(rid);
      db.get_revision(rid, rev);

      set<revision_id> marked_revs;

      if (!mask.empty())
        {
          roster_t roster;
          marking_map markings;
          db.get_roster(rid, roster, markings);

          // get all revision ids mentioned in one of the markings
          for (marking_map::const_iterator m = markings.begin();
               m != markings.end(); ++m)
            {
              node_id const & node = m->first;
              marking_t const & marks = m->second;

              if (mask.includes(roster, node))
                {
                  marked_revs.insert(marks->file_content.begin(),
                                     marks->file_content.end());
                  marked_revs.insert(marks->parent_name.begin(),
                                     marks->parent_name.end());
                  for (map<attr_key, set<revision_id> >::const_iterator
                         a = marks->attrs.begin(); a != marks->attrs.end(); ++a)
                    marked_revs.insert(a->second.begin(), a->second.end());
                }
            }

          // find out whether the current rev is to be printed
          // we don't care about changed paths if it is not marked
          if (!use_markings || marked_revs.find(rid) != marked_revs.end())
            {
              set<node_id> nodes_modified;
              select_nodes_modified_by_rev(db, rev, roster,
                                           nodes_modified);

              for (set<node_id>::const_iterator n = nodes_modified.begin();
                   n != nodes_modified.end(); ++n)
                {
                  // a deleted node will be "modified" but won't
                  // exist in the result.
                  // we don't want to print them.
                  if (roster.has_node(*n) && mask.includes(roster, *n))
                    {
                      print_this = true;
                    }
                }
            }
        }

      if (app.opts.no_merges && rev.is_merge_node())
        print_this = false;
      else if (!app.opts.revision_selectors.empty() &&
          selected_revs.find(rid) == selected_revs.end())
        print_this = false;

      set<revision_id> interesting;
      // if rid is not marked we can jump directly to the marked ancestors,
      // otherwise we need to visit the parents
      if (use_markings && marked_revs.find(rid) == marked_revs.end())
        {
          interesting.insert(marked_revs.begin(), marked_revs.end());
        }
      else if (direction == log_forward)
        {
          loader.load_children(rid, interesting);
        }
      else if (direction == log_reverse)
        {
          loader.load_parents(rid, interesting);
        }
      else
        I(false);

      if (print_this)
        {
          if (automate)
            output << rid << "\n";
          else
            {
              ostringstream out;
              log_print_rev (app, db, project, rid, rev, date_fmt, mask_diff, out);

              string out_system;
              utf8_to_system_best_effort(utf8(out.str(), origin::internal), out_system);
              if (app.opts.no_graph)
                output << out_system;
              else
                graph.print(rid, interesting, out_system);
            }

          if (next > 0)
            next--;
          else if (last > 0)
            last--;
        }
      else if (!automate && use_markings && !app.opts.no_graph)
        graph.print(rid, interesting,
                    (F("(Revision: %s)") % rid).str());

      output.flush();

      frontier.pop(); // beware: rid is invalid from now on

      if (!log_selected)
        {
          // only add revs to the frontier when not logging specific selected revs
          for (set<revision_id>::const_iterator i = interesting.begin();
               i != interesting.end(); ++i)
            {
              if (!app.opts.to.empty() && (ending_revs.find(*i) != ending_revs.end()))
                continue;
              rev_height height;
              db.get_rev_height(*i, height);
              frontier.push(make_pair(height, *i));
            }
        }
    }
}

CMD(log, "log", "", CMD_REF(informative), N_("[PATH] ..."),
    N_("Prints selected history in forward or reverse order"),
    N_("This command prints selected history in forward or reverse order, "
       "filtering it by PATH if given."),
    options::opts::last | options::opts::next |
    options::opts::from | options::opts::to | options::opts::revision |
    options::opts::brief | options::opts::diffs |
    options::opts::depth | options::opts::exclude |
    options::opts::no_merges | options::opts::no_files |
    options::opts::no_graph | options::opts::colorize )
{
  log_common (app, args, false, cout);
}

CMD_AUTOMATE(log, N_("[PATH] ..."),
             N_("list selected revision history"),
             "",
    options::opts::last | options::opts::next |
    options::opts::from | options::opts::to |
    options::opts::depth | options::opts::exclude |
    options::opts::no_merges)
{
  log_common (app, args, true, output);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
