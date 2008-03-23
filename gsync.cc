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

#include "constants.hh"
#include "cset.hh"
#include "database.hh"
#include "globish.hh"
#include "graph.hh"
#include "gsync.hh"
#include "revision.hh"
#include "sanity.hh"
#include "ui.hh"
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
using std::vector;
using std::string;
using std::pair;

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
determine_common_core(channel const & ch,
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

      ch.inquire_about_revs(query_revs, revs_present);
      do_set_difference(query_revs, revs_present, revs_absent);

      L(FL("pass #%d: inquired about %d revs, they have %d of them, missing %d of them")
        % pass
        % query_revs.size()
        % revs_present.size()
        % revs_absent.size());

      // FIXME: "ancestors" is a misnomer; it's a graph-closure calculation...

      get_all_ancestors(revs_present, child_to_parent_map, present_ancs);
      do_set_union(revs_present, present_ancs, present_closure);

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
push_revs(database & db,
          channel const & ch,
          vector<revision_id> const & outbound_revs,
          bool const dryrun)
{
  ticker rev_ticker(N_("revisions"), "R", 1);
  ticker file_ticker(N_("files"), "f", 1);

  rev_ticker.set_total(outbound_revs.size());

  transaction_guard guard(db);

  for (vector<revision_id>::const_iterator i = outbound_revs.begin();
       i != outbound_revs.end(); ++i)
    {
      revision_t rev;
      db.get_revision(*i, rev);
      ++rev_ticker;

      for (edge_map::const_iterator e = rev.edges.begin();
           e != rev.edges.end(); ++e)
        {
          cset const & cs = edge_changes(e);
          for (map<file_path, file_id>::const_iterator
                 f = cs.files_added.begin(); f != cs.files_added.end(); ++f)
            {
              file_data data;
              db.get_file_version(f->second, data);
              if (!dryrun)
                ch.push_file_data(f->second, data);
              ++file_ticker;
            }

          for (map<file_path, pair<file_id, file_id> >::const_iterator
                 f = cs.deltas_applied.begin(); f != cs.deltas_applied.end(); ++f)
            {
              file_delta delta;
              db.get_arbitrary_file_delta(f->second.first, f->second.second, delta);
              if (!dryrun)
                ch.push_file_delta(f->second.first, f->second.second, delta);
              ++file_ticker;
            }
        }

      if (!dryrun)
        ch.push_rev(*i, rev);
    }

  if (!dryrun)
    guard.commit();
}

static void
pull_revs(database & db,
          channel const & ch,
          vector<revision_id> const & inbound_revs,
          bool const dryrun)
{
  ticker rev_ticker(N_("revisions"), "R", 1);
  ticker file_ticker(N_("files"), "f", 1);

  rev_ticker.set_total(inbound_revs.size());

  transaction_guard guard(db);

  for (vector<revision_id>::const_iterator i = inbound_revs.begin();
       i != inbound_revs.end(); ++i)
    {
      revision_t rev;
      ch.pull_rev(*i, rev);
      ++rev_ticker;

      transaction_guard guard(db);

      for (edge_map::const_iterator e = rev.edges.begin();
           e != rev.edges.end(); ++e)
        {
          cset const & cs = edge_changes(e);
          for (map<file_path, file_id>::const_iterator
                 f = cs.files_added.begin(); f != cs.files_added.end(); ++f)
            {
              file_data data;
              ch.pull_file_data(f->second, data);
              if (!dryrun)
                db.put_file(f->second, data);
              ++file_ticker;
            }

          for (map<file_path, pair<file_id, file_id> >::const_iterator
                 f = cs.deltas_applied.begin(); f != cs.deltas_applied.end(); ++f)
            {
              file_delta delta;
              ch.pull_file_delta(f->second.first, f->second.second, delta);
              if (!dryrun)
                db.put_file_version(f->second.first, f->second.second, delta);
              ++file_ticker;
            }
        }

      if (!dryrun)
        db.put_revision(*i, rev);
    }

  if (!dryrun)
    guard.commit();
}

void
run_gsync_protocol(lua_hooks & lua, database & db, channel const & ch,
                   globish const & include_pattern, // FIXME: use this pattern
                   globish const & exclude_pattern, // FIXME: use this pattern
                   bool const dryrun)
{
  bool pushing = true, pulling = true;

  rev_ancestry_map parent_to_child_map, child_to_parent_map;
  db.get_revision_ancestry(parent_to_child_map);
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

  set<revision_id> common_revs;
  determine_common_core(ch, our_revs, child_to_parent_map, parent_to_child_map, common_revs);

  P(F("%d common revisions") % common_revs.size());

  set<revision_id> core_frontier = common_revs;

  if (core_frontier.empty())
    core_frontier.insert(revision_id());
  else
    erase_ancestors(db, core_frontier);

  P(F("%d frontier revisions") % core_frontier.size());

  set<revision_id> outbound_set;
  do_set_difference(our_revs, common_revs, outbound_set);
  vector<revision_id> outbound_revs, inbound_revs;
  toposort(db, outbound_set, outbound_revs);

  P(F("%d outbound revisions") % outbound_revs.size());

  ch.get_descendants(core_frontier, inbound_revs);

  P(F("%d inbound revisions") % inbound_revs.size());

  // gsync is a request/response protocol where a "client" sends requests to
  // a "server" and then receives responses from the "server".  the client
  // will first push any unique revs that only it has and the server will
  // then push back any unique revs that only it has. both sides push revs
  // from the core frontier in ancestry order, so that parent revisions are
  // always received before child revisions.

  if (pushing)
    push_revs(db, ch, outbound_revs, dryrun);

  if (pulling)
    pull_revs(db, ch, inbound_revs, dryrun);
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

class test_channel
  : public channel
{
  set<revision_id> & theirs;
public:
  test_channel(set<revision_id> & theirs)
    : theirs(theirs)
  { };

  void inquire_about_revs(set<revision_id> const & query_set,
                          set<revision_id> & result) const
  {
    result.clear();
    for (set<revision_id>::const_iterator i = query_set.begin();
         i != query_set.end(); ++i)
      if (theirs.find(*i) != theirs.end())
        result.insert(*i);
  };

  void get_descendants(std::set<revision_id> const & common_revs,
                       std::vector<revision_id> & inbound_revs) const
  {
    I(false);
  }

  void push_file_data(file_id const & id,
                              file_data const & data) const
  {
  }

  void push_file_delta(file_id const & old_id,
                               file_id const & new_id,
                               file_delta const & delta) const
  {
  }

  void push_rev(revision_id const & rid, revision_t const & rev) const
  {
  }

  void pull_rev(revision_id const & rid, revision_t & rev) const
  {
  }

  void pull_file_data(file_id const & id,
                              file_data & data) const
  {
  }

  void pull_file_delta(file_id const & old_id,
                       file_id const & new_id,
                       file_delta & delta) const
  {
  }

};

UNIT_TEST(gsync, gsync_common_core)
{
  L(FL("TEST: begin checking gsync protocol functions"));

  revision_id rid1("0000000000000000000000000000000000000001");
  revision_id rid2("0000000000000000000000000000000000000002");
  revision_id rid3("0000000000000000000000000000000000000003");
  revision_id rid4("0000000000000000000000000000000000000004");
  revision_id rid5("0000000000000000000000000000000000000005");
  revision_id rid6("0000000000000000000000000000000000000006");
  revision_id rid7("0000000000000000000000000000000000000007");
  revision_id rid8("0000000000000000000000000000000000000008");

  // simulate having revisions 1, 2, 3, 5, 6 and 8 locally
  set<revision_id> ours;
  ours.insert(rid1);
  ours.insert(rid2);
  ours.insert(rid3);
  ours.insert(rid5);
  ours.insert(rid6);
  ours.insert(rid8);

  // prepare an ancestry map
  rev_ancestry_map parent_to_child_map, child_to_parent_map;
  parent_to_child_map.insert(make_pair(rid1, rid2));
  parent_to_child_map.insert(make_pair(rid1, rid3));
  parent_to_child_map.insert(make_pair(rid2, rid5));
  parent_to_child_map.insert(make_pair(rid3, rid5));
  parent_to_child_map.insert(make_pair(rid5, rid6));
  parent_to_child_map.insert(make_pair(rid5, rid8));
  invert_ancestry(parent_to_child_map, child_to_parent_map);

  // the other side has revisions 1, 2, 4 and 7
  set<revision_id> theirs;
  theirs.insert(rid1);
  theirs.insert(rid2);
  theirs.insert(rid4);
  theirs.insert(rid7);

  // setup the test channel and determine the common core
  test_channel ch(theirs);
  set<revision_id> common_core;
  determine_common_core(ch, ours, child_to_parent_map, parent_to_child_map, common_core);

  I(common_core.size() == 2);
  I(common_core.find(rid1) != common_core.end());
  I(common_core.find(rid2) != common_core.end());
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
