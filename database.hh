#ifndef __DATABASE_HH__
#define __DATABASE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

struct sqlite3;
struct sqlite3_stmt;
struct cert;
int sqlite3_finalize(sqlite3_stmt *);

#include <stdarg.h>

#include <vector>
#include <set>
#include <map>
#include <string>

#include "cset.hh"
#include "numeric_vocab.hh"
#include "paths.hh"
#include "cleanup.hh"
#include "roster.hh"
#include "selectors.hh"
#include "vocab.hh"

// this file defines a public, typed interface to the database.
// the database class encapsulates all knowledge about sqlite,
// the schema, and all SQL statements used to access the schema.
//
// one thing which is rather important to note is that this file
// deals with two sorts of version relationships. the versions
// stored in the database are all *backwards* from those the program
// sees. so for example if you have two versions of a file
// 
// file.1, file.2
//
// where file.2 was a modification of file.1, then as far as the rest of
// the application is concerned -- and the ancestry graph -- file.1 is the
// "old" version and file.2 is the "new" version. note the use of terms
// which describe time, and the sequence of edits a user makes to a
// file. those are ancestry terms. when the application composes a
// patchset, for example, it'll contain the diff delta(file.1, file.2)
//
// from the database's perspective, however, file.1 is the derived version,
// and file.2 is the base version. the base version is stored in the
// "files" table, and the *reverse* diff delta(file.2, file.1) is stored in
// the "file_deltas" table, under the id of file.1, with the id of file.2
// listed as its base. note the use of the terms which describe
// reconstruction; those are storage-system terms.
//
// the interface *to* the database, and the ancestry version graphs, use
// the old / new metaphor of ancestry, but within the database (including
// the private helper methods, and the storage version graphs) the
// base/derived storage metaphor is used. the only real way to tell which
// is which is to look at the parameter names and code. I might try to
// express this in the type system some day, but not presently.
//
// the key phrase to keep repeating when working on this code is:
// 
// "base files are new, derived files are old"
//
// it makes the code confusing, I know. this is possibly the worst part of
// the program. I don't know if there's any way to make it clearer.

class transaction_guard;
struct posting;
struct app_state;
struct revision_set;

class database
{
  system_path filename;
  std::string const schema;
  void check_schema();
  void check_format();

  struct statement {
    statement() : count(0), stmt(0, sqlite3_finalize) {}
    int count;
    cleanup_ptr<sqlite3_stmt*, int> stmt;
  };

  std::map<std::string, statement> statement_cache;

  struct app_state * __app;
  struct sqlite3 * __sql;
  struct sqlite3 * sql(bool init = false, bool migrating_format = false);
  int transaction_level;
  bool transaction_exclusive;

  void install_functions(app_state * app);
  void install_views();

  typedef std::vector< std::vector<std::string> > results;
 
  void execute(char const * query, ...);

  // structure to distinguish between blob and string arguments
  // in sqlite3 a blob never equals a string even when binary identical
  // so we need to remember which type to pass to the query
  struct queryarg : public std::string
  {
    bool blob;
    queryarg(std::string const & s = std::string(), bool blob = false)
      : std::string(s), blob(blob) {}
  };
  
  void execute(std::string const& query, std::vector<queryarg> const& args);

  statement& prepare(char const * query,
                     int const want_cols);

  void fetch(statement & stmt, 
             results & res, 
             int const want_rows,
             const char *query);
  
  void fetch(results & res, 
             int const want_cols, 
             int const want_rows, 
             char const * query, ...);
  
  void fetch(results & res, 
             int const want_cols, 
             int const want_rows, 
             char const * query, 
             va_list args);

  // this variant is binary transparent
  void fetch(results & res, 
             int const want_cols, 
             int const want_rows, 
             std::string const& query, 
             std::vector<queryarg> const& args);
 
  bool exists(hexenc<id> const & ident, 
              std::string const & table);
  bool delta_exists(hexenc<id> const & ident,
                    std::string const & table);

