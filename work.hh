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

#include <string>
#include <set>
#include <map>

#include "vocab.hh"
#include "paths.hh"
#include "roster.hh"

//
// this file defines structures to deal with the "workspace" of a tree
//

//
// workspace book-keeping files are stored in a directory called _MTN, off
// the root of the workspace source tree (analogous to the CVS or .svn
// directories). there is no hierarchy of _MTN directories; only one exists,
// and it is always at the root. it contains the following files:
//
// _MTN/revision     -- contains the id of the checked out revision
// _MTN/work         -- (optional) a set of added, deleted or moved pathnames
//                      this file is, syntactically, a cset
// _MTN/options      -- the database, branch and key options currently in use
// _MTN/log          -- user edited log file
// _MTN/inodeprints  -- file fingerprint cache, presence turns on "reckless"
//                      mode
//
// as work proceeds, the files in the workspace either change their
// sha1 fingerprints from those listed in the revision's manifest, or else are
// added or deleted or renamed (and the paths of those changes recorded in
// '_MTN/work').
//
// when it comes time to commit, the cset in _MTN/work (which can have no
// deltas) is applied to the base roster, then a new roster is built by
// analyzing the content of every file in the roster, as it appears in the
// workspace. a final cset is calculated which contains the requisite
// deltas, and placed in a rev, which is written to the db.
//
// _MTN/inodeprints, if present, can be used to speed up this last step.

class path_restriction;
class content_merge_adaptor;
class database;
class app_state;
class lua_hooks;

struct workspace
{
  void find_missing(roster_t const & new_roster_shape,
                    node_restriction const & mask,
                    path_set & missing);

  void find_unknown_and_ignored(path_restriction const & mask,
				std::vector<file_path> const & roots,
                                path_set & unknown, path_set & ignored);

  void perform_additions(path_set const & targets, bool recursive = true);

  void perform_deletions(path_set const & targets, bool recursive, 
                         bool execute);

  void perform_rename(std::set<file_path> const & src_paths,
                      file_path const & dst_dir,
                      bool execute);

  void perform_pivot_root(file_path const & new_root,
                          file_path const & put_old,
                          bool execute);

  void perform_content_update(cset const & cs,
                              content_merge_adaptor const & ca);

  void update_any_attrs();

  // transitional: the write half of this is exposed, the read half isn't.
  // write out a new (partial) revision describing the current workspace;
  // the important pieces of this are the base revision id and the "shape"
  // changeset (representing tree rearrangements).
  void put_work_rev(revision_t const & rev);

  // the current cset representing uncommitted add/drop/rename operations
  // (not deltas)
  void get_work_cset(cset & w);

  // the base revision id that the current working copy was checked out from
  void get_revision_id(revision_id & c);

  // structures derived from the above
  void get_base_revision(revision_id & rid, roster_t & ros);
  void get_base_revision(revision_id & rid, roster_t & ros, marking_map & mm);
  void get_base_roster(roster_t & ros);

  // This returns the current roster, except it does not bother updating the
  // hashes in that roster -- the "shape" is correct, all files and dirs exist
  // and under the correct names -- but do not trust file content hashes.
  // If you need the current roster with correct file content hashes, call
  // update_current_roster_from_filesystem on the result of this function.
  void get_current_roster_shape(roster_t & ros, node_id_source & nis);

  // This returns both the base roster (as get_base_roster would) and the
  // current roster shape (as get_current_roster_shape would).  The caveats
  // for get_current_roster_shape also apply to this function.
  void get_base_and_current_roster_shape(roster_t & base_roster,
                                         roster_t & current_roster,
                                         node_id_source & nis);

  void classify_roster_paths(roster_t const & ros,
                             path_set & unchanged,
                             path_set & changed,
                             path_set & missing);

  void update_current_roster_from_filesystem(roster_t & ros, app_state & app);
  void update_current_roster_from_filesystem(roster_t & ros,
                                             node_restriction const & mask,
                                             app_state & app);


  // the "user log" is a file the user can edit as they program to record
  // changes they make to their source code. Upon commit the file is read
  // and passed to the edit_comment lua hook. If the commit is a success,
  // the user log is then blanked. If the commit does not succeed, no
  // change is made to the user log file.

  void get_user_log_path(bookkeeping_path & ul_path);
  void read_user_log(data & dat);
  void write_user_log(data const & dat);
  void blank_user_log();
  bool has_contents_user_log();

  // the "options map" is another administrative file, stored in
  // _MTN/options. it keeps a list of name/value pairs which are considered
  // "persistent options", associated with a particular the workspace and
  // implied unless overridden on the command line. the set of valid keys
  // corresponds exactly to the argument list of these functions.

  void get_ws_options(utf8 & database_option,
                      utf8 & branch_option,
                      utf8 & key_option,
                      utf8 & keydir_option);
  void set_ws_options(utf8 & database_option,
                      utf8 & branch_option,
                      utf8 & key_option,
                      utf8 & keydir_option);

  // the "workspace format version" is a nonnegative integer value, stored
  // in _MTN/format as an unadorned decimal number.  at any given time
  // monotone supports actual use of only one workspace format.
  // check_ws_format throws an error if the workspace's format number is not
  // equal to the currently supported format number.  it is automatically
  // called for all commands defined with CMD() (not CMD_NO_WORKSPACE()).
  // migrate_ws_format is called only on explicit user request (mtn ws
  // migrate) and will convert a workspace from any older format to the new
  // one.  unlike most routines in this class, it is defined in its own
  // file, work_migration.cc.  finally, write_ws_format is called only when
  // a workspace is created, and simply writes the current workspace format
  // number to _MTN/format.
  void check_ws_format(app_state & app);
  void migrate_ws_format();
  void write_ws_format();

  // the "local dump file' is a debugging file, stored in _MTN/debug.  if we
  // crash, we save some debugging information here.

  void get_local_dump_path(bookkeeping_path & d_path);

  // the 'inodeprints file' contains inode fingerprints

  void enable_inodeprints();
  void maybe_update_inodeprints(app_state & app);

  // constructor and locals.
  // by caching a pointer to the database, we don't have to pass
  // app_state into a lot of functions.
  workspace(database & db, lua_hooks & lua) : db(db), lua(lua) {};
private:
  database & db;
  lua_hooks & lua;
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __WORK_HH__
