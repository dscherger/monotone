// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "cert.hh"
#include "constants.hh"
#include "database.hh" // lookup key name for hashing
#include "netio.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"

using std::string;

bool
cert::operator<(cert const & other) const
{
  return (ident < other.ident)
    || ((ident == other.ident) && name < other.name)
    || (((ident == other.ident) && name == other.name)
        && value < other.value)
    || ((((ident == other.ident) && name == other.name)
         && value == other.value) && key < other.key)
    || (((((ident == other.ident) && name == other.name)
          && value == other.value) && key == other.key) && sig < other.sig);
}

bool
cert::operator==(cert const & other) const
{
  return
    (ident == other.ident)
    && (name == other.name)
    && (value == other.value)
    && (key == other.key)
    && (sig == other.sig);
}

// netio support
enum read_cert_version {read_cert_v6, read_cert_current};

static bool
read_cert(database & db, string const & in, cert & t,
          read_cert_version ver, key_name & keyname)
{
  size_t pos = 0;
  id hash = id(extract_substring(in, pos,
                                 constants::merkle_hash_length_in_bytes,
                                 "cert hash"),
               origin::network);
  revision_id ident = revision_id(extract_substring(in, pos,
                                  constants::merkle_hash_length_in_bytes,
                                                    "cert ident"),
                                  origin::network);
  string name, val, key, sig;
  extract_variable_length_string(in, name, pos, "cert name");
  extract_variable_length_string(in, val, pos, "cert val");
  extract_variable_length_string(in, key, pos, "cert key");
  extract_variable_length_string(in, sig, pos, "cert sig");
  assert_end_of_buffer(in, pos, "cert");

  cert tmp;
  tmp.ident = ident;
  tmp.name = cert_name(name, origin::network);
  tmp.value = cert_value(val, origin::network);
  tmp.sig = rsa_sha1_signature(sig, origin::network);
  string signable;
  tmp.signable_text(signable);

  key_id keyid;
  switch(ver)
    {
    case read_cert_v6:
      {
        keyname = key_name(key, origin::network);
        bool found = false;
        std::vector<key_id> all_keys;
        db.get_key_ids(all_keys);
        for (std::vector<key_id>::const_iterator i = all_keys.begin();
             i != all_keys.end(); ++i)
          {
            key_name i_keyname;
            rsa_pub_key pub;
            db.get_pubkey(*i, i_keyname, pub);
            if (i_keyname() == key)
              {
                if(db.check_signature(*i, signable, tmp.sig) == cert_ok)
                  {
                    tmp.key = *i;
                    found = true;
                    break;
                  }
              }
          }
        if (!found)
          {
            W(F("Cannot find appropriate key '%s' for old-style cert")
              % name);
            return false;
          }
      }
      break;
    case read_cert_current:
      tmp.key = key_id(key, origin::network);
      break;
    default:
      I(false);
    }

  rsa_pub_key junk;
  db.get_pubkey(tmp.key, keyname, junk);

  id check;
  tmp.hash_code(keyname, check);
  if (!(check == hash))
    throw bad_decode(F("calculated cert hash '%s' does not match '%s'")
                     % check % hash);
  t = tmp;
  return true;
}

bool cert::read_cert_v6(database & db, std::string const & s, cert & c,
                        key_name & keyname)
{
  return ::read_cert(db, s, c, ::read_cert_v6, keyname);
}

bool cert::read_cert(database & db, std::string const & s, cert & c)
{
  key_name keyname;
  return ::read_cert(db, s, c, read_cert_current, keyname);
}

cert::cert(database & db, std::string const & s, origin::type m)
  : origin_aware(m)
{
  key_name keyname;
  ::read_cert(db, s, *this, read_cert_current, keyname);
}

void
cert::marshal_for_netio(key_name const & keyname, string & out) const
{
  id hash;
  hash_code(keyname, hash);

  out.append(hash());
  out.append(this->ident.inner()());
  insert_variable_length_string(this->name(), out);
  insert_variable_length_string(this->value(), out);
  insert_variable_length_string(this->key.inner()(), out);
  insert_variable_length_string(this->sig(), out);
}

void
cert::marshal_for_netio_v6(key_name const & keyname, string & out) const
{
  id hash;
  hash_code(keyname, hash);

  out.append(hash());
  out.append(this->ident.inner()());
  insert_variable_length_string(this->name(), out);
  insert_variable_length_string(this->value(), out);
  insert_variable_length_string(keyname(), out);
  insert_variable_length_string(this->sig(), out);
}

void
cert::signable_text(string & out) const
{
  base64<cert_value> val_encoded(encode_base64(this->value));
  string ident_encoded(encode_hexenc(this->ident.inner()(),
                                     this->ident.inner().made_from));

  out.clear();
  out.reserve(4 + this->name().size() + ident_encoded.size()
              + val_encoded().size());

  out += '[';
  out.append(this->name());
  out += '@';
  out.append(ident_encoded);
  out += ':';
  append_without_ws(out, val_encoded());
  out += ']';

  L(FL("cert: signable text %s") % out);
}

void
cert::hash_code(key_name const & keyname, id & out) const
{
  base64<rsa_sha1_signature> sig_encoded(encode_base64(this->sig));
  base64<cert_value> val_encoded(encode_base64(this->value));
  string ident_encoded(encode_hexenc(this->ident.inner()(),
                                     this->ident.inner().made_from));
  string tmp;
  tmp.reserve(4 + ident_encoded.size()
              + this->name().size() + val_encoded().size()
              + this->key.inner()().size() + sig_encoded().size());

  tmp.append(ident_encoded);
  tmp += ':';
  tmp.append(this->name());
  tmp += ':';
  append_without_ws(tmp, val_encoded());
  tmp += ':';
  tmp.append(keyname());
  tmp += ':';
  append_without_ws(tmp, sig_encoded());

  data tdat(tmp, origin::internal);
  calculate_ident(tdat, out);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
