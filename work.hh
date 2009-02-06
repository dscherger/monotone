#ifndef __WORK_HH__
#define __WORK_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <set>
#include "paths.hh"
#include "rev_types.hh"

class path_restriction;
class node_restriction;
struct content_merge_adaptor;
class lua_hooks;
class i18n_format;
struct options;
class app_state;

//
// this file defines structures to deal with the "workspace" of a tree
//
// at present the presence or absence of a workspace is intrinsically global
// state, because it affects things like file_path construction (over in
// paths.cc) and the current working directory.  also, there are a bunch of
// operations, mostly during program initialization, that are conditional on
// whether or not we are inside a workspace.  this has two visible
// consequences to this api: first, you cannot create more than one
// workspace object, and second, the workspace class has many class methods
// as well as many instance methods.  class methods can be used when you're
// not sure yet whether or not there is a workspace.  instance methods can
// only be used if there definitely is a workspace; the workspace object
// constructor will throw an N() if there isn't one.  (this can also be
// triggered by the class method require_workspace, for the sake of a few
// places that need to do that but not create the workspace object yet.)
//

//
// workspace book-keeping files are stored in a directory called _MTN, off
// the root of the workspace source tree (analogous to the CVS or .svn
// directories). there is no hierarchy of _MTN directories; only one exists,
// and it is always at the root. it contains the following files:
//

// _MTN/revision     -- this file can be thought of as an approximation to the
//                      revision that would be added to the database if one
//                      were to execute 'mtn commit' with the current set of
//                      changes.  it records the id of the revision that was
//                      checked out (the "parent revision") plus a cset
//                      describing pathname and attribute modifications
//                      relative to that revision.  if the workspace is the
//                      result of a merge, the revision will have more than
//                      one parent and thus more than one cset.  files
//                      changed solely in content do not appear in
//                      _MTN/revision; this is the major difference between
//                      the revision in this file and the revision that 'mtn
//                      commit' adds to the database.
// _MTN/options      -- the database, branch and key options currently in use
// _MTN/log          -- user edited log file
// _MTN/inodeprints  -- file fingerprint cache, see below
//
// as work proceeds, the files in the workspace either change their
// sha1 fingerprints from those listed in the revision's manifest, or else are
// added or deleted or renamed (and the paths of those changes recorded in
// '_MTN/revision').
//
// many operations need to work with a revision that accurately describes
// both pathname and content changes.  constructing this revision is the
// function of update_current_roster_from_filesystem().  this operation
// intrinsically requires reading every file in the workspace, which can be
// slow.  _MTN/inodeprints, if present, is used to speed up this process; it
// records information accessible via stat() that is expected to change
// whenever a file is modified.  this expectation is not true under all
// conditions, but works in practice (it is, for instance, the same
// expectation used by "make").  nonetheless, this mode is off by default.

bool directory_is_workspace(system_path const & dir);

struct workspace
{
  // This is a public flag because it's set from monotone.cc using a
  // function (find_and_go_to_workspace) which cannot presently be moved
  // from paths.cc.
  static bool found;

private:
  // This is used by get_ws_options and set_ws_options.
  static bool branch_is_sticky;

  // This is used by a lot of instance methods.
  lua_hooks & lua;

  // Interfaces.
public:
  static void require_workspace();
  static void require_workspace(i18n_format const & explanation);

  static void create_workspace(options const & opts,
                               lua_hooks & lua,
                               system_path const & new_dir);

  // Constructor.  In normal usage, calling this transitions from the state
  // where there may or may not be a workspace to the state where there
  // definitely is.
  explicit workspace(app_state & app, bool writeback_options = true);
  explicit workspace(app_state & app, i18n_format const & explanation,
                     bool writeback_options = true);
  explicit workspace(options const & opts, lua_hooks & lua,
                     i18n_format const & explanation, bool writeback_options = true);

  // Methods for manipulating the workspace's content.
  void find_missing(roster_t const & new_roster_shape,
                    node_restriction const & mask,
                    std::set<file_path> & missing);

  void find_unknown_and_ignored(database & db,
                                path_restriction const & mask,
                                std::vector<file_path> const & roots,
                                std::set<file_path> & unknown,
                                std::set<file_path> & ignored);

  void perform_additions(database & db,
                         std::set<file_path> const & targets,
                         bool recursive = false,
                         bool respect_ignore = true);

  void perform_deletions(database & db,
                         std::set<file_path> const & targets,
			 bool recursive, 
                         bool bookkeep_only);

