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

static void
read_cert(string const & in, cert & t)
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

  cert tmp(ident, cert_name(name, origin::network),
           cert_value(val, origin::network),
           key_name(key, origin::network),
           rsa_sha1_signature(sig, origin::network));

  id check;
  tmp.hash_code(check);
  if (!(check == hash))
    throw bad_decode(F("calculated cert hash '%s' does not match '%s'")
                     % check % hash);
  t = tmp;
}

cert::cert(std::string const & s)
{
  read_cert(s, *this);
}

cert::cert(std::string const & s, origin::type m)
  : origin_aware(m)
{
  read_cert(s, *this);
}

void
cert::marshal_for_netio(string & out) const
{
  string name, key;
  id hash;

  hash_code(hash);

  out.append(hash());
  out.append(this->ident.inner()());
  insert_variable_length_string(this->name(), out);
  insert_variable_length_string(this->value(), out);
  insert_variable_length_string(this->key(), out);
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
cert::hash_code(id & out) const
{
  base64<rsa_sha1_signature> sig_encoded(encode_base64(this->sig));
  base64<cert_value> val_encoded(encode_base64(this->value));
  string ident_encoded(encode_hexenc(this->ident.inner()(),
                                     this->ident.inner().made_from));
  string tmp;
  tmp.reserve(4 + ident_encoded.size()
              + this->name().size() + val_encoded().size()
              + this->key().size() + sig_encoded().size());
  tmp.append(ident_encoded);
  tmp += ':';
  tmp.append(this->name());
  tmp += ':';
  append_without_ws(tmp, val_encoded());
  tmp += ':';
  tmp.append(this->key());
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
