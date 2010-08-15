// Copyright (C) 2010 Stephen Leake <stephen_leake@stephe-leake.org>
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
#include <deque>
#include <fstream>
#include <iterator>
#include <list>
#include <numeric>
#include <set>
#include <sstream>
#include "vector.hh"

#include <string.h>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>

#include <botan/botan.h>
#include <botan/rsa.h>
#include <botan/pem.h>
#include <botan/look_pk.h>
#include "lazy_rng.hh"

#include <sqlite3.h>

#include "lexical_cast.hh"

#include "app_state.hh"
#include "cert.hh"
#include "project.hh"
#include "cleanup.hh"
#include "constants.hh"
#include "dates.hh"
#include "database.hh"
#include "hash_map.hh"
#include "keys.hh"
#include "platform-wrapped.hh"
#include "revision.hh"
#include "safe_map.hh"
#include "sanity.hh"
#include "migration.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "ui.hh" // tickers
#include "vocab.hh"
#include "vocab_cast.hh"
#include "xdelta.hh"
#include "epoch.hh"
#include "graph.hh"
#include "roster.hh"
#include "roster_delta.hh"
#include "rev_height.hh"
#include "vocab_hash.hh"
#include "globish.hh"
#include "work.hh"
#include "lua_hooks.hh"
#include "outdated_indicator.hh"
#include "lru_writeback_cache.hh"

// defined in schema.c, generated from schema.sql:
extern char const schema_constant[];

// this file defines a public, typed interface to the database.
// the database class encapsulates all knowledge about sqlite,
// the schema, and all SQL statements used to access the schema.
//
// see file schema.sql for the text of the schema.

using std::deque;
using std::istream;
using std::make_pair;
using std::map;
using std::multimap;
using std::ostream;
using std::pair;
using std::remove_if;
using std::set;
using std::sort;
using std::string;
using std::vector;
using std::accumulate;

using boost::shared_ptr;
using boost::shared_dynamic_cast;
using boost::lexical_cast;
using boost::get;
using boost::tuple;
using boost::lexical_cast;

using Botan::PK_Encryptor;
using Botan::PK_Verifier;
using Botan::SecureVector;
using Botan::X509_PublicKey;
using Botan::RSA_PublicKey;
using Botan::get_pk_encryptor;

int const one_row = 1;
int const one_col = 1;
int const any_rows = -1;
int const any_cols = -1;

namespace
{
  struct query_param
  {
    enum arg_type { text, blob };
    arg_type type;
    string data;
  };

  query_param
  text(string const & txt)
  {
    MM(txt);
    for (string::const_iterator i = txt.begin();
         i != txt.end(); ++i)
      {
        I(*i >= 10 && *i < 127);
      }
    query_param q = {
      query_param::text,
      txt,
    };
    return q;
  }

  query_param
  blob(string const & blb)
  {
    query_param q = {
      query_param::blob,
      blb,
    };
    return q;
  }

  struct query
  {
    explicit query(string const & cmd)
      : sql_cmd(cmd)
    {}

    query()
    {}

    query & operator %(query_param const & qp)
    {
      args.push_back(qp);
      return *this;
    }

    vector<query_param> args;
    string sql_cmd;
  };

  typedef vector< vector<string> > results;

  struct statement
  {
    statement() : count(0), stmt(0, sqlite3_finalize) {}
    int count;
    cleanup_ptr<sqlite3_stmt*, int> stmt;
  };

  struct roster_size_estimator
  {
    unsigned long operator() (cached_roster const & cr)
    {
      I(cr.first);
      I(cr.second);
      // do estimate using a totally made up multiplier, probably wildly off
      return (cr.first->all_nodes().size()
              * constants::db_estimated_roster_node_sz);
    }
  };

  struct datasz
  {
    unsigned long operator()(data const & t) { return t().size(); }
  };

  enum open_mode { normal_mode = 0,
                   schema_bypass_mode,
                   format_bypass_mode,
                   cache_bypass_mode };

  typedef hashmap::hash_map<revision_id, set<revision_id> > parent_id_map;
  typedef hashmap::hash_map<revision_id, rev_height> height_map;

  typedef hashmap::hash_map<key_id,
                            pair<shared_ptr<Botan::PK_Verifier>,
                                 shared_ptr<Botan::RSA_PublicKey> >
                            > verifier_cache;

} // anonymous namespace

class database_impl
{
  friend class database;

  // for scoped_ptr's sake
public:
  explicit database_impl(system_path const & f, db_type t,
                         system_path const & roster_cache_performance_log);
  ~database_impl();

private:

  //
  // --== Opening the database and schema checking ==--
  //
  system_path filename;
  db_type type;
  struct sqlite3 * __sql;

  void install_functions();
  struct sqlite3 * sql(enum open_mode mode = normal_mode);

  void check_filename();
  void check_db_exists();
  void check_db_nonexistent();
  void open();
  void close();
  void check_format();
  void check_caches();

  bool table_has_data(string const & name);

  //
  // --== Basic SQL interface and statement caching ==--
  //
  map<string, statement> statement_cache;

  void fetch(results & res,
             int const want_cols, int const want_rows,
             query const & q);
  void execute(query const & q);

  bool table_has_entry(id const & key, string const & column,
                       string const & table);

  //
  // --== Generic database metadata gathering ==--
  //
  string count(string const & table);
  string space(string const & table,
                    string const & concatenated_columns,
                    u64 & total);
  unsigned int page_size();
  unsigned int cache_size();

  //
  // --== Transactions ==--
  //
  int transaction_level;
  bool transaction_exclusive;
  void begin_transaction(bool exclusive);
  void commit_transaction();
  void rollback_transaction();
  friend class conditional_transaction_guard;

  struct roster_writeback_manager
  {
    database_impl & imp;
    roster_writeback_manager(database_impl & imp) : imp(imp) {}
    void writeout(revision_id const &, cached_roster const &);
  };
  LRUWritebackCache<revision_id, cached_roster,
                    roster_size_estimator, roster_writeback_manager>
    roster_cache;

  bool have_delayed_file(file_id const & id);
  void load_delayed_file(file_id const & id, file_data & dat);
  void cancel_delayed_file(file_id const & id);
  void drop_or_cancel_file(file_id const & id);
  void schedule_delayed_file(file_id const & id, file_data const & dat);

  map<file_id, file_data> delayed_files;
  size_t delayed_writes_size;

  void flush_delayed_writes();
  void clear_delayed_writes();
  void write_delayed_file(file_id const & new_id,
                          file_data const & dat);

  void write_delayed_roster(revision_id const & new_id,
                            roster_t const & roster,
                            marking_map const & marking);

  //
  // --== Reading/writing delta-compressed objects ==--
  //

  // "do we have any entry for 'ident' that is a base version"
  bool roster_base_stored(revision_id const & ident);
  bool roster_base_available(revision_id const & ident);

  // "do we have any entry for 'ident' that is a delta"
  bool delta_exists(file_id const & ident,
                    file_id const & base,
                    string const & table);

  bool file_or_manifest_base_exists(id const & ident,
                                    std::string const & table);

  void get_file_or_manifest_base_unchecked(id const & new_id,
                                           data & dat,
                                           string const & table);
  void get_file_or_manifest_delta_unchecked(id const & ident,
                                            id const & base,
                                            delta & del,
                                            string const & table);
  void get_roster_base(revision_id const & ident,
                       roster_t & roster, marking_map & marking);
  void get_roster_delta(id const & ident,
                        id const & base,
                        roster_delta & del);

  friend struct file_and_manifest_reconstruction_graph;
  friend struct roster_reconstruction_graph;

  LRUWritebackCache<id, data, datasz> vcache;

  void get_version(id const & ident,
                   data & dat,
                   string const & data_table,
                   string const & delta_table);

  void drop(id const & base,
            string const & table);
  void put_file_delta(file_id const & ident,
                      file_id const & base,
                      file_delta const & del);

  void put_roster_delta(revision_id const & ident,
                        revision_id const & base,
                        roster_delta const & del);

  //
  // --== The ancestry graph ==--
  //
  void get_ids(string const & table, set<id> & ids);

  //
  // --== Rosters ==--
  //
  struct extractor;
  struct file_content_extractor;
  struct markings_extractor;
  void extract_from_deltas(revision_id const & id, extractor & x);

  height_map height_cache;
  parent_id_map parent_cache;

  //
  // --== Keys ==--
  //
  void get_keys(string const & table, vector<key_name> & keys);

  // cache of verifiers for public keys
  verifier_cache verifiers;

  //
  // --== Certs ==--
  //
  // note: this section is ridiculous. please do something about it.
  bool cert_exists(cert const & t,
                   string const & table);
  void put_cert(cert const & t, string const & table);
  void results_to_certs(results const & res,
                        vector<cert> & certs);
  void results_to_certs(results const & res,
                        vector<pair<id, cert> > & certs);
  void oldstyle_results_to_certs(results const & res,
                                 vector<cert> & certs);

  void get_certs(vector<cert> & certs,
                 string const & table);

  void get_oldstyle_certs(id const & ident,
                          vector<cert> & certs,
                          string const & table);

  void get_certs(id const & ident,
                 vector<cert> & certs,
                 string const & table);

  void get_certs(cert_name const & name,
                 vector<cert> & certs,
                 string const & table);

  void get_oldstyle_certs(cert_name const & name,
                          vector<cert> & certs,
                          string const & table);

  void get_certs(id const & ident,
                 cert_name const & name,
                 vector<cert> & certs,
                 string const & table);

  void get_certs(id const & ident,
                 cert_name const & name,
                 cert_value const & val,
                 vector<cert> & certs,
                 string const & table);

  void get_certs(cert_name const & name,
                 cert_value const & val,
                 vector<pair<id, cert> > & certs,
                 string const & table);

  outdated_indicator_factory cert_stamper;

  void add_prefix_matching_constraint(string const & colname,
                                      string const & prefix,
                                      query & q);
};

#ifdef SUPPORT_SQLITE_BEFORE_3003014
// SQLite versions up to and including 3.3.12 didn't have the hex() function
void
sqlite3_hex_fn(sqlite3_context *f, int nargs, sqlite3_value **args)
{
  if (nargs != 1)
    {
      sqlite3_result_error(f, "need exactly 1 arg to hex()", -1);
      return;
    }
  string decoded;

  // This operation may throw (un)recoverable_failure.  We must intercept that
  // and turn it into a call to sqlite3_result_error, or rollback will fail.
  try
    {
      decoded = encode_hexenc(reinterpret_cast<char const *>(
        sqlite3_value_text(args[0])), origin::database);
    }
  catch (recoverable_failure & e)
    {
      sqlite3_result_error(f, e.what(), -1);
      return;
    }
  catch (unrecoverable_failure & e)
    {
      sqlite3_result_error(f, e.what(), -1);
      return;
    }

  sqlite3_result_blob(f, decoded.data(), decoded.size(), SQLITE_TRANSIENT);
}
#endif

database_impl::database_impl(system_path const & f, db_type t,
                             system_path const & roster_cache_performance_log) :
  filename(f),
  type(t),
  __sql(NULL),
  transaction_level(0),
  roster_cache(constants::db_roster_cache_sz,
               constants::db_roster_cache_min_count,
               roster_writeback_manager(*this),
               roster_cache_performance_log.as_external()),
  delayed_writes_size(0),
  vcache(constants::db_version_cache_sz, 1)
{}

database_impl::~database_impl()
{
  L(FL("statement cache statistics"));
  L(FL("prepared %d statements") % statement_cache.size());

  for (map<string, statement>::const_iterator i = statement_cache.begin();
       i != statement_cache.end(); ++i)
    L(FL("%d executions of %s") % i->second.count % i->first);
  // trigger destructors to finalize cached statements
  statement_cache.clear();

  if (__sql)
    close();
}

database_cache database::dbcache;

database::database(app_state & app)
  : opts(app.opts), lua(app.lua)
{
  init();
}

database::database(options const & o, lua_hooks & l)
  : opts(o), lua(l)
{
  init();
}

void
database::init()
{
  database_path_helper helper(lua);
  system_path dbpath;
  helper.get_database_path(opts, dbpath);

  // FIXME: for all :memory: databases an empty path is returned above, thus
  // all requests for a :memory: database point to the same database
  // implementation. This means we cannot use two different memory databases
  // within the same monotone process
  if (dbcache.find(dbpath) == dbcache.end())
    {
      L(FL("creating new database_impl instance for %s") % dbpath);
      dbcache.insert(make_pair(dbpath, boost::shared_ptr<database_impl>(
        new database_impl(dbpath, opts.dbname_type, opts.roster_cache_performance_log)
      )));
    }

  imp = dbcache[dbpath];
}

database::~database()
{}

void
database::reset_cache()
{
  dbcache.clear();
}

system_path
database::get_filename()
{
  return imp->filename;
}

bool
database::is_dbfile(any_path const & file)
{
  if (imp->type == memory_db)
    return false;
  system_path fn(file); // canonicalize
  bool same = (imp->filename == fn);
  if (same)
    L(FL("'%s' is the database file") % file);
  return same;
}

bool
database::database_specified()
{
  return imp->type == memory_db || !imp->filename.empty();
}

void
database::create_if_not_exists()
{
  imp->check_filename();
  if (!file_exists(imp->filename))
    {
      P(F("initializing new database '%s'") % imp->filename);
      initialize();
    }
}

void
database::check_is_not_rosterified()
{
  E(!imp->table_has_data("rosters"), origin::user,
    F("this database already contains rosters"));
}

bool
database_impl::table_has_data(string const & name)
{
  results res;
  fetch(res, one_col, any_rows, query("SELECT 1 FROM " + name + " LIMIT 1"));
  return !res.empty();
}

void
database_impl::check_format()
{
  if (table_has_data("manifests"))
    {
      // The rosters and heights tables should be empty.
      I(!table_has_data("rosters") && !table_has_data("heights"));

      // they need to either changesetify or rosterify.  which?
      if (table_has_data("revisions"))
        E(false, origin::no_fault,
          F("database %s contains old-style revisions\n"
            "if you are a project leader or doing local testing:\n"
            "  see the file UPGRADE for instructions on upgrading.\n"
            "if you are not a project leader:\n"
            "  wait for a leader to migrate project data, and then\n"
            "  pull into a fresh database.\n"
            "sorry about the inconvenience.")
          % filename);
      else
        E(false, origin::no_fault,
          F("database %s contains manifests but no revisions\n"
            "this is a very old database; it needs to be upgraded\n"
            "please see README.changesets for details")
          % filename);
    }
}

void
database_impl::check_caches()
{
  if (table_has_data("revisions"))
    {
      E(table_has_data("rosters") && table_has_data("heights"),
        origin::no_fault,
        F("database %s lacks some cached data\n"
          "run '%s db regenerate_caches' to restore use of this database")
        % filename % prog_name);
    }
}

static void
sqlite3_gunzip_fn(sqlite3_context *f, int nargs, sqlite3_value ** args)
{
  if (nargs != 1)
    {
      sqlite3_result_error(f, "need exactly 1 arg to gunzip()", -1);
      return;
    }
  data unpacked;
  const char *val = (const char*) sqlite3_value_blob(args[0]);
  int bytes = sqlite3_value_bytes(args[0]);
  decode_gzip(gzip<data>(string(val,val+bytes), origin::database), unpacked);
  sqlite3_result_blob(f, unpacked().c_str(), unpacked().size(), SQLITE_TRANSIENT);
}

struct sqlite3 *
database_impl::sql(enum open_mode mode)
{
  if (! __sql)
    {
      if (type == memory_db)
        {
          open();

          sqlite3_exec(__sql, schema_constant, NULL, NULL, NULL);
          assert_sqlite3_ok(__sql);

          sqlite3_exec(__sql, (FL("PRAGMA user_version = %u;")
                               % mtn_creator_code).str().c_str(), NULL, NULL, NULL);
          assert_sqlite3_ok(__sql);
        }
      else
        {
          check_filename();
          check_db_exists();
          open();

          if (mode != schema_bypass_mode)
            {
              check_sql_schema(__sql, filename);

              if (mode != format_bypass_mode)
                {
                  check_format();

                  if (mode != cache_bypass_mode)
                    check_caches();
                }
            }
        }
      install_functions();
    }
  else
    I(mode == normal_mode);

  return __sql;
}

