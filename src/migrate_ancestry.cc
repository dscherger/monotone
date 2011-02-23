// Copyright (C) 2010 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "migration.hh"
#include "revision.hh"
#include "roster.hh"
#include "project.hh"
#include "constants.hh"
#include "database.hh"
#include "graph.hh"
#include "key_store.hh"
#include "lazy_rng.hh"
#include "legacy.hh"
#include "outdated_indicator.hh"
#include "simplestring_xform.hh"
#include "ui.hh"
#include "vocab_cast.hh"

#include "safe_map.hh"
#include <sstream>
#include <queue>
#include "vector.hh"
#include <boost/shared_ptr.hpp>

using std::back_inserter;
using std::deque;
using std::make_pair;
using std::map;
using std::multimap;
using std::ostringstream;
using std::pair;
using std::queue;
using std::set;
using std::string;
using std::vector;
using boost::shared_ptr;

// Stuff related to rebuilding the revision graph. Unfortunately this is a
// real enough error case that we need support code for it.

typedef map<u64, pair<shared_ptr<roster_t>, shared_ptr<marking_map> > >
parent_roster_map;

template <> void
dump(parent_roster_map const & prm, string & out)
{
  ostringstream oss;
  for (parent_roster_map::const_iterator i = prm.begin(); i != prm.end(); ++i)
    {
      oss << "roster: " << i->first << '\n';
      string roster_str, indented_roster_str;
      dump(*i->second.first, roster_str);
      prefix_lines_with("    ", roster_str, indented_roster_str);
      oss << indented_roster_str;
      oss << "\nroster's marking:\n";
      string marking_str, indented_marking_str;
      dump(*i->second.second, marking_str);
      prefix_lines_with("    ", marking_str, indented_marking_str);
      oss << indented_marking_str;
      oss << "\n\n";
    }
  out = oss.str();
}

// FIXME: this algorithm is incredibly inefficient; it's O(n) where n is the
// size of the entire revision graph.

