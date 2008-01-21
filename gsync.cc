// Copyright (C) 2007 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "app_state.hh"
#include "database.hh"
#include "globish.hh"
#include "http_client.hh"
#include "json_io.hh"
#include "json_msgs.hh"
#include "revision.hh"
#include "sanity.hh"
#include "lexical_cast.hh"
#include "uri.hh"

//
// Gsync is the "new new" synchronization system for monotone,
// obsoleting netsync, the "old new" system that was developed back in
// the bad old days of unstructured sets. With any luck it'll be the
// last synchronization system. We'll see.
//
// The "g" in gsync stands for "graph". It is an algorithm quite
// strongly based on DAGs. It does not have much to do with
// unstructured sets. There are no merkle tries either. So long old
// friends.
//
// It is also significantly simpler than netsync.
//
// The algorithm consists of three types of client-initiated exchange:
// introduction, inquiry and playback. There is no coupling between
// these steps. They can be done in any order, interrupted at any
// time, repeated any number of times, etc. Like netsync, they are all
// idempotent, informative actions.
//
// In the introduction step, the client asks the server to describe
// its public key, branches, etc. such that the client knows what sort
// of material it can ask for in an authenticated fashion. 
//
// In the inquiry step, the client sends a set of revids to the server
// and asks which of them the server has. The server responds with the
// subset that it has. The goal in this step is for the client to
// figure out how much of history client and server have in
// common. Crucially, the client does not need to enumerate all of its
// revids this way: every time it learns of a rev that the server has,
// it also knows that the server has all the ancestors of that rev;
// and if it learns of a rev the server *doesn't* have, it also knows
// that the server doesn't have any of the descendents of that rev. It
// selects revids in essentially random order (lexicographic by
// hash). This is a quasi-randomized-ish algorithm and it converges
// very fast. Once the client determines a shared historical core DAG,
// it calculates the graph frontier of that core.
// 
// Depending on the mode (push, pull, or sync) the playback phase
// then involves one or both of the following:
//
//   - Sending a request to the server to play back from the frontier.
//     The frontier to playback from is sent along with this
//     request. It's stateless.
//
//   - Initiating and playing forward from the frontier on the client
//     side. Similarly, these are stateless "submit" commands.
//


using std::make_pair;
using std::map;
using std::min;
using std::set;
using std::string;
using std::pair;

using json_io::json_value_t;
using boost::lexical_cast;

/////////////////////////////////////////////////////////////////////
// core logic of gsync algorithm
/////////////////////////////////////////////////////////////////////

static inline void
do_set_union(set<revision_id> const & a,
	     set<revision_id> const & b,
	     set<revision_id> & c)
{
  c.clear();
  set_union(a.begin(), a.end(), b.begin(), b.end(), inserter(c, c.begin()));
}

static inline void
do_set_difference(set<revision_id> const & a,
		  set<revision_id> const & b,
		  set<revision_id> & c)
{
  c.clear();
  set_difference(a.begin(), a.end(), b.begin(), b.end(), inserter(c, c.begin()));
}


static void 
inquire_about_revs(http_client & h,
		   set<revision_id> const & query_set,
		   set<revision_id> & theirs)
{
  theirs.clear();  
  json_value_t query = encode_msg_inquire(query_set);  
  json_value_t response = h.transact_json(query);
  E(decode_msg_confirm(response, theirs),
    F("received unexpected reply to 'inquire' message"));
}

