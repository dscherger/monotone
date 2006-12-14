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
  // involved loading too much of the storage graph into memory at any
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

// This is a complicated algorithm, but it has to go very fast in order for
// "pull" to work tolerably well -- this function is called once for every
// merge revision written to the database.
//
// We're aiming to find, for revs Left and Right:
// 
//  - all revs that are ancestors of Left and not ancestors of Right
//  - all revs that are ancestors of Right and not ancestors of Left
//
// However, we want to do this without actually enumerating every ancestor of
// Left and every ancestor of Right, and then taking set differences --
// usually, divergences are short, and all uncommon ancestors occur within the
// last few dozen revisions.
//
// There are many algorithms for this that work perfectly most of the time
// cases, but then in some particular torturous case fail.  Here is one that
// always succeeds, and takes time and space O(total number of uncommon
// ancestors):
//
// Phase 1
// -------
//
// Our immediate goal is to construct a set of ancestors of Left that contain
// every uncommon ancestor of Left (though it might contain other things as
// well), and similarly for Right.  To do this, we do two
// breadth-first-by-height traversals in parallel, one starting from Left and
// one from Right.  Our goal is to stop when we know that each of these
// traversals contains all of the uncommon ancestors on that side.  We record
// every node found by these two traversals in left_candidates and
// right_candidates, respectively.
//
// The reason we do breadth-first-by-height search, rather than pure
// breadth-first search, is that it gives us a convexity property which we
// will need later.  In particular, if A and B are both in a candidate set,
// and A is an ancestor of B, then every upward path from B to A stays
// entirely within the candidate set.  (Proof: intermdiate nodes are ancestors
// of B, so they will be reached by the search eventually; they are also
// descendents of A, thus have larger heights, thus will be traversed before
// A.)
//
// So, we are going along expanding our sets, and we need to know when to
// stop.  To do this, we periodically run a check.  (To keep running time
// linear, the length of these periods doubles each time.)  This check
// consists of running a secondary, depth-first traversal from each of left
// and right.  For example, running from left, this traversal:
//   -- prunes paths that hit the root of the graph
//   -- prunes paths that enter right_candidates
//   -- aborts with failure if some path exits the left_candidates set.
// The last property keeps us from expending arbitrary time on the check; the
// previous two properties let us determine whether we have expanded our
// candidate sets far enough to cover all the uncommon ancestors we want.
//
// When the traversal from both sides succeeds, without aborting, then we can
// be certain that every uncommon ancestor is included in at least one
// candidate set.  This is because we only stop traversing when we have either
// run out of nodes altogether -- in which case the path we were on certainly
// was not going to encounter any more uncommon ancestors -- or when we have
// provably entered the domain of _common_ ancestors -- at which point there
// cannot possibly be any more uncommon ancestors to be found.
//
// We can also infer one more critical property from this check.  If we now
// take the _union_ of the left_candidates and right_candidates, that union is
// _also_ a convex set.  Proof: suppose we have two nodes A and B in the
// union, with A > B.  If A and B were both in the same candidate set, the
// problem would be trivial, so assume they're not, and without loss of
// generality assume A is in right_candidates and B is in left_candidates.  We
// want to show that every upward path from B to A stays within the union of
// left_candidates and right_candidates.  Obviously, because B is in
// left_candidates, there is some path from left to B.  Furthermore, because
// of our check, we know that every extension of this path that leaves
// left_candidates, immediately enters right_candidates.  Since
// right_candidates is itself convex, this proves that every path from B to A
// stays within the union, and thus the union is convex.
//
// Phase 2
// -------
//
// Let 'candidates' refer to the union of the old left_candidates and
// right_candidates (which are otherwise now discarded).  From the arguments
// above, we know that candidates:
//   -- contains both left and right
//   -- contains every uncommon ancestor of left and right
//   -- is convex.
//
// Therefore, to finally find the uncommon ancestor sets, simply enumerate (by
// yet another depth-first traversal) all ancestors of left and right that are
// candidates, calling the results left_ancestors and right_ancestors.  Then
// the uncommon ancestors are simply
//   left_uncommon_ancestors  = left_ancestors \ right_ancestors
//   right_uncommon_ancestors = right_ancestors \ left_ancestors
//
// Proof: Trivial -- left and right are in the candidates set, and the
// candidates set is convex, which implies that if some node is in fact an
// ancestor of either of them, then this will be discovered by the
// candidates-bounded traversal.  Therefore, left_ancestors and
// right_ancestors do in fact exactly enumerate those ancestors of left that
// are in the candidates set, and those ancestors of right that are in the
// candidates set.  Therefore, the set differences above are exactly those
// uncommon ancestors which are in the candidates set.  However, since we know
// that _every_ uncommon ancestor is in the candidates set, this is _all_
// uncommon ancestors.
//
// QED, and about bloody time.
//
// Note that the convexity part of the argument is absolutely critical.  Here
// is an example:
// 
//              9
//              |\                  . Extraneous dots brought to you by the
//              8 \                 . Committee to Shut Up the C Preprocessor
//             /|  \                . (CSUCPP), and viewers like you and me.
//            / |   |
//           /  7   |
//          |   |   |
//          |   6   |
//          |   |   |
//          |   5   |
//          |   |   |
//          |   4   |
//          |   |   |
//          |   :   |  <-- insert arbitrarily many revisions at the ellipsis
//          |   :   |
//          |   |   |
//          1   2   3
//           \ / \ /
//            L   R
//
// Suppose we did simple breadth-first traversals in Phase 1, rather than
// breadth-first-by-height, and thus gave up convexity.  After deepening 3
// times, we have:
//   left_candidates: 1 2 4 5 8 9 (not 6 or 7 yet!)
//   right_candidates: 2 3 4 5 9 (not 6, 7, or 8 yet!)
// Note that since both 2 and 9 are in both sets, our test for termination
// succeeds.  One path on the left goes 1-8-9 before terminating in the
// right_candidates, when further deepening would have caused it to terminate
// after just going 1-8, but it does terminate. (And note that this correctly
// indicates that we have found all uncommon ancestors, since 6 and 7 are
// actually common ancestors.)  But, if we now union these two sets and
// calculate the left_ancestors and right_ancestors sets, we will find:
//   left_ancestors: 1 2 4 5 8 9
//   right_ancestors: 2 3 4 5 9
//   left_ancestors \ right_ancestors: 1 8
// But this is _wrong_; 8 is a common ancestor!  The problem is that when we
// restricted to the final candidates set, we left out 6 and 7.  Without those
// nodes, we cannot reach 8 from right, and thus we cannot tell that it is a
// common ancestor, and our algorithm gives the wrong answer.
//
// When we traverse breadth-first-by-height, however, 9 cannot enter the
// right_candidates set before 8, and since at least one of them has to enter
// right_candidates before the termination criterion can be met, this problem
// goes away.  We must to restricted traversals to bound complexity, but
// without convexity we have no guarantee that restricted reachability matches
// true reachability.

