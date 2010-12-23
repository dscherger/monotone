// Copyright (C) 2005 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <sstream>

#include <botan/botan.h>
#include <botan/rsa.h>
#include <botan/pem.h>
#include <botan/look_pk.h>

#include "char_classifiers.hh"
#include "key_store.hh"
#include "file_io.hh"
#include "packet.hh"
#include "project.hh"
#include "database.hh"
#include "keys.hh"
#include "globish.hh"
#include "app_state.hh"
#include "transforms.hh"
#include "constants.hh"
#include "ssh_agent.hh"
#include "safe_map.hh"
#include "charset.hh"
#include "ui.hh"
#include "lazy_rng.hh"
#include "botan_pipe_cache.hh"

using std::make_pair;
using std::istringstream;
using std::map;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

using boost::scoped_ptr;
using boost::shared_ptr;
using boost::shared_dynamic_cast;

using Botan::RSA_PrivateKey;
using Botan::RSA_PublicKey;
using Botan::SecureVector;
using Botan::X509_PublicKey;
using Botan::PKCS8_PrivateKey;
using Botan::PK_Decryptor;
using Botan::PK_Signer;
using Botan::Pipe;
using Botan::get_pk_decryptor;
using Botan::get_cipher;


typedef pair<key_name, keypair> key_info;
typedef pair<key_id, key_info> full_key_info;
typedef map<key_id, key_info> key_map;

struct key_store_state
{
  system_path const key_dir;
  string const ssh_sign_mode;
  bool non_interactive;
  bool have_read;
  lua_hooks & lua;
  key_map keys;

  // These are used to cache keys and signers (if the hook allows).
  map<key_id, shared_ptr<RSA_PrivateKey> > privkey_cache;
  map<key_id, shared_ptr<PK_Signer> > signer_cache;

  // Initialized when first required.
  scoped_ptr<ssh_agent> agent;

  key_store_state(app_state & app)
    : key_dir(app.opts.key_dir), ssh_sign_mode(app.opts.ssh_sign),
      non_interactive(app.opts.non_interactive),
      have_read(false), lua(app.lua)
  {
    E(app.opts.key_dir_given
      || app.opts.key_dir != system_path(get_default_keydir(), origin::user)
      || app.opts.conf_dir_given
      || !app.opts.no_default_confdir,
      origin::user,
      F("No available keystore found"));
  }

  // internal methods
  void get_key_file(key_id const & ident, key_name const & name,
                    system_path & file);
  void get_old_key_file(key_name const & name, system_path & file);
  void write_key(full_key_info const & info);
  void maybe_read_key_dir();
  shared_ptr<RSA_PrivateKey> decrypt_private_key(key_id const & id,
                                                 bool force_from_user = false);

  // just like put_key_pair except that the key is _not_ written to disk.
  // for internal use in reading keys back from disk.
  bool put_key_pair_memory(full_key_info const & info);

  // wrapper around accesses to agent, initializes as needed
  ssh_agent & get_agent()
  {
    if (!agent)
      agent.reset(new ssh_agent);
    return *agent;
  }

  // duplicates of key_store interfaces for use by key_store_state methods
  // and the keyreader.
  bool maybe_get_key_pair(key_id const & ident,
                          key_name & name,
                          keypair & kp);
  bool put_key_pair(full_key_info const & info);
  void migrate_old_key_pair(key_name const & id,
                            old_arc4_rsa_priv_key const & old_priv,
                            rsa_pub_key const & pub);
};

namespace
{
  struct keyreader : public packet_consumer
  {
    key_store_state & kss;

    keyreader(key_store_state & kss): kss(kss) {}
    virtual void consume_file_data(file_id const & ident,
                                   file_data const & dat)
    {E(false, origin::system, F("Extraneous data in key store."));}
    virtual void consume_file_delta(file_id const & id_old,
                                    file_id const & id_new,
                                    file_delta const & del)
    {E(false, origin::system, F("Extraneous data in key store."));}

    virtual void consume_revision_data(revision_id const & ident,
                                       revision_data const & dat)
    {E(false, origin::system, F("Extraneous data in key store."));}
    virtual void consume_revision_cert(cert const & t)
    {E(false, origin::system, F("Extraneous data in key store."));}


