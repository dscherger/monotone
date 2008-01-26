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
#include "vector.hh"
#include <list>

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
#include "merge.hh"
#include "paths.hh"
#include "platform-wrapped.hh"
#include "project.hh"
#include "rcs_file.hh"
#include "revision.hh"
#include "roster_merge.hh"
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
using std::stack;
using std::string;
using std::vector;
using std::deque;
using std::list;
using std::insert_iterator;
using std::back_insert_iterator;
using std::for_each;
using std::swap;

using boost::scoped_ptr;
using boost::shared_ptr;
using boost::lexical_cast;

// additional debugging information
// not defined: DEBUG_BLOB_SPLITTER
// not defined: DEBUG_GRAPHVIZ
// not defined: DEBUG_DIJKSTRA

// cvs history recording stuff

typedef u32 cvs_authorclog;
typedef u32 cvs_mtn_version;   // the new file id in monotone
typedef u32 cvs_rcs_version;   // the old RCS version number
typedef u32 cvs_symbol_no;
typedef u32 cvs_path;

const cvs_symbol_no invalid_symbol = cvs_symbol_no(-1);

typedef enum
{
  ET_COMMIT = 0,
  ET_TAG_POINT = 1,
  ET_BRANCH_POINT = 2,
  ET_BRANCH_START = 3,
  ET_BRANCH_END = 4,
  ET_SYMBOL = 5
} event_type;

typedef u64 time_i;

struct cvs_history;

struct cvs_event_digest
{
  u32 digest;

  cvs_event_digest(const event_type t, const unsigned int v)
    {
      I(sizeof(struct cvs_event_digest) == 4);

      I(v < ((u32) 1 << 29));
      I(t < 8);
      digest = t << 29 | v;
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
      return digest >> 29 == (u32) ET_COMMIT;
    }

  bool is_symbol() const
    {
      return digest >> 29 == (u32) ET_SYMBOL;
    }

  bool is_tag() const
    {
      return digest >> 29 == (u32) ET_TAG_POINT;
    }

  bool is_branch_start() const
    {
      return digest >> 29 == (u32) ET_BRANCH_START;
    }

  bool is_branch_end() const
    {
      return digest >> 29 == (u32) ET_BRANCH_END;
    }
};

std::ostream & operator<<(std::ostream & o, struct cvs_event_digest const & d)
{
  return o << d.digest;
}

class cvs_event;

typedef cvs_event* cvs_event_ptr;

class cvs_blob;
typedef vector<cvs_blob>::size_type cvs_blob_index;
typedef vector<cvs_blob_index>::const_iterator blob_index_iter;

struct
cvs_event
{
public:
  time_i adj_time;
  cvs_path path;
  cvs_blob_index bi;

  // symbol constructor
  cvs_event(const cvs_path p, const time_i ti)
    : adj_time(ti * 100),
      path(p)
    { };
};

struct
cvs_commit : cvs_event
{
  time_i given_time;

  // additional information for commits
  cvs_mtn_version mtn_version;
  cvs_rcs_version rcs_version;
  bool alive;

  // commit constructor
  cvs_commit(const cvs_path p, const time_i ti, const cvs_mtn_version v,
             const cvs_rcs_version r, const bool al)
    : cvs_event(p, ti),
      given_time(ti),
      mtn_version(v),
      rcs_version(r),
      alive(al)
    { };
};

typedef vector< cvs_event_ptr >::const_iterator blob_event_iter;

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
  vector< cvs_event_ptr > events;

  bool has_cached_deps;
  bool events_are_sorted;
  bool cached_deps_are_sorted;
  vector<cvs_blob_index> dependents_cache;

public:
  event_type etype;
  union {
    cvs_authorclog authorclog;    // for commit events
    cvs_symbol_no symbol;         // for all symbol events
  };

  cvs_symbol_no in_branch;
  revision_id assigned_rid;

  // helper field for Depth First Search algorithm
  dfs_color color;

  cvs_blob(const event_type t, const u32 no)
    : has_cached_deps(false),
      events_are_sorted(true),
      cached_deps_are_sorted(false),
      etype(t),
      symbol(no)
    { };

  cvs_blob(const cvs_blob & b)
    : events(b.events),
      has_cached_deps(false),
      events_are_sorted(false),
      cached_deps_are_sorted(false),
      etype(b.etype),
      symbol(b.symbol)
    { };

  void push_back(cvs_event_ptr c)
    {
      events.push_back(c);
      events_are_sorted = false;
    }

  vector< cvs_event_ptr > & get_events()
    {
      return events;
    }

  void clear()
    {
      events.clear();
    }

  blob_event_iter begin() const
    {
      return events.begin();
    }

  blob_event_iter end() const
    {
      return events.end();
    }

  vector<cvs_event_ptr>::iterator begin()
    {
      return events.begin();
    }

  vector<cvs_event_ptr>::iterator end()
    {
      return events.end();
    }

  bool empty() const
    {
      return events.empty();
    }

  const cvs_event_digest get_digest() const
    {
      return cvs_event_digest(etype, symbol);
    }

  vector<cvs_blob_index> & get_dependents(cvs_history & cvs);
  void sort_deps_cache(cvs_history & cvs);

  void reset_deps_cache(void)
    {
      has_cached_deps = false;
    }

  void resort_deps_cache(void)
    {
      cached_deps_are_sorted = false;
    }

  u64 get_avg_time(void) const
    {
      time_i avg = 0;
      for (blob_event_iter i = events.begin(); i != events.end(); ++i)
        avg += (*i)->adj_time;
      avg /= events.size();
      return avg;
    }

  void sort_events(void);

  int build_cset(cvs_history & cvs,
                 roster_t & base_ros,
                 cset & cs);

  void add_missing_parents(file_path const & path,
                           roster_t & base_ros, cset & cs);
};

typedef struct
{
  cvs_blob_index bi;
  blob_index_iter ei;
} dfs_context;

// okay, this is a little memory hackery...
#define POOL_SIZE (1024 * 1024 * 4)
struct event_pool
{
  char *pool_start, *pool_end;

  event_pool(void)
    : pool_start(NULL),
      pool_end(NULL)
    { };

  template<typename T>
  T *allocate(void)
    {
      int s = sizeof(T);

      if ((pool_end - pool_start) <= s)
        {
          pool_start = (char*) malloc(POOL_SIZE);
          pool_end = pool_start + POOL_SIZE;
        }

      I((pool_end - pool_start) > s);
      T* res = (T*) pool_start;
      memset(res, 0, sizeof(T));
      pool_start += s;
      return res;
    }
};

struct blob_splitter;

string get_event_repr(cvs_history & cvs, cvs_event_ptr ev);

typedef pair<cvs_event_ptr, cvs_event_ptr> event_dep;
typedef vector<event_dep>::iterator event_dep_iter;

class
dep_loop
{
private:
  event_dep_iter pos, end;

public:
  dep_loop(event_dep_iter p, event_dep_iter e)
    : pos(p),
      end(e)
    { };

  bool
  ended(void) const
    {
      return (pos == end);
    }

  event_dep_iter
  get_pos(void) const
    {
      return pos;
    }

  cvs_event_ptr
  operator *(void) const
    {
      return pos->second;
    };

  void
  operator ++(void)
    {
      ++pos;

      // skip erased dependencies
      while (!ended() && !pos->first && !pos->second)
        ++pos;
    };
};

class
dep_checker
{
private:
  cvs_blob_index bi, dep_bi;

public:
  dep_checker(const cvs_blob_index bi, const cvs_blob_index dep_bi)
    : bi(bi),
      dep_bi(dep_bi)
    { };

  bool
  operator () (const event_dep & e)
    {
      return (e.first->bi == bi) && (e.second->bi == dep_bi);
    }
};

