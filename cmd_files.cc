// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>

#include "annotate.hh"
#include "revision.hh"
#include "cmd.hh"
#include "diff_patch.hh"
#include "file_io.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "app_state.hh"

using std::cout;
using std::ostream_iterator;
using std::string;
using std::vector;

// fload, fmerge, and fdiff are simple commands for debugging the line
// merger.

CMD(fload, "", CMD_REF(debug), "",
    N_("Loads a file's contents into the database"),
    N_(""),
    options::opts::none)
{
  data dat;
  read_data_stdin(dat);

  file_id f_id;
  file_data f_data(dat);

  calculate_ident (f_data, f_id);

  transaction_guard guard(app.db);
  app.db.put_file(f_id, f_data);
  guard.commit();
}

CMD(fmerge, "", CMD_REF(debug), N_("<parent> <left> <right>"),
    N_("Merges 3 files and outputs the result"),
    N_(""),
    options::opts::none)
{
  if (args.size() != 3)
    throw usage(execid);

  file_id 
    anc_id(idx(args, 0)()), 
    left_id(idx(args, 1)()), 
    right_id(idx(args, 2)());

  file_data anc, left, right;

  N(app.db.file_version_exists (anc_id),
    F("ancestor file id does not exist"));

  N(app.db.file_version_exists (left_id),
    F("left file id does not exist"));

  N(app.db.file_version_exists (right_id),
    F("right file id does not exist"));

  app.db.get_file_version(anc_id, anc);
  app.db.get_file_version(left_id, left);
  app.db.get_file_version(right_id, right);

  vector<string> anc_lines, left_lines, right_lines, merged_lines;

  split_into_lines(anc.inner()(), anc_lines);
  split_into_lines(left.inner()(), left_lines);
  split_into_lines(right.inner()(), right_lines);
  N(merge3(anc_lines, left_lines, right_lines, merged_lines), F("merge failed"));
  copy(merged_lines.begin(), merged_lines.end(), ostream_iterator<string>(cout, "\n"));

}

CMD(fdiff, "", CMD_REF(debug), N_("SRCNAME DESTNAME SRCID DESTID"),
    N_("Differences 2 files and outputs the result"),
    N_(""),
    options::opts::diff_options)
{
  if (args.size() != 4)
    throw usage(execid);

  string const
    & src_name = idx(args, 0)(),
    & dst_name = idx(args, 1)();

  file_id 
    src_id(idx(args, 2)()), 
    dst_id(idx(args, 3)());

  file_data src, dst;

  N(app.db.file_version_exists (src_id),
    F("source file id does not exist"));

  N(app.db.file_version_exists (dst_id),
    F("destination file id does not exist"));

  app.db.get_file_version(src_id, src);
  app.db.get_file_version(dst_id, dst);

  string pattern("");
  if (!app.opts.no_show_encloser)
    app.lua.hook_get_encloser_pattern(file_path_external(utf8(src_name)), pattern);

  make_diff(src_name, dst_name,
            src_id, dst_id,
            src.inner(), dst.inner(),
            cout, app.opts.diff_format, pattern);
}

CMD(annotate, "", CMD_REF(informative), N_("PATH"),
    N_("Prints an annotated copy of a file"),
    N_("Calculates and prints an annotated copy of the given file from "
       "the specified REVISION."),
    options::opts::revision | options::opts::brief)
{
  revision_id rid;

  if (app.opts.revision_selectors.size() == 0)
    app.require_workspace();

  if ((args.size() != 1) || (app.opts.revision_selectors.size() > 1))
    throw usage(execid);

  file_path file = file_path_external(idx(args, 0));
  split_path sp;
  file.split(sp);

  L(FL("annotate file '%s'") % file);

  roster_t roster;
  if (app.opts.revision_selectors.size() == 0)
    {
      // What this _should_ do is calculate the current workspace roster
      // and/or revision and hand that to do_annotate.  This should just
      // work, no matter how many parents the workspace has.  However,
      // do_annotate currently expects to be given a file_t and revision_id
      // corresponding to items already in the database.  This is a minor
      // bug in the one-parent case (it means annotate will not show you
      // changes in the working copy) but is fatal in the two-parent case.
      // Thus, what we do instead is get the parent rosters, refuse to
      // proceed if there's more than one, and give do_annotate what it
      // wants.  See tests/two_parent_workspace_annotate.

      revision_t rev;
      app.work.get_work_rev(rev);
      N(rev.edges.size() == 1,
        F("with no revision selected, this command can only be used in "
          "a single-parent workspace"));

      rid = edge_old_revision(rev.edges.begin());

      // this call will change to something else when the above bug is
      // fixed, and so should not be merged with the identical call in
      // the else branch.
      app.db.get_roster(rid, roster);
    }
  else
    {
      complete(app, idx(app.opts.revision_selectors, 0)(), rid);
      N(!null_id(rid), 
        F("no revision for file '%s' in database") % file);
      N(app.db.revision_exists(rid), 
        F("no such revision '%s'") % rid);

      app.db.get_roster(rid, roster);
    }

  // find the version of the file requested
  N(roster.has_node(sp), 
    F("no such file '%s' in revision '%s'") % file % rid);
  node_t node = roster.get_node(sp);
  N(is_file_t(node), 
    F("'%s' in revision '%s' is not a file") % file % rid);

  file_t file_node = downcast_to_file_t(node);
  L(FL("annotate for file_id %s") % file_node->self);
  do_annotate(app, file_node, rid, app.opts.brief);
}

