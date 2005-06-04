// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003, 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

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
#include "cycle_detector.hh"
#include "database.hh"
#include "file_io.hh"
#include "keys.hh"
#include "interner.hh"
#include "manifest.hh"
#include "packet.hh"
#include "rcs_file.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"

using namespace std;
using boost::shared_ptr;
using boost::scoped_ptr;

// cvs history recording stuff

typedef unsigned long cvs_branchname;
typedef unsigned long cvs_author;
typedef unsigned long cvs_changelog;
typedef unsigned long cvs_version;
typedef unsigned long cvs_path;

struct cvs_history;

struct 
cvs_key
{
  cvs_key() {}
  cvs_key(rcs_file const & r, 
          string const & version, 
          cvs_history & cvs);

  inline bool similar_enough(cvs_key const & other) const
  {
    L(F("Checking similarity of %d and %d\n") % id % other.id);
    if (changelog != other.changelog)
      return false;
    if (author != other.author)
      return false;
    if (labs(time - other.time) > constants::cvs_window)
      return false;
    for (map<file_path,string>::const_iterator it = files.begin(); it!=files.end(); it++)
      {
        map<file_path,string>::const_iterator otherit;
        
        L(F("checking %s %s\n") % it->first % it->second);
        otherit = other.files.find(it->first);
        if (otherit != other.files.end() && it->second!=otherit->second)
          {
            L(F("!similar_enough: %d/%d\n") % id % other.id);
            return false;
          }
        else if (otherit != other.files.end())
          {
            L(F("Same file, different version: %s and %s\n") % it->second % otherit->second);
          }
      }
    L(F("similar_enough: %d/%d\n") % id % other.id);
    return true;
  }

  inline bool operator==(cvs_key const & other) const
  {
    L(F("Checking equality of %d and %d\n") % id % other.id);
    return is_synthetic_branch_founding_commit == other.is_synthetic_branch_founding_commit &&
      branch == other.branch &&
      changelog == other.changelog &&
      author == other.author &&
      time == other.time;
  }
  inline bool operator<(cvs_key const & other) const
  {
    // nb: this must sort as > to construct the edges in the right direction

    if (is_synthetic_branch_founding_commit)
      {
        I(!other.is_synthetic_branch_founding_commit);
        return false;
      }

    if (other.is_synthetic_branch_founding_commit)
      {
        I(!is_synthetic_branch_founding_commit);
        return true;
      }
    
    return time > other.time ||

      (time == other.time 
       && author > other.author) ||

      (time == other.time 
       && author == other.author 
       && changelog > other.changelog) ||

      (time == other.time 
       && author == other.author 
       && changelog == other.changelog
       && branch > other.branch);
  }

  inline void add_file(file_path const &file, string const &version)
  {
    L(F("Adding file %s version %s to CVS key %d\n") % file % version % id);
    files.insert( make_pair(file, version) );
  }

  bool is_synthetic_branch_founding_commit;
  cvs_branchname branch;
  cvs_changelog changelog;
  cvs_author author;
  time_t time;
  map<file_path, string> files; // Maps file to version
  int id; // Only used for debug output

  static int nextid; // Used to initialise id
};

int cvs_key::nextid = 0;

struct 
cvs_file_edge
{
  cvs_file_edge (file_id const & pv, 
                 file_path const & pp,
                 bool pl,
                 file_id const & cv, 
                 file_path const & cp,
                 bool cl,
                 cvs_history & cvs);
  cvs_version parent_version;
  cvs_path parent_path;
  bool parent_live_p;
  cvs_version child_version;
  cvs_path child_path;
  bool child_live_p;
  inline bool operator<(cvs_file_edge const & other) const
  {
#if 0
    return (parent_path < other.parent_path) 
                       || ((parent_path == other.parent_path) 
       && ((parent_version < other.parent_version) 
                       || ((parent_version == other.parent_version) 
       && ((parent_live_p < other.parent_live_p) 
                       || ((parent_live_p == other.parent_live_p) 
       && ((child_path < other.child_path) 
                       || ((child_path == other.child_path) 
       && ((child_version < other.child_version) 
                       || ((child_version == other.child_version) 
       && (child_live_p < other.child_live_p) )))))))));
#else
    return (parent_path < other.parent_path) 
                        || ((parent_path == other.parent_path) 
        && ((parent_version < other.parent_version) 
                        || ((parent_version == other.parent_version) 
        && ((parent_live_p < other.parent_live_p) 
                        || ((parent_live_p == other.parent_live_p) 
        && ((child_path < other.child_path) 
                        || ((child_path == other.child_path) 
        && ((child_version < other.child_version) 
                        || ((child_version == other.child_version) 
        && (child_live_p < other.child_live_p) )))))))));
#endif
  }
};

struct 
cvs_state
{
  set<cvs_file_edge> in_edges;
};

typedef map<cvs_key, shared_ptr<cvs_state> > 
cvs_branch;

