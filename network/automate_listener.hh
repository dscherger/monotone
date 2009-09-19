// Copyright (C) 2008 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __AUTOMATE_LISTENER_HH__
#define __AUTOMATE_LISTENER_HH__

#include "network/listener_base.hh"

class app_state;
class transaction_guard;
class reactor;

class automate_listener : public listener_base
{
  app_state & app;
  boost::shared_ptr<transaction_guard> &guard;
  Netxx::Address addr;
  Netxx::Timeout timeout;
  reactor & react;
public:
  automate_listener(app_state & app,
                    boost::shared_ptr<transaction_guard> & guard,
                    reactor & react,
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
