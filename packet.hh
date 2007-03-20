#ifndef __PACKET_HH__
#define __PACKET_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iosfwd>
#include <boost/function.hpp>

#include "vocab.hh"

class app_state;
struct cert;

// the idea here is that monotone can produce and consume "packet streams",
// where each packet is *informative* rather than transactional. that is to
// say, they contain no information which needs to be replied to or processed
// in any particular manner during some communication session.
//
// unlike nearly every other part of this program, the packet stream
// interface is really *stream* oriented. the idea being that, while you
// might be able to keep any one delta or file in memory at once, asking
// you to keep *all* the deltas or files associated with a large chunk of
// work, in memory, is a bit much.
//
// packet streams are ascii text, formatted for comfortable viewing on a
// terminal or inclusion in an email / netnews post. they can be edited with
// vi, filtered with grep, and concatenated with cat.
//
// there are currently 8 types of packets, though this can grow without hurting
// anyone's feelings. if there's a backwards compatibility problem, just introduce
// a new packet type.

struct packet_consumer
{
protected:
  boost::function1<void, revision_id> on_revision_written;
  boost::function1<void, cert const &> on_cert_written;
  boost::function1<void, rsa_keypair_id> on_pubkey_written;
  boost::function1<void, rsa_keypair_id> on_keypair_written;
public:

  virtual void set_on_revision_written(boost::function1<void, revision_id>
                                                const & x);
  virtual void set_on_cert_written(boost::function1<void, cert const &>
                                                const & x);
  virtual void set_on_pubkey_written(boost::function1<void, rsa_keypair_id>
                                                const & x);
  virtual void set_on_keypair_written(boost::function1<void, rsa_keypair_id>
                                                const & x);

  virtual ~packet_consumer() {}
  virtual void consume_file_data(file_id const & ident,
                                 file_data const & dat) = 0;
  virtual void consume_file_delta(file_id const & id_old,
                                  file_id const & id_new,
                                  file_delta const & del) = 0;

  virtual void consume_revision_data(revision_id const & ident,
                                     revision_data const & dat) = 0;
  virtual void consume_revision_cert(revision<cert> const & t) = 0;


  virtual void consume_public_key(rsa_keypair_id const & ident,
                                  base64< rsa_pub_key > const & k) = 0;
  virtual void consume_key_pair(rsa_keypair_id const & ident,
                                keypair const & kp) = 0;
  virtual void consume_old_private_key(rsa_keypair_id const & ident,
                                       base64< arc4<rsa_priv_key> > const & k)
  = 0;
                                       
};

// this writer writes packets into a stream

struct packet_writer : public packet_consumer
{
  std::ostream & ost;
  explicit packet_writer(std::ostream & o);
  virtual ~packet_writer() {}
  virtual void consume_file_data(file_id const & ident,
                                 file_data const & dat);
  virtual void consume_file_delta(file_id const & id_old,
                                  file_id const & id_new,
                                  file_delta const & del);

  virtual void consume_revision_data(revision_id const & ident,
                                     revision_data const & dat);
  virtual void consume_revision_cert(revision<cert> const & t);

  virtual void consume_public_key(rsa_keypair_id const & ident,
                                  base64< rsa_pub_key > const & k);
  virtual void consume_key_pair(rsa_keypair_id const & ident,
                                keypair const & kp);
  // this one always I()s [stupid C++ won't let me leave it pure-virtual]
  virtual void consume_old_private_key(rsa_keypair_id const & ident,
                                       base64< arc4<rsa_priv_key> > const & k);
};

// this writer injects packets it receives to the database.

struct packet_db_writer : public packet_consumer
{
  app_state & app;
public:
  packet_db_writer(app_state & app);
  virtual ~packet_db_writer();
  virtual void consume_file_data(file_id const & ident,
                                 file_data const & dat);
  virtual void consume_file_delta(file_id const & id_old,
                                  file_id const & id_new,
                                  file_delta const & del);

  virtual void consume_revision_data(revision_id const & ident,
                                     revision_data const & dat);
  virtual void consume_revision_cert(revision<cert> const & t);

  virtual void consume_public_key(rsa_keypair_id const & ident,
                                  base64< rsa_pub_key > const & k);
  virtual void consume_key_pair(rsa_keypair_id const & ident,
                                keypair const & kp);
  virtual void consume_old_private_key(rsa_keypair_id const & ident,
                                       base64< arc4<rsa_priv_key> > const & k);
};

size_t read_packets(std::istream & in, packet_consumer & cons);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
