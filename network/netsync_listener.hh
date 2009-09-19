// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __NETSYNC_LISTENER_HH__
#define __NETSYNC_LISTENER_HH__

#include <list>

#include "netcmd.hh"
#include "network/listener_base.hh"
#include "vocab.hh"

//#include <boost/shared_ptr.hpp>

class options;
class lua_hooks;
class key_store;
class project_t;
class reactor;
class transaction_guard;

class listener : public listener_base
{
  options & opts;
  lua_hooks & lua;
  project_t & project;
  key_store & keys;

  reactor & react;

  protocol_role role;
  Netxx::Timeout timeout;

  boost::shared_ptr<transaction_guard> & guard;
  Netxx::Address addr;
public:

  listener(options & opts,
           lua_hooks & lua,
           project_t & project,
           key_store & keys,
           reactor & react,
           protocol_role role,
           std::list<utf8> const & addresses,
           boost::shared_ptr<transaction_guard> &guard,
           bool use_ipv6);

  bool do_io(Netxx::Probe::ready_type event);
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
