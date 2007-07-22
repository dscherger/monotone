// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include <ostream>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <vector>
#include <list>
#include <iterator>

#include <unistd.h>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include "lexical_cast.hh"
#include <boost/tokenizer.hpp>

#include "app_state.hh"
#include "cert.hh"
#include "constants.hh"
#include "cycle_detector.hh"
#include "database.hh"
#include "file_io.hh"
#include "interner.hh"
#include "keys.hh"
#include "paths.hh"
#include "platform-wrapped.hh"
#include "project.hh"
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
using std::list;
using std::back_insert_iterator;

using boost::scoped_ptr;
using boost::shared_ptr;
using boost::lexical_cast;

// additional debugging information
// not defined: DEBUG_BRANCH_REDUCTION
// not defined: DEBUG_BLOB_SPLITTER
// not defined: DEBUG_GRAPHVIZ

// cvs history recording stuff

typedef unsigned long cvs_branchname;
typedef unsigned long cvs_authorclog;
typedef unsigned long cvs_mtn_version;   // the new file id in monotone
typedef unsigned long cvs_rcs_version;   // the old RCS version number
typedef unsigned long cvs_path;
typedef unsigned long cvs_tag;

const cvs_branchname invalid_branch = cvs_branchname(-1);

typedef enum
{
  ET_COMMIT = 0,
  ET_TAG = 2,
  ET_BRANCH = 3
} event_type;

struct cvs_history;

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
  vector< cvs_event_ptr > dependents;

  cvs_event(const cvs_path p, const time_t ti)
    : time(ti),
      path(p)
    { };

  virtual ~cvs_event() { };
  virtual cvs_event_digest get_digest(void) const = 0;

  const bool operator < (const cvs_event & e) const
    {
      return time < e.time;
    }
};

void add_dependency(cvs_event_ptr ev, cvs_event_ptr dep)
{
  /* Adds dep as a dependency of ev. */
  ev->dependencies.push_back(dep);
  dep->dependents.push_back(ev);
}
  

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
  cvs_branchname branchname;

  // The first event(s) in the branch. This is used in the blob_consumer, to
  // distinguish between events which only depend on the branchpoint (i.e.
  // have to come later) and events which have taken place in the branch
  // and therefore depend on the branchpoint, too.
  vector< cvs_event_ptr > branch_contents;

  cvs_event_branch(const cvs_path p, const cvs_branchname bn)
    : cvs_event(p, 0),
      branchname(bn)
    { };

  cvs_event_branch(const cvs_path p, const cvs_branchname bn, time_t ti)
    : cvs_event(p, ti),
      branchname(bn)
    { };

  virtual cvs_event_digest get_digest(void) const
    {
      return cvs_event_digest(ET_BRANCH, branchname);
    };
};

class
cvs_event_tag
  : public cvs_event
{
public:
  cvs_tag tag;

  cvs_event_tag(const cvs_path p, const time_t ti, const cvs_tag ta)
    : cvs_event(p, ti),
      tag(ta)
    { };

  virtual cvs_event_digest get_digest(void) const
    {
      return cvs_event_digest(ET_TAG, tag);
    };
};

class cvs_blob;

typedef vector< cvs_event_ptr >::const_iterator blob_event_iter;
typedef vector< cvs_event_ptr >::const_iterator dependency_iter;

typedef vector<cvs_blob>::size_type cvs_blob_index;
typedef vector<cvs_blob_index>::const_iterator blob_index_iter;

// for the depth first search algo
typedef pair< cvs_blob_index, cvs_blob_index > Edge;

typedef multimap<cvs_event_digest, cvs_blob_index>::iterator
  blob_index_iterator;

const cvs_blob_index invalid_blob = cvs_blob_index(-1);

typedef enum { white, grey, black } dfs_color;

