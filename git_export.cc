// Copyright (C) 2009 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "cert.hh"
#include "database.hh"
#include "dates.hh"
#include "file_io.hh"
#include "git_change.hh"
#include "git_export.hh"
#include "outdated_indicator.hh"
#include "project.hh"
#include "revision.hh"
#include "roster.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "ui.hh"

#include <iostream>
#include <sstream>

using std::cout;
using std::istringstream;
using std::map;
using std::ostringstream;
using std::set;
using std::string;
using std::vector;

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
};

void
read_mappings(system_path const & path, map<string, string> & mappings)
{
  data names;
  vector<string> lines;

  read_data(path, names);
  split_into_lines(names(), lines);

  for (vector<string>::const_iterator i = lines.begin(); i != lines.end(); ++i)
    {
      string line = trim(*i);
      size_t index = line.find('=');
      if (index != string::npos || index < line.length()-1)
        {
          string key = trim(line.substr(0, index));
          string value = trim(line.substr(index+1));
          mappings[key] = value;
        }
      else if (!line.empty())
        W(F("ignored invalid mapping '%s'") % line);
    }
}

void
import_marks(system_path const & marks_file,
             map<revision_id, size_t> & marked_revs)
{
  size_t mark_id = 1;

  data mark_data;
  read_data(marks_file, mark_data);
  istringstream marks(mark_data());
  while (!marks.eof())
    {
      char c;
      size_t mark;
      string tmp;

      marks.get(c);
      E(c == ':', origin::user, F("missing leading ':' in marks file"));
      marks >> mark;

      marks.get(c);
      E(c == ' ', origin::user, F("missing space after mark"));
      marks >> tmp;
      E(tmp.size() == 40, origin::user, F("bad revision id in marks file"));
      revision_id revid(decode_hexenc(tmp, origin::user), origin::user);

      marks.get(c);
      E(c == '\n', origin::user, F("incomplete line in marks file"));

      marked_revs[revid] = mark;
      if (mark > mark_id) mark_id = mark+1;
      marks.peek();
    }
}


void
export_marks(system_path const & marks_file,
             map<revision_id, size_t> const & marked_revs)
{
  ostringstream marks;
  for (map<revision_id, size_t>::const_iterator
         i = marked_revs.begin(); i != marked_revs.end(); ++i)
    marks << ":" << i->second << " " << i->first << "\n";

  data mark_data(marks.str(), origin::internal);
  system_path tmp("."); // use the current directory for tmp
  write_data(marks_file, mark_data, tmp);
}

void
load_changes(database & db,
             vector<revision_id> const & revisions,
             map<revision_id, git_change> & change_map)
{
  // process revisions in reverse order and calculate the file changes for
  // each revision. these are cached in a map for use in the export phase
  // where revisions are processed in forward order. this trades off memory
  // for speed, loading rosters in reverse order is ~5x faster than loading
  // them in forward order and the memory required for file changes is
  // generally quite small. the memory required here should be comparable to
  // that for all of the revision texts in the database being exported.
  //
  // testing exports of a current monotone database with ~18MB of revision
  // text in ~15K revisions and a current piding database with ~20MB of
  // revision text in ~27K revisions indicate that this is a reasonable
  // approach. the export process reaches around 203MB VSS and 126MB RSS
  // for the monotone database and around 206MB VSS and 129MB RSS for the
  // pidgin database.

  ticker loaded(_("loading"), "r", 1);
  loaded.set_total(revisions.size());

  for (vector<revision_id>::const_reverse_iterator
         r = revisions.rbegin(); r != revisions.rend(); ++r)
    {
      revision_t revision;
      db.get_revision(*r, revision);

      // we apparently only need/want the changes from the first parent.
      // including the changes from the second parent seems to cause
      // failures due to repeated renames. verification of git merge nodes
      // against the monotone source seems to show that they are correct.
      // presumably this is somehow because of the 'from' and 'merge'
      // lines in exported commits below.

      revision_id parent1;
      edge_map::const_iterator edge = revision.edges.begin();
      parent1 = edge_old_revision(edge);

      roster_t old_roster, new_roster;
      db.get_roster(parent1, old_roster);
      db.get_roster(*r, new_roster);

      git_change changes;
      get_change(old_roster, new_roster, changes);
      change_map[*r] = changes;

      ++loaded;
    }
}