struct 
cvs_history
{

  interner<unsigned long> branch_interner;
  interner<unsigned long> author_interner;
  interner<unsigned long> changelog_interner;
  interner<unsigned long> file_version_interner;
  interner<unsigned long> path_interner;
  interner<unsigned long> manifest_version_interner;

  cycle_detector<unsigned long> manifest_cycle_detector;

  bool find_key_and_state(rcs_file const & r, 
                          string const & version,
                          cvs_key & key,
                          shared_ptr<cvs_state> & state);


  // assume admin has foo:X.Y.0.N in it, then 
  // this multimap contains entries of the form
  // X.Y   -> foo
  multimap<string, string> branchpoints;
  
  // and this map contains entries of the form
  // X.Y.N.1 -> foo
  map<string, string> branch_first_entries;

  // branch name -> branch
  map<string, shared_ptr<cvs_branch> > branches;

  // branch name -> whether there are any commits on the
  //                branch (as opposed to just branchpoints)
  map<string, bool> branch_has_commit;

  // stack of branches we're injecting states into
  stack< shared_ptr<cvs_branch> > stk;
  stack< cvs_branchname > bstk;

  file_path curr_file;
  
  string base_branch;

  ticker n_versions;
  ticker n_tree_branches;  

  cvs_history();
  void set_filename(string const & file,
                    file_id const & ident);

  void index_branchpoint_symbols(rcs_file const & r);


  enum note_type { note_branchpoint,
                   note_branch_first_commit };

  void note_state_at_branch_beginning(rcs_file const & r,
				      string const & branchname,
				      string const & version, 
				      file_id const & ident,
                                      note_type nt);

  void push_branch(string const & branch_name, bool private_branch);

  void note_file_edge(rcs_file const & r, 
                      string const & prev_rcs_version_num,
                      string const & next_rcs_version_num,
                      file_id const & prev_version,
                      file_id const & next_version);

  void pop_branch();
};


// piece table stuff

struct piece;

struct 
piece_store
{
  vector< boost::shared_ptr<rcs_deltatext> > texts;
  void index_deltatext(boost::shared_ptr<rcs_deltatext> const & dt,
                       vector<piece> & pieces);
  void build_string(vector<piece> const & pieces,
                    string & out);
  void reset() { texts.clear(); }
};

// FIXME: kludge, I was lazy and did not make this
// a properly scoped variable. 

static piece_store global_pieces;


struct 
piece
{
  piece(string::size_type p, string::size_type l, unsigned long id) :
    pos(p), len(l), string_id(id) {}
  string::size_type pos;
  string::size_type len;
  unsigned long string_id;
  string operator*() const
  {
    return string(global_pieces.texts.at(string_id)->text.data() + pos, len);
  }
};


void 
piece_store::build_string(vector<piece> const & pieces,
                          string & out)
{
  out.clear();
  out.reserve(pieces.size() * 60);
  for(vector<piece>::const_iterator i = pieces.begin();
      i != pieces.end(); ++i)
    out.append(texts.at(i->string_id)->text, i->pos, i->len);
}

void 
piece_store::index_deltatext(boost::shared_ptr<rcs_deltatext> const & dt,
                             vector<piece> & pieces)
{
  pieces.clear();
  pieces.reserve(dt->text.size() / 30);  
  texts.push_back(dt);
  unsigned long id = texts.size() - 1;
  string::size_type begin = 0;
  string::size_type end = dt->text.find('\n');
  while(end != string::npos)
    {
      // nb: the piece includes the '\n'
      pieces.push_back(piece(begin, (end - begin) + 1, id));
      begin = end + 1;
      end = dt->text.find('\n', begin);
    }
  if (begin != dt->text.size())
    {
      // the text didn't end with '\n', so neither does the piece
      end = dt->text.size();
      pieces.push_back(piece(begin, end - begin, id));
    }
}


static void 
process_one_hunk(vector< piece > const & source,
                 vector< piece > & dest,
                 vector< piece >::const_iterator & i,
                 int & cursor)
{
  string directive = **i;
  assert(directive.size() > 1);
  ++i;

  try 
    {
      char code;
      int pos, len;
      if (sscanf(directive.c_str(), " %c %d %d", &code, &pos, &len) != 3)
	      throw oops("illformed directive '" + directive + "'");

      if (code == 'a')
        {
          // 'ax y' means "copy from source to dest until cursor == x, then
          // copy y lines from delta, leaving cursor where it is"
          while (cursor < pos)
            dest.push_back(source.at(cursor++));
          I(cursor == pos);
          while (len--)
            dest.push_back(*i++);
        }
      else if (code == 'd')
        {      
          // 'dx y' means "copy from source to dest until cursor == x-1,
          // then increment cursor by y, ignoring those y lines"
          while (cursor < (pos - 1))
            dest.push_back(source.at(cursor++));
          I(cursor == pos - 1);
          cursor += len;
        }
      else 
        throw oops("unknown directive '" + directive + "'");
    } 
  catch (std::out_of_range & oor)
    {
      throw oops("std::out_of_range while processing " + directive 
                 + " with source.size() == " 
                 + boost::lexical_cast<string>(source.size())
                 + " and cursor == "
                 + boost::lexical_cast<string>(cursor));
    }  
}

static void
construct_version(vector< piece > const & source_lines,
                  string const & dest_version, 
                  vector< piece > & dest_lines,
                  rcs_file const & r)
{
  dest_lines.clear();
  dest_lines.reserve(source_lines.size());

  I(r.deltas.find(dest_version) != r.deltas.end());
  shared_ptr<rcs_delta> delta = r.deltas.find(dest_version)->second;
  
  I(r.deltatexts.find(dest_version) != r.deltatexts.end());
  shared_ptr<rcs_deltatext> deltatext = r.deltatexts.find(dest_version)->second;
  
  vector<piece> deltalines;
  global_pieces.index_deltatext(deltatext, deltalines);
  
  int cursor = 0;
  for (vector<piece>::const_iterator i = deltalines.begin(); 
       i != deltalines.end(); )
    process_one_hunk(source_lines, dest_lines, i, cursor);
  while (cursor < static_cast<int>(source_lines.size()))
    dest_lines.push_back(source_lines[cursor++]);
}

