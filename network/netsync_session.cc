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

netsync_session::netsync_session(session * owner,
                                 options & opts,
                                 lua_hooks & lua,
                                 project_t & project,
                                 key_store & keys,
                                 protocol_role role,
                                 globish const & our_include_pattern,
                                 globish const & our_exclude_pattern,
                                 shared_conn_counts counts,
                                 bool initiated_by_server) :
  wrapped_session(owner),
  role(role),
  our_include_pattern(our_include_pattern),
  our_exclude_pattern(our_exclude_pattern),
  our_matcher(our_include_pattern, our_exclude_pattern),
  project(project),
  keys(keys),
  lua(lua),
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
  set_totals(false),
  epoch_refiner(epoch_item, get_voice(), *this),
  key_refiner(key_item, get_voice(), *this),
  cert_refiner(cert_item, get_voice(), *this),
  rev_refiner(revision_item, get_voice(), *this),
  is_dry_run(opts.dryrun),
  dry_run_keys_refined(false),
  counts(counts),
  rev_enumerator(project, *this),
  initiated_by_server(initiated_by_server)
{
  I(counts);

  for (vector<external_key_name>::const_iterator i = opts.keys_to_push.begin();
       i != opts.keys_to_push.end(); ++i)
    {
      key_identity_info ident;
      project.get_key_identity(keys, lua, *i, ident);
      keys_to_push.push_back(ident.id);
    }
}

netsync_session::~netsync_session()
{ }

void netsync_session::on_begin(size_t ident, key_identity_info const & remote_key)
{
  lua.hook_note_netsync_start(ident,
                              get_voice() == server_voice ? "server" : "client",
                              role,
                              get_peer(), remote_key,
                              our_include_pattern, our_exclude_pattern);
}

