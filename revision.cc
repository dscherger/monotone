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
#include "cset.hh"
#include "rev_height.hh"
#include "roster.hh"
#include "database.hh"

#include "interner.hh"
#include "basic_io.hh"
#include "transforms.hh"

#include "safe_map.hh"
#include "vector.hh"
#include <set>
#include <stack>
#include <boost/dynamic_bitset.hpp>
#include <boost/shared_ptr.hpp>

using std::make_pair;
using std::map;
using std::max;
using std::multimap;
using std::pair;
using std::set;
using std::stack;
using std::string;
using std::vector;

using boost::dynamic_bitset;
using boost::shared_ptr;

void revision_t::check_sane() const
{
  E(!null_id(new_manifest), made_from, F("Revision has no manifest id"));

  if (edges.size() == 1)
    {
      // no particular checks to be done right now
    }
  else if (edges.size() == 2)
    {
      // merge nodes cannot have null revisions
      for (edge_map::const_iterator i = edges.begin(); i != edges.end(); ++i)
        E(!null_id(edge_old_revision(i)), made_from,
          F("Merge revision has a null parent"));
    }
  else
    // revisions must always have either 1 or 2 edges
    E(false, made_from, F("Revision has %d edges, not 1 or 2") % edges.size());

  // we used to also check that if there were multiple edges that had patches
  // for the same file, then the new hashes on each edge matched each other.
  // this is not ported over to roster-style revisions because it's an
  // inadequate check, and the real check, that the new manifest id is correct
  // (done in put_revision, for instance) covers this case automatically.
}

bool
revision_t::is_merge_node() const
{
  return edges.size() > 1;
}

bool
revision_t::is_nontrivial() const
{
  check_sane();
  // merge revisions are never trivial, because even if the resulting node
  // happens to be identical to both parents, the merge is still recording
  // that fact.
  if (is_merge_node())
    return true;
  else
    return !edge_changes(edges.begin()).empty();
}

revision_t::revision_t(revision_t const & other)
  : origin_aware(other)
{
  /* behave like normal constructor if other is empty */
  made_for = made_for_nobody;
  if (null_id(other.new_manifest) && other.edges.empty()) return;
  other.check_sane();
  new_manifest = other.new_manifest;
  edges = other.edges;
  made_for = other.made_for;
}

revision_t const &
revision_t::operator=(revision_t const & other)
{
  other.check_sane();
  new_manifest = other.new_manifest;
  edges = other.edges;
  made_for = other.made_for;
  return *this;
}


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


void
make_revision(revision_id const & old_rev_id,
              roster_t const & old_roster,
              roster_t const & new_roster,
              revision_t & rev)
{
  shared_ptr<cset> cs(new cset());

  rev.edges.clear();
  make_cset(old_roster, new_roster, *cs);

  calculate_ident(new_roster, rev.new_manifest);

  if (global_sanity.debug_p())
    L(FL("new manifest_id is %s")
      % rev.new_manifest);

  safe_insert(rev.edges, make_pair(old_rev_id, cs));
  rev.made_for = made_for_database;
}

void
make_revision(revision_id const & old_rev_id,
              roster_t const & old_roster,
              cset const & changes,
              revision_t & rev)
{
  roster_t new_roster = old_roster;
  {
    temp_node_id_source nis;
    editable_roster_base er(new_roster, nis);
    changes.apply_to(er);
  }

  shared_ptr<cset> cs(new cset(changes));
  rev.edges.clear();

  calculate_ident(new_roster, rev.new_manifest);

  if (global_sanity.debug_p())
    L(FL("new manifest_id is %s")
      % rev.new_manifest);

  safe_insert(rev.edges, make_pair(old_rev_id, cs));
  rev.made_for = made_for_database;
}

void
make_revision(parent_map const & old_rosters,
              roster_t const & new_roster,
              revision_t & rev)
{
  edge_map edges;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      shared_ptr<cset> cs(new cset());
      make_cset(parent_roster(i), new_roster, *cs);
      safe_insert(edges, make_pair(parent_id(i), cs));
    }

  rev.edges = edges;
  calculate_ident(new_roster, rev.new_manifest);

  if (global_sanity.debug_p())
    L(FL("new manifest_id is %s")
      % rev.new_manifest);
}

