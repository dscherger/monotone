// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <iostream>
#include <string>

#include <boost/optional.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>

#include "app_state.hh"
#include "cset.hh"
#include "constants.hh"
#include "packet.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "keys.hh"

using namespace std;
using boost::shared_ptr;
using boost::lexical_cast;
using boost::match_default;
using boost::match_results;
using boost::regex;

// --- packet db writer --
//
// FIXME: this comment is out of date, and untrustworthy.
// 
// the packet_db_writer::impl class (see below) manages writes to the
// database. it also ensures that those writes follow the semantic
// dependencies implied by the objects being written.
//
// an incoming manifest delta has three states:
//
// when it is first received, it is (probably) "non-constructable".
// this means that we do not have a way of building its preimage from either
// the database or from the memory cache of deltas we keep in this class
// 
// a non-constructable manifest delta is given a prerequisite of
// constructibility on its preimage.
//
// when the preimage becomes constructable, the manifest delta (probably)
// changes to "non-writable" state. this means that we have a way to build
// the manifest, we haven't received all the files which depend on it yet,
// so we won't write it to the database.
//
// when a manifest becomes constructable (but not necessarily writable) we
// call an analyzer back, if we have one, with the pre- and post-states of
// the delta.  this happens in order to give the netsync layer a chance to
// request all the file deltas which accompany the manifest delta.
// 
// a non-writable manifest delta is given prerequisites on all its
// non-existing underlying files, and delayed again.
//
// when all the files arrive, a non-writable manifest is written to the
// database.
//
// files are delayed to depend on their preimage, like non-constructable
// manifests. however, once they are constructable they are immediately
// written to the database.
//
/////////////////////////////////////////////////////////////////
//
// how it's done:
//
// each manifest or file has a companion class called a "prerequisite". a
// prerequisite has a set of delayed packets which depend on it. these
// delayed packets are also called dependents. a prerequisite can either be
// "unsatisfied" or "satisfied". when it is first constructed, it is
// unsatisfied. when it is satisfied, it calls all its dependents to inform
// them that it has become satisfied.
//
// when all the prerequisites of a given dependent is satisfied, the
// dependent writes itself back to the db writer. the dependent is then
// dead, and the prerequisite will forget about it.
//
// dependents' lifetimes are managed by prerequisites. when all
// prerequisites forget about their dependents, the dependent is destroyed
// (it is reference counted with a shared pointer). similarly, the
// packet_db_writer::impl holds references to prerequisites, and when
// a prerequisite no longer has any dependents, it is dropped from the
// packet_db_writer::impl, destroying it.
//
/////////////////////////////////////////////////////////////////
// 
// this same machinery is also re-used for the "valved" packet writer, as a
// convenient way to queue up commands in memory while the valve is closed.
// in this usage, we simply never add any prerequisites to any packet, and
// just call apply_delayed_packet when the valve opens.

typedef enum 
  {
    prereq_revision,
    prereq_file
  } 
prereq_type;

class delayed_packet;

class 
prerequisite
{
  hexenc<id> ident;
  prereq_type type;
  set< shared_ptr<delayed_packet> > delayed;
public:
  prerequisite(hexenc<id> const & i, prereq_type pt) 
    : ident(i), type(pt)
  {}
  void add_dependent(shared_ptr<delayed_packet> p);
  bool has_live_dependents();
  void satisfy(shared_ptr<prerequisite> self,
               packet_db_writer & pw);
  bool operator<(prerequisite const & other)
  {
    return type < other.type ||
      (type == other.type && ident < other.ident);
  }  
  // we need to be able to avoid circular dependencies between prerequisite and
  // delayed_packet shared_ptrs.
  void cleanup() { delayed.clear(); }
};

class 
delayed_packet
{
  set< shared_ptr<prerequisite> > unsatisfied_prereqs;
  set< shared_ptr<prerequisite> > satisfied_prereqs;
public:
  void add_prerequisite(shared_ptr<prerequisite> p);
  bool all_prerequisites_satisfied();
  void prerequisite_satisfied(shared_ptr<prerequisite> p, 
                              packet_db_writer & pw);
  virtual void apply_delayed_packet(packet_db_writer & pw) = 0;
  virtual ~delayed_packet() {}
};

void 
prerequisite::add_dependent(shared_ptr<delayed_packet> d)
{
  delayed.insert(d);
}

