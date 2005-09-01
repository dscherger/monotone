// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// vim:sw=2:
// Copyright (C) 2005  Petr Baudis <pasky@suse.cz>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// Sponsored by Google's Summer of Code and SuSE

// This whole thing needs massive cleanup and codesharing with git_import.cc.

#include <algorithm>
#include <iostream>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>
#include <queue>
#include <stdlib.h>

#ifndef WIN32

#include <unistd.h>
#include <sys/stat.h> // mkdir()

#include <stdio.h>
#include <string.h> // strdup(), woo-hoo!

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "botan/botan.h"

#include "app_state.hh"
#include "cert.hh"
#include "constants.hh"
#include "database.hh"
#include "file_io.hh"
#include "git.hh"
#include "git_export.hh"
#include "keys.hh"
#include "manifest.hh"
#include "mkstemp.hh"
#include "packet.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"

using namespace std;
using boost::shared_ptr;
using boost::scoped_ptr;

struct
git_tree_entry
{
  bool execute;
  git_object_id blob_id;
  file_path path;
};

struct git_history;

// staging area for exporting
struct
git_staging
{
  system_path path;
  git_history *git;

  git_staging(git_history *g);
  ~git_staging();

  git_object_id blob_save(data const &blob);
  git_object_id tree_save(set<shared_ptr<git_tree_entry> > const &entries);
  git_object_id commit_save(git_object_id const &tree,
                            set<git_object_id> const &parents,
			    git_person const &author,
			    boost::posix_time::ptime const &atime,
			    git_person const &committer,
			    boost::posix_time::ptime const &ctime,
			    data const &logmsg);

private:
  system_path index_file;
};


// This is pretty much the _reverse_ of git_import.cc's git_history!
struct
git_history
{
  git_staging staging;

  map<revision_id, git_object_id> commitmap;
  map<manifest_id, git_object_id> treemap;
  map<file_id, git_object_id> filemap;

  ticker n_revs;
  ticker n_objs;

  string branch;

  git_history();
};


/*** The raw GIT interface */

// XXX: Code duplication with git_db::load_revs().
static void
get_gitrev_ancestry(git_object_id revision, set<git_object_id> &ancestry)
{
  filebuf fb;
  capture_git_cmd_output(F("git-rev-list %s") % revision(), fb);
  istream stream(&fb);

  stack<git_object_id> st;
  while (!stream.eof())
    {
      char revbuf[41];
      stream.getline(revbuf, 41);
      if (strlen(revbuf) < 40)
        continue;
      L(F("noted revision %s") % revbuf);
      ancestry.insert(git_object_id(string(revbuf)));
    }
  L(F("Loaded all revisions"));
}


git_staging::git_staging(git_history *g)
  : git(g)
{
  // Make a temporary staging directory:
  char *tmpdir = getenv("TMPDIR");
  if (!tmpdir)
    tmpdir = "/tmp";
  string tmpfile(tmpdir);
  tmpfile += "/mtexport.XXXXXX";
  int fd = monotone_mkstemp(tmpfile);

  // Hope for the racy best.
  close(fd);
  delete_file(system_path(tmpfile));

  N(mkdir(tmpfile.c_str(), 0700) == 0, F("mkdir(%s) failed") % tmpfile);


  path = system_path(tmpfile);
  index_file = path / "index";
}

git_staging::~git_staging()
{
  rmdir(path.as_external().c_str());
}

git_object_id
git_staging::blob_save(data const &blob)
{
  ++git->n_objs;

  string strpath = path.as_external();
  string blobpath = (path / "blob").as_external();
  {
    ofstream file(blobpath.c_str(), ios_base::out | ios_base::trunc | ios_base::binary);
    N(file, F("cannot open file %s for writing") % blobpath);
    Botan::Pipe pipe(new Botan::DataSink_Stream(file));
    pipe.process_msg(blob());
  }

  set_git_env("GIT_INDEX_FILE", index_file.as_external());

  string cmdline("cd '" + strpath + "' && git-update-cache --add blob");
  L(F("Invoking: %s") % cmdline);
  N(system(cmdline.c_str()) == 0, F("Adding '%s' failed") % blobpath);

  filebuf fb;
  capture_git_cmd_output(F("cd '%s' && git-ls-files --stage") % strpath, fb);
  istream stream(&fb);
  string line;
  stream_grabline(stream, line);
  N(line.length() >= 40, F("Invalid generated index, containing: '%s'") % line);
  git_object_id gitoid(line.substr(line.find(" ") + 1, 40));

  delete_file(index_file);
  delete_file(path / "blob");

  return gitoid;
}