    virtual void consume_public_key(key_name const & ident,
                                    rsa_pub_key const & k)
    {E(false, origin::system, F("Extraneous data in key store."));}

    virtual void consume_key_pair(key_name const & name,
                                  keypair const & kp)
    {
      L(FL("reading key pair '%s' from key store") % name);

      key_id ident;
      key_hash_code(name, kp.pub, ident);
      E(kss.put_key_pair_memory(full_key_info(ident, key_info(name, kp))),
        origin::system,
        F("Key store has multiple copies of the key with id '%s'.") % ident);

      L(FL("successfully read key pair '%s' from key store") % ident);
    }

    // for backward compatibility
    virtual void consume_old_private_key(key_name const & ident,
                                         old_arc4_rsa_priv_key const & k)
    {
      W(F("converting old-format private key '%s'") % ident);

      rsa_pub_key dummy;
      kss.migrate_old_key_pair(ident, k, dummy);

      L(FL("successfully read key pair '%s' from key store") % ident);
    }
  };
}

key_store::key_store(app_state & a)
  : s(new key_store_state(a))
{}

key_store::~key_store()
{}

bool
key_store::have_signing_key() const
{
  return !signing_key.inner()().empty();
}

system_path const &
key_store::get_key_dir() const
{
  return s->key_dir;
}

void
key_store_state::maybe_read_key_dir()
{
  if (have_read)
    return;
  have_read = true;

  if (!directory_exists(key_dir))
    {
      L(FL("key dir '%s' does not exist") % key_dir);
      return;
    }

  L(FL("reading key dir '%s'") % key_dir);

  vector<system_path> key_files;
  fill_path_vec<system_path> fill_key_files(key_dir, key_files, false);
  dirent_ignore ignore;
  read_directory(key_dir, fill_key_files, ignore, ignore);

  keyreader kr(*this);
  for (vector<system_path>::const_iterator i = key_files.begin();
       i != key_files.end(); ++i)
    {
      L(FL("reading keys from file '%s'") % (*i));
      data dat;
      read_data(*i, dat);
      istringstream is(dat());
      if (read_packets(is, kr) == 0)
        W(F("ignored invalid key file ('%s') in key store") % (*i) );
    }
}

void
key_store::get_key_ids(vector<key_id> & priv)
{
  s->maybe_read_key_dir();
  priv.clear();
  for (key_map::const_iterator i = s->keys.begin(); i != s->keys.end(); ++i)
    priv.push_back(i->first);
}

bool
key_store::key_pair_exists(key_id const & ident)
{
  s->maybe_read_key_dir();
  return s->keys.find(ident) != s->keys.end();
}

bool
key_store::key_pair_exists(key_name const & name)
{
  s->maybe_read_key_dir();
  for (key_map::const_iterator i = s->keys.begin();
       i != s->keys.end(); ++i)
    {
      if (i->second.first == name)
        return true;
    }
  return false;
}

bool
key_store_state::maybe_get_key_pair(key_id const & ident,
                                    key_name & name,
                                    keypair & kp)
{
  maybe_read_key_dir();
  key_map::const_iterator i = keys.find(ident);
  if (i == keys.end())
    return false;
  name = i->second.first;
  kp = i->second.second;
  return true;
}

bool
key_store::maybe_get_key_pair(key_id const & ident,
                              keypair & kp)
{
  key_name name;
  return s->maybe_get_key_pair(ident, name, kp);
}

void
key_store::get_key_pair(key_id const & ident,
                        keypair & kp)
{
  MM(ident);
  bool found = maybe_get_key_pair(ident, kp);
  I(found);
}

bool
key_store::maybe_get_key_pair(key_id const & hash,
                              key_name & keyid,
                              keypair & kp)
{
  key_map::const_iterator ki = s->keys.find(hash);
  if (ki == s->keys.end())
    return false;
  keyid = ki->second.first;
  kp = ki->second.second;
  return true;
}

void
key_store::get_key_pair(key_id const & hash,
                        key_name & keyid,
                        keypair & kp)
{
  MM(hash);
  bool found = maybe_get_key_pair(hash, keyid, kp);
  I(found);
}

