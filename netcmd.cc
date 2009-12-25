// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "vector.hh"
#include <utility>

#include "constants.hh"
#include "netcmd.hh"
#include "netio.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "hmac.hh"
#include "globish.hh"

using std::pair;
using std::string;
using std::vector;

static netcmd_item_type
read_netcmd_item_type(string const & in,
                      size_t & pos,
                      string const & name)
{
  u8 tmp = extract_datum_lsb<u8>(in, pos, name);
  switch (tmp)
    {
    case static_cast<u8>(revision_item):
      return revision_item;
    case static_cast<u8>(file_item):
      return file_item;
    case static_cast<u8>(cert_item):
      return cert_item;
    case static_cast<u8>(key_item):
      return key_item;
    case static_cast<u8>(epoch_item):
      return epoch_item;
    default:
      throw bad_decode(F("unknown item type 0x%x for '%s'")
                       % static_cast<int>(tmp) % name);
    }
}

netcmd::netcmd(u8 ver)
  : version(ver),
    cmd_code(error_cmd)
{}

size_t netcmd::encoded_size() const
{
  string tmp;
  insert_datum_uleb128<size_t>(payload.size(), tmp);
  return 1 + 1 + tmp.size() + payload.size() + 4;
}

bool
netcmd::operator==(netcmd const & other) const
{
  return version == other.version &&
    cmd_code == other.cmd_code &&
    payload == other.payload;
}

// note: usher_reply_cmd does not get included in the hmac.
void
netcmd::write(string & out, chained_hmac & hmac) const
{
  size_t oldlen = out.size();
  out += static_cast<char>(version);
  out += static_cast<char>(cmd_code);
  insert_variable_length_string(payload, out);

  if (hmac.is_active() && cmd_code != usher_reply_cmd && cmd_code != usher_cmd)
    {
      string digest = hmac.process(out, oldlen);
      I(hmac.hmac_length == constants::netsync_hmac_value_length_in_bytes);
      out.append(digest);
    }
}

// note: usher_cmd does not get included in the hmac.
bool
netcmd::read(u8 min_version, u8 max_version,
             string_queue & inbuf, chained_hmac & hmac)
{
  size_t pos = 0;

  if (inbuf.size() < constants::netcmd_minsz)
    return false;

  u8 extracted_ver = extract_datum_lsb<u8>(inbuf, pos, "netcmd protocol number");
  bool too_old = extracted_ver < min_version;
  bool too_new = extracted_ver > max_version;

  u8 cmd_byte = extract_datum_lsb<u8>(inbuf, pos, "netcmd code");
  switch (cmd_byte)
    {
    case static_cast<u8>(hello_cmd):
    case static_cast<u8>(bye_cmd):
    case static_cast<u8>(anonymous_cmd):
    case static_cast<u8>(auth_cmd):
    case static_cast<u8>(error_cmd):
    case static_cast<u8>(confirm_cmd):
    case static_cast<u8>(refine_cmd):
    case static_cast<u8>(done_cmd):
    case static_cast<u8>(data_cmd):
    case static_cast<u8>(delta_cmd):
    case static_cast<u8>(automate_cmd):
    case static_cast<u8>(automate_headers_request_cmd):
    case static_cast<u8>(automate_headers_reply_cmd):
    case static_cast<u8>(automate_command_cmd):
    case static_cast<u8>(automate_packet_cmd):
    case static_cast<u8>(usher_cmd):
    case static_cast<u8>(usher_reply_cmd):
      cmd_code = static_cast<netcmd_code>(cmd_byte);
      break;
    default:
      // if the versions don't match, we will throw the more descriptive
      // error immediately after this switch.
      if (!too_old && !too_new)
        throw bad_decode(F("unknown netcmd code 0x%x")
                          % widen<u32,u8>(cmd_byte));
    }
  // check that the version is reasonable
  if (cmd_code != usher_cmd)
    {
      if (too_old || (cmd_code != usher_reply_cmd && too_new))
        {
          throw bad_decode(F("protocol version mismatch: wanted between '%d' and '%d' got '%d' (netcmd code %d)\n"
                             "%s")
                           % widen<u32,u8>(min_version)
                           % widen<u32,u8>(max_version)
                           % widen<u32,u8>(extracted_ver)
                           % widen<u32,u8>(cmd_code)
                           % ((max_version < extracted_ver)
                              ? _("the remote side has a newer, incompatible version of monotone")
                              : _("the remote side has an older, incompatible version of monotone")));
        }
    }
  version = extracted_ver;

  // check to see if we have even enough bytes for a complete uleb128
  size_t payload_len = 0;
  if (!try_extract_datum_uleb128<size_t>(inbuf, pos, "netcmd payload length",
      payload_len))
      return false;

  // they might have given us a bogus size
  if (payload_len > constants::netcmd_payload_limit)
    throw bad_decode(F("oversized payload of '%d' bytes") % payload_len);

  // there might not be enough data yet in the input buffer
  unsigned int minsize;
  if (hmac.is_active() && cmd_code != usher_cmd && cmd_code != usher_reply_cmd)
    minsize = pos + payload_len + constants::netsync_hmac_value_length_in_bytes;
  else
    minsize = pos + payload_len;

  if (inbuf.size() < minsize)
    {
      return false;
    }

  string digest;
  string cmd_digest;

  if (hmac.is_active() && cmd_code != usher_cmd && cmd_code != usher_reply_cmd)
    {
      // grab it before the data gets munged
      I(hmac.hmac_length == constants::netsync_hmac_value_length_in_bytes);
      digest = hmac.process(inbuf, 0, pos + payload_len);
    }

  payload = extract_substring(inbuf, pos, payload_len, "netcmd payload");

  if (hmac.is_active() && cmd_code != usher_cmd && cmd_code != usher_reply_cmd)
    {
      // they might have given us bogus data
      cmd_digest = extract_substring(inbuf, pos,
                                     constants::netsync_hmac_value_length_in_bytes,
                                     "netcmd HMAC");
    }

  inbuf.pop_front(pos);

  if (hmac.is_active()
      && cmd_code != usher_cmd && cmd_code != usher_reply_cmd
      && cmd_digest != digest)
    {
      throw bad_decode(F("bad HMAC checksum (got %s, wanted %s)\n"
                         "this suggests data was corrupted in transit")
                       % encode_hexenc(cmd_digest, origin::network)
                       % encode_hexenc(digest, origin::network));
    }

  L(FL("read packet with code %d and version %d")
    % widen<u32>(cmd_code) % widen<u32>(version));

  return true;
}