  void perform_rename(database & db,
                      std::set<file_path> const & src_paths,
                      file_path const & dst_dir,
                      bool bookkeep_only);

  void perform_pivot_root(database & db,
                          file_path const & new_root,
                          file_path const & put_old,
                          bool bookkeep_only);

  void perform_content_update(database & db,
                              cset const & cs,
                              content_merge_adaptor const & ca,
                              bool messages = true);

  void update_any_attrs(database & db);
  void init_attributes(file_path const & path, editable_roster_base & er);

  bool has_changes(database & db);

  // write out a new (partial) revision describing the current workspace;
  // the important pieces of this are the base revision id and the "shape"
  // changeset (representing tree rearrangements).
  void put_work_rev(revision_t const & rev);

  // read the (partial) revision describing the current workspace.
  void get_work_rev(revision_t & rev);

  // convenience wrappers around the above functions.

  // This returns the current roster, except it does not bother updating the
  // hashes in that roster -- the "shape" is correct, all files and dirs
  // exist and under the correct names -- but do not trust file content
  // hashes.  If you need the current roster with correct file content
  // hashes, call update_current_roster_from_filesystem on the result of
  // this function.  Under almost all conditions, NIS should be a
  // temp_node_id_source.
  void get_current_roster_shape(database & db, node_id_source & nis,
                                roster_t & ros);

  // This returns a map whose keys are revision_ids and whose values are
  // rosters, there being one such pair for each parent of the current
  // revision.
  void get_parent_rosters(database & db, parent_map & parents);

  // This updates the file-content hashes in ROSTER, which is assumed to be
  // the "current" roster returned by one of the above get_*_roster_shape
  // functions.  If a node_restriction is provided, only the files matching
  // the restriction have their hashes updated.
  void update_current_roster_from_filesystem(roster_t & ros);
  void update_current_roster_from_filesystem(roster_t & ros,
                                             node_restriction const & mask);


  // the "user log" is a file the user can edit as they program to record
  // changes they make to their source code. Upon commit the file is read
  // and passed to the edit_comment lua hook. If the commit is a success,
  // the user log is then blanked. If the commit does not succeed, no
  // change is made to the user log file.

  void read_user_log(utf8 & dat);
  void write_user_log(utf8 const & dat);
  void blank_user_log();
  bool has_contents_user_log();

  // the "options map" is another administrative file, stored in
  // _MTN/options. it keeps a list of name/value pairs which are considered
  // "persistent options", associated with a particular workspace and
  // implied unless overridden on the command line.
  static void get_ws_options(options & opts);
  static void get_database_option(system_path const & workspace_root,
                                  system_path & database_option);
  static void set_ws_options(options const & opts, bool branch_is_sticky);
  static void print_ws_option(utf8 const & opt, std::ostream & output);

  // the "workspace format version" is a nonnegative integer value, stored
  // in _MTN/format as an unadorned decimal number.  at any given time
  // monotone supports actual use of only one workspace format.
  // check_ws_format throws an error if the workspace exists but its format
  // number is not equal to the currently supported format number.  it is
  // automatically called for all commands defined with CMD() (not
  // CMD_NO_WORKSPACE()).  migrate_ws_format is called only on explicit user
  // request (mtn ws migrate) and will convert a workspace from any older
  // format to the new one.  finally, write_ws_format is called only when a
  // workspace is created, and simply writes the current workspace format
  // number to _MTN/format.  unlike most routines in this class, these
  // functions are defined in their own file, work_migration.cc.
  static void check_ws_format();
  static void write_ws_format();
  void migrate_ws_format();

  // the "local dump file' is a debugging file, stored in _MTN/debug.  if we
  // crash, we save some debugging information here.

  static void get_local_dump_path(bookkeeping_path & d_path);

  // the 'inodeprints file' contains inode fingerprints

  bool in_inodeprints_mode();
  void read_inodeprints(data & dat);
  void write_inodeprints(data const & dat);

  void enable_inodeprints();
  void maybe_update_inodeprints(database &);

  // the 'ignore file', .mtn-ignore in the root of the workspace, contains a
  // set of regular expressions that match pathnames.  any file or directory
  // that exists, is unknown, and matches one of these regexps is treated as
  // if it did not exist, instead of being an unknown file.
  bool ignore_file(file_path const & path);
};

// This object turns the workspace ignore_file method into a path predicate,
// suitable for passing to restriction constructors (for instance).
struct ignored_file : public path_predicate<file_path>
{
  ignored_file(workspace & work) : work(work) {}
  bool operator()(file_path const &) const;

private:
  workspace & work;
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __WORK_HH__
 
