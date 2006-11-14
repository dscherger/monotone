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

#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>
#include <boost/graph/graph_traits.hpp>

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
typedef unsigned long cvs_authorclog;
typedef unsigned long cvs_mtn_version;   // the new file id in monotone
typedef unsigned long cvs_rcs_version;   // the old RCS version number
typedef unsigned long cvs_path;
typedef unsigned long cvs_tag;

typedef enum
{
  ET_COMMIT = 0,
  ET_TAG = 2,
  ET_BRANCH = 3
} event_type;

struct cvs_history;
struct cvs_branch;

struct cvs_event_digest
{
  u32 digest;

  cvs_event_digest(const event_type t, const unsigned int v)
    {
      I(sizeof(struct cvs_event_digest) == 4);

      I(v < ((u32) 1 << 30));
      I(t < 4);
      digest = t << 30 | v;
    }

  cvs_event_digest(const cvs_event_digest & d)
    : digest(d.digest)
    { };

  bool operator < (const struct cvs_event_digest & other) const
    {
      return digest < other.digest;
    }

  bool operator == (const struct cvs_event_digest & other) const
    {
      return digest == other.digest;
    }

  bool is_commit() const
    {
      return digest >> 30 <= 1;
    }

  bool is_tag() const
    {
      return digest >> 30 == 2;
    }

  bool is_branch() const
    {
      return digest >> 30 == 3;
    }
};

std::ostream & operator<<(std::ostream & o, struct cvs_event_digest const & d)
{
  return o << d.digest;
}

class cvs_event;

class
cvs_event_ptr
  : public shared_ptr< cvs_event >
{
public:

  cvs_event_ptr(void)
    : shared_ptr< cvs_event >()
    { }

  cvs_event_ptr(const shared_ptr< cvs_event > & p)
    : shared_ptr< cvs_event >(p)
    { }

  bool operator < (const cvs_event_ptr & c) const;
};

class
cvs_event
{
public:
  time_t time;
  cvs_path path;
  vector< cvs_event_ptr > dependencies;

  cvs_event(const cvs_path p, const time_t ti)
    : time(ti),
      path(p)
    { };

  cvs_event(const cvs_event_ptr dep)
    : time(dep->time),
      path(dep->path)
    {
      dependencies.push_back(dep);
    };

  virtual ~cvs_event() { };
  virtual cvs_event_digest get_digest(void) const = 0;

  const bool operator < (const cvs_event & e) const
    {
      return time < e.time;
    }
};

bool
cvs_event_ptr::operator < (const cvs_event_ptr & c) const
{
  return ((*this)->time < c->time);
};

class
cvs_commit
  : public cvs_event
{
public:
  cvs_authorclog authorclog;
  cvs_mtn_version mtn_version;
  cvs_rcs_version rcs_version;
  bool alive;

  cvs_commit(const cvs_path p, const time_t ti, const cvs_mtn_version v,
             const cvs_rcs_version r, const cvs_authorclog ac,
             const bool al)
    : cvs_event(p, ti),
      authorclog(ac),
      mtn_version(v),
      rcs_version(r),
      alive(al)
    { }

  virtual cvs_event_digest get_digest(void) const
    {
      return cvs_event_digest(ET_COMMIT, authorclog);
    };
};

class
cvs_event_branch
  : public cvs_event
{
public:
  shared_ptr<struct cvs_branch> branch;

  cvs_event_branch(const cvs_event_ptr dep, shared_ptr<struct cvs_branch> b)
    : cvs_event(dep),
      branch(b)
    { };

  virtual cvs_event_digest get_digest(void) const;
};

class
cvs_event_tag
  : public cvs_event
{
public:
  cvs_tag tag;

  cvs_event_tag(const cvs_event_ptr dep, const cvs_tag t)
    : cvs_event(dep),
      tag(t)
    { };

  virtual cvs_event_digest get_digest(void) const
    {
      return cvs_event_digest(ET_TAG, tag);
    };
};

typedef vector< cvs_event_ptr >::const_iterator blob_event_iter;
typedef vector< cvs_event_ptr >::const_iterator dependency_iter;