////////////////////////////////////////////
// payload reader/writer functions follow //
////////////////////////////////////////////

void
netcmd::read_error_cmd(string & errmsg) const
{
  size_t pos = 0;
  // syntax is: <errmsg:vstr>
  extract_variable_length_string(payload, errmsg, pos, "error netcmd, message");
  assert_end_of_buffer(payload, pos, "error netcmd payload");
}

void
netcmd::write_error_cmd(string const & errmsg)
{
  cmd_code = error_cmd;
  payload.clear();
  insert_variable_length_string(errmsg, payload);
}


void
netcmd::read_hello_cmd(u8 & server_version,
                       key_name & server_keyname,
                       rsa_pub_key & server_key,
                       id & nonce) const
{
  server_version = version;
  size_t pos = 0;
  // syntax is: <server keyname:vstr> <server pubkey:vstr> <nonce:20 random bytes>
  string skn_str, sk_str;
  extract_variable_length_string(payload, skn_str, pos,
                                 "hello netcmd, server key name");
  server_keyname = key_name(skn_str, origin::network);
  extract_variable_length_string(payload, sk_str, pos,
                                 "hello netcmd, server key");
  server_key = rsa_pub_key(sk_str, origin::network);
  nonce = id(extract_substring(payload, pos,
                               constants::merkle_hash_length_in_bytes,
                               "hello netcmd, nonce"), origin::network);
  assert_end_of_buffer(payload, pos, "hello netcmd payload");
}

void
netcmd::write_hello_cmd(key_name const & server_keyname,
                        rsa_pub_key const & server_key,
                        id const & nonce)
{
  cmd_code = hello_cmd;
  payload.clear();
  I(nonce().size() == constants::merkle_hash_length_in_bytes);
  insert_variable_length_string(server_keyname(), payload);
  insert_variable_length_string(server_key(), payload);
  payload += nonce();
}


void
netcmd::read_bye_cmd(u8 & phase) const
{
  size_t pos = 0;
  // syntax is: <phase: 1 byte>
  phase = extract_datum_lsb<u8>(payload, pos, "bye netcmd, phase number");
  assert_end_of_buffer(payload, pos, "bye netcmd payload");
}


