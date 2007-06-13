// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>

#include "lexical_cast.hh"
#include "app_state.hh"
#include "cert.hh"
#include "constants.hh"
#include "interner.hh"
#include "keys.hh"
#include "netio.hh"
#include "option.hh"
#include "revision.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "ui.hh"

using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;
using std::remove_if;

using boost::shared_ptr;
using boost::get;
using boost::tuple;
using boost::lexical_cast;

// The alternaive is to #include "cert.hh" in vocab.*, which is even
// uglier.

#include "vocab_macros.hh"
cc_DECORATE(revision)
cc_DECORATE(manifest)
template <typename T>
static inline void
verify(T & val)
{}
template class revision<cert>;
template class manifest<cert>;

// FIXME: the bogus-cert family of functions is ridiculous
// and needs to be replaced, or at least factored.

struct
bogus_cert_p
{
  app_state & app;
  bogus_cert_p(app_state & a) : app(a) {};

  bool cert_is_bogus(cert const & c) const
  {
    cert_status status = check_cert(app, c);
    if (status == cert_ok)
      {
        L(FL("cert ok"));
        return false;
      }
    else if (status == cert_bad)
      {
        string txt;
        cert_signable_text(c, txt);
        W(F("ignoring bad signature by '%s' on '%s'") % c.key() % txt);
        return true;
      }
    else
      {
        I(status == cert_unknown);
        string txt;
        cert_signable_text(c, txt);
        W(F("ignoring unknown signature by '%s' on '%s'") % c.key() % txt);
        return true;
      }
  }

  bool operator()(revision<cert> const & c) const
  {
    return cert_is_bogus(c.inner());
  }

  bool operator()(manifest<cert> const & c) const
  {
    return cert_is_bogus(c.inner());
  }
};


