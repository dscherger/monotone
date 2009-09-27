// Copyright (C) 2009 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __SESSION_HH__
#define __SESSION_HH__

#include "network/session_base.hh"
#include "network/wrapped_session.hh"

#include "netcmd.hh"
#include "vocab.hh"

class key_store;
class lua_hooks;
class options;
class project_t;

class session : public session_base
{
  u8 version;
  u8 max_version;
  u8 min_version;

  bool use_transport_auth;
  key_id const & signing_key;

  netcmd cmd_in;
  bool armed;

  bool received_remote_key;
  key_id remote_peer_key_id;
  netsync_session_key session_key;
  chained_hmac read_hmac;
  chained_hmac write_hmac;
  bool authenticated;

  void set_session_key(std::string const & key);
  void set_session_key(rsa_oaep_sha_data const & key_encrypted);

  id hello_nonce;
  id saved_nonce;
  id mk_nonce();

  bool completed_hello;

  int error_code;

  size_t session_id;
  static size_t session_num;

  options & opts;
  lua_hooks & lua;
  project_t & project;
  key_store & keys;
  std::string peer;
  boost::shared_ptr<wrapped_session> wrapped;

  void queue_bye_cmd(u8 phase);
  bool process_bye_cmd(u8 phase, transaction_guard & guard);

  bool handle_service_request();
public:
  session(options & opts, lua_hooks & lua, project_t & project,
          key_store & keys,
          protocol_voice voice,
          std::string const & peer,
          boost::shared_ptr<Netxx::StreamBase> sock,
          bool use_transport_auth = true);
  ~session();
  void set_inner(boost::shared_ptr<wrapped_session> wrapped);

  bool arm();
  bool do_work(transaction_guard & guard);
  void begin_service();

  void write_netcmd(netcmd const & cmd);
  u8 get_version() const;
  protocol_voice get_voice() const;
  std::string get_peer() const;

  void request_netsync(protocol_role role,
                       globish const & our_include_pattern,
                       globish const & our_exclude_pattern);
  void request_automate();

  // This method triggers a special "error unwind" mode to netsync.  In this
  // mode, all received data is ignored, and no new data is queued.  We simply
  // stay connected long enough for the current write buffer to be flushed, to
  // ensure that our peer receives the error message.
  // Affects read_some, write_some, and process .
  void error(int errcode, std::string const & message);
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