void
netcmd::write_bye_cmd(u8 phase)
{
  cmd_code = bye_cmd;
  payload.clear();
  payload += phase;
}


void
netcmd::read_anonymous_cmd(protocol_role & role,
                           globish & include_pattern,
                           globish & exclude_pattern,
                           rsa_oaep_sha_data & hmac_key_encrypted) const
{
  size_t pos = 0;
  // syntax is: <role:1 byte> <include_pattern: vstr> <exclude_pattern: vstr> <hmac_key_encrypted: vstr>
  u8 role_byte = extract_datum_lsb<u8>(payload, pos, "anonymous(hmac) netcmd, role");
  if (role_byte != static_cast<u8>(source_role)
      && role_byte != static_cast<u8>(sink_role)
      && role_byte != static_cast<u8>(source_and_sink_role))
    throw bad_decode(F("unknown role specifier %d") % widen<u32,u8>(role_byte));
  role = static_cast<protocol_role>(role_byte);
  string pattern_string;
  extract_variable_length_string(payload, pattern_string, pos,
                                 "anonymous(hmac) netcmd, include_pattern");
  include_pattern = globish(pattern_string, origin::network);
  extract_variable_length_string(payload, pattern_string, pos,
                                 "anonymous(hmac) netcmd, exclude_pattern");
  exclude_pattern = globish(pattern_string, origin::network);
  string hmac_key_string;
  extract_variable_length_string(payload, hmac_key_string, pos,
                                 "anonymous(hmac) netcmd, hmac_key_encrypted");
  hmac_key_encrypted = rsa_oaep_sha_data(hmac_key_string, origin::network);
  assert_end_of_buffer(payload, pos, "anonymous(hmac) netcmd payload");
}

void
netcmd::write_anonymous_cmd(protocol_role role,
                            globish const & include_pattern,
                            globish const & exclude_pattern,
                            rsa_oaep_sha_data const & hmac_key_encrypted)
{
  cmd_code = anonymous_cmd;
  payload += static_cast<char>(role);
  insert_variable_length_string(include_pattern(), payload);
  insert_variable_length_string(exclude_pattern(), payload);
  insert_variable_length_string(hmac_key_encrypted(), payload);
}

void
netcmd::read_auth_cmd(protocol_role & role,
                      globish & include_pattern,
                      globish & exclude_pattern,
                      key_id & client,
                      id & nonce1,
                      rsa_oaep_sha_data & hmac_key_encrypted,
                      rsa_sha1_signature & signature) const
{
  size_t pos = 0;
  // syntax is: <role:1 byte> <include_pattern: vstr> <exclude_pattern: vstr>
  //            <client: 20 bytes sha1> <nonce1: 20 random bytes>
  //            <hmac_key_encrypted: vstr> <signature: vstr>
  u8 role_byte = extract_datum_lsb<u8>(payload, pos, "auth netcmd, role");
  if (role_byte != static_cast<u8>(source_role)
      && role_byte != static_cast<u8>(sink_role)
      && role_byte != static_cast<u8>(source_and_sink_role))
    throw bad_decode(F("unknown role specifier %d") % widen<u32,u8>(role_byte));
  role = static_cast<protocol_role>(role_byte);
  string pattern_string;
  extract_variable_length_string(payload, pattern_string, pos,
                                 "auth(hmac) netcmd, include_pattern");
  include_pattern = globish(pattern_string, origin::network);
  extract_variable_length_string(payload, pattern_string, pos,
                                 "auth(hmac) netcmd, exclude_pattern");
  exclude_pattern = globish(pattern_string, origin::network);
  client = key_id(extract_substring(payload, pos,
                                    constants::merkle_hash_length_in_bytes,
                                    "auth(hmac) netcmd, client identifier"),
                  origin::network);
  nonce1 = id(extract_substring(payload, pos,
                                constants::merkle_hash_length_in_bytes,
                                "auth(hmac) netcmd, nonce1"),
              origin::network);
  string hmac_key;
  extract_variable_length_string(payload, hmac_key, pos,
                                 "auth(hmac) netcmd, hmac_key_encrypted");
  hmac_key_encrypted = rsa_oaep_sha_data(hmac_key, origin::network);
  string sig_string;
  extract_variable_length_string(payload, sig_string, pos,
                                 "auth(hmac) netcmd, signature");
  signature = rsa_sha1_signature(sig_string, origin::network);
  assert_end_of_buffer(payload, pos, "auth(hmac) netcmd payload");
}