void
prerequisite::satisfy(shared_ptr<prerequisite> self,
                      packet_db_writer & pw)
{
  set< shared_ptr<delayed_packet> > dead;
  for (set< shared_ptr<delayed_packet> >::const_iterator i = delayed.begin();
       i != delayed.end(); ++i)
    {
      (*i)->prerequisite_satisfied(self, pw);
      if ((*i)->all_prerequisites_satisfied())
        dead.insert(*i);
    }
  for (set< shared_ptr<delayed_packet> >::const_iterator i = dead.begin();
       i != dead.end(); ++i)
    {
      delayed.erase(*i);
    }
}

void 
delayed_packet::add_prerequisite(shared_ptr<prerequisite> p)
{
  unsatisfied_prereqs.insert(p);
}

bool 
delayed_packet::all_prerequisites_satisfied()
{
  return unsatisfied_prereqs.empty();
}

void 
delayed_packet::prerequisite_satisfied(shared_ptr<prerequisite> p, 
                                       packet_db_writer & pw)
{
  I(unsatisfied_prereqs.find(p) != unsatisfied_prereqs.end());
  unsatisfied_prereqs.erase(p);
  satisfied_prereqs.insert(p);
  if (all_prerequisites_satisfied())
    {
      apply_delayed_packet(pw);
    }
}


// concrete delayed packets

class 
delayed_revision_data_packet 
  : public delayed_packet
{
  revision_id ident;
  revision_data dat;
public:
  delayed_revision_data_packet(revision_id const & i, 
                               revision_data const & md) 
    : ident(i), dat(md)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_revision_data_packet();
};


class 
delayed_file_data_packet 
  : public delayed_packet
{
  file_id ident;
  file_data dat;
public:
  delayed_file_data_packet(file_id const & i, 
                           file_data const & fd) 
    : ident(i), dat(fd)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_file_data_packet();
};

class 
delayed_file_delta_packet 
  : public delayed_packet
{
  file_id old_id;
  file_id new_id;
  file_delta del;
  bool forward_delta;
  bool write_full;
public:
  delayed_file_delta_packet(file_id const & oi, 
                            file_id const & ni,
                            file_delta const & md,
                            bool fwd,
                            bool full = false) 
    : old_id(oi), new_id(ni), del(md), forward_delta(fwd), write_full(full)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_file_delta_packet();
};


class 
delayed_revision_cert_packet 
  : public delayed_packet
{
  revision<cert> c;
public:
  delayed_revision_cert_packet(revision<cert> const & c) 
    : c(c)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_revision_cert_packet();
};

class 
delayed_public_key_packet 
  : public delayed_packet
{
  rsa_keypair_id id;
  base64<rsa_pub_key> key;
public:
  delayed_public_key_packet(rsa_keypair_id const & id,
                            base64<rsa_pub_key> key)
    : id(id), key(key)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_public_key_packet();
};

class 
delayed_keypair_packet 
  : public delayed_packet
{
  rsa_keypair_id id;
  keypair kp;
public:
  delayed_keypair_packet(rsa_keypair_id const & id,
                         keypair const & kp)
    : id(id), kp(kp)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_keypair_packet();
};

void 
delayed_revision_data_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed revision data packet for %s\n") % ident);
  pw.consume_revision_data(ident, dat);
}

delayed_revision_data_packet::~delayed_revision_data_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding revision data packet %s with unmet dependencies\n") % ident);
}


void 
delayed_file_data_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed file data packet for %s\n") % ident);
  pw.consume_file_data(ident, dat);
}

delayed_file_data_packet::~delayed_file_data_packet()
{
  // files have no prerequisites
  I(all_prerequisites_satisfied());
}


void 
delayed_file_delta_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed file %s packet for %s -> %s%s\n") 
    % (forward_delta ? "delta" : "reverse delta")
    % (forward_delta ? old_id : new_id)
    % (forward_delta ? new_id : old_id)
    % (write_full ? " (writing in full)" : ""));
  if (forward_delta)
    pw.consume_file_delta(old_id, new_id, del, write_full);
  else
    pw.consume_file_reverse_delta(new_id, old_id, del);
}

delayed_file_delta_packet::~delayed_file_delta_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding file delta packet %s -> %s with unmet dependencies\n")
        % old_id % new_id);
}

void 
delayed_revision_cert_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed revision cert on %s\n") % c.inner().ident);
  pw.consume_revision_cert(c);
}

delayed_revision_cert_packet::~delayed_revision_cert_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding revision cert packet %s with unmet dependencies\n")
      % c.inner().ident);
}

void 
delayed_public_key_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed public key %s\n") % id());
  pw.consume_public_key(id, key);
}

delayed_public_key_packet::~delayed_public_key_packet()
{
  // keys don't have dependencies
  I(all_prerequisites_satisfied());
}

