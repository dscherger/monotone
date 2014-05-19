// Copyright (C) 2008, 2014 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __SESSION_BASE_HH__
#define __SESSION_BASE_HH__

#include <memory>
#include <deque>

#include "../netxx/stream.h"
#include "../netcmd.hh" // for protocol_voice
#include "../string_queue.hh"
#include "reactable.hh"

// This is not currently needed because there's only one kind of session.
// But it's already here and not hurting anything, and might be useful if
// we want to add another kind of session later (something that talks
// http or fastcgi?).
class session_base : public reactable
{
  void read_some(bool & failed, bool & eof);
  bool write_some();
  void mark_recent_io();
protected:
  virtual void note_bytes_in(int /* count */) { return; }
  virtual void note_bytes_out(int /* count */) { return; }
  string_queue inbuf;
private:
  std::deque< std::pair<std::string, size_t> > outbuf;
  size_t outbuf_bytes; // so we can avoid queueing up too much stuff
protected:
  void queue_output(std::string const & s);
public:
  bool output_overfull() const;
  bool output_empty() const;
public:
  std::string peer_id;
  std::string name() { return peer_id; }
private:
  std::shared_ptr<Netxx::StreamBase> str;
  time_t last_io_time;
public:

  enum
    {
      working_state,
      shutdown_state,
      confirmed_state
    }
    protocol_state;

  protocol_voice const voice;

  bool encountered_error;

  session_base(protocol_voice voice,
               std::string const & peer_id,
               std::shared_ptr<Netxx::StreamBase> str);
  virtual ~session_base();
  virtual bool arm() = 0;
  virtual bool do_work(transaction_guard & guard) = 0;

private:
  Netxx::Probe::ready_type which_events();
public:
  virtual bool do_io(Netxx::Probe::ready_type);
  bool can_timeout() { return true; }
  bool timed_out(time_t now);

  bool is_pipe_pair();
  std::vector<Netxx::socket_type> get_sockets();
  void add_to_probe(Netxx::PipeCompatibleProbe & probe);
  void remove_from_probe(Netxx::PipeCompatibleProbe & probe);
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
