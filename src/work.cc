// Copyright (C) 2009, 2010, 2012, 2014 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "work.hh"

#include <ostream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <queue>

#include "lexical_cast.hh"
#include "basic_io.hh"
#include "cset.hh"
#include "file_io.hh"
#include "platform-wrapped.hh"
#include "restrictions.hh"
#include "sanity.hh"
#include "safe_map.hh"
#include "revision.hh"
#include "inodeprint.hh"
#include "merge_content.hh"
#include "charset.hh"
#include "app_state.hh"
#include "database.hh"
#include "roster.hh"
#include "transforms.hh"
#include "vocab_cast.hh"

using std::deque;
using std::exception;
using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::lexical_cast;

// workspace / book-keeping file code

static char const inodeprints_file_name[] = "inodeprints";
static char const local_dump_file_name[] = "debug";
static char const options_file_name[] = "options";
static char const user_log_file_name[] = "log";
static char const commit_file_name[] = "commit";
static char const revision_file_name[] = "revision";
static char const update_file_name[] = "update";
static char const bisect_file_name[] = "bisect";

static bookkeeping_path
get_revision_path()
{
  bookkeeping_path m_path = bookkeeping_root / revision_file_name;
  L(FL("revision path is %s") % m_path);
  return m_path;
}

static void
get_options_path(bookkeeping_path & o_path)
{
  o_path = bookkeeping_root / options_file_name;
  L(FL("options path is %s") % o_path);
}

static void
get_options_path(system_path const & workspace, system_path & o_path)
{
  o_path = workspace / bookkeeping_root_component / options_file_name;
  L(FL("options path is %s") % o_path);
}

static void
get_inodeprints_path(bookkeeping_path & ip_path)
{
  ip_path = bookkeeping_root / inodeprints_file_name;
  L(FL("inodeprints path is %s") % ip_path);
}

static void
get_user_log_path(bookkeeping_path & ul_path)
{
  ul_path = bookkeeping_root / user_log_file_name;
  L(FL("user log path is %s") % ul_path);
}

static bookkeeping_path
get_commit_path()
{
  bookkeeping_path commit_path = bookkeeping_root / commit_file_name;
  L(FL("commit path is %s") % commit_path);
  return commit_path;
}

static bookkeeping_path
get_update_path()
{
  bookkeeping_path update_path = bookkeeping_root / update_file_name;
  L(FL("update path is %s") % update_path);
  return update_path;
}

static bookkeeping_path
get_bisect_path()
{
  bookkeeping_path bisect_path = bookkeeping_root / bisect_file_name;
  L(FL("bisect path is %s") % bisect_path);
  return bisect_path;
}

//

bool
directory_is_workspace(system_path const & dir)
{
  // as far as the users of this function are concerned, a version 0
  // workspace (MT directory instead of _MTN) does not count.
  return directory_exists(dir / bookkeeping_root_component);
}

bool workspace::found;
bool workspace::used;
bool workspace::branch_is_sticky;

void
workspace::require_workspace()
{
  E(workspace::found, origin::user,
    F("workspace required but not found"));
  workspace::used = true;
}

void
workspace::require_workspace(i18n_format const & explanation)
{
  E(workspace::found, origin::user,
    F("workspace required but not found\n%s") % explanation.str());
  workspace::used = true;
}

void
workspace::create_workspace(options const & opts,
                            lua_hooks & lua,
                            system_path const & new_dir)
{
  E(!new_dir.empty(), origin::user, F("invalid directory ''"));

  L(FL("creating workspace in %s") % new_dir);

  mkdir_p(new_dir);
  go_to_workspace(new_dir);
  mark_std_paths_used();

  E(!directory_exists(bookkeeping_root), origin::user,
    F("monotone bookkeeping directory '%s' already exists in '%s'")
    % bookkeeping_root % new_dir);

  L(FL("creating bookkeeping directory '%s' for workspace in '%s'")
    % bookkeeping_root % new_dir);

  mkdir_p(bookkeeping_root);

  workspace::found = true;
  workspace::set_options(opts, lua, true);
  workspace::write_format();

  data empty;
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);
  write_data(ul_path, empty);

  if (lua.hook_use_inodeprints())
    {
      bookkeeping_path ip_path;
      get_inodeprints_path(ip_path);
      write_data(ip_path, empty);
    }

  // The 'false' means that, e.g., if we're running checkout,
  // then it's okay for dumps to go into our starting working
  // dir's _MTN rather than the new workspace dir's _MTN.
  global_sanity.set_dump_path
    (system_path(workspace::get_local_dump_path(), false).as_external());
}

// Normal-use constructor.
workspace::workspace(app_state & app)
  : lua(app.lua)
{
  require_workspace();
}

workspace::workspace(app_state & app, i18n_format const & explanation)
  : lua(app.lua)
{
  require_workspace(explanation);
}

workspace::workspace(lua_hooks & lua, i18n_format const & explanation)
  : lua(lua)
{
  require_workspace(explanation);
}

// routines for manipulating the bookkeeping directory

// revision file contains a partial revision describing the workspace
revision_t
workspace::get_work_rev()
{
  bookkeeping_path rev_path(get_revision_path());
  data rev_data;
  MM(rev_data);
  try
    {
      rev_data = read_data(rev_path);
    }
  catch(exception & e)
    {
      E(false, origin::system,
        F("workspace is corrupt: reading '%s': %s")
        % rev_path % e.what());
    }

  revision_t rev = read_revision(rev_data);
  // Mark it so it doesn't creep into the database.
  rev.made_for = made_for_workspace;
  return rev;
}

void
workspace::put_work_rev(revision_t const & rev)
{
  MM(rev);
  I(rev.made_for == made_for_workspace);
  rev.check_sane();

  data rev_data;
  write_revision(rev, rev_data);

  write_data(get_revision_path(), rev_data);
}

revision_id
workspace::get_update_id()
{
  revision_id update_id;
  bookkeeping_path update_path = get_update_path();
  E(file_exists(update_path), origin::user,
    F("no update has occurred in this workspace"));

  data update_data = read_data(update_path);
  update_id = revision_id(decode_hexenc(update_data(), origin::internal),
                          origin::internal);
  E(!null_id(update_id), origin::internal,
    F("no update revision available"));
  return update_id;
}

void
workspace::put_update_id(revision_id const & update_id)
{
  data update_data(encode_hexenc(update_id.inner()(), origin::internal),
                   origin::internal);
  bookkeeping_path update_path = get_update_path();
  write_data(update_path, update_data);
}

// structures derived from the work revision, the database, and possibly
// the workspace

static cached_roster
get_roster_for_rid(database & db,
                   revision_id const & rid)
{
  cached_roster cr;
  // We may be asked for a roster corresponding to the null rid, which
  // is not in the database.  In this situation, what is wanted is an empty
  // roster (and marking map).
  if (null_id(rid))
    {
      cr.first = std::shared_ptr<roster_t const>(new roster_t);
      cr.second = std::shared_ptr<marking_map const>(new marking_map);
    }
  else
    {
      E(db.revision_exists(rid), origin::user,
        F("base revision %s does not exist in database") % rid);
      db.get_roster(rid, cr);
    }
  L(FL("base roster has %d entries") % cr.first->all_nodes().size());
  return cr;
}

