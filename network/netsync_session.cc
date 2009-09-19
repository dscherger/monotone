// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "network/netsync_session.hh"

#include <map>
#include <queue>

#include "cert.hh"
#include "database.hh"
#include "epoch.hh"
#include "key_store.hh"
#include "keys.hh"
#include "lua_hooks.hh"
#include "netio.hh"
#include "options.hh"
#include "project.hh"
#include "revision.hh"
#include "vocab_cast.hh"

using std::deque;
using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;



struct netsync_error
{
  string msg;
  netsync_error(string const & s): msg(s) {}
};

static inline void
require(bool check, string const & context)
{
  if (!check)
    throw bad_decode(F("check of '%s' failed") % context);
}

static void
read_pubkey(string const & in,
            key_name & id,
            rsa_pub_key & pub)
{
  string tmp_id, tmp_key;
  size_t pos = 0;
  extract_variable_length_string(in, tmp_id, pos, "pubkey id");
  extract_variable_length_string(in, tmp_key, pos, "pubkey value");
  id = key_name(tmp_id, origin::network);
  pub = rsa_pub_key(tmp_key, origin::network);
}

static void
write_pubkey(key_name const & id,
             rsa_pub_key const & pub,
             string & out)
{
  insert_variable_length_string(id(), out);
  insert_variable_length_string(pub(), out);
}


size_t netsync_session::session_count = 0;

netsync_session::netsync_session(options & opts,
                                 lua_hooks & lua,
                                 project_t & project,
                                 key_store & keys,
                                 protocol_role role,
                                 protocol_voice voice,
                                 globish const & our_include_pattern,
                                 globish const & our_exclude_pattern,
                                 string const & peer,
                                 shared_ptr<Netxx::StreamBase> sock,
                                 bool initiated_by_server) :
  session_base(peer, sock),
  version(opts.max_netsync_version),
  max_version(opts.max_netsync_version),
  min_version(opts.min_netsync_version),
  role(role),
  voice(voice),
  our_include_pattern(our_include_pattern),
  our_exclude_pattern(our_exclude_pattern),
  our_matcher(our_include_pattern, our_exclude_pattern),
  project(project),
  keys(keys),
  lua(lua),
  use_transport_auth(opts.use_transport_auth),
  signing_key(keys.signing_key),
  cmd_in(opts.max_netsync_version),
  armed(false),
  received_remote_key(false),
  session_key(constants::netsync_key_initializer),
  read_hmac(netsync_session_key(constants::netsync_key_initializer),
            use_transport_auth),
  write_hmac(netsync_session_key(constants::netsync_key_initializer),
             use_transport_auth),
  authenticated(false),
  byte_in_ticker(NULL),
  byte_out_ticker(NULL),
  cert_in_ticker(NULL),
  cert_out_ticker(NULL),
  revision_in_ticker(NULL),
  revision_out_ticker(NULL),
  bytes_in(0), bytes_out(0),
  certs_in(0), certs_out(0),
  revs_in(0), revs_out(0),
  keys_in(0), keys_out(0),
  session_id(++session_count),
  saved_nonce(""),
  error_code(no_transfer),
  set_totals(false),
  epoch_refiner(epoch_item, voice, *this),
  key_refiner(key_item, voice, *this),
  cert_refiner(cert_item, voice, *this),
  rev_refiner(revision_item, voice, *this),
  rev_enumerator(project, *this),
  initiated_by_server(initiated_by_server)
{
  for (vector<external_key_name>::const_iterator i = opts.keys_to_push.begin();
       i != opts.keys_to_push.end(); ++i)
    {
      key_identity_info ident;
      project.get_key_identity(keys, lua, *i, ident);
      keys_to_push.push_back(ident.id);
    }
}

netsync_session::~netsync_session()
{
  if (protocol_state == confirmed_state)
    error_code = no_error;
  else if (error_code == no_transfer &&
           (revs_in || revs_out ||
            certs_in || certs_out ||
            keys_in || keys_out))
    error_code = partial_transfer;

  vector<cert> unattached_written_certs;
  map<revision_id, vector<cert> > rev_written_certs;
  for (vector<revision_id>::iterator i = written_revisions.begin();
       i != written_revisions.end(); ++i)
    rev_written_certs.insert(make_pair(*i, vector<cert>()));
  for (vector<cert>::iterator i = written_certs.begin();
       i != written_certs.end(); ++i)
    {
      map<revision_id, vector<cert> >::iterator j;
      j = rev_written_certs.find(revision_id(i->ident));
      if (j == rev_written_certs.end())
        unattached_written_certs.push_back(*i);
      else
        j->second.push_back(*i);
    }

  if (!written_keys.empty()
      || !written_revisions.empty()
      || !written_certs.empty())
    {

      //Keys
      for (vector<key_id>::iterator i = written_keys.begin();
           i != written_keys.end(); ++i)
        {
          key_identity_info identity;
          identity.id = *i;
          project.complete_key_identity(keys, lua, identity);
          lua.hook_note_netsync_pubkey_received(identity, session_id);
        }

      //Revisions
      for (vector<revision_id>::iterator i = written_revisions.begin();
           i != written_revisions.end(); ++i)
        {
          vector<cert> & ctmp(rev_written_certs[*i]);
          set<pair<key_identity_info, pair<cert_name, cert_value> > > certs;
          for (vector<cert>::const_iterator j = ctmp.begin();
               j != ctmp.end(); ++j)
            {
              key_identity_info identity;
              identity.id = j->key;
              project.complete_key_identity(keys, lua, identity);
              certs.insert(make_pair(identity, make_pair(j->name, j->value)));
            }

          revision_data rdat;
          project.db.get_revision(*i, rdat);
          lua.hook_note_netsync_revision_received(*i, rdat, certs,
                                                  session_id);
        }

      //Certs (not attached to a new revision)
      for (vector<cert>::iterator i = unattached_written_certs.begin();
           i != unattached_written_certs.end(); ++i)
        {
          key_identity_info identity;
          identity.id = i->key;
          project.complete_key_identity(keys, lua, identity);
          lua.hook_note_netsync_cert_received(revision_id(i->ident), identity,
                                              i->name, i->value, session_id);
        }
    }

  if (!sent_keys.empty()
      || !sent_revisions.empty()
      || !sent_certs.empty())
    {

      vector<cert> unattached_sent_certs;
      map<revision_id, vector<cert> > rev_sent_certs;
      for (vector<revision_id>::iterator i = sent_revisions.begin();
           i != sent_revisions.end(); ++i)
        rev_sent_certs.insert(make_pair(*i, vector<cert>()));
      for (vector<cert>::iterator i = sent_certs.begin();
           i != sent_certs.end(); ++i)
        {
          map<revision_id, vector<cert> >::iterator j;
          j = rev_sent_certs.find(revision_id(i->ident));
          if (j == rev_sent_certs.end())
            unattached_sent_certs.push_back(*i);
          else
            j->second.push_back(*i);
        }

      //Keys
      for (vector<key_id>::iterator i = sent_keys.begin();
           i != sent_keys.end(); ++i)
        {
          key_identity_info identity;
          identity.id = *i;
          project.complete_key_identity(keys, lua, identity);
          lua.hook_note_netsync_pubkey_sent(identity, session_id);
        }

      //Revisions
      for (vector<revision_id>::iterator i = sent_revisions.begin();
           i != sent_revisions.end(); ++i)
        {
          vector<cert> & ctmp(rev_sent_certs[*i]);
          set<pair<key_identity_info, pair<cert_name, cert_value> > > certs;
          for (vector<cert>::const_iterator j = ctmp.begin();
               j != ctmp.end(); ++j)
            {
              key_identity_info identity;
              identity.id = j->key;
              project.complete_key_identity(keys, lua, identity);
              certs.insert(make_pair(identity, make_pair(j->name, j->value)));
            }

          revision_data rdat;
          project.db.get_revision(*i, rdat);
          lua.hook_note_netsync_revision_sent(*i, rdat, certs,
                                                  session_id);
        }

      //Certs (not attached to a new revision)
      for (vector<cert>::iterator i = unattached_sent_certs.begin();
           i != unattached_sent_certs.end(); ++i)
        {
          key_identity_info identity;
          identity.id = i->key;
          project.complete_key_identity(keys, lua, identity);
          lua.hook_note_netsync_cert_sent(revision_id(i->ident), identity,
                                          i->name, i->value, session_id);
        }
    }

  lua.hook_note_netsync_end(session_id, error_code,
                            bytes_in, bytes_out,
                            certs_in, certs_out,
                            revs_in, revs_out,
                            keys_in, keys_out);
}

