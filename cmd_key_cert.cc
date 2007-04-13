// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>
#include <sstream>
#include <fstream>

#include "cert.hh"
#include "charset.hh"
#include "cmd.hh"
#include "app_state.hh"
#include "keys.hh"
#include "transforms.hh"
#include "ssh_agent.hh"
#include "botan/pipe.h"

using std::cout;
using std::ostream_iterator;
using std::ostringstream;
using std::set;
using std::string;
using std::ofstream;
using Botan::Pipe;
using Botan::RSA_PrivateKey;

CMD(genkey, "", N_("key and cert"), N_("KEYID"),
    N_("Generates an RSA key-pair"),
    N_(""),
    options::opts::none)
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

CMD(dropkey, "", N_("key and cert"), N_("KEYID"),
    N_("Drops a public and/or private key"),
    N_(""),
    options::opts::none)
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

CMD(passphrase, "", N_("key and cert"), N_("KEYID"),
    N_("Changes the passphrase of a private RSA key"),
    N_(""),
    options::opts::none)
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

CMD(ssh_agent_export, "", N_("key and cert"),
    N_("[FILENAME]"),
    N_("Exports a private key for use with ssh-agent"),
    N_(""),
    options::opts::none)
{
  if (args.size() > 1)
    throw usage(name);

  rsa_keypair_id id;
  keypair key;
  get_user_key(id, app);
  N(priv_key_exists(app, id), F("the key you specified cannot be found"));
  app.keys.get_key_pair(id, key);
  shared_ptr<RSA_PrivateKey> priv = get_private_key(app.lua, id, key.priv);
  utf8 new_phrase;
  get_passphrase(app.lua, id, new_phrase, true, true, "enter new passphrase");
  Pipe p;
  p.start_msg();
  if (new_phrase().length())
    {
      Botan::PKCS8::encrypt_key(*priv,
                                p,
                                new_phrase(),
                                "PBE-PKCS5v20(SHA-1,TripleDES/CBC)");
    }
  else
    {
      Botan::PKCS8::encode(*priv, p);
    }
  string decoded_key = p.read_all_as_string();
  if (args.size() == 0)
    cout << decoded_key;
  else
    {
      ofstream fout(idx(args,0)().c_str(), ofstream::out);
      fout << decoded_key;
    }
}

CMD(ssh_agent_add, "", N_("key and cert"), "",
    N_("Adds a private key to ssh-agent"),
    N_(""),
    options::opts::none)
{
  if (args.size() > 1)
    throw usage(name);

  rsa_keypair_id id;
  keypair key;
  get_user_key(id, app);
  N(priv_key_exists(app, id), F("the key you specified cannot be found"));
  app.keys.get_key_pair(id, key);
  shared_ptr<RSA_PrivateKey> priv = get_private_key(app.lua, id, key.priv);
  app.agent.add_identity(*priv, id());
}

CMD(cert, "", N_("key and cert"), N_("REVISION CERTNAME [CERTVAL]"),
    N_("Creates a certificate for a revision"),
    N_(""),
    options::opts::none)
{
  if ((args.size() != 3) && (args.size() != 2))
    throw usage(name);

  transaction_guard guard(app.db);

  revision_id rid;
  complete(app, idx(args, 0)(), rid);

  cert_name cname;
  internalize_cert_name(idx(args, 1), cname);

  rsa_keypair_id key;
  get_user_key(key, app);

  cert_value val;
  if (args.size() == 3)
    val = cert_value(idx(args, 2)());
  else
    {
      data dat;
      read_data_stdin(dat);
      val = cert_value(dat());
    }

  app.get_project().put_cert(rid, cname, val);
  guard.commit();
}

CMD(trusted, "", N_("key and cert"), 
    N_("REVISION NAME VALUE SIGNER1 [SIGNER2 [...]]"),
    N_("Tests whether a hypothetical certificate would be trusted"),
    N_("The current settings are used to run the test."),
    options::opts::none)
{
  if (args.size() < 4)
    throw usage(name);

  revision_id rid;
  complete(app, idx(args, 0)(), rid, false);
  hexenc<id> ident(rid.inner());

  cert_name cname;
  internalize_cert_name(idx(args, 1), cname);

  cert_value value(idx(args, 2)());

  set<rsa_keypair_id> signers;
  for (unsigned int i = 3; i != args.size(); ++i)
    {
      rsa_keypair_id keyid;
      internalize_rsa_keypair_id(idx(args, i), keyid);
      signers.insert(keyid);
    }


  bool trusted = app.lua.hook_get_revision_cert_trust(signers, ident,
                                                      cname, value);


  ostringstream all_signers;
  copy(signers.begin(), signers.end(),
       ostream_iterator<rsa_keypair_id>(all_signers, " "));

  cout << (F("if a cert on: %s\n"
            "with key: %s\n"
            "and value: %s\n"
            "was signed by: %s\n"
            "it would be: %s")
    % ident
    % cname
    % value
    % all_signers.str()
    % (trusted ? _("trusted") : _("UNtrusted")))
    << '\n'; // final newline is kept out of the translation
}

CMD(tag, "", N_("review"), N_("REVISION TAGNAME"),
    N_("Puts a symbolic tag certificate on a revision"),
    N_(""),
    options::opts::none)
{
  if (args.size() != 2)
    throw usage(name);

  revision_id r;
  complete(app, idx(args, 0)(), r);
  cert_revision_tag(r, idx(args, 1)(), app);
}


CMD(testresult, "", N_("review"), N_("ID (pass|fail|true|false|yes|no|1|0)"),
    N_("Notes the results of running a test on a revision"),
    N_(""),
    options::opts::none)
{
  if (args.size() != 2)
    throw usage(name);

  revision_id r;
  complete(app, idx(args, 0)(), r);
  cert_revision_testresult(r, idx(args, 1)(), app);
}


CMD(approve, "", N_("review"), N_("REVISION"),
    N_("Approves a particular revision"),
    N_(""),
    options::opts::branch)
{
  if (args.size() != 1)
    throw usage(name);

  revision_id r;
  complete(app, idx(args, 0)(), r);
  guess_branch(r, app);
  N(app.opts.branchname() != "", F("need --branch argument for approval"));
  app.get_project().put_revision_in_branch(r, app.opts.branchname);
}

CMD(comment, "", N_("review"), N_("REVISION [COMMENT]"),
    N_("Comments on a particular revision"),
    N_(""),
    options::opts::none)
{
  if (args.size() != 1 && args.size() != 2)
    throw usage(name);

  utf8 comment;
  if (args.size() == 2)
    comment = idx(args, 1);
  else
    {
      external comment_external;
      N(app.lua.hook_edit_comment(external(""), external(""), comment_external),
        F("edit comment failed"));
      system_to_utf8(comment_external, comment);
    }

  N(comment().find_first_not_of("\n\r\t ") != string::npos,
    F("empty comment"));

  revision_id r;
  complete(app, idx(args, 0)(), r);
  cert_revision_comment(r, comment, app);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