void 
delayed_keypair_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed private key %s\n") % id());
  pw.consume_key_pair(id, kp);
}

delayed_keypair_packet::~delayed_keypair_packet()
{
  // keys don't have dependencies
  I(all_prerequisites_satisfied());
}


void
packet_consumer::set_on_revision_written(boost::function1<void,
                                                        revision_id> const & x)
{
  on_revision_written=x;
}

void
packet_consumer::set_on_cert_written(boost::function1<void,
                                                      cert const &> const & x)
{
  on_cert_written=x;
}

void
packet_consumer::set_on_pubkey_written(boost::function1<void, rsa_keypair_id>
                                                  const & x)
{
  on_pubkey_written=x;
}

void
packet_consumer::set_on_keypair_written(boost::function1<void, rsa_keypair_id>
                                                  const & x)
{
  on_keypair_written=x;
}


struct packet_db_writer::impl
{
  app_state & app;
  bool take_keys;
  size_t count;

  map<revision_id, shared_ptr<prerequisite> > revision_prereqs;
  map<file_id, shared_ptr<prerequisite> > file_prereqs;

  //   ticker cert;
  //   ticker manc;
  //   ticker manw;
  //   ticker filec;

  bool revision_exists_in_db(revision_id const & r);
  bool file_version_exists_in_db(file_id const & f);

  void get_revision_prereq(revision_id const & revision, shared_ptr<prerequisite> & p);
  void get_file_prereq(file_id const & file, shared_ptr<prerequisite> & p);

  void accepted_revision(revision_id const & r, packet_db_writer & dbw);
  void accepted_file(file_id const & f, packet_db_writer & dbw);

  impl(app_state & app, bool take_keys) 
    : app(app), take_keys(take_keys), count(0)
    // cert("cert", 1), manc("manc", 1), manw("manw", 1), filec("filec", 1)
  {}

  ~impl();
};

packet_db_writer::packet_db_writer(app_state & app, bool take_keys) 
  : pimpl(new impl(app, take_keys))
{}

packet_db_writer::~packet_db_writer() 
{}

packet_db_writer::impl::~impl()
{

  // break any circular dependencies for unsatisfied prerequisites
  for (map<revision_id, shared_ptr<prerequisite> >::const_iterator i =
      revision_prereqs.begin(); i != revision_prereqs.end(); i++)
    {
      i->second->cleanup();
    }
  for (map<file_id, shared_ptr<prerequisite> >::const_iterator i =
      file_prereqs.begin(); i != file_prereqs.end(); i++)
    {
      i->second->cleanup();
    }
}

bool 
packet_db_writer::impl::revision_exists_in_db(revision_id const & r)
{
  return app.db.revision_exists(r);
}


bool 
packet_db_writer::impl::file_version_exists_in_db(file_id const & f)
{
  return app.db.file_version_exists(f);
}

void 
packet_db_writer::impl::get_file_prereq(file_id const & file, 
                                        shared_ptr<prerequisite> & p)
{
  map<file_id, shared_ptr<prerequisite> >::const_iterator i;
  i = file_prereqs.find(file);
  if (i != file_prereqs.end())
    p = i->second;
  else
    {
      p = shared_ptr<prerequisite>(new prerequisite(file.inner(), prereq_file));
      file_prereqs.insert(make_pair(file, p));
    }
}


void
packet_db_writer::impl::get_revision_prereq(revision_id const & rev, 
                                            shared_ptr<prerequisite> & p)
{
  map<revision_id, shared_ptr<prerequisite> >::const_iterator i;
  i = revision_prereqs.find(rev);
  if (i != revision_prereqs.end())
    p = i->second;
  else
    {
      p = shared_ptr<prerequisite>(new prerequisite(rev.inner(), prereq_revision));
      revision_prereqs.insert(make_pair(rev, p));
    }
}


void 
packet_db_writer::impl::accepted_revision(revision_id const & r, packet_db_writer & dbw)
{
  L(F("noting acceptence of revision %s\n") % r);
  map<revision_id, shared_ptr<prerequisite> >::iterator i = revision_prereqs.find(r);
  if (i != revision_prereqs.end())
    {
      shared_ptr<prerequisite> prereq = i->second;
      revision_prereqs.erase(i);
      prereq->satisfy(prereq, dbw);
    }
}