void
workspace::require_parents_in_db(database & db,
                                 revision_t const & rev)
{
  for (edge_map::const_iterator i = rev.edges.begin();
       i != rev.edges.end(); i++)
    {
      revision_id const & parent(edge_old_revision(i));
      E(null_id(parent) || db.revision_exists(parent), origin::user,
        F("parent revision %s does not exist, did you specify the wrong database?")
        % parent);
    }
}

parent_map
workspace::get_parent_rosters(database & db)
{
  revision_t rev = get_work_rev();
  require_parents_in_db(db, rev);

  parent_map parents;
  for (edge_map::const_iterator i = rev.edges.begin();
       i != rev.edges.end(); i++)
    {
      cached_roster cr = get_roster_for_rid(db, edge_old_revision(i));
      safe_insert(parents, make_pair(edge_old_revision(i), cr));
    }

  return parents;
}

roster_t
workspace::get_current_roster_shape(database & db)
{
  temp_node_id_source nis;
  return get_current_roster_shape(db, nis);
}

roster_t
workspace::get_current_roster_shape(database & db,
                                    node_id_source & nis)
{
  revision_t rev = get_work_rev();
  require_parents_in_db(db, rev);
  revision_id new_rid(fake_id());

  // If there is just one parent, it might be the null ID, which
  // make_roster_for_revision does not handle correctly.
  roster_t ros;
  if (rev.edges.size() == 1 && null_id(edge_old_revision(rev.edges.begin())))
    {
      editable_roster_base er(ros, nis);
      edge_changes(rev.edges.begin()).apply_to(er);
    }
  else
    {
      marking_map dummy;
      make_roster_for_revision(db, nis, rev, new_rid, ros, dummy);
    }
  return ros;
}

bool
workspace::has_changes(database & db)
{
  parent_map parents = get_parent_rosters(db);

  // if we have more than one parent roster then this workspace contains
  // a merge which means this is always a committable change
  if (parents.size() > 1)
    return true;

  roster_t old_roster = parent_roster(parents.begin()),
    new_roster = get_current_roster_shape(db);

  update_current_roster_from_filesystem(new_roster);

  return !(old_roster == new_roster);
}

// user log file

utf8
workspace::read_user_log()
{
  utf8 result;
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);

  if (file_exists(ul_path))
    {
      data tmp = read_data(ul_path);
      system_to_utf8(typecast_vocab<external>(tmp), result);
    }

  return result;
}

void
workspace::write_user_log(utf8 const & dat)
{
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);

  external tmp;
  utf8_to_system_best_effort(dat, tmp);
  write_data(ul_path, typecast_vocab<data>(tmp));
}

void
workspace::blank_user_log()
{
  data empty;
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);
  write_data(ul_path, empty);
}

bool
workspace::has_contents_user_log()
{
  utf8 user_log_message = read_user_log();
  return user_log_message().length() > 0;
}

// commit buffer backup file

utf8
workspace::load_commit_text()
{
  utf8 dat;
  bookkeeping_path commit_path = get_commit_path();
  if (file_exists(commit_path))
    {
      data tmp = read_data(commit_path);
      system_to_utf8(typecast_vocab<external>(tmp), dat);
    }

  return dat;
}

void
workspace::save_commit_text(utf8 const & dat)
{
  bookkeeping_path commit_path = get_commit_path();

  external tmp;
  utf8_to_system_best_effort(dat, tmp);
  write_data(commit_path, typecast_vocab<data>(tmp));
}

void
workspace::clear_commit_text()
{
  delete_file(get_commit_path());
}

// _MTN/options handling.

static void
read_options_file(any_path const & optspath,
                  options & opts)
{
  data dat;
  try
    {
      dat = read_data(optspath);
    }
  catch (exception & e)
    {
      W(F("Failed to read options file '%s': %s") % optspath % e.what());
      return;
    }

  basic_io::input_source src(dat(), optspath.as_external(), origin::workspace);
  basic_io::tokenizer tok(src);
  basic_io::parser parser(tok);

  while (parser.symp())
    {
      string opt, val;
      parser.sym(opt);
      parser.str(val);

      if (opt == "database")
        {
          E(val != memory_db_identifier, origin::user,
            F("a memory database '%s' cannot be used in a workspace")
                % memory_db_identifier);

          if (val.find(':') == 0)
            {
              opts.dbname_alias = val;
              opts.dbname_given = true;
              opts.dbname_type = managed_db;
            }
          else
            {
              opts.dbname = system_path(val, origin::workspace);
              opts.dbname_given = true;
              opts.dbname_type = unmanaged_db;
            }
        }
      else if (opt == "branch")
        {
          opts.branch = branch_name(val, origin::workspace);
          opts.branch_given = true;
        }
      else if (opt == "key")
        {
          opts.key = external_key_name(val, origin::workspace);
          opts.key_given = true;
        }
      else if (opt == "keydir")
        {
          opts.key_dir = system_path(val, origin::workspace);
          opts.key_dir_given = true;
        }
      else
        W(F("unrecognized key '%s' in options file '%s' - ignored")
          % opt % optspath);
    }
  E(src.lookahead == EOF, src.made_from,
    F("Could not parse entire options file '%s'") % optspath);
}

static void
write_options_file(bookkeeping_path const & optspath,
                   options const & opts)
{
  basic_io::stanza st;

  E(opts.dbname_type != memory_db, origin::user,
    F("a memory database '%s' cannot be used in a workspace")
      % memory_db_identifier);

  // if we have both, alias and full path, prefer the alias
  if (opts.dbname_type == managed_db && !opts.dbname_alias.empty())
    st.push_str_pair(symbol("database"), opts.dbname_alias);
  else
  if (opts.dbname_type == unmanaged_db && !opts.dbname.as_internal().empty())
    st.push_str_pair(symbol("database"), opts.dbname.as_internal());

  if (!opts.branch().empty())
    st.push_str_pair(symbol("branch"), opts.branch());
  if (!opts.key().empty())
    st.push_str_pair(symbol("key"), opts.key());
  if (!opts.key_dir.as_internal().empty())
    st.push_str_pair(symbol("keydir"), opts.key_dir.as_internal());

  basic_io::printer pr;
  pr.print_stanza(st);
  try
    {
      write_data(optspath, data(pr.buf, origin::internal));
    }
  catch(exception & e)
    {
      W(F("Failed to write options file '%s': %s") % optspath % e.what());
    }
}

void
workspace::append_options_to(options & opts)
{
  if (!workspace::found)
    return;

  options cur_opts;
  bookkeeping_path o_path;
  get_options_path(o_path);
  read_options_file(o_path, cur_opts);

  // Workspace options are not to override the command line.
  if (!opts.dbname_given)
    {
      opts.dbname = cur_opts.dbname;
      opts.dbname_alias = cur_opts.dbname_alias;
      opts.dbname_type = cur_opts.dbname_type;
      opts.dbname_given = cur_opts.dbname_type;
    }

  if (!opts.key_dir_given && !opts.conf_dir_given && cur_opts.key_dir_given)
    { // if empty/missing, we want to keep the default
      opts.key_dir = cur_opts.key_dir;
      // one would expect that we should set the key_dir_given flag here, but
      // we do not because of the interaction between --confdir and --keydir.
      // If --keydir is not given and --confdir is, then --keydir will default
      // to the "keys" subdirectory of the given confdir. This works by the
      // --confdir option body looking at key_dir_given; if reading the keydir
      // from _MTN/options set that, then --confdir would stop setting the
      // default keydir when in a workspace.
      //opts.key_dir_given = true;
    }

  if (opts.branch().empty() && cur_opts.branch_given)
    {
      opts.branch = cur_opts.branch;
      branch_is_sticky = true;
    }

  L(FL("branch name is '%s'") % opts.branch);

  if (!opts.key_given)
    opts.key = cur_opts.key;
}

