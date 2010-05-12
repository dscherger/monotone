// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __DATABASE_HH__
#define __DATABASE_HH__

#include "vector.hh"
#include <set>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <botan/version.h>

#include "rev_types.hh"
#include "cert.hh"

class app_state;
class lua_hooks;
struct date_t;
struct globish;
class key_store;
class outdated_indicator;
class rev_height;
class lazy_rng;
class project_t;

class migration_status;

typedef std::pair<var_domain, var_name> var_key;
typedef enum {cert_ok, cert_bad, cert_unknown} cert_status;

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

class database_impl;
struct key_identity_info;

class database
{
  //
  // --== Opening the database and schema checking ==--
  //
public:
  explicit database(app_state &);
  ~database();

  system_path get_filename();
  bool is_dbfile(any_path const & file);
  bool database_specified();
  void check_is_not_rosterified();

  void ensure_open();
  void ensure_open_for_format_changes();
  void ensure_open_for_cache_reset();
private:
  void ensure_open_for_maintenance();

  //
  // --== Transactions ==--
  //
private:
  friend class conditional_transaction_guard;

  //
  // --== Reading/writing delta-compressed objects ==--
  //
public:
  bool file_version_exists(file_id const & ident);
  bool revision_exists(revision_id const & ident);
  bool roster_link_exists_for_revision(revision_id const & ident);
  bool roster_exists_for_revision(revision_id const & ident);


  // get plain version if it exists, or reconstruct version
  // from deltas (if they exist)
  void get_file_version(file_id const & ident,
                        file_data & dat);

  // put file w/o predecessor into db
  void put_file(file_id const & new_id,
                file_data const & dat);

  // store new version and update old version to be a delta
  void put_file_version(file_id const & old_id,
                        file_id const & new_id,
                        file_delta const & del);

  void get_arbitrary_file_delta(file_id const & src_id,
                                file_id const & dst_id,
                                file_delta & del);

  // get plain version if it exists, or reconstruct version
  // from deltas (if they exist).
  void get_manifest_version(manifest_id const & ident,
                            manifest_data & dat);

private:
  bool file_or_manifest_base_exists(file_id const & ident,
                                    std::string const & table);
  bool delta_exists(id const & ident,
                    std::string const & table);
  void put_file_delta(file_id const & ident,
                      file_id const & base,
                      file_delta const & del);

  friend void rcs_put_raw_file_edge(database & db,
                                    file_id const & old_id,
                                    file_id const & new_id,
                                    delta const & del);


  //
  // --== The ancestry graph ==--
  //
public:
  void get_forward_ancestry(rev_ancestry_map & graph);
  void get_reverse_ancestry(rev_ancestry_map & graph);

  void get_revision_parents(revision_id const & ident,
                           std::set<revision_id> & parents);

  void get_revision_children(revision_id const & ident,
                             std::set<revision_id> & children);

  void get_leaves(std::set<revision_id> & leaves);

  void get_revision_manifest(revision_id const & cid,
                             manifest_id & mid);

  void get_common_ancestors(std::set<revision_id> const & revs,
                            std::set<revision_id> & common_ancestors);

  bool is_a_ancestor_of_b(revision_id const & ancestor,
                          revision_id const & child);

  void get_revision_ids(std::set<revision_id> & ids);
  // this is exposed for 'db check':
  void get_file_ids(std::set<file_id> & ids);

  //
  // --== Revision reading/writing ==--
  //
public:
  void get_revision(revision_id const & ident,
                    revision_t & cs);

  void get_revision(revision_id const & ident,
                    revision_data & dat);

  bool put_revision(revision_id const & new_id,
                    revision_t const & rev);

  bool put_revision(revision_id const & new_id,
                    revision_data const & dat);

private:
  void deltify_revision(revision_id const & rid);

  //
  // --== Rosters ==--
  //
public:
  node_id next_node_id();

  void get_roster(revision_id const & rid,
                  roster_t & roster);

  void get_roster(revision_id const & rid,
                  roster_t & roster,
                  marking_map & marks);

  void get_roster(revision_id const & rid,
                  cached_roster & cr);

  // these are exposed for the use of database_check.cc
  bool roster_version_exists(revision_id const & ident);
  void get_roster_ids(std::set<revision_id> & ids);

  // using roster deltas
  void get_markings(revision_id const & id,
                    node_id const & nid,
                    const_marking_t & markings);

  void get_file_content(revision_id const & id,
                        node_id const & nid,
                        file_id & content);

private:
  void get_roster_version(revision_id const & ros_id,
                          cached_roster & cr);