static bool
member(revision_id const & i, 
       set<revision_id> const & s)
{
  return s.find(i) != s.end();
}

bool
our_ua_candidate_set_is_complete(revision_id const & our_begin,
                                 ancestry_map const & child_to_parent_map,
                                 set<revision_id> const & our_candidates,
                                 set<revision_id> const & their_candidates)
{
  //
  // Given "our" candidates P and "their" candidates Q, we say that P is
  // "complete" if, for every node N, the ancestors of N are all either:
  //
  //   - in P
  //   - in, or at least require passing through, Q
  //   - roots of the storage graph
  // 
  // We define the quest for completion via a loop. We're "complete" if we
  // finish the loop without failing. The loop is DFS starting from
  // "our_begin".
  //
  // We trim nodes from our search -- neither considering them nor expanding
  // into their parents -- that are either roots of the ancestry graph or
  // members of "their" candidate set, Q.
  //
  // We fail if, after considering potential trim conditions, our search has
  // hit a node outside our candidate set.
  //

  stack<revision_id> stk;
  set<revision_id> seen;
  stk.push(our_begin);
  while (!stk.empty())
    {
      revision_id r = stk.top();
      stk.pop();

      if (seen.find(r) != seen.end())
        continue;
      safe_insert(seen, r);

      if (null_id(r))
        continue;

      if (their_candidates.find(r) != their_candidates.end())
        continue;

      if (our_candidates.find(r) != our_candidates.end())
        return false;

      typedef ancestry_map::const_iterator ci;
      pair<ci,ci> range = child_to_parent_map.equal_range(r);
      for (ci i = range.first; i != range.second; ++i)
        {
          if (i->first == r)
            stk.push(i->second);
        }
    }
  return true;
}

