#include <map>
#include <vector>
#include <set>
#include <boost/shared_ptr.hpp>

#include "sanity.hh"
#include "graph.hh"
#include "safe_map.hh"

using boost::shared_ptr;
using std::string;
using std::vector;
using std::set;
using std::pair;

////////////////////////////////////////////////////////////////////////
// get_reconstruction_path
////////////////////////////////////////////////////////////////////////

void
get_reconstruction_path(std::string const & start,
                        reconstruction_graph const & graph,
                        reconstruction_path & path)
{
  // This function does a breadth-first search from a starting point, until it
  // finds some node that matches an arbitrary condition.  The intended usage
  // is for finding reconstruction paths in a database of deltas -- we start
  // from the node we want to reconstruct, and follow existing deltas outward
  // until we reach a full-text base.  We return the shortest path from
  // 'start' to a base version.
  //
  // The algorithm involves keeping a set of parallel linear paths, starting
  // from 'start', that move forward through the DAG until we hit a base.
  //
  // On each iteration, we extend every active path by one step. If our
  // extension involves a fork, we duplicate the path. If any path
  // contains a cycle, we fault.
  //
  // If, by extending a path C, we enter a node which another path
  // D has already seen, we kill path C. This avoids the possibility of
  // exponential growth in the number of paths due to extensive forking
  // and merging.

  // Long ago, we used to do this with the boost graph library, but it
  // invovled loading too much of the storage graph into memory at any
  // moment. this imperative version only loads the descendents of the
  // reconstruction node, so it much cheaper in terms of memory.

  vector< shared_ptr<reconstruction_path> > live_paths;

  {
    shared_ptr<reconstruction_path> pth0 = shared_ptr<reconstruction_path>(new reconstruction_path());
    pth0->push_back(start);
    live_paths.push_back(pth0);
  }

  shared_ptr<reconstruction_path> selected_path;
  set<string> seen_nodes;

  while (!selected_path)
    {
      vector< shared_ptr<reconstruction_path> > next_paths;

      I(!live_paths.empty());
      for (vector<shared_ptr<reconstruction_path> >::const_iterator i = live_paths.begin();
           i != live_paths.end(); ++i)
        {
          shared_ptr<reconstruction_path> pth = *i;
          string tip = pth->back();

          if (graph.is_base(tip))
            {
              selected_path = pth;
              break;
            }
          else
            {
              // This tip is not a root, so extend the path.
              set<string> next;
              graph.get_next(tip, next);
              I(!next.empty());

              // Replicate the path if there's a fork.
              bool first = true;
              for (set<string>::const_iterator j = next.begin(); j != next.end(); ++j)
                {
                  L(FL("considering %s -> %s") % tip % *j);
                  if (seen_nodes.find(*j) == seen_nodes.end())
                    {
                      shared_ptr<reconstruction_path> pthN;
                      if (first)
                        {
                          pthN = pth;
                          first = false;
                        }
                      else
                        {
                          // NOTE: this is not the first iteration of the loop, and
                          // the first iteration appended one item to pth.  So, we
                          // want to remove one before we use it.  (Why not just
                          // copy every time?  Because that makes this into an
                          // O(n^2) algorithm, in the common case where there is
                          // only one direction to go at each stop.)
                          pthN = shared_ptr<reconstruction_path>(new reconstruction_path(*pth));
                          I(!pthN->empty());
                          pthN->pop_back();
                        }
                      // check for a cycle... not that anything would break if
                      // there were one, but it's nice to let us know we have a bug
                      for (reconstruction_path::const_iterator k = pthN->begin(); k != pthN->end(); ++k)
                        I(*k != *j);
                      pthN->push_back(*j);
                      next_paths.push_back(pthN);
                      seen_nodes.insert(*j);
                    }
                }
            }
        }

      I(selected_path || !next_paths.empty());
      live_paths = next_paths;
    }

  path = *selected_path;
}



////////////////////////////////////////////////////////////////////////
// get_uncommon_ancestors
////////////////////////////////////////////////////////////////////////