  void put_roster(revision_id const & rev_id,
                  revision_t const & rev,
                  roster_t_cp const & roster,
                  marking_map_cp const & marking);

  //
  // --== Keys ==--
  //
public:
  void get_key_ids(std::vector<key_id> & pubkeys);

  void get_public_keys(std::vector<key_name> & pubkeys);

  bool public_key_exists(key_id const & hash);
  bool public_key_exists(key_name const & ident);

  void get_pubkey(key_id const & hash,
                  key_name & ident,
                  rsa_pub_key & pub);

  void get_key(key_id const & ident, rsa_pub_key & pub);
  bool put_key(key_name const & ident, rsa_pub_key const & pub);

  void delete_public_key(key_id const & pub_id);

  // Crypto operations

  void encrypt_rsa(key_id const & pub_id,
                   std::string const & plaintext,
                   rsa_oaep_sha_data & ciphertext);

  cert_status check_signature(key_id const & id,
                              std::string const & alleged_text,
                              rsa_sha1_signature const & signature);
  cert_status check_cert(cert const & t);

  //
  // --== Certs ==--
  //
  // note: this section is ridiculous. please do something about it.
public:
  bool revision_cert_exists(cert const & cert);
  bool revision_cert_exists(revision_id const & hash);

  bool put_revision_cert(cert const & cert);
  void record_as_branch_leaf(cert_value const & branch, revision_id const & rev);

  // this variant has to be rather coarse and fast, for netsync's use
  outdated_indicator get_revision_cert_nobranch_index(std::vector< std::pair<revision_id,
                              std::pair<revision_id, key_id> > > & idx);

  // Only used by database_check.cc
  outdated_indicator get_revision_certs(std::vector<cert> & certs);

  outdated_indicator get_revision_certs(cert_name const & name,
                          std::vector<cert> & certs);

  outdated_indicator get_revision_certs(revision_id const & ident,
                          cert_name const & name,
                          std::vector<cert> & certs);

  // Only used by get_branch_certs (project.cc)
  outdated_indicator get_revision_certs(cert_name const & name,
                                        cert_value const & val,
                                        std::vector<std::pair<id, cert> > & certs);

  // Only used by revision_is_in_branch (project.cc)
  outdated_indicator get_revision_certs(revision_id const & ident,
                          cert_name const & name,
                          cert_value const & value,
                          std::vector<cert> & certs);

  // Only used by get_branch_heads (project.cc)
  outdated_indicator get_revisions_with_cert(cert_name const & name,
                               cert_value const & value,
                               std::set<revision_id> & revisions);

  // Used by get_branch_heads (project.cc)
  // Will also be needed by daggy-refinement, if/when implemented
  outdated_indicator get_branch_leaves(cert_value const & value,
                                       std::set<revision_id> & revisions);

private:
  void recalc_branch_leaves(cert_value const & value);
public:

  // Used through project.cc
  outdated_indicator get_revision_certs(revision_id const & ident,
                          std::vector<cert> & certs);

  // Used through get_revision_cert_hashes (project.cc)
  outdated_indicator get_revision_certs(revision_id const & ident,
                          std::vector<id> & hashes);

  void get_revision_cert(id const & hash, cert & c);

  typedef boost::function<bool(std::set<key_id> const &,
                               id const &,
                               cert_name const &,
                               cert_value const &)> cert_trust_checker;
  // this takes a project_t so it can translate key names for the trust hook
  void erase_bogus_certs(project_t & project, std::vector<cert> & certs);
  // permit alternative trust functions
  void erase_bogus_certs(std::vector<cert> & certs,
                         cert_trust_checker const & checker);

  //
  // --== Epochs ==--
  //
public:
  void get_epochs(std::map<branch_name, epoch_data> & epochs);

  void get_epoch(epoch_id const & eid, branch_name & branch, epoch_data & epo);

  bool epoch_exists(epoch_id const & eid);

  void set_epoch(branch_name const & branch, epoch_data const & epo);

  void clear_epoch(branch_name const & branch);

  //
  // --== Database 'vars' ==--
  //
public:
  void get_vars(std::map<var_key, var_value > & vars);

  void get_var(var_key const & key, var_value & value);

  bool var_exists(var_key const & key);

  void set_var(var_key const & key, var_value const & value);

  void clear_var(var_key const & key);

  //
  // --== Completion ==--
  //
public:
  void prefix_matching_constraint(std::string const & colname,
                                   std::string const & prefix,
                                   std::string & constraint);

  void complete(std::string const & partial,
                std::set<revision_id> & completions);

  void complete(std::string const & partial,
                std::set<file_id> & completions);

  void complete(std::string const & partial,
                std::set< std::pair<key_id, utf8 > > & completions);

