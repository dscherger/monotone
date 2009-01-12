// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
// Copyright (C) 2009 Derek Scherger <derek@echologic.com>
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
#include "cert.hh"
#include "database.hh"
#include "project.hh"
#include "parallel_iter.hh"
#include "rcs_import.hh"
#include "revision.hh"
#include "roster.hh"
#include "simplestring_xform.hh"
#include "keys.hh"
#include "key_store.hh"

#include <time.h>
#include <iostream>
#include <sstream>

using std::cerr;
using std::cout;
using std::map;
using std::ostringstream;
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
    test_parse_rcs_file(system_path((*i)()));
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

  N(app.opts.branchname() != "",
    F("need base --branch argument for importing"));

  system_path cvsroot(idx(args, 0)());
  require_path_is_directory(cvsroot,
                            F("path %s does not exist") % cvsroot,
                            F("'%s' is not a directory") % cvsroot);

  // make sure we can sign certs using the selected key; also requests
  // the password (if necessary) up front rather than after some arbitrary
  // amount of work
  cache_user_key(app.opts, app.lua, db, keys);

  import_cvs_repo(project, keys, cvsroot, app.opts.branchname);
}

namespace
{
  string quote_path(file_path const & path)
  {
    string raw = path.as_internal();
    string quoted;
    quoted.reserve(raw.size() + 8);
    
    quoted += "\"";
    
    for (string::const_iterator i = raw.begin(); i != raw.end(); ++i)
      {
        if (*i == '"')
          quoted += "\\";
        quoted += *i;
      }
    
    quoted += "\"";

    return quoted;
  }

  // FIXME: perhaps add lua hooks for fixing branch names, author strings, etc.

  struct file_delete
  {
    file_path path;
    file_delete(file_path path) : 
      path(path) {}
  };

  struct file_rename
  {
    file_path old_path;
    file_path new_path;
    file_rename(file_path old_path, file_path new_path) : 
      old_path(old_path), new_path(new_path) {}
  };

  struct file_add
  {
    file_path path;
    file_id content;
    string mode;
    file_add(file_path path, file_id content, string mode) :
      path(path), content(content), mode(mode) {}
  };

  attr_key exe_attr("mtn:execute");

  void
  get_changes(roster_t const & left, roster_t const & right, 
              vector<file_delete> & deletions,
              vector<file_rename> & renames,
              vector<file_add> & additions)
  {

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
                deletions.push_back(file_delete(path));
              }
            break;

          case parallel::in_right:
            // added
            if (is_file_t(i.right_data()))
              {
                file_t file = downcast_to_file_t(i.right_data());

                full_attr_map_t::const_iterator 
                  exe = file->attrs.find(exe_attr);

                string mode = "100644";
                if (exe != file->attrs.end() && 
                    exe->second.first && // live attr
                    exe->second.second() == "true")
                  mode = "100755";

                file_path path;
                right.get_name(i.right_key(), path);
                additions.push_back(file_add(path, file->content, mode));
              }
            break;

          case parallel::in_both:
            // moved/renamed/patched/attribute changes
            if (is_file_t(i.left_data()))
              {
                file_t left_file = downcast_to_file_t(i.left_data());
                file_t right_file = downcast_to_file_t(i.right_data());

                full_attr_map_t::const_iterator 
                  left_attr = left_file->attrs.find(exe_attr);
                full_attr_map_t::const_iterator 
                  right_attr = right_file->attrs.find(exe_attr);

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
                  renames.push_back(file_rename(left_path, right_path));

                // git handles content changes as additions
                if (left_file->content != right_file->content || 
                    left_mode != right_mode)
                  additions.push_back(file_add(right_path, 
                                               right_file->content,
                                               right_mode));
              }
            break;
          }
      }
  }

};