  unsigned long count(std::string const & table);
  unsigned long space_usage(std::string const & table,
                            std::string const & concatenated_columns);

  void get_ids(std::string const & table, std::set< hexenc<id> > & ids); 

  void get(hexenc<id> const & new_id,
           data & dat,
           std::string const & table);
  void get_delta(hexenc<id> const & ident,
                 hexenc<id> const & base,
                 delta & del,
                 std::string const & table);
  void get_version(hexenc<id> const & id,
                   data & dat,
                   std::string const & data_table,
                   std::string const & delta_table);
  
  void put(hexenc<id> const & new_id,
           data const & dat,
           std::string const & table);
  void drop(hexenc<id> const & base,
            std::string const & table);
  void put_delta(hexenc<id> const & id,
                 hexenc<id> const & base,
                 delta const & del,
                 std::string const & table);
  void put_version(hexenc<id> const & old_id,
                   hexenc<id> const & new_id,
                   delta const & del,
                   std::string const & data_table,
                   std::string const & delta_table);
  void remove_version(hexenc<id> const & target_id,
                      std::string const & data_table,
                      std::string const & delta_table);

  void get_keys(std::string const & table, std::vector<rsa_keypair_id> & keys);

  bool cert_exists(cert const & t,
                  std::string const & table);
  void put_cert(cert const & t, std::string const & table);  
  void results_to_certs(results const & res,
                       std::vector<cert> & certs);

  void get_certs(std::vector< cert > & certs,
                 std::string const & table);  

  void get_certs(hexenc<id> const & id, 
                 std::vector< cert > & certs,
                 std::string const & table);  

  void get_certs(cert_name const & name,              
                 std::vector< cert > & certs,
                 std::string const & table);

  void get_certs(hexenc<id> const & id,
                 cert_name const & name,
                 std::vector< cert > & certs,
                 std::string const & table);  

  void get_certs(hexenc<id> const & id,
                 cert_name const & name,
                 base64<cert_value> const & val, 
                 std::vector< cert > & certs,
                 std::string const & table);  

  void get_certs(cert_name const & name,
                 base64<cert_value> const & val, 
                 std::vector<cert> & certs,
                 std::string const & table);

  void begin_transaction(bool exclusive);
  void commit_transaction();
  void rollback_transaction();
  friend class transaction_guard;
  friend void rcs_put_raw_file_edge(hexenc<id> const & old_id,
                                    hexenc<id> const & new_id,
                                    delta const & del,
                                    database & db);

  void put_roster(revision_id const & rev_id,
                  roster_t & roster,
                  marking_map & marks);

  void check_filename();
  void check_db_exists();
  void open();
  void close();

public:

  database(system_path const & file);

  void set_filename(system_path const & file);
  void initialize();
  void debug(std::string const & sql, std::ostream & out);
  void dump(std::ostream &);
  void load(std::istream &);
  void info(std::ostream &);
  void version(std::ostream &);
  void migrate();
  void ensure_open();
  void ensure_open_for_format_changes();
  bool database_specified();
  
  bool file_version_exists(file_id const & id);
  bool roster_version_exists(hexenc<id> const & id);
  bool revision_exists(revision_id const & id);
  bool roster_link_exists_for_revision(revision_id const & id);
  bool roster_exists_for_revision(revision_id const & id);

  void get_roster_links(std::map<revision_id, hexenc<id> > & links);
  void get_file_ids(std::set<file_id> & ids);
  void get_revision_ids(std::set<revision_id> & ids);
  void get_roster_ids(std::set< hexenc<id> > & ids) ;

  void set_app(app_state * app);
  
  // get plain version if it exists, or reconstruct version
  // from deltas (if they exist)
  void get_file_version(file_id const & id,
                        file_data & dat);

  // put file w/o predecessor into db
  void put_file(file_id const & new_id,
                file_data const & dat);

  // store new version and update old version to be a delta
  void put_file_version(file_id const & old_id,
                        file_id const & new_id,
                        file_delta const & del);