struct
cvs_history
{
  event_pool ev_pool;

  interner<u32> authorclog_interner;
  interner<u32> mtn_version_interner;
  interner<u32> rcs_version_interner;
  interner<u32> symbol_interner;
  interner<u32> path_interner;

  // all the blobs of the whole repository
  vector<cvs_blob> blobs;

  // all the blobs by their event_digest
  multimap<cvs_event_digest, cvs_blob_index> blob_index;

  // assume an RCS file has foo:X.Y.0.N in it, then
  // this map contains entries of the form
  // X.Y.N.1 -> foo
  // this map is cleared for every RCS file.
  map<string, string> branch_first_entries;

  // event dependency tracking vectors
  vector<event_dep> dependencies;
  vector<event_dep> weak_dependencies;
  vector<event_dep> dependents;

  // final ordering of the blobs for the import
  vector<cvs_blob_index> import_order;

  file_path curr_file;
  cvs_path curr_file_interned;

  cvs_symbol_no base_branch;
  cvs_blob_index root_blob;
  cvs_event_ptr root_event;

  ticker n_rcs_revisions;
  ticker n_rcs_symbols;

  int unnamed_branch_counter;

  // step number of graphviz output, when enabled.
  int step_no;

  bool deps_sorted;

  cvs_history(void)
    : n_rcs_revisions(_("RCS revisions"), "r", 1),
      n_rcs_symbols(_("RCS symbols"), "s", 1),
      unnamed_branch_counter(0),
      step_no(0),
      deps_sorted(false)
    { };

  void set_filename(string const & file,
                    file_id const & ident);

  void index_branchpoint_symbols(rcs_file & r);

  blob_index_iterator add_blob(const event_type t, const u32 no)
  {
    // add a blob..
    cvs_blob_index i = blobs.size();
    blobs.push_back(cvs_blob(t, no));

    // ..and an index entry for the blob
    blob_index_iterator j = blob_index.insert(make_pair(cvs_event_digest(t, no), i));
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
  get_blobs(const event_type t, const u32 no, bool create)
  {
    cvs_event_digest d(t, no);
    pair<blob_index_iterator, blob_index_iterator> range = 
      blob_index.equal_range(d);

    if ((range.first == range.second) && create)
      {
        range.first = add_blob(t, no);
        range.second = range.first;
        range.second++;
        return range;
      }

    I(range.first != range.second);
    return range;
  }

  cvs_blob_index get_or_create_blob(const event_type t, const u32 no)
  {
    pair<blob_index_iterator, blob_index_iterator> range =
      get_blobs(t, no, true);

    cvs_blob_index res = range.first->second;

    // make sure we found only one such blob
    I(++range.first == range.second);

    return res;
  }

  void append_event_to(const cvs_blob_index bi, cvs_event_ptr c)
  {
    blobs[bi].push_back(c);
    c->bi = bi;
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

  string get_branchname(const cvs_symbol_no bname)
  {
    if (bname == base_branch)
      return symbol_interner.lookup(base_branch);
    else
      {
        string branchname(symbol_interner.lookup(bname));
        if (branchname.empty())
          return branchname;
        else
          return symbol_interner.lookup(base_branch) +
            "." + branchname;
      }
  }

  template<typename Visitor>
  void depth_first_search(Visitor & vis,
                          back_insert_iterator< vector<cvs_blob_index> > oi);

  void sort_dependencies(void)
  {
    sort(dependencies.begin(), dependencies.end());
    sort(dependents.begin(), dependents.end());
    deps_sorted = true;
  }

  void add_dependency(cvs_event_ptr ev, cvs_event_ptr dep)
  {
    /* Adds dep as a dependency of ev. */
    I(ev != dep);
    dependencies.push_back(make_pair(ev, dep));
    dependents.push_back(make_pair(dep, ev));
    deps_sorted = false;
  }

  void add_weak_dependency(cvs_event_ptr ev, cvs_event_ptr dep)
  {
    add_dependency(ev, dep);
    weak_dependencies.push_back(make_pair(ev, dep));
  }

  void add_dependency(cvs_blob_index & bi, cvs_blob_index & dep_bi)
  {
    // try to add dependencies between events on the
    // same files.
    int deps_added = 0;
    for (blob_event_iter i = blobs[bi].begin(); i != blobs[bi].end(); ++i)
      {
        cvs_event_ptr ev = *i;

        for (blob_event_iter j = blobs[dep_bi].begin(); j != blobs[dep_bi].end(); ++j)
          if (ev->path == (*j)->path)
            {
              add_dependency(ev, *j);
              deps_added++;
            }
      }

    // if there are no common files, we need to add at least
    // one dependency, even if the event paths don't match.
    if (deps_added == 0)
      add_dependency(*blobs[bi].begin(), *blobs[dep_bi].begin());

    // make sure the dependents cache of the other blob gets
    // an update.
    blobs[dep_bi].reset_deps_cache();
  };

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
  };

  void
  add_weak_dependencies(cvs_event_ptr ev, vector<cvs_event_ptr> last_events,
                        bool reverse_import)
  {
    vector<cvs_event_ptr>::iterator i;

    if (reverse_import)
      {
        // make the last commit (i.e. 1.3) depend on the current one
        // (i.e. 1.2), as it which comes _before_ in the CVS history.
        for (i = last_events.begin(); i != last_events.end(); ++i)
          add_weak_dependency(*i, ev);
      }
    else
      {
        // vendor branches are processed in historic order, i.e.
        // older version first. Thus the last commit may be 1.1.1.1
        // while the current one is 1.1.1.2.
        for (i = last_events.begin(); i != last_events.end(); ++i)
          add_weak_dependency(ev, *i);
      }      
  };

  // removes an edge between two blobs.
  void remove_deps(cvs_blob_index const bi, cvs_blob_index const dep_bi)
  {
    L(FL("  removing dependency from blob %d to blob %d") % bi % dep_bi);

    if (!deps_sorted)
      sort_dependencies();

    dependencies.erase(remove_if(dependencies.begin(),
                                 dependencies.end(),
                                 dep_checker(bi, dep_bi)),
                       dependencies.end());

    dependents.erase(remove_if(dependents.begin(),
                               dependents.end(),
                               dep_checker(dep_bi, bi)),
                     dependents.end());

    // blob 'bi' is no longer a dependent of 'dep_bi', so update it's cache
    blobs[dep_bi].reset_deps_cache();
    vector<cvs_blob_index> deps = blobs[dep_bi].get_dependents(*this);
    blob_index_iter y = find(deps.begin(), deps.end(), bi);
    I(y == deps.end());
  };

  dep_loop
  get_dependencies(const cvs_event_ptr e)
  {
    if (!deps_sorted)
      sort_dependencies();

    event_dep_iter lower = lower_bound(dependencies.begin(),
                                       dependencies.end(),
                                       make_pair(e, (cvs_event_ptr) NULL));
    event_dep_iter upper = upper_bound(dependencies.begin(),
                                       dependencies.end(),
                                       make_pair(e + 1, (cvs_event_ptr) NULL));

    dep_loop i(lower, upper);
    return i;
  };

  dep_loop
  get_dependents(const cvs_event_ptr e)
  {
    if (!deps_sorted)
      sort_dependencies();

    event_dep_iter lower = lower_bound(dependents.begin(),
                                       dependents.end(),
                                       make_pair(e, (cvs_event_ptr) NULL));
    event_dep_iter upper = upper_bound(dependents.begin(),
                                       dependents.end(),
                                       make_pair(e, (cvs_event_ptr) -1));

    dep_loop i(lower, upper);
    return i;
  };

  void
  remove_weak_dependencies(void)
  {
    for (cvs_blob_index bi = 0; bi < blobs.size(); ++bi)
      blobs[bi].reset_deps_cache();

    if (!deps_sorted)
      sort_dependencies();

    for (event_dep_iter i = weak_dependencies.begin();
          i != weak_dependencies.end(); ++i)
      {
        dependencies.erase(lower_bound(dependencies.begin(),
                                       dependencies.end(),
                                       *i));
        dependents.erase(lower_bound(dependents.begin(),
                                     dependents.end(),
                                     make_pair(i->second, i->first)));
      }

    weak_dependencies.clear();
  };
};

class
event_ptr_time_cmp
{
public:
  bool operator() (const cvs_event_ptr & a, const cvs_event_ptr & b) const
    {
      return a->adj_time < b->adj_time; 
    }
};

class
event_ptr_path_strcmp
{
public:
  cvs_history & cvs;

  event_ptr_path_strcmp(cvs_history & c)
    : cvs(c)
  { };

  bool operator() (const cvs_event_ptr & a, const cvs_event_ptr & b) const
    {
      return cvs.path_interner.lookup(a->path) <
        cvs.path_interner.lookup(b->path); 
    }
};

class
blob_index_time_cmp
{
public:
  cvs_history & cvs;

  blob_index_time_cmp(cvs_history & c)
    : cvs(c)
  { };

  bool operator() (const cvs_blob_index a, const cvs_blob_index b)
    {
      return cvs.blobs[a].get_avg_time() < cvs.blobs[b].get_avg_time();
    }
};

string
get_event_repr(cvs_history & cvs, cvs_event_ptr ev)
{
  cvs_blob & blob(cvs.blobs[ev->bi]);
  if (blob.get_digest().is_commit())
    {
      cvs_commit *ce = (cvs_commit*) ev;
      return (F("commit rev %s on file %s")
                % cvs.rcs_version_interner.lookup(ce->rcs_version)
                % cvs.path_interner.lookup(ev->path)).str();
    }
  else if (blob.get_digest().is_symbol())
    {
      return (F("symbol %s on file %s")
                % cvs.symbol_interner.lookup(blob.symbol)
                % cvs.path_interner.lookup(ev->path)).str();
    }
  else if (blob.get_digest().is_branch_start())
    {
      return (F("start of branch %s on file %s")
                % cvs.symbol_interner.lookup(blob.symbol)
                % cvs.path_interner.lookup(ev->path)).str();
    }
  else if (blob.get_digest().is_branch_end())
    {
      return (F("end of branch %s on file %s")
                % cvs.symbol_interner.lookup(blob.symbol)
                % cvs.path_interner.lookup(ev->path)).str();
    }
  else
    {
      I(blob.get_digest().is_tag());
      return (F("tag %s on file %s")
              % cvs.symbol_interner.lookup(blob.symbol)
              % cvs.path_interner.lookup(ev->path)).str();
    }
}

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

  E(r.deltatexts.find(dest_version) != r.deltatexts.end(),
    F("delta for revision %s is missing") % dest_version);

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

typedef pair< cvs_event_ptr, cvs_event_ptr> t_violation;
typedef list<t_violation>::iterator violation_iter;
typedef pair< violation_iter, int > t_solution;

// returns the number of the violation which is cheapest to solve and
// wether it should be solved downwards or upwards. The return value
// consists of the index into the violations vector plus a direction
// indicator. That one is zero for adjusting downwards (i.e. adjusting
// the dependent) or one for adjusting upwards (i.e. adjusting the
// dependency).
static t_solution
get_cheapest_violation_to_solve(cvs_history & cvs, list<t_violation> & violations)
{
  unsigned int best_solution_price = (unsigned int) -1;
  t_solution best_solution;

  for (violation_iter i = violations.begin(); i != violations.end(); ++i)
    {
      unsigned int price = 0;

      stack< pair< cvs_event_ptr, time_i > > stack;
      set< cvs_event_ptr > done;
      cvs_event_ptr dep = i->first;
      cvs_event_ptr ev = i->second;

      // property of violation: an event ev has a lower timestamp
      // (i.e. is said to come before) than another event dep, on which
      // ev depends.
      I(ev->adj_time <= dep->adj_time);

      // check price of downward adjustment, i.e. adjusting all dependents
      // until we hit a dependent which has a timestamp higher that the
      // event dep.
      stack.push(make_pair(ev, dep->adj_time + 1));
      while (!stack.empty())
        {
          cvs_event_ptr e = stack.top().first;
          time_i time_goal = stack.top().second;
          stack.pop();

          set<cvs_event_ptr>::const_iterator ity = done.find(e);
          if (ity != done.end())
            continue;
          done.insert(ity, e);

          if (e->adj_time <= time_goal)
            {
              price++;
              for (dep_loop j = cvs.get_dependents(e); !j.ended(); ++j)
                stack.push(make_pair(*j, time_goal + 1));
            }
        }

      if (price < best_solution_price)
        {
          best_solution_price = price;
          best_solution = make_pair(i, 0);
        }

      // check price of upward adjustment, i.e. adjusting all dependencies
      // until we hit a dependency which has a timestamp lower that the
      // event ev.
      price = 0;
      done.clear();
      stack.push(make_pair(dep, ev->adj_time - 1));
      while (!stack.empty())
        {
          cvs_event_ptr e = stack.top().first;
          time_i time_goal = stack.top().second;
          stack.pop();

          set<cvs_event_ptr>::const_iterator ity = done.find(e);
          if (ity != done.end())
            continue;
          done.insert(ity, e);

          if (e->adj_time >= time_goal)
            {
              price++;
              for (dep_loop j = cvs.get_dependencies(e); !j.ended(); ++j)
                stack.push(make_pair(*j, time_goal - 1));
            }
        }

      if (price < best_solution_price)
        {
          best_solution_price = price;
          best_solution = make_pair(i, 1);
        }

    }

  return best_solution;
}

static void
solve_violation(cvs_history & cvs, t_solution & solution)
{
  stack< pair< cvs_event_ptr, time_i > > stack;
  set< cvs_event_ptr > done;
  cvs_event_ptr dep = solution.first->first;
  cvs_event_ptr ev = solution.first->second;
  int direction = solution.second;

  L(FL("Resolving conflicting timestamps of: %s and %s")
    % get_event_repr(cvs, dep) % get_event_repr(cvs, ev));

  if (direction == 0)
    {
      // downward adjustment, i.e. adjusting all dependents
      stack.push(make_pair(ev, dep->adj_time + 1));
      while (!stack.empty())
        {
          cvs_event_ptr e = stack.top().first;
          time_i time_goal = stack.top().second;
          stack.pop();

          set<cvs_event_ptr>::const_iterator ity = done.find(e);
          if (ity != done.end())
            continue;
          done.insert(ity, e);

          if (e->adj_time <= time_goal)
            {
              L(FL("  adjusting event %s by %d seconds.")
                % get_event_repr(cvs, e)
                % (time_goal - e->adj_time));
              e->adj_time = time_goal;
              for (dep_loop j = cvs.get_dependents(e); !j.ended(); ++j)
                stack.push(make_pair(*j, time_goal + 1));
            }
        }
    }
  else
    {
      // upward adjustmest, i.e. adjusting all dependencies
      stack.push(make_pair(dep, ev->adj_time - 1));
      while (!stack.empty())
        {
          cvs_event_ptr e = stack.top().first;
          time_i time_goal = stack.top().second;
          stack.pop();

          set<cvs_event_ptr>::const_iterator ity = done.find(e);
          if (ity != done.end())
            continue;
          done.insert(ity, e);

          if (e->adj_time >= time_goal)
            {
              L(FL("  adjusting event %s by %d seconds.")
                % get_event_repr(cvs, e)
                % (e->adj_time - time_goal));
              e->adj_time = time_goal;
              for (dep_loop j = cvs.get_dependencies(e); !j.ended(); ++j)
                stack.push(make_pair(*j, time_goal - 1));
            }
        }
    }
}

static void
sanitize_rcs_file_timestamps(cvs_history & cvs,
                             const cvs_event_ptr root_event)
{
  while (1)
    {
      // we start at the root event for the current file and scan for
      // timestamp pairs which violate the corresponding dependency.
      stack< cvs_event_ptr > stack;
      set< cvs_event_ptr > done;
      stack.push(root_event);

      list<t_violation> violations;

      while (!stack.empty())
        {
          cvs_event_ptr ev = stack.top();
          stack.pop();

          set<cvs_event_ptr>::const_iterator ity = done.find(ev);
          if (ity != done.end())
            continue;
          done.insert(ity, ev);

          for (dep_loop i = cvs.get_dependents(ev); !i.ended(); ++i)
            {
              if (ev->adj_time >= (*i)->adj_time)
                violations.push_back(make_pair(ev, *i));

              stack.push(*i);
            }
        }

      if (violations.empty())
        break;

      t_solution x = get_cheapest_violation_to_solve(cvs, violations);
      solve_violation(cvs, x);
    }
}

static cvs_event_ptr
process_rcs_branch(cvs_symbol_no const & current_branchname,
                   string const & begin_version,
               vector< piece > const & begin_lines,
               data const & begin_data,
               hexenc<id> const & begin_id,
               rcs_file const & r,
               database & db,
               cvs_history & cvs,
               app_state & app,
               bool reverse_import)
{
  cvs_event_ptr curr_commit = NULL,
                first_commit = NULL;
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
      I(commit_time > 0);

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
      cvs_authorclog ac = cvs.authorclog_interner.intern(ac_str);

      cvs_mtn_version mv = cvs.mtn_version_interner.intern(
        file_id(curr_id).inner()());

      cvs_rcs_version rv = cvs.rcs_version_interner.intern(curr_version);

      curr_commit = (cvs_event*)
          new (cvs.ev_pool.allocate<cvs_commit>())
            cvs_commit(cvs.curr_file_interned, commit_time,
                       mv, rv, alive);

      if (!first_commit)
        first_commit = curr_commit;

      // add the commit to the cvs history
      cvs_blob_index bi = cvs.get_or_create_blob(ET_COMMIT, ac);
      cvs.append_event_to(bi, curr_commit);
      ++cvs.n_rcs_revisions;

      curr_events.push_back(curr_commit);

      // add proper dependencies, based on forward or reverse import
      if (!last_events.empty())
        cvs.add_dependencies(curr_commit, last_events, reverse_import);

      // create tag events for all tags on this commit
      typedef multimap<string,string>::const_iterator ity;
      pair<ity,ity> range = r.admin.symbols.equal_range(curr_version);
      for (ity i = range.first; i != range.second; ++i)
        {
          if (i->first == curr_version)
           {
              L(FL("version %s -> tag %s") % curr_version % i->second);

              // allow the user to ignore tag symbols depending on their
              // name.
              if (app.lua.hook_ignore_cvs_symbol(i->second))
                continue;

              cvs_symbol_no tag = cvs.symbol_interner.intern(i->second);

              cvs_event_ptr tag_symbol = (cvs_event_ptr)
                    new (cvs.ev_pool.allocate<cvs_event>())
                      cvs_event(curr_commit->path,
                                commit_time);

              tag_symbol->adj_time = curr_commit->adj_time + 1;
              if (alive)
                cvs.add_dependency(tag_symbol, curr_commit);
              else
                cvs.add_weak_dependency(tag_symbol, curr_commit);

              bi = cvs.get_or_create_blob(ET_SYMBOL, tag);
              cvs.append_event_to(bi, tag_symbol);
              curr_events.push_back(tag_symbol);

              // Append to the last_event deps. While not quite obvious,
              // we absolutely need this dependency! Think of it as: the
              // 'action of tagging' must come before the next commit.
              //
              // If we didn't add this dependency, the tag could be deferred
              // by the toposort to many revisions later. Instead, we want
              // to raise a conflict, if a commit interferes with a tagging
              // action.
              cvs.add_weak_dependencies(tag_symbol, last_events, reverse_import);

              cvs_event_ptr tag_event = (cvs_event_ptr)
                    new (cvs.ev_pool.allocate<cvs_event>())
                      cvs_event(curr_commit->path,
                                commit_time);

              tag_event->adj_time = curr_commit->adj_time + 2;
              cvs.add_dependency(tag_event, tag_symbol);

              bi = cvs.get_or_create_blob(ET_TAG_POINT, tag);
              cvs.append_event_to(bi, tag_event);
              ++cvs.n_rcs_symbols;
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
                             app.opts.dryrun);
            }

          cvs_symbol_no bname = cvs.symbol_interner.intern(branchname);

          // recursively process child branches
          cvs_event_ptr first_event_in_branch =
            process_rcs_branch(bname, *i, branch_lines, branch_data,
                               branch_id, r, db, cvs, app, false);
          if (!priv)
            L(FL("finished RCS branch %s = '%s'") % (*i) % branchname);
          else
            L(FL("finished private RCS branch %s") % (*i));

          cvs_event_ptr branch_point = (cvs_event_ptr)
                new (cvs.ev_pool.allocate<cvs_event>())
                   cvs_event(curr_commit->path,
                             commit_time);
          branch_point->adj_time = curr_commit->adj_time + 1;

          // Normal branches depend on the current commit. But vendor
          // branches don't depend on anything. They theoretically come
          // before anything else (i.e. initial import).
          //
          // To make sure the DFS algorithm sees this event anyway, we
          // make it dependent on the root_blob, which is artificial
          // anyway.
          if (!is_vendor_branch)
            if (alive)
              cvs.add_dependency(branch_point, curr_commit);
            else
              cvs.add_weak_dependency(branch_point, curr_commit);
          else
            cvs.add_dependency(branch_point, cvs.root_event);

          curr_events.push_back(branch_point);

          if (first_event_in_branch)
            {
              L(FL("adding branch start event"));
              // Add a branch start event, on which the first event in
              // the branch depends. The other way around, this branch
              // start only depends on the branch point. While this
              // distinction may be confusing, it really helps later on
              // when determining what branch a blob belongs to.
              cvs_event_ptr branch_start = (cvs_event_ptr)
                    new (cvs.ev_pool.allocate<cvs_event>())
                      cvs_event(curr_commit->path,
                                commit_time);
              branch_start->adj_time = curr_commit->adj_time + 2;

              bi = cvs.get_or_create_blob(ET_BRANCH_START, bname);
              cvs.append_event_to(bi, branch_start);
              cvs.add_dependency(first_event_in_branch, branch_start);
              cvs.add_dependency(branch_start, branch_point);
            }
          else
            L(FL("branch %s remained empty for this file") % branchname);

          // make sure curr_commit exists in the cvs history
          I(cvs.blob_exists(cvs.blobs[curr_commit->bi].get_digest()));

          // add the blob to the bucket
          bi = cvs.get_or_create_blob(ET_SYMBOL, bname);
          cvs.append_event_to(bi, branch_point);

          L(FL("added branch event for file %s into branch %s")
            % cvs.path_interner.lookup(curr_commit->path)
            % branchname);

          // Make the last commit depend on this branch, so that this
          // commit action certainly comes after the branch action. See
          // the comment above for tags.
          if (!is_vendor_branch)
            cvs.add_weak_dependencies(branch_point, last_events, reverse_import);

          ++cvs.n_rcs_symbols;
        }

      string next_version = r.deltas.find(curr_version)->second->next;

      if (!next_version.empty())
        {
          L(FL("following RCS edge %s -> %s") % curr_version % next_version);

          construct_version(*curr_lines, next_version, *next_lines, r);
          L(FL("constructed RCS version %s, inserting into database") %
            next_version);

          insert_into_db(curr_data, curr_id,
                         *next_lines, next_data, next_id, db,
                         app.opts.dryrun);
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
    {
      if (curr_commit)
        {
          cvs_event_ptr branch_end_point =
                new (cvs.ev_pool.allocate<cvs_event>())
                  cvs_event(cvs.curr_file_interned,
                            first_commit->adj_time + 1);

          cvs_blob_index bi = cvs.get_or_create_blob(ET_BRANCH_END,
                                                     current_branchname);
          cvs.append_event_to(bi, branch_end_point);
          cvs.add_dependency(branch_end_point, first_commit);
        }

      return curr_commit;
    }
  else
    {
      if (first_commit)
        {
          cvs_event_ptr branch_end_point =
                new (cvs.ev_pool.allocate<cvs_event>())
                  cvs_event(cvs.curr_file_interned,
                            curr_commit->adj_time + 1);

          cvs_blob_index bi = cvs.get_or_create_blob(ET_BRANCH_END,
                                                     current_branchname);
          cvs.append_event_to(bi, branch_end_point);

          // add a hard dependency on first_commit
          cvs.add_dependency(branch_end_point, curr_commit);

          // all others are weak dependencies
          vector<cvs_event_ptr>::iterator ity(
            find(curr_events.begin(), curr_events.end(), curr_commit));
          I(ity != curr_events.end());
          curr_events.erase(ity);
          cvs.add_weak_dependencies(branch_end_point, curr_events, reverse_import);
        }

      return first_commit;
    }
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

    cvs.set_filename(filename, fid);
    cvs.index_branchpoint_symbols (r);
    if (!app.opts.dryrun)
      app.db.put_file(fid, file_data(dat));

    global_pieces.reset();
    global_pieces.index_deltatext(r.deltatexts.find(r.admin.head)->second,
                                  head_lines);

    // add a pseudo trunk branch event (at time 0)
    cvs.root_event =
          new cvs_event(cvs.curr_file_interned, cvs.base_branch);
    cvs.root_blob = cvs.get_or_create_blob(ET_BRANCH_START, cvs.base_branch);
    cvs.append_event_to(cvs.root_blob, cvs.root_event);

    cvs_event_ptr first_event =
      process_rcs_branch(cvs.base_branch, r.admin.head, head_lines,
                         dat, id, r, app.db, cvs, app, true);

    // link the pseudo trunk branch to the first event in the branch
    cvs.add_dependency(first_event, cvs.root_event);

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

void
cvs_history::set_filename(string const & file,
                          file_id const & ident)
{
  L(FL("importing file '%s'") % file);
  I(file.size() > 2);
  I(file.substr(file.size() - 2) == string(",v"));
  string ss = file;
  ui.set_tick_trailer(ss);

  // remove Attic/ if present
  string::size_type last_slash = ss.rfind('/');
  if (last_slash != string::npos && last_slash >= 5
        && ss.substr(last_slash - 5, 6)=="Attic/")
    {
      ss.erase(last_slash - 5, 6);

      if (file_exists(file_path_internal(ss)))
        W(F("File %s exists alive as well as in the Attic! "
            "Merging contents.") % ss);
    }

  // strip the extension
  ss.resize(ss.size() - 2);

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
            transaction_guard guard(app.db);
            import_rcs_file_with_cvs(file, app, cvs);
            guard.commit();
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

  temp_node_id_source nis;

  blob_consumer(cvs_history & cvs,
                app_state & app,
                ticker & n_revs)
  : cvs(cvs),
    app(app),
    n_revisions(n_revs)
  { };

  void create_artificial_revisions(cvs_blob_index bi,
                                   set<cvs_blob_index> & parent_blobs,
                                   revision_id & parent_rid,
                                   cvs_symbol_no & in_branch);

  void merge_parents_for_artificial_rev(cvs_blob_index bi,
                                        set<revision_id> & parent_rids,
                                        map<cvs_event_ptr, revision_id> & event_parent_rids);

  void operator() (cvs_blob_index bi);
};

struct dij_context
{
  int dist;
  cvs_blob_index prev;

  // My STL's map implementation insists on having this constructor, but
  // we don't want it to be called ever!
  dij_context()
    {
      I(false);
    };

  dij_context(int d, cvs_blob_index p)
    : dist(d),
      prev(p)
    { };
};

// This returns the shortest path from a blob following it's dependents,
// i.e. downwards, from the root of the tree to the leaves *or* following
// it's dependencies, i.e. upwards, from the leaves to the root. It can
// ignore blobs depending on their color (painted by a previous depth
// first search) to reduce the number of blobs it needs to touch. As if
// that weren't enough flexibility already, it can also abort the search
// as soon as it hits the first grey blob.
//
// Attention: the path is returned in reversed order!
template<typename Container> void
dijkstra_shortest_path(cvs_history &cvs,
                       cvs_blob_index from, cvs_blob_index to,
                       insert_iterator<Container> ity,
                       bool direction_downwards,
                       bool follow_white,
                       bool follow_grey,
                       bool follow_black,
                       bool break_on_grey,
                       pair< cvs_blob_index, cvs_blob_index > edge_to_ignore)
{
  map< cvs_blob_index, dij_context > distances;
  stack< cvs_blob_index > stack;

  stack.push(from);
  distances.insert(make_pair(from, dij_context(0, invalid_blob)));

  if (break_on_grey)
    I(follow_grey);

  cvs_blob_index bi;
  while (stack.size() > 0)
    {
      bi = stack.top();
      stack.pop();

      if (break_on_grey && cvs.blobs[bi].color == grey)
        break;

      if (bi == to)
        break;

      I(distances.count(bi) > 0);
      int curr_dist = distances[bi].dist;

      if (direction_downwards)
        {
          vector<cvs_blob_index> & deps = cvs.blobs[bi].get_dependents(cvs);
          for (blob_index_iter i = deps.begin(); i != deps.end(); ++i)
            if ((follow_white && cvs.blobs[*i].color == white) ||
                (follow_grey && cvs.blobs[*i].color == grey) ||
                (follow_black && cvs.blobs[*i].color == black))
              if (distances.count(*i) == 0 &&
                  make_pair(bi, *i) != edge_to_ignore)
                {
                  distances.insert(make_pair(*i, dij_context(curr_dist + 1, bi)));
                  stack.push(*i);
                }
        }
      else
        for (blob_event_iter i = cvs.blobs[bi].begin();
             i != cvs.blobs[bi].end(); ++i)
          for (dep_loop j = cvs.get_dependencies(*i); !j.ended(); ++j)
            {
              cvs_blob_index dep_bi = (*j)->bi;

              if ((follow_white && cvs.blobs[dep_bi].color == white) ||
                  (follow_grey && cvs.blobs[dep_bi].color == grey) ||
                  (follow_black && cvs.blobs[dep_bi].color == black))
                if (distances.count(dep_bi) == 0 &&
                    make_pair(bi, dep_bi) != edge_to_ignore)
                  {
                    distances.insert(make_pair(dep_bi, dij_context(curr_dist + 1, bi)));
                    stack.push(dep_bi);
                  }
            }
    }

  if (!break_on_grey && bi != to)
    return;

  // Trace back the shortest path and remember all vertices
  // on the path. These are part of the cycle we want to
  // split.
  I(break_on_grey || bi == to);
#ifdef DEBUG_DIJKSTRA
  L(FL("dijkstra's algorithm, shortest path:"));
#endif

  while (true)
    {
      *ity++ = bi;
#ifdef DEBUG_DIJKSTRA
      L(FL("    blob %d.") % bi);
#endif
      I(distances.count(bi) > 0);

      if (bi == from)
        break;

      bi = distances[bi].prev;
    }
}

#ifdef DEBUG_GRAPHVIZ
void
write_graphviz_partial(cvs_history & cvs, string const & desc,
                       set<cvs_blob_index> & blobs_to_mark,
                       int add_depth);

void
write_graphviz_complete(cvs_history & cvs, string const & desc);

#endif

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

  bool abort()
    {
      return !cycle_members.empty();
    }

  void tree_edge(Edge e)
    {
#ifdef DEBUG_BLOB_SPLITTER
      L(FL("blob_splitter: tree edge: %d -> %d") % e.first % e.second);
#endif
    }

  void forward_or_cross_edge(Edge e)
    {
#ifdef DEBUG_BLOB_SPLITTER
      L(FL("blob_splitter: cross edge: %d -> %d") % e.first % e.second);
#endif
    }

  void back_edge(Edge e)
    {
#ifdef DEBUG_BLOB_SPLITTER
      L(FL("blob_splitter: back edge: %d -> %d") % e.first % e.second);
#endif

      if (e.first == e.second)
        {
          // The cycle consists of only one blob - we have to solve an
          // intra blob dependency.
          cycle_members.insert(e.first);

#ifdef DEBUG_GRAPHVIZ
          write_graphviz_partial(cvs, "splitter", cycle_members, 5);
#endif
        }
      else
        {
          // We run Dijkstra's algorithm to find the shortest path from
          // e.second to e.first. All vertices in that path are part of
          // the smallest cycle which includes this back edge. To speed
          // things up a bit, we do not take vertices into account, which
          // have already been completely processed by the proceeding
          // depth first search run (thus only considering blobs marked
          // grey or white).
          insert_iterator< set< cvs_blob_index > >
            ity(cycle_members, cycle_members.begin());
          dijkstra_shortest_path(cvs, e.second, e.first, ity,
                                 true,              // downwards
                                 true, true, false, // follow white and grey
                                 false,
                                 make_pair(invalid_blob, invalid_blob));
          I(!cycle_members.empty());

#ifdef DEBUG_GRAPHVIZ
          write_graphviz_partial(cvs, "splitter", cycle_members, 5);
#endif
        }
    }
};


// Different functors for deciding where to split a blob
class split_decider_func
{
public:
  virtual ~split_decider_func ()
    { };  // a no-op, just here to make the compiler happy.
  virtual bool operator () (const cvs_event_ptr & ev) = 0;
};

// A simple functor which decides based on the event's timestamp.
class split_by_time
  : public split_decider_func
{
  u64 split_point;

public:
  split_by_time(time_i const ti)
    : split_point(ti)
    { };

  virtual bool operator () (const cvs_event_ptr & ev)
    {
      return ev->adj_time > split_point;
    }
};

// A more clever functor which decides, based on the blob's dependencies:
// if the event depends on one or more blobs in path_a, it's put in the
// new blob, if it has one or more dependencies to blobs in path_b, it
// remains in the old blob.
//
// Attention: the blob to split must have at least one dependency to
// a blob in path A or path B. Additionally, if there are dependencies
// to blobs in path A, there must not be any dependencies to path B and
// vice verca.
class split_by_path
  : public split_decider_func
{
  cvs_history & cvs;
  vector<cvs_blob_index> path;

public:
  split_by_path(cvs_history & cvs,
                vector<cvs_blob_index> & path)
    : cvs(cvs),
      path(path)
    { };

  virtual bool operator () (const cvs_event_ptr & ev)
    {
      set<cvs_blob_index> done;
      stack<cvs_blob_index> stack;

      I(!path.empty());

      // start at the event given and recursively check all its
      // dependencies for blobs in the path.
      for (dep_loop i = cvs.get_dependencies(ev); !i.ended(); ++i)
        stack.push((*i)->bi);

      // Mark all dependencies of the first blob in the path as
      // done. If we hit one of those, we don't have to go
      // further.
      cvs_blob & first_blob = cvs.blobs[*path.begin()];
      for (blob_event_iter i = first_blob.begin(); i != first_blob.end(); ++i)
        for (dep_loop j = cvs.get_dependencies(*i); !j.ended(); ++j)
          {
            cvs_blob_index dep_bi = (*j)->bi;
            if (done.find(dep_bi) == done.end())
              done.insert(dep_bi);
          }

      while (!stack.empty())
        {
          cvs_blob_index bi = stack.top();
          stack.pop();

          set<cvs_blob_index>::const_iterator ity = done.find(bi);
          if (ity != done.end())
            continue;
          done.insert(ity, bi);

          if (find(path.begin(), path.end(), bi) != path.end())
            return true;
          else
            for (blob_event_iter i = cvs.blobs[bi].begin();
                 i != cvs.blobs[bi].end(); ++i)
              for (dep_loop j = cvs.get_dependencies(*i); !j.ended(); ++j)
                {
                  cvs_blob_index dep_bi = (*j)->bi;
                  stack.push(dep_bi);
                }
        }

      return false;
    }
};

void
log_path(vector<cvs_blob_index> const & path, string const & str)
{
  L(FL("%s") % str);
  for (vector<cvs_blob_index>::const_iterator i = path.begin(); i != path.end(); ++i)
    L(FL("  blob: %d") % *i);
}

void
split_blob_at(cvs_history & cvs, const cvs_blob_index blob_to_split,
              split_decider_func & split_decider);


struct branch_sanitizer
{
protected:
  cvs_history & cvs;
  bool & dfs_restart_needed;

public:
  branch_sanitizer(cvs_history & c, bool & r)
    : cvs(c),
      dfs_restart_needed(r)
    { }

  bool abort()
    {
      return dfs_restart_needed;
    }

  void tree_edge(Edge e)
    {
#ifdef DEBUG_BLOB_SPLITTER
      L(FL("branch_sanitizer: tree edge: %d -> %d") % e.first % e.second);
#endif
    }

  void forward_or_cross_edge(Edge e)
    {
#ifdef DEBUG_BLOB_SPLITTER
      L(FL("branch_sanitizer: cross edge: %d -> %d") % e.first % e.second);
#endif

      // a short circuit for branch start and tag blobs, which may very
      // well have multiple ancestors (i.e. cross or forward edges)
      if (cvs.blobs[e.second].get_digest().is_branch_start() ||
          cvs.blobs[e.second].get_digest().is_tag())
        return;

      // On a forward or cross edge, we first have to find the common
      // ancestor of both blobs involved. For that we go upwards from
      // the target (e.second) blob, until we reach the first grey
      // blob. That must be a common ancestor (but not necessarily the
      // lowest one).
      vector< cvs_blob_index > path_a, path_b;
      insert_iterator< vector< cvs_blob_index > >
        ity_a(path_a, path_a.end()),
        ity_b(path_b, path_b.end());

      I(cvs.blobs[e.second].color == black);
      dijkstra_shortest_path(cvs, e.second, cvs.root_blob, ity_a,
                             false,               // upwards direction,
                             false, true, true,   // follow grey and black, but
                             true,                // break on first grey
                             make_pair(e.second, e.first)); // ignore direct path
      I(!path_a.empty());

      // From that common ancestor, we now follow the grey blobs downwards,
      // until we find the source (e.first) blob of the cross edge.
      I(cvs.blobs[e.first].color == grey);
      I(cvs.blobs[path_a[0]].color == grey);
      dijkstra_shortest_path(cvs, e.first, path_a[0], ity_b,
                             false,               // upwards
                             false, true, false,  // follow only grey
                             false,
                             make_pair(invalid_blob, invalid_blob));
      I(!path_b.empty());
      path_b.push_back(e.second);

      // At this point we have two different paths, both going from the
      // (grey) common ancestor we've found...
      I(path_a[0] == path_b[0]);

      // ..to the target of the cross edge (e.second).
      I(*path_a.rbegin() == e.second);
      I(*path_b.rbegin() == e.second);


      // Unfortunately, the common ancestor we've found isn't
      // necessarily the lowest common ancestor. However, it doesn't
      // need to be, we just need to make sure, that there are no
      // dependencies between blobs of path_a and path_b.
      //
      // As path_a is all colored black from the depth first search
      // run, we already know there are no cross or forward edges from
      // it to any blob of path_b.
      //
      // But blobs of path_b can very well have dependencies to blobs
      // of path_a. An example (from the test importing_cvs_branches2):
      //
      //                        2
      //              p       /    \       p
      //              a     10 <-.  12     a
      //              t      |   |   |     t
      //              h      9   |  11     h
      //                     |   |   |
      //              a      5    '- 3     b
      //                      \     /
      //                         8
      //
      // Here, the cross edge discovered was 3 -> 8. And path_a is:
      // (2, 10, 9, 5, 8), while path_b is (2, 12, 11, 3, 8). The edge
      // 3 -> 10 is another cross edge, but just  hasn't been discovered
      // so far.
      //
      // However, we better handle the higher cross path first, since it
      // may have an effect on how the later cross path needs to be
      // handled. Thus in the above case, we decide to handle the parallel
      // paths (2, 10) and (2, 12, 11, 3, 10) first. Only after having
      // resolved those, we handle the lower parallel paths
      // (3, 10, 9, 5, 8) and (3, 8).

      check_for_cross_path(path_a, path_b, false);
    }

  void check_for_cross_path(vector<cvs_blob_index> & path_a,
                            vector<cvs_blob_index> & path_b,
                            bool switch_needed)
    {
      // Branch starts and tag blobs can have multiple ancestors (which
      // should all be symbol blobs), so we never want to split those.
      if (cvs.blobs[*path_a.rbegin()].get_digest().is_branch_start() ||
          cvs.blobs[*path_a.rbegin()].get_digest().is_tag())
        return;

#ifdef DEBUG_BLOB_SPLITTER
      log_path(path_a, "handling path a:");
      log_path(path_b, "         path b:");
      L(FL("         %s") % (switch_needed ? "switch needed"
                                           : "no switch needed"));
      L(FL("         %s") % (path_a_is_all_black ? "path_a is all black"
                                                 : "path_a is colored"));
#endif

      vector<cvs_blob_index> cross_path;
      insert_iterator< vector< cvs_blob_index > >
        ity_c(cross_path, cross_path.end());

      dijkstra_shortest_path(cvs, *(++path_a.rbegin()), *(++path_b.begin()),
                             ity_c,
                             false,                // upwards
                             true, true, true,    // follow all colors
                             false,
                             make_pair(invalid_blob, invalid_blob));

      if (!cross_path.empty())
        {
#ifdef DEBUG_BLOB_SPLITTER
          L(FL("    found cross path:")); 
          log_path(cross_path, "         cross path:");
#endif

          // Find the first element in the cross path, which is also
          // part of the existing path_a. That will be our new target,
          // i.e. becomes the new target blob.
          vector<cvs_blob_index>::iterator pa_i, cp_i;
          for (cp_i = cross_path.begin();
               cp_i != cross_path.end(); ++cp_i)
            {
              pa_i = find(path_a.begin(), path_a.end(), *cp_i);
              if (pa_i != path_a.end())
                break;
            }
          I(cp_i != cross_path.end());
          I(pa_i != path_a.end());

          // handle the upper parallel paths
          {
            vector<cvs_blob_index> new_path_a;
            vector<cvs_blob_index> new_path_b;

            cvs_blob_index target_bi = *cp_i;

            // copy all elements from the cross_path until
            // cp_i (the first element in the cross path, which is
            // also part of existing path_a).
            new_path_b.push_back(*path_b.begin());
            copy(cross_path.begin(), ++cp_i, back_inserter(new_path_b));

            // copy all elements from the beginning of path a until
            // that same element.
            copy(path_a.begin(), ++pa_i, back_inserter(new_path_a));

            // Recursive call. If we don't need to switch anymore *and*
            // path_a is still all black, we don't need to switch for
            // that sub-path.
            check_for_cross_path(new_path_a, new_path_b, true);
          }

          // Short circuit if one of the above cross path resolution
          // steps already require a DFS restart.
          if (dfs_restart_needed)
            return;

          // then handle the lower parallel paths
          {
            vector<cvs_blob_index> new_path_a;
            vector<cvs_blob_index> new_path_b;

            // get the lowest common ancestor, which must be
            // part of path_b
            vector<cvs_blob_index>::iterator ib = ++path_b.begin();
            vector<cvs_blob_index>::iterator ic = cross_path.begin();

            I(*ib == *ic);
            while (*(ib + 1) == *(ic + 1))
              {
                ib++;
                ic++;
              }

            copy(ic, cross_path.end(), back_inserter(new_path_a));
            new_path_a.push_back(*path_b.rbegin());

            copy(ib, path_b.end(), back_inserter(new_path_b));

            // Recursive call, where we require the paths to be
            // switched and checked for reverse cross paths, as we
            // cannot be sure that all elements in new_path_a are
            // black already. Thus, there might also be a cross
            // path from new_path_a to new_path_b.
            check_for_cross_path(new_path_a, new_path_b, true);
          }
      }

      // Short circuit if one of the above cross path resolution
      // steps already require a DFS restart.
      if (dfs_restart_needed)
        return;

      if (switch_needed)
        {
          // If we still need a switch, do the reversed cross path
          // check, but 'path_a_is_all_black' is certainly no longer
          // true.
          vector<cvs_blob_index> new_path_a(path_b);
          vector<cvs_blob_index> new_path_b(path_a);
          check_for_cross_path(new_path_a, new_path_b, false);
        }
      else
        {
          // Extra check the other way around. Since either a DFS or
          // our cross checks already guarantee that there are no more
          // reverse cross paths, this should always succeed.
          vector<cvs_blob_index> cross_path;
          insert_iterator< vector< cvs_blob_index > >
            ity_c(cross_path, cross_path.end());

          dijkstra_shortest_path(cvs, *(++path_a.begin()), *(++path_b.rbegin()),
                                 ity_c,
                                 true,                // downwards
                                 true, true, true,    // follow all colors
                                 false,
                                 make_pair(invalid_blob, invalid_blob));
          I(cross_path.empty());

          handle_paths_of_cross_edge(path_a, path_b);
        }
    };

  void handle_paths_of_cross_edge(vector<cvs_blob_index> & path_a,
                                   vector<cvs_blob_index> & path_b)
    {
#ifdef DEBUG_BLOB_SPLITTER
      L(FL("branch_sanitizer: handle paths"));
#endif

      cvs_blob_index target_bi = *path_a.rbegin();
      I(target_bi == *path_b.rbegin());

      // Check if any one of the two paths contains a branch start.
      bool a_has_branch = false;
      vector<cvs_blob_index>::iterator first_branch_start_in_path_a;
      bool b_has_branch = false;
      vector<cvs_blob_index>::iterator first_branch_start_in_path_b;
      for (vector<cvs_blob_index>::iterator i = ++path_a.begin();
           i != path_a.end(); ++i)
        if (cvs.blobs[*i].get_digest().is_branch_start())
          {
            L(FL("path a contains a branch blob: %d (%s)") % *i % get_event_repr(cvs, *cvs.blobs[*i].begin()));
            a_has_branch = true;
            first_branch_start_in_path_a = i;
            break;
          }

      for (vector<cvs_blob_index>::iterator i = ++path_b.begin();
           i != path_b.end(); ++i)
        if (cvs.blobs[*i].get_digest().is_branch_start())
          {
            L(FL("path b contains a branch blob: %d (%s)") % *i % get_event_repr(cvs, *cvs.blobs[*i].begin()));
            b_has_branch = true;
            first_branch_start_in_path_b = i;
            break;
          }

#ifdef DEBUG_GRAPHVIZ
      {
        set<cvs_blob_index> blobs_to_show;

        for (vector<cvs_blob_index>::iterator i = path_a.begin(); i != path_a.end(); ++i)
          blobs_to_show.insert(*i);

        for (vector<cvs_blob_index>::iterator i = path_b.begin(); i != path_b.end(); ++i)
          blobs_to_show.insert(*i);

        write_graphviz_partial(cvs, "splitter", blobs_to_show, 5);
      }
#endif

      // Swap a and b, if only b has a branch, but not a. This reduces
      // to three cases: no branches, only a has a branch and both
      // paths contain branches.
      if (b_has_branch && !a_has_branch)
        {
          L(FL("swapping paths a and b"));
          swap(path_a, path_b);
          swap(a_has_branch, b_has_branch);
          swap(first_branch_start_in_path_a, first_branch_start_in_path_b);
        }

      if (a_has_branch && b_has_branch)
        {
          // The target blob seems to be part of two (or even more)
          // branches, thus we need to split that blob.

          vector< cvs_blob_index > tmp_a((++path_a.rbegin()).base() - first_branch_start_in_path_a);
          copy(first_branch_start_in_path_a, (++path_a.rbegin()).base(), tmp_a.begin());

          vector< cvs_blob_index > tmp_b((++path_b.rbegin()).base() - first_branch_start_in_path_b);
          copy(first_branch_start_in_path_b, (++path_b.rbegin()).base(), tmp_b.begin());

          I(!tmp_a.empty());
          I(!tmp_b.empty());

          split_by_path func_a(cvs, tmp_a),
                        func_b(cvs, tmp_b);

          // Count all events, and check where we can splitt
          int pa_deps = 0,
              pb_deps = 0,
              total_events = 0;

          for (blob_event_iter j = cvs.blobs[target_bi].get_events().begin();
               j != cvs.blobs[target_bi].get_events().end(); ++j)
            {
              bool depends_on_path_a = func_a(*j);
              bool depends_on_path_b = func_b(*j);

              if (depends_on_path_a && depends_on_path_b)
                L(FL("event %s depends on both paths!") % get_event_repr(cvs, *j));

              if (depends_on_path_a)
                pa_deps++;
              if (depends_on_path_b)
                pb_deps++;

              total_events++;
            }

          L(FL("of %d total events, %d depend on path a, %d on path b")
            % total_events % pa_deps % pb_deps);

          I(pa_deps > 0);
          I(pb_deps > 0);

          if ((pa_deps == total_events) && (pb_deps == total_events))
            {
              // if all events depend on both paths, we can't really
              // split. Thas mostly happens if total_events == 1, which
              // is a strange thing per se. (Requesting to split a
              // blob of size 1 is the strange thing).
              I(false);
            }
          else if (pa_deps >= pb_deps)
          {
            L(FL("  splitting dependencies from path b"));
            split_blob_at(cvs, target_bi, func_b);
          }
          else
          {
            L(FL("  splitting dependencies from path a"));
            split_blob_at(cvs, target_bi, func_a);
          }

          dfs_restart_needed = true;
        }
      else if (a_has_branch && !b_has_branch)
        {
          // Path A started into another branch, while all the
          // blobs of path B are in the same branch as the target
          // blob.

          cvs_blob_index bi_a = *(++path_a.rbegin());
          cvs_blob_index bi_b = *(++path_b.rbegin());

          if (cvs.blobs[path_a[1]].get_digest().is_symbol() &&
              cvs.blobs[path_a[2]].get_digest().is_branch_start() &&
              cvs.blobs[target_bi].get_digest().is_symbol() &&
              path_b.size() == 2)
            {
              // This is a special case, where none of the commits in path_a
              // touch a certain file. A symbol then has a dependency on
              // an RCS version which seems to belong to another branch. We
              // simply drop that dependency to relocate the symbol and make
              // it very clear what branch it belongs to.
              //
              //                common
              //               ancestor
              //               (symbol)
              //                /    \         path_b is pretty empty, i.e.
              //            branch    |        there is a direct dependency
              //            symbol    |        from the common ancestor (a
              //               |      |        branch symbol) to target_bi
              //            branch    |        (also a symbol, but any kind)
              //            start     |
              //               |      |
              //              (+)     |
              //                \    /
              //              target_bi
              //               (symbol)
              //
              cvs.remove_deps(target_bi, bi_b);

              dfs_restart_needed = true;
            }
          else if ((path_a[0] == cvs.root_blob) && (path_a.size() == 5) &&
                   cvs.blobs[target_bi].get_digest().is_symbol())
            {
              // another special case: with a vendor branch, 1.1.1.1 gets
              // tagged, instead of 1.1. This poses a problem for further
              // sub-branches, as those might have dependencies on the
              // vendor branch as well as on the trunk.
              //
              // We simply remove the dependency to the vendor branch,
              // which are imported independently.
              //
              //                root
              //                blob
              //                /    \.
              //           vendor     |
              //           branch     |
              //           symbol   initial
              //               |     import
              //           vendor    (1.1)
              //           branch     |
              //           start      |
              //               |     (+)
              //           initial    |
              //           import     |
              //          (1.1.1.1)   |
              //                \    /
              //              target_bi
              //               (symbol)
              //
              cvs.remove_deps(target_bi, bi_a);

              dfs_restart_needed = true;
            }
          else
            {
              // Okay, it's getting tricky here: the target blob is not a
              // branch or tag point, neither are bi_a nor bi_b. So we
              // have the following situation:
              //
              //                common
              //               ancestor              (+) stands for one
              //                /    \                   or more commit
              //              (+)     |                  blobs.
              //               |      |
              //            branch   (+)
              //            start     |
              //               |      |
              //              (+)     |    <----- from here on, there are
              //               |      |           only commit blobs
              //             bi_a    bi_b
              //                \    /
              //              target_bi
              //
              // As the target blob has a dependency on bi_a (which is not
              // a branchpoint), we have to split the target blob into
              // events which belong to path A and events which belong to
              // path B.

              vector< cvs_blob_index > tmp_a((++path_a.rbegin()).base() - first_branch_start_in_path_a);
              copy(first_branch_start_in_path_a, (++path_a.rbegin()).base(), tmp_a.begin());

              split_by_path func(cvs, tmp_a);

              // Count all events, and check where we can splitt
              int pa_deps = 0,
                  total_events = 0;

              for (blob_event_iter j = cvs.blobs[target_bi].get_events().begin();
                   j != cvs.blobs[target_bi].get_events().end(); ++j)
                {
                  bool depends_on_path_a = func(*j);

                  if (depends_on_path_a)
                    pa_deps++;
                  total_events++;
                }

              I(pa_deps > 0);

              if (pa_deps == total_events)
                {
                  // all events in the target blob depend in a way on the
                  // branch start in path a, thus we should better simply
                  // drop the dependency from bi_b to the target blob.
                  cvs.remove_deps(target_bi, bi_b);

                  dfs_restart_needed = true;
                }
              else
                {
                  I(pa_deps < total_events);
                  L(FL("splitting at path a"));
                  split_blob_at(cvs, target_bi, func);

                  dfs_restart_needed = true;
                }
            }
        }
      else
        {
          I(!a_has_branch);
          I(!b_has_branch);

          // If none of the two paths has a branch start, we can simply
          // join them into one path, which satisfies all of the
          // dependencies and is correct with regard to timestamps.
          //
          // At any position, we have an ancestor and two dependents, one
          // in each path. To streamline the graph, we take the younger
          // one first and adjust dependencies as follows:
          //
          //       ANC    (if A is younger than B)   ANC
          //      /   \             -->              /
          //     A     B                            A ->  B
          //
          // A then becomes the ancestor of the next round. We repeat
          // this until we reach the final blob which triggered the
          // cross edge (the target blob).

          vector<cvs_blob_index>::iterator ity_anc = path_a.begin();
          I(*ity_anc == *(path_b.begin()));

          vector<cvs_blob_index>::iterator ity_a = ++path_a.begin();
          vector<cvs_blob_index>::iterator ity_b = ++path_b.begin();

          blob_index_time_cmp blob_time_cmp(cvs);

          while ((*ity_a != target_bi) || (*ity_b != target_bi))
            {
              // Just to be extra sure, we reset the dependents
              // caches of all the tree blobs involved. This can
              // certainly be optimized.
              cvs.blobs[*ity_anc].reset_deps_cache();
              cvs.blobs[*ity_b].reset_deps_cache();
              cvs.blobs[*ity_a].reset_deps_cache();

              I(ity_a != path_a.end());
              I(ity_b != path_b.end());

              if ((!blob_time_cmp(*ity_a, *ity_b) || (*ity_a == target_bi)) &&
                  (*ity_b != target_bi))
                {
                  // swap path a and path b, so that path a contains the
                  // youngest blob, which needs to become the new common
                  // ancestor.
                  swap(path_a, path_b);
                  swap(ity_a, ity_b);
                }

              // ity_a comes before ity_b, so we drop the dependency
              // of ity_b to it's ancestor and add one to ity_a
              // instead.
              L(FL("  with common ancestor %d, blob %d wins over blob %d")
                % *ity_anc % *ity_a % *ity_b);

              cvs.remove_deps(*ity_b, *ity_anc);
              dfs_restart_needed = true;

              // If ity_b points to the last blob in path_b, the
              // common target blob, then we can abort the loop, because
              // it is also the end of path a.
              if (*ity_b == target_bi)
                break;

              // make very sure we don't introduce a back edge
              vector< cvs_blob_index > back_path;
              insert_iterator< vector< cvs_blob_index > >
                back_ity(back_path, back_path.end());

              dijkstra_shortest_path(cvs, *ity_a, *ity_b, back_ity,
                                     false,               // upwards,
                                     true, true, true,   // follow all
                                     false,
                                     make_pair(invalid_blob, invalid_blob));
              I(back_path.empty());


              // We want a dependency from *ity_a to *ity_b. We only need
              // to add one, if none exists.
              dijkstra_shortest_path(cvs, *ity_a, *ity_b, back_ity,
                                     true,               // downwards,
                                     true, true, true,   // follow all
                                     false,
                                     make_pair(invalid_blob, invalid_blob));
              if (back_path.empty())
                {
                  L(FL("  adding dependency from blob %d to blob %d") % *ity_a % *ity_b);
                  cvs.add_dependency(*ity_b, *ity_a);
                }
              else
                L(FL("  no need to add a dependency, there already exists one."));

              ity_anc = ity_a;
              ity_a++;
            }
        }
    }

  void back_edge(Edge e)
    {
#ifdef DEBUG_GRAPHVIZ
       set<cvs_blob_index> blobs_to_show;

       blobs_to_show.insert(e.first);
       blobs_to_show.insert(e.second);

      write_graphviz_partial(cvs, "invalid_back_edge", blobs_to_show, 5);
#endif

      L(FL("branch_sanitizer: back edge from blob %d (%s) to blob %d (%s) - ABORTING!")
        % e.first % get_event_repr(cvs, *cvs.blobs[e.first].begin())
        % e.second % get_event_repr(cvs, *cvs.blobs[e.second].begin()));

      I(false);
    }
};

// single blob split points: search only for intra-blob dependencies
// and return split points to resolve these dependencies.
time_i
get_best_split_point(cvs_history & cvs, cvs_blob_index bi)
{
  list< pair<time_i, time_i> > ib_deps;

  // Collect the conflicting intra-blob dependencies, storing the
  // timestamps of both events involved.
  for (blob_event_iter i = cvs.blobs[bi].begin();
       i != cvs.blobs[bi].end(); ++i)
    {
      cvs_event_ptr ev = *i;

      // check for time gaps between this event and it's dependencies
      for (dep_loop j = cvs.get_dependencies(ev); !j.ended(); ++j)
        {
          cvs_event_ptr dep = *j;

          if ((cvs.blobs[dep->bi].get_digest() == cvs.blobs[bi].get_digest()) &&
              (dep->bi == bi))
            {
              I(ev->adj_time != dep->adj_time);
              if (ev->adj_time > dep->adj_time)
                ib_deps.push_back(make_pair(dep->adj_time, ev->adj_time));
              else
                ib_deps.push_back(make_pair(ev->adj_time, dep->adj_time));
            }
        }

      // additionally, check for time gaps between this event and any other
      // events on the same file.
      for (blob_event_iter j = i + 1; j != cvs.blobs[bi].end(); ++j)
        if ((*i)->path == (*j)->path)
          {
            I((*i)->adj_time != (*j)->adj_time);
            if ((*i)->adj_time > (*j)->adj_time)
              ib_deps.push_back(make_pair((*j)->adj_time, (*i)->adj_time));
            else
              ib_deps.push_back(make_pair((*i)->adj_time, (*j)->adj_time));
          }
    }

  // What we have now, are durations, or multiple time ranges. We need to
  // split the blob somewhere in that range. But because there are multiple
  // files involved, these ranges can be overlapping, i.e.:
  //
  //           o    o   o       |
  //     A ->  |    |   |       | t       -o-      commits
  //           o    |   |       | i       A, B, C  possible split points
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

  typedef list< pair<time_i, time_i> >::iterator dep_ity;

  set< time_i > event_times;
  for (dep_ity i = ib_deps.begin(); i != ib_deps.end(); ++i)
    {
      if (event_times.find(i->first) == event_times.end())
        event_times.insert(i->first);

      if (event_times.find(i->second) == event_times.end())
        event_times.insert(i->second);
    }

  I(event_times.size() > 0);

  set<time_i>::const_iterator last, curr;
  last = event_times.begin();
  curr = last;
  curr++;
  pair<time_i, time_i> best_split_range = make_pair(0, 0);
  vector<dep_ity> deps_resolved_by_best_split;
  unsigned int best_score = 0;
  for ( ; curr != event_times.end(); ++curr)
    {
      time_i curr_split_point = *last + (*curr - *last) / 2;

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

  time_i best_split_point = best_split_range.first +
    (best_split_range.second - best_split_range.first) / 2;

  L(FL("Best split range: %d - %d (@%d)")
    % best_split_range.first % best_split_range.second % best_split_point);

  return best_split_point;
}

void
split_cycle(cvs_history & cvs, set< cvs_blob_index > const & cycle_members)
{
  cvs_blob_index blob_to_split;

  /* shortcut for intra blob dependencies */
  I(cycle_members.size() > 1);

  L(FL("choosing a blob to split (out of %d blobs)") % cycle_members.size());
  typedef set<cvs_blob_index>::const_iterator cm_ity;

  time_i largest_gap = 0;
  time_i largest_gap_at = 0;
  int largest_gap_blob = -1;

  for (cm_ity cc = cycle_members.begin(); cc != cycle_members.end(); ++cc)
    {
      // we never split branch starts or tags, instead we split the
      // underlying symbol.
      if (cvs.blobs[*cc].get_digest().is_branch_start() ||
          cvs.blobs[*cc].get_digest().is_tag())
        continue;

      // make sure the blob's events are sorted by timestamp
      cvs.blobs[*cc].sort_events();
      vector< cvs_event_ptr > & blob_events = cvs.blobs[*cc].get_events();

      blob_event_iter ity;

      cvs_event_ptr this_ev, last_ev;

      ity = blob_events.begin();
      this_ev = *ity;
      ++ity;
      for ( ; ity != blob_events.end(); ++ity)
        {
          last_ev = this_ev;
          this_ev = *ity;

          time_i time_diff = this_ev->adj_time - last_ev->adj_time;
          if (time_diff > largest_gap)
            {
              largest_gap = time_diff;
              largest_gap_at = last_ev->adj_time + time_diff / 2;
              largest_gap_blob = *cc;
            }
        }

      int count_intra_cycle_deps = 0;
      for (cm_ity dd = cycle_members.begin(); dd != cycle_members.end(); ++dd)
        {
          for (blob_event_iter ii = cvs.blobs[*cc].begin(); ii != cvs.blobs[*cc].end(); ++ii)
            {
              cvs_event_ptr ev = *ii;
              for (dep_loop j = cvs.get_dependencies(ev); !j.ended(); ++j)
                {
                  cvs_event_ptr dep = *j;

                  if (dep->bi == *dd)
                    count_intra_cycle_deps++;
                }
            }
        }

      if (count_intra_cycle_deps <= 0)
        W(F("  warning: no intra cycle dependencies... why is it a cycle???"));
    }

  if (largest_gap_at == 0)
    {
      W(F("Unable to split the following cycle:"));
      for (cm_ity cc = cycle_members.begin(); cc != cycle_members.end(); ++cc)
        {
          cvs_blob & blob = cvs.blobs[*cc];
          L(FL("  blob %d: %s") % *cc
            % (blob.get_digest().is_symbol() ? "symbol"
              : (blob.get_digest().is_branch_start() ? "branch start"
                : (blob.get_digest().is_tag() ? "tag" : "commit"))));

          if (blob.get_digest().is_commit())
            {
              for (blob_event_iter ii = blob.begin(); ii != blob.end(); ++ii)
                {
                  cvs_commit *ce = (cvs_commit*) *ii;
                  L(FL("    path: %s @ %s, time: %d")
                    % cvs.path_interner.lookup(ce->path)
                    % cvs.rcs_version_interner.lookup(ce->rcs_version)
                    % ce->adj_time);
                }
            }
        }
    }

  I(largest_gap_at != 0);
  I(largest_gap_blob >= 0);
  I(!cvs.blobs[largest_gap_blob].get_digest().is_branch_start());

  split_by_time func(largest_gap_at);
  L(FL("splitting by time %d") % largest_gap_at);
  split_blob_at(cvs, largest_gap_blob, func);
}

void
split_blob_at(cvs_history & cvs, const cvs_blob_index blob_to_split,
              split_decider_func & split_decider)
{
  // make sure the blob's events are sorted by timestamp
  cvs_blob_index bi = blob_to_split;
  cvs.blobs[bi].sort_events();

  // Reset the dependents cache of the origin blob.
  cvs.blobs[bi].reset_deps_cache();

  // some short cuts
  cvs_event_digest d = cvs.blobs[bi].get_digest();
  vector<cvs_event_ptr>::iterator i;

  L(FL("splitting blob %d") % bi);

  // make sure we can split the blob
  I(cvs.blobs[bi].get_events().size() > 1);

  // Add a blob
  cvs_blob_index new_bi = cvs.add_blob(cvs.blobs[bi].etype,
                                       cvs.blobs[bi].symbol)->second;

  // Reassign events to the new blob as necessary
  for (i = cvs.blobs[bi].get_events().begin(); i != cvs.blobs[bi].get_events().end(); )
    {
      // Assign the event to the existing or to the new blob
      if (split_decider(*i))
        {
          cvs.blobs[new_bi].get_events().push_back(*i);
          (*i)->bi = new_bi;

          // Reset the dependents cache of all dependencies of this event
          for (dep_loop j = cvs.get_dependencies(*i); !j.ended(); ++j)
            {
              cvs_blob_index dep_bi = (*j)->bi;
              cvs.blobs[dep_bi].reset_deps_cache();
            }

          // delete from old bolb and advance
          i = cvs.blobs[bi].get_events().erase(i);
        }
      else
        {
          // Force a new sorting step to the dependents cache of all
          // dependencies of this event, as it's avg time most probably
          // changed. Thus the ordering must change.
          for (dep_loop j = cvs.get_dependencies(*i); !j.ended(); ++j)
            {
              cvs_blob_index dep_bi = (*j)->bi;
              cvs.blobs[dep_bi].resort_deps_cache();
            }

          // advance
          i++;
        }
    }

  I(!cvs.blobs[bi].empty());
  I(!cvs.blobs[new_bi].empty());
}

bool
resolve_intra_blob_conflicts_for_blob(cvs_history & cvs, cvs_blob_index bi)
{
  cvs_blob & blob = cvs.blobs[bi];
  for (vector<cvs_event_ptr>::iterator i = blob.begin(); i != blob.end(); ++i)
    {
      for (vector<cvs_event_ptr>::iterator j = i + 1; j != blob.end(); )
        if (((*i)->path == (*j)->path) &&
             ((*i)->adj_time == (*j)->adj_time))
          {
            // For events in the *same* blob, with the *same* changelog
            // and author or same symbol name, we simply merge the
            // events. They were not even important enough to get
            // different changelog texts. Most probably they originate
            // from duplicate RCS files in Attic and alive.

            L(FL("merging events %s and %s") % get_event_repr(cvs, *i) % get_event_repr(cvs, *j));

            if (blob.etype == ET_COMMIT)
              {
                cvs_commit *ci = (cvs_commit*) (*i);
                cvs_commit *cj = (cvs_commit*) (*j);
                I(ci->rcs_version == cj->rcs_version);
              }

            // let the first take over its dependencies...
            for (dep_loop k = cvs.get_dependencies(*j); !k.ended(); ++k)
              {
                event_dep_iter ep(k.get_pos());
                I(ep->first == *j);
                ep->first = *i;
              }
            cvs.blobs[(*i)->bi].reset_deps_cache();

            // ..and correct dependents
            for (dep_loop k = cvs.get_dependents(*j); !k.ended(); ++k)
              {
                event_dep_iter ep(k.get_pos());
                I(ep->first == *j);
                ep->first = *i;
                cvs.blobs[ep->second->bi].reset_deps_cache();
              }

            cvs.sort_dependencies();

            // remove second event from the blob
            j = blob.get_events().erase(j);
          }
        else
          ++j;
    }

  // We don't need to split because of end of branch events, which
  // might have different timestamps even if they conflict, thus they
  // won't be merged above.
  if (blob.etype == ET_BRANCH_END)
    return true;

  for (blob_event_iter i = blob.begin(); i != blob.end(); ++i)
    {
      for (blob_event_iter j = i + 1; j != blob.end(); ++j)
        if ((*i)->path == (*j)->path)
          {
            L(FL("Trying to split blob %d, because of events %s and %s")
              % bi % get_event_repr(cvs, *i) % get_event_repr(cvs, *j));
            split_by_time func(get_best_split_point(cvs, bi));
            split_blob_at(cvs, bi, func);
            return false;
          }
    }
  return true;
}

// This is a somewhat rude approach to circumvent certain errors. It
// simply makes sure that no blob contains multiple events for a single
// path. Otherwise, the blob gets split.
void
resolve_intra_blob_conflicts(cvs_history & cvs, ticker & n_splits)
{
  for (cvs_blob_index bi = 0; bi < cvs.blobs.size(); ++bi)
    {
      while (!resolve_intra_blob_conflicts_for_blob(cvs, bi))
        ++n_splits;
    }
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
          label = (FL("blob %d: commit\\n") % v).str();

          if (b.empty())
            {
              label += "empty blob!!!";
            }
          else
            {
              string author, clog;
              cvs.split_authorclog(b.authorclog, author, clog);
              label += author + "\\n";

              // limit length of changelog we output
              if (clog.length() > 40)
                clog = clog.substr(0, 40);

              // poor man's escape...
              for (unsigned int i = 0; i < clog.length(); ++i)
                {
                  if (clog[i] < 32)
                    clog[i] = '.';
                  if (clog[i] == '\"')
                    clog[i] = '\'';
                }
              label += "\\\"" + clog + "\\\"\\n";
            }
        }
      else if (b.get_digest().is_branch_start())
        {
          label = (FL("blob %d: start of branch: ") % v).str();

          if (b.empty())
            {
              label += "empty blob!!!";
            }
          else
            {
              label += cvs.symbol_interner.lookup(b.symbol);
              label += "\\n";
            }
        }
      else if (b.get_digest().is_branch_end())
        {
          label = (FL("blob %d: end of branch: ") % v).str();

          if (b.empty())
            {
              label += "empty blob!!!";
            }
          else
            {
              label += cvs.symbol_interner.lookup(b.symbol);
              label += "\\n";
            }
        }
      else if (b.get_digest().is_tag())
        {
          label = (FL("blob %d: tag: ") % v).str();

          if (b.empty())
            {
              label += "empty blob!!!";
            }
          else
            {
              label += cvs.symbol_interner.lookup(b.symbol);
              label += "\\n";
            }
        }
      else if (b.get_digest().is_symbol())
        {
          label = (FL("blob %d: symbol %s\\n")
                   % v % cvs.symbol_interner.lookup(b.symbol)).str();
        }
      else
        {
          label = (FL("blob %d: unknow type\\n") % v).str();
        }

      if (!b.empty())
        {
          struct tm * timeinfo;
          char buffer [80];
          time_t tt = b.get_avg_time() / 100;

          timeinfo = localtime(&tt);
          strftime (buffer, 80, "t: %Y-%m-%d %H:%M:%S\\n", timeinfo);

          // print the time of the blob
          label += buffer;
          label += "\\n";

          // print the contents of the blob, i.e. the single files
          for (blob_event_iter i = b.begin(); i != b.end(); i++)
            {
              cvs_commit *ce = (cvs_commit*) *i;
              label += cvs.path_interner.lookup((*i)->path);

              if (b.get_digest().is_commit())
                {
                  tt = (*i)->adj_time / 100;
                  timeinfo = localtime(&tt);
                  strftime (buffer, 80, " %Y-%m-%d %H:%M:%S", timeinfo);

                  label += "@";
                  label += cvs.rcs_version_interner.lookup(ce->rcs_version);
                  label += buffer;
                }
              label += "\\n";
            }
        }
      else
        label += "-- empty --";

      out << label;
    }
};

