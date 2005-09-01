// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// vim:sw=2:
// Copyright (C) 2005  Petr Baudis <pasky@suse.cz>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// Based on cvs_import by graydon hoare <graydon@pobox.com>
// Sponsored by Google's Summer of Code and SuSE

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
#include "git_import.hh"
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

struct git_history;

struct
git_db
{
  const system_path path;

  void get_object(const string type, const git_object_id objid, filebuf &fb);
  void get_object(const string type, const git_object_id objid, data &dat);
  string get_object_type(const git_object_id objid);

  // DAG of the revision ancestry in topological order
  // (top of the stack are the earliest revisions)
  // The @revision can be even head name.
  stack<git_object_id> load_revs(const string revision, const set<git_object_id> &exclude);

  git_db(const system_path &path_) : path(path_) { }
};


struct
git_history
{
  git_db db;

  map<git_object_id, pair<revision_id, manifest_id> > commitmap;
  map<git_object_id, file_id> filemap;

  ticker n_revs;
  ticker n_objs;

  string branch;

  git_history(const system_path & path);
};



/*** The raw GIT interface */

void
git_db::get_object(const string type, const git_object_id objid, filebuf &fb)
{
  capture_git_cmd_output(F("git-cat-file %s %s") % type % objid(), fb);
}

void
git_db::get_object(const string type, const git_object_id objid, data &dat)
{
  filebuf fb;
  get_object(type, objid, fb);
  istream stream(&fb);

  Botan::Pipe pipe;
  pipe.start_msg();
  stream >> pipe;
  pipe.end_msg();
  dat = pipe.read_all_as_string();
}

string
git_db::get_object_type(const git_object_id objid)
{
  filebuf fb;
  capture_git_cmd_output(F("git-cat-file -t %s") % objid(), fb);
  istream stream(&fb);

  string type;
  stream >> type;
  return type;
}


stack<git_object_id>
git_db::load_revs(const string revision, const set<git_object_id> &exclude)
{
  string excludestr;
  for (set<git_object_id>::const_iterator i = exclude.begin();
       i != exclude.end(); ++i)
    excludestr += " \"^" + (*i)() + "\"";

  filebuf fb;
  capture_git_cmd_output(F("git-rev-list --topo-order %s %s")
                     % revision % excludestr, fb);
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
	  file_path fpath = file_path_internal(fullname);

          if (mode & 0100) // executable
            {
              L(F("marking '%s' as executable") % fullname);
              attrs[fpath]["execute"] = "true";
            }

          file_id fid = import_git_blob(git, app, gitoid);
          L(F("entry monoid [%s]") % fid.inner());
          manifest.insert(manifest_entry(fpath, fid));
        }
    }

  ++git.n_objs;
}


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
// this is used for incremental import. Being smart, it also
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

  N(false,
    F("Wicked revision tree - incremental import wanted to import a GIT commit\n"
      "whose parent is not in the Monotone database yet. This means a hole must\n"
      "have popped up in the Monotone revision history."));
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
      L(F("full_change_set: looking up '%s' - hits old %d and new %d")
	% *i % (j != m_old.end()) % (k != m_new.end()));
      if (j == m_old.end())
	{
	  L(F("full_change_set: adding %s") % manifest_entry_id(k));
	  cs.add_file(*i, manifest_entry_id(k));
	}
      else if (k == m_new.end())
	{
	  L(F("full_change_set: deleting %s") % manifest_entry_id(j));
	  cs.delete_file(*i);
	}
      else if (!(manifest_entry_id(j) == manifest_entry_id(k)))
	{
	  L(F("full_change_set: delta %s -> %s") % manifest_entry_id(j) % manifest_entry_id(k));
	  cs.deltas.insert(std::make_pair(*i, std::make_pair(manifest_entry_id(j),
							     manifest_entry_id(k))));
	}
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
  filebuf fb;
  git.db.get_object("commit", gitrid, fb);
  istream stream(&fb);

  bool header = true;
  revision_set rev;
  edge_map edges;

  manifest_map manifest;
  // XXX: it might be user policy decision whether to take author
  // or committer as monotone author
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
          revision_id parent_rev;
          manifest_id parent_mid;

          // given the topo order, we ought to have the parent hashed - except
          // for incremental imports
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

	  {
	    data cset;
	    write_change_set(*changes, cset);
	    L(F("Changeset:\n%s") % cset());
	  }

          edges.insert(make_pair(parent_rev, make_pair(parent_mid, changes)));
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

  // Connect with the ancestry:

  edge_map::const_iterator e = edges.begin();
  // In the normal case, edges will have only single member.
  if (e != edges.end()) // Root commit has no parents!
    rev.edges.insert(*(e++));

  // For regular merges, it will have two members.
  if (e != edges.end())
    rev.edges.insert(*(e++));

  revision_id rid;
  bool put_commit = true;
  // But for octopus merges, it will have even more. That's why are
  // we doing all this funny iteration stuff.
  bool octopus = false;

  while (put_commit)
    {
      calculate_ident(rev, rid);
      L(F("[%s] Monotone commit ID: '%s'") % gitrid() % rid.inner());
      if (! app.db.revision_exists(rid))
	app.db.put_revision(rid, rev);
      git.commitmap[gitrid()] = make_pair(rid, rev.new_manifest);
      ++git.n_revs;
      ++git.n_objs;

      packet_db_writer dbw(app);
      cert_revision_in_branch(rid, cert_value(git.branch), app, dbw);
      cert_revision_author(rid, author.name, app, dbw);
      cert_revision_date_time(rid, commit_time, app, dbw);
      if (octopus)
	cert_revision_changelog(rid,
				"Dummy commit representing GIT octopus merge.\n(See the previous commit.)",
				app, dbw);
      else
	cert_revision_changelog(rid, logmsg, app, dbw);

      put_simple_revision_cert(rid, gitcommit_id_cert_name,
			       gitrid(), app, dbw);
      string ctercert = committer.name + " <" + committer.email + "> "
			+ boost::lexical_cast<string>(commit_time);
      put_simple_revision_cert(rid, gitcommit_committer_cert_name,
			       ctercert, app, dbw);

      put_commit = false;
      if (e != edges.end())
        {
	  L(F("OCTOPUS MERGE"));
	  // Octopus merge - keep going.
	  put_commit = true;
	  octopus = true;

	  rev.edges.clear();
	  rev.edges.insert(*(e++));
	  // The current commit. Manifest stays the same so we needn't
	  // bother with changeset.
          rev.edges.insert(make_pair(rid, make_pair(rev.new_manifest,
                                                boost::shared_ptr<change_set>(new change_set())
						)));
	}
    }

  return rid;
}