void
netcmd::write_auth_cmd(protocol_role role,
                       globish const & include_pattern,
                       globish const & exclude_pattern,
                       key_id const & client,
                       id const & nonce1,
                       rsa_oaep_sha_data const & hmac_key_encrypted,
                       rsa_sha1_signature const & signature)
{
  cmd_code = auth_cmd;
  I(client.inner()().size() == constants::merkle_hash_length_in_bytes);
  I(nonce1().size() == constants::merkle_hash_length_in_bytes);
  payload += static_cast<char>(role);
  insert_variable_length_string(include_pattern(), payload);
  insert_variable_length_string(exclude_pattern(), payload);
  payload += client.inner()();
  payload += nonce1();
  insert_variable_length_string(hmac_key_encrypted(), payload);
  insert_variable_length_string(signature(), payload);
}

void
netcmd::read_confirm_cmd() const
{
  size_t pos = 0;
  assert_end_of_buffer(payload, pos, "confirm netcmd payload");
}

void
netcmd::write_confirm_cmd()
{
  cmd_code = confirm_cmd;
  payload.clear();
}

void
netcmd::read_refine_cmd(refinement_type & ty, merkle_node & node) const
{
  // syntax is: <u8: refinement type> <node: a merkle tree node>
  size_t pos = 0;
  ty = static_cast<refinement_type>
    (extract_datum_lsb<u8>
     (payload, pos,
      "refine netcmd, refinement type"));
  read_node(payload, pos, node);
  assert_end_of_buffer(payload, pos, "refine cmd");
}

void
netcmd::write_refine_cmd(refinement_type ty, merkle_node const & node)
{
  cmd_code = refine_cmd;
  payload.clear();
  payload += static_cast<char>(ty);
  write_node(node, payload);
}

void
netcmd::read_done_cmd(netcmd_item_type & type, size_t & n_items)  const
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <n_items: uleb128>
  type = read_netcmd_item_type(payload, pos, "done netcmd, item type");
  n_items = extract_datum_uleb128<size_t>(payload, pos,
                                          "done netcmd, item-to-send count");
  assert_end_of_buffer(payload, pos, "done netcmd payload");
}

void
netcmd::write_done_cmd(netcmd_item_type type,
                       size_t n_items)
{
  cmd_code = done_cmd;
  payload.clear();
  payload += static_cast<char>(type);
  insert_datum_uleb128<size_t>(n_items, payload);
}

void
netcmd::read_data_cmd(netcmd_item_type & type,
                      id & item, string & dat) const
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <id: 20 bytes sha1>
  //            <compressed_p1: 1 byte> <dat: vstr>

  type = read_netcmd_item_type(payload, pos, "data netcmd, item type");
  item = id(extract_substring(payload, pos,
                              constants::merkle_hash_length_in_bytes,
                              "data netcmd, item identifier"),
            origin::network);

  dat.clear();
  u8 compressed_p = extract_datum_lsb<u8>(payload, pos,
                                          "data netcmd, compression flag");
  extract_variable_length_string(payload, dat, pos,
                                  "data netcmd, data payload");
  if (compressed_p == 1)
  {
    gzip<data> zdat(dat, origin::network);
    data tdat;
    decode_gzip(zdat, tdat);
    dat = tdat();
  }
  assert_end_of_buffer(payload, pos, "data netcmd payload");
}

void
netcmd::write_data_cmd(netcmd_item_type type,
                       id const & item,
                       string const & dat)
{
  cmd_code = data_cmd;
  I(item().size() == constants::merkle_hash_length_in_bytes);
  payload += static_cast<char>(type);
  payload += item();
  if (dat.size() > constants::netcmd_minimum_bytes_to_bother_with_gzip)
    {
      gzip<data> zdat;
      encode_gzip(data(dat, origin::internal), zdat);
      payload += static_cast<char>(1); // compressed flag
      insert_variable_length_string(zdat(), payload);
    }
  else
    {
      payload += static_cast<char>(0); // compressed flag
      insert_variable_length_string(dat, payload);
    }
}


