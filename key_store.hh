#ifndef __KEY_STORE_H__
#define __KEY_STORE_H__

#include <map>

#include "vocab.hh"
#include "paths.hh"
#include "platform.hh"

class app_state;

struct keyreader;

class key_store
{
private:
  friend struct keyreader;
  system_path key_dir;
  bool have_read;
  app_state * app;
  std::map<rsa_keypair_id, keypair> keys;
  std::map<hexenc<id>, rsa_keypair_id> hashes;

  void get_key_file(rsa_keypair_id const & ident, system_path & file);
  void write_key(rsa_keypair_id const & ident);
  void read_key_dir();
  void maybe_read_key_dir();
public:
  key_store(app_state * a);
  void set_key_dir(system_path const & kd);
  system_path const & get_key_dir();

  void ensure_in_database(rsa_keypair_id const & ident);
  bool try_ensure_in_db(hexenc<id> const & hash);

  void get_key_ids(std::string const & pattern,
                   std::vector<rsa_keypair_id> & priv);

  void get_keys(std::vector<rsa_keypair_id> & priv);

  bool key_pair_exists(rsa_keypair_id const & ident);

  void get_key_pair(rsa_keypair_id const & ident,
                    keypair & kp);

  void put_key_pair(rsa_keypair_id const & ident,
                    keypair const & kp);

  void delete_key(rsa_keypair_id const & ident);
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