class
heads_tree_walker
  : public absolute_tree_walker
{
  git_history & git;
  app_state & app;
public:
  heads_tree_walker(git_history & g, app_state & a)
    : git(g), app(a)
  {
  }
  virtual void visit_file(system_path const & path)
  {
    L(F("Processing head file '%s'") % path);

    data refdata;
    read_data(path, refdata);

    // We can't just .leaf() - there can be heads like "net/ipv4" and such.
    // XXX: My head hurts from all those temporary variables.
    system_path spheadsdir = git.db.path / "refs/heads";
    fs::path headsdir(spheadsdir.as_external(), fs::native);
    fs::path headpath(path.as_external(), fs::native);
    std::string strheadpath = headpath.string(), strheadsdir = headsdir.string();

    N(strheadpath.substr(0, strheadsdir.length()) == strheadsdir,
      F("heads directory name screwed up - %s does not begin with %s")
      % strheadpath % strheadsdir);
    std::string headname(strheadpath.substr(strheadsdir.length() + 1)); // + '/'

    git.branch = app.branch_name();
    if (headname != "master")
      git.branch += "." + headname;

    set<git_object_id> revs_exclude;
    git_heads_on_branch(git, app, revs_exclude);
    stack<git_object_id> revs = git.db.load_revs(headname, revs_exclude);

    while (!revs.empty())
      {
        ui.set_tick_trailer(revs.top()());
        import_git_commit(git, app, revs.top());
        revs.pop();
      }
    ui.set_tick_trailer("");
  }
  virtual ~heads_tree_walker() {}
};


static void
import_git_tag(git_history &git, app_state &app, git_object_id gittid,
               git_object_id &targetobj)
{
  L(F("Importing tag '%s'") % gittid());
  data dat;
  git.db.get_object("tag", gittid, dat);
  string str(dat());

  // The tag object header always starts with an "object" line which is the
  // only thing interesting for us.

  str.erase(0, str.find(' ') + 1);
  str.erase(str.find('\n'));
  I(str.length() == 40);
  targetobj = str;
}

