#include <boost/shared_ptr.hpp>

#include "sanity.hh"
#include "graph.hh"

using boost::shared_ptr;
using std::string;
using std::vector;
using std::set;

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
                     revision_id const & rev,
                     set<revision_id> const & candidates,
                     set<revision_id> & target_frontier)
{
  set<revision_id> frontier;

  I(member(rev, candidates));

  safe_insert(frontier, rev);

  while (!frontier.empty())
    {
      set<revision_id> next_frontier;
      for (set<revision_id>::const_iterator i = frontier.begin();
           i != frontier.end(); ++i)
        {
          revision_id const & r = *i;
          target_frontier.erase(r);

          if (member(r, candidates))
            {
              typedef ancestry_map::const_iterator ci;
              pair<ci,ci> range = child_to_parent_map.equal_range(r);
              for (ci j = range.first; j != range.second; ++j)
                if (j->first == r
                    && member(j->second, candidates)
                    && !member(j->second, next_frontier))
                  safe_insert(next_frontier, j->second);
            }
        }
      frontier = next_frontier;
    }
}

static void
expand_uncommon_ancestor_frontier(ancestry_map const & child_to_parent_map,
                                  set<revision_id> & our_candidates,                                  
                                  set<revision_id> const & our_frontier,
                                  set<revision_id> & our_next_frontier,
                                  set<revision_id> const & other_candidates,
                                  set<revision_id> & other_frontier)
{
  for (set<revision_id>::const_iterator i = our_frontier.begin();
       i != our_frontier.end(); ++i)
    {
      revision_id const & rev = *i;

      if (!member(rev, our_candidates))
        {
          safe_insert(our_candidates, rev);

          typedef ancestry_map::const_iterator ci;
          pair<ci,ci> range = child_to_parent_map.equal_range(rev);

          for (ci j = range.first; j != range.second; ++j)
            if (j->first == rev && 
                !member(j->second, our_next_frontier))
              safe_insert(our_next_frontier, j->second);
        }

      if (member(rev, other_candidates))
        trim_search_frontier(child_to_parent_map, rev, 
                             other_candidates, 
                             other_frontier);
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

  left_candidates.insert(a);
  right_candidates.insert(b);

  left_frontier.insert(a);
  right_frontier.insert(b);

  while (! (left_frontier.empty() && right_frontier.empty()))
    {
      set<revision_id> left_next_frontier;
      set<revision_id> right_next_frontier;
      expand_uncommon_ancestor_frontier(child_to_parent_map, 
                                        left_candidates, left_frontier, left_next_frontier, 
                                        right_candidates, right_frontier);

      expand_uncommon_ancestor_frontier(child_to_parent_map, 
                                        right_candidates, right_frontier, right_next_frontier, 
                                        left_candidates, left_frontier);
      left_frontier = left_next_frontier;
      right_frontier = right_next_frontier;
    }

  set_difference(left_candidates.begin(), left_candidates.end(),
                 right_candidates.begin(), right_candidates.end(),
                 inserter(left_uncommon_ancs, left_uncommon_ancs.begin()));

  set_difference(right_candidates.begin(), right_candidates.end(),
                 left_candidates.begin(), left_candidates.end(),
                 inserter(right_uncommon_ancs, right_uncommon_ancs.begin()));
}