git_object_id
git_staging::tree_save(set<shared_ptr<git_tree_entry> > const &entries)
{
  ++git->n_objs;

  set_git_env("GIT_INDEX_FILE", index_file.as_external());

  string cmdline("git-update-cache --add ");
  for (set<shared_ptr<git_tree_entry> >::const_iterator i = entries.begin();
       i != entries.end(); ++i)
    {
      cmdline += "--cacheinfo ";
      cmdline += (*i)->execute ? "777" : "666";
      cmdline += " " + (*i)->blob_id();
      // FIXME: Quote!
      cmdline += " '" + (*i)->path.as_external() + "' ";
    }
  L(F("Invoking: %s") % cmdline);
  N(system(cmdline.c_str()) == 0, F("Writing tree index failed"));

  filebuf fb;
  capture_git_cmd_output(F("git-write-tree"), fb);
  istream stream(&fb);
  string line;
  stream_grabline(stream, line);
  N(line.length() == 40, F("Invalid git-write-tree output: %s") % line);
  git_object_id gittid(line);

  delete_file(index_file);

  return gittid;
}

git_object_id
git_staging::commit_save(git_object_id const &tree,
                         set<git_object_id> const &parents,
                         git_person const &author,
			 boost::posix_time::ptime const &atime,
			 git_person const &committer,
			 boost::posix_time::ptime const &ctime,
			 data const &logmsg)
{
  ++git->n_revs;
  ++git->n_objs;

  L(F("Author: %s/%s, Committer: %s/%s") % author.name % author.email % committer.name % committer.email);
  set_git_env("GIT_AUTHOR_NAME", author.name);
  set_git_env("GIT_AUTHOR_EMAIL", author.email);
  set_git_env("GIT_AUTHOR_DATE", to_iso_extended_string(atime));
  set_git_env("GIT_COMMITTER_NAME", committer.name);
  set_git_env("GIT_COMMITTER_EMAIL", committer.email);
  set_git_env("GIT_COMMITTER_DATE", to_iso_extended_string(ctime));
  L(F("Logmsg: %s") % logmsg());
  data mylogmsg = data(logmsg() + "\n");

  string cmdline("git-commit-tree " + tree() + " ");
  for (set<git_object_id>::const_iterator i = parents.begin();
       i != parents.end(); ++i)
    {
      cmdline += "-p " + (*i)() + " ";
    }
  filebuf fb;
  capture_git_cmd_io(F("%s") % cmdline, mylogmsg, fb);
  istream stream(&fb);
  string line;
  stream_grabline(stream, line);
  N(line.length() == 40, F("Invalid git-commit-tree output: %s") % line);
  git_object_id gitcid(line);
  return gitcid;
}


static git_object_id
export_git_blob(git_history &git, app_state &app, file_id fid)
{
  L(F("Exporting file '%s'") % fid.inner());

  map<file_id, git_object_id>::const_iterator i = git.filemap.find(fid);
  if (i != git.filemap.end())
    {
      return i->second;
    }

  file_data fdata;
  app.db.get_file_version(fid, fdata);
  git_object_id gitbid = git.staging.blob_save(fdata.inner());
  git.filemap.insert(make_pair(fid, gitbid));
  return gitbid;
}

