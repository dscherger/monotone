// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <algorithm>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>
#include <cstdio>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>

#include "app_state.hh"
#include "cert.hh"
#include "constants.hh"
#include "cycle_detector.hh"
#include "database.hh"
#include "file_io.hh"
#include "interner.hh"
#include "keys.hh"
#include "packet.hh"
#include "paths.hh"
#include "platform-wrapped.hh"
#include "rcs_file.hh"
#include "revision.hh"
#include "safe_map.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"

using std::make_pair;
using std::map;
using std::multimap;
using std::out_of_range;
using std::pair;
using std::search;
using std::set;
using std::sscanf;
using std::stable_sort;
using std::stack;
using std::string;
using std::vector;

using boost::scoped_ptr;
using boost::shared_ptr;
using boost::lexical_cast;

// cvs history recording stuff

typedef unsigned long cvs_branchname;
typedef unsigned long cvs_author;
typedef unsigned long cvs_changelog;
typedef unsigned long cvs_version;
typedef unsigned long cvs_path;
typedef unsigned long cvs_tag;

struct cvs_history;

typedef enum event_type
{
  ET_COMMIT,
  ET_BRANCH,
  ET_TAG
} event_type;

struct cvs_branch;

struct cvs_event_digest
{
  unsigned int digest;

  cvs_event_digest(cvs_author a, cvs_changelog c, cvs_tag t,
                   shared_ptr<struct cvs_branch> b)
    : digest(a | c | t | *reinterpret_cast<int*>(b.get())) {}

  bool operator < (const struct cvs_event_digest & other) const
    {
      return digest < other.digest;
    }
};

class
cvs_event
{
public:
  event_type type;
  time_t time;
  cvs_path path;
  shared_ptr<struct cvs_event> dependency;

  cvs_event();
  cvs_event(event_type t);
  virtual ~cvs_event();
  virtual cvs_event_digest get_digest(void) = 0;
};

class
cvs_commit
  : public cvs_event
{
public:
  cvs_author author;
  cvs_changelog changelog;
  cvs_version version;
  string rcs;

  bool alive;
  bool is_synthetic_branch_root;

  cvs_commit();
  cvs_commit(rcs_file const & r,
             string const & rcs_version,
             file_id const & ident,
             cvs_history & cvs);
  virtual cvs_event_digest get_digest(void);
};

class
cvs_event_branch
  : public cvs_event
{
public:
  shared_ptr<struct cvs_branch> branch;
  cvs_path path;

  cvs_event_branch(shared_ptr<cvs_commit> dep);
  virtual cvs_event_digest get_digest(void);
};

class
cvs_event_tag
  : public cvs_event
{
public:
  cvs_tag tag;
  cvs_path path;

  cvs_event_tag(shared_ptr<cvs_commit> dep, const cvs_tag t);
  virtual cvs_event_digest get_digest(void);
};

typedef vector<shared_ptr<cvs_event> > cvs_blob;

struct
cvs_branch
{
  bool has_a_commit;
  bool has_parent_rid;
  revision_id parent_rid;

  // all the blobs
  multimap<cvs_event_digest, vector<shared_ptr<cvs_event> > > blobs;

  cvs_branch()
    : has_a_commit(false),
      has_parent_rid(false)
  {
  }

  void append_event(shared_ptr<cvs_event> c) 
  {
    if (c->type == ET_COMMIT)
      {
        I(c->time != 0);
        has_a_commit = true;
      }

    typedef multimap<cvs_event_digest, cvs_blob>::const_iterator ity;
    pair<ity,ity> range = blobs.equal_range(c->get_digest());

    if (range.first == blobs.end())
      {
        blobs.insert(make_pair(c->get_digest(),
                     vector<shared_ptr<cvs_event> >()));
        range = blobs.equal_range(c->get_digest());
        I(range.first != blobs.end());
      }

    // it's a multimap, but we want only one blob per digest
    // at this time (when filling it)
    I(range.first == range.second);

    cvs_blob blob_events = range.first->second;
    blob_events.push_back(c);
  }
};

struct
cvs_history
{

  interner<unsigned long> branch_interner;
  interner<unsigned long> author_interner;
  interner<unsigned long> changelog_interner;
  interner<unsigned long> file_version_interner;
  interner<unsigned long> path_interner;
  interner<unsigned long> tag_interner;

  // assume admin has foo:X.Y.0.N in it, then
  // this multimap contains entries of the form
  // X.Y   -> foo
  multimap<string, string> branchpoints;

  // and this map contains entries of the form
  // X.Y.N.1 -> foo
  map<string, string> branch_first_entries;

  // branch name -> branch
  map<string, shared_ptr<cvs_branch> > branches;
  shared_ptr<cvs_branch> trunk;

  // stack of branches we're injecting states into
  stack< shared_ptr<cvs_branch> > stk;
  stack< cvs_branchname > bstk;

  // tag -> time, revision
  //
  // used to resolve the *last* revision which has a given tag
  // applied; this is the revision which wins the tag.
  map<unsigned long, pair<time_t, revision_id> > resolved_tags;

  file_path curr_file;
  cvs_path curr_file_interned;

  string base_branch;

  ticker n_versions;
  ticker n_tree_branches;

  cvs_history();
  void set_filename(string const & file,
                    file_id const & ident);

  void index_branchpoint_symbols(rcs_file const & r);

  void push_branch(string const & branch_name, bool private_branch);
  void pop_branch();
};


