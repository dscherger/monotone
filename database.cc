// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <deque>
#include <fstream>
#include <iterator>
#include <list>
#include <set>
#include <sstream>
#include <vector>

#include <string.h>

#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include <sqlite3.h>

#include "app_state.hh"
#include "cert.hh"
#include "cleanup.hh"
#include "constants.hh"
#include "database.hh"
#include "hash_map.hh"
#include "keys.hh"
#include "sanity.hh"
#include "schema_migration.hh"
#include "transforms.hh"
#include "ui.hh"
#include "vocab.hh"
#include "xdelta.hh"
#include "epoch.hh"

// defined in schema.sql, converted to header:
#include "schema.h"

// defined in views.sql, converted to header:
#include "views.h"

// this file defines a public, typed interface to the database.
// the database class encapsulates all knowledge about sqlite,
// the schema, and all SQL statements used to access the schema.
//
// see file schema.sql for the text of the schema.

using boost::shared_ptr;
using boost::lexical_cast;
using namespace std;

int const one_row = 1;
int const one_col = 1;
int const any_rows = -1;
int const any_cols = -1;

namespace
{
  // track all open databases for close_all_databases() handler
  set<sqlite3*> sql_contexts;
}

extern "C" {
// some wrappers to ease migration
  const char *sqlite3_value_text_s(sqlite3_value *v);
  const char *sqlite3_column_text_s(sqlite3_stmt*, int col);
}

database::database(system_path const & fn) :
  filename(fn),
  // nb. update this if you change the schema. unfortunately we are not
  // using self-digesting schemas due to comment irregularities and
  // non-alphabetic ordering of tables in sql source files. we could create
  // a temporary db, write our intended schema into it, and read it back,
  // but this seems like it would be too rude. possibly revisit this issue.
  schema("9d2b5d7b86df00c30ac34fe87a3c20f1195bb2df"),
  __sql(NULL),
  transaction_level(0)
{}

void 
database::check_schema()
{
  string db_schema_id;  
  calculate_schema_id (__sql, db_schema_id);
  N (schema == db_schema_id,
     F("layout of database %s doesn't match this version of monotone\n"
       "wanted schema %s, got %s\n"
       "try 'monotone db migrate' to upgrade\n"
       "(this is irreversible; you may want to make a backup copy first)")
     % filename % schema % db_schema_id);
}

void
database::check_rosterified()
{
  results res;
  string rosters_query = "SELECT 1 FROM rosters LIMIT 1";
  string revisions_query = "SELECT 1 FROM revisions LIMIT 1";

  fetch(res, one_col, any_rows, revisions_query.c_str());
  if (res.size() > 0)
    {
      fetch(res, one_col, any_rows, rosters_query.c_str());
      N (res.size() != 0,
         F("database %s contains revisions but no rosters\n"
           "try 'monotone db rosterify' to add rosters\n"
           "(this is irreversible; you may want to make a backup copy first)")
         % filename);
    }
}

// sqlite3_value_text gives a const unsigned char * but most of the time
// we need a signed char
const char *
sqlite3_value_text_s(sqlite3_value *v)
{  
  return (const char *)(sqlite3_value_text(v));
}

const char *
sqlite3_column_text_s(sqlite3_stmt *stmt, int col)
{
  return (const char *)(sqlite3_column_text(stmt, col));
}

static void 
sqlite3_unbase64_fn(sqlite3_context *f, int nargs, sqlite3_value ** args)
{
  if (nargs != 1)
    {
      sqlite3_result_error(f, "need exactly 1 arg to unbase64()", -1);
      return;
    }
  data decoded;
  decode_base64(base64<data>(string(sqlite3_value_text_s(args[0]))), decoded);
  sqlite3_result_blob(f, decoded().c_str(), decoded().size(), SQLITE_TRANSIENT);
}

static void
sqlite3_unpack_fn(sqlite3_context *f, int nargs, sqlite3_value ** args)
{
  if (nargs != 1)
    {
      sqlite3_result_error(f, "need exactly 1 arg to unpack()", -1);
      return;
    }
  data unpacked;
  unpack(base64< gzip<data> >(string(sqlite3_value_text_s(args[0]))), unpacked);
  sqlite3_result_blob(f, unpacked().c_str(), unpacked().size(), SQLITE_TRANSIENT);
}

void
database::set_app(app_state * app)
{
  __app = app;
}

static void
check_sqlite_format_version(system_path const & filename)
{
  // sqlite 3 files begin with this constant string
  // (version 2 files begin with a different one)
  std::string version_string("SQLite format 3");

  std::ifstream file(filename.as_external().c_str());
  N(file, F("unable to probe database version in file %s") % filename);

  for (std::string::const_iterator i = version_string.begin();
       i != version_string.end(); ++i)
    {
      char c;
      file.get(c);
      N(c == *i, F("database %s is not an sqlite version 3 file, "
                   "try dump and reload") % filename);
    }
}


static void
assert_sqlite3_ok(sqlite3 *s) 
{
  int errcode = sqlite3_errcode(s);

  if (errcode == SQLITE_OK) return;
  
  const char * errmsg = sqlite3_errmsg(s);

  // sometimes sqlite is not very helpful
  // so we keep a table of errors people have gotten and more helpful versions
  if (errcode != SQLITE_OK)
    {
      // first log the code so we can find _out_ what the confusing code
      // was... note that code does not uniquely identify the errmsg, unlike
      // errno's.
      L(FL("sqlite error: %d: %s") % errcode % errmsg);
    }
  std::string auxiliary_message = "";
  if (errcode == SQLITE_ERROR)
    {
      auxiliary_message += _("make sure database and containing directory are writeable");
    }
  // if the last message is empty, the \n will be stripped off too
  E(errcode == SQLITE_OK,
    // kind of string surgery to avoid ~duplicate strings
    F("sqlite error: %d: %s\n%s") % errcode % errmsg % auxiliary_message);
}

struct sqlite3 * 
database::sql(bool init)
{
  if (! __sql)
    {
      check_filename();

      if (! init)
        {
          check_db_exists();
          check_sqlite_format_version(filename);
        }

      open();

      if (init)
        {
          sqlite3_exec(__sql, schema_constant, NULL, NULL, NULL);
          assert_sqlite3_ok(__sql);
        }

      check_schema();
      install_functions(__app);
      install_views();
    }
  return __sql;
}

void 
database::initialize()
{
  if (__sql)
    throw oops("cannot initialize database while it is open");

  require_path_is_nonexistent(filename,
                              F("could not initialize database: %s: already exists") 
                              % filename);

  system_path journal(filename.as_internal() + "-journal");
  require_path_is_nonexistent(journal,
                              F("existing (possibly stale) journal file '%s' "
                                "has same stem as new database '%s'\n"
                                "cancelling database creation")
                              % journal % filename);

  sqlite3 *s = sql(true);
  I(s != NULL);
}


struct 
dump_request
{
  dump_request() {};
  struct sqlite3 *sql;
  string table_name;
  ostream *out;
};