void
key_store_state::get_key_file(key_id const & ident,
                              key_name const & name,
                              system_path & file)
{
  hexenc<id> encoded;
  encode_hexenc(ident.inner(), encoded);

  static const string allowed_special_chars("@%^_-+=.,;~[]");
  string basename;
  for (string::const_iterator iter = name().begin();
       iter != name().end(); ++iter)
    {
      if (is_alnum(*iter) ||
          is_space(*iter) ||
          allowed_special_chars.find(*iter) != string::npos)
        {
          basename += *iter;
        }
      else
        {
          basename += '?';
        }
    }

  file = key_dir / path_component(basename + "." + encoded(),
                                  origin::internal);
}

void
key_store_state::get_old_key_file(key_name const & name,
                                  system_path & file)
{
  // filename is the keypair id, except that some characters can't be put in
  // filenames (especially on windows).
  string leaf = name();
  for (unsigned int i = 0; i < leaf.size(); ++i)
    if (leaf.at(i) == '+')
      leaf.at(i) = '_';

  file = key_dir / path_component(leaf, origin::internal);

}

void
key_store_state::write_key(full_key_info const & info)
{
  ostringstream oss;
  packet_writer pw(oss);
  pw.consume_key_pair(info.second.first, info.second.second);
  data dat(oss.str(), info.second.first.made_from);

  system_path file;
  get_key_file(info.first, info.second.first, file);

  // Make sure the private key is not readable by anyone other than the user.
  L(FL("writing key '%s' to file '%s' in dir '%s'")
    % info.first % file % key_dir);
  write_data_userprivate(file, dat, key_dir);

  system_path old_file;
  get_old_key_file(info.second.first, old_file);
  if (file_exists(old_file))
    delete_file(old_file);
}

bool
key_store_state::put_key_pair(full_key_info const & info)
{
  maybe_read_key_dir();
  bool newkey = put_key_pair_memory(info);
  if (newkey)
    write_key(info);
  return newkey;
}

bool
key_store::put_key_pair(key_name const & name,
                        keypair const & kp)
{
  key_id ident;
  key_hash_code(name, kp.pub, ident);
  return s->put_key_pair(full_key_info(ident, key_info(name, kp)));
}

bool
key_store_state::put_key_pair_memory(full_key_info const & info)
{
  L(FL("putting key pair '%s'") % info.first);
  pair<key_map::iterator, bool> res;
  res = keys.insert(info);
  if (!res.second)
    {
      L(FL("skipping existing key pair %s") % info.first);
      return false;
    }
  return true;
}

struct key_delete_validator : public packet_consumer
{
  key_id expected_ident;
  system_path file;
  key_delete_validator(key_id const & id, system_path const & f)
    : expected_ident(id), file(f) {}
  virtual ~key_delete_validator() {}
  virtual void consume_file_data(file_id const & ident,
                                 file_data const & dat)
  { E(false, origin::system, F("Invalid data in key file.")); }
  virtual void consume_file_delta(file_id const & id_old,
                                  file_id const & id_new,
                                  file_delta const & del)
  { E(false, origin::system, F("Invalid data in key file.")); }
  virtual void consume_revision_data(revision_id const & ident,
                                     revision_data const & dat)
  { E(false, origin::system, F("Invalid data in key file.")); }
  virtual void consume_revision_cert(cert const & t)
  { E(false, origin::system, F("Invalid data in key file.")); }
  virtual void consume_public_key(key_name const & ident,
                                  rsa_pub_key const & k)
  { E(false, origin::system, F("Invalid data in key file.")); }
  virtual void consume_key_pair(key_name const & name,
                                keypair const & kp)
  {
     L(FL("reading key pair '%s' from key store for validation") % name);
     key_id ident;
     key_hash_code(name, kp.pub, ident);
     E(ident == expected_ident, origin::user,
       F("expected key with id '%s' in key file '%s', got key with id '%s'")
         % expected_ident % file % ident);
  }
  virtual void consume_old_private_key(key_name const & ident,
                                       old_arc4_rsa_priv_key const & k)
  { L(FL("skipping id check before deleting old private key in '%s'") % file); }
};

