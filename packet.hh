#ifndef __PACKET_HH__
#define __PACKET_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <iostream>
#include <set>
#include <map>

#include <boost/optional.hpp>

#include "app_state.hh"
#include "ui.hh"
#include "vocab.hh"

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
  virtual ~packet_consumer() {}
  virtual void consume_file_data(file_id const & ident, 
				 file_data const & dat) = 0;
  virtual void consume_file_delta(file_id const & id_old, 
				  file_id const & id_new,
				  file_delta const & del) = 0;
  virtual void consume_file_cert(file<cert> const & t) = 0;
  
  virtual void consume_manifest_data(manifest_id const & ident, 
				     manifest_data const & dat) = 0;
  virtual void consume_manifest_delta(manifest_id const & id_old, 
				      manifest_id const & id_new,
				      manifest_delta const & del) = 0;
  virtual void consume_manifest_cert(manifest<cert> const & t) = 0;  

  virtual void consume_public_key(rsa_keypair_id const & ident,
				  base64< rsa_pub_key > const & k) = 0;  
  virtual void consume_private_key(rsa_keypair_id const & ident,
				   base64< arc4<rsa_priv_key> > const & k) = 0;  
};

// this writer queues packets to be sent to the network

struct queueing_packet_writer : public packet_consumer
{
  app_state & app;
  std::set<url> targets;
  boost::optional<reverse_queue> rev;

  ticker n_bytes;
  ticker n_packets;
  
  queueing_packet_writer(app_state & a, std::set<url> const & t);
  virtual ~queueing_packet_writer() {}

  void queue_blob_for_network(std::string const & str);

  virtual void consume_file_data(file_id const & ident, 
				 file_data const & dat);
  virtual void consume_file_delta(file_id const & id_old, 
				  file_id const & id_new,
				  file_delta const & del);
  virtual void consume_file_cert(file<cert> const & t);
  
  virtual void consume_manifest_data(manifest_id const & ident, 
				     manifest_data const & dat);
  virtual void consume_manifest_delta(manifest_id const & id_old, 
				      manifest_id const & id_new,
				      manifest_delta const & del);
  virtual void consume_manifest_cert(manifest<cert> const & t);

  virtual void consume_public_key(rsa_keypair_id const & ident,
				  base64< rsa_pub_key > const & k);
  virtual void consume_private_key(rsa_keypair_id const & ident,
				   base64< arc4<rsa_priv_key> > const & k);
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
  virtual void consume_file_cert(file<cert> const & t);
  
  virtual void consume_manifest_data(manifest_id const & ident, 
				     manifest_data const & dat);
  virtual void consume_manifest_delta(manifest_id const & id_old, 
				      manifest_id const & id_new,
				      manifest_delta const & del);
  virtual void consume_manifest_cert(manifest<cert> const & t);

  virtual void consume_public_key(rsa_keypair_id const & ident,
				  base64< rsa_pub_key > const & k);
  virtual void consume_private_key(rsa_keypair_id const & ident,
				   base64< arc4<rsa_priv_key> > const & k);
};

// this writer injects packets it receives to the database.

struct packet_db_writer : public packet_consumer
{
  app_state & app;
  bool take_keys;
  size_t count;

  boost::optional<url> server;

  packet_db_writer(app_state & app, bool take_keys = false);
  virtual ~packet_db_writer() {}
  virtual void consume_file_data(file_id const & ident, 
				 file_data const & dat);
  virtual void consume_file_delta(file_id const & id_old, 
				  file_id const & id_new,
				  file_delta const & del);
  virtual void consume_file_cert(file<cert> const & t);
  
  virtual void consume_manifest_data(manifest_id const & ident, 
				     manifest_data const & dat);
  virtual void consume_manifest_delta(manifest_id const & id_old, 
				      manifest_id const & id_new,
				      manifest_delta const & del);
  virtual void consume_manifest_cert(manifest<cert> const & t);  

  virtual void consume_public_key(rsa_keypair_id const & ident,
				  base64< rsa_pub_key > const & k);
  virtual void consume_private_key(rsa_keypair_id const & ident,
				   base64< arc4<rsa_priv_key> > const & k);
};

size_t read_packets(std::istream & in, packet_consumer & cons);

#endif