void 
packet_db_writer::impl::accepted_file(file_id const & f, packet_db_writer & dbw)
{
  L(F("noting acceptence of file %s\n") % f);
  map<file_id, shared_ptr<prerequisite> >::iterator i = file_prereqs.find(f);  
  if (i != file_prereqs.end())
    {
      shared_ptr<prerequisite> prereq = i->second;
      file_prereqs.erase(i);
      prereq->satisfy(prereq, dbw);
    }
}


void 
packet_db_writer::consume_file_data(file_id const & ident, 
                                    file_data const & dat)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->file_version_exists_in_db(ident))
    {
      pimpl->app.db.put_file(ident, dat);
      pimpl->accepted_file(ident, *this);
    }
  else
    L(F("skipping existing file version %s\n") % ident);
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_file_delta(file_id const & old_id, 
                                     file_id const & new_id,
                                     file_delta const & del)
{
  consume_file_delta(old_id, new_id, del, false);
}

void 
packet_db_writer::consume_file_delta(file_id const & old_id, 
                                     file_id const & new_id,
                                     file_delta const & del,
                                     bool write_full)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->file_version_exists_in_db(new_id))
    {
      if (pimpl->file_version_exists_in_db(old_id))
        {
          file_id confirm;
          file_data old_dat;
          data new_dat;
          pimpl->app.db.get_file_version(old_id, old_dat);
          patch(old_dat.inner(), del.inner(), new_dat);
          calculate_ident(file_data(new_dat), confirm);
          if (confirm == new_id)
            {
              if (!write_full)
                pimpl->app.db.put_file_version(old_id, new_id, del);
              else
                pimpl->app.db.put_file(new_id, file_data(new_dat));
              pimpl->accepted_file(new_id, *this);
            }
          else
            {
              W(F("reconstructed file from delta '%s' -> '%s' has wrong id '%s'\n") 
                % old_id % new_id % confirm);
            }
        }
      else
        {
          L(F("delaying file delta %s -> %s for preimage\n") % old_id % new_id);
          shared_ptr<delayed_packet> dp;
          dp = shared_ptr<delayed_packet>(new delayed_file_delta_packet(old_id, new_id, del, true, write_full));
          shared_ptr<prerequisite> fp;
          pimpl->get_file_prereq(old_id, fp); 
          dp->add_prerequisite(fp);
          fp->add_dependent(dp);
        }
    }
  else
    L(F("skipping delta to existing file version %s\n") % new_id);
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_file_reverse_delta(file_id const & new_id,
                                             file_id const & old_id,
                                             file_delta const & del)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->file_version_exists_in_db(old_id))
    {
      if (pimpl->file_version_exists_in_db(new_id))
        {
          file_id confirm;
          file_data new_dat;
          data old_dat;
          pimpl->app.db.get_file_version(new_id, new_dat);
          patch(new_dat.inner(), del.inner(), old_dat);
          calculate_ident(file_data(old_dat), confirm);
          if (confirm == old_id)
            {
              pimpl->app.db.put_file_reverse_version(new_id, old_id, del);
              pimpl->accepted_file(old_id, *this);
            }
          else
            {
              W(F("reconstructed file from reverse delta '%s' -> '%s' has wrong id '%s'\n") 
                % new_id % old_id % confirm);
            }
        }
      else
        {
          L(F("delaying reverse file delta %s -> %s for preimage\n") % new_id % old_id);
          shared_ptr<delayed_packet> dp;
          dp = shared_ptr<delayed_packet>(new delayed_file_delta_packet(old_id, new_id, del, false));
          shared_ptr<prerequisite> fp;
          pimpl->get_file_prereq(new_id, fp); 
          dp->add_prerequisite(fp);
          fp->add_dependent(dp);
        }
    }
  else
    L(F("skipping reverse delta to existing file version %s\n") % old_id);
  ++(pimpl->count);
  guard.commit();
}