// FIXME: should these be someplace else? using 'friend' to reach into the
// DB is stupid, but it's also stupid to put raw edge insert methods on the
// DB itself. or is it? hmm.. encapsulation vs. usage guidance..
void 
rcs_put_raw_file_edge(hexenc<id> const & old_id,
                      hexenc<id> const & new_id,
                      delta const & del,
                      database & db)
{
  if (old_id == new_id)
    {
      L(F("skipping identity file edge\n"));
      return;
    }

  if (db.file_version_exists(old_id))
    {
      // we already have a way to get to this old version,
      // no need to insert another reconstruction path
      L(F("existing path to %s found, skipping\n") % old_id);
    }
  else
    {
      I(db.exists(new_id, "files")
        || db.delta_exists(new_id, "file_deltas"));
      db.put_delta(old_id, new_id, del, "file_deltas");
    }
}

void 
rcs_put_raw_manifest_edge(hexenc<id> const & old_id,
                          hexenc<id> const & new_id,
                          delta const & del,
                          database & db)
{
  if (old_id == new_id)
    {
      L(F("skipping identity manifest edge\n"));
      return;
    }

  if (db.manifest_version_exists(old_id))
    {
      // we already have a way to get to this old version,
      // no need to insert another reconstruction path
      L(F("existing path to %s found, skipping\n") % old_id);
    }
  else
    {
      db.put_delta(old_id, new_id, del, "manifest_deltas");
    }
}


static void
insert_into_db(data const & curr_data,
               hexenc<id> const & curr_id,
               vector< piece > const & next_lines,
               data & next_data,
               hexenc<id> & next_id,
               database & db)
{
  // inserting into the DB
  // note: curr_lines is a "new" (base) version
  //       and next_lines is an "old" (derived) version.
  //       all storage edges go from new -> old.
  {
    string tmp;
    global_pieces.build_string(next_lines, tmp);
    next_data = tmp;
  }
  delta del;
  diff(curr_data, next_data, del);
  calculate_ident(next_data, next_id);
  rcs_put_raw_file_edge(next_id, curr_id, del, db);
}

static void 
process_branch(string const & begin_version, 
               vector< piece > const & begin_lines,
               data const & begin_data,
               hexenc<id> const & begin_id,
               rcs_file const & r, 
               database & db,
               cvs_history & cvs)
{
  string curr_version = begin_version;
  scoped_ptr< vector< piece > > next_lines(new vector<piece>);
  scoped_ptr< vector< piece > > curr_lines(new vector<piece> 
                                           (begin_lines.begin(),
                                            begin_lines.end()));  
  data curr_data(begin_data), next_data;
  hexenc<id> curr_id(begin_id), next_id;
  
  while(! (r.deltas.find(curr_version) == r.deltas.end()))
    {
      L(F("version %s has %d lines\n") % curr_version % curr_lines->size());
      
      string next_version = r.deltas.find(curr_version)->second->next;
      if (!next_version.empty())
      {  // construct this edge on our own branch
         L(F("following RCS edge %s -> %s\n") % curr_version % next_version);

         construct_version(*curr_lines, next_version, *next_lines, r);
         L(F("constructed RCS version %s, inserting into database\n") % 
           next_version);

         insert_into_db(curr_data, curr_id, 
                     *next_lines, next_data, next_id, db);

          cvs.note_file_edge (r, curr_version, next_version, 
                          file_id(curr_id), file_id(next_id));
      }
      else
      {  L(F("revision %s has no successor\n") % curr_version);
         if (curr_version=="1.1")
         {  // mark this file as newly present since this commit
            // (and as not present before)
            
            // perhaps this should get a member function of cvs_history ?
            L(F("marking %s as not present in older manifests\n") % curr_version);
            cvs_key k;
            shared_ptr<cvs_state> s;
            cvs.find_key_and_state(r, curr_version, k, s);
            I(r.deltas.find(curr_version) != r.deltas.end());
            bool live_p = r.deltas.find(curr_version)->second->state != "dead";
            s->in_edges.insert(cvs_file_edge(curr_id, cvs.curr_file, false,
                                             curr_id, cvs.curr_file, live_p,
                                             cvs));
            ++cvs.n_versions;
         }
      }


      /*
       
      please read this exhaustingly long comment and understand it
      before mucking with the branch inference logic.

      we are processing a file version. a branch might begin here. if
      the current version is X.Y, then there is a branch B starting
      here iff there is a symbol in the admin section called X.Y.0.Z,
      where Z is the branch number (or if there is a private branch 
      called X.Y.Z, which is either an import branch or some private
      RCS cruft).

      the version X.Y is then considered the branchpoint of B in the
      current file. this does *not* mean that the CVS key -- an
      abstraction representing whole-tree operations -- of X.Y is the
      branchpoint across the CVS archive we're processing.

      in fact, CVS does not record the occurrence of a branching
      action (tag -b). we have no idea who executed that command and
      when. what we know instead is the commit X.Y immediately
      preceeding the branch -- CVS consideres this the branchpoint --
      in this file's reduced view of history. we also know the first
      commit X.Y.Z.1 inside the branch (which might not exist).

      our old strategy was to consider all branches nested in a
      hierarchy, which was a super-tree of all the branch trees in all
      the CVS files in a repository. this involved considering X.Y as
      the parent version of branch X.Y.Z, an selecting "the"
      branchpoint connecting the two as the least CVS key X.Y.Z.1
      committed inside the branch B.

      this was a mistake, for two significant reasons.

      first, some files do not *have* any commit inside the branch B,
      only a branchpoint X.Y.0.Z. this branchpoint is actually the
      last commit *before* the user branched, and could be a very old
      commit, long before the branch was formed, so it is useless in
      determining the branch structure.

      second, some files do not have a branch B, or worse, have
      branched into B from an "ancestor" branch A, where a different
      file branches into B from a different ancestor branch C. in
      other words, while there *is* a tree structure within the X.Y.Z
      branches of each file, there is *no* shared tree structure
      between the branch names across a repository. in one file A can
      be an ancestor of B, in another file B can be an ancestor of A.

      thus, we give up on establishing a hierarchy between branches
      altogether. all branches exist in a flat namespace, and all are
      direct descendents of the empty revision at the root of
      history. each branchpoint symbol mentioned in the
      administrative section of a file is considered the root of a new
      lineage.
      
      */

      typedef multimap<string,string>::const_iterator ity;
      pair<ity,ity> range = cvs.branchpoints.equal_range(curr_version);
      if (range.first != cvs.branchpoints.end() 
	  && range.first->first == curr_version)
	{
	  for (ity branch = range.first; branch != range.second; ++branch)
	    {
	      cvs.note_state_at_branch_beginning(r, branch->second,
						 curr_version, 
						 curr_id,
                                                 cvs_history::note_branchpoint);
	    }
	}

      // recursively follow any branch commits coming from the branchpoint
      boost::shared_ptr<rcs_delta> curr_delta = r.deltas.find(curr_version)->second;
      for(vector<string>::const_iterator i = curr_delta->branches.begin();
	  i != curr_delta->branches.end(); ++i)
	{
	  string branch;
	  bool priv = false;
	  map<string, string>::const_iterator be = cvs.branch_first_entries.find(*i);

	  if (be != cvs.branch_first_entries.end())
	    branch = be->second;
	  else
	    priv = true;
	  
	  L(F("following RCS branch %s = '%s'\n") % (*i) % branch);
	  vector< piece > branch_lines;
	  construct_version(*curr_lines, *i, branch_lines, r);
          	  
	  data branch_data;
	  hexenc<id> branch_id;
	  insert_into_db(curr_data, curr_id, 
			 branch_lines, branch_data, branch_id, db);

          // update the branch beginning time to reflect improved
          // information, if there was a commit on the branch
          if (!priv)
            cvs.note_state_at_branch_beginning(r, branch,
                                               *i, 
                                               branch_id,
                                               cvs_history::note_branch_first_commit);

	  cvs.push_branch (branch, priv);
	  cvs.note_file_edge (r, curr_version, *i,
			      file_id(curr_id), file_id(branch_id));	      
	  process_branch(*i, branch_lines, branch_data, branch_id, r, db, cvs);
	  cvs.pop_branch();
	  
	  L(F("finished RCS branch %s = '%s'\n") % (*i) % branch);
	}

      if (!r.deltas.find(curr_version)->second->next.empty())
      {  // advance
         curr_data = next_data;
         curr_id = next_id;
         curr_version = next_version;
         swap(next_lines, curr_lines);
         next_lines->clear();
      }
      else break;
    }
} 


