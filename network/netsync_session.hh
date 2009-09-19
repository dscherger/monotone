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

#include "network/session_base.hh"

class cert;

class
netsync_session:
  public refiner_callbacks,
  public enumerator_callbacks,
  public session_base
{
  u8 version;
  u8 max_version;
  u8 min_version;
  protocol_role role;
  protocol_voice const voice;
  globish our_include_pattern;
  globish our_exclude_pattern;
  globish_matcher our_matcher;

  project_t & project;
  key_store & keys;
  lua_hooks & lua;
  bool use_transport_auth;
  key_id const & signing_key;
  std::vector<key_id> keys_to_push;

  netcmd cmd_in;
  bool armed;
public:
  bool arm();
private:

  bool received_remote_key;
  key_id remote_peer_key_id;
  netsync_session_key session_key;
  chained_hmac read_hmac;
  chained_hmac write_hmac;
  bool authenticated;

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
  // used to identify this session to the netsync hooks.
  // We can't just use saved_nonce, because that's blank for all
  // anonymous connections and could lead to confusion.
  size_t session_id;
  static size_t session_count;

  // These are read from the server, written to the local database
  std::vector<revision_id> written_revisions;
  std::vector<key_id> written_keys;
  std::vector<cert> written_certs;

  // These are sent to the server
  std::vector<revision_id> sent_revisions;
  std::vector<key_id> sent_keys;
  std::vector<cert> sent_certs;

  id saved_nonce;

  static const int no_error = 200;
  static const int partial_transfer = 211;
  static const int no_transfer = 212;

  static const int not_permitted = 412;
  static const int unknown_key = 422;
  static const int mixing_versions = 432;

  static const int role_mismatch = 512;
  static const int bad_command = 521;

  static const int failed_identification = 532;
  //static const int bad_data = 541;

  int error_code;

  bool set_totals;

  // Interface to refinement.
  refiner epoch_refiner;
  refiner key_refiner;
  refiner cert_refiner;
  refiner rev_refiner;

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
  netsync_session(options & opts,
                  lua_hooks & lua,
                  project_t & project,
                  key_store & keys,
                  protocol_role role,
                  protocol_voice voice,
                  globish const & our_include_pattern,
                  globish const & our_exclude_pattern,
                  std::string const & peer,
                  boost::shared_ptr<Netxx::StreamBase> sock,
                  bool initiated_by_server = false);

  virtual ~netsync_session();
private:

  id mk_nonce();

  void set_session_key(std::string const & key);
  void set_session_key(rsa_oaep_sha_data const & key_encrypted);

  void setup_client_tickers();
  bool done_all_refinements();
  bool queued_all_items();
  bool received_all_items();
  bool finished_working();
  void maybe_step();
  void maybe_say_goodbye(transaction_guard & guard);

  void note_item_arrived(netcmd_item_type ty, id const & i);
  void maybe_note_epochs_finished();
  void note_item_sent(netcmd_item_type ty, id const & i);

public:
  bool do_work(transaction_guard & guard);
private:
  void note_bytes_in(int count);
  void note_bytes_out(int count);

  void error(int errcode, std::string const & errmsg);

  void write_netcmd_and_try_flush(netcmd const & cmd);

  // Outgoing queue-writers.
  void queue_usher_cmd(utf8 const & message);
  void queue_bye_cmd(u8 phase);
  void queue_error_cmd(std::string const & errmsg);
  void queue_done_cmd(netcmd_item_type type, size_t n_items);
  void queue_hello_cmd(key_name const & key_name,
                       rsa_pub_key const & pub_encoded,
                       id const & nonce);
  void queue_anonymous_cmd(protocol_role role,
                           globish const & include_pattern,
                           globish const & exclude_pattern,
                           id const & nonce2);
  void queue_auth_cmd(protocol_role role,
                      globish const & include_pattern,
                      globish const & exclude_pattern,
                      key_id const & client,
                      id const & nonce1,
                      id const & nonce2,
                      rsa_sha1_signature const & signature);
  void queue_confirm_cmd();
  void queue_refine_cmd(refinement_type ty, merkle_node const & node);
  void queue_data_cmd(netcmd_item_type type,
                      id const & item,
                      std::string const & dat);
  void queue_delta_cmd(netcmd_item_type type,
                       id const & base,
                       id const & ident,
                       delta const & del);

  // Incoming dispatch-called methods.
  bool process_error_cmd(std::string const & errmsg);
  bool process_hello_cmd(u8 server_version,
                         key_name const & server_keyname,
                         rsa_pub_key const & server_key,
                         id const & nonce);
  bool process_bye_cmd(u8 phase, transaction_guard & guard);
  bool process_anonymous_cmd(protocol_role role,
                             globish const & their_include_pattern,
                             globish const & their_exclude_pattern);
  bool process_auth_cmd(protocol_role role,
                        globish const & their_include_pattern,
                        globish const & their_exclude_pattern,
                        key_id const & client,
                        id const & nonce1,
                        rsa_sha1_signature const & signature);
  bool process_refine_cmd(refinement_type ty, merkle_node const & node);
  bool process_done_cmd(netcmd_item_type type, size_t n_items);
  bool process_data_cmd(netcmd_item_type type,
                        id const & item,
                        std::string const & dat);
  bool process_delta_cmd(netcmd_item_type type,
                         id const & base,
                         id const & ident,
                         delta const & del);
  bool process_usher_cmd(utf8 const & msg);
  bool process_usher_reply_cmd(u8 client_version,
                               utf8 const & server,
                               globish const & pattern);

  // The incoming dispatcher.
  bool dispatch_payload(netcmd const & cmd,
                        transaction_guard & guard);

  // Various helpers.
  void assume_corresponding_role(protocol_role their_role);
  void respond_to_confirm_cmd();
  bool data_exists(netcmd_item_type type,
                   id const & item);
  void load_data(netcmd_item_type type,
                 id const & item,
                 std::string & out);

  void rebuild_merkle_trees(std::set<branch_name> const & branches);

  void send_all_data(netcmd_item_type ty, std::set<id> const & items);
public:
  void begin_service();
private:
  bool process(transaction_guard & guard);

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
