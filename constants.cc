// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// this file contains magic constants which you could, in theory, tweak.
// probably best not to tweak them though.

#include "constants.hh"
#include "numeric_vocab.hh"

#include <boost/static_assert.hpp>

using std::string;

namespace constants
{

  // number of bits in an RSA key we use
  size_t const keylen = 1024;

  // number of seconds in window, in which to consider CVS commits equivalent
  // if they have otherwise compatible contents (author, changelog)
  time_t const cvs_window = 60 * 5;

  // size of a line of database traffic logging, beyond which lines will be
  // truncated.
  size_t const db_log_line_sz = 70;

  size_t const default_terminal_width = 72;

  // size in bytes of the database xdelta version reconstruction cache.
  // the value of 7 MB was determined as the optimal point after timing
  // various values with a pull of the monotone repository - it could
  // be tweaked further.
  size_t db_version_cache_sz = 7 * (1 << 20);

  size_t const db_roster_cache_sz = 7;

  unsigned long const db_max_pending_writes_bytes = 16 * 1024 * 1024;
 
  // size of a line of text in the log buffer, beyond which log lines will be
  // truncated.
  size_t const log_line_sz = 0x300;

  // all the ASCII characters (bytes) which are legal in a packet.
  char const * const legal_packet_bytes =
  // LDH characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"
  "-"
  // extra base64 codes
  "+/="
  // separators
  ".@[]"
  // whitespace
  " \r\n\t"
  ;

  string const regex_legal_packet_bytes("([a-zA-Z0-9+/=[:space:]]+)");

  // all the ASCII characters (bytes) which are legal in a SHA1 hex id
  char const * const legal_id_bytes =
  "0123456789abcdef"
  ;

  string const regex_legal_id_bytes("([[:xdigit:]]{40})");

  // all the ASCII characters (bytes) which are legal in an ACE string
  char const * const legal_ace_bytes =
  // LDH characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"
  "-"
  // label separators
  ".@"
  ;

  // all the ASCII characters (bytes) which can occur in cert names
  char const * const legal_cert_name_bytes =
  // LDH characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"
  "-"
  ;

  string const regex_legal_cert_name_bytes("([-a-zA-Z0-9]+)");

  // all the ASCII characters (bytes) which can occur in key names
  char const * const legal_key_name_bytes =
  // LDH characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"
  "-"
  // other non-shell, non-selector metacharacters allowed in (unquoted) local
  // parts by RFC2821/RFC2822.  The full list is !#$%&'*+-/=?^_`|{}~.
  "+_."
  // label and component separators
  ".@"
  ;

  string const regex_legal_key_name_bytes("([-a-zA-Z0-9\\.@\\+_]+)");

  // merkle tree / netcmd / netsync related stuff

  size_t const merkle_fanout_bits = 4;

  // all other merkle constants are derived
  size_t const merkle_hash_length_in_bits = merkle_hash_length_in_bytes * 8;
  size_t const merkle_num_tree_levels = merkle_hash_length_in_bits / merkle_fanout_bits;
  size_t const merkle_num_slots = 1 << merkle_fanout_bits;
  size_t const merkle_bitmap_length_in_bits = merkle_num_slots * 2;
  size_t const merkle_bitmap_length_in_bytes = merkle_bitmap_length_in_bits / 8;

  BOOST_STATIC_ASSERT(sizeof(char) == 1);
  BOOST_STATIC_ASSERT(CHAR_BIT == 8);
  BOOST_STATIC_ASSERT(merkle_num_tree_levels > 0);
  BOOST_STATIC_ASSERT(merkle_num_tree_levels < 256);
  BOOST_STATIC_ASSERT(merkle_fanout_bits > 0);
  BOOST_STATIC_ASSERT(merkle_fanout_bits < 32);
  BOOST_STATIC_ASSERT(merkle_hash_length_in_bits > 0);
  BOOST_STATIC_ASSERT((merkle_hash_length_in_bits % merkle_fanout_bits) == 0);
  BOOST_STATIC_ASSERT(merkle_bitmap_length_in_bits > 0);
  BOOST_STATIC_ASSERT((merkle_bitmap_length_in_bits % 8) == 0);

  u8 const netcmd_current_protocol_version = 6;

  size_t const netcmd_minimum_bytes_to_bother_with_gzip = 0xfff;

  size_t const netsync_session_key_length_in_bytes = 20;     // 160 bits
  size_t const netsync_hmac_value_length_in_bytes = 20;      // 160 bits

  string const & netsync_key_initializer = string(netsync_session_key_length_in_bytes, 0);

  // attributes
  string const encoding_attribute("mtn:encoding");
  string const manual_merge_attribute("mtn:manual_merge");
  string const binary_encoding("binary");
  string const default_encoding("default");
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
