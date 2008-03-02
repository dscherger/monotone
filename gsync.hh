#ifndef __GSYNC_HH__
#define __GSYNC_HH__

// Copyright (C) 2008 Markus Schiltknecht <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include <set>

#include "vocab.hh"

struct uri;
struct globish;
class lua_hooks;
class database;

class
channel
{
public:
  virtual
  void inquire_about_revs(std::set<revision_id> const & query_set,
                          std::set<revision_id> & theirs) const = 0;
  virtual
  void push_rev(revision_id const & rid) const = 0;
  virtual ~channel() {}
};

extern void
run_gsync_protocol(lua_hooks & lua, database & db, channel const & ch,
                   globish const & include_pattern,
                   globish const & exclude_pattern);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __GSYNC_HH__