void
write_graphviz(std::ofstream & of, cvs_history & cvs,
               blob_label_writer & blw,
               set<cvs_blob_index> const & blobs_to_show,
               set<cvs_blob_index> const & blobs_to_mark)
{
  of << "digraph G {\n";

  for (unsigned int i = 0; i < cvs.blobs.size(); ++i)
    if ((blobs_to_show.find(i) != blobs_to_show.end()) ||
        blobs_to_show.empty())
    {
      of << (FL("  blob%d [label=\"") % i);
      blw(of, i);
      of << "\"]\n";

      for (blob_index_iter j = cvs.blobs[i].get_dependents(cvs).begin();
           j != cvs.blobs[i].get_dependents(cvs).end(); ++j)
        if ((blobs_to_show.find(*j) != blobs_to_show.end()) ||
            blobs_to_show.empty())
        {
          of << (FL("  blob%d -> blob%d\n") % i % *j);
          if ((blobs_to_mark.find(i) != blobs_to_mark.end()) &&
              (blobs_to_mark.find(*j) != blobs_to_mark.end()))
            of << " [color=red]";
          of << "\n";
        }
    }

  of << "};\n";
}

void
write_graphviz_complete(cvs_history & cvs, string const & desc)
{
  std::ofstream viz_file;
  blob_label_writer blw(cvs);

  set<cvs_blob_index> blobs_to_show;
  set<cvs_blob_index> blobs_to_mark;

  cvs.step_no++;
  viz_file.open((FL("cvs_graph.%s.%d.viz") % desc % cvs.step_no).str().c_str());
  write_graphviz(viz_file, cvs, blw, blobs_to_show, blobs_to_mark);
  viz_file.close();
}