class
cvs_blob
{
private:
  cvs_event_digest digest;
  vector< cvs_event_ptr > events;

public:
  cvs_blob(const cvs_event_digest d)
    : digest(d)
    { };

  cvs_blob(const cvs_blob & b)
    : digest(b.digest),
      events(b.events)
    { };

  void push_back(cvs_event_ptr c)
    {
      I(digest == c->get_digest());
      events.push_back(c);
    }

  vector< cvs_event_ptr > & get_events()
    {
      return events;
    }

  blob_event_iter & begin() const
    {
      return *(new blob_event_iter(events.begin()));
    }

  blob_event_iter & end() const
    {
      // blob_event_iter i = events.end();
      return *(new blob_event_iter(events.end()));
    }

  bool empty() const
    {
      return events.empty();
    }

  const cvs_event_digest get_digest() const
    {
      return digest;
    }
};

typedef vector<cvs_blob>::size_type cvs_blob_index;
typedef multimap<cvs_event_digest, cvs_blob_index>::iterator blob_index_iterator;

struct
cvs_branch
{
  bool has_a_commit;
  revision_id parent_rid;
  cvs_branchname branchname;
  shared_ptr< cvs_branch > parent_branch;

  // all the blobs
  vector<cvs_blob> blobs;

  // to lookup blobs by their event_digest
  multimap<cvs_event_digest, cvs_blob_index> blob_index;

  cvs_branch(cvs_branchname name)
    : has_a_commit(false),
      branchname(name)
  {
  }

  blob_index_iterator add_blob(const cvs_event_digest d)
  {
    // add a blob..
    cvs_blob_index i = blobs.size();
    blobs.push_back(cvs_blob(d));

    // ..and an index entry for the blob
    blob_index_iterator j = blob_index.insert(make_pair(d, i));
    return j;
  }

  blob_index_iterator get_blob(const cvs_event_digest d, bool create)
  {
    pair<blob_index_iterator, blob_index_iterator> range = 
      blob_index.equal_range(d);

    if ((range.first == range.second) && create)
      return add_blob(d);

    // it's a multimap, but we want only one blob per digest
    // at this time (when filling it)
    I(range.first != range.second);
    return range.first;
  }

  cvs_blob_index append_event(cvs_event_ptr c) 
  {
    if (c->get_digest().is_commit())
      {
        I(c->time != 0);
        has_a_commit = true;
      }

    blob_index_iterator b = get_blob(c->get_digest(), true);
    blobs[b->second].push_back(c);
    return b->second;
  }
};

cvs_event_digest
cvs_event_branch::get_digest(void) const
{
  return cvs_event_digest(ET_BRANCH, branch->branchname);
};

