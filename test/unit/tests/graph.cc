// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../../../src/graph.hh"

#include "../randomizer.hh"

#include "../../../src/transforms.hh"
#include "../../../src/lexical_cast.hh"
#include "../../../src/paths.hh"
#include "../../../src/rev_height.hh"
#include "../../../src/safe_map.hh"

using boost::lexical_cast;
using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

typedef std::multimap<id, id> rg_map;
struct mock_reconstruction_graph : public reconstruction_graph
{
  rg_map ancestry;
  set<id> bases;
  mock_reconstruction_graph(rg_map const & ancestry, set<id> const & bases)
    : ancestry(ancestry), bases(bases)
  {}
  virtual bool is_base(id const & node) const
  {
    return bases.find(node) != bases.end();
  }
  virtual void get_next(id const & from, set<id> & next) const
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
                                 vector<id> & all_nodes, rg_map & ancestry,
                                 set<id> & bases,
                                 randomizer & rng)
{
  for (size_t i = 0; i != num_nodes; ++i)
    {
      id hash;
      string s(lexical_cast<string>(i));
      calculate_ident(data(s, origin::internal), hash);
      all_nodes.push_back(hash);
    }
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
check_reconstruction_path(id const & start, reconstruction_graph const & graph,
                          reconstruction_path const & path)
{
  I(!path.empty());
  I(*path.begin() == start);
  reconstruction_path::const_iterator last = path.end();
  --last;
  I(graph.is_base(*last));
  for (reconstruction_path::const_iterator i = path.begin(); i != last; ++i)
    {
      set<id> children;
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
  vector<id> all_nodes;
  rg_map ancestry;
  set<id> bases;
  make_random_reconstruction_graph(num_nodes, num_random_edges, num_random_bases,
                                   all_nodes, ancestry, bases,
                                   rng);
  mock_reconstruction_graph graph(ancestry, bases);
  for (vector<id>::const_iterator i = all_nodes.begin();
       i != all_nodes.end(); ++i)
    {
      reconstruction_path path;
      get_reconstruction_path(*i, graph, path);
      check_reconstruction_path(*i, graph, path);
    }
}

UNIT_TEST(random_get_reconstruction_path)
{
  randomizer rng;
  // Some arbitrary numbers.
  run_get_reconstruction_path_tests_on_random_graph(100, 100, 10, rng);
  run_get_reconstruction_path_tests_on_random_graph(100, 200, 5, rng);
  run_get_reconstruction_path_tests_on_random_graph(1000, 1000, 50, rng);
  run_get_reconstruction_path_tests_on_random_graph(1000, 2000, 100, rng);
}


#include <map>
#include "../unit_tests.hh"
#include "../randomizer.hh"
#include "../../../src/roster.hh"


static void
get_all_ancestors(revision_id const & start, rev_ancestry_map const & child_to_parent_map,
                  set<revision_id> & ancestors)
{
  ancestors.clear();
  vector<revision_id> frontier;
  frontier.push_back(start);
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

struct mock_rev_graph : rev_graph
{
  mock_rev_graph(rev_ancestry_map const & child_to_parent_map)
    : child_to_parent_map(child_to_parent_map)
  {
    // assign sensible heights
    height_map.clear();

    // toposort expects parent->child
    rev_ancestry_map parent_to_child;
    for (rev_ancestry_map::const_iterator i = child_to_parent_map.begin();
      i != child_to_parent_map.end(); i++)
    {
      parent_to_child.insert(make_pair(i->second, i->first));
    }
    vector<revision_id> topo_revs;
    toposort_rev_ancestry(parent_to_child, topo_revs);

    // this is ugly but works. just give each one a sequential number.
    rev_height top = rev_height::root_height();
    u32 num = 1;
    for (vector<revision_id>::const_iterator r = topo_revs.begin();
      r != topo_revs.end(); r++, num++)
    {
      height_map.insert(make_pair(*r, top.child_height(num)));
    }
  }

  virtual void get_parents(revision_id const & node, set<revision_id> & parents) const
  {
    parents.clear();
    for (rev_ancestry_map::const_iterator i = child_to_parent_map.lower_bound(node);
      i != child_to_parent_map.upper_bound(node); i++)
    {
      if (!null_id(i->second))
        safe_insert(parents, i->second);
    }
  }

  virtual void get_children(revision_id const & node, set<revision_id> & parents) const
  {
    // not required
    I(false);
  }

  virtual void get_height(revision_id const & rev, rev_height & h) const
  {
    MM(rev);
    h = safe_get(height_map, rev);
  }


  rev_ancestry_map const & child_to_parent_map;
  map<revision_id, rev_height> height_map;
};


static void
run_a_get_uncommon_ancestors_test(rev_ancestry_map const & child_to_parent_map,
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
  mock_rev_graph rg(child_to_parent_map);
  get_uncommon_ancestors(left, right, rg,
                         calculated_left_uncommon_ancestors,
                         calculated_right_uncommon_ancestors);
  I(calculated_left_uncommon_ancestors == true_left_uncommon_ancestors);
  I(calculated_right_uncommon_ancestors == true_right_uncommon_ancestors);
  get_uncommon_ancestors(right, left, rg,
                         calculated_right_uncommon_ancestors,
                         calculated_left_uncommon_ancestors);
  I(calculated_left_uncommon_ancestors == true_left_uncommon_ancestors);
  I(calculated_right_uncommon_ancestors == true_right_uncommon_ancestors);
}

UNIT_TEST(get_uncommon_ancestors_nasty_convexity_case)
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

  rev_ancestry_map child_to_parent_map;
  revision_id left(fake_id()), right(fake_id());
  revision_id one(fake_id()), two(fake_id()), eight(fake_id()), three(fake_id()), nine(fake_id());
  MM(left);
  MM(right);
  MM(one);
  MM(two);
  MM(three);
  MM(eight);
  MM(nine);
  child_to_parent_map.insert(make_pair(left, one));
  child_to_parent_map.insert(make_pair(one, eight));
  child_to_parent_map.insert(make_pair(eight, nine));
  child_to_parent_map.insert(make_pair(right, three));
  child_to_parent_map.insert(make_pair(three, nine));

  revision_id middle(fake_id());
  child_to_parent_map.insert(make_pair(left, two));
  child_to_parent_map.insert(make_pair(right, two));
  // We insert a _lot_ of revisions at the ellipsis, to make sure that
  // whatever sort of step-size is used on the expansion, we can't take the
  // entire middle portion in one big gulp and make the test pointless.
  for (int i = 0; i != 1000; ++i)
    {
      revision_id next(fake_id());
      child_to_parent_map.insert(make_pair(middle, next));
      middle = next;
    }
  child_to_parent_map.insert(make_pair(middle, eight));

  run_a_get_uncommon_ancestors_test(child_to_parent_map, left, right);
}

double const new_root_freq = 0.05;
double const merge_node_freq = 0.2;
double const skip_up_freq = 0.5;

static revision_id
pick_node_from_set(set<revision_id> const & heads,
                   randomizer & rng)
{
  I(!heads.empty());
  size_t which_start = rng.uniform(heads.size());
  set<revision_id>::const_iterator i = heads.begin();
  for (size_t j = 0; j != which_start; ++j)
    ++i;
  return *i;
}

static revision_id
pick_node_or_ancestor(set<revision_id> const & heads,
                      rev_ancestry_map const & child_to_parent_map,
                      randomizer & rng)
{
  revision_id rev = pick_node_from_set(heads, rng);
  // now we recurse up from this starting point
  while (rng.bernoulli(skip_up_freq))
    {
      typedef rev_ancestry_map::const_iterator ci;
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
          if (rng.flip())
            rev = range.first->second;
          else
            rev = second->second;
        }
    }
  return rev;
}

static void
make_random_graph(size_t num_nodes,
                  rev_ancestry_map & child_to_parent_map,
                  vector<revision_id> & nodes,
                  randomizer & rng)
{
  set<revision_id> heads;

  for (size_t i = 0; i != num_nodes; ++i)
    {
      revision_id new_rid = revision_id(fake_id());
      nodes.push_back(new_rid);
      set<revision_id> parents;
      if (heads.empty() || rng.bernoulli(new_root_freq))
        parents.insert(revision_id());
      else if (rng.bernoulli(merge_node_freq) && heads.size() > 1)
        {
          // maybe we'll pick the same node twice and end up not doing a
          // merge, oh well...
          parents.insert(pick_node_from_set(heads, rng));
          parents.insert(pick_node_from_set(heads, rng));
        }
      else
        {
          parents.insert(pick_node_or_ancestor(heads, child_to_parent_map, rng));
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
run_a_get_uncommon_ancestors_random_test(size_t num_nodes,
                                         size_t iterations,
                                         randomizer & rng)
{
  rev_ancestry_map child_to_parent_map;
  vector<revision_id> nodes;
  make_random_graph(num_nodes, child_to_parent_map, nodes, rng);
  for (size_t i = 0; i != iterations; ++i)
    {
      L(FL("get_uncommon_ancestors: random test %s-%s") % num_nodes % i);
      revision_id left = idx(nodes, rng.uniform(nodes.size()));
      revision_id right = idx(nodes, rng.uniform(nodes.size()));
      run_a_get_uncommon_ancestors_test(child_to_parent_map, left, right);
    }
}

UNIT_TEST(get_uncommon_ancestors_randomly)
{
  randomizer rng;
  run_a_get_uncommon_ancestors_random_test(100, 100, rng);
  run_a_get_uncommon_ancestors_random_test(1000, 100, rng);
  run_a_get_uncommon_ancestors_random_test(10000, 100, rng);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
