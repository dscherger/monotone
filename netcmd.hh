// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __NETCMD_HH__
#define __NETCMD_HH__

#include "vector.hh"
#include <list>
#include <utility>
#include <iostream>

#include "globish.hh"
#include "merkle_tree.hh"
#include "numeric_vocab.hh"
#include "uri.hh"
#include "vocab.hh"
#include "hmac.hh"
#include "string_queue.hh"

struct globish;
class database;
class project_t;
class key_store;
class lua_hooks;
class options;

class app_state;

namespace error_codes {
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
}

typedef enum
  {
    server_voice,
    client_voice
  }
protocol_voice;

typedef enum
  {
    source_role = 1,
    sink_role = 2,
    source_and_sink_role = 3
  }
protocol_role;

typedef enum
  {
    refinement_query = 0,
    refinement_response = 1
  }
refinement_type;

typedef enum
  {
    // general commands
    error_cmd = 0,
    bye_cmd = 1,

    // authentication commands
    hello_cmd = 2,
    anonymous_cmd = 3,
    auth_cmd = 4,
    confirm_cmd = 5,

    // refinement commands
    refine_cmd = 6,
    done_cmd = 7,

    // transmission commands
    data_cmd = 8,
    delta_cmd = 9,

    // automation commands
    automate_cmd = 10,
    automate_command_cmd = 11,
    automate_packet_cmd = 12,

    // usher commands
    // usher_cmd is sent either by a proxy that needs to know where
    // to forward a connection (the reply gives the desired hostname and
    // include pattern), or by a server performing protocol
    // version negotiation.
    usher_cmd = 100,
    usher_reply_cmd = 101
  }
netcmd_code;

class netcmd
{
private:
  u8 version;
  netcmd_code cmd_code;
  std::string payload;
public:
  explicit netcmd(u8 ver);
  netcmd_code get_cmd_code() const {return cmd_code;}
  u8 get_version() const { return version; }
  size_t encoded_size() const;
  bool operator==(netcmd const & other) const;


  // basic cmd i/o (including checksums)
  void write(std::string & out,
             chained_hmac & hmac) const;
  bool read(u8 min_version, u8 max_version,
            string_queue & inbuf,
            chained_hmac & hmac);
  bool read_string(std::string & inbuf,
                   chained_hmac & hmac) {
    // this is here only for the regression tests because they want to
    // read and write to the same type, but we want to have reads from
    // a string queue so that when data is read in from the network it
    // can be processed efficiently
    string_queue tmp(inbuf.size());
    tmp.append(inbuf);
    // allow any version
    bool ret = read(0, 255, tmp, hmac);
    inbuf = tmp.substr(0,tmp.size());
    return ret;
  }
  // i/o functions for each type of command payload
  void read_error_cmd(std::string & errmsg) const;
  void write_error_cmd(std::string const & errmsg);

  void read_hello_cmd(u8 & server_version,
                      key_name & server_keyname,
                      rsa_pub_key & server_key,
                      id & nonce) const;
  void write_hello_cmd(key_name const & server_keyname,
                       rsa_pub_key const & server_key,
                       id const & nonce);

  void read_bye_cmd(u8 & phase) const;
  void write_bye_cmd(u8 phase);

  void read_anonymous_cmd(protocol_role & role,
                          globish & include_pattern,
                          globish & exclude_pattern,
                          rsa_oaep_sha_data & hmac_key_encrypted) const;
  void write_anonymous_cmd(protocol_role role,
                           globish const & include_pattern,
                           globish const & exclude_pattern,
                           rsa_oaep_sha_data const & hmac_key_encrypted);

  void read_auth_cmd(protocol_role & role,
                     globish & include_pattern,
                     globish & exclude_pattern,
                     key_id & client,
                     id & nonce1,
                     rsa_oaep_sha_data & hmac_key_encrypted,
                     rsa_sha1_signature & signature) const;
  void write_auth_cmd(protocol_role role,
                      globish const & include_pattern,
                      globish const & exclude_pattern,
                      key_id const & client,
                      id const & nonce1,
                      rsa_oaep_sha_data const & hmac_key_encrypted,
                      rsa_sha1_signature const & signature);

  void read_confirm_cmd() const;
  void write_confirm_cmd();

  void read_refine_cmd(refinement_type & ty, merkle_node & node) const;
  void write_refine_cmd(refinement_type ty, merkle_node const & node);

  void read_done_cmd(netcmd_item_type & type, size_t & n_items) const;
  void write_done_cmd(netcmd_item_type type, size_t n_items);

  void read_data_cmd(netcmd_item_type & type,
                     id & item,
                     std::string & dat) const;
  void write_data_cmd(netcmd_item_type type,
                      id const & item,
                      std::string const & dat);

  void read_delta_cmd(netcmd_item_type & type,
                      id & base, id & ident,
                      delta & del) const;
  void write_delta_cmd(netcmd_item_type & type,
                       id const & base, id const & ident,
                       delta const & del);

  void read_automate_cmd(key_id & client,
                         id & nonce1,
                         rsa_oaep_sha_data & hmac_key_encrypted,
                         rsa_sha1_signature & signature) const;
  void write_automate_cmd(key_id const & client,
                          id const & nonce1,
                          rsa_oaep_sha_data & hmac_key_encrypted,
                          rsa_sha1_signature & signature);
  void read_automate_command_cmd(std::vector<std::string> & args,
                                 std::vector<std::pair<std::string, std::string> > & opts) const;
  void write_automate_command_cmd(std::vector<std::string> const & args,
                                  std::vector<std::pair<std::string, std::string> > const & opts);
  void read_automate_packet_cmd(int & command_num,
                                int & err_code,
                                bool & last,
                                std::string & packet_data) const;
  void write_automate_packet_cmd(int command_num,
                                 int err_code,
                                 bool last,
                                 std::string const & packet_data);

  void read_usher_cmd(utf8 & greeting) const;
  void write_usher_cmd(utf8 const & greeting);
  void read_usher_reply_cmd(u8 & version, utf8 & server, std::string & pattern) const;
  void write_usher_reply_cmd(utf8 const & server, std::string const & pattern);

};

struct netsync_connection_info
{
  struct Server
  {
    std::list<utf8> addrs;
  } server;
  enum conn_type
    {
      netsync_connection,
      automate_connection
    };
  struct Client
  {
    globish include_pattern;
    globish exclude_pattern;
    uri_t uri;
    utf8 unparsed;
    std::vector<std::string> argv;
    bool use_argv;
    conn_type connection_type;
    std::istream & stdio_input_stream;
    Client() : stdio_input_stream(std::cin) {}
  } client;
};

void run_netsync_protocol(app_state & app,
                          options & opts, lua_hooks & lua,
                          project_t & project, key_store & keys,
                          protocol_voice voice,
                          protocol_role role,
                          netsync_connection_info const & info);

#endif // __NETCMD_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
