// Copyright (C) 2008 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "network/automate_listener.hh"

#include "netxx/sockopt.h"
#include "netxx/stream.h"
#include "netxx/streamserver.h"

#include "app_state.hh"
#include "constants.hh"
#include "network/automate_session.hh"
#include "network/make_server.hh"
#include "network/reactor.hh"

using std::string;

using boost::lexical_cast;
using boost::shared_ptr;

automate_listener::automate_listener(app_state & app,
                                     boost::shared_ptr<transaction_guard> & guard,
                                     reactor & react,
                                     bool use_ipv6) :
  listener_base(shared_ptr<Netxx::StreamServer>()),
  app(app), guard(guard), addr(use_ipv6),
  timeout(static_cast<long>(constants::netsync_timeout_seconds)),
  react(react)
{
  srv = make_server(app.opts.bind_automate_uris, 0, timeout, use_ipv6, addr);
}
bool automate_listener::do_io(Netxx::Probe::ready_type event)
{
  L(FL("accepting new automate connection on %s : %s")
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
        shared_ptr<Netxx::Stream>
        (new Netxx::Stream(client.get_socketfd(), timeout));

      shared_ptr<reactable> sess(new automate_session(app,
                                                      lexical_cast<string>(client),
                                                      str));
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
