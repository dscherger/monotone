// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "sanity.hh"
#include "revision.hh"
#include "rev_height.hh"
#include "roster.hh"

#include "database.hh"
#include "interner.hh"

#include "safe_map.hh"
#include <stack>
#include <boost/shared_ptr.hpp>
#include <boost/dynamic_bitset.hpp>

using std::make_pair;
using std::map;
using std::max;
using std::multimap;
using std::pair;
using std::set;
using std::stack;
using std::vector;

using boost::shared_ptr;
using boost::dynamic_bitset;

// For a surprisingly long time, we have been using an algorithm which
// is nonsense, based on a misunderstanding of what "LCA" means. The
// LCA of two nodes is *not* the first common ancestor which you find
// when iteratively expanding their ancestor sets. Instead, the LCA is
// the common ancestor which is a descendent of all other common
// ancestors.
//
// In general, a set of nodes in a DAG doesn't always have an
// LCA. There might be multiple common ancestors which are not parents
// of one another. So we implement something which is "functionally
// useful" for finding a merge point (and moreover, which always
// terminates): we find an LCA of the input set if it exists,
// otherwise we replace the input set with the nodes we did find and
// repeat.
//
// All previous discussions in monotone-land, before say August 2005,
// of LCA (and LCAD) are essentially wrong due to our silly
// misunderstanding. It's unfortunate, but our half-baked
// approximations worked almost well enough to take us through 3 years
// of deployed use. Hopefully this more accurate new use will serve us
// even longer.

typedef unsigned long ctx;
typedef dynamic_bitset<> bitmap;
typedef shared_ptr<bitmap> shared_bitmap;

static void
calculate_ancestors_from_graph(interner<ctx> & intern,
                               revision_id const & init,
                               multimap<revision_id, revision_id> const & graph,
                               map< ctx, shared_bitmap > & ancestors,
                               shared_bitmap & total_union);

void
find_common_ancestor_for_merge(database & db,
                               revision_id const & left,
                               revision_id const & right,
                               revision_id & anc)
{
  interner<ctx> intern;
  set<ctx> leaves;
  map<ctx, shared_bitmap> ancestors;

  shared_bitmap isect = shared_bitmap(new bitmap());
  shared_bitmap isect_ancs = shared_bitmap(new bitmap());

  leaves.insert(intern.intern(left.inner()()));
  leaves.insert(intern.intern(right.inner()()));


  multimap<revision_id, revision_id> inverse_graph;
  {
    multimap<revision_id, revision_id> graph;
    db.get_revision_ancestry(graph);
    typedef multimap<revision_id, revision_id>::const_iterator gi;
    for (gi i = graph.begin(); i != graph.end(); ++i)
      inverse_graph.insert(make_pair(i->second, i->first));
  }


  while (leaves.size() != 1)
    {
      isect->clear();
      isect_ancs->clear();

      // First intersect all ancestors of current leaf set
      for (set<ctx>::const_iterator i = leaves.begin(); i != leaves.end(); ++i)
        {
          ctx curr_leaf = *i;
          shared_bitmap curr_leaf_ancestors;
          map<ctx, shared_bitmap >::const_iterator j = ancestors.find(*i);
          if (j != ancestors.end())
            curr_leaf_ancestors = j->second;
          else
            {
              curr_leaf_ancestors = shared_bitmap(new bitmap());
              calculate_ancestors_from_graph(intern, revision_id(intern.lookup(curr_leaf),
                                                                 origin::internal),
                                             inverse_graph, ancestors,
                                             curr_leaf_ancestors);
            }
          if (isect->size() > curr_leaf_ancestors->size())
            curr_leaf_ancestors->resize(isect->size());

          if (curr_leaf_ancestors->size() > isect->size())
            isect->resize(curr_leaf_ancestors->size());

          if (i == leaves.begin())
            *isect = *curr_leaf_ancestors;
          else
            (*isect) &= (*curr_leaf_ancestors);
        }

      // isect is now the set of common ancestors of leaves, but that is not enough.
      // We need the set of leaves of isect; to do that we calculate the set of
      // ancestors of isect, in order to subtract it from isect (below).
      set<ctx> new_leaves;
      for (ctx i = 0; i < isect->size(); ++i)
        {
          if (isect->test(i))
            {
              calculate_ancestors_from_graph(intern, revision_id(intern.lookup(i),
                                                                 origin::internal),
                                             inverse_graph, ancestors, isect_ancs);
            }
        }

      // Finally, the subtraction step: for any element i of isect, if
      // it's *not* in isect_ancs, it survives as a new leaf.
      leaves.clear();
      for (ctx i = 0; i < isect->size(); ++i)
        {
          if (!isect->test(i))
            continue;
          if (i < isect_ancs->size() && isect_ancs->test(i))
            continue;
          safe_insert(leaves, i);
        }
    }

  I(leaves.size() == 1);
  anc = revision_id(intern.lookup(*leaves.begin()), origin::internal);
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
                               multimap<revision_id, revision_id> const & graph,
                               map< ctx, shared_bitmap > & ancestors,
                               shared_bitmap & total_union)
{
  typedef multimap<revision_id, revision_id>::const_iterator gi;
  stack<ctx> stk;

  stk.push(intern.intern(init.inner()()));

  while (! stk.empty())
    {
      ctx us = stk.top();
      revision_id rev(intern.lookup(us), origin::internal);

      pair<gi,gi> parents = graph.equal_range(rev);
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
          map< ctx, shared_bitmap >::const_iterator j = ancestors.find(parent);
          I(j != ancestors.end());
          add_bitset_to_union(j->second, b);
        }

      add_bitset_to_union(b, total_union);
      ancestors.insert(make_pair(us, b));
      stk.pop();
    }
}