bool
netsync_session::process_this_rev(revision_id const & rev)
{
  return (rev_refiner.items_to_send.find(rev.inner())
          != rev_refiner.items_to_send.end());
}

bool
netsync_session::queue_this_cert(id const & c)
{
  return (cert_refiner.items_to_send.find(c)
          != cert_refiner.items_to_send.end());
}

bool
netsync_session::queue_this_file(id const & f)
{
  return file_items_sent.find(file_id(f)) == file_items_sent.end();
}

void
netsync_session::note_file_data(file_id const & f)
{
  if (role == sink_role)
    return;
  file_data fd;
  project.db.get_file_version(f, fd);
  queue_data_cmd(file_item, f.inner(), fd.inner()());
  file_items_sent.insert(f);
}

void
netsync_session::note_file_delta(file_id const & src, file_id const & dst)
{
  if (role == sink_role)
    return;
  file_delta fdel;
  project.db.get_arbitrary_file_delta(src, dst, fdel);
  queue_delta_cmd(file_item, src.inner(), dst.inner(), fdel.inner());
  file_items_sent.insert(dst);
}

void
netsync_session::note_rev(revision_id const & rev)
{
  if (role == sink_role)
    return;
  revision_t rs;
  project.db.get_revision(rev, rs);
  data tmp;
  write_revision(rs, tmp);
  queue_data_cmd(revision_item, rev.inner(), tmp());
  sent_revisions.push_back(rev);
}

void
netsync_session::note_cert(id const & i)
{
  if (role == sink_role)
    return;
  cert c;
  string str;
  project.db.get_revision_cert(i, c);
  key_name keyname;
  rsa_pub_key junk;
  project.db.get_pubkey(c.key, keyname, junk);
  if (version >= 7)
    {
      c.marshal_for_netio(keyname, str);
    }
  else
    {
      c.marshal_for_netio_v6(keyname, str);
    }
  queue_data_cmd(cert_item, i, str);
  sent_certs.push_back(c);
}


id
netsync_session::mk_nonce()
{
  I(this->saved_nonce().empty());
  char buf[constants::merkle_hash_length_in_bytes];

#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
  keys.get_rng().randomize(reinterpret_cast<Botan::byte *>(buf),
                           constants::merkle_hash_length_in_bytes);
#else
  Botan::Global_RNG::randomize(reinterpret_cast<Botan::byte *>(buf),
                               constants::merkle_hash_length_in_bytes);
#endif
  this->saved_nonce = id(string(buf, buf + constants::merkle_hash_length_in_bytes),
                         origin::internal);
  I(this->saved_nonce().size() == constants::merkle_hash_length_in_bytes);
  return this->saved_nonce;
}

void
netsync_session::set_session_key(string const & key)
{
  session_key = netsync_session_key(key, origin::internal);
  read_hmac.set_key(session_key);
  write_hmac.set_key(session_key);
}

void
netsync_session::set_session_key(rsa_oaep_sha_data const & hmac_key_encrypted)
{
  if (use_transport_auth)
    {
      string hmac_key;
      keys.decrypt_rsa(signing_key, hmac_key_encrypted, hmac_key);
      set_session_key(hmac_key);
    }
}

void
netsync_session::setup_client_tickers()
{
  // xgettext: please use short message and try to avoid multibytes chars
  byte_in_ticker.reset(new ticker(N_("bytes in"), ">", 1024, true));
  // xgettext: please use short message and try to avoid multibytes chars
  byte_out_ticker.reset(new ticker(N_("bytes out"), "<", 1024, true));
  if (role == sink_role)
    {
      // xgettext: please use short message and try to avoid multibytes chars
      cert_in_ticker.reset(new ticker(N_("certs in"), "c", 3));
      // xgettext: please use short message and try to avoid multibytes chars
      revision_in_ticker.reset(new ticker(N_("revs in"), "r", 1));
    }
  else if (role == source_role)
    {
      // xgettext: please use short message and try to avoid multibytes chars
      cert_out_ticker.reset(new ticker(N_("certs out"), "C", 3));
      // xgettext: please use short message and try to avoid multibytes chars
      revision_out_ticker.reset(new ticker(N_("revs out"), "R", 1));
    }
  else
    {
      I(role == source_and_sink_role);
      // xgettext: please use short message and try to avoid multibytes chars
      revision_in_ticker.reset(new ticker(N_("revs in"), "r", 1));
      // xgettext: please use short message and try to avoid multibytes chars
      revision_out_ticker.reset(new ticker(N_("revs out"), "R", 1));
    }
}

bool
netsync_session::done_all_refinements()
{
  bool all = rev_refiner.done
    && cert_refiner.done
    && key_refiner.done
    && epoch_refiner.done;

  if (all && !set_totals)
    {
      L(FL("All refinements done for peer %s") % peer_id);
      if (cert_out_ticker.get())
        cert_out_ticker->set_total(cert_refiner.items_to_send.size());

      if (revision_out_ticker.get())
        revision_out_ticker->set_total(rev_refiner.items_to_send.size());

      if (cert_in_ticker.get())
        cert_in_ticker->set_total(cert_refiner.items_to_receive);

      if (revision_in_ticker.get())
        revision_in_ticker->set_total(rev_refiner.items_to_receive);

      set_totals = true;
    }
  return all;
}



bool
netsync_session::received_all_items()
{
  if (role == source_role)
    return true;
  bool all = rev_refiner.items_to_receive == 0
    && cert_refiner.items_to_receive == 0
    && key_refiner.items_to_receive == 0
    && epoch_refiner.items_to_receive == 0;
  return all;
}

bool
netsync_session::finished_working()
{
  bool all = done_all_refinements()
    && received_all_items()
    && queued_all_items()
    && rev_enumerator.done();
  return all;
}

bool
netsync_session::queued_all_items()
{
  if (role == sink_role)
    return true;
  bool all = rev_refiner.items_to_send.empty()
    && cert_refiner.items_to_send.empty()
    && key_refiner.items_to_send.empty()
    && epoch_refiner.items_to_send.empty();
  return all;
}


void
netsync_session::maybe_note_epochs_finished()
{
  // Maybe there are outstanding epoch requests.
  // These only matter if we're in sink or source-and-sink mode.
  if (!(epoch_refiner.items_to_receive == 0) && !(role == source_role))
    return;

  // And maybe we haven't even finished the refinement.
  if (!epoch_refiner.done)
    return;

  // If we ran into an error -- say a mismatched epoch -- don't do any
  // further refinements.
  if (encountered_error)
    return;

  // But otherwise, we're ready to go. Start the next
  // set of refinements.
  if (voice == client_voice)
    {
      L(FL("epoch refinement finished; beginning other refinements"));
      key_refiner.begin_refinement();
      cert_refiner.begin_refinement();
      rev_refiner.begin_refinement();
    }
  else
    L(FL("epoch refinement finished"));
}