  // get plain version if it exists, or reconstruct version
  // from deltas (if they exist). 
  void get_manifest_version(manifest_id const & id,
                            manifest_data & dat);

  void get_revision_ancestry(std::multimap<revision_id, revision_id> & graph);

  void get_revision_parents(revision_id const & id,
                           std::set<revision_id> & parents);

  void get_revision_children(revision_id const & id,
                             std::set<revision_id> & children);

  void get_revision_manifest(revision_id const & cid,
                             manifest_id & mid);

  void deltify_revision(revision_id const & rid);

  void get_revision(revision_id const & id,
                   revision_set & cs);

  void get_revision(revision_id const & id,
                   revision_data & dat);

  void put_revision(revision_id const & new_id,
                    revision_set const & rev);

  void put_revision(revision_id const & new_id,
                    revision_data const & dat);
  
  void delete_existing_revs_and_certs();

  void delete_existing_manifests();

  void delete_existing_rev_and_certs(revision_id const & rid);
  
  void delete_branch_named(cert_value const & branch);

  void delete_tag_named(cert_value const & tag);

  // crypto key / cert operations

  void get_key_ids(std::string const & pattern,
                   std::vector<rsa_keypair_id> & pubkeys);

  void get_public_keys(std::vector<rsa_keypair_id> & pubkeys);

  bool public_key_exists(hexenc<id> const & hash);
  bool public_key_exists(rsa_keypair_id const & id);

  
  void get_pubkey(hexenc<id> const & hash, 
                  rsa_keypair_id & id,
                  base64<rsa_pub_key> & pub_encoded);

  void get_key(rsa_keypair_id const & id, 
               base64<rsa_pub_key> & pub_encoded);

  void put_key(rsa_keypair_id const & id, 
               base64<rsa_pub_key> const & pub_encoded);

  void delete_public_key(rsa_keypair_id const & pub_id);
  
  // note: this section is ridiculous. please do something about it.

  bool revision_cert_exists(revision<cert> const & cert);
  bool revision_cert_exists(hexenc<id> const & hash);

  void put_revision_cert(revision<cert> const & cert);

  // this variant has to be rather coarse and fast, for netsync's use
  void get_revision_cert_nobranch_index(std::vector< std::pair<hexenc<id>,
                               std::pair<revision_id, rsa_keypair_id> > > & idx);

  void get_revision_certs(std::vector< revision<cert> > & certs);

  void get_revision_certs(cert_name const & name, 
                          std::vector< revision<cert> > & certs);

  void get_revision_certs(revision_id const & id, 
                          cert_name const & name, 
                          std::vector< revision<cert> > & certs);

  void get_revision_certs(cert_name const & name,
                          base64<cert_value> const & val, 
                          std::vector< revision<cert> > & certs);

  void get_revision_certs(revision_id const & id, 
                          cert_name const & name, 
                          base64<cert_value> const & value,
                          std::vector< revision<cert> > & certs);

  void get_revision_certs(revision_id const & id, 
                          std::vector< revision<cert> > & certs);

  void get_revision_certs(revision_id const & id, 
                          std::vector< hexenc<id> > & hashes);

  void get_revision_cert(hexenc<id> const & hash,
                         revision<cert> & c);
  
  void get_manifest_certs(manifest_id const & id, 
                          std::vector< manifest<cert> > & certs);

  void get_manifest_certs(cert_name const & name, 
                          std::vector< manifest<cert> > & certs);

  // epochs 

  void get_epochs(std::map<cert_value, epoch_data> & epochs);

  void get_epoch(epoch_id const & eid, cert_value & branch, epoch_data & epo);
  
  bool epoch_exists(epoch_id const & eid);

  void set_epoch(cert_value const & branch, epoch_data const & epo);  

  void clear_epoch(cert_value const & branch);
 
  // vars

  void get_vars(std::map<var_key, var_value > & vars);

  void get_var(var_key const & key, var_value & value);

