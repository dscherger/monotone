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
#include "git_export.hh"
#include "outdated_indicator.hh"
#include "parallel_iter.hh"
#include "revision.hh"
#include "roster.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "ui.hh"

#include <iostream>
#include <sstream>
#include <stack>

using std::cout;
using std::istringstream;
using std::make_pair;
using std::map;
using std::ostringstream;
using std::set;
using std::stack;
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

  typedef vector<file_delete>::const_iterator delete_iterator;
  typedef vector<file_rename>::const_iterator rename_iterator;
  typedef vector<file_add>::const_iterator add_iterator;

  attr_key exe_attr("mtn:execute");

  void
  get_changes(roster_t const & left, roster_t const & right, 
              file_changes & changes)
  {

    typedef full_attr_map_t::const_iterator attr_iterator;

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
                changes.deletions.push_back(file_delete(path));
              }
            break;

          case parallel::in_right:
            // added
            if (is_file_t(i.right_data()))
              {
                file_t file = downcast_to_file_t(i.right_data());

                attr_iterator exe = file->attrs.find(exe_attr);

                string mode = "100644";
                if (exe != file->attrs.end() && 
                    exe->second.first && // live attr
                    exe->second.second() == "true")
                  mode = "100755";

                file_path path;
                right.get_name(i.right_key(), path);
                changes.additions.push_back(file_add(path, 
                                                     file->content, 
                                                     mode));
              }
            break;

          case parallel::in_both:
            // moved/renamed/patched/attribute changes
            if (is_file_t(i.left_data()))
              {
                file_t left_file = downcast_to_file_t(i.left_data());
                file_t right_file = downcast_to_file_t(i.right_data());

                attr_iterator left_attr = left_file->attrs.find(exe_attr);
                attr_iterator right_attr = right_file->attrs.find(exe_attr);

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
                  changes.renames.push_back(file_rename(left_path, 
                                                        right_path));

                // git handles content changes as additions
                if (left_file->content != right_file->content || 
                    left_mode != right_mode)
                  changes.additions.push_back(file_add(right_path, 
                                                       right_file->content,
                                                       right_mode));
              }
            break;
          }
      }
  }

  // re-order renames so that they occur in the correct order
  // i.e. rename a->b + rename b->c will be re-ordered as
  //      rename b->c + rename a->b
  // this will also insert temporary names to resolve circular 
  // renames and name swaps:
  // i.e. rename a->b + rename b->a will be re-ordered as
  //      rename a->tmp + rename b->a + rename tmp->b
  void
  reorder_renames(vector<file_rename> const & renames, 
                  vector<file_rename> & reordered_renames)
  {
    typedef map<file_path, file_path> map_type;

    map_type rename_map;

    for (rename_iterator i = renames.begin(); i != renames.end(); ++i)
      rename_map.insert(make_pair(i->old_path, i->new_path));

    while (!rename_map.empty())
      {
        map_type::iterator i = rename_map.begin();
        I(i != rename_map.end());
        file_rename base(i->first, i->second);
        rename_map.erase(i);

        map_type::iterator next = rename_map.find(base.new_path);
        stack<file_rename> rename_stack;

        // stack renames so their order can be reversed
        while (next != rename_map.end())
          {
            file_rename rename(next->first, next->second);
            rename_stack.push(rename);
            rename_map.erase(next);
            next = rename_map.find(rename.new_path);
          }

        // break rename loops
        if (!rename_stack.empty())
          {
            file_rename const & top = rename_stack.top();
            // if there is a loop push another rename onto the stack that
            // renames the old base to a temporary and adjust the base
            // rename to account for this
            if (base.old_path == top.new_path)
              {
                // the temporary path introduced here is pretty weak in
                // terms of random filenames but should suffice for the
                // already rare situations where any of this is required.
                string path = top.new_path.as_internal();
                path += ".tmp.break-rename-loop";
                file_path tmp = file_path_internal(path);
                rename_stack.push(file_rename(base.old_path, tmp));
                base.old_path = tmp;
              }
          }

        // insert the stacked renames in reverse order
        while (!rename_stack.empty())
          {
            file_rename rename = rename_stack.top();
            rename_stack.pop();
            reordered_renames.push_back(rename);
          }
        
        reordered_renames.push_back(base);
      }
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
             map<revision_id, file_changes> & change_map)
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

      file_changes changes;
      get_changes(old_roster, new_roster, changes);
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
               map<revision_id, file_changes> const & change_map,
               bool log_revids, bool log_certs)
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

      typedef vector< revision<cert> > cert_vector;
      typedef cert_vector::const_iterator cert_iterator;
      typedef map<string, string>::const_iterator lookup_iterator;

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

      // default to <unknown> committer and author if no author certs exist
      // this may be mapped to a different value with the authors-file option
      string author_name = "<unknown>"; // used as the git author
      string author_key  = "<unknown>"; // used as the git committer
      date_t author_date = date_t::now();

      cert_iterator author = authors.begin();

      if (author != authors.end())
        {
          author_name = trim(author->inner().value());
          author_key  = trim(author->inner().key());
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
        author_date = date_t(date->inner().value());

      // default to unknown branch if no branch certs exist
      // this may be mapped to a different value with the branches-file option
      string branch_name = "unknown";

      if (!branches.empty())
        branch_name = branches.begin()->inner().value();

      branch_name = trim(branch_name);

      lookup_iterator branch_lookup = branch_map.find(branch_name);

      if (branch_lookup != branch_map.end())
        branch_name = branch_lookup->second;

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

      map<revision_id, file_changes>::const_iterator f = change_map.find(*r);
      I(f != change_map.end());
      file_changes const & changes = f->second;

      vector<file_rename> reordered_renames;
      reorder_renames(changes.renames, reordered_renames);

      // emit file data blobs for modified and added files

      for (add_iterator 
             i = changes.additions.begin(); i != changes.additions.end(); ++i)
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
            message << "Monotone-Author: " << author->inner().value() << "\n";

          for ( ; date != dates.end(); ++date)
            message << "Monotone-Date: " << date->inner().value() << "\n";

          for (cert_iterator 
                 branch = branches.begin() ; branch != branches.end(); ++branch)
            message << "Monotone-Branch: " << branch->inner().value() << "\n";

          for (cert_iterator tag = tags.begin(); tag != tags.end(); ++tag)
            message << "Monotone-Tag: " << tag->inner().value() << "\n";
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
             i = changes.deletions.begin(); i != changes.deletions.end(); ++i)
        cout << "D " << quote_path(i->path) << "\n";

      for (rename_iterator
             i = reordered_renames.begin(); i != reordered_renames.end(); ++i)
        cout << "R " 
             << quote_path(i->old_path) << " " 
             << quote_path(i->new_path) << "\n";

      for (add_iterator
             i = changes.additions.begin(); i != changes.additions.end(); ++i)
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
              branch_name = trim(branch->inner().value());

              lookup_iterator branch_lookup = branch_map.find(branch_name);

              if (branch_lookup != branch_map.end())
                branch_name = branch_lookup->second;

              cout << "reset refs/heads/" << branch_name << "\n"
                   << "from :" << marked_revs[*r] << "\n";
            }
        }

      // create tag refs
      for (cert_iterator tag = tags.begin(); tag != tags.end(); ++tag)
        cout << "reset refs/tags/" << tag->inner().value() << "\n"
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