options
workspace::get_options(system_path const & workspace_root)
{
  system_path o_path = (workspace_root
                        / bookkeeping_root_component
                        / options_file_name);
  options opts;
  read_options_file(o_path, opts);
  return opts;
}

void
workspace::maybe_set_options(options const & opts, lua_hooks & lua)
{
  if (workspace::found && workspace::used)
      set_options(opts, lua, false);
}

// This function should usually be called at the (successful)
// execution of a function, because we don't do many checks here, f.e.
// if this is a valid sqlite file and if it contains the correct identifier,
// so be warned that you do not call this too early
void
workspace::set_options(options const & opts, lua_hooks & lua,
                       bool branch_is_sticky)
{
  E(workspace::found, origin::user, F("workspace required but not found"));

  bookkeeping_path o_path;
  get_options_path(o_path);

  database_path_helper helper(lua);
  system_path old_db_path, new_db_path;

  helper.get_database_path(opts, new_db_path);

  // If any of the incoming options was empty, we want to leave that option
  // as is in _MTN/options, not write out an empty option.
  options cur_opts;
  if (file_exists(o_path))
  {
    read_options_file(o_path, cur_opts);
    helper.get_database_path(cur_opts, old_db_path);
  }

  bool options_changed = false;
  if (old_db_path != new_db_path && file_exists(new_db_path))
    {
      // remove the currently registered workspace from the old
      // database and add it to the new one
      system_path current_workspace;
      get_current_workspace(current_workspace);

      if (cur_opts.dbname_given)
        {
          try
            {
              database old_db(cur_opts, lua);
              old_db.unregister_workspace(current_workspace);
            }
          catch (recoverable_failure & rf)
            {
              W(F("could not unregister workspace from old database '%s'")
                % old_db_path);
            }
        }

      database new_db(opts, lua);
      new_db.register_workspace(current_workspace);

      cur_opts.dbname_type = opts.dbname_type;
      cur_opts.dbname_alias = opts.dbname_alias;
      cur_opts.dbname = opts.dbname;
      options_changed = true;
    }

  if (!opts.key_dir.as_internal().empty() &&
      directory_exists(opts.key_dir) &&
      cur_opts.key_dir != opts.key_dir)
    {
      cur_opts.key_dir = opts.key_dir;
      options_changed = true;
    }

  if ((branch_is_sticky || workspace::branch_is_sticky) &&
      !opts.branch().empty() &&
      cur_opts.branch != opts.branch)
    {
      cur_opts.branch = opts.branch;
      options_changed = true;
    }

  if (opts.key_given && cur_opts.key != opts.key)
    {
      cur_opts.key = opts.key;
      options_changed = true;
    }

  // only rewrite the options file if there are actual changes
  if (options_changed)
    {
      L(FL("workspace options changed - writing back to _MTN/options"));
      write_options_file(o_path, cur_opts);
    }
}

void
workspace::print_option(utf8 const & opt, std::ostream & output)
{
  E(workspace::found, origin::user, F("workspace required but not found"));

  bookkeeping_path o_path;
  get_options_path(o_path);

  options opts;
  read_options_file(o_path, opts);

  if (opt() == "database")
    output << opts.dbname << '\n';
  else if (opt() == "branch")
    output << opts.branch << '\n';
  else if (opt() == "key")
    output << opts.key << '\n';
  else if (opt() == "keydir")
    output << opts.key_dir << '\n';
  else
    E(false, origin::user, F("'%s' is not a recognized workspace option") % opt);
}

// _MTN/bisect handling.

namespace syms
{
    symbol const start("start");
    symbol const good("good");
    symbol const bad("bad");
    symbol const skipped("skipped");
};

vector<bisect::entry>
workspace::get_bisect_info()
{
  bookkeeping_path bisect_path = get_bisect_path();

  if (!file_exists(bisect_path))
    return vector<bisect::entry>();

  data dat = read_data(bisect_path);

  string name("bisect");
  basic_io::input_source src(dat(), name, origin::workspace);
  basic_io::tokenizer tok(src);
  basic_io::parser parser(tok);

  vector<bisect::entry> bisect;
  while (parser.symp())
    {
      string rev;
      bisect::type type;
      if (parser.symp(syms::start))
        {
          parser.sym();
          parser.hex(rev);
          type = bisect::start;
        }
      else if (parser.symp(syms::good))
        {
          parser.sym();
          parser.hex(rev);
          type = bisect::good;
        }
      else if (parser.symp(syms::bad))
        {
          parser.sym();
          parser.hex(rev);
          type = bisect::bad;
        }
      else if (parser.symp(syms::skipped))
        {
          parser.sym();
          parser.hex(rev);
          type = bisect::skipped;
        }
      else
        I(false);

      revision_id rid =
        decode_hexenc_as<revision_id>(rev, parser.tok.in.made_from);
      bisect.push_back(make_pair(type, rid));
    }

  return bisect;
}

void
workspace::put_bisect_info(vector<bisect::entry> const & bisect)
{
  bookkeeping_path bisect_path = get_bisect_path();

  basic_io::stanza st;
  for (vector<bisect::entry>::const_iterator i = bisect.begin();
       i != bisect.end(); ++i)
    {
      switch (i->first)
        {
        case bisect::start:
          st.push_binary_pair(syms::start, i->second.inner());
          break;

        case bisect::good:
          st.push_binary_pair(syms::good, i->second.inner());
          break;

        case bisect::bad:
          st.push_binary_pair(syms::bad, i->second.inner());
          break;

        case bisect::skipped:
          st.push_binary_pair(syms::skipped, i->second.inner());
          break;

        case bisect::update:
          // this value is not persisted, it is only used by the bisect
          // update command to rerun a selection and update based on current
          // bisect information
          I(false);
          break;
        }
    }

  basic_io::printer pr;
  pr.print_stanza(st);
  data dat(pr.buf, origin::internal);

  write_data(bisect_path, dat);
}

void
workspace::remove_bisect_info()
{
  delete_file(get_bisect_path());
}

// local dump file

bookkeeping_path
workspace::get_local_dump_path()
{
  E(workspace::found, origin::user, F("workspace required but not found"));

  bookkeeping_path d_path = bookkeeping_root / local_dump_file_name;
  L(FL("local dump path is %s") % d_path);
  return d_path;
}

// inodeprint file

bool
workspace::in_inodeprints_mode()
{
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  return file_exists(ip_path);
}

data
workspace::read_inodeprints()
{
  I(in_inodeprints_mode());
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  return read_data(ip_path);
}

void
workspace::write_inodeprints(data const & dat)
{
  I(in_inodeprints_mode());
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  write_data(ip_path, dat);
}