static void
decrement_if_nonzero(netcmd_item_type ty,
                     size_t & n)
{
  if (n == 0)
    {
      string typestr;
      netcmd_item_type_to_string(ty, typestr);
      E(false, origin::network,
        F("underflow on count of %s items to receive") % typestr);
    }
  --n;
  if (n == 0)
    {
      string typestr;
      netcmd_item_type_to_string(ty, typestr);
      L(FL("count of %s items to receive has reached zero") % typestr);
    }
}

void
netsync_session::note_item_arrived(netcmd_item_type ty, id const & ident)
{
  switch (ty)
    {
    case cert_item:
      decrement_if_nonzero(ty, cert_refiner.items_to_receive);
      if (cert_in_ticker.get() != NULL)
        ++(*cert_in_ticker);
      ++certs_in;
      break;
    case revision_item:
      decrement_if_nonzero(ty, rev_refiner.items_to_receive);
      if (revision_in_ticker.get() != NULL)
        ++(*revision_in_ticker);
      ++revs_in;
      break;
    case key_item:
      decrement_if_nonzero(ty, key_refiner.items_to_receive);
      ++keys_in;
      break;
    case epoch_item:
      decrement_if_nonzero(ty, epoch_refiner.items_to_receive);
      break;
    default:
      // No ticker for other things.
      break;
    }
}



void
netsync_session::note_item_sent(netcmd_item_type ty, id const & ident)
{
  switch (ty)
    {
    case cert_item:
      cert_refiner.items_to_send.erase(ident);
      if (cert_out_ticker.get() != NULL)
        ++(*cert_out_ticker);
      ++certs_out;
      break;
    case revision_item:
      rev_refiner.items_to_send.erase(ident);
      if (revision_out_ticker.get() != NULL)
        ++(*revision_out_ticker);
      ++revs_out;
      break;
    case key_item:
      key_refiner.items_to_send.erase(ident);
      ++keys_out;
      break;
    case epoch_item:
      epoch_refiner.items_to_send.erase(ident);
      break;
    default:
      // No ticker for other things.
      break;
    }
}

void
netsync_session::write_netcmd_and_try_flush(netcmd const & cmd)
{
  if (!encountered_error)
  {
    string buf;
    cmd.write(buf, write_hmac);
    queue_output(buf);
  }
  else
    L(FL("dropping outgoing netcmd (because we're in error unwind mode)"));
}

// This method triggers a special "error unwind" mode to netsync.  In this
// mode, all received data is ignored, and no new data is queued.  We simply
// stay connected long enough for the current write buffer to be flushed, to
// ensure that our peer receives the error message.
// Affects read_some, write_some, and process .
void
netsync_session::error(int errcode, string const & errmsg)
{
  error_code = errcode;
  throw netsync_error(errmsg);
}

bool
netsync_session::do_work(transaction_guard & guard)
{
  if (process(guard))
    {
      maybe_step();
      maybe_say_goodbye(guard);
      return true;
    }
  else
    return false;
}

void
netsync_session::note_bytes_in(int count)
{
  if (byte_in_ticker.get() != NULL)
    (*byte_in_ticker) += count;
  bytes_in += count;
}

void
netsync_session::note_bytes_out(int count)
{
  if (byte_out_ticker.get() != NULL)
    (*byte_out_ticker) += count;
  bytes_out += count;
}

// senders

void
netsync_session::queue_error_cmd(string const & errmsg)
{
  L(FL("queueing 'error' command"));
  netcmd cmd(version);
  cmd.write_error_cmd(errmsg);
  write_netcmd_and_try_flush(cmd);
}

void
netsync_session::queue_bye_cmd(u8 phase)
{
  L(FL("queueing 'bye' command, phase %d")
    % static_cast<size_t>(phase));
  netcmd cmd(version);
  cmd.write_bye_cmd(phase);
  write_netcmd_and_try_flush(cmd);
}

void
netsync_session::queue_done_cmd(netcmd_item_type type,
                        size_t n_items)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(FL("queueing 'done' command for %s (%d items)")
    % typestr % n_items);
  netcmd cmd(version);
  cmd.write_done_cmd(type, n_items);
  write_netcmd_and_try_flush(cmd);
}

void
netsync_session::queue_hello_cmd(key_name const & key_name,
                                 rsa_pub_key const & pub,
                                 id const & nonce)
{
  netcmd cmd(version);
  if (use_transport_auth)
    cmd.write_hello_cmd(key_name, pub, nonce);
  else
    cmd.write_hello_cmd(key_name, rsa_pub_key(), nonce);
  write_netcmd_and_try_flush(cmd);
}

void
netsync_session::queue_anonymous_cmd(protocol_role role,
                                     globish const & include_pattern,
                                     globish const & exclude_pattern,
                                     id const & nonce2)
{
  netcmd cmd(version);
  rsa_oaep_sha_data hmac_key_encrypted;
  if (use_transport_auth)
    project.db.encrypt_rsa(remote_peer_key_id, nonce2(), hmac_key_encrypted);
  cmd.write_anonymous_cmd(role, include_pattern, exclude_pattern,
                          hmac_key_encrypted);
  write_netcmd_and_try_flush(cmd);
  set_session_key(nonce2());
}

void
netsync_session::queue_auth_cmd(protocol_role role,
                                globish const & include_pattern,
                                globish const & exclude_pattern,
                                key_id const & client,
                                id const & nonce1,
                                id const & nonce2,
                                rsa_sha1_signature const & signature)
{
  netcmd cmd(version);
  rsa_oaep_sha_data hmac_key_encrypted;
  I(use_transport_auth);
  project.db.encrypt_rsa(remote_peer_key_id, nonce2(), hmac_key_encrypted);
  cmd.write_auth_cmd(role, include_pattern, exclude_pattern, client,
                     nonce1, hmac_key_encrypted, signature);
  write_netcmd_and_try_flush(cmd);
  set_session_key(nonce2());
}

void
netsync_session::queue_confirm_cmd()
{
  netcmd cmd(version);
  cmd.write_confirm_cmd();
  write_netcmd_and_try_flush(cmd);
}

void
netsync_session::queue_refine_cmd(refinement_type ty, merkle_node const & node)
{
  string typestr;
  hexenc<prefix> hpref;
  node.get_hex_prefix(hpref);
  netcmd_item_type_to_string(node.type, typestr);
  L(FL("queueing refinement %s of %s node '%s', level %d")
    % (ty == refinement_query ? "query" : "response")
    % typestr % hpref() % static_cast<int>(node.level));
  netcmd cmd(version);
  cmd.write_refine_cmd(ty, node);
  write_netcmd_and_try_flush(cmd);
}

void
netsync_session::queue_data_cmd(netcmd_item_type type,
                                id const & item,
                                string const & dat)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hid;

  if (global_sanity.debug_p())
    encode_hexenc(item, hid);

  if (role == sink_role)
    {
      L(FL("not queueing %s data for '%s' as we are in pure sink role")
        % typestr % hid());
      return;
    }

  L(FL("queueing %d bytes of data for %s item '%s'")
    % dat.size() % typestr % hid());

  netcmd cmd(version);
  // TODO: This pair of functions will make two copies of a large
  // file, the first in cmd.write_data_cmd, and the second in
  // write_netcmd_and_try_flush when the data is copied from the
  // cmd.payload variable to the string buffer for output.  This
  // double copy should be collapsed out, it may be better to use
  // a string_queue for output as well as input, as that will reduce
  // the amount of mallocs that happen when the string queue is large
  // enough to just store the data.
  cmd.write_data_cmd(type, item, dat);
  write_netcmd_and_try_flush(cmd);
  note_item_sent(type, item);
}

