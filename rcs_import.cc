// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

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
#include <string>
#include <vector>

#include <unistd.h>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>

#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graphviz.hpp>

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
  cvs_branchname branchname;

  cvs_event_branch(const cvs_event_ptr dep,
                   const cvs_branchname bn)
    : cvs_event(dep),
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
};

typedef vector<cvs_blob>::size_type cvs_blob_index;
typedef multimap<cvs_event_digest, cvs_blob_index>::iterator
  blob_index_iterator;

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

  file_path curr_file;
  cvs_path curr_file_interned;

  string base_branch;

  ticker n_versions;
  ticker n_tree_branches;

  cvs_history();
  void set_filename(string const & file,
                    file_id const & ident);

  void index_branchpoint_symbols(rcs_file const & r);

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
      I(c->time != 0);

    blob_index_iterator b = get_blob(c->get_digest(), true);
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
    changelog = ac_str.substr(i+4);
  }

  string join_authorclog(const string author, const string clog)
  {
    I(author.size() > 0);
    I(clog.size() > 0);
    return author + "|||" + clog;
  }
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

static cvs_event_ptr
process_rcs_branch(string const & begin_version,
               vector< piece > const & begin_lines,
               data const & begin_data,
               hexenc<id> const & begin_id,
               rcs_file const & r,
               database & db,
               cvs_history & cvs,
               bool dryrun)
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

      // add the commit to the cvs history
      cvs.append_event(curr_commit);
      ++cvs.n_versions;

      // make the last commit depend on the current one (which
      // comes _before_ in the CVS history).
      if (last_commit)
        last_commit->dependencies.push_back(curr_commit);

      // create tag events for all tags on this commit
      typedef multimap<string,string>::const_iterator ity;
      pair<ity,ity> range = r.admin.symbols.equal_range(curr_version);
      for (ity i = range.first; i != range.second; ++i)
        {
          if (i->first == curr_version)
           {
              L(FL("version %s -> tag %s") % curr_version % i->second);

              cvs_tag tag = cvs.tag_interner.intern(i->second);
              cvs_event_ptr event = 
                boost::static_pointer_cast<cvs_event, cvs_event_tag>(
                  shared_ptr<cvs_event_tag>(
                    new cvs_event_tag(curr_commit, tag)));

              cvs_blob_index bi = cvs.append_event(event);

              // append to the last_commit deps
              if (last_commit)
                last_commit->dependencies.push_back(event);
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
                         *next_lines, next_data, next_id, db, dryrun);
        }

      // recursively follow any branch commits coming from the branchpoint
      shared_ptr<rcs_delta> curr_delta = r.deltas.find(curr_version)->second;
      for(vector<string>::const_iterator i = curr_delta->branches.begin();
          i != curr_delta->branches.end(); ++i)
        {
          string branchname;
          data branch_data;
          hexenc<id> branch_id;
          vector< piece > branch_lines;
          bool priv = false;

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
                               branch_id, r, db, cvs, dryrun);
          if (!priv)
            L(FL("finished RCS branch %s = '%s'") % (*i) % branchname);
          else
            L(FL("finished private RCS branch %s") % (*i));

          if (first_event_in_branch)
            {
              cvs_event_ptr branch_event =
                boost::static_pointer_cast<cvs_event, cvs_event_branch>(
                  shared_ptr<cvs_event_branch>(
                    new cvs_event_branch(curr_commit, 
                      cvs.branchname_interner.intern(branchname))));

              first_event_in_branch->dependencies.push_back(branch_event);

              // FIXME: is this still needed here?
              // make sure curr_commit exists in the blob
              cvs.get_blob(curr_commit->get_digest(), false);

              // add the blob to the bucket
              cvs_blob_index bi = cvs.append_event(branch_event);

              L(FL("added branch event for file %s into branch %s")
                % cvs.path_interner.lookup(curr_commit->path)
                % branchname);

              // make the last commit depend on this branch, so
              // that comes after the new branchpoint
              if (last_commit)
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

  return curr_commit;
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

    if (!app.db.file_version_exists (fid) && !app.opts.dryrun)
      app.db.put_file(fid, file_data(dat));

    global_pieces.reset();
    global_pieces.index_deltatext(r.deltatexts.find(r.admin.head)->second,
                                  head_lines);
    process_rcs_branch(r.admin.head, head_lines, dat, id, r, app.db, cvs,
                       app.opts.dryrun);
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

          vector<string>::const_iterator j;
          for(j = curr_delta->branches.begin();
              j != curr_delta->branches.end(); ++j)
            {
              if (*j == first_entry_version)
                break;
            }

          // if the delta does not yet contain that branch, we add it
          if (j == curr_delta->branches.end())
            curr_delta->branches.push_back(first_entry_version);
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
cluster_consumer
{
  cvs_history & cvs;
  app_state & app;
  string const & branchname;
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
                   ticker & n_revs);

  void consume_blob(const cvs_blob & blob);
  void add_missing_parents(split_path const & sp, cset & cs);
  void build_cset(const cvs_blob & blob, cset & cs);
  void store_auxiliary_certs(prepared_revision const & p);
  void store_revisions();
};