static void
recalculate_manifest_id_for_restricted_rev(parent_map const & old_rosters,
                                           edge_map & edges,
                                           revision_t & rev)
{
  // In order to get the correct manifest ID, recalculate the new roster
  // using one of the restricted csets.  It doesn't matter which of the
  // parent roster/cset pairs we use for this; by construction, they must
  // all produce the same result.
  revision_id rid = parent_id(old_rosters.begin());
  roster_t restricted_roster = *(safe_get(old_rosters, rid).first);

  temp_node_id_source nis;
  editable_roster_base er(restricted_roster, nis);
  safe_get(edges, rid)->apply_to(er);

  calculate_ident(restricted_roster, rev.new_manifest);
  rev.edges = edges;

  if (global_sanity.debug_p())
    L(FL("new manifest_id is %s")
      % rev.new_manifest);
}

void
make_restricted_revision(parent_map const & old_rosters,
                         roster_t const & new_roster,
                         node_restriction const & mask,
                         revision_t & rev)
{
  edge_map edges;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      shared_ptr<cset> included(new cset());
      roster_t restricted_roster;

      make_restricted_roster(parent_roster(i), new_roster, 
                             restricted_roster, mask);
      make_cset(parent_roster(i), restricted_roster, *included);
      safe_insert(edges, make_pair(parent_id(i), included));
    }

  recalculate_manifest_id_for_restricted_rev(old_rosters, edges, rev);
}

void
make_restricted_revision(parent_map const & old_rosters,
                         roster_t const & new_roster,
                         node_restriction const & mask,
                         revision_t & rev,
                         cset & excluded,
                         utf8 const & cmd_name)
{
  edge_map edges;
  bool no_excludes = true;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      shared_ptr<cset> included(new cset());
      roster_t restricted_roster;

      make_restricted_roster(parent_roster(i), new_roster, 
                             restricted_roster, mask);
      make_cset(parent_roster(i), restricted_roster, *included);
      make_cset(restricted_roster, new_roster, excluded);
      safe_insert(edges, make_pair(parent_id(i), included));
      if (!excluded.empty())
        no_excludes = false;
    }

  E(old_rosters.size() == 1 || no_excludes, origin::user,
    F("the command '%s %s' cannot be restricted in a two-parent workspace")
    % prog_name % cmd_name);

  recalculate_manifest_id_for_restricted_rev(old_rosters, edges, rev);
}

// Workspace-only revisions, with fake rev.new_manifest and content
// changes suppressed.
void
make_revision_for_workspace(revision_id const & old_rev_id,
                            cset const & changes,
                            revision_t & rev)
{
  MM(old_rev_id);
  MM(changes);
  MM(rev);
  shared_ptr<cset> cs(new cset(changes));
  cs->deltas_applied.clear();

  rev.edges.clear();
  safe_insert(rev.edges, make_pair(old_rev_id, cs));
  rev.new_manifest = manifest_id(fake_id());
  rev.made_for = made_for_workspace;
}

void
make_revision_for_workspace(revision_id const & old_rev_id,
                            roster_t const & old_roster,
                            roster_t const & new_roster,
                            revision_t & rev)
{
  MM(old_rev_id);
  MM(old_roster);
  MM(new_roster);
  MM(rev);
  cset changes;
  make_cset(old_roster, new_roster, changes);
  make_revision_for_workspace(old_rev_id, changes, rev);
}

void
make_revision_for_workspace(parent_map const & old_rosters,
                            roster_t const & new_roster,
                            revision_t & rev)
{
  edge_map edges;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      shared_ptr<cset> cs(new cset());
      make_cset(parent_roster(i), new_roster, *cs);
      cs->deltas_applied.clear();
      safe_insert(edges, make_pair(parent_id(i), cs));
    }

  rev.edges = edges;
  rev.new_manifest = manifest_id(fake_id());
  rev.made_for = made_for_workspace;
}

// i/o stuff

namespace
{
  namespace syms
  {
    symbol const format_version("format_version");
    symbol const old_revision("old_revision");
    symbol const new_manifest("new_manifest");
  }
}

void
print_edge(basic_io::printer & printer,
           edge_entry const & e)
{
  basic_io::stanza st;
  st.push_binary_pair(syms::old_revision, edge_old_revision(e).inner());
  printer.print_stanza(st);
  print_cset(printer, edge_changes(e));
}

static void
print_insane_revision(basic_io::printer & printer,
                      revision_t const & rev)
{

  basic_io::stanza format_stanza;
  format_stanza.push_str_pair(syms::format_version, "1");
  printer.print_stanza(format_stanza);

  basic_io::stanza manifest_stanza;
  manifest_stanza.push_binary_pair(syms::new_manifest, rev.new_manifest.inner());
  printer.print_stanza(manifest_stanza);

  for (edge_map::const_iterator edge = rev.edges.begin();
       edge != rev.edges.end(); ++edge)
    print_edge(printer, *edge);
}