void
netsync_session::queue_delta_cmd(netcmd_item_type type,
                                 id const & base,
                                 id const & ident,
                                 delta const & del)
{
  I(type == file_item);
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> base_hid,
             ident_hid;

  if (global_sanity.debug_p())
    {
      encode_hexenc(base, base_hid);
      encode_hexenc(ident, ident_hid);
    }

  if (role == sink_role)
    {
      L(FL("not queueing %s delta '%s' -> '%s' as we are in pure sink role")
        % typestr % base_hid() % ident_hid());
      return;
    }

  L(FL("queueing %s delta '%s' -> '%s'")
    % typestr % base_hid() % ident_hid());
  netcmd cmd(version);
  cmd.write_delta_cmd(type, base, ident, del);
  write_netcmd_and_try_flush(cmd);
  note_item_sent(type, ident);
}


// processors

bool
netsync_session::process_error_cmd(string const & errmsg)
{
  // "xxx string" with xxx being digits means there's an error code
  if (errmsg.size() > 4 && errmsg.substr(3,1) == " ")
    {
      try
        {
          int err = boost::lexical_cast<int>(errmsg.substr(0,3));
          if (err >= 100)
            {
              error_code = err;
              throw bad_decode(F("received network error: %s")
                               % errmsg.substr(4));
            }
        }
      catch (boost::bad_lexical_cast)
        { // ok, so it wasn't a number
        }
    }
  throw bad_decode(F("received network error: %s") % errmsg);
}

static const var_domain known_servers_domain = var_domain("known-servers");

bool
netsync_session::process_hello_cmd(u8 server_version,
                                   key_name const & their_keyname,
                                   rsa_pub_key const & their_key,
                                   id const & nonce)
{
  I(!this->received_remote_key);
  I(this->saved_nonce().empty());

  // version sanity has already been checked by netcmd::read()
  L(FL("received hello command; setting version from %d to %d")
    % widen<u32>(version)
    % widen<u32>(server_version));
  version = server_version;

  key_identity_info their_identity;
  if (use_transport_auth)
    {
      key_hash_code(their_keyname, their_key, their_identity.id);
      var_value printable_key_hash;
      {
        hexenc<id> encoded_key_hash;
        encode_hexenc(their_identity.id.inner(), encoded_key_hash);
        printable_key_hash = typecast_vocab<var_value>(encoded_key_hash);
      }
      L(FL("server key has name %s, hash %s")
        % their_keyname % printable_key_hash);
      var_key their_key_key(known_servers_domain,
                            var_name(peer_id, origin::internal));
      if (project.db.var_exists(their_key_key))
        {
          var_value expected_key_hash;
          project.db.get_var(their_key_key, expected_key_hash);
          if (expected_key_hash != printable_key_hash)
            {
              P(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
                  "@ WARNING: SERVER IDENTIFICATION HAS CHANGED              @\n"
                  "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
                  "IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY\n"
                  "it is also possible that the server key has just been changed\n"
                  "remote host sent key %s\n"
                  "I expected %s\n"
                  "'%s unset %s %s' overrides this check")
                % printable_key_hash
                % expected_key_hash
                % prog_name % their_key_key.first % their_key_key.second);
              E(false, origin::network, F("server key changed"));
            }
        }
      else
        {
          P(F("first time connecting to server %s\n"
              "I'll assume it's really them, but you might want to double-check\n"
              "their key's fingerprint: %s")
            % peer_id
            % printable_key_hash);
          project.db.set_var(their_key_key, printable_key_hash);
        }

      if (!project.db.public_key_exists(their_identity.id))
        {
          // this should now always return true since we just checked
          // for the existence of this particular key
          I(project.db.put_key(their_keyname, their_key));
          W(F("saving public key for %s to database") % their_keyname);
        }

      {
        hexenc<id> hnonce;
        encode_hexenc(nonce, hnonce);
        L(FL("received 'hello' netcmd from server '%s' with nonce '%s'")
          % printable_key_hash % hnonce);
      }

      I(project.db.public_key_exists(their_identity.id));
      project.complete_key_identity(keys, lua, their_identity);

      // save their identity
      this->received_remote_key = true;
      this->remote_peer_key_id = their_identity.id;
    }

  // clients always include in the synchronization set, every branch that the
  // user requested
  set<branch_name> all_branches, ok_branches;
  project.get_branch_list(all_branches);
  for (set<branch_name>::const_iterator i = all_branches.begin();
      i != all_branches.end(); i++)
    {
      if (our_matcher((*i)()))
        ok_branches.insert(*i);
    }
  rebuild_merkle_trees(ok_branches);

  if (!initiated_by_server)
    setup_client_tickers();

  if (use_transport_auth && signing_key.inner()() != "")
    {
      // get our key pair
      load_key_pair(keys, signing_key);

      // make a signature with it;
      // this also ensures our public key is in the database
      rsa_sha1_signature sig;
      keys.make_signature(project.db, signing_key, nonce(), sig);

      // make a new nonce of our own and send off the 'auth'
      queue_auth_cmd(this->role, our_include_pattern, our_exclude_pattern,
                     signing_key, nonce, mk_nonce(), sig);
    }
  else
    {
      queue_anonymous_cmd(this->role, our_include_pattern,
                          our_exclude_pattern, mk_nonce());
    }

  lua.hook_note_netsync_start(session_id, "client", this->role,
                              peer_id, their_identity,
                              our_include_pattern, our_exclude_pattern);
  return true;
}

bool
netsync_session::process_anonymous_cmd(protocol_role their_role,
                                       globish const & their_include_pattern,
                                       globish const & their_exclude_pattern)
{
  // Internally netsync thinks in terms of sources and sinks. Users like
  // thinking of repositories as "readonly", "readwrite", or "writeonly".
  //
  // We therefore use the read/write terminology when dealing with the UI:
  // if the user asks to run a "read only" service, this means they are
  // willing to be a source but not a sink.
  //
  // nb: The "role" here is the role the *client* wants to play
  //     so we need to check that the opposite role is allowed for us,
  //     in our this->role field.
  //

  lua.hook_note_netsync_start(session_id, "server", their_role,
                              peer_id, key_identity_info(),
                              their_include_pattern, their_exclude_pattern);

  // Client must be a sink and server must be a source (anonymous
  // read-only), unless transport auth is disabled.
  //
  // If running in no-transport-auth mode, we operate anonymously and
  // permit adoption of any role.

  if (use_transport_auth)
    {
      if (their_role != sink_role)
        {
          this->saved_nonce = id("");
          error(not_permitted,
                F("rejected attempt at anonymous connection for write").str());
        }

      if (this->role == sink_role)
        {
          this->saved_nonce = id("");
          error(role_mismatch,
                F("rejected attempt at anonymous connection while running as sink").str());
        }
    }

  set<branch_name> all_branches, ok_branches;
  project.get_branch_list(all_branches);
  globish_matcher their_matcher(their_include_pattern, their_exclude_pattern);
  for (set<branch_name>::const_iterator i = all_branches.begin();
      i != all_branches.end(); i++)
    {
      if (their_matcher((*i)()))
        {
          if (use_transport_auth &&
              !lua.hook_get_netsync_read_permitted((*i)()))
            {
              error(not_permitted,
                    (F("anonymous access to branch '%s' denied by server")
                     % *i).str());
            }
          else
            ok_branches.insert(*i);
        }
    }

  if (use_transport_auth)
    {
      P(F("allowed anonymous read permission for '%s' excluding '%s'")
        % their_include_pattern % their_exclude_pattern);
      this->role = source_role;
    }
  else
    {
      P(F("allowed anonymous read/write permission for '%s' excluding '%s'")
        % their_include_pattern % their_exclude_pattern);
      assume_corresponding_role(their_role);
    }

  rebuild_merkle_trees(ok_branches);

  this->remote_peer_key_id = key_id();
  this->authenticated = true;
  return true;
}

