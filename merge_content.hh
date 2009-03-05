// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __MERGE_HH__
#define __MERGE_HH__

#include "vocab.hh"
#include "rev_types.hh"

class database;
class lua_hooks;
struct roster_merge_result;
struct options;

struct
content_merge_adaptor
{
  virtual void record_merge(file_id const & left_ident,
                            file_id const & right_ident,
                            file_id const & merged_ident,
                            file_data const & left_data,
                            file_data const & right_data,
                            file_data const & merged_data) = 0;

  // For use when one side of the merge is dropped
  virtual void record_file(file_id const & parent_ident,
                           file_id const & merged_ident,
                           file_data const & parent_data,
                           file_data const & merged_data) = 0;

  virtual void get_ancestral_roster(node_id nid,
                                    revision_id & rid,
                                    boost::shared_ptr<roster_t const> & anc) = 0;

  virtual void get_version(file_id const & ident,
                           file_data & dat) const = 0;

  virtual ~content_merge_adaptor() {}
};

struct
content_merge_database_adaptor
  : public content_merge_adaptor
{
  database & db;
  revision_id lca;
  revision_id left_rid;
  revision_id right_rid;
  marking_map const & left_mm;
  marking_map const & right_mm;
  std::map<revision_id, boost::shared_ptr<roster_t const> > rosters;
  content_merge_database_adaptor(database & db,
                                 revision_id const & left,
                                 revision_id const & right,
                                 marking_map const & left_mm,
                                 marking_map const & right_mm);
  void record_merge(file_id const & left_ident,
                    file_id const & right_ident,
                    file_id const & merged_ident,
                    file_data const & left_data,
                    file_data const & right_data,
                    file_data const & merged_data);

  void record_file(file_id const & parent_ident,
                   file_id const & merged_ident,
                   file_data const & parent_data,
                   file_data const & merged_data);

  void cache_roster(revision_id const & rid,
                    boost::shared_ptr<roster_t const> roster);

  void get_ancestral_roster(node_id nid,
                            revision_id & rid,
                            boost::shared_ptr<roster_t const> & anc);

  void get_version(file_id const & ident,
                   file_data & dat) const;
};

struct
content_merge_workspace_adaptor
  : public content_merge_adaptor
{
  std::map<file_id, file_data> temporary_store;
  database & db;
  revision_id const lca;
  boost::shared_ptr<roster_t const> base;
  marking_map const & left_mm;
  marking_map const & right_mm;
  std::map<revision_id, boost::shared_ptr<roster_t const> > rosters;
  std::map<file_id, file_path> content_paths;
  content_merge_workspace_adaptor(database & db,
                                  revision_id const & lca,
                                  boost::shared_ptr<roster_t const> base,
                                  marking_map const & left_mm,
                                  marking_map const & right_mm,
                                  std::map<file_id, file_path> const & paths)
    : db(db), lca(lca), base(base),
      left_mm(left_mm), right_mm(right_mm), content_paths(paths)
  {}

  void cache_roster(revision_id const & rid,
                    boost::shared_ptr<roster_t const> roster);

  void record_merge(file_id const & left_ident,
                    file_id const & right_ident,
                    file_id const & merged_ident,
                    file_data const & left_data,
                    file_data const & right_data,
                    file_data const & merged_data);

  void record_file(file_id const & parent_ident,
                   file_id const & merged_ident,
                   file_data const & parent_data,
                   file_data const & merged_data);

  void get_ancestral_roster(node_id nid,
                            revision_id & rid,
                            boost::shared_ptr<roster_t const> & anc);

  void get_version(file_id const & ident,
                   file_data & dat) const;
};

struct
content_merge_checkout_adaptor
  : public content_merge_adaptor
{
  database & db;
  content_merge_checkout_adaptor(database & db)
    : db(db)
  {}

  void record_merge(file_id const & left_ident,
                    file_id const & right_ident,
                    file_id const & merged_ident,
                    file_data const & left_data,
                    file_data const & right_data,
                    file_data const & merged_data);

  void record_file(file_id const & parent_ident,
                   file_id const & merged_ident,
                   file_data const & parent_data,
                   file_data const & merged_data);

  void get_ancestral_roster(node_id nid,
                            revision_id & rid,
                            boost::shared_ptr<roster_t const> & anc);

  void get_version(file_id const & ident,
                   file_data & dat) const;

};


