// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <iterator>
#include <functional>

#include <boost/lexical_cast.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/shared_ptr.hpp>

#include "botan/botan.h"

#include "basic_io.hh"
#include "change_set.hh"
#include "constants.hh"
#include "numeric_vocab.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"
#include "vocab.hh"
#include "keys.hh"

void revision_set::check_sane() const
{
  I(!null_id(new_manifest));

  manifest_map fragment;
  for (edge_map::const_iterator i = edges.begin(); i != edges.end(); ++i)
    {
      change_set const & cs = edge_changes(i);
      cs.check_sane();
      if (!global_sanity.relaxed)
        {
          // null old revisions come with null old manifests
          I(!null_id(edge_old_revision(i)) || null_id(edge_old_manifest(i)));
        }
      for (change_set::delta_map::const_iterator j = cs.deltas.begin(); j != cs.deltas.end(); ++j)
        {
          manifest_map::const_iterator k = fragment.find(delta_entry_path(j));
          if (k == fragment.end())
            fragment.insert(std::make_pair(delta_entry_path(j),
                                           delta_entry_dst(j)));
          else
            {
              if (!global_sanity.relaxed)
                {                  
                  I(delta_entry_dst(j) == manifest_entry_id(k));
                }
            }
        }
    }
}

revision_set::revision_set(revision_set const & other)
{
  other.check_sane();
  new_manifest = other.new_manifest;
  edges = other.edges;
}

revision_set const & 
revision_set::operator=(revision_set const & other)
{
  other.check_sane();
  new_manifest = other.new_manifest;
  edges = other.edges;
  return *this;
}


// Traces history back 'depth' levels from 'child_id', ensuring that
// historical information is consistent within this subgraph.
// The child must be in the database already.
//
// "Consistent" currently means that we compose manifests along every path (of
// any length) that terminates at the child, and for each one check that paths
// that should be the same in fact are the same, and that the calculated
// change sets can be applied to the old manifests to create the new
// manifest.
//
// NB: While this function has some invariants in it itself, a lot of its
// purpose is just to exercise all the invariants inside change_set.cc.  So
// don't remove those invariants.  (As if you needed another reason...)
void
check_sane_history(revision_id const & child_id,
                   int depth,
                   app_state & app)
{
  // We are, unfortunately, still quite slow.  So we want to give at least a
  // little feedback.  Let's print exactly one warning, on the _second_ time
  // we are called within one run -- just checking one revision isn't too
  // slow, so no need to print anything on "commit", but usually if we're
  // checking 2 revisions we're checking a lot more.
  // FIXME: make sanity checking faster, so we can remove this kluge
  // altogether...
  static int num_checked = 0;
  ++num_checked;
  if (num_checked == 2)
    P(F("verifying new revisions (this may take a while)\n"));

  L(F("Verifying revision %s has sane history (to depth %i)\n")
    % child_id % depth);

  typedef boost::shared_ptr<change_set> shared_cs;
  // (ancestor, change_set from ancestor to child)
  std::map<revision_id, shared_cs> changesets;
  
  manifest_id m_child_id;
  app.db.get_revision_manifest(child_id, m_child_id);
  manifest_map m_child;
  app.db.get_manifest(m_child_id, m_child);

  std::set<revision_id> frontier;
  frontier.insert(child_id);
    
  // FIXME: if we don't check the LCA already, check it.

  while (depth-- > 0)
    {
      std::set<revision_id> next_frontier;
      
      for (std::set<revision_id>::const_iterator i = frontier.begin();
           i != frontier.end();
           ++i)
        {
          revision_id current_id = *i;
          revision_set current;
          app.db.get_revision(current_id, current);
          // and the parents's manifests to the manifests
          // and the change_set's to the parents to the changesets
          for (edge_map::const_iterator j = current.edges.begin();
               j != current.edges.end();
               ++j)
            {
              revision_id old_id = edge_old_revision(j);
              manifest_id m_old_id = edge_old_manifest(j);
              change_set old_to_current_changes = edge_changes(j);
              if (!null_id(old_id))
                next_frontier.insert(old_id);
              
              L(F("Examining %s -> %s\n") % old_id % child_id);

              // build the change_set
              // if 
              shared_cs old_to_child_changes_p = shared_cs(new change_set);
              if (current_id == child_id)
                *old_to_child_changes_p = old_to_current_changes;
              else
                {
                  shared_cs current_to_child_changes_p;
                  I(changesets.find(current_id) != changesets.end());
                  current_to_child_changes_p = changesets.find(current_id)->second;
                  concatenate_change_sets(old_to_current_changes,
                                          *current_to_child_changes_p,
                                          *old_to_child_changes_p);
                }

              // we have the change_set; now, is it one we've seen before?
              if (changesets.find(old_id) != changesets.end())
                {
                  // If it is, then make sure the paths agree on the
                  // changeset.
                  I(*changesets.find(old_id)->second == *old_to_child_changes_p);
                }
              else
                {
                  // If not, this is the first time we've seen this.
                  // So store it in the map for later reference:
                  changesets.insert(std::make_pair(old_id, old_to_child_changes_p));
                  // and check that it works:

                  manifest_map purported_m_child;
                  // The null revision has empty manifest, which is the
                  // default.
                  if (!null_id(old_id))
                    app.db.get_manifest(m_old_id, purported_m_child);
                  apply_change_set(*old_to_child_changes_p, purported_m_child);
                  I(purported_m_child == m_child);
                }
            }
        }
      frontier = next_frontier;
    }

  // Finally, there's a danger that if we have a long divergence, then after a
  // merge, the common ancestor will be far back enough that the above
  // depth-limited search won't have any idea whether the ancestry invariants
  // are actually preserved.  So do an additional check on merge revisions, to
  // make sure that the paths to both ways going back to their parents's
  // common ancestor give the same change_set (i.e., this is a valid merge at
  // all).
  if (!global_sanity.relaxed)
    {
      revision_set child_rev;
      app.db.get_revision(child_id, child_rev);
      // Nothing inherently impossible about having more than 2 parents, but if
      // you come up with some case where it should be possible then you'll
      // have to also adjust the code below to figure out what "common
      // ancestor" means.
      I(child_rev.edges.size() <= 2);
      if (child_rev.edges.size() != 2)
        return;
      edge_map::const_iterator i = child_rev.edges.begin();
      revision_id parent_left = edge_old_revision(i);
      change_set left_edge = edge_changes(i);
      ++i;
      revision_id parent_right = edge_old_revision(i);
      change_set right_edge = edge_changes(i);
      ++i;
      I(i == child_rev.edges.end());
      revision_id lca;
      if (!find_least_common_ancestor(parent_left, parent_right, lca, app))
        {
          L(F("%s and %s have no common ancestor, so done\n")
            % parent_left % parent_right);
          return;
        }
      if (changesets.find(lca) != changesets.end())
        {
          L(F("already checked common ancestor, so done\n"));
          return;
        }
      L(F("%s is a merge; verifying paths to common ancestor %s are sane\n")
        % child_id % lca);
      // we have a merge node, with an lca sufficiently far back in history
      // that we haven't yet figured out whether this is a valid merge or
      // not.  so find out.
      change_set cs_parent_left, cs_parent_right, cs_left, cs_right;
      calculate_composite_change_set(lca, parent_left, app, cs_parent_left);
      calculate_composite_change_set(lca, parent_right, app, cs_parent_right);
      concatenate_change_sets(cs_parent_left, left_edge, cs_left);
      concatenate_change_sets(cs_parent_right, right_edge, cs_right);
      I(cs_left == cs_right);
    }
}