static bool
is_sbr(shared_ptr<rcs_delta> dl,
       shared_ptr<rcs_deltatext> dt)
{

  // CVS abuses the RCS format a bit (ha!) when storing a file which
  // was only added on a branch: on the root of the branch there'll be
  // a commit with dead state, empty text, and a log message
  // containing the string "file foo was initially added on branch
  // bar". We recognize and ignore these cases, as they do not
  // "really" represent commits to be clustered together.

  if (dl->state != "dead")
    return false;

  if (!dt->text.empty())
    return false;

  string log_bit = "was initially added on branch";
  string::const_iterator i = search(dt->log.begin(),
                                    dt->log.end(),
                                    log_bit.begin(),
                                    log_bit.end());

  return i != dt->log.end();
}

cvs_event::cvs_event(void)
{
}

cvs_event::cvs_event(event_type t)
  : type(t)
{
}

cvs_event::~cvs_event(void)
{
}

cvs_commit::cvs_commit(void)
  : cvs_event(ET_COMMIT)
{
}

cvs_commit::cvs_commit(rcs_file const & r,
                       string const & rcs_version,
                       file_id const & ident,
                       cvs_history & cvs)
{
  type = ET_COMMIT;

  map<string, shared_ptr<rcs_delta> >::const_iterator delta =
    r.deltas.find(rcs_version);
  I(delta != r.deltas.end());

  map<string, shared_ptr<rcs_deltatext> >::const_iterator deltatext =
    r.deltatexts.find(rcs_version);
  I(deltatext != r.deltatexts.end());

  struct tm t;
  // We need to initialize t to all zeros, because strptime has a habit of
  // leaving bits of the data structure alone, letting garbage sneak into
  // our output.
  memset(&t, 0, sizeof(t));
  char const * dp = delta->second->date.c_str();
  L(FL("Calculating time of %s") % dp);
#ifdef HAVE_STRPTIME
  if (strptime(dp, "%y.%m.%d.%H.%M.%S", &t) == NULL)
    I(strptime(dp, "%Y.%m.%d.%H.%M.%S", &t) != NULL);
#else
  I(sscanf(dp, "%d.%d.%d.%d.%d.%d", &(t.tm_year), &(t.tm_mon),
           &(t.tm_mday), &(t.tm_hour), &(t.tm_min), &(t.tm_sec))==6);
  t.tm_mon--;
  // Apparently some RCS files have 2 digit years, others four; tm always
  // wants a 2 (or 3) digit year (years since 1900).
  if (t.tm_year > 1900)
    t.tm_year-=1900;
#endif
  time = mktime(&t);
  L(FL("= %i") % time);

  is_synthetic_branch_root = is_sbr(delta->second,
                                    deltatext->second);

  alive = delta->second->state != "dead";
  if (is_synthetic_branch_root)
    changelog = cvs.changelog_interner.intern("synthetic branch root changelog");
  else
    changelog = cvs.changelog_interner.intern(deltatext->second->log);
  author = cvs.author_interner.intern(delta->second->author);
  path = cvs.curr_file_interned;
  version = cvs.file_version_interner.intern(ident.inner()());
  rcs = string(rcs_version);
}

cvs_event_digest
cvs_commit::get_digest(void)
{
  return cvs_event_digest(author, changelog, 0, shared_ptr<cvs_branch>());
};

cvs_event_digest
cvs_event::get_digest(void)
{
  return cvs_event_digest(0, 0, 0, shared_ptr<cvs_branch>());
};

cvs_event_branch::cvs_event_branch(shared_ptr<cvs_commit> dep)
{
  type = ET_BRANCH;
  path = dep->path;
}

cvs_event_digest
cvs_event_branch::get_digest(void)
{
  return cvs_event_digest(0, 0, 0, branch);
}

cvs_event_tag::cvs_event_tag(shared_ptr<cvs_commit> dep, const cvs_tag t)
{
  type = ET_TAG;
  path = dep->path;
  tag = t;
}

cvs_event_digest
cvs_event_tag::get_digest(void)
{
  return cvs_event_digest(0, 0, tag, shared_ptr<cvs_branch>());
}

// piece table stuff

struct piece;

