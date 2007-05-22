#ifndef __NETSYNC_H__
#define __NETSYNC_H__

// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <vector>

#include "app_state.hh"
#include "network.hh"
#include "vocab.hh"

#include <boost/shared_ptr.hpp>
using boost::shared_ptr;

class netsync;

class netsync_service : public service
{
  shared_ptr<netsync> impl;
  static netsync_service mapped;
  friend class netsync;
  void send(netcmd const & cmd);
  bool can_send() const;
  netsync_service();
  netsync_service(netsync_service const & other);
  netsync_service const & operator=(netsync_service const & other);//I(false)
public:
  enum netsync_op {push, pull, sync};
  netsync_service(netsync_op what,
                  globish const & include,
                  globish const & exclude,
                  app_state & app);
  ~netsync_service();

  service * copy();
  void begin_service();
  void request_service();
  bool can_process();
  state process(transaction_guard & guard);
  state received(netcmd const & cmd, transaction_guard & guard);
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __NETSYNC_H__