// calculating least common ancestors is a delicate thing.
// 
// it turns out that we cannot choose the simple "least common ancestor"
// for purposes of a merge, because it is possible that there are two
// equally reachable common ancestors, and this produces ambiguity in the
// merge. the result -- in a pathological case -- is silently accepting one
// set of edits while discarding another; not exactly what you want a
// version control tool to do.
//
// a conservative approximation is what we'll call a "subgraph recurring"
// LCA algorithm. this is somewhat like locating the least common dominator
// node, but not quite. it is actually just a vanilla LCA search, except
// that any time there's a fork (a historical merge looks like a fork from
// our perspective, working backwards from children to parents) it reduces
// the fork to a common parent via a sequence of pairwise recursive calls
// to itself before proceeding. this will always resolve to a common parent
// with no ambiguity, unless it falls off the root of the graph.
//
// unfortunately the subgraph recurring algorithm sometimes goes too far
// back in history -- for example if there is an unambiguous propagate from
// one branch to another, the entire subgraph preceeding the propagate on
// the recipient branch is elided, since it is a merge.
//
// our current hypothesis is that the *exact* condition we're looking for,
// when doing a merge, is the least node which dominates one side of the
// merge and is an ancestor of the other.

typedef unsigned long ctx;
typedef boost::dynamic_bitset<> bitmap;
typedef boost::shared_ptr<bitmap> shared_bitmap;

static void 
ensure_parents_loaded(ctx child,
                      std::map<ctx, shared_bitmap> & parents,
                      interner<ctx> & intern,
                      app_state & app)
{
  if (parents.find(child) != parents.end())
    return;

  L(F("loading parents for node %d\n") % child);

  std::set<revision_id> imm_parents;
  app.db.get_revision_parents(revision_id(intern.lookup(child)), imm_parents);

  // The null revision is not a parent for purposes of finding common
  // ancestors.
  for (std::set<revision_id>::iterator p = imm_parents.begin();
       p != imm_parents.end(); ++p)
    {
      if (null_id(*p))
        imm_parents.erase(p);
    }
              
  shared_bitmap bits = shared_bitmap(new bitmap(parents.size()));
  
  for (std::set<revision_id>::const_iterator p = imm_parents.begin();
       p != imm_parents.end(); ++p)
    {
      ctx pn = intern.intern(p->inner()());
      L(F("parent %s -> node %d\n") % *p % pn);
      if (pn >= bits->size()) 
        bits->resize(pn+1);
      bits->set(pn);
    }
    
  parents.insert(std::make_pair(child, bits));
}

static bool 
expand_dominators(std::map<ctx, shared_bitmap> & parents,
                  std::map<ctx, shared_bitmap> & dominators,
                  interner<ctx> & intern,
                  app_state & app)
{
  bool something_changed = false;
  std::vector<ctx> nodes;

  nodes.reserve(dominators.size());

  // pass 1, pull out all the node numbers we're going to scan this time around
  for (std::map<ctx, shared_bitmap>::const_iterator e = dominators.begin(); 
       e != dominators.end(); ++e)
    nodes.push_back(e->first);
  
  // pass 2, update any of the dominator entries we can
  for (std::vector<ctx>::const_iterator n = nodes.begin(); 
       n != nodes.end(); ++n)
    {
      shared_bitmap bits = dominators[*n];
      bitmap saved(*bits);
      if (bits->size() <= *n)
        bits->resize(*n + 1);
      bits->set(*n);
      
      ensure_parents_loaded(*n, parents, intern, app);
      shared_bitmap n_parents = parents[*n];
      
      bitmap intersection(bits->size());
      
      bool first = true;
      for (unsigned long parent = 0; 
           parent != n_parents->size(); ++parent)
        {
          if (! n_parents->test(parent))
            continue;

          if (dominators.find(parent) == dominators.end())
            dominators.insert(std::make_pair(parent, 
                                             shared_bitmap(new bitmap())));
          shared_bitmap pbits = dominators[parent];

          if (bits->size() > pbits->size())
            pbits->resize(bits->size());

          if (pbits->size() > bits->size())
            bits->resize(pbits->size());

          if (first)
            {
              intersection = (*pbits);
              first = false;
            }
          else
            intersection &= (*pbits);
        }

      (*bits) |= intersection;
      if (*bits != saved)
        something_changed = true;
    }
  return something_changed;
}


