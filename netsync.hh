// Copyright (C) 2005 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __NETSYNC_HH__
#define __NETSYNC_HH__

#include "netcmd.hh"
#include "network/connection_info.hh"

struct server_initiated_sync_request
{
  std::string address;
  std::string include;
  std::string exclude;
  protocol_role role;
};

void run_netsync_protocol(app_state & app,
                          options & opts, lua_hooks & lua,
                          project_t & project, key_store & keys,
                          protocol_voice voice,
                          protocol_role role,
                          shared_conn_info & info);

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