static git_object_id
export_git_tree(git_history &git, app_state &app, manifest_id mid)
{
  L(F("Exporting tree '%s'") % mid.inner());

  map<manifest_id, git_object_id>::const_iterator i = git.treemap.find(mid);
  if (i != git.treemap.end())
    {
      return i->second;
    }

  manifest_map manifest;
  app.db.get_manifest(mid, manifest);

  attr_map attrs;
  read_attr_map_from_db(manifest, attrs, app);

  set<shared_ptr<git_tree_entry> > tree;
  for (manifest_map::const_iterator i = manifest.begin();
       i != manifest.end(); ++i)
    {
      L(F("Queuing '%s' [%s]") % manifest_entry_path(*i) % manifest_entry_id(*i));
      shared_ptr<git_tree_entry> entry(new git_tree_entry);
      entry->blob_id = export_git_blob(git, app, manifest_entry_id(*i));
      entry->path = manifest_entry_path(*i);

      string attrval;
      if (find_in_attr_map(attrs, entry->path, "execute", attrval))
	entry->execute = (attrval == "true");

      tree.insert(entry);
    }

  git_object_id gittid = git.staging.tree_save(tree);
  git.treemap.insert(make_pair(mid, gittid));
  return gittid;
}


static bool
has_cert(app_state &app, revision_id rid, cert_name name, string content)
{
  L(F("Has cert '%s' of value '%s'?") % name % content);

  vector< revision<cert> > certs;
  app.db.get_revision_certs(rid, name, certs);
  erase_bogus_certs(certs, app);
  for (vector< revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      if (content == tv())
	return true;
    }
  L(F("... nope"));
  return false;
}

static void
load_cert(app_state &app, revision_id rid, cert_name name, string &content)
{
  L(F("Loading cert '%s'") % name);

  vector< revision<cert> > certs;
  app.db.get_revision_certs(rid, name, certs);
  erase_bogus_certs(certs, app);
  if (certs.begin() == certs.end())
    return;

  cert_value tv;
  decode_base64(certs.begin()->inner().value, tv);
  content = tv();

  L(F("... '%s'") % content);
}

static void
historical_monorev_to_gitrev(git_history &git, app_state &app,
                             revision_id rid, git_object_id &gitrid)
{
  cert_name commitid_name(gitcommit_id_cert_name);
  string commitid;
  load_cert(app, rid, commitid_name, commitid);
  N(!commitid.empty(), F("Current commit's parent %s was not imported yet?!") % rid.inner());
  gitrid = git_object_id(commitid);
}

static bool
export_git_revision(git_history &git, app_state &app, revision_id rid, git_object_id &gitcid)
{
  L(F("Exporting commit '%s'") % rid.inner());

  cert_name branch_name(branch_cert_name);

  if (!has_cert(app, rid, branch_name, git.branch))
    {
      L(F("Skipping, not on my branch."));
      return false;
    }

  revision_set rev;
  app.db.get_revision(rid, rev);
  git_object_id gittid = export_git_tree(git, app, rev.new_manifest);

  set<git_object_id> parents;
  for (edge_map::const_iterator e = rev.edges.begin();
      e != rev.edges.end(); ++e)
    {
      if (null_id(edge_old_revision(e)))
	continue;

      L(F("Considering edge %s -> %s") % rid.inner() % edge_old_revision(e).inner());
      map<revision_id, git_object_id>::const_iterator i;
      i = git.commitmap.find(edge_old_revision(e));
      git_object_id parent_gitcid;
      if (i != git.commitmap.end())
        {
	  parent_gitcid = i->second;
	} else {
	  historical_monorev_to_gitrev(git, app, edge_old_revision(e), parent_gitcid);
	}
      parents.insert(parent_gitcid);
    }

  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);
  cert_name committer_name(gitcommit_committer_cert_name);
  cert_name changelog_name(changelog_cert_name);

  git_person author;
  load_cert(app, rid, author_name, author.email);

  boost::posix_time::ptime atime;
  string atimestr;
  load_cert(app, rid, date_name, atimestr);
  atime = boost::posix_time::from_iso_string(atimestr);

  git_person committer;
  string commitline;
  load_cert(app, rid, committer_name, commitline);
  committer.name = commitline.substr(0, commitline.find("<") - 1);
  commitline.erase(0, commitline.find("<") + 1);
  committer.email = commitline.substr(0, commitline.find(">"));
  commitline.erase(0, commitline.find(">") + 2);
  boost::posix_time::ptime ctime;
  ctime = boost::posix_time::from_iso_string(commitline.substr(0, commitline.find(" ")));

  string logmsg;
  load_cert(app, rid, changelog_name, logmsg);

  gitcid = git.staging.commit_save(gittid, parents,
                                   author, atime,
				   committer, ctime,
				   data(logmsg));
  git.commitmap.insert(make_pair(rid, gitcid));

  packet_db_writer dbw(app);
  put_simple_revision_cert(rid, gitcommit_id_cert_name, gitcid(), app, dbw);

  return true;
}


