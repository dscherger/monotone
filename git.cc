// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// vim:sw=2:
// Copyright (C) 2005  Petr Baudis <pasky@ucw.cz>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// Based on cvs_import by graydon hoare <graydon@pobox.com>
// Sponsored by Google's Summer of Code and SuSE

// TODO:
// * Incremental import
// * Export, incremental export
// * Non-master head
// * Do not use external commands, but libgit directly (?)
// * Remote repositories

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

#include <stdio.h>
#include <string.h> // strdup(), woo-hoo!

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "app_state.hh"
#include "cert.hh"
#include "constants.hh"
#include "database.hh"
#include "file_io.hh"
#include "git.hh"
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

/* How do we import the history?
 *
 * The GIT history is a DAG, each commit contains list of zero or more parents.
 * At the start, we know the "head" commit ID, but in order to reconstruct
 * the history in monotone, we have to start from the root commit ID and
 * traverse to its children.
 *
 * The approach we take is to take the head, and get topologically sorted DAG
 * of its ancestry, with the roots at the top of the stack. Then, we take the
 * revisions and convert them one-by-one. To translate the parents properly,
 * we keep a git_id->monotone_id hashmap.
 *
 * The alternative approach would be to do the topological sort on our own and
 * while doing it also make a reversed connectivity graph, with each commit
 * associated with its children. That should be faster and you wouldn't need
 * the hash, but it wouldn't be as easy to code, so it's a TODO.
 */

typedef hexenc<id> git_object_id;

struct git_history;

struct
git_person
{
  string name, email;
};

struct
git_db
{
  const fs::path path;

  filebuf &get_object(const string type, const git_object_id objid);
  void get_object(const string type, const git_object_id objid, data &dat);

  // DAG of the revision ancestry in topological order
  // (top of the stack are the earliest revisions)
  // The @revision can be even head name.
  stack<git_object_id> load_revs(const string revision, const set<git_object_id> &exclude);

  git_db(const fs::path &path_) : path(path_) { }
};


struct
git_history
{
  git_db db;

  map<git_object_id, pair<revision_id, manifest_id> > commitmap;
  map<git_object_id, file_id> filemap;

  ticker n_revs;
  ticker n_objs;

  string base_branch;

  git_history(const fs::path &path);
};



/*** The raw GIT interface */
// This should change to libgit calls in the future


static filebuf &
capture_cmd_output(boost::format const & fmt)
{
  string str;
  try
    {
      str = fmt.str();
    }
  catch (std::exception & e)
    {
      ui.inform(string("fatal: capture_cmd_output() formatter failed:") + e.what());
      throw e;
    }

  char *tmpdir = getenv("TMPDIR");
  if (!tmpdir)
    tmpdir = "/tmp";
  string tmpfile(tmpdir);
  tmpfile += "/mtoutput.XXXXXX";
  int fd = monotone_mkstemp(tmpfile);

  L(F("Capturing cmd output: %s") % ("(" + str + ") >" + tmpfile));
  if (system(string("(" + str + ") >" + tmpfile).c_str()))
    throw oops("git command " + str + " failed");
  filebuf &fb = *new filebuf;
  fb.open(tmpfile.c_str(), ios::in);
  close(fd);
  fs::remove(tmpfile);
  return fb;
}


filebuf &
git_db::get_object(const string type, const git_object_id objid)
{
  filebuf &fb = capture_cmd_output(F("git-cat-file %s %s") % type % objid());
  return fb;
}

void
git_db::get_object(const string type, const git_object_id objid, data &dat)
{
  filebuf &fb = get_object(type, objid);
  istream stream(&fb);

  Botan::Pipe pipe;
  pipe.start_msg();
  stream >> pipe;
  pipe.end_msg();
  dat = pipe.read_all_as_string();
  delete &fb;
}


stack<git_object_id>
git_db::load_revs(const string revision, const set<git_object_id> &exclude)
{
  string excludestr;
  for (set<git_object_id>::const_iterator i = exclude.begin();
       i != exclude.end(); ++i)
    excludestr += " \"^" + (*i)() + "\"";

  filebuf &fb = capture_cmd_output(F("git-rev-list --topo-order %s %s")
                                   % revision % excludestr);
  istream stream(&fb);

  stack<git_object_id> st;
  while (!stream.eof())
    {
      char revbuf[41];
      stream.getline(revbuf, 41);
      if (strlen(revbuf) < 40)
	continue;
      L(F("noted revision %s") % revbuf);
      st.push(git_object_id(string(revbuf)));
    }
  L(F("Loaded all revisions"));
  delete &fb;
  return st;
}