void
netcmd::read_delta_cmd(netcmd_item_type & type,
                       id & base, id & ident, delta & del) const
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <src: 20 bytes sha1> <dst: 20 bytes sha1>
  //            <compressed_p: 1 byte> <del: vstr>
  type = read_netcmd_item_type(payload, pos, "delta netcmd, item type");
  base = id(extract_substring(payload, pos,
                              constants::merkle_hash_length_in_bytes,
                              "delta netcmd, base identifier"),
            origin::network);
  ident = id(extract_substring(payload, pos,
                               constants::merkle_hash_length_in_bytes,
                               "delta netcmd, ident identifier"),
             origin::network);
  u8 compressed_p = extract_datum_lsb<u8>(payload, pos,
                                          "delta netcmd, compression flag");
  string tmp;
  extract_variable_length_string(payload, tmp, pos,
                                 "delta netcmd, delta payload");
  if (compressed_p == 1)
    {
      gzip<delta> zdel(tmp, origin::network);
      decode_gzip(zdel, del);
    }
  else
    {
      del = delta(tmp, origin::network);
    }
  assert_end_of_buffer(payload, pos, "delta netcmd payload");
}

void
netcmd::write_delta_cmd(netcmd_item_type & type,
                        id const & base, id const & ident,
                        delta const & del)
{
  cmd_code = delta_cmd;
  I(base().size() == constants::merkle_hash_length_in_bytes);
  I(ident().size() == constants::merkle_hash_length_in_bytes);
  payload += static_cast<char>(type);
  payload += base();
  payload += ident();

  string tmp;

  if (del().size() > constants::netcmd_minimum_bytes_to_bother_with_gzip)
    {
      payload += static_cast<char>(1); // compressed flag
      gzip<delta> zdel;
      encode_gzip(del, zdel);
      tmp = zdel();
    }
  else
    {
      payload += static_cast<char>(0); // compressed flag
      tmp = del();
    }
  I(tmp.size() <= constants::netcmd_payload_limit);
  insert_variable_length_string(tmp, payload);
}

void
netcmd::read_automate_cmd(key_id & client,
                          id & nonce1,
                          rsa_oaep_sha_data & hmac_key_encrypted,
                          rsa_sha1_signature & signature) const
{
  size_t pos = 0;
  client = key_id(extract_substring(payload, pos,
                                    constants::merkle_hash_length_in_bytes,
                                    "automate netcmd, key id"),
                  origin::network);
  nonce1 = id(extract_substring(payload, pos,
                                constants::merkle_hash_length_in_bytes,
                                "automate netcmd, nonce1"),
              origin::network);
  {
    string hmac_key;
    extract_variable_length_string(payload, hmac_key, pos,
                                   "automate netcmd, hmac_key_encrypted");
    hmac_key_encrypted = rsa_oaep_sha_data(hmac_key, origin::network);
  }
  {
    string sig;
    extract_variable_length_string(payload, sig, pos,
                                   "automate netcmd, signature");
    signature = rsa_sha1_signature(sig, origin::network);
  }
  assert_end_of_buffer(payload, pos, "automate netcmd payload");
}

void
netcmd::write_automate_cmd(key_id const & client,
                           id const & nonce1,
                           rsa_oaep_sha_data & hmac_key_encrypted,
                           rsa_sha1_signature & signature)
{
  cmd_code = automate_cmd;

  I(client.inner()().size() == constants::merkle_hash_length_in_bytes);
  I(nonce1().size() == constants::merkle_hash_length_in_bytes);

  payload += client.inner()();
  payload += nonce1();

  insert_variable_length_string(hmac_key_encrypted(), payload);
  insert_variable_length_string(signature(), payload);
}

void
netcmd::read_automate_headers_request_cmd() const
{
  size_t pos = 0;
  assert_end_of_buffer(payload, pos, "read automate headers request netcmd payload");
}

void
netcmd::write_automate_headers_request_cmd()
{
  cmd_code = automate_headers_request_cmd;
}

void
netcmd::read_automate_headers_reply_cmd(vector<pair<string, string> > & headers) const
{
  size_t pos = 0;
  size_t nheaders = extract_datum_uleb128<size_t>(payload, pos,
                                               "automate headers reply netcmd, count");
  headers.clear();
  for (size_t i = 0; i < nheaders; ++i)
    {
      string name;
      extract_variable_length_string(payload, name, pos,
                                     "automate headers reply netcmd, name");
      string value;
      extract_variable_length_string(payload, value, pos,
                                     "automate headers reply netcmd, value");
      headers.push_back(make_pair(name, value));
    }
  assert_end_of_buffer(payload, pos, "automate headers reply netcmd payload");
}

