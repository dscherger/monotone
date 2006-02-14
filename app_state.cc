#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>              // for strtoul()

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>

#include "app_state.hh"
#include "database.hh"
#include "file_io.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "work.hh"
#include "platform.hh"

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

using namespace std;

static string const database_option("database");
static string const branch_option("branch");
static string const key_option("key");
static string const keydir_option("keydir");

app_state::app_state() 
  : branch_name(""), db(system_path()), keys(this), stdhooks(true),
    rcfiles(true), diffs(false),
    no_merges(false), set_default(false), verbose(false), date_set(false),
    search_root("/"),
    depth(-1), last(-1), diff_format(unified_diff), diff_args_provided(false),
    use_lca(false), execute(false), bind_address(""), bind_port(""), 
    missing(false), unknown(false),
    confdir(get_default_confdir()), have_set_key_dir(false)
{
  db.set_app(this);
  lua.set_app(this);
  keys.set_key_dir(confdir / "keys");
}

app_state::~app_state()
{
}

void
app_state::set_is_explicit_option (int option_id)
{
  explicit_option_map[option_id] = true;
}

bool
app_state::is_explicit_option(int option_id) const
{
  std::map<int, bool>::const_iterator i = explicit_option_map.find(option_id);
  if (i == explicit_option_map.end()) return false;
  return i->second;
}

void
app_state::allow_working_copy()
{
  L(F("initializing from directory %s\n") % fs::initial_path().string());
  found_working_copy = find_and_go_to_working_copy(search_root);

  if (found_working_copy) 
    {
      read_options();

      if (!options[database_option]().empty())
        {
          system_path dbname = system_path(options[database_option]);
          db.set_filename(dbname);
        }

      if (!options[keydir_option]().empty())
        {
          system_path keydir = system_path(options[keydir_option]);
          set_key_dir(keydir);
        }

      if (branch_name().empty())
        branch_name = options[branch_option];
      L(F("branch name is '%s'\n") % branch_name());
      internalize_rsa_keypair_id(options[key_option], signing_key);

      if (global_sanity.filename.empty())
        {
          bookkeeping_path dump_path;
          get_local_dump_path(dump_path);
          L(F("setting dump path to %s\n") % dump_path);
          // the 'true' means that, e.g., if we're running checkout, then it's
          // okay for dumps to go into our starting working dir's MT rather
          // than the checked-out dir's MT.
          global_sanity.filename = system_path(dump_path, false);
        }
    }
  load_rcfiles();
}

void 
app_state::require_working_copy(std::string const & explanation)
{
  N(found_working_copy,
    F("working copy directory required but not found%s%s")
    % (explanation.empty() ? "" : "\n") % explanation);
  write_options();
}

void 
app_state::create_working_copy(system_path const & new_dir)
{
  N(!new_dir.empty(), F("invalid directory ''"));

  L(F("creating working copy in %s\n") % new_dir);
  
  mkdir_p(new_dir);
  go_to_working_copy(new_dir);

  N(!directory_exists(bookkeeping_root),
    F("monotone bookkeeping directory '%s' already exists in '%s'\n") 
    % bookkeeping_root % new_dir);

  L(F("creating bookkeeping directory '%s' for working copy in '%s'\n")
    % bookkeeping_root % new_dir);

  mkdir_p(bookkeeping_root);

  make_branch_sticky();

  write_options();

  blank_user_log();

  if (lua.hook_use_inodeprints())
    enable_inodeprints();

  load_rcfiles();
}

void 
app_state::set_database(system_path const & filename)
{
  if (!filename.empty()) db.set_filename(filename);

  options[database_option] = filename.as_internal();
}

void 
app_state::set_key_dir(system_path const & filename)
{
  if (!filename.empty())
    {
      keys.set_key_dir(filename);
      have_set_key_dir = true;
    }

  options[keydir_option] = filename.as_internal();
}

void 
app_state::set_branch(utf8 const & branch)
{
  branch_name = branch();
}

void
app_state::make_branch_sticky()
{
  options[branch_option] = branch_name();
  if (found_working_copy)
    {
      // already have a working copy, can (must) write options directly,
      // because no-one else will do so
      // if we don't have a working copy yet, then require_working_copy (for
      // instance) will call write_options when it finds one.
      write_options();
    }
}

void 
app_state::set_signing_key(utf8 const & key)
{
  internalize_rsa_keypair_id(key, signing_key);

  options[key_option] = key;
}