struct
cvs_history
{
  interner<unsigned long> branchname_interner;
  interner<unsigned long> authorclog_interner;
  interner<unsigned long> mtn_version_interner;
  interner<unsigned long> rcs_version_interner;
  interner<unsigned long> path_interner;
  interner<unsigned long> tag_interner;

  // assume admin has foo:X.Y.0.N in it, then
  // this map contains entries of the form
  // X.Y.N.1 -> foo
  map<string, string> branch_first_entries;

  // branch name -> branch
  map<cvs_branchname, shared_ptr<cvs_branch> > branches;
  shared_ptr<cvs_branch> trunk;

  // store a list of possible parents for every symbol
  map< cvs_event_digest, set< shared_ptr< cvs_branch > > > symbol_parents;

  // stack of branches we're injecting states into
  stack< shared_ptr<cvs_branch> > stk;
  stack< cvs_branchname > bstk;

  file_path curr_file;
  cvs_path curr_file_interned;

  string base_branch;

  ticker n_versions;
  ticker n_tree_branches;

  cvs_history();
  void set_filename(string const & file,
                    file_id const & ident);

  void index_branchpoint_symbols(rcs_file const & r);

  void add_symbol_parent(const cvs_event_digest d);
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


static time_t
parse_time(const char * dp)
{
  time_t time;
  struct tm t;
  // We need to initialize t to all zeros, because strptime has a habit of
  // leaving bits of the data structure alone, letting garbage sneak into
  // our output.
  memset(&t, 0, sizeof(t));
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
  return time;
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
  cvs_event_ptr curr_commit;
  cvs_event_ptr last_commit;
  string curr_version = begin_version;
  scoped_ptr< vector< piece > > next_lines(new vector<piece>);
  scoped_ptr< vector< piece > > curr_lines(new vector<piece>
                                           (begin_lines.begin(),
                                            begin_lines.end()));
  data curr_data(begin_data), next_data;
  hexenc<id> curr_id(begin_id), next_id;

  while(! (r.deltas.find(curr_version) == r.deltas.end()))
    {
      L(FL("version %s has %d lines") % curr_version % curr_lines->size());

      // fetch the next deltas
      map<string, shared_ptr<rcs_delta> >::const_iterator delta =
        r.deltas.find(curr_version);
      I(delta != r.deltas.end());

      map<string, shared_ptr<rcs_deltatext> >::const_iterator deltatext =
        r.deltatexts.find(curr_version);
      I(deltatext != r.deltatexts.end());

      time_t commit_time = parse_time(delta->second->date.c_str());

      bool is_synthetic_branch_root = is_sbr(delta->second,
                                             deltatext->second);

      bool alive = delta->second->state != "dead";

      string ac_str = delta->second->author + "|||\n";

      if (is_synthetic_branch_root)
        ac_str += "synthetic branch root changelog";
      else
        ac_str += deltatext->second->log;

      L(FL("authorclog: %s") % ac_str);
      cvs_authorclog ac = cvs.authorclog_interner.intern(ac_str);

      if (alive)
        {
          cvs_mtn_version mv = cvs.mtn_version_interner.intern(
            file_id(curr_id).inner()());

          cvs_rcs_version rv = cvs.rcs_version_interner.intern(curr_version);

          curr_commit = boost::static_pointer_cast<cvs_event, cvs_commit>(
            shared_ptr<cvs_commit>(
              new cvs_commit(cvs.curr_file_interned,
                             commit_time, mv, rv,
                             ac, alive)));

          // add the commit to the branch
          cvs.stk.top()->append_event(curr_commit);
          ++cvs.n_versions;

          // make the last commit depend on the current one (which
          // comes _before_ in the CVS history).
          if (last_commit != NULL)
            last_commit->dependencies.push_back(curr_commit);
        }

      // create tag events for all tags on this commit
      typedef multimap<string,string>::const_iterator ity;
      pair<ity,ity> range = r.admin.symbols.equal_range(curr_version);
      for (ity i = range.first; i != range.second; ++i)
        {
          if (i->first == curr_version)
           {
              // ignore tags on dead commits
              if (alive)
                {
                  L(FL("version %s -> tag %s") % curr_version % i->second);

                  cvs_tag tag = cvs.tag_interner.intern(i->second);
                  cvs_event_ptr event = 
                    boost::static_pointer_cast<cvs_event, cvs_event_tag>(
                      shared_ptr<cvs_event_tag>(
                        new cvs_event_tag(curr_commit, tag)));

                  cvs_blob_index bi = cvs.stk.top()->append_event(event);
                  cvs.add_symbol_parent(event->get_digest());

                  // append to the last_commit deps
                  if (last_commit != NULL)
                    last_commit->dependencies.push_back(event);
                }
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

          process_branch(*i, branch_lines, branch_data,
                         branch_id, r, db, cvs);

          shared_ptr<struct cvs_branch> sub_branch(cvs.stk.top());

          cvs.pop_branch();
          L(FL("finished RCS branch %s = '%s'") % (*i) % branch);


          // add a branch event, linked to this new branch if it's
          // not a dead commit
          if (alive)
            {
              cvs_event_ptr branch_event =
                boost::static_pointer_cast<cvs_event, cvs_event_branch>(
                  shared_ptr<cvs_event_branch>(
                    new cvs_event_branch(curr_commit, sub_branch)));

              // make sure curr_commit exists in the blob
              cvs.stk.top()->get_blob(curr_commit->get_digest(), false);

              // then append it to the parent branch
              cvs_blob_index bi = cvs.stk.top()->append_event(branch_event);
              cvs.add_symbol_parent(branch_event->get_digest());

              L(FL("added branch event for file %s from branch %s into branch %s")
                % cvs.path_interner.lookup(curr_commit->path)
                % cvs.bstk.top()
                % branch);

              // append to the last_commit deps
              if (last_commit != NULL)
                last_commit->dependencies.push_back(branch_event);
            }
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
    process_branch(r.admin.head, head_lines, dat, id, r, db, cvs);
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
    }
}

void
cvs_history::add_symbol_parent(const cvs_event_digest d)
{
  if (symbol_parents.find(d) == symbol_parents.end())
    symbol_parents.insert(make_pair(d, set< shared_ptr < cvs_branch > > ()));

  map< cvs_event_digest,
       set< shared_ptr< cvs_branch > > >::iterator parent_list_iter =
          symbol_parents.find(d);

  I(parent_list_iter != symbol_parents.end());

  set< shared_ptr< cvs_branch > > & parent_list = parent_list_iter->second;

  if (parent_list.find(stk.top()) == parent_list.end())
    parent_list.insert(stk.top());
}


void
cvs_history::push_branch(string const & branch_name, bool private_branch)
{
  shared_ptr<cvs_branch> branch;

  string bname = base_branch + "." + branch_name;
  I(stk.size() > 0);

  if (private_branch)
    {
      cvs_branchname bn = branchname_interner.intern("");
      branch = shared_ptr<cvs_branch>(new cvs_branch(bn));
      stk.push(branch);
      bstk.push(bn);
      return;
    }
  else
    {
      cvs_branchname bn = branchname_interner.intern(bname);

      map<cvs_branchname, shared_ptr<cvs_branch> >::const_iterator b = branches.find(bn);

      if (b == branches.end())
        {
          branch = shared_ptr<cvs_branch>(new cvs_branch(bn));
          branches.insert(make_pair(bn, branch));
          ++n_tree_branches;
        }
      else
        branch = b->second;

      stk.push(branch);
      bstk.push(bn);
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


struct
cluster_consumer
{
  cvs_history & cvs;
  app_state & app;
  string const & branchname;
  cvs_branch const & branch;
  set<split_path> created_dirs;
  map<cvs_path, cvs_mtn_version> live_files;
  ticker & n_revisions;

  struct prepared_revision
  {
    prepared_revision(revision_id i,
                      shared_ptr<revision_t> r,
                      const cvs_blob & blob);
    revision_id rid;
    shared_ptr<revision_t> rev;
    time_t time;
    cvs_authorclog authorclog;
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

  void consume_blob(const cvs_blob & blob);
  void add_missing_parents(split_path const & sp, cset & cs);
  void build_cset(const cvs_blob & blob, cset & cs);
  void store_auxiliary_certs(prepared_revision const & p);
  void store_revisions();
};

template < class MyEdge >
struct blob_splitter 
  : public boost::dfs_visitor<>
{
protected:
  shared_ptr< cvs_branch > branch;
  vector< MyEdge > & back_edges;

public:
  blob_splitter(shared_ptr< cvs_branch > b, vector< MyEdge > & be)
    : branch(b),
      back_edges(be)
    { }

  template < class Edge, class Graph >
  void tree_edge(Edge e, Graph & g)
    {
      L(FL("blob_splitter: tree edge: %s") % e);
    }

  template < class Edge, class Graph >
  void back_edge(Edge e, Graph & g)
    {
      L(FL("blob_splitter: back edge: %s") % e);
      back_edges.push_back(MyEdge(e.m_source, e.m_target));
    }
};

class revision_iterator
{
private:
	cvs_blob_index current_blob;
  shared_ptr< cluster_consumer > cons;
  shared_ptr< cvs_branch > branch;

public:
  revision_iterator(shared_ptr< cluster_consumer > const c,
                    shared_ptr< cvs_branch > const b)
    : current_blob(0),
      cons(c),
      branch(b)
    {}

  revision_iterator(const revision_iterator & ri)
    : current_blob(ri.current_blob),
      cons(ri.cons),
      branch(ri.branch)
    {}

	revision_iterator & operator * (void)
    {
      return *this;
    };

  revision_iterator & operator = (cvs_blob_index current_blob)
    {
      L(FL("next blob number from toposort: %d") % current_blob);
      cons->consume_blob(branch->blobs[current_blob]);
      return *this;
    }

	revision_iterator & operator ++ (void)
    {
      return *this;
    }

	revision_iterator & operator ++ (int i)
    {
      return *this;
    };
};

typedef pair< cvs_blob_index, cvs_blob_index > Edge;
typedef boost::adjacency_list< boost::vecS, boost::vecS,
                               boost::bidirectionalS > Graph;

void
add_blob_dependency_edges(shared_ptr<cvs_branch> const & branch,
                          const cvs_blob_index i,
                          Graph & g)
{
  const cvs_blob & blob = branch->blobs[i];

  for(blob_event_iter event = blob.begin(); event != blob.end(); ++event)
    {
      for(dependency_iter dep = (*event)->dependencies.begin();
          dep != (*event)->dependencies.end(); ++dep)
        {
          blob_index_iterator k =
            branch->get_blob((*dep)->get_digest(), false);

          for ( ; (k->second < branch->blobs.size()) &&
                  (branch->blobs[k->second].get_digest() == 
                                    (*dep)->get_digest()); ++k)
            {
              bool found = false;

              for (dependency_iter di = branch->blobs[k->second].get_events().begin();
                   di != branch->blobs[k->second].get_events().end(); ++ di)
                {
                  if (*di == *dep)
                    found = true;
                }

              if (found)
                {
                  L(FL("blob %d depends on blob %d") % i % k->second);
                  add_edge(i, k->second, g);
                }
            }
        }
    }
}

void
split_blobs_at(shared_ptr<cvs_branch> const & branch,
               const Edge & e, Graph & g)
{
  L(FL("splitting at edge: %d -> %d") % e.first % e.second);

  cvs_event_digest target_blob_digest(branch->blobs[e.second].get_digest());

  // we can only split commit events, not branches or tags
  I(target_blob_digest.is_commit());

  vector< cvs_event_ptr > blob_events(branch->blobs[e.second].get_events());

  // sort the blob events by timestamp
  sort(blob_events.begin(), blob_events.end());

  // now detect the largest gap between any two events
  time_t max_diff = 0;
  blob_event_iter max_at = blob_events.begin();

  blob_event_iter i, last;
  i = blob_events.begin();
  last = i;
  i++;
  for ( ; i != blob_events.end(); ++i)
    {
      time_t diff = (*i)->time - (*last)->time;

      if (diff > max_diff)
        {
          max_diff = diff;
          max_at = i;
        }

      last = i;
    }

  L(FL("max. time difference is: %d") % max_diff);

  // add a blob
  cvs_event_digest d = branch->blobs[e.second].get_digest();
  cvs_blob_index new_blob = branch->add_blob(d)->second;

  // reassign all events and split into the two blobs
  branch->blobs[e.second].get_events().clear();
  I(!blob_events.empty());
  I(branch->blobs[e.second].empty());

  for (i = blob_events.begin(); i != blob_events.end(); ++i)
    if ((*i)->time >= (*max_at)->time)
      branch->blobs[new_blob].push_back(*i);
    else
      branch->blobs[e.second].push_back(*i);

  {
    // in edges, blobs which depend on this one blob we should split
    pair< boost::graph_traits<Graph>::in_edge_iterator,
          boost::graph_traits<Graph>::in_edge_iterator > range;

    range = in_edges(e.second, g);

    vector< cvs_blob_index > in_deps_from;

    // get all blobs with dependencies to the blob which has been split
    for (boost::graph_traits<Graph>::in_edge_iterator ity = range.first;
         ity != range.second; ++ity)
      {
        L(FL("removing in edge %s") % *ity);
        in_deps_from.push_back(ity->m_source);
        I(ity->m_target == e.second);
      }

    // remove all those edges
    for (vector< cvs_blob_index >::const_iterator ity = in_deps_from.begin();
         ity != in_deps_from.end(); ++ity)
          remove_edge(*ity, e.second, const_cast<Graph &>(g));

    // now check each in_deps_from blob and add proper edges to the
    // newly splitted blobs
    for (vector< cvs_blob_index >::const_iterator ity = in_deps_from.begin();
         ity != in_deps_from.end(); ++ity)
      {
        cvs_blob & other_blob = branch->blobs[*ity];

        for (vector< cvs_event_ptr >::const_iterator j = 
              other_blob.get_events().begin();
              j != other_blob.get_events().end(); ++j)
          {
            for (dependency_iter ob_dep = (*j)->dependencies.begin();
                 ob_dep != (*j)->dependencies.end(); ++ob_dep)

              if ((*ob_dep)->get_digest() == d)
              {
                if ((*ob_dep)->time >= (*max_at)->time)
                {
                  L(FL("adding new edge %d -> %d") % *ity % new_blob);
                  add_edge(*ity, new_blob, const_cast<Graph &>(g));
                }
                else
                {
                  L(FL("keeping edge %d -> %d") % *ity % new_blob);
                  add_edge(*ity, e.second, const_cast<Graph &>(g));
                }
              }
          }
      }
  }

  // adjust out edges of the new blob
  {
    // in edges, blobs which depend on this one blob which we are splitting
    pair< boost::graph_traits<Graph>::out_edge_iterator,
          boost::graph_traits<Graph>::out_edge_iterator > range;

    range = out_edges(e.second, g);

    // remove all existing out edges
    for (boost::graph_traits<Graph>::out_edge_iterator ity = range.first;
         ity != range.second; ++ity)
      {
        L(FL("removing out edge %s") % *ity);
        remove_edge(ity->m_source, ity->m_target, const_cast<Graph &>(g));
      }

    add_blob_dependency_edges(branch, e.second, const_cast<Graph &>(g));
    add_blob_dependency_edges(branch, new_blob, const_cast<Graph &>(g));
  }
}

//
// After stuffing all cvs_events into blobs of events with the same
// author and changelog, we have to make sure their dependencies are
// respected.
//
void
resolve_blob_dependencies(cvs_history &cvs,
                          app_state & app,
                          string const & branchname,
                          shared_ptr<cvs_branch> const & branch,
                          ticker & n_revs)
{
  L(FL("branch %s currently has %d blobs.") % branchname % branch->blobs.size());

  Graph g(branch->blobs.size());

  // fill the graph with all blob dependencies as edges between
  // the blobs (vertices).
  for (cvs_blob_index i = 0; i < branch->blobs.size(); ++i)
    add_blob_dependency_edges(branch, i, g);

  // check for cycles
  vector< Edge > back_edges;
	blob_splitter< Edge > vis(branch, back_edges);

  do
  {
    back_edges.clear();
  	depth_first_search(g, visitor(vis));

    // Just split the first blob which had a back edge
    if (back_edges.begin() != back_edges.end())
        split_blobs_at(branch, *back_edges.begin(), g);

  } while (!back_edges.empty());

  // start the topological sort, which calls our revision
  // iterator to insert the revisions into our database. 
  shared_ptr<cluster_consumer> cons = shared_ptr<cluster_consumer>(
    new cluster_consumer(cvs, app, branchname, *branch, n_revs));
  revision_iterator ri(cons, branch);

  L(FL("starting toposort the blobs of branch %s") % branchname);
  topological_sort(g, ri);

  // finally store the revisions
  // (ms) why is this an extra step? Is it faster?
  cons->store_revisions();
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
  N(app.opts.branch_name() != "", F("need base --branch argument for importing"));
  cvs.base_branch = app.opts.branch_name();

  // push the trunk
  cvs_branchname bn = cvs.branchname_interner.intern(cvs.base_branch);
  cvs.trunk = shared_ptr<cvs_branch>(new cvs_branch(bn));
  cvs.stk.push(cvs.trunk);
  cvs.bstk.push(bn);

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

  // make sure all symbol blobs are only present in one branch
  int unresolved_symbols = 99999998;
  int prev_unresolved_symbols = 99999999;

  while ((unresolved_symbols > 0) && (unresolved_symbols < prev_unresolved_symbols))
    {
      prev_unresolved_symbols = unresolved_symbols;
      unresolved_symbols = 0;

  map< cvs_event_digest, set< shared_ptr< cvs_branch > > >::iterator i;
  for (i = cvs.symbol_parents.begin(); i != cvs.symbol_parents.end(); ++i)
    {
      I(i->second.size() > 0);
      cvs_event_digest d = i->first;

      if (i->second.size() == 1)
        {
          shared_ptr< cvs_branch > branch = *i->second.begin();
          blob_index_iterator bi = branch->get_blob(d, false);
          cvs_blob & blob = branch->blobs[bi->second];

          shared_ptr< cvs_event > ev = *(blob.get_events().begin());

          if (d.is_branch())
            {
              shared_ptr<cvs_event_branch> be =
                boost::static_pointer_cast<cvs_event_branch, cvs_event>(ev);

              be->branch->parent_branch = branch;
            }
        }
      else if (i->second.size() > 1)
        {

          I(!d.is_commit());

          for (set< shared_ptr< cvs_branch > >::iterator j =
              i->second.begin(); j != i->second.end(); ++j)
            {
              unresolved_symbols++;

              blob_index_iterator bi = (*j)->get_blob(d, false);
              cvs_blob & blob = (*j)->blobs[bi->second];

              shared_ptr< cvs_event > ev = *(blob.get_events().begin());

              if (d.is_branch())
                {
                  shared_ptr<cvs_event_branch> be =
                    boost::static_pointer_cast<cvs_event_branch,
                                               cvs_event>(ev);

                  L(FL("XXXXX: Symbol (branch %s) is in branch %s")
                    % cvs.branchname_interner.lookup(be->branch->branchname)
                    % cvs.branchname_interner.lookup((*j)->branchname));
                }
              else if (d.is_tag())
                {
                  shared_ptr<cvs_event_tag> te =
                    boost::static_pointer_cast<cvs_event_tag, cvs_event>(ev);

                  L(FL("XXXXX: symbol tag %s is in branch %s")
                    % cvs.tag_interner.lookup(te->tag)
                    % cvs.branchname_interner.lookup((*j)->branchname));
                }
              else
                I(false);

              for (set< shared_ptr< cvs_branch > >::iterator k = j;
                  k != i->second.end(); ++k)
                {
                  if ((*j)->parent_branch == (*k))
                    {
                      L(FL("branch %s is a child of branch %s")
                        % cvs.branchname_interner.lookup((*j)->branchname)
                        % cvs.branchname_interner.lookup((*k)->branchname));

                      i->second.erase(*k);
                    }
                  else if ((*k)->parent_branch == (*j))
                    {
                      L(FL("branch %s is a child of branch %s")
                        % cvs.branchname_interner.lookup((*k)->branchname)
                        % cvs.branchname_interner.lookup((*j)->branchname));

                      i->second.erase(*j);
                    }
                }
            }
        }
    }

      L(FL("XXXX: still unresolved symbols: %d") % unresolved_symbols);
    }

  I(cvs.stk.size() == 1);

  ticker n_revs(_("revisions"), "r", 1);

  {
    transaction_guard guard(app.db);
    resolve_blob_dependencies(cvs, app, cvs.base_branch, cvs.trunk, n_revs);
    guard.commit();
  }

  for(map<cvs_branchname, shared_ptr<cvs_branch> >::const_iterator i =
          cvs.branches.begin(); i != cvs.branches.end(); ++i)
    {
      transaction_guard guard(app.db);
      string branchname = cvs.branchname_interner.lookup(i->first);
      shared_ptr<cvs_branch> branch = i->second;
      resolve_blob_dependencies(cvs, app, branchname, branch, n_revs);
      guard.commit();
    }

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
  if (!null_id(branch.parent_rid))
    {
      L(FL("starting cluster for branch %s from revision %s which contains:")
           % branchname
           % branch.parent_rid);

      parent_rid = branch.parent_rid;
      app.db.get_roster(parent_rid, ros);

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
              live_files[path] = cvs.mtn_version_interner.intern(rev);
            }
        }
    }

  if (!branch.has_a_commit)
    {
      W(F("Ignoring branch %s because it is empty.") % branchname);
    }
}

cluster_consumer::prepared_revision::prepared_revision(revision_id i, 
                                                       shared_ptr<revision_t> r,
                                                       const cvs_blob & blob)
  : rid(i),
    rev(r)
{
  I(blob.get_digest().is_commit());

  shared_ptr<cvs_commit> ce =
    boost::static_pointer_cast<cvs_commit, cvs_event>(*blob.begin());

  authorclog = ce->authorclog;

  // FIXME: calculate an avg time
  time = ce->time;

/* FIXME:
  for (set<cvs_tag>::const_iterator i = c.tags.begin();
       i != c.tags.end(); ++i)
    {
      tags.push_back(*i);
    }
*/
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

  string ac_str = cvs.authorclog_interner.lookup(p.authorclog);
  int i = ac_str.find("|||\n");

  string author = ac_str.substr(0, i);
  string changelog = ac_str.substr(i+4);

  cert_revision_in_branch(p.rid, cert_value(branchname), app, dbw);
  cert_revision_author(p.rid, author, app, dbw);
  cert_revision_changelog(p.rid, changelog, app, dbw);
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
cluster_consumer::build_cset(const cvs_blob & blob,
                             cset & cs)
{
  for (blob_event_iter i = blob.begin(); i != blob.end(); ++i)
    {
      I((*i)->get_digest().is_commit());

      shared_ptr<cvs_commit> ce =
        boost::static_pointer_cast<cvs_commit, cvs_event>(*i);

      file_path pth = file_path_internal(cvs.path_interner.lookup(ce->path));

      L(FL("cluster_consumer::build_cset: file_path: %s") % pth);

      split_path sp;
      pth.split(sp);

      file_id fid(cvs.mtn_version_interner.lookup(ce->mtn_version));

      if (ce->alive)
        {
          map<cvs_path, cvs_mtn_version>::const_iterator e =
            live_files.find(ce->path);

          if (e == live_files.end())
            {
              add_missing_parents(sp, cs);
              L(FL("adding entry state '%s' on '%s'") % fid % pth);
              safe_insert(cs.files_added, make_pair(sp, fid));
              live_files[ce->path] = ce->mtn_version;
            }
          else if (e->second != ce->mtn_version)
            {
              file_id old_fid(cvs.mtn_version_interner.lookup(e->second));
              L(FL("applying state delta on '%s' : '%s' -> '%s'")
                % pth % old_fid % fid);
              safe_insert(cs.deltas_applied,
                          make_pair(sp, make_pair(old_fid, fid)));
              live_files[ce->path] = ce->mtn_version;
            }
        }
      else
        {
          map<cvs_path, cvs_mtn_version>::const_iterator e =
            live_files.find(ce->path);

          if (e != live_files.end())
            {
              L(FL("deleting entry state '%s' on '%s'") % fid % pth);
              safe_insert(cs.nodes_deleted, sp);
              live_files.erase(ce->path);
            }
        }
    }
}

void
cluster_consumer::consume_blob(const cvs_blob & blob)
{
  if (blob.get_digest().is_commit())
    {
      // we should never have an empty blob; it's *possible* to have
      // an empty changeset (say on a vendor import) but every cluster
      // should have been created by at least one file commit, even
      // if the commit made no changes. it's a logical inconsistency if
      // you have an empty blob.
      I(!blob.empty());

      shared_ptr<revision_t> rev(new revision_t());
      shared_ptr<cset> cs(new cset());

      build_cset(blob, *cs);

      cs->apply_to(editable_ros);
      manifest_id child_mid;
      calculate_ident(ros, child_mid);

      rev->made_for = made_for_database;
      rev->new_manifest = child_mid;
      rev->edges.insert(make_pair(parent_rid, cs));

      calculate_ident(*rev, child_rid);

      preps.push_back(prepared_revision(child_rid, rev, blob));

      parent_rid = child_rid;
    }
  else if (blob.get_digest().is_branch())
    {
      shared_ptr<cvs_event_branch> cbe =
        boost::static_pointer_cast<cvs_event_branch, cvs_event>(*blob.begin());

      string child_rid_str;
      dump(child_rid, child_rid_str);

      L(FL("setting the parent revision id of branch %s to: %s") % 
        cvs.branchname_interner.lookup(cbe->branch->branchname) % child_rid_str);

      cbe->branch->parent_rid = child_rid;
    }
  else if (blob.get_digest().is_tag())
    {
      W(F("ignoring tag blob (not implemented)"));
    }
  else
    I(false);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