void
key_store::delete_key(key_id const & ident)
{
  s->maybe_read_key_dir();
  key_map::iterator i = s->keys.find(ident);
  if (i != s->keys.end())
    {
      system_path file;
      s->get_key_file(ident, i->second.first, file);
      if (!file_exists(file))
          s->get_old_key_file(i->second.first, file);

      // sanity: if we read the key originally from a file which did not
      // follow the NAME.IDENT scheme and have another key pair with NAME
      // in the key dir, we could accidentially drop the wrong private key
      // here, so validate if the file really contains the key with the
      // ID we want to delete, before going mad
        {
          key_delete_validator val(ident, file);
          data dat;
          read_data(file, dat);
          istringstream is(dat());
          I(read_packets(is, val));
        }

      delete_file(file);

      s->keys.erase(i);
      s->signer_cache.erase(ident);
      s->privkey_cache.erase(ident);
    }
}

//
// Crypto operations
//

// "raw" passphrase prompter; unaware of passphrase caching or the laziness
// hook.  KEYID is used only in prompts.  CONFIRM_PHRASE causes the user to
// be prompted to type the same thing twice, and will loop if they don't
// match.  Prompts are worded slightly differently if GENERATING_KEY is true.
static void
get_passphrase(utf8 & phrase,
               key_name const & keyname,
               key_id const & keyid,
               bool confirm_phrase,
               bool generating_key)
{
  string prompt1, prompt2;
  char pass1[constants::maxpasswd];
  char pass2[constants::maxpasswd];
  int i = 0;

  hexenc<id> hexid;
  encode_hexenc(keyid.inner(), hexid);
  string const short_id = hexid().substr(0, 8) + "...";

  if (confirm_phrase && !generating_key)
    prompt1 = (F("enter new passphrase for key ID [%s] (%s): ")
               % keyname % short_id).str();
  else
    prompt1 = (F("enter passphrase for key ID [%s] (%s): ")
               % keyname % short_id).str();

  if (confirm_phrase)
    prompt2 = (F("confirm passphrase for key ID [%s] (%s): ")
               % keyname % short_id).str();

  try
    {
      for (;;)
        {
          memset(pass1, 0, constants::maxpasswd);
          memset(pass2, 0, constants::maxpasswd);
          ui.ensure_clean_line();

          read_password(prompt1, pass1, constants::maxpasswd);
          if (!confirm_phrase)
            break;

          ui.ensure_clean_line();
          read_password(prompt2, pass2, constants::maxpasswd);
          if (strcmp(pass1, pass2) == 0)
            break;

          E(i++ < 2, origin::user, F("too many failed passphrases"));
          P(F("passphrases do not match, try again"));
        }

      external ext_phrase(pass1);
      system_to_utf8(ext_phrase, phrase);
    }
  catch (...)
    {
      memset(pass1, 0, constants::maxpasswd);
      memset(pass2, 0, constants::maxpasswd);
      throw;
    }
  memset(pass1, 0, constants::maxpasswd);
  memset(pass2, 0, constants::maxpasswd);
}



shared_ptr<RSA_PrivateKey>
key_store_state::decrypt_private_key(key_id const & id,
                                     bool force_from_user)
{
  // See if we have this key in the decrypted key cache.
  map<key_id, shared_ptr<RSA_PrivateKey> >::const_iterator
    cpk = privkey_cache.find(id);
  if (cpk != privkey_cache.end())
    return cpk->second;

  keypair kp;
  key_name name;
  E(maybe_get_key_pair(id, name, kp), origin::user,
    F("no key pair '%s' found in key store '%s'") % id % key_dir);

  L(FL("%d-byte private key") % kp.priv().size());

  shared_ptr<PKCS8_PrivateKey> pkcs8_key;
  try // with empty passphrase
    {
      Botan::DataSource_Memory ds(kp.priv());
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
      pkcs8_key.reset(Botan::PKCS8::load_key(ds, lazy_rng::get(), ""));
#else
      pkcs8_key.reset(Botan::PKCS8::load_key(ds, ""));
#endif
    }
  catch (Botan::Exception & e)
    {
      L(FL("failed to load key with no passphrase: %s") % e.what());

      utf8 phrase;
      string lua_phrase;
      key_identity_info identity;
      identity.id = id;
      identity.given_name = name;

      // See whether a lua hook will tell us the passphrase.
      if ((!force_from_user || non_interactive) &&
          lua.hook_get_passphrase(identity, lua_phrase))
        {
          phrase = utf8(lua_phrase, origin::user);
        }
      else if (!non_interactive)
        {
          get_passphrase(phrase, name, id, false, false);
        }

      int cycles = 0;
      for (;;)
        try
          {
            Botan::DataSource_Memory ds(kp.priv());
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
            pkcs8_key.reset(Botan::PKCS8::load_key(ds, lazy_rng::get(), phrase()));
#else
            pkcs8_key.reset(Botan::PKCS8::load_key(ds, phrase()));
#endif
            break;
          }
        catch (Botan::Exception & e)
          {
            cycles++;
            L(FL("decrypt_private_key: failure %d to load encrypted key: %s")
              % cycles % e.what());
            E(cycles < 3 && !non_interactive, origin::no_fault,
              F("failed to decrypt old private RSA key, probably incorrect "
                "passphrase or missing 'get_passphrase' lua hook"));

            get_passphrase(phrase, name, id, false, false);
            continue;
          }
    }

  I(pkcs8_key);

  shared_ptr<RSA_PrivateKey> priv_key;
  priv_key = shared_dynamic_cast<RSA_PrivateKey>(pkcs8_key);
  E(priv_key, origin::no_fault,
    F("failed to extract RSA private key from PKCS#8 keypair"));

  // Cache the decrypted key if we're allowed.
  if (lua.hook_persist_phrase_ok())
    safe_insert(privkey_cache, make_pair(id, priv_key));

  return priv_key;
}