void
toposort(database & db,
         set<revision_id> const & revisions,
         vector<revision_id> & sorted)
{
  map<rev_height, revision_id> work;

  for (set<revision_id>::const_iterator i = revisions.begin();
       i != revisions.end(); ++i)
    {
      rev_height height;
      db.get_rev_height(*i, height);
      work.insert(make_pair(height, *i));
    }

  sorted.clear();

  for (map<rev_height, revision_id>::const_iterator i = work.begin();
       i != work.end(); ++i)
    {
      sorted.push_back(i->second);
    }
}

static void
accumulate_strict_ancestors(database & db,
                            revision_id const & start,
                            set<revision_id> & all_ancestors,
                            multimap<revision_id, revision_id> const & inverse_graph,
                            rev_height const & min_height)
{
  typedef multimap<revision_id, revision_id>::const_iterator gi;

  vector<revision_id> frontier;
  frontier.push_back(start);

  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      pair<gi, gi> parents = inverse_graph.equal_range(rid);
      for (gi i = parents.first; i != parents.second; ++i)
        {
          revision_id const & parent = i->second;
          if (all_ancestors.find(parent) == all_ancestors.end())
            {
              // prune if we're below min_height
              rev_height h;
              db.get_rev_height(parent, h);
              if (h >= min_height)
                {
                  all_ancestors.insert(parent);
                  frontier.push_back(parent);
                }
            }
        }
    }
}

// this call is equivalent to running:
//   erase(remove_if(candidates.begin(), candidates.end(), p));
//   erase_ancestors(candidates, db);
// however, by interleaving the two operations, it can in common cases make
// many fewer calls to the predicate, which can be a significant speed win.