template < class MyEdge, class MyColorMap >
struct blob_splitter 
  : public boost::dfs_visitor<>
{
protected:
  cvs_history & cvs;
  set< cvs_blob_index > & cycle_members;
  MyColorMap & colormap;

public:
  blob_splitter(cvs_history & c, set< cvs_blob_index > & cm,
                MyColorMap & cmap)
    : cvs(c),
      cycle_members(cm),
      colormap(cmap)
    { }

  template < class Edge, class Graph >
  void tree_edge(Edge e, Graph & g)
    {
      L(FL("blob_splitter: tree edge: %s") % e);
    }

  template < class Edge, class Graph >
  void back_edge(Edge e, Graph & g)
    {
      if (!cycle_members.empty())
        return;

      L(FL("blob_splitter: back edge: %s") % e);

      if (e.m_source == e.m_target)
        {
          // The cycle consists of only one blob - we have to solve an
          // intra blob dependency.
          cycle_members.insert(e.m_source);
        }
      else
        {
          cycle_members.insert(e.m_source);
          cycle_members.insert(e.m_target);

          cvs_blob_index ci = target(e, g);

          int limit = 1000;
          while (limit > 0)
            {
              // try to find out what blobs belong to that cycle
              pair< typename boost::graph_traits<Graph>::adjacency_iterator, typename boost::graph_traits<Graph>::adjacency_iterator > adj_vert_range = boost::adjacent_vertices(ci, g);

              typename boost::graph_traits<Graph>::adjacency_iterator ity;
              for (ity = adj_vert_range.first; ity != adj_vert_range.second; ++ity)
                {
                  typedef typename MyColorMap::value_type ColorValue;
                  typedef boost::color_traits< ColorValue > Color;

                  if (colormap[*ity] == Color::gray())
                    break;
                }

              if (cycle_members.find(*ity) != cycle_members.end())
                break;

              cycle_members.insert(*ity);
              ci = *ity;

              limit--;
            }
        }
    }
};

class revision_iterator
{
private:
	cvs_blob_index current_blob;
  cvs_history & cvs;
  cluster_consumer & cons;

public:
  revision_iterator(cvs_history & h, cluster_consumer & c)
    : current_blob(0),
      cvs(h),
      cons(c)
    {}

  revision_iterator(const revision_iterator & ri)
    : current_blob(ri.current_blob),
      cvs(ri.cvs),
      cons(ri.cons)
    {}

	revision_iterator & operator * (void)
    {
      return *this;
    };

