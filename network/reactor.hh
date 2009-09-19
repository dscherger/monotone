// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __REACTOR_HH__
#define __REACTOR_HH__

#include <map>
#include <set>

#include <boost/shared_ptr.hpp>

#include "netxx_pipe.hh"

class reactable;
class transaction_guard;

class reactor
{
  bool have_pipe;
  Netxx::Timeout forever, timeout, instant;
  bool can_have_timeout;

  Netxx::PipeCompatibleProbe probe;
  std::set<boost::shared_ptr<reactable> > items;

  std::map<Netxx::socket_type, boost::shared_ptr<reactable> > lookup;

  bool readying;
  int have_armed;
  void ready_for_io(boost::shared_ptr<reactable> item,
                    transaction_guard & guard);
public:
  reactor();
  void add(boost::shared_ptr<reactable> item,
           transaction_guard & guard);
  void remove(boost::shared_ptr<reactable> item);

  int size() const;

  void ready(transaction_guard & guard);
  bool do_io();
  void prune();
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
