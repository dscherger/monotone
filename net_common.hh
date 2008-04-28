#ifndef __NET_COMMON_HH__
#define __NET_COMMON_HH__

// Copyright (C) 2008 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include <list>

#include "netxx/types.h"

namespace Netxx {
  class Address;
  class Timeout;
  class StreamBase;
}

struct utf8;
struct options;
struct netsync_connection_info;
class lua_hooks;


// This just covers helper routines that are shared across networking
// facilities (netsync and gsync). When we retire netsync, we can retire
// this file and shift the code into http_client or gsync.

void
add_address_names(Netxx::Address & addr,
                  std::list<utf8> const & addresses,
                  Netxx::port_type default_port);

boost::shared_ptr<Netxx::StreamBase>
build_stream_to_server(options & opts, lua_hooks & lua,
                       netsync_connection_info info,
                       Netxx::port_type default_port,
                       Netxx::Timeout timeout);


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __NET_COMMON_HH__
