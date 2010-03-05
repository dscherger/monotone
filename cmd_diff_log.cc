// Copyright (C) 2009 Stephen Leake <stephen_leake@stephe-leake.org>
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
#include "diff_output.hh"
#include "file_io.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "rev_height.hh"
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

using boost::lexical_cast;

// The changes_summary structure holds a list all of files and directories
// affected in a revision, and is useful in the 'log' command to print this
// information easily.  It has to be constructed from all cset objects
// that belong to a revision.

struct
changes_summary
{
  cset cs;
  changes_summary(void);
  void add_change_set(cset const & cs);
  void print(ostream & os, size_t max_cols) const;
};

changes_summary::changes_summary(void)
{
}

void
changes_summary::add_change_set(cset const & c)
{
  if (c.empty())
    return;

  // FIXME: not sure whether it matters for an informal summary
  // object like this, but the pre-state names in deletes and renames
  // are not really sensible to union; they refer to different trees
  // so mixing them up in a single set is potentially ambiguous.

  copy(c.nodes_deleted.begin(), c.nodes_deleted.end(),
       inserter(cs.nodes_deleted, cs.nodes_deleted.begin()));

  copy(c.files_added.begin(), c.files_added.end(),
       inserter(cs.files_added, cs.files_added.begin()));

  copy(c.dirs_added.begin(), c.dirs_added.end(),
       inserter(cs.dirs_added, cs.dirs_added.begin()));

  copy(c.nodes_renamed.begin(), c.nodes_renamed.end(),
       inserter(cs.nodes_renamed, cs.nodes_renamed.begin()));

  copy(c.deltas_applied.begin(), c.deltas_applied.end(),
       inserter(cs.deltas_applied, cs.deltas_applied.begin()));

  copy(c.attrs_cleared.begin(), c.attrs_cleared.end(),
       inserter(cs.attrs_cleared, cs.attrs_cleared.begin()));

  copy(c.attrs_set.begin(), c.attrs_set.end(),
       inserter(cs.attrs_set, cs.attrs_set.begin()));
}

static void
print_indented_set(ostream & os,
                   set<file_path> const & s,
                   size_t max_cols)
{
  size_t cols = 8;
  os << "       ";
  for (set<file_path>::const_iterator i = s.begin();
       i != s.end(); i++)
    {
      string str = lexical_cast<string>(*i);
      if (str.empty())
        str = "."; // project root
      if (cols > 8 && cols + str.size() + 1 >= max_cols)
        {
          cols = 8;
          os << "\n       ";
        }
      os << ' ' << str;
      cols += str.size() + 1;
    }
  os << '\n';
}

void
changes_summary::print(ostream & os, size_t max_cols) const
{

  if (! cs.nodes_deleted.empty())
    {
      os << _("Deleted entries:") << '\n';
      print_indented_set(os, cs.nodes_deleted, max_cols);
    }

  if (! cs.nodes_renamed.empty())
    {
      os << _("Renamed entries:") << '\n';
      for (map<file_path, file_path>::const_iterator
           i = cs.nodes_renamed.begin();
           i != cs.nodes_renamed.end(); i++)
        os << "        " << i->first
           << " to " << i->second << '\n';
    }

  if (! cs.files_added.empty())
    {
      set<file_path> tmp;
      for (map<file_path, file_id>::const_iterator
             i = cs.files_added.begin();
           i != cs.files_added.end(); ++i)
        tmp.insert(i->first);
      os << _("Added files:") << '\n';
      print_indented_set(os, tmp, max_cols);
    }

  if (! cs.dirs_added.empty())
    {
      os << _("Added directories:") << '\n';
      print_indented_set(os, cs.dirs_added, max_cols);
    }

  if (! cs.deltas_applied.empty())
    {
      set<file_path> tmp;
      for (map<file_path, pair<file_id, file_id> >::const_iterator
             i = cs.deltas_applied.begin();
           i != cs.deltas_applied.end(); ++i)
        tmp.insert(i->first);
      os << _("Modified files:") << '\n';
      print_indented_set(os, tmp, max_cols);
    }

  if (! cs.attrs_set.empty() || ! cs.attrs_cleared.empty())
    {
      set<file_path> tmp;
      for (set<pair<file_path, attr_key> >::const_iterator
             i = cs.attrs_cleared.begin();
           i != cs.attrs_cleared.end(); ++i)
        tmp.insert(i->first);

      for (map<pair<file_path, attr_key>, attr_value>::const_iterator
             i = cs.attrs_set.begin();
           i != cs.attrs_set.end(); ++i)
        tmp.insert(i->first.first);

      os << _("Modified attrs:") << '\n';
      print_indented_set(os, tmp, max_cols);
    }
}