static int 
dump_row_cb(dump_request *dump, sqlite3_stmt *stmt)
{
  I(dump->out != NULL);
  *(dump->out) << boost::format("INSERT INTO %s VALUES(") % dump->table_name;
  unsigned n = sqlite3_data_count(stmt);
  for (unsigned i = 0; i < n; ++i)
    {
      if (i != 0)
        *(dump->out) << ',';

      if (sqlite3_column_type(stmt, i) == SQLITE_BLOB)
        {
          *(dump->out) << "X'";
          const char *val = (const char*) sqlite3_column_blob(stmt, i);
          int bytes = sqlite3_column_bytes(stmt, i);
          *(dump->out) << encode_hexenc(std::string(val,val+bytes));
          *(dump->out) << "'";
        }
      else 
        {
          const unsigned char *val = sqlite3_column_text(stmt, i);
          if (val == NULL)
            *(dump->out) << "NULL";
          else
            {
              *(dump->out) << "'";
              for (const unsigned char *cp = val; *cp; ++cp)
                {
                  if (*cp == '\'')
                    *(dump->out) << "''";
                  else
                    *(dump->out) << *cp;
                }
              *(dump->out) << "'";
            }
        }
    }
  *(dump->out) << ");\n";  
  return 0;
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
  dump->table_name = string(vals[0]);
  string query = "SELECT * FROM " + string(vals[0]);
  sqlite3_stmt *stmt=0;
  sqlite3_prepare(dump->sql, query.c_str(), -1, &stmt, NULL);
  assert_sqlite3_ok(dump->sql);

  int stepresult=SQLITE_DONE;
  do
  { stepresult=sqlite3_step(stmt);
    I(stepresult==SQLITE_DONE || stepresult==SQLITE_ROW);
    if (stepresult==SQLITE_ROW) 
      dump_row_cb(dump, stmt);
  } while (stepresult==SQLITE_ROW);

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

void 
database::dump(ostream & out)
{
  transaction_guard guard(*this);
  dump_request req;
  req.out = &out;
  req.sql = sql();
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
  out << "COMMIT;\n";
  guard.commit();
}

void 
database::load(istream & in)
{
  char buf[constants::bufsz];
  string tmp;

  check_filename();

  require_path_is_nonexistent(filename,
                              F("cannot create %s; it already exists") % filename);

  open();

  while(in)
    {
      in.read(buf, constants::bufsz);
      tmp.append(buf, in.gcount());

      const char* last_statement = 0;
      sqlite3_complete_last(tmp.c_str(), &last_statement);
      if (last_statement == 0)
        continue;
      string::size_type len = last_statement + 1 - tmp.c_str();
      sqlite3_exec(__sql, tmp.substr(0, len).c_str(), NULL, NULL, NULL);
      tmp.erase(0, len);
    }

  if (!tmp.empty())
    sqlite3_exec(__sql, tmp.c_str(), NULL, NULL, NULL);
  assert_sqlite3_ok(__sql);
}


void 
database::debug(string const & sql, ostream & out)
{
  results res;
  fetch(res, any_cols, any_rows, sql.c_str());
  out << "'" << sql << "' -> " << res.size() << " rows\n" << endl;
  for (size_t i = 0; i < res.size(); ++i)
    {
      for (size_t j = 0; j < res[i].size(); ++j)
        {
          if (j != 0)
            out << " | ";
          out << res[i][j];
        }
      out << endl;
    }
}


namespace
{
  unsigned long
  add(unsigned long count, unsigned long & total)
  {
    total += count;
    return count;
  }
}

void 
database::info(ostream & out)
{
  string id;
  calculate_schema_id(sql(), id);

  unsigned long total = 0UL;

#define SPACE_USAGE(TABLE, COLS) add(space_usage(TABLE, COLS), total)

  out << \
    F("schema version    : %s\n"
      "counts:\n"
      "  full rosters    : %u\n"
      "  roster deltas   : %u\n"
      "  full files      : %u\n"
      "  file deltas     : %u\n"
      "  revisions       : %u\n"
      "  ancestry edges  : %u\n"
      "  certs           : %u\n"
      "bytes:\n"
      "  full rosters    : %u\n"
      "  roster deltas   : %u\n"
      "  full files      : %u\n"
      "  file deltas     : %u\n"
      "  revisions       : %u\n"
      "  cached ancestry : %u\n"
      "  certs           : %u\n"
      "  total           : %u\n"
      )
    % id
    // counts
    % count("rosters")
    % count("roster_deltas")
    % count("files")
    % count("file_deltas")
    % count("revisions")
    % count("revision_ancestry")
    % count("revision_certs")
    // bytes
    % SPACE_USAGE("rosters", "id || data")
    % SPACE_USAGE("roster_deltas", "id || base || delta")
    % SPACE_USAGE("files", "id || data")
    % SPACE_USAGE("file_deltas", "id || base || delta")
    % SPACE_USAGE("revisions", "id || data")
    % SPACE_USAGE("revision_ancestry", "parent || child")
    % SPACE_USAGE("revision_certs", "hash || id || name || value || keypair || signature")
    % total;

#undef SPACE_USAGE
}

void
database::version(ostream & out)
{
  string id;

  check_filename();
  check_db_exists();
  open();

  calculate_schema_id(__sql, id);

  close();

  out << F("database schema version: %s") % id << endl;
}

void
database::migrate()
{
  check_filename();
  check_db_exists();
  open();

  migrate_monotone_schema(__sql, __app);

  close();
}

void 
database::ensure_open()
{
  sqlite3 *s = sql();
  I(s != NULL);
}

database::~database() 
{
  L(FL("statement cache statistics\n"));
  L(FL("prepared %d statements\n") % statement_cache.size());

  for (map<string, statement>::const_iterator i = statement_cache.begin(); 
       i != statement_cache.end(); ++i)
    L(FL("%d executions of %s\n") % i->second.count % i->first);
  // trigger destructors to finalize cached statements
  statement_cache.clear();

  close();
}

void 
database::execute(char const * query, ...)
{
  results res;
  va_list args;
  va_start(args, query);
  fetch(res, 0, 0, query, args);
  va_end(args);
}

void
database::execute(std::string const& query, std::vector<queryarg> const& args)
{ 
  results res;
  fetch(res, 0, 0, query, args);
}

void 
database::fetch(results & res, 
                int const want_cols, 
                int const want_rows, 
                char const * query, ...)
{
  va_list args;
  va_start(args, query);
  fetch(res, want_cols, want_rows, query, args);
  va_end(args);
}

database::statement &
database::prepare(char const * query,
                  int const want_cols)
{
  map<string, statement>::iterator i = statement_cache.find(query);
  if (i == statement_cache.end()) 
    {
      statement_cache.insert(make_pair(query, statement()));
      i = statement_cache.find(query);
      I(i != statement_cache.end());

      const char * tail;
      sqlite3_prepare(sql(), query, -1, i->second.stmt.paddr(), &tail);
      assert_sqlite3_ok(sql());
      L(FL("prepared statement %s\n") % query);

      // no support for multiple statements here
      E(*tail == 0, 
        F("multiple statements in query: %s\n") % query);
    }

  int ncol = sqlite3_column_count(i->second.stmt());

  E(want_cols == any_cols || want_cols == ncol, 
    F("wanted %d columns got %d in query: %s\n") % want_cols % ncol % query);

  return i->second;
}

// we need a vector<string> variant for binary transparency
void 
database::fetch(results & res, 
                int const want_cols, 
                int const want_rows, 
                char const * query, 
                va_list args)
{
  res.clear();
  res.resize(0);

  statement &stmt = prepare(query, want_cols);
  // bind parameters for this execution

  int params = sqlite3_bind_parameter_count(stmt.stmt());

  // profiling finds this logging to be quite expensive
  if (global_sanity.debug)
    L(FL("binding %d parameters for %s\n") % params % query);

  for (int param = 1; param <= params; param++)
    {
      char *value = va_arg(args, char *);

      // profiling finds this logging to be quite expensive
      if (global_sanity.debug)
        {
          string log = string(value);
          
          if (log.size() > constants::log_line_sz)
            log = log.substr(0, constants::log_line_sz);
          
          L(FL("binding %d with value '%s'\n") % param % log);
        }

      sqlite3_bind_text(stmt.stmt(), param, value, -1, SQLITE_TRANSIENT);
      assert_sqlite3_ok(sql());
    }
    
  fetch(stmt, res, want_rows, query);
}

void 
database::fetch(statement & stmt,
                results & res,
                int const want_rows,
                const char *query)
{
  // execute and process results

  int nrow = 0;
  int ncol = sqlite3_column_count(stmt.stmt());
  int rescode;

  for (rescode = sqlite3_step(stmt.stmt()); rescode == SQLITE_ROW; 
       rescode = sqlite3_step(stmt.stmt()))
    {
      vector<string> row;
      for (int col = 0; col < ncol; col++) 
        {
          const char * value = (const char*)sqlite3_column_blob(stmt.stmt(), col);
          int bytes = sqlite3_column_bytes(stmt.stmt(), col);
          E(value, F("null result in query: %s\n") % query);
          row.push_back(std::string(value,value+bytes));
          //L(FL("row %d col %d value='%s'\n") % nrow % col % value);
        }
      res.push_back(row);
    }
  
  if (rescode != SQLITE_DONE)
    assert_sqlite3_ok(sql());

  sqlite3_reset(stmt.stmt());
  assert_sqlite3_ok(sql());

  nrow = res.size();

  stmt.count++;

  E(want_rows == any_rows || want_rows == nrow,
    F("wanted %d rows got %s in query: %s\n") % want_rows % nrow % query);
}

void 
database::fetch(results & res, 
                int const want_cols, 
                int const want_rows, 
                std::string const& query, 
                std::vector<queryarg> const& args)
{
  res.clear();
  res.resize(0);

  statement &stmt = prepare(query.c_str(), want_cols);

  // bind parameters for this execution

  int params = sqlite3_bind_parameter_count(stmt.stmt());
  
  I(args.size()==size_t(params));

  L(FL("binding %d parameters for %s\n") % params % query);

  for (int param = 1; param <= params; param++)
    {
      string log = args[param-1];

      if (log.size() > constants::log_line_sz)
        log = log.substr(0, constants::log_line_sz);

      L(FL("binding %d with value '%s'\n") % param % log);

      // we can use SQLITE_STATIC since the array is destructed after
      // fetching the parameters (and sqlite3_reset)
      if (args[param-1].binary)
        sqlite3_bind_blob(stmt.stmt(), param, args[param-1].data(), args[param-1].size(), SQLITE_STATIC);
      else
        sqlite3_bind_text(stmt.stmt(), param, args[param-1].c_str(), -1, SQLITE_STATIC);
      assert_sqlite3_ok(sql());
    }
    
  fetch(stmt, res, want_rows, query.c_str());
}

// general application-level logic

void 
database::set_filename(system_path const & file)
{
  I(!__sql);
  filename = file;
}

void 
database::begin_transaction(bool exclusive) 
{
  if (transaction_level == 0)
    {
      if (exclusive)
        execute("BEGIN EXCLUSIVE");
      else
        execute("BEGIN DEFERRED");
      transaction_exclusive = exclusive;
    }
  else
    {
      E(!exclusive || transaction_exclusive, F("Attempt to start exclusive transaction within non-exclusive transaction."));
    }
  transaction_level++;
}

void 
database::commit_transaction()
{
  if (transaction_level == 1)
    execute("COMMIT");
  transaction_level--;
}

void 
database::rollback_transaction()
{
  if (transaction_level == 1)
    execute("ROLLBACK");
  transaction_level--;
}


bool 
database::exists(hexenc<id> const & ident,
                      string const & table)
{
  results res;
  string query = "SELECT id FROM " + table + " WHERE id = ?";
  fetch(res, one_col, any_rows, query.c_str(), ident().c_str());
  I((res.size() == 1) || (res.size() == 0));
  return res.size() == 1;
}


bool 
database::delta_exists(hexenc<id> const & ident,
                       string const & table)
{
  results res;
  string query = "SELECT id FROM " + table + " WHERE id = ?";
  fetch(res, one_col, any_rows, query.c_str(), ident().c_str());
  return res.size() > 0;
}

unsigned long
database::count(string const & table)
{
  results res;
  string query = "SELECT COUNT(*) FROM " + table;
  fetch(res, one_col, one_row, query.c_str());
  return lexical_cast<unsigned long>(res[0][0]);  
}

unsigned long
database::space_usage(string const & table, string const & concatenated_columns)
{
  results res;
  // COALESCE is required since SUM({empty set}) is NULL.
  // the sqlite docs for SUM suggest this as a workaround
  string query = "SELECT COALESCE(SUM(LENGTH(" + concatenated_columns + ")), 0) FROM " + table;
  fetch(res, one_col, one_row, query.c_str());
  return lexical_cast<unsigned long>(res[0][0]);
}

void
database::get_ids(string const & table, set< hexenc<id> > & ids) 
{
  results res;
  string query = "SELECT id FROM " + table;
  fetch(res, one_col, any_rows, query.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    {
      ids.insert(hexenc<id>(res[i][0]));
    }
}

void 
database::get(hexenc<id> const & ident,
              data & dat,
              string const & table)
{
  results res;
  string query = "SELECT data FROM " + table + " WHERE id = ?";
  fetch(res, one_col, one_row, query.c_str(), ident().c_str());

  // consistency check
  gzip<data> rdata(res[0][0]);
  data rdata_unpacked;
  decode_gzip(rdata,rdata_unpacked);

  hexenc<id> tid;
  calculate_ident(rdata_unpacked, tid);
  I(tid == ident);

  dat = rdata_unpacked;
}

void 
database::get_delta(hexenc<id> const & ident,
                    hexenc<id> const & base,
                    delta & del,
                    string const & table)
{
  I(ident() != "");
  I(base() != "");
  results res;
  string query = "SELECT delta FROM " + table + " WHERE id = ? AND base = ?";
  fetch(res, one_col, one_row, query.c_str(), 
        ident().c_str(), base().c_str());

  gzip<delta> del_packed(res[0][0]);
  decode_gzip(del_packed, del);
}

void 
database::put(hexenc<id> const & ident,
              data const & dat,
              string const & table)
{
  // consistency check
  I(ident() != "");
  hexenc<id> tid;
  calculate_ident(dat, tid);
  MM(ident);
  MM(tid);
  I(tid == ident);

  gzip<data> dat_packed;
  encode_gzip(dat, dat_packed);
  
  string insert = "INSERT INTO " + table + " VALUES(?, ?)";
  std::vector<queryarg> args;
  args.push_back(ident());
  args.push_back(queryarg(dat_packed(),true));
  execute(insert,args);
}
void 
database::put_delta(hexenc<id> const & ident,
                    hexenc<id> const & base,
                    delta const & del,
                    string const & table)
{
  // nb: delta schema is (id, base, delta)
  I(ident() != "");
  I(base() != "");

  gzip<delta> del_packed;
  encode_gzip(del, del_packed);

  std::vector<queryarg> args;
  args.push_back(ident());
  args.push_back(base());
  args.push_back(queryarg(del_packed(),true));
  string insert = "INSERT INTO "+table+" VALUES(?, ?, ?)";
  execute(insert, args);
}

// static ticker cache_hits("vcache hits", "h", 1);

struct version_cache
{
  size_t capacity;
  size_t use;
  std::map<hexenc<id>, data> cache;  

  version_cache() : capacity(constants::db_version_cache_sz), use(0) 
  {
    srand(time(NULL));
  }

  void put(hexenc<id> const & ident, data const & dat)
  {
    while (!cache.empty() 
           && use + dat().size() > capacity)
      {      
        std::string key = (boost::format("%08.8x%08.8x%08.8x%08.8x%08.8x") 
                           % rand() % rand() % rand() % rand() % rand()).str();
        std::map<hexenc<id>, data>::const_iterator i;
        i = cache.lower_bound(hexenc<id>(key));
        if (i == cache.end())
          {
            // we can't find a random entry, probably there's only one
            // entry and we missed it. delete first entry instead.
            i = cache.begin();
          }
        I(i != cache.end());
        I(use >= i->second().size());
        L(FL("version cache expiring %s\n") % i->first);
        use -= i->second().size();          
        cache.erase(i->first);
      }
    cache.insert(std::make_pair(ident, dat));
    use += dat().size();
  }

  bool exists(hexenc<id> const & ident)
  {
    std::map<hexenc<id>, data>::const_iterator i;
    i = cache.find(ident);
    return i != cache.end();
  }

  bool get(hexenc<id> const & ident, data & dat)
  {
    std::map<hexenc<id>, data>::const_iterator i;
    i = cache.find(ident);
    if (i == cache.end())
      return false;
    // ++cache_hits;
    L(FL("version cache hit on %s\n") % ident);
    dat = i->second;
    return true;
  }
};

static version_cache vcache;

typedef vector< hexenc<id> > version_path;

static void
extend_path_if_not_cycle(string table_name, 
                         shared_ptr<version_path> p, 
                         hexenc<id> const & ext,
                         set< hexenc<id> > & seen_nodes,
                         vector< shared_ptr<version_path> > & next_paths)
{
  for (version_path::const_iterator i = p->begin(); i != p->end(); ++i)
    {
      if ((*i)() == ext())
        throw oops("cycle in table '" + table_name + "', at node " 
                   + (*i)() + " <- " + ext());
    }

  if (seen_nodes.find(ext) == seen_nodes.end())
    {      
      p->push_back(ext);
      next_paths.push_back(p);
      seen_nodes.insert(ext);
    }
}

void 
database::get_version(hexenc<id> const & ident,
                      data & dat,
                      string const & data_table,
                      string const & delta_table)
{
  I(ident() != "");

  if (vcache.get(ident, dat))
    {
      return;
    }
  else if (exists(ident, data_table))
    {
     // easy path
     get(ident, dat, data_table);
    }
  else
    {
      // tricky path

      // we start from the file we want to reconstruct and work *forwards*
      // through the database, until we get to a full data object. we then
      // trace back through the list of edges we followed to get to the data
      // object, applying reverse deltas.
      //
      // the effect of this algorithm is breadth-first search, backwards
      // through the storage graph, to discover a forwards shortest path, and
      // then following that shortest path with delta application.
      //
      // we used to do this with the boost graph library, but it invovled
      // loading too much of the storage graph into memory at any moment. this
      // imperative version only loads the descendents of the reconstruction
      // node, so it much cheaper in terms of memory.
      //
      // we also maintain a cycle-detecting set, just to be safe
      
      L(FL("reconstructing %s in %s\n") % ident % delta_table);
      I(delta_exists(ident, delta_table));

      // Our reconstruction algorithm involves keeping a set of parallel
      // linear paths, starting from ident, moving forward through the
      // storage DAG until we hit a storage root.
      //
      // On each iteration, we extend every active path by one step. If our
      // extension involves a fork, we duplicate the path. If any path
      // contains a cycle, we fault. 
      //
      // If, by extending a path C, we enter a node which another path
      // D has already seen, we kill path C. This avoids the possibility of
      // exponential growth in the number of paths due to extensive forking
      // and merging.

      vector< shared_ptr<version_path> > live_paths;

      string delta_query = "SELECT base FROM " + delta_table + " WHERE id = ?";

      {
        shared_ptr<version_path> pth0 = shared_ptr<version_path>(new version_path());      
        pth0->push_back(ident);
        live_paths.push_back(pth0);
      }

      shared_ptr<version_path> selected_path;
      set< hexenc<id> > seen_nodes;

      while (!selected_path)
        {
          vector< shared_ptr<version_path> > next_paths;

          for (vector<shared_ptr<version_path> >::const_iterator i = live_paths.begin();
               i != live_paths.end(); ++i)
            {
              shared_ptr<version_path> pth = *i;
              hexenc<id> tip = pth->back();

              if (vcache.exists(tip) || exists(tip, data_table))
                {
                  selected_path = pth;
                  break;
                }
              else
                {
                  // This tip is not a root, so extend the path.
                  results res;                  
                  fetch(res, one_col, any_rows, 
                        delta_query.c_str(), tip().c_str());

                  I(res.size() != 0);

                  // Replicate the path if there's a fork.
                  for (size_t k = 1; k < res.size(); ++k)
                    {
                      shared_ptr<version_path> pthN 
                        = shared_ptr<version_path>(new version_path(*pth));
                      extend_path_if_not_cycle(delta_table, pthN, 
                                               hexenc<id>(res[k][0]),
                                               seen_nodes, next_paths);
                    }

                  // And extend the base path we're examining.
                  extend_path_if_not_cycle(delta_table, pth, 
                                           hexenc<id>(res[0][0]),
                                           seen_nodes, next_paths);
                }
            }

          I(selected_path || !next_paths.empty());
          live_paths = next_paths;
        }

      // Found a root, now trace it back along the path.

      I(selected_path);
      I(selected_path->size() > 1);

      hexenc<id> curr = selected_path->back();
      selected_path->pop_back();
      data begin;

      if (vcache.exists(curr))
        {
          I(vcache.get(curr, begin));
        }
      else
        {
          get(curr, begin, data_table);
        }

      boost::shared_ptr<delta_applicator> app = new_piecewise_applicator();
      app->begin(begin());
      
      for (version_path::reverse_iterator i = selected_path->rbegin();
           i != selected_path->rend(); ++i)
        {
          hexenc<id> const nxt = *i;

          if (!vcache.exists(curr))
            {
              string tmp;
              app->finish(tmp);
              vcache.put(curr, tmp);
            }

          L(FL("following delta %s -> %s\n") % curr % nxt);
          delta del;
          get_delta(nxt, curr, del, delta_table);
          apply_delta (app, del());
          
          app->next();
          curr = nxt;
        }

      string tmp;
      app->finish(tmp);
      dat = data(tmp);

      hexenc<id> final;
      calculate_ident(dat, final);
      I(final == ident);
    }
  vcache.put(ident, dat);
}


void 
database::drop(hexenc<id> const & ident, 
               string const & table)
{
  string drop = "DELETE FROM " + table + " WHERE id = ?";
  execute(drop.c_str(), ident().c_str());
}

void 
database::put_version(hexenc<id> const & old_id,
                      hexenc<id> const & new_id,
                      delta const & del,
                      string const & data_table,
                      string const & delta_table)
{

  data old_data, new_data;
  delta reverse_delta;
  
  get_version(old_id, old_data, data_table, delta_table);
  patch(old_data, del, new_data);
  diff(new_data, old_data, reverse_delta);
      
  transaction_guard guard(*this);
  if (exists(old_id, data_table))
    {
      // descendent of a head version replaces the head, therefore old head
      // must be disposed of
      drop(old_id, data_table);
    }
  put(new_id, new_data, data_table);
  put_delta(old_id, new_id, reverse_delta, delta_table);
  guard.commit();
}

void 
database::remove_version(hexenc<id> const & target_id,
                         string const & data_table,
                         string const & delta_table)
{
  // We have a one of two cases (for multiple 'older' nodes):
  //
  //    1.  pre:        older <- target <- newer
  //       post:                  older <- newer
  //
  //    2.  pre:        older <- target (a root)
  //       post:                  older (a root)
  // 
  // In case 1 we want to build new deltas bypassing the target we're
  // removing. In case 2 we just promote the older object to a root.

  transaction_guard guard(*this);

  I(exists(target_id, data_table) 
    || delta_exists(target_id, delta_table));

  map<hexenc<id>, data> older;

  {
    results res;
    string query = "SELECT id FROM " + delta_table + " WHERE base = ?";
    fetch(res, one_col, any_rows, 
          query.c_str(), target_id().c_str());
    for (size_t i = 0; i < res.size(); ++i)
      {
        hexenc<id> old_id(res[i][0]);
        data old_data;
        get_version(old_id, old_data, data_table, delta_table);
        older.insert(make_pair(old_id, old_data));
      }
  }

  if (delta_exists(target_id, delta_table))
    {
      if (!older.empty())
        {          
          // Case 1: need to re-deltify all the older values against a newer
          // member of the delta chain. Doesn't really matter which newer
          // element (we have no good heuristic for guessing a good one
          // anyways).
          hexenc<id> newer_id;
          data newer_data;
          results res;
          string query = "SELECT base FROM " + delta_table + " WHERE id = ?";
          fetch(res, one_col, any_rows, 
                query.c_str(), target_id().c_str());
          I(res.size() > 0);
          newer_id = hexenc<id>(res[0][0]);
          get_version(newer_id, newer_data, data_table, delta_table);
          for (map<hexenc<id>, data>::const_iterator i = older.begin();
               i != older.end(); ++i)
            {
              delta bypass_delta;
              diff(newer_data, i->second, bypass_delta);
              put_delta(i->first, newer_id, bypass_delta, delta_table);
            }
        }
      string query = "DELETE from " + delta_table + " WHERE id = ?";
      execute(query.c_str(), target_id().c_str());
    }
  else
    {
      // Case 2: just plop the older values down as new storage roots.
      I(exists(target_id, data_table));
      for (map<hexenc<id>, data>::const_iterator i = older.begin();
           i != older.end(); ++i)
        put(i->first, i->second, data_table);
      string query = "DELETE from " + data_table + " WHERE id = ?";
      execute(query.c_str(), target_id().c_str());
    }

  guard.commit();
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
    || exists(id.inner(), "files");
}

bool 
database::roster_version_exists(hexenc<id> const & id)
{
  return delta_exists(id(), "roster_deltas") 
    || exists(id(), "rosters");
}

bool 
database::revision_exists(revision_id const & id)
{
  return exists(id.inner(), "revisions");
}

bool
database::roster_link_exists_for_revision(revision_id const & rev_id)
{
  results res;
  string query = ("SELECT roster_id FROM revision_roster WHERE rev_id = ? ");
  fetch(res, one_col, any_rows, query.c_str(), 
        rev_id.inner()().c_str());
  I((res.size() == 1) || (res.size() == 0));
  return res.size() == 1;
}

bool
database::roster_exists_for_revision(revision_id const & rev_id)
{
  results res;
  string query = ("SELECT roster_id FROM revision_roster WHERE rev_id = ? ");
  fetch(res, one_col, any_rows, query.c_str(), 
        rev_id.inner()().c_str());
  I((res.size() == 1) || (res.size() == 0));
  return (res.size() == 1) && roster_version_exists(hexenc<id>(res[0][0]));
}

void 
database::get_roster_links(std::map<revision_id, hexenc<id> > & links)
{
  links.clear();
  results res;
  string query = ("SELECT rev_id, roster_id FROM revision_roster");
  fetch(res, 2, any_rows, query.c_str());
  for (size_t i = 0; i < res.size(); ++i)
    {
      links.insert(make_pair(revision_id(res[i][0]), 
                             hexenc<id>(res[i][1])));
    }
}

void 
database::get_file_ids(set<file_id> & ids) 
{
  ids.clear();
  set< hexenc<id> > tmp;
  get_ids("files", tmp);
  get_ids("file_deltas", tmp);
  ids.insert(tmp.begin(), tmp.end());
}

void 
database::get_revision_ids(set<revision_id> & ids) 
{
  ids.clear();
  set< hexenc<id> > tmp;
  get_ids("revisions", tmp);
  ids.insert(tmp.begin(), tmp.end());
}

void 
database::get_roster_ids(set< hexenc<id> > & ids) 
{
  ids.clear();
  set< hexenc<id> > tmp;
  get_ids("rosters", tmp);
  get_ids("roster_deltas", tmp);
  ids.insert(tmp.begin(), tmp.end());
}

void 
database::get_file_version(file_id const & id,
                           file_data & dat)
{
  data tmp;
  get_version(id.inner(), tmp, "files", "file_deltas");
  dat = tmp;
}

void 
database::get_manifest_version(manifest_id const & id,
                               manifest_data & dat)
{
  data tmp;
  get_version(id.inner(), tmp, "manifests", "manifest_deltas");
  dat = tmp;
}

void 
database::put_file(file_id const & id,
                   file_data const & dat)
{
  put(id.inner(), dat.inner(), "files");
}

void 
database::put_file_version(file_id const & old_id,
                           file_id const & new_id,
                           file_delta const & del)
{
  put_version(old_id.inner(), new_id.inner(), del.inner(), 
              "files", "file_deltas");
}

void 
database::get_revision_ancestry(std::multimap<revision_id, revision_id> & graph)
{
  results res;
  graph.clear();
  fetch(res, 2, any_rows, 
        "SELECT parent,child FROM revision_ancestry");
  for (size_t i = 0; i < res.size(); ++i)
    graph.insert(std::make_pair(revision_id(res[i][0]),
                                revision_id(res[i][1])));
}

void 
database::get_revision_parents(revision_id const & id,
                               set<revision_id> & parents)
{
  I(!null_id(id));
  results res;
  parents.clear();
  fetch(res, one_col, any_rows, 
        "SELECT parent FROM revision_ancestry WHERE child = ?",
        id.inner()().c_str());
  for (size_t i = 0; i < res.size(); ++i)
    parents.insert(revision_id(res[i][0]));
}

void 
database::get_revision_children(revision_id const & id,
                                set<revision_id> & children)
{
  results res;
  children.clear();
  fetch(res, one_col, any_rows, 
        "SELECT child FROM revision_ancestry WHERE parent = ?",
        id.inner()().c_str());
  for (size_t i = 0; i < res.size(); ++i)
    children.insert(revision_id(res[i][0]));
}

void 
database::get_revision_manifest(revision_id const & rid,
                               manifest_id & mid)
{
  revision_set rev;
  get_revision(rid, rev);
  mid = rev.new_manifest;
}

void 
database::get_revision(revision_id const & id,
                       revision_set & rev)
{
  revision_data d;
  get_revision(id, d);
  read_revision_set(d, rev);
}

void 
database::get_revision(revision_id const & id,
                       revision_data & dat)
{
  I(!null_id(id));
  results res;
  fetch(res, one_col, one_row, 
        "SELECT data FROM revisions WHERE id = ?",
        id.inner()().c_str());

  gzip<data> gzdata(res[0][0]);
  data rdat;
  decode_gzip(gzdata,rdat);

  // verify that we got a revision with the right id
  {
    revision_id tmp;
    calculate_ident(rdat, tmp);
    I(id == tmp);
  }

  dat = rdat;
}

void
database::deltify_revision(revision_id const & rid)
{
  transaction_guard guard(*this);
  revision_set rev;
  MM(rev);
  MM(rid);
  get_revision(rid, rev);
  // Make sure that all parent revs have their files replaced with deltas
  // from this rev's files.
  {
    for (edge_map::const_iterator i = rev.edges.begin();
         i != rev.edges.end(); ++i)
      {
        for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator
               j = edge_changes(i).deltas_applied.begin();
             j != edge_changes(i).deltas_applied.end(); ++j)
          {
            if (exists(delta_entry_src(j).inner(), "files") &&
                file_version_exists(delta_entry_dst(j)))
              {
                file_data old_data;
                file_data new_data;
                get_file_version(delta_entry_src(j), old_data);
                get_file_version(delta_entry_dst(j), new_data);
                delta delt;
                diff(old_data.inner(), new_data.inner(), delt);
                file_delta del(delt);
                drop(delta_entry_dst(j).inner(), "files");
                drop(delta_entry_dst(j).inner(), "file_deltas");
                put_file_version(delta_entry_src(j), delta_entry_dst(j), del);
              }
          }
      }
  }
  guard.commit();
}


void 
database::put_revision(revision_id const & new_id,
                       revision_set const & rev)
{
  MM(new_id);
  MM(rev);

  I(!null_id(new_id));
  I(!revision_exists(new_id));

  rev.check_sane();
  revision_data d;
  MM(d.inner());
  write_revision_set(rev, d);

  // Phase 1: confirm the revision makes sense
  {
    revision_id tmp;
    MM(tmp);
    calculate_ident(d, tmp);
    I(tmp == new_id);
  }

  transaction_guard guard(*this);
  
  // Phase 2: construct a new roster and sanity-check its manifest_id
  // against the manifest_id of the revision you're writing.
  roster_t ros;
  marking_map mm;
  {
    manifest_id roster_manifest_id;
    MM(roster_manifest_id);
    make_roster_for_revision(rev, new_id, ros, mm, *__app);
    calculate_ident(ros, roster_manifest_id);
    I(rev.new_manifest == roster_manifest_id);
  }

  // Phase 3: Write the revision data

  std::vector<queryarg> args;
  args.push_back(new_id.inner()());
  gzip<data> d_packed;
  encode_gzip(d.inner(), d_packed);
  args.push_back(queryarg(d_packed(),true));
  execute(std::string("INSERT INTO revisions VALUES(?, ?)"), args);

  for (edge_map::const_iterator e = rev.edges.begin();
       e != rev.edges.end(); ++e)
    {
      execute("INSERT INTO revision_ancestry VALUES(?, ?)", 
              edge_old_revision(e).inner()().c_str(),
              new_id.inner()().c_str());
    }

  deltify_revision(new_id);

  // Phase 4: write the roster data and commit
  put_roster(new_id, ros, mm);

  guard.commit();
}


void
database::put_revision(revision_id const & new_id,
                       revision_data const & dat)
{
  revision_set rev;
  read_revision_set(dat, rev);
  put_revision(new_id, rev);
}


void 
database::delete_existing_revs_and_certs()
{
  execute("DELETE FROM revisions");
  execute("DELETE FROM revision_ancestry");
  execute("DELETE FROM revision_certs");
}

void
database::delete_existing_manifests()
{
  execute("DELETE FROM manifests");
  execute("DELETE FROM manifest_deltas");
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
  I(!children.size());
  

  L(FL("Killing revision %s locally\n") % rid);

  // Kill the certs, ancestry, and rev itself.
  execute("DELETE from revision_certs WHERE id = ?",rid.inner()().c_str());
  execute("DELETE from revision_ancestry WHERE child = ?", rid.inner()().c_str());
  execute("DELETE from revisions WHERE id = ?",rid.inner()().c_str());
  
  // Find the associated roster and count the number of links to it
  hexenc<id> roster_id;
  size_t link_count = 0;  
  get_roster_id_for_revision(rid, roster_id);
  {  
    results res;
    string query = ("SELECT rev_id, roster_id FROM revision_roster "
                    "WHERE roster_id = ?");
    fetch(res, 2, any_rows, query.c_str(), roster_id().c_str());
    I(res.size() > 0);
    link_count = res.size();
  }
  
  // Delete our link.
  execute("DELETE from revision_roster WHERE rev_id = ?", rid.inner()().c_str());

  // If that was the last link to the roster, kill the roster too.
  if (link_count == 1)
    remove_version(roster_id, "rosters", "roster_deltas");

  guard.commit();
}

/// Deletes all certs referring to a particular branch. 
void
database::delete_branch_named(cert_value const & branch)
{
  L(FL("Deleting all references to branch %s\n") % branch);
  
  std::vector<queryarg> args;
  args.push_back(queryarg(branch(),true));
  execute(std::string("DELETE FROM revision_certs WHERE name='branch' AND value =?"), args);
  execute(std::string("DELETE FROM branch_epochs WHERE branch=?"), args);
}

/// Deletes all certs referring to a particular tag. 
void
database::delete_tag_named(cert_value const & tag)
{
  L(FL("Deleting all references to tag %s\n") % tag);
  std::vector<queryarg> args;
  args.push_back(queryarg(tag(),true));
  execute(std::string("DELETE FROM revision_certs WHERE name='tag' AND value =?"), args);
}

// crypto key management

void 
database::get_key_ids(string const & pattern,
                      vector<rsa_keypair_id> & pubkeys)
{
  pubkeys.clear();
  results res;

  if (pattern != "")
    fetch(res, one_col, any_rows, 
          "SELECT id FROM public_keys WHERE id GLOB ?",
          pattern.c_str());
  else
    fetch(res, one_col, any_rows, 
          "SELECT id FROM public_keys");

  for (size_t i = 0; i < res.size(); ++i)
    pubkeys.push_back(res[i][0]);
}

void 
database::get_keys(string const & table, vector<rsa_keypair_id> & keys)
{
  keys.clear();
  results res;
  string query = "SELECT id FROM " + table;
  fetch(res, one_col, any_rows, query.c_str());
  for (size_t i = 0; i < res.size(); ++i)
    keys.push_back(res[i][0]);
}

void 
database::get_public_keys(vector<rsa_keypair_id> & keys)
{
  get_keys("public_keys", keys);
}

bool 
database::public_key_exists(hexenc<id> const & hash)
{
  results res;
  fetch(res, one_col, any_rows, 
        "SELECT id FROM public_keys WHERE hash = ?",
        hash().c_str());
  I((res.size() == 1) || (res.size() == 0));
  if (res.size() == 1) 
    return true;
  return false;
}

bool 
database::public_key_exists(rsa_keypair_id const & id)
{
  results res;
  fetch(res, one_col, any_rows, 
        "SELECT id FROM public_keys WHERE id = ?",
        id().c_str());
  I((res.size() == 1) || (res.size() == 0));
  if (res.size() == 1) 
    return true;
  return false;
}

void 
database::get_pubkey(hexenc<id> const & hash, 
                     rsa_keypair_id & id,
                     base64<rsa_pub_key> & pub_encoded)
{
  results res;
  fetch(res, 2, one_row, 
        "SELECT id, keydata FROM public_keys WHERE hash = ?", 
        hash().c_str());
  id = res[0][0];
  encode_base64(rsa_pub_key(res[0][1]), pub_encoded);
}

void 
database::get_key(rsa_keypair_id const & pub_id, 
                  base64<rsa_pub_key> & pub_encoded)
{
  results res;
  fetch(res, one_col, one_row, 
        "SELECT keydata FROM public_keys WHERE id = ?", 
        pub_id().c_str());
  encode_base64(rsa_pub_key(res[0][0]), pub_encoded);
}

void 
database::put_key(rsa_keypair_id const & pub_id, 
                  base64<rsa_pub_key> const & pub_encoded)
{
  hexenc<id> thash;
  key_hash_code(pub_id, pub_encoded, thash);
  I(!public_key_exists(thash));
  E(!public_key_exists(pub_id),
    F("another key with name '%s' already exists") % pub_id);
  rsa_pub_key pub_key;
  decode_base64(pub_encoded, pub_key);
  std::vector<queryarg> args;
  args.push_back(thash());
  args.push_back(pub_id());
  args.push_back(queryarg(pub_key(),true));
  execute(std::string("INSERT INTO public_keys VALUES(?, ?, ?)"), args);
}

void
database::delete_public_key(rsa_keypair_id const & pub_id)
{
  execute("DELETE FROM public_keys WHERE id = ?",
          pub_id().c_str());
}

// cert management

bool 
database::cert_exists(cert const & t,
                      string const & table)
{
  results res;
  string query = 
    "SELECT id FROM " + table + " WHERE id = ? "
    "AND name = ? "
    "AND value = ? " 
    "AND keypair = ? "
    "AND signature = ?";
  std::vector<queryarg> args;
  args.push_back(t.ident());
  args.push_back(t.name());
  cert_value value;
  decode_base64(t.value, value);
  args.push_back(queryarg(value(),true));
  args.push_back(t.key());
  rsa_sha1_signature sig;
  decode_base64(t.sig, sig);
  args.push_back(queryarg(sig(),true));
  
  fetch(res, 1, any_rows, query, args);
  I(res.size() == 0 || res.size() == 1);
  return res.size() == 1;
}

void 
database::put_cert(cert const & t,
                   string const & table)
{
  std::vector<queryarg> args;
  hexenc<id> thash;
  cert_hash_code(t, thash);
  args.push_back(thash());
  args.push_back(t.ident());
  args.push_back(t.name());
  cert_value value;
  decode_base64(t.value, value);
  args.push_back(queryarg(value(),true));
  args.push_back(t.key());
  rsa_sha1_signature sig;
  decode_base64(t.sig, sig);
  args.push_back(queryarg(sig(),true));

  string insert = "INSERT INTO " + table + " VALUES(?, ?, ?, ?, ?, ?)";
  execute(insert, args);
}

void 
database::results_to_certs(results const & res,
                           vector<cert> & certs)
{
  certs.clear();
  for (size_t i = 0; i < res.size(); ++i)
    {
      cert t;
      base64<cert_value> value;
      encode_base64(cert_value(res[i][2]), value);
      base64<rsa_sha1_signature> sig;
      encode_base64(rsa_sha1_signature(res[i][4]), sig);
      t = cert(hexenc<id>(res[i][0]), 
              cert_name(res[i][1]),
              value,
              rsa_keypair_id(res[i][3]),
              sig);
      certs.push_back(t);
    }
}

void
database::install_functions(app_state * app)
{
  // register any functions we're going to use
  I(sqlite3_create_function(sql(), "unbase64", -1, 
                           SQLITE_UTF8, NULL,
                           &sqlite3_unbase64_fn, 
                           NULL, NULL) == 0);
  I(sqlite3_create_function(sql(), "unpack", -1, 
                           SQLITE_UTF8, NULL,
                           &sqlite3_unpack_fn, 
                           NULL, NULL) == 0);
}

void
database::install_views()
{
  // we don't currently use any views. re-enable this code if you find a
  // compelling reason to use views.

  /*
  // delete any existing views
  results res;
  fetch(res, one_col, any_rows,
        "SELECT name FROM sqlite_master WHERE type='view'");

  for (size_t i = 0; i < res.size(); ++i)
    {
      string drop = "DROP VIEW " + res[i][0];
      execute(drop.c_str());
    }
  // register any views we're going to use
  execute(views_constant);
  */
}

void 
database::get_certs(vector<cert> & certs,                       
                    string const & table)
{
  results res;
  string query = "SELECT id, name, value, keypair, signature FROM " + table; 
  fetch(res, 5, any_rows, query.c_str());
  results_to_certs(res, certs);
}


void 
database::get_certs(hexenc<id> const & ident, 
                    vector<cert> & certs,                       
                    string const & table)
{
  results res;
  string query = 
    "SELECT id, name, value, keypair, signature FROM " + table + 
    " WHERE id = ?";

  fetch(res, 5, any_rows, query.c_str(), ident().c_str());
  results_to_certs(res, certs);
}


void 
database::get_certs(cert_name const & name,           
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  string query = 
    "SELECT id, name, value, keypair, signature FROM " + table + 
    " WHERE name = ?";
  fetch(res, 5, any_rows, query.c_str(), name().c_str());
  results_to_certs(res, certs);
}


void 
database::get_certs(hexenc<id> const & ident, 
                    cert_name const & name,           
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  string query = 
    "SELECT id, name, value, keypair, signature FROM " + table +
    " WHERE id = ? AND name = ?";

  fetch(res, 5, any_rows, query.c_str(), 
        ident().c_str(), name().c_str());
  results_to_certs(res, certs);
}

void 
database::get_certs(cert_name const & name,
                    base64<cert_value> const & val, 
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  string query = 
    "SELECT id, name, value, keypair, signature FROM " + table + 
    " WHERE name = ? AND value = ?";
  std::vector<queryarg> args;
  args.push_back(name());
  cert_value binvalue;
  decode_base64(val, binvalue);
  args.push_back(queryarg(binvalue(),true));
  fetch(res, 5, any_rows, query, args);
  results_to_certs(res, certs);
}


void 
database::get_certs(hexenc<id> const & ident, 
                    cert_name const & name,           
                    base64<cert_value> const & value,
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  string query = 
    "SELECT id, name, value, keypair, signature FROM " + table + 
    " WHERE id = ? AND name = ? AND value = ?";

  std::vector<queryarg> args;
  args.push_back(ident());
  args.push_back(name());
  cert_value binvalue;
  decode_base64(value, binvalue);
  args.push_back(queryarg(binvalue(),true));
  fetch(res, 5, any_rows, query, args);
  results_to_certs(res, certs);
}



bool 
database::revision_cert_exists(revision<cert> const & cert)
{ 
  return cert_exists(cert.inner(), "revision_certs"); 
}

void 
database::put_revision_cert(revision<cert> const & cert)
{ 
  put_cert(cert.inner(), "revision_certs"); 
}

void database::get_revision_cert_nobranch_index(std::vector< std::pair<hexenc<id>,
                                       std::pair<revision_id, rsa_keypair_id> > > & idx)
{
  results res;
  fetch(res, 3, any_rows, 
        "SELECT hash, id, keypair "
        "FROM 'revision_certs' WHERE name != 'branch'");

  idx.clear();
  idx.reserve(res.size());
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      idx.push_back(std::make_pair(hexenc<id>((*i)[0]), 
                                   std::make_pair(revision_id((*i)[1]),
                                                  rsa_keypair_id((*i)[2]))));
    }
}