static bool 
expand_ancestors(std::map<ctx, shared_bitmap> & parents,
                 std::map<ctx, shared_bitmap> & ancestors,
                 interner<ctx> & intern,
                 app_state & app)
{
  bool something_changed = false;
  std::vector<ctx> nodes;

  nodes.reserve(ancestors.size());

  // pass 1, pull out all the node numbers we're going to scan this time around
  for (std::map<ctx, shared_bitmap>::const_iterator e = ancestors.begin(); 
       e != ancestors.end(); ++e)
    nodes.push_back(e->first);
  
  // pass 2, update any of the ancestor entries we can
  for (std::vector<ctx>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    {
      shared_bitmap bits = ancestors[*n];
      bitmap saved(*bits);
      if (bits->size() <= *n)
        bits->resize(*n + 1);
      bits->set(*n);

      ensure_parents_loaded(*n, parents, intern, app);
      shared_bitmap n_parents = parents[*n];
      for (ctx parent = 0; parent != n_parents->size(); ++parent)
        {
          if (! n_parents->test(parent))
            continue;

          if (bits->size() <= parent)
            bits->resize(parent + 1);
          bits->set(parent);

          if (ancestors.find(parent) == ancestors.end())
            ancestors.insert(make_pair(parent, 
                                        shared_bitmap(new bitmap())));
          shared_bitmap pbits = ancestors[parent];

          if (bits->size() > pbits->size())
            pbits->resize(bits->size());

          if (pbits->size() > bits->size())
            bits->resize(pbits->size());

          (*bits) |= (*pbits);
        }
      if (*bits != saved)
        something_changed = true;
    }
  return something_changed;
}

static bool 
find_intersecting_node(bitmap & fst, 
                       bitmap & snd, 
                       interner<ctx> const & intern, 
                       revision_id & anc)
{
  
  if (fst.size() > snd.size())
    snd.resize(fst.size());
  else if (snd.size() > fst.size())
    fst.resize(snd.size());
  
  bitmap intersection = fst & snd;
  if (intersection.any())
    {
      L(F("found %d intersecting nodes\n") % intersection.count());
      for (ctx i = 0; i < intersection.size(); ++i)
        {
          if (intersection.test(i))
            {
              anc = revision_id(intern.lookup(i));
              return true;
            }
        }
    }
  return false;
}

//  static void
//  dump_bitset_map(std::string const & hdr,
//              std::map< ctx, shared_bitmap > const & mm)
//  {
//    L(F("dumping [%s] (%d entries)\n") % hdr % mm.size());
//    for (std::map< ctx, shared_bitmap >::const_iterator i = mm.begin();
//         i != mm.end(); ++i)
//      {
//        L(F("dump [%s]: %d -> %s\n") % hdr % i->first % (*(i->second)));
//      }
//  }

bool 
find_common_ancestor_for_merge(revision_id const & left,
                               revision_id const & right,
                               revision_id & anc,
                               app_state & app)
{
  interner<ctx> intern;
  std::map< ctx, shared_bitmap > 
    parents, ancestors, dominators;
  
  ctx ln = intern.intern(left.inner()());
  ctx rn = intern.intern(right.inner()());
  
  shared_bitmap lanc = shared_bitmap(new bitmap());
  shared_bitmap ranc = shared_bitmap(new bitmap());
  shared_bitmap ldom = shared_bitmap(new bitmap());
  shared_bitmap rdom = shared_bitmap(new bitmap());

  ancestors.insert(make_pair(ln, lanc));
  ancestors.insert(make_pair(rn, ranc));
  dominators.insert(make_pair(ln, ldom));
  dominators.insert(make_pair(rn, rdom));
  
  L(F("searching for common ancestor, left=%s right=%s\n") % left % right);
  
  while (expand_ancestors(parents, ancestors, intern, app) ||
         expand_dominators(parents, dominators, intern, app))
    {
      L(F("common ancestor scan [par=%d,anc=%d,dom=%d]\n") % 
        parents.size() % ancestors.size() % dominators.size());

      if (find_intersecting_node(*lanc, *rdom, intern, anc))
        {
          L(F("found node %d, ancestor of left %s and dominating right %s\n")
            % anc % left % right);
          return true;
        }
      
      else if (find_intersecting_node(*ranc, *ldom, intern, anc))
        {
          L(F("found node %d, ancestor of right %s and dominating left %s\n")
            % anc % right % left);
          return true;
        }
    }
//      dump_bitset_map("ancestors", ancestors);
//      dump_bitset_map("dominators", dominators);
//      dump_bitset_map("parents", parents);
  return false;
}


bool
find_least_common_ancestor(revision_id const & left,
                           revision_id const & right,
                           revision_id & anc,
                           app_state & app)
{
  interner<ctx> intern;
  std::map< ctx, shared_bitmap >
    parents, ancestors;

  ctx ln = intern.intern(left.inner()());
  ctx rn = intern.intern(right.inner()());

  shared_bitmap lanc = shared_bitmap(new bitmap());
  shared_bitmap ranc = shared_bitmap(new bitmap());

  ancestors.insert(make_pair(ln, lanc));
  ancestors.insert(make_pair(rn, ranc));

  L(F("searching for least common ancestor, left=%s right=%s\n") % left % right);

  while (expand_ancestors(parents, ancestors, intern, app))
    {
      L(F("least common ancestor scan [par=%d,anc=%d]\n") %
        parents.size() % ancestors.size());

      if (find_intersecting_node(*lanc, *ranc, intern, anc))
        {
          L(F("found node %d, ancestor of left %s and right %s\n")
            % anc % left % right);
          return true;
        }
    }
//      dump_bitset_map("ancestors", ancestors);
//      dump_bitset_map("parents", parents);
  return false;
}


// FIXME: this algorithm is incredibly inefficient; it's O(n) where n is the
// size of the entire revision graph.

static bool
is_ancestor(revision_id const & ancestor_id,
            revision_id const & descendent_id,
            std::multimap<revision_id, revision_id> const & graph)
{

  std::set<revision_id> visited;
  std::queue<revision_id> queue;

  queue.push(ancestor_id);

  while (!queue.empty())
    {
      revision_id current_id = queue.front();
      queue.pop();

      if (current_id == descendent_id)
        return true;
      else
        {
          typedef std::multimap<revision_id, revision_id>::const_iterator gi;
          std::pair<gi, gi> children = graph.equal_range(current_id);
          for (gi i = children.first; i != children.second; ++i)
            {
              if (visited.find(i->second) == visited.end())
                {
                  queue.push(i->second);
                  visited.insert(i->second);
                }
            }
        }
    }
  return false;  
}

bool
is_ancestor(revision_id const & ancestor_id,
            revision_id const & descendent_id,
            app_state & app)
{
  L(F("checking whether %s is an ancestor of %s\n") % ancestor_id % descendent_id);

  std::multimap<revision_id, revision_id> graph;
  app.db.get_revision_ancestry(graph);
  return is_ancestor(ancestor_id, descendent_id, graph);
}


static void 
add_bitset_to_union(shared_bitmap src,
                    shared_bitmap dst)
{
  if (dst->size() > src->size())
    src->resize(dst->size());
  if (src->size() > dst->size())
    dst->resize(src->size());
  *dst |= *src;
}


static void 
calculate_ancestors_from_graph(interner<ctx> & intern,
                               revision_id const & init,
                               std::multimap<revision_id, revision_id> const & graph, 
                               std::map< ctx, shared_bitmap > & ancestors,
                               shared_bitmap & total_union)
{
  typedef std::multimap<revision_id, revision_id>::const_iterator gi;
  std::stack<ctx> stk;

  stk.push(intern.intern(init.inner()()));

  while (! stk.empty())
    {
      ctx us = stk.top();
      revision_id rev(hexenc<id>(intern.lookup(us)));

      std::pair<gi,gi> parents = graph.equal_range(rev);
      bool pushed = false;

      // first make sure all parents are done
      for (gi i = parents.first; i != parents.second; ++i)
        {
          ctx parent = intern.intern(i->second.inner()());
          if (ancestors.find(parent) == ancestors.end())
            {
              stk.push(parent);
              pushed = true;
              break;
            }
        }

      // if we pushed anything we stop now. we'll come back later when all
      // the parents are done.
      if (pushed)
        continue;

      shared_bitmap b = shared_bitmap(new bitmap());

      for (gi i = parents.first; i != parents.second; ++i)
        {
          ctx parent = intern.intern(i->second.inner()());

          // set all parents
          if (b->size() <= parent)
            b->resize(parent + 1);
          b->set(parent);

          // ensure all parents are loaded into the ancestor map
          I(ancestors.find(parent) != ancestors.end());

          // union them into our map
          std::map< ctx, shared_bitmap >::const_iterator j = ancestors.find(parent);
          I(j != ancestors.end());
          add_bitset_to_union(j->second, b);
        }

      add_bitset_to_union(b, total_union);
      ancestors.insert(std::make_pair(us, b));
      stk.pop();
    }
}

// this function actually toposorts the whole graph, and then filters by the
// passed in set.  if anyone ever needs to toposort the whole graph, then,
// this function would be a good thing to generalize...
void
toposort(std::set<revision_id> const & revisions,
         std::vector<revision_id> & sorted,
         app_state & app)
{
  sorted.clear();
  typedef std::multimap<revision_id, revision_id>::iterator gi;
  std::multimap<revision_id, revision_id> graph;
  app.db.get_revision_ancestry(graph);
  std::set<revision_id> leaves;
  app.db.get_revision_ids(leaves);
  while (!graph.empty())
    {
      // first find the set of current graph roots
      std::set<revision_id> roots;
      for (gi i = graph.begin(); i != graph.end(); ++i)
        roots.insert(i->first);
      for (gi i = graph.begin(); i != graph.end(); ++i)
        roots.erase(i->second);
      // now stick them in our ordering (if wanted), and remove them from the
      // graph
      for (std::set<revision_id>::const_iterator i = roots.begin();
           i != roots.end(); ++i)
        {
          L(F("new root: %s\n") % (*i));
          if (revisions.find(*i) != revisions.end())
            sorted.push_back(*i);
          graph.erase(*i);
          leaves.erase(*i);
        }
    }
  for (std::set<revision_id>::const_iterator i = leaves.begin();
       i != leaves.end(); ++i)
    {
      L(F("new leaf: %s\n") % (*i));
      if (revisions.find(*i) != revisions.end())
        sorted.push_back(*i);
    }
}

// This function looks at a set of revisions, and for every pair A, B in that
// set such that A is an ancestor of B, it erases A.

void
erase_ancestors(std::set<revision_id> & revisions, app_state & app)
{
  typedef std::multimap<revision_id, revision_id>::const_iterator gi;
  std::multimap<revision_id, revision_id> graph;
  std::multimap<revision_id, revision_id> inverse_graph;

  app.db.get_revision_ancestry(graph);
  for (gi i = graph.begin(); i != graph.end(); ++i)
    inverse_graph.insert(std::make_pair(i->second, i->first));

  interner<ctx> intern;
  std::map< ctx, shared_bitmap > ancestors;

  shared_bitmap u = shared_bitmap(new bitmap());

  for (std::set<revision_id>::const_iterator i = revisions.begin();
       i != revisions.end(); ++i)
    {      
      calculate_ancestors_from_graph(intern, *i, inverse_graph, ancestors, u);
    }

  std::set<revision_id> tmp;
  for (std::set<revision_id>::const_iterator i = revisions.begin();
       i != revisions.end(); ++i)
    {
      ctx id = intern.intern(i->inner()());
      bool has_ancestor_in_set = id < u->size() && u->test(id);
      if (!has_ancestor_in_set)
        tmp.insert(*i);
    }
  
  revisions = tmp;
}

// This function takes a revision A and a set of revision Bs, calculates the
// ancestry of each, and returns the set of revisions that are in A's ancestry
// but not in the ancestry of any of the Bs.  It tells you 'what's new' in A
// that's not in the Bs.  If the output set if non-empty, then A will
// certainly be in it; but the output set might be empty.
void
ancestry_difference(revision_id const & a, std::set<revision_id> const & bs,
                    std::set<revision_id> & new_stuff,
                    app_state & app)
{
  new_stuff.clear();
  typedef std::multimap<revision_id, revision_id>::const_iterator gi;
  std::multimap<revision_id, revision_id> graph;
  std::multimap<revision_id, revision_id> inverse_graph;

  app.db.get_revision_ancestry(graph);
  for (gi i = graph.begin(); i != graph.end(); ++i)
    inverse_graph.insert(std::make_pair(i->second, i->first));

  interner<ctx> intern;
  std::map< ctx, shared_bitmap > ancestors;

  shared_bitmap u = shared_bitmap(new bitmap());

  for (std::set<revision_id>::const_iterator i = bs.begin();
       i != bs.end(); ++i)
    {      
      calculate_ancestors_from_graph(intern, *i, inverse_graph, ancestors, u);
      ctx c = intern.intern(i->inner()());
      if (u->size() <= c)
        u->resize(c + 1);
      u->set(c);
    }

  shared_bitmap au = shared_bitmap(new bitmap());
  calculate_ancestors_from_graph(intern, a, inverse_graph, ancestors, au);
  {
    ctx c = intern.intern(a.inner()());
    if (au->size() <= c)
      au->resize(c + 1);
    au->set(c);
  }

  au->resize(std::max(au->size(), u->size()));
  u->resize(std::max(au->size(), u->size()));
  
  *au -= *u;

  for (unsigned int i = 0; i != au->size(); ++i)
  {
    if (au->test(i))
      {
        revision_id rid(intern.lookup(i));
        if (!null_id(rid))
          new_stuff.insert(rid);
      }
  }
}

// 
// The idea with this algorithm is to walk from child up to ancestor,
// recursively, accumulating all the change_sets associated with
// intermediate nodes into *one big change_set*.
//
// clever readers will realize this is an overlapping-subproblem type
// situation and thus needs to keep a dynamic programming map to keep
// itself in linear complexity.
//
// in fact, we keep two: one which maps to computed results (partial_csets)
// and one which just keeps a set of all nodes we traversed
// (visited_nodes). in theory it could be one map with an extra bool stuck
// on each entry, but I think that would make it even less readable. it's
// already quite ugly.
//

static bool 
calculate_change_sets_recursive(revision_id const & ancestor,
                                revision_id const & child,
                                app_state & app,
                                change_set & cumulative_cset,
                                std::map<revision_id, boost::shared_ptr<change_set> > & partial_csets,
                                std::set<revision_id> & visited_nodes,
                                std::set<revision_id> const & subgraph)
{

  if (ancestor == child)
    return true;

  if (subgraph.find(child) == subgraph.end())
    return false;

  visited_nodes.insert(child);

  bool relevant_child = false;

  revision_set rev;
  app.db.get_revision(child, rev);

  L(F("exploring changesets from parents of %s, seeking towards %s\n") 
    % child % ancestor);

  for(edge_map::const_iterator i = rev.edges.begin(); i != rev.edges.end(); ++i)
    {
      bool relevant_parent = false;
      revision_id curr_parent = edge_old_revision(i);

      if (curr_parent.inner()().empty())
        continue;

      change_set cset_to_curr_parent;

      L(F("considering parent %s of %s\n") % curr_parent % child);

      std::map<revision_id, boost::shared_ptr<change_set> >::const_iterator j = 
        partial_csets.find(curr_parent);
      if (j != partial_csets.end()) 
        {
          // a recursive call has traversed this parent before and built an
          // existing cset. just reuse that rather than re-traversing
          cset_to_curr_parent = *(j->second);
          relevant_parent = true;
        }
      else if (visited_nodes.find(curr_parent) != visited_nodes.end())
        {
          // a recursive call has traversed this parent, but there was no
          // path from it to the root, so the parent is irrelevant. skip.
          relevant_parent = false;
        }
      else
        relevant_parent = calculate_change_sets_recursive(ancestor, curr_parent, app, 
                                                          cset_to_curr_parent, 
                                                          partial_csets,
                                                          visited_nodes,
                                                          subgraph);

      if (relevant_parent)
        {
          L(F("revision %s is relevant, composing with edge to %s\n") 
            % curr_parent % child);
          concatenate_change_sets(cset_to_curr_parent, edge_changes(i), cumulative_cset);
          relevant_child = true;
          break;
        }
      else
        L(F("parent %s of %s is not relevant\n") % curr_parent % child);
    }

  // store the partial edge from ancestor -> child, so that if anyone
  // re-traverses this edge they'll just fetch from the partial_edges
  // cache.
  if (relevant_child)
    partial_csets.insert(std::make_pair(child, 
                                        boost::shared_ptr<change_set>
                                        (new change_set(cumulative_cset))));
  
  return relevant_child;
}

// this finds (by breadth-first search) the set of nodes you'll have to
// walk over in calculate_change_sets_recursive, to build the composite
// changeset. this is to prevent the recursive algorithm from going way
// back in history on an unlucky guess of parent.

static void
find_subgraph_for_composite_search(revision_id const & ancestor,
                                   revision_id const & child,
                                   app_state & app,
                                   std::set<revision_id> & subgraph)
{
  std::set<revision_id> frontier;
  frontier.insert(child);
  subgraph.insert(child);
  while (!frontier.empty())
    {
      std::set<revision_id> next_frontier;      
      for (std::set<revision_id>::const_iterator i = frontier.begin();
           i != frontier.end(); ++i)
        {
          revision_set rev;
          app.db.get_revision(*i, rev);
          L(F("adding parents of %s to subgraph\n") % *i);
          
          for(edge_map::const_iterator j = rev.edges.begin(); j != rev.edges.end(); ++j)
            {
              revision_id curr_parent = edge_old_revision(j);
              if (null_id(curr_parent))
                continue;
              subgraph.insert(curr_parent);
              if (curr_parent == ancestor)
                {
                  L(F("found parent %s of %s\n") % curr_parent % *i);
                  return;
                }
              else
                L(F("adding parent %s to next frontier\n") % curr_parent);
                next_frontier.insert(curr_parent);
            }
        }
      frontier = next_frontier;
    }
}

void 
calculate_composite_change_set(revision_id const & ancestor,
                               revision_id const & child,
                               app_state & app,
                               change_set & composed)
{
  I(composed.empty());
  L(F("calculating composite changeset between %s and %s\n")
    % ancestor % child);
  if (ancestor == child)
    return;
  std::set<revision_id> visited;
  std::set<revision_id> subgraph;
  std::map<revision_id, boost::shared_ptr<change_set> > partial;
  find_subgraph_for_composite_search(ancestor, child, app, subgraph);
  calculate_change_sets_recursive(ancestor, child, app, composed, partial, 
                                  visited, subgraph);
}

void
calculate_arbitrary_change_set(revision_id const & start,
                               revision_id const & end,
                               app_state & app,
                               change_set & composed)
{
  L(F("calculating changeset from %s to %s\n") % start % end);
  revision_id r_ca_id;
  change_set ca_to_start, ca_to_end, start_to_ca;
  N(find_least_common_ancestor(start, end, r_ca_id, app),
    F("no common ancestor for %s and %s\n") % start % end);
  L(F("common ancestor is %s\n") % r_ca_id);
  calculate_composite_change_set(r_ca_id, start, app, ca_to_start);
  calculate_composite_change_set(r_ca_id, end, app, ca_to_end);
  manifest_id m_ca_id;
  manifest_map m_ca;
  app.db.get_revision_manifest(r_ca_id, m_ca_id);
  app.db.get_manifest(m_ca_id, m_ca);
  invert_change_set(ca_to_start, m_ca, start_to_ca);
  concatenate_change_sets(start_to_ca, ca_to_end, composed);
}


// Stuff related to rebuilding the revision graph. Unfortunately this is a
// real enough error case that we need support code for it.


static void 
analyze_manifest_changes(app_state & app,
                         manifest_id const & parent, 
                         manifest_id const & child, 
                         std::set<file_path> const & need_history_splitting,
                         change_set & cs)
{
  manifest_map m_parent, m_child;

  if (!null_id(parent))
    app.db.get_manifest(parent, m_parent);

  I(!null_id(child));
  app.db.get_manifest(child, m_child);

  L(F("analyzing manifest changes from '%s' -> '%s'\n") % parent % child);

  for (manifest_map::const_iterator i = m_parent.begin(); 
       i != m_parent.end(); ++i)
    {
      file_path f = manifest_entry_path(i);
      manifest_map::const_iterator j = m_child.find(f);
      if (j == m_child.end())
        {
          cs.delete_file(f);
        }
      else if (need_history_splitting.find(f) != need_history_splitting.end())
        {
          P(F("splitting ancestry for file %s\n") % f);
          cs.delete_file(f);
          cs.add_file(f, manifest_entry_id(j));
        }
      else if (! (manifest_entry_id(i) == manifest_entry_id(j)))
        {
          cs.apply_delta(manifest_entry_path(i),
                         manifest_entry_id(i), 
                         manifest_entry_id(j));
        }
    }
  for (manifest_map::const_iterator i = m_child.begin(); 
       i != m_child.end(); ++i)
    {
      manifest_map::const_iterator j = m_parent.find(manifest_entry_path(i));
      if (j == m_parent.end())
        cs.add_file(manifest_entry_path(i),
                    manifest_entry_id(i));
    }
}


struct anc_graph
{
  anc_graph(bool existing, app_state & a) : 
    existing_graph(existing),
    app(a), 
    max_node(0), 
    n_nodes("nodes", "n", 1),
    n_certs_in("certs in", "c", 1),
    n_revs_out("revs out", "r", 1),
    n_certs_out("certs out", "C", 1)
  {}
  
  bool existing_graph;
  app_state & app;
  u64 max_node;

  ticker n_nodes;
  ticker n_certs_in;
  ticker n_revs_out;
  ticker n_certs_out;

  std::map<u64,manifest_id> node_to_old_man;
  std::map<manifest_id,u64> old_man_to_node;

  std::map<u64,revision_id> node_to_old_rev;
  std::map<revision_id,u64> old_rev_to_node;

  std::map<u64,revision_id> node_to_new_rev;
  std::multimap<u64, std::pair<cert_name, cert_value> > certs;
  std::multimap<u64, u64> ancestry;
  std::set<std::string> branches;
  
  void add_node_ancestry(u64 child, u64 parent);  
  void write_certs();
  void kluge_for_3_ancestor_nodes();
  void rebuild_ancestry();
  void get_node_manifest(u64 node, manifest_id & man);
  u64 add_node_for_old_manifest(manifest_id const & man);
  u64 add_node_for_old_revision(revision_id const & rev);                     
  revision_id construct_revision_from_ancestry(u64 child);
};


void anc_graph::add_node_ancestry(u64 child, u64 parent)
{
  L(F("noting ancestry from child %d -> parent %d\n") % child % parent);
  ancestry.insert(std::make_pair(child, parent));
}

void anc_graph::get_node_manifest(u64 node, manifest_id & man)
{
  std::map<u64,manifest_id>::const_iterator i = node_to_old_man.find(node);
  I(i != node_to_old_man.end());
  man = i->second;
}

void anc_graph::write_certs()
{
  std::set<cert_name> cnames;
  cnames.insert(cert_name(branch_cert_name));
  cnames.insert(cert_name(date_cert_name));
  cnames.insert(cert_name(author_cert_name));
  cnames.insert(cert_name(tag_cert_name));
  cnames.insert(cert_name(changelog_cert_name));
  cnames.insert(cert_name(comment_cert_name));
  cnames.insert(cert_name(testresult_cert_name));


  {
    // regenerate epochs on all branches to random states
    
    for (std::set<std::string>::const_iterator i = branches.begin(); i != branches.end(); ++i)
      {
        char buf[constants::epochlen_bytes];
        Botan::Global_RNG::randomize(reinterpret_cast<Botan::byte *>(buf), constants::epochlen_bytes);
        hexenc<data> hexdata;
        encode_hexenc(data(std::string(buf, buf + constants::epochlen_bytes)), hexdata);
        epoch_data new_epoch(hexdata);
        L(F("setting epoch for %s to %s\n") % *i % new_epoch);
        app.db.set_epoch(cert_value(*i), new_epoch);
      }
  }


  typedef std::multimap<u64, std::pair<cert_name, cert_value> >::const_iterator ci;
    
  for (std::map<u64,revision_id>::const_iterator i = node_to_new_rev.begin();
       i != node_to_new_rev.end(); ++i)
    {
      revision_id rev(i->second);

      std::pair<ci,ci> range = certs.equal_range(i->first);
        
      for (ci j = range.first; j != range.second; ++j)
        {
          cert_name name(j->second.first);
          cert_value val(j->second.second);

          if (cnames.find(name) == cnames.end())
            continue;

          cert new_cert;
          make_simple_cert(rev.inner(), name, val, app, new_cert);
          revision<cert> rcert(new_cert);
          if (! app.db.revision_cert_exists(rcert))
            {
              ++n_certs_out;
              app.db.put_revision_cert(rcert);
            }
        }        
    }    
}

void
anc_graph::kluge_for_3_ancestor_nodes()
{
  // This method is, as the name suggests, a kluge.  It exists because in the
  // 0.17 timeframe, monotone's ancestry graph has several nodes with 3
  // parents.  This isn't, in principle, necessarily a bad thing; having 3
  // parents is reasonably well defined, I don't know of much code that is
  // dependent on revisions having only 2 parents, etc.  But it is a very
  // weird thing, that we would never under any circumstances create today,
  // and it only exists as a side-effect of the pre-changeset days.  In the
  // future we may decide to allow 3+-parent revisions; we may decide to
  // disallow it.  Right now, I'd rather keep options open.
  // We remove only edges that are "redundant" (i.e., already weird...).
  // These are also something that we currently refuse to produce -- when a
  // node has more than one parent, and one parent is an ancestor of another.
  // These edges, again, only exist because of weirdnesses in the
  // pre-changeset days, and are not particularly meaningful.  Again, we may
  // decide in the future to use them for some things, but...
  // FIXME: remove this method eventually, since it is (mildly) destructive on
  // history, and isn't really doing anything that necessarily needs to happen
  // anyway.
  P(F("scanning for nodes with 3+ parents\n"));
  std::set<u64> manyparents;
  for (std::multimap<u64, u64>::const_iterator i = ancestry.begin();
       i != ancestry.end(); ++i)
    {
      if (ancestry.count(i->first) > 2)
        manyparents.insert(i->first);
    }
  for (std::set<u64>::const_iterator i = manyparents.begin();
       i != manyparents.end(); ++i)
    {
      std::set<u64> indirect_ancestors;
      std::set<u64> parents;
      std::vector<u64> to_examine;
      for (std::multimap<u64, u64>::const_iterator j = ancestry.lower_bound(*i);
           j != ancestry.upper_bound(*i); ++j)
        {
          to_examine.push_back(j->second);
          parents.insert(j->second);
        }
      I(!to_examine.empty());
      while (!to_examine.empty())
        {
          u64 current = to_examine.back();
          to_examine.pop_back();
          for (std::multimap<u64, u64>::const_iterator j = ancestry.lower_bound(current);
               j != ancestry.upper_bound(current); ++j)
            {
              if (indirect_ancestors.find(j->second) == indirect_ancestors.end())
                {
                  to_examine.push_back(j->second);
                  indirect_ancestors.insert(j->second);
                }
            }
        }
      size_t killed = 0;
      for (std::set<u64>::const_iterator p = parents.begin();
           p != parents.end(); ++p)
        {
          if (indirect_ancestors.find(*p) != indirect_ancestors.end())
            {
              P(F("optimizing out redundant edge %i -> %i\n") % (*p) % (*i));
              // sometimes I hate STL.  or at least my incompetence at using it.
              size_t old_size = ancestry.size();
              for (std::multimap<u64, u64>::iterator e = ancestry.lower_bound(*i);
                   e != ancestry.upper_bound(*i); ++e)
                {
                  I(e->first == *i);
                  if (e->second == *p)
                    {
                      ancestry.erase(e);
                      break;
                    }
                }
              I(old_size - 1 == ancestry.size());
              ++killed;
            }
        }
      I(killed < parents.size());
      I(ancestry.find(*i) != ancestry.end());
    }
}

void
anc_graph::rebuild_ancestry()
{
  kluge_for_3_ancestor_nodes();

  P(F("rebuilding %d nodes\n") % max_node);
  {
    transaction_guard guard(app.db);
    if (existing_graph)
      app.db.delete_existing_revs_and_certs();
    
    std::set<u64> parents, children, heads;
    for (std::multimap<u64, u64>::const_iterator i = ancestry.begin();
         i != ancestry.end(); ++i)
      {
        children.insert(i->first);
        parents.insert(i->second);
      }
    set_difference(children.begin(), children.end(),
                   parents.begin(), parents.end(),
                   std::inserter(heads, heads.begin()));

    // FIXME: should do a depth-first traversal here, or something like,
    // instead of being recursive.
    for (std::set<u64>::const_iterator i = heads.begin();
         i != heads.end(); ++i)
      {
        construct_revision_from_ancestry(*i);
      }
    write_certs();
    guard.commit();
  }
}

u64 anc_graph::add_node_for_old_manifest(manifest_id const & man)
{
  I(!existing_graph);
  u64 node = 0;
  if (old_man_to_node.find(man) == old_man_to_node.end())
    {
      node = max_node++;
      ++n_nodes;
      L(F("node %d = manifest %s\n") % node % man);
      old_man_to_node.insert(std::make_pair(man, node));
      node_to_old_man.insert(std::make_pair(node, man));
      
      // load certs
      std::vector< manifest<cert> > mcerts;
      app.db.get_manifest_certs(man, mcerts);
      erase_bogus_certs(mcerts, app);      
      for(std::vector< manifest<cert> >::const_iterator i = mcerts.begin();
          i != mcerts.end(); ++i)
        {
          L(F("loaded '%s' manifest cert for node %s\n") % i->inner().name % node);
          cert_value tv;
          decode_base64(i->inner().value, tv);
          ++n_certs_in;
          certs.insert(std::make_pair(node, 
                                      std::make_pair(i->inner().name, tv)));
        }
    }
  else
    {
      node = old_man_to_node[man];
    }
  return node;
}

u64 anc_graph::add_node_for_old_revision(revision_id const & rev)
{
  I(existing_graph);
  I(!null_id(rev));
  u64 node = 0;
  if (old_rev_to_node.find(rev) == old_rev_to_node.end())
    {
      node = max_node++;
      ++n_nodes;
      
      manifest_id man;
      app.db.get_revision_manifest(rev, man);
      
      L(F("node %d = revision %s = manifest %s\n") % node % rev % man);
      old_rev_to_node.insert(std::make_pair(rev, node));
      node_to_old_rev.insert(std::make_pair(node, rev));
      node_to_old_man.insert(std::make_pair(node, man));

      // load certs
      std::vector< revision<cert> > rcerts;
      app.db.get_revision_certs(rev, rcerts);
      erase_bogus_certs(rcerts, app);      
      for(std::vector< revision<cert> >::const_iterator i = rcerts.begin();
          i != rcerts.end(); ++i)
        {
          L(F("loaded '%s' revision cert for node %s\n") % i->inner().name % node);
          cert_value tv;
          decode_base64(i->inner().value, tv);
          ++n_certs_in;
          certs.insert(std::make_pair(node, 
                                      std::make_pair(i->inner().name, tv)));

          if (i->inner().name == branch_cert_name)            
            branches.insert(tv());
        }
    }
  else
    {
      node = old_rev_to_node[rev];
    }

  return node;
}

// FIXME: this is recursive -- stack depth grows as ancestry depth -- and will
// overflow the stack on large histories.
revision_id
anc_graph::construct_revision_from_ancestry(u64 child)
{
  L(F("processing node %d\n") % child);

  if (node_to_new_rev.find(child) != node_to_new_rev.end())
    {
      L(F("node %d already processed, skipping\n") % child);
      return node_to_new_rev.find(child)->second;
    }

  manifest_id child_man;
  get_node_manifest(child, child_man);

  revision_set rev;
  rev.new_manifest = child_man;

  typedef std::multimap<u64, u64>::const_iterator ci;
  std::pair<ci,ci> range = ancestry.equal_range(child);
  if (range.first == range.second)
    {
      L(F("node %d is a root node\n") % child);
      revision_id null_rid;
      manifest_id null_mid;
      boost::shared_ptr<change_set> cs(new change_set());
      std::set<file_path> blah;
      analyze_manifest_changes(app, null_mid, child_man, blah, *cs);
      rev.edges.insert(std::make_pair(null_rid,
                                      std::make_pair(null_mid, cs)));
    }
  else if (std::distance(range.first, range.second) == 1)
    {
      I(child == range.first->first);
      u64 parent = range.first->second;
      if (node_to_new_rev.find(parent) == node_to_new_rev.end())
        construct_revision_from_ancestry(parent);
      revision_id parent_rid = node_to_new_rev.find(parent)->second;
      L(F("parent node %d = revision %s\n") % parent % parent_rid);      
      manifest_id parent_man;
      get_node_manifest(parent, parent_man);
      boost::shared_ptr<change_set> cs(new change_set());
      std::set<file_path> need_killing_files;
      analyze_manifest_changes(app, parent_man, child_man, need_killing_files,
                               *cs);
      rev.edges.insert(std::make_pair(parent_rid,
                                      std::make_pair(parent_man, cs)));
    }
  else
    {
      // this section has lots and lots of rigmorale, in order to handle the
      // following case: A -> B -> D, A -> C -> D.  File "foo" exists in
      // manifests A, C, and D only.  (I.e., it was deleted and then re-added
      // along the B path, and left alone along the C path.)  The problem here
      // is that we have to synthesize a delete/add pair on the C -> D edge,
      // to make sure that the A -> D changeset is path invariant -- we have
      // to make sure that no matter how you go from A to D, we will learn
      // that A's "foo" and D's "foo" are actually logically distinct files.
      //
      // In order to do this, before we generate the changeset for any merged
      // revision, we calculate the changeset through from the parents's
      // common ancestor to the _other_ parent, which will tell us which files
      // have been deleted since then.  We also calculate the changeset
      // through from the parents's common ancestor to the current parent,
      // which tells us which files have already been deleted on our side, and
      // so don't need to be deleted again.
      //
      // Finally, we feed the list of deleted files to analyze_manifest, which
      // uses that information to break file ancestries when necessary.
      // 
      // we only know how to preserve file ids when there are exactly two
      // parents, so assert that there are.
      std::vector<u64> parents, others;
      {
        u64 left_p, right_p;
        ci i = range.first;
        I(child == i->first);
        left_p = i->second;
        ++i;
        I(child == i->first);
        right_p = i->second;
        ++i;
        I(i == range.second);
        parents.push_back(left_p);
        others.push_back(right_p);
        parents.push_back(right_p);
        others.push_back(left_p);
      }
      // make sure our parents are all saved.
      for (std::vector<u64>::const_iterator i = parents.begin(); i != parents.end(); ++i)
        {
          if (node_to_new_rev.find(*i) == node_to_new_rev.end())
            construct_revision_from_ancestry(*i);
          I(node_to_new_rev.find(*i) != node_to_new_rev.end());
        }
      // actually process the two nodes
      for (int i = 0; i != 2; ++i)
        {
          u64 parent = idx(parents, i);
          u64 other_parent = idx(others, i);
          L(F("processing edge from child %d -> parent %d\n") % child % parent);

          revision_id parent_rid = node_to_new_rev.find(parent)->second;
          revision_id other_parent_rid = node_to_new_rev.find(other_parent)->second;
          // this is stupidly inefficient, in that we do this whole expensive
          // changeset finding thing twice in a row.  oh well.
          revision_id lca;
          std::set<file_path> need_killing_files;
          if (find_least_common_ancestor(parent_rid, other_parent_rid, lca, app))
            {
              change_set parent_cs, other_parent_cs;
              calculate_composite_change_set(lca, other_parent_rid, app, other_parent_cs);
              calculate_composite_change_set(lca, parent_rid, app, parent_cs);
              std::set_difference(other_parent_cs.rearrangement.deleted_files.begin(),
                                  other_parent_cs.rearrangement.deleted_files.end(),
                                  parent_cs.rearrangement.deleted_files.begin(),
                                  parent_cs.rearrangement.deleted_files.end(),
                                  std::inserter(need_killing_files,
                                                need_killing_files.begin()));
            }

          L(F("parent node %d = revision %s\n") % parent % parent_rid);      
          manifest_id parent_man;
          get_node_manifest(parent, parent_man);
          boost::shared_ptr<change_set> cs(new change_set());
          analyze_manifest_changes(app, parent_man, child_man, need_killing_files,
                                   *cs);
          rev.edges.insert(std::make_pair(parent_rid,
                                          std::make_pair(parent_man, cs)));
        }
    }

  revision_id rid;
  calculate_ident(rev, rid);
  node_to_new_rev.insert(std::make_pair(child, rid));

  if (!app.db.revision_exists (rid))
    {
      L(F("mapped node %d to revision %s\n") % child % rid);
      app.db.put_revision(rid, rev);
      ++n_revs_out;
    }
  else
    {
      L(F("skipping already existing revision %s\n") % rid);
    }

  return rid;  
}

void 
build_changesets_from_existing_revs(app_state & app)
{
  global_sanity.set_relaxed(true);
  anc_graph graph(true, app);

  P(F("rebuilding revision graph from existing graph\n"));
  std::multimap<revision_id, revision_id> existing_graph;

  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    N(guess_default_key(key,app),
      F("no unique private key for cert construction"));
    require_password(key, app);
  }

  app.db.get_revision_ancestry(existing_graph);
  for (std::multimap<revision_id, revision_id>::const_iterator i = existing_graph.begin();
       i != existing_graph.end(); ++i)
    {
      if (!null_id(i->first))
        {
          u64 parent_node = graph.add_node_for_old_revision(i->first);
          u64 child_node = graph.add_node_for_old_revision(i->second);
          graph.add_node_ancestry(child_node, parent_node);
        }
    }

  global_sanity.set_relaxed(false);
  graph.rebuild_ancestry();
}