void
netsync_session::assume_corresponding_role(protocol_role their_role)
{
  // Assume the (possibly degraded) opposite role.
  switch (their_role)
    {
    case source_role:
      I(this->role != source_role);
      this->role = sink_role;
      break;

    case source_and_sink_role:
      I(this->role == source_and_sink_role);
      break;

    case sink_role:
      I(this->role != sink_role);
      this->role = source_role;
      break;
    }
}

bool
netsync_session::process_auth_cmd(protocol_role their_role,
                                  globish const & their_include_pattern,
                                  globish const & their_exclude_pattern,
                                  key_id const & client,
                                  id const & nonce1,
                                  rsa_sha1_signature const & signature)
{
  I(!this->received_remote_key);
  I(this->saved_nonce().size() == constants::merkle_hash_length_in_bytes);

  globish_matcher their_matcher(their_include_pattern, their_exclude_pattern);

  if (!project.db.public_key_exists(client))
    {
      // If it's not in the db, it still could be in the keystore if we
      // have the private key that goes with it.
      key_name their_key_id;
      keypair their_keypair;
      if (keys.maybe_get_key_pair(client, their_key_id, their_keypair))
        project.db.put_key(their_key_id, their_keypair.pub);
      else
        {
          return process_anonymous_cmd(their_role,
                                       their_include_pattern,
                                       their_exclude_pattern);
          /*
          this->saved_nonce = id("");

          lua.hook_note_netsync_start(session_id, "server", their_role,
                                      peer_id, key_name("-unknown-"),
                                      their_include_pattern,
                                      their_exclude_pattern);
          error(unknown_key,
                (F("remote public key hash '%s' is unknown")
                 % client).str());
          */
        }
    }

  // Get their public key.
  key_name their_id;
  rsa_pub_key their_key;
  project.db.get_pubkey(client, their_id, their_key);
  key_identity_info client_identity;
  client_identity.id = client;
  project.complete_key_identity(keys, lua, client_identity);

  lua.hook_note_netsync_start(session_id, "server", their_role,
                              peer_id, client_identity,
                              their_include_pattern, their_exclude_pattern);

  // Check that they replied with the nonce we asked for.
  if (!(nonce1 == this->saved_nonce))
    {
      this->saved_nonce = id("");
      error(failed_identification,
            F("detected replay attack in auth netcmd").str());
    }

  // Internally netsync thinks in terms of sources and sinks. users like
  // thinking of repositories as "readonly", "readwrite", or "writeonly".
  //
  // We therefore use the read/write terminology when dealing with the UI:
  // if the user asks to run a "read only" service, this means they are
  // willing to be a source but not a sink.
  //
  // nb: The "their_role" here is the role the *client* wants to play
  //     so we need to check that the opposite role is allowed for us,
  //     in our this->role field.

  // Client as sink, server as source (reading).

  if (their_role == sink_role || their_role == source_and_sink_role)
    {
      if (this->role != source_role && this->role != source_and_sink_role)
        {
          this->saved_nonce = id("");
          error(not_permitted,
                (F("denied '%s' read permission for '%s' excluding '%s' while running as pure sink")
                 % their_id % their_include_pattern % their_exclude_pattern).str());
        }
    }

  set<branch_name> all_branches, ok_branches;
  project.get_branch_list(all_branches);
  for (set<branch_name>::const_iterator i = all_branches.begin();
       i != all_branches.end(); i++)
    {
      if (their_matcher((*i)()))
        {
          if (!lua.hook_get_netsync_read_permitted((*i)(), client_identity))
            {
              error(not_permitted,
                    (F("denied '%s' read permission for '%s' excluding '%s' because of branch '%s'")
                     % their_id % their_include_pattern % their_exclude_pattern % *i).str());
            }
          else
            ok_branches.insert(*i);
        }
    }

  // If we're source_and_sink_role, continue even with no branches readable
  // eg. serve --db=empty.db
  P(F("allowed '%s' read permission for '%s' excluding '%s'")
    % their_id % their_include_pattern % their_exclude_pattern);

  // Client as source, server as sink (writing).

  if (their_role == source_role || their_role == source_and_sink_role)
    {
      if (this->role != sink_role && this->role != source_and_sink_role)
        {
          this->saved_nonce = id("");
          error(not_permitted,
                (F("denied '%s' write permission for '%s' excluding '%s' while running as pure source")
                 % their_id % their_include_pattern % their_exclude_pattern).str());
        }

      if (!lua.hook_get_netsync_write_permitted(client_identity))
        {
          this->saved_nonce = id("");
          error(not_permitted,
                (F("denied '%s' write permission for '%s' excluding '%s'")
                 % their_id % their_include_pattern % their_exclude_pattern).str());
        }

      P(F("allowed '%s' write permission for '%s' excluding '%s'")
        % their_id % their_include_pattern % their_exclude_pattern);
    }

  rebuild_merkle_trees(ok_branches);

  this->received_remote_key = true;

  // Check the signature.
  if (project.db.check_signature(client, nonce1(), signature) == cert_ok)
    {
      // Get our private key and sign back.
      L(FL("client signature OK, accepting authentication"));
      this->authenticated = true;
      this->remote_peer_key_id = client;

      assume_corresponding_role(their_role);
      return true;
    }
  else
    {
      error(failed_identification, (F("bad client signature")).str());
    }
  return false;
}

bool
netsync_session::process_refine_cmd(refinement_type ty, merkle_node const & node)
{
  string typestr;
  netcmd_item_type_to_string(node.type, typestr);
  L(FL("processing refine cmd for %s node at level %d")
    % typestr % node.level);

  switch (node.type)
    {
    case file_item:
      W(F("Unexpected 'refine' command on non-refined item type"));
      break;

    case key_item:
      key_refiner.process_refinement_command(ty, node);
      break;

    case revision_item:
      rev_refiner.process_refinement_command(ty, node);
      break;

    case cert_item:
      cert_refiner.process_refinement_command(ty, node);
      break;

    case epoch_item:
      epoch_refiner.process_refinement_command(ty, node);
      break;
    }
  return true;
}

bool
netsync_session::process_bye_cmd(u8 phase,
                         transaction_guard & guard)
{

// Ideal shutdown
// ~~~~~~~~~~~~~~~
//
//             I/O events                 state transitions
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   ~~~~~~~~~~~~~~~~~~~
//                                        client: C_WORKING
//                                        server: S_WORKING
// 0. [refinement, data, deltas, etc.]
//                                        client: C_SHUTDOWN
//                                        (client checkpoints here)
// 1. client -> "bye 0"
// 2.           "bye 0"  -> server
//                                        server: S_SHUTDOWN
//                                        (server checkpoints here)
// 3.           "bye 1"  <- server
// 4. client <- "bye 1"
//                                        client: C_CONFIRMED
// 5. client -> "bye 2"
// 6.           "bye 2"  -> server
//                                        server: S_CONFIRMED
// 7. [server drops connection]
//
//
// Affects of I/O errors or disconnections
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//   C_WORKING: report error and fault
//   S_WORKING: report error and recover
//  C_SHUTDOWN: report error and fault
//  S_SHUTDOWN: report success and recover
//              (and warn that client might falsely see error)
// C_CONFIRMED: report success
// S_CONFIRMED: report success

  switch (phase)
    {
    case 0:
      if (voice == server_voice &&
          protocol_state == working_state)
        {
          protocol_state = shutdown_state;
          guard.do_checkpoint();
          queue_bye_cmd(1);
        }
      else
        error(bad_command, "unexpected bye phase 0 received");
      break;

    case 1:
      if (voice == client_voice &&
          protocol_state == shutdown_state)
        {
          protocol_state = confirmed_state;
          queue_bye_cmd(2);
        }
      else
        error(bad_command, "unexpected bye phase 1 received");
      break;

    case 2:
      if (voice == server_voice &&
          protocol_state == shutdown_state)
        {
          protocol_state = confirmed_state;
          return false;
        }
      else
        error(bad_command, "unexpected bye phase 2 received");
      break;

    default:
      error(bad_command, (F("unknown bye phase %d received") % phase).str());
    }

  return true;
}

