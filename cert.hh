#ifndef __CERT_HH__
#define __CERT_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <map>
#include <set>
#include <vector>

#include "vocab.hh"
#include "dates.hh"

// Certs associate an opaque name/value pair with a revision ID, and
// are accompanied by an RSA public-key signature attesting to the
// association. Users can write as much extra meta-data as they like
// about revisions, using certs, without needing anyone's special
// permission.

class app_state;
class key_store;
class database;

struct cert
{
  cert();

  // This is to make revision<cert> and manifest<cert> work.
  explicit cert(std::string const & s);

  cert(hexenc<id> const & ident,
      cert_name const & name,
      base64<cert_value> const & value,
      rsa_keypair_id const & key);
  cert(hexenc<id> const & ident,
      cert_name const & name,
      base64<cert_value> const & value,
      rsa_keypair_id const & key,
      base64<rsa_sha1_signature> const & sig);
  hexenc<id> ident;
  cert_name name;
  base64<cert_value> value;
  rsa_keypair_id key;
  base64<rsa_sha1_signature> sig;
  bool operator<(cert const & other) const;
  bool operator==(cert const & other) const;
};

EXTERN template class revision<cert>;
EXTERN template class manifest<cert>;


// These 3 are for netio support.
void read_cert(std::string const & in, cert & t);
void write_cert(cert const & t, std::string & out);
void cert_hash_code(cert const & t, hexenc<id> & out);

typedef enum {cert_ok, cert_bad, cert_unknown} cert_status;

void cert_signable_text(cert const & t,std::string & out);
cert_status check_cert(database & db, cert const & t);
bool priv_key_exists(key_store & keys, rsa_keypair_id const & id);
void load_key_pair(key_store & keys,
                   rsa_keypair_id const & id,
                   keypair & kp);

// Only used in cert.cc, and in revision.cc in what looks
// like migration code.
void make_simple_cert(hexenc<id> const & id,
                      cert_name const & nm,
                      cert_value const & cv,
                      app_state & app,
                      cert & c);

void put_simple_revision_cert(revision_id const & id,
                              cert_name const & nm,
                              cert_value const & val,
                              app_state & app);

void erase_bogus_certs(std::vector< revision<cert> > & certs,
                       database & db);

void erase_bogus_certs(std::vector< manifest<cert> > & certs,
                       database & db);

// Special certs -- system won't work without them.

#define branch_cert_name cert_name("branch")

void
cert_revision_in_branch(revision_id const & ctx,
                        branch_name const & branchname,
                        app_state & app);


// We also define some common cert types, to help establish useful
// conventions. you should use these unless you have a compelling
// reason not to.

// N()'s out if there is no unique key for us to use
void
get_user_key(rsa_keypair_id & key, key_store & keys);

void
guess_branch(revision_id const & id, app_state & app, branch_name & branchname);
void
guess_branch(revision_id const & id, app_state & app);

#define date_cert_name cert_name("date")
#define author_cert_name cert_name("author")
#define tag_cert_name cert_name("tag")
#define changelog_cert_name cert_name("changelog")
#define comment_cert_name cert_name("comment")
#define testresult_cert_name cert_name("testresult")

void
cert_revision_date_time(revision_id const & m,
                        date_t const & t,
                        app_state & app);

void
cert_revision_author(revision_id const & m,
                    std::string const & author,
                    app_state & app);

void
cert_revision_author_default(revision_id const & m,
                            app_state & app);

void
cert_revision_tag(revision_id const & m,
                 std::string const & tagname,
                 app_state & app);

void
cert_revision_changelog(revision_id const & m,
                        utf8 const & changelog,
                        app_state & app);

void
cert_revision_comment(revision_id const & m,
                      utf8 const & comment,
                      app_state & app);

void
cert_revision_testresult(revision_id const & m,
                         std::string const & results,
                         app_state & app);


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __CERT_HH__