void
netcmd::write_automate_headers_reply_cmd(vector<pair<string, string> > const & headers)
{
  cmd_code = automate_headers_reply_cmd;

  insert_datum_uleb128<size_t>(headers.size(), payload);
  for (vector<pair<string, string> >::const_iterator h = headers.begin();
       h != headers.end(); ++h)
    {
      insert_variable_length_string(h->first, payload);
      insert_variable_length_string(h->second, payload);
    }
}

void
netcmd::read_automate_command_cmd(vector<string> & args,
                                  vector<pair<string, string> > & opts) const
{
  size_t pos = 0;
  {
    size_t nargs = extract_datum_uleb128<size_t>(payload, pos,
                                                 "automate_command netcmd, arg count");
    args.clear();
    for (size_t i = 0; i < nargs; ++i)
      {
        string arg;
        extract_variable_length_string(payload, arg, pos,
                                       "automate_command netcmd, argument");
        args.push_back(arg);
      }
  }
  {
    size_t nopts = extract_datum_uleb128<size_t>(payload, pos,
                                                 "automate_command netcmd, option count");
    opts.clear();
    for (size_t i = 0; i < nopts; ++i)
      {
        string name;
        extract_variable_length_string(payload, name, pos,
                                       "automate_command netcmd, option name");
        string value;
        extract_variable_length_string(payload, value, pos,
                                       "automate_command netcmd, option value");
        opts.push_back(make_pair(name, value));
      }
  }
  assert_end_of_buffer(payload, pos, "automate_command netcmd payload");
}

void
netcmd::write_automate_command_cmd(vector<string> const & args,
                                   vector<pair<string, string> > const & opts)
{
  cmd_code = automate_command_cmd;

  insert_datum_uleb128<size_t>(args.size(), payload);
  for (vector<string>::const_iterator a = args.begin();
       a != args.end(); ++a)
    {
      insert_variable_length_string(*a, payload);
    }

  insert_datum_uleb128<size_t>(opts.size(), payload);
  for (vector<pair<string, string> >::const_iterator o = opts.begin();
       o != opts.end(); ++o)
    {
      insert_variable_length_string(o->first, payload);
      insert_variable_length_string(o->second, payload);
    }
}

void
netcmd::read_automate_packet_cmd(int & command_num,
                                 char & stream,
                                 string & packet_data) const
{
  size_t pos = 0;

  command_num = int(extract_datum_uleb128<size_t>(payload, pos,
                                                  "automate_packet netcmd, command_num"));
  stream = char(extract_datum_uleb128<size_t>(payload, pos,
                                        "automate_packet netcmd, stream"));
  extract_variable_length_string(payload, packet_data, pos,
                                 "automate_packet netcmd, packet_data");
  assert_end_of_buffer(payload, pos, "automate_packet netcmd payload");
}

void
netcmd::write_automate_packet_cmd(int command_num,
                                  char stream,
                                  string const & packet_data)
{
  cmd_code = automate_packet_cmd;

  insert_datum_uleb128<size_t>(size_t(command_num), payload);
  insert_datum_uleb128<size_t>(size_t(stream), payload);
  insert_variable_length_string(packet_data, payload);
}

void
netcmd::read_usher_cmd(utf8 & greeting) const
{
  size_t pos = 0;
  string str;
  extract_variable_length_string(payload, str, pos, "usher netcmd, message");
  greeting = utf8(str, origin::network);
  assert_end_of_buffer(payload, pos, "usher netcmd payload");
}

void
netcmd::write_usher_cmd(utf8 const & greeting)
{
  version = 0;
  cmd_code = usher_cmd;
  insert_variable_length_string(greeting(), payload);
}

void
netcmd::read_usher_reply_cmd(u8 & version_out,
                             utf8 & server, string & pattern) const
{
  version_out = this->version;
  string str;
  size_t pos = 0;
  extract_variable_length_string(payload, str, pos, "usher_reply netcmd, server");
  server = utf8(str, origin::network);
  extract_variable_length_string(payload, pattern, pos, "usher_reply netcmd, pattern");
  assert_end_of_buffer(payload, pos, "usher_reply netcmd payload");
}

void
netcmd::write_usher_reply_cmd(utf8 const & server, string const & pattern)
{
  cmd_code = usher_reply_cmd;
  payload.clear();
  insert_variable_length_string(server(), payload);
  insert_variable_length_string(pattern, payload);
}



// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
