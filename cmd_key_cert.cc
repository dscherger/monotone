// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iostream>
#include <sstream>
#include <iterator>

#include "basic_io.hh"
#include "charset.hh"
#include "cmd.hh"
#include "app_state.hh"
#include "database.hh"
#include "file_io.hh"
#include "project.hh"
#include "keys.hh"
#include "key_store.hh"
#include "maybe_workspace_updater.hh"
#include "transforms.hh"
#include "vocab_cast.hh"

using std::cout;
using std::ostream_iterator;
using std::ostringstream;
using std::set;
using std::string;

namespace
{
  namespace syms
  {
    symbol const name("name");
    symbol const hash("hash");
    symbol const public_location("public_location");
    symbol const private_location("private_location");
  }
};

CMD(genkey, "genkey", "", CMD_REF(key_and_cert), N_("KEY_NAME"),
    N_("Generates an RSA key-pair"),
    "",
    options::opts::force_duplicate_key)
{
  database db(app);
  key_store keys(app);

  if (args.size() != 1)
    throw usage(execid);

  key_name name = typecast_vocab<key_name>(idx(args, 0));

  if (!app.opts.force_duplicate_key)
    {
      E(!keys.key_pair_exists(name), origin::user,
        F("you already have a key named '%s'") % name);
      if (db.database_specified())
        {
          E(!db.public_key_exists(name), origin::user,
            F("there is another key named '%s'") % name);
        }
    }

  keys.create_key_pair(db, name);
}

CMD_AUTOMATE(generate_key, N_("KEY_NAME PASSPHRASE"),
             N_("Generates an RSA key-pair"),
             "",
             options::opts::force_duplicate_key)
{
  // not unified with CMD(genkey), because the call to create_key_pair is
  // significantly different.

  E(args.size() == 2, origin::user,
    F("wrong argument count"));

  database db(app);
  key_store keys(app);

  key_name name = typecast_vocab<key_name>(idx(args, 0));

  if (!app.opts.force_duplicate_key)
    {
      E(!keys.key_pair_exists(name), origin::user,
        F("you already have a key named '%s'") % name);
      if (db.database_specified())
        {
          E(!db.public_key_exists(name), origin::user,
            F("there is another key named '%s'") % name);
        }
    }

  utf8 passphrase = idx(args, 1);

  key_id hash;
  keys.create_key_pair(db, name, key_store::create_quiet, &passphrase, &hash);

  basic_io::printer prt;
  basic_io::stanza stz;
  vector<string> publocs, privlocs;
  if (db.database_specified())
    publocs.push_back("database");
  publocs.push_back("keystore");
  privlocs.push_back("keystore");

  stz.push_str_pair(syms::name, name());
  stz.push_binary_pair(syms::hash, hash.inner());
  stz.push_str_multi(syms::public_location, publocs);
  stz.push_str_multi(syms::private_location, privlocs);
  prt.print_stanza(stz);

  output.write(prt.buf.data(), prt.buf.size());

}

static void
dropkey_common(app_state & app,
               args_vector args,
               bool drop_private)
{
  database db(app);
  key_store keys(app);
  bool key_deleted = false;
  bool checked_db = false;

  key_identity_info identity;
  project_t project(db);
  project.get_key_identity(keys, app.lua,
                           typecast_vocab<external_key_name>(idx(args, 0)),
                           identity);

  if (db.database_specified())
    {
      transaction_guard guard(db);
      if (db.public_key_exists(identity.id))
        {
          P(F("dropping public key '%s' from database") % identity.id);
          db.delete_public_key(identity.id);
          key_deleted = true;
        }
      guard.commit();
      checked_db = true;
    }

  if (drop_private && keys.key_pair_exists(identity.id))
    {
      P(F("dropping key pair '%s' from keystore") % identity.id);
      keys.delete_key(identity.id);
      key_deleted = true;
    }

  i18n_format fmt;
  if (checked_db)
    fmt = F("public or private key '%s' does not exist "
            "in keystore or database");
  else
    fmt = F("public or private key '%s' does not exist "
            "in keystore, and no database was specified");
  E(key_deleted, origin::user, fmt % idx(args, 0)());
}