void
export_changes(database & db,
               vector<revision_id> const & revisions,
               map<revision_id, size_t> & marked_revs,
               map<string, string> const & author_map,
               map<string, string> const & branch_map,
               map<revision_id, git_change> const & change_map,
               bool log_revids, bool log_certs,
               bool use_one_changelog)
{
  size_t revnum = 0;
  size_t revmax = revisions.size();

  size_t mark_id = 0;
  for (map<revision_id, size_t>::const_iterator i = marked_revs.begin();
       i != marked_revs.end(); ++i)
    if (i->second > mark_id) mark_id = i->second;
  mark_id++;

  map<file_id, size_t> marked_files;

  // process the revisions in forward order and write out the fast-export
  // data stream.

  ticker exported(_("exporting"), "r", 1);
  exported.set_total(revisions.size());

  for (vector<revision_id>::const_iterator
         r = revisions.begin(); r != revisions.end(); ++r)
    {
      revnum++;

      typedef vector<cert> cert_vector;
      typedef cert_vector::const_iterator cert_iterator;
      typedef map<string, string>::const_iterator lookup_iterator;

      cert_vector certs;
      cert_vector authors;
      cert_vector branches;
      cert_vector changelogs;
      cert_vector comments;
      cert_vector dates;
      cert_vector tags;

      db.get_revision_certs(*r, certs);

      for (cert_iterator i = certs.begin(); i != certs.end(); ++i)
        {
          if (i->name == author_cert_name)
            authors.push_back(*i);
          else if (i->name == branch_cert_name)
            branches.push_back(*i);
          else if (i->name == changelog_cert_name)
            changelogs.push_back(*i);
          else if (i->name == date_cert_name)
            dates.push_back(*i);
          else if (i->name == tag_cert_name)
            tags.push_back(*i);
          else if (i->name == comment_cert_name)
            comments.push_back(*i);
        }

      // default to <unknown> committer and author if no author certs exist
      // this may be mapped to a different value with the authors-file option
      string author_name = "<unknown>"; // used as the git author
      string author_key  = "<unknown>"; // used as the git committer
      date_t author_date = date_t::now();

      cert_iterator author = authors.begin();

      if (author != authors.end())
        {
          author_name = trim(author->value());
          if (db.public_key_exists(author->key))
            {
              rsa_pub_key pub;
              key_name name;
              db.get_pubkey(author->key, name, pub);
              author_key = trim(name());
            }
        }

      // all monotone keys and authors that don't follow the "Name <email>"
      // convention used by git must be mapped or they may cause the import
      // to fail. the full list of these values is available from monotone
      // using the 'db execute' command. the following queries will list all
      // author keys and author cert values.
      //
      // 'select distinct keypair from revision_certs'
      // 'select distinct value from revision_certs where name = "author"'

      lookup_iterator key_lookup = author_map.find(author_key);

      if (key_lookup != author_map.end())
        author_key = key_lookup->second;
      else if (author_key.find('<') == string::npos &&
               author_key.find('>') == string::npos)
        author_key = "<" + author_key + ">";

      lookup_iterator name_lookup = author_map.find(author_name);

      if (name_lookup != author_map.end())
        author_name = name_lookup->second;
      else if (author_name.find('<') == string::npos &&
               author_name.find('>') == string::npos)
        author_name = "<" + author_name + ">";

      cert_iterator date = dates.begin();

      if (date != dates.end())
        author_date = date_t(date->value());

      // default to unknown branch if no branch certs exist
      // this may be mapped to a different value with the branches-file option
      string branch_name = "unknown";

      if (!branches.empty())
        branch_name = branches.begin()->value();

      branch_name = trim(branch_name);

      lookup_iterator branch_lookup = branch_map.find(branch_name);

      if (branch_lookup != branch_map.end())
        branch_name = branch_lookup->second;

      ostringstream message;
      set<string> messages;

      // process comment certs with changelog certs

      if (!use_one_changelog)
        changelogs.insert(changelogs.end(),
                          comments.begin(), comments.end());

      for (cert_iterator changelog = changelogs.begin();
           changelog != changelogs.end(); ++changelog)
        {
          string value = changelog->value();
          if (messages.find(value) == messages.end())
            {
              messages.insert(value);
              message << value;
              if (value[value.size()-1] != '\n')
                message << "\n";
              if (use_one_changelog)
                break;
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

      map<revision_id, git_change>::const_iterator f = change_map.find(*r);
      I(f != change_map.end());
      git_change const & change = f->second;

      vector<git_rename> reordered_renames;
      reorder_renames(change.renames, reordered_renames);

      // emit file data blobs for modified and added files

      for (add_iterator
             i = change.additions.begin(); i != change.additions.end(); ++i)
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

      if (log_revids)
        {
          message << "\n";

          if (!null_id(parent1))
            message << "Monotone-Parent: " << parent1 << "\n";

          if (!null_id(parent2))
            message << "Monotone-Parent: " << parent2 << "\n";

          message << "Monotone-Revision: " << *r << "\n";
        }

      if (log_certs)
        {
          message << "\n";
          for ( ; author != authors.end(); ++author)
            message << "Monotone-Author: " << author->value() << "\n";

          for ( ; date != dates.end(); ++date)
            message << "Monotone-Date: " << date->value() << "\n";

          for (cert_iterator
                 branch = branches.begin() ; branch != branches.end(); ++branch)
            message << "Monotone-Branch: " << branch->value() << "\n";

          for (cert_iterator tag = tags.begin(); tag != tags.end(); ++tag)
            message << "Monotone-Tag: " << tag->value() << "\n";
        }

      string data = message.str();

      marked_revs[*r] = mark_id++;

      cout << "commit refs/heads/" << branch_name << "\n"
           << "mark :" << marked_revs[*r] << "\n"
           << "author " << author_name << " "
           << (author_date.as_millisecs_since_unix_epoch() / 1000) << " +0000\n"
           << "committer " << author_key << " "
           << (author_date.as_millisecs_since_unix_epoch() / 1000) << " +0000\n"
           << "data " << data.size() << "\n" << data << "\n";

      if (!null_id(parent1))
        cout << "from :" << marked_revs[parent1] << "\n";

      if (!null_id(parent2))
        cout << "merge :" << marked_revs[parent2] << "\n";

      for (delete_iterator
             i = change.deletions.begin(); i != change.deletions.end(); ++i)
        cout << "D " << quote_path(*i) << "\n";

      for (rename_iterator
             i = reordered_renames.begin(); i != reordered_renames.end(); ++i)
        cout << "R "
             << quote_path(i->first) << " "
             << quote_path(i->second) << "\n";

      for (add_iterator
             i = change.additions.begin(); i != change.additions.end(); ++i)
        cout << "M " << i->mode << " :"
             << marked_files[i->content] << " "
             << quote_path(i->path) << "\n";

      // create additional branch refs
      if (!branches.empty())
        {
          cert_iterator branch = branches.begin();
          branch++;
          for ( ; branch != branches.end(); ++branch)
            {
              branch_name = trim(branch->value());

              lookup_iterator branch_lookup = branch_map.find(branch_name);

              if (branch_lookup != branch_map.end())
                branch_name = branch_lookup->second;

              cout << "reset refs/heads/" << branch_name << "\n"
                   << "from :" << marked_revs[*r] << "\n";
            }
        }

      // create tag refs
      for (cert_iterator tag = tags.begin(); tag != tags.end(); ++tag)
        cout << "reset refs/tags/" << tag->value() << "\n"
             << "from :" << marked_revs[*r] << "\n";

      // report progress to the export file which will be reported during import
      cout << "progress revision " << *r
           << " (" << revnum << "/" << revmax << ")\n"
           << "#############################################################\n";

      ++exported;
    }
}

void
export_rev_refs(vector<revision_id> const & revisions,
                map<revision_id, size_t> & marked_revs)
{
  for (vector<revision_id>::const_iterator
         i = revisions.begin(); i != revisions.end(); ++i)
    cout << "reset refs/mtn/revs/" << *i << "\n"
         << "from :" << marked_revs[*i] << "\n";
}

void
export_root_refs(database & db,
                map<revision_id, size_t> & marked_revs)
{
  set<revision_id> roots;
  revision_id nullid;
  db.get_revision_children(nullid, roots);
  for (set<revision_id>::const_iterator
         i = roots.begin(); i != roots.end(); ++i)
    cout << "reset refs/mtn/roots/" << *i << "\n"
         << "from :" << marked_revs[*i] << "\n";
}

void
export_leaf_refs(database & db,
                 map<revision_id, size_t> & marked_revs)
{
  set<revision_id> leaves;
  db.get_leaves(leaves);
  for (set<revision_id>::const_iterator
         i = leaves.begin(); i != leaves.end(); ++i)
    cout << "reset refs/mtn/leaves/" << *i << "\n"
         << "from :" << marked_revs[*i] << "\n";
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