static void 
import_rcs_file_with_cvs(string const & filename, database & db, cvs_history & cvs)
{
  rcs_file r;
  L(F("parsing RCS file %s\n") % filename);
  parse_rcs_file(filename, r);
  L(F("parsed RCS file %s OK\n") % filename);

  {
    vector< piece > head_lines;  
    I(r.deltatexts.find(r.admin.head) != r.deltatexts.end());
    I(r.deltas.find(r.admin.head) != r.deltas.end());

    hexenc<id> id; 
    data dat(r.deltatexts.find(r.admin.head)->second->text);
    calculate_ident(dat, id);
    file_id fid = id;

    cvs.set_filename (filename, fid);
    cvs.index_branchpoint_symbols (r);

    if (! db.file_version_exists (fid))
      {
        db.put_file(fid, dat); 
      }
        
    {
      // create the head state in case it is a loner
      cvs_key k;
      shared_ptr<cvs_state> s;
      L(F("noting head version %s : %s\n") % cvs.curr_file % r.admin.head);
      cvs.find_key_and_state (r, r.admin.head, k, s);
    }
    
    global_pieces.reset();
    global_pieces.index_deltatext(r.deltatexts.find(r.admin.head)->second, head_lines);
    process_branch(r.admin.head, head_lines, dat, id, r, db, cvs);
    global_pieces.reset();
  }

  ui.set_tick_trailer("");
}


void 
import_rcs_file(fs::path const & filename, database & db)
{
  cvs_history cvs;

  I(! fs::is_directory(filename));
  I(! filename.empty());

  fs::path leaf = mkpath(filename.leaf());
  fs::path branch = mkpath(filename.branch_path().string());

  I(! branch.empty());
  I(! leaf.empty());
  I( fs::is_directory(branch));
  I( fs::exists(branch));

  I(chdir(filename.branch_path().native_directory_string().c_str()) == 0); 

  I(fs::exists(leaf));

  import_rcs_file_with_cvs(leaf.native_file_string(), db, cvs);
}


// CVS importing stuff follows

/*

  we define a "cvs key" as a triple of author, commit time and
  changelog. the equality of keys is a bit blurry due to a window of time
  in which "the same" commit may begin and end. the window is evaluated
  during the multimap walk though; for insertion in the multimap a true >
  is used. a key identifies a particular commit.

  we reconstruct the history of a CVS archive by accumulating file edges
  into archive nodes. each node is called a "cvs_state", but it is really a
  collection of file *edges* leading into that archive state. we accumulate
  file edges by walking up the trunk and down the branches of each RCS file.

  once we've got all the edges accumulated into archive nodes, we walk the
  tree of cvs_states, up through the trunk and down through the branches,
  carrying a manifest_map with us during the walk. for each edge, we
  construct either the parent or child state of the edge (depending on
  which way we're walking) and then calculate and write out a manifest
  delta for the difference between the previous and current manifest map. we
  also write out manifest certs, though the direction of ancestry changes
  depending on whether we're going up the trunk or down the branches.

 */