void
database::initialize()
{
  imp->check_filename();
  imp->check_db_nonexistent();
  imp->open();

  sqlite3 *sql = imp->__sql;

  sqlite3_exec(sql, schema_constant, NULL, NULL, NULL);
  assert_sqlite3_ok(sql);

  sqlite3_exec(sql, (FL("PRAGMA user_version = %u;")
                     % mtn_creator_code).str().c_str(), NULL, NULL, NULL);
  assert_sqlite3_ok(sql);

  // make sure what we wanted is what we got
  check_sql_schema(sql, imp->filename);

  imp->close();
}

struct
dump_request
{
  dump_request() : sql(), out() {};
  struct sqlite3 *sql;
  ostream *out;
};

static void
dump_row(ostream &out, sqlite3_stmt *stmt, string const& table_name)
{
  out << FL("INSERT INTO %s VALUES(") % table_name;
  unsigned n = sqlite3_data_count(stmt);
  for (unsigned i = 0; i < n; ++i)
    {
      if (i != 0)
        out << ',';

      if (sqlite3_column_type(stmt, i) == SQLITE_BLOB)
        {
          out << "X'";
          const char *val = (const char*) sqlite3_column_blob(stmt, i);
          int bytes = sqlite3_column_bytes(stmt, i);
          out << encode_hexenc(string(val,val+bytes), origin::internal);
          out << '\'';
        }
      else
        {
          const unsigned char *val = sqlite3_column_text(stmt, i);
          if (val == NULL)
            out << "NULL";
          else
            {
              out << '\'';
              for (const unsigned char *cp = val; *cp; ++cp)
                {
                  if (*cp == '\'')
                    out << "''";
                  else
                    out << *cp;
                }
              out << '\'';
            }
        }
    }
  out << ");\n";
}

static int
dump_table_cb(void *data, int n, char **vals, char **cols)
{
  dump_request *dump = reinterpret_cast<dump_request *>(data);
  I(dump != NULL);
  I(dump->sql != NULL);
  I(vals != NULL);
  I(vals[0] != NULL);
  I(vals[1] != NULL);
  I(vals[2] != NULL);
  I(n == 3);
  I(string(vals[1]) == "table");
  *(dump->out) << vals[2] << ";\n";
  string table_name(vals[0]);
  string query = "SELECT * FROM " + table_name;
  sqlite3_stmt *stmt = 0;
  sqlite3_prepare_v2(dump->sql, query.c_str(), -1, &stmt, NULL);
  assert_sqlite3_ok(dump->sql);

  int stepresult = SQLITE_DONE;
  do
    {
      stepresult = sqlite3_step(stmt);
      I(stepresult == SQLITE_DONE || stepresult == SQLITE_ROW);
      if (stepresult == SQLITE_ROW)
        dump_row(*(dump->out), stmt, table_name);
    }
  while (stepresult == SQLITE_ROW);

  sqlite3_finalize(stmt);
  assert_sqlite3_ok(dump->sql);
  return 0;
}

static int
dump_index_cb(void *data, int n, char **vals, char **cols)
{
  dump_request *dump = reinterpret_cast<dump_request *>(data);
  I(dump != NULL);
  I(dump->sql != NULL);
  I(vals != NULL);
  I(vals[0] != NULL);
  I(vals[1] != NULL);
  I(vals[2] != NULL);
  I(n == 3);
  I(string(vals[1]) == "index");
  *(dump->out) << vals[2] << ";\n";
  return 0;
}

static int
dump_user_version_cb(void *data, int n, char **vals, char **cols)
{
  dump_request *dump = reinterpret_cast<dump_request *>(data);
  I(dump != NULL);
  I(dump->sql != NULL);
  I(vals != NULL);
  I(vals[0] != NULL);
  I(n == 1);
  *(dump->out) << "PRAGMA user_version = " << vals[0] << ";\n";
  return 0;
}

void
database::dump(ostream & out)
{
  ensure_open_for_maintenance();

  {
    transaction_guard guard(*this);
    dump_request req;
    req.out = &out;
    req.sql = imp->sql();
    out << "BEGIN EXCLUSIVE;\n";
    int res;
    res = sqlite3_exec(req.sql,
                          "SELECT name, type, sql FROM sqlite_master "
                          "WHERE type='table' AND sql NOT NULL "
                          "AND name not like 'sqlite_stat%' "
                          "ORDER BY name",
                          dump_table_cb, &req, NULL);
    assert_sqlite3_ok(req.sql);
    res = sqlite3_exec(req.sql,
                          "SELECT name, type, sql FROM sqlite_master "
                          "WHERE type='index' AND sql NOT NULL "
                          "ORDER BY name",
                          dump_index_cb, &req, NULL);
    assert_sqlite3_ok(req.sql);
    res = sqlite3_exec(req.sql,
                       "PRAGMA user_version;",
                       dump_user_version_cb, &req, NULL);
    assert_sqlite3_ok(req.sql);
    out << "COMMIT;\n";
    guard.commit();
  }
}

void
database::load(istream & in)
{
  string line;
  string sql_stmt;

  imp->check_filename();
  imp->check_db_nonexistent();
  imp->open();

  sqlite3 * sql = imp->__sql;

  // the page size can only be set before any other commands have been executed
  sqlite3_exec(sql, "PRAGMA page_size=8192", NULL, NULL, NULL);
  assert_sqlite3_ok(sql);

  while(in)
    {
      getline(in, line, ';');
      sql_stmt += line + ';';

      if (sqlite3_complete(sql_stmt.c_str()))
        {
          sqlite3_exec(sql, sql_stmt.c_str(), NULL, NULL, NULL);
          assert_sqlite3_ok(sql);
          sql_stmt.clear();
        }
    }

  assert_sqlite3_ok(sql);
}


void
database::debug(string const & sql, ostream & out)
{
  ensure_open_for_maintenance();

  results res;
  imp->fetch(res, any_cols, any_rows, query(sql));
  out << '\'' << sql << "' -> " << res.size() << " rows\n\n";
  for (size_t i = 0; i < res.size(); ++i)
    {
      for (size_t j = 0; j < res[i].size(); ++j)
        {
          if (j != 0)
            out << " | ";
          out << res[i][j];
        }
      out << '\n';
    }
}

// Subroutine of info().  This compares strings that might either be numbers
// or error messages surrounded by square brackets.  We want the longest
// number, even if there's an error message that's longer than that.
static bool longest_number(string a, string b)
{
  if(a.length() > 0 && a[0] == '[')
    return true;  // b is longer
  if(b.length() > 0 && b[0] == '[')
    return false; // a is longer

  return a.length() < b.length();
}

// Subroutine of info() and some things it calls.
// Given an informative_failure which is believed to represent an SQLite
// error, either return a string version of the error message (if it was an
// SQLite error) or rethrow the execption (if it wasn't).
static string
format_sqlite_error_for_info(recoverable_failure const & e)
{
  string err(e.what());
  string prefix = _("error: ");
  prefix.append(_("sqlite error: "));
  if (err.find(prefix) != 0)
    throw;

  err.replace(0, prefix.length(), "[");
  string::size_type nl = err.find('\n');
  if (nl != string::npos)
    err.erase(nl);

  err.append("]");
  return err;
}

// Subroutine of info().  Pretty-print the database's "creator code", which
// is a 32-bit unsigned number that we interpret as a four-character ASCII
// string, provided that all four characters are graphic.  (On disk, it's
// stored in the "user version" field of the database.)
static string
format_creator_code(u32 code)
{
  char buf[5];
  string result;

  if (code == 0)
    return _("not set");

  buf[4] = '\0';
  buf[3] = ((code & 0x000000ff) >>  0);
  buf[2] = ((code & 0x0000ff00) >>  8);
  buf[1] = ((code & 0x00ff0000) >> 16);
  buf[0] = ((code & 0xff000000) >> 24);

  if (isgraph(buf[0]) && isgraph(buf[1]) && isgraph(buf[2]) && isgraph(buf[3]))
    result = (FL("%s (0x%08x)") % buf % code).str();
  else
    result = (FL("0x%08x") % code).str();
  if (code != mtn_creator_code)
    result += _(" (not a monotone database)");
  return result;
}


void
database::info(ostream & out, bool analyze)
{
  // don't check the schema
  ensure_open_for_maintenance();

  // do a dummy query to confirm that the database file is an sqlite3
  // database.  (this doesn't happen on open() because sqlite postpones the
  // actual file open until the first access.  we can't piggyback it on the
  // query of the user version because there's a bug in sqlite 3.3.10:
  // the routine that reads meta-values from the database header does not
  // check the file format.  reported as sqlite bug #2182.)
  sqlite3_exec(imp->__sql, "SELECT 1 FROM sqlite_master LIMIT 0", 0, 0, 0);
  assert_sqlite3_ok(imp->__sql);

  u32 ccode;
  {
    results res;
    imp->fetch(res, one_col, one_row, query("PRAGMA user_version"));
    I(res.size() == 1);
    ccode = lexical_cast<u32>(res[0][0]);
  }

  vector<string> counts;
  counts.push_back(imp->count("rosters"));
  counts.push_back(imp->count("roster_deltas"));
  counts.push_back(imp->count("files"));
  counts.push_back(imp->count("file_deltas"));
  counts.push_back(imp->count("revisions"));
  counts.push_back(imp->count("revision_ancestry"));
  counts.push_back(imp->count("revision_certs"));

  {
    results res;
    try
      {
        imp->fetch(res, one_col, any_rows,
              query("SELECT node FROM next_roster_node_number"));
        if (res.empty())
          counts.push_back("0");
        else
          {
            I(res.size() == 1);
            u64 n = lexical_cast<u64>(res[0][0]) - 1;
            counts.push_back((F("%u") % n).str());
          }
      }
    catch (recoverable_failure const & e)
      {
        counts.push_back(format_sqlite_error_for_info(e));
      }
  }

  vector<string> bytes;
  {
    u64 total = 0;
    bytes.push_back(imp->space("rosters",
                          "length(id) + length(checksum) + length(data)",
                          total));
    bytes.push_back(imp->space("roster_deltas",
                          "length(id) + length(checksum)"
                          "+ length(base) + length(delta)", total));
    bytes.push_back(imp->space("files", "length(id) + length(data)", total));
    bytes.push_back(imp->space("file_deltas",
                          "length(id) + length(base) + length(delta)", total));
    bytes.push_back(imp->space("revisions", "length(id) + length(data)", total));
    bytes.push_back(imp->space("revision_ancestry",
                          "length(parent) + length(child)", total));
    bytes.push_back(imp->space("revision_certs",
                          "length(hash) + length(revision_id) + length(name)"
                          "+ length(value) + length(keypair_id)"
                          "+ length(signature)", total));
    bytes.push_back(imp->space("heights", "length(revision) + length(height)",
                          total));
    bytes.push_back((F("%u") % total).str());
  }

  // pad each vector's strings on the left with spaces to make them all the
  // same length
  {
    string::size_type width
      = max_element(counts.begin(), counts.end(), longest_number)->length();
    for(vector<string>::iterator i = counts.begin(); i != counts.end(); i++)
      if (width > i->length() && (*i)[0] != '[')
        i->insert(0U, width - i->length(), ' ');

    width = max_element(bytes.begin(), bytes.end(), longest_number)->length();
    for(vector<string>::iterator i = bytes.begin(); i != bytes.end(); i++)
      if (width > i->length() && (*i)[0] != '[')
        i->insert(0U, width - i->length(), ' ');
  }

  i18n_format form =
    F("creator code      : %s\n"
      "schema version    : %s\n"
      "counts:\n"
      "  full rosters    : %s\n"
      "  roster deltas   : %s\n"
      "  full files      : %s\n"
      "  file deltas     : %s\n"
      "  revisions       : %s\n"
      "  ancestry edges  : %s\n"
      "  certs           : %s\n"
      "  logical files   : %s\n"
      "bytes:\n"
      "  full rosters    : %s\n"
      "  roster deltas   : %s\n"
      "  full files      : %s\n"
      "  file deltas     : %s\n"
      "  revisions       : %s\n"
      "  cached ancestry : %s\n"
      "  certs           : %s\n"
      "  heights         : %s\n"
      "  total           : %s\n"
      "database:\n"
      "  page size       : %s\n"
      "  cache size      : %s"
      );

  form = form % format_creator_code(ccode);
  form = form % describe_sql_schema(imp->__sql);

  for (vector<string>::iterator i = counts.begin(); i != counts.end(); i++)
    form = form % *i;

  for (vector<string>::iterator i = bytes.begin(); i != bytes.end(); i++)
    form = form % *i;

  form = form % imp->page_size();
  form = form % imp->cache_size();

  out << form.str() << '\n'; // final newline is kept out of the translation

  // the following analyzation is only done for --verbose info
  if (!analyze)
    return;


  typedef map<revision_id, date_t> rev_date;
  rev_date rd;
  vector<cert> certs;

  L(FL("fetching revision dates"));
  imp->get_certs(date_cert_name, certs, "revision_certs");

  L(FL("analyzing revision dates"));
  rev_date::iterator d;
  for (vector<cert>::iterator i = certs.begin(); i != certs.end(); ++i)
    {
      date_t cert_date;
      try
        {
          cert_date = date_t(i->value());
        }
      catch (recoverable_failure & e)
        {
          // simply skip dates we cannot parse
          W(F("invalid date '%s' for revision %s; skipped")
            % i->value() % i->ident);
        }

      if (cert_date.valid())
        {
          if ((d = rd.find(i->ident)) == rd.end())
            rd.insert(make_pair(i->ident, cert_date));
          else
            {
              if (d->second > cert_date)
                d->second = cert_date;
            }
        }
    }

  L(FL("fetching ancestry map"));
  typedef multimap<revision_id, revision_id>::const_iterator gi;
  rev_ancestry_map graph;
  get_forward_ancestry(graph);

  L(FL("checking timestamps differences of related revisions"));
  int correct = 0,
      equal = 0,
      incorrect = 0,
      root_anc = 0,
      missing = 0;

  vector<s64> diffs;

  for (gi i = graph.begin(); i != graph.end(); ++i)
    {
      revision_id anc_rid = i->first,
                  desc_rid = i->second;

      if (null_id(anc_rid))
        {
          root_anc++;
          continue;
        }
      I(!null_id(desc_rid));

      date_t anc_date,
             desc_date;

      map<revision_id, date_t>::iterator j;
      if ((j = rd.find(anc_rid)) != rd.end())
        anc_date = j->second;

      if ((j = rd.find(desc_rid)) != rd.end())
        desc_date = j->second;

      if (anc_date.valid() && desc_date.valid())
        {
          // we only need seconds precision here
          s64 diff = (desc_date - anc_date) / 1000;
          diffs.push_back(diff);

          if (anc_date < desc_date)
            correct++;
          else if (anc_date == desc_date)
            equal++;
          else
            {
              L(FL("   rev %s -> rev %s") % anc_rid % desc_rid);
              L(FL("   but date %s ! -> %s")
                % anc_date.as_iso_8601_extended()
                % desc_date.as_iso_8601_extended());
              L(FL("   (difference: %d seconds)")
                % (anc_date - desc_date));
              incorrect++;
            }
        }
      else
        missing++;
    }

  // no information to provide in this case
  if (diffs.size() == 0)
    return;

  form =
    F("timestamp correctness between revisions:\n"
      "  correct dates   : %s edges\n"
      "  equal dates     : %s edges\n"
      "  incorrect dates : %s edges\n"
      "  based on root   : %s edges\n"
      "  missing dates   : %s edges\n"
      "\n"
      "timestamp differences between revisions:\n"
      "  mean            : %d sec\n"
      "  min             : %d sec\n"
      "  max             : %d sec\n"
      "\n"
      "  1st percentile  : %s sec\n"
      "  5th percentile  : %s sec\n"
      "  10th percentile : %s sec\n"
      "  25th percentile : %s sec\n"
      "  50th percentile : %s sec\n"
      "  75th percentile : %s sec\n"
      "  90th percentile : %s sec\n"
      "  95th percentile : %s sec\n"
      "  99th percentile : %s sec\n"
      );

  form = form % correct % equal % incorrect % root_anc % missing;

  // sort, so that we can get percentile values
  sort(diffs.begin(), diffs.end());

  // calculate mean time difference, output that, min and max
  s64 mean = accumulate(diffs.begin(), diffs.end(), 0);
  mean /= diffs.size();
  s64 median = *(diffs.begin() + diffs.size()/2);
  form = form % mean % *diffs.begin() % *diffs.rbegin()
    % *(diffs.begin() + int(diffs.size() * 0.01))
    % *(diffs.begin() + int(diffs.size() * 0.05))
    % *(diffs.begin() + int(diffs.size() * 0.10))
    % *(diffs.begin() + int(diffs.size() * 0.25))
    % *(diffs.begin() + int(diffs.size() * 0.50))
    % *(diffs.begin() + int(diffs.size() * 0.75))
    % *(diffs.begin() + int(diffs.size() * 0.90))
    % *(diffs.begin() + int(diffs.size() * 0.95))
    % *(diffs.begin() + int(diffs.size() * 0.99));

  // output the string, with some newlines out of translation
  out << '\n' << '\n' << form.str() << '\n';
}