static bool
resolve_git_tag(git_history &git, app_state &app, string &name,
                git_object_id &gitoid, revision_id &rev)
{
  // The cheapest first:
  map<git_object_id, pair<revision_id, manifest_id> >::const_iterator i = git.commitmap.find(gitoid());
  if (i != git.commitmap.end())
    {
      L(F("commitmap hit '%s'") % i->second.first.inner());
      rev = i->second.first;
      return true;
    }

  // Here, we could check the other maps and throw an error, but since tags
  // of other objects than tags are extremely rare, it's really not worth it.

  // To avoid potentially scanning all the history, check if it's a tag object
  // (very common), or indeed a "strange" one:
  string type = git.db.get_object_type(gitoid);

  if (type == "tag")
    {
      git_object_id obj;
      import_git_tag(git, app, gitoid, obj);
      return resolve_git_tag(git, app, name, obj, rev);
    }
  else if (type == "commit")
    {
      historical_gitrev_to_monorev(git, app, gitoid, rev);
      return true;
    }
  else
    {
      ui.warn(F("Warning: GIT tag '%s' (%s) does not tag a revision but a %s. Skipping...")
              % name % gitoid() % type);
      return false;
    }
}

static void
import_unresolved_git_tag(git_history &git, app_state &app, string name, git_object_id gitoid)
{
  L(F("Importing tag '%s' -> '%s'") % name % gitoid());

  // Does the tag already exist?
  // FIXME: Just look it up in the db.
  vector< revision<cert> > certs;
  app.db.get_revision_certs(tag_cert_name, certs);
  for (vector< revision<cert> >::const_iterator i = certs.begin();
      i != certs.end(); ++i)
    {
      cert_value cname;
      cert c = i->inner();
      decode_base64(c.value, cname);
      if (cname == name)
        {
          L(F("tag already exists"));
          return;
        }
    }

  revision_id rev;
  if (!resolve_git_tag(git, app, name, gitoid, rev))
    return;

  L(F("Writing tag '%s' -> '%s'") % name % rev.inner());
  packet_db_writer dbw(app);
  cert_revision_tag(rev.inner(), name, app, dbw);
}

class
tags_tree_walker
  : public absolute_tree_walker
{
  git_history & git;
  app_state & app;
public:
  tags_tree_walker(git_history & g, app_state & a)
    : git(g), app(a)
  {
  }
  virtual void visit_file(system_path const & path)
  {
    L(F("Processing tag file '%s'") % path);

    data refdata;
    read_data(path, refdata);

    // We can't just .leaf() - there can be tags like "net/v1.0" and such.
    // XXX: My head hurts from all those temporary variables.
    system_path sptagsdir = git.db.path / "refs/tags";
    fs::path tagsdir(sptagsdir.as_external(), fs::native);
    fs::path tagpath(path.as_external(), fs::native);
    std::string strtagpath = tagpath.string(), strtagsdir = tagsdir.string();

    N(strtagpath.substr(0, strtagsdir.length()) == strtagsdir,
      F("tags directory name screwed up - %s does not being with %s")
      % strtagpath % strtagsdir);
    std::string tagname(strtagpath.substr(strtagsdir.length() + 1)); // + '/'

    import_unresolved_git_tag(git, app, tagname, refdata().substr(0, 40));
  }
  virtual ~tags_tree_walker() {}
};


git_history::git_history(system_path const & path)
  : db(path), n_revs("revisions", "r", 1), n_objs("objects", "o", 10)
{
}


void
import_git_repo(system_path const & gitrepo,
                app_state & app)
{
  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    N(guess_default_key(key,app),
      F("no unique private key for cert construction"));
    require_password(key, app);
  }

  require_path_is_directory(gitrepo,
                            F("repo %s does not exist") % gitrepo,
                            F("repo %s is not a directory") % gitrepo);

  {
    char * env_entry = strdup((string("GIT_DIR=") + gitrepo.as_external()).c_str());
    putenv(env_entry);
  }

  N(app.branch_name() != "", F("need base --branch argument for importing"));

  git_history git(gitrepo);

  {
    system_path heads_tree = gitrepo / "refs/heads";
    N(directory_exists(heads_tree),
      F("path %s is not a directory") % heads_tree);

    transaction_guard guard(app.db);
    app.db.ensure_open();

    heads_tree_walker walker(git, app);
    walk_tree_absolute(heads_tree, walker);
    guard.commit();
  }

  system_path tags_tree = gitrepo / "refs/tags";
  if (path_exists(tags_tree))
    {
      N(directory_exists(tags_tree),
        F("path %s is not a directory") % tags_tree);

      transaction_guard guard(app.db);
      app.db.ensure_open();

      tags_tree_walker walker(git, app);
      walk_tree_absolute(tags_tree, walker);
      guard.commit();
    }

  return;
}


#else // WIN32

void
import_git_repo(system_path const & gitrepo,
                app_state & app)
{
  E("git import not supported on win32");
}

#endif
