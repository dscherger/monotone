// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//               2014-2016 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __LISTENER_HH__
#define __LISTENER_HH__

#include "../base.hh"
#include <asio.hpp>

#include "../netcmd.hh"
#include "../vector.hh"
#include "../vocab.hh"

class app_state;
class key_store;
class project_t;
class reactor;
class transaction_guard;

class ear;
class session;

// Accepts new network connections and creates 'session' instances
// for them.
class listener
{
  app_state & app;
  project_t & project;
  key_store & keys;
  std::shared_ptr<transaction_guard> & guard;

  asio::io_service & ios;
  protocol_role role;
  std::vector<host_port_pair> addresses;

  // FIXME: Netxx::Timeout timeout;

public:
  listener(app_state & app,
           project_t & project,
           key_store & keys,
           std::shared_ptr<transaction_guard> &guard,
           asio::io_service & ios,
           protocol_role role,
           std::vector<host_port_pair> && addresses);

  void start_listening();
  void stop_listening();
  bool is_listening();

  friend class ear;

private:
  std::vector<std::shared_ptr<ear> > open_ears;
  std::set<std::shared_ptr<session> > open_sessions;

  void start_acceptor_for(asio::ip::tcp::endpoint const & ep);
  void start_acceptors_for(asio::ip::tcp::resolver::iterator ity);
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
