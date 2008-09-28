// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// this fragment is included into both vocab.hh and vocab.cc,
// in order to facilitate external instantiation of most of the
// vocabulary, minimize code duplication, speed up compilation, etc.

ATOMIC_NOVERIFY(external);    // "external" string in unknown system charset
ATOMIC_NOVERIFY(utf8);        // unknown string in UTF8 charset
ATOMIC(symbol);               // valid basic io symbol (alphanumeric or _ chars)

ATOMIC_BINARY(id);            // hash of data
ATOMIC_NOVERIFY(data);        // meaningless blob
ATOMIC_NOVERIFY(delta);       // xdelta between 2 datas
ATOMIC_NOVERIFY(inodeprint);  // fingerprint of an inode

ATOMIC_NOVERIFY(branch_uid); // what goes in the database

ATOMIC(cert_name);            // symbol-of-your-choosing
ATOMIC_NOVERIFY(cert_value);  // symbol-of-your-choosing

// some domains: "database" (+ default_server, default_pattern),
//   server_key (+ servername/key)
//   branch_alias (+ short form/long form)
//   trust_seed (+ branch/seed)
ATOMIC_NOVERIFY(var_domain);  // symbol-of-your-choosing
ATOMIC_NOVERIFY(var_name);    // symbol-of-your-choosing
ATOMIC_NOVERIFY(var_value);   // symbol-of-your-choosing

ATOMIC(rsa_keypair_id);              // keyname@domain.you.own
ATOMIC_NOVERIFY(rsa_pub_key);        // some nice numbers
ATOMIC_NOVERIFY(rsa_priv_key);       // some nice numbers
ATOMIC_NOVERIFY(old_arc4_rsa_priv_key); // ... in the old storage format
ATOMIC_NOVERIFY(rsa_sha1_signature); // some other nice numbers
ATOMIC_NOVERIFY(rsa_oaep_sha_data);

// Special case: these classes need to befriend their verify functions.
// See vocab.cc for details.

// key for netsync session HMAC
ATOMIC_HOOKED(netsync_session_key,
              friend void verify(netsync_session_key &););
// 160-bit SHA-1 HMAC
ATOMIC_HOOKED(netsync_hmac_value,
              friend void verify(netsync_hmac_value &););

ATOMIC_NOVERIFY(attr_key);
ATOMIC_NOVERIFY(attr_value);

DECORATE(revision);           // thing associated with a revision
DECORATE(roster);             // thing associated with a roster
DECORATE(manifest);           // thing associated with a manifest
DECORATE(file);               // thing associated with a file
DECORATE(key);                // thing associated with a key
DECORATE(epoch);              // thing associated with an epoch

ENCODING_NOVERIFY(gzip);      // thing which is gzipped
ENCODING(hexenc);             // thing which is hex-encoded
ENCODING_NOVERIFY(base64);    // thing which is base64-encoded

ATOMIC_NOVERIFY(prefix);      // raw encoding of a merkle tree prefix
ATOMIC_NOVERIFY(merkle);      // raw encoding of a merkle tree node

// instantiate those bits of the template vocabulary actually in use.

// decorations
EXTERN template class       epoch<id>;
EXTERN template class        file<id>;
EXTERN template class         key<id>;
EXTERN template class    manifest<id>;
EXTERN template class    revision<id>;
EXTERN template class      roster<id>;

EXTERN template class     epoch<data>;
EXTERN template class      file<data>;
EXTERN template class  manifest<data>;
EXTERN template class  revision<data>;
EXTERN template class    roster<data>;

EXTERN template class     file<delta>;
EXTERN template class manifest<delta>;
EXTERN template class   roster<delta>;

// encodings
EXTERN template class hexenc<data>;
EXTERN template class hexenc<id>;
EXTERN template class hexenc<inodeprint>;
EXTERN template class hexenc<prefix>;
EXTERN template class hexenc<rsa_sha1_signature>;

EXTERN template class base64<cert_value>;
EXTERN template class base64<data>;
EXTERN template class base64<merkle>;
EXTERN template class base64<old_arc4_rsa_priv_key>;
EXTERN template class base64<rsa_priv_key>;
EXTERN template class base64<rsa_pub_key>;
EXTERN template class base64<rsa_sha1_signature>;
EXTERN template class base64<var_name>;
EXTERN template class base64<var_value>;

EXTERN template class         gzip<data>   ;
EXTERN template class base64< gzip<data>  >;

EXTERN template class         gzip<delta>  ;
EXTERN template class base64< gzip<delta> >;

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