static void
add_gitrevs_descendants(git_history &git, app_state &app,
                        set<revision_id> &list, set<git_object_id> gitrevs)
{
  queue<revision_id> frontier;
  set<revision_id> seen;

  set<revision_id> heads;
  get_branch_heads(git.branch, app, heads);
  for (set<revision_id>::const_iterator i = heads.begin();
       i != heads.end(); ++i)
    frontier.push(*i);

  while (!frontier.empty())
    {
      revision_id rid = frontier.front(); frontier.pop();

      if (seen.find(rid) != seen.end())
        continue;
      seen.insert(rid);

      revision_set rev;
      app.db.get_revision(rid, rev);

      vector<revision<cert> > certs;
      app.db.get_revision_certs(rid, gitcommit_id_cert_name, certs);
      I(certs.size() < 2);
      if (certs.size() > 0)
        {
          // This is a GIT commit, then.
          cert_value cv;
          decode_base64(certs[0].inner().value, cv);
          git_object_id gitoid = cv();

	  if (gitrevs.find(cv()) != gitrevs.end())
	    continue;
        }
      list.insert(rid);

      for (edge_map::const_iterator e = rev.edges.begin();
           e != rev.edges.end(); ++e)
        {
          frontier.push(edge_old_revision(e));
        }
    }
}


git_history::git_history()
  : staging(this), n_revs("revisions", "r", 1), n_objs("objects", "o", 4)
{
}


void
export_git_repo(system_path const & gitrepo,
                string const &headname_,
                app_state & app)
{
  require_path_is_directory(gitrepo,
                            F("repo %s does not exist") % gitrepo,
                            F("repo %s is not a directory") % gitrepo);

  N(app.branch_name() != "", F("need base --branch argument for exporting"));

  set<revision_id> heads;
  get_branch_heads(app.branch_name(), app, heads);
  N(heads.size() == 1, F("the to-be-exported branch has to have exactly one head"));

  set_git_env("GIT_DIR", gitrepo.as_external());
  git_history git;
  git.branch = app.branch_name();

  string headname(headname_.empty() ? "master" : headname_);
  system_path headpath(gitrepo / "refs/heads" / headname);

  // Nothing shall disturb us!
  transaction_guard guard(app.db);
  app.db.ensure_open();

  set<revision_id> filter;
  toposort_filter filtertype = topo_all;
  if (file_exists(headpath))
    {
      ifstream file(headpath.as_external().c_str(), ios_base::in);
      N(file, F("cannot open file %s for reading") % headpath);
      string line;
      stream_grabline(file, line);
      I(line.length() == 40);
      git_object_id gitrev(line);
      set<git_object_id> ancestry;
      get_gitrev_ancestry(gitrev, ancestry);
      add_gitrevs_descendants(git, app, filter, ancestry);
      filtertype = topo_include;

      try
        {
	  revision_id rev;
          historical_gitrev_to_monorev(git.branch, NULL, app, gitrev, rev);
        }
      catch (std::exception &e)
        {
	  N(false, F("head %s is not subset of our tree; perhaps import first?") % headname);
	}
    }

  vector<revision_id> revlist; revlist.clear();
  // fill revlist with all the revisions, toposorted
  toposort(filter, revlist, app, filtertype);
  //reverse(revlist.begin(), revlist.end());

  for (vector<revision_id>::const_iterator i = revlist.begin();
       i != revlist.end(); ++i)
    {
      if (null_id(*i))
	continue;

      ui.set_tick_trailer((*i).inner()());
      git_object_id gitcid;
      if (!export_git_revision(git, app, *i, gitcid))
	continue;

      ofstream file(headpath.as_external().c_str(),
	            ios_base::out | ios_base::trunc);
      N(file, F("cannot open file %s for writing") % headpath);
      file << gitcid() << endl;
    }
  ui.set_tick_trailer("");

  guard.commit();

  return;
}

#else // WIN32

void
export_git_repo(system_path const & gitrepo,
                app_state & app)
{
  E("git export not supported on win32");
}

#endif