void
key_store::cache_decrypted_key(const key_id & id)
{
  signing_key = id;
  keypair key;
  get_key_pair(id, key);
  if (s->get_agent().has_key(key))
    {
      L(FL("ssh-agent has key '%s' loaded, skipping internal cache") % id);
      return;
    }

  if (s->lua.hook_persist_phrase_ok())
    s->decrypt_private_key(id);
}

void
key_store::create_key_pair(database & db,
                           key_name const & ident,
                           create_key_pair_mode create_mode,
                           utf8 const * maybe_passphrase,
                           key_id * const maybe_hash)
{
  conditional_transaction_guard guard(db);

  bool exists = false;
  for (key_map::iterator i = s->keys.begin(); i != s->keys.end(); ++i)
    {
      if (i->second.first == ident)
        exists = true;
    }
  E(!exists, origin::user, F("key '%s' already exists") % ident);

  utf8 prompted_passphrase;
  if (!maybe_passphrase)
    {
      get_passphrase(prompted_passphrase, ident, key_id(), true, true);
      maybe_passphrase = &prompted_passphrase;
    }

  // okay, now we can create the key
  if (create_mode == create_verbose)
    {
      P(F("generating key-pair '%s'") % ident);
    }
  else
    {
      L(FL("generating key-pair '%s'") % ident);
    }
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
  RSA_PrivateKey priv(lazy_rng::get(),
                      static_cast<Botan::u32bit>(constants::keylen));
#else
  RSA_PrivateKey priv(static_cast<Botan::u32bit>(constants::keylen));
#endif

  // serialize and maybe encrypt the private key
  keypair kp;
  SecureVector<Botan::byte> pubkey, privkey;

  unfiltered_pipe->start_msg();
  if ((*maybe_passphrase)().length())
    Botan::PKCS8::encrypt_key(priv, *unfiltered_pipe,
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
                              lazy_rng::get(),
#endif
                              (*maybe_passphrase)(),
                              "PBE-PKCS5v20(SHA-1,TripleDES/CBC)",
                              Botan::RAW_BER);
  else
    Botan::PKCS8::encode(priv, *unfiltered_pipe);
  unfiltered_pipe->end_msg();
  kp.priv = rsa_priv_key(unfiltered_pipe->read_all_as_string(Pipe::LAST_MESSAGE),
                         origin::internal);

  // serialize the public key
  unfiltered_pipe->start_msg();
  Botan::X509::encode(priv, *unfiltered_pipe, Botan::RAW_BER);
  unfiltered_pipe->end_msg();
  kp.pub = rsa_pub_key(unfiltered_pipe->read_all_as_string(Pipe::LAST_MESSAGE),
                       origin::internal);

  // convert to storage format
  L(FL("generated %d-byte public key\n"
      "generated %d-byte (encrypted) private key\n")
    % kp.pub().size()
    % kp.priv().size());

  // and save it.
  if (create_mode == create_verbose)
    {
      P(F("storing key-pair '%s' in %s/") % ident % get_key_dir());
    }
  else
    {
      L(FL("storing key-pair '%s' in %s/") % ident % get_key_dir());
    }
  put_key_pair(ident, kp);

  if (db.database_specified())
    {
      guard.acquire();
      if (create_mode == create_verbose)
        {
          P(F("storing public key '%s' in %s") % ident % db.get_filename());
        }
      else
        {
          L(FL("storing public key '%s' in %s") % ident % db.get_filename());
        }
      db.put_key(ident, kp.pub);
      guard.commit();
    }

  key_id hash;
  key_hash_code(ident, kp.pub, hash);
  if (maybe_hash)
    *maybe_hash = hash;
  if (create_mode == create_verbose)
    {
      P(F("key '%s' has hash '%s'") % ident % hash);
    }
}