void netsync_session::on_end(size_t ident)
{
  int error_code = get_error_code();
  if (shutdown_confirmed())
    error_code = error_codes::no_error;
  else if (error_code == error_codes::no_transfer &&
           (revs_in || revs_out ||
            certs_in || certs_out ||
            keys_in || keys_out))
    error_code = error_codes::partial_transfer;

  // Call Lua hooks
  vector<cert> unattached_written_certs;
  map<revision_id, vector<cert> > rev_written_certs;
  for (vector<revision_id>::const_iterator i = counts->revs_in.items.begin();
       i != counts->revs_in.items.end(); ++i)
    rev_written_certs.insert(make_pair(*i, vector<cert>()));
  for (vector<cert>::const_iterator i = counts->certs_in.items.begin();
       i != counts->certs_in.items.end(); ++i)
    {
      map<revision_id, vector<cert> >::iterator j;
      j = rev_written_certs.find(revision_id(i->ident));
      if (j == rev_written_certs.end())
        unattached_written_certs.push_back(*i);
      else
        j->second.push_back(*i);
    }

  if (!counts->revs_in.items.empty()
      || !counts->keys_in.items.empty()
      || !counts->certs_in.items.empty())
    {

      //Keys
      for (vector<key_id>::const_iterator i = counts->keys_in.items.begin();
           i != counts->keys_in.items.end(); ++i)
        {
          key_identity_info identity;
          identity.id = *i;
          project.complete_key_identity_from_id(keys, lua, identity);
          lua.hook_note_netsync_pubkey_received(identity, ident);
        }

      //Revisions
      for (vector<revision_id>::const_iterator i = counts->revs_in.items.begin();
           i != counts->revs_in.items.end(); ++i)
        {
          vector<cert> & ctmp(rev_written_certs[*i]);
          set<pair<key_identity_info, pair<cert_name, cert_value> > > certs;
          for (vector<cert>::const_iterator j = ctmp.begin();
               j != ctmp.end(); ++j)
            {
              key_identity_info identity;
              identity.id = j->key;
              project.complete_key_identity_from_id(keys, lua, identity);
              certs.insert(make_pair(identity, make_pair(j->name, j->value)));
            }

          revision_data rdat;
          project.db.get_revision(*i, rdat);
          lua.hook_note_netsync_revision_received(*i, rdat, certs,
                                                  ident);
        }

      //Certs (not attached to a new revision)
      for (vector<cert>::iterator i = unattached_written_certs.begin();
           i != unattached_written_certs.end(); ++i)
        {
          key_identity_info identity;
          identity.id = i->key;
          project.complete_key_identity_from_id(keys, lua, identity);
          lua.hook_note_netsync_cert_received(revision_id(i->ident), identity,
                                              i->name, i->value, ident);
        }
    }

  if (!counts->keys_out.items.empty()
      || !counts->revs_out.items.empty()
      || !counts->certs_out.items.empty())
    {

      vector<cert> unattached_sent_certs;
      map<revision_id, vector<cert> > rev_sent_certs;
      for (vector<revision_id>::const_iterator i = counts->revs_out.items.begin();
           i != counts->revs_out.items.end(); ++i)
        rev_sent_certs.insert(make_pair(*i, vector<cert>()));
      for (vector<cert>::const_iterator i = counts->certs_out.items.begin();
           i != counts->certs_out.items.end(); ++i)
        {
          map<revision_id, vector<cert> >::iterator j;
          j = rev_sent_certs.find(revision_id(i->ident));
          if (j == rev_sent_certs.end())
            unattached_sent_certs.push_back(*i);
          else
            j->second.push_back(*i);
        }

      //Keys
      for (vector<key_id>::const_iterator i = counts->keys_out.items.begin();
           i != counts->keys_out.items.end(); ++i)
        {
          key_identity_info identity;
          identity.id = *i;
          project.complete_key_identity_from_id(keys, lua, identity);
          lua.hook_note_netsync_pubkey_sent(identity, ident);
        }

      //Revisions
      for (vector<revision_id>::const_iterator i = counts->revs_out.items.begin();
           i != counts->revs_out.items.end(); ++i)
        {
          vector<cert> & ctmp(rev_sent_certs[*i]);
          set<pair<key_identity_info, pair<cert_name, cert_value> > > certs;
          for (vector<cert>::const_iterator j = ctmp.begin();
               j != ctmp.end(); ++j)
            {
              key_identity_info identity;
              identity.id = j->key;
              project.complete_key_identity_from_id(keys, lua, identity);
              certs.insert(make_pair(identity, make_pair(j->name, j->value)));
            }

          revision_data rdat;
          project.db.get_revision(*i, rdat);
          lua.hook_note_netsync_revision_sent(*i, rdat, certs,
                                                  ident);
        }

      //Certs (not attached to a new revision)
      for (vector<cert>::iterator i = unattached_sent_certs.begin();
           i != unattached_sent_certs.end(); ++i)
        {
          key_identity_info identity;
          identity.id = i->key;
          project.complete_key_identity_from_id(keys, lua, identity);
          lua.hook_note_netsync_cert_sent(revision_id(i->ident), identity,
                                          i->name, i->value, ident);
        }
    }

  lua.hook_note_netsync_end(ident, error_code,
                            bytes_in, bytes_out,
                            certs_in, certs_out,
                            revs_in, revs_out,
                            keys_in, keys_out);
}

string
netsync_session::usher_reply_data() const
{
  return our_include_pattern();
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
  counts->revs_out.add_item(rev);
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
  if (get_version() >= 7)
    {
      c.marshal_for_netio(keyname, str);
    }
  else
    {
      c.marshal_for_netio_v6(keyname, str);
    }
  queue_data_cmd(cert_item, i, str);
  counts->certs_out.add_item(c);
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

      // the following two tickers may be skipped if we have size restrictions
      // xgettext: please use short message and try to avoid multibytes chars
      cert_in_ticker.reset(new ticker(N_("certs in"), "c", 3, false, true));
      // xgettext: please use short message and try to avoid multibytes chars
      cert_out_ticker.reset(new ticker(N_("certs out"), "C", 3, false, true));
    }
}