cvs_file_edge::cvs_file_edge (file_id const & pv, file_path const & pp, bool pl,
                              file_id const & cv, file_path const & cp, bool cl,
                              cvs_history & cvs) :
  parent_version(cvs.file_version_interner.intern(pv.inner()())), 
  parent_path(cvs.path_interner.intern(pp())),
  parent_live_p(pl),
  child_version(cvs.file_version_interner.intern(cv.inner()())), 
  child_path(cvs.path_interner.intern(cp())),
  child_live_p(cl)
{
}

static void
split_version(string const & v, vector<string> & vs)
{
  vs.clear();
  boost::char_separator<char> sep(".");
  typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
  tokenizer tokens(v, sep);
  copy(tokens.begin(), tokens.end(), back_inserter(vs));
}

static void
join_version(vector<string> const & vs, string & v)
{
  v.clear();
  for (vector<string>::const_iterator i = vs.begin();
       i != vs.end(); ++i)
    {
      if (i != vs.begin())
	v += ".";
      v += *i;
    }
}

cvs_key::cvs_key(rcs_file const & r, string const & version,
                 cvs_history & cvs) :
  is_synthetic_branch_founding_commit(false)
{
  map<string, shared_ptr<rcs_delta> >::const_iterator delta = 
    r.deltas.find(version);
  I(delta != r.deltas.end());

  map<string, shared_ptr<rcs_deltatext> >::const_iterator deltatext = 
    r.deltatexts.find(version);
  I(deltatext != r.deltatexts.end());

  {    
    struct tm t;
    // We need to initialize t to all zeros, because strptime has a habit of
    // leaving bits of the data structure alone, letting garbage sneak into
    // our output.
    memset(&t, 0, sizeof(t));
    char const * dp = delta->second->date.c_str();
    L(F("Calculating time of %s\n") % dp);
#ifdef WIN32
    I(sscanf(dp, "%d.%d.%d.%d.%d.%d", &(t.tm_year), &(t.tm_mon), 
             &(t.tm_mday), &(t.tm_hour), &(t.tm_min), &(t.tm_sec))==6);
    t.tm_mon--;
    // Apparently some RCS files have 2 digit years, others four; tm always
    // wants a 2 (or 3) digit year (years since 1900).
    if (t.tm_year > 1900)
        t.tm_year-=1900;
#else
    if (strptime(dp, "%y.%m.%d.%H.%M.%S", &t) == NULL)
      I(strptime(dp, "%Y.%m.%d.%H.%M.%S", &t) != NULL);
#endif
    time=mktime(&t);
    L(F("= %i\n") % time);
    id = nextid++;
  }

  branch = cvs.bstk.top();
  changelog = cvs.changelog_interner.intern(deltatext->second->log);
  author = cvs.author_interner.intern(delta->second->author);
}


cvs_history::cvs_history() :
  n_versions("versions", "v", 1),
  n_tree_branches("branches", "b", 1)
{
}

void 
cvs_history::set_filename(string const & file,
                          file_id const & ident) 
{
  L(F("importing file '%s'\n") % file);
  I(file.size() > 2);
  I(file.substr(file.size() - 2) == string(",v"));
  string ss = file;
  ui.set_tick_trailer(ss);
  ss.resize(ss.size() - 2);
  // remove Attic/ if present
  std::string::size_type last_slash=ss.rfind('/');
  if (last_slash!=std::string::npos && last_slash>=5
        && ss.substr(last_slash-5,6)=="Attic/")
     ss.erase(last_slash-5,6);
  curr_file = file_path(ss);
}

void cvs_history::index_branchpoint_symbols(rcs_file const & r)
{
  branchpoints.clear();
  branch_first_entries.clear();

  for (std::multimap<std::string, std::string>::const_iterator i = 
	 r.admin.symbols.begin(); i != r.admin.symbols.end(); ++i)
    {
      std::string const & num = i->first;
      std::string const & sym = i->second;

      vector<string> components;
      split_version(num, components);

      if (components.size() > 2 && 
	  components[components.size() - 2] == string("0"))
	{
	  string first_entry_version;
	  components[components.size() - 2] = components[components.size() - 1];
	  components[components.size() - 1] = string("1");
	  join_version(components, first_entry_version);

	  L(F("first version in branch %s would be %s\n") 
	    % sym % first_entry_version);
	  branch_first_entries.insert(make_pair(first_entry_version, sym));

	  string branchpoint_version;
	  components.erase(components.end() - 2, components.end());
	  join_version(components, branchpoint_version);

	  L(F("file branchpoint for %s at %s\n") % sym % branchpoint_version);
	  branchpoints.insert(make_pair(branchpoint_version, sym));
	}
    }
}