struct ua_qentry
{
  rev_height height;
  revision_id rev;
  ua_qentry(revision_id const & r, 
            database & db) : rev(r)
  {
    db.get_rev_height(rev, height);
  }
  bool operator<(ua_qentry const & other) const 
  {
    return heght < other.height
      || height == other.height && rev < other.rev;
  }
};

void
step_ua_candidate_search(revision_id const & our_base_rid,            
                         ancestry_map const & child_to_parent_map,
                         set<revision_id> & our_candidates,
                         set<revision_id> const & their_candidates,
                         priority_queue<ua_qentry> & our_queue,
                         database & db,
                         bool & complete_p,
                         size_t iter,
                         size_t next_check)
{
  if (!complete_p)
    {
      ua_qentry qe = our_queue.top();
      our_queue.pop();

      if (our_candidates.find(qe.rev) != our_candidates.end())
        continue;
      safe_insert(our_candidates, qe.rev);

      typedef ancestry_map::const_iterator ci;
      pair<ci,ci> range = child_to_parent_map.equal_range(qe.rev);
      for (ci i = range.first; i != range.second; ++i)
        {
          if (i->first == qe.rev)
            our_queue.push(ua_qentry(i->second, db));
        }

      if (iter == next_check)
        complete_p = our_ua_candidate_set_is_complete(our_base_rid, 
                                                      child_to_parent_map,
                                                      our_candidates,
                                                      their_candidates);
    }
}

void
constrained_transitive_closure(revision_id const & begin,
                               set<revision_id> const & candidates,
                               ancestry_map const & child_to_parent_map,
                               set<revision_id> & ancs)
{
  stack<revision_id> stk;
  stk.push_back(begin);
  while (!stk.empty())
    {
      revision_id r = stk.top();
      stk.pop();

      if (candidates.find(r) == candidates.end())
        continue;

      if (ancs.find(r) != ancs.end())
        continue;      
      ancs.insert(r);

      typedef ancestry_map::const_iterator ci;
      pair<ci,ci> range = child_to_parent_map.equal_range(r);
      for (ci i = range.first; i != range.second; ++i)
        {
          if (i->first == r)
            stk.push(i->second);
        }
    }
}

void
get_uncommon_ancestors(revision_id const & left_rid, revision_id const & right_rid,
                       ancestry_map const & child_to_parent_map,
                       std::set<revision_id> & left_uncommon_ancs,
                       std::set<revision_id> & right_uncommon_ancs,
                       database & db)
{
  set<revision_id> candidates;

  {
    // Phase 1: build the candidate sets and union them.
    
    set<revision_id> left_candidates;
    set<revision_id> right_candidates;
    
    priority_queue<ua_qentry> left_queue;
    priority_queue<ua_qentry> right_queue;
    
    left_queue.push(ua_qentry(left_rid, db));
    right_queue.push(ua_qentry(right_rid, db));
    
    bool right_complete = false;
    bool left_complete = false;
    
    size_t iter = 0;
    size_t next_check = 1;
    
    while (!(right_complete && left_complete))
      {
        step_ua_candidate_search(left_rid, child_to_parent_map,
                                 left_candidates, right_candidates, left_queue, 
                                 db, left_complete, iter, next_check);
        
        step_ua_candidate_search(right_rid, child_to_parent_map,
                                 right_candidates, left_candidates, right_queue, 
                                 db, right_complete, iter, next_check);
        
        if (iter == next_check)
          next_check <<= 1;
        iter++;
      }

    set_union(left_candidates.begin(), left_candidates.end(),
              right_candidates.begin(), right_candidates.end(),
              inserter(candidates, candidates.begin()));
  }

  {
    // Phase 2: search for anc-sets-that-are-candidates and take differences.

    set<revision_id> left_ancs;
    set<revision_id> right_ancs;

    constrained_transitive_closure(left_rid, candidates, 
                                   child_to_parent_map, left_ancs);
    constrained_transitive_closure(right_rid, candidates, 
                                   child_to_parent_map, right_ancs);

    left_uncommon_ancestors.clear();
    right_uncommon_ancestors.clear();

    set_difference(left_ancs.begin(), left_ancs.end(),
                   right_ancs.begin(), right_ancs.end(),
                   inserter(left_uncommon_ancestors, 
                            left_uncommon_ancestors.begin()));

    set_difference(right_ancs.begin(), right_ancs.end(),
                   left_ancs.begin(), left_ancs.end(),
                   inserter(right_uncommon_ancestors, 
                            right_uncommon_ancestors.begin()));    
  }

}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "randomizer.hh"