void
workspace::enable_inodeprints()
{
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  data dat;
  write_data(ip_path, dat);
}

void
workspace::maybe_update_inodeprints(database & db)
{
  maybe_update_inodeprints(db, node_restriction());
}

void
workspace::maybe_update_inodeprints(database & db,
                                    node_restriction const & mask)
{
  if (!in_inodeprints_mode())
    return;

  // We update the cache only for files that are included in the
  // restriction. The only guarantee that inodeprints mode makes is that if
  // a file's current inodeprint matches its cached inodeprint then it has
  // not changed. i.e. for a missing file, the cache would not be updated
  // but the old cached value can't possibly equal the current value since
  // the file does not exist and cannot have an inodeprint.

  inodeprint_map ipm_new;
  roster_t new_roster = get_current_roster_shape(db);
  update_current_roster_from_filesystem(new_roster, mask);

  parent_map parents = get_parent_rosters(db);

  node_map const & new_nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = new_nodes.begin(); i != new_nodes.end(); ++i)
    {
      node_id nid = i->first;

      if (!mask.includes(new_roster, nid))
        continue;

      if (!is_file_t(i->second))
        continue;
      file_t new_file = downcast_to_file_t(i->second);
      bool all_same = true;

      for (parent_map::const_iterator parent = parents.begin();
           parent != parents.end(); ++parent)
        {
          roster_t const & parent_ros = parent_roster(parent);
          if (parent_ros.has_node(nid))
            {
              const_node_t old_node = parent_ros.get_node(nid);
              I(is_file_t(old_node));
              const_file_t old_file = downcast_to_file_t(old_node);

              if (new_file->content != old_file->content)
                {
                  all_same = false;
                  break;
                }
            }
        }

      if (all_same)
        {
          file_path fp;
          new_roster.get_name(nid, fp);
          hexenc<inodeprint> ip;
          if (inodeprint_file(fp, ip))
            ipm_new.insert(inodeprint_entry(fp, ip));
        }
    }
  data dat;
  write_inodeprint_map(ipm_new, dat);
  write_inodeprints(dat);
}

bool
workspace::ignore_file(file_path const & path)
{
  return lua.hook_ignore_file(path);
}

bool
ignored_file::operator()(file_path const & f) const
{
  return work.ignore_file(f);
}

void
workspace::init_attributes(file_path const & path, editable_roster_base & er)
{
  map<string, string> attrs;
  lua.hook_init_attributes(path, attrs);
  if (!attrs.empty())
    for (map<string, string>::const_iterator i = attrs.begin();
         i != attrs.end(); ++i)
      er.set_attr(path, attr_key(i->first, origin::user),
                  attr_value(i->second, origin::user));
}

// objects and routines for manipulating the workspace itself
namespace {

struct file_itemizer : public tree_walker
{
  database & db;
  workspace & work;
  set<file_path> & known;
  set<file_path> & unknown;
  set<file_path> & ignored;
  path_restriction const & mask;
  bool const recurse;
  file_itemizer(database & db, workspace & work,
                set<file_path> & k,
                set<file_path> & u,
                set<file_path> & i,
                path_restriction const & r,
                bool recurse)
    : db(db), work(work), known(k), unknown(u), ignored(i), mask(r), recurse(recurse) {}
  virtual bool visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
};


bool
file_itemizer::visit_dir(file_path const & path)
{
  this->visit_file(path);
  // Don't recurse into ignored directories, even for 'ls ignored'.
  return recurse && ignored.find(path) == ignored.end();
}

void
file_itemizer::visit_file(file_path const & path)
{
  if (mask.includes(path) && known.find(path) == known.end())
    {
      if (work.ignore_file(path) || db.is_dbfile(path))
        ignored.insert(path);
      else
        unknown.insert(path);
    }
}


struct workspace_itemizer : public tree_walker
{
  roster_t & roster;
  set<file_path> const & known;
  node_id_source & nis;

  workspace_itemizer(roster_t & roster, set<file_path> const & paths,
                     node_id_source & nis);
  virtual bool visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
};

workspace_itemizer::workspace_itemizer(roster_t & roster,
                                       set<file_path> const & paths,
                                       node_id_source & nis)
    : roster(roster), known(paths), nis(nis)
{
  node_id root_nid = roster.create_dir_node(nis);
  roster.attach_node(root_nid, file_path_internal(""));
}

bool
workspace_itemizer::visit_dir(file_path const & path)
{
  node_id nid = roster.create_dir_node(nis);
  roster.attach_node(nid, path);
  return known.find(path) != known.end();
}

void
workspace_itemizer::visit_file(file_path const & path)
{
  file_id fid;
  node_id nid = roster.create_file_node(fid, nis);
  roster.attach_node(nid, path);
}


class
addition_builder
  : public tree_walker
{
  database & db;
  workspace & work;
  roster_t & ros;
  editable_roster_base & er;
  bool respect_ignore;
  bool recursive;
public:
  addition_builder(database & db, workspace & work,
                   roster_t & r, editable_roster_base & e,
                   bool i, bool rec)
    : db(db), work(work), ros(r), er(e), respect_ignore(i), recursive(rec)
  {}
  virtual bool visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
  void add_nodes_for(file_path const & path, file_path const & goal);
};

void
addition_builder::add_nodes_for(file_path const & path,
                                file_path const & goal)
{
  // this check suffices to terminate the recursion; our caller guarantees
  // that the roster has a root node, which will be a directory.
  if (ros.has_node(path))
    {
      E(is_dir_t(ros.get_node(path)), origin::user,
        F("cannot add '%s', because '%s' is recorded as a file "
          "in the workspace manifest") % goal % path);
      return;
    }

  add_nodes_for(path.dirname(), goal);
  P(F("adding '%s' to workspace manifest") % path);

  node_id nid = the_null_node;
  switch (get_path_status(path))
    {
    case path::nonexistent:
      return;
    case path::file:
      {
        file_id ident;
        I(ident_existing_file(path, ident));
        nid = er.create_file_node(ident);
      }
      break;
    case path::directory:
      nid = er.create_dir_node();
      break;
    }

  I(nid != the_null_node);
  er.attach_node(nid, path);

  work.init_attributes(path, er);
}


bool
addition_builder::visit_dir(file_path const & path)
{
  this->visit_file(path);
  // when --recursive, don't recurse into ignored dirs (it would just waste time)
  // when --no-recursive, this result is ignored (see workspace::perform_additions)
  return !work.ignore_file(path);
}

void
addition_builder::visit_file(file_path const & path)
{
  if ((respect_ignore && work.ignore_file(path)) || db.is_dbfile(path))
    {
      P(F("skipping ignorable file '%s'") % path);
      return;
    }

  if (ros.has_node(path))
    {
      if (!path.empty())
        P(F("skipping '%s', already accounted for in workspace") % path);
      return;
    }

  I(ros.has_root());
  add_nodes_for(path, path);
}

struct editable_working_tree : public editable_tree
{
  editable_working_tree(workspace & work, lua_hooks & lua,
                        content_merge_adaptor const & source,
                        bool const messages)
    : work(work), lua(lua), source(source), next_nid(1),
      root_dir_attached(true), messages(messages)
  {};

  virtual node_id detach_node(file_path const & src);
  virtual void drop_detached_node(node_id nid);

