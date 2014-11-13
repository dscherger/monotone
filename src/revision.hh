// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __REVISION_HH__
#define __REVISION_HH__

#include <set>
#include "vector.hh"
#include "rev_types.hh"

class key_store;
class node_restriction;
class path_restriction;

// a revision is a text object. It has a precise, normalizable serial form
// as UTF-8 text. it also has some sub-components. not all of these
// sub-components are separately serialized (they could be but there is no
// call for it). a grammar (aside from the parsing code) for the serialized
// form will show up here eventually. until then, here is an example.
//
// new_manifest [16afa28e8783987223993d67f54700f0ecfedfaa]
//
// old_revision [d023242b16cbdfd46686a5d217af14e3c339f2b4]
//
// delete "deleted-file.cc"
//
// rename "old-file.cc"
//     to "new-file.cc"
//
// add_file "added-file.cc"
//  content [da39a3ee5e6b4b0d3255bfef95601890afd80709]
//
// patch "changed-file.cc"
//  from [588fd8a7bcde43a46f0bde1dd1d13e9e77cf25a1]
//    to [559133b166c3154c864f912e9f9452bfc452dfdd]
//
// patch "new-file.cc"
//  from [95b50ede90037557fd0fbbfad6a9fdd67b0bf413]
//    to [bd39086b9da776fc22abd45734836e8afb59c8c0]

enum made_for { made_for_nobody, made_for_workspace, made_for_database };

class
revision_t : public origin_aware
{
public:
  revision_t() : made_for(made_for_nobody) {}

  // moveable, not copyable
  revision_t(revision_t && other) = default;
  revision_t(revision_t const &) = delete;
  revision_t & operator=(revision_t && other) = default;

  void check_sane() const;
  bool is_merge_node() const;
  // trivial revisions are ones that have no effect -- e.g., commit should
  // refuse to commit them, saying that there are no changes to commit.
  bool is_nontrivial() const;
  manifest_id new_manifest;
  edge_map edges;
  // workspace::put_work_rev refuses to apply a rev that doesn't have this
  // set to "workspace", and database::put_revision refuses to apply a rev
  // that doesn't have it set to "database".  the default constructor sets
  // it to "nobody".
  enum made_for made_for;
};

class graph_loader
{
 public:
  graph_loader(database & db) : db(db) {}

  std::set<revision_id> load_parents(revision_id const rid);
  std::set<revision_id> load_children(revision_id const rid);
  void load_ancestors(std::set<revision_id> & revs);
  void load_descendants(std::set<revision_id> & revs);

 private:
  database & db;
  enum load_direction { ancestors, descendants };

  void load_revs(load_direction const direction, std::set<revision_id> &);
};

inline revision_id const &
edge_old_revision(edge_entry const & e)
{
  return e.first;
}

inline revision_id const &
edge_old_revision(edge_map::const_iterator i)
{
  return i->first;
}

inline cset const &
edge_changes(edge_entry const & e)
{
  return *(e.second);
}

inline cset const &
edge_changes(edge_map::const_iterator i)
{
  return *(i->second);
}

template <> void
dump(revision_t const & rev, std::string & out);

revision_t
read_revision(revision_data const & dat);

revision_data
write_revision(revision_t const & rev);

revision_id
calculate_ident(revision_t const & rev);

// sanity checking

void
find_common_ancestor_for_merge(database & db,
                               revision_id const & left,
                               revision_id const & right,
                               revision_id & anc);

bool
is_ancestor(database & db, revision_id const & ancestor,
            revision_id const & descendent);

void
toposort(database & db,
         std::set<revision_id> const & revisions,
         std::vector<revision_id> & sorted);

void
erase_ancestors(database & db, std::set<revision_id> & revisions);

struct is_failure
{
  virtual bool operator()(revision_id const & rid) = 0;
  virtual ~is_failure() {};
};
void
erase_ancestors_and_failures(database & db,
                             std::set<revision_id> & revisions,
                             is_failure & p,
                             std::multimap<revision_id, revision_id> *inverse_graph_cache_ptr = NULL);

void
erase_descendants(database & db, std::set<revision_id> & revisions);

void
erase_descendants_and_failures(database & db,
                               std::set<revision_id> & revisions,
                               is_failure & p,
                               std::multimap<revision_id, revision_id> *inverse_graph_cache_ptr = NULL);

std::set<revision_id>
ancestry_difference(database & db, revision_id const & a,
                    std::set<revision_id> const & bs);


// FIXME: can probably optimize this passing a lookaside cache of the active
// frontier set of shared_ptr<roster_t>s, while traversing history.
std::set<node_id>
select_nodes_modified_by_rev(database & db,
                             revision_t const & rev,
                             roster_t const & roster);

revision_t
make_revision(revision_id const & old_rev_id,
              roster_t const & old_roster,
              roster_t const & new_roster);

revision_t
make_revision(parent_map const & old_rosters,
              roster_t const & new_roster);

// This overload takes a base roster and a changeset instead.
revision_t
make_revision(revision_id const & old_rev_id,
              roster_t const & old_roster,
              cset && changes);

// These functions produce a faked "new_manifest" id and discard all
// content-only changes from the cset.  They are only to be used to
// construct a revision that will be written to the workspace.  Don't use
// them for revisions written to the database or presented to the user.
revision_t
make_revision_for_workspace(revision_id const & old_rev_id,
                            cset && changes);

revision_t
make_revision_for_workspace(revision_id const & old_rev_id,
                            roster_t const & old_roster,
                            roster_t const & new_roster);

revision_t
make_revision_for_workspace(parent_map const & old_rosters,
                            roster_t const & new_roster);

revision_t
make_restricted_revision(parent_map const & old_rosters,
                         roster_t const & new_roster,
                         node_restriction const & mask);

revision_t
make_restricted_revision(parent_map const & old_rosters,
                         roster_t const & new_roster,
                         node_restriction const & mask,
                         cset & excluded,
                         utf8 const & cmd_name);

// basic_io access to printers and parsers
void
print_revision(basic_io::printer & printer,
               revision_t const & rev);

revision_t
parse_revision(basic_io::parser & parser);

void
print_edge(basic_io::printer & printer,
           edge_entry const & e);

void
parse_edge(basic_io::parser & parser,
           edge_map & es);

#endif // __REVISION_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
