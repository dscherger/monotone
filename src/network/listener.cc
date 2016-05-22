// Copyright (C) 2008, 2014 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2014-2016 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../base.hh"
#include "../lexical_cast.hh"

#include <functional>

#include "listener.hh"
#include "netsync_session.hh"
#include "stream.hh"
#include "session.hh"

using std::make_shared;
using std::unique_ptr;
using std::shared_ptr;
using std::string;
using std::vector;

using boost::lexical_cast;
using asio::ip::tcp;
using asio::ip::address_v4;
using asio::ip::address_v6;


// This is one ear of the listener.  A listener may have multiple ears:
// maybe it's listening on different ports or one ear for IPv4 and the other
// for IPv6.
class ear {
  listener & parent;
  tcp::socket socket;
  tcp::acceptor acceptor;

public:
  typedef boost::shared_ptr<ear> session_ptr;

  ear(listener & p, tcp::endpoint ep)
    : parent(p),
      socket(parent.ios),
      acceptor(parent.ios, ep, true)
  {
    accept_next();
  };

  void close()
  {
    socket.close();
  }

  void drop_session(shared_ptr<session> sess, bool success)
  {
    if (success)
      L(FL("Session terminated gracefully."));
    else
      L(FL("Session aborted."));

    sess->stop();

    // drop all references to the session
    sess->set_term_handler(nullptr);
    parent.open_sessions.erase(sess);

    // The local sess variable should be the only remaining pointer to this
    // session, so the session's destructor will be called.
    I(sess.use_count() == 1);
  }

  void handle_accept(shared_ptr<session> sess, const asio::error_code & err)
  {
    if (!err)
      {
        sess->stream_conn_established();

        P(F("Accepted new client connection from %s")
          % sess->get_stream().get_remote_name());

        parent.open_sessions.insert(sess);
        sess->start();
      }
    else
      W(F("Error binding to %s: %s")
        % acceptor.local_endpoint() % err.message());

    accept_next();
  }

  void accept_next()
  {
    tcp_stream * stream_ptr = new tcp_stream(parent.ios);
    // Note that the unique_ptr passed to the session overtakes ownership of
    // this stream_ptr, while we retain and even use stream_ptr later on to
    // pass the socket to async_accept. However, it is guaranteed that the
    // session below outlives the stream_ptr.
    shared_ptr<session> new_session
      (new session(parent.app, parent.project, parent.keys, parent.guard,
                   unique_ptr<abstract_stream>(stream_ptr),
                   server_voice));

    // keep track of this session
    new_session->set_term_handler([this, new_session](bool success)
      {
        drop_session(new_session, success);
      });

    acceptor.async_accept(stream_ptr->get_socket(),
                          [this, new_session](asio::error_code const & err)
      {
        handle_accept(new_session, err);        
      });
  }
};

listener::listener(app_state & app,
                   project_t & project,
                   key_store & keys,
                   shared_ptr<transaction_guard> &guard,
                   asio::io_service & ios,
                   protocol_role role,
                   vector<host_port_pair> && addresses)
  : app(app), project(project), keys(keys), guard(guard),
    ios(ios), role(role),
    addresses(move(addresses))
  /* FIXME: timeout(static_cast<long>(constants::netsync_timeout_seconds)), */
{
  start_listening();
}

void
listener::start_listening()
{
  string default_port =
    lexical_cast<string>(constants::netsync_default_port);

  tcp::resolver resolver(ios);
  if (addresses.empty())
	{
	  tcp::resolver::query query(default_port /*,
                                                tcp::resolver_query::passive*/);
      start_acceptors_for(resolver.resolve(query));
    }
  else
    {
      for (host_port_pair const & ity : addresses)
        {
	      const string & host_str = ity.first;
	      const string & port_str = ity.second;
          unsigned short port;

          try
            {
	          port = port_str.empty()
                ? constants::netsync_default_port
                : lexical_cast<unsigned short>(port_str);
            }
          catch (boost::bad_lexical_cast &e)
            {
              W(F("Invalid port number: '%s'") % port_str);
              continue;
            }

          if (host_str.empty() || host_str == "*")
            {
              if (!port_str.empty())
                start_acceptor_for(tcp::endpoint(asio::ip::address(), port));
              continue;
            }

	      try
            {
	          asio::error_code ec;
	          address_v4 addr4 = address_v4::from_string(host_str, ec);
	          if (!ec)
                {
                  start_acceptor_for(tcp::endpoint(addr4, port));
                  continue;
                }

	          address_v6 addr6 = address_v6::from_string(host_str, ec);
	          if (!ec)
                {
                  start_acceptor_for(tcp::endpoint(addr6, port));
                  continue;
                }

	          tcp::resolver::query query(host_str,
                                         port_str.empty() ? default_port
                                                          : port_str);
              /*,
                tcp::resolver_query::passive*/
	          start_acceptors_for(resolver.resolve(query));
            }
          catch (asio::system_error & e)
            {
              L(FL("Error looking up host '%s': %s")
                % host_str % e.what());
            }
	    }
    }
}

void
listener::stop_listening()
{
  for (shared_ptr<ear> const & ear : open_ears)
    ear->close();
  open_ears.clear();
}

bool
listener::is_listening()
{
  return !open_ears.empty();
}


void
listener::start_acceptors_for(tcp::resolver::iterator ity)
{
  for ( ; ity != tcp::resolver::iterator(); ++ity)
    start_acceptor_for(ity->endpoint());
}

void
listener::start_acceptor_for(tcp::endpoint const & ep)
{
  try
    {
      open_ears.push_back(shared_ptr<ear>(new ear(*this, ep)));

      if (ep.address() == asio::ip::address())
        P(F("Beginning service on *:%s") % lexical_cast<string>(ep.port()));
      else
        P(F("Beginning service on %s") % ep);
    }
  catch (asio::system_error & e)
    {
      if (ep.address() == asio::ip::address())
        W(F("Failed to open port *:%d for listening: %s")
          % ep.port() % e.what());
      else
        W(F("Failed to open port %s for listening: %s")
          % ep % e.what());
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
