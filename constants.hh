#ifndef __CONSTANTS_HH__
#define __CONSTANTS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <unistd.h>
#include <string>
#include "numeric_vocab.hh"

namespace constants
{

  // this file contains magic constants which you could, in theory, tweak.
  // probably best not to tweak them though.

  // block size in bytes for "automate stdio" output
  extern size_t const automate_stdio_size;

  // number of bits in an RSA key we use
  extern size_t const keylen; 

  // number of characters in a SHA1 id
  static size_t const idlen = 40; 

  // number of characters in an encoded epoch
  static size_t const epochlen = idlen;

  // number of characters in a raw epoch
  static size_t const epochlen_bytes = epochlen / 2;

  // number of seconds in window, in which to consider CVS commits equivalent
  // if they have otherwise compatible contents (author, changelog)
  extern time_t const cvs_window; 

  // number of bytes in a password buffer. further bytes will be dropped.
  static size_t const maxpasswd = 0xfff;

  // number of bytes to use in buffers, for buffered i/o operations
  static size_t const bufsz = 0x3ffff;

  // size of a line of database traffic logging, beyond which lines will be
  // truncated.
  extern size_t const db_log_line_sz;

  // size in bytes of the database xdelta version reconstruction cache
  extern size_t const db_version_cache_sz;

  // size of a line of text in the log buffer, beyond which log lines will be
  // truncated.
  extern size_t const log_line_sz;

  // assumed width of the terminal, when we can't query for it directly
  extern size_t const default_terminal_width;

  // all the ASCII characters (bytes) which are legal in a packet
  extern char const * const legal_packet_bytes;

  // boost regex that matches the bytes in legal_packet_bytes
  extern std::string const regex_legal_packet_bytes;

  // all the ASCII characters (bytes) which are legal in an ACE string
  extern char const * const legal_ace_bytes;

  // all the ASCII characters (bytes) which are legal in a SHA1 hex id
  extern char const * const legal_id_bytes;

  // boost regex that matches the bytes in legal_id_bytes
  extern std::string const regex_legal_id_bytes;

  // all the ASCII characters (bytes) which can occur in cert names
  extern char const * const legal_cert_name_bytes;

  // boost regex that matches the bytes in legal_cert_name_bytes
  extern std::string const regex_legal_cert_name_bytes;

  // all the ASCII characters (bytes) which can occur in key names
  extern char const * const legal_key_name_bytes;

  // boost regex that matches the bytes in legal_key_name_bytes
  extern std::string const regex_legal_key_name_bytes;

  // all the ASCII characters (bytes) which are illegal in a (file|local)_path
  extern char const * const illegal_path_bytes;

  // remaining constants are related to netsync protocol

  // number of bytes in the hash used in netsync
  static size_t const merkle_hash_length_in_bytes = 20;

  // number of bits of merkle prefix consumed by each level of tree
  extern size_t const merkle_fanout_bits;

  // derived from hash_length_in_bytes
  extern size_t const merkle_hash_length_in_bits;

  // derived from fanout_bits
  extern size_t const merkle_num_tree_levels;

  // derived from fanout_bits
  extern size_t const merkle_num_slots;

  // derived from fanout_bits
  extern size_t const merkle_bitmap_length_in_bits;

  // derived from fanout_bits
  extern size_t const merkle_bitmap_length_in_bytes;

  // the current netcmd/netsync protocol version
  extern u8 const netcmd_current_protocol_version;

  // minimum size of any netcmd on the wire
  static size_t const netcmd_minsz = (1     // version
                                      + 1   // cmd code
                                      + 1   // smallest uleb possible
                                      + 4); // adler32

  
  // largest command *payload* allowed in a netcmd
  // in practice, this sets the size of the largest compressed file
  static size_t const netcmd_payload_limit = 2 << 27;

  // maximum size of any netcmd on the wire, including payload
  static size_t const netcmd_maxsz = netcmd_minsz + netcmd_payload_limit;

  // netsync fragments larger than this are zlibbed
  extern size_t const netcmd_minimum_bytes_to_bother_with_zlib;

  // TCP port to listen on / connect to when doing netsync
  static size_t const netsync_default_port = 4691;

  // maximum number of simultaneous clients on a server
  static size_t const netsync_connection_limit = 1024;

  // number of seconds a connection can be idle before it's dropped
  static size_t const netsync_timeout_seconds = 21600; // 6 hours

  // netsync HMAC key length
  extern size_t const netsync_session_key_length_in_bytes;

  // netsync HMAC value length
  extern size_t const netsync_hmac_value_length_in_bytes;

  // how long a sha1 digest should be
  static size_t const sha1_digest_length = 20; // 160 bits

  // netsync session key default initializer
  extern std::string const & netsync_key_initializer;
}

#endif // __CONSTANTS_HH__