#ifdef BUILD_UNIT_TESTS

#include "unit_tests.hh"

UNIT_TEST(git_rename_reordering, reorder_chained_renames)
{
  vector<file_rename> renames, reordered_renames;
  renames.push_back(file_rename(file_path_internal("a"), file_path_internal("b")));
  renames.push_back(file_rename(file_path_internal("b"), file_path_internal("c")));
  renames.push_back(file_rename(file_path_internal("c"), file_path_internal("d")));

  // these should be reordered from a->b b->c c->d to c->d b->c a->b
  reorder_renames(renames, reordered_renames);
  rename_iterator rename = reordered_renames.begin();
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("c"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("d"));
  ++rename;
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("b"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("c"));
  ++rename;
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("a"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("b"));
  ++rename;
  UNIT_TEST_CHECK(rename == reordered_renames.end());
}

UNIT_TEST(git_rename_reordering, reorder_swapped_renames)
{
  vector<file_rename> renames, reordered_renames;
  renames.push_back(file_rename(file_path_internal("a"), file_path_internal("b")));
  renames.push_back(file_rename(file_path_internal("b"), file_path_internal("a")));

  // these should be reordered from a->b b->a to a->tmp b->a tmp->b
  reorder_renames(renames, reordered_renames);
  rename_iterator rename = reordered_renames.begin();
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("a"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("a.tmp.break-rename-loop"));
  ++rename;
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("b"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("a"));
  ++rename;
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("a.tmp.break-rename-loop"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("b"));
  ++rename;
  UNIT_TEST_CHECK(rename == reordered_renames.end());
}

UNIT_TEST(git_rename_reordering, reorder_rename_loop)
{
  vector<file_rename> renames, reordered_renames;
  renames.push_back(file_rename(file_path_internal("a"), file_path_internal("b")));
  renames.push_back(file_rename(file_path_internal("b"), file_path_internal("c")));
  renames.push_back(file_rename(file_path_internal("c"), file_path_internal("a")));

  // these should be reordered from a->b b->c c->a to a->tmp c->a b->c a->b tmp->b
  reorder_renames(renames, reordered_renames);
  rename_iterator rename = reordered_renames.begin();
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("a"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("a.tmp.break-rename-loop"));
  ++rename;
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("c"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("a"));
  ++rename;
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("b"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("c"));
  ++rename;
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("a.tmp.break-rename-loop"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("b"));
  ++rename;
  UNIT_TEST_CHECK(rename == reordered_renames.end());
}

UNIT_TEST(git_rename_reordering, reorder_reversed_rename_loop)
{
  vector<file_rename> renames, reordered_renames;
  renames.push_back(file_rename(file_path_internal("z"), file_path_internal("y")));
  renames.push_back(file_rename(file_path_internal("y"), file_path_internal("x")));
  renames.push_back(file_rename(file_path_internal("x"), file_path_internal("z")));

  // assuming that the x->z rename gets pulled from the rename map first
  // these should be reordered from z->y y->x x->z to x->tmp y->x z->y tmp->z
  reorder_renames(renames, reordered_renames);
  rename_iterator rename = reordered_renames.begin();
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("x"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("x.tmp.break-rename-loop"));
  ++rename;
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("y"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("x"));
  ++rename;
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("z"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("y"));
  ++rename;
  UNIT_TEST_CHECK(rename->old_path == file_path_internal("x.tmp.break-rename-loop"));
  UNIT_TEST_CHECK(rename->new_path == file_path_internal("z"));
  ++rename;
  UNIT_TEST_CHECK(rename == reordered_renames.end());
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