using namespace randomizer;

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

static void
run_a_get_uncommon_ancestors_test(ancestry_map const & child_to_parent_map,
                                  revision_id const & left, revision_id const & right)
{
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
  get_uncommon_ancestors(right, left, child_to_parent_map,
                         calculated_right_uncommon_ancestors,
                         calculated_left_uncommon_ancestors);
  I(calculated_left_uncommon_ancestors == true_left_uncommon_ancestors);
  I(calculated_right_uncommon_ancestors == true_right_uncommon_ancestors);
}

static void
test_get_uncommon_ancestors_nasty_convexity_case()
{
  // This tests the nasty case described in the giant comment above
  // get_uncommon_ancestors:
  // 
  //              9
  //              |\                  . Extraneous dots brought to you by the
  //              8 \                 . Committee to Shut Up the C Preprocessor
  //             /|  \                . (CSUCPP), and viewers like you and me.
  //            / |   |
  //           /  7   |
  //          |   |   |
  //          |   6   |
  //          |   |   |
  //          |   5   |
  //          |   |   |
  //          |   4   |
  //          |   |   |
  //          |   :   |  <-- insert arbitrarily many revisions at the ellipsis
  //          |   :   |
  //          |   |   |
  //          1   2   3
  //           \ / \ /
  //            L   R

  ancestry_map child_to_parent_map;
  revision_id left(fake_id()), right(fake_id());
  revision_id one(fake_id()), eight(fake_id()), three(fake_id()), nine(fake_id());
  safe_insert(child_to_parent_map, make_pair(left, one));
  safe_insert(child_to_parent_map, make_pair(one, eight));
  safe_insert(child_to_parent_map, make_pair(eight, nine));
  safe_insert(child_to_parent_map, make_pair(right, three));
  safe_insert(child_to_parent_map, make_pair(three, nine));

  revision_id middle(fake_id());
  safe_insert(child_to_parent_map, make_pair(left, two));
  safe_insert(child_to_parent_map, make_pair(right, two));
  // We insert a _lot_ of revisions at the ellipsis, to make sure that
  // whatever sort of step-size is used on the expansion, we can't take the
  // entire middle portion in one big gulp and make the test pointless.
  for (int i = 0; i != 1000; ++i)
    {
      revision_id next(fake_id());
      safe_insert(child_to_parent_map, make_pair(middle, next));
      middle = next;
    }
  safe_insert(child_to_parent_map, make_pair(middle, eight));

  run_a_get_uncommon_ancestors_test(child_to_parent_map, left, right);
}

double const new_root_freq = 0.05;
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
      if (heads.empty() || bernoulli(new_root_freq))
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
      run_a_get_uncommon_ancestors_test(child_to_parent_map, left, right);
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
  suite->add(BOOST_TEST_CASE(&test_get_uncommon_ancestors_nasty_convexity_case));
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
#ifdef BUILD_UNIT_TESTS