void
key_store::change_key_passphrase(key_id const & id)
{
  key_name name;
  keypair kp;
  {
    bool found = false;
    s->maybe_read_key_dir();
    key_map::const_iterator i = s->keys.find(id);
    E(i != s->keys.end(), origin::user,
      F("no key pair '%s' found in key store '%s'") % id % s->key_dir);
    name = i->second.first;
    kp = i->second.second;
  }
  shared_ptr<RSA_PrivateKey> priv = s->decrypt_private_key(id, true);

  utf8 new_phrase;
  get_passphrase(new_phrase, name, id, true, false);

  unfiltered_pipe->start_msg();
  if (new_phrase().length())
    Botan::PKCS8::encrypt_key(*priv, *unfiltered_pipe,
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
                              lazy_rng::get(),
#endif
                              new_phrase(),
                              "PBE-PKCS5v20(SHA-1,TripleDES/CBC)",
                              Botan::RAW_BER);
  else
    Botan::PKCS8::encode(*priv, *unfiltered_pipe);

  unfiltered_pipe->end_msg();
  kp.priv = rsa_priv_key(unfiltered_pipe->read_all_as_string(Pipe::LAST_MESSAGE),
                         origin::internal);

  delete_key(id);
  put_key_pair(name, kp);
}

void
key_store::decrypt_rsa(key_id const & id,
                       rsa_oaep_sha_data const & ciphertext,
                       string & plaintext)
{
  try
    {
      keypair kp;
      load_key_pair(*this, id, kp);
      shared_ptr<RSA_PrivateKey> priv_key = s->decrypt_private_key(id);

      shared_ptr<PK_Decryptor>
        decryptor(get_pk_decryptor(*priv_key, "EME1(SHA-1)"));

      SecureVector<Botan::byte> plain =
        decryptor->decrypt(reinterpret_cast<Botan::byte const *>(ciphertext().data()),
                           ciphertext().size());
      plaintext = string(reinterpret_cast<char const*>(plain.begin()),
                         plain.size());
    }
  catch (Botan::Exception & ex)
    {
      E(false, ciphertext.made_from,
        F("Botan error decrypting data: '%s'") % ex.what());
    }
}