void
cvs_history::note_state_at_branch_beginning(rcs_file const & r,
					    string const & branchname,
					    string const & version, 
					    file_id const & ident,
                                            note_type nt)
{
  // here we manufacture a single synthetic commit -- the "branch
  // birth" commit -- representing the cumulative affect of all the
  // tag -b operations the user once performed. it has a synthetic
  // author ("cvs_import") and a synthetic log message ("beginning of
  // branch foo"), and occurs at the time of the *last* branchpoint of
  // any files which entered this branch.
  //
  // note that this does not establish a revision-ancestry
  // relationship between the branchpoint and the branch. the branch
  // is considered a child of the null revision, as far as monotone is
  // concerned.

  L(F("noting branchpoint for %s = %s\n") % branchname % version);

  push_branch(branchname, false);

  cvs_key k;
  shared_ptr<cvs_state> s;
  I(stk.size() > 0);
  shared_ptr<cvs_branch> branch = stk.top();

  string branch_birth_message = "beginning of branch " + branchname;
  string branch_birth_author = "cvs_import";

  cvs_changelog clog = changelog_interner.intern(branch_birth_message);
  cvs_author auth = author_interner.intern(branch_birth_author);

  // note: SBFC is short for "synthetic branch-founding commit"

  if (branch->empty())
    {
      I(nt == note_branchpoint);
      find_key_and_state (r, version, k, s);
      branch->erase(k);
      k.changelog = clog;
      k.author = auth;
      k.add_file(curr_file, version);
      k.is_synthetic_branch_founding_commit = true;
      branch->insert(make_pair(k, s));
      L(F("added SBFC for %s at id %d, time %d\n") 
        % branchname % k.id % k.time);
    }
  else
    {
      cvs_key nk(r, version, *this);
      
      cvs_branch::iterator i = branch->end(); 
      i--;
      I(i->first.is_synthetic_branch_founding_commit);
      I(i->first.author == auth);
      I(i->first.changelog == clog);
      
      k = i->first;
      s = i->second;

      L(F("found existing SBFC at id %d, time %d\n") 
        % k.id % k.time);
      if (nt == note_branchpoint 
          && nk.time > k.time
          && branch_has_commit[branchname] == false)
	{
          L(F("moving SBFC for %s to later branchpoint at %d\n") 
            % branchname % nk.time);
	  branch->erase(i);
	  k.time = nk.time;
	  k.add_file(curr_file, version);
	  branch->insert(make_pair(k, s));
	}
      else if (nt == note_branch_first_commit
               && nk.time < k.time)
	{
          L(F("moving SBFC for %s to earlier branch commit at %d\n") 
            % branchname % nk.time);
	  branch->erase(i);
	  k.time = nk.time;
	  branch->insert(make_pair(k, s));
          branch_has_commit[branchname] = true;
	}

    }

  if (nt == note_branchpoint)
    {
      map<string, shared_ptr<rcs_delta> >::const_iterator del;
      del = r.deltas.find(version);
      I(del != r.deltas.end());
      bool alive = del->second->state != "dead";
      
      s->in_edges.insert(cvs_file_edge(file_id(), curr_file, alive,
                                       ident, curr_file, alive,
                                       *this));
    }

  pop_branch();
}

bool 
cvs_history::find_key_and_state(rcs_file const & r, 
                                string const & version,
                                cvs_key & key,
                                shared_ptr<cvs_state> & state)
{
  I(stk.size() > 0);
  shared_ptr<cvs_branch> branch = stk.top();
  cvs_key nk(r, version, *this);

  nk.add_file(curr_file, version);
  // key+(window/2) is in the future, key-(window/2) is in the past. the
  // past is considered "greater than" the future in this map, so we take:
  // 
  //  - new, the lower bound of key+(window/2) in the map
  //  - old, the upper bound of key-(window/2) in the map
  //
  // and search all the nodes inside this section, from new to old bound.

  map< cvs_key, shared_ptr<cvs_state> >::iterator i_new, i_old, i;
  cvs_key k_new(nk), k_old(nk);

  if (static_cast<time_t>(k_new.time + constants::cvs_window / 2) > k_new.time)
    k_new.time += constants::cvs_window / 2;

  if (static_cast<time_t>(k_old.time - constants::cvs_window / 2) < k_old.time)
    k_old.time -= constants::cvs_window / 2;
  
  i_new = branch->lower_bound(k_new);
  i_old = branch->upper_bound(k_old);

  for (i = i_new; i != i_old; ++i)
    {
      if (i->first.similar_enough(nk))
        {
          key = i->first;
          state = i->second;
	  branch->erase(i);
          key.add_file(curr_file, version);
	  branch->insert(make_pair(key,state));
          return true;
        }
    }
  key = nk;
  state = shared_ptr<cvs_state>(new cvs_state());
  branch->insert(make_pair(key, state));
  return false;
}

void 
cvs_history::push_branch(string const & branch_name, bool private_branch)
{      
  shared_ptr<cvs_branch> branch;

  I(stk.size() > 0);

  map<string, shared_ptr<cvs_branch> >::const_iterator b = branches.find(branch_name);
  if (b == branches.end())
    {
      branch = shared_ptr<cvs_branch>(new cvs_branch());
      if (!private_branch)
	branches.insert(make_pair(branch_name, branch));      
    }
  else
    branch = b->second;

  stk.push(branch);

  if (private_branch)
    bstk.push(bstk.top());
  else
    bstk.push(branch_interner.intern(base_branch + "." + branch_name));
}