struct
piece_store
{
  vector< shared_ptr<rcs_deltatext> > texts;
  void index_deltatext(shared_ptr<rcs_deltatext> const & dt,
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
piece_store::index_deltatext(shared_ptr<rcs_deltatext> const & dt,
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
  catch (out_of_range &)
    {
      throw oops("out_of_range while processing " + directive
                 + " with source.size() == "
                 + lexical_cast<string>(source.size())
                 + " and cursor == "
                 + lexical_cast<string>(cursor));
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
      L(FL("skipping identity file edge"));
      return;
    }

  if (db.file_version_exists(old_id))
    {
      // we already have a way to get to this old version,
      // no need to insert another reconstruction path
      L(FL("existing path to %s found, skipping") % old_id);
    }
  else
    {
      I(db.file_or_manifest_base_exists(new_id(), "files")
        || db.delta_exists(new_id(), "file_deltas"));
      db.put_file_delta(file_id(old_id), file_id(new_id), file_delta(del));
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


static void
process_branch(string const & begin_version,
               shared_ptr<cvs_commit> last_commit,
               vector< piece > const & begin_lines,
               data const & begin_data,
               hexenc<id> const & begin_id,
               rcs_file const & r,
               database & db,
               cvs_history & cvs)
{
  shared_ptr<cvs_commit> curr_commit;
  string curr_version = begin_version;
  scoped_ptr< vector< piece > > next_lines(new vector<piece>);
  scoped_ptr< vector< piece > > curr_lines(new vector<piece>
                                           (begin_lines.begin(),
                                            begin_lines.end()));
  data curr_data(begin_data), next_data;
  hexenc<id> curr_id(begin_id), next_id;

  if (last_commit)
    {
      // add a branch event if we have a previous commit
      shared_ptr<cvs_event_branch> branch_event =
        shared_ptr<cvs_event_branch>(new cvs_event_branch(last_commit));
      branch_event->branch = cvs.stk.top();
      cvs.stk.top()->append_event(branch_event);

      L(FL("added branch event for file %s in branch %s")
        % cvs.path_interner.lookup(last_commit->path)
        % cvs.bstk.top());
    }

  while(! (r.deltas.find(curr_version) == r.deltas.end()))
    {
      L(FL("version %s has %d lines") % curr_version % curr_lines->size());

      curr_commit = shared_ptr<cvs_commit>(
        new cvs_commit(r, curr_version, curr_id, cvs));

      if (!curr_commit->is_synthetic_branch_root)
        {
          cvs.stk.top()->append_event(curr_commit);
          ++cvs.n_versions;
        }

      // make the last commit depend on the current one (which
      // comes _before_ due to the reverse processing here)
      if (last_commit != NULL)
        {
          last_commit->dependency = curr_commit;
        }

      // create tag events for all tags on this commit
      typedef multimap<string,string>::const_iterator ity;
      pair<ity,ity> range = r.admin.symbols.equal_range(curr_version);
      for (ity i = range.first; i != range.second; ++i)
        {
          if (i->first == curr_version)
           {
              shared_ptr<cvs_event_tag> event;
              L(FL("version %s -> tag %s") % curr_version % i->second);

              cvs_tag tag = cvs.tag_interner.intern(i->second);
              event = shared_ptr<cvs_event_tag>(
                new cvs_event_tag(curr_commit, tag));
              cvs.stk.top()->append_event(event);
            }
        }

      string next_version = r.deltas.find(curr_version)->second->next;

      if (! next_version.empty())
        {
          L(FL("following RCS edge %s -> %s") % curr_version % next_version);

          construct_version(*curr_lines, next_version, *next_lines, r);
          L(FL("constructed RCS version %s, inserting into database") %
            next_version);

          insert_into_db(curr_data, curr_id,
                         *next_lines, next_data, next_id, db);
        }

      // recursively follow any branch commits coming from the branchpoint
      shared_ptr<rcs_delta> curr_delta = r.deltas.find(curr_version)->second;
      for(vector<string>::const_iterator i = curr_delta->branches.begin();
          i != curr_delta->branches.end(); ++i)
        {
          string branch;
          data branch_data;
          hexenc<id> branch_id;
          vector< piece > branch_lines;
          bool priv = false;
          map<string, string>::const_iterator be = cvs.branch_first_entries.find(*i);

          if (be != cvs.branch_first_entries.end())
            branch = be->second;
          else
            priv = true;

          L(FL("following RCS branch %s = '%s'\n") % (*i) % branch);
          
          construct_version(*curr_lines, *i, branch_lines, r);
          insert_into_db(curr_data, curr_id, 
                         branch_lines, branch_data, branch_id, db);

          cvs.push_branch(branch, priv);
          process_branch(*i, curr_commit, branch_lines, branch_data,
                         branch_id, r, db, cvs);
          cvs.pop_branch();

          L(FL("finished RCS branch %s = '%s'") % (*i) % branch);
        }

      if (!r.deltas.find(curr_version)->second->next.empty())
        {
          // advance
          curr_data = next_data;
          curr_id = next_id;
          curr_version = next_version;
          swap(next_lines, curr_lines);
          next_lines->clear();
          last_commit = curr_commit;
        }
      else break;
    }
}


static void
import_rcs_file_with_cvs(string const & filename, database & db, cvs_history & cvs)
{
  rcs_file r;
  L(FL("parsing RCS file %s") % filename);
  parse_rcs_file(filename, r);
  L(FL("parsed RCS file %s OK") % filename);

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

    global_pieces.reset();
    global_pieces.index_deltatext(r.deltatexts.find(r.admin.head)->second,
                                  head_lines);
    process_branch(r.admin.head, shared_ptr<cvs_commit>(), head_lines, dat, id,
                   r, db, cvs);
    global_pieces.reset();
  }

  ui.set_tick_trailer("");
}


void
test_parse_rcs_file(system_path const & filename, database & db)
{
  cvs_history cvs;

  I(! filename.empty());
  assert_path_is_file(filename);

  P(F("parsing RCS file %s") % filename);
  rcs_file r;
  parse_rcs_file(filename.as_external(), r);
  P(F("parsed RCS file %s OK") % filename);
}


// CVS importing stuff follows


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

cvs_history::cvs_history() :
  n_versions("versions", "v", 1),
  n_tree_branches("branches", "b", 1)
{
}

void
cvs_history::set_filename(string const & file,
                          file_id const & ident)
{
  L(FL("importing file '%s'") % file);
  I(file.size() > 2);
  I(file.substr(file.size() - 2) == string(",v"));
  string ss = file;
  ui.set_tick_trailer(ss);
  ss.resize(ss.size() - 2);
  // remove Attic/ if present
  string::size_type last_slash=ss.rfind('/');
  if (last_slash!=string::npos && last_slash>=5
        && ss.substr(last_slash-5,6)=="Attic/")
     ss.erase(last_slash-5,6);
  curr_file = file_path_internal(ss);
  curr_file_interned = path_interner.intern(ss);
}

void cvs_history::index_branchpoint_symbols(rcs_file const & r)
{
  branchpoints.clear();
  branch_first_entries.clear();

  for (multimap<string, string>::const_iterator i =
         r.admin.symbols.begin(); i != r.admin.symbols.end(); ++i)
    {
      string const & num = i->first;
      string const & sym = i->second;

      vector<string> components;
      split_version(num, components);

      vector<string> first_entry_components;
      vector<string> branchpoint_components;

      if (components.size() > 2 &&
          (components.size() % 2 == 1))
        {
          // this is a "vendor" branch
          //
          // such as "1.1.1", where "1.1" is the branchpoint and
          // "1.1.1.1" will be the first commit on it.
          
          first_entry_components = components;
          first_entry_components.push_back("1");

          branchpoint_components = components;
          branchpoint_components.erase(branchpoint_components.end() - 1,
                                       branchpoint_components.end());

        }

      else if (components.size() > 2 &&
               (components.size() % 2 == 0) &&
               components[components.size() - 2] == string("0"))
        {
          // this is a "normal" branch
          //
          // such as "1.3.0.2", where "1.3" is the branchpoint and
          // "1.3.2.1"

          first_entry_components = components;
          first_entry_components[first_entry_components.size() - 2]
            = first_entry_components[first_entry_components.size() - 1];
          first_entry_components[first_entry_components.size() - 1]
            = string("1");

          branchpoint_components = components;
          branchpoint_components.erase(branchpoint_components.end() - 2,
                                       branchpoint_components.end());
        }

      string first_entry_version;
      join_version(first_entry_components, first_entry_version);

      L(FL("first version in branch %s would be %s\n") 
        % sym % first_entry_version);
      branch_first_entries.insert(make_pair(first_entry_version, sym));

      string branchpoint_version;
      join_version(branchpoint_components, branchpoint_version);

      L(FL("file branchpoint for %s at %s\n") % sym % branchpoint_version);
      branchpoints.insert(make_pair(branchpoint_version, sym));
    }
}



void
cvs_history::push_branch(string const & branch_name, bool private_branch)
{
  shared_ptr<cvs_branch> branch;

  string bname = base_branch + "." + branch_name;
  I(stk.size() > 0);

  if (private_branch)
    {
      branch = shared_ptr<cvs_branch>(new cvs_branch());
      stk.push(branch);
      bstk.push(branch_interner.intern(""));
      return;
    }
  else
    {
      map<string, shared_ptr<cvs_branch> >::const_iterator b = branches.find(bname);
      if (b == branches.end())
        {
          branch = shared_ptr<cvs_branch>(new cvs_branch());
          branches.insert(make_pair(bname, branch));
          ++n_tree_branches;
        }
      else
        branch = b->second;

      stk.push(branch);
      bstk.push(branch_interner.intern(bname));
    }
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
    string file = path.as_external();
    if (file.substr(file.size() - 2) == string(",v"))
      {
        try
          {
            import_rcs_file_with_cvs(file, db, cvs);
          }
        catch (oops const & o)
          {
            W(F("error reading RCS file %s: %s") % file % o.what());
          }
      }
    else
      L(FL("skipping non-RCS file %s") % file);
  }
  virtual ~cvs_tree_walker() {}
};



//
// After stuffing all cvs_events into blobs of events with the same
// author and changelog, we have to make sure their dependencies are
// respected.
//
void
resolve_blob_dependencies(cvs_history &cvs,
                          string const & branchname,
                          shared_ptr<cvs_branch> const & branch,
                          ticker & n_blobs)
{
  // first try to resolve all intra-blob dependencies
  typedef multimap<cvs_event_digest, cvs_blob>::const_iterator ity;
  for (ity i = branch->blobs.begin(); i != branch->blobs.end(); ++i)
    {
      cvs_blob blob = i->second;
    }
}




//
// our task here is to produce a sequence of revision descriptions
// from the per-file commit records we have. we do this by rolling
// forwards through the temporally sorted file-commit list
// accumulating file-commits into revisions and flushing the
// revisions when we feel they are "complete".
//
// revisions have to have a time associated with them. this time
// will be the first time of any commit associated with the
// revision. they have an author and a changelog, which is shared
// by all the file-commits in the revision.
//
// there might be multiple revisions overlapping in time. this is
// legal wrt. CVS. we keep a set, and search all members of the set
// for the best match.
//
// consider this situation of overlapping revisions:
//
//    +---------------+   +---------------+   +---------------+
//    | rev #1 @ 0011 |   | rev #2 @ 0012 |   | rev #3 @ 0013 |
//    |~~~~~~~~~~~~~~~|   |~~~~~~~~~~~~~~~|   |~~~~~~~~~~~~~~~|
//    | patch foo.txt |   | patch bar.txt |   | patch baz.txt |
//    +---------------+   +---------------+   +---------------+
//
// suppose you have this situation and you run across a "patch
// bar.txt" commit at timestamp 0014. what do you do?
//
// - you know that rev #2 cannot accept this commit, simply because
//   two commits on the same file makes *two* revisions, not one.
//
// - perhaps rev #3 could accept it; after all, it could be that the
//   commit associated with rev #2 released its commit lock, and the
//   commit associated with rev #3 quickly updated and committed at
//   0013, finishing off at 0014.
//
// - can rev #1 accept it? no. because CVS calcualted the version it
//   expected to see in bar.txt before calling up the server, when
//   committing rev #1. the version it expected to see was the version
//   in bar.txt *before* time 0012; that is, before rev #2 had any affect
//   on bar.txt. when it contacted the server, the commit associated
//   with rev #1 would have aborted if it had seen any other number.
//   so rev #1 could not start before an edit to bar.txt and then
//   include its own edit to bar.txt.
//
// so we have only one case where bar.txt can be accepted. if the
// commit is not accepted into a legal rev (outside the window,
// wrong changelog/author) it starts a new revision.
//
// as we scan forwards, if we hit timestamps which lie beyond rev #n's
// window, we flush rev #n.
//
// if there are multiple coincident and legal revs to direct a
// commit to (all with the same author/changelog), we direct the
// commit to the rev with the closest initial timestamp. that is,
// the *latest* beginning time.

struct
cvs_cluster
{
  event_type type;
  time_t start_time;
  time_t end_time;
  cvs_author author;
  cvs_changelog changelog;
  set<cvs_tag> tags;
  shared_ptr<cvs_branch> branch;

  cvs_cluster(event_type ty,
              time_t t, 
              cvs_author a,
              cvs_changelog c)
    : type(ty),
      start_time(t),
      end_time(t),
      author(a),
      changelog(c)
  {}

  struct entry
  {
    bool live;
    cvs_version version;
    time_t time;
    entry(bool l, cvs_version v, time_t t)
      : live(l),
        version(v),
        time(t)
    {}
  };

  typedef map<cvs_path, entry> entry_map;
  entry_map entries;
};


struct
cluster_consumer
{
  cvs_history & cvs;
  app_state & app;
  string const & branchname;
  cvs_branch const & branch;
  set<split_path> created_dirs;
  map<cvs_path, cvs_version> live_files;
  ticker & n_revisions;

  struct prepared_revision
  {
    prepared_revision(revision_id i,
                      shared_ptr<revision_t> r,
                      cvs_cluster const & c);
    revision_id rid;
    shared_ptr<revision_t> rev;
    time_t time;
    cvs_author author;
    cvs_changelog changelog;
    vector<cvs_tag> tags;
  };

  vector<prepared_revision> preps;

  roster_t ros;
  temp_node_id_source nis;
  editable_roster_base editable_ros;
  revision_id parent_rid, child_rid;

  cluster_consumer(cvs_history & cvs,
                   app_state & app,
                   string const & branchname,
                   cvs_branch const & branch,
                   ticker & n_revs);

  void consume_cluster(cvs_cluster const & c);
  void add_missing_parents(split_path const & sp, cset & cs);
  void build_cset(cvs_cluster const & c, cset & cs);
  void store_auxiliary_certs(prepared_revision const & p);
  void store_revisions();
};

typedef shared_ptr<cvs_cluster>
cluster_ptr;

struct
cluster_ptr_lt
{
  bool operator()(cluster_ptr const & a,
                  cluster_ptr const & b) const
  {
    return a->end_time < b->start_time;
  }
};

typedef set<cluster_ptr, cluster_ptr_lt>
cluster_set;

void
import_branch(cvs_history & cvs,
              app_state & app,
              string const & branchname,
              shared_ptr<cvs_branch> const & branch,
              ticker & n_revs)
{
  cluster_set clusters;
  cluster_consumer cons(cvs, app, branchname, *branch, n_revs);
//  unsigned long commits_remaining = branch->lineage.size();

#if 0
  /* sort the branch lineage */
  L(FL("sorting lineage of branch %s\n") % branchname);
  stable_sort(cvs.trunk->lineage.begin(), cvs.trunk->lineage.end());

  for (vector<cvs_event>::iterator i = branch->lineage.begin();
       i != branch->lineage.end(); ++i)
    {
      commits_remaining--;

      if (i->type == ET_COMMIT)
        {
          L(FL("examining next event: commit [t:%d] [p:%s] [a:%s] [c:%s]")
            % i->time 
            % cvs.path_interner.lookup(i->path)
            % cvs.author_interner.lookup(i->author)
            % cvs.changelog_interner.lookup(i->changelog));
        }
      else
        {
          // correct the cvs_event time of the branch event
          i->time = i->branch->branch_time;
          L(FL("examining next event: branch [t:%d] [p:%s]")
            % i->time
            % cvs.path_interner.lookup(i->path));
        }
      
      // step 2: expire all clusters from the beginning of the set which
      // have passed the window size
      while (!clusters.empty())
        {
          cluster_set::iterator j = clusters.begin();
          if ((*j)->end_time + constants::cvs_window < i->time)
            {
              L(FL("expiring cluster"));
              cons.consume_cluster(**j);
              clusters.erase(j);
            }
          else
            break;
        }

      // step 3: find the last still-live cluster to have touched this
      // file
      // (ms) the current event needs to be inserted into a a cluster
      // following (i.e. newer than) this cluster. Regardless if it's a
      // commit or branch event.
      time_t time_of_last_cluster_touching_this_file = 0;

      unsigned clu = 0;
      for (cluster_set::const_iterator j = clusters.begin();
           j != clusters.end(); ++j)
        {
          L(FL("examining cluster %d to see if it touched %s")
            % clu++
            % cvs.path_interner.lookup(i->path));

          cvs_cluster::entry_map::const_iterator k = (*j)->entries.find(i->path);
          if ((k != (*j)->entries.end())
              && (k->second.time > time_of_last_cluster_touching_this_file))
            {
              L(FL("found cluster touching %s: [t:%d-%d] [a:%d] [c:%d]")
                % cvs.path_interner.lookup(i->path)
                % (*j)->start_time
                % (*j)->end_time
                % (*j)->author
                % (*j)->changelog);
              time_of_last_cluster_touching_this_file = (*j)->end_time;
            }
        }
      L(FL("last modification time is %d")
        % time_of_last_cluster_touching_this_file);

      cluster_ptr target;
      if (i->type == ET_COMMIT)
        {
          // step 4: find a commit cluster which starts on or after the
          // last_modify_time, which doesn't modify the file in question,
          // and which contains the same author and changelog as our
          // commit
          for (cluster_set::const_iterator j = clusters.begin();
               j != clusters.end(); ++j)
            {
              if (((*j)->start_time >= time_of_last_cluster_touching_this_file)
                  && ((*j)->type == ET_COMMIT)
                  && ((*j)->author == i->author)
                  && ((*j)->changelog == i->changelog)
                  && ((*j)->entries.find(i->path) == (*j)->entries.end()))
                {
                  L(FL("picked existing cluster (commit) [t:%d-%d] [a:%d] [c:%d]")
                    % (*j)->start_time
                    % (*j)->end_time
                    % (*j)->author
                    % (*j)->changelog);

                  target = (*j);
                }
            }
        }
      else if (i->type == ET_BRANCH)
        {
          // step 4: find the branchpoint cluster for the branch in question
          for (cluster_set::const_iterator j = clusters.begin();
               j != clusters.end(); ++j)
            {
              if (((*j)->type == ET_BRANCH)
                  // && ((*j)->branch == i->branch)
                  && ((*j)->entries.find(i->path) == (*j)->entries.end()))
                {              
                  L(FL("picked existing cluster (branchpoint) [t:%d-%d]")
                    % (*j)->start_time
                    % (*j)->end_time);

                  target = (*j);
                }
            }
        }
  
      // if we're still not finding an active cluster,
      // this is probably the first commit in it. make
      // a new one.
      if (!target)
        {
          if (i->type == ET_COMMIT)
            {
              L(FL("building new cluster: commit [t:%d] [a:%d] [c:%d]")
                % i->time
                % i->author
                % i->changelog);

              target = cluster_ptr(new cvs_cluster(ET_COMMIT,
                                                   i->time,
                                                   i->author,
                                                   i->changelog));
            }
          else
            {
              L(FL("building new cluster: branch [t:%d]")
                % i->time);

              target = cluster_ptr(new cvs_cluster(ET_BRANCH,
                                                   i->time,
                                                   i->author,
                                                   i->changelog));
              target->branch = i->branch;
            }

          clusters.insert(target);
        }

      I(target);
      target->entries.insert(make_pair(i->path,
                                       cvs_cluster::entry(i->alive,
                                                          i->version,
                                                          i->time)));
      /* update the target's end time */
      if (target->end_time < i->time)
        target->end_time = i->time;

      for (vector<cvs_tag>::const_iterator j = i->tags.begin();
           j != i->tags.end(); ++j)
        {
          target->tags.insert(*j);
        }
    }


  // now we are done this lineage; flush all remaining clusters
  L(FL("finished branch commits, writing all pending clusters"));
  while (!clusters.empty())
    {
      cons.consume_cluster(**clusters.begin());
      clusters.erase(clusters.begin());
    }
  L(FL("finished writing pending clusters"));

  cons.store_revisions();
#endif
}

void
import_cvs_repo(system_path const & cvsroot,
                app_state & app)
{
  N(!directory_exists(cvsroot / "CVSROOT"),
    F("%s appears to be a CVS repository root directory\n"
      "try importing a module instead, with 'cvs_import %s/<module_name>")
    % cvsroot % cvsroot);

  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    get_user_key(key, app);
    require_password(key, app);
  }

  cvs_history cvs;
  N(app.branch_name() != "", F("need base --branch argument for importing"));
  cvs.base_branch = app.branch_name();

  // push the trunk
  cvs.trunk = shared_ptr<cvs_branch>(new cvs_branch());
  cvs.stk.push(cvs.trunk);
  cvs.bstk.push(cvs.branch_interner.intern(cvs.base_branch));

  {
    transaction_guard guard(app.db);
    cvs_tree_walker walker(cvs, app.db);
    require_path_is_directory(cvsroot,
                              F("path %s does not exist") % cvsroot,
                              F("'%s' is not a directory") % cvsroot);
    app.db.ensure_open();
    change_current_working_dir(cvsroot);
    walk_tree(file_path(), walker);
    guard.commit();
  }

  I(cvs.stk.size() == 1);

  ticker n_blobs(_("blobs"), "b", 1);
  resolve_blob_dependencies(cvs, cvs.base_branch, cvs.trunk, n_blobs);


  ticker n_revs(_("revisions"), "r", 1);

#if 0
  for(map<string, shared_ptr<cvs_branch> >::const_iterator i = cvs.branches.begin();
          i != cvs.branches.end(); ++i)
    {
      string branchname = i->first;
      shared_ptr<cvs_branch> branch = i->second;
    }
#endif

  {
    transaction_guard guard(app.db);
//    L(FL("trunk has %d entries") % cvs.trunk->lineage.size());
    import_branch(cvs, app, cvs.base_branch, cvs.trunk, n_revs);
    guard.commit();
  }


/*

  // check branch times
  for(map<string, shared_ptr<cvs_branch> >::const_iterator i = cvs.branches.begin();
          i != cvs.branches.end(); ++i)
    {
      string branchname = i->first;
      shared_ptr<cvs_branch> branch = i->second;
      L(FL("branch %s has %d entries\n") % branchname % branch->lineage.size());

      L(FL("checking branch time of branch %s") % branchname);

      time_t branched_before = 0;
      if (branch->has_a_commit)
        branched_before = branch->first_commit;

      if (branch->first_commit_after_branching > 0)
        if ((branch->first_commit_after_branching < branched_before)
            || (branched_before == 0))
          {
            branched_before = branch->first_commit_after_branching;
          }

      if (branch->last_branchpoint < branched_before)
        {
          branch->branch_time = branch->last_branchpoint +
            ((branched_before - branch->last_branchpoint) / 2);
          L(FL("guessing branchpoint time for branch %s: %d")
               % branchname
               % branch->branch_time);
        }
      else
        {
          L(FL("unable to find a branchpoint time for branch %s") % branchname);
          L(FL("last branchpoint:             %d)") % branch->last_branchpoint);
          L(FL("first commit in branch:       %d)") % branch->first_commit);
          L(FL("first commit after branching: %d)") % branch->first_commit_after_branching);
        }
    }

  // First, sort the lineages of the trunk and all branches. Thanks to the
  // logic in the 'operator<' this correctly handles branch events, which
  // have their time stored in branch->branch_time.
  // This is updated in import_branch later on.
  L(FL("sorting lineage of trunk\n"));
  stable_sort(cvs.trunk->lineage.begin(), cvs.trunk->lineage.end());
  for(map<string, shared_ptr<cvs_branch> >::const_iterator i = cvs.branches.begin();
          i != cvs.branches.end(); ++i)
    {
      string branchname = i->first;
      shared_ptr<cvs_branch> branch = i->second;

      L(FL("sorting lineage of branch %s") % branchname);
      stable_sort(branch->lineage.begin(), branch->lineage.end());
    }

  // import trunk first
  {
    transaction_guard guard(app.db);
    L(FL("trunk has %d entries") % cvs.trunk->lineage.size());
    import_branch(cvs, app, cvs.base_branch, cvs.trunk, n_revs);
    guard.commit();
  }

  while (cvs.branches.size() > 0)
    {
      transaction_guard guard(app.db);
      map<string, shared_ptr<cvs_branch> >::const_iterator i;
      shared_ptr<cvs_branch> branch;

      // import branches in the correct order
      for (i = cvs.branches.begin(); i != cvs.branches.end(); ++i)
        {
          branch = i->second;
          if (branch->has_parent_rid) break;
        }

      if (i == cvs.branches.end())
        {
          L(FL("no more connected branches... unconnected import"));
          i = cvs.branches.begin();
          branch = i->second;
        }

      string branchname = i->first;
      
      L(FL("branch %s has %d entries\n") % branchname % branch->lineage.size());
      import_branch(cvs, app, branchname, branch, n_revs);

      // free up some memory
      cvs.branches.erase(branchname); 
      guard.commit();
    }

  // now we have a "last" rev for each tag
  {
    ticker n_tags(_("tags"), "t", 1);
    packet_db_writer dbw(app);
    transaction_guard guard(app.db);
    for (map<unsigned long, pair<time_t, revision_id> >::const_iterator i = cvs.resolved_tags.begin();
         i != cvs.resolved_tags.end(); ++i)
      {
        string tag = cvs.tag_interner.lookup(i->first);
        ui.set_tick_trailer("marking tag " + tag);
        cert_revision_tag(i->second.second, tag, app, dbw);
        ++n_tags;
      }
    guard.commit();
  }
*/


  return;
}

cluster_consumer::cluster_consumer(cvs_history & cvs,
                                   app_state & app,
                                   string const & branchname,
                                   cvs_branch const & branch,
                                   ticker & n_revs)
  : cvs(cvs),
    app(app),
    branchname(branchname),
    branch(branch),
    n_revisions(n_revs),
    editable_ros(ros, nis)
{
  if (branch.has_parent_rid)
    {
      parent_rid = branch.parent_rid;
      app.db.get_roster(parent_rid, ros);

      L(FL("starting cluster for branch %s from revision %s which contains:")
           % branchname
           % branch.parent_rid);

      // populate the cluster_consumer's live_files and created_dirs according
      // to the roster.
      node_map nodes = ros.all_nodes();
      for (node_map::iterator i = nodes.begin(); i != nodes.end(); ++i)
        {
          shared_ptr<node> node = i->second;

          if (is_dir_t(node))
            {
              split_path dir;

              ros.get_name(node->self, dir);
              L(FL("   dir:  %s") % dir);
              safe_insert(created_dirs, dir);
            }
          else if (is_file_t(node))
            {
              std::string rev;
              std::string name;
              cvs_path path;
              split_path sp;

              ros.get_name(node->self, sp);
              file_path fp(sp);
              path = cvs.path_interner.intern(fp.as_internal());

              dump(downcast_to_file_t(node)->content, rev);

              L(FL("   file: %s at revision %s") % fp.as_internal() % rev);
              live_files[path] = cvs.file_version_interner.intern(rev);
            }
        }
    }

#if 0
  if ((!branch.live_at_beginning.empty()) && (
      /*
       * We insert a special 'beginning of branch' commit if we eigther
       * have not found a parent revision or...
       */
      (!branch.has_parent_rid) ||
      /*
       * ..if we found one, but the branch remained empty.
       */
      (branch.has_parent_rid && (!branch.has_a_commit))))
    {
      cvs_author synthetic_author = 
        cvs.author_interner.intern("cvs_import");

      cvs_changelog synthetic_cl =
        cvs.changelog_interner.intern("beginning of branch " 
                                      + branchname);

      time_t synthetic_time = branch.beginning();
      cvs_cluster initial_cluster(ET_COMMIT,
                                  synthetic_time,
                                  synthetic_author,
                                  synthetic_cl);

      L(FL("initial cluster on branch %s has %d live entries") % 
        branchname % branch.live_at_beginning.size());

      for (map<cvs_path, cvs_version>::const_iterator i = branch.live_at_beginning.begin();
           i != branch.live_at_beginning.end(); ++i)
        {
          cvs_cluster::entry e(true, i->second, synthetic_time);
          L(FL("initial cluster contains %s at %s") %
            cvs.path_interner.lookup(i->first) %
            cvs.file_version_interner.lookup(i->second));
          initial_cluster.entries.insert(make_pair(i->first, e));
        }
      consume_cluster(initial_cluster);
    }
  #endif
}

cluster_consumer::prepared_revision::prepared_revision(revision_id i, 
                                                       shared_ptr<revision_t> r,
                                                       cvs_cluster const & c)
  : rid(i),
    rev(r),
    time(c.start_time),
    author(c.author),
    changelog(c.changelog)
{
  for (set<cvs_tag>::const_iterator i = c.tags.begin();
       i != c.tags.end(); ++i)
    {
      tags.push_back(*i);
    }
}


void
cluster_consumer::store_revisions()
{
  for (vector<prepared_revision>::const_iterator i = preps.begin();
       i != preps.end(); ++i)
    {
      if (! app.db.revision_exists(i->rid))
        {
          data tmp;
          write_revision(*(i->rev), tmp);
          app.db.put_revision(i->rid, *(i->rev));
          store_auxiliary_certs(*i);
          ++n_revisions;
        }
    }
}

void
cluster_consumer::store_auxiliary_certs(prepared_revision const & p)
{
  packet_db_writer dbw(app);

  for (vector<cvs_tag>::const_iterator i = p.tags.begin();
       i != p.tags.end(); ++i)
    {
      map<unsigned long, pair<time_t, revision_id> >::const_iterator j
        = cvs.resolved_tags.find(*i);

      if (j != cvs.resolved_tags.end())
        {
          if (j->second.first < p.time)
            {
              // move the tag forwards
              cvs.resolved_tags.erase(*i);
              cvs.resolved_tags.insert(make_pair(*i, make_pair(p.time, p.rid)));
            }
        }
      else
        {
          cvs.resolved_tags.insert(make_pair(*i, make_pair(p.time, p.rid)));
        }
    }

  cert_revision_in_branch(p.rid, cert_value(branchname), app, dbw);
  cert_revision_author(p.rid, cvs.author_interner.lookup(p.author), app, dbw);
  cert_revision_changelog(p.rid, cvs.changelog_interner.lookup(p.changelog), app, dbw);
  cert_revision_date_time(p.rid, p.time, app, dbw);
}

void
cluster_consumer::add_missing_parents(split_path const & sp, cset & cs)
{
  split_path tmp(sp);
  if (tmp.empty())
    return;
  tmp.pop_back();
  while (!tmp.empty())
    {
      if (created_dirs.find(tmp) == created_dirs.end())
        {
          safe_insert(created_dirs, tmp);
          safe_insert(cs.dirs_added, tmp);
        }
      tmp.pop_back();
    }
}

void
cluster_consumer::build_cset(cvs_cluster const & c,
                             cset & cs)
{
  for (cvs_cluster::entry_map::const_iterator i = c.entries.begin();
       i != c.entries.end(); ++i)
    {
      file_path pth = file_path_internal(cvs.path_interner.lookup(i->first));
      split_path sp;
      pth.split(sp);

      file_id fid(cvs.file_version_interner.lookup(i->second.version));
      if (i->second.live)
        {
          map<cvs_path, cvs_version>::const_iterator e = live_files.find(i->first);
          if (e == live_files.end())
            {
              add_missing_parents(sp, cs);
              L(FL("adding entry state '%s' on '%s'") % fid % pth);
              safe_insert(cs.files_added, make_pair(sp, fid));
              live_files[i->first] = i->second.version;
            }
          else if (e->second != i->second.version)
            {
              file_id old_fid(cvs.file_version_interner.lookup(e->second));
              L(FL("applying state delta on '%s' : '%s' -> '%s'")
                % pth % old_fid % fid);
              safe_insert(cs.deltas_applied,
                          make_pair(sp, make_pair(old_fid, fid)));
              live_files[i->first] = i->second.version;
            }
        }
      else
        {
          map<cvs_path, cvs_version>::const_iterator e = live_files.find(i->first);
          if (e != live_files.end())
            {
              L(FL("deleting entry state '%s' on '%s'") % fid % pth);
              safe_insert(cs.nodes_deleted, sp);
              live_files.erase(i->first);
            }
        }
    }
}

void
cluster_consumer::consume_cluster(cvs_cluster const & c)
{
  if (c.type == ET_COMMIT)
    {
      // we should never have an empty cluster; it's *possible* to have
      // an empty changeset (say on a vendor import) but every cluster
      // should have been created by at least one file commit, even
      // if the commit made no changes. it's a logical inconsistency if
      // you have an empty cluster.
      I(!c.entries.empty());

      shared_ptr<revision_t> rev(new revision_t());
      shared_ptr<cset> cs(new cset());
      build_cset(c, *cs);

      cs->apply_to(editable_ros);
      manifest_id child_mid;
      calculate_ident(ros, child_mid);
  rev->made_for = made_for_database;
      rev->new_manifest = child_mid;
      rev->edges.insert(make_pair(parent_rid, cs));
      calculate_ident(*rev, child_rid);

      preps.push_back(prepared_revision(child_rid, rev, c));

      parent_rid = child_rid;
    }
  else if (c.type == ET_BRANCH)
    {
      /* set the parent revision id of the branch */
      L(FL("setting the parent revision id of a branch"));
      c.branch->parent_rid = parent_rid;
      c.branch->has_parent_rid = true;
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