CMD(dropkey, "dropkey", "", CMD_REF(key_and_cert), N_("KEY_NAME_OR_HASH"),
    N_("Drops a public and/or private key"),
    "",
    options::opts::none)
{
  if (args.size() != 1)
    throw usage(execid);

  dropkey_common(app, args,
                 true); // drop_private
}

CMD_AUTOMATE(drop_public_key, N_("KEY_NAME_OR_HASH"),
    N_("Drops a public key"),
    "",
    options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  dropkey_common(app, args,
                 false); // drop_private
}

CMD(passphrase, "passphrase", "", CMD_REF(key_and_cert), N_("KEY_NAME_OR_HASH"),
    N_("Changes the passphrase of a private RSA key"),
    "",
    options::opts::none)
{
  if (args.size() != 1)
    throw usage(execid);

  key_store keys(app);
  database db(app);
  project_t project(db);
  key_identity_info identity;

  project.get_key_identity(keys, app.lua,
                           typecast_vocab<external_key_name>(idx(args, 0)),
                           identity);

  keys.change_key_passphrase(identity.id);
  P(F("passphrase changed"));
}

CMD(ssh_agent_export, "ssh_agent_export", "", CMD_REF(key_and_cert),
    N_("[FILENAME]"),
    N_("Exports a private key for use with ssh-agent"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() > 1)
    throw usage(execid);

  key_id id;
  get_user_key(app.opts, app.lua, db, keys, project, id);

  if (args.empty())
    keys.export_key_for_agent(id, cout);
  else
    {
      ostringstream fout;
      keys.export_key_for_agent(id, fout);
      data keydat(fout.str(), origin::system);

      system_path fname(idx(args, 0));
      write_data_userprivate(fname, keydat, fname.dirname());
    }
}

CMD(ssh_agent_add, "ssh_agent_add", "", CMD_REF(key_and_cert), "",
    N_("Adds a private key to ssh-agent"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() > 1)
    throw usage(execid);

  key_id id;
  get_user_key(app.opts, app.lua, db, keys, project, id);
  keys.add_key_to_agent(id);
}

CMD(cert, "cert", "", CMD_REF(key_and_cert),
    N_("SELECTOR CERTNAME [CERTVAL]"),
    N_("Creates a certificate for a revision or set of revisions"),
    N_("Creates a certificate with the given name and value on each revision "
       "that matches the given selector"),
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if ((args.size() != 3) && (args.size() != 2))
    throw usage(execid);

  transaction_guard guard(db);

  set<revision_id> revisions;
  complete(app.opts, app.lua,  project, idx(args, 0)(), revisions);

  cert_name cname = typecast_vocab<cert_name>(idx(args, 1));

  cache_user_key(app.opts, project, keys, app.lua);

  cert_value val;
  if (args.size() == 3)
    val = typecast_vocab<cert_value>(idx(args, 2));
  else
    {
      data dat;
      read_data_stdin(dat);
      val = typecast_vocab<cert_value>(dat);
    }
  for (set<revision_id>::const_iterator r = revisions.begin();
       r != revisions.end(); ++r)
    {
      project.put_cert(keys, *r, cname, val);
    }
  guard.commit();
}

