#ifndef __GRAPH__HH__
#define __GRAPH__HH__

// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file contains generic graph algorithms.  They are split out from any
// particular concrete graph (e.g., the revision graph, the delta storage
// graphs) to easy re-use, and to make them easier to test on their own.  We
// have a number of graph algorithms that are not genericized in this way
// (e.g., in revision.cc); FIXME it would be good to move them in here as
// opportunity permits.

#include <map>
#include <string>
#include <set>
#include <vector>
#include "vocab.hh"

struct reconstruction_graph
{
  virtual bool is_base(std::string const & node) const = 0;
  virtual void get_next(std::string const & from, std::set<std::string> & next) const = 0;
  virtual ~reconstruction_graph() {};
};

typedef std::vector<std::string> reconstruction_path;

void
get_reconstruction_path(std::string const & start,
                        reconstruction_graph const & graph,
                        reconstruction_path & path);

typedef std::multimap<revision_id, revision_id> ancestry_map;

void
get_uncommon_ancestors(revision_id const & left_rid, revision_id const & right_rid,
                       ancestry_map const & child_to_parent_map,
                       std::set<revision_id> & left_uncommon_ancs,
                       std::set<revision_id> & right_uncommon_ancs);
                       

#endif // __GRAPH__HH__


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