void
database::version(ostream & out)
{
  ensure_open_for_maintenance();
  out << (F("database schema version: %s")
          % describe_sql_schema(imp->__sql)).str()
      << '\n';
}

void
database::migrate(key_store & keys, migration_status & mstat)
{
  ensure_open_for_maintenance();
  mstat = migrate_sql_schema(imp->__sql, keys, get_filename());
}

void
database::test_migration_step(key_store & keys, string const & schema)
{
  ensure_open_for_maintenance();
  ::test_migration_step(imp->__sql, keys, get_filename(), schema);
}

void
database::fix_bad_certs(bool drop_not_fixable)
{
  vector<key_id> all_keys;
  get_key_ids(all_keys);

  P(F("loading certs"));
  vector<pair<id, cert> > all_certs;
  {
    results res;
    query q("SELECT revision_id, name, value, keypair_id, signature, hash FROM revision_certs");
    imp->fetch(res, 6, any_rows, q);
    imp->results_to_certs(res, all_certs);
  }

  P(F("checking"));

  ticker tick_checked(_("checked"), "c", 25);
  ticker tick_bad(_("bad"), "b", 1);
  ticker tick_fixed(_("fixed"), "f", 1);
  shared_ptr<ticker> tick_dropped;
  if (drop_not_fixable)
    tick_dropped.reset(new ticker(_("dropped"), "d", 1));
  tick_checked.set_total(all_certs.size());

  int num_bad(0), num_fixed(0), num_dropped(0);

  for (vector<pair<id, cert> >::const_iterator cert_iter = all_certs.begin();
       cert_iter != all_certs.end(); ++cert_iter)
    {
      cert const & c(cert_iter->second);
      id const & certid(cert_iter->first);
      cert_status status = check_cert(c);
      ++tick_checked;
      if (status == cert_bad)
        {
          ++tick_bad;
          ++num_bad;
          bool fixed = false;
          string signable;
          c.signable_text(signable);
          for (vector<key_id>::const_iterator key_iter = all_keys.begin();
               key_iter != all_keys.end(); ++key_iter)
            {
              key_id const & keyid(*key_iter);
              if (check_signature(keyid, signable, c.sig) == cert_ok)
                {
                  key_name candidate_name;
                  rsa_pub_key junk;
                  get_pubkey(keyid, candidate_name, junk);
                  id chk_id;
                  c.hash_code(candidate_name, chk_id);
                  if (chk_id == certid)
                    {
                      imp->execute(query("UPDATE revision_certs SET keypair_id = ? WHERE hash = ?")
                                   % blob(keyid.inner()()) % blob(certid()));
                      ++tick_fixed;
                      ++num_fixed;
                      fixed = true;
                      break;
                    }
                }
            }
          if (!fixed)
            {
              if (drop_not_fixable)
                {
                  imp->execute(query("DELETE FROM revision_certs WHERE hash = ?")
                               % blob(certid()));
                  ++(*tick_dropped);
                  ++num_dropped;
                }
            }
        }
    }
  if (drop_not_fixable)
    {
      P(F("checked %d certs, found %d bad, fixed %d, dropped %d")
        % all_certs.size() % num_bad % num_fixed % num_dropped);
    }
  else
    {
      P(F("checked %d certs, found %d bad, fixed %d")
        % all_certs.size() % num_bad % num_fixed);
    }
}

void
database::ensure_open()
{
  imp->sql();
}

void
database::ensure_open_for_format_changes()
{
  imp->sql(format_bypass_mode);
}

void
database::ensure_open_for_cache_reset()
{
  imp->sql(cache_bypass_mode);
}

void
database::ensure_open_for_maintenance()
{
  imp->sql(schema_bypass_mode);
}

void
database_impl::execute(query const & query)
{
  results res;
  fetch(res, 0, 0, query);
}

void
database_impl::fetch(results & res,
                      int const want_cols,
                      int const want_rows,
                      query const & query)
{
  int nrow;
  int ncol;
  int rescode;

  res.clear();
  res.resize(0);

  map<string, statement>::iterator i = statement_cache.find(query.sql_cmd);
  if (i == statement_cache.end())
    {
      statement_cache.insert(make_pair(query.sql_cmd, statement()));
      i = statement_cache.find(query.sql_cmd);
      I(i != statement_cache.end());

      const char * tail;
      sqlite3_prepare_v2(sql(), query.sql_cmd.c_str(), -1, i->second.stmt.paddr(), &tail);
      assert_sqlite3_ok(sql());
      L(FL("prepared statement %s") % query.sql_cmd);

      // no support for multiple statements here
      E(*tail == 0, origin::internal,
        F("multiple statements in query: %s") % query.sql_cmd);
    }

  ncol = sqlite3_column_count(i->second.stmt());

  E(want_cols == any_cols || want_cols == ncol, origin::database,
    F("wanted %d columns got %d in query: %s") % want_cols % ncol % query.sql_cmd);

  // bind parameters for this execution

  int params = sqlite3_bind_parameter_count(i->second.stmt());

  // Ensure that exactly the right number of parameters were given
  I(params == int(query.args.size()));

  L(FL("binding %d parameters for %s") % params % query.sql_cmd);

  for (int param = 1; param <= params; param++)
    {
      // profiling finds this logging to be quite expensive
      if (global_sanity.debug_p())
        {
          string prefix;
          string log(query.args[param-1].data);

          if (query.args[param-1].type == query_param::blob)
            {
              prefix = "x";
              log = encode_hexenc(log, origin::internal);
            }

          if (log.size() > constants::db_log_line_sz)
            log = log.substr(0, constants::db_log_line_sz - 2) + "..";

          L(FL("binding %d with value '%s'") % param % log);
        }

      switch (idx(query.args, param - 1).type)
        {
        case query_param::text:
          sqlite3_bind_text(i->second.stmt(), param,
                            idx(query.args, param - 1).data.c_str(), -1,
                            SQLITE_STATIC);
          break;
        case query_param::blob:
          {
            string const & data = idx(query.args, param - 1).data;
            sqlite3_bind_blob(i->second.stmt(), param,
                              data.data(), data.size(),
                              SQLITE_STATIC);
          }
          break;
        default:
          I(false);
        }

      assert_sqlite3_ok(sql());
    }

  // execute and process results

  nrow = 0;
  for (rescode = sqlite3_step(i->second.stmt()); rescode == SQLITE_ROW;
       rescode = sqlite3_step(i->second.stmt()))
    {
      vector<string> row;
      for (int col = 0; col < ncol; col++)
        {
          const char * value = (const char*)sqlite3_column_blob(i->second.stmt(), col);
          int bytes = sqlite3_column_bytes(i->second.stmt(), col);
          E(value, origin::database,
            F("null result in query: %s") % query.sql_cmd);
          row.push_back(string(value, value + bytes));
          //L(FL("row %d col %d value='%s'") % nrow % col % value);
        }
      res.push_back(row);
    }

  if (rescode != SQLITE_DONE)
    assert_sqlite3_ok(sql());

  sqlite3_reset(i->second.stmt());
  assert_sqlite3_ok(sql());

  nrow = res.size();

  i->second.count++;

  E(want_rows == any_rows || want_rows == nrow,
    origin::database,
    F("wanted %d rows got %d in query: %s") % want_rows % nrow % query.sql_cmd);
}

bool
database_impl::table_has_entry(id const & key,
                               std::string const & column,
                               std::string const & table)
{
  results res;
  query q("SELECT 1 FROM " + table + " WHERE " + column + " = ? LIMIT 1");
  fetch(res, one_col, any_rows, q % blob(key()));
  return !res.empty();
}

// general application-level logic

void
database_impl::begin_transaction(bool exclusive)
{
  if (transaction_level == 0)
    {
      I(delayed_files.empty());
      I(roster_cache.all_clean());
      if (exclusive)
        execute(query("BEGIN EXCLUSIVE"));
      else
        execute(query("BEGIN DEFERRED"));
      transaction_exclusive = exclusive;
    }
  else
    {
      // You can't start an exclusive transaction within a non-exclusive
      // transaction
      I(!exclusive || transaction_exclusive);
    }
  transaction_level++;
}


static size_t
size_delayed_file(file_id const & id, file_data const & dat)
{
  return id.inner()().size() + dat.inner()().size();
}

bool
database_impl::have_delayed_file(file_id const & id)
{
  return delayed_files.find(id) != delayed_files.end();
}

void
database_impl::load_delayed_file(file_id const & id, file_data & dat)
{
  dat = safe_get(delayed_files, id);
}

// precondition: have_delayed_file(an_id) == true
void
database_impl::cancel_delayed_file(file_id const & an_id)
{
  file_data const & dat = safe_get(delayed_files, an_id);
  size_t cancel_size = size_delayed_file(an_id, dat);
  I(cancel_size <= delayed_writes_size);
  delayed_writes_size -= cancel_size;

  safe_erase(delayed_files, an_id);
}

void
database_impl::drop_or_cancel_file(file_id const & id)
{
  if (have_delayed_file(id))
    cancel_delayed_file(id);
  else
    drop(id.inner(), "files");
}

void
database_impl::schedule_delayed_file(file_id const & an_id,
                                      file_data const & dat)
{
  if (!have_delayed_file(an_id))
    {
      safe_insert(delayed_files, make_pair(an_id, dat));
      delayed_writes_size += size_delayed_file(an_id, dat);
    }
  if (delayed_writes_size > constants::db_max_delayed_file_bytes)
    flush_delayed_writes();
}

void
database_impl::flush_delayed_writes()
{
  for (map<file_id, file_data>::const_iterator i = delayed_files.begin();
       i != delayed_files.end(); ++i)
    write_delayed_file(i->first, i->second);
  clear_delayed_writes();
}

void
database_impl::clear_delayed_writes()
{
  delayed_files.clear();
  delayed_writes_size = 0;
}

void
database_impl::roster_writeback_manager::writeout(revision_id const & id,
                                                  cached_roster const & cr)
{
  I(cr.first);
  I(cr.second);
  imp.write_delayed_roster(id, *(cr.first), *(cr.second));
}

void
database_impl::commit_transaction()
{
  if (transaction_level == 1)
    {
      flush_delayed_writes();
      roster_cache.clean_all();
      execute(query("COMMIT"));
    }
  transaction_level--;
}

void
database_impl::rollback_transaction()
{
  if (transaction_level == 1)
    {
      clear_delayed_writes();
      roster_cache.clear_and_drop_writes();
      execute(query("ROLLBACK"));
    }
  transaction_level--;
}


bool
database_impl::file_or_manifest_base_exists(id const & ident,
                                            string const & table)
{
  // just check for a delayed file, since there are no delayed manifests
  if (have_delayed_file(file_id(ident)))
    return true;
  return table_has_entry(ident, "id", table);
}

bool
database::file_or_manifest_base_exists(file_id const & ident,
                                       string const & table)
{
  return imp->file_or_manifest_base_exists(ident.inner(), table);
}

// returns true if we are currently storing (or planning to store) a
// full-text for 'ident'
bool
database_impl::roster_base_stored(revision_id const & ident)
{
  if (roster_cache.exists(ident) && roster_cache.is_dirty(ident))
    return true;
  return table_has_entry(ident.inner(), "id", "rosters");
}

// returns true if we currently have a full-text for 'ident' available
// (possibly cached).  Warning: the results of this method are invalidated
// by calling roster_cache.insert_{clean,dirty}, because they can trigger
// cache cleaning.
bool
database_impl::roster_base_available(revision_id const & ident)
{
  if (roster_cache.exists(ident))
    return true;
  return table_has_entry(ident.inner(), "id", "rosters");
}

bool
database::delta_exists(id const & ident,
                       string const & table)
{
  return imp->table_has_entry(ident, "id", table);
}

bool
database_impl::delta_exists(file_id const & ident,
                            file_id const & base,
                            string const & table)
{
  results res;
  query q("SELECT 1 FROM " + table + " WHERE id = ? and base = ? LIMIT 1");
  fetch(res, one_col, any_rows,
        q % blob(ident.inner()()) % blob(base.inner()()));
  return !res.empty();
}

string
database_impl::count(string const & table)
{
  try
    {
      results res;
      query q("SELECT COUNT(*) FROM " + table);
      fetch(res, one_col, one_row, q);
      return (F("%u") % lexical_cast<u64>(res[0][0])).str();
    }
  catch (recoverable_failure const & e)
    {
      return format_sqlite_error_for_info(e);
    }

}

string
database_impl::space(string const & table, string const & rowspace, u64 & total)
{
  try
    {
      results res;
      // SUM({empty set}) is NULL; TOTAL({empty set}) is 0.0
      query q("SELECT TOTAL(" + rowspace + ") FROM " + table);
      fetch(res, one_col, one_row, q);
      u64 bytes = static_cast<u64>(lexical_cast<double>(res[0][0]));
      total += bytes;
      return (F("%u") % bytes).str();
    }
  catch (recoverable_failure & e)
    {
      return format_sqlite_error_for_info(e);
    }
}

unsigned int
database_impl::page_size()
{
  results res;
  query q("PRAGMA page_size");
  fetch(res, one_col, one_row, q);
  return lexical_cast<unsigned int>(res[0][0]);
}

unsigned int
database_impl::cache_size()
{
  // This returns the persistent (default) cache size.  It's possible to
  // override this setting transiently at runtime by setting PRAGMA
  // cache_size.
  results res;
  query q("PRAGMA default_cache_size");
  fetch(res, one_col, one_row, q);
  return lexical_cast<unsigned int>(res[0][0]);
}

void
database_impl::get_ids(string const & table, set<id> & ids)
{
  results res;
  query q("SELECT id FROM " + table);
  fetch(res, one_col, any_rows, q);

  for (size_t i = 0; i < res.size(); ++i)
    {
      ids.insert(id(res[i][0], origin::database));
    }
}

// for files and legacy manifest support
void
database_impl::get_file_or_manifest_base_unchecked(id const & ident,
                                                   data & dat,
                                                   string const & table)
{
  if (have_delayed_file(file_id(ident)))
    {
      file_data tmp;
      load_delayed_file(file_id(ident), tmp);
      dat = tmp.inner();
      return;
    }

  results res;
  query q("SELECT data FROM " + table + " WHERE id = ?");
  fetch(res, one_col, one_row, q % blob(ident()));

  gzip<data> rdata(res[0][0], origin::database);
  data rdata_unpacked;
  decode_gzip(rdata,rdata_unpacked);

  dat = rdata_unpacked;
}

// for files and legacy manifest support
void
database_impl::get_file_or_manifest_delta_unchecked(id const & ident,
                                                    id const & base,
                                                    delta & del,
                                                    string const & table)
{
  I(ident() != "");
  I(base() != "");
  results res;
  query q("SELECT delta FROM " + table + " WHERE id = ? AND base = ?");
  fetch(res, one_col, one_row,
        q % blob(ident()) % blob(base()));

  gzip<delta> del_packed(res[0][0], origin::database);
  decode_gzip(del_packed, del);
}

void
database_impl::get_roster_base(revision_id const & ident,
                               roster_t & roster, marking_map & marking)
{
  if (roster_cache.exists(ident))
    {
      cached_roster cr;
      roster_cache.fetch(ident, cr);
      I(cr.first);
      roster = *(cr.first);
      I(cr.second);
      marking = *(cr.second);
      return;
    }
  results res;
  query q("SELECT checksum, data FROM rosters WHERE id = ?");
  fetch(res, 2, one_row, q % blob(ident.inner()()));

  id checksum(res[0][0], origin::database);
  id calculated;
  calculate_ident(data(res[0][1], origin::database), calculated);
  E(calculated == checksum, origin::database,
    F("roster does not match hash"));

  gzip<data> dat_packed(res[0][1], origin::database);
  data dat;
  decode_gzip(dat_packed, dat);
  read_roster_and_marking(roster_data(dat), roster, marking);
}