bool
netsync_session::process_done_cmd(netcmd_item_type type, size_t n_items)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(FL("received 'done' command for %s (%s items)") % typestr % n_items);
  switch (type)
    {
    case file_item:
      W(F("Unexpected 'done' command on non-refined item type"));
      break;

    case key_item:
      key_refiner.process_done_command(n_items);
      if (key_refiner.done && role != sink_role)
        send_all_data(key_item, key_refiner.items_to_send);
      break;

    case revision_item:
      rev_refiner.process_done_command(n_items);
      break;

    case cert_item:
      cert_refiner.process_done_command(n_items);
      break;

    case epoch_item:
      epoch_refiner.process_done_command(n_items);
      if (epoch_refiner.done)
        {
          send_all_data(epoch_item, epoch_refiner.items_to_send);
          maybe_note_epochs_finished();
        }
      break;
    }
  return true;
}

void
netsync_session::respond_to_confirm_cmd()
{
  epoch_refiner.begin_refinement();
}

bool
netsync_session::data_exists(netcmd_item_type type,
                             id const & item)
{
  switch (type)
    {
    case key_item:
      return key_refiner.local_item_exists(item)
        || project.db.public_key_exists(key_id(item));
    case file_item:
      return project.db.file_version_exists(file_id(item));
    case revision_item:
      return rev_refiner.local_item_exists(item)
        || project.db.revision_exists(revision_id(item));
    case cert_item:
      return cert_refiner.local_item_exists(item)
        || project.db.revision_cert_exists(revision_id(item));
    case epoch_item:
      return epoch_refiner.local_item_exists(item)
        || project.db.epoch_exists(epoch_id(item));
    }
  return false;
}

void
netsync_session::load_data(netcmd_item_type type,
                           id const & item,
                           string & out)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);

  if (!data_exists(type, item))
    throw bad_decode(F("%s with hash '%s' does not exist in our database")
                     % typestr % item);

  switch (type)
    {
    case epoch_item:
      {
        branch_name branch;
        epoch_data epoch;
        project.db.get_epoch(epoch_id(item), branch, epoch);
        write_epoch(branch, epoch, out);
      }
      break;
    case key_item:
      {
        key_name keyid;
        rsa_pub_key pub;
        project.db.get_pubkey(key_id(item), keyid, pub);
        L(FL("public key '%s' is also called '%s'") % item % keyid);
        write_pubkey(keyid, pub, out);
        sent_keys.push_back(key_id(item));
      }
      break;

    case revision_item:
      {
        revision_data mdat;
        data dat;
        project.db.get_revision(revision_id(item), mdat);
        out = mdat.inner()();
      }
      break;

    case file_item:
      {
        file_data fdat;
        data dat;
        project.db.get_file_version(file_id(item), fdat);
        out = fdat.inner()();
      }
      break;

    case cert_item:
      {
        cert c;
        project.db.get_revision_cert(item, c);
        string tmp;
        key_name keyname;
        rsa_pub_key junk;
        project.db.get_pubkey(c.key, keyname, junk);
        if (version >= 7)
          {
            c.marshal_for_netio(keyname, out);
          }
        else
          {
            c.marshal_for_netio_v6(keyname, out);
          }
      }
      break;
    }
}

bool
netsync_session::process_data_cmd(netcmd_item_type type,
                                  id const & item,
                                  string const & dat)
{
  hexenc<id> hitem;
  encode_hexenc(item, hitem);

  string typestr;
  netcmd_item_type_to_string(type, typestr);

  note_item_arrived(type, item);
  if (data_exists(type, item))
    {
      L(FL("%s '%s' already exists in our database") % typestr % hitem());
      if (type == epoch_item)
        maybe_note_epochs_finished();
      return true;
    }

  switch (type)
    {
    case epoch_item:
      {
        branch_name branch;
        epoch_data epoch;
        read_epoch(dat, branch, epoch);
        L(FL("received epoch %s for branch %s")
          % epoch % branch);
        map<branch_name, epoch_data> epochs;
        project.db.get_epochs(epochs);
        map<branch_name, epoch_data>::const_iterator i;
        i = epochs.find(branch);
        if (i == epochs.end())
          {
            L(FL("branch %s has no epoch; setting epoch to %s")
              % branch % epoch);
            project.db.set_epoch(branch, epoch);
          }
        else
          {
            L(FL("branch %s already has an epoch; checking") % branch);
            // If we get here, then we know that the epoch must be
            // different, because if it were the same then the
            // if (epoch_exists()) branch up above would have been taken.
            // if somehow this is wrong, then we have broken epoch
            // hashing or something, which is very dangerous, so play it
            // safe...
            I(!(i->second == epoch));

            // It is safe to call 'error' here, because if we get here,
            // then the current netcmd packet cannot possibly have
            // written anything to the database.
            hexenc<data> my_epoch;
            hexenc<data> their_epoch;
            encode_hexenc(i->second.inner(), my_epoch);
            encode_hexenc(epoch.inner(), their_epoch);
            error(mixing_versions,
                  (F("Mismatched epoch on branch %s."
                     " Server has '%s', client has '%s'.")
                   % branch
                   % (voice == server_voice ? my_epoch : their_epoch)()
                   % (voice == server_voice ? their_epoch : my_epoch)()).str());
          }
      }
      maybe_note_epochs_finished();
      break;

    case key_item:
      {
        key_name keyid;
        rsa_pub_key pub;
        read_pubkey(dat, keyid, pub);
        key_id tmp;
        key_hash_code(keyid, pub, tmp);
        if (! (tmp.inner() == item))
          {
            throw bad_decode(F("hash check failed for public key '%s' (%s);"
                               " wanted '%s' got '%s'")
                             % hitem() % keyid % hitem()
                               % tmp);
          }
        if (project.db.put_key(keyid, pub))
          written_keys.push_back(key_id(item));
        else
          error(partial_transfer,
                (F("Received duplicate key %s") % keyid).str());
      }
      break;

    case cert_item:
      {
        cert c;
        bool matched;
        key_name keyname;
        if (version >= 7)
          {
            matched = cert::read_cert(project.db, dat, c, keyname);
            if (!matched)
              {
                W(F("Dropping incoming cert which claims to be signed by key\n"
                    "'%s' (name '%s'), but has a bad signature")
                  % c.key % keyname);
              }
          }
        else
          {
            matched = cert::read_cert_v6(project.db, dat, c, keyname);
            if (!matched)
              {
                W(F("dropping incoming cert which was signed by a key we don't have\n"
                    "you probably need to obtain this key from a more recent netsync peer\n"
                    "the name of the key involved is '%s', but note that there are multiple\n"
                    "keys with this name and we don't know which one it is") % keyname);
              }
          }

        if (matched)
          {
            key_name keyname;
            rsa_pub_key junk;
            project.db.get_pubkey(c.key, keyname, junk);
            id tmp;
            c.hash_code(keyname, tmp);
            if (! (tmp == item))
              throw bad_decode(F("hash check failed for revision cert '%s'") % hitem());
            if (project.db.put_revision_cert(c))
              written_certs.push_back(c);
          }
      }
      break;

    case revision_item:
      {
        L(FL("received revision '%s'") % hitem());
        data d(dat, origin::network);
        id tmp;
        calculate_ident(d, tmp);
        if (!(tmp == item))
          throw bad_decode(F("hash check failed for revision %s") % item);
        revision_t rev;
        read_revision(d, rev);
        if (project.db.put_revision(revision_id(item), rev))
          written_revisions.push_back(revision_id(item));
      }
      break;

    case file_item:
      {
        L(FL("received file '%s'") % hitem());
        data d(dat, origin::network);
        id tmp;
        calculate_ident(d, tmp);
        if (!(tmp == item))
          throw bad_decode(F("hash check failed for file %s") % item);
        project.db.put_file(file_id(item),
                            file_data(d));
      }
      break;
    }
  return true;
}