void
erase_bogus_certs(vector< manifest<cert> > & certs,
                  app_state & app)
{
  typedef vector< manifest<cert> >::iterator it;
  it e = remove_if(certs.begin(), certs.end(), bogus_cert_p(app));
  certs.erase(e, certs.end());

  vector< manifest<cert> > tmp_certs;

  // Sorry, this is a crazy data structure
  typedef tuple< hexenc<id>, cert_name, base64<cert_value> > trust_key;
  typedef map< trust_key, 
    pair< shared_ptr< set<rsa_keypair_id> >, it > > trust_map;
  trust_map trust;

  for (it i = certs.begin(); i != certs.end(); ++i)
    {
      trust_key key = trust_key(i->inner().ident, 
                                i->inner().name, 
                                i->inner().value);
      trust_map::iterator j = trust.find(key);
      shared_ptr< set<rsa_keypair_id> > s;
      if (j == trust.end())
        {
          s.reset(new set<rsa_keypair_id>());
          trust.insert(make_pair(key, make_pair(s, i)));
        }
      else
        s = j->second.first;
      s->insert(i->inner().key);
    }

  for (trust_map::const_iterator i = trust.begin();
       i != trust.end(); ++i)
    {
      cert_value decoded_value;
      decode_base64(get<2>(i->first), decoded_value);
      if (app.lua.hook_get_manifest_cert_trust(*(i->second.first),
                                               get<0>(i->first),
                                               get<1>(i->first),
                                               decoded_value))
        {
          L(FL("trust function liked %d signers of %s cert on manifest %s")
            % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
          tmp_certs.push_back(*(i->second.second));
        }
      else
        {
          W(F("trust function disliked %d signers of %s cert on manifest %s")
            % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
        }
    }
  certs = tmp_certs;
}

void
erase_bogus_certs(vector< revision<cert> > & certs,
                  app_state & app)
{
  typedef vector< revision<cert> >::iterator it;
  it e = remove_if(certs.begin(), certs.end(), bogus_cert_p(app));
  certs.erase(e, certs.end());

  vector< revision<cert> > tmp_certs;

  // sorry, this is a crazy data structure
  typedef tuple< hexenc<id>, 
    cert_name, base64<cert_value> > trust_key;
  typedef map< trust_key, 
    pair< shared_ptr< set<rsa_keypair_id> >, it > > trust_map;
  trust_map trust;

  for (it i = certs.begin(); i != certs.end(); ++i)
    {
      trust_key key = trust_key(i->inner().ident, 
                                i->inner().name, 
                                i->inner().value);
      trust_map::iterator j = trust.find(key);
      shared_ptr< set<rsa_keypair_id> > s;
      if (j == trust.end())
        {
          s.reset(new set<rsa_keypair_id>());
          trust.insert(make_pair(key, make_pair(s, i)));
        }
      else
        s = j->second.first;
      s->insert(i->inner().key);
    }

  for (trust_map::const_iterator i = trust.begin();
       i != trust.end(); ++i)
    {
      cert_value decoded_value;
      decode_base64(get<2>(i->first), decoded_value);
      if (app.lua.hook_get_revision_cert_trust(*(i->second.first),
                                               get<0>(i->first),
                                               get<1>(i->first),
                                               decoded_value))
        {
          L(FL("trust function liked %d signers of %s cert on revision %s")
            % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
          tmp_certs.push_back(*(i->second.second));
        }
      else
        {
          W(F("trust function disliked %d signers of %s cert on revision %s")
            % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
        }
    }
  certs = tmp_certs;
}


// cert-managing routines

cert::cert()
{}

cert::cert(std::string const & s)
{
  read_cert(s, *this);
}

cert::cert(hexenc<id> const & ident,
           cert_name const & name,
           base64<cert_value> const & value,
           rsa_keypair_id const & key)
  : ident(ident), name(name), value(value), key(key)
{}

cert::cert(hexenc<id> const & ident,
         cert_name const & name,
         base64<cert_value> const & value,
         rsa_keypair_id const & key,
         base64<rsa_sha1_signature> const & sig)
  : ident(ident), name(name), value(value), key(key), sig(sig)
{}

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

void
read_cert(string const & in, cert & t)
{
  size_t pos = 0;
  id hash = id(extract_substring(in, pos,
                                 constants::merkle_hash_length_in_bytes,
                                 "cert hash"));
  id ident = id(extract_substring(in, pos,
                                  constants::merkle_hash_length_in_bytes,
                                  "cert ident"));
  string name, val, key, sig;
  extract_variable_length_string(in, name, pos, "cert name");
  extract_variable_length_string(in, val, pos, "cert val");
  extract_variable_length_string(in, key, pos, "cert key");
  extract_variable_length_string(in, sig, pos, "cert sig");
  assert_end_of_buffer(in, pos, "cert");

  hexenc<id> hid;
  base64<cert_value> bval;
  base64<rsa_sha1_signature> bsig;

  encode_hexenc(ident, hid);
  encode_base64(cert_value(val), bval);
  encode_base64(rsa_sha1_signature(sig), bsig);

  cert tmp(hid, cert_name(name), bval, rsa_keypair_id(key), bsig);

  hexenc<id> hcheck;
  id check;
  cert_hash_code(tmp, hcheck);
  decode_hexenc(hcheck, check);
  if (!(check == hash))
    {
      hexenc<id> hhash;
      encode_hexenc(hash, hhash);
      throw bad_decode(F("calculated cert hash '%s' does not match '%s'")
                       % hcheck % hhash);
    }
  t = tmp;
}

void
write_cert(cert const & t, string & out)
{
  string name, key;
  hexenc<id> hash;
  id ident_decoded, hash_decoded;
  rsa_sha1_signature sig_decoded;
  cert_value value_decoded;

  cert_hash_code(t, hash);
  decode_base64(t.value, value_decoded);
  decode_base64(t.sig, sig_decoded);
  decode_hexenc(t.ident, ident_decoded);
  decode_hexenc(hash, hash_decoded);

  out.append(hash_decoded());
  out.append(ident_decoded());
  insert_variable_length_string(t.name(), out);
  insert_variable_length_string(value_decoded(), out);
  insert_variable_length_string(t.key(), out);
  insert_variable_length_string(sig_decoded(), out);
}

void
cert_signable_text(cert const & t,
                   string & out)
{
  out = (FL("[%s@%s:%s]") % t.name % t.ident % remove_ws(t.value())).str();
  L(FL("cert: signable text %s") % out);
}

void
cert_hash_code(cert const & t, hexenc<id> & out)
{
  string tmp;
  tmp.reserve(4+t.ident().size() + t.name().size() + t.value().size() +
              t.key().size() + t.sig().size());
  tmp.append(t.ident());
  tmp += ':';
  tmp.append(t.name());
  tmp += ':';
  append_without_ws(tmp,t.value());
  tmp += ':';
  tmp.append(t.key());
  tmp += ':';
  append_without_ws(tmp,t.sig());

  data tdat(tmp);
  calculate_ident(tdat, out);
}

bool
priv_key_exists(app_state & app, rsa_keypair_id const & id)
{

  return app.keys.key_pair_exists(id);
}

// Loads a key pair for a given key id, from either a lua hook
// or the key store. This will bomb out if the same keyid exists
// in both with differing contents.

void
load_key_pair(app_state & app,
              rsa_keypair_id const & id,
              keypair & kp)
{

  static map<rsa_keypair_id, keypair> keys;
  bool persist_ok = (!keys.empty()) || app.lua.hook_persist_phrase_ok();

  if (persist_ok && keys.find(id) != keys.end())
    {
      kp = keys[id];
    }
  else
    {
      N(app.keys.key_pair_exists(id),
        F("no key pair '%s' found in key store '%s'")
        % id % app.keys.get_key_dir());
      app.keys.get_key_pair(id, kp);
      if (persist_ok)
        keys.insert(make_pair(id, kp));
    }
}

void
calculate_cert(app_state & app, cert & t)
{
  string signed_text;
  keypair kp;
  cert_signable_text(t, signed_text);

  load_key_pair(app, t.key, kp);
  app.db.put_key(t.key, kp.pub);

  make_signature(app, t.key, kp.priv, signed_text, t.sig);
}

cert_status
check_cert(app_state & app, cert const & t)
{

  base64< rsa_pub_key > pub;

  static map<rsa_keypair_id, base64< rsa_pub_key > > pubkeys;
  bool persist_ok = (!pubkeys.empty()) || app.lua.hook_persist_phrase_ok();

  if (persist_ok
      && pubkeys.find(t.key) != pubkeys.end())
    {
      pub = pubkeys[t.key];
    }
  else
    {
      if (!app.db.public_key_exists(t.key))
        return cert_unknown;
      app.db.get_key(t.key, pub);
      if (persist_ok)
        pubkeys.insert(make_pair(t.key, pub));
    }

  string signed_text;
  cert_signable_text(t, signed_text);
  if (check_signature(app, t.key, pub, signed_text, t.sig))
    return cert_ok;
  else
    return cert_bad;
}


// "special certs"

void
get_user_key(rsa_keypair_id & key, app_state & app)
{

  if (app.opts.signing_key() != "")
    {
      key = app.opts.signing_key;
      return;
    }

  if (app.lua.hook_get_branch_key(app.opts.branchname, key))
    return;

  vector<rsa_keypair_id> all_privkeys;
  app.keys.get_keys(all_privkeys);
  N(!all_privkeys.empty(), 
    F("you have no private key to make signatures with\n"
      "perhaps you need to 'genkey <your email>'"));
  N(all_privkeys.size() == 1,
    F("you have multiple private keys\n"
      "pick one to use for signatures by adding '-k<keyname>' to your command"));
  key = all_privkeys[0];
}

// Guess which branch is appropriate for a commit below IDENT.
// APP may override.  Branch name is returned in BRANCHNAME.
// Does not modify branch state in APP.
void
guess_branch(revision_id const & ident, app_state & app, branch_name & branchname)
{
  if (app.opts.branch_given && !app.opts.branchname().empty())
    branchname = app.opts.branchname;
  else
    {
      N(!ident.inner()().empty(),
        F("no branch found for empty revision, "
          "please provide a branch name"));

      set<branch_name> branches;
      app.get_project().get_revision_branches(ident, branches);

      N(branches.size() != 0,
        F("no branch certs found for revision %s, "
          "please provide a branch name") % ident);

      N(branches.size() == 1,
        F("multiple branch certs found for revision %s, "
          "please provide a branch name") % ident);

      set<branch_name>::iterator i = branches.begin();
      I(i != branches.end());
      branchname = *i;
    }
}

// As above, but set the branch name in the app state.
void
guess_branch(revision_id const & ident, app_state & app)
{
  branch_name branchname;
  guess_branch(ident, app, branchname);
  app.opts.branchname = branchname;
}

void
make_simple_cert(hexenc<id> const & id,
                 cert_name const & nm,
                 cert_value const & cv,
                 app_state & app,
                 cert & c)
{
  rsa_keypair_id key;
  get_user_key(key, app);
  base64<cert_value> encoded_val;
  encode_base64(cv, encoded_val);
  cert t(id, nm, encoded_val, key);
  calculate_cert(app, t);
  c = t;
}

void
put_simple_revision_cert(revision_id const & id,
                         cert_name const & nm,
                         cert_value const & val,
                         app_state & app)
{
  cert t;
  make_simple_cert(id.inner(), nm, val, app, t);
  revision<cert> cc(t);
  app.db.put_revision_cert(cc);
}

void
cert_revision_in_branch(revision_id const & rev,
                        branch_name const & branch,
                        app_state & app)
{
  put_simple_revision_cert (rev, branch_cert_name, cert_value(branch()),
                            app);
}


// "standard certs"

void
cert_revision_date_time(revision_id const & m,
                        date_t const & t,
                        app_state & app)
{
  cert_value val = cert_value(t.as_iso_8601_extended());
  put_simple_revision_cert(m, date_cert_name, val, app);
}

void
cert_revision_author(revision_id const & m,
                     string const & author,
                     app_state & app)
{
  put_simple_revision_cert(m, author_cert_name, cert_value(author), app);
}

void
cert_revision_author_default(revision_id const & m,
                             app_state & app)
{
  string author;
  rsa_keypair_id key;
  get_user_key(key, app);

  if (!app.lua.hook_get_author(app.opts.branchname, key, author))
    {
      author = key();
    }
  cert_revision_author(m, author, app);
}

void
cert_revision_tag(revision_id const & m,
                  string const & tagname,
                  app_state & app)
{
  put_simple_revision_cert(m, tag_cert_name, cert_value(tagname), app);
}


void
cert_revision_changelog(revision_id const & m,
                        utf8 const & log,
                        app_state & app)
{
  put_simple_revision_cert(m, changelog_cert_name, cert_value(log()), app);
}

void
cert_revision_comment(revision_id const & m,
                      utf8 const & comment,
                      app_state & app)
{
  put_simple_revision_cert(m, comment_cert_name, cert_value(comment()), app);
}

void
cert_revision_testresult(revision_id const & r,
                         string const & results,
                         app_state & app)
{
  bool passed = false;
  if (lowercase(results) == "true" ||
      lowercase(results) == "yes" ||
      lowercase(results) == "pass" ||
      results == "1")
    passed = true;
  else if (lowercase(results) == "false" ||
           lowercase(results) == "no" ||
           lowercase(results) == "fail" ||
           results == "0")
    passed = false;
  else
    throw informative_failure("could not interpret test results, "
                              "tried '0/1' 'yes/no', 'true/false', "
                              "'pass/fail'");

  put_simple_revision_cert(r, testresult_cert_name,
                           cert_value(lexical_cast<string>(passed)), app);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