CMD(trusted, "trusted", "", CMD_REF(key_and_cert),
    N_("REVISION NAME VALUE SIGNER1 [SIGNER2 [...]]"),
    N_("Tests whether a hypothetical certificate would be trusted"),
    N_("The current settings are used to run the test."),
    options::opts::none)
{
  key_store keys(app); // so the user can name keys that aren't in the db
  database db(app);
  project_t project(db);

  if (args.size() < 4)
    throw usage(execid);

  set<revision_id> rids;
  complete(app.opts, app.lua,  project, idx(args, 0)(), rids);

  revision_id ident;
  if (!rids.empty())
    ident = *rids.begin();

  cert_name cname = typecast_vocab<cert_name>(idx(args, 1));
  cert_value value = typecast_vocab<cert_value>(idx(args, 2));

  set<key_identity_info> signers;
  for (unsigned int i = 3; i != args.size(); ++i)
    {
      key_identity_info identity;
      project.get_key_identity(keys, app.lua,
                               typecast_vocab<external_key_name>(idx(args, i)),
                               identity);
      signers.insert(identity);
    }


  bool trusted = app.lua.hook_get_revision_cert_trust(signers,
                                                      ident.inner(),
                                                      cname, value);


  ostringstream all_signers;
  copy(signers.begin(), signers.end(),
       ostream_iterator<key_identity_info>(all_signers, " "));

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

CMD(tag, "tag", "", CMD_REF(review), N_("REVISION TAGNAME"),
    N_("Puts a symbolic tag certificate on a revision"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 2)
    throw usage(execid);

  revision_id r;
  complete(app.opts, app.lua, project, idx(args, 0)(), r);

  cache_user_key(app.opts, project, keys, app.lua);
  project.put_tag(keys, r, idx(args, 1)());
}


CMD(testresult, "testresult", "", CMD_REF(review),
    N_("ID (pass|fail|true|false|yes|no|1|0)"),
    N_("Notes the results of running a test on a revision"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 2)
    throw usage(execid);

  revision_id r;
  complete(app.opts, app.lua, project, idx(args, 0)(), r);

  cache_user_key(app.opts, project, keys, app.lua);
  project.put_revision_testresult(keys, r, idx(args, 1)());
}


CMD(approve, "approve", "", CMD_REF(review), N_("REVISION"),
    N_("Approves a particular revision"),
    "",
    options::opts::branch | options::opts::auto_update)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 1)
    throw usage(execid);

  maybe_workspace_updater updater(app, project);

  revision_id r;
  complete(app.opts, app.lua, project, idx(args, 0)(), r);
  guess_branch(app.opts, project, r);
  E(!app.opts.branch().empty(), origin::user,
    F("need --branch argument for approval"));

  cache_user_key(app.opts, project, keys, app.lua);
  project.put_revision_in_branch(keys, r, app.opts.branch);

  updater.maybe_do_update();
}

CMD(suspend, "suspend", "", CMD_REF(review), N_("REVISION"),
    N_("Suspends a particular revision"),
    "",
    options::opts::branch | options::opts::auto_update)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 1)
    throw usage(execid);

  maybe_workspace_updater updater(app, project);

  revision_id r;
  complete(app.opts, app.lua, project, idx(args, 0)(), r);
  guess_branch(app.opts, project, r);
  E(!app.opts.branch().empty(), origin::user,
    F("need --branch argument to suspend"));

  cache_user_key(app.opts, project, keys, app.lua);
  project.suspend_revision_in_branch(keys, r, app.opts.branch);

  updater.maybe_do_update();
}

CMD(comment, "comment", "", CMD_REF(review), N_("REVISION [COMMENT]"),
    N_("Comments on a particular revision"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 1 && args.size() != 2)
    throw usage(execid);

  utf8 comment;
  if (args.size() == 2)
    comment = idx(args, 1);
  else
    {
      external comment_external;
      E(app.lua.hook_edit_comment(external(""), comment_external),
        origin::user,
        F("edit comment failed"));
      system_to_utf8(comment_external, comment);
    }

  E(comment().find_first_not_of("\n\r\t ") != string::npos,
    origin::user,
    F("empty comment"));

  revision_id r;
  complete(app.opts, app.lua, project, idx(args, 0)(), r);

  cache_user_key(app.opts, project, keys, app.lua);
  project.put_revision_comment(keys, r, comment);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