void
packet_db_writer::consume_revision_data(revision_id const & ident, 
                                        revision_data const & dat)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->revision_exists_in_db(ident))
    {

      shared_ptr<delayed_packet> dp;
      dp = shared_ptr<delayed_packet>(new delayed_revision_data_packet(ident, dat));
      
      revision_set rev;
      read_revision_set(dat, rev);
      
      for (edge_map::const_iterator i = rev.edges.begin(); 
           i != rev.edges.end(); ++i)
        {
          if (! (edge_old_revision(i).inner()().empty() 
                 || pimpl->revision_exists_in_db(edge_old_revision(i))))
            {
              L(F("delaying revision %s for old revision %s\n") 
                % ident % edge_old_revision(i));
              shared_ptr<prerequisite> fp;
              pimpl->get_revision_prereq(edge_old_revision(i), fp);
              dp->add_prerequisite(fp);
              fp->add_dependent(dp);
            }

          for (std::map<split_path, file_id>::const_iterator a 
		 = edge_changes(i).files_added.begin(); 
	       a != edge_changes(i).files_added.end(); ++a)		 
	    {
              if (! pimpl->file_version_exists_in_db(a->second))
                {
                  L(F("delaying revision %s for added file %s\n") 
                    % ident % a->second);
                  shared_ptr<prerequisite> fp;
                  pimpl->get_file_prereq(a->second, fp);
                  dp->add_prerequisite(fp);
                  fp->add_dependent(dp);
                }	      
	    }

          for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator d 
		 = edge_changes(i).deltas_applied.begin();
               d != edge_changes(i).deltas_applied.end(); ++d)
            {
              I(!delta_entry_src(d).inner()().empty());
              I(!delta_entry_dst(d).inner()().empty());
              if (! pimpl->file_version_exists_in_db(delta_entry_src(d)))
                {
                  L(F("delaying revision %s for old file %s\n") 
                    % ident % delta_entry_src(d));
                  shared_ptr<prerequisite> fp;
                  pimpl->get_file_prereq(delta_entry_src(d), fp);
                  dp->add_prerequisite(fp);
                  fp->add_dependent(dp);
                }
              if (! pimpl->file_version_exists_in_db(delta_entry_dst(d)))
                {
                  L(F("delaying revision %s for new file %s\n") 
                    % ident % delta_entry_dst(d));
                  shared_ptr<prerequisite> fp;
                  pimpl->get_file_prereq(delta_entry_dst(d), fp);
                  dp->add_prerequisite(fp);
                  fp->add_dependent(dp);
                }
            }     
        }

      if (dp->all_prerequisites_satisfied())
        {
          pimpl->app.db.put_revision(ident, dat);
          if(on_revision_written) on_revision_written(ident);
          pimpl->accepted_revision(ident, *this);
        }
    }
  else
    L(F("skipping existing revision %s\n") % ident);  
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_revision_cert(revision<cert> const & t)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->app.db.revision_cert_exists(t))
    {
      if (pimpl->revision_exists_in_db(revision_id(t.inner().ident)))
        {
          pimpl->app.db.put_revision_cert(t);
          if(on_cert_written) on_cert_written(t.inner());
        }
      else
        {
          L(F("delaying revision cert on %s\n") % t.inner().ident);
          shared_ptr<delayed_packet> dp;
          dp = shared_ptr<delayed_packet>(new delayed_revision_cert_packet(t));
          shared_ptr<prerequisite> fp;
          pimpl->get_revision_prereq(revision_id(t.inner().ident), fp); 
          dp->add_prerequisite(fp);
          fp->add_dependent(dp);
        }
    }
  else
    {
      string s;
      cert_signable_text(t.inner(), s);
      L(F("skipping existing revision cert %s\n") % s);
    }
  ++(pimpl->count);
  guard.commit();
}


void 
packet_db_writer::consume_public_key(rsa_keypair_id const & ident,
                                     base64< rsa_pub_key > const & k)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->take_keys) 
    {
      W(F("skipping prohibited public key %s\n") % ident);
      return;
    }
  if (! pimpl->app.db.public_key_exists(ident))
    {
      pimpl->app.db.put_key(ident, k);
      if(on_pubkey_written) on_pubkey_written(ident);
    }
  else
    {
      base64<rsa_pub_key> tmp;
      pimpl->app.db.get_key(ident, tmp);
      if (!keys_match(ident, tmp, ident, k))
        W(F("key '%s' is not equal to key '%s' in database\n") % ident % ident);
      L(F("skipping existing public key %s\n") % ident);
    }
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_key_pair(rsa_keypair_id const & ident,
                                   keypair const & kp)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->take_keys) 
    {
      W(F("skipping prohibited key pair %s\n") % ident);
      return;
    }
  if (! pimpl->app.keys.key_pair_exists(ident))
    {
      pimpl->app.keys.put_key_pair(ident, kp);
      if(on_keypair_written) on_keypair_written(ident);
    }
  else
    L(F("skipping existing key pair %s\n") % ident);
  ++(pimpl->count);
  guard.commit();
}


// --- valved packet writer ---

struct packet_db_valve::impl
{
  packet_db_writer writer;
  std::vector< boost::shared_ptr<delayed_packet> > packets;
  bool valve_is_open;
  impl(app_state & app, bool take_keys)
    : writer(app, take_keys),
      valve_is_open(false)
  {}
  void do_packet(boost::shared_ptr<delayed_packet> packet)
  {
    if (valve_is_open)
      packet->apply_delayed_packet(writer);
    else
      packets.push_back(packet);
  }
};