void 
build_changesets_from_manifest_ancestry(app_state & app)
{
  anc_graph graph(false, app);

  P(F("rebuilding revision graph from manifest certs\n"));

  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    N(guess_default_key(key,app),
      F("no unique private key for cert construction"));
    require_password(key, app);
  }

  std::vector< manifest<cert> > tmp;
  app.db.get_manifest_certs(cert_name("ancestor"), tmp);
  erase_bogus_certs(tmp, app);

  for (std::vector< manifest<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      manifest_id child, parent;
      child = i->inner().ident;
      parent = hexenc<id>(tv());

      u64 parent_node = graph.add_node_for_old_manifest(parent);
      u64 child_node = graph.add_node_for_old_manifest(child);
      graph.add_node_ancestry(child_node, parent_node);
    }
  
  graph.rebuild_ancestry();
}


// i/o stuff

std::string revision_file_name("revision");

namespace 
{
  namespace syms
  {
    std::string const old_revision("old_revision");
    std::string const new_manifest("new_manifest");
    std::string const old_manifest("old_manifest");
  }
}


void 
print_edge(basic_io::printer & printer,
           edge_entry const & e)
{       
  basic_io::stanza st;
  st.push_hex_pair(syms::old_revision, edge_old_revision(e).inner()());
  st.push_hex_pair(syms::old_manifest, edge_old_manifest(e).inner()());
  printer.print_stanza(st);
  print_change_set(printer, edge_changes(e)); 
}