struct
content_merge_empty_adaptor
  : public content_merge_adaptor
{
  void record_merge(file_id const & left_ident,
                    file_id const & right_ident,
                    file_id const & merged_ident,
                    file_data const & left_data,
                    file_data const & right_data,
                    file_data const & merged_data);

  void record_file(file_id const & parent_ident,
                   file_id const & merged_ident,
                   file_data const & parent_data,
                   file_data const & merged_data);

  void get_ancestral_roster(node_id nid,
                            revision_id & rid,
                            boost::shared_ptr<roster_t const> & anc);

  void get_version(file_id const & ident,
                   file_data & dat) const;
};

struct content_merger
{
  lua_hooks & lua;
  roster_t const & anc_ros;
  roster_t const & left_ros;
  roster_t const & right_ros;

  content_merge_adaptor & adaptor;

  content_merger(lua_hooks & lua,
                 roster_t const & anc_ros,
                 roster_t const & left_ros,
                 roster_t const & right_ros,
                 content_merge_adaptor & adaptor)
    : lua(lua),
      anc_ros(anc_ros),
      left_ros(left_ros),
      right_ros(right_ros),
      adaptor(adaptor)
  {}

  // Attempt merge3 on a file (line by line). Return true and valid data if
  // it would succeed; false and invalid data otherwise.
  bool attempt_auto_merge(file_path const & anc_path, // inputs
                          file_path const & left_path,
                          file_path const & right_path,
                          file_id const & ancestor_id,
                          file_id const & left_id,
                          file_id const & right_id,
                          file_data & left_data, // outputs
                          file_data & right_data,
                          file_data & merge_data);

  // Attempt merge3 on a file (line by line). If it succeeded, store results
  // in database and return true and valid merged_id; return false
  // otherwise.
  bool try_auto_merge(file_path const & anc_path,
                      file_path const & left_path,
                      file_path const & right_path,
                      file_path const & merged_path,
                      file_id const & ancestor_id,
                      file_id const & left_id,
                      file_id const & right,
                      file_id & merged_id);

  bool try_user_merge(file_path const & anc_path,
                      file_path const & left_path,
                      file_path const & right_path,
                      file_path const & merged_path,
                      file_id const & ancestor_id,
                      file_id const & left_id,
                      file_id const & right,
                      file_id & merged_id);

  std::string get_file_encoding(file_path const & path,
                                roster_t const & ros);

  bool attribute_manual_merge(file_path const & path,
                              roster_t const & ros);
};


// Destructively alter a roster_merge_result to attempt to remove any
// conflicts in it. Takes a content_merge_adaptor to pass on to the content
// merger; used from both the merge-to-database code (below) and the
// merge-to-workspace "update" code in commands.cc.

void
resolve_merge_conflicts(lua_hooks & lua,
                        roster_t const & left_roster,
                        roster_t const & right_roster,
                        roster_merge_result & result,
                        content_merge_adaptor & adaptor,
                        bool const resolutions_given);

// traditional resolve-all-conflicts-as-you-go style merging with 3-way merge
//   for file texts
// throws if merge fails
// writes out resulting revision to the db, along with author and date certs
//   (but _not_ branch or changelog certs)
// this version can only be used to merge revisions that are in the db, and
//   that are written straight back out to the db; some refactoring would
//   probably be good
// 'update' requires some slightly different interface, to deal with the gunk
//   around the revision and its files not being in the db, and the resulting
//   revision and its merged files not being written back to the db
void
interactive_merge_and_store(lua_hooks & lua,
                            database & db,
                            options const & opts,
                            revision_id const & left,
                            revision_id const & right,
                            revision_id & merged);

void
store_roster_merge_result(database & db,
                          roster_t const & left_roster,
                          roster_t const & right_roster,
                          roster_merge_result & result,
                          revision_id const & left_rid,
                          revision_id const & right_rid,
                          revision_id & merged_rid);

// Do a three-way merge on file content, expressed as vectors of
// strings (one per line).

bool merge3(std::vector<std::string> const & ancestor,
            std::vector<std::string> const & left,
            std::vector<std::string> const & right,
            std::vector<std::string> & merged);


#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
