// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __NETSYNC_SESSION_HH__
#define __NETSYNC_SESSION_HH__

#include <memory>
#include <set>
#include <vector>

#include <boost/shared_ptr.hpp>

#include "enumerator.hh"
#include "netcmd.hh"
#include "refiner.hh"
#include "ui.hh"

#include "network/connection_info.hh"
#include "network/wrapped_session.hh"

class cert;

// A set of session innards that knows how to talk 'netsync'.
class
netsync_session:
  public refiner_callbacks,
  public enumerator_callbacks,
  public wrapped_session
{
  protocol_role role;
  globish our_include_pattern;
  globish our_exclude_pattern;
  globish_matcher our_matcher;

  project_t & project;
  key_store & keys;
  lua_hooks & lua;
  std::vector<key_id> keys_to_push;

  std::auto_ptr<ticker> byte_in_ticker;
  std::auto_ptr<ticker> byte_out_ticker;
  std::auto_ptr<ticker> cert_in_ticker;
  std::auto_ptr<ticker> cert_out_ticker;
  std::auto_ptr<ticker> revision_in_ticker;
  std::auto_ptr<ticker> revision_out_ticker;
  size_t bytes_in, bytes_out;
  size_t certs_in, certs_out;
  size_t revs_in, revs_out;
  size_t keys_in, keys_out;

  // These are read from the server, written to the local database
  std::vector<revision_id> written_revisions;
  std::vector<key_id> written_keys;
  std::vector<cert> written_certs;

  // These are sent to the server
  std::vector<revision_id> sent_revisions;
  std::vector<key_id> sent_keys;
  std::vector<cert> sent_certs;

  mutable bool set_totals;

  // Interface to refinement.
  refiner epoch_refiner;
  refiner key_refiner;
  refiner cert_refiner;
  refiner rev_refiner;

  // dry-run info
  bool is_dry_run;
  bool dry_run_keys_refined;
  shared_conn_info conn_info;
  bool dry_run_finished() const;

  // Interface to ancestry grovelling.
  revision_enumerator rev_enumerator;

  // Enumerator_callbacks methods.
  std::set<file_id> file_items_sent;
  bool process_this_rev(revision_id const & rev);
  bool queue_this_cert(id const & c);
  bool queue_this_file(id const & f);
  void note_file_data(file_id const & f);
  void note_file_delta(file_id const & src, file_id const & dst);
  void note_rev(revision_id const & rev);
  void note_cert(id const & c);

public:
  netsync_session(session * owner,
                  options & opts,
                  lua_hooks & lua,
                  project_t & project,
                  key_store & keys,
                  protocol_role role,
                  globish const & our_include_pattern,
                  globish const & our_exclude_pattern,
                  shared_conn_info info,
                  bool initiated_by_server = false);

  virtual ~netsync_session();

  std::string usher_reply_data() const;
  bool have_work() const;
  void accept_service();
  void request_service();
  void prepare_to_confirm(key_identity_info const & remote_key,
                          bool use_transport_auth);

  void on_begin(size_t ident, key_identity_info const & remote_key);
  void on_end(size_t ident);
private:

  void setup_client_tickers();
  bool done_all_refinements() const;
  bool queued_all_items() const;
  bool received_all_items() const;
  bool finished_working() const;
  void maybe_step();

  void note_item_arrived(netcmd_item_type ty, id const & i);
  void maybe_note_epochs_finished();
  void note_item_sent(netcmd_item_type ty, id const & i);

public:
  bool do_work(transaction_guard & guard,
               netcmd const * const cmd_in);
private:
  void note_bytes_in(int count);
  void note_bytes_out(int count);

  // Outgoing queue-writers.
  void queue_done_cmd(netcmd_item_type type, size_t n_items);
  void queue_refine_cmd(refinement_type ty, merkle_node const & node);
  void queue_data_cmd(netcmd_item_type type,
                      id const & item,
                      std::string const & dat);
  void queue_delta_cmd(netcmd_item_type type,
                       id const & base,
                       id const & ident,
                       delta const & del);

  // Incoming dispatch-called methods.
  bool process_hello_cmd(u8 server_version,
                         key_name const & server_keyname,
                         rsa_pub_key const & server_key,
                         id const & nonce);
  bool process_refine_cmd(refinement_type ty, merkle_node const & node);
  bool process_done_cmd(netcmd_item_type type, size_t n_items);
  bool process_data_cmd(netcmd_item_type type,
                        id const & item,
                        std::string const & dat);
  bool process_delta_cmd(netcmd_item_type type,
                         id const & base,
                         id const & ident,
                         delta const & del);

  // The incoming dispatcher.
  bool dispatch_payload(netcmd const & cmd,
                        transaction_guard & guard);

  // Various helpers.
  bool data_exists(netcmd_item_type type,
                   id const & item);
  void load_data(netcmd_item_type type,
                 id const & item,
                 std::string & out);

  void rebuild_merkle_trees(std::set<branch_name> const & branches);

  void send_all_data(netcmd_item_type ty, std::set<id> const & items);
private:
  bool process(transaction_guard & guard,
               netcmd const & cmd_in);

  bool initiated_by_server;
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