CMD(identify, "", CMD_REF(debug), N_("[PATH]"),
    N_("Calculates the identity of a file or stdin"),
    N_("If any PATH is given, calculates their identity; otherwise, the "
       "one from the standard input is calculated."),
    options::opts::none)
{
  if (!(args.size() == 0 || args.size() == 1))
    throw usage(execid);

  data dat;

  if (args.size() == 1)
    {
      read_data_for_command_line(idx(args, 0), dat);
    }
  else
    {
      read_data_stdin(dat);
    }

  hexenc<id> ident;
  calculate_ident(dat, ident);
  cout << ident << '\n';
}

// Name: identify
// Arguments:
//   1: a file path
// Added in: 4.2
// Purpose: Prints the fileid of the given file (aka hash)
//
// Output format: a single, 40 byte long hex-encoded id
//
// Error conditions: If the file path doesn't point to a valid file prints
// an error message to stderr and exits with status 1.
CMD_AUTOMATE(identify, N_("PATH"),
             N_("TODO"),
             N_(""),
             options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));
  
  utf8 path = idx(args, 0);
  
  N(path() != "-",
    F("Cannot read from stdin"));
  
  data dat;
  read_data_for_command_line(path, dat);
  
  hexenc<id> ident;
  calculate_ident(dat, ident);
  
  output << ident << '\n';
}

static void
dump_file(std::ostream & output, app_state & app, file_id & ident)
{
  N(app.db.file_version_exists(ident),
    F("no file version %s found in database") % ident);

  file_data dat;
  L(FL("dumping file %s") % ident);
  app.db.get_file_version(ident, dat);
  output.write(dat.inner()().data(), dat.inner()().size());
}

static void
dump_file(std::ostream & output, app_state & app, revision_id rid, utf8 filename)
{
  N(app.db.revision_exists(rid), 
    F("no such revision '%s'") % rid);

  // Paths are interpreted as standard external ones when we're in a
  // workspace, but as project-rooted external ones otherwise.
  file_path fp;
  split_path sp;
  fp = file_path_external(filename);
  fp.split(sp);

  roster_t roster;
  marking_map marks;
  app.db.get_roster(rid, roster, marks);
  N(roster.has_node(sp), 
    F("no file '%s' found in revision '%s'") % fp % rid);
  
  node_t node = roster.get_node(sp);
  N((!null_node(node->self) && is_file_t(node)), 
    F("no file '%s' found in revision '%s'") % fp % rid);

  file_t file_node = downcast_to_file_t(node);
  dump_file(output, app, file_node->content);
}

CMD(cat, "", CMD_REF(informative),
    N_("FILENAME"),
    N_("Prints a file from the database"),
    N_("Fetches the given file FILENAME from the database and prints it "
       "to the standard output."),
    options::opts::revision)
{
  if (args.size() != 1)
    throw usage(execid);

  revision_id rid;
  if (app.opts.revision_selectors.size() == 0)
    {
      app.require_workspace();

      parent_map parents;
      app.work.get_parent_rosters(parents);
      N(parents.size() == 1,
        F("this command can only be used in a single-parent workspace"));
      rid = parent_id(parents.begin());
    }
  else
      complete(app, idx(app.opts.revision_selectors, 0)(), rid);

  dump_file(cout, app, rid, idx(args, 0));
}

// Name: get_file
// Arguments:
//   1: a file id
// Added in: 1.0
// Purpose: Prints the contents of the specified file.
//
// Output format: The file contents are output without modification.
//
// Error conditions: If the file id specified is unknown or invalid prints
// an error message to stderr and exits with status 1.
CMD_AUTOMATE(get_file, N_("FILEID"),
             N_("TODO"),
             N_(""),
             options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));

  file_id ident(idx(args, 0)());
  dump_file(output, app, ident);
}

// Name: get_fileof
// Arguments:
//   1: a filename
//
// Options:
//   r: a revision id
//
// Added in: 4.0
// Purpose: Prints the contents of the specified file.
//
// Output format: The file contents are output without modification.
//
// Error conditions: If the file id specified is unknown or invalid prints
// an error message to stderr and exits with status 1.
CMD_AUTOMATE(get_file_of, N_("FILENAME"),
             N_("TODO"),
             N_(""),
             options::opts::revision)
{
  N(args.size() == 1,
    F("wrong argument count"));

  revision_id rid;
  if (app.opts.revision_selectors.size() == 0)
    {
      app.require_workspace();

      parent_map parents;
      app.work.get_parent_rosters(parents);
      N(parents.size() == 1,
        F("this command can only be used in a single-parent workspace"));
      rid = parent_id(parents.begin());
    }
  else
      complete(app, idx(app.opts.revision_selectors, 0)(), rid);

  dump_file(output, app, rid, idx(args, 0));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
