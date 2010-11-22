// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iostream>
#include <iterator>

#include "annotate.hh"
#include "revision.hh"
#include "cmd.hh"
#include "diff_output.hh"
#include "merge_content.hh"
#include "file_io.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "app_state.hh"
#include "project.hh"
#include "database.hh"
#include "work.hh"
#include "roster.hh"
#include "lexical_cast.hh"

using std::cout;
using std::ostream_iterator;
using std::string;
using std::vector;
using boost::lexical_cast;

// fload, fmerge, and fdiff are simple commands for debugging the line
// merger.

CMD(fload, "fload", "", CMD_REF(debug), "",
    N_("Loads a file's contents into the database"),
    "",
    options::opts::none)
{
  data dat;
  read_data_stdin(dat);

  file_id f_id;
  file_data f_data(dat);

  calculate_ident (f_data, f_id);

  database db(app);
  transaction_guard guard(db);
  db.put_file(f_id, f_data);
  guard.commit();
}

CMD(fmerge, "fmerge", "", CMD_REF(debug),
    N_("PARENT_FILEID LEFT_FILEID RIGHT_FILEID"),
    N_("Merges 3 files and outputs the result"),
    "",
    options::opts::none)
{
  if (args.size() != 3)
    throw usage(execid);

  file_id
    anc_id(decode_hexenc_as<file_id>(idx(args, 0)(), origin::user)),
    left_id(decode_hexenc_as<file_id>(idx(args, 1)(), origin::user)),
    right_id(decode_hexenc_as<file_id>(idx(args, 2)(), origin::user));

  file_data anc, left, right;

  database db(app);
  E(db.file_version_exists (anc_id), origin::user,
    F("ancestor file id does not exist"));

  E(db.file_version_exists (left_id), origin::user,
    F("left file id does not exist"));

  E(db.file_version_exists (right_id), origin::user,
    F("right file id does not exist"));

  db.get_file_version(anc_id, anc);
  db.get_file_version(left_id, left);
  db.get_file_version(right_id, right);

  vector<string> anc_lines, left_lines, right_lines, merged_lines;

  split_into_lines(anc.inner()(), anc_lines);
  split_into_lines(left.inner()(), left_lines);
  split_into_lines(right.inner()(), right_lines);
  E(merge3(anc_lines, left_lines, right_lines, merged_lines),
    origin::user, F("merge failed"));
  copy(merged_lines.begin(), merged_lines.end(), ostream_iterator<string>(cout, "\n"));

}

CMD(fdiff, "fdiff", "", CMD_REF(debug), N_("SRCNAME DESTNAME SRCID DESTID"),
    N_("Differences 2 files and outputs the result"),
    "",
    options::opts::diff_options)
{
  if (args.size() != 4)
    throw usage(execid);

  string const
    & src_name = idx(args, 0)(),
    & dst_name = idx(args, 1)();

  file_id
    src_id(decode_hexenc_as<file_id>(idx(args, 2)(), origin::user)),
    dst_id(decode_hexenc_as<file_id>(idx(args, 3)(), origin::user));

  file_data src, dst;

  database db(app);
  E(db.file_version_exists (src_id), origin::user,
    F("source file id does not exist"));

  E(db.file_version_exists (dst_id), origin::user,
    F("destination file id does not exist"));

  db.get_file_version(src_id, src);
  db.get_file_version(dst_id, dst);

  string pattern("");
  if (!app.opts.no_show_encloser)
    app.lua.hook_get_encloser_pattern(file_path_external(utf8(src_name,
                                                              origin::user)),
                                      pattern);

  make_diff(src_name, dst_name,
            src_id, dst_id,
            src.inner(), dst.inner(),
            cout, app.opts.diff_format, pattern);
}

CMD(annotate, "annotate", "", CMD_REF(informative), N_("PATH"),
    N_("Prints an annotated copy of a file"),
    N_("Calculates and prints an annotated copy of the given file from "
       "the specified REVISION."),
    options::opts::revision | options::opts::revs_only)
{
  revision_id rid;
  database db(app);
  project_t project(db, app.lua, app.opts);

  if ((args.size() != 1) || (app.opts.revision.size() > 1))
    throw usage(execid);

  file_path file = file_path_external(idx(args, 0));

  L(FL("annotate file '%s'") % file);

  roster_t roster;
  if (app.opts.revision.empty())
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
      workspace work(app);
      revision_t rev;
      work.get_work_rev(rev);
      E(rev.edges.size() == 1, origin::user,
        F("with no revision selected, this command can only be used in "
          "a single-parent workspace"));

      rid = edge_old_revision(rev.edges.begin());

      // this call will change to something else when the above bug is
      // fixed, and so should not be merged with the identical call in
      // the else branch.
      db.get_roster(rid, roster);
    }
  else
    {
      complete(app.opts, app.lua, project, idx(app.opts.revision, 0)(), rid);
      db.get_roster(rid, roster);
    }

  // find the version of the file requested
  E(roster.has_node(file), origin::user,
    F("no such file '%s' in revision '%s'")
      % file % rid);
  const_node_t node = roster.get_node(file);
  E(is_file_t(node), origin::user,
    F("'%s' in revision '%s' is not a file")
      % file % rid);

  const_file_t file_node = downcast_to_file_t(node);
  L(FL("annotate for file_id %s") % file_node->self);
  do_annotate(app, project, file_node, rid, app.opts.revs_only);
}