/*** The GIT importer itself */

static file_id
import_git_blob(git_history &git, app_state &app, git_object_id gitbid)
{
  L(F("Importing blob '%s'") % gitbid());
  map<git_object_id, file_id>::const_iterator i = git.filemap.find(gitbid);
  if (i != git.filemap.end())
    {
      L(F("  -> map hit '%s'") % i->second);
      return i->second;
    }

  data dat;
  git.db.get_object("blob", gitbid, dat);
  file_id fid;
  calculate_ident(dat, fid);

  if (! app.db.file_version_exists(fid))
    {
      app.db.put_file(fid, dat);
    }
  git.filemap[gitbid()] = fid;
  ++git.n_objs;
  return fid;
}

static void
import_git_tree(git_history &git, app_state &app, git_object_id gittid,
                manifest_map &manifest, string prefix, attr_map &attrs)
{
  L(F("Importing tree '%s'") % gittid());

  data dat;
  git.db.get_object("tree", gittid, dat);

  unsigned pos = 0;
  while (pos < dat().length())
    {
      /* "mode name\0hash" */
      int infoend = dat().find('\0', pos);
      istringstream str(dat().substr(pos, infoend - pos));
      int mode;
      string name;
      str >> oct >> mode;
      str >> name;
      L(F("tree entry %o '%s' (%d)") % mode % name % (infoend - pos));
      pos = infoend + 1;

      string rawid = dat().substr(pos, 20);
      git_object_id gitoid(encode_hexenc(rawid));
      L(F("   [%s]") % gitoid());
      pos += 20;

      string fullname(prefix + name);

      if (mode & 040000) // directory
	import_git_tree(git, app, gitoid, manifest, fullname + '/', attrs);
      else
        {
	  if (mode & 0100) // executable
	    {
	      L(F("marking '%s' as executable") % fullname);
	      attrs[fullname]["execute"] = "true";
	    }

	  file_id fid = import_git_blob(git, app, gitoid);
	  L(F("entry monoid [%s]") % fid.inner());
	  manifest.insert(manifest_entry(file_path(fullname), fid));
	}
    }

  ++git.n_objs;
}


static string const gitcommit_id_cert_name = "gitcommit-id";
static string const gitcommit_author_cert_name = "gitcommit-author";

// TODO: Make git_heads_on_branch() and historical_gitrev_to_monorev() share
// code.

// Get the list of GIT heads in the database.
// Under some circumstances, it might insert some redundant items into the set
// (which doesn't matter for our current usage).
static void
git_heads_on_branch(git_history &git, app_state &app, set<git_object_id> &git_heads)
{
  queue<revision_id> frontier;
  set<revision_id> seen;

  // Take only heads in our branch - even if the commits are already in the db,
  // we want to import them again, just to add our branch membership to them.
  // (TODO)
  set<revision_id> heads;
  get_branch_heads(git.base_branch, app, heads);
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
	  git_object_id gitrid = cv();

	  git.commitmap[gitrid()] = make_pair(rid, rev.new_manifest);

	  git_heads.insert(gitrid);
	  continue; // stop traversing in this direction
	}

      for (edge_map::const_iterator e = rev.edges.begin();
	   e != rev.edges.end(); ++e)
	{
	  frontier.push(edge_old_revision(e));
	}
    }
}

// Look up given GIT commit id in present monotone history;
// this is used for incremental merging. Being smart, it also
// populates the commitmap with GIT commits it finds along the way.
static void
historical_gitrev_to_monorev(git_history &git, app_state &app,
                             git_object_id gitrid, revision_id &found_rid)
{
  queue<revision_id> frontier;
  set<revision_id> seen;

  // All the ancestry should be at least already in our branch, so there is
  // no need to work over the whole database.
  set<revision_id> heads;
  get_branch_heads(git.base_branch, app, heads);
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

	  git.commitmap[gitoid()] = make_pair(rid, rev.new_manifest);

	  if (gitoid == gitrid)
	    {
	      found_rid = rid;
	      return;
	    }
	}

      for (edge_map::const_iterator e = rev.edges.begin();
	   e != rev.edges.end(); ++e)
	{
	  frontier.push(edge_old_revision(e));
	}
    }

  throw oops("Wicked revision tree - incremental import wanted to\n"
             "import a GIT commit whose parent is not in the Monotone\n"
	     "database yet. This means a hole must have popped up\n"
	     "in the Monotone revision history.");
}