  revision_iterator & operator = (cvs_blob_index current_blob)
    {
      L(FL("next blob number from toposort: %d") % current_blob);
      cons.consume_blob(cvs.blobs[current_blob]);
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

typedef map< cvs_blob_index, boost::default_color_type > ColorMap;
typedef boost::associative_property_map< map< cvs_blob_index, boost::default_color_type> > ColorPMap;


void
add_blob_dependency_edges(cvs_history & cvs,
                          const cvs_blob_index i,
                          Graph & g)
{
  const cvs_blob & blob = cvs.blobs[i];

  for(blob_event_iter event = blob.begin(); event != blob.end(); ++event)
    {
      for(dependency_iter dep = (*event)->dependencies.begin();
          dep != (*event)->dependencies.end(); ++dep)
        {
          blob_index_iterator k =
            cvs.get_blob((*dep)->get_digest(), false);

          for ( ; (k->second < cvs.blobs.size()) &&
                  (cvs.blobs[k->second].get_digest() == 
                                    (*dep)->get_digest()); ++k)
            {
              bool found_dep = false;

              for (dependency_iter di =
                     cvs.blobs[k->second].get_events().begin();
                   di != cvs.blobs[k->second].get_events().end(); ++ di)
                {
                  if (*di == *dep)
                    {
                      found_dep = true;
                      break;
                    }
                }

              // add the edge, if we found the dependency *and* if the
              // edge does not exist, yet.
              if (found_dep && (!boost::edge(i, k->second, g).second))
                add_edge(i, k->second, g);
            }
        }
    }
}

/*
 * single blob split points: search only for intra-blob dependencies
 * and return split points to resolve these dependencies.
 */
vector< pair<time_t, time_t> >
get_split_points(cvs_history & cvs, cvs_blob_index bi)
{
  cvs_blob & blob = cvs.blobs[bi];

  vector< pair<time_t, time_t> > result_set;

  for (blob_event_iter i = blob.begin(); i != blob.end(); ++i)
    {
      cvs_event_ptr ev = *i;

      for (dependency_iter j = ev->dependencies.begin(); j != ev->dependencies.end(); ++j)
        {
          cvs_event_ptr dep = *j;

          if (dep->get_digest() == blob.get_digest())
            {
              L(FL("event time: %d  - dep time: %d") % ev->time % dep->time);
              I(ev->time >= dep->time);
              result_set.push_back(make_pair(dep->time, ev->time));
            }
        }
    }

  return result_set;
}

void
split_blob_at(cvs_history & cvs, const cvs_blob_index bi,
              time_t split_point, Graph & g);

void
split_cycle(cvs_history & cvs, set< cvs_blob_index > const & cycle_members,
            Graph & g)
{
  cvs_blob_index blob_to_split;

  /* shortcut for intra blob dependencies */
  I(cycle_members.size() > 0);
  if (cycle_members.size() == 1)
    {
      L(FL("should split blob %d") % *cycle_members.begin());
      blob_to_split = *cycle_members.begin();

      vector< pair< time_t, time_t > > split_points =
        get_split_points(cvs, *cycle_members.begin());

      for (vector< pair< time_t, time_t > >::const_iterator i = split_points.begin();
           i != split_points.end(); ++i)
        {
          time_t split_point = i->second - ((i->second - i->first) / 2);
          L(FL("splitting blob between %d and %d (at %d)") % i->first % i->second % split_point);

          split_blob_at(cvs, *cycle_members.begin(), split_point, g);
        }
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

      cc = 0;
      for (cc = 0; cc < mymap.size(); ++cc)
        {
          cvs_blob_index prev_blob = mymap[(cc > 0 ? cc - 1 : mymap.size() - 1)];
          cvs_blob_index this_blob = mymap[cc];
          cvs_blob_index next_blob = mymap[(cc < mymap.size() - 1 ? cc + 1 : 0)];

          L(FL("  testing deps %d -> %d -> %d") % prev_blob % this_blob % next_blob);

          time_t prev_min_time = 0;
          for (blob_event_iter ii = cvs.blobs[prev_blob].begin(); ii != cvs.blobs[prev_blob].end(); ++ii)
            {
              cvs_event_ptr ev = *ii;

              for (dependency_iter j = ev->dependencies.begin(); j != ev->dependencies.end(); ++j)
                {
                  cvs_event_ptr dep = *j;

                  if (dep->get_digest() == cvs.blobs[this_blob].get_digest())
                    {
                      if ((prev_min_time == 0) || (dep->time < prev_min_time))
                        prev_min_time = dep->time;
                    }
                }
            }
          L(FL("      prev min time: %d") % prev_min_time);

          time_t next_max_time = 0;
          for (blob_event_iter ii = cvs.blobs[this_blob].begin(); ii != cvs.blobs[this_blob].end(); ++ii)
            {
              cvs_event_ptr ev = *ii;

              for (dependency_iter j = ev->dependencies.begin(); j != ev->dependencies.end(); ++j)
                {
                  cvs_event_ptr dep = *j;

                  if (dep->get_digest() == cvs.blobs[next_blob].get_digest())
                    {
                      if (dep->time > next_max_time)
                        next_max_time = dep->time;
                    }
                }
            }
          L(FL("      next max time: %d") % next_max_time);

          // We assume we have found both dependencies
          I(prev_min_time > 0);
          I(next_max_time > 0);

          if (prev_min_time < next_max_time)
            {
              L(FL("      this blob is a candidate for splitting..."));
              L(FL("      for now, we just split that one!"));
              split_blob_at(cvs, this_blob, next_max_time, g);
              break;
            }
        }
    }
}

void
split_blob_at(cvs_history & cvs, const cvs_blob_index bi,
              time_t split_point, Graph & g)
{
  vector< cvs_event_ptr > blob_events(cvs.blobs[bi].get_events());

  // sort the blob events by timestamp
  sort(blob_events.begin(), blob_events.end());

  // add a blob
  cvs_event_digest d = cvs.blobs[bi].get_digest();
  cvs_blob_index new_bi = cvs.add_blob(d)->second;

  // reassign all events and split into the two blobs
  cvs.blobs[bi].get_events().clear();
  I(!blob_events.empty());
  I(cvs.blobs[bi].empty());
  I(cvs.blobs[new_bi].empty());

  for (blob_event_iter i = blob_events.begin(); i != blob_events.end(); ++i)
    if ((*i)->time >= split_point)
      cvs.blobs[new_bi].push_back(*i);
    else
      cvs.blobs[bi].push_back(*i);


  {
    // in edges, blobs which depend on this one blob we should split
    pair< boost::graph_traits<Graph>::in_edge_iterator,
          boost::graph_traits<Graph>::in_edge_iterator > range;

    range = in_edges(bi, g);

    vector< cvs_blob_index > in_deps_from;

    // get all blobs with dependencies to the blob which has been split
    for (boost::graph_traits<Graph>::in_edge_iterator ity = range.first;
         ity != range.second; ++ity)
      {
        L(FL("removing in edge %s") % *ity);
        in_deps_from.push_back(ity->m_source);
        I(ity->m_target == bi);
      }

    // remove all those edges
    for (vector< cvs_blob_index >::const_iterator ity = in_deps_from.begin();
         ity != in_deps_from.end(); ++ity)
          remove_edge(*ity, bi, const_cast<Graph &>(g));

    // now check each in_deps_from blob and add proper edges to the
    // newly splitted blobs
    for (vector< cvs_blob_index >::const_iterator ity = in_deps_from.begin();
         ity != in_deps_from.end(); ++ity)
      {
        cvs_blob & other_blob = cvs.blobs[*ity];

        for (vector< cvs_event_ptr >::const_iterator j = 
              other_blob.get_events().begin();
              j != other_blob.get_events().end(); ++j)
          {
            for (dependency_iter ob_dep = (*j)->dependencies.begin();
                 ob_dep != (*j)->dependencies.end(); ++ob_dep)

              if ((*ob_dep)->get_digest() == d)
              {
                if ((*ob_dep)->time >= split_point)
                {
                  // L(FL("adding new edge %d -> %d") % *ity % new_bi);
                  if (!boost::edge(*ity, new_bi, g).second)
                    add_edge(*ity, new_bi, const_cast<Graph &>(g));
                }
                else
                {
                  // L(FL("keeping edge %d -> %d") % *ity % new_bi);
                  if (!boost::edge(*ity, bi, g).second)
                    add_edge(*ity, bi, const_cast<Graph &>(g));
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

    range = out_edges(bi, g);

    // remove all existing out edges
    for (boost::graph_traits<Graph>::out_edge_iterator ity = range.first;
         ity != range.second; ++ity)
      {
        L(FL("removing out edge %s") % *ity);
        remove_edge(ity->m_source, ity->m_target, const_cast<Graph &>(g));
      }

    add_blob_dependency_edges(cvs, bi, const_cast<Graph &>(g));
    add_blob_dependency_edges(cvs, new_bi, const_cast<Graph &>(g));
  }
}

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

          if (b.begin() != b.end())
            {
              string author, clog;
              const shared_ptr< cvs_commit > ce =
                boost::static_pointer_cast<cvs_commit, cvs_event>(*b.begin());

              cvs.split_authorclog(ce->authorclog, author, clog);
              label += author + "\\n";

              // poor man's escape...
              for (unsigned int i = 0; i < clog.length(); ++i)
                if (clog[i] < 32)
                  clog[i] = ' ';
              label += "\\\"" + clog + "\\\"\\n";

              label += "\\n";

              for (blob_event_iter i = b.begin(); i != b.end(); i++)
                {
                  const shared_ptr< cvs_commit > ce =
                    boost::static_pointer_cast<cvs_commit, cvs_event>(*i);

                  label += cvs.path_interner.lookup(ce->path);
                  label += "@";
                  label += cvs.rcs_version_interner.lookup(ce->rcs_version);
                  label += "\\n";
                }
            }
          else
            label += "-- empty --";
        }
      else if (b.get_digest().is_branch())
        {
          L(FL("blob %d: branch") % v);

          label = (FL("blob %d: branch: ") % v).str();

          const shared_ptr< cvs_event_branch > cb =
            boost::static_pointer_cast<cvs_event_branch, cvs_event>(*b.begin());

          label += cvs.branchname_interner.lookup(cb->branchname);
        }
      else if (b.get_digest().is_tag())
        {
          L(FL("blob %d: tag") % v);

          label = (FL("blob %d: tag: ") % v).str();

          const shared_ptr< cvs_event_tag > cb =
            boost::static_pointer_cast<cvs_event_tag, cvs_event>(*b.begin());

          label += cvs.tag_interner.lookup(cb->tag);
        }
      else
        {
          label = (FL("blob %d: unknow type") % v).str();
        }

      out << "[label=\"" << label << "\"]";
    }
};

//
// After stuffing all cvs_events into blobs of events with the same
// author and changelog, we have to make sure their dependencies are
// respected.
//
void
resolve_blob_dependencies(cvs_history &cvs,
                          app_state & app,
                          string const & branchname,
                          ticker & n_revs)
{
  L(FL("Breaking dependency cycles (%d blobs)") % cvs.blobs.size());

  int step_no = 1;
  std::ofstream viz_file;
  blob_label_writer blw(cvs);

  Graph g(cvs.blobs.size());

  // fill the graph with all blob dependencies as edges between
  // the blobs (vertices).
  for (cvs_blob_index i = 0; i < cvs.blobs.size(); ++i)
    add_blob_dependency_edges(cvs, i, g);

  ColorMap colormap;
  ColorPMap colorpmap(colormap);

  // check for cycles
  set< cvs_blob_index > cycle_members;
  blob_splitter< Edge, ColorMap > vis(cvs, cycle_members, colormap);

  while (1)
  {
    if (global_sanity.debug)
      {
        viz_file.open((FL("cvs_graph.%d.viz") % step_no).str().c_str());
        boost::write_graphviz(viz_file, g, blw);
        viz_file.close();
        step_no++;
      }

    cycle_members.clear();
    depth_first_search(g, vis, colorpmap);

    // If we have a cycle, go split it. Otherwise we don't have any
    // cycles left and can proceed.
    if (!cycle_members.empty())
      split_cycle(cvs, cycle_members, g);
    else
      break;
  };

  // start the topological sort, which calls our revision
  // iterator to insert the revisions into our database. 
  cluster_consumer cons(cvs, app, branchname, n_revs);
  revision_iterator ri(cvs, cons);

  L(FL("starting toposort the blobs of branch %s") % branchname);
  topological_sort(g, ri);

  // finally store the revisions
  // (ms) why is this an extra step? Is it faster?
  cons.store_revisions();
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
  cvs.base_branch = app.opts.branchname();

  // add the trunk branch name
  cvs_branchname bn = cvs.branchname_interner.intern(cvs.base_branch);


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
    resolve_blob_dependencies(cvs, app, cvs.base_branch, n_revs);
    guard.commit();
  }

  return;
}

cluster_consumer::cluster_consumer(cvs_history & cvs,
                                   app_state & app,
                                   string const & branchname,
                                   ticker & n_revs)
  : cvs(cvs),
    app(app),
    branchname(branchname),
    n_revisions(n_revs),
    editable_ros(ros, nis)
{
#if 0
  if (!null_id(branch.parent_rid))
    {
      L(FL("starting cluster for branch %s from revision")
           % branchname);

      // ??? FIXME: parent_rid = branch.parent_rid;
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
#endif
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
}


void
cluster_consumer::store_revisions()
{
  for (vector<prepared_revision>::const_iterator i = preps.begin();
       i != preps.end(); ++i)
    {
      if (!app.db.revision_exists(i->rid))
        {
          if (!app.opts.dryrun)
            {
              data tmp;
              write_revision(*(i->rev), tmp);
              app.db.put_revision(i->rid, *(i->rev));
              store_auxiliary_certs(*i);
              ++n_revisions;
            }
        }
    }
}

void
cluster_consumer::store_auxiliary_certs(prepared_revision const & p)
{
  string author, changelog;

  cvs.split_authorclog(p.authorclog, author, changelog);
  packet_db_writer dbw(app);
  app.get_project().put_standard_certs(p.rid,
                                       branch_name(branchname),
                                       utf8(changelog),
                                       date_t::from_unix_epoch(p.time),
                                       utf8(author),
                                       dbw);
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

      shared_ptr<cvs_commit> ce =
        boost::static_pointer_cast<cvs_commit, cvs_event>(*blob.begin());

      if (ce->alive)
        {
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
    }
  else if (blob.get_digest().is_branch())
    {
      if (!blob.empty())
        {
          string child_rid_str;
          dump(child_rid, child_rid_str);

          shared_ptr<cvs_event_branch> cbe =
            boost::static_pointer_cast<cvs_event_branch, cvs_event>(
              *blob.begin());
        }
    }
  else if (blob.get_digest().is_tag())
    {
      if (!blob.empty())
        {
          shared_ptr<cvs_event_tag> cte =
            boost::static_pointer_cast<cvs_event_tag, cvs_event>(
              *blob.begin());

          // FIXME: before, I've only inserted into cvs.resolved_tags,
          //        but I should just add the cert here...
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