packet_db_valve::packet_db_valve(app_state & app, bool take_keys)
  : pimpl(new impl(app, take_keys))
{}
    
packet_db_valve::~packet_db_valve()
{}

void
packet_db_valve::open_valve()
{
  L(F("packet valve opened\n"));
  pimpl->valve_is_open = true;
  int written = 0;
  for (std::vector< boost::shared_ptr<delayed_packet> >::reverse_iterator
         i = pimpl->packets.rbegin();
       i != pimpl->packets.rend();
       ++i)
    {
      pimpl->do_packet(*i);
      ++written;
    }
  pimpl->packets.clear();
  L(F("wrote %i queued packets\n") % written);
}

#define DOIT(x) pimpl->do_packet(boost::shared_ptr<delayed_packet>(new x));

void
packet_db_valve::set_on_revision_written(boost::function1<void,
                                                        revision_id> const & x)
{
  on_revision_written=x;
  pimpl->writer.set_on_revision_written(x);
}

void
packet_db_valve::set_on_cert_written(boost::function1<void,
                                                      cert const &> const & x)
{
  on_cert_written=x;
  pimpl->writer.set_on_cert_written(x);
}

void
packet_db_valve::set_on_pubkey_written(boost::function1<void, rsa_keypair_id>
                                                  const & x)
{
  on_pubkey_written=x;
  pimpl->writer.set_on_pubkey_written(x);
}

void
packet_db_valve::set_on_keypair_written(boost::function1<void, rsa_keypair_id>
                                                  const & x)
{
  on_keypair_written=x;
  pimpl->writer.set_on_keypair_written(x);
}

void
packet_db_valve::consume_file_data(file_id const & ident, 
                                   file_data const & dat)
{
  DOIT(delayed_file_data_packet(ident, dat));
}

void
packet_db_valve::consume_file_delta(file_id const & id_old, 
                                    file_id const & id_new,
                                    file_delta const & del)
{
  DOIT(delayed_file_delta_packet(id_old, id_new, del, true));
}

void
packet_db_valve::consume_file_delta(file_id const & id_old, 
                                    file_id const & id_new,
                                    file_delta const & del,
                                    bool write_full)
{
  DOIT(delayed_file_delta_packet(id_old, id_new, del, true, write_full));
}

void
packet_db_valve::consume_file_reverse_delta(file_id const & id_new,
                                            file_id const & id_old,
                                            file_delta const & del)
{
  DOIT(delayed_file_delta_packet(id_old, id_new, del, false));
}

void
packet_db_valve::consume_revision_data(revision_id const & ident, 
                                       revision_data const & dat)
{
  DOIT(delayed_revision_data_packet(ident, dat));
}

void
packet_db_valve::consume_revision_cert(revision<cert> const & t)
{
  DOIT(delayed_revision_cert_packet(t));
}

void
packet_db_valve::consume_public_key(rsa_keypair_id const & ident,
                                    base64< rsa_pub_key > const & k)
{
  DOIT(delayed_public_key_packet(ident, k));
}

void
packet_db_valve::consume_key_pair(rsa_keypair_id const & ident,
                                  keypair const & kp)
{
  DOIT(delayed_keypair_packet(ident, kp));
}

#undef DOIT

// --- packet writer ---

packet_writer::packet_writer(ostream & o) : ost(o) {}