CMD(identify, "identify", "", CMD_REF(debug), N_("[PATH]"),
    N_("Calculates the identity of a file or stdin"),
    N_("If any PATH is given, calculates their identity; otherwise, the "
       "one from the standard input is calculated."),
    options::opts::none)
{
  if (args.size() > 1)
    throw usage(execid);

  data dat;

  if (args.empty())
    read_data_stdin(dat);
  else
    read_data_for_command_line(idx(args, 0), dat);

  id ident;
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
             N_("Prints the file identifier of a file"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  utf8 path = idx(args, 0);

  E(path() != "-", origin::user,
    F("Cannot read from stdin"));

  data dat;
  read_data_for_command_line(path, dat);

  id ident;
  calculate_ident(dat, ident);
  output << ident << '\n';
}

static void
dump_file(database & db, std::ostream & output, file_id const & ident)
{
  E(db.file_version_exists(ident), origin::user,
    F("no file version %s found in database") % ident);

  file_data dat;
  L(FL("dumping file %s") % ident);
  db.get_file_version(ident, dat);
  output << dat;
}

static void
dump_file(database & db, std::ostream & output, revision_id rid, utf8 filename)
{
  E(db.revision_exists(rid), origin::user,
    F("no such revision '%s'") % rid);

  // Paths are interpreted as standard external ones when we're in a
  // workspace, but as project-rooted external ones otherwise.
  file_path fp = file_path_external(filename);

  roster_t roster;
  marking_map marks;
  db.get_roster(rid, roster, marks);
  E(roster.has_node(fp), origin::user,
    F("no file '%s' found in revision '%s'") % fp % rid);

  const_node_t node = roster.get_node(fp);
  E((!null_node(node->self) && is_file_t(node)), origin::user,
    F("no file '%s' found in revision '%s'") % fp % rid);

  const_file_t file_node = downcast_to_file_t(node);
  dump_file(db, output, file_node->content);
}

CMD(cat, "cat", "", CMD_REF(informative),
    N_("FILENAME"),
    N_("Prints a file from the database"),
    N_("Fetches the given file FILENAME from the database and prints it "
       "to the standard output."),
    options::opts::revision)
{
  if (args.size() != 1)
    throw usage(execid);

  database db(app);
  revision_id rid;
  if (app.opts.revision.empty())
    {
      workspace work(app);
      parent_map parents;
      work.get_parent_rosters(db, parents);
      E(parents.size() == 1, origin::user,
        F("this command can only be used in a single-parent workspace"));
      rid = parent_id(parents.begin());
    }
  else
    {
      project_t project(db, app.lua, app.opts);
      complete(app.opts, app.lua, project, idx(app.opts.revision, 0)(), rid);
    }

  dump_file(db, cout, rid, idx(args, 0));
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
             N_("Prints the contents of a file (given an identifier)"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);
  hexenc<id> hident(idx(args, 0)(), origin::user);
  file_id ident(decode_hexenc_as<file_id>(hident(), hident.made_from));
  dump_file(db, output, ident);
}

// Name: get_file_size
// Arguments:
//   1: a file id
// Added in: 13.0
// Purpose: Prints the recorded size of the file in bytes
//
// Output format: A integer > 0
//
// Error conditions: If the file id specified is unknown or invalid prints
// an error message to stderr and exits with status 1.
CMD_AUTOMATE(get_file_size, N_("FILEID"),
             N_("Prints the size of a file (given an identifier)"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);
  hexenc<id> hident(idx(args, 0)(), origin::user);
  file_id ident(decode_hexenc_as<file_id>(hident(), hident.made_from));

  E(db.file_version_exists(ident), origin::user,
    F("no file version %s found in database") % ident);

  file_size size;
  db.get_file_size(ident, size);
  output << lexical_cast<string>(size) << "\n";
}

// Name: get_file_of
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
             N_("Prints the contents of a file (given a name)"),
             "",
             options::opts::revision)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);

  revision_id rid;
  if (app.opts.revision.empty())
    {
      workspace work(app);

      parent_map parents;
      work.get_parent_rosters(db, parents);
      E(parents.size() == 1, origin::user,
        F("this command can only be used in a single-parent workspace"));
      rid = parent_id(parents.begin());
    }
  else
    {
      project_t project(db, app.lua, app.opts);
      complete(app.opts, app.lua, project, idx(app.opts.revision, 0)(), rid);
    }

  dump_file(db, output, rid, idx(args, 0));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
