// Copyright (C) 2008 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include "app_state.hh"
#include "globish.hh"
#include "net_common.hh"
#include "uri.hh"
#include "vocab.hh"

#include <list>
#include <string>
#include <vector>

#include <boost/shared_ptr.hpp>

#include "netxx/address.h"
#include "netxx/stream.h"
#include "netxx/streambase.h"
#include "netxx/timeout.h"
#include "netxx_pipe.hh"


using std::string;
using std::list;
using std::vector;
using boost::shared_ptr;

void 
add_address_names(Netxx::Address & addr,
                  std::list<utf8> const & addresses,
                  Netxx::port_type default_port)
{
  if (addresses.empty())
    addr.add_all_addresses(default_port);
  else
    {
      for (std::list<utf8>::const_iterator it = addresses.begin(); it != addresses.end(); ++it)
        {
          const utf8 & address = *it;
          if (!address().empty())
            {
              size_t l_colon = address().find(':');
              size_t r_colon = address().rfind(':');

              if (l_colon == r_colon && l_colon == 0)
                {
                  // can't be an IPv6 address as there is only one colon
                  // must be a : followed by a port
                  string port_str = address().substr(1);
                  addr.add_all_addresses(std::atoi(port_str.c_str()));
                }
              else
                addr.add_address(address().c_str(), default_port);
            }
        }
    }
}

shared_ptr<Netxx::StreamBase>
build_stream_to_server(app_state & app,
                       uri const & u,
                       globish const & include_pattern,
                       globish const & exclude_pattern,                       
                       Netxx::port_type default_port,
                       Netxx::Timeout timeout)
{
  shared_ptr<Netxx::StreamBase> server;
  vector<string> argv;

  if (app.lua.hook_get_netsync_connect_command(u,
                                               include_pattern,
                                               exclude_pattern,
                                               global_sanity.debug_p(),
                                               argv))
    {
      I(argv.size() > 0);
      string cmd = argv[0];
      argv.erase(argv.begin());
      app.opts.use_transport_auth = app.lua.hook_use_transport_auth(u);
      return shared_ptr<Netxx::StreamBase>
        (new Netxx::PipeStream(cmd, argv));
    }
  else
    {
#ifdef USE_IPV6
      bool use_ipv6=true;
#else
      bool use_ipv6=false;
#endif
      Netxx::Address addr(u.host.c_str(), default_port, use_ipv6);
      return shared_ptr<Netxx::StreamBase>(new Netxx::Stream(addr, timeout));
    }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