void 
print_revision(basic_io::printer & printer,
               revision_set const & rev)
{
  rev.check_sane();
  basic_io::stanza st; 
  st.push_hex_pair(syms::new_manifest, rev.new_manifest.inner()());
  printer.print_stanza(st);
  for (edge_map::const_iterator edge = rev.edges.begin();
       edge != rev.edges.end(); ++edge)
    print_edge(printer, *edge);
}


void 
parse_edge(basic_io::parser & parser,
           edge_map & es)
{
  boost::shared_ptr<change_set> cs(new change_set());
  manifest_id old_man;
  revision_id old_rev;
  std::string tmp;
  
  parser.esym(syms::old_revision);
  parser.hex(tmp);
  old_rev = revision_id(tmp);
  
  parser.esym(syms::old_manifest);
  parser.hex(tmp);
  old_man = manifest_id(tmp);
  
  parse_change_set(parser, *cs);

  es.insert(std::make_pair(old_rev, std::make_pair(old_man, cs)));
}


void 
parse_revision(basic_io::parser & parser,
               revision_set & rev)
{
  rev.edges.clear();
  std::string tmp;
  parser.esym(syms::new_manifest);
  parser.hex(tmp);
  rev.new_manifest = manifest_id(tmp);
  while (parser.symp(syms::old_revision))
    parse_edge(parser, rev.edges);
  rev.check_sane();
}

void 
read_revision_set(data const & dat,
                  revision_set & rev)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "revision");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_revision(pars, rev);
  I(src.lookahead == EOF);
  rev.check_sane();
}

void 
read_revision_set(revision_data const & dat,
                  revision_set & rev)
{
  data unpacked;
  unpack(dat.inner(), unpacked);
  read_revision_set(unpacked, rev);
  rev.check_sane();
}

void
write_revision_set(revision_set const & rev,
                   data & dat)
{
  rev.check_sane();
  std::ostringstream oss;
  basic_io::printer pr(oss);
  print_revision(pr, rev);
  dat = data(oss.str());
}

void
write_revision_set(revision_set const & rev,
                   revision_data & dat)
{
  rev.check_sane();
  data d;
  write_revision_set(rev, d);
  base64< gzip<data> > packed;
  pack(d, packed);
  dat = revision_data(packed);
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "sanity.hh"

static void 
revision_test()
{
}

void 
add_revision_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&revision_test));
}


#endif // BUILD_UNIT_TESTS