  virtual node_id create_dir_node();
  virtual node_id create_file_node(file_id const & content);
  virtual void attach_node(node_id nid, file_path const & dst);

  virtual void apply_delta(file_path const & pth,
                           file_id const & old_id,
                           file_id const & new_id);
  virtual void clear_attr(file_path const & path,
                          attr_key const & key);
  virtual void set_attr(file_path const & path,
                        attr_key const & key,
                        attr_value const & val);

  virtual void commit();

  virtual ~editable_working_tree();
private:
  workspace & work;
  lua_hooks & lua;
  content_merge_adaptor const & source;
  node_id next_nid;
  std::map<bookkeeping_path, file_path> rename_add_drop_map;
  bool root_dir_attached;
  bool messages;
};


struct simulated_working_tree : public editable_tree
{
  roster_t & workspace;
  node_id_source & nis;

  set<file_path> blocked_paths;
  set<file_path> conflicting_paths;
  int conflicts;
  map<node_id, file_path> nid_map;

  simulated_working_tree(roster_t & r, temp_node_id_source & n)
    : workspace(r), nis(n), conflicts(0) {}

  virtual node_id detach_node(file_path const & src);
  virtual void drop_detached_node(node_id nid);

  virtual node_id create_dir_node();
  virtual node_id create_file_node(file_id const & content);
  virtual void attach_node(node_id nid, file_path const & dst);

  virtual void apply_delta(file_path const & pth,
                           file_id const & old_id,
                           file_id const & new_id);
  virtual void clear_attr(file_path const & path,
                          attr_key const & key);
  virtual void set_attr(file_path const & path,
                        attr_key const & key,
                        attr_value const & val);

  virtual void commit();

  virtual bool has_conflicting_paths() const { return conflicting_paths.size() > 0; }
  virtual set<file_path> get_conflicting_paths() const { return conflicting_paths; }

  virtual ~simulated_working_tree();
};


// editable_working_tree implementation

static inline bookkeeping_path
path_for_detached_nids()
{
  return bookkeeping_root / "detached";
}

static inline bookkeeping_path
path_for_detached_nid(node_id nid)
{
  return path_for_detached_nids() / path_component(lexical_cast<string>(nid),
                                                   origin::internal);
}

// Attaching/detaching the root directory:
//   This is tricky, because we don't want to simply move it around, like
// other directories.  That would require some very snazzy handling of the
// _MTN directory, and never be possible on windows anyway[1].  So, what we do
// is fake it -- whenever we want to move the root directory into the
// temporary dir, we instead create a new dir in the temporary dir, move
// all of the root's contents into this new dir, and make a note that the root
// directory is logically non-existent.  Whenever we want to move some
// directory out of the temporary dir and onto the root directory, we instead
// check that the root is logically nonexistent, move its contents, and note
// that it exists again.
//
// [1] Because the root directory is our working directory, and thus locked in
// place.  We _could_ chdir out, then move _MTN out, then move the real root
// directory into our newly-moved _MTN, etc., but aside from being very finicky,
// this would require that we know our root directory's name relative to its
// parent.

node_id
editable_working_tree::detach_node(file_path const & src_pth)
{
  I(root_dir_attached);
  node_id nid = next_nid++;
  bookkeeping_path dst_pth = path_for_detached_nid(nid);
  safe_insert(rename_add_drop_map, make_pair(dst_pth, src_pth));
  if (src_pth == file_path())
    {
      // root dir detach, so we move contents, rather than the dir itself
      mkdir_p(dst_pth);

      vector<file_path> files, dirs;
      fill_path_vec<file_path> fill_files(src_pth, files, false);
      fill_path_vec<file_path> fill_dirs(src_pth, dirs, true);
      read_directory(src_pth, fill_files, fill_dirs);

      for (vector<file_path>::const_iterator i = files.begin();
           i != files.end(); ++i)
        move_file(*i, dst_pth / (*i).basename());
      for (vector<file_path>::const_iterator i = dirs.begin();
           i != dirs.end(); ++i)
        move_dir(*i, dst_pth / (*i).basename());

      root_dir_attached = false;
    }
  else
    move_path(src_pth, dst_pth);
  return nid;
}

void
editable_working_tree::drop_detached_node(node_id nid)
{
  bookkeeping_path pth = path_for_detached_nid(nid);
  map<bookkeeping_path, file_path>::const_iterator i
    = rename_add_drop_map.find(pth);
  I(i != rename_add_drop_map.end());
  P(F("dropping '%s'") % i->second);
  safe_erase(rename_add_drop_map, pth);
  delete_file_or_dir_shallow(pth);
}

node_id
editable_working_tree::create_dir_node()
{
  node_id nid = next_nid++;
  bookkeeping_path pth = path_for_detached_nid(nid);
  require_path_is_nonexistent(pth,
                              F("path '%s' already exists") % pth);
  mkdir_p(pth);
  return nid;
}

node_id
editable_working_tree::create_file_node(file_id const & content)
{
  node_id nid = next_nid++;
  bookkeeping_path pth = path_for_detached_nid(nid);
  require_path_is_nonexistent(pth,
                              F("path '%s' already exists") % pth);
  write_data(pth, source.get_version(content).inner());
  return nid;
}

void
editable_working_tree::attach_node(node_id nid, file_path const & dst_pth)
{
  bookkeeping_path src_pth = path_for_detached_nid(nid);

  map<bookkeeping_path, file_path>::const_iterator i
    = rename_add_drop_map.find(src_pth);
  if (i != rename_add_drop_map.end())
    {
      if (messages)
        P(F("renaming '%s' to '%s'") % i->second % dst_pth);
      safe_erase(rename_add_drop_map, src_pth);
    }
  else if (messages)
     P(F("adding '%s'") % dst_pth);

  if (dst_pth == file_path())
    {
      // root dir attach, so we move contents, rather than the dir itself
      vector<bookkeeping_path> files, dirs;
      fill_path_vec<bookkeeping_path> fill_files(src_pth, files, false);
      fill_path_vec<bookkeeping_path> fill_dirs(src_pth, dirs, true);
      read_directory(src_pth, fill_files, fill_dirs);

      for (vector<bookkeeping_path>::const_iterator i = files.begin();
           i != files.end(); ++i)
        move_file(*i, dst_pth / (*i).basename());
      for (vector<bookkeeping_path>::const_iterator i = dirs.begin();
           i != dirs.end(); ++i)
        move_dir(*i, dst_pth / (*i).basename());

      delete_dir_shallow(src_pth);
      root_dir_attached = true;
    }
  else
    // This will complain if the move is actually impossible
    move_path(src_pth, dst_pth);
}

void
editable_working_tree::apply_delta(file_path const & pth,
                                   file_id const & old_id,
                                   file_id const & new_id)
{
  require_path_is_file(pth,
                       F("file '%s' does not exist") % pth,
                       F("file '%s' is a directory") % pth);
  E(calculate_ident(pth) == old_id, origin::system,
    F("content of file '%s' has changed, not overwriting") % pth);
  P(F("updating '%s'") % pth);

  write_data(pth, source.get_version(new_id).inner());
}

void
editable_working_tree::clear_attr(file_path const & path,
                                  attr_key const & key)
{
  L(FL("calling hook to clear attribute %s on %s") % key % path);
  lua.hook_clear_attribute(key(), path);
}

void
editable_working_tree::set_attr(file_path const & path,
                                attr_key const & key,
                                attr_value const & value)
{
  L(FL("calling hook to set attribute %s on %s to %s") % key % path % value);
  lua.hook_set_attribute(key(), path, value());
}

void
editable_working_tree::commit()
{
  I(rename_add_drop_map.empty());
  I(root_dir_attached);
}

editable_working_tree::~editable_working_tree()
{
}


node_id
simulated_working_tree::detach_node(file_path const & src)
{
  node_id nid = workspace.detach_node(src);
  nid_map.insert(make_pair(nid, src));
  return nid;
}

void
simulated_working_tree::drop_detached_node(node_id nid)
{
  const_node_t node = workspace.get_node(nid);
  if (is_dir_t(node))
    {
      const_dir_t dir = downcast_to_dir_t(node);
      if (!dir->children.empty())
        {
          map<node_id, file_path>::const_iterator i = nid_map.find(nid);
          I(i != nid_map.end());
          W(F("cannot drop non-empty directory '%s'") % i->second);
          conflicts++;
          for (dir_map::const_iterator j = dir->children.begin();
               j != dir->children.end(); ++j)
            conflicting_paths.insert(i->second / j->first);
        }
    }
}

node_id
simulated_working_tree::create_dir_node()
{
  return workspace.create_dir_node(nis);
}

node_id
simulated_working_tree::create_file_node(file_id const & content)
{
  return workspace.create_file_node(content, nis);
}

void
simulated_working_tree::attach_node(node_id nid, file_path const & dst)
{
  // this check is needed for checkout because we're using a roster to
  // represent paths that *may* block the checkout. however to represent
  // these we *must* have a root node in the roster which will *always*
  // block us. so here we check for that case and avoid it.
  if (dst.empty() && workspace.has_root())
    return;

  if (workspace.has_node(dst))
    {
      W(F("attach node %d blocked by unversioned path '%s'") % nid % dst);
      blocked_paths.insert(dst);
      conflicting_paths.insert(dst);
      conflicts++;
    }
  else if (dst.empty())
    {
      // the parent of the workspace root cannot be in the blocked set
      // this attach would have been caught above if it were a problem
      workspace.attach_node(nid, dst);
    }
  else
    {
      file_path parent = dst.dirname();

      if (blocked_paths.find(parent) == blocked_paths.end())
        workspace.attach_node(nid, dst);
      else
        {
          W(F("attach node %d blocked by blocked parent '%s'")
            % nid % parent);
          blocked_paths.insert(dst);
        }
    }
}

void
simulated_working_tree::apply_delta(file_path const & /* path */,
                                    file_id const & /* old_id */,
                                    file_id const & /* new_id */)
{
  // this may fail if path is not a file but that will be caught
  // earlier in update_current_roster_from_filesystem
}

void
simulated_working_tree::clear_attr(file_path const & /* path */,
                                   attr_key const & /* key */)
{
}

void
simulated_working_tree::set_attr(file_path const & /* path */,
                                 attr_key const & /* key */,
                                 attr_value const & /* val */)
{
}

void
simulated_working_tree::commit()
{
  // This used to error out on any conflicts, but now some can be resolved
  // (by --move-conflicting-paths), so we just warn. The non-resolved
  // conflicts generate other errors downstream.
  if (conflicts > 0)
    F("%d workspace conflicts") % conflicts;
}

simulated_working_tree::~simulated_working_tree()
{
}


}; // anonymous namespace

