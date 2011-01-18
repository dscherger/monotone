// Copyright (C) 2010 Stephen Leake <stephen_leake@stephe-leake.org>
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

#include "cmd.hh"
#include "app_state.hh"
#include "database.hh"
#include "key_store.hh"
#include "key_packet.hh"
#include "packet.hh"
#include "project.hh"
#include "vocab_cast.hh"

using std::cin;
using std::cout;
using std::istringstream;
using std::vector;

namespace
{
  // this writer injects key packets it receives to the database and/or
  // keystore.

  struct key_packet_db_writer : public key_packet_consumer
  {
    database & db;
    key_store & keys;

  public:
    key_packet_db_writer(database & db, key_store & keys)
      : db(db), keys(keys)
    {}
    virtual ~key_packet_db_writer() {}
    virtual void consume_public_key(key_name const & ident,
                                    rsa_pub_key const & k)
    {
      transaction_guard guard(db);
      db.put_key(ident, k);
      guard.commit();
    }

    virtual void consume_key_pair(key_name const & ident,
                                  keypair const & kp)
    {
      keys.put_key_pair(ident, kp);
    }

    virtual void consume_old_private_key(key_name const & ident,
                                         old_arc4_rsa_priv_key const & k)
    {
      rsa_pub_key dummy;
      keys.migrate_old_key_pair(ident, k, dummy);
    }
  };
}

CMD_AUTOMATE(put_public_key, N_("KEY-PACKET-DATA"),
             N_("Store the public key in the database"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);
  key_store keys(app);
  key_packet_db_writer dbw(db, keys);

  istringstream ss(idx(args,0)());
  read_key_packets(ss, dbw);
}

static void
pubkey_common(app_state & app,
              args_vector args,
              std::ostream & output)
{
  database db(app, database::maybe_unspecified);
  key_store keys(app);
  project_t project(db);

  key_identity_info identity;
  project.get_key_identity(keys, app.lua,
                           typecast_vocab<external_key_name>(idx(args, 0)),
                           identity);
  bool exists(false);
  rsa_pub_key key;
  if (db.database_specified() && db.public_key_exists(identity.id))
    {
      db.get_key(identity.id, key);
      exists = true;
    }
  if (keys.key_pair_exists(identity.id))
    {
      keypair kp;
      keys.get_key_pair(identity.id, kp);
      key = kp.pub;
      exists = true;
    }
  E(exists, origin::user,
    F("public key '%s' does not exist") % idx(args, 0)());

  packet_writer pw(output);
  pw.consume_public_key(identity.given_name, key);
}

CMD(pubkey, "pubkey", "", CMD_REF(packet_io), N_("KEY_NAME_OR_HASH"),
    N_("Prints a public key packet"),
    "",
    options::opts::none)
{
  if (args.size() != 1)
    throw usage(execid);

  pubkey_common(app, args, cout);
}

CMD_AUTOMATE(get_public_key, N_("KEY_NAME_OR_HASH"),
    N_("Prints a public key packet"),
    "",
    options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  pubkey_common(app, args, output);
}

CMD(privkey, "privkey", "", CMD_REF(packet_io), N_("ID"),
    N_("Prints a private key packet"),
    "",
    options::opts::none)
{
  database db(app, database::maybe_unspecified);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 1)
    throw usage(execid);

  key_identity_info identity;
  project.get_key_identity(keys,
                           app.lua,
                           typecast_vocab<external_key_name>(idx(args, 0)),
                           identity);

  packet_writer pw(cout);
  keypair kp;
  key_name given_name;
  keys.get_key_pair(identity.id, given_name, kp);
  pw.consume_key_pair(given_name, kp);
}

namespace
{
  // this writer injects packets it receives to the database
  // and/or keystore.

  struct packet_db_writer : public packet_consumer
  {
    database & db;
    key_store & keys;

  public:
    packet_db_writer(database & db, key_store & keys)
      : db(db), keys(keys)
    {}
    virtual ~packet_db_writer() {}
    virtual void consume_file_data(file_id const & ident,
                                   file_data const & dat)
    {
      transaction_guard guard(db);
      db.put_file(ident, dat);
      guard.commit();
    }

    virtual void consume_file_delta(file_id const & old_id,
                                    file_id const & new_id,
                                    file_delta const & del)
    {
      transaction_guard guard(db);
      db.put_file_version(old_id, new_id, del);
      guard.commit();
    }

    virtual void consume_revision_data(revision_id const & ident,
                                       revision_data const & dat)
    {
      transaction_guard guard(db);
      db.put_revision(ident, dat);
      guard.commit();
    }

    virtual void consume_revision_cert(cert const & t)
    {
      transaction_guard guard(db);
      db.put_revision_cert(t);
      guard.commit();
    }

    virtual void consume_public_key(key_name const & ident,
                                    rsa_pub_key const & k)
    {
      transaction_guard guard(db);
      db.put_key(ident, k);
      guard.commit();
    }

    virtual void consume_key_pair(key_name const & ident,
                                  keypair const & kp)
    {
      keys.put_key_pair(ident, kp);
    }

    virtual void consume_old_private_key(key_name const & ident,
                                         old_arc4_rsa_priv_key const & k)
    {
      rsa_pub_key dummy;
      keys.migrate_old_key_pair(ident, k, dummy);
    }
  };
}

// Name : read_packets
// Arguments:
//   packet-data
// Added in: 9.0
// Purpose:
//   Store public keys (and incidentally anything else that can be
//   represented as a packet) into the database.
// Input format:
//   The format of the packet-data argument is identical to the output
//   of "mtn pubkey <keyname>" (or other packet output commands).
// Output format:
//   No output.
// Error conditions:
//   Invalid input formatting.
CMD_AUTOMATE(read_packets, N_("PACKET-DATA"),
             N_("Load the given packets into the database"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);
  key_store keys(app);
  packet_db_writer dbw(db, keys);

  istringstream ss(idx(args,0)());
  read_packets(ss, dbw);
}

CMD(read, "read", "", CMD_REF(packet_io), "[FILE1 [FILE2 [...]]]",
    N_("Reads packets from files"),
    N_("If no files are provided, the standard input is used."),
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  packet_db_writer dbw(db, keys);
  size_t count = 0;
  if (args.empty())
    {
      count += read_packets(cin, dbw);
      E(count != 0, origin::user, F("no packets found on stdin"));
    }
  else
    {
      for (args_vector::const_iterator i = args.begin();
           i != args.end(); ++i)
        {
          data dat;
          read_data(system_path(*i), dat);
          istringstream ss(dat());
          count += read_packets(ss, dbw);
        }
      E(count != 0, origin::user,
        FP("no packets found in given file",
           "no packets found in given files",
           args.size()));
    }
  P(FP("read %d packet", "read %d packets", count) % count);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