static void
do_external_diff(options & opts, lua_hooks & lua, database & db,
                 cset const & cs, bool new_is_archived)
{
  for (map<file_path, pair<file_id, file_id> >::const_iterator
         i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      data data_old;
      data data_new;

      file_data f_old;
      db.get_file_version(delta_entry_src(i), f_old);
      data_old = f_old.inner();

      if (new_is_archived)
        {
          file_data f_new;
          db.get_file_version(delta_entry_dst(i), f_new);
          data_new = f_new.inner();
        }
      else
        {
          read_data(delta_entry_path(i), data_new);
        }

      bool is_binary = false;
      if (guess_binary(data_old()) ||
          guess_binary(data_new()))
        is_binary = true;

      lua.hook_external_diff(delta_entry_path(i),
                             data_old,
                             data_new,
                             is_binary,
                             opts.external_diff_args_given,
                             opts.external_diff_args,
                             encode_hexenc(delta_entry_src(i).inner()(),
                                           delta_entry_src(i).inner().made_from),
                             encode_hexenc(delta_entry_dst(i).inner()(),
                                           delta_entry_dst(i).inner().made_from));
    }
}

static void
dump_diffs(lua_hooks & lua,
           database & db,
           cset const & cs,
           set<file_path> const & paths,
           std::ostream & output,
           diff_type diff_format,
           bool new_is_archived,
           bool old_is_archived,
           bool show_encloser,
           bool limit_paths)
{
  // 60 is somewhat arbitrary, but less than 80
  string patch_sep = string(60, '=');

  for (map<file_path, file_id>::const_iterator
         i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      if (limit_paths && paths.find(i->first) == paths.end())
        continue;

      output << patch_sep << '\n';
      data unpacked;
      vector<string> lines;

      if (new_is_archived)
        {
          file_data dat;
          db.get_file_version(i->second, dat);
          unpacked = dat.inner();
        }
      else
        {
          read_data(i->first, unpacked);
        }

      std::string pattern("");
      if (show_encloser)
        lua.hook_get_encloser_pattern(i->first, pattern);

      make_diff(i->first.as_internal(),
                i->first.as_internal(),
                i->second,
                i->second,
                data(), unpacked,
                output, diff_format, pattern);
    }

  map<file_path, file_path> reverse_rename_map;

  for (map<file_path, file_path>::const_iterator
         i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    {
      reverse_rename_map.insert(make_pair(i->second, i->first));
    }

  for (map<file_path, pair<file_id, file_id> >::const_iterator
         i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      if (limit_paths && paths.find(i->first) == paths.end())
        continue;

      file_data f_old;
      data data_old, data_new;

      output << patch_sep << '\n';

      if (old_is_archived)
        {
          db.get_file_version(delta_entry_src(i), f_old);
          data_old = f_old.inner();
        }
      else
        {
          I(new_is_archived);
          read_data(delta_entry_path(i), data_old);
        }

      if (new_is_archived)
        {
          file_data f_new;
          db.get_file_version(delta_entry_dst(i), f_new);
          data_new = f_new.inner();
        }
      else
        {
          I(old_is_archived);
          read_data(delta_entry_path(i), data_new);
        }

      file_path dst_path = delta_entry_path(i);
      file_path src_path = dst_path;
      map<file_path, file_path>::const_iterator re;
      re = reverse_rename_map.find(dst_path);
      if (re != reverse_rename_map.end())
        src_path = re->second;

      std::string pattern("");
      if (show_encloser)
        lua.hook_get_encloser_pattern(src_path, pattern);

      make_diff(src_path.as_internal(),
                dst_path.as_internal(),
                delta_entry_src(i),
                delta_entry_dst(i),
                data_old, data_new,
                output, diff_format, pattern);
    }
}

static void
dump_diffs(lua_hooks & lua,
           database & db,
           cset const & cs,
           std::ostream & output,
           diff_type diff_format,
           bool new_is_archived,
           bool old_is_archived,
           bool show_encloser)
{
  set<file_path> dummy;
  dump_diffs(lua, db, cs, dummy, output,
             diff_format, new_is_archived, old_is_archived, show_encloser, false);
}

// common functionality for diff and automate content_diff to determine
// revisions and rosters which should be diffed
// FIXME needs app_state in order to create workspace objects (sometimes)
static void
prepare_diff(app_state & app,
             database & db,
             cset & included,
             args_vector args,
             bool & new_is_archived,
             bool & old_is_archived,
             std::string & revheader)
{
  temp_node_id_source nis;
  ostringstream header;

  // The resulting diff is output in 'included'.

  // initialize before transaction so we have a database to work with.
  project_t project(db);

  E(app.opts.revision_selectors.size() <= 2, origin::user,
    F("more than two revisions given"));

  E(!app.opts.reverse || app.opts.revision_selectors.size() == 1, origin::user,
    F("--reverse only allowed with exactly one revision"));

  if (app.opts.revision_selectors.empty())
    {
      roster_t old_roster, restricted_roster, new_roster;
      revision_id old_rid;
      parent_map parents;
      workspace work(app);

      work.get_parent_rosters(db, parents);

      // With no arguments, which parent should we diff against?
      E(parents.size() == 1, origin::user,
        F("this workspace has more than one parent\n"
          "(specify a revision to diff against with --revision)"));

      old_rid = parent_id(parents.begin());
      old_roster = parent_roster(parents.begin());
      work.get_current_roster_shape(db, nis, new_roster);

      node_restriction mask(args_to_paths(args),
                            args_to_paths(app.opts.exclude_patterns),
                            app.opts.depth,
                            old_roster, new_roster, ignored_file(work));

      work.update_current_roster_from_filesystem(new_roster, mask);

      make_restricted_roster(old_roster, new_roster, restricted_roster,
                             mask);

      make_cset(old_roster, restricted_roster, included);

      new_is_archived = false;
      old_is_archived = true;
      header << "# old_revision [" << old_rid << "]\n";
    }
  else if (app.opts.revision_selectors.size() == 1)
    {
      roster_t old_roster, restricted_roster, new_roster;
      revision_id r_old_id;
      workspace work(app);

      complete(app.opts, app.lua, project, idx(app.opts.revision_selectors, 0)(), r_old_id);

      db.get_roster(r_old_id, old_roster);
      work.get_current_roster_shape(db, nis, new_roster);

      node_restriction mask(args_to_paths(args),
                            args_to_paths(app.opts.exclude_patterns),
                            app.opts.depth,
                            old_roster, new_roster, ignored_file(work));

      work.update_current_roster_from_filesystem(new_roster, mask);

      make_restricted_roster(old_roster, new_roster, restricted_roster,
                             mask);

      if (app.opts.reverse)
        {
          make_cset(restricted_roster, old_roster, included);
          new_is_archived = true;
          old_is_archived = false;
        }
      else
        {
          make_cset(old_roster, restricted_roster, included);
          new_is_archived = false;
          old_is_archived = true;
        }

      header << "# old_revision [" << r_old_id << "]\n";
    }
  else if (app.opts.revision_selectors.size() == 2)
    {
      roster_t old_roster, restricted_roster, new_roster;
      revision_id r_old_id, r_new_id;

      complete(app.opts, app.lua, project, idx(app.opts.revision_selectors, 0)(), r_old_id);
      complete(app.opts, app.lua, project, idx(app.opts.revision_selectors, 1)(), r_new_id);

      db.get_roster(r_old_id, old_roster);
      db.get_roster(r_new_id, new_roster);

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
                            old_roster, new_roster);

      make_restricted_roster(old_roster, new_roster, restricted_roster,
                             mask);

      make_cset(old_roster, restricted_roster, included);

      new_is_archived = true;
      old_is_archived = true;

    }
  else
    {
      I(false);
    }

    revheader = header.str();
}