bool
netsync_session::done_all_refinements() const
{
  bool all = rev_refiner.done
    && cert_refiner.done
    && key_refiner.done
    && epoch_refiner.done;

  if (all && !set_totals)
    {
      L(FL("All refinements done for peer %s") % get_peer());
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
netsync_session::received_all_items() const
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
netsync_session::dry_run_finished() const
{
  bool all = rev_refiner.done
    && cert_refiner.done
    && dry_run_keys_refined;

  if (all)
    {
      counts->revs_in.set_count(rev_refiner.items_to_receive, false);
      counts->certs_in.set_count(cert_refiner.items_to_receive, false);
      counts->keys_in.set_count(key_refiner.min_items_to_receive,
                                key_refiner.may_receive_more_than_min);

      counts->revs_out.set_items(rev_refiner.items_to_send);
      counts->certs_out.set_count(cert_refiner.items_to_send.size(), false);
      counts->keys_out.set_items(key_refiner.items_to_send);
    }

  return all;
}

bool
netsync_session::finished_working() const
{
  if (dry_run_finished())
    return true;

  bool all = done_all_refinements()
    && received_all_items()
    && queued_all_items()
    && rev_enumerator.done();
  return all;
}

bool
netsync_session::queued_all_items() const
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
  if (encountered_error())
    return;

  // But otherwise, we're ready to go. Start the next
  // set of refinements.
  if (get_voice() == client_voice)
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


bool
netsync_session::do_work(transaction_guard & guard,
                         netcmd const * const cmd_in)
{
  if (!cmd_in || process(guard, *cmd_in))
    {
      maybe_step();
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
netsync_session::queue_done_cmd(netcmd_item_type type,
                        size_t n_items)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  if (is_dry_run && type == key_item)
    {
      dry_run_keys_refined = true;
      return;
    }
  L(FL("queueing 'done' command for %s (%d items)")
    % typestr % n_items);
  netcmd cmd(get_version());
  cmd.write_done_cmd(type, n_items);
  write_netcmd(cmd);
}

void
netsync_session::request_service()
{

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

  request_netsync(role, our_include_pattern, our_exclude_pattern);
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
  netcmd cmd(get_version());
  cmd.write_refine_cmd(ty, node);
  write_netcmd(cmd);
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

  netcmd cmd(get_version());
  // TODO: This pair of functions will make two copies of a large
  // file, the first in cmd.write_data_cmd, and the second in
  // write_netcmd when the data is copied from the
  // cmd.payload variable to the string buffer for output.  This
  // double copy should be collapsed out, it may be better to use
  // a string_queue for output as well as input, as that will reduce
  // the amount of mallocs that happen when the string queue is large
  // enough to just store the data.
  cmd.write_data_cmd(type, item, dat);
  write_netcmd(cmd);
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
  netcmd cmd(get_version());
  cmd.write_delta_cmd(type, base, ident, del);
  write_netcmd(cmd);
  note_item_sent(type, ident);
}


// processors

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
netsync_session::accept_service()
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
        counts->keys_out.add_item(key_id(item));
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
        if (get_version() >= 7)
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
            bool am_server = (get_voice() == server_voice);
            error(error_codes::mixing_versions,
                  (F("Mismatched epoch on branch %s."
                     " Server has '%s', client has '%s'.")
                   % branch
                   % (am_server ? my_epoch : their_epoch)()
                   % (am_server ? their_epoch : my_epoch)()).str());
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
          counts->keys_in.add_item(key_id(item));
        else
          error(error_codes::partial_transfer,
                (F("Received duplicate key %s") % keyid).str());
      }
      break;

    case cert_item:
      {
        cert c;
        bool matched;
        key_name keyname;
        if (get_version() >= 7)
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
              counts->certs_in.add_item(c);
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
          counts->revs_in.add_item(revision_id(item));
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
    case refine_cmd:
      require(get_authenticated(), "refine netcmd received when authenticated");
      {
        merkle_node node;
        refinement_type ty;
        cmd.read_refine_cmd(ty, node);
        return process_refine_cmd(ty, node);
      }
      break;

    case done_cmd:
      require(get_authenticated(), "done netcmd received when not authenticated");
      {
        size_t n_items;
        netcmd_item_type type;
        cmd.read_done_cmd(type, n_items);
        return process_done_cmd(type, n_items);
      }
      break;

    case data_cmd:
      require(get_authenticated(), "data netcmd received when not authenticated");
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
      require(get_authenticated(), "delta netcmd received when not authenticated");
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

    default:
      return false;
    }
  return false;
}

bool
netsync_session::have_work() const
{
  return done_all_refinements()
    && !rev_enumerator.done()
    && !output_overfull();
}

void
netsync_session::maybe_step()
{
  date_t start_time = date_t::now();

  while (have_work())
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
netsync_session::prepare_to_confirm(key_identity_info const & client_identity,
                                    bool use_transport_auth)
{
  if (!get_authenticated() && role != source_role && use_transport_auth)
    {
      error(error_codes::not_permitted,
            F("rejected attempt at anonymous connection for write").str());
    }

  set<branch_name> ok_branches, all_branches;

  project.get_branch_list(all_branches);
  globish_matcher matcher(our_include_pattern, our_exclude_pattern);

  for (set<branch_name>::const_iterator i = all_branches.begin();
      i != all_branches.end(); i++)
    {
      if (matcher((*i)()))
        {
          if (use_transport_auth)
            {
              if (!get_authenticated())
                {
                  if (!lua.hook_get_netsync_read_permitted((*i)()))
                    error(error_codes::not_permitted,
                          (F("anonymous access to branch '%s' denied by server")
                           % *i).str());
                }
              else
                {
                  if (!lua.hook_get_netsync_read_permitted((*i)(), client_identity))
                    error(error_codes::not_permitted,
                          (F("denied '%s' read permission for '%s' excluding '%s' because of branch '%s'")
                           % client_identity.id
                           % our_include_pattern % our_exclude_pattern % *i).str());
                }
            }
          ok_branches.insert(*i);
        }
    }

  if (get_authenticated())
    {
      P(F("allowed '%s' read permission for '%s' excluding '%s'")
        % client_identity.id % our_include_pattern % our_exclude_pattern);
    }
  else if (use_transport_auth)
    {
      P(F("allowed anonymous read permission for '%s' excluding '%s'")
        % our_include_pattern % our_exclude_pattern);
    }
  else
    {
      P(F("allowed anonymous read/write permission for '%s' excluding '%s'")
        % our_include_pattern % our_exclude_pattern);
    }

  if (use_transport_auth && (role == sink_role || role == source_and_sink_role))
    {
      if (!lua.hook_get_netsync_write_permitted(client_identity))
        {
          error(error_codes::not_permitted,
                (F("denied '%s' write permission for '%s' excluding '%s'")
                 % client_identity.id
                 % our_include_pattern % our_exclude_pattern).str());
        }

      P(F("allowed '%s' write permission for '%s' excluding '%s'")
        % client_identity.id % our_include_pattern % our_exclude_pattern);
    }

  rebuild_merkle_trees(ok_branches);
}

bool netsync_session::process(transaction_guard & guard,
                              netcmd const & cmd_in)
{
  try
    {
      size_t sz = cmd_in.encoded_size();
      bool ret = dispatch_payload(cmd_in, guard);

      guard.maybe_checkpoint(sz);

      if (!ret)
        L(FL("peer %s finishing processing with '%d' packet")
          % get_peer() % cmd_in.get_cmd_code());
      return ret;
    }
  catch (bad_decode & bd)
    {
      W(F("protocol error while processing peer %s: '%s'")
        % get_peer() % bd.what);
      return false;
    }
  catch (recoverable_failure & rf)
    {
      W(F("recoverable '%s' error while processing peer %s: '%s'")
        % origin::type_to_string(rf.caused_by())
        % get_peer() % rf.what());
      return false;
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
          L(FL("including key %s by special request") % *key);
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
