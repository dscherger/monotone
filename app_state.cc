// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <cstdlib>              // for strtoul()
#include <iostream>
#include <string>
#include <vector>

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include "botan/pubkey.h"
#include "botan/rsa.h"

#include "app_state.hh"
#include "charset.hh"
#include "database.hh"
#include "file_io.hh"
#include "platform.hh"
#include "sanity.hh"
#include "ui.hh"
#include "work.hh"

using std::exception;
using std::map;
using std::string;
using std::vector;
using std::vector;

app_state::app_state()
  : db(system_path()),
    keys(this), work(db, lua),
//    search_root(current_root_path()),
//    diff_format(unified_diff),
    branch_is_sticky(false),
    branches(*this)
{
  db.set_app(this);
  lua.set_app(this);
  keys.set_key_dir(opts.conf_dir / "keys");
}

app_state::~app_state()
{
}


void
app_state::allow_workspace()
{
  L(FL("initializing from directory %s") % fs::initial_path().string());
  found_workspace = find_and_go_to_workspace(opts.root);

  if (found_workspace)
    {
      if (global_sanity.filename.empty())
        {
          bookkeeping_path dump_path;
          work.get_local_dump_path(dump_path);
          L(FL("setting dump path to %s") % dump_path);
          // The 'true' means that, e.g., if we're running checkout,
          // then it's okay for dumps to go into our starting working
          // dir's _MTN rather than the new workspace dir's _MTN.
          global_sanity.filename = system_path(dump_path, false).as_external();
        }
    }
  load_rcfiles();
}

void
app_state::process_options()
{
  utf8 database_option, branch_option, key_option, keydir_option;

  if (!found_workspace)
    return;

  work.check_ws_format();
  work.get_ws_options(database_option, branch_option,
                      key_option, keydir_option);

  // Workspace options are not to override the command line.
  if (db.get_filename().as_internal().empty() && !database_option().empty())
    db.set_filename(system_path(database_option));

  if (keys.get_key_dir().as_internal().empty() && !keydir_option().empty())
    set_key_dir(system_path(keydir_option));

  if (opts.branch_name().empty() && !branch_option().empty())
    {
      opts.branch_name = branch_option;
      branch_is_sticky = true;
    }

  L(FL("branch name is '%s'") % opts.branch_name);

  if (!opts.key_given)
    internalize_rsa_keypair_id(key_option, opts.signing_key);
}

void
app_state::write_options()
{
  utf8 database_option, branch_option, key_option, keydir_option;

  database_option = db.get_filename().as_internal();
  keydir_option = keys.get_key_dir().as_internal();

  if (branch_is_sticky)
    branch_option = opts.branch_name;

  if (opts.key_given)
    externalize_rsa_keypair_id(opts.signing_key, key_option);
  work.set_ws_options(database_option, branch_option,
                      key_option, keydir_option);
}

void
app_state::require_workspace(string const & explanation)
{
  N(found_workspace,
    F("workspace required but not found%s%s")
    % (explanation.empty() ? "" : "\n") % explanation);
  write_options();
}

void
app_state::create_workspace(system_path const & new_dir)
{
  N(!new_dir.empty(), F("invalid directory ''"));

  L(FL("creating workspace in %s") % new_dir);

  mkdir_p(new_dir);
  go_to_workspace(new_dir);

  N(!directory_exists(bookkeeping_root),
    F("monotone bookkeeping directory '%s' already exists in '%s'")
    % bookkeeping_root % new_dir);

  L(FL("creating bookkeeping directory '%s' for workspace in '%s'")
    % bookkeeping_root % new_dir);

  mkdir_p(bookkeeping_root);

  make_branch_sticky();

  write_options();

  work.write_ws_format();
  work.blank_user_log();

  if (lua.hook_use_inodeprints())
    work.enable_inodeprints();

  load_rcfiles();
}

void
app_state::set_database(system_path const & filename)
{
  if (!filename.empty())
    db.set_filename(filename);
}

void
app_state::set_key_dir(system_path const & filename)
{
  if (!filename.empty())
    {
      keys.set_key_dir(filename);
    }
}

void
app_state::make_branch_sticky()
{
  branch_is_sticky = true;
  if (found_workspace)
    {
      // Already have a workspace, can (must) write options directly,
      // because no-one else will do so. If we don't have a workspace
      // yet, then require_workspace (for instance) will call
      // write_options when it finds one.
      write_options();
    }
}

void
app_state::set_root(system_path const & path)
{
  require_path_is_directory
    (path,
     F("search root '%s' does not exist") % path,
     F("search root '%s' is not a directory") % path);
  opts.root = path;
  L(FL("set search root to %s") % opts.root);
}

// rc files are loaded after we've changed to the workspace so that
// _MTN/monotonerc can be loaded between ~/.monotone/monotonerc and other
// rcfiles.

void
app_state::load_rcfiles()
{
  // Built-in rc settings are defaults.

  if (!opts.nostd)
    lua.add_std_hooks();

  // ~/.monotone/monotonerc overrides that, and
  // _MTN/monotonerc overrides *that*.

  if (!opts.norc)
    {
      system_path default_rcfile;
      bookkeeping_path workspace_rcfile;
      lua.default_rcfilename(default_rcfile);
      lua.workspace_rcfilename(workspace_rcfile);
      lua.load_rcfile(default_rcfile, false);
      lua.load_rcfile(workspace_rcfile, false);
    }

  // Command-line rcfiles override even that.

  for (vector<utf8>::const_iterator i = opts.extra_rcfiles.begin();
       i != opts.extra_rcfiles.end(); ++i)
    {
      lua.load_rcfile(*i);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
