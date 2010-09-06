// Copyright (C) 2010 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.
//
// This duplicates part of packet.hh; the intent is to deprecate that but
// keep this.

#ifndef __KEY_PACKET_HH__
#define __KEY_PACKET_HH__

#include "vocab.hh"

struct cert;
struct keypair;

// packet streams are ascii text, formatted for comfortable viewing on a
// terminal or inclusion in an email / netnews post. they can be edited with
// vi, filtered with grep, and concatenated with cat.

struct key_packet_consumer
{
public:
  virtual ~key_packet_consumer() {}
  virtual void consume_public_key(key_name const & ident,
                                  rsa_pub_key const & k) = 0;
  virtual void consume_key_pair(key_name const & ident,
                                keypair const & kp) = 0;
  virtual void consume_old_private_key(key_name const & ident,
                                       old_arc4_rsa_priv_key const & k) = 0;
};

// this writer writes key_packets into a stream

struct key_packet_writer : public key_packet_consumer
{
  std::ostream & ost;
  explicit key_packet_writer(std::ostream & o);
  virtual ~key_packet_writer() {}

  virtual void consume_public_key(key_name const & ident,
                                  rsa_pub_key const & k);
  virtual void consume_key_pair(key_name const & ident,
                                keypair const & kp);
  virtual void consume_old_private_key(key_name const & ident,
                                       old_arc4_rsa_priv_key const & k);
};

size_t read_key_packets(std::istream & in, key_packet_consumer & cons);

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
