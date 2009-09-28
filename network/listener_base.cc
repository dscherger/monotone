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
#include "network/listener_base.hh"

#include "netxx/streamserver.h"

#include "constants.hh"

using std::string;
using std::vector;

using boost::shared_ptr;

listener_base::listener_base(shared_ptr<Netxx::StreamServer> srv)
  : srv(srv)
{
}
listener_base::~listener_base()
{
}
bool listener_base::timed_out(time_t now) { return false; }
bool listener_base::do_work(transaction_guard & guard) { return true; }
bool listener_base::arm() { return false; }
bool listener_base::can_timeout() { return false; }

string listener_base::name() { return ""; } // FIXME

bool listener_base::is_pipe_pair()
{
  return false;
}
vector<Netxx::socket_type> listener_base::get_sockets()
{
  return srv->get_probe_info()->get_sockets();
}
void listener_base::add_to_probe(Netxx::PipeCompatibleProbe & probe)
{
  if (num_reactables() >= constants::netsync_connection_limit)
    {
      W(F("session limit %d reached, some connections "
          "will be refused") % constants::netsync_connection_limit);
    }
  else
    {
      probe.add(*srv);
    }
}
void listener_base::remove_from_probe(Netxx::PipeCompatibleProbe & probe)
{
  probe.remove(*srv);
}
