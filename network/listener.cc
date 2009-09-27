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
#include "network/listener.hh"

#include "netxx/sockopt.h"
#include "netxx/stream.h"
#include "netxx/streamserver.h"

#include "lexical_cast.hh"
#include "network/make_server.hh"
#include "network/netsync_session.hh"
#include "network/reactor.hh"
#include "network/session.hh"

using std::list;
using std::string;

using boost::lexical_cast;
using boost::shared_ptr;

listener::listener(app_state & app,
                   project_t & project,
                   key_store & keys,
                   reactor & react,
                   protocol_role role,
                   list<utf8> const & addresses,
                   shared_ptr<transaction_guard> &guard,
                   bool use_ipv6)
  : listener_base(shared_ptr<Netxx::StreamServer>()),
    app(app), project(project), keys(keys),
    react(react), role(role),
    timeout(static_cast<long>(constants::netsync_timeout_seconds)),
    guard(guard),
    addr(use_ipv6)
{
  srv = make_server(addresses, constants::netsync_default_port,
                    timeout, use_ipv6, addr);
}

bool
listener::do_io(Netxx::Probe::ready_type event)
{
  L(FL("accepting new connection on %s : %s")
    % (addr.get_name()?addr.get_name():"") % lexical_cast<string>(addr.get_port()));
  Netxx::Peer client = srv->accept_connection();

  if (!client)
    {
      L(FL("accept() returned a dead client"));
    }
  else
    {
      P(F("accepted new client connection from %s : %s")
        % client.get_address() % lexical_cast<string>(client.get_port()));

      // 'false' here means not to revert changes when the SockOpt
      // goes out of scope.
      Netxx::SockOpt socket_options(client.get_socketfd(), false);
      socket_options.set_non_blocking();

      shared_ptr<Netxx::Stream> str =
        shared_ptr<Netxx::Stream>(new Netxx::Stream(client.get_socketfd(),
                                                    timeout));

      shared_ptr<session> sess(new session(app, project, keys,
                                           server_voice,
                                           lexical_cast<string>(client),
                                           str));
      sess->begin_service();
      I(guard);
      react.add(sess, *guard);
    }
  return true;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