void
database_impl::get_roster_delta(id const & ident,
                                id const & base,
                                roster<delta> & del)
{
  results res;
  query q("SELECT checksum, delta FROM roster_deltas WHERE id = ? AND base = ?");
  fetch(res, 2, one_row, q % blob(ident()) % blob(base()));

  id checksum(res[0][0], origin::database);
  id calculated;
  calculate_ident(data(res[0][1], origin::database), calculated);
  E(calculated == checksum, origin::database,
    F("roster_delta does not match hash"));

  gzip<delta> del_packed(res[0][1], origin::database);
  delta tmp;
  decode_gzip(del_packed, tmp);
  del = roster<delta>(tmp);
}

void
database_impl::write_delayed_file(file_id const & ident,
                                   file_data const & dat)
{
  gzip<data> dat_packed;
  encode_gzip(dat.inner(), dat_packed);

  // ident is a hash, which we should check
  I(!null_id(ident));
  file_id tid;
  calculate_ident(dat, tid);
  MM(ident);
  MM(tid);
  I(tid == ident);
  // and then write things to the db
  query q("INSERT INTO files (id, data) VALUES (?, ?)");
  execute(q % blob(ident.inner()()) % blob(dat_packed()));
}

void
database_impl::write_delayed_roster(revision_id const & ident,
                                     roster_t const & roster,
                                     marking_map const & marking)
{
  roster_data dat;
  write_roster_and_marking(roster, marking, dat);
  gzip<data> dat_packed;
  encode_gzip(dat.inner(), dat_packed);

  // ident is a number, and we should calculate a checksum on what
  // we write
  id checksum;
  calculate_ident(typecast_vocab<data>(dat_packed), checksum);

  // and then write it
  execute(query("INSERT INTO rosters (id, checksum, data) VALUES (?, ?, ?)")
          % blob(ident.inner()())
          % blob(checksum())
          % blob(dat_packed()));
}


void
database::put_file_delta(file_id const & ident,
                         file_id const & base,
                         file_delta const & del)
{
  I(!null_id(ident));
  I(!null_id(base));

  gzip<delta> del_packed;
  encode_gzip(del.inner(), del_packed);

  imp->execute(query("INSERT INTO file_deltas (id, base, delta) VALUES (?, ?, ?)")
               % blob(ident.inner()())
               % blob(base.inner()())
               % blob(del_packed()));
}

void
database_impl::put_roster_delta(revision_id const & ident,
                                 revision_id const & base,
                                 roster_delta const & del)
{
  gzip<delta> del_packed;
  encode_gzip(del.inner(), del_packed);

  id checksum;
  calculate_ident(typecast_vocab<data>(del_packed), checksum);

  query q("INSERT INTO roster_deltas (id, base, checksum, delta) VALUES (?, ?, ?, ?)");
  execute(q
          % blob(ident.inner()())
          % blob(base.inner()())
          % blob(checksum())
          % blob(del_packed()));
}

struct file_and_manifest_reconstruction_graph : public reconstruction_graph
{
  database_impl & imp;
  string const & data_table;
  string const & delta_table;

  file_and_manifest_reconstruction_graph(database_impl & imp,
                                         string const & data_table,
                                         string const & delta_table)
    : imp(imp), data_table(data_table), delta_table(delta_table)
  {}
  virtual bool is_base(id const & node) const
  {
    return imp.vcache.exists(node)
      || imp.file_or_manifest_base_exists(node, data_table);
  }
  virtual void get_next(id const & from, set<id> & next) const
  {
    next.clear();
    results res;
    query q("SELECT base FROM " + delta_table + " WHERE id = ?");
    imp.fetch(res, one_col, any_rows, q % blob(from()));
    for (results::const_iterator i = res.begin(); i != res.end(); ++i)
      next.insert(id((*i)[0], origin::database));
  }
};

// used for files and legacy manifest migration
void
database_impl::get_version(id const & ident,
                           data & dat,
                           string const & data_table,
                           string const & delta_table)
{
  I(ident() != "");

  reconstruction_path selected_path;
  {
    file_and_manifest_reconstruction_graph graph(*this, data_table, delta_table);
    get_reconstruction_path(ident, graph, selected_path);
  }

  I(!selected_path.empty());

  id curr = selected_path.back();
  selected_path.pop_back();
  data begin;

  if (vcache.exists(curr))
    I(vcache.fetch(curr, begin));
  else
    get_file_or_manifest_base_unchecked(curr, begin, data_table);

  shared_ptr<delta_applicator> appl = new_piecewise_applicator();
  appl->begin(begin());

  for (reconstruction_path::reverse_iterator i = selected_path.rbegin();
       i != selected_path.rend(); ++i)
    {
      id const nxt = id(*i);

      if (!vcache.exists(curr))
        {
          string tmp;
          appl->finish(tmp);
          vcache.insert_clean(curr, data(tmp, origin::database));
        }

      if (global_sanity.debug_p())
        L(FL("following delta %s -> %s") % curr % nxt);
      delta del;
      get_file_or_manifest_delta_unchecked(nxt, curr, del, delta_table);
      apply_delta(appl, del());

      appl->next();
      curr = nxt;
    }

  string tmp;
  appl->finish(tmp);
  dat = data(tmp, origin::database);

  id final;
  calculate_ident(dat, final);
  E(final == ident, origin::database,
    F("delta-reconstructed '%s' item does not match hash")
    % data_table);

  if (!vcache.exists(ident))
    vcache.insert_clean(ident, dat);
}

struct roster_reconstruction_graph : public reconstruction_graph
{
  database_impl & imp;
  roster_reconstruction_graph(database_impl & imp) : imp(imp) {}
  virtual bool is_base(id const & node) const
  {
    return imp.roster_base_available(revision_id(node));
  }
  virtual void get_next(id const & from, set<id> & next) const
  {
    next.clear();
    results res;
    query q("SELECT base FROM roster_deltas WHERE id = ?");
    imp.fetch(res, one_col, any_rows, q % blob(from()));
    for (results::const_iterator i = res.begin(); i != res.end(); ++i)
      next.insert(id((*i)[0], origin::database));
  }
};

struct database_impl::extractor
{
  virtual bool look_at_delta(roster_delta const & del) = 0;
  virtual void look_at_roster(roster_t const & roster, marking_map const & mm) = 0;
  virtual ~extractor() {};
};

struct database_impl::markings_extractor : public database_impl::extractor
{
private:
  node_id const & nid;
  const_marking_t & markings;

public:
  markings_extractor(node_id const & _nid, const_marking_t & _markings) :
    nid(_nid), markings(_markings) {} ;

  bool look_at_delta(roster_delta const & del)
  {
    return try_get_markings_from_roster_delta(del, nid, markings);
  }

  void look_at_roster(roster_t const & roster, marking_map const & mm)
  {
    markings = mm.get_marking(nid);
  }
};

struct database_impl::file_content_extractor : database_impl::extractor
{
private:
  node_id const & nid;
  file_id & content;

public:
  file_content_extractor(node_id const & _nid, file_id & _content) :
    nid(_nid), content(_content) {} ;

  bool look_at_delta(roster_delta const & del)
  {
    return try_get_content_from_roster_delta(del, nid, content);
  }

  void look_at_roster(roster_t const & roster, marking_map const & mm)
  {
    if (roster.has_node(nid))
      content = downcast_to_file_t(roster.get_node(nid))->content;
    else
      content = file_id();
  }
};

void
database_impl::extract_from_deltas(revision_id const & ident, extractor & x)
{
  reconstruction_path selected_path;
  {
    roster_reconstruction_graph graph(*this);
    {
      // we look at the nearest delta(s) first, without constructing the
      // whole path, as that would be a rather expensive operation.
      //
      // the reason why this strategy is worth the effort is, that in most
      // cases we are looking at the parent of a (content-)marked node, thus
      // the information we are for is right there in the delta leading to
      // this node.
      //
      // recording the deltas visited here in a set as to avoid inspecting
      // them later seems to be of little value, as it imposes a cost here,
      // but can seldom be exploited.
      set<id> deltas;
      graph.get_next(ident.inner(), deltas);
      for (set<id>::const_iterator i = deltas.begin();
           i != deltas.end(); ++i)
        {
          roster_delta del;
          get_roster_delta(ident.inner(), *i, del);
          bool found = x.look_at_delta(del);
          if (found)
            return;
        }
    }
    get_reconstruction_path(ident.inner(), graph, selected_path);
  }

  int path_length(selected_path.size());
  int i(0);
  id target_rev;

  for (reconstruction_path::const_iterator p = selected_path.begin();
       p != selected_path.end(); ++p)
    {
      if (i > 0)
        {
          roster_delta del;
          get_roster_delta(target_rev, id(*p), del);
          bool found = x.look_at_delta(del);
          if (found)
            return;
        }
      if (i == path_length-1)
        {
          // last iteration, we have reached a roster base
          roster_t roster;
          marking_map mm;
          get_roster_base(revision_id(*p), roster, mm);
          x.look_at_roster(roster, mm);
          return;
        }
      target_rev = id(*p);
      ++i;
    }
}

void
database::get_markings(revision_id const & id,
                       node_id const & nid,
                       const_marking_t & markings)
{
  database_impl::markings_extractor x(nid, markings);
  imp->extract_from_deltas(id, x);
}

void
database::get_file_content(revision_id const & id,
                           node_id const & nid,
                           file_id & content)
{
  // the imaginary root revision doesn't have any file.
  if (null_id(id))
    {
      content = file_id();
      return;
    }
  database_impl::file_content_extractor x(nid, content);
  imp->extract_from_deltas(id, x);
}

void
database::get_roster_version(revision_id const & ros_id,
                             cached_roster & cr)
{
  // if we already have it, exit early
  if (imp->roster_cache.exists(ros_id))
    {
      imp->roster_cache.fetch(ros_id, cr);
      return;
    }

  reconstruction_path selected_path;
  {
    roster_reconstruction_graph graph(*imp);
    get_reconstruction_path(ros_id.inner(), graph, selected_path);
  }

  id curr(selected_path.back());
  selected_path.pop_back();
  // we know that this isn't already in the cache (because of the early exit
  // above), so we should create new objects and spend time filling them in.
  shared_ptr<roster_t> roster(new roster_t);
  shared_ptr<marking_map> marking(new marking_map);
  imp->get_roster_base(revision_id(curr), *roster, *marking);

  for (reconstruction_path::reverse_iterator i = selected_path.rbegin();
       i != selected_path.rend(); ++i)
    {
      id const nxt(*i);
      if (global_sanity.debug_p())
        L(FL("following delta %s -> %s") % curr % nxt);
      roster_delta del;
      imp->get_roster_delta(nxt, curr, del);
      apply_roster_delta(del, *roster, *marking);
      curr = nxt;
    }

  // Double-check that the thing we got out looks okay.  We know that when
  // the roster was written to the database, it passed both of these tests,
  // and we also know that the data on disk has passed our checks for data
  // corruption -- so in theory, we know that what we got out is exactly
  // what we put in, and these checks are redundant.  (They cannot catch all
  // possible errors in any case, e.g., they don't test that the marking is
  // correct.)  What they can do, though, is serve as a sanity check on the
  // delta reconstruction code; if there is a bug where we put something
  // into the database and then later get something different back out, then
  // this is the only thing that can catch it.
  roster->check_sane_against(*marking);
  manifest_id expected_mid, actual_mid;
  get_revision_manifest(ros_id, expected_mid);
  calculate_ident(*roster, actual_mid);
  I(expected_mid == actual_mid);

  // const'ify the objects, to save them and pass them out
  cr.first = roster;
  cr.second = marking;
  imp->roster_cache.insert_clean(ros_id, cr);
}


void
database_impl::drop(id const & ident,
                    string const & table)
{
  string drop = "DELETE FROM " + table + " WHERE id = ?";
  execute(query(drop) % blob(ident()));
}

// ------------------------------------------------------------
// --                                                        --
// --              public interface follows                  --
// --                                                        --
// ------------------------------------------------------------

bool
database::file_version_exists(file_id const & id)
{
  return delta_exists(id.inner(), "file_deltas")
    || imp->file_or_manifest_base_exists(id.inner(), "files");
}

bool
database::roster_version_exists(revision_id const & id)
{
  return delta_exists(id.inner(), "roster_deltas")
    || imp->roster_base_available(id);
}

bool
database::revision_exists(revision_id const & id)
{
  results res;
  query q("SELECT id FROM revisions WHERE id = ?");
  imp->fetch(res, one_col, any_rows, q % blob(id.inner()()));
  I(res.size() <= 1);
  return res.size() == 1;
}

void
database::get_file_ids(set<file_id> & ids)
{
  ids.clear();
  set<id> tmp;
  imp->get_ids("files", tmp);
  imp->get_ids("file_deltas", tmp);
  add_decoration_to_container(tmp, ids);
}

void
database::get_revision_ids(set<revision_id> & ids)
{
  ids.clear();
  set<id> tmp;
  imp->get_ids("revisions", tmp);
  add_decoration_to_container(tmp, ids);
}

void
database::get_roster_ids(set<revision_id> & ids)
{
  ids.clear();
  set<id> tmp;
  imp->get_ids("rosters", tmp);
  add_decoration_to_container(tmp, ids);
  imp->get_ids("roster_deltas", tmp);
  add_decoration_to_container(tmp, ids);
}

void
database::get_file_version(file_id const & id,
                           file_data & dat)
{
  data tmp;
  imp->get_version(id.inner(), tmp, "files", "file_deltas");
  dat = file_data(tmp);
}

void
database::get_manifest_version(manifest_id const & id,
                               manifest_data & dat)
{
  data tmp;
  imp->get_version(id.inner(), tmp, "manifests", "manifest_deltas");
  dat = manifest_data(tmp);
}

void
database::put_file(file_id const & id,
                   file_data const & dat)
{
  if (file_version_exists(id))
    L(FL("file version '%s' already exists in db") % id);
  else
    imp->schedule_delayed_file(id, dat);
}

void
database::put_file_version(file_id const & old_id,
                           file_id const & new_id,
                           file_delta const & del)
{
  I(!(old_id == new_id));

  if (!file_version_exists(old_id))
    {
      W(F("file preimage '%s' missing in db") % old_id);
      W(F("dropping delta '%s' -> '%s'") % old_id % new_id);
      return;
    }

  var_value delta_direction("reverse");
  var_key key(var_domain("database"), var_name("delta-direction"));
  if (var_exists(key))
    {
      get_var(key, delta_direction);
    }
  bool make_reverse_deltas(delta_direction() == "reverse" ||
                           delta_direction() == "both");
  bool make_forward_deltas(delta_direction() == "forward" ||
                           delta_direction() == "both");
  if (!make_reverse_deltas && !make_forward_deltas)
    {
      W(F("Unknown delta direction '%s'; assuming 'reverse'. Valid "
          "values are 'reverse', 'forward', 'both'.") % delta_direction);
      make_reverse_deltas = true;
    }

  file_data old_data, new_data;
  get_file_version(old_id, old_data);
  {
    data tmp;
    patch(old_data.inner(), del.inner(), tmp);
    new_data = file_data(tmp);
  }

  file_delta reverse_delta;
  {
    string tmp;
    invert_xdelta(old_data.inner()(), del.inner()(), tmp);
    reverse_delta = file_delta(tmp, origin::database);
    data old_tmp;
    patch(new_data.inner(), reverse_delta.inner(), old_tmp);
    // We already have the real old data, so compare the
    // reconstruction to that directly instead of hashing
    // the reconstruction and comparing hashes.
    I(old_tmp == old_data.inner());
  }

  transaction_guard guard(*this);
  if (make_reverse_deltas)
    {
      if (!file_or_manifest_base_exists(new_id, "files"))
        {
          imp->schedule_delayed_file(new_id, new_data);
        }
      if (!imp->delta_exists(old_id, new_id, "file_deltas"))
        {
          put_file_delta(old_id, new_id, reverse_delta);
        }
    }
  if (make_forward_deltas)
    {
      if (!imp->delta_exists(new_id, old_id, "file_deltas"))
        {
          put_file_delta(new_id, old_id, del);
        }
    }
  else
    {
      imp->drop(new_id.inner(), "file_deltas");
    }
  if (file_or_manifest_base_exists(old_id, "files"))
    {
      // descendent of a head version replaces the head, therefore old head
      // must be disposed of
      if (delta_exists(old_id.inner(), "file_deltas"))
        imp->drop_or_cancel_file(old_id);
    }
  guard.commit();
}