void 
database::get_revision_certs(vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(certs, "revision_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));  
}

void 
database::get_revision_certs(cert_name const & name,
                            vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(name, certs, "revision_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));  
}

void 
database::get_revision_certs(revision_id const & id, 
                             cert_name const & name, 
                             vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(id.inner(), name, certs, "revision_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));  
}

void 
database::get_revision_certs(revision_id const & id, 
                             cert_name const & name,
                             base64<cert_value> const & val, 
                             vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(id.inner(), name, val, certs, "revision_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));  
}

void 
database::get_revision_certs(cert_name const & name,
                             base64<cert_value> const & val, 
                             vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(name, val, certs, "revision_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));  
}

void 
database::get_revision_certs(revision_id const & id, 
                             vector< revision<cert> > & ts)
{ 
  vector<cert> certs;
  get_certs(id.inner(), certs, "revision_certs"); 
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));
}

void 
database::get_revision_certs(revision_id const & ident, 
                             vector< hexenc<id> > & ts)
{ 
  results res;
  vector<cert> certs;
  fetch(res, one_col, any_rows, 
        "SELECT hash "
        "FROM revision_certs "
        "WHERE id = ?", 
        ident.inner()().c_str());
  ts.clear();
  for (size_t i = 0; i < res.size(); ++i)
    ts.push_back(hexenc<id>(res[i][0]));
}

void 
database::get_revision_cert(hexenc<id> const & hash,
                            revision<cert> & c)
{
  results res;
  vector<cert> certs;
  fetch(res, 5, one_row, 
        "SELECT id, name, value, keypair, signature "
        "FROM revision_certs "
        "WHERE hash = ?", 
        hash().c_str());
  results_to_certs(res, certs);
  I(certs.size() == 1);
  c = revision<cert>(certs[0]);
}

bool 
database::revision_cert_exists(hexenc<id> const & hash)
{
  results res;
  vector<cert> certs;
  fetch(res, one_col, any_rows, 
        "SELECT id "
        "FROM revision_certs "
        "WHERE hash = ?", 
        hash().c_str());
  I(res.size() == 0 || res.size() == 1);
  return (res.size() == 1);
}

void 
database::get_manifest_certs(manifest_id const & id, 
                             vector< manifest<cert> > & ts)
{ 
  vector<cert> certs;
  get_certs(id.inner(), certs, "manifest_certs"); 
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));
}


void 
database::get_manifest_certs(cert_name const & name, 
                            vector< manifest<cert> > & ts)
{
  vector<cert> certs;
  get_certs(name, certs, "manifest_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));  
}


// completions
void 
database::complete(string const & partial,
                   set<revision_id> & completions)
{
  results res;
  completions.clear();

  string pattern = partial + "*";

  fetch(res, 1, any_rows,
        "SELECT id FROM revisions WHERE id GLOB ?",
        pattern.c_str());
  
  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(revision_id(res[i][0]));  
}


void 
database::complete(string const & partial,
                   set<file_id> & completions)
{
  results res;
  completions.clear();

  string pattern = partial + "*";

  fetch(res, 1, any_rows,
        "SELECT id FROM files WHERE id GLOB ?",
        pattern.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(file_id(res[i][0]));  
  
  res.clear();

  fetch(res, 1, any_rows,
        "SELECT id FROM file_deltas WHERE id GLOB ?",
        pattern.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(file_id(res[i][0]));  
}

void 
database::complete(string const & partial,
                   set< pair<key_id, utf8 > > & completions)
{
  results res;
  completions.clear();

  string pattern = partial + "*";

  fetch(res, 2, any_rows,
        "SELECT hash, id FROM public_keys WHERE hash GLOB ?",
        pattern.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(make_pair(key_id(res[i][0]), utf8(res[i][1])));
}

using selectors::selector_type;

static void selector_to_certname(selector_type ty,
                                 string & s,
                                 string & prefix,
                                 string & suffix)
{
  prefix = suffix = "*";
  switch (ty)
    {
    case selectors::sel_author:
      s = author_cert_name;
      break;
    case selectors::sel_branch:
      prefix = suffix = "";
      s = branch_cert_name;
      break;
    case selectors::sel_head:
      prefix = suffix = "";
      s = branch_cert_name;
      break;
    case selectors::sel_date:
    case selectors::sel_later:
    case selectors::sel_earlier:
      s = date_cert_name;
      break;
    case selectors::sel_tag:
      prefix = suffix = "";
      s = tag_cert_name;
      break;
    case selectors::sel_ident:
    case selectors::sel_cert:
    case selectors::sel_unknown:
      I(false); // don't do this.
      break;
    }
}

void database::complete(selector_type ty,
                        string const & partial,
                        vector<pair<selector_type, string> > const & limit,
                        set<string> & completions)
{
  //L(FL("database::complete for partial '%s'\n") % partial);
  completions.clear();

  // step 1: the limit is transformed into an SQL select statement which
  // selects a set of IDs from the manifest_certs table which match the
  // limit.  this is done by building an SQL select statement for each term
  // in the limit and then INTERSECTing them all.

  string lim = "(";
  if (limit.empty())
    {
      lim += "SELECT id FROM revision_certs";
    }
  else
    {
      bool first_limit = true;
      for (vector<pair<selector_type, string> >::const_iterator i = limit.begin();
           i != limit.end(); ++i)
        {
          if (first_limit)
            first_limit = false;
          else
            lim += " INTERSECT ";
          
          if (i->first == selectors::sel_ident)
            {
              lim += "SELECT id FROM revision_certs ";
              lim += (boost::format("WHERE id GLOB '%s*'") 
                      % i->second).str();
            }
          else if (i->first == selectors::sel_cert)
            {
              if (i->second.length() > 0)
                {
                  size_t spot = i->second.find("=");

                  if (spot != (size_t)-1)
                    {
                      string certname;
                      string certvalue;

                      certname = i->second.substr(0, spot);
                      spot++;
                      certvalue = i->second.substr(spot);
                      lim += "SELECT id FROM revision_certs ";
                      lim += (boost::format("WHERE name='%s' AND CAST(value AS TEXT) glob '%s'")
                              % certname % certvalue).str();
                    }
                  else
                    {
                      lim += "SELECT id FROM revision_certs ";
                      lim += (boost::format("WHERE name='%s'")
                              % i->second).str();
                    }

                }
            }
          else if (i->first == selectors::sel_unknown)
            {
              lim += "SELECT id FROM revision_certs ";
              lim += (boost::format(" WHERE (name='%s' OR name='%s' OR name='%s')")
                      % author_cert_name 
                      % tag_cert_name 
                      % branch_cert_name).str();
              lim += (boost::format(" AND CAST(value AS TEXT) glob '*%s*'")
                      % i->second).str();     
            }
          else if (i->first == selectors::sel_head) 
            {
              // get branch names
              vector<cert_value> branch_names;
              if (i->second.size() == 0)
                {
                  __app->require_working_copy("the empty head selector h: refers to the head of the current branch");
                  branch_names.push_back((__app->branch_name)());
                }
              else
                {
                  string subquery = (boost::format("SELECT DISTINCT value FROM revision_certs WHERE name='%s' and CAST(value AS TEXT) glob '%s'") 
                                     % branch_cert_name % i->second).str();
                  results res;
                  fetch(res, one_col, any_rows, subquery.c_str());
                  for (size_t i = 0; i < res.size(); ++i)
                    {
                      data row_decoded(res[i][0]);
                      branch_names.push_back(row_decoded());
                    }
                }

              // for each branch name, get the branch heads
              set<revision_id> heads;
              for (vector<cert_value>::const_iterator bn = branch_names.begin(); bn != branch_names.end(); bn++)
                {
                  set<revision_id> branch_heads;
                  get_branch_heads(*bn, *__app, branch_heads);
                  heads.insert(branch_heads.begin(), branch_heads.end());
                  L(FL("after get_branch_heads for %s, heads has %d entries\n") % (*bn) % heads.size());
                }

              lim += "SELECT id FROM revision_certs WHERE id IN (";
              if (heads.size())
                {
                  set<revision_id>::const_iterator r = heads.begin();
                  lim += (boost::format("'%s'") % r->inner()()).str();
                  r++;
                  while (r != heads.end())
                    {
                      lim += (boost::format(", '%s'") % r->inner()()).str();
                      r++;
                    }
                }
              lim += ") ";
            }
          else
            {
              string certname;
              string prefix;
              string suffix;
              selector_to_certname(i->first, certname, prefix, suffix);
              L(FL("processing selector type %d with i->second '%s'\n") % ty % i->second);
              if ((i->first == selectors::sel_branch) && (i->second.size() == 0))
                {
                  __app->require_working_copy("the empty branch selector b: refers to the current branch");
                  lim += (boost::format("SELECT id FROM revision_certs WHERE name='%s' AND CAST(value AS TEXT) glob '%s'")
                          % branch_cert_name % __app->branch_name).str();
                  L(FL("limiting to current branch '%s'\n") % __app->branch_name);
                }
              else
                {
                  lim += (boost::format("SELECT id FROM revision_certs WHERE name='%s' AND ") % certname).str();
                  switch (i->first)
                    {
                    case selectors::sel_earlier:
                      lim += (boost::format("value <= X'%s'") % encode_hexenc(i->second)).str();
                      break;
                    case selectors::sel_later:
                      lim += (boost::format("value > X'%s'") % encode_hexenc(i->second)).str();
                      break;
                    default:
                      lim += (boost::format("CAST(value AS TEXT) glob '%s%s%s'")
                              % prefix % i->second % suffix).str();
                      break;
                    }
                }
            }
          //L(FL("found selector type %d, selecting_head is now %d\n") % i->first % selecting_head);
        }
    }
  lim += ")";
  
  // step 2: depending on what we've been asked to disambiguate, we
  // will complete either some idents, or cert values, or "unknown"
  // which generally means "author, tag or branch"

  string query;
  if (ty == selectors::sel_ident)
    {
      query = (boost::format("SELECT id FROM %s") % lim).str();
    }
  else 
    {
      string prefix = "*";
      string suffix = "*";
      query = "SELECT value FROM revision_certs WHERE";
      if (ty == selectors::sel_unknown)
        {               
          query += 
            (boost::format(" (name='%s' OR name='%s' OR name='%s')")
             % author_cert_name 
             % tag_cert_name 
             % branch_cert_name).str();
        }
      else
        {
          string certname;
          selector_to_certname(ty, certname, prefix, suffix);
          query += 
            (boost::format(" (name='%s')") % certname).str();
        }
        
      query += (boost::format(" AND (CAST(value AS TEXT) GLOB '%s%s%s')")
                % prefix % partial % suffix).str();
      query += (boost::format(" AND (id IN %s)") % lim).str();
    }

  // std::cerr << query << std::endl;    // debug expr

  results res;
  fetch(res, one_col, any_rows, query.c_str());
  for (size_t i = 0; i < res.size(); ++i)
    {
      if (ty == selectors::sel_ident) 
        completions.insert(res[i][0]);
      else
        {
          data row_decoded(res[i][0]);
          completions.insert(row_decoded());
        }
    }
}

// epochs 

void 
database::get_epochs(std::map<cert_value, epoch_data> & epochs)
{
  epochs.clear();
  results res;
  fetch(res, 2, any_rows, "SELECT branch, epoch FROM branch_epochs");
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {      
      cert_value decoded(idx(*i, 0));
      I(epochs.find(decoded) == epochs.end());
      epochs.insert(std::make_pair(decoded, epoch_data(idx(*i, 1))));
    }
}

void
database::get_epoch(epoch_id const & eid,
                    cert_value & branch, epoch_data & epo)
{
  I(epoch_exists(eid));
  results res;
  fetch(res, 2, any_rows,
        "SELECT branch, epoch FROM branch_epochs"
        " WHERE hash = ?",
        eid.inner()().c_str());
  I(res.size() == 1);
  branch = cert_value(idx(idx(res, 0), 0));
  epo = epoch_data(idx(idx(res, 0), 1));
}

bool
database::epoch_exists(epoch_id const & eid)
{
  results res;
  fetch(res, one_col, any_rows,
        "SELECT hash FROM branch_epochs WHERE hash = ?",
        eid.inner()().c_str());
  I(res.size() == 1 || res.size() == 0);
  return res.size() == 1;
}

void 
database::set_epoch(cert_value const & branch, epoch_data const & epo)
{
  epoch_id eid;
  epoch_hash_code(branch, epo, eid);
  I(epo.inner()().size() == constants::epochlen);
  std::vector<queryarg> args;
  args.push_back(eid.inner()());
  args.push_back(queryarg(branch(),true));
  args.push_back(epo.inner()());
  execute(std::string("INSERT OR REPLACE INTO branch_epochs VALUES(?, ?, ?)"), args);
}

void 
database::clear_epoch(cert_value const & branch)
{
  std::vector<queryarg> args;
  args.push_back(queryarg(branch(),true));
  execute(std::string("DELETE FROM branch_epochs WHERE branch = ?"), args);
}

// vars

void
database::get_vars(std::map<var_key, var_value> & vars)
{
  vars.clear();
  results res;
  fetch(res, 3, any_rows, "SELECT domain, name, value FROM db_vars");
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      var_domain domain(idx(*i, 0));
      var_name name(idx(*i, 1));
      var_value value(idx(*i, 2));
      I(vars.find(std::make_pair(domain, name)) == vars.end());
      vars.insert(std::make_pair(std::make_pair(domain, name), value));
    }
}

void
database::get_var(var_key const & key, var_value & value)
{
  // FIXME: sillyly inefficient.  Doesn't really matter, though.
  std::map<var_key, var_value> vars;
  get_vars(vars);
  std::map<var_key, var_value>::const_iterator i = vars.find(key);
  I(i != vars.end());
  value = i->second;
}

bool
database::var_exists(var_key const & key)
{
  // FIXME: sillyly inefficient.  Doesn't really matter, though.
  std::map<var_key, var_value> vars;
  get_vars(vars);
  std::map<var_key, var_value>::const_iterator i = vars.find(key);
  return i != vars.end();
}

void
database::set_var(var_key const & key, var_value const & value)
{
  std::vector<queryarg> args;
  args.push_back(key.first());
  args.push_back(queryarg(key.second(),true));
  args.push_back(queryarg(value(),true));
  execute(std::string("INSERT OR REPLACE INTO db_vars VALUES(?, ?, ?)"), args);
}

void
database::clear_var(var_key const & key)
{
  std::vector<queryarg> args;
  args.push_back(key.first());
  args.push_back(queryarg(key.second(),true));
  execute(std::string("DELETE FROM db_vars WHERE domain = ? AND name = ?"), args);
}

// branches

void
database::get_branches(vector<string> & names)
{
    results res;
    string query="SELECT DISTINCT value FROM revision_certs WHERE name= ?";
    string cert_name="branch";
    fetch(res, one_col, any_rows, query.c_str(), cert_name.c_str());
    for (size_t i = 0; i < res.size(); ++i)
      {
        names.push_back(res[i][0]);
      }
}

void
database::get_roster_id_for_revision(revision_id const & rev_id,
                                     hexenc<id> & roster_id)
{
  if (rev_id.inner()().empty())
    {
      roster_id = hexenc<id>();
      return;
    }

  results res;
  string query = ("SELECT roster_id FROM revision_roster WHERE rev_id = ? ");  
  fetch(res, one_col, any_rows, query.c_str(),
        rev_id.inner()().c_str());
  if (res.size() == 0)
    {
      check_rosterified();
    }
  I(res.size() == 1);
  roster_id = hexenc<id>(res[0][0]);
}

void 
database::get_roster(revision_id const & rev_id, 
                     roster_t & roster)
{
  marking_map mm;
  get_roster(rev_id, roster, mm);
}

void 
database::get_roster(hexenc<id> const & ros_id, 
                     data & dat)
{
  string data_table = "rosters";
  string delta_table = "roster_deltas";

  get_version(ros_id, dat, data_table, delta_table);
}

void 
database::get_roster(revision_id const & rev_id, 
                     roster_t & roster,
                     marking_map & marks)
{
  if (rev_id.inner()().empty())
    {
      roster = roster_t();
      marks = marking_map();
      return;
    }

  data dat;
  hexenc<id> ident;

  get_roster_id_for_revision(rev_id, ident);
  get_roster(ident, dat);
  read_roster_and_marking(dat, roster, marks);
}


void
database::put_roster(revision_id const & rev_id,
                     roster_t & roster,
                     marking_map & marks)
{
  MM(rev_id);
  data old_data, new_data;
  delta reverse_delta;
  hexenc<id> old_id, new_id;

  write_roster_and_marking(roster, marks, new_data);
  calculate_ident(new_data, new_id);

  // First: find the "old" revision; if there are multiple old
  // revisions, we just pick the first. It probably doesn't matter for
  // the sake of delta-encoding.

  string data_table = "rosters";
  string delta_table = "roster_deltas";

  transaction_guard guard(*this);

  execute("INSERT into revision_roster VALUES (?, ?)", 
          rev_id.inner()().c_str(),
          new_id().c_str());

  if (exists(new_id, data_table) 
      || delta_exists(new_id, delta_table))
    {
      guard.commit();
      return;
    }

  // Else we have a new roster the database hasn't seen yet; our task is to
  // add it, and deltify all the incoming edges (if they aren't already).

  put(new_id, new_data, data_table);

  std::set<revision_id> parents;
  get_revision_parents(rev_id, parents);

  // Now do what deltify would do if we bothered (we have the
  // roster written now, so might as well do it here).
  for (std::set<revision_id>::const_iterator i = parents.begin();
       i != parents.end(); ++i)
    {
      if (null_id(*i))
        continue;      
      revision_id old_rev = *i;
      get_roster_id_for_revision(old_rev, old_id);
      if (exists(new_id, data_table))
        {
          get_version(old_id, old_data, data_table, delta_table);
          diff(new_data, old_data, reverse_delta);
          drop(old_id, data_table);
          put_delta(old_id, new_id, reverse_delta, delta_table);
        }
    }
  guard.commit();
}


typedef hashmap::hash_multimap<string,string,hashmap::string_hash> ancestry_map;

static void 
transitive_closure(string const & x,
                   ancestry_map const & m,
                   set<revision_id> & results)
{
  results.clear();

  deque<string> work;
  work.push_back(x);
  while (!work.empty())
    {
      string c = work.front();
      work.pop_front();
      revision_id curr(c);
      if (results.find(curr) == results.end())
        {
          results.insert(curr);
          pair<ancestry_map::const_iterator, ancestry_map::const_iterator> range;
          range = m.equal_range(c);
          for (ancestry_map::const_iterator i = range.first; i != range.second; ++i)
            {
              if (i->first == c)
                work.push_back(i->second);
            }
        }
    }
}

void 
database::get_uncommon_ancestors(revision_id const & a,
                                 revision_id const & b,
                                 set<revision_id> & a_uncommon_ancs,
                                 set<revision_id> & b_uncommon_ancs)
{
  // FIXME: This is a somewhat ugly, and possibly unaccepably slow way
  // to do it. Another approach involves maintaining frontier sets for
  // each and slowly deepening them into history; would need to
  // benchmark to know which is actually faster on real datasets.

  a_uncommon_ancs.clear();
  b_uncommon_ancs.clear();

  results res;
  a_uncommon_ancs.clear();
  b_uncommon_ancs.clear();

  fetch(res, 2, any_rows, 
        "SELECT parent,child FROM revision_ancestry");

  set<revision_id> a_ancs, b_ancs;

  ancestry_map child_to_parent_map;
  for (size_t i = 0; i < res.size(); ++i)
    child_to_parent_map.insert(make_pair(res[i][1], res[i][0]));

  transitive_closure(a.inner()(), child_to_parent_map, a_ancs);
  transitive_closure(b.inner()(), child_to_parent_map, b_ancs);
  
  set_difference(a_ancs.begin(), a_ancs.end(), 
                 b_ancs.begin(), b_ancs.end(),
                 inserter(a_uncommon_ancs, a_uncommon_ancs.begin()));

  set_difference(b_ancs.begin(), b_ancs.end(), 
                 a_ancs.begin(), a_ancs.end(),
                 inserter(b_uncommon_ancs, b_uncommon_ancs.begin()));
}

node_id 
database::next_node_id()
{
  transaction_guard guard(*this);
  results res;

  // We implement this as a fixed db var.

  fetch(res, one_col, any_rows, 
        "SELECT node FROM next_roster_node_number");
  
  node_id n;
  if (res.empty())
    {
      n = 1;
      execute ("INSERT INTO next_roster_node_number VALUES(?)", 
               lexical_cast<string>(n).c_str());
    }
  else
    {
      I(res.size() == 1);
      n = lexical_cast<node_id>(res[0][0]);
      ++n;
      execute ("UPDATE next_roster_node_number SET node = ?",
               lexical_cast<string>(n).c_str());
      
    }
  guard.commit();
  return n;
}


void
database::check_filename()
{
  N(!filename.empty(), F("no database specified"));
}


void
database::check_db_exists()
{
  require_path_is_file(filename,
                       F("database %s does not exist") % filename,
                       F("%s is a directory, not a database") % filename);
}

bool
database::database_specified()
{
  return !filename.empty();
}


void
database::open()
{
  int error;

  I(!__sql);

  error = sqlite3_open(filename.as_external().c_str(), &__sql);

  if (__sql)
    {
      I(sql_contexts.find(__sql) == sql_contexts.end());
      sql_contexts.insert(__sql);
    }

  N(!error, (F("could not open database '%s': %s")
             % filename % string(sqlite3_errmsg(__sql))));
}

void
database::close()
{
  if (__sql)
    {
      sqlite3_close(__sql);
      I(sql_contexts.find(__sql) != sql_contexts.end());
      sql_contexts.erase(__sql);
      __sql = 0;
    }
}


// transaction guards

transaction_guard::transaction_guard(database & d, bool exclusive,
                                     size_t checkpoint_batch_size,
                                     size_t checkpoint_batch_bytes) 
  : committed(false), db(d), exclusive(exclusive),
    checkpoint_batch_size(checkpoint_batch_size),
    checkpoint_batch_bytes(checkpoint_batch_bytes),
    checkpointed_calls(0),
    checkpointed_bytes(0)
{
  db.begin_transaction(exclusive);
}

transaction_guard::~transaction_guard()
{
  if (committed)
    db.commit_transaction();
  else
    db.rollback_transaction();
}

void 
transaction_guard::do_checkpoint()
{
  db.commit_transaction();
  db.begin_transaction(exclusive);
  checkpointed_calls = 0;
  checkpointed_bytes = 0;
}

void 
transaction_guard::maybe_checkpoint(size_t nbytes)
{
  checkpointed_calls += 1;
  checkpointed_bytes += nbytes;
  if (checkpointed_calls >= checkpoint_batch_size
      || checkpointed_bytes >= checkpoint_batch_bytes)
    do_checkpoint();
}

void 
transaction_guard::commit()
{
  committed = true;
}



// called to avoid foo.db-journal files hanging around if we exit cleanly
// without unwinding the stack (happens with SIGINT & SIGTERM)
void
close_all_databases()
{
  L(FL("attempting to rollback and close %d databases") % sql_contexts.size());
  for (set<sqlite3*>::iterator i = sql_contexts.begin();
       i != sql_contexts.end(); i++)
    {
      // the ROLLBACK is required here, even though the sqlite docs
      // imply that transactions are rolled back on database closure
      int exec_err = sqlite3_exec(*i, "ROLLBACK", NULL, NULL, NULL);
      int close_err = sqlite3_close(*i);

      L(FL("exec_err = %d, close_err = %d") % exec_err % close_err);
    }
  sql_contexts.clear();
}