// This is a slightly complicated algorithm, but it has to go *very* fast in
// order for "pull" to work tolerably well -- because this must be called once
// for every merge revision.
//
// We're aiming to find, for revs Left and Right:
// 
//  - all revs that are ancestors of Left and not ancestors of Right
//  - all revs that are ancestors of Right and not ancestors of Left
//
// To do this we build two "candidate sets" for Left and Right, then take set
// differences.
//
//        left_candidates \ right_candidates ==> left_uncommon_ancestors
//        right_candidates \ left_candidates ==> right_uncommon_ancestors
//
// The candidate sets for X is a *subset* of the ancestors of X. The goal is
// to make the candidate sets very small, and to stop expanding them rapidly.
//
// To this end, we build frontier sets Left and Right. Each frontier set is
// expanded incrementally. Each expansion adds a rev to the candidate set, and
// if the rev is not yet seen, schedules its parents for the next frontier.
//
// Crucially, each expansion *also* checks the candidate set of the *other*
// side.  So when expanding left_frontier, we have left_node, and we check to
// see if left_node is in right_candidates. If so, then right_frontier has
// already scanned forward past left_node. We then project left_node forward
// through all its ancestors in right_candidates, deleting any nodes we
// encounter in the projection from right_frontier.
//
// This should cause right_frontier to collapse rapidly. Naturally we also
// project nodes from right_frontier through left_candidates, to purge common
// ancestry from left_frontier.

static bool
member(revision_id const & i, 
       set<revision_id> const & s)
{
  return s.find(i) != s.end();
}

static void
trim_search_frontier(ancestry_map const & child_to_parent_map,
                     revision_id const & a_rev,
                     set<revision_id> const & b_candidates,
                     set<revision_id> & a_candidates,
                     set<revision_id> & b_frontier)
{
  // b_frontier must be a subset of b_candidates (in general
  // b_frontier is much smaller than b_candidates, so we do a loop instead
  // of using the equivalent STL operation).
  for (set<revision_id>::const_iterator i = b_frontier.begin();
       i != b_frontier.end(); ++i)
    I(member(*i, b_candidates));
  I(member(a_rev, b_candidates));

  // We just do a straightforward enumeration of 'rev's ancestry set, keeping
  // the usual 'seen' set, with the only wrinkles being:
  //   - we restrict ourselves to nodes that are b_candidates (i.e., our search
  //     is bounded by how far the other search has gotten)
  //   - we erase any nodes we find from the b_frontier.
  //   - we add any nodes we find to the a_candidates set, so they will cancel
  //     out the corresponding b_candidates
  set<revision_id> seen;
  vector<revision_id> frontier;
  frontier.push_back(a_rev);
  while (!frontier.empty())
    {
      revision_id r = frontier.back();
      frontier.pop_back();
      if (member(r, seen))
        continue;
      if (!member(r, b_candidates))
        continue;
      safe_insert(seen, r);

      a_candidates.insert(r);
      b_frontier.erase(r);
      
      typedef ancestry_map::const_iterator ci;
      pair<ci,ci> range = child_to_parent_map.equal_range(r);
      for (ci j = range.first; j != range.second; ++j)
        {
          I(j->first == r);
          frontier.push_back(j->second);
        }
    }
}

static void
expand_uncommon_ancestor_frontier(ancestry_map const & child_to_parent_map,
                                  set<revision_id> & a_candidates,                                  
                                  set<revision_id> const & a_frontier,
                                  set<revision_id> & a_next_frontier,
                                  set<revision_id> const & b_candidates,
                                  set<revision_id> & b_frontier)
{
  for (set<revision_id>::const_iterator i = a_frontier.begin();
       i != a_frontier.end(); ++i)
    {
      revision_id const & rev = *i;

      // We insert revisions to the candidate set _before_ processing them
      // fully; this makes trim_search_frontier simpler.  It also avoids a
      // possible failure case where a node is on the frontier, gets trimmed
      // by the other side, and then it gets put on the frontier again by
      // another path, and we have no record that we ever saw it:
      //       *
      //      /|\                      .
      //     1 | \                     . These dots brought to you by
      //     | 3  4                    . the Committee to Shut Up the
      //     2 |   \                   . C PreProcessor (CTSUCPP).
      //      \|    \                  .
      //       A     B
      // It could happen that A's frontier is {1, *}, and seen set is {2, 3},
      // when B's frontier reaches *, and trims it from A's frontier.  Then
      // A's frontier becomes {1}, with seen set {2, 3}, so it immediately
      // re-loads *.  Maybe this would be okay because * was put into B's
      // candidate set, but really, just making the candidate set a superset
      // of the frontier is easier to think about.
      I(member(rev, a_candidates));

      // Okay, now check to see if we've wandered into the other side's
      // ancestry set.
      if (member(rev, b_candidates))
        {
          // Ah, we have!  So we kill off any of our ancestors that b is still
          // working on exploring -- that exploration is useless.
          trim_search_frontier(child_to_parent_map, rev,
                               b_candidates, 
                               a_candidates, b_frontier);
          // And we should stop exploring this path ourselves.
          continue;
        }

      // If we got this far, then this is a node that (as far as we know) is
      // only in a's ancestor set.  So we load its parents and keep
      // exploring.
      typedef ancestry_map::const_iterator ci;
      pair<ci,ci> range = child_to_parent_map.equal_range(rev);
      for (ci j = range.first; j != range.second; ++j)
        {
          I(j->first == rev);
          // Again, we prune already-explored paths now, not when we process
          // the resulting frontier.
          if (!member(j->second, a_candidates))
            {
              safe_insert(a_candidates, j->second);
              // This revision might already be on the next frontier -- that's
              // okay.  Thus, .insert instead of safe_insert.
              a_next_frontier.insert(j->second);
            }
        }
    }
}