void
key_store::make_signature(database & db,
                          key_id const & id,
                          string const & tosign,
                          rsa_sha1_signature & signature)
{
  key_name name;
  keypair key;
  get_key_pair(id, name, key);

  // If the database doesn't have this public key, add it now.
  if (!db.public_key_exists(id))
    db.put_key(name, key.pub);

  string sig_string;
  ssh_agent & agent = s->get_agent();

  //sign with ssh-agent (if connected)
  E(agent.connected() || s->ssh_sign_mode != "only", origin::user,
    F("You have chosen to sign only with ssh-agent but ssh-agent"
      " does not seem to be running."));
  if (s->ssh_sign_mode == "yes"
      || s->ssh_sign_mode == "check"
      || s->ssh_sign_mode == "only")
    {
      if (agent.connected()) {
        //grab the monotone public key as an RSA_PublicKey
        SecureVector<Botan::byte> pub_block;
        pub_block.set(reinterpret_cast<Botan::byte const *>(key.pub().data()),
                      key.pub().size());
        L(FL("make_signature: building %d-byte pub key") % pub_block.size());
        shared_ptr<X509_PublicKey> x509_key =
          shared_ptr<X509_PublicKey>(Botan::X509::load_key(pub_block));
        shared_ptr<RSA_PublicKey> pub_key = shared_dynamic_cast<RSA_PublicKey>(x509_key);

        if (!pub_key)
          throw recoverable_failure(origin::system,
                                    "Failed to get monotone RSA public key");

        agent.sign_data(*pub_key, tosign, sig_string);
      }
      if (sig_string.length() <= 0)
        L(FL("make_signature: monotone and ssh-agent keys do not match, will"
             " use monotone signing"));
    }

  string ssh_sig = sig_string;

  E(ssh_sig.length() > 0 || s->ssh_sign_mode != "only", origin::user,
    F("You don't seem to have your monotone key imported "));

  if (ssh_sig.length() <= 0
      || s->ssh_sign_mode == "check"
      || s->ssh_sign_mode == "no")
    {
      SecureVector<Botan::byte> sig;

      // we permit the user to relax security here, by caching a decrypted key
      // (if they permit it) through the life of a program run. this helps when
      // you're making a half-dozen certs during a commit or merge or
      // something.

      bool persist_phrase = (!s->signer_cache.empty()
                             || s->lua.hook_persist_phrase_ok());

      shared_ptr<PK_Signer> signer;
      shared_ptr<RSA_PrivateKey> priv_key;
      if (persist_phrase && s->signer_cache.find(id) != s->signer_cache.end())
        signer = s->signer_cache[id];

      else
        {
          priv_key = s->decrypt_private_key(id);
          if (agent.connected()
              && s->ssh_sign_mode != "only"
              && s->ssh_sign_mode != "no") {
            L(FL("make_signature: adding private key (%s) to ssh-agent") % id);
            agent.add_identity(*priv_key, name());
          }
          signer = shared_ptr<PK_Signer>(get_pk_signer(*priv_key, "EMSA3(SHA-1)"));

          /* If persist_phrase is true, the RSA_PrivateKey object is
             cached in s->active_keys and will survive as long as the
             PK_Signer object does.  */
          if (persist_phrase)
            s->signer_cache.insert(make_pair(id, signer));
        }

#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
      sig = signer->sign_message(
        reinterpret_cast<Botan::byte const *>(tosign.data()),
        tosign.size(), lazy_rng::get());
#else
      sig = signer->sign_message(
        reinterpret_cast<Botan::byte const *>(tosign.data()),
        tosign.size());
#endif
      sig_string = string(reinterpret_cast<char const*>(sig.begin()), sig.size());
    }

  if (s->ssh_sign_mode == "check" && ssh_sig.length() > 0)
    {
      E(ssh_sig == sig_string, origin::system,
        F("make_signature: ssh signature (%i) != monotone signature (%i)\n"
          "ssh signature     : %s\n"
          "monotone signature: %s")
        % ssh_sig.length()
        % sig_string.length()
        % ssh_sig
        % sig_string);
      L(FL("make_signature: signatures from ssh-agent and monotone"
           " are the same"));
    }

  L(FL("make_signature: produced %d-byte signature") % sig_string.size());
  signature = rsa_sha1_signature(sig_string, origin::internal);

  cert_status s = db.check_signature(id, tosign, signature);
  I(s != cert_unknown);
  E(s == cert_ok, origin::system, F("make_signature: signature is not valid"));
}

//
// Interoperation with ssh-agent (see also above)
//

void
key_store::add_key_to_agent(key_id const & id)
{
  ssh_agent & agent = s->get_agent();
  E(agent.connected(), origin::user,
    F("no ssh-agent is available, cannot add key '%s'") % id);

  shared_ptr<RSA_PrivateKey> priv = s->decrypt_private_key(id);

  key_name name;
  keypair kp;
  s->maybe_get_key_pair(id, name, kp);
  agent.add_identity(*priv, name());
}

void
key_store::export_key_for_agent(key_id const & id,
                                std::ostream & os)
{
  shared_ptr<RSA_PrivateKey> priv = s->decrypt_private_key(id);

  key_name name;
  {
    keypair kp;
    I(s->maybe_get_key_pair(id, name, kp));
  }

  utf8 new_phrase;
  get_passphrase(new_phrase, name, id, true, false);

  // This pipe cannot sensibly be recycled.
  Pipe p(new Botan::DataSink_Stream(os));
  p.start_msg();
  if (new_phrase().length())
    Botan::PKCS8::encrypt_key(*priv,
                              p,
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
                              lazy_rng::get(),
#endif
                              new_phrase(),
                              "PBE-PKCS5v20(SHA-1,TripleDES/CBC)");
  else
    Botan::PKCS8::encode(*priv, p);
  p.end_msg();
}