CMD(git_export, "git_export", "", CMD_REF(vcs), N_(""),
    N_("Produces a git fast-export data stream on stdout"),
    N_(""),
    options::opts::none)
{
  database db(app);

  if (args.size() != 0)
    throw usage(execid);

  set<revision_id> revision_set;
  db.get_revision_ids(revision_set);

  vector<revision_id> revisions;
  toposort(db, revision_set, revisions);

  size_t revnum = 0;
  size_t revmax = revisions.size();

  map<revision_id, size_t> marked_revs;
  map<file_id, size_t> marked_files;

  size_t mark_id = 1;

  // this is done to ensure mktime below produces UTC times
  // according to timegm(3) this is the portable way to do it
  setenv("TZ", "UTC", 1);
  tzset();

  for (vector<revision_id>::const_iterator r = revisions.begin(); r != revisions.end(); ++r)
    {
      revnum++;

      typedef vector< revision<cert> > cert_vector;
      typedef cert_vector::const_iterator cert_iterator;

      cert_vector authors;
      cert_vector branches;
      cert_vector changelogs;
      cert_vector comments;
      cert_vector dates;
      cert_vector tags;

      db.get_revision_certs(*r, author_cert_name, authors);
      db.get_revision_certs(*r, branch_cert_name, branches);
      db.get_revision_certs(*r, changelog_cert_name, changelogs);
      db.get_revision_certs(*r, comment_cert_name, comments);
      db.get_revision_certs(*r, date_cert_name, dates);
      db.get_revision_certs(*r, tag_cert_name, tags);

      string author_name = "unknown";
      string author_email = "<unknown>";
      time_t author_date = 0;

      cert_iterator author = authors.begin();

      if (author != authors.end())
        author_name = author->inner().value();

      size_t lt = author_name.find('<');
      size_t gt = author_name.find('>');
      size_t at = author_name.find('@');

      // FIXME: parsing of author/email could be better

      if (lt != string::npos && gt != string::npos && lt < gt)
        {
          author_email = author_name.substr(lt, gt-lt+1);
          author_name = trim_ws(author_name.substr(0, lt)) + " ";
          // FIXME: ensure remainder of cert value after > is empty
        }
      else if (lt == string::npos && gt == string::npos && at != string::npos)
        {
          author_email = "<" + trim_ws(author_name) + ">";
          author_name = "";
        }

      cert_iterator date = dates.begin();

      if (date != dates.end())
        {
          // FIXME: do something better here (would nvm.dates help?)
          struct tm tm;
          string datestr = date->inner().value();
          strptime(datestr.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
          author_date = mktime(&tm);
        }

      // default to master branch if no branch certs exist
      string branchname = "master";

      if (!branches.empty())
        branchname = branches.begin()->inner().value();

      ostringstream message;
      set<string> messages;

      // process comment certs with changelog certs

      changelogs.insert(changelogs.end(), 
                        comments.begin(), comments.end());

      for (cert_iterator changelog = changelogs.begin(); 
           changelog != changelogs.end(); ++changelog)
        {
          string value = changelog->inner().value();
          if (messages.find(value) == messages.end())
            {
              messages.insert(value);
              message << value;
              if (value[value.size()-1] != '\n')
                message << "\n";
            }
        }

      revision_t revision;
      db.get_revision(*r, revision);

      edge_map::const_iterator edge = revision.edges.begin();

      revision_id parent1, parent2;

      if (revision.edges.size() == 1)
        {
          parent1 = edge_old_revision(edge);
        }
      else if (revision.edges.size() == 2)
        {
          parent1 = edge_old_revision(edge);
          ++edge;
          parent2 = edge_old_revision(edge);
        }
      else
        I(false);

      // we apparently only need/want the changes from the first parent.
      // including the changes from the second parent seems to cause failures
      // due to repeated renames. verification of git merge nodes against the
      // monotone source seems to show that they are correct.  presumably this
      // is somehow because of the 'from' and 'merge' lines in exported commits
      // below.

      roster_t old_roster, new_roster;
      db.get_roster(parent1, old_roster);
      db.get_roster(*r, new_roster);

      vector<file_delete> deletions;
      vector<file_rename> renames;
      vector<file_add> additions;

      typedef vector<file_delete>::const_iterator delete_iterator;
      typedef vector<file_rename>::const_iterator rename_iterator;
      typedef vector<file_add>::const_iterator add_iterator;

      get_changes(old_roster, new_roster, deletions, renames, additions);

      // emit file data blobs for modified and added files

      for (add_iterator i = additions.begin(); i != additions.end(); ++i)
        {
          if (marked_files.find(i->content) == marked_files.end())
            {
              // only mark and emit a blob the first time it is encountered
              file_data data;
              db.get_file_version(i->content, data);
              marked_files[i->content] = mark_id++;
              cout << "blob\n"
                   << "mark :" << marked_files[i->content] << "\n"
                   << "data " << data.inner()().size() << "\n" 
                   << data.inner()() << "\n";
            }
        }

      // FIXME: optionally include these in the commit message

      message << "\n";

      if (!null_id(parent1))
        message << "Monotone-Parent: " << parent1 << "\n";

      if (!null_id(parent2))
        message << "Monotone-Parent: " << parent2 << "\n";

      message << "Monotone-Revision: " << *r << "\n";

      for ( ; author != authors.end(); ++author)
        message << "Monotone-Author: " << author->inner().value() << "\n";

      for ( ; date != dates.end(); ++date)
        message << "Monotone-Date: " << date->inner().value() << "\n";

      for (cert_iterator branch = branches.begin() ; branch != branches.end(); ++branch)
        message << "Monotone-Branch: " << branch->inner().value() << "\n";

      for (cert_iterator tag = tags.begin(); tag != tags.end(); ++tag)
        message << "Monotone-Tag: " << tag->inner().value() << "\n";

      string data = message.str();

      marked_revs[*r] = mark_id++;

      cout << "commit refs/heads/" << branchname << "\n"
           << "mark :" << marked_revs[*r] << "\n"
           << "committer " << author_name << author_email << " " << author_date << " +0000\n"
           << "data " << data.size() << "\n" << data << "\n";

      if (!null_id(parent1))
        cout << "from :" << marked_revs[parent1] << "\n";
      
      if (!null_id(parent2))
        cout << "merge :" << marked_revs[parent2] << "\n";

      for (delete_iterator i = deletions.begin(); i != deletions.end(); ++i)
        cout << "D " << quote_path(i->path) << "\n";

      // FIXME: handle rename ordering issues
      for (rename_iterator i = renames.begin(); i != renames.end(); ++i)
        cout << "R " 
             << quote_path(i->old_path) << " " 
             << quote_path(i->new_path) << "\n";

      for (add_iterator i = additions.begin(); i != additions.end(); ++i)
        cout << "M " << i->mode << " :" 
             << marked_files[i->content] << " " 
             << quote_path(i->path) << "\n";
      
      // create additional branch refs
      if (!branches.empty())
        {
          cert_iterator branch = branches.begin();
          branch++;
          for ( ; branch != branches.end(); ++branch)
            cout << "reset refs/heads/" << branch->inner().value() << "\n"
                 << "from :" << marked_revs[*r] << "\n";
        }

      // FIXME: posibly create refs/mtn/revid

      // create tag refs
      for (cert_iterator tag = tags.begin(); tag != tags.end(); ++tag)
        cout << "reset refs/tags/" << tag->inner().value() << "\n"
             << "from :" << marked_revs[*r] << "\n";

      // report progress to stderr
      cerr << "progress revision " << *r 
           << " (" << revnum << "/" << revmax << ")\n";

      // report progress to the export file which will be reported during import
      cout << "progress revision " << *r 
           << " (" << revnum << "/" << revmax << ")\n"
           << "#############################################################\n";

      // since this creates a fast-import data stream one option for users
      // that encounter problems, is to save the data to a text file, split
      // it into smaller files as required -- see split(1), and edit the
      // offending commands in the file. this technique could also be used
      // to fix up various author names, etc. once the basic information has
      // been exported from monotone.

    }

  // FIXME: add an option to write out the revision marks to a file
  // then use of git fast-import --export-marks will give a corresponding file
  // of git revision ids
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