void 
cvs_history::note_file_edge(rcs_file const & r, 
                            string const & prev_rcs_version_num,
                            string const & next_rcs_version_num,
                            file_id const & prev_version,
                            file_id const & next_version) 
{

  cvs_key k;
  shared_ptr<cvs_state> s;

  I(stk.size() > 0);
  I(! curr_file().empty());

  L(F("noting file edge %s -> %s\n") % prev_rcs_version_num % next_rcs_version_num);

  // we can't use operator[] since it is non-const
  std::map<std::string, boost::shared_ptr<rcs_delta> >::const_iterator
        prev_delta = r.deltas.find(prev_rcs_version_num),
        next_delta = r.deltas.find(next_rcs_version_num);
  I(prev_delta!=r.deltas.end());
  I(next_delta!=r.deltas.end());
  bool prev_alive = prev_delta->second->state!="dead";
  bool next_alive = next_delta->second->state!="dead";
  
  L(F("note_file_edge %s %d -> %s %d\n") 
    % prev_rcs_version_num % prev_alive
    % next_rcs_version_num % next_alive);

  // we always aggregate in-edges in children, but we will also create
  // parents as we encounter them.
  if (stk.size() == 1)
    {
      // we are on the trunk, prev is child, next is parent.
      L(F("noting trunk edge %s : %s -> %s\n") % curr_file
        % next_rcs_version_num
        % prev_rcs_version_num);
      find_key_and_state (r, next_rcs_version_num, k, s); // just to create it if necessary      
      find_key_and_state (r, prev_rcs_version_num, k, s);

      L(F("trunk edge entering key state %d\n") % k.id);
      s->in_edges.insert(cvs_file_edge(next_version, curr_file, next_alive,
                                       prev_version, curr_file, prev_alive,
                                       *this));
    }
  else
    {
      // we are on a branch, prev is parent, next is child.
      L(F("noting branch edge %s : %s -> %s\n") % curr_file
        % prev_rcs_version_num
        % next_rcs_version_num);
      find_key_and_state (r, next_rcs_version_num, k, s);
      L(F("branch edge on %s entering key state %d\n") 
        % branch_interner.lookup(k.branch) % k.id);
      s->in_edges.insert(cvs_file_edge(prev_version, curr_file, prev_alive,
                                       next_version, curr_file, next_alive,
                                       *this));
    }
    
  ++n_versions;
}

void 
cvs_history::pop_branch() 
{
  I(stk.size() > 1);
  stk.pop();
  bstk.pop();
}


class 
cvs_tree_walker 
  : public tree_walker
{
  cvs_history & cvs;
  database & db;
public:
  cvs_tree_walker(cvs_history & c, database & d) : 
    cvs(c), db(d) 
  {
  }
  virtual void visit_file(file_path const & path)
  {
    string file = path();
    if (file.substr(file.size() - 2) == string(",v"))      
      {
        import_rcs_file_with_cvs(file, db, cvs);
      }
    else
      L(F("skipping non-RCS file %s\n") % file);
  }
  virtual ~cvs_tree_walker() {}
};


static void 
store_manifest_edge(manifest_map const & parent,
                    manifest_map const & child,
                    manifest_id const & parent_mid,
                    manifest_id const & child_mid,
                    app_state & app,
                    cvs_history & cvs,
                    bool head_manifest_p)
{

  L(F("storing manifest %s (base %s)\n") % parent_mid % child_mid);

  if (head_manifest_p)
    {
      L(F("storing head %s\n") % child_mid);
      // a branch has one very important manifest: the head.  this is
      // the "newest" of all manifests within the branch (including
      // the trunk), and we store it in its entirety.
      if (! app.db.manifest_version_exists(child_mid))
        {
          manifest_data mdat;
          write_manifest_map(child, mdat);
          app.db.put_manifest(child_mid, mdat);
        }
    }

  if (null_id(parent_mid))
    {
      L(F("skipping null manifest\n"));
      return;
    }

  unsigned long older, newer;

  older = cvs.manifest_version_interner.intern(parent_mid.inner()());
  newer = cvs.manifest_version_interner.intern(child_mid.inner()());

  if (cvs.manifest_cycle_detector.edge_makes_cycle(older,newer))        
    {

      L(F("skipping cyclical manifest delta %s -> %s\n") 
	% parent_mid % child_mid);
      // we are potentially breaking the chain one would use to get to
      // p. we need to make sure p exists.
      if (!app.db.manifest_version_exists(parent_mid))
	{
	  L(F("writing full manifest %s\n") % parent_mid);
	  manifest_data mdat;
	  write_manifest_map(parent, mdat);
	  app.db.put_manifest(parent_mid, mdat);
	}
      return;
    }
  
  cvs.manifest_cycle_detector.put_edge(older,newer);        

  L(F("storing manifest delta %s -> %s\n") 
    % child_mid % parent_mid);
  
  // the ancestry-based 'child' is a 'new' version as far as the
  // storage system is concerned; that is to say that the
  // ancestry-based 'parent' is a temporally older tree version, which
  // can be constructed from the 'newer' child. so the delta should
  // run from child (new) -> parent (old).
      
  delta del;
  diff(child, parent, del);
  rcs_put_raw_manifest_edge(parent_mid.inner(),
			    child_mid.inner(),
			    del, app.db);
}


static void 
store_auxiliary_certs(cvs_key const & key, 
                      revision_id const & id, 
                      app_state & app, 
                      cvs_history const & cvs)
{
  packet_db_writer dbw(app);
  cert_revision_in_branch(id, cert_value(cvs.branch_interner.lookup(key.branch)), app, dbw); 
  cert_revision_author(id, cvs.author_interner.lookup(key.author), app, dbw); 
  cert_revision_changelog(id, cvs.changelog_interner.lookup(key.changelog), app, dbw);
  cert_revision_date_time(id, key.time, app, dbw);
}