// extract_path_set() is silly and wipes its playground first
static void
extract_path_set_cont(manifest_map const & man, path_set & paths)
{
  for (manifest_map::const_iterator i = man.begin();
       i != man.end(); ++i)
    paths.insert(manifest_entry_path(i));
}

static file_id null_ident;

// complete_change_set() does not work for file additions/removals,
// so let's do it ourselves. We need nothing of this funky analysis
// stuff since we support no renames.
static void
full_change_set(manifest_map const & m_old,
                manifest_map const & m_new,
                change_set & cs)
{
  set<file_path> paths;
  extract_path_set_cont(m_old, paths);
  extract_path_set_cont(m_new, paths);

  for (set<file_path>::const_iterator i = paths.begin();
       i != paths.end(); ++i)
    {
      manifest_map::const_iterator j = m_old.find(*i);
      manifest_map::const_iterator k = m_new.find(*i);
      if (j == m_old.end())
	cs.add_file(*i, manifest_entry_id(k));
      else if (k == m_new.end())
	cs.delete_file(*i);
      else if (!(manifest_entry_id(j) == manifest_entry_id(k)))
        cs.deltas.insert(std::make_pair(*i, std::make_pair(manifest_entry_id(j),
                                                           manifest_entry_id(k))));
    }
}

static void
parse_person_line(string &line, git_person &person, time_t &time)
{
  int emailstart = line.find('<');
  int emailend = line.find('>', emailstart);
  int timeend = line.find(' ', emailend + 2);
  person.name = line.substr(0, emailstart - 1);
  person.email = line.substr(emailstart + 1, emailend - emailstart - 1);
  time = atol(line.substr(emailend + 2, timeend - emailend - 2).c_str());
  L(F("Person name: '%s', email: '%s', time: '%d'")
    % person.name % person.email % time);
}