static void
move_conflicting_paths_into_bookkeeping(set<file_path> const & leftover_paths)
{
  I(leftover_paths.size() > 0);

  // There is some concern that this fixed bookkeeping path will cause
  // problems, if a user forgets to clean up, and then does something that
  // involves the same name again. However, I can't think of a reasonable
  // use case that does that, so I can't think of a reasonable solution. One
  // solution is to generate a random directory name, another is to use the
  // current time in some format to generate a directory name.
  //
  // now().as_iso_8601_extended doesn't work on Windows, because it has
  // colons in it.
  //
  // Random or time based directory names significantly complicate testing,
  // since you can't predict the directory name.
  //
  // If this turns out to be a problem, a modification of
  // now().as_iso_8601_extended to eliminate the colons, or some appropriate
  // format for now().as_formatted_localtime would be simple and
  // probably adequate.
  bookkeeping_path leftover_path = bookkeeping_resolutions_dir;

  mkdir_p(leftover_path);

  for (set<file_path>::const_iterator i = leftover_paths.begin();
        i != leftover_paths.end(); ++i)
    {
      L(FL("processing %s") % *i);

      file_path basedir = (*i).dirname();
      if (!basedir.empty())
        mkdir_p(leftover_path / basedir);

      bookkeeping_path new_path = leftover_path / *i;
      if (directory_exists(*i))
        move_dir(*i, new_path);
      else if (file_exists(*i))
        move_file(*i, new_path);
      else
        I(false);

      P(F("moved conflicting path '%s' to '%s'") % *i % new_path);
    }
}

static void
add_parent_dirs(database & db, node_id_source & nis, workspace & work,
                file_path const & dst, roster_t & ros)
{
  editable_roster_base er(ros, nis);
  addition_builder build(db, work, ros, er, false, true);

  // FIXME: this is a somewhat odd way to use the builder
  build.visit_dir(dst.dirname());
}

// updating rosters from the workspace

void
workspace::update_current_roster_from_filesystem(roster_t & ros)
{
  update_current_roster_from_filesystem(ros, node_restriction());
}

void
workspace::update_current_roster_from_filesystem(roster_t & ros,
                                                 node_restriction const & mask)
{
  temp_node_id_source nis;
  inodeprint_map ipm;

  if (in_inodeprints_mode())
    read_inodeprint_map(read_inodeprints(), ipm);

  size_t missing_items = 0;

  // this code is speed critical, hence the use of inode fingerprints so be
  // careful when making changes in here and preferably do some timing tests

  if (!ros.has_root())
    return;

  node_map const & nodes = ros.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;
      node_t node = i->second;

      // Only analyze restriction-included files and dirs
      if (!mask.includes(ros, nid))
        continue;

      file_path fp;
      ros.get_name(nid, fp);

      const path::status status(get_path_status(fp));

      if (is_dir_t(node))
        {
          if (status == path::nonexistent)
            {
              W(F("missing directory '%s'") % (fp));
              missing_items++;
            }
          else if (status != path::directory)
            {
              W(F("not a directory '%s'") % (fp));
              missing_items++;
            }
        }
      else
        {
          // Only analyze changed files (or all files if inodeprints mode
          // is disabled).
          if (inodeprint_unchanged(ipm, fp))
            continue;

          if (status == path::nonexistent)
            {
              W(F("missing file '%s'") % (fp));
              missing_items++;
            }
          else if (status != path::file)
            {
              W(F("not a file '%s'") % (fp));
              missing_items++;
            }

          file_id fid;
          ident_existing_file(fp, fid, status);
          file_t file = downcast_to_file_t(node);
          if (file->content != fid)
            {
              ros.unshare(node);
              downcast_to_file_t(node)->content = fid;
            }
        }

    }

  E(missing_items == 0, origin::user,
    F("%d missing items; use '%s ls missing' to view.\n"
      "To restore consistency, on each missing item run either\n"
      " '%s drop ITEM' to remove it permanently, or\n"
      " '%s revert ITEM' to restore it.\n"
      "To handle all at once, simply use\n"
      " '%s drop --missing' or\n"
      " '%s revert --missing'")
    % missing_items % prog_name % prog_name % prog_name
    % prog_name % prog_name);
}

