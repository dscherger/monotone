// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __LISTENER_BASE_HH__
#define __LISTENER_BASE_HH__

#include "network/reactable.hh"

#include <boost/shared_ptr.hpp>

class listener_base : public reactable
{
protected:
  boost::shared_ptr<Netxx::StreamServer> srv;
public:
  listener_base(boost::shared_ptr<Netxx::StreamServer> srv);
  virtual ~listener_base();
  virtual bool do_io(Netxx::Probe::ready_type event) = 0;
  bool timed_out(time_t now);
  bool do_work(transaction_guard & guard);
  bool arm();
  bool can_timeout();

  std::string name();

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
