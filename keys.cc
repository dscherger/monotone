// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <cstring>

#include "keys.hh"
#include "sanity.hh"
#include "constants.hh"
#include "platform.hh"
#include "transforms.hh"
#include "simplestring_xform.hh"
#include "charset.hh"
#include "lua_hooks.hh"
#include "options.hh"
#include "project.hh"
#include "key_store.hh"
#include "database.hh"
#include "uri.hh"
#include "globish.hh"
#include "network/connection_info.hh"

using std::string;
using std::vector;
using std::memset;

// there will probably forever be bugs in this file. it's very
// hard to get right, portably and securely. sorry about that.

// Loads a key pair for a given key id, considering it a user error
// if that key pair is not available.

void
load_key_pair(key_store & keys, key_id const & id)
{
  E(keys.key_pair_exists(id), origin::user,
    F("no key pair '%s' found in key store '%s'")
    % id % keys.get_key_dir());
}

void
load_key_pair(key_store & keys,
              key_id const & id,
              keypair & kp)
{
  load_key_pair(keys, id);
  keys.get_key_pair(id, kp);
}

void
load_key_pair(key_store & keys,
              key_id const & id,
              key_name & name,
              keypair & kp)
{
  load_key_pair(keys, id);
  keys.get_key_pair(id, name, kp);
}

namespace {
  void check_and_save_chosen_key(database & db,
                                 key_store & keys,
                                 key_id const & chosen_key)
  {
    // Ensure that the specified key actually exists.
    key_name name;
    keypair priv_key;
    load_key_pair(keys, chosen_key, name, priv_key);

    if (db.database_specified())
      {
        // If the database doesn't have this public key, add it now; otherwise
        // make sure the database and key-store agree on the public key.
        if (!db.public_key_exists(chosen_key))
          db.put_key(name, priv_key.pub);
        else
          {
            rsa_pub_key pub_key;
            db.get_key(chosen_key, pub_key);
            E(keys_match(name, pub_key, name, priv_key.pub),
              origin::no_fault,
              F("The key '%s' stored in your database does\n"
                "not match the version in your local key store!")
              % chosen_key);
          }
      }

    // Decrypt and cache the key now.
    keys.cache_decrypted_key(chosen_key);
  }
  bool get_only_key(key_store & keys, key_requiredness_flag key_requiredness, key_id & key)
  {
    vector<key_id> all_privkeys;
    keys.get_key_ids(all_privkeys);
    E(key_requiredness == key_optional || !all_privkeys.empty(), origin::user,
      F("you have no private key to make signatures with\n"
        "perhaps you need to 'genkey <your email>'"));
    E(key_requiredness == key_optional || all_privkeys.size() < 2, origin::user,
      F("you have multiple private keys\n"
        "pick one to use for signatures by adding "
        "'-k<keyname>' to your command"));

    if (all_privkeys.size() == 1)
      {
        key = all_privkeys[0];
        return true;
      }
    else
      {
        return false;
      }
  }
}

void
get_user_key(options const & opts, lua_hooks & lua,
             database & db, key_store & keys,
             project_t & project, key_id & key,
             key_cache_flag const cache)
{
  if (keys.have_signing_key())
    {
      key = keys.signing_key;
      return;
    }

  // key_given is not set if the key option was extracted from the workspace
  if (opts.key_given || !opts.signing_key().empty())
    {
      if (!opts.signing_key().empty())
        {
          key_identity_info identity;
          project.get_key_identity(keys, lua, opts.signing_key, identity);
          key = identity.id;
        }
      else
        {
          E(false, origin::user,
            F("a key is required for this operation, but the --key option "
              "was given with an empty argument"));
        }
    }
  else if (lua.hook_get_branch_key(opts.branch, keys, project, key))
    ; // the lua hook sets the key
  else
    {
      get_only_key(keys, key_required, key);
    }

  if (cache == cache_enable)
    check_and_save_chosen_key(db, keys, key);
}

void
cache_netsync_key(options const & opts,
                  project_t & project,
                  key_store & keys,
                  lua_hooks & lua,
                  shared_conn_info const & info,
                  key_requiredness_flag key_requiredness)
{
  if (keys.have_signing_key())
    {
      return;
    }

  bool found_key = false;
  key_id key;

  // key_given is not set if the key option was extracted from the workspace
  if (opts.key_given || !opts.signing_key().empty())
    {
      key_identity_info identity;
      // maybe they specifically requested no key ("--key ''")
      if (!opts.signing_key().empty())
        {
          project.get_key_identity(keys, lua, opts.signing_key, identity);
          key = identity.id;
          found_key = true;
        }
    }
  else if (lua.hook_get_netsync_key(utf8(info->client.get_uri().resource(), origin::user),
                                    info->client.get_include_pattern(),
                                    info->client.get_exclude_pattern(),
                                    keys, project, key))
    {
      found_key = true;
    }
  else
    {
      found_key = get_only_key(keys, key_requiredness, key);
    }

  if (found_key)
    {
      check_and_save_chosen_key(project.db, keys, key);
    }
}

void
cache_user_key(options const & opts,
               project_t & project,
               key_store & keys,
               lua_hooks & lua)
{
  key_id key;
  get_user_key(opts, lua, project.db, keys, project, key);
}

void
key_hash_code(key_name const & ident,
              rsa_pub_key const & pub,
              key_id & out)
{
  data tdat(ident() + ":" + remove_ws(encode_base64(pub)()),
            origin::internal);
  id tmp;
  calculate_ident(tdat, tmp);
  out = key_id(tmp);
}

// helper to compare if two keys have the same hash
// (ie are the same key)
bool
keys_match(key_name const & id1,
           rsa_pub_key const & key1,
           key_name const & id2,
           rsa_pub_key const & key2)
{
  key_id hash1, hash2;
  key_hash_code(id1, key1, hash1);
  key_hash_code(id2, key2, hash2);
  return hash1 == hash2;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