set<file_path>
workspace::find_missing(roster_t const & new_roster_shape,
                        node_restriction const & mask)
{
  set<file_path> missing;
  node_map const & nodes = new_roster_shape.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;

      if (!new_roster_shape.is_root(nid)
          && mask.includes(new_roster_shape, nid))
        {
          file_path fp;
          new_roster_shape.get_name(nid, fp);
          if (!path_exists(fp))
            missing.insert(fp);
        }
    }
  return missing;
}

void
workspace::find_unknown_and_ignored(database & db,
                                    path_restriction const & mask,
                                    bool recurse,
                                    vector<file_path> const & roots,
                                    set<file_path> & unknown,
                                    set<file_path> & ignored)
{
  set<file_path> known;
  roster_t new_roster = get_current_roster_shape(db);
  new_roster.extract_path_set(known);

  file_itemizer u(db, *this, known, unknown, ignored, mask, recurse);
  for (vector<file_path>::const_iterator
         i = roots.begin(); i != roots.end(); ++i)
    {
      walk_tree(*i, u);
    }
}

void
workspace::perform_additions(database & db, set<file_path> const & paths,
                             bool recursive, bool respect_ignore)
{
  if (paths.empty())
    return;

  temp_node_id_source nis;
  roster_t new_roster = get_current_roster_shape(db, nis);
  MM(new_roster);

  editable_roster_base er(new_roster, nis);

  if (!new_roster.has_root())
    {
      er.attach_node(er.create_dir_node(), file_path_internal(""));
    }

  I(new_roster.has_root());
  addition_builder build(db, *this, new_roster, er, respect_ignore, recursive);

  for (set<file_path>::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      if (recursive)
        {
          // NB.: walk_tree will handle error checking for non-existent paths
          walk_tree(*i, build);
        }
      else
        {
          // in the case where we're just handed a set of paths, we use the
          // builder in this strange way.
          switch (get_path_status(*i))
            {
            case path::nonexistent:
              E(false, origin::user,
                F("no such file or directory: '%s'") % *i);
              break;
            case path::file:
              build.visit_file(*i);
              break;
            case path::directory:
              build.visit_dir(*i);
              break;
            }
        }
    }

  parent_map parents = get_parent_rosters(db);
  put_work_rev(make_revision_for_workspace(parents, new_roster));
}

static bool
in_parent_roster(const parent_map & parents, const node_id & nid)
{
  for (parent_map::const_iterator i = parents.begin();
       i != parents.end();
       i++)
    {
      if (parent_roster(i).has_node(nid))
        return true;
    }

  return false;
}

void
workspace::perform_deletions(database & db,
                             set<file_path> const & paths,
                             bool recursive, bool bookkeep_only)
{
  if (paths.empty())
    return;

  roster_t new_roster = get_current_roster_shape(db);
  MM(new_roster);

  parent_map parents = get_parent_rosters(db);

  // we traverse the the paths backwards, so that we always hit deep paths
  // before shallow paths (because set<file_path> is lexicographically
  // sorted).  this is important in cases like
  //    monotone drop foo/bar foo foo/baz
  // where, when processing 'foo', we need to know whether or not it is empty
  // (and thus legal to remove)

  deque<file_path> todo;
  set<file_path>::const_reverse_iterator i = paths.rbegin();
  todo.push_back(*i);
  ++i;

  while (todo.size())
    {
      file_path const & name(todo.front());

      E(!name.empty(), origin::user,
        F("unable to drop the root directory"));

      if (!new_roster.has_node(name))
        P(F("skipping '%s', not currently tracked") % name);
      else
        {
          const_node_t n = new_roster.get_node(name);
          if (is_dir_t(n))
            {
              const_dir_t d = downcast_to_dir_t(n);
              if (!d->children.empty())
                {
                  E(recursive, origin::user,
                    F("cannot remove '%s/', it is not empty") % name);
                  for (dir_map::const_iterator j = d->children.begin();
                       j != d->children.end(); ++j)
                    todo.push_front(name / j->first);
                  continue;
                }
            }
          if (!bookkeep_only && path_exists(name)
              && in_parent_roster(parents, n->self))
            {
              if (is_dir_t(n))
                {
                  if (directory_empty(name))
                    delete_file_or_dir_shallow(name);
                  else
                    W(F("directory '%s' not empty - "
                        "it will be dropped but not deleted") % name);
                }
              else
                {
                  const_file_t file = downcast_to_file_t(n);
                  file_id fid;
                  I(ident_existing_file(name, fid));
                  if (file->content == fid)
                    delete_file_or_dir_shallow(name);
                  else
                    W(F("file '%s' changed - "
                        "it will be dropped but not deleted") % name);
                }
            }
          P(F("dropping '%s' from workspace manifest") % name);
          new_roster.drop_detached_node(new_roster.detach_node(name));
        }
      todo.pop_front();
      if (i != paths.rend())
        {
          todo.push_back(*i);
          ++i;
        }
    }

  put_work_rev(make_revision_for_workspace(parents, new_roster));
}