  //
  // --== Revision selectors ==--
  //
public:
  void select_parent(std::string const & partial,
                     std::set<revision_id> & completions);
  void select_cert(std::string const & certname,
                   std::set<revision_id> & completions);
  void select_cert(std::string const & certname, std::string const & certvalue,
                   std::set<revision_id> & completions);
  void select_author_tag_or_branch(std::string const & partial,
                                   std::set<revision_id> & completions);
  void select_date(std::string const & date, std::string const & comparison,
                   std::set<revision_id> & completions);

  //
  // --== The 'db' family of top-level commands ==--
  //
public:
  void initialize();
  void debug(std::string const & sql, std::ostream & out);
  void dump(std::ostream &);
  void load(std::istream &);
  void info(std::ostream &, bool analyze);
  void version(std::ostream &);
  void migrate(key_store &, migration_status &);
  void test_migration_step(key_store &, std::string const &);
  void fix_bad_certs(bool drop_not_fixable);
  // for kill_rev_locally:
  void delete_existing_rev_and_certs(revision_id const & rid);
  // for kill_branch_certs_locally:
  void delete_branch_named(cert_value const & branch);
  // for kill_tag_locally:
  void delete_tag_named(cert_value const & tag);

public:
  // branches
  outdated_indicator get_branches(std::vector<std::string> & names);
  outdated_indicator get_branches(globish const & glob,
                                  std::vector<std::string> & names);

  bool check_integrity();

  void get_uncommon_ancestors(revision_id const & a,
                              revision_id const & b,
                              std::set<revision_id> & a_uncommon_ancs,
                              std::set<revision_id> & b_uncommon_ancs);

  // for changesetify, rosterify
  void delete_existing_revs_and_certs();
  void delete_existing_manifests();

  void get_manifest_certs(manifest_id const & id, std::vector<cert> & certs);
  void get_manifest_certs(cert_name const & name, std::vector<cert> & certs);
  void get_revision_certs_with_keynames(revision_id const & id,
                                        std::vector<cert> & certs);

  // heights
  void get_rev_height(revision_id const & id,
                      rev_height & height);

  void put_rev_height(revision_id const & id,
                      rev_height const & height);

  bool has_rev_height(rev_height const & height);
  void delete_existing_heights();

  void put_height_for_revision(revision_id const & new_id,
                               revision_t const & rev);

  // for regenerate_rosters
  void delete_existing_rosters();
  void put_roster_for_revision(revision_id const & new_id,
                               revision_t const & rev);

private:
  boost::shared_ptr<database_impl> imp;
  lua_hooks & lua;
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
  boost::shared_ptr<lazy_rng> rng;
#endif
};

// not a member function, because it has to be called in non-db
// context as well
void resolve_managed_path(lua_hooks & lua, system_path & path);

// not a member function, defined in database_check.cc
void check_db(database & db);

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
//
// A conditional transaction guard is just like a transaction guard,
// except that it doesn't begin the transaction until you call acquire().
// If you don't call acquire(), you must not call commit(), do_checkpoint(),
// or maybe_checkpoint() either.
//
// Implementation note: Making transaction_guard inherit from
// conditional_transaction_guard means we can reuse all the latter's methods
// and just call acquire() in transaction_guard's constructor.  If we did it
// the other way around they would wind up being totally unrelated classes.

class conditional_transaction_guard
{
  database & db;
  size_t const checkpoint_batch_size;
  size_t const checkpoint_batch_bytes;
  size_t checkpointed_calls;
  size_t checkpointed_bytes;
  bool committed;
  bool acquired;
  bool const exclusive;
public:
  conditional_transaction_guard(database & db, bool exclusive=true,
                                size_t checkpoint_batch_size=1000,
                                size_t checkpoint_batch_bytes=0xfffff)
    : db(db),
      checkpoint_batch_size(checkpoint_batch_size),
      checkpoint_batch_bytes(checkpoint_batch_bytes),
      checkpointed_calls(0),
      checkpointed_bytes(0),
      committed(false), acquired(false), exclusive(exclusive)
  {}

  ~conditional_transaction_guard();
  void acquire();
  void commit();
  void do_checkpoint();
  void maybe_checkpoint(size_t nbytes);
};

class transaction_guard : public conditional_transaction_guard
{
public:
  transaction_guard(database & d, bool exclusive=true,
                    size_t checkpoint_batch_size=1000,
                    size_t checkpoint_batch_bytes=0xfffff)
    : conditional_transaction_guard(d, exclusive, checkpoint_batch_size,
                                    checkpoint_batch_bytes)
  {
    acquire();
  }
};

#endif // __DATABASE_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