bool
netsync_session::process_delta_cmd(netcmd_item_type type,
                                   id const & base,
                                   id const & ident,
                                   delta const & del)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);

  pair<id,id> id_pair = make_pair(base, ident);

  note_item_arrived(type, ident);

  switch (type)
    {
    case file_item:
      {
        file_id src_file(base), dst_file(ident);
        project.db.put_file_version(src_file, dst_file, file_delta(del));
      }
      break;

    default:
      L(FL("ignoring delta received for item type %s") % typestr);
      break;
    }
  return true;
}

bool
netsync_session::process_usher_cmd(utf8 const & msg)
{
  if (msg().size())
    {
      if (msg()[0] == '!')
        P(F("Received warning from usher: %s") % msg().substr(1));
      else
        L(FL("Received greeting from usher: %s") % msg().substr(1));
    }
  netcmd cmdout(version);
  cmdout.write_usher_reply_cmd(utf8(peer_id, origin::internal),
                               our_include_pattern);
  write_netcmd_and_try_flush(cmdout);
  L(FL("Sent reply."));
  return true;
}


void
netsync_session::send_all_data(netcmd_item_type ty, set<id> const & items)
{
  string typestr;
  netcmd_item_type_to_string(ty, typestr);

  // Use temporary; passed arg will be invalidated during iteration.
  set<id> tmp = items;

  for (set<id>::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      if (data_exists(ty, *i))
        {
          string out;
          load_data(ty, *i, out);
          queue_data_cmd(ty, *i, out);
        }
    }
}

bool
netsync_session::dispatch_payload(netcmd const & cmd,
                                  transaction_guard & guard)
{

  switch (cmd.get_cmd_code())
    {

    case error_cmd:
      {
        string errmsg;
        cmd.read_error_cmd(errmsg);
        return process_error_cmd(errmsg);
      }
      break;

    case hello_cmd:
      require(! authenticated, "hello netcmd received when not authenticated");
      require(voice == client_voice, "hello netcmd received in client voice");
      {
        u8 server_version(0);
        key_name server_keyname;
        rsa_pub_key server_key;
        id nonce;
        cmd.read_hello_cmd(server_version, server_keyname, server_key, nonce);
        return process_hello_cmd(server_version, server_keyname, server_key, nonce);
      }
      break;

    case bye_cmd:
      require(authenticated, "bye netcmd received when not authenticated");
      {
        u8 phase;
        cmd.read_bye_cmd(phase);
        return process_bye_cmd(phase, guard);
      }
      break;

    case anonymous_cmd:
      require(! authenticated, "anonymous netcmd received when not authenticated");
      require(voice == server_voice, "anonymous netcmd received in server voice");
      require(role == source_role ||
              role == source_and_sink_role,
              "anonymous netcmd received in source or source/sink role");
      {
        protocol_role role;
        globish their_include_pattern, their_exclude_pattern;
        rsa_oaep_sha_data hmac_key_encrypted;
        cmd.read_anonymous_cmd(role, their_include_pattern, their_exclude_pattern, hmac_key_encrypted);
        L(FL("received 'anonymous' netcmd from client for pattern '%s' excluding '%s' "
            "in %s mode\n")
          % their_include_pattern % their_exclude_pattern
          % (role == source_and_sink_role ? _("source and sink") :
             (role == source_role ? _("source") : _("sink"))));

        set_session_key(hmac_key_encrypted);
        if (!process_anonymous_cmd(role, their_include_pattern, their_exclude_pattern))
            return false;
        queue_confirm_cmd();
        return true;
      }
      break;

    case auth_cmd:
      require(! authenticated, "auth netcmd received when not authenticated");
      require(voice == server_voice, "auth netcmd received in server voice");
      {
        protocol_role role;
        rsa_sha1_signature signature;
        globish their_include_pattern, their_exclude_pattern;
        key_id client;
        id nonce1, nonce2;
        rsa_oaep_sha_data hmac_key_encrypted;
        cmd.read_auth_cmd(role, their_include_pattern, their_exclude_pattern,
                          client, nonce1, hmac_key_encrypted, signature);

        L(FL("received 'auth(hmac)' netcmd from client '%s' for pattern '%s' "
            "exclude '%s' in %s mode with nonce1 '%s'\n")
          % client % their_include_pattern % their_exclude_pattern
          % (role == source_and_sink_role ? _("source and sink") :
             (role == source_role ? _("source") : _("sink")))
          % nonce1);

        set_session_key(hmac_key_encrypted);

        if (!process_auth_cmd(role, their_include_pattern, their_exclude_pattern,
                              client, nonce1, signature))
            return false;
        queue_confirm_cmd();
        return true;
      }
      break;

    case confirm_cmd:
      require(! authenticated, "confirm netcmd received when not authenticated");
      require(voice == client_voice, "confirm netcmd received in client voice");
      {
        string signature;
        cmd.read_confirm_cmd();
        this->authenticated = true;
        respond_to_confirm_cmd();
        return true;
      }
      break;

    case refine_cmd:
      require(authenticated, "refine netcmd received when authenticated");
      {
        merkle_node node;
        refinement_type ty;
        cmd.read_refine_cmd(ty, node);
        return process_refine_cmd(ty, node);
      }
      break;

    case done_cmd:
      require(authenticated, "done netcmd received when not authenticated");
      {
        size_t n_items;
        netcmd_item_type type;
        cmd.read_done_cmd(type, n_items);
        return process_done_cmd(type, n_items);
      }
      break;

    case data_cmd:
      require(authenticated, "data netcmd received when not authenticated");
      require(role == sink_role ||
              role == source_and_sink_role,
              "data netcmd received in source or source/sink role");
      {
        netcmd_item_type type;
        id item;
        string dat;
        cmd.read_data_cmd(type, item, dat);
        return process_data_cmd(type, item, dat);
      }
      break;

    case delta_cmd:
      require(authenticated, "delta netcmd received when not authenticated");
      require(role == sink_role ||
              role == source_and_sink_role,
              "delta netcmd received in source or source/sink role");
      {
        netcmd_item_type type;
        id base, ident;
        delta del;
        cmd.read_delta_cmd(type, base, ident, del);
        return process_delta_cmd(type, base, ident, del);
      }
      break;

    case usher_cmd:
      {
        utf8 greeting;
        cmd.read_usher_cmd(greeting);
        return process_usher_cmd(greeting);
      }
      break;

    case usher_reply_cmd:
      {
        u8 client_version;
        utf8 server;
        globish pattern;
        cmd.read_usher_reply_cmd(client_version, server, pattern);
        return process_usher_reply_cmd(client_version, server, pattern);
      }
      break;
    }
  return false;
}

// This kicks off the whole cascade starting from "hello".
void
netsync_session::begin_service()
{
  queue_usher_cmd(utf8("", origin::internal));
}

void
netsync_session::queue_usher_cmd(utf8 const & message)
{
  L(FL("queueing 'usher' command"));
  netcmd cmd(0);
  cmd.write_usher_cmd(message);
  write_netcmd_and_try_flush(cmd);
}

bool
netsync_session::process_usher_reply_cmd(u8 client_version,
                                         utf8 const & server,
                                         globish const & pattern)
{
  // netcmd::read() has already checked that the client isn't too old
  if (client_version < max_version)
    {
      version = client_version;
    }
  L(FL("client has maximum version %d, using %d")
    % widen<u32>(client_version) % widen<u32>(version));

  key_name name;
  keypair kp;
  if (use_transport_auth)
    keys.get_key_pair(signing_key, name, kp);
  queue_hello_cmd(name, kp.pub, mk_nonce());
  return true;
}

