// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __REACTABLE_HH__
#define __REACTABLE_HH__

#include "netxx_pipe.hh"

class transaction_guard;

class reactable
{
  static unsigned int count;
protected:
  static unsigned int num_reactables() { return count; }
public:
  reactable() { ++count; }
  virtual ~reactable()
  {
    I(count != 0);
    --count;
  }

  // Handle an I/O event.
  virtual bool do_io(Netxx::Probe::ready_type event) = 0;
  // Can we timeout after being idle for a long time?
  virtual bool can_timeout() = 0;
  // Have we been idle for too long?
  virtual bool timed_out(time_t now) = 0;
  // Do one unit of work.
  virtual bool do_work(transaction_guard & guard) = 0;
  // Is there any work waiting to be done?
  virtual bool arm() = 0;
  // Are we a pipe pair (as opposed to a socket)?
  // Netxx::PipeCompatibleProbe acts slightly differently, depending.
  virtual bool is_pipe_pair() = 0;
  // Netxx::Probe::ready() returns sockets, reactor needs to be
  // able to map them back to reactables.
  virtual std::vector<Netxx::socket_type> get_sockets() = 0;
  // Netxx::StreamBase and Netxx::StreamServer don't have a
  // common base, so we don't have anything we can expose to
  // let the reactor add us to the probe itself.
  virtual void add_to_probe(Netxx::PipeCompatibleProbe & probe) = 0;
  virtual void remove_from_probe(Netxx::PipeCompatibleProbe & probe) = 0;
  // Where are we talking to / listening on?
  virtual std::string name() = 0;
};

#endif


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