void dump_header(std::string const & revs,
                 cset const & changes,
                 std::ostream & out,
                 bool show_if_empty)
{
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
    options::opts::revision | options::opts::depth | options::opts::exclude
    | options::opts::diff_options)
{
  if (app.opts.external_diff_args_given)
    E(app.opts.diff_format == external_diff, origin::user,
      F("--diff-args requires --external\n"
        "try adding --external or removing --diff-args?"));

  cset included;
  std::string revs;
  bool new_is_archived;
  bool old_is_archived;
  database db(app);

  prepare_diff(app, db, included, args, new_is_archived, old_is_archived, revs);

  if (!app.opts.without_header)
    {
      dump_header(revs, included, cout, true);
    }

  if (app.opts.diff_format == external_diff)
    {
      do_external_diff(app.opts, app.lua, db, included, new_is_archived);
    }
  else
    {
      dump_diffs(app.lua, db, included, cout,
                 app.opts.diff_format, new_is_archived, old_is_archived,
                 !app.opts.no_show_encloser);
    }
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
  cset included;
  std::string dummy_header;
  bool new_is_archived;
  bool old_is_archived;
  database db(app);

  prepare_diff(app, db, included, args, new_is_archived, old_is_archived, dummy_header);


  if (app.opts.with_header)
    {
      dump_header(dummy_header, included, output, false);
    }

  dump_diffs(app.lua, db, included, output,
             app.opts.diff_format, new_is_archived, old_is_archived, !app.opts.no_show_encloser);
}