void
netsync_session::maybe_step()
{
  date_t start_time = date_t::now();

  while (done_all_refinements()
         && !rev_enumerator.done()
         && !output_overfull())
    {
      rev_enumerator.step();

      // Safety check, don't spin too long without
      // returning to the event loop.
      s64 elapsed_millisec = date_t::now() - start_time;
      if (elapsed_millisec > 1000 * 10)
        break;
    }
}

void
netsync_session::maybe_say_goodbye(transaction_guard & guard)
{
  if (voice == client_voice
      && protocol_state == working_state
      && finished_working())
    {
      protocol_state = shutdown_state;
      guard.do_checkpoint();
      queue_bye_cmd(0);
    }
}

bool
netsync_session::arm()
{
  if (!armed)
    {
      // Don't pack the buffer unnecessarily.
      if (output_overfull())
        return false;

      if (cmd_in.read(min_version, max_version, inbuf, read_hmac))
        {
          armed = true;
        }
    }
  return armed;
}

bool netsync_session::process(transaction_guard & guard)
{
  if (encountered_error)
    return true;
  try
    {
      if (!arm())
        return true;

      armed = false;
      L(FL("processing %d byte input buffer from peer %s")
        % inbuf.size() % peer_id);

      size_t sz = cmd_in.encoded_size();
      bool ret = dispatch_payload(cmd_in, guard);

      if (inbuf.size() >= constants::netcmd_maxsz)
        W(F("input buffer for peer %s is overfull "
            "after netcmd dispatch") % peer_id);

      guard.maybe_checkpoint(sz);

      if (!ret)
        L(FL("peer %s finishing processing with '%d' packet")
          % peer_id % cmd_in.get_cmd_code());
      return ret;
    }
  catch (bad_decode & bd)
    {
      W(F("protocol error while processing peer %s: '%s'")
        % peer_id % bd.what);
      return false;
    }
  catch (recoverable_failure & rf)
    {
      W(F("recoverable '%s' error while processing peer %s: '%s'")
        % origin::type_to_string(rf.caused_by())
        % peer_id % rf.what());
      return false;
    }
  catch (netsync_error & err)
    {
      W(F("error: %s") % err.msg);
      queue_error_cmd(boost::lexical_cast<string>(error_code) + " " + err.msg);
      encountered_error = true;
      return true; // Don't terminate until we've send the error_cmd.
    }
}

static void
insert_with_parents(revision_id rev,
                    refiner & ref,
                    revision_enumerator & rev_enumerator,
                    set<revision_id> & revs,
                    ticker & revisions_ticker)
{
  deque<revision_id> work;
  work.push_back(rev);
  while (!work.empty())
    {
      revision_id rid = work.front();
      work.pop_front();

      if (!null_id(rid) && revs.find(rid) == revs.end())
        {
          revs.insert(rid);
          ++revisions_ticker;
          ref.note_local_item(rid.inner());
          vector<revision_id> parents;
          rev_enumerator.get_revision_parents(rid, parents);
          for (vector<revision_id>::const_iterator i = parents.begin();
               i != parents.end(); ++i)
            {
              work.push_back(*i);
            }
        }
    }
}

void
netsync_session::rebuild_merkle_trees(set<branch_name> const & branchnames)
{
  P(F("finding items to synchronize:"));
  for (set<branch_name>::const_iterator i = branchnames.begin();
      i != branchnames.end(); ++i)
    L(FL("including branch %s") % *i);

  // xgettext: please use short message and try to avoid multibytes chars
  ticker revisions_ticker(N_("revisions"), "r", 64);
  // xgettext: please use short message and try to avoid multibytes chars
  ticker certs_ticker(N_("certificates"), "c", 256);
  // xgettext: please use short message and try to avoid multibytes chars
  ticker keys_ticker(N_("keys"), "k", 1);

  set<revision_id> revision_ids;
  set<key_id> inserted_keys;

  {
    for (set<branch_name>::const_iterator i = branchnames.begin();
         i != branchnames.end(); ++i)
      {
        // Get branch certs.
        vector<pair<id, cert> > certs;
        project.get_branch_certs(*i, certs);
        for (vector<pair<id, cert> >::const_iterator j = certs.begin();
             j != certs.end(); j++)
          {
            revision_id rid(j->second.ident);
            insert_with_parents(rid, rev_refiner, rev_enumerator,
                                revision_ids, revisions_ticker);
            // Branch certs go in here, others later on.
            cert_refiner.note_local_item(j->first);
            rev_enumerator.note_cert(rid, j->first);
            if (inserted_keys.find(j->second.key) == inserted_keys.end())
              inserted_keys.insert(j->second.key);
          }
      }
  }

  {
    map<branch_name, epoch_data> epochs;
    project.db.get_epochs(epochs);

    epoch_data epoch_zero(string(constants::epochlen_bytes, '\x00'),
                          origin::internal);
    for (set<branch_name>::const_iterator i = branchnames.begin();
         i != branchnames.end(); ++i)
      {
        branch_name const & branch(*i);
        map<branch_name, epoch_data>::const_iterator j;
        j = epochs.find(branch);

        // Set to zero any epoch which is not yet set.
        if (j == epochs.end())
          {
            L(FL("setting epoch on %s to zero") % branch);
            epochs.insert(make_pair(branch, epoch_zero));
            project.db.set_epoch(branch, epoch_zero);
          }

        // Then insert all epochs into merkle tree.
        j = epochs.find(branch);
        I(j != epochs.end());
        epoch_id eid;
        epoch_hash_code(j->first, j->second, eid);
        epoch_refiner.note_local_item(eid.inner());
      }
  }

  {
    typedef vector< pair<revision_id,
      pair<revision_id, key_id> > > cert_idx;

    cert_idx idx;
    project.db.get_revision_cert_nobranch_index(idx);

    // Insert all non-branch certs reachable via these revisions
    // (branch certs were inserted earlier).

    for (cert_idx::const_iterator i = idx.begin(); i != idx.end(); ++i)
      {
        revision_id const & hash = i->first;
        revision_id const & ident = i->second.first;
        key_id const & key = i->second.second;

        rev_enumerator.note_cert(ident, hash.inner());

        if (revision_ids.find(ident) == revision_ids.end())
          continue;

        cert_refiner.note_local_item(hash.inner());
        ++certs_ticker;
        if (inserted_keys.find(key) == inserted_keys.end())
            inserted_keys.insert(key);
      }
  }

  // Add any keys specified on the command line.
  for (vector<key_id>::const_iterator key
         = keys_to_push.begin();
       key != keys_to_push.end(); ++key)
    {
      if (inserted_keys.find(*key) == inserted_keys.end())
        {
          if (!project.db.public_key_exists(*key))
            {
              key_name name;
              keypair kp;
              if (keys.maybe_get_key_pair(*key, name, kp))
                project.db.put_key(name, kp.pub);
              else
                W(F("Cannot find key '%s'") % *key);
            }
          inserted_keys.insert(*key);
        }
    }

  // Insert all the keys.
  for (set<key_id>::const_iterator key = inserted_keys.begin();
       key != inserted_keys.end(); key++)
    {
      if (project.db.public_key_exists(*key))
        {
          if (global_sanity.debug_p())
            L(FL("noting key '%s' to send")
              % *key);

          key_refiner.note_local_item(key->inner());
          ++keys_ticker;
        }
    }

  rev_refiner.reindex_local_items();
  cert_refiner.reindex_local_items();
  key_refiner.reindex_local_items();
  epoch_refiner.reindex_local_items();
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