void
write_graphviz_partial(cvs_history & cvs, string const & desc,
                       set<cvs_blob_index> & blobs_to_mark,
                       int add_depth)
{
  std::ofstream viz_file;
  blob_label_writer blw(cvs);

  stack< pair< cvs_blob_index, int > > stack;
  set<cvs_blob_index> blobs_to_show;

  for (set<cvs_blob_index>::iterator i = blobs_to_mark.begin();
       i != blobs_to_mark.end(); ++i)
    stack.push(make_pair(*i, 0));

  while (!stack.empty())
    {
      cvs_blob_index bi = stack.top().first;
      int depth = stack.top().second;
      stack.pop();

      blobs_to_show.insert(bi);

      depth++;
      if (depth < add_depth)
        {
          cvs_blob & blob = cvs.blobs[bi];
          vector<cvs_blob_index> deps = blob.get_dependents(cvs);
          for (blob_index_iter j = deps.begin(); j != deps.end(); ++j)
            if (blobs_to_show.find(*j) == blobs_to_show.end())
              stack.push(make_pair(*j, depth));

          for (blob_event_iter j = blob.begin(); j != blob.end(); ++j)
            for (dep_loop k = cvs.get_dependencies(*j); !k.ended(); ++k)
              {
                cvs_blob_index dep_bi = (*k)->bi;
                if (blobs_to_show.find(dep_bi) == blobs_to_show.end())
                  stack.push(make_pair(dep_bi, depth));
              }
        }
    }

  cvs.step_no++;
  viz_file.open((FL("cvs_graph.%s.%d.viz") % desc % cvs.step_no).str().c_str());
  write_graphviz(viz_file, cvs, blw, blobs_to_show, blobs_to_mark);
  viz_file.close();
}