//
// Migration from old databases
//

void
key_store_state::migrate_old_key_pair
    (key_name const & id,
     old_arc4_rsa_priv_key const & old_priv,
     rsa_pub_key const & pub)
{
  keypair kp;
  SecureVector<Botan::byte> arc4_key;
  utf8 phrase;
  shared_ptr<PKCS8_PrivateKey> pkcs8_key;
  shared_ptr<RSA_PrivateKey> priv_key;

  // See whether a lua hook will tell us the passphrase.
  key_identity_info identity;
  identity.given_name = id;
  string lua_phrase;
  if (lua.hook_get_passphrase(identity, lua_phrase))
    phrase = utf8(lua_phrase, origin::user);
  else
    get_passphrase(phrase, id, key_id(), false, false);

  int cycles = 1;
  for (;;)
    try
      {
        arc4_key.set(reinterpret_cast<Botan::byte const *>(phrase().data()),
                     phrase().size());

        Pipe arc4_decryptor(get_cipher("ARC4", arc4_key, Botan::DECRYPTION));

        arc4_decryptor.process_msg(old_priv());

        // This is necessary because PKCS8::load_key() cannot currently
        // recognize an unencrypted, raw-BER blob as such, but gets it
        // right if it's PEM-coded.
        SecureVector<Botan::byte> arc4_decrypt(arc4_decryptor.read_all());
        Botan::DataSource_Memory ds(Botan::PEM_Code::encode(arc4_decrypt,
                                                            "PRIVATE KEY"));
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
        pkcs8_key.reset(Botan::PKCS8::load_key(ds, lazy_rng::get()));
#else
        pkcs8_key.reset(Botan::PKCS8::load_key(ds));
#endif
        break;
      }
    catch (Botan::Exception & e)
      {
        L(FL("migrate_old_key_pair: failure %d to load old private key: %s")
          % cycles % e.what());

        E(cycles <= 3, origin::no_fault,
          F("failed to decrypt old private RSA key, "
            "probably incorrect passphrase"));

        get_passphrase(phrase, id, key_id(), false, false);
        cycles++;
        continue;
      }

  priv_key = shared_dynamic_cast<RSA_PrivateKey>(pkcs8_key);
  I(priv_key);

  // now we can write out the new key
  unfiltered_pipe->start_msg();
  Botan::PKCS8::encrypt_key(*priv_key, *unfiltered_pipe,
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
                            lazy_rng::get(),
#endif
                            phrase(),
                            "PBE-PKCS5v20(SHA-1,TripleDES/CBC)",
                            Botan::RAW_BER);
  unfiltered_pipe->end_msg();
  kp.priv = rsa_priv_key(unfiltered_pipe->read_all_as_string(Pipe::LAST_MESSAGE),
                         origin::internal);

  // also the public key (which is derivable from the private key; asking
  // Botan for the X.509 encoding of the private key implies that we want
  // it to derive and produce the public key)
  unfiltered_pipe->start_msg();
  Botan::X509::encode(*priv_key, *unfiltered_pipe, Botan::RAW_BER);
  unfiltered_pipe->end_msg();
  kp.pub = rsa_pub_key(unfiltered_pipe->read_all_as_string(Pipe::LAST_MESSAGE),
                       origin::internal);

  // if the database had a public key entry for this key, make sure it
  // matches what we derived from the private key entry, but don't abort the
  // whole migration if it doesn't.
  if (!pub().empty() && !keys_match(id, pub, id, kp.pub))
    W(F("public and private keys for %s don't match") % id);

  key_id hash;
  key_hash_code(id, kp.pub, hash);
  put_key_pair(full_key_info(hash, key_info(id, kp)));
}

void
key_store::migrate_old_key_pair
    (key_name const & id,
     old_arc4_rsa_priv_key const & old_priv,
     rsa_pub_key const & pub)
{
  s->migrate_old_key_pair(id, old_priv, pub);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