void
database::get_arbitrary_file_delta(file_id const & src_id,
                                   file_id const & dst_id,
                                   file_delta & del)
{
  delta dtmp;
  // Deltas stored in the database go from base -> id.
  results res;
  query q1("SELECT delta FROM file_deltas "
           "WHERE base = ? AND id = ?");
  imp->fetch(res, one_col, any_rows,
             q1 % blob(src_id.inner()()) % blob(dst_id.inner()()));

  if (!res.empty())
    {
      // Exact hit: a plain delta from src -> dst.
      gzip<delta> del_packed(res[0][0], origin::database);
      decode_gzip(del_packed, dtmp);
      del = file_delta(dtmp);
      return;
    }

  query q2("SELECT delta FROM file_deltas "
           "WHERE base = ? AND id = ?");
  imp->fetch(res, one_col, any_rows,
             q2 % blob(dst_id.inner()()) % blob(src_id.inner()()));

  if (!res.empty())
    {
      // We have a delta from dst -> src; we need to
      // invert this to a delta from src -> dst.
      gzip<delta> del_packed(res[0][0], origin::database);
      decode_gzip(del_packed, dtmp);
      string fwd_delta;
      file_data dst;
      get_file_version(dst_id, dst);
      invert_xdelta(dst.inner()(), dtmp(), fwd_delta);
      del = file_delta(fwd_delta, origin::database);
      return;
    }

  // No deltas of use; just load both versions and diff.
  file_data fd1, fd2;
  get_file_version(src_id, fd1);
  get_file_version(dst_id, fd2);
  diff(fd1.inner(), fd2.inner(), dtmp);
  del = file_delta(dtmp);
}


void
database::get_forward_ancestry(rev_ancestry_map & graph)
{
  // share some storage
  id::symtab id_syms;

  results res;
  graph.clear();
  imp->fetch(res, 2, any_rows,
             query("SELECT parent,child FROM revision_ancestry"));
  for (size_t i = 0; i < res.size(); ++i)
    graph.insert(make_pair(revision_id(res[i][0], origin::database),
                           revision_id(res[i][1], origin::database)));
}

void
database::get_reverse_ancestry(rev_ancestry_map & graph)
{
  // share some storage
  id::symtab id_syms;

  results res;
  graph.clear();
  imp->fetch(res, 2, any_rows,
             query("SELECT child,parent FROM revision_ancestry"));
  for (size_t i = 0; i < res.size(); ++i)
    graph.insert(make_pair(revision_id(res[i][0], origin::database),
                           revision_id(res[i][1], origin::database)));
}

void
database::get_revision_parents(revision_id const & id,
                               set<revision_id> & parents)
{
  I(!null_id(id));
  parent_id_map::iterator i = imp->parent_cache.find(id);
  if (i == imp->parent_cache.end())
    {
      results res;
      parents.clear();
      imp->fetch(res, one_col, any_rows,
                 query("SELECT parent FROM revision_ancestry WHERE child = ?")
                 % blob(id.inner()()));
      for (size_t i = 0; i < res.size(); ++i)
        parents.insert(revision_id(res[i][0], origin::database));

      imp->parent_cache.insert(make_pair(id, parents));
    }
  else
    {
      parents = i->second;
    }
}

void
database::get_revision_children(revision_id const & id,
                                set<revision_id> & children)
{
  results res;
  children.clear();
  imp->fetch(res, one_col, any_rows,
             query("SELECT child FROM revision_ancestry WHERE parent = ?")
        % blob(id.inner()()));
  for (size_t i = 0; i < res.size(); ++i)
    children.insert(revision_id(res[i][0], origin::database));
}

void
database::get_leaves(set<revision_id> & leaves)
{
  results res;
  leaves.clear();
  imp->fetch(res, one_col, any_rows,
             query("SELECT revisions.id FROM revisions "
                   "LEFT JOIN revision_ancestry "
                   "ON revisions.id = revision_ancestry.parent "
                   "WHERE revision_ancestry.child IS null"));
  for (size_t i = 0; i < res.size(); ++i)
    leaves.insert(revision_id(res[i][0], origin::database));
}


void
database::get_revision_manifest(revision_id const & rid,
                               manifest_id & mid)
{
  revision_t rev;
  get_revision(rid, rev);
  mid = rev.new_manifest;
}

void
database::get_common_ancestors(std::set<revision_id> const & revs,
                               std::set<revision_id> & common_ancestors)
{
  set<revision_id> ancestors, all_common_ancestors;
  vector<revision_id> frontier;
  for (set<revision_id>::const_iterator i = revs.begin();
       i != revs.end(); ++i)
    {
      I(revision_exists(*i));
      ancestors.clear();
      ancestors.insert(*i);
      frontier.push_back(*i);
      while (!frontier.empty())
        {
          revision_id rid = frontier.back();
          frontier.pop_back();
          if(!null_id(rid))
            {
              set<revision_id> parents;
              get_revision_parents(rid, parents);
              for (set<revision_id>::const_iterator i = parents.begin();
                   i != parents.end(); ++i)
                {
                  if (ancestors.find(*i) == ancestors.end())
                    {
                      frontier.push_back(*i);
                      ancestors.insert(*i);
                    }
                }
            }
        }
      if (all_common_ancestors.empty())
        all_common_ancestors = ancestors;
      else
        {
          set<revision_id> common;
          set_intersection(ancestors.begin(), ancestors.end(),
                         all_common_ancestors.begin(), all_common_ancestors.end(),
                         inserter(common, common.begin()));
          all_common_ancestors = common;
        }
    }

  for (set<revision_id>::const_iterator i = all_common_ancestors.begin();
       i != all_common_ancestors.end(); ++i)
    {
      // null id's here come from the empty parents of root revisions.
      // these should not be considered as common ancestors and are skipped.
      if (null_id(*i)) continue;
      common_ancestors.insert(*i);
    }
}

bool
database::is_a_ancestor_of_b(revision_id const & ancestor,
                             revision_id const & child)
{
  if (ancestor == child)
    return false;

  rev_height anc_height;
  rev_height child_height;
  get_rev_height(ancestor, anc_height);
  get_rev_height(child, child_height);

  if (anc_height > child_height)
    return false;


  vector<revision_id> todo;
  todo.push_back(ancestor);
  set<revision_id> seen;
  while (!todo.empty())
    {
      revision_id anc = todo.back();
      todo.pop_back();
      set<revision_id> anc_children;
      get_revision_children(anc, anc_children);
      for (set<revision_id>::const_iterator i = anc_children.begin();
           i != anc_children.end(); ++i)
        {
          if (*i == child)
            return true;
          else if (seen.find(*i) != seen.end())
            continue;
          else
            {
              get_rev_height(*i, anc_height);
              if (child_height > anc_height)
                {
                  seen.insert(*i);
                  todo.push_back(*i);
                }
            }
        }
    }
  return false;
}

void
database::get_revision(revision_id const & id,
                       revision_t & rev)
{
  revision_data d;
  get_revision(id, d);
  read_revision(d, rev);
}

void
database::get_revision(revision_id const & id,
                       revision_data & dat)
{
  I(!null_id(id));
  results res;
  imp->fetch(res, one_col, one_row,
             query("SELECT data FROM revisions WHERE id = ?")
             % blob(id.inner()()));

  gzip<data> gzdata(res[0][0], origin::database);
  data rdat;
  decode_gzip(gzdata,rdat);

  // verify that we got a revision with the right id
  {
    revision_id tmp;
    calculate_ident(revision_data(rdat), tmp);
    E(id == tmp, origin::database,
      F("revision does not match hash"));
  }

  dat = revision_data(rdat);
}

void
database::get_rev_height(revision_id const & id,
                         rev_height & height)
{
  if (null_id(id))
    {
      height = rev_height::root_height();
      return;
    }

  height_map::const_iterator i = imp->height_cache.find(id);
  if (i == imp->height_cache.end())
    {
      results res;
      imp->fetch(res, one_col, one_row,
                 query("SELECT height FROM heights WHERE revision = ?")
                 % blob(id.inner()()));

      I(res.size() == 1);

      height = rev_height(res[0][0]);
      imp->height_cache.insert(make_pair(id, height));
    }
  else
    {
      height = i->second;
    }

  I(height.valid());
}

void
database::put_rev_height(revision_id const & id,
                         rev_height const & height)
{
  I(!null_id(id));
  I(revision_exists(id));
  I(height.valid());

  imp->height_cache.erase(id);

  imp->execute(query("INSERT INTO heights VALUES(?, ?)")
               % blob(id.inner()())
               % blob(height()));
}

bool
database::has_rev_height(rev_height const & height)
{
  results res;
  imp->fetch(res, one_col, any_rows,
             query("SELECT height FROM heights WHERE height = ?")
             % blob(height()));
  I((res.size() == 1) || (res.empty()));
  return res.size() == 1;
}

void
database::deltify_revision(revision_id const & rid)
{
  transaction_guard guard(*this);
  revision_t rev;
  MM(rev);
  MM(rid);
  get_revision(rid, rev);
  // Make sure that all parent revs have their files replaced with deltas
  // from this rev's files.
  {
    for (edge_map::const_iterator i = rev.edges.begin();
         i != rev.edges.end(); ++i)
      {
        for (map<file_path, pair<file_id, file_id> >::const_iterator
               j = edge_changes(i).deltas_applied.begin();
             j != edge_changes(i).deltas_applied.end(); ++j)
          {
            file_id old_id(delta_entry_src(j));
            file_id new_id(delta_entry_dst(j));
            // if not yet deltified
            if (file_or_manifest_base_exists(old_id, "files") &&
                file_version_exists(new_id))
              {
                file_data old_data;
                file_data new_data;
                get_file_version(old_id, old_data);
                get_file_version(new_id, new_data);
                delta delt;
                diff(old_data.inner(), new_data.inner(), delt);
                file_delta del(delt);
                imp->drop_or_cancel_file(new_id);
                imp->drop(new_id.inner(), "file_deltas");
                put_file_version(old_id, new_id, del);
              }
          }
      }
  }
  guard.commit();
}


bool
database::put_revision(revision_id const & new_id,
                       revision_t const & rev)
{
  MM(new_id);
  MM(rev);

  I(!null_id(new_id));

  if (revision_exists(new_id))
    {
      if (global_sanity.debug_p())
        L(FL("revision '%s' already exists in db") % new_id);
      return false;
    }

  I(rev.made_for == made_for_database);
  rev.check_sane();

  // Phase 1: confirm the revision makes sense, and the required files
  // actually exist
  for (edge_map::const_iterator i = rev.edges.begin();
       i != rev.edges.end(); ++i)
    {
      if (!edge_old_revision(i).inner()().empty()
          && !revision_exists(edge_old_revision(i)))
        {
          W(F("missing prerequisite revision '%s'")
            % edge_old_revision(i));
          W(F("dropping revision '%s'") % new_id);
          return false;
        }

      for (map<file_path, file_id>::const_iterator a
             = edge_changes(i).files_added.begin();
           a != edge_changes(i).files_added.end(); ++a)
        {
          if (! file_version_exists(a->second))
            {
              W(F("missing prerequisite file '%s'") % a->second);
              W(F("dropping revision '%s'") % new_id);
              return false;
            }
        }

      for (map<file_path, pair<file_id, file_id> >::const_iterator d
             = edge_changes(i).deltas_applied.begin();
           d != edge_changes(i).deltas_applied.end(); ++d)
        {
          I(!delta_entry_src(d).inner()().empty());
          I(!delta_entry_dst(d).inner()().empty());

          if (! file_version_exists(delta_entry_src(d)))
            {
              W(F("missing prerequisite file pre-delta '%s'")
                % delta_entry_src(d));
              W(F("dropping revision '%s'") % new_id);
              return false;
            }

          if (! file_version_exists(delta_entry_dst(d)))
            {
              W(F("missing prerequisite file post-delta '%s'")
                % delta_entry_dst(d));
              W(F("dropping revision '%s'") % new_id);
              return false;
            }
        }
    }

  transaction_guard guard(*this);

  // Phase 2: Write the revision data (inside a transaction)

  revision_data d;
  write_revision(rev, d);
  gzip<data> d_packed;
  encode_gzip(d.inner(), d_packed);
  imp->execute(query("INSERT INTO revisions VALUES(?, ?)")
               % blob(new_id.inner()())
               % blob(d_packed()));

  for (edge_map::const_iterator e = rev.edges.begin();
       e != rev.edges.end(); ++e)
    {
      imp->execute(query("INSERT INTO revision_ancestry VALUES(?, ?)")
                   % blob(edge_old_revision(e).inner()())
                   % blob(new_id.inner()()));
    }
  // We don't have to clear out the child's entry in the parent_cache,
  // because the child did not exist before this function was called, so
  // it can't be in the parent_cache already.

  // Phase 3: Construct and write the roster (which also checks the manifest
  // id as it goes), but only if the roster does not already exist in the db
  // (i.e. because it was left over by a kill_rev_locally)
  // FIXME: there is no knowledge yet on speed implications for commands which
  // put a lot of revisions in a row (i.e. tailor or cvs_import)!

  if (!roster_version_exists(new_id))
    {
      put_roster_for_revision(new_id, rev);
    }
  else
    {
      L(FL("roster for revision '%s' already exists in db") % new_id);
    }

  // Phase 4: rewrite any files that need deltas added

  deltify_revision(new_id);

  // Phase 5: determine the revision height

  put_height_for_revision(new_id, rev);

  // Finally, commit.

  guard.commit();
  return true;
}

void
database::put_height_for_revision(revision_id const & new_id,
                                  revision_t const & rev)
{
  I(!null_id(new_id));

  rev_height highest_parent;
  // we always branch off the highest parent ...
  for (edge_map::const_iterator e = rev.edges.begin();
       e != rev.edges.end(); ++e)
    {
      rev_height parent; MM(parent);
      get_rev_height(edge_old_revision(e), parent);
      if (parent > highest_parent)
      {
        highest_parent = parent;
      }
    }

  // ... then find the first unused child
  u32 childnr(0);
  rev_height candidate; MM(candidate);
  while(true)
    {
      candidate = highest_parent.child_height(childnr);
      if (!has_rev_height(candidate))
        {
          break;
        }
      I(childnr < std::numeric_limits<u32>::max());
      ++childnr;
    }
  put_rev_height(new_id, candidate);
}

void
database::put_roster_for_revision(revision_id const & new_id,
                                  revision_t const & rev)
{
  // Construct, the roster, sanity-check the manifest id, and then write it
  // to the db
  shared_ptr<roster_t> ros_writeable(new roster_t); MM(*ros_writeable);
  shared_ptr<marking_map> mm_writeable(new marking_map); MM(*mm_writeable);
  manifest_id roster_manifest_id;
  MM(roster_manifest_id);
  make_roster_for_revision(*this, rev, new_id, *ros_writeable, *mm_writeable);
  calculate_ident(*ros_writeable, roster_manifest_id, false);
  E(rev.new_manifest == roster_manifest_id, rev.made_from,
    F("revision contains incorrect manifest_id"));
  // const'ify the objects, suitable for caching etc.
  roster_t_cp ros = ros_writeable;
  marking_map_cp mm = mm_writeable;
  put_roster(new_id, rev, ros, mm);
}

bool
database::put_revision(revision_id const & new_id,
                       revision_data const & dat)
{
  revision_t rev;
  read_revision(dat, rev);
  return put_revision(new_id, rev);
}


void
database::delete_existing_revs_and_certs()
{
  imp->execute(query("DELETE FROM revisions"));
  imp->execute(query("DELETE FROM revision_ancestry"));
  imp->execute(query("DELETE FROM revision_certs"));
  imp->execute(query("DELETE FROM branch_leaves"));
}

void
database::delete_existing_manifests()
{
  imp->execute(query("DELETE FROM manifests"));
  imp->execute(query("DELETE FROM manifest_deltas"));
}

void
database::delete_existing_rosters()
{
  imp->execute(query("DELETE FROM rosters"));
  imp->execute(query("DELETE FROM roster_deltas"));
  imp->execute(query("DELETE FROM next_roster_node_number"));
}

void
database::delete_existing_heights()
{
  imp->execute(query("DELETE FROM heights"));
}

void
database::delete_existing_branch_leaves()
{
  imp->execute(query("DELETE FROM branch_leaves"));
}