#endif


vector<cvs_blob_index> & cvs_blob::get_dependents(cvs_history & cvs)
{
  if (has_cached_deps)
    {
      if (!cached_deps_are_sorted)
        sort_deps_cache(cvs);

      return dependents_cache;
    }

  // clear the cache
  dependents_cache.clear();

  // fill the dependency cache from the single event deps
  for (blob_event_iter i = begin(); i != end(); ++i)
    {
      for (dep_loop j = cvs.get_dependents(*i); !j.ended(); ++j)
        {
          cvs_blob_index dep_bi = (*j)->bi;
          if (find(dependents_cache.begin(), dependents_cache.end(), dep_bi) == dependents_cache.end())
            dependents_cache.push_back(dep_bi);
        }
    }

  has_cached_deps = true;
  sort_deps_cache(cvs);

  return dependents_cache;
}

void cvs_blob::sort_deps_cache(cvs_history & cvs)
{
  // sort by timestamp
  I(has_cached_deps);
  blob_index_time_cmp cmp(cvs);
  sort(dependents_cache.begin(), dependents_cache.end(), cmp);
  cached_deps_are_sorted = true;
}

void cvs_blob::sort_events(void)
{
  if (!events_are_sorted)
    {
      event_ptr_time_cmp cmp;
      sort(events.begin(), events.end(), cmp);
      events_are_sorted = true;
    }
}

