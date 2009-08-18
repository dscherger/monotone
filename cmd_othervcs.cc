// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//               2009 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "cmd.hh"
#include "app_state.hh"
#include "database.hh"
#include "git_change.hh"
#include "git_export.hh"
#include "project.hh"
#include "rcs_import.hh"
#include "revision.hh"
#include "keys.hh"
#include "key_store.hh"

using std::map;
using std::set;
using std::string;
using std::vector;

CMD(rcs_import, "rcs_import", "", CMD_REF(debug), N_("RCSFILE..."),
    N_("Parses versions in RCS files"),
    N_("This command doesn't reconstruct or import revisions.  "
       "You probably want to use cvs_import."),
    options::opts::branch)
{
  if (args.size() < 1)
    throw usage(execid);

  for (args_vector::const_iterator i = args.begin();
       i != args.end(); ++i)
    test_parse_rcs_file(system_path((*i)(), origin::user));
}


CMD(cvs_import, "cvs_import", "", CMD_REF(vcs), N_("CVSROOT"),
    N_("Imports all versions in a CVS repository"),
    "",
    options::opts::branch)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 1)
    throw usage(execid);

  E(!app.opts.branch().empty(), origin::user,
    F("need base --branch argument for importing"));

  system_path cvsroot(idx(args, 0)(), origin::user);
  require_path_is_directory(cvsroot,
                            F("path %s does not exist") % cvsroot,
                            F("'%s' is not a directory") % cvsroot);

  // make sure we can sign certs using the selected key; also requests
  // the password (if necessary) up front rather than after some arbitrary
  // amount of work
  cache_user_key(app.opts, app.lua, db, keys, project);

  import_cvs_repo(project, keys, cvsroot, app.opts.branch);
}


CMD(git_export, "git_export", "", CMD_REF(vcs), (""),
    N_("Produces a git fast-export data stream on stdout"),
    (""),
    options::opts::authors_file | options::opts::branches_file |
    options::opts::log_revids | options::opts::log_certs | 
    options::opts::use_one_changelog |
    options::opts::import_marks | options::opts::export_marks |
    options::opts::refs)
{
  database db(app);

  if (args.size() != 0)
    throw usage(execid);

  map<string, string> author_map;
  map<string, string> branch_map;

  if (!app.opts.authors_file.empty())
    {
      P(F("reading author mappings from '%s'") % app.opts.authors_file);
      read_mappings(app.opts.authors_file, author_map);
    }

  if (!app.opts.branches_file.empty())
    {
      P(F("reading branch mappings from '%s'") % app.opts.branches_file);
      read_mappings(app.opts.branches_file, branch_map);
    }

  map<revision_id, size_t> marked_revs;

  if (!app.opts.import_marks.empty())
    {
      P(F("importing revision marks from '%s'") % app.opts.import_marks);
      import_marks(app.opts.import_marks, marked_revs);
    }

  set<revision_id> revision_set;
  db.get_revision_ids(revision_set);

  // remove marked revs from the set to be exported
  for (map<revision_id, size_t>::const_iterator
         i = marked_revs.begin(); i != marked_revs.end(); ++i)
    revision_set.erase(i->first);

  vector<revision_id> revisions;
  toposort(db, revision_set, revisions);

  map<revision_id, git_change> change_map;

  load_changes(db, revisions, change_map);

  // needs author and branch maps
  export_changes(db,
                 revisions, marked_revs,
                 author_map, branch_map, change_map,
                 app.opts.log_revids, app.opts.log_certs,
                 app.opts.use_one_changelog);

  if (app.opts.refs.find("revs") != app.opts.refs.end())
    export_rev_refs(revisions, marked_revs);

  if (app.opts.refs.find("roots") != app.opts.refs.end())
    export_root_refs(db, marked_revs);

  if (app.opts.refs.find("leaves") != app.opts.refs.end())
    export_leaf_refs(db, marked_revs);

  if (!app.opts.export_marks.empty())
    {
      P(F("exporting revision marks to '%s'") % app.opts.export_marks);
      export_marks(app.opts.export_marks, marked_revs);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
