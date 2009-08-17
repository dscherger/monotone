// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __CERT_HH__
#define __CERT_HH__

#include "vocab.hh"

// Certs associate an opaque name/value pair with a revision ID, and
// are accompanied by an RSA public-key signature attesting to the
// association. Users can write as much extra meta-data as they like
// about revisions, using certs, without needing anyone's special
// permission.

struct cert : public origin_aware
{
  cert() {}

  cert(revision_id const & ident,
       cert_name const & name,
       cert_value const & value,
       rsa_keypair_id const & key)
    : ident(ident), name(name), value(value), key(key)
  {}

  cert(revision_id const & ident,
       cert_name const & name,
       cert_value const & value,
       rsa_keypair_id const & key,
       rsa_sha1_signature const & sig)
    : ident(ident), name(name), value(value), key(key), sig(sig)
  {}

  // These understand the netsync serialization.
  explicit cert(std::string const & s);
  cert(std::string const & s, origin::type m);

  revision_id ident;
  cert_name name;
  cert_value value;
  rsa_keypair_id key;
  rsa_sha1_signature sig;

  bool operator<(cert const & other) const;
  bool operator==(cert const & other) const;

  void hash_code(id & out) const;
  void signable_text(std::string & out) const;
  void marshal_for_netio(std::string & out) const;
};

#endif // __CERT_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