void
workspace::perform_rename(database & db,
                          set<file_path> const & srcs,
                          file_path const & dst,
                          bool bookkeep_only)
{
  temp_node_id_source nis;
  set< pair<file_path, file_path> > renames;
  roster_t new_roster = get_current_roster_shape(db, nis);
  MM(new_roster);

  I(!srcs.empty());

  // validation.  it's okay if the target exists as a file; we just won't
  // clobber it (in !--bookkeep-only mode).  similarly, it's okay if the
  // source does not exist as a file.
  if (srcs.size() == 1 && !new_roster.has_node(dst))
    {
      // "rename SRC DST", DST is a file
      file_path const & src = *srcs.begin();
      file_path dpath = dst;

      E(!src.empty(), origin::user,
        F("cannot rename the workspace root (try '%s pivot_root' instead)")
        % prog_name);
      E(new_roster.has_node(src), origin::user,
        F("source file '%s' is not versioned") % src);

      if (src == dst || dst.is_beneath_of(src))
        {
          if (get_path_status(dst) == path::directory)
            W(F("cannot move '%s' to a subdirectory of itself, '%s/%s'") % src % dst % src);
          else
            W(F("'%s' and '%s' are the same file") % src % dst);
        }
      else
        {
          //this allows the 'magic add' of a non-versioned directory to happen in
          //all cases.  previously, mtn mv fileA dir/ woudl fail if dir/ wasn't
          //versioned whereas mtn mv fileA dir/fileA would add dir/ if necessary
          //and then reparent fileA.
          //
          //Note that we checked above that dst is not a directory

          //this handles the case where:
          // touch foo
          // mtn mv foo bar/foo where bar doesn't exist
          file_path parent = dst.dirname();
          E(get_path_status(parent) == path::directory, origin::user,
            F("destination path's parent directory '%s/' doesn't exist") % parent);

          renames.insert(make_pair(src, dpath));
          add_parent_dirs(db, nis, *this, dpath, new_roster);
        }
    }
  else
    {
      // Either srcs has more than one element, or dst is an existing
      // directory (or both). So we have one of:
      //
      // 1) rename SRC1 [SRC2 ...] DSTDIR
      //
      // 2) mv foo bar
      //    mtn mv --bookkeep-only foo bar

      E(get_path_status(dst) == path::directory, origin::user,
        F("destination '%s/' is not a directory") % dst);

      for (set<file_path>::const_iterator i = srcs.begin();
           i != srcs.end(); i++)
        {
          E(!i->empty(), origin::user,
            F("cannot rename the workspace root (try '%s pivot_root' instead)")
            % prog_name);
          E(new_roster.has_node(*i), origin::user,
            F("source file '%s' is not versioned") % *i);

          file_path d = dst / i->basename();
          if (bookkeep_only &&
              srcs.size() == 1 &&
              get_path_status(*srcs.begin()) == path::directory &&
              get_path_status(dst) == path::directory)
            {
              // case 2)
              d = dst;
            }
          else
            {
              // case 1)
              d = dst / i->basename();

              E(!new_roster.has_node(d), origin::user,
                F("destination '%s' already exists in the workspace manifest") % d);
            }

          if (*i == dst || dst.is_beneath_of(*i))
            {
              W(F("cannot move '%s' to a subdirectory of itself, '%s/%s'")
                % *i % dst % *i);
            }
          else
            {
              renames.insert(make_pair(*i, d));

              add_parent_dirs(db, nis, *this, d, new_roster);
            }
        }
    }

  // do the attach/detaching
  for (set< pair<file_path, file_path> >::const_iterator i = renames.begin();
       i != renames.end(); i++)
    {
      node_id nid = new_roster.detach_node(i->first);
      new_roster.attach_node(nid, i->second);
      P(F("renaming '%s' to '%s' in workspace manifest") % i->first % i->second);
    }

  parent_map parents = get_parent_rosters(db);
  put_work_rev(make_revision_for_workspace(parents, new_roster));

  if (!bookkeep_only)
    for (set< pair<file_path, file_path> >::const_iterator i = renames.begin();
         i != renames.end(); i++)
      {
        file_path const & s(i->first);
        file_path const & d(i->second);
        // silently skip files where src doesn't exist or dst does
        bool have_src = path_exists(s);
        bool have_dst = path_exists(d);
        if (have_src && !have_dst)
          {
            move_path(s, d);
          }
        else if (!have_src && !have_dst)
          {
            W(F("'%s' doesn't exist in workspace, skipping") % s);
          }
        else if (have_src && have_dst)
          {
            W(F("destination '%s' already exists in workspace, "
                "skipping filesystem rename") % d);
          }
        else
          {
            W(F("'%s' doesn't exist in workspace and '%s' does, "
                "skipping filesystem rename") % s % d);
          }
      }
}

void
workspace::perform_pivot_root(database & db,
                              file_path const & new_root,
                              file_path const & put_old,
                              bool bookkeep_only,
                              bool move_conflicting_paths)
{
  temp_node_id_source nis;
  roster_t old_roster = get_current_roster_shape(db, nis);
  MM(old_roster);

  roster_t new_roster;
  MM(new_roster);

  I(old_roster.has_root());
  E(old_roster.has_node(new_root), origin::user,
    F("proposed new root directory '%s' is not versioned or does not exist")
    % new_root);
  E(is_dir_t(old_roster.get_node(new_root)), origin::user,
    F("proposed new root directory '%s' is not a directory") % new_root);
  {
    E(!old_roster.has_node(new_root / bookkeeping_root_component), origin::user,
      F("proposed new root directory '%s' contains illegal path '%s'")
      % new_root % bookkeeping_root);
  }

  {
    file_path current_path_to_put_old = (new_root / put_old);
    file_path current_path_to_put_old_parent
      = current_path_to_put_old.dirname();

    E(old_roster.has_node(current_path_to_put_old_parent), origin::user,
      F("directory '%s' is not versioned or does not exist")
      % current_path_to_put_old_parent);
    E(is_dir_t(old_roster.get_node(current_path_to_put_old_parent)),
      origin::user,
      F("'%s' is not a directory")
      % current_path_to_put_old_parent);
    E(!old_roster.has_node(current_path_to_put_old),
      origin::user,
      F("'%s' is in the way") % current_path_to_put_old);
  }

  cset cs;
  safe_insert(cs.nodes_renamed, make_pair(file_path_internal(""), put_old));
  safe_insert(cs.nodes_renamed, make_pair(new_root, file_path_internal("")));

  {
    new_roster = old_roster;
    editable_roster_base e(new_roster, nis);
    cs.apply_to(e);
  }

  put_work_rev(make_revision_for_workspace(get_parent_rosters(db),
                                           new_roster));

  if (!bookkeep_only)
    {
      content_merge_empty_adaptor cmea;
      perform_content_update(old_roster, new_roster, cs, cmea, true,
                             move_conflicting_paths);
    }
}

void
workspace::perform_content_update(roster_t const & old_roster,
                                  roster_t const & new_roster,
                                  cset const & update,
                                  content_merge_adaptor const & ca,
                                  bool const messages,
                                  bool const move_conflicting_paths)
{
  roster_t test_roster;
  temp_node_id_source nis;
  set<file_path> known;
  bookkeeping_path detached = path_for_detached_nids();
  bool moved_conflicting = false;

  E(!directory_exists(detached), origin::user,
    F("workspace is locked\n"
      "you must clean up and remove the %s directory")
    % detached);

  old_roster.extract_path_set(known);

  workspace_itemizer itemizer(test_roster, known, nis);
  walk_tree(file_path(), itemizer);

  simulated_working_tree swt(test_roster, nis);
  update.apply_to(swt);

  // if we have found paths during the test-run which will conflict with
  // newly attached or to-be-dropped nodes, move these paths out of the way
  // into _MTN while keeping the path to these paths intact in case the user
  // wants them back
  if (swt.has_conflicting_paths())
    {
      E(move_conflicting_paths, origin::user,
        F("re-run this command with '--move-conflicting-paths' to move "
          "conflicting paths out of the way"));
      move_conflicting_paths_into_bookkeeping(swt.get_conflicting_paths());
      moved_conflicting = true;
    }

  mkdir_p(detached);

  editable_working_tree ewt(*this, this->lua, ca, messages);
  update.apply_to(ewt);

  // attributes on updated files must be reset because apply_delta writes
  // new versions of files to _MTN/tmp and then renames them over top of the
  // old versions and doesn't reset attributes (mtn:execute).

  for (map<file_path, pair<file_id, file_id> >::const_iterator
         i = update.deltas_applied.begin(); i != update.deltas_applied.end();
       ++i)
    {
      const_node_t node = new_roster.get_node(i->first);
      for (attr_map_t::const_iterator a = node->attrs.begin();
           a != node->attrs.end(); ++a)
        {
          if (a->second.first)
            this->lua.hook_set_attribute(a->first(), i->first,
                                         a->second.second());
        }
    }

  delete_dir_shallow(detached);

  if (moved_conflicting)
    P(F("moved some conflicting files into '%s'") % bookkeeping_resolutions_dir);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