void
get_uncommon_ancestors(revision_id const & left_rid, revision_id const & right_rid,
                       ancestry_map const & child_to_parent_map,
                       std::set<revision_id> & left_uncommon_ancs,
                       std::set<revision_id> & right_uncommon_ancs)
{
  set<revision_id> left_candidates;
  set<revision_id> right_candidates;

  set<revision_id> left_frontier;
  set<revision_id> right_frontier;

  left_uncommon_ancs.clear();
  right_uncommon_ancs.clear();

  left_candidates.insert(left_rid);
  right_candidates.insert(right_rid);

  left_frontier.insert(left_rid);
  right_frontier.insert(right_rid);

  while (! (left_frontier.empty() && right_frontier.empty()))
    {
      DUMP(left_frontier);
      DUMP(right_frontier);
      set<revision_id> left_next_frontier;
      set<revision_id> right_next_frontier;
      expand_uncommon_ancestor_frontier(child_to_parent_map, 
                                        left_candidates, left_frontier, left_next_frontier, 
                                        right_candidates, right_frontier);
      DUMP(left_next_frontier);
      left_frontier = left_next_frontier;

      expand_uncommon_ancestor_frontier(child_to_parent_map, 
                                        right_candidates, right_frontier, right_next_frontier, 
                                        left_candidates, left_frontier);
      DUMP(right_next_frontier);
      right_frontier = right_next_frontier;
    }

  set_difference(left_candidates.begin(), left_candidates.end(),
                 right_candidates.begin(), right_candidates.end(),
                 inserter(left_uncommon_ancs, left_uncommon_ancs.begin()));

  set_difference(right_candidates.begin(), right_candidates.end(),
                 left_candidates.begin(), left_candidates.end(),
                 inserter(right_uncommon_ancs, right_uncommon_ancs.begin()));
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "randomizer.hh"

using namespace randomizer;

double const merge_node_freq = 0.2;
double const skip_up_freq = 0.5;

static revision_id
pick_node_from_set(set<revision_id> const & heads)
{
  I(!heads.empty());
  size_t which_start = uniform(heads.size());
  set<revision_id>::const_iterator i = heads.begin();
  for (size_t j = 0; j != which_start; ++j)
    ++i;
  return *i;
}

static revision_id
pick_node_or_ancestor(set<revision_id> const & heads, ancestry_map const & child_to_parent_map)
{
  revision_id rev = pick_node_from_set(heads);
  // now we recurse up from this starting point
  while (bernoulli(skip_up_freq))
    {
      typedef ancestry_map::const_iterator ci;
      pair<ci,ci> range = child_to_parent_map.equal_range(rev);
      if (range.first == range.second)
        break;
      ci second = range.first;
      ++second;
      if (second == range.second)
        // there was only one parent
        rev = range.first->second;
      else
        {
          // there are two parents, pick one randomly
          if (flip())
            rev = range.first->second;
          else
            rev = second->second;
        }
    }
  return rev;
}

static void
make_random_graph(size_t num_nodes,
                  ancestry_map & child_to_parent_map, vector<revision_id> & nodes)
{
  set<revision_id> heads;

  for (size_t i = 0; i != num_nodes; ++i)
    {
      revision_id new_rid = revision_id(fake_id());
      nodes.push_back(new_rid);
      set<revision_id> parents;
      if (heads.empty())
        parents.insert(revision_id());
      else if (bernoulli(merge_node_freq) && heads.size() > 1)
        {
          // maybe we'll pick the same node twice and end up not doing a
          // merge, oh well...
          parents.insert(pick_node_from_set(heads));
          parents.insert(pick_node_from_set(heads));
        }
      else
        {
          parents.insert(pick_node_or_ancestor(heads, child_to_parent_map));
        }
      for (set<revision_id>::const_iterator j = parents.begin();
           j != parents.end(); ++j)
        {
          heads.erase(*j);
          child_to_parent_map.insert(std::make_pair(new_rid, *j));
        }
      safe_insert(heads, new_rid);
    }
}

static void
get_all_ancestors(revision_id const & start, ancestry_map const & child_to_parent_map,
                  set<revision_id> & ancestors)
{
  ancestors.clear();
  vector<revision_id> frontier;
  frontier.push_back(start);
  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      if (member(rid, ancestors))
        continue;
      safe_insert(ancestors, rid);
      typedef ancestry_map::const_iterator ci;
      pair<ci,ci> range = child_to_parent_map.equal_range(rid);
      for (ci i = range.first; i != range.second; ++i)
        frontier.push_back(i->second);
    }
}