static void
log_certs(vector<cert> const & certs, ostream & os, cert_name const & name,
          char const * label, char const * separator,
          bool multiline, bool newline)
{
  bool first = true;

  if (multiline)
    newline = true;

  for (vector<cert>::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      if (i->name == name)
        {
          if (first)
            os << label;
          else
            os << separator;

          if (multiline)
            os << "\n\n";
          os << i->value;
          if (newline)
            os << '\n';

          first = false;
        }
    }
}

static void
log_certs(vector<cert> const & certs, ostream & os, cert_name const & name,
          char const * label, bool multiline)
{
  log_certs(certs, os, name, label, label, multiline, true);
}

static void
log_certs(vector<cert> const & certs, ostream & os, cert_name const & name)
{
  log_certs(certs, os, name, " ", ",", false, false);
}

static void
log_date_certs(vector<cert> const & certs, ostream & os, string const & fmt,
               char const * label, char const * separator,
               bool multiline, bool newline)
{
  cert_name const date_name(date_cert_name);

  bool first = true;
  if (multiline)
    newline = true;

  for (vector<cert>::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      if (i->name == date_name)
        {
          if (first)
            os << label;
          else
            os << separator;

          if (multiline)
            os << "\n\n";
          if (fmt.empty())
            os << i->value;
          else
            os << date_t(i->value()).as_formatted_localtime(fmt);
          if (newline)
            os << '\n';

          first = false;
        }
    }
}

static void
log_date_certs(vector<cert> const & certs, ostream & os, string const & fmt,
               char const * label, bool multiline)
{
  log_date_certs(certs, os, fmt, label, label, multiline, true);
}