/// Deletes one revision from the local database.
/// @see kill_rev_locally
void
database::delete_existing_rev_and_certs(revision_id const & rid)
{
  transaction_guard guard (*this);

  // Check that the revision exists and doesn't have any children.
  I(revision_exists(rid));
  set<revision_id> children;
  get_revision_children(rid, children);
  I(children.empty());


  L(FL("Killing revision %s locally") % rid);

  // Kill the certs, ancestry, and revision.
  imp->execute(query("DELETE from revision_certs WHERE revision_id = ?")
               % blob(rid.inner()()));
  {
    results res;
    imp->fetch(res, one_col, any_rows,
               query("SELECT branch FROM branch_leaves where revision_id = ?")
               % blob(rid.inner()()));
    for (results::const_iterator i = res.begin(); i != res.end(); ++i)
      {
        recalc_branch_leaves(cert_value((*i)[0], origin::database));
      }
  }
  imp->cert_stamper.note_change();

  imp->execute(query("DELETE from revision_ancestry WHERE child = ?")
               % blob(rid.inner()()));

  imp->execute(query("DELETE from heights WHERE revision = ?")
               % blob(rid.inner()()));

  imp->execute(query("DELETE from revisions WHERE id = ?")
               % blob(rid.inner()()));

  guard.commit();
}

void
database::compute_branch_leaves(cert_value const & branch_name, set<revision_id> & revs)
{
  imp->execute(query("DELETE FROM branch_leaves WHERE branch = ?") % blob(branch_name()));
  get_revisions_with_cert(cert_name("branch"), branch_name, revs);
  erase_ancestors(*this, revs);
}

void
database::recalc_branch_leaves(cert_value const & branch_name)
{
  imp->execute(query("DELETE FROM branch_leaves WHERE branch = ?") % blob(branch_name()));
  set<revision_id> revs;
  compute_branch_leaves(branch_name, revs);
  for (set<revision_id>::const_iterator i = revs.begin(); i != revs.end(); ++i)
    {
      imp->execute(query("INSERT INTO branch_leaves (branch, revision_id) "
                         "VALUES (?, ?)") % blob(branch_name()) % blob((*i).inner()()));
    }
}

void database::delete_certs_locally(revision_id const & rev,
                                    cert_name const & name)
{
  imp->execute(query("DELETE FROM revision_certs WHERE revision_id = ? AND name = ?")
               % blob(rev.inner()()) % text(name()));
  imp->cert_stamper.note_change();
}
void database::delete_certs_locally(revision_id const & rev,
                                    cert_name const & name,
                                    cert_value const & value)
{
  imp->execute(query("DELETE FROM revision_certs WHERE revision_id = ? AND name = ? AND value = ?")
               % blob(rev.inner()()) % text(name()) % blob(value()));
  imp->cert_stamper.note_change();
}

// crypto key management

void
database::get_key_ids(vector<key_id> & pubkeys)
{
  pubkeys.clear();
  results res;

  imp->fetch(res, one_col, any_rows, query("SELECT id FROM public_keys"));

  for (size_t i = 0; i < res.size(); ++i)
    pubkeys.push_back(key_id(res[i][0], origin::database));
}

void
database_impl::get_keys(string const & table, vector<key_name> & keys)
{
  keys.clear();
  results res;
  fetch(res, one_col, any_rows, query("SELECT id FROM " + table));
  for (size_t i = 0; i < res.size(); ++i)
    keys.push_back(key_name(res[i][0], origin::database));
}

void
database::get_public_keys(vector<key_name> & keys)
{
  imp->get_keys("public_keys", keys);
}

bool
database::public_key_exists(key_id const & hash)
{
  MM(hash);
  results res;
  imp->fetch(res, one_col, any_rows,
             query("SELECT id FROM public_keys WHERE id = ?")
             % blob(hash.inner()()));
  I((res.size() == 1) || (res.empty()));
  if (res.size() == 1)
    return true;
  return false;
}

bool
database::public_key_exists(key_name const & id)
{
  MM(id);
  results res;
  imp->fetch(res, one_col, any_rows,
             query("SELECT id FROM public_keys WHERE name = ?")
             % text(id()));
  I((res.size() == 1) || (res.empty()));
  if (res.size() == 1)
    return true;
  return false;
}

void
database::get_pubkey(key_id const & hash,
                     key_name & id,
                     rsa_pub_key & pub)
{
  MM(hash);
  results res;
  imp->fetch(res, 2, one_row,
             query("SELECT name, keydata FROM public_keys WHERE id = ?")
             % blob(hash.inner()()));
  id = key_name(res[0][0], origin::database);
  pub = rsa_pub_key(res[0][1], origin::database);
}

void
database::get_key(key_id const & pub_id,
                  rsa_pub_key & pub)
{
  MM(pub_id);
  results res;
  imp->fetch(res, one_col, one_row,
             query("SELECT keydata FROM public_keys WHERE id = ?")
             % blob(pub_id.inner()()));
  pub = rsa_pub_key(res[0][0], origin::database);
}

bool
database::put_key(key_name const & pub_id,
                  rsa_pub_key const & pub)
{
  MM(pub_id);
  MM(pub);
  key_id thash;
  key_hash_code(pub_id, pub, thash);

  if (public_key_exists(thash))
    {
      L(FL("skipping existing public key %s") % pub_id);
      return false;
    }

  L(FL("putting public key %s") % pub_id);

  imp->execute(query("INSERT INTO public_keys(id, name, keydata) VALUES(?, ?, ?)")
               % blob(thash.inner()())
               % text(pub_id())
               % blob(pub()));

  return true;
}

void
database::delete_public_key(key_id const & pub_id)
{
  MM(pub_id);
  imp->execute(query("DELETE FROM public_keys WHERE id = ?")
               % blob(pub_id.inner()()));
}

void
database::encrypt_rsa(key_id const & pub_id,
                      string const & plaintext,
                      rsa_oaep_sha_data & ciphertext)
{
  MM(pub_id);
  rsa_pub_key pub;
  get_key(pub_id, pub);

  SecureVector<Botan::byte> pub_block;
  pub_block.set(reinterpret_cast<Botan::byte const *>(pub().data()),
                pub().size());

  shared_ptr<X509_PublicKey> x509_key(Botan::X509::load_key(pub_block));
  shared_ptr<RSA_PublicKey> pub_key
    = shared_dynamic_cast<RSA_PublicKey>(x509_key);
  if (!pub_key)
    throw recoverable_failure(origin::system,
                              "Failed to get RSA encrypting key");

  shared_ptr<PK_Encryptor>
    encryptor(get_pk_encryptor(*pub_key, "EME1(SHA-1)"));

  SecureVector<Botan::byte> ct;

#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
  ct = encryptor->encrypt(
          reinterpret_cast<Botan::byte const *>(plaintext.data()),
          plaintext.size(), lazy_rng::get());
#else
  ct = encryptor->encrypt(
          reinterpret_cast<Botan::byte const *>(plaintext.data()),
          plaintext.size());
#endif
  ciphertext = rsa_oaep_sha_data(string(reinterpret_cast<char const *>(ct.begin()),
                                        ct.size()),
                                 origin::database);
}

cert_status
database::check_signature(key_id const & id,
                          string const & alleged_text,
                          rsa_sha1_signature const & signature)
{
  MM(id);
  MM(alleged_text);
  shared_ptr<PK_Verifier> verifier;

  verifier_cache::const_iterator i = imp->verifiers.find(id);
  if (i != imp->verifiers.end())
    verifier = i->second.first;

  else
    {
      rsa_pub_key pub;
      SecureVector<Botan::byte> pub_block;

      if (!public_key_exists(id))
        return cert_unknown;

      get_key(id, pub);
      pub_block.set(reinterpret_cast<Botan::byte const *>(pub().data()),
                    pub().size());

      L(FL("building verifier for %d-byte pub key") % pub_block.size());
      shared_ptr<X509_PublicKey> x509_key(Botan::X509::load_key(pub_block));
      shared_ptr<RSA_PublicKey> pub_key
        = boost::shared_dynamic_cast<RSA_PublicKey>(x509_key);

      E(pub_key, id.inner().made_from,
        F("Failed to get RSA verifying key for %s") % id);

      verifier.reset(get_pk_verifier(*pub_key, "EMSA3(SHA-1)"));

      /* XXX This is ugly. We need to keep the key around
       * as long as the verifier is around, but the shared_ptr will go
       * away after we leave this scope. Hence we store a pair of
       * <verifier,key> so they both exist. */
      imp->verifiers.insert(make_pair(id, make_pair(verifier, pub_key)));
    }

  // check the text+sig against the key
  L(FL("checking %d-byte signature") % signature().size());

  if (verifier->verify_message(
        reinterpret_cast<Botan::byte const*>(alleged_text.data()),
        alleged_text.size(),
        reinterpret_cast<Botan::byte const*>(signature().data()),
        signature().size()))
    return cert_ok;
  else
    return cert_bad;
}

cert_status
database::check_cert(cert const & t)
{
  string signed_text;
  t.signable_text(signed_text);
  return check_signature(t.key, signed_text, t.sig);
}

// cert management

bool
database_impl::cert_exists(cert const & t,
                           string const & table)
{
  results res;
  query q = query("SELECT revision_id FROM " + table + " WHERE revision_id = ? "
                  "AND name = ? "
                  "AND value = ? "
                  "AND keypair_id = ? "
                  "AND signature = ?")
    % blob(t.ident.inner()())
    % text(t.name())
    % blob(t.value())
    % blob(t.key.inner()())
    % blob(t.sig());

  fetch(res, 1, any_rows, q);

  I(res.empty() || res.size() == 1);
  return res.size() == 1;
}

void
database_impl::put_cert(cert const & t,
                        string const & table)
{
  results res;
  fetch(res, 1, one_row,
        query("SELECT name FROM public_keys WHERE id = ?")
        % blob(t.key.inner()()));
  key_name keyname(res[0][0], origin::database);

  id thash;
  t.hash_code(keyname, thash);
  rsa_sha1_signature sig;

  string insert = "INSERT INTO " + table + " VALUES(?, ?, ?, ?, ?, ?)";

  execute(query(insert)
          % blob(thash())
          % blob(t.ident.inner()())
          % text(t.name())
          % blob(t.value())
          % blob(t.key.inner()())
          % blob(t.sig()));
}

void
database_impl::results_to_certs(results const & res,
                                vector<cert> & certs)
{
  certs.clear();
  for (size_t i = 0; i < res.size(); ++i)
    {
      cert t;
      t = cert(revision_id(res[i][0], origin::database),
               cert_name(res[i][1], origin::database),
               cert_value(res[i][2], origin::database),
               key_id(res[i][3], origin::database),
               rsa_sha1_signature(res[i][4], origin::database));
      certs.push_back(t);
    }
}

void
database_impl::results_to_certs(results const & res,
                                vector<pair<id, cert> > & certs)
{
  certs.clear();
  for (size_t i = 0; i < res.size(); ++i)
    {
      cert t;
      t = cert(revision_id(res[i][0], origin::database),
               cert_name(res[i][1], origin::database),
               cert_value(res[i][2], origin::database),
               key_id(res[i][3], origin::database),
               rsa_sha1_signature(res[i][4], origin::database));
      certs.push_back(make_pair(id(res[i][5], origin::database),
                                t));
    }
}

void
database_impl::oldstyle_results_to_certs(results const & res,
                                         vector<cert> & certs)
{
  certs.clear();
  for (size_t i = 0; i < res.size(); ++i)
    {
      revision_id rev_id(res[i][0], origin::database);
      cert_name name(res[i][1], origin::database);
      cert_value value(res[i][2], origin::database);

      key_name k_name(res[i][3], origin::database);
      key_id k_id;
      {
        results key_res;
        query lookup_key("SELECT id FROM public_keys WHERE name = ?");
        fetch(key_res, 1, any_rows, lookup_key % text(k_name()));
        if (key_res.size() == 0)
          break; // no key, cert is bogus
        else if (key_res.size() == 1)
          k_id = key_id(key_res[0][0], origin::database);
        else
          E(false, origin::database,
            F("Your database contains multiple keys named %s") % k_name);
      }

      rsa_sha1_signature sig(res[i][4], origin::database);
      certs.push_back(cert(rev_id, name, value, k_id, sig));
    }
}

void
database_impl::install_functions()
{
#ifdef SUPPORT_SQLITE_BEFORE_3003014
  if (sqlite3_libversion_number() < 3003013)
    I(sqlite3_create_function(sql(), "hex", -1,
                              SQLITE_UTF8, NULL,
                              &sqlite3_hex_fn,
                              NULL, NULL) == 0);
#endif

  // register any functions we're going to use
  I(sqlite3_create_function(sql(), "gunzip", -1,
                           SQLITE_UTF8, NULL,
                           &sqlite3_gunzip_fn,
                           NULL, NULL) == 0);
}

void
database_impl::get_certs(vector<cert> & certs,
                         string const & table)
{
  results res;
  query q("SELECT revision_id, name, value, keypair_id, signature FROM " + table);
  fetch(res, 5, any_rows, q);
  results_to_certs(res, certs);
}


void
database_impl::get_oldstyle_certs(id const & ident,
                                  vector<cert> & certs,
                                  string const & table)
{
  MM(ident);
  results res;
  query q("SELECT id, name, value, keypair, signature FROM " + table +
          " WHERE id = ?");

  fetch(res, 5, any_rows, q % blob(ident()));
  oldstyle_results_to_certs(res, certs);
}

void
database_impl::get_certs(id const & ident,
                         vector<cert> & certs,
                         string const & table)
{
  MM(ident);
  results res;
  query q("SELECT revision_id, name, value, keypair_id, signature FROM " + table +
          " WHERE revision_id = ?");

  fetch(res, 5, any_rows, q % blob(ident()));
  results_to_certs(res, certs);
}

void
database_impl::get_certs(cert_name const & name,
                         vector<cert> & certs,
                         string const & table)
{
  MM(name);
  results res;
  query q("SELECT revision_id, name, value, keypair_id, signature FROM " + table +
          " WHERE name = ?");
  fetch(res, 5, any_rows, q % text(name()));
  results_to_certs(res, certs);
}

void
database_impl::get_oldstyle_certs(cert_name const & name,
                                  vector<cert> & certs,
                                  string const & table)
{
  results res;
  query q("SELECT id, name, value, keypair, signature FROM " + table +
          " WHERE name = ?");
  fetch(res, 5, any_rows, q % text(name()));
  oldstyle_results_to_certs(res, certs);
}

void
database_impl::get_certs(id const & ident,
                         cert_name const & name,
                         vector<cert> & certs,
                         string const & table)
{
  results res;
  query q("SELECT revision_id, name, value, keypair_id, signature FROM " + table +
          " WHERE revision_id = ? AND name = ?");

  fetch(res, 5, any_rows,
        q % blob(ident())
          % text(name()));
  results_to_certs(res, certs);
}

void
database_impl::get_certs(cert_name const & name,
                         cert_value const & val,
                         vector<pair<id, cert> > & certs,
                         string const & table)
{
  results res;
  query q("SELECT revision_id, name, value, keypair_id, signature, hash FROM " + table +
          " WHERE name = ? AND value = ?");

  fetch(res, 6, any_rows,
        q % text(name())
          % blob(val()));
  results_to_certs(res, certs);
}


void
database_impl::get_certs(id const & ident,
                         cert_name const & name,
                         cert_value const & value,
                         vector<cert> & certs,
                         string const & table)
{
  results res;
  query q("SELECT revision_id, name, value, keypair_id, signature FROM " + table +
          " WHERE revision_id = ? AND name = ? AND value = ?");

  fetch(res, 5, any_rows,
        q % blob(ident())
          % text(name())
          % blob(value()));
  results_to_certs(res, certs);
}



bool
database::revision_cert_exists(cert const & cert)
{
  return imp->cert_exists(cert, "revision_certs");
}

bool
database::put_revision_cert(cert const & cert)
{
  if (revision_cert_exists(cert))
    {
      L(FL("revision cert on '%s' already exists in db")
        % cert.ident);
      return false;
    }

  if (!revision_exists(revision_id(cert.ident)))
    {
      W(F("cert revision '%s' does not exist in db")
        % cert.ident);
      W(F("dropping cert"));
      return false;
    }

  if (cert.name() == "branch")
    {
      string branch_name = cert.value();
      if (branch_name.find_first_of("?,*%%+{}[]!^") != string::npos ||
          branch_name.find_first_of('-') == 0)
        {
          W(F("The branch name\n"
              "  '%s'\n"
              "contains meta characters (one or more of '?,*%%+{}[]!^') or\n"
              "starts with a dash, which might cause malfunctions when used\n"
              "in a netsync branch pattern.\n\n"
              "If you want to undo this operation, please use the\n"
              "'%s local kill_certs' command to delete the particular branch\n"
              "cert and re-add a valid one.")
            % cert.value() % prog_name);
        }
    }

  imp->put_cert(cert, "revision_certs");

  if (cert.name() == "branch")
    {
      record_as_branch_leaf(cert.value, cert.ident);
    }

  imp->cert_stamper.note_change();
  return true;
}

