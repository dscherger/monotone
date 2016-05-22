// Copyright (C) 2014-2016 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../base.hh"

#include <functional>
#include <string>
#include <sstream>

#include <asio.hpp>

#include "../sanity.hh"
#include "../origin_type.hh"
#include "../uri.hh"
#include "../globish.hh"
#include "connection_info.hh"
#include "stream.hh"

#include "../platform.hh"

using std::string;
using std::stringstream;
using std::vector;
using asio::ip::tcp;

abstract_stream * abstract_stream::create_stream_for(
  netsync_connection_info::Client const & client,
  asio::io_service & ios)
{
  if (client.get_use_argv())
    {
      vector<string> args = client.get_argv();
      I(!args.empty());

      return unix_local_stream::create_stream_for(ios, args, "file");
    }
  else
    {
      string host(client.get_uri().host);
      unsigned short port = client.get_port();

      I(!host.empty());

      tcp::resolver resolver(ios);
      tcp::resolver::query query(host, lexical_cast<string>(port));

      tcp_stream * new_stream = new tcp_stream(ios);

      asio::error_code ec;
      tcp::resolver::iterator ity = resolver.resolve(query, ec);
      E(!ec, origin::network, F("name resolution failure for %s: %s")
        % client.get_uri().host % ec.message());

      new_stream->connect_to_one_of(ity);
      return new_stream;
    }
}

abstract_stream::abstract_stream()
  : connected(false)
{
}

void abstract_stream::set_conn_handler(std::function<void()> conn_handler)
{
  this->conn_handler = conn_handler;

  // If already connected, call the connection handler right away. (Usually
  // the case with pipes.)
  if (is_connected() && conn_handler)
    conn_handler();
}

bool abstract_stream::is_connected() const
{
  return connected;
}

string const & abstract_stream::get_remote_name() const
{
  I(connected);
  return remote_name;
}


void tcp_stream::connect_to_one_of(tcp::resolver::iterator ity)
{
  tcp::endpoint ep(*ity);
  socket.async_connect(ep, [this, ity](asio::error_code const & err) {
      handle_connect(ity, err);
    });
}

void tcp_stream::handle_connect(tcp::resolver::iterator ity,
								asio::error_code const & err)
{
  I(!connected);
  if (!err)
    {
      L(FL("Successfully connected to: %s") % ity->endpoint());
      connection_established();
    }
  else
    {
      tcp::endpoint const & failed_ep = ity->endpoint();

      // Advance to the next possible endpoint. Bail out, if there is none.
      E(++ity != tcp::resolver::iterator(), origin::network,
        F("Unable to connect to %s: %s")
        % failed_ep % err.message());

      W(F("Failed connecting to %s: %s. Now trying via %s.")
        % failed_ep % err.message() % ity->endpoint());
      connect_to_one_of(ity);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