template <typename T> void
dump(set<T> const & obj, string & out)
{
  out = (FL("set with %s members:\n") % obj.size()).str();
  for (typename set<T>::const_iterator i = obj.begin(); i != obj.end(); ++i)
    {
      string subobj_str;
      dump(*i, subobj_str);
      out += "  " + subobj_str + "\n";
    }
}

static void
run_a_get_uncommon_ancestors_random_test(size_t num_nodes, size_t iterations)
{
  ancestry_map child_to_parent_map;
  vector<revision_id> nodes;
  make_random_graph(num_nodes, child_to_parent_map, nodes);
  for (size_t i = 0; i != iterations; ++i)
    {
      L(FL("get_uncommon_ancestors: random test %s-%s") % num_nodes % i);
      revision_id left = idx(nodes, uniform(nodes.size()));
      revision_id right = idx(nodes, uniform(nodes.size()));
      set<revision_id> true_left_ancestors, true_right_ancestors;
      get_all_ancestors(left, child_to_parent_map, true_left_ancestors);
      get_all_ancestors(right, child_to_parent_map, true_right_ancestors);
      set<revision_id> true_left_uncommon_ancestors, true_right_uncommon_ancestors;
      MM(true_left_uncommon_ancestors);
      MM(true_right_uncommon_ancestors);
      set_difference(true_left_ancestors.begin(), true_left_ancestors.end(),
                     true_right_ancestors.begin(), true_right_ancestors.end(),
                     inserter(true_left_uncommon_ancestors, true_left_uncommon_ancestors.begin()));
      set_difference(true_right_ancestors.begin(), true_right_ancestors.end(),
                     true_left_ancestors.begin(), true_left_ancestors.end(),
                     inserter(true_right_uncommon_ancestors, true_right_uncommon_ancestors.begin()));
      
      set<revision_id> calculated_left_uncommon_ancestors, calculated_right_uncommon_ancestors;
      MM(calculated_left_uncommon_ancestors);
      MM(calculated_right_uncommon_ancestors);
      get_uncommon_ancestors(left, right, child_to_parent_map,
                             calculated_left_uncommon_ancestors,
                             calculated_right_uncommon_ancestors);
      I(calculated_left_uncommon_ancestors == true_left_uncommon_ancestors);
      I(calculated_right_uncommon_ancestors == true_right_uncommon_ancestors);
    }
}

static void
test_get_uncommon_ancestors_randomly()
{
  run_a_get_uncommon_ancestors_random_test(100, 100);
  run_a_get_uncommon_ancestors_random_test(1000, 100);
  run_a_get_uncommon_ancestors_random_test(10000, 1000);
}

void
add_graph_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_get_uncommon_ancestors_randomly));
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