void 
packet_writer::consume_file_data(file_id const & ident, 
                                 file_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[fdata " << ident.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_file_delta(file_id const & old_id, 
                                  file_id const & new_id,
                                  file_delta const & del)
{
  base64<gzip<delta> > packed;
  pack(del.inner(), packed);
  ost << "[fdelta " << old_id.inner()() << endl 
      << "        " << new_id.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_file_reverse_delta(file_id const & new_id, 
                                          file_id const & old_id,
                                          file_delta const & del)
{
  base64<gzip<delta> > packed;
  pack(del.inner(), packed);
  ost << "[frdelta " << new_id.inner()() << endl 
      << "         " << old_id.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_revision_data(revision_id const & ident, 
                                     revision_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[rdata " << ident.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_revision_cert(revision<cert> const & t)
{
  ost << "[rcert " << t.inner().ident() << endl
      << "       " << t.inner().name() << endl
      << "       " << t.inner().key() << endl
      << "       " << trim_ws(t.inner().value()) << "]" << endl
      << trim_ws(t.inner().sig()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_public_key(rsa_keypair_id const & ident,
                                  base64< rsa_pub_key > const & k)
{
  ost << "[pubkey " << ident() << "]" << endl
      << trim_ws(k()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_key_pair(rsa_keypair_id const & ident,
                                keypair const & kp)
{
  ost << "[keypair " << ident() << "]" << endl
      << trim_ws(kp.pub()) <<"#\n" <<trim_ws(kp.priv()) << endl
      << "[end]" << endl;
}


// -- remainder just deals with the regexes for reading packets off streams

struct 
feed_packet_consumer
{
  size_t & count;
  packet_consumer & cons;
  std::string ident;
  std::string key;
  std::string certname;
  std::string base;
  std::string sp;
  feed_packet_consumer(size_t & count, packet_consumer & c)
   : count(count), cons(c),
     ident(constants::regex_legal_id_bytes),
     key(constants::regex_legal_key_name_bytes),
     certname(constants::regex_legal_cert_name_bytes),
     base(constants::regex_legal_packet_bytes),
     sp("[[:space:]]+")
  {}
  void require(bool x) const
  {
    E(x, F("malformed packet"));
  }
  bool operator()(match_results<std::string::const_iterator> const & res) const
  {
    if (res.size() != 4)
      throw oops("matched impossible packet with " 
                 + lexical_cast<string>(res.size()) + " matching parts: " +
                 string(res[0].first, res[0].second));
    I(res[1].matched);
    I(res[2].matched);
    I(res[3].matched);
    std::string type(res[1].first, res[1].second);
    std::string args(res[2].first, res[2].second);
    std::string body(res[3].first, res[3].second);
    if (regex_match(type, regex("[fr]data")))
      {
        L(F("read data packet"));
        require(regex_match(args, regex(ident)));
        require(regex_match(body, regex(base)));
        base64<gzip<data> > body_packed(trim_ws(body));
        data contents;
        unpack(body_packed, contents);
        if (type == "rdata")
          cons.consume_revision_data(revision_id(hexenc<id>(args)), 
                                     revision_data(contents));
        else if (type == "fdata")
          cons.consume_file_data(file_id(hexenc<id>(args)), 
                                 file_data(contents));
        else
          throw oops("matched impossible data packet with head '" + type + "'");
      }
    else if (regex_match(type, regex("fr?delta")))
      {
        L(F("read delta packet"));
        match_results<std::string::const_iterator> matches;
        require(regex_match(args, matches, regex(ident + sp + ident)));
        string src_id(matches[1].first, matches[1].second);
        string dst_id(matches[2].first, matches[2].second);
        require(regex_match(body, regex(base)));
        base64<gzip<delta> > body_packed(trim_ws(body));
        delta contents;
        unpack(body_packed, contents);
        if (type == "fdelta")
          cons.consume_file_delta(file_id(hexenc<id>(src_id)), 
                                  file_id(hexenc<id>(dst_id)), 
                                  file_delta(contents));
        else if (type == "frdelta")
          cons.consume_file_reverse_delta(file_id(hexenc<id>(src_id)), 
                                          file_id(hexenc<id>(dst_id)), 
                                          file_delta(contents));
        else
          throw oops("matched impossible delta packet with head '"
                     + type + "'");
      }
    else if (type == "rcert")
      {
        L(F("read cert packet"));
        match_results<std::string::const_iterator> matches;
        require(regex_match(args, matches, regex(ident + sp + certname
                                                 + key + sp + base)));
        string certid(matches[1].first, matches[1].second);
        string name(matches[2].first, matches[2].second);
        string keyid(matches[3].first, matches[3].second);
        string val(matches[4].first, matches[4].second);
        string contents(trim_ws(body));

        // canonicalize the base64 encodings to permit searches
        cert t = cert(hexenc<id>(certid),
                      cert_name(name),
                      base64<cert_value>(canonical_base64(val)),
                      rsa_keypair_id(keyid),
                      base64<rsa_sha1_signature>(canonical_base64(contents)));
        cons.consume_revision_cert(revision<cert>(t));
      } 
    else if (type == "pubkey")
      {
        L(F("read pubkey data packet"));
        require(regex_match(args, regex(key)));
        require(regex_match(body, regex(base)));
        string contents(trim_ws(body));
        cons.consume_public_key(rsa_keypair_id(args),
                                base64<rsa_pub_key>(contents));
      }
    else if (type == "keypair")
      {
        L(F("read keypair data packet"));
        require(regex_match(args, regex(key)));
        match_results<std::string::const_iterator> matches;
        require(regex_match(body, matches, regex(base + "#" + base)));
        string pub_dat(trim_ws(string(matches[1].first, matches[1].second)));
        string priv_dat(trim_ws(string(matches[2].first, matches[2].second)));
        cons.consume_key_pair(rsa_keypair_id(args), keypair(pub_dat, priv_dat));
      }
    else
      {
        W(F("unknown packet type: '%s'") % type);
        return true;
      }
    ++count;
    return true;
  }
};

static size_t 
extract_packets(string const & s, packet_consumer & cons, bool last)
{
  std::string r(s);
  {
    // since we don't have privkey packets anymore, translate a
    // pubkey packet immediately followed by a matching privkey
    // packet into a keypair packet (which is what privkey packets
    // have been replaced by)
    string const pubkey("\\[pubkey[[:space:]]+" + constants::regex_legal_key_name_bytes
                        + "\\]" + constants::regex_legal_packet_bytes + "\\[end\\]");
    string const privkey("\\[privkey \\1\\]" + constants::regex_legal_packet_bytes
                         + "\\[end\\]");
    string const pubkey_privkey = pubkey + "[[:space:]]*" + privkey;
    string const keypair_fmt("[keypair $1]$2#$3[end]");
    r = regex_replace(s, regex(pubkey_privkey), keypair_fmt);
    bool pub = regex_match(s, regex(pubkey + ".*"));
    bool two = regex_match(s, regex(".+\\[end\\].+\\[end\\]"));
    if (!last && pub && !two)
      return 0;
  }

  string const head("\\[([a-z]+)[[:space:]]+([^\\[\\]]+)\\]");
  string const body("([^\\[\\]]+)");
  string const tail("\\[end\\]");
  string const whole = head + body + tail;
  regex expr(whole);
  size_t count = 0;
  regex_grep(feed_packet_consumer(count, cons), r, expr, match_default);
  return count;
}


size_t 
read_packets(istream & in, packet_consumer & cons)
{
  string accum, tmp;
  size_t count = 0;
  size_t const bufsz = 0xff;
  char buf[bufsz];
  string const end("[end]");
  while(in)
    {
      in.read(buf, bufsz);
      accum.append(buf, in.gcount());      
      string::size_type endpos = string::npos;
      endpos = accum.rfind(end);
      if (endpos != string::npos)
        {
          endpos += end.size();
          string tmp = accum.substr(0, endpos);
          size_t num = extract_packets(tmp, cons, false);
          count += num;
          if (num)
            if (endpos < accum.size() - 1)
              accum = accum.substr(endpos+1);
            else
              accum.clear();
        }
    }
  count += extract_packets(accum, cons, true);
  return count;
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "transforms.hh"

static void 
packet_roundabout_test()
{
  string tmp;

  {
    ostringstream oss;
    packet_writer pw(oss);

    // an fdata packet
    file_data fdata(data("this is some file data"));
    file_id fid;
    calculate_ident(fdata, fid);
    pw.consume_file_data(fid, fdata);

    // an fdelta packet    
    file_data fdata2(data("this is some file data which is not the same as the first one"));
    file_id fid2;
    calculate_ident(fdata2, fid);
    delta del;
    diff(fdata.inner(), fdata2.inner(), del);
    pw.consume_file_delta(fid, fid2, file_delta(del));

    // a cert packet
    base64<cert_value> val;
    encode_base64(cert_value("peaches"), val);
    base64<rsa_sha1_signature> sig;
    encode_base64(rsa_sha1_signature("blah blah there is no way this is a valid signature"), sig);    
    // should be a type violation to use a file id here instead of a revision
    // id, but no-one checks...
    cert c(fid.inner(), cert_name("smell"), val, 
           rsa_keypair_id("fun@moonman.com"), sig);
    pw.consume_revision_cert(revision<cert>(c));

    keypair kp;
    // a public key packet
    encode_base64(rsa_pub_key("this is not a real rsa key"), kp.pub);
    pw.consume_public_key(rsa_keypair_id("test@lala.com"), kp.pub);

    // a private key packet
    encode_base64(rsa_priv_key("this is not a real rsa key either!"), kp.priv);
    
    pw.consume_key_pair(rsa_keypair_id("test@lala.com"), kp);
    
  }
  
  for (int i = 0; i < 10; ++i)
    {
      // now spin around sending and receiving this a few times
      ostringstream oss;
      packet_writer pw(oss);      
      istringstream iss(tmp);
      read_packets(iss, pw);
      BOOST_CHECK(oss.str() == tmp);
      tmp = oss.str();
    }
}

void 
add_packet_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&packet_roundabout_test));
}

#endif // BUILD_UNIT_TESTS