static revision_id
import_git_commit(git_history &git, app_state &app, git_object_id gitrid)
{
  L(F("Importing commit '%s'") % gitrid());
  filebuf &fb = git.db.get_object("commit", gitrid);
  istream stream(&fb);

  bool header = true;
  revision_set rev;

  manifest_map manifest;
  // XXX: it might be user policy decision whether to take author or committer
  // as monotone author; the time should be always commit time, though
  git_person author;
  time_t author_time = 0;
  git_person committer;
  time_t commit_time = 0;
  string logmsg;

  // Read until eof - we have to peek() first since eof is set only after
  // a read _after_ the end of the file, so we would get one superfluous
  // iteration introducing trailing empty line (from failed getline()).
  while (!(stream.peek(), stream.eof()))
    {
      // XXX: Allow arbitrarily long lines
      char linebuf[256];
      stream.getline(linebuf, 256);
      string line = linebuf;

      if (header && line.size() == 0)
        {
	  header = false;
	  continue;
        }

      if (!header)
        {
	  L(F("LOG: %s") % line);
	  logmsg += line + '\n';
	  continue;
	}

      // HEADER
      // The order is always: tree, parent, author, committer
      // Parent may be present zero times or more, all the other items
      // are always present exactly one time.

      string keyword = line.substr(0, line.find(' '));
      string param = line.substr(line.find(' ') + 1);

      L(F("HDR: '%s' => '%s'") % keyword % param);
      if (keyword == "tree")
	{
	  attr_map attrs;
	  import_git_tree(git, app, param, manifest, "", attrs);

	  // Write the attribute map
	  {
	    data attr_data;
	    write_attr_map(attr_data, attrs);

	    file_id fid;
	    calculate_ident(attr_data, fid);
	    if (! app.db.file_version_exists(fid))
	      app.db.put_file(fid, attr_data);

	    file_path attr_path;
	    get_attr_path(attr_path);
	    manifest.insert(manifest_entry(attr_path, fid));
	  }

	  calculate_ident(manifest, rev.new_manifest);
	  if (! app.db.manifest_version_exists(rev.new_manifest))
	    {
	      manifest_data manidata;
	      write_manifest_map(manifest, manidata);
	      // TODO: put_manifest_with_delta()
	      app.db.put_manifest(rev.new_manifest, manidata);
	    }

	  L(F("[%s] Manifest ID: '%s'") % gitrid() % rev.new_manifest.inner());
	}
      else if (keyword == "parent")
	{
	  // FIXME: So far, in all the known GIT histories there was only a
	  // single "octopus" (>2 parents) merge. So this should be fixed
	  // to something a bit more history-friendly but it's not worth
	  // making a huge fuzz about it.
	  if (rev.edges.size() >= 2)
	    continue;

	  revision_id parent_rev;
	  manifest_id parent_mid;

	  // given the topo order, we ought to have the parent hashed - except
	  // for incremental pulls
	  map<git_object_id, pair<revision_id, manifest_id>
	      >::const_iterator i = git.commitmap.find(param);
	  if (i != git.commitmap.end())
	    {
	      parent_rev = i->second.first;
	      parent_mid = i->second.second;
	    }
	  else
	    {
	      historical_gitrev_to_monorev(git, app, param, parent_rev);
	      app.db.get_revision_manifest(parent_rev, parent_mid);
	    }

	  manifest_map parent_man;
	  L(F("parent revision '%s'") % parent_rev.inner());
	  L(F("parent manifest '%s', loading...") % parent_mid.inner());
	  app.db.get_manifest(parent_mid, parent_man);

	  boost::shared_ptr<change_set> changes(new change_set());

	  // complete_change_set(parent_man, manifest, *changes);
	  full_change_set(parent_man, manifest, *changes);

	  rev.edges.insert(make_pair(parent_rev, make_pair(parent_mid, changes)));
	}
      else if (keyword == "committer")
	{
	  parse_person_line(param, committer, commit_time);
	}
      else if (keyword == "author")
	{
	  parse_person_line(param, author, author_time);
	}
    }

  delete &fb;

  revision_id rid;
  calculate_ident(rev, rid);
  L(F("[%s] Monotone commit ID: '%s'") % gitrid() % rid.inner());
  if (! app.db.revision_exists(rid))
    app.db.put_revision(rid, rev);
  git.commitmap[gitrid()] = make_pair(rid, rev.new_manifest);
  ++git.n_revs;
  ++git.n_objs;

  packet_db_writer dbw(app);
  cert_revision_in_branch(rid, cert_value(git.base_branch), app, dbw);
  cert_revision_author(rid, committer.name, app, dbw);
  cert_revision_changelog(rid, logmsg, app, dbw);
  cert_revision_date_time(rid, commit_time, app, dbw);

  put_simple_revision_cert(rid, gitcommit_id_cert_name,
                           gitrid(), app, dbw);
  string authorcert = author.name + " <" + author.email + "> "
                    + boost::lexical_cast<string>(author_time);
  put_simple_revision_cert(rid, gitcommit_author_cert_name,
                           authorcert, app, dbw);

  return rid;
}


git_history::git_history(const fs::path &path)
  : db(path), n_revs("revisions", "r", 1), n_objs("objects", "o", 10)
{
}


void
import_git_repo(fs::path const & gitrepo,
                app_state & app)
{
  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    N(guess_default_key(key,app),
      F("no unique private key for cert construction"));
    require_password(key, app);
  }

  N(fs::exists(gitrepo),
    F("path %s does not exist") % gitrepo.string());
  N(fs::is_directory(gitrepo),
    F("path %s is not a directory") % gitrepo.string());
  putenv((char*)strdup((string("GIT_DIR=")+gitrepo.native_directory_string()).c_str()));

  N(app.branch_name() != "", F("need base --branch argument for importing"));

  git_history git(gitrepo);
  git.base_branch = app.branch_name();

  {
    transaction_guard guard(app.db);
    app.db.ensure_open();

    set<git_object_id> revs_exclude;
    git_heads_on_branch(git, app, revs_exclude);
    stack<git_object_id> revs = git.db.load_revs("master", revs_exclude);

    while (!revs.empty())
      {
	ui.set_tick_trailer(revs.top()());
	import_git_commit(git, app, revs.top());
	revs.pop();
      }
    ui.set_tick_trailer("");
    guard.commit();
  }

  // TODO: tags

  return;
}


#else // WIN32

void
import_git_repo(fs::path const & gitrepo,
                app_state & app)
{
  throw oops("git import not supported on win32");
}

#endif