void
print_revision(basic_io::printer & printer,
               revision_t const & rev)
{
  rev.check_sane();
  print_insane_revision(printer, rev);
}


void
parse_edge(basic_io::parser & parser,
           revision_t & rev)
{
  shared_ptr<cset> cs(new cset());
  MM(*cs);
  manifest_id old_man;
  revision_id old_rev;
  string tmp;

  parser.esym(syms::old_revision);
  parser.hex(tmp);
  old_rev = decode_hexenc_as<revision_id>(tmp, parser.tok.in.made_from);

  parse_cset(parser, *cs);

  rev.edges.insert(make_pair(old_rev, cs));
}


void
parse_revision(basic_io::parser & parser,
               revision_t & rev)
{
  MM(rev);
  rev.edges.clear();
  rev.made_for = made_for_database;
  rev.made_from = parser.tok.in.made_from;
  string tmp;
  parser.esym(syms::format_version);
  parser.str(tmp);
  E(tmp == "1", parser.tok.in.made_from,
    F("encountered a revision with unknown format, version '%s'\n"
      "I only know how to understand the version '1' format\n"
      "a newer version of monotone is required to complete this operation")
    % tmp);
  parser.esym(syms::new_manifest);
  parser.hex(tmp);
  rev.new_manifest = decode_hexenc_as<manifest_id>(tmp, parser.tok.in.made_from);
  while (parser.symp(syms::old_revision))
    parse_edge(parser, rev);
  rev.check_sane();
}

void
read_revision(data const & dat,
              revision_t & rev)
{
  MM(rev);
  basic_io::input_source src(dat(), "revision");
  src.made_from = dat.made_from;
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_revision(pars, rev);
  E(src.lookahead == EOF, rev.made_from,
    F("failed to parse revision"));
  rev.check_sane();
}

void
read_revision(revision_data const & dat,
              revision_t & rev)
{
  read_revision(dat.inner(), rev);
  rev.check_sane();
}

static void write_insane_revision(revision_t const & rev,
                                  data & dat)
{
  basic_io::printer pr;
  print_insane_revision(pr, rev);
  dat = data(pr.buf, origin::internal);
}

template <> void
dump(revision_t const & rev, string & out)
{
  data dat;
  write_insane_revision(rev, dat);
  out = dat();
}

void
write_revision(revision_t const & rev,
               data & dat)
{
  rev.check_sane();
  write_insane_revision(rev, dat);
}

void
write_revision(revision_t const & rev,
               revision_data & dat)
{
  data d;
  write_revision(rev, d);
  dat = revision_data(d);
}

void calculate_ident(revision_t const & cs,
                     revision_id & ident)
{
  data tmp;
  id tid;
  write_revision(cs, tmp);
  calculate_ident(tmp, tid);
  ident = revision_id(tid);
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "lexical_cast.hh"

UNIT_TEST(revision, from_network)
{
  char const * bad_revisions[] = {
    "",

    "format_version \"1\"\n",

    "format_version \"1\"\n"
    "\n"
    "new_manifest [0000000000000000000000000000000000000000]\n",

    "format_version \"1\"\n"
    "\n"
    "new_manifest [000000000000000]\n",

    "format_version \"1\"\n"
    "\n"
    "new_manifest [0000000000000000000000000000000000000000]\n"
    "\n"
    "old_revision [66ff7f4640593afacdb056fefc069349e7d9ed9e]\n"
    "\n"
    "rename \"some_file\"\n"
    "   foo \"x\"\n",

    "format_version \"1\"\n"
    "\n"
    "new_manifest [0000000000000000000000000000000000000000]\n"
    "\n"
    "old_revision [66ff7f4640593afacdb056fefc069349e7d9ed9e]\n"
    "\n"
    "rename \"some_file\"\n"
    "   foo \"some_file\"\n"
  };
  revision_t rev;
  for (unsigned i = 0; i < sizeof(bad_revisions)/sizeof(char const*); ++i)
    {
      UNIT_TEST_CHECKPOINT((string("iteration ")
                            + boost::lexical_cast<string>(i)).c_str());
      UNIT_TEST_CHECK_THROW(read_revision(data(bad_revisions[i],
                                               origin::network),
                                          rev),
                            recoverable_failure);
    }
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