class
cvs_blob
{
private:
  cvs_event_digest digest;
  vector< cvs_event_ptr > events;

  bool has_cached_deps;
  vector<cvs_blob_index> dependents_cache;

public:
  cvs_blob_index in_branch;

  // helper fields for Depth First Search algorithms
  dfs_color colors[2];

  // Used only for branches and tags: keeps track of the original blob from
  // which this got split. The original blob keeps a split counter.
  cvs_blob_index split_origin;

  union {
    int split_counter;    // for the origin blob
    int split_index;      // for all derived blobs
  };

  cvs_blob(const cvs_event_digest d)
    : digest(d),
      has_cached_deps(false),
      in_branch(invalid_blob),
      split_origin(invalid_blob),
      split_counter(0)
    { };

  cvs_blob(const cvs_blob & b)
    : digest(b.digest),
      events(b.events),
      has_cached_deps(false),
      in_branch(invalid_blob),
      split_origin(invalid_blob),
      split_counter(0)
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

  void clear()
    {
      events.clear();
    }

  blob_event_iter & begin() const
    {
      return *(new blob_event_iter(events.begin()));
    }

  blob_event_iter & end() const
    {
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

  vector<cvs_blob_index> & get_dependents(cvs_history & cvs);

  void reset_deps_cache(void)
    {
      has_cached_deps = false;
    }
};

typedef struct
{
  cvs_blob_index bi;
  blob_index_iter ei;
} dfs_context;

struct blob_splitter;

struct
cvs_history
{
  interner<unsigned long> branchname_interner;
  interner<unsigned long> authorclog_interner;
  interner<unsigned long> mtn_version_interner;
  interner<unsigned long> rcs_version_interner;
  interner<unsigned long> path_interner;
  interner<unsigned long> tag_interner;

  // all the blobs of the whole repository
  vector<cvs_blob> blobs;

  // all the blobs by their event_digest
  multimap<cvs_event_digest, cvs_blob_index> blob_index;

  // assume an RCS file has foo:X.Y.0.N in it, then
  // this map contains entries of the form
  // X.Y.N.1 -> foo
  // this map is cleared for every RCS file.
  map<string, string> branch_first_entries;

  // tag -> revision
  //
  // used to resolve the *last* revision which has a given tag
  // applied; this is the revision which wins the tag.
  map<cvs_tag, revision_id> resolved_tags;

  file_path curr_file;
  cvs_path curr_file_interned;

  cvs_branchname base_branch;
  cvs_blob_index root_event;

  ticker n_versions;
  ticker n_tree_branches;

  cvs_history();
  void set_filename(string const & file,
                    file_id const & ident);

  void index_branchpoint_symbols(rcs_file & r);

  blob_index_iterator add_blob(const cvs_event_digest d)
  {
    // add a blob..
    cvs_blob_index i = blobs.size();
    blobs.push_back(cvs_blob(d));

    // ..and an index entry for the blob
    blob_index_iterator j = blob_index.insert(make_pair(d, i));
    return j;
  }

  bool
  blob_exists(const cvs_event_digest d)
  {
    pair<blob_index_iterator, blob_index_iterator> range = 
      blob_index.equal_range(d);

    return (range.first != range.second);
  }

  pair< blob_index_iterator, blob_index_iterator >
  get_blobs(const cvs_event_digest d, bool create)
  {
    pair<blob_index_iterator, blob_index_iterator> range = 
      blob_index.equal_range(d);

    if ((range.first == range.second) && create)
      {
        // TODO: is this correct?
        range.first = add_blob(d);
        range.second = range.first;
        range.second++;
        return range;
      }

    I(range.first != range.second);
    return range;
  }

  cvs_blob_index
  get_blob_of(const cvs_event_ptr ev)
  {
    // We have to check all the blobs in the equal_range, if the event we
    // are looking for is in that blob.
    pair<blob_index_iterator, blob_index_iterator> range = 
      get_blobs(ev->get_digest(), false);

    I(range.first != range.second);
    for ( ; range.first != range.second; range.first++)
      {
        cvs_blob_index bi = range.first->second;
        cvs_blob & dep_blob = blobs[bi];

        for (blob_event_iter k = dep_blob.begin(); k != dep_blob.end(); ++k)
          {
            if (*k == ev)
              return bi;
          }
      }

    I(false);
  }

  cvs_blob_index
  get_branch_blob(const cvs_branchname bn)
  {
    I(bn != invalid_branch);

    pair<blob_index_iterator, blob_index_iterator> range =
      get_blobs(cvs_event_digest(ET_BRANCH, bn), false);

    I(range.first != range.second);
    cvs_blob_index result(range.first->second);

    // We are unable to handle split branches here, check for that.
    range.first++;
    I(range.first == range.second);

    return result;
  }

  cvs_blob_index append_event(cvs_event_ptr c) 
  {
    if (c->get_digest().is_commit())
      I(c->time != 0);

    blob_index_iterator b = get_blobs(c->get_digest(), true).first;
    blobs[b->second].push_back(c);

    return b->second;
  }

  void split_authorclog(const cvs_authorclog ac, string & author,
                        string & changelog)
  {
    string ac_str = authorclog_interner.lookup(ac);
    int i = ac_str.find("|||");
    I(i > 0);

    author = ac_str.substr(0, i);
    changelog = ac_str.substr(i+3);
  }

  string join_authorclog(const string author, const string clog)
  {
    I(author.size() > 0);
    I(clog.size() > 0);
    return author + "|||" + clog;
  }

  string get_branchname(const cvs_branchname bname)
  {
    if (bname == base_branch)
      return branchname_interner.lookup(base_branch);
    else
      return branchname_interner.lookup(base_branch) + "." +
        branchname_interner.lookup(bname);
  }

  void depth_first_search(blob_splitter & vis,
                          back_insert_iterator< vector<cvs_blob_index> > oi);
};


static bool
is_sbr(shared_ptr<rcs_delta> dl,
       shared_ptr<rcs_deltatext> dt)
{
  I(dl);
  I(!dl->state.empty());
  I(dt);

  // CVS abuses the RCS format a bit (ha!) when storing a file which
  // was only added on a branch: on the root of the branch there'll be
  // a commit with dead state, empty text, and a log message
  // containing the string "file foo was initially added on branch
  // bar". We recognize and ignore these cases, as they do not
  // "really" represent commits to be put together in a blob.

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

  if (db.file_version_exists(file_id(old_id)))
    {
      // we already have a way to get to this old version,
      // no need to insert another reconstruction path
      L(FL("existing path to %s found, skipping") % old_id);
    }
  else
    {
      I(db.file_or_manifest_base_exists(new_id, "files")
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
               database & db,
               bool dryrun)
{
  // inserting into the DB
  // note: curr_lines is a "new" (base) version
  //       and next_lines is an "old" (derived) version.
  //       all storage edges go from new -> old.
  {
    string tmp;
    global_pieces.build_string(next_lines, tmp);
    next_data = data(tmp);
  }
  delta del;
  diff(curr_data, next_data, del);
  calculate_ident(next_data, next_id);

  if (!dryrun)
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

void
add_dependencies(cvs_event_ptr ev, vector<cvs_event_ptr> last_events,
                 bool reverse_import)
{
  vector<cvs_event_ptr>::iterator i;

  if (reverse_import)
    {
      // make the last commit (i.e. 1.3) depend on the current one
      // (i.e. 1.2), as it which comes _before_ in the CVS history.
      for (i = last_events.begin(); i != last_events.end(); ++i)
        add_dependency(*i, ev);
    }
  else
    {
      // vendor branches are processed in historic order, i.e.
      // older version first. Thus the last commit may be 1.1.1.1
      // while the current one is 1.1.1.2.
      for (i = last_events.begin(); i != last_events.end(); ++i)
        add_dependency(ev, *i);
    }      
}

static cvs_event_ptr
process_rcs_branch(string const & begin_version,
               vector< piece > const & begin_lines,
               data const & begin_data,
               hexenc<id> const & begin_id,
               rcs_file const & r,
               database & db,
               cvs_history & cvs,
               bool dryrun,
               bool reverse_import)
{
  cvs_event_ptr curr_commit, first_commit;
  vector<cvs_event_ptr> curr_events, last_events;
  string curr_version = begin_version;
  scoped_ptr< vector< piece > > next_lines(new vector<piece>);
  scoped_ptr< vector< piece > > curr_lines(new vector<piece>
                                           (begin_lines.begin(),
                                            begin_lines.end()));
  data curr_data(begin_data), next_data;
  hexenc<id> curr_id(begin_id), next_id;

  while(! (r.deltas.find(curr_version) == r.deltas.end()))
    {
      curr_events.clear();

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

      string ac_str;
      if (is_synthetic_branch_root)
        ac_str = cvs.join_authorclog(delta->second->author,
                                     "systhetic branch root changelog");
      else
        ac_str = cvs.join_authorclog(delta->second->author,
                                     deltatext->second->log);

      L(FL("author and changelog: %s") % ac_str);
      cvs_authorclog ac = cvs.authorclog_interner.intern(ac_str);

      cvs_mtn_version mv = cvs.mtn_version_interner.intern(
        file_id(curr_id).inner()());

      cvs_rcs_version rv = cvs.rcs_version_interner.intern(curr_version);

      curr_commit = boost::static_pointer_cast<cvs_event, cvs_commit>(
        shared_ptr<cvs_commit>(
          new cvs_commit(cvs.curr_file_interned,
                         commit_time, mv, rv,
                         ac, alive)));

      if (!first_commit)
        first_commit = curr_commit;

      // add the commit to the cvs history
      cvs.append_event(curr_commit);
      ++cvs.n_versions;

      curr_events.push_back(curr_commit);

      // add proper dependencies, based on forward or reverse import
      if (!last_events.empty())
        add_dependencies(curr_commit, last_events, reverse_import);

      // create tag events for all tags on this commit
      typedef multimap<string,string>::const_iterator ity;
      pair<ity,ity> range = r.admin.symbols.equal_range(curr_version);
      for (ity i = range.first; i != range.second; ++i)
        {
          if (i->first == curr_version)
           {
              L(FL("version %s -> tag %s") % curr_version % i->second);

              cvs_tag tag = cvs.tag_interner.intern(i->second);
              cvs_event_ptr tag_event = 
                boost::static_pointer_cast<cvs_event, cvs_event_tag>(
                  shared_ptr<cvs_event_tag>(
                    new cvs_event_tag(curr_commit->path, curr_commit->time,
                                      tag)));
              add_dependency(tag_event, curr_commit);

              cvs_blob_index bi = cvs.append_event(tag_event);
              curr_events.push_back(curr_commit);

              // Append to the last_event deps. While not quite obvious,
              // we absolutely need this dependency! Think of it as: the
              // 'action of tagging' must come before the next commit.
              //
              // If we didn't add this dependency, the tag could be deferred
              // by the toposort to many revisions later. Instead, we want
              // to raise a conflict, if a commit interferes with a tagging
              // action.
              add_dependencies(tag_event, last_events, reverse_import);
            }
        }

      // recursively follow any branch commits coming from the branchpoint
      shared_ptr<rcs_delta> curr_delta = r.deltas.find(curr_version)->second;
      for(set<string>::const_iterator i = curr_delta->branches.begin();
          i != curr_delta->branches.end(); ++i)
        {
          string branchname;
          data branch_data;
          hexenc<id> branch_id;
          vector< piece > branch_lines;
          bool priv = false;
          bool is_vendor_branch = (r.vendor_branches.find(*i) !=
            r.vendor_branches.end());

          map<string, string>::const_iterator be =
            cvs.branch_first_entries.find(*i);

          if (be != cvs.branch_first_entries.end())
            branchname = be->second;
          else
            priv = true;

          if (!priv)
            {
              I(branchname.length() > 0);
              L(FL("following RCS branch %s = '%s'") % (*i) % branchname);
            }
          else
            {
              L(FL("following private branch RCS %s") % (*i));
            }

          // Only construct the version if the delta exists. We
          // have possbily added invalid deltas in
          // index_branchpoint_symbols().
          if (r.deltas.find(*i) != r.deltas.end())
            {
              construct_version(*curr_lines, *i, branch_lines, r);
              insert_into_db(curr_data, curr_id, 
                             branch_lines, branch_data, branch_id, db,
                             dryrun);
            }

          // recursively process child branches
          cvs_event_ptr first_event_in_branch =
            process_rcs_branch(*i, branch_lines, branch_data,
                               branch_id, r, db, cvs, dryrun,
                               !is_vendor_branch);
          if (!priv)
            L(FL("finished RCS branch %s = '%s'") % (*i) % branchname);
          else
            L(FL("finished private RCS branch %s") % (*i));

          cvs_branchname bname = cvs.branchname_interner.intern(branchname);

          cvs_event_ptr branch_event =
            boost::static_pointer_cast<cvs_event, cvs_event_branch>(
              shared_ptr<cvs_event_branch>(
                new cvs_event_branch(curr_commit->path, bname,
                                     curr_commit->time)));
          if (!is_vendor_branch)
            add_dependency(branch_event, curr_commit);

          curr_events.push_back(branch_event);

          if (first_event_in_branch)
            {
              // add correct dependencies and remember the first event in
              // the branch, to be able to differenciate between that and
              // other events which depend on this branch event, later on.
              add_dependency(first_event_in_branch, branch_event);
              boost::static_pointer_cast<cvs_event_branch, cvs_event>(
                branch_event)->branch_contents.push_back(first_event_in_branch);
            }
          else
            L(FL("branch %s remained empty for this file") % branchname);

          // make sure curr_commit exists in the cvs history
          I(cvs.blob_exists(curr_commit->get_digest()));

          // add the blob to the bucket
          cvs_blob_index bi = cvs.append_event(branch_event);

          L(FL("added branch event for file %s into branch %s")
            % cvs.path_interner.lookup(curr_commit->path)
            % branchname);

          // Make the last commit depend on this branch, so that this
          // commit action certainly comes after the branch action. See
          // the comment above for tags.
          if (!is_vendor_branch)
            add_dependencies(branch_event, last_events, reverse_import);
        }

      string next_version = r.deltas.find(curr_version)->second->next;

      if (!next_version.empty())
        {
          L(FL("following RCS edge %s -> %s") % curr_version % next_version);

          construct_version(*curr_lines, next_version, *next_lines, r);
          L(FL("constructed RCS version %s, inserting into database") %
            next_version);

          insert_into_db(curr_data, curr_id,
                         *next_lines, next_data, next_id, db, dryrun);
        }

      if (!r.deltas.find(curr_version)->second->next.empty())
        {
          // advance
          curr_data = next_data;
          curr_id = next_id;
          curr_version = next_version;
          swap(next_lines, curr_lines);
          next_lines->clear();

          if (reverse_import)
            {
              last_events.clear();
              last_events.push_back(curr_commit);
            }
          else
            last_events = curr_events;
        }
      else break;
    }

  if (reverse_import)
    return curr_commit;
  else
    return first_commit;
}


static void
import_rcs_file_with_cvs(string const & filename, app_state & app,
                         cvs_history & cvs)
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
    file_id fid(id);

    cvs.set_filename (filename, fid);
    cvs.index_branchpoint_symbols (r);
    if (!app.opts.dryrun)
      app.db.put_file(fid, file_data(dat));

    global_pieces.reset();
    global_pieces.index_deltatext(r.deltatexts.find(r.admin.head)->second,
                                  head_lines);

    // add a pseudo trunk branch event (at time 0)
    cvs_event_ptr root_event =
      boost::static_pointer_cast<cvs_event, cvs_event_branch>(
        shared_ptr<cvs_event_branch>(
          new cvs_event_branch(cvs.curr_file_interned, cvs.base_branch)));
    cvs.root_event = cvs.append_event(root_event);

    cvs_event_ptr first_event =
      process_rcs_branch(r.admin.head, head_lines, dat, id, r, app.db, cvs,
                         app.opts.dryrun, true);

    // link the pseudo trunk branch to the first event in the branch
    add_dependency(first_event, root_event);
    boost::static_pointer_cast<cvs_event_branch, cvs_event>(
      root_event)->branch_contents.push_back(first_event);

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

void cvs_history::index_branchpoint_symbols(rcs_file & r)
{
  branch_first_entries.clear();

  for (multimap<string, string>::const_iterator i =
         r.admin.symbols.begin(); i != r.admin.symbols.end(); ++i)
    {
      bool is_vendor_branch = false;
      string const & num = i->first;
      string const & sym = i->second;

      vector<string> components;
      split_version(num, components);

      vector<string> first_entry_components;
      vector<string> branchpoint_components;

      // require a valid RCS version
      E(components.size() >= 2,
        F("Invalid RCS version: %s")
          % num);

      if (components.size() > 2 &&
          (components.size() % 2 == 1))
        {
          // this is a "vendor" branch
          //
          // such as "1.1.1", where "1.1" is the branchpoint and
          // "1.1.1.1" will be the first commit on it.

          is_vendor_branch = true;

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
          // "1.3.2.1" is the first commit in the branch.

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

      L(FL("first version in branch %s would be %s") 
        % sym % first_entry_version);
      branch_first_entries.insert(make_pair(first_entry_version, sym));

      string branchpoint_version;
      join_version(branchpoint_components, branchpoint_version);

      if (branchpoint_version.length() > 0)
        {
          // possibly add the branch to a delta
          map< string, shared_ptr<rcs_delta> >::const_iterator di =
            r.deltas.find(branchpoint_version);

          // the delta must exist
          E(di != r.deltas.end(),
            F("delta for a branchpoint is missing (%s)")
              % branchpoint_version);

          shared_ptr<rcs_delta> curr_delta = di->second;

          if (curr_delta->branches.find(first_entry_version) ==
              curr_delta->branches.end())
            curr_delta->branches.insert(first_entry_version);

          if (is_vendor_branch)
            {
              if (r.vendor_branches.find(first_entry_version) ==
                  r.vendor_branches.end())
                r.vendor_branches.insert(first_entry_version);
            }
        }
    }
}

class
cvs_tree_walker
  : public tree_walker
{
  cvs_history & cvs;
  app_state & app;
public:
  cvs_tree_walker(cvs_history & c, app_state & a) : 
    cvs(c), app(a)
  {
  }
  virtual void visit_file(file_path const & path)
  {
    string file = path.as_external();
    if (file.substr(file.size() - 2) == string(",v"))
      {
        try
          {
            import_rcs_file_with_cvs(file, app, cvs);
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
blob_consumer
{
  cvs_history & cvs;
  app_state & app;
  ticker & n_revisions;

  struct branch_state
  {
    revision_id current_rid;
    roster_t ros;
    set<file_path> created_dirs;
    map<cvs_path, cvs_mtn_version> live_files;
  };

  temp_node_id_source nis;
  map<cvs_branchname, branch_state> branch_states;

  blob_consumer(cvs_history & cvs,
                   app_state & app,
                   ticker & n_revs);

  void consume_blob(cvs_blob_index bi);
  void add_missing_parents(branch_state & bstate,
                           file_path const & path, cset & cs);
  void build_cset(const cvs_blob & blob, branch_state & bstate, cset & cs);
};

struct blob_splitter
{
protected:
  cvs_history & cvs;
  set< cvs_blob_index > & cycle_members;

public:
  blob_splitter(cvs_history & c, set< cvs_blob_index > & cm)
    : cvs(c),
      cycle_members(cm)
    { }

  void back_edge(Edge e)
    {
      if (!cycle_members.empty())
        return;

#ifdef DEBUG_BLOB_SPLITTER
      L(FL("blob_splitter: back edge: %d -> %d") % e.first % e.second);
#endif

      if (e.first == e.second)
        {
          // The cycle consists of only one blob - we have to solve an
          // intra blob dependency.
          cycle_members.insert(e.first);
        }
      else
        {
          cycle_members.insert(e.first);
          cycle_members.insert(e.second);

          cvs_blob_index ci = e.second;

          int limit = 1000;
          while (limit > 0)
            {
              I(false);

              cvs_blob & blob = cvs.blobs[ci];

              pair< blob_index_iter, blob_index_iter > d = make_pair(
                blob.get_dependents(cvs).begin(),
                blob.get_dependents(cvs).end());

              // try to find out what blobs belong to that cycle
              blob_index_iter first_grey_blob;
              for (first_grey_blob = d.first; first_grey_blob != d.second;
                   ++first_grey_blob)
                {
                  if (cvs.blobs[*first_grey_blob].colors[0] == grey)
                    break;
                }

              if (cycle_members.find(*first_grey_blob) != cycle_members.end())
                break;

              cycle_members.insert(*first_grey_blob);
              ci = *first_grey_blob;

              limit--;
            }
        }
    }
};


/*
 * single blob split points: search only for intra-blob dependencies
 * and return split points to resolve these dependencies.
 */
time_t
get_best_split_point(cvs_history & cvs, cvs_blob_index bi)
{
  list< pair<time_t, time_t> > ib_deps;

  // Collect the conflicting intra-blob dependencies, storing the
  // timestamps of both events involved.
  for (blob_event_iter i = cvs.blobs[bi].begin();
       i != cvs.blobs[bi].end(); ++i)
    {
      cvs_event_ptr ev = *i;

      for (dependency_iter j = ev->dependencies.begin(); j != ev->dependencies.end(); ++j)
        {
          cvs_event_ptr dep = *j;

          if ((dep->get_digest() == cvs.blobs[bi].get_digest()) &&
              (cvs.get_blob_of(dep) == bi))
            {
              I(ev->time != dep->time);
              if (ev->time > dep->time)
              {
                L(FL("    added IBD range %d - %d") % dep->time % ev->time);
                ib_deps.push_back(make_pair(dep->time, ev->time));
              }
              else
              {
                L(FL("    added reverse IBD range %d - %d") % ev->time % dep->time);
                ib_deps.push_back(make_pair(ev->time, dep->time));
              }
            }
        }
    }

  // What we have now, are durations, or multiple time ranges. We need to
  // split the blob somewhere in that range. But because there are multiple
  // files involved, these ranges can be overlapping, i.e.:
  //
  //           o    o   o       |
  //     A ->  |    |   |       | t
  //           o    |   |       | i
  //     B ->  |    |   |       | m
  //           |    o   |       | e
  //     C ->  |    |   |       |
  //           o    o   o       v
  //
  // From this example, it's clear that we should split at A and C, the
  // first split will resolve three intra-blob dependencies, the second one
  // two. By then, all intra-blob dependencies are resolved. There's no
  // need to split at B.
  //
  // To figure that out, we recursively try to get the split point which
  // resolves the most intra-blob dependencies, until no ones are left. For
  // that, we simply count how many intra-blob deps a split between any two
  // events would resolve.

  typedef list< pair<time_t, time_t> >::iterator dep_ity;

  vector< pair<time_t, time_t> > results;
  set< time_t > event_times;
  for (dep_ity i = ib_deps.begin(); i != ib_deps.end(); ++i)
    {
      if (event_times.find(i->first) == event_times.end())
        event_times.insert(i->first);

      if (event_times.find(i->second) == event_times.end())
        event_times.insert(i->second);
    }

  I(event_times.size() > 1);
  set<time_t>::const_iterator last, curr;
  last = event_times.begin();
  curr = last;
  curr++;
  pair<time_t, time_t> best_split_range = make_pair(0, 0);
  vector<dep_ity> deps_resolved_by_best_split;
  unsigned int best_score = 0;
  for ( ; curr != event_times.end(); ++curr)
    {
      time_t curr_split_point = *last + (*curr - *last) / 2;

      L(FL("  testing split point @%d") % curr_split_point);

      // just to get everything right here...
      I(*curr > *last);
      I(curr_split_point < *curr);
      I(curr_split_point >= *last);

      unsigned int num_deps_resolved = 0;
      for (dep_ity i = ib_deps.begin(); i != ib_deps.end(); ++i)
        if ((curr_split_point >= i->first) &&
            (curr_split_point < i->second))
          num_deps_resolved++;

      if (num_deps_resolved > best_score)
        {
          I(*curr > *last);
          best_split_range = make_pair(*last, *curr);
          best_score = num_deps_resolved;
        }

      last = curr;
    }

  I(best_score > 0);
  vector<dep_ity>::const_iterator i;
  for (i = deps_resolved_by_best_split.begin();
       i != deps_resolved_by_best_split.end(); ++i)
    ib_deps.erase(*i);

  // FIXME: we should check if there are events in that range, which are
  //        not involved in the dependency cycle, but for which we need to
  //        decide where to put them.

  time_t best_split_point = best_split_range.first +
    (best_split_range.second - best_split_range.first) / 2;

  L(FL("Best split range: %d - %d (@%d)")
    % best_split_range.first % best_split_range.second % best_split_point);

  return best_split_point;
}

void
split_blob_at(cvs_history & cvs, const cvs_blob_index bi,
              time_t split_point);

void
split_cycle(cvs_history & cvs, set< cvs_blob_index > const & cycle_members)
{
  cvs_blob_index blob_to_split;

  /* shortcut for intra blob dependencies */
  I(cycle_members.size() > 0);
  if (cycle_members.size() == 1)
    {
      L(FL("should split blob %d") % *cycle_members.begin());
      blob_to_split = *cycle_members.begin();

      time_t split_point = get_best_split_point(cvs, blob_to_split);
      I(split_point > 0);

      split_blob_at(cvs, blob_to_split, split_point);
    }
  else
    {
      L(FL("choosing a blob to split (out of %d blobs)") % cycle_members.size());

      // convert the set to a map
      map< int, cvs_blob_index > mymap;
      unsigned int cc = 0;
      for (set< cvs_blob_index >::const_iterator i = cycle_members.begin();
           i != cycle_members.end(); ++i)
        {
          mymap[cc] = *i;
          cc++;
        }

      time_t largest_gap = 0;
      time_t largest_gap_at = 0;
      int largest_gap_blob = -1;

      cc = 0;
      for (cc = 0; cc < mymap.size(); ++cc)
        {
          L(FL("  testing blob %d") % mymap[cc]);

          // sort the blob events by timestamp
          vector< cvs_event_ptr > & blob_events = cvs.blobs[mymap[cc]].get_events();
          sort(blob_events.begin(), blob_events.end());

          blob_event_iter ity;

          cvs_event_ptr this_ev, last_ev;

          ity = blob_events.begin();
          this_ev = *ity;
          ++ity;
          for ( ; ity != blob_events.end(); ++ity)
            {
              last_ev = this_ev;
              this_ev = *ity;

              time_t time_diff = this_ev->time - last_ev->time;
              L(FL("       time gap: %s") % time_diff);

              if (time_diff > largest_gap)
                {
                  largest_gap = time_diff;
                  largest_gap_at = last_ev->time + time_diff / 2;
                  largest_gap_blob = mymap[cc];
                }
            }

          for (unsigned int dd = 0; dd < mymap.size(); ++dd)
            {
              for (blob_event_iter ii = cvs.blobs[mymap[cc]].begin(); ii != cvs.blobs[mymap[cc]].end(); ++ii)
                {
                  cvs_event_ptr ev = *ii;
                  for (dependency_iter j = ev->dependencies.begin(); j != ev->dependencies.end(); ++j)
                    {
                      cvs_event_ptr dep = *j;

                      if (dep->get_digest() == cvs.blobs[mymap[dd]].get_digest())
                        {
                          L(FL("     depends on blob %d") % mymap[dd]);
                        }
                    }
                }
            }
        }

      I(largest_gap_at != 0);
      I(largest_gap_blob >= 0);
      split_blob_at(cvs, largest_gap_blob, largest_gap_at);
    }
}

void
split_blob_at(cvs_history & cvs, const cvs_blob_index bi,
              time_t split_point)
{
  L(FL(" splitting blob %d at %d") % bi % split_point);

  // Sort the blob events by timestamp
  vector< cvs_event_ptr > blob_events(cvs.blobs[bi].get_events());
  sort(blob_events.begin(), blob_events.end());

  // Add a blob
  cvs_event_digest d = cvs.blobs[bi].get_digest();
  cvs_blob_index new_bi = cvs.add_blob(d)->second;

  // Reset the dependency cache of the origin blob.
  cvs.blobs[bi].reset_deps_cache();

  // For branches and tags, we need to keep track of the original blob and
  // increment its split counter.
  if (d.is_branch() || d.is_tag())
  {
    cvs_blob_index origin = cvs.blobs[bi].split_origin;
    if (origin == invalid_blob)
      origin = bi;
    I(origin < new_bi);

    cvs.blobs[new_bi].split_origin = origin;
    cvs.blobs[origin].split_counter++;
    cvs.blobs[new_bi].split_index = cvs.blobs[origin].split_counter;
  }

  // Reassign all events and split into the two blobs
  cvs.blobs[bi].get_events().clear();
  I(!blob_events.empty());

  for (blob_event_iter i = blob_events.begin(); i != blob_events.end(); ++i)
    {
      // Reset the dependents cache of all dependencies.
      for (dependency_iter j = (*i)->dependencies.begin();
           j != (*i)->dependencies.end(); ++j)
        {
          cvs_blob_index dep_bi = cvs.get_blob_of(*j);
          cvs.blobs[dep_bi].reset_deps_cache();
        }

      // Assign the event to the existing or to the new blob
      if ((*i)->time <= split_point)
        cvs.blobs[bi].push_back(*i);
      else
        cvs.blobs[new_bi].push_back(*i);
    }

  I(!cvs.blobs[bi].empty());
  I(!cvs.blobs[new_bi].empty());
}

#ifdef DEBUG_GRAPHVIZ

class blob_label_writer
{
  public:
    cvs_history & cvs;

    blob_label_writer(cvs_history & c) : cvs(c) {};

    template <class VertexOrEdge>
    void operator()(std::ostream & out, const VertexOrEdge & v) const
    {
      string label;
      cvs_blob b = cvs.blobs[v];

      if (b.get_digest().is_commit())
        {
          L(FL("blob %d: commit") % v);

          label = (FL("blob %d: commit\\n") % v).str();

          if (b.empty())
            {
              label += "empty blob!!!";
            }
          else
            {
          string author, clog;
          const shared_ptr< cvs_commit > ce =
            boost::static_pointer_cast<cvs_commit, cvs_event>(*b.begin());

          cvs.split_authorclog(ce->authorclog, author, clog);
          label += author + "\\n";

          // limit length of changelog we output
          if (clog.length() > 20)
            clog = clog.substr(20);

          // poor man's escape...
          for (unsigned int i = 0; i < clog.length(); ++i)
            {
              if (clog[i] < 32)
                clog[i] = '?';
              if (clog[i] == '\"')
                clog[i] = '\'';
            }
          label += "\\\"" + clog + "\\\"\\n";
            }
        }
      else if (b.get_digest().is_branch())
        {
          L(FL("blob %d: branch") % v);

          label = (FL("blob %d: branch: ") % v).str();

          if (b.empty())
            {
              label += "empty blob!!!";
            }
          else
            {
          const shared_ptr< cvs_event_branch > cb =
            boost::static_pointer_cast<cvs_event_branch, cvs_event>(*b.begin());

          label += cvs.branchname_interner.lookup(cb->branchname);
          label += "\\n";
            }
        }
      else if (b.get_digest().is_tag())
        {
          L(FL("blob %d: tag") % v);

          label = (FL("blob %d: tag: ") % v).str();

          if (b.empty())
            {
              label += "empty blob!!!";
            }
          else
            {
          const shared_ptr< cvs_event_tag > cb =
            boost::static_pointer_cast<cvs_event_tag, cvs_event>(*b.begin());

          label += cvs.tag_interner.lookup(cb->tag);
          label += "\\n";
            }
        }
      else
        {
          label = (FL("blob %d: unknow type\\n") % v).str();
        }

      if (!b.empty())
        {
      // print the time of the blob
      label += (FL("time: %d\\n") % (*b.begin())->time).str();
      label += "\\n";

      // print the contents of the blob, i.e. the single files
        for (blob_event_iter i = b.begin(); i != b.end(); i++)
          {
            label += cvs.path_interner.lookup((*i)->path);

            if (b.get_digest().is_commit())
              {
                const shared_ptr< cvs_commit > ce =
                  boost::static_pointer_cast<cvs_commit, cvs_event>(*i);

                label += "@";
                label += cvs.rcs_version_interner.lookup(ce->rcs_version);

                label += (FL(":%d") % (ce->time)).str();
              }
            label += "\\n";
          }
      else
        label += "-- empty --";

      out << "[label=\"" << label << "\"]";
    }
};
#endif


vector<cvs_blob_index> & cvs_blob::get_dependents(cvs_history & cvs)
{
  if (has_cached_deps)
    return dependents_cache;

  // fill the dependency cache from the single event deps
  for (blob_event_iter i = begin(); i != end(); ++i)
    {
      for (dependency_iter j = (*i)->dependents.begin();
           j != (*i)->dependents.end(); ++j)
        {
          cvs_blob_index dep_bi = cvs.get_blob_of(*j);
          if (find(dependents_cache.begin(), dependents_cache.end(), dep_bi) == dependents_cache.end())
            dependents_cache.push_back(dep_bi);
        }
    }

  // sort by timestamp
  sort(dependents_cache.begin(), dependents_cache.end());

  has_cached_deps = true;
  return dependents_cache;
}


void cvs_history::depth_first_search(blob_splitter & vis,
       back_insert_iterator< vector<cvs_blob_index> > oi)
  {
    dfs_context ctx;

    for (vector<cvs_blob>::iterator ity = blobs.begin();
         ity != blobs.end(); ++ity)
      ity->colors[0] = white;

    // start with blob 0
    ctx.bi = 0;
    // vis.discover_vertex(bi);
    blobs[ctx.bi].colors[0] = grey;
    ctx.ei = blobs[ctx.bi].get_dependents(*this).begin();

    stack< dfs_context > stack;

    // possible optimization for vector based stack:
    // stack.reserve()

    stack.push(ctx);

    while (!stack.empty())
      {
        dfs_context ctx = stack.top();
        stack.pop();
        while (ctx.ei != blobs[ctx.bi].get_dependents(*this).end())
          {
            // vis.examine_edge(*ei, g);
            if (blobs[*ctx.ei].colors[0] == white)
              {
                // vis.tree_edge(bi -> *ei);

                // push the current context to the stack and, but
                // advance to the next edge, as we are processing this
                // one just now.
                stack.push(ctx);
                stack.top().ei++;

                // switch to that blob and follow its edges
                ctx.bi = *ctx.ei;
                blobs[ctx.bi].colors[0] = grey;
                // vis.discover_vertex(bi, g);
                ctx.ei = blobs[ctx.bi].get_dependents(*this).begin();
              }
            else if (blobs[*ctx.ei].colors[0] == grey)
              {
                // vis.back_edge(bi -> *ei);
                ++ctx.ei;
              }
            else
              {
                // vis.forward_or_cross_edge(bi -> *ei);
                ++ctx.ei;
              }
          }
        blobs[ctx.bi].colors[0] = black;
        // vis.finish_vertex(bi, g);

        // output the blob index in postordering fashion (for later
        // use as topologically sorted list)
        *oi++ = ctx.bi;
      }
  }


//
// After stuffing all cvs_events into blobs of events with the same
// author and changelog, we have to make sure their dependencies are
// respected.
//
void
resolve_blob_dependencies(cvs_history & cvs,
                          app_state & app,
                          ticker & n_revs)
{
  L(FL("Breaking dependency cycles (%d blobs)") % cvs.blobs.size());

#ifdef DEBUG_GRAPHVIZ
  int step_no = 1;
  std::ofstream viz_file;
  blob_label_writer blw(cvs);
#endif

  vector<cvs_blob_index> import_order;

  while (1)
  {
    // this set will be filled with the blobs in a cycle
    set< cvs_blob_index > cycle_members;

    blob_splitter vis(cvs, cycle_members);

#ifdef DEBUG_GRAPHVIZ
    if (global_sanity.debug)
      {
        viz_file.open((FL("cvs_graph.%d.viz") % step_no).str().c_str());
        boost::write_graphviz(viz_file, g, blw);
        viz_file.close();
        step_no++;
      }
#endif

    cvs.depth_first_search(vis, back_inserter(import_order));

    // If we have a cycle, go split it. Otherwise we don't have any
    // cycles left and can proceed.
    if (!cycle_members.empty())
      split_cycle(cvs, cycle_members);
    else
      break;
  };

  // After a depth-first-search-run without any cycles or illegal forward
  // or cross edges, we have a possible, but reverse order which satisfies
  // all the dependencies (topologically sorted).
  blob_consumer cons(cvs, app, n_revs);
  for (vector<cvs_blob_index>::const_reverse_iterator i = import_order.rbegin();
       i != import_order.rend(); ++i)
    {
      L(FL("next blob number from toposort: %d") % *i);
      cons.consume_blob(*i);
    }
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
  N(app.opts.branchname() != "", F("need base --branch argument for importing"));

  // add the trunk branch name
  string bn = app.opts.branchname();
  cvs.base_branch = cvs.branchname_interner.intern(bn);


  //
  // first step of importing legacy VCS: collect all revisions
  // of all files we know. This already creates file deltas and
  // hashes. We end up with a DAG of blobs,
  {
    transaction_guard guard(app.db);
    cvs_tree_walker walker(cvs, app);
    require_path_is_directory(cvsroot,
                              F("path %s does not exist") % cvsroot,
                              F("'%s' is not a directory") % cvsroot);
    app.db.ensure_open();
    change_current_working_dir(cvsroot);
    walk_tree(file_path(), walker);
    guard.commit();
  }

  ticker n_revs(_("revisions"), "r", 1);

  {
    transaction_guard guard(app.db);
    resolve_blob_dependencies(cvs, app, n_revs);
    guard.commit();
  }

  // add all the tags
  {
    ticker n_tags(_("tags"), "t", 1);
    transaction_guard guard(app.db);
    for (map<cvs_tag, revision_id>::const_iterator i = cvs.resolved_tags.begin();
         i != cvs.resolved_tags.end(); ++i)
      {
        string tag = cvs.tag_interner.lookup(i->first);
        ui.set_tick_trailer("marking tag " + tag);
        app.get_project().put_tag(i->second, tag);
        ++n_tags;
      }
    guard.commit();
  }

  return;
}

blob_consumer::blob_consumer(cvs_history & cvs,
                             app_state & app,
                             ticker & n_revs)
  : cvs(cvs),
    app(app),
    n_revisions(n_revs)
{
  // add an initial branch state for the trunk
  branch_states.insert(make_pair(
    cvs.base_branch, branch_state()));
}

void
blob_consumer::add_missing_parents(branch_state & bstate,
                                   file_path const & path, cset & cs)
{
  if (bstate.created_dirs.find(path) != bstate.created_dirs.end())
    return;

  if (!path.empty())
    add_missing_parents(bstate, path.dirname(), cs);

  safe_insert(bstate.created_dirs, path);
  safe_insert(cs.dirs_added, path);
}

void
blob_consumer::build_cset(const cvs_blob & blob,
                          branch_state & bstate,
                          cset & cs)
{
  I(blob.in_branch != invalid_blob);

  map<cvs_path, cvs_mtn_version> & branch_live_files =
    bstate.live_files;

  for (blob_event_iter i = blob.begin(); i != blob.end(); ++i)
    {
      I((*i)->get_digest().is_commit());

      shared_ptr<cvs_commit> ce =
        boost::static_pointer_cast<cvs_commit, cvs_event>(*i);

      file_path pth = file_path_internal(cvs.path_interner.lookup(ce->path));

      L(FL("blob_consumer::build_cset: file_path: %s") % pth);


      file_id fid(cvs.mtn_version_interner.lookup(ce->mtn_version));

      if (ce->alive)
        {
          map<cvs_path, cvs_mtn_version>::const_iterator e =
            branch_live_files.find(ce->path);

          if (e == branch_live_files.end())
            {
              add_missing_parents(bstate, pth.dirname(), cs);
              L(FL("adding entry state '%s' on '%s'") % fid % pth);
              safe_insert(cs.files_added, make_pair(pth, fid));
              branch_live_files[ce->path] = ce->mtn_version;
            }
          else if (e->second != ce->mtn_version)
            {
              file_id old_fid(cvs.mtn_version_interner.lookup(e->second));
              L(FL("applying state delta on '%s' : '%s' -> '%s'")
                % pth % old_fid % fid);
              safe_insert(cs.deltas_applied,
                          make_pair(pth, make_pair(old_fid, fid)));
              branch_live_files[ce->path] = ce->mtn_version;
            }
        }
      else
        {
          map<cvs_path, cvs_mtn_version>::const_iterator e =
            branch_live_files.find(ce->path);

          if (e != branch_live_files.end())
            {
              L(FL("deleting entry state '%s' on '%s'") % fid % pth);
              safe_insert(cs.nodes_deleted, pth);
              branch_live_files.erase(ce->path);
            }
        }
    }
}

void
blob_consumer::consume_blob(cvs_blob_index bi)
{
  cvs_blob & blob = cvs.blobs[bi];
  I(bi == cvs.get_blob_of(*blob.begin()));

  // Search through all direct dependencies and check what branches
  // those are in.
  set< cvs_blob_index > dep_branches;
  for (blob_event_iter i = blob.begin(); i != blob.end(); ++i)
    {
      cvs_event_ptr ev = *i;

      for (dependency_iter j = ev->dependencies.begin();
           j != ev->dependencies.end(); ++j)
        {
          cvs_event_ptr dep = *j;

          cvs_blob_index dep_bi = cvs.get_blob_of(dep);
          cvs_blob & dep_blob = cvs.blobs[dep_bi];

          // The blob we depend on must have been imported already, and thus
          // must already be in a branch.
          I(dep_blob.in_branch != invalid_blob);

          // If this blob depends on a branch event, we have to check if
          // this blob is one of the first blobs in the branch, i.e. the
          // start of the branch's contents. In that case, we add the
          // branch to the dep_branches.
          if (dep_blob.get_digest().is_branch())
            {
              for (blob_event_iter l = dep_blob.begin();
                   l != dep_blob.end(); ++l)
                {
                  shared_ptr<cvs_event_branch> cbe =
                    boost::static_pointer_cast<cvs_event_branch,
                                               cvs_event>(*l);

                  vector< cvs_event_ptr >::const_iterator k;
                  for (k = cbe->branch_contents.begin();
                       k != cbe->branch_contents.end(); ++k)
                    {
                      cvs_blob_index cont_bi = cvs.get_blob_of(*k);
                      cvs_blob_index my_bi = cvs.get_blob_of(*blob.begin());
                      if (my_bi == cont_bi)
                        if (dep_branches.find(dep_bi) == dep_branches.end())
                          dep_branches.insert(dep_bi);
                    }
                }
            }
          else
            {
              // For commits and tags, we add that blob's branch.
              if (dep_branches.find(dep_blob.in_branch) == dep_branches.end())
                dep_branches.insert(dep_blob.in_branch);
            }
        }
    }

  if (dep_branches.size() > 0)
    {
      set< cvs_blob_index >::const_iterator i;

#ifdef DEBUG_BRANCH_REDUCTION
      // this is only for debug information
      L(FL("This blob depends on the following branches:"));
      for (i = dep_branches.begin(); i != dep_branches.end(); ++i)
        L(FL("  branch %s") % cvs.get_branchname(static_cast<cvs_event_branch&>(**cvs.blobs[*i].begin()).branchname));
#endif

      // eliminate direct parent branches
      bool set_modified;
      do
      {
        set_modified = false;
        cvs_blob_index my_bi;
        shared_ptr< cvs_event_branch > cbe;
        for (i = dep_branches.begin(); i != dep_branches.end(); ++i)
          {
            // For each branch that this blob depends on, we check if that
            // branch is a parent of another branch we depend on. In that
            // case, we can remove the parent branch dependency and only
            // keep the newest branch dependency.
            I(*i != invalid_blob);
            my_bi = *i;
            while (cvs.blobs[my_bi].in_branch != invalid_branch)
              {
#ifdef DEBUG_BRANCH_REDUCTION
                L(FL("       checking branch %d: %s") % my_bi
                  % cvs.branchname_interner.lookup(
                    boost::static_pointer_cast<cvs_event_branch, cvs_event>(
                      *cvs.blobs[my_bi].begin())->branchname));
#endif

                if (dep_branches.find(cvs.blobs[my_bi].in_branch) !=
                    dep_branches.end())
                  {
                    // remove that branch, since it's a parent of another
                    // branch this blob depends on.
                    dep_branches.erase(cvs.blobs[my_bi].in_branch);
                    set_modified = true;
                  }

                // continue to remove grand-parents
                I(cvs.blobs[my_bi].in_branch != invalid_blob);
                cvs_blob_index new_bi = cvs.blobs[my_bi].in_branch;

                if (my_bi == new_bi)
                  break;

                I((boost::static_pointer_cast<cvs_event_branch, cvs_event>(
                    *(cvs.blobs[my_bi].begin()))->branchname !=
                   boost::static_pointer_cast<cvs_event_branch, cvs_event>(
                    *(cvs.blobs[new_bi].begin()))->branchname));

                my_bi = new_bi;
              }

            if (set_modified)
              break;
          }

      } while (set_modified);

#ifdef DEBUG_BRANCH_REDUCTION
      // this is only for debug information
      L(FL("After elimination of parent branches, this blob depends on:"));
      for (i = dep_branches.begin(); i != dep_branches.end(); ++i)
        L(FL("  branch %s") % cvs.get_branchname(static_cast<cvs_event_branch&>(**cvs.blobs[*i].begin()).branchname));
#endif

      // FIXME: this invariant gets violated by tests
      I(dep_branches.size() <= 1);
    }

  if (dep_branches.size() == 1)
    blob.in_branch = *dep_branches.begin();
  else
    blob.in_branch = cvs.base_branch;

  cvs_branchname in_branch = static_cast<cvs_event_branch &>(
    **cvs.blobs[blob.in_branch].begin()).branchname;

  I(branch_states.find(in_branch) != branch_states.end());
  branch_state & bstate = branch_states.find(in_branch)->second;

#ifdef DEBUG_BRANCH_REDUCTION
  L(FL("  branch %s is at revision: %s") % cvs.get_branchname(in_branch) % bstate.current_rid);
#endif

  if (blob.get_digest().is_commit())
    {
      // we should never have an empty blob; it's *possible* to have
      // an empty changeset (say on a vendor import) but every blob
      // should have been created by at least one file commit, even
      // if the commit made no changes. it's a logical inconsistency if
      // you have an empty blob.
      I(!blob.empty());

      shared_ptr<cvs_commit> ce =
        boost::static_pointer_cast<cvs_commit, cvs_event>(*blob.begin());

      revision_id parent_rid, child_rid;
      parent_rid = bstate.current_rid;

      if (null_id(parent_rid))
        W(F("Warning: null parent_rid, should better be the root"));

      shared_ptr<revision_t> rev(new revision_t());
      shared_ptr<cset> cs(new cset());

      build_cset(blob, bstate, *cs);

      editable_roster_base editable_ros(bstate.ros, nis);

      cs->apply_to(editable_ros);
      manifest_id child_mid;
      calculate_ident(bstate.ros, child_mid);

      rev->made_for = made_for_database;
      rev->new_manifest = child_mid;
      rev->edges.insert(make_pair(parent_rid, cs));

      calculate_ident(*rev, child_rid);

      if (!app.opts.dryrun)
        {
          if (app.db.put_revision(child_rid, *rev))
            {
              // FIXME: calculate an avg time
              time_t commit_time = ce->time;
              string author, changelog;

              cvs.split_authorclog(ce->authorclog, author, changelog);
              string bn = cvs.get_branchname(in_branch);
              app.get_project().put_standard_certs(child_rid,
                branch_name(bn),
                utf8(changelog),
                date_t::from_unix_epoch(commit_time),
                utf8(author));

              ++n_revisions;
            }
          else
            I(false);  // revision shouldn't exist already!
        }

      bstate.current_rid = child_rid;
    }
  else if (blob.get_digest().is_branch())
    {
      if (!blob.empty())
        {
          shared_ptr<cvs_event_branch> cbe =
            boost::static_pointer_cast<cvs_event_branch, cvs_event>(
              *blob.begin());

          if (blob.split_counter != 0)
            {
              int idx = (blob.split_origin == bi ? 0 : blob.split_index);

              // As the get_blob_of handles invalid entries in the blob_index
              // just well, we don't care to remove the old entry for that
              // blob.

              // cvs.blob_index.remove(make_pair(cte->tag->get_digest(), bi));

              string branchname = cvs.branchname_interner.lookup(cbe->branchname);
              if (branchname.empty())
                branchname = (FL(";UNNAMED_BRANCH#%d") % idx).str();
              else
                branchname = (FL("%s#%d") % cbe->branchname % idx).str();

              cbe->branchname = cvs.branchname_interner.intern(branchname);

              for (blob_event_iter i = blob.begin(); i != blob.end(); ++i)
                static_cast< cvs_event_branch & >(**i).branchname = cbe->branchname;

              cvs.blob_index.insert(make_pair(cbe->get_digest(), bi));
            }

          // Set the base revision of the branch to the branchpoint revision
          // and initialize the map of live files.
          // FIXME: this is certainly bogus:
          if (cbe->branchname != in_branch)
            {
              I(cbe->branchname != cvs.base_branch);
              I(branch_states.find(cbe->branchname) == branch_states.end());

              // copy the branch state
              branch_states.insert(make_pair(cbe->branchname,
                branch_state(bstate)));
            }
          else
            {
              // this must be the trunk?
            }
        }
    }
  else if (blob.get_digest().is_tag())
    {
      if (!blob.empty())
        {
          shared_ptr<cvs_event_tag> cte =
            boost::static_pointer_cast<cvs_event_tag, cvs_event>(
              *blob.begin());

          if (blob.split_counter > 0)
            {
              int idx = (blob.split_origin == bi ? 0 : blob.split_index);

              // As the get_blob_of handles invalid entries in the blob_index
              // just well, we don't care to remove the old entry for that
              // blob.

              // cvs.blob_index.remove(make_pair(cte->tag->get_digest(), bi));

              string tagname = (FL("%s#%d") % cvs.tag_interner.lookup(cte->tag)
                                           % idx).str();
              cte->tag = cvs.tag_interner.intern(tagname);

              for (blob_event_iter i = blob.begin(); i != blob.end(); ++i)
                static_cast< cvs_event_tag & >(**i).tag = cte->tag;

              cvs.blob_index.insert(make_pair(cte->get_digest(), bi));
            }

          cvs.resolved_tags.insert(make_pair(cte->tag, bstate.current_rid));
        }
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