static void 
build_change_set(shared_ptr<cvs_state> state,
                 manifest_map const & state_map,
                 cvs_history & cvs,
                 change_set & cs)
{
  change_set empty;
  cs = empty;

  for (set<cvs_file_edge>::const_iterator f = state->in_edges.begin();
       f != state->in_edges.end(); ++f)
    {
      file_id fid(cvs.file_version_interner.lookup(f->child_version));
      file_path pth(cvs.path_interner.lookup(f->child_path));
      if (!f->child_live_p)
        {  
          if (f->parent_live_p)
            {
              L(F("deleting entry state '%s' on '%s'\n") % fid % pth);              
              cs.delete_file(pth);
            }
          else
            {
              // it can actually happen that we have a file that went from
              // dead to dead.  when a file is created on a branch, cvs first
              // _commits a deleted file_ on mainline, and then branches from
              // it and resurrects it.  In such cases, we should just ignore
              // the file, it doesn't actually exist.  So, in this block, we
              // do nothing.
            }
        }
      else 
        {
          manifest_map::const_iterator i = state_map.find(pth);
          if (i == state_map.end())
            {
              L(F("adding entry state '%s' on '%s'\n") % fid % pth);          
              cs.add_file(pth, fid);          
            }
          else if (manifest_entry_id(i) == fid)
            {
              L(F("skipping preserved entry state '%s' on '%s'\n")
                % fid % pth);         
            }
          else
            {
              L(F("applying state delta on '%s' : '%s' -> '%s'\n") 
                % pth % manifest_entry_id(i) % fid);          
              cs.apply_delta(pth, manifest_entry_id(i), fid);
            }
        }  
    }
  L(F("logical changeset from parent -> child has %d file state changes\n") 
    % state->in_edges.size());
}

static void 
import_branch_states(ticker & n_edges, 
		     cvs_branch & branch,
		     cvs_history & cvs,
		     app_state & app,
		     vector< pair<cvs_key, revision_set> > & revisions)
{
  manifest_map parent_map, child_map;
  manifest_id parent_mid, child_mid;
  revision_id parent_rid, child_rid;
  
  // we look through the branch temporally *backwards* from oldest to
  // newest

  for (cvs_branch::reverse_iterator i = branch.rbegin(); 
       i != branch.rend(); ++i)
    {
      L(F("importing branch %s, state [%d: %s @ %d]\n")
        % cvs.branch_interner.lookup(i->first.branch)
        % i->first.id
        % cvs.author_interner.lookup(i->first.author)
        % i->first.time);

      revision_set rev;
      boost::shared_ptr<change_set> cs(new change_set());
      build_change_set(i->second, parent_map, cvs, *cs);

      apply_change_set(*cs, child_map);
      calculate_ident(child_map, child_mid);

      rev.new_manifest = child_mid;
      rev.edges.insert(make_pair(parent_rid, make_pair(parent_mid, cs)));
      calculate_ident(rev, child_rid);

      revisions.push_back(make_pair(i->first, rev));

      store_manifest_edge(parent_map, child_map, 
                          parent_mid, child_mid, 
                          app, cvs, i->first == branch.begin()->first);

      // now apply same change set to parent_map, making parent_map == child_map
      apply_change_set(*cs, parent_map);
      parent_mid = child_mid;
      parent_rid = child_rid;
      ++n_edges;
    }
}

void 
import_cvs_repo(fs::path const & cvsroot, 
                app_state & app)
{
  N(!fs::exists(cvsroot / "CVSROOT"),
    F("%s appears to be a CVS repository root directory\n"
      "try importing a module instead, with 'cvs_import %s/<module_name>")
    % cvsroot.native_directory_string() % cvsroot.native_directory_string());
  
  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    N(guess_default_key(key,app),
      F("no unique private key for cert construction"));
    require_password(key, app);
  }

  cvs_history cvs;
  N(app.branch_name() != "", F("need base --branch argument for importing"));
  cvs.base_branch = app.branch_name();

  // push the trunk
  cvs.stk.push(shared_ptr<cvs_branch>(new cvs_branch()));  
  cvs.bstk.push(cvs.branch_interner.intern(cvs.base_branch));

  {
    transaction_guard guard(app.db);
    cvs_tree_walker walker(cvs, app.db);
    N( fs::exists(cvsroot),
       F("path %s does not exist") % cvsroot.string());
    N( fs::is_directory(cvsroot),
       F("path %s is not a directory") % cvsroot.string());
    app.db.ensure_open();
    N(chdir(cvsroot.native_directory_string().c_str()) == 0,
      F("could not change directory to %s") % cvsroot.string());
    walk_tree(walker);
    guard.commit();
  }

  P(F("phase 1 (version import) complete\n"));

  I(cvs.stk.size() == 1);

  vector< pair<cvs_key, revision_set> > revisions;
  {
    ticker n_branches("finished branches", "b", 1);
    ticker n_edges("finished edges", "e", 1);
    transaction_guard guard(app.db);
    manifest_map root_manifest;
    manifest_id root_mid;
    revision_id root_rid; 
    

    ui.set_tick_trailer("building trunk");
    import_branch_states(n_edges, *cvs.stk.top(), cvs, app, revisions);

    for(map<string, shared_ptr<cvs_branch> >::const_iterator branch = cvs.branches.begin();
	branch != cvs.branches.end(); ++branch)
      {
	ui.set_tick_trailer("building branch " + branch->first);
	++n_branches;
	import_branch_states(n_edges, *(branch->second), cvs, app, revisions);
      }

    P(F("phase 2 (ancestry reconstruction) complete\n"));
    guard.commit();
  }
  
  {
    ticker n_revisions("written revisions", "r", 1);
    ui.set_tick_trailer("");
    transaction_guard guard(app.db);
    for (vector< pair<cvs_key, revision_set> >::const_iterator
           i = revisions.begin(); i != revisions.end(); ++i)
      {
        revision_id rid;
        calculate_ident(i->second, rid);
        if (! app.db.revision_exists(rid))
          app.db.put_revision(rid, i->second);
        store_auxiliary_certs(i->first, rid, app, cvs);
        ++n_revisions;
      }
    P(F("phase 3 (writing revisions) complete\n"));
    guard.commit();
  }
}