template<typename Visitor>
void cvs_history::depth_first_search(Visitor & vis,
       back_insert_iterator< vector<cvs_blob_index> > oi)
  {
    dfs_context ctx;

    for (vector<cvs_blob>::iterator ity = blobs.begin();
         ity != blobs.end(); ++ity)
      ity->color = white;

    // start with blob 0
    ctx.bi = 0;
    // vis.discover_vertex(bi);
    blobs[ctx.bi].color = grey;
    ctx.ei = blobs[ctx.bi].get_dependents(*this).begin();

    stack< dfs_context > stack;

    // possible optimization for vector based stack:
    // stack.reserve()

    stack.push(ctx);

    while (!stack.empty() && !vis.abort())
      {
        dfs_context ctx = stack.top();
        stack.pop();
        while ((ctx.ei != blobs[ctx.bi].get_dependents(*this).end()) &&
                !vis.abort())
          {
            // vis.examine_edge(*ei, g);
            if (blobs[*ctx.ei].color == white)
              {
                vis.tree_edge(make_pair(ctx.bi, *ctx.ei));

                // push the current context to the stack and, but
                // advance to the next edge, as we are processing this
                // one just now.
                stack.push(ctx);
                stack.top().ei++;

                // switch to that blob and follow its edges
                ctx.bi = *ctx.ei;
                blobs[ctx.bi].color = grey;
                // vis.discover_vertex(bi, g);
                ctx.ei = blobs[ctx.bi].get_dependents(*this).begin();
              }
            else if (blobs[*ctx.ei].color == grey)
              {
                vis.back_edge(make_pair(ctx.bi, *ctx.ei));
                ++ctx.ei;
              }
            else
              {
                vis.forward_or_cross_edge(make_pair(ctx.bi, *ctx.ei));
                ++ctx.ei;
              }
          }
        blobs[ctx.bi].color = black;
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
resolve_blob_dependencies(cvs_history & cvs, ticker & n_splits)
{
  L(FL("Breaking dependency cycles (%d blobs)") % cvs.blobs.size());

#ifdef DEBUG_GRAPHVIZ
    write_graphviz_complete(cvs, "all");
#endif

  while (1)
  {
    // this set will be filled with the blobs in a cycle
    set< cvs_blob_index > cycle_members;

    cvs.import_order.clear();
    blob_splitter vis(cvs, cycle_members);
    cvs.depth_first_search(vis, back_inserter(cvs.import_order));

    // If we have a cycle, go split it. Otherwise we don't have any
    // cycles left and can proceed.
    if (!cycle_members.empty())
      split_cycle(cvs, cycle_members);
    else
      break;

    ++n_splits;
  };

  // remove all weak dependencies
  cvs.remove_weak_dependencies();

#ifdef DEBUG_GRAPHVIZ
  write_graphviz_complete(cvs, "no-weak");
#endif

  // After a depth-first-search-run without any cycles, we have a possible
  // import order which satisfies all the dependencies (topologically
  // sorted).
  //
  // Now we inspect forward or cross edges to make sure no blob ends up in
  // two branches.
  while (1)
    {
      bool dfs_restart_needed = false;
      cvs.import_order.clear();
      branch_sanitizer vis(cvs, dfs_restart_needed);
      cvs.depth_first_search(vis, back_inserter(cvs.import_order));

      if (!dfs_restart_needed)
        break;
    }

#ifdef DEBUG_GRAPHVIZ
    write_graphviz_complete(cvs, "all");
#endif
}

void
number_unnamed_branches(cvs_history & cvs)
{
  int nr = 1;

  for (cvs_blob_index bi = 0; bi < cvs.blobs.size(); ++bi)
    {
      cvs_blob & blob = cvs.blobs[bi];

      if (blob.get_digest().is_symbol())
        {
          // handle unnamed branches
          string sym_name = cvs.symbol_interner.lookup(blob.symbol);
          if (sym_name.empty())
            {
              sym_name = (FL("UNNAMED_BRANCH_%d") % nr++).str();
              blob.symbol = cvs.symbol_interner.intern(sym_name);
              cvs.blob_index.insert(make_pair(blob.get_digest(), bi));

              // FIXME: after renaming the symbol, we should rename the
              //         dependent branch_start blob, no?
            }
        }
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
  cvs.base_branch = cvs.symbol_interner.intern(bn);


  // first step of importing legacy VCS: collect all revisions
  // of all files we know. This already creates file deltas and
  // hashes. We end up with a DAG of blobs.
  {
    cvs_tree_walker walker(cvs, app);
    require_path_is_directory(cvsroot,
                              F("path %s does not exist") % cvsroot,
                              F("'%s' is not a directory") % cvsroot);
    app.db.ensure_open();
    change_current_working_dir(cvsroot);
    walk_tree(file_path(), walker);
  }

  // try to sanitize the timestamps within all RCS files with
  // respect to the dependencies given.
  {
    for (blob_event_iter i = cvs.blobs[cvs.root_blob].begin();
         i != cvs.blobs[cvs.root_blob].end(); ++i)
      sanitize_rcs_file_timestamps(cvs, *i);
  }

  // then we use algorithms from graph theory to get the blobs into
  // a logically meaningful ordering.
  {
    ticker n_splits(_("blob splits"), "p", 1);
    resolve_intra_blob_conflicts(cvs, n_splits);
    resolve_blob_dependencies(cvs, n_splits);
  }

  // number through all unnamed branches
  number_unnamed_branches(cvs);
  ticker n_revs(_("revisions"), "r", 1);
  {
    transaction_guard guard(app.db);
    blob_consumer cons(cvs, app, n_revs);
    for_each(cvs.import_order.rbegin(), cvs.import_order.rend(), cons);
    guard.commit();
  }

  return;
}

void
cvs_blob::add_missing_parents(file_path const & path,
                              roster_t & base_ros,
                              cset & cs)
{
  if (cs.dirs_added.find(path) != cs.dirs_added.end())
    return;

  if (base_ros.has_node(path))
    {
      node_t n(base_ros.get_node(path));
      I(is_dir_t(n));
      return;
    }

  if (!path.empty())
    add_missing_parents(path.dirname(), base_ros, cs);

  safe_insert(cs.dirs_added, path);
}

int
cvs_blob::build_cset(cvs_history & cvs,
                     roster_t & base_ros,
                     cset & cs)
{
  int changes = 0;

  for (blob_event_iter i = begin(); i != end(); ++i)
    {
      I(cvs.blobs[(*i)->bi].get_digest().is_commit());

      cvs_commit *ce = (cvs_commit*) *i;

      file_path pth = file_path_internal(cvs.path_interner.lookup(ce->path));
      file_id new_file_id(cvs.mtn_version_interner.lookup(ce->mtn_version));

      if (ce->alive)
        {
          if (!base_ros.has_node(pth))
            {
              add_missing_parents(pth.dirname(), base_ros, cs);
              L(FL("  adding entry state for file '%s': '%s'") % pth % new_file_id);
              safe_insert(cs.files_added, make_pair(pth, new_file_id));
              changes++;
            }
          else
            {
              node_t n(base_ros.get_node(pth));
              I(is_file_t(n));

              file_t fn(downcast_to_file_t(n));
              if (fn->content != new_file_id)
                {
                  L(FL("  applying state delta on file '%s' : '%s' -> '%s'")
                    % pth % fn->content % new_file_id);
                  safe_insert(cs.deltas_applied,
                    make_pair(pth, make_pair(fn->content, new_file_id)));
                  changes++;
                }
            }
        }
      else
        {
          if (base_ros.has_node(pth))
            {
              L(FL("  deleting file '%s'") % pth);
              cs.nodes_deleted.insert(pth);
              changes++;
            }
        }
    }

  // add an attribute to the root node, which keeps track of what
  // files at what RCS versions have gone into this revision. 
  string fval = "cvs\n";
  event_ptr_path_strcmp cmp(cvs);
  sort(begin(), end(), cmp);
  for (blob_event_iter i = begin(); i != end(); ++i)
    {
      cvs_commit *ce = (cvs_commit*) *i;
      fval += cvs.path_interner.lookup(ce->path) + "@";
      fval += cvs.rcs_version_interner.lookup(ce->rcs_version) + "\n";
    }

  attr_key k("mtn:origin_info");
  attr_value v(fval);

  safe_insert(cs.attrs_set, make_pair(make_pair(file_path(), k), v));

  return changes;
}

void
add_missing_parents(roster_t & src_roster, node_t & n, roster_t & dest_roster)
{
  if (null_node(n->parent))
    return;

  if (!dest_roster.has_node(n->parent))
    {
      node_t parent_node = src_roster.get_node(n->parent);
      add_missing_parents(src_roster, parent_node, dest_roster);

      I(!dest_roster.has_node(parent_node->self));
      dest_roster.create_dir_node(parent_node->self);

      node_t dest_parent_node = dest_roster.get_node(parent_node->self);
      dir_t dest_parent_dir(downcast_to_dir_t(dest_parent_node));
      I(parent_node->self == dest_parent_node->self);
      dest_roster.attach_node(dest_parent_node->self, parent_node->parent, parent_node->name);
    }
}

void
blob_consumer::merge_parents_for_artificial_rev(
  cvs_blob_index bi,
  set<revision_id> & parent_rids,
  map<cvs_event_ptr, revision_id> & event_parent_rids)
{
  revision_id left_rid, right_rid, merged_rid;

  if (parent_rids.size() > 2)
    I(false);  // should recursively call itself...
  else
    {
      left_rid = *parent_rids.begin();
      right_rid = *(++parent_rids.begin());
    }

  I(left_rid != right_rid);

  // We start from the left roster, and "copy" over files from
  // the right one, where necessary.
  roster_t left_roster, right_roster, merged_roster;

  app.db.get_roster(left_rid, left_roster);
  app.db.get_roster(right_rid, right_roster);

  // copy the left roster, we start from that one and apply changes
  // from the right roster where necessary.
  merged_roster = left_roster;

  L(FL("  merging %s") % left_rid);
  L(FL("      and %s") % right_rid);

  for (map<cvs_event_ptr, revision_id>::iterator i = event_parent_rids.begin();
       i != event_parent_rids.end(); ++i)
    {
      cvs_event_ptr ev = i->first;
      revision_id wanted_rid = i->second;

      file_path pth = file_path_internal(cvs.path_interner.lookup(ev->path));
      L(FL("    handling file %s") % pth);

      if (wanted_rid == right_rid)
        {
          if (right_roster.has_node(pth))
            {
              node_t right_node(right_roster.get_node(pth));
              I(is_file_t(right_node));

              file_t right_fn(downcast_to_file_t(right_node));

              if (merged_roster.has_node(pth))
                {
                  node_t merged_node(merged_roster.get_node(pth));
                  I(is_file_t(merged_node));
                  file_t merged_fn(downcast_to_file_t(merged_node));

                  // FIXME: if there were renames, this could be false, but
                  //         as we never rename for CVS imports, it should
                  //         always hold true

                  I(!null_node(merged_fn->self));
                  I(merged_fn->self == right_fn->self);

                  if (merged_fn->content != right_fn->content)
                    {
                      merged_fn->content = right_fn->content;

                      L(FL("    using right revision for file '%s' at '%s'")
                        % pth % right_fn->content);
                    }
                  else
                    L(FL("    using right revision for file '%s' as is.")
                      % pth);
                }
              else
                {
                  // FIXME: again, same renaming issue applies 
                  I(!merged_roster.has_node(right_fn->self));

                  add_missing_parents(right_roster, right_node,
                                      merged_roster);

                  merged_roster.create_file_node(right_fn->content,
                                                 right_fn->self);
                  node_t merged_node(
                    merged_roster.get_node(right_fn->self));
                  I(merged_node->self == right_fn->self);
                  merged_roster.attach_node(merged_node->self,
                                            right_fn->parent, right_fn->name);

                  L(FL("    using right revision for file '%s' at '%s'")
                    % pth % right_fn->content);
                }
            }
          else
            {
              // The right node does not have that node, but we are asked to
              // inherit from it, so should remove the node.
              node_id nid = merged_roster.detach_node(pth);
              merged_roster.drop_detached_node(nid);

              // FIXME: possibly delete empty directories here...

              L(FL("    using right revision for file '%s' (deleted)") % pth);
            }
        }
      else if (wanted_rid == left_rid)
        {
          L(FL("    using left revision for file '%s'") % pth);
        }
      else
        {
          L(FL("    none, using left revision for file '%s'") % pth);
        }
    }

  {
    // write new files into the db, this is mostly taken from
    // store_roster_merge_result(), but with some tweaks.
    merged_roster.check_sane();

    revision_t merged_rev;
    merged_rev.made_for = made_for_database;

    calculate_ident(merged_roster, merged_rev.new_manifest);

    // Check if the new_manifest is really new, perhaps we
    // don't even need to add an artificial revision for these
    // two parents.
    revision_t left_rev, right_rev;
    app.db.get_revision(left_rid, left_rev);
    app.db.get_revision(right_rid, right_rev);

    if (merged_rev.new_manifest == left_rev.new_manifest)
      merged_rid = left_rid;
    else if (merged_rev.new_manifest == right_rev.new_manifest)
      merged_rid = right_rid;
    else
      {
        shared_ptr<cset> left_to_merged(new cset);
        make_cset(left_roster, merged_roster, *left_to_merged);
        safe_insert(merged_rev.edges, make_pair(left_rid, left_to_merged));

        shared_ptr<cset> right_to_merged(new cset);
        make_cset(right_roster, merged_roster, *right_to_merged);
        safe_insert(merged_rev.edges, make_pair(right_rid, right_to_merged));

        revision_data merged_data;
        write_revision(merged_rev, merged_data);
        calculate_ident(merged_data, merged_rid);
        {
          transaction_guard guard(app.db);

          app.db.put_revision(merged_rid, merged_rev);

          guard.commit();
        }

        L(FL("  created merged revision %s") % merged_rid);
      }
    }

  // loop over the event_parent_rids again to set the new merged_rid
  // where necessary.
  for (map<cvs_event_ptr, revision_id>::iterator i = event_parent_rids.begin();
       i != event_parent_rids.end(); ++i)
    {
      cvs_event_ptr ev = i->first;
      revision_id & wanted_rid = i->second;

      if ((wanted_rid == right_rid) || (wanted_rid == left_rid))
        wanted_rid = merged_rid;
    }

  // Remove the left and right revision_id, and add the merged one
  // instead.
  parent_rids.erase(left_rid);
  parent_rids.erase(right_rid);

  safe_insert(parent_rids, merged_rid);

  L(FL("  reduced number of parent rids to: %d") % parent_rids.size());
}

void
blob_consumer::create_artificial_revisions(cvs_blob_index bi,
                                           set<cvs_blob_index> & parent_blobs,
                                           revision_id & parent_rid,
                                           cvs_symbol_no & in_branch)
{
  L(FL("creating artificial revision for %d parents.")
    % parent_blobs.size());

  set<revision_id> parent_rids;
  map<cvs_symbol_no, int> parent_branches;
  map<cvs_event_ptr, revision_id> event_parent_rids;

  // While a blob in our graph can have multiple ancestors, which depend on
  // each other, monotone cannot represent that. Instead, we have to remove
  // all ancestors from the set. I.e.:
  //
  //        A ___,                    A
  //        |     \                   |
  //        |      |          =>      |
  //        v      v                  v
  //        B -> blob                 B -> blob

  // get all parent revision ids
  for (set<cvs_blob_index>::iterator i = parent_blobs.begin();
       i != parent_blobs.end(); ++i)
    safe_insert(parent_rids, cvs.blobs[*i].assigned_rid);

  // then erase the ancestors, because we cannot merge with a
  // descendent.
  I(parent_rids.size() >= 2);
  erase_ancestors(parent_rids, app);

  // loop over all events in the blob, to find the most recent ancestor,
  // from which we inherit - and possibly back patch.
  for (vector<cvs_event_ptr>::iterator i = cvs.blobs[bi].begin();
       i != cvs.blobs[bi].end(); ++i)
    {
      set<cvs_blob_index> event_parent_blobs;
      for (dep_loop j = cvs.get_dependencies(*i); !j.ended(); ++j)
        {
          cvs_event_ptr dep = *j;
          cvs_blob_index dep_bi = dep->bi;
          if (event_parent_blobs.find(dep_bi) == event_parent_blobs.end())
            event_parent_blobs.insert(dep_bi);
        }

      I(event_parent_blobs.size() == 1);
      cvs_blob & event_parent = cvs.blobs[*event_parent_blobs.begin()];

      if (parent_rids.find(event_parent.assigned_rid) != parent_rids.end())
        {
          // this parent has no descendents in parent_rids, so we can use
          // that to inherit from.
          event_parent_rids.insert(make_pair(*i, event_parent.assigned_rid));

          if (parent_branches.find(event_parent.in_branch) == 
                                                      parent_branches.end())
            parent_branches.insert(make_pair(event_parent.in_branch, 1));
          else
            parent_branches.find(event_parent.in_branch)->second++;
        }
      else
        {
          // this parent already has a descendent in parent_rids, so we need
          // to find that and inherit from that, but back patch.
          set<revision_id>::iterator j;
          for (j = parent_rids.begin(); j != parent_rids.end(); ++j)
            if (is_ancestor(event_parent.assigned_rid, *j, app))
              break;
          I(j != parent_rids.end());

          event_parent_rids.insert(make_pair(*i, *j));
        }
    }

  I(!parent_branches.empty());
  if (parent_branches.size() > 1)
    {
      // Uh... decide for the branch which the highest counter, i.e.
      // from which most files inherit.
      int max = 0;
      for (map<cvs_symbol_no, int>::iterator i = parent_branches.begin();
           i != parent_branches.end(); ++i)
        if (i->second > max)
          {
            in_branch = i->first;
            max = i->second;
          }
    }
  else
    in_branch = parent_branches.begin()->first;

  if (parent_rids.size() > 1)
    merge_parents_for_artificial_rev(bi, parent_rids, event_parent_rids);

  L(FL("remaining parent rids: %d") % parent_rids.size());
  I(parent_rids.size() == 1);
  parent_rid = *parent_rids.begin();

  revision_id new_rid;
  roster_t ros;
  shared_ptr<cset> cs(new cset());

  I(!null_id(parent_rid));
  app.db.get_roster(parent_rid, ros);

  int changes = 0;

  // Here, we have exactly one parent revision id remaining. Possibly,
  // we still need to create an artificial revision based on that
  // parent but with back patches for some files to older revisions.
  for (blob_event_iter i = cvs.blobs[bi].begin();
       i != cvs.blobs[bi].end(); ++i)
    {
      set<cvs_blob_index> event_parent_blobs;
      for (dep_loop j = cvs.get_dependencies(*i); !j.ended(); ++j)
        {
          cvs_event_ptr dep = *j;
          cvs_blob_index dep_bi = dep->bi;
          if (event_parent_blobs.find(dep_bi) == event_parent_blobs.end())
            event_parent_blobs.insert(dep_bi);
        }

      I(event_parent_blobs.size() == 1);
      revision_id & ev_parent_rid = 
        cvs.blobs[*event_parent_blobs.begin()].assigned_rid;

      if (ev_parent_rid != parent_rid)
        {
          roster_t e_ros;

          if (!null_id(ev_parent_rid))
            {
              // event needs reverting patch
              app.db.get_roster(ev_parent_rid, e_ros);

              file_path pth = file_path_internal(cvs.path_interner.lookup((*i)->path));

              L(FL("  checking file '%s'") % pth);

              if (e_ros.has_node(pth) && ros.has_node(pth))
                {
                  node_t base_node(ros.get_node(pth)),
                         target_node(e_ros.get_node(pth));
                  I(is_file_t(base_node));
                  I(is_file_t(target_node));
                  file_t base_fn(downcast_to_file_t(base_node)),
                         target_fn(downcast_to_file_t(target_node));

                  // FIXME: renaming issue!
                  I(base_node->self == target_node->self);

                  if (base_fn->content != target_fn->content)
                    {
                      L(FL("  applying reverse delta on file '%s' : '%s' -> '%s'")
                        % pth % base_fn->content % target_fn->content);
                      safe_insert(cs->deltas_applied,
                        make_pair(pth, make_pair(base_fn->content, target_fn->content)));
                      changes++;
                    }
                }
              else if (!e_ros.has_node(pth) && ros.has_node(pth))
                {
                  L(FL("  dropping file '%s'") % pth);
                  safe_insert(cs->nodes_deleted, pth);
                  changes++;
                }
              else if (e_ros.has_node(pth) && !ros.has_node(pth))
                {
                  node_t target_node(e_ros.get_node(pth));
                  I(is_file_t(target_node));
                  file_t target_fn(downcast_to_file_t(target_node));

                  // Hm.. that's going to create a newish node id, which
                  // might not be what we want. OTOH, we don't have
                  // resurrection, yet.
                  L(FL("  re-adding file '%s' at '%s'")
                    % pth % target_fn->content);
                  safe_insert(cs->files_added,
                    make_pair(pth, target_fn->content));
                  changes++;
                }
            }
          else
            {
              // path should not exist in this revision
              L(FL("warning: should remove %s") % cvs.path_interner.lookup((*i)->path));
              I(false);
            }
        }
    }

  // only if at least one file has been changed (reverted), we need to
  // add an artificial revision (otherwise we would get the same
  // revision_id anyway).
  if (changes > 0)
    {
      editable_roster_base editable_ros(ros, nis);
      cs->apply_to(editable_ros);

      manifest_id child_mid;
      calculate_ident(ros, child_mid);

      revision_t rev;
      rev.made_for = made_for_database;
      rev.new_manifest = child_mid;
      rev.edges.insert(make_pair(parent_rid, cs));

      calculate_ident(rev, new_rid);

      L(FL("creating new revision %s") % new_rid);

      if (app.db.put_revision(new_rid, rev))
      {
        time_i avg_time = cvs.blobs[bi].get_avg_time();
        time_t commit_time = avg_time / 100;
        string author("mtn:cvs_import"), changelog("artificial revision");
        string bn = cvs.get_branchname(cvs.blobs[bi].in_branch);
        I(!bn.empty());
        app.get_project().put_standard_certs(new_rid,
              branch_name(bn),
              utf8(changelog),
              date_t::from_unix_epoch(commit_time),
              utf8(author));

        ++n_revisions;
      }

      parent_rid = new_rid;
    }
}

void
blob_consumer::operator()(cvs_blob_index bi)
{
  cvs_blob & blob = cvs.blobs[bi];

  // Check what revision to use as a parent revision.
  set< cvs_blob_index > parent_blobs;
  for (blob_event_iter i = blob.begin(); i != blob.end(); ++i)
    for (dep_loop j = cvs.get_dependencies(*i); !j.ended(); ++j)
      {
        cvs_event_ptr dep = *j;
        cvs_blob_index dep_bi = dep->bi;

        if (parent_blobs.find(dep_bi) == parent_blobs.end())
          parent_blobs.insert(dep_bi);
      }

  revision_id parent_rid;
  if (parent_blobs.empty())
    blob.in_branch = cvs.base_branch;
  else
    {
      cvs_event_digest d = blob.get_digest();
      if ((d.is_branch_start() || d.is_branch_end() || d.is_tag())
          && (parent_blobs.size() > 1))
        {
          create_artificial_revisions(bi, parent_blobs,
                                      parent_rid, blob.in_branch);
        }
      else
        {
          I(parent_blobs.size() == 1);
          blob.in_branch = cvs.blobs[*parent_blobs.begin()].in_branch;
          parent_rid = cvs.blobs[*parent_blobs.begin()].assigned_rid;
        }
    }

  L(FL("parent rid: %s") % parent_rid);
  blob.assigned_rid = parent_rid;

  if (blob.get_digest().is_commit())
    {
      // we should never have an empty blob; it's *possible* to have
      // an empty changeset (say on a vendor import) but every blob
      // should have been created by at least one file commit, even
      // if the commit made no changes. it's a logical inconsistency if
      // you have an empty blob.
      L(FL("consuming blob %d: commit") % bi);
      I(!blob.empty());

      if (app.opts.dryrun)
        {
          ++n_revisions;
          blob.assigned_rid = revision_id("deadbeef00000000000000000000000000000000");
          return;
        }

      cvs_event_ptr ce = *blob.begin();

      revision_id new_rid;
      roster_t ros;
      shared_ptr<cset> cs(new cset());

      // even when having a parent_blob, that blob might not
      // have produced a revision id, yet.
      if (!null_id(parent_rid))
        app.db.get_roster(parent_rid, ros);

      // applies the blob to the roster. Returns the number of
      // nodes changed. In case of a 'dead' blob, we don't commit
      // anything. Such a dead blob can be created when files are
      // added on a branch in CVS.
      if (blob.build_cset(cvs, ros, *cs) == 0)
        return;

      editable_roster_base editable_ros(ros, nis);
      cs->apply_to(editable_ros);

      manifest_id child_mid;
      calculate_ident(ros, child_mid);

      revision_t rev;
      rev.made_for = made_for_database;
      rev.new_manifest = child_mid;
      rev.edges.insert(make_pair(parent_rid, cs));

      calculate_ident(rev, new_rid);

      L(FL("creating new revision %s") % new_rid);

      I(app.db.put_revision(new_rid, rev));

      {
        time_i avg_time = blob.get_avg_time();
        time_t commit_time = avg_time / 100;
        string author, changelog;

        cvs.split_authorclog(blob.authorclog, author, changelog);
        string bn = cvs.get_branchname(blob.in_branch);
        I(!bn.empty());
        app.get_project().put_standard_certs(new_rid,
                branch_name(bn),
                utf8(changelog),
                date_t::from_unix_epoch(commit_time),
                utf8(author));

        ++n_revisions;
      }

      blob.assigned_rid = new_rid;
    }
  else if (blob.get_digest().is_symbol())
    {
      I(!blob.empty());

      string sym_name = cvs.get_branchname(blob.symbol);

      if (sym_name.empty())
        L(FL("consuming blob %d: symbol for an unnamed branch")
          % bi);
      else
        L(FL("consuming blob %d: symbol %s")
          % bi % sym_name);

      blob.assigned_rid = parent_rid;
    }
  else if (blob.get_digest().is_branch_start())
    {
      string branchname = cvs.get_branchname(blob.symbol);

      if (branchname.empty())
        {
          cvs.unnamed_branch_counter++;
          branchname = (FL("UNNAMED_BRANCH_%d")
                        % cvs.unnamed_branch_counter).str();
          blob.in_branch = cvs.symbol_interner.intern(branchname);
        }
      else
        blob.in_branch = blob.symbol;

      L(FL("consuming blob %d: start of branch %s")
        % bi % branchname);
    }
  else if (blob.get_digest().is_branch_end())
    {
      // Nothing to be done at the end of a branch.
    }
  else if (blob.get_digest().is_tag())
    {
      L(FL("consuming blob %d: tag %s")
        % bi % cvs.symbol_interner.lookup(blob.symbol));

      if (!app.opts.dryrun)
        app.get_project().put_tag(parent_rid, cvs.symbol_interner.lookup(blob.symbol));
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