void
database::record_as_branch_leaf(cert_value const & branch, revision_id const & rev)
{
  set<revision_id> parents;
  get_revision_parents(rev, parents);
  set<revision_id> current_leaves;
  get_branch_leaves(branch, current_leaves);

  set<revision_id>::const_iterator self = current_leaves.find(rev);
  if (self != current_leaves.end())
    return; // already recorded (must be adding a second branch cert)

  bool all_parents_were_leaves = true;
  bool some_ancestor_was_leaf = false;
  for (set<revision_id>::const_iterator p = parents.begin();
       p != parents.end(); ++p)
    {
      set<revision_id>::iterator l = current_leaves.find(*p);
      if (l == current_leaves.end())
        all_parents_were_leaves = false;
      else
        {
          some_ancestor_was_leaf = true;
          imp->execute(query("DELETE FROM branch_leaves "
                             "WHERE branch = ? AND revision_id = ?")
                       % blob(branch()) % blob(l->inner()()));
          current_leaves.erase(l);
        }
    }

  // This check is needed for this case:
  //
  //  r1 (branch1)
  //  |
  //  r2 (branch2)
  //  |
  //  r3 (branch1)

  if (!all_parents_were_leaves)
    {
      for (set<revision_id>::const_iterator r = current_leaves.begin();
           r != current_leaves.end(); ++r)
        {
          if (is_a_ancestor_of_b(*r, rev))
            {
              some_ancestor_was_leaf = true;
              imp->execute(query("DELETE FROM branch_leaves "
                                 "WHERE branch = ? AND revision_id = ?")
                           % blob(branch()) % blob(r->inner()()));
            }
        }
    }

  // are we really a leaf (ie, not an ancestor of an existing leaf)?
  //
  // see tests/branch_leaves_sync_bug for a scenario that requires this.
  if (!some_ancestor_was_leaf)
    {
      bool really_a_leaf = true;
      for (set<revision_id>::const_iterator r = current_leaves.begin();
           r != current_leaves.end(); ++r)
        {
          if (is_a_ancestor_of_b(rev, *r))
            {
              really_a_leaf = false;
              break;
            }
        }
      if (!really_a_leaf)
        return;
    }

  imp->execute(query("INSERT INTO branch_leaves(branch, revision_id) "
                     "VALUES (?, ?)")
               % blob(branch()) % blob(rev.inner()()));
}

