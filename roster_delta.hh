// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __ROSTER_DELTA_HH__
#define __ROSTER_DELTA_HH__

// This file contains "diff"/"patch" code that operates directly on rosters
// (with their associated markings).

#include "rev_types.hh"

// You should pass a null for 'reverse_cs' if this is a merge edge, because in
// that case even untouched nodes can have different mark sets.
// Note that 'reverse_cs' is the cset that starts with 'to' and gives 'from',
// because that's what's available in the only place this is used.
void
delta_rosters(roster_t const & from, marking_map const & from_markings,
              roster_t const & to, marking_map const & to_markings,
              roster_delta & del,
              cset const * reverse_cs);

// mutates its arguments
void
apply_roster_delta(roster_delta const & del,
                   roster_t & roster, marking_map & markings);

bool
try_get_markings_from_roster_delta(roster_delta const & del,
                                   node_id const & nid,
                                   marking_t & markings);

// See the comment on this function's body for a description of its api.
bool
try_get_content_from_roster_delta(roster_delta const & del,
                                  node_id const & nid,
                                  file_id & content);

#endif // __ROSTER_DELTA_HH__


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
