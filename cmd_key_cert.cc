// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <sstream>
#include <iostream>

#include "cert.hh"
#include "charset.hh"
#include "cmd.hh"
#include "keys.hh"
#include "packet.hh"
#include "transforms.hh"

using std::cout;
using std::ostream_iterator;
using std::ostringstream;
using std::set;
using std::string;

CMD(genkey, N_("key and cert"), N_("KEYID"), 
    N_("generate an RSA key-pair"), OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  rsa_keypair_id ident;
  internalize_rsa_keypair_id(idx(args, 0), ident);
  bool exists = app.keys.key_pair_exists(ident);
  if (app.db.database_specified())
    {
      transaction_guard guard(app.db);
      exists = exists || app.db.public_key_exists(ident);
      guard.commit();
    }

  N(!exists, F("key '%s' already exists") % ident);

  keypair kp;
  P(F("generating key-pair '%s'") % ident);
  generate_key_pair(app.lua, ident, kp);
  P(F("storing key-pair '%s' in %s/") 
    % ident % app.keys.get_key_dir());
  app.keys.put_key_pair(ident, kp);
}

CMD(dropkey, N_("key and cert"), N_("KEYID"), 
    N_("drop a public and private key"), OPT_NONE)
{
  bool key_deleted = false;

  if (args.size() != 1)
    throw usage(name);

  rsa_keypair_id ident(idx(args, 0)());
  bool checked_db = false;
  if (app.db.database_specified())
    {
      transaction_guard guard(app.db);
      if (app.db.public_key_exists(ident))
        {
          P(F("dropping public key '%s' from database") % ident);
          app.db.delete_public_key(ident);
          key_deleted = true;
        }
      guard.commit();
      checked_db = true;
    }

  if (app.keys.key_pair_exists(ident))
    {
      P(F("dropping key pair '%s' from keystore") % ident);
      app.keys.delete_key(ident);
      key_deleted = true;
    }

  i18n_format fmt;
  if (checked_db)
    fmt = F("public or private key '%s' does not exist "
	    "in keystore or database");
  else
    fmt = F("public or private key '%s' does not exist "
	    "in keystore, and no database was specified");
  N(key_deleted, fmt % idx(args, 0)());
}

CMD(chkeypass, N_("key and cert"), N_("KEYID"),
    N_("change passphrase of a private RSA key"),
    OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  rsa_keypair_id ident;
  internalize_rsa_keypair_id(idx(args, 0), ident);

  N(app.keys.key_pair_exists(ident),
    F("key '%s' does not exist in the keystore") % ident);

  keypair key;
  app.keys.get_key_pair(ident, key);
  change_key_passphrase(app.lua, ident, key.priv);
  app.keys.delete_key(ident);
  app.keys.put_key_pair(ident, key);
  P(F("passphrase changed"));
}

CMD(cert, N_("key and cert"), N_("REVISION CERTNAME [CERTVAL]"),
    N_("create a cert for a revision"), OPT_NONE)
{
  if ((args.size() != 3) && (args.size() != 2))
    throw usage(name);

  transaction_guard guard(app.db);

  hexenc<id> ident;
  revision_id rid;
  complete(app, idx(args, 0)(), rid);
  ident = rid.inner();

  cert_name name;
  internalize_cert_name(idx(args, 1), name);

  rsa_keypair_id key;
  get_user_key(key, app);

  cert_value val;
  if (args.size() == 3)
    val = cert_value(idx(args, 2)());
  else
    val = cert_value(get_stdin());

  base64<cert_value> val_encoded;
  encode_base64(val, val_encoded);

  cert t(ident, name, val_encoded, key);

  packet_db_writer dbw(app);
  calculate_cert(app, t);
  dbw.consume_revision_cert(revision<cert>(t));
  guard.commit();
}

CMD(trusted, N_("key and cert"), 
    N_("REVISION NAME VALUE SIGNER1 [SIGNER2 [...]]"),
    N_("test whether a hypothetical cert would be trusted\n"
       "by current settings"),
    OPT_NONE)
{
  if (args.size() < 4)
    throw usage(name);

  revision_id rid;
  complete(app, idx(args, 0)(), rid, false);
  hexenc<id> ident(rid.inner());

  cert_name name;
  internalize_cert_name(idx(args, 1), name);

  cert_value value(idx(args, 2)());

  set<rsa_keypair_id> signers;
  for (unsigned int i = 3; i != args.size(); ++i)
    {
      rsa_keypair_id keyid;
      internalize_rsa_keypair_id(idx(args, i), keyid);
      signers.insert(keyid);
    }


  bool trusted = app.lua.hook_get_revision_cert_trust(signers, ident,
                                                      name, value);


  ostringstream all_signers;
  copy(signers.begin(), signers.end(),
       ostream_iterator<rsa_keypair_id>(all_signers, " "));

  cout << F("if a cert on: %s\n"
            "with key: %s\n"
            "and value: %s\n"
            "was signed by: %s\n"
            "it would be: %s\n")
    % ident
    % name
    % value
    % all_signers.str()
    % (trusted ? _("trusted") : _("UNtrusted"));
}

CMD(tag, N_("review"), N_("REVISION TAGNAME"),
    N_("put a symbolic tag cert on a revision"), OPT_NONE)
{
  if (args.size() != 2)
    throw usage(name);

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_revision_tag(r, idx(args, 1)(), app, dbw);
}


CMD(testresult, N_("review"), N_("ID (pass|fail|true|false|yes|no|1|0)"),
    N_("note the results of running a test on a revision"), OPT_NONE)
{
  if (args.size() != 2)
    throw usage(name);

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_revision_testresult(r, idx(args, 1)(), app, dbw);
}


CMD(approve, N_("review"), N_("REVISION"),
    N_("approve of a particular revision"),
    OPT_BRANCH_NAME)
{
  if (args.size() != 1)
    throw usage(name);

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_value branchname;
  guess_branch(r, app, branchname);
  N(app.branch_name() != "", F("need --branch argument for approval"));
  cert_revision_in_branch(r, app.branch_name(), app, dbw);
}

CMD(comment, N_("review"), N_("REVISION [COMMENT]"),
    N_("comment on a particular revision"), OPT_NONE)
{
  if (args.size() != 1 && args.size() != 2)
    throw usage(name);

  string comment;
  if (args.size() == 2)
    comment = idx(args, 1)();
  else
    N(app.lua.hook_edit_comment("", "", comment),
      F("edit comment failed"));

  N(comment.find_first_not_of("\n\r\t ") != string::npos,
    F("empty comment"));

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_revision_comment(r, comment, app, dbw);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
