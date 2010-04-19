// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __KEYS_HH__
#define __KEYS_HH__

#include "vocab.hh"

struct options;
class project_t;
class lua_hooks;
class key_store;
class database;
struct keypair;
class globish;

// keys.{hh,cc} does all the "delicate" crypto (meaning: that which needs
// to read passphrases and manipulate raw, decrypted private keys). it
// could in theory be in transforms.cc too, but that file's already kinda
// big and this stuff "feels" different, imho.

// Find the key to be used for signing certs.  If possible, ensure the
// database and the key_store agree on that key, and optionally cache it in
// decrypted form, so as not to bother the user for their passphrase later.
void get_user_key(options const & opts, lua_hooks & lua,
                  database & db, key_store & keys,
                  project_t & project, key_id & key,
                  bool const cache = true);

// As above, but does not report which key has been selected; for use when
// the important thing is to have selected one and cached the decrypted key.
void cache_user_key(options const & opts, lua_hooks & lua,
                    database & db, key_store & keys,
                    project_t & project);

// Find the key to be used for netsync authentication.  If possible, ensure the
// database and the key_store agree on that key, and cache it in decrypted
// form, so as not to bother the user for their passphrase later.
enum netsync_key_requiredness {KEY_OPTIONAL, KEY_REQUIRED};
void cache_netsync_key(options const & opts,
                       database & db,
                       key_store & keys,
                       lua_hooks & lua,
                       project_t & project,
                       utf8 const & host,
                       globish const & include,
                       globish const & exclude,
                       netsync_key_requiredness key_requiredness);

void load_key_pair(key_store & keys,
                   key_id const & id);

void load_key_pair(key_store & keys,
                   key_id const & id,
                   keypair & kp);

// netsync stuff

void key_hash_code(key_name const & ident,
                   rsa_pub_key const & pub,
                   key_id & out);

bool keys_match(key_name const & id1,
                rsa_pub_key const & key1,
                key_name const & id2,
                rsa_pub_key const & key2);

#endif // __KEYS_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
