// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __MAKE_SERVER_HH__
#define __MAKE_SERVER_HH__

#include <list>

#include <boost/shared_ptr.hpp>

#include "netxx/streamserver.h"

class utf8;

boost::shared_ptr<Netxx::StreamServer>
make_server(std::list<utf8> const & addresses,
            Netxx::port_type default_port,
            Netxx::Timeout timeout,
            bool use_ipv6,
            Netxx::Address & addr);

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
