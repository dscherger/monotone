// Copyright (C) 2005 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __KEY_STORE_H__
#define __KEY_STORE_H__

#include <boost/scoped_ptr.hpp>

#include <botan/botan.h>
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
#include <botan/rng.h>
#else
#include <botan/libstate.h>
#endif

#include "vector.hh"
#include "vocab.hh"
#include "paths.hh"

class app_state;
struct globish;
class database;

struct keypair
{
  rsa_pub_key pub;
  rsa_priv_key priv;
  keypair()
  {}
  keypair(rsa_pub_key const & a,
          rsa_priv_key const & b)
   : pub(a), priv(b)
  {}
};

struct key_store_state;

class key_store
{
private:
  boost::scoped_ptr<key_store_state> s;

public:
  key_id signing_key;
  bool have_signing_key() const;

  explicit key_store(app_state & a);
  ~key_store();

#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
  Botan::RandomNumberGenerator & get_rng();
#endif

  system_path const & get_key_dir() const;

  // Basic key I/O

  void get_key_ids(std::vector<key_id> & priv);

  bool key_pair_exists(key_id const & ident);
  bool key_pair_exists(key_name const & name);

  void get_key_pair(key_id const & ident,
                    keypair & kp);
  bool maybe_get_key_pair(key_id const & ident,
                          keypair & kp);
  void get_key_pair(key_id const & hash,
                    key_name & ident,
                    keypair & kp);
  bool maybe_get_key_pair(key_id const & hash,
                          key_name & ident,
                          keypair & kp);

  bool put_key_pair(key_name const & name,
                    keypair const & kp);

  void delete_key(key_id const & ident);

  // Crypto operations

  void cache_decrypted_key(key_id const & id);

  enum create_key_pair_mode { create_quiet, create_verbose };
  void create_key_pair(database & db, key_name const & ident,
                       create_key_pair_mode create_mode = create_verbose,
                       utf8 const * maybe_passphrase = NULL,
                       key_id * const maybe_hash = NULL);

  // This is always your own key, so you probably want to
  // always use the given name.
  void change_key_passphrase(key_id const & id);

  void decrypt_rsa(key_id const & id,
                   rsa_oaep_sha_data const & ciphertext,
                   std::string & plaintext);

  void make_signature(database & db, key_id const & id,
                      std::string const & tosign,
                      rsa_sha1_signature & signature);

  // Interoperation with ssh-agent

  void add_key_to_agent(key_id const & id);
  void export_key_for_agent(key_id const & id,
                            std::ostream & os);

  // Migration from old databases

  void migrate_old_key_pair(key_name const & id,
                            old_arc4_rsa_priv_key const & old_priv,
                            rsa_pub_key const & pub);
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