#include <map>
#include "unit_tests.hh"
#include "randomizer.hh"

#include <boost/lexical_cast.hpp>

using boost::lexical_cast;
using std::pair;

typedef std::multimap<string, string> rg_map;
struct mock_reconstruction_graph : public reconstruction_graph
{
  rg_map ancestry;
  set<string> bases;
  mock_reconstruction_graph(rg_map const & ancestry, set<string> const & bases)
    : ancestry(ancestry), bases(bases)
  {}
  virtual bool is_base(string const & node) const
  {
    return bases.find(node) != bases.end();
  }
  virtual void get_next(string const & from, set<string> & next) const
  {
    typedef rg_map::const_iterator ci;
    pair<ci, ci> range = ancestry.equal_range(from);
    for (ci i = range.first; i != range.second; ++i)
      next.insert(i->second);
  }
};

static void
make_random_reconstruction_graph(size_t num_nodes, size_t num_random_edges,
                                 size_t num_random_bases,
                                 vector<string> & all_nodes, rg_map & ancestry,
                                 set<string> & bases,
                                 randomizer & rng)
{
  for (size_t i = 0; i != num_nodes; ++i)
    all_nodes.push_back(lexical_cast<string>(i));
  // We put a single long chain of edges in, to make sure that everything is
  // reconstructable somehow.
  for (size_t i = 1; i != num_nodes; ++i)
    ancestry.insert(make_pair(idx(all_nodes, i - 1), idx(all_nodes, i)));
  bases.insert(all_nodes.back());
  // Then we insert a bunch of random edges too.  These edges always go
  // forwards, to avoid creating cycles (which make get_reconstruction_path
  // unhappy).
  for (size_t i = 0; i != num_random_edges; ++i)
    {
      size_t from_idx = rng.uniform(all_nodes.size() - 1);
      size_t to_idx = from_idx + 1 + rng.uniform(all_nodes.size() - 1 - from_idx);
      ancestry.insert(make_pair(idx(all_nodes, from_idx),
                                idx(all_nodes, to_idx)));
    }
  // And a bunch of random bases.
  for (size_t i = 0; i != num_random_bases; ++i)
    bases.insert(idx(all_nodes, rng.uniform(all_nodes.size())));
}

static void
check_reconstruction_path(string const & start, reconstruction_graph const & graph,
                          reconstruction_path const & path)
{
  I(!path.empty());
  I(*path.begin() == start);
  reconstruction_path::const_iterator last = path.end();
  --last;
  I(graph.is_base(*last));
  for (reconstruction_path::const_iterator i = path.begin(); i != last; ++i)
    {
      set<string> children;
      graph.get_next(*i, children);
      reconstruction_path::const_iterator next = i;
      ++next;
      I(children.find(*next) != children.end());
    }
}

static void
run_get_reconstruction_path_tests_on_random_graph(size_t num_nodes,
                                                  size_t num_random_edges,
                                                  size_t num_random_bases,
                                                  randomizer & rng)
{
  vector<string> all_nodes;
  rg_map ancestry;
  set<string> bases;
  make_random_reconstruction_graph(num_nodes, num_random_edges, num_random_bases,
                                   all_nodes, ancestry, bases,
                                   rng);
  mock_reconstruction_graph graph(ancestry, bases);
  for (vector<string>::const_iterator i = all_nodes.begin();
       i != all_nodes.end(); ++i)
    {
      reconstruction_path path;
      get_reconstruction_path(*i, graph, path);
      check_reconstruction_path(*i, graph, path);
    }
}

UNIT_TEST(graph, random_get_reconstruction_path)
{
  randomizer rng;
  // Some arbitrary numbers.
  run_get_reconstruction_path_tests_on_random_graph(100, 100, 10, rng);
  run_get_reconstruction_path_tests_on_random_graph(100, 200, 5, rng);
  run_get_reconstruction_path_tests_on_random_graph(1000, 1000, 50, rng);
  run_get_reconstruction_path_tests_on_random_graph(1000, 2000, 100, rng);
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