void 
app_state::add_key_to_push(utf8 const & key)
{
  rsa_keypair_id k;
  internalize_rsa_keypair_id(key, k);
  keys_to_push.push_back(k);
}

void 
app_state::set_root(system_path const & path)
{
  require_path_is_directory(path,
                            F("search root '%s' does not exist") % path,
                            F("search root '%s' is not a directory\n") % path);
  search_root = path;
  L(F("set search root to %s\n") % search_root);
}

void
app_state::set_message(utf8 const & m)
{
  message = m;
}

void
app_state::set_message_file(utf8 const & m)
{
  message_file = m;
}

void
app_state::set_date(utf8 const & d)
{
  try
    {
      // boost::posix_time is lame: it can parse "basic" ISO times, of the
      // form 20000101T120000, but not "extended" ISO times, of the form
      // 2000-01-01T12:00:00.  So do something stupid to convert one to the
      // other.
      std::string tmp = d();
      std::string::size_type pos = 0;
      while ((pos = tmp.find_first_of("-:")) != string::npos)
        tmp.erase(pos, 1);
      date = boost::posix_time::from_iso_string(tmp);
      date_set = true;
    }
  catch (std::exception &e)
    {
      N(false, F("failed to parse date string '%s': %s") % d % e.what());
    }
}

void
app_state::set_author(utf8 const & a)
{
  author = a;
}

void
app_state::set_depth(long d)
{
  N(d >= 0,
    F("negative depth not allowed\n"));
  depth = d;
}

void
app_state::set_last(long l)
{
  N(l > 0,
    F("negative or zero last not allowed\n"));
  last = l;
}

void
app_state::set_pidfile(system_path const & p)
{
  pidfile = p;
}

void
app_state::add_revision(utf8 const & selector)
{
  revision_selectors.push_back(selector);
}

void
app_state::add_exclude(utf8 const & exclude_pattern)
{
  exclude_patterns.push_back(exclude_pattern);
}

void
app_state::set_diff_format(diff_type dtype)
{
  diff_format = dtype;
}

void
app_state::set_diff_args(utf8 const & args)
{
  diff_args_provided = true;
  diff_args = args;
}

void
app_state::set_stdhooks(bool b)
{
  stdhooks = b;
}

void
app_state::set_rcfiles(bool b)
{
  rcfiles = b;
}

void
app_state::set_verbose(bool b)
{
  verbose = b;
}

void
app_state::add_rcfile(utf8 const & filename)
{
  extra_rcfiles.push_back(filename);
}

void
app_state::set_confdir(system_path const & cd)
{
  confdir = cd;
  if (!have_set_key_dir)
    keys.set_key_dir(cd / "keys");
}

system_path
app_state::get_confdir()
{
  return confdir;
}

// rc files are loaded after we've changed to the working copy directory so
// that MT/monotonerc can be loaded between ~/.monotone/monotonerc and other
// rcfiles

void
app_state::load_rcfiles()
{
  // built-in rc settings are defaults

  if (stdhooks)
    lua.add_std_hooks();

  // ~/.monotone/monotonerc overrides that, and
  // MT/monotonerc overrides *that*

  if (rcfiles)
    {
      system_path default_rcfile;
      bookkeeping_path working_copy_rcfile;
      lua.default_rcfilename(default_rcfile);
      lua.working_copy_rcfilename(working_copy_rcfile);
      lua.load_rcfile(default_rcfile, false);
      lua.load_rcfile(working_copy_rcfile, false);
    }

  // command-line rcfiles override even that

  for (vector<utf8>::const_iterator i = extra_rcfiles.begin();
       i != extra_rcfiles.end(); ++i)
    {
      lua.load_rcfile(*i);
    }
}

void
app_state::read_options()
{
  bookkeeping_path o_path;
  get_options_path(o_path);
  try
    {
      if (path_exists(o_path))
        {
          data dat;
          read_data(o_path, dat);
          read_options_map(dat, options);
        }
    }
  catch(std::exception & e)
    {
      W(F("Failed to read options file %s") % o_path);
    }
}

void 
app_state::write_options()
{
  bookkeeping_path o_path;
  get_options_path(o_path);
  try
    {
      data dat;
      write_options_map(dat, options);
      write_data(o_path, dat);
    }
  catch(std::exception & e)
    {
      W(F("Failed to write options file %s") % o_path);
    }
}