void
erase_ancestors_and_failures(database & db,
                             std::set<revision_id> & candidates,
                             is_failure & p,
                             multimap<revision_id, revision_id> *inverse_graph_cache_ptr)
{
  // Load up the ancestry graph
  multimap<revision_id, revision_id> inverse_graph;

  if (candidates.empty())
    return;

  if (inverse_graph_cache_ptr == NULL)
    inverse_graph_cache_ptr = &inverse_graph;
  if (inverse_graph_cache_ptr->empty())
  {
    multimap<revision_id, revision_id> graph;
    db.get_revision_ancestry(graph);
    for (multimap<revision_id, revision_id>::const_iterator i = graph.begin();
         i != graph.end(); ++i)
      inverse_graph_cache_ptr->insert(make_pair(i->second, i->first));
  }

  // Keep a set of all ancestors that we've traversed -- to avoid
  // combinatorial explosion.
  set<revision_id> all_ancestors;

  rev_height min_height;
  db.get_rev_height(*candidates.begin(), min_height);
  for (std::set<revision_id>::const_iterator it = candidates.begin(); it != candidates.end(); it++)
    {
      rev_height h;
      db.get_rev_height(*it, h);
      if (h < min_height)
        min_height = h;
    }

  vector<revision_id> todo(candidates.begin(), candidates.end());
  std::random_shuffle(todo.begin(), todo.end());

  size_t predicates = 0;
  while (!todo.empty())
    {
      revision_id rid = todo.back();
      todo.pop_back();
      // check if this one has already been eliminated
      if (all_ancestors.find(rid) != all_ancestors.end())
        continue;
      // and then whether it actually should stay in the running:
      ++predicates;
      if (p(rid))
        {
          candidates.erase(rid);
          continue;
        }
      // okay, it is good enough that all its ancestors should be
      // eliminated
      accumulate_strict_ancestors(db, rid, all_ancestors, *inverse_graph_cache_ptr, min_height);
    }

  // now go and eliminate the ancestors
  for (set<revision_id>::const_iterator i = all_ancestors.begin();
       i != all_ancestors.end(); ++i)
    candidates.erase(*i);

  L(FL("called predicate %s times") % predicates);
}

// This function looks at a set of revisions, and for every pair A, B in that
// set such that A is an ancestor of B, it erases A.

namespace
{
  struct no_failures : public is_failure
  {
    virtual bool operator()(revision_id const & rid)
    {
      return false;
    }
  };
}
void
erase_ancestors(database & db, set<revision_id> & revisions)
{
  no_failures p;
  erase_ancestors_and_failures(db, revisions, p);
}

