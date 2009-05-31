// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <map>
#include <utility>
#include <list>
#include <boost/shared_ptr.hpp>

#include "sanity.hh"
#include "graph.hh"
#include "safe_map.hh"
#include "numeric_vocab.hh"
#include "hash_map.hh"
#include "vocab_hash.hh"
#include "rev_height.hh"
#include "transforms.hh"

using boost::shared_ptr;
using std::string;
using std::vector;
using std::set;
using std::pair;
using std::map;
using std::multimap;
using std::make_pair;
using std::list;

using hashmap::hash_set;

void
get_reconstruction_path(id const & start,
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

  set<id> seen_nodes;
  vector< shared_ptr<reconstruction_path> > live_paths;

  {
    shared_ptr<reconstruction_path> pth0 = shared_ptr<reconstruction_path>(new reconstruction_path());
    pth0->push_back(start);
    live_paths.push_back(pth0);
    seen_nodes.insert(start);
  }

  shared_ptr<reconstruction_path> selected_path;

  while (!selected_path)
    {
      vector< shared_ptr<reconstruction_path> > next_paths;

      I(!live_paths.empty());
      for (vector<shared_ptr<reconstruction_path> >::const_iterator i = live_paths.begin();
           i != live_paths.end(); ++i)
        {
          shared_ptr<reconstruction_path> pth = *i;
          id tip = pth->back();

          if (graph.is_base(tip))
            {
              selected_path = pth;
              break;
            }
          else
            {
              // This tip is not a root, so extend the path.
              set<id> next;
              graph.get_next(tip, next);
              I(!next.empty());

              // Replicate the path if there's a fork.
              bool first = true;
              for (set<id>::const_iterator j = next.begin();
                    j != next.end(); ++j)
                {
                  if (global_sanity.debug_p())
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
                      for (reconstruction_path::const_iterator k = pthN->begin();
                           k != pthN->end(); ++k)
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




// graph is a parent->child map
void toposort_rev_ancestry(rev_ancestry_map const & graph,
                           vector<revision_id> & revisions)
{
  typedef multimap<revision_id, revision_id>::const_iterator gi;
  typedef map<revision_id, int>::iterator pi;

  revisions.clear();
  revisions.reserve(graph.size());
  // determine the number of parents for each rev
  map<revision_id, int> pcount;
  for (gi i = graph.begin(); i != graph.end(); ++i)
    pcount.insert(pcount.end(), make_pair(i->first, 0));
  for (gi i = graph.begin(); i != graph.end(); ++i)
    ++(pcount[i->second]);

  // find the set of graph roots
  list<revision_id> roots;
  for (pi i = pcount.begin(); i != pcount.end(); ++i)
    if(i->second==0)
      roots.push_back(i->first);

  while (!roots.empty())
    {
      revision_id cur = roots.front();
      roots.pop_front();
      if (!null_id(cur))
        revisions.push_back(cur);

      std::pair<gi, gi> bounds = graph.equal_range(cur);
      for(gi i = bounds.first;
          i != bounds.second; i++)
        if(--(pcount[i->second]) == 0)
          roots.push_back(i->second);
    }
}


// get_uncommon_ancestors
typedef std::pair<rev_height, revision_id> height_rev_pair;

static void
advance_frontier(set<height_rev_pair> & frontier,
                 hash_set<revision_id> & seen,
                 rev_graph const & rg)
{
  const height_rev_pair h_node = *frontier.rbegin();
  const revision_id & node(h_node.second);
  frontier.erase(h_node);
  set<revision_id> parents;
  rg.get_parents(node, parents);
  for (set<revision_id>::const_iterator r = parents.begin();
        r != parents.end(); r++)
  {
    if (seen.find(*r) == seen.end())
    {
      rev_height h;
      rg.get_height(*r, h);
      frontier.insert(make_pair(h, *r));
      seen.insert(*r);
    }
  }
}

void
get_uncommon_ancestors(revision_id const & a,
                       revision_id const & b,
                       rev_graph const & rg,
                       set<revision_id> & a_uncommon_ancs,
                       set<revision_id> & b_uncommon_ancs)
{
  a_uncommon_ancs.clear();
  b_uncommon_ancs.clear();

  // We extend a frontier from each revision until it reaches
  // a revision that has been seen by the other frontier. By
  // traversing in ascending height order we can ensure that
  // any common ancestor will have been 'seen' by both sides
  // before it is traversed.

  set<height_rev_pair> a_frontier, b_frontier, common_frontier;
  {
    rev_height h;
    rg.get_height(a, h);
    a_frontier.insert(make_pair(h, a));
    rg.get_height(b, h);
    b_frontier.insert(make_pair(h, b));
  }

  hash_set<revision_id> a_seen, b_seen, common_seen;
  a_seen.insert(a);
  b_seen.insert(b);

  while (!a_frontier.empty() || !b_frontier.empty())
  {
    // We take the leaf-most (ie highest) height entry from any frontier.
    // Note: the default height is the lowest possible.
    rev_height a_height, b_height, common_height;
    if (!a_frontier.empty())
      a_height = a_frontier.rbegin()->first;
    if (!b_frontier.empty())
      b_height = b_frontier.rbegin()->first;
    if (!common_frontier.empty())
      common_height = common_frontier.rbegin()->first;

    if (a_height > b_height && a_height > common_height)
      {
        a_uncommon_ancs.insert(a_frontier.rbegin()->second);
        advance_frontier(a_frontier, a_seen, rg);
      }
    else if (b_height > a_height && b_height > common_height)
      {
        b_uncommon_ancs.insert(b_frontier.rbegin()->second);
        advance_frontier(b_frontier, b_seen, rg);
      }
    else if (common_height > a_height && common_height > b_height)
      {
        advance_frontier(common_frontier, common_seen, rg);
      }
    else if (a_height == b_height) // may or may not also == common_height
      {
        // if both frontiers are the same, then we can safely say that
        // we've found all uncommon ancestors. This stopping condition
        // can result in traversing more nodes than required, but is simple.
        if (a_frontier == b_frontier)
          break;

        common_frontier.insert(*a_frontier.rbegin());
        a_frontier.erase(*a_frontier.rbegin());
        b_frontier.erase(*b_frontier.rbegin());
      }
    else if (a_height == common_height)
      {
        a_frontier.erase(*a_frontier.rbegin());
      }
    else if (b_height == common_height)
      {
        b_frontier.erase(*b_frontier.rbegin());
      }
    else
      I(false);
  }  
}

void
get_all_ancestors(set<revision_id> const & start,
                  rev_ancestry_map const & child_to_parent_map,
                  set<revision_id> & ancestors)
{
  ancestors.clear();
  vector<revision_id> frontier(start.begin(), start.end());
  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      if (ancestors.find(rid) != ancestors.end())
        continue;
      safe_insert(ancestors, rid);
      typedef rev_ancestry_map::const_iterator ci;
      pair<ci,ci> range = child_to_parent_map.equal_range(rid);
      for (ci i = range.first; i != range.second; ++i)
        frontier.push_back(i->second);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