  bool var_exists(var_key const & key);

  void set_var(var_key const & key, var_value const & value);

  void clear_var(var_key const & key);

  // branches
  void get_branches(std::vector<std::string> & names);

  // roster and node_id stuff
  void get_roster_id_for_revision(revision_id const & rev_id,
                                  hexenc<id> & roster_id);

  void get_roster(revision_id const & rid, 
                  roster_t & roster);

  void get_roster(revision_id const & rid, 
                  roster_t & roster,
                  marking_map & marks);

  void get_roster(hexenc<id> const & roster_id,
                  data & dat);

  void get_uncommon_ancestors(revision_id const & a,
                              revision_id const & b,
                              std::set<revision_id> & a_uncommon_ancs,
                              std::set<revision_id> & b_uncommon_ancs);
                              
  node_id next_node_id();
  
  // completion stuff

  void complete(std::string const & partial,
                std::set<revision_id> & completions);

  void complete(std::string const & partial,
                std::set<file_id> & completions);

  void complete(std::string const & partial,
                std::set< std::pair<key_id, utf8 > > & completions);

  void complete(selectors::selector_type ty,
                std::string const & partial,
                std::vector<std::pair<selectors::selector_type, 
                                      std::string> > const & limit,
                std::set<std::string> & completions);
  
  ~database();

};

// Transaction guards nest. Acquire one in any scope you'd like
// transaction-protected, and it'll make sure the db aborts a transaction
// if there's any exception before you call commit().
//
// By default, transaction_guard locks the database exclusively. If the
// transaction is intended to be read-only, construct the guard with
// exclusive=false. In this case, if a database update is attempted and
// another process is accessing the database an exception will be thrown -
// uglier and more confusing for the user - however no data inconsistency
// should result.
// 
// An exception is thrown if an exclusive transaction_guard is created
// while a non-exclusive transaction_guard exists.
//
// Transaction guards also support splitting long transactions up into
// checkpoints. Any time you feel the database is in an
// acceptably-consistent state, you can call maybe_checkpoint(nn) with a
// given number of bytes. When the number of bytes and number of
// maybe_checkpoint() calls exceeds the guard's parameters, the transaction
// is committed and reopened. Any time you feel the database has reached a
// point where want to ensure a transaction commit, without destructing the
// object, you can call do_checkpoint().
//
// This does *not* free you from having to call .commit() on the guard when
// it "completes" its lifecycle. Here's a way to think of checkpointing: a
// normal transaction guard is associated with a program-control
// scope. Sometimes (notably in netsync) it is not convenient to create a
// scope which exactly matches the size of work-unit you want to commit (a
// bunch of packets, or a session-close, whichever comes first) so
// checkpointing allows you to use a long-lived transaction guard and mark
// off the moments where commits are desired, without destructing the
// guard. The guard still performs an error-management task in case of an
// exception, so you still have to clean it before destruction using
// .commit().
//
// Checkpointing also does not override the transaction guard nesting: if
// there's an enclosing transaction_guard, your checkpointing calls have no
// affect.
//
// The purpose of checkpointing is to provide an alternative to "many short
// transactions" on platforms (OSX in particular) where the overhead of
// full commits at high frequency is too high. The solution for these
// platforms is to run inside a longer-lived transaction (session-length),
// and checkpoint at higher granularity (every megabyte or so).

class transaction_guard
{
  bool committed;
  database & db;
  bool exclusive;
  size_t const checkpoint_batch_size;
  size_t const checkpoint_batch_bytes;
  size_t checkpointed_calls;
  size_t checkpointed_bytes;
public:
  transaction_guard(database & d, bool exclusive=true,
                    size_t checkpoint_batch_size=100,
                    size_t checkpoint_batch_bytes=0xfffff);
  ~transaction_guard();
  void do_checkpoint();
  void maybe_checkpoint(size_t nbytes);
  void commit();
};


void
close_all_databases();


#endif // __DATABASE_HH__