// This function takes a revision A and a set of revision Bs, calculates the
// ancestry of each, and returns the set of revisions that are in A's ancestry
// but not in the ancestry of any of the Bs.  It tells you 'what's new' in A
// that's not in the Bs.  If the output set if non-empty, then A will
// certainly be in it; but the output set might be empty.
void
ancestry_difference(database & db, revision_id const & a,
                    set<revision_id> const & bs,
                    set<revision_id> & new_stuff)
{
  new_stuff.clear();
  typedef multimap<revision_id, revision_id>::const_iterator gi;
  multimap<revision_id, revision_id> graph;
  multimap<revision_id, revision_id> inverse_graph;

  db.get_revision_ancestry(graph);
  for (gi i = graph.begin(); i != graph.end(); ++i)
    inverse_graph.insert(make_pair(i->second, i->first));

  interner<ctx> intern;
  map< ctx, shared_bitmap > ancestors;

  shared_bitmap u = shared_bitmap(new bitmap());

  for (set<revision_id>::const_iterator i = bs.begin();
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

  au->resize(max(au->size(), u->size()));
  u->resize(max(au->size(), u->size()));

  *au -= *u;

  for (unsigned int i = 0; i != au->size(); ++i)
  {
    if (au->test(i))
      {
        revision_id rid(intern.lookup(i), origin::internal);
        if (!null_id(rid))
          new_stuff.insert(rid);
      }
  }
}

void
select_nodes_modified_by_rev(database & db,
                             revision_t const & rev,
                             roster_t const new_roster,
                             set<node_id> & nodes_modified)
{
  nodes_modified.clear();

  for (edge_map::const_iterator i = rev.edges.begin();
       i != rev.edges.end(); ++i)
    {
      set<node_id> edge_nodes_modified;
      roster_t old_roster;
      db.get_roster(edge_old_revision(i), old_roster);
      select_nodes_modified_by_cset(edge_changes(i),
                                    old_roster,
                                    new_roster,
                                    edge_nodes_modified);

      copy(edge_nodes_modified.begin(), edge_nodes_modified.end(),
                inserter(nodes_modified, nodes_modified.begin()));
    }
}

// These functions create new ancestry!

namespace {
  struct true_node_id_source
    : public node_id_source
  {
    true_node_id_source(database & db) : db(db) {}
    virtual node_id next()
    {
      node_id n = db.next_node_id();
      I(!temp_node(n));
      return n;
    }
    database & db;
  };
}

// WARNING: these functions have no unit tests.  All the real work
// should be done in the alternative overloads in roster.cc, where it
// can be unit tested.  (See comments in that file for further explanation.)
static void
make_roster_for_merge(revision_t const & rev, revision_id const & new_rid,
                      roster_t & new_roster, marking_map & new_markings,
                      database & db, node_id_source & nis)
{
  edge_map::const_iterator i = rev.edges.begin();
  revision_id const & left_rid = edge_old_revision(i);
  cset const & left_cs = edge_changes(i);
  ++i;
  revision_id const & right_rid = edge_old_revision(i);
  cset const & right_cs = edge_changes(i);

  I(!null_id(left_rid) && !null_id(right_rid));
  cached_roster left_cached, right_cached;
  db.get_roster(left_rid, left_cached);
  db.get_roster(right_rid, right_cached);

  set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;
  db.get_uncommon_ancestors(left_rid, right_rid,
                            left_uncommon_ancestors,
                            right_uncommon_ancestors);

  make_roster_for_merge(left_rid, *left_cached.first, *left_cached.second,
                        left_cs, left_uncommon_ancestors,
                        right_rid, *right_cached.first, *right_cached.second,
                        right_cs, right_uncommon_ancestors,
                        new_rid,
                        new_roster, new_markings,
                        nis);
}

static void
make_roster_for_nonmerge(revision_t const & rev,
                         revision_id const & new_rid,
                         roster_t & new_roster, marking_map & new_markings,
                         database & db, node_id_source & nis)
{
  revision_id const & parent_rid = edge_old_revision(rev.edges.begin());
  cset const & parent_cs = edge_changes(rev.edges.begin());
  db.get_roster(parent_rid, new_roster, new_markings);
  make_roster_for_nonmerge(parent_cs, new_rid, new_roster, new_markings, nis);
}

void
make_roster_for_revision(database & db, node_id_source & nis,
                         revision_t const & rev, revision_id const & new_rid,
                         roster_t & new_roster, marking_map & new_markings)
{
  MM(rev);
  MM(new_rid);
  MM(new_roster);
  MM(new_markings);
  if (rev.edges.size() == 1)
    make_roster_for_nonmerge(rev, new_rid, new_roster, new_markings, db, nis);
  else if (rev.edges.size() == 2)
    make_roster_for_merge(rev, new_rid, new_roster, new_markings, db, nis);
  else
    I(false);

  // If nis is not a true_node_id_source, we have to assume we can get temp
  // node ids out of it.  ??? Provide a predicate method on node_id_sources
  // instead of doing a typeinfo comparison.
  new_roster.check_sane_against(new_markings,
                                typeid(nis) != typeid(true_node_id_source));
}

void
make_roster_for_revision(database & db,
                         revision_t const & rev, revision_id const & new_rid,
                         roster_t & new_roster, marking_map & new_markings)
{
  true_node_id_source nis(db);
  make_roster_for_revision(db, nis, rev, new_rid, new_roster, new_markings);
}

// ancestry graph loader

void
graph_loader::load_parents(revision_id const rid,
                          set<revision_id> & parents)
{
  db.get_revision_parents(rid, parents);
}

void
graph_loader::load_children(revision_id const rid,
                           set<revision_id> & children)
{
  db.get_revision_children(rid, children);
}

void
graph_loader::load_ancestors(set<revision_id> & revs)
{
  load_revs(ancestors, revs);
}

void
graph_loader::load_descendants(set<revision_id> & revs)
{
  load_revs(descendants, revs);
}

void
graph_loader::load_revs(load_direction const direction,
                       set<revision_id> & revs)
{
  std::deque<revision_id> next(revs.begin(), revs.end());

  while (!next.empty())
    {
      revision_id const & rid(next.front());
      MM(rid);

      set<revision_id> relatives;
      MM(relatives);

      if (direction == ancestors)
        load_parents(rid, relatives);
      else if (direction == descendants)
        load_children(rid, relatives);
      else
        I(false);

      for (set<revision_id>::const_iterator i = relatives.begin();
           i != relatives.end(); ++i)
        {
          if (null_id(*i))
            continue;
          pair<set<revision_id>::iterator, bool> res = revs.insert(*i);
          if (res.second)
            next.push_back(*i);
        }

      next.pop_front();
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