template<typename T> static bool
is_ancestor(T const & ancestor_id,
            T const & descendent_id,
            multimap<T, T> const & graph)
{
  set<T> visited;
  queue<T> queue;

  queue.push(ancestor_id);

  while (!queue.empty())
    {
      T current_id = queue.front();
      queue.pop();

      if (current_id == descendent_id)
        return true;
      else
        {
          typedef typename multimap<T, T>::const_iterator gi;
          pair<gi, gi> children = graph.equal_range(current_id);
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
is_ancestor(database & db,
            revision_id const & ancestor_id,
            revision_id const & descendent_id)
{
  L(FL("checking whether %s is an ancestor of %s")
    % ancestor_id
    % descendent_id);

  multimap<revision_id, revision_id> graph;
  db.get_forward_ancestry(graph);
  return is_ancestor(ancestor_id, descendent_id, graph);
}

namespace {

struct anc_graph
{
  anc_graph(bool existing, database & db, key_store & keys,
            project_t & project) :
    existing_graph(existing),
    db(db),
    keys(keys),
    project(project),
    max_node(0),
    n_nodes("nodes", "n", 1),
    n_certs_in("certs in", "c", 1),
    n_revs_out("revs out", "r", 1),
    n_certs_out("certs out", "C", 1)
  {}

  bool existing_graph;
  database & db;
  key_store & keys;
  project_t & project;
  u64 max_node;

  ticker n_nodes;
  ticker n_certs_in;
  ticker n_revs_out;
  ticker n_certs_out;

  map<u64,manifest_id> node_to_old_man;
  map<manifest_id,u64> old_man_to_node;

  map<u64,revision_id> node_to_old_rev;
  map<revision_id,u64> old_rev_to_node;

  map<u64,revision_id> node_to_new_rev;
  map<revision_id,u64> new_rev_to_node;

  map<u64, legacy::renames_map> node_to_renames;

  multimap<u64, pair<cert_name, cert_value> > certs;
  multimap<u64, u64> ancestry;
  set<string> branches;

  void add_node_ancestry(u64 child, u64 parent);
  void write_certs();
  void kluge_for_bogus_merge_edges();
  void rebuild_ancestry(set<string> const & attrs_to_drop);
  void get_node_manifest(u64 node, manifest_id & man);
  u64 add_node_for_old_manifest(manifest_id const & man);
  u64 add_node_for_oldstyle_revision(revision_id const & rev);
  void construct_revisions_from_ancestry(set<string> const & attrs_to_drop);
  void fixup_node_identities(parent_roster_map const & parent_rosters,
                             roster_t & child_roster,
                             legacy::renames_map const & renames);
};

}

void anc_graph::add_node_ancestry(u64 child, u64 parent)
{
  L(FL("noting ancestry from child %d -> parent %d") % child % parent);
  ancestry.insert(make_pair(child, parent));
}

void anc_graph::get_node_manifest(u64 node, manifest_id & man)
{
  map<u64,manifest_id>::const_iterator i = node_to_old_man.find(node);
  I(i != node_to_old_man.end());
  man = i->second;
}

void anc_graph::write_certs()
{
  {
    // regenerate epochs on all branches to random states

    for (set<string>::const_iterator i = branches.begin(); i != branches.end(); ++i)
      {
        char buf[constants::epochlen_bytes];
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
        lazy_rng::get().randomize(reinterpret_cast<Botan::byte *>(buf),
                                 constants::epochlen_bytes);
#else
        Botan::Global_RNG::randomize(reinterpret_cast<Botan::byte *>(buf),
                                     constants::epochlen_bytes);
#endif
        epoch_data new_epoch(string(buf, buf + constants::epochlen_bytes),
                             origin::internal);
        L(FL("setting epoch for %s to %s")
          % *i % new_epoch);
        db.set_epoch(branch_name(*i, origin::internal), new_epoch);
      }
  }


  typedef multimap<u64, pair<cert_name, cert_value> >::const_iterator ci;

  for (map<u64,revision_id>::const_iterator i = node_to_new_rev.begin();
       i != node_to_new_rev.end(); ++i)
    {
      revision_id rev(i->second);

      pair<ci,ci> range = certs.equal_range(i->first);

      for (ci j = range.first; j != range.second; ++j)
        {
          cert_name name(j->second.first);
          cert_value val(j->second.second);

          if (project.put_cert(keys, rev, name, val))
            ++n_certs_out;
        }
    }
}

void
anc_graph::kluge_for_bogus_merge_edges()
{
  // This kluge exists because in the 0.24-era monotone databases, several
  // bad merges still existed in which one side of the merge is an ancestor
  // of the other side of the merge. In other words, graphs which look like
  // this:
  //
  //  a ----------------------> e
  //   \                       /
  //    \---> b -> c -> d ----/
  //
  // Such merges confuse the roster-building algorithm, because they should
  // never have occurred in the first place: a was not a head at the time
  // of the merge, e should simply have been considered an extension of d.
  //
  // So... we drop the a->e edges entirely.
  //
  // Note: this kluge drops edges which are a struct superset of those
  // dropped by a previous kluge ("3-ancestor") so we have removed that
  // code.

  P(F("scanning for bogus merge edges"));

  multimap<u64,u64> parent_to_child_map;
    for (multimap<u64, u64>::const_iterator i = ancestry.begin();
         i != ancestry.end(); ++i)
      parent_to_child_map.insert(make_pair(i->second, i->first));

  map<u64, u64> edges_to_kill;
  for (multimap<u64, u64>::const_iterator i = ancestry.begin();
       i != ancestry.end(); ++i)
    {
      multimap<u64, u64>::const_iterator j = i;
      ++j;
      u64 child = i->first;
      // NB: ancestry is a multimap from child->parent(s)
      if (j != ancestry.end())
        {
          if (j->first == i->first)
            {
              L(FL("considering old merge edge %s") %
                safe_get(node_to_old_rev, i->first));
              u64 parent1 = i->second;
              u64 parent2 = j->second;
              if (is_ancestor (parent1, parent2, parent_to_child_map))
                safe_insert(edges_to_kill, make_pair(child, parent1));
              else if (is_ancestor (parent2, parent1, parent_to_child_map))
                safe_insert(edges_to_kill, make_pair(child, parent2));
            }
        }
    }

  for (map<u64, u64>::const_iterator i = edges_to_kill.begin();
       i != edges_to_kill.end(); ++i)
    {
      u64 child = i->first;
      u64 parent = i->second;
      bool killed = false;
      for (multimap<u64, u64>::iterator j = ancestry.lower_bound(child);
           j->first == child; ++j)
        {
          if (j->second == parent)
            {
              P(F("optimizing out redundant edge %d -> %d")
                % parent % child);
              ancestry.erase(j);
              killed = true;
              break;
            }
        }

      if (!killed)
        W(F("failed to eliminate edge %d -> %d")
          % parent % child);
    }
}


void
anc_graph::rebuild_ancestry(set<string> const & attrs_to_drop)
{
  kluge_for_bogus_merge_edges();

  P(F("rebuilding %d nodes") % max_node);
  {
    transaction_guard guard(db);
    if (existing_graph)
      db.delete_existing_revs_and_certs();
    construct_revisions_from_ancestry(attrs_to_drop);
    write_certs();
    if (existing_graph)
      db.delete_existing_manifests();
    guard.commit();
  }
}

u64
anc_graph::add_node_for_old_manifest(manifest_id const & man)
{
  I(!existing_graph);
  u64 node = 0;
  if (old_man_to_node.find(man) == old_man_to_node.end())
    {
      node = max_node++;
      ++n_nodes;
      L(FL("node %d = manifest %s")
        % node % man);
      old_man_to_node.insert(make_pair(man, node));
      node_to_old_man.insert(make_pair(node, man));

      // load certs
      vector<cert> mcerts;
      db.get_manifest_certs(man, mcerts);
      for(vector<cert>::const_iterator i = mcerts.begin();
          i != mcerts.end(); ++i)
        {
          L(FL("loaded '%s' manifest cert for node %s") % i->name % node);
          ++n_certs_in;
          certs.insert(make_pair(node, make_pair(i->name, i->value)));
        }
    }
  else
    {
      node = old_man_to_node[man];
    }
  return node;
}

u64 anc_graph::add_node_for_oldstyle_revision(revision_id const & rev)
{
  I(existing_graph);
  I(!null_id(rev));
  u64 node = 0;
  if (old_rev_to_node.find(rev) == old_rev_to_node.end())
    {
      node = max_node++;
      ++n_nodes;

      manifest_id man;
      legacy::renames_map renames;
      legacy::get_manifest_and_renames_for_rev(db, rev, man, renames);

      L(FL("node %d = revision %s = manifest %s")
        % node
        % rev
        % man);
      old_rev_to_node.insert(make_pair(rev, node));
      node_to_old_rev.insert(make_pair(node, rev));
      node_to_old_man.insert(make_pair(node, man));
      node_to_renames.insert(make_pair(node, renames));

      // load certs
      vector<cert> rcerts;
      db.get_revision_certs(rev, rcerts);
      db.erase_bogus_certs(project, rcerts);
      for(vector<cert>::const_iterator i = rcerts.begin();
          i != rcerts.end(); ++i)
        {
          L(FL("loaded '%s' revision cert for node %s") % i->name % node);
          ++n_certs_in;
          certs.insert(make_pair(node, make_pair(i->name,
                                                 i->value)));

          if (i->name == branch_cert_name)
            branches.insert(i->value());
        }
    }
  else
    {
      node = old_rev_to_node[rev];
    }

  return node;
}

static bool
not_dead_yet(node_id nid, u64 birth_rev,
             parent_roster_map const & parent_rosters,
             multimap<u64, u64> const & child_to_parents)
{
  // Any given node, at each point in the revision graph, is in one of the
  // states "alive", "unborn", "dead".  The invariant we must maintain in
  // constructing our revision graph is that if a node is dead in any parent,
  // then it must also be dead in the child.  The purpose of this function is
  // to take a node, and a list of parents, and determine whether that node is
  // allowed to be alive in a child of the given parents.

  // "Alive" means, the node currently exists in the revision's tree.
  // "Unborn" means, the node does not exist in the revision's tree, and the
  // node's birth revision is _not_ an ancestor of the revision.
  // "Dead" means, the node does not exist in the revision's tree, and the
  // node's birth revision _is_ an ancestor of the revision.

  // L(FL("testing liveliness of node %d, born in rev %d") % nid % birth_rev);
  for (parent_roster_map::const_iterator r = parent_rosters.begin();
       r != parent_rosters.end(); ++r)
    {
      shared_ptr<roster_t> parent = r->second.first;
      // L(FL("node %d %s in parent roster %d")
      //             % nid
      //             % (parent->has_node(n->first) ? "exists" : "does not exist" )
      //             % r->first);

      if (!parent->has_node(nid))
        {
          deque<u64> work;
          set<u64> seen;
          work.push_back(r->first);
          while (!work.empty())
            {
              u64 curr = work.front();
              work.pop_front();
              // L(FL("examining ancestor %d of parent roster %d, looking for anc=%d")
              //                     % curr % r->first % birth_rev);

              if (seen.find(curr) != seen.end())
                continue;
              seen.insert(curr);

              if (curr == birth_rev)
                {
                  // L(FL("node is dead in %d") % r->first);
                  return false;
                }
              typedef multimap<u64, u64>::const_iterator ci;
              pair<ci,ci> range = child_to_parents.equal_range(curr);
              for (ci i = range.first; i != range.second; ++i)
                {
                  if (i->first != curr)
                    continue;
                  work.push_back(i->second);
                }
            }
        }
    }
  // L(FL("node is alive in all parents, returning true"));
  return true;
}

// Recursive helper function for insert_into_roster.
static void
insert_parents_into_roster(roster_t & child_roster,
                           temp_node_id_source & nis,
                           file_path const & pth,
                           file_path const & full)
{
  if (child_roster.has_node(pth))
    {
      E(is_dir_t(child_roster.get_node(pth)), origin::internal,
        F("Directory '%s' for path '%s' cannot be added, "
          "as there is a file in the way") % pth % full);
      return;
    }

  if (!pth.empty())
    insert_parents_into_roster(child_roster, nis, pth.dirname(), full);

  child_roster.attach_node(child_roster.create_dir_node(nis), pth);
}

static void
insert_into_roster(roster_t & child_roster,
                   temp_node_id_source & nis,
                   file_path const & pth,
                   file_id const & fid)
{
  if (child_roster.has_node(pth))
    {
      const_node_t n = child_roster.get_node(pth);
      E(is_file_t(n), origin::internal,
        F("Path '%s' cannot be added, as there is a directory in the way") % pth);
      const_file_t f = downcast_to_file_t(n);
      E(f->content == fid, origin::internal,
        F("Path '%s' added twice with differing content") % pth);
      return;
    }

  insert_parents_into_roster(child_roster, nis, pth.dirname(), pth);
  child_roster.attach_node(child_roster.create_file_node(fid, nis), pth);
}

void
anc_graph::fixup_node_identities(parent_roster_map const & parent_rosters,
                                 roster_t & child_roster,
                                 legacy::renames_map const & renames)
{
  // Our strategy here is to iterate over every node in every parent, and
  // for each parent node P find zero or one tmp nodes in the child which
  // represents the fate of P:
  //
  //   - If any of the parents thinks that P has died, we do not search for
  //     it in the child; we leave it as "dropped".
  //
  //   - We fetch the name N of the parent node P, and apply the rename map
  //     to N, getting "remapped name" M.  If we find a child node C with
  //     name M in the child roster, with the same type as P, we identify P
  //     and C, and swap P for C in the child.


  // Map node_id -> birth rev
  map<node_id, u64> nodes_in_any_parent;

  // Stage 1: collect all nodes (and their birth revs) in any parent.
  for (parent_roster_map::const_iterator i = parent_rosters.begin();
       i != parent_rosters.end(); ++i)
    {
      shared_ptr<roster_t> parent_roster = i->second.first;
      shared_ptr<marking_map> parent_marking = i->second.second;

      node_map const & nodes = parent_roster->all_nodes();
      for (node_map::const_iterator j = nodes.begin(); j != nodes.end(); ++j)
        {
          node_id n = j->first;
          revision_id birth_rev = parent_marking->get_marking(n)->birth_revision;
          u64 birth_node = safe_get(new_rev_to_node, birth_rev);
          map<node_id, u64>::const_iterator i = nodes_in_any_parent.find(n);
          if (i != nodes_in_any_parent.end())
            I(i->second == birth_node);
          else
            safe_insert(nodes_in_any_parent,
                        make_pair(n, birth_node));
        }
    }

  // Stage 2: For any node which is actually live, try to locate a mapping
  // from a parent instance of it to a child node.
  for (map<node_id, u64>::const_iterator i = nodes_in_any_parent.begin();
       i != nodes_in_any_parent.end(); ++i)
    {
      node_id n = i->first;
      u64 birth_rev = i->second;

      if (child_roster.has_node(n))
        continue;

      if (not_dead_yet(n, birth_rev, parent_rosters, ancestry))
        {
          for (parent_roster_map::const_iterator j = parent_rosters.begin();
               j != parent_rosters.end(); ++j)
            {
              shared_ptr<roster_t> parent_roster = j->second.first;

              if (!parent_roster->has_node(n))
                continue;

              file_path fp;
              parent_roster->get_name(n, fp);

              // Try remapping the name.
              if (node_to_old_rev.find(j->first) != node_to_old_rev.end())
                {
                  legacy::renames_map::const_iterator rmap;
                  revision_id parent_rid = safe_get(node_to_old_rev, j->first);
                  rmap = renames.find(parent_rid);
                  if (rmap != renames.end())
                    fp = find_new_path_for(rmap->second, fp);
                }

              // See if we can match this node against a child.
              if ((!child_roster.has_node(n))
                  && child_roster.has_node(fp))
                {
                  const_node_t pn = parent_roster->get_node(n);
                  const_node_t cn = child_roster.get_node(fp);
                  if (is_file_t(pn) == is_file_t(cn))
                    {
                      child_roster.replace_node_id(cn->self, n);
                      break;
                    }
                }
            }
        }
    }
}

struct
current_rev_debugger
{
  u64 node;
  anc_graph const & agraph;
  current_rev_debugger(u64 n, anc_graph const & ag)
    : node(n), agraph(ag)
  {
  }
};

template <> void
dump(current_rev_debugger const & d, string & out)
{
  typedef multimap<u64, pair<cert_name, cert_value> >::const_iterator ci;
  pair<ci,ci> range = d.agraph.certs.equal_range(d.node);
  for(ci i = range.first; i != range.second; ++i)
    {
      if (i->first == d.node)
        {
          out += "cert '" + i->second.first() + "'";
          out += "= '" + i->second.second() + "'\n";
        }
    }
}


void
anc_graph::construct_revisions_from_ancestry(set<string> const & attrs_to_drop)
{
  // This is an incredibly cheesy, and also reasonably simple sorting
  // system: we put all the root nodes in the work queue. we take a
  // node out of the work queue and check if its parents are done. if
  // they are, we process it and insert its children. otherwise we put
  // it back on the end of the work queue. This both ensures that we're
  // always processing something *like* a frontier, while avoiding the
  // need to worry about one side of the frontier advancing faster than
  // another.

  typedef multimap<u64,u64>::const_iterator ci;
  multimap<u64,u64> parent_to_child_map;
  deque<u64> work;
  set<u64> done;

  {
    // Set up the parent->child mapping and prime the work queue

    set<u64> children, all;
    for (multimap<u64, u64>::const_iterator i = ancestry.begin();
         i != ancestry.end(); ++i)
      {
        parent_to_child_map.insert(make_pair(i->second, i->first));
        children.insert(i->first);
      }
    for (map<u64,manifest_id>::const_iterator i = node_to_old_man.begin();
         i != node_to_old_man.end(); ++i)
      {
        all.insert(i->first);
      }

    set_difference(all.begin(), all.end(),
                   children.begin(), children.end(),
                   back_inserter(work));
  }

  while (!work.empty())
    {

      u64 child = work.front();

      current_rev_debugger dbg(child, *this);
      MM(dbg);

      work.pop_front();

      if (done.find(child) != done.end())
        continue;

      pair<ci,ci> parent_range = ancestry.equal_range(child);
      set<u64> parents;
      bool parents_all_done = true;
      for (ci i = parent_range.first; parents_all_done && i != parent_range.second; ++i)
      {
        if (i->first != child)
          continue;
        u64 parent = i->second;
        if (done.find(parent) == done.end())
          {
            work.push_back(child);
            parents_all_done = false;
          }
        else
          parents.insert(parent);
      }

      if (parents_all_done
          && (node_to_new_rev.find(child) == node_to_new_rev.end()))
        {
          L(FL("processing node %d") % child);

          manifest_id old_child_mid;
          legacy::manifest_map old_child_man;

          get_node_manifest(child, old_child_mid);
          manifest_data mdat;
          db.get_manifest_version(old_child_mid, mdat);
          legacy::read_manifest_map(mdat, old_child_man);

          // Load all the parent rosters into a temporary roster map
          parent_roster_map parent_rosters;
          MM(parent_rosters);

          for (ci i = parent_range.first; parents_all_done && i != parent_range.second; ++i)
            {
              if (i->first != child)
                continue;
              u64 parent = i->second;
              if (parent_rosters.find(parent) == parent_rosters.end())
                {
                  shared_ptr<roster_t> ros = shared_ptr<roster_t>(new roster_t());
                  shared_ptr<marking_map> mm = shared_ptr<marking_map>(new marking_map());
                  db.get_roster(safe_get(node_to_new_rev, parent), *ros, *mm);
                  safe_insert(parent_rosters, make_pair(parent, make_pair(ros, mm)));
                }
            }

          file_path attr_path = file_path_internal(".mt-attrs");
          file_path old_ignore_path = file_path_internal(".mt-ignore");
          file_path new_ignore_path = file_path_internal(".mtn-ignore");

          roster_t child_roster;
          MM(child_roster);
          temp_node_id_source nis;

          // all rosters shall have a root node.
          child_roster.attach_node(child_roster.create_dir_node(nis),
                                   file_path_internal(""));

          for (legacy::manifest_map::const_iterator i = old_child_man.begin();
               i != old_child_man.end(); ++i)
            {
              if (i->first == attr_path)
                continue;
              // convert .mt-ignore to .mtn-ignore... except if .mtn-ignore
              // already exists, just leave things alone.
              else if (i->first == old_ignore_path
                       && old_child_man.find(new_ignore_path) == old_child_man.end())
                insert_into_roster(child_roster, nis, new_ignore_path, i->second);
              else
                insert_into_roster(child_roster, nis, i->first, i->second);
            }

          // migrate attributes out of .mt-attrs
          {
            legacy::manifest_map::const_iterator i = old_child_man.find(attr_path);
            if (i != old_child_man.end())
              {
                file_data dat;
                db.get_file_version(i->second, dat);
                legacy::dot_mt_attrs_map attrs;
                legacy::read_dot_mt_attrs(dat.inner(), attrs);
                for (legacy::dot_mt_attrs_map::const_iterator j = attrs.begin();
                     j != attrs.end(); ++j)
                  {
                    if (child_roster.has_node(j->first))
                      {
                        map<string, string> const &
                          fattrs = j->second;
                        for (map<string, string>::const_iterator
                               k = fattrs.begin();
                             k != fattrs.end(); ++k)
                          {
                            string key = k->first;
                            if (attrs_to_drop.find(key) != attrs_to_drop.end())
                              {
                                // ignore it
                              }
                            else if (key == "execute" || key == "manual_merge")
                              child_roster.set_attr(j->first,
                                                    attr_key("mtn:" + key,
                                                             origin::internal),
                                                    attr_value(k->second,
                                                               origin::internal));
                            else
                              E(false, origin::no_fault,
                                F("unknown attribute '%s' on path '%s'\n"
                                  "please contact %s so we can work out the right way to migrate this\n"
                                  "(if you just want it to go away, see the switch --drop-attr, but\n"
                                  "seriously, if you'd like to keep it, we're happy to figure out how)")
                                % key % j->first % PACKAGE_BUGREPORT);
                          }
                      }
                  }
              }
          }

          // Now knit the parent node IDs into child node IDs (which are currently all
          // tmpids), wherever possible.
          fixup_node_identities(parent_rosters, child_roster, node_to_renames[child]);

          revision_t rev;
          rev.made_for = made_for_database;
          MM(rev);
          calculate_ident(child_roster, rev.new_manifest);

          // For each parent, construct an edge in the revision structure by analyzing the
          // relationship between the parent roster and the child roster (and placing the
          // result in a cset)

          for (parent_roster_map::const_iterator i = parent_rosters.begin();
               i != parent_rosters.end(); ++i)
            {
              u64 parent = i->first;
              revision_id parent_rid = safe_get(node_to_new_rev, parent);
              shared_ptr<roster_t> parent_roster = i->second.first;
              shared_ptr<cset> cs = shared_ptr<cset>(new cset());
              MM(*cs);
              make_cset(*parent_roster, child_roster, *cs);
              safe_insert(rev.edges, make_pair(parent_rid, cs));
            }

          // It is possible that we're at a "root" node here -- a node
          // which had no parent in the old rev graph -- in which case we
          // synthesize an edge from the empty revision to the current,
          // containing a cset which adds all the files in the child.

          if (rev.edges.empty())
            {
              revision_id parent_rid;
              shared_ptr<roster_t> parent_roster = shared_ptr<roster_t>(new roster_t());
              shared_ptr<cset> cs = shared_ptr<cset>(new cset());
              MM(*cs);
              make_cset(*parent_roster, child_roster, *cs);
              safe_insert(rev.edges, make_pair (parent_rid, cs));

            }

          // Finally, put all this excitement into the database and save
          // the new_rid for use in the cert-writing pass.

          revision_id new_rid;
          calculate_ident(rev, new_rid);
          node_to_new_rev.insert(make_pair(child, new_rid));
          new_rev_to_node.insert(make_pair(new_rid, child));

          /*
          P(F("------------------------------------------------"));
          P(F("made revision %s with %d edges, manifest id = %s")
            % new_rid % rev.edges.size() % rev.new_manifest);

          {
            string rtmp;
            data dtmp;
            dump(dbg, rtmp);
            write_revision(rev, dtmp);
            P(F("%s") % rtmp);
            P(F("%s") % dtmp);
          }
          P(F("------------------------------------------------"));
          */

          L(FL("mapped node %d to revision %s") % child % new_rid);
          if (db.put_revision(new_rid, rev))
            {
              db.put_file_sizes_for_revision(rev);
              ++n_revs_out;
            }

          // Mark this child as done, hooray!
          safe_insert(done, child);

          // Extend the work queue with all the children of this child
          pair<ci,ci> grandchild_range = parent_to_child_map.equal_range(child);
          for (ci i = grandchild_range.first;
               i != grandchild_range.second; ++i)
            {
              if (i->first != child)
                continue;
              if (done.find(i->second) == done.end())
                work.push_back(i->second);
            }
        }
    }
}

void
build_roster_style_revs_from_manifest_style_revs(database & db, key_store & keys,
                                                 project_t & project,
                                                 set<string> const & attrs_to_drop)
{
  anc_graph graph(true, db, keys, project);

  P(F("converting existing revision graph to new roster-style revisions"));
  multimap<revision_id, revision_id> existing_graph;

  // cross-check that we're getting everything
  // in fact the code in this function is wrong, because if a revision has no
  // parents and no children (it is a root revision, and no children have been
  // committed under it), then we will simply drop it!
  // This code at least causes this case to throw an assertion; FIXME: make
  // this case actually work.
  set<revision_id> all_rev_ids;
  db.get_revision_ids(all_rev_ids);

  db.get_forward_ancestry(existing_graph);
  for (multimap<revision_id, revision_id>::const_iterator i = existing_graph.begin();
       i != existing_graph.end(); ++i)
    {
      // FIXME: insert for the null id as well, and do the same for the
      // changesetify code, and then reach rebuild_ancestry how to deal with
      // such things.  (I guess u64(0) should represent the null parent?)
      if (!null_id(i->first))
        {
          u64 parent_node = graph.add_node_for_oldstyle_revision(i->first);
          all_rev_ids.erase(i->first);
          u64 child_node = graph.add_node_for_oldstyle_revision(i->second);
          all_rev_ids.erase(i->second);
          graph.add_node_ancestry(child_node, parent_node);
        }
    }

  for (set<revision_id>::const_iterator i = all_rev_ids.begin();
       i != all_rev_ids.end(); ++i)
    {
      graph.add_node_for_oldstyle_revision(*i);
    }

  graph.rebuild_ancestry(attrs_to_drop);
}


void
build_changesets_from_manifest_ancestry(database & db, key_store & keys,
                                        project_t & project,
                                        set<string> const & attrs_to_drop)
{
  anc_graph graph(false, db, keys, project);

  P(F("rebuilding revision graph from manifest certs"));

  vector<cert> tmp;
  db.get_manifest_certs(cert_name("ancestor"), tmp);

  for (vector<cert>::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      manifest_id child, parent;
      child = manifest_id(i->ident.inner());
      parent = typecast_vocab<manifest_id>(i->value);

      u64 parent_node = graph.add_node_for_old_manifest(parent);
      u64 child_node = graph.add_node_for_old_manifest(child);
      graph.add_node_ancestry(child_node, parent_node);
    }

  graph.rebuild_ancestry(attrs_to_drop);
}


// This is a special function solely for the use of regenerate_caches -- it
// must work even when caches (especially, the height cache!) do not exist.
// For all other purposes, use toposort above.
static void
allrevs_toposorted(database & db,
                   vector<revision_id> & revisions)
{
  // get the complete ancestry
  rev_ancestry_map graph;
  db.get_forward_ancestry(graph);
  toposort_rev_ancestry(graph, revisions);
}

static void
regenerate_heights(database & db)
{
  P(F("regenerating cached heights"));
  db.ensure_open_for_cache_reset();

  {
    transaction_guard guard(db);
    db.delete_existing_heights();

    vector<revision_id> sorted_ids;
    allrevs_toposorted(db, sorted_ids);

    ticker done(_("regenerated"), "r", 1);
    done.set_total(sorted_ids.size());

    for (std::vector<revision_id>::const_iterator i = sorted_ids.begin();
         i != sorted_ids.end(); ++i)
      {
        revision_t rev;
        revision_id const & rev_id = *i;
        db.get_revision(rev_id, rev);
        db.put_height_for_revision(rev_id, rev);
        ++done;
      }

    guard.commit();
  }
  P(F("finished regenerating cached heights"));
}

static void
regenerate_rosters(database & db)
{
  P(F("regenerating cached rosters"));
  db.ensure_open_for_cache_reset();

  {
    transaction_guard guard(db);
    db.delete_existing_rosters();

    vector<revision_id> sorted_ids;
    allrevs_toposorted(db, sorted_ids);

    ticker done(_("regenerated"), "r", 1);
    done.set_total(sorted_ids.size());

    for (std::vector<revision_id>::const_iterator i = sorted_ids.begin();
         i != sorted_ids.end(); ++i)
      {
        revision_t rev;
        revision_id const & rev_id = *i;
        db.get_revision(rev_id, rev);
        db.put_roster_for_revision(rev_id, rev);
        ++done;
      }

    guard.commit();
  }
  P(F("finished regenerating cached rosters"));
}

static void
regenerate_branches(database & db)
{
  P(F("regenerating cached branches"));
  db.ensure_open_for_cache_reset();

  {
    transaction_guard guard(db);
    db.delete_existing_branch_leaves();

    vector<cert> all_branch_certs;
    db.get_revision_certs(branch_cert_name, all_branch_certs);
    set<string> seen_branches;

    ticker done(_("regenerated"), "r", 1);

    for (vector<cert>::const_iterator i = all_branch_certs.begin();
         i != all_branch_certs.end(); ++i)
      {
        string const name = i->value();

        std::pair<set<string>::iterator, bool> inserted =
          seen_branches.insert(name);

        if (inserted.second)
          {
            db.recalc_branch_leaves(i->value);
            ++done;
          }
      }
    guard.commit();
  }
  P(F("finished regenerating cached branches"));
}

static void
regenerate_file_sizes(database & db)
{
  P(F("regenerating cached file sizes for revisions"));
  db.ensure_open_for_cache_reset();

  {
    transaction_guard guard(db);
    db.delete_existing_file_sizes();

    vector<revision_id> sorted_ids;
    allrevs_toposorted(db, sorted_ids);

    ticker done(_("regenerated"), "r", 1);
    done.set_total(sorted_ids.size());

    for (std::vector<revision_id>::const_iterator i = sorted_ids.begin();
         i != sorted_ids.end(); ++i)
      {
        revision_t rev;
        revision_id const & rev_id = *i;
        db.get_revision(rev_id, rev);
        db.put_file_sizes_for_revision(rev);
        ++done;
      }

    guard.commit();
  }
  P(F("finished regenerating cached file sizes"));
}

void
regenerate_caches(database & db, regen_cache_type type)
{
  I(type != regen_none);

  if ((type & regen_heights) == regen_heights)
    regenerate_heights(db);
  if ((type & regen_rosters) == regen_rosters)
    regenerate_rosters(db);
  if ((type & regen_branches) == regen_branches)
    regenerate_branches(db);
  if ((type & regen_file_sizes) == regen_file_sizes)
    regenerate_file_sizes(db);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