static void
determine_common_core(http_client & h,
		      set<revision_id> const & our_revs,
		      rev_ancestry_map const & child_to_parent_map,
		      rev_ancestry_map const & parent_to_child_map,
		      set<revision_id> & common_core)
{
  common_core.clear();
  set<revision_id> unknown_revs = our_revs;
  size_t pass = 0;

  while (!unknown_revs.empty())
    {
      ++pass;
      set<revision_id> query_revs;
      
      // Bite off a chunk of the remaining unknowns to ask about.
      set<revision_id>::const_iterator r = unknown_revs.begin();
      for (size_t i = 0; 
	   i < constants::gsync_max_probe_set_size && r != unknown_revs.end(); 
	   ++i, ++r)
	{
	  query_revs.insert(*r);
	}

      // Ask what they have of that chunk, form closures of the
      // positive and negative sets on our side.
      set<revision_id> revs_present, present_ancs, present_closure;
      set<revision_id> revs_absent, absent_descs, absent_closure;

      inquire_about_revs(h, query_revs, revs_present);
      do_set_difference(query_revs, revs_present, revs_absent);

      L(FL("pass #%d: inquired about %d revs, they have %d of them, missing %d of them") 
	% pass
	% query_revs.size() 
	% revs_present.size()
	% revs_absent.size());

      get_all_ancestors(revs_present, child_to_parent_map, present_ancs);
      do_set_union(revs_present, present_ancs, present_closure);

      // FIXME: "ancestors" is a misnomer; it's a graph-closure calculation...
      get_all_ancestors(revs_absent, parent_to_child_map, absent_descs);
      do_set_union(revs_absent, absent_descs, absent_closure);

      // Update the set we do not yet know about.
      set<revision_id> new_unknown;
      L(FL("pass #%d: unknown set initially: %d nodes") % pass % unknown_revs.size());

      do_set_difference(unknown_revs, present_closure, new_unknown);
      unknown_revs = new_unknown;
      L(FL("pass #%d: unknown set after removing %d-entry present closure: %d nodes") 
	% pass % present_closure.size() % unknown_revs.size());

      do_set_difference(unknown_revs, absent_closure, new_unknown);
      unknown_revs = new_unknown;
      L(FL("pass #%d: unknown set after removing %d-entry absent closure: %d nodes") 
	% pass % absent_closure.size() % unknown_revs.size());

      // Update our total knowledge about them.
      common_core.insert(present_closure.begin(), present_closure.end());
    }
}

static void
invert_ancestry(rev_ancestry_map const & in,
		rev_ancestry_map & out)
{
  out.clear();
  for (rev_ancestry_map::const_iterator i = in.begin();
       i != in.end(); i++)
    out.insert(make_pair(i->second, i->first));
}

static void
do_missing_playback(http_client & h,
		    app_state & app, 
		    set<revision_id> const & core_frontier, 
		    rev_ancestry_map const & child_to_parent_map)
{
  rev_ancestry_map parent_to_child_map;
  invert_ancestry(child_to_parent_map, parent_to_child_map);
}


static void 
request_missing_playback(http_client & h,
			 app_state & app,
			 set<revision_id> const & core_frontier)
{
  
}

void
run_gsync_protocol(utf8 const & addr,
		   globish const & include_pattern,
		   globish const & exclude_pattern,
		   app_state & app)
{
  uri u;
  parse_uri(addr(), u);
  http_client h(app, u, include_pattern, exclude_pattern);
  
  bool pushing = true, pulling = true;

  rev_ancestry_map parent_to_child_map, child_to_parent_map;
  app.db.get_revision_ancestry(parent_to_child_map);
  invert_ancestry(parent_to_child_map, child_to_parent_map);

  set<revision_id> our_revs;
  for (rev_ancestry_map::const_iterator i = child_to_parent_map.begin(); 
       i != child_to_parent_map.end(); ++i)
    {
      if (!i->first.inner()().empty())
	our_revs.insert(i->first);
      if (!i->second.inner()().empty())
	our_revs.insert(i->second);
    }

  set<revision_id> common_core;
  determine_common_core(h, our_revs, child_to_parent_map, parent_to_child_map, common_core);

  set<revision_id> ours_alone;
  do_set_difference(our_revs, common_core, ours_alone);
  P(F("revs to send: %d") % ours_alone.size());

  set<revision_id> core_frontier = common_core;
  erase_ancestors(common_core, app);

  if (pushing)
    do_missing_playback(h, app, core_frontier, child_to_parent_map);

  if (pulling)
    request_missing_playback(h, app, core_frontier);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