outdated_indicator
database::get_revision_cert_nobranch_index(vector< pair<revision_id,
                                           pair<revision_id, key_id> > > & idx)
{
  // share some storage
  id::symtab id_syms;

  results res;
  imp->fetch(res, 3, any_rows,
             query("SELECT hash, revision_id, keypair_id "
                   "FROM revision_certs WHERE name != 'branch'"));

  idx.clear();
  idx.reserve(res.size());
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      idx.push_back(make_pair(revision_id((*i)[0], origin::database),
                              make_pair(revision_id((*i)[1], origin::database),
                                        key_id((*i)[2], origin::database))));
    }
  return imp->cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(vector<cert> & certs)
{
  imp->get_certs(certs, "revision_certs");
  return imp->cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(cert_name const & name,
                            vector<cert> & certs)
{
  imp->get_certs(name, certs, "revision_certs");
  return imp->cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(revision_id const & id,
                             cert_name const & name,
                             vector<cert> & certs)
{
  imp->get_certs(id.inner(), name, certs, "revision_certs");
  return imp->cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(revision_id const & id,
                             cert_name const & name,
                             cert_value const & val,
                             vector<cert> & certs)
{
  imp->get_certs(id.inner(), name, val, certs, "revision_certs");
  return imp->cert_stamper.get_indicator();
}

outdated_indicator
database::get_revisions_with_cert(cert_name const & name,
                                  cert_value const & val,
                                  set<revision_id> & revisions)
{
  revisions.clear();
  results res;
  query q("SELECT revision_id FROM revision_certs WHERE name = ? AND value = ?");
  imp->fetch(res, one_col, any_rows, q % text(name()) % blob(val()));
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    revisions.insert(revision_id((*i)[0], origin::database));
  return imp->cert_stamper.get_indicator();
}

outdated_indicator
database::get_branch_leaves(cert_value const & value,
                            set<revision_id> & revisions)
{
  revisions.clear();
  results res;
  query q("SELECT revision_id FROM branch_leaves WHERE branch = ?");
  imp->fetch(res, one_col, any_rows, q % blob(value()));
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    revisions.insert(revision_id((*i)[0], origin::database));
  return imp->cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(cert_name const & name,
                             cert_value const & val,
                             vector<pair<id, cert> > & certs)
{
  imp->get_certs(name, val, certs, "revision_certs");
  return imp->cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(revision_id const & id,
                             vector<cert> & certs)
{
  imp->get_certs(id.inner(), certs, "revision_certs");
  return imp->cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(revision_id const & ident,
                             vector<id> & ids)
{
  results res;
  imp->fetch(res, one_col, any_rows,
             query("SELECT hash "
                   "FROM revision_certs "
                   "WHERE revision_id = ?")
             % blob(ident.inner()()));
  ids.clear();
  for (size_t i = 0; i < res.size(); ++i)
    ids.push_back(id(res[i][0], origin::database));
  return imp->cert_stamper.get_indicator();
}

void
database::get_revision_cert(id const & hash,
                            cert & c)
{
  results res;
  vector<cert> certs;
  imp->fetch(res, 5, one_row,
             query("SELECT revision_id, name, value, keypair_id, signature "
                   "FROM revision_certs "
                   "WHERE hash = ?")
             % blob(hash()));
  imp->results_to_certs(res, certs);
  I(certs.size() == 1);
  c = certs[0];
}

bool
database::revision_cert_exists(revision_id const & hash)
{
  results res;
  vector<cert> certs;
  imp->fetch(res, one_col, any_rows,
             query("SELECT revision_id "
                   "FROM revision_certs "
                   "WHERE hash = ?")
             % blob(hash.inner()()));
  I(res.empty() || res.size() == 1);
  return (res.size() == 1);
}

// FIXME: the bogus-cert family of functions is ridiculous
// and needs to be replaced, or at least factored.
namespace {
  struct trust_value
  {
    set<key_id> good_sigs;
    set<key_id> bad_sigs;
    set<key_id> unknown_sigs;
  };

  // returns *one* of each trusted cert key/value
  // if two keys signed the same thing, we get two certs as input and
  // just pick one (assuming neither is invalid) to use in the output
  void
  erase_bogus_certs_internal(vector<cert> & certs,
                             database & db,
                             database::cert_trust_checker const & checker)
  {
    // sorry, this is a crazy data structure
    typedef tuple<id, cert_name, cert_value> trust_key;
    typedef map< trust_key, trust_value > trust_map;
    trust_map trust;

    for (vector<cert>::iterator i = certs.begin(); i != certs.end(); ++i)
      {
        trust_key key = trust_key(i->ident.inner(),
                                  i->name,
                                  i->value);
        trust_value & value = trust[key];
        switch (db.check_cert(*i))
          {
          case cert_ok:
            value.good_sigs.insert(i->key);
            break;
          case cert_bad:
            value.bad_sigs.insert(i->key);
            break;
          case cert_unknown:
            value.unknown_sigs.insert(i->key);
            break;
          }
      }

    certs.clear();

    for (trust_map::const_iterator i = trust.begin();
         i != trust.end(); ++i)
      {
        cert out(typecast_vocab<revision_id>(get<0>(i->first)),
                 get<1>(i->first), get<2>(i->first), key_id());
        if (!i->second.good_sigs.empty() &&
            checker(i->second.good_sigs,
                    get<0>(i->first),
                    get<1>(i->first),
                    get<2>(i->first)))
          {
            L(FL("trust function liked %d signers of %s cert on revision %s")
              % i->second.good_sigs.size()
              % get<1>(i->first)
              % get<0>(i->first));
            out.key = *i->second.good_sigs.begin();
            certs.push_back(out);
          }
        else
          {
            string txt;
            out.signable_text(txt);
            for (set<key_id>::const_iterator b = i->second.bad_sigs.begin();
                 b != i->second.bad_sigs.end(); ++b)
              {
                W(F("ignoring bad signature by '%s' on '%s'") % *b % txt);
              }
            for (set<key_id>::const_iterator u = i->second.unknown_sigs.begin();
                 u != i->second.unknown_sigs.end(); ++u)
              {
                W(F("ignoring unknown signature by '%s' on '%s'") % *u % txt);
              }
            W(F("trust function disliked %d signers of %s cert on revision %s")
              % i->second.good_sigs.size()
              % get<1>(i->first)
              % get<0>(i->first));
          }
      }
  }
  // the lua hook wants key_identity_info, but all that's been
  // pulled from the certs is key_id. So this is needed to translate.
  // use pointers for project and lua so bind() doesn't make copies
  bool check_revision_cert_trust(project_t const * const project,
                                 lua_hooks * const lua,
                                 set<key_id> const & signers,
                                 id const & hash,
                                 cert_name const & name,
                                 cert_value const & value)
  {
    set<key_identity_info> signer_identities;
    for (set<key_id>::const_iterator i = signers.begin();
         i != signers.end(); ++i)
      {
        key_identity_info identity;
        identity.id = *i;
        project->complete_key_identity_from_id(*lua, identity);
        signer_identities.insert(identity);
      }

    return lua->hook_get_revision_cert_trust(signer_identities,
                                             hash, name, value);
  }
  // and the lua hook for manifest trust checking wants a key_name
  bool check_manifest_cert_trust(database * const db,
                                 lua_hooks * const lua,
                                 set<key_id> const & signers,
                                 id const & hash,
                                 cert_name const & name,
                                 cert_value const & value)
  {
    set<key_name> signer_names;
    for (set<key_id>::const_iterator i = signers.begin();
         i != signers.end(); ++i)
      {
        key_name name;
        rsa_pub_key pub;
        db->get_pubkey(*i, name, pub);
        signer_names.insert(name);
      }

    return lua->hook_get_manifest_cert_trust(signer_names,
                                             hash, name, value);
  }
} // anonymous namespace

void
database::erase_bogus_certs(project_t const & project, vector<cert> & certs)
{
  erase_bogus_certs_internal(certs, *this,
                             boost::bind(&check_revision_cert_trust,
                                         &project, &this->lua, _1, _2, _3, _4));
}
void
database::erase_bogus_certs(vector<cert> & certs,
                            database::cert_trust_checker const & checker)
{
  erase_bogus_certs_internal(certs, *this, checker);
}

// These are only used by migration from old manifest-style ancestry, so we
// don't much worry that they are not perfectly typesafe.  Also, we know
// that the callers want bogus certs erased.

void
database::get_manifest_certs(manifest_id const & id, std::vector<cert> & certs)
{
  imp->get_oldstyle_certs(id.inner(), certs, "manifest_certs");
  erase_bogus_certs_internal(certs, *this,
                             boost::bind(&check_manifest_cert_trust,
                                         this, &this->lua, _1, _2, _3, _4));
}

void
database::get_manifest_certs(cert_name const & name, std::vector<cert> & certs)
{
  imp->get_oldstyle_certs(name, certs, "manifest_certs");
  erase_bogus_certs_internal(certs, *this,
                             boost::bind(&check_manifest_cert_trust,
                                         this, &this->lua, _1, _2, _3, _4));
}

// completions
void
database_impl::add_prefix_matching_constraint(string const & colname,
                                              string const & prefix,
                                              query & q)
{
  L(FL("add_prefix_matching_constraint for '%s'") % prefix);

  if (prefix.empty())
    q.sql_cmd += "1";  // always true
  else if (prefix.size() > constants::idlen)
    q.sql_cmd += "0"; // always false
  else
    {
      string lower_hex = prefix;
      if (lower_hex.size() < constants::idlen)
        lower_hex.append(constants::idlen - lower_hex.size(), '0');
      string lower_bound = decode_hexenc(lower_hex, origin::internal);

      string upper_hex = prefix;
      if (upper_hex.size() < constants::idlen)
        upper_hex.append(constants::idlen - upper_hex.size(), 'f');
      string upper_bound = decode_hexenc(upper_hex, origin::internal);

      if (global_sanity.debug_p())
        L(FL("prefix_matcher: lower bound ('%s') and upper bound ('%s')")
          % encode_hexenc(lower_bound, origin::internal)
          % encode_hexenc(upper_bound, origin::internal));

      q.sql_cmd += colname + " BETWEEN ? AND ?";
      q.args.push_back(blob(lower_bound));
      q.args.push_back(blob(upper_bound));
    }
}

void
database::complete(string const & partial,
                   set<revision_id> & completions)
{
  results res;
  completions.clear();
  query q("SELECT id FROM revisions WHERE ");

  imp->add_prefix_matching_constraint("id", partial, q);
  imp->fetch(res, 1, any_rows, q);

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(revision_id(res[i][0], origin::database));
}


void
database::complete(string const & partial,
                   set<file_id> & completions)
{
  results res;
  completions.clear();

  query q("SELECT id FROM files WHERE ");
  imp->add_prefix_matching_constraint("id", partial, q);
  imp->fetch(res, 1, any_rows, q);

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(file_id(res[i][0], origin::database));

  res.clear();

  q = query("SELECT id FROM file_deltas WHERE ");
  imp->add_prefix_matching_constraint("id", partial, q);
  imp->fetch(res, 1, any_rows, q);

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(file_id(res[i][0], origin::database));
}

void
database::complete(string const & partial,
                   set< pair<key_id, utf8 > > & completions)
{
  results res;
  completions.clear();
  query q("SELECT id, name FROM public_keys WHERE ");

  imp->add_prefix_matching_constraint("id", partial, q);
  imp->fetch(res, 2, any_rows, q);

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(make_pair(key_id(res[i][0], origin::database),
                                 utf8(res[i][1], origin::database)));
}

// revision selectors

void
database::select_parent(string const & partial,
                        set<revision_id> & completions)
{
  results res;
  completions.clear();

  query q("SELECT DISTINCT parent FROM revision_ancestry WHERE ");
  imp->add_prefix_matching_constraint("child", partial, q);
  imp->fetch(res, 1, any_rows, q);

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(revision_id(res[i][0], origin::database));
}

void
database::select_cert(string const & certname,
                      set<revision_id> & completions)
{
  results res;
  completions.clear();

  imp->fetch(res, 1, any_rows,
             query("SELECT DISTINCT revision_id FROM revision_certs WHERE name = ?")
             % text(certname));

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(revision_id(res[i][0], origin::database));
}

void
database::select_cert(string const & certname, string const & certvalue,
                      set<revision_id> & completions)
{
  results res;
  completions.clear();

  imp->fetch(res, 1, any_rows,
             query("SELECT DISTINCT revision_id FROM revision_certs"
                   " WHERE name = ? AND CAST(value AS TEXT) GLOB ?")
             % text(certname) % text(certvalue));

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(revision_id(res[i][0], origin::database));
}

void
database::select_author_tag_or_branch(string const & partial,
                                      set<revision_id> & completions)
{
  results res;
  completions.clear();

  string pattern = partial + "*";

  imp->fetch(res, 1, any_rows,
             query("SELECT DISTINCT revision_id FROM revision_certs"
                   " WHERE (name=? OR name=? OR name=?)"
                   " AND CAST(value AS TEXT) GLOB ?")
             % text(author_cert_name()) % text(tag_cert_name())
             % text(branch_cert_name()) % text(pattern));

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(revision_id(res[i][0], origin::database));
}

void
database::select_date(string const & date, string const & comparison,
                      set<revision_id> & completions)
{
  results res;
  completions.clear();

  query q;
  q.sql_cmd = ("SELECT DISTINCT revision_id FROM revision_certs "
               "WHERE name = ? AND CAST(value AS TEXT) ");
  q.sql_cmd += comparison;
  q.sql_cmd += " ?";

  imp->fetch(res, 1, any_rows,
             q % text(date_cert_name()) % text(date));
  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(revision_id(res[i][0], origin::database));
}

void
database::select_key(key_id const & id, set<revision_id> & completions)
{
  results res;
  completions.clear();

  imp->fetch(res, 1, any_rows,
             query("SELECT DISTINCT revision_id FROM revision_certs"
                   " WHERE keypair_id = ?")
             % blob(id.inner()()));

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(revision_id(res[i][0], origin::database));
}

// epochs

void
database::get_epochs(map<branch_uid, epoch_data> & epochs)
{
  epochs.clear();
  results res;
  imp->fetch(res, 2, any_rows, query("SELECT branch, epoch FROM branch_epochs"));
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      branch_uid decoded(idx(*i, 0), origin::database);
      I(epochs.find(decoded) == epochs.end());
      epochs.insert(make_pair(decoded,
                              epoch_data(idx(*i, 1),
                                         origin::database)));
    }
}

void
database::get_epoch(epoch_id const & eid,
                    branch_uid & branch, epoch_data & epo)
{
  I(epoch_exists(eid));
  results res;
  imp->fetch(res, 2, any_rows,
             query("SELECT branch, epoch FROM branch_epochs"
                   " WHERE hash = ?")
             % blob(eid.inner()()));
  I(res.size() == 1);
  branch = branch_uid(idx(idx(res, 0), 0), origin::database);
  epo = epoch_data(idx(idx(res, 0), 1), origin::database);
}

bool
database::epoch_exists(epoch_id const & eid)
{
  results res;
  imp->fetch(res, one_col, any_rows,
             query("SELECT hash FROM branch_epochs WHERE hash = ?")
             % blob(eid.inner()()));
  I(res.size() == 1 || res.empty());
  return res.size() == 1;
}

void
database::set_epoch(branch_uid const & branch, epoch_data const & epo)
{
  epoch_id eid;
  epoch_hash_code(branch, epo, eid);
  I(epo.inner()().size() == constants::epochlen_bytes);
  imp->execute(query("INSERT OR REPLACE INTO branch_epochs VALUES(?, ?, ?)")
               % blob(eid.inner()())
               % blob(branch())
               % blob(epo.inner()()));
}

void
database::clear_epoch(branch_uid const & branch)
{
  imp->execute(query("DELETE FROM branch_epochs WHERE branch = ?")
               % blob(branch()));
}

bool
database::check_integrity()
{
  results res;
  imp->fetch(res, one_col, any_rows, query("PRAGMA integrity_check"));
  I(res.size() == 1);
  I(res[0].size() == 1);

  return res[0][0] == "ok";
}

// vars

void
database::get_vars(map<var_key, var_value> & vars)
{
  vars.clear();
  results res;
  imp->fetch(res, 3, any_rows, query("SELECT domain, name, value FROM db_vars"));
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      var_domain domain(idx(*i, 0), origin::database);
      var_name name(idx(*i, 1), origin::database);
      var_value value(idx(*i, 2), origin::database);
      I(vars.find(make_pair(domain, name)) == vars.end());
      vars.insert(make_pair(make_pair(domain, name), value));
    }
}

void
database::get_var(var_key const & key, var_value & value)
{
  // FIXME: sillyly inefficient.  Doesn't really matter, though.
  map<var_key, var_value> vars;
  get_vars(vars);
  map<var_key, var_value>::const_iterator i = vars.find(key);
  I(i != vars.end());
  value = i->second;
}

bool
database::var_exists(var_key const & key)
{
  // FIXME: sillyly inefficient.  Doesn't really matter, though.
  map<var_key, var_value> vars;
  get_vars(vars);
  map<var_key, var_value>::const_iterator i = vars.find(key);
  return i != vars.end();
}

void
database::set_var(var_key const & key, var_value const & value)
{
  imp->execute(query("INSERT OR REPLACE INTO db_vars VALUES(?, ?, ?)")
               % text(key.first())
               % blob(key.second())
               % blob(value()));
}

void
database::clear_var(var_key const & key)
{
  imp->execute(query("DELETE FROM db_vars WHERE domain = ? AND name = ?")
               % text(key.first())
               % blob(key.second()));
}

#define KNOWN_WORKSPACES_KEY                        \
  var_key(make_pair(                                \
    var_domain("database", origin::internal),       \
    var_name("known-workspaces", origin::internal)  \
  ))

void
database::register_workspace(system_path const & workspace)
{
  var_value val;
  if (var_exists(KNOWN_WORKSPACES_KEY))
    get_var(KNOWN_WORKSPACES_KEY, val);

  vector<string> workspaces;
  split_into_lines(val(), workspaces);

  vector<string>::iterator pos =
    find(workspaces.begin(),
         workspaces.end(),
         workspace.as_internal());
  if (pos == workspaces.end())
    workspaces.push_back(workspace.as_internal());

  string ws;
  join_lines(workspaces, ws);

  set_var(KNOWN_WORKSPACES_KEY, var_value(ws, origin::internal));
}

void
database::unregister_workspace(system_path const & workspace)
{
  if (var_exists(KNOWN_WORKSPACES_KEY))
    {
      var_value val;
      get_var(KNOWN_WORKSPACES_KEY, val);

      vector<string> workspaces;
      split_into_lines(val(), workspaces);

      vector<string>::iterator pos =
        find(workspaces.begin(),
             workspaces.end(),
             workspace.as_internal());
      if (pos != workspaces.end())
        workspaces.erase(pos);

      string ws;
      join_lines(workspaces, ws);

      set_var(KNOWN_WORKSPACES_KEY, var_value(ws, origin::internal));
    }
}

void
database::get_registered_workspaces(vector<system_path> & workspaces)
{
  if (var_exists(KNOWN_WORKSPACES_KEY))
    {
      var_value val;
      get_var(KNOWN_WORKSPACES_KEY, val);

      vector<string> paths;
      split_into_lines(val(), paths);

      for (vector<string>::const_iterator i = paths.begin();
           i != paths.end(); ++i)
        {
          system_path workspace_path(*i, origin::database);
          workspaces.push_back(workspace_path);
        }
    }
}

void
database::set_registered_workspaces(vector<system_path> const & workspaces)
{
  vector<string> paths;
  for (vector<system_path>::const_iterator i = workspaces.begin();
       i != workspaces.end(); ++i)
    {
      paths.push_back((*i).as_internal());
    }

  string ws;
  join_lines(paths, ws);
  set_var(KNOWN_WORKSPACES_KEY, var_value(ws, origin::internal));
}

#undef KNOWN_WORKSPACES_KEY

// branches

outdated_indicator
database::get_branches(vector<string> & names)
{
    results res;
    query q("SELECT DISTINCT branch FROM branch_leaves");
    string cert_name = "branch";
    imp->fetch(res, one_col, any_rows, q);
    for (size_t i = 0; i < res.size(); ++i)
      {
        names.push_back(res[i][0]);
      }
    return imp->cert_stamper.get_indicator();
}

outdated_indicator
database::get_branches(globish const & glob,
                       vector<string> & names)
{
    results res;
    query q("SELECT DISTINCT value FROM revision_certs WHERE name = ?");
    string cert_name = "branch";
    imp->fetch(res, one_col, any_rows, q % text(cert_name));
    for (size_t i = 0; i < res.size(); ++i)
      {
        if (glob.matches(res[i][0]))
          names.push_back(res[i][0]);
      }
    return imp->cert_stamper.get_indicator();
}

void
database::get_roster(revision_id const & rev_id,
                     roster_t & roster)
{
  marking_map mm;
  get_roster(rev_id, roster, mm);
}

void
database::get_roster(revision_id const & rev_id,
                     roster_t & roster,
                     marking_map & marking)
{
  if (rev_id.inner()().empty())
    {
      roster = roster_t();
      marking = marking_map();
      return;
    }

  cached_roster cr;
  get_roster(rev_id, cr);
  roster = *cr.first;
  marking = *cr.second;
}

void
database::get_roster(revision_id const & rev_id, cached_roster & cr)
{
  get_roster_version(rev_id, cr);
  I(cr.first);
  I(cr.second);
}

void
database::put_roster(revision_id const & rev_id,
                     revision_t const & rev,
                     roster_t_cp const & roster,
                     marking_map_cp const & marking)
{
  I(roster);
  I(marking);
  MM(rev_id);

  transaction_guard guard(*this);

  // Our task is to add this roster, and deltify all the incoming edges (if
  // they aren't already).

  imp->roster_cache.insert_dirty(rev_id, make_pair(roster, marking));

  // Now do what deltify would do if we bothered
  size_t num_edges = rev.edges.size();
  for (edge_map::const_iterator i = rev.edges.begin();
       i != rev.edges.end(); ++i)
    {
      revision_id old_rev = edge_old_revision(*i);
      if (null_id(old_rev))
        continue;
      if (imp->roster_base_stored(old_rev))
        {
          cached_roster cr;
          get_roster_version(old_rev, cr);
          roster_delta reverse_delta;
          cset const & changes = edge_changes(i);
          delta_rosters(*roster, *marking,
                        *(cr.first), *(cr.second),
                        reverse_delta,
                        num_edges > 1 ? 0 : &changes);
          if (imp->roster_cache.exists(old_rev))
            imp->roster_cache.mark_clean(old_rev);
          imp->drop(old_rev.inner(), "rosters");
          imp->put_roster_delta(old_rev, rev_id, reverse_delta);
        }
    }
  guard.commit();
}

// for get_uncommon_ancestors
struct rev_height_graph : rev_graph
{
  rev_height_graph(database & db) : db(db) {}
  virtual void get_parents(revision_id const & rev, set<revision_id> & parents) const
  {
    db.get_revision_parents(rev, parents);
  }
  virtual void get_children(revision_id const & rev, set<revision_id> & parents) const
  {
    // not required
    I(false);
  }
  virtual void get_height(revision_id const & rev, rev_height & h) const
  {
    db.get_rev_height(rev, h);
  }

  database & db;
};

void
database::get_uncommon_ancestors(revision_id const & a,
                                 revision_id const & b,
                                 set<revision_id> & a_uncommon_ancs,
                                 set<revision_id> & b_uncommon_ancs)
{

  rev_height_graph graph(*this);
  ::get_uncommon_ancestors(a, b, graph, a_uncommon_ancs, b_uncommon_ancs);
}

node_id
database::next_node_id()
{
  transaction_guard guard(*this);
  results res;

  // We implement this as a fixed db var.
  imp->fetch(res, one_col, any_rows,
             query("SELECT node FROM next_roster_node_number"));

  u64 n = 1;
  if (res.empty())
    {
      imp->execute(query("INSERT INTO next_roster_node_number VALUES(1)"));
    }
  else
    {
      I(res.size() == 1);
      n = lexical_cast<u64>(res[0][0]);
      ++n;
      imp->execute(query("UPDATE next_roster_node_number SET node = ?")
                   % text(lexical_cast<string>(n)));
    }
  guard.commit();
  return static_cast<node_id>(n);
}

void
database_impl::check_filename()
{
  E(!filename.empty(), origin::user, F("no database specified"));
}


void
database_impl::check_db_exists()
{
  switch (get_path_status(filename))
    {
    case path::file:
      return;

    case path::nonexistent:
      E(false, origin::user, F("database %s does not exist") % filename);

    case path::directory:
      if (directory_is_workspace(filename))
        {
          options opts;
          workspace::get_options(filename, opts);
          E(opts.dbname.as_internal().empty(), origin::user,
            F("%s is a workspace, not a database\n"
              "(did you mean %s?)") % filename % opts.dbname);
        }
      E(false, origin::user,
        F("%s is a directory, not a database") % filename);
    }
}

void
database_impl::check_db_nonexistent()
{
  require_path_is_nonexistent(filename,
                              F("database %s already exists")
                              % filename);

  system_path journal(filename.as_internal() + "-journal", origin::internal);
  require_path_is_nonexistent(journal,
                              F("existing (possibly stale) journal file '%s' "
                                "has same stem as new database '%s'\n"
                                "cancelling database creation")
                              % journal % filename);

}

void
database_impl::open()
{
  I(!__sql);

  string to_open;
  if (type == memory_db)
    to_open = memory_db_identifier;
  else
    {
      system_path base_dir = filename.dirname();
      if (!directory_exists(base_dir))
        mkdir_p(base_dir);
      to_open = filename.as_external();
    }

  if (sqlite3_open(to_open.c_str(), &__sql) == SQLITE_NOMEM)
    throw std::bad_alloc();

  I(__sql);
  assert_sqlite3_ok(__sql);
}

void
database_impl::close()
{
  I(__sql);

  sqlite3_close(__sql);
  __sql = 0;

  I(!__sql);
}

// transaction guards

conditional_transaction_guard::~conditional_transaction_guard()
{
  if (!acquired)
    return;
  if (committed)
    db.imp->commit_transaction();
  else
    db.imp->rollback_transaction();
}

void
conditional_transaction_guard::acquire()
{
  I(!acquired);
  acquired = true;
  db.imp->begin_transaction(exclusive);
}

void
conditional_transaction_guard::do_checkpoint()
{
  I(acquired);
  db.imp->commit_transaction();
  db.imp->begin_transaction(exclusive);
  checkpointed_calls = 0;
  checkpointed_bytes = 0;
}

void
conditional_transaction_guard::maybe_checkpoint(size_t nbytes)
{
  I(acquired);
  checkpointed_calls += 1;
  checkpointed_bytes += nbytes;
  if (checkpointed_calls >= checkpoint_batch_size
      || checkpointed_bytes >= checkpoint_batch_bytes)
    do_checkpoint();
}

void
conditional_transaction_guard::commit()
{
  I(acquired);
  committed = true;
}

void
database_path_helper::get_database_path(options const & opts, system_path & path)
{
  if (!opts.dbname_given ||
      (opts.dbname.as_internal().empty() &&
       opts.dbname_alias.empty()))
    {
      L(FL("no database option given or options empty"));
      return;
    }

  if (opts.dbname_type == unmanaged_db)
    {
      path = opts.dbname;
      return;
    }

  if (opts.dbname_type == memory_db)
    {
      return;
    }

  I(opts.dbname_type == managed_db);

  path_component basename;
  validate_and_clean_alias(opts.dbname_alias, basename);

  vector<system_path> candidates;
  vector<system_path> search_paths;

  E(lua.hook_get_default_database_locations(search_paths) && search_paths.size() > 0,
    origin::user, F("could not query default database locations"));

  for (vector<system_path>::const_iterator i = search_paths.begin();
     i != search_paths.end(); ++i)
    {
      if (file_exists((*i) / basename))
        {
          candidates.push_back((*i) / basename);
          continue;
        }
    }

  MM(candidates);

  // if we did not found the database anywhere, use the first
  // available default path to possible save it there
  if (candidates.size() == 0)
    {
      path = (*search_paths.begin()) / basename;
      L(FL("no path expansions found for '%s', using '%s'")
          % opts.dbname_alias % path);
      return;
    }

  if (candidates.size() == 1)
    {
      path = (*candidates.begin());
      L(FL("one path expansion found for '%s': '%s'")
          % opts.dbname_alias % path);
      return;
    }

  if (candidates.size() > 1)
    {
      string err =
        (F("the database alias '%s' has multiple ambiguous expansions:")
         % opts.dbname_alias).str();

      for (vector<system_path>::const_iterator i = candidates.begin();
           i != candidates.end(); ++i)
        err += ("\n  " + (*i).as_internal());

      E(false, origin::user, i18n_format(err));
    }
}

void
database_path_helper::maybe_set_default_alias(options & opts)
{
  if (opts.dbname_given && (
       !opts.dbname.as_internal().empty() ||
       !opts.dbname_alias.empty()))
    {
      return;
    }

  string alias;
  E(lua.hook_get_default_database_alias(alias) && !alias.empty(),
    origin::user, F("could not query default database alias"));

  opts.dbname_given = true;
  opts.dbname_alias = alias;
  opts.dbname_type = managed_db;
}

void
database_path_helper::validate_and_clean_alias(string const & alias, path_component & pc)
{
  E(alias.find(':') == 0, origin::system,
    F("invalid database alias '%s': does not start with a colon") % alias);

  string pure_alias = alias.substr(1);
  E(pure_alias.size() > 0, origin::system,
    F("invalid database alias '%s': must not be empty") % alias);

  size_t pos = pure_alias.rfind('.');
  if (pos == string::npos || pure_alias.substr(pos + 1) != "mtn")
    pure_alias += ".mtn";

  try
    {
      pc = path_component(pure_alias, origin::system);
    }
  catch (...)
    {
      E(false, origin::system,
        F("invalid database alias '%s': does contain invalid characters") % alias);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