static void
log_date_certs(vector<cert> const & certs, ostream & os, string const & fmt)
{
  log_date_certs(certs, os, fmt, " ", ",", false, false);
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

CMD(log, "log", "", CMD_REF(informative), N_("[PATH] ..."),
    N_("Prints selected history in forward or reverse order"),
    N_("This command prints selected history in forward or reverse order, "
       "filtering it by PATH if given."),
    options::opts::last | options::opts::next |
    options::opts::from | options::opts::to | options::opts::revision |
    options::opts::brief | options::opts::diffs |
    options::opts::format_dates | options::opts::date_fmt |
    options::opts::depth | options::opts::exclude |
    options::opts::no_merges | options::opts::no_files |
    options::opts::no_graph)
{
  database db(app);
  project_t project(db);

  string date_fmt;
  if (app.opts.format_dates)
    {
      if (!app.opts.date_fmt.empty())
        date_fmt = app.opts.date_fmt;
      else
        app.lua.hook_get_date_format_spec(date_fmt);
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
          starting_revs.insert(edge_old_revision(i));
          if (i == rev.edges.begin())
            first_rid = edge_old_revision(i);
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

  node_restriction mask;

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
                                  ignored_file(work));
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
                                  app.opts.depth, roster);
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

  cert_name const author_name(author_cert_name);
  cert_name const branch_name(branch_cert_name);
  cert_name const tag_name(tag_cert_name);
  cert_name const changelog_name(changelog_cert_name);
  cert_name const comment_name(comment_cert_name);

  // we can use the markings if we walk backwards for a restricted log
  bool use_markings(direction == log_reverse && !mask.empty());

  set<revision_id> seen;
  revision_t rev;
  // this is instantiated even when not used, but it's lightweight
  asciik graph(cout);
  while(!frontier.empty() && last != 0 && next != 0)
    {
      revision_id const & rid = frontier.top().second;

      bool print_this = mask.empty();
      set<file_path> diff_paths;

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
                      if (app.opts.diffs)
                        {
                          file_path fp;
                          roster.get_name(*n, fp);
                          diff_paths.insert(fp);
                        }
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
          vector<cert> certs;
          project.get_revision_certs(rid, certs);

          ostringstream out;
          if (app.opts.brief)
            {
              out << rid;
              log_certs(certs, out, author_name);
              if (app.opts.no_graph)
                log_date_certs(certs, out, date_fmt);
              else
                {
                  out << '\n';
                  log_date_certs(certs, out, date_fmt, "", "", false, false);
                }
              log_certs(certs, out, branch_name);
              out << '\n';
            }
          else
            {
              out << string(65, '-') << '\n';
              out << _("Revision: ") << rid << '\n';

              changes_summary csum;

              set<revision_id> ancestors;

              for (edge_map::const_iterator e = rev.edges.begin();
                   e != rev.edges.end(); ++e)
                {
                  ancestors.insert(edge_old_revision(e));
                  csum.add_change_set(edge_changes(e));
                }

              for (set<revision_id>::const_iterator anc = ancestors.begin();
                   anc != ancestors.end(); ++anc)
                out << _("Ancestor: ") << *anc << '\n';

              log_certs(certs, out, author_name,   _("Author: "), false);
              log_date_certs(certs, out, date_fmt, _("Date: "), false);
              log_certs(certs, out, branch_name,   _("Branch: "), false);
              log_certs(certs, out, tag_name,      _("Tag: "),    false);

              if (!app.opts.no_files && !csum.cs.empty())
                {
                  out << '\n';
                  csum.print(out, 70);
                  out << '\n';
                }

              log_certs(certs, out, changelog_name, _("ChangeLog: "), true);
              log_certs(certs, out, comment_name,   _("Comments: "),  true);
            }

          if (app.opts.diffs)
            {
              for (edge_map::const_iterator e = rev.edges.begin();
                   e != rev.edges.end(); ++e)
                dump_diffs(app.lua, db, edge_changes(e), diff_paths, out,
                           app.opts.diff_format, true, true,
                           !app.opts.no_show_encloser, !mask.empty());
            }

          if (next > 0)
            next--;
          else if (last > 0)
            last--;

          string out_system;
          utf8_to_system_best_effort(utf8(out.str(), origin::internal), out_system);
          if (app.opts.no_graph)
            cout << out_system;
          else
            graph.print(rid, interesting, out_system);
        }
      else if (use_markings && !app.opts.no_graph)
        graph.print(rid, interesting,
                    (F("(Revision: %s)") % rid).str());

      cout.flush();

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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
