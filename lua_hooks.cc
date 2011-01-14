// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "base.hh"

#include <set>
#include <map>
#include <fstream>
#include <iostream>

#include "lua.hh"

#include "app_state.hh"
#include "file_io.hh"
#include "lua_hooks.hh"
#include "sanity.hh"
#include "vocab.hh"
#include "transforms.hh"
#include "paths.hh"
#include "project.hh"
#include "uri.hh"
#include "cmd.hh"
#include "commands.hh"
#include "globish.hh"
#include "simplestring_xform.hh"

// defined in std_hooks.c, generated from std_hooks.lua
extern char const std_hooks_constant[];

using std::make_pair;
using std::sort;

static int panic_thrower(lua_State * st)
{
  throw oops("lua panic");
}

// this lets the lua callbacks (monotone_*_for_lua) have access to the
// app_state the're associated with.
// it was added so that the confdir (normally ~/.monotone) can be specified on
// the command line (and so known only to the app_state), and still be
// available to lua
// please *don't* use it for complex things that can throw errors
static map<lua_State*, app_state*> map_of_lua_to_app;

extern "C"
{
  static int
  monotone_get_confdir_for_lua(lua_State *LS)
  {
    map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(LS);
    if (i != map_of_lua_to_app.end())
      {
        if (i->second->opts.conf_dir_given
            || !i->second->opts.no_default_confdir)
          {
            system_path dir = i->second->opts.conf_dir;
            string confdir = dir.as_external();
            lua_pushstring(LS, confdir.c_str());
          }
        else
          lua_pushnil(LS);
      }
    else
      lua_pushnil(LS);
    return 1;
  }
  // taken from http://medek.wordpress.com/2009/02/03/wrapping-lua-errors-and-print-function/
  static int
  monotone_message(lua_State *LS)
  {
    int nArgs = lua_gettop(LS);
    lua_getglobal(LS, "tostring");

    string ret;
    for (int i = 1; i <= nArgs; ++i)
      {
        const char *s;
        lua_pushvalue(LS, -1);
        lua_pushvalue(LS, i);
        lua_call(LS, 1, 1);
        s = lua_tostring(LS, -1);
        if (s == NULL)
          return luaL_error(
            LS, LUA_QL("tostring") " must return a string to ", LUA_QL("print")
          );

        if (i > 1)
          ret.append("\t");

        ret.append(s);
        lua_pop(LS, 1);
      }

    string prefixed;
    prefix_lines_with(_("lua: "), ret, prefixed);
    P(F("%s") % prefixed);
    return 0;
  }
}

app_state*
get_app_state(lua_State *LS)
{
  map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(LS);
  if (i != map_of_lua_to_app.end())
    return i->second;
  else
    return NULL;
}

namespace {
  Lua & push_key_identity_info(Lua & ll,
                               key_identity_info const & info)
  {
    hexenc<id> hexid;
    encode_hexenc(info.id.inner(), hexid);
    ll.push_table()
      .push_str(hexid())
      .set_field("id")
      .push_str(info.given_name())
      .set_field("given_name")
      .push_str(info.official_name())
      .set_field("name");
    return ll;
  }
}

lua_hooks::lua_hooks(app_state * app)
{
  st = luaL_newstate();
  I(st);

  lua_atpanic (st, &panic_thrower);

  luaL_openlibs(st);

  lua_register(st, "get_confdir", monotone_get_confdir_for_lua);
  lua_register(st, "message", monotone_message);
  add_functions(st);

  // Disable any functions we don't want. This is easiest
  // to do just by running a lua string.
  static char const disable_dangerous[] =
    "os.execute = function(c) "
    " error(\"os.execute disabled for security reasons.  Try spawn().\") "
    "end "
    "io.popen = function(c,t) "
    " error(\"io.popen disabled for security reasons.  Try spawn_pipe().\") "
    "end ";

    if (!run_string(st, disable_dangerous,
                    "<disabled dangerous functions>"))
    throw oops("lua error while disabling existing functions");

  // redirect output to internal message handler which calls into
  // our user interface code. Note that we send _everything_ to stderr
  // or as out-of-band progress stream to keep our stdout clean
  static char const redirect_output[] =
    "io.write = function(...) "
    "  message(...) "
    "end "
    "print = function(...) "
    "  message(...) "
    "end ";

    if (!run_string(st, redirect_output,
                    "<redirect output>"))
    throw oops("lua error while redirecting output");

  map_of_lua_to_app.insert(make_pair(st, app));
}

lua_hooks::~lua_hooks()
{
  map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(st);
  if (st)
    lua_close (st);
  if (i != map_of_lua_to_app.end())
    map_of_lua_to_app.erase(i);
}

bool
lua_hooks::check_lua_state(lua_State * p_st) const
{
  return (p_st == st);
}

void
lua_hooks::add_std_hooks()
{
  if (!run_string(st, std_hooks_constant, "<std hooks>"))
    throw oops("lua error while setting up standard hooks");
}

void
lua_hooks::load_rcfile(utf8 const & rc)
{
  I(st);
  if (rc() != "-" && directory_exists(system_path(rc)))
    run_directory(st, system_path(rc).as_external().c_str(), "*");
  else
    {
      data dat;
      L(FL("opening rcfile '%s'") % rc);
      read_data_for_command_line(rc, dat);
      E(run_string(st, dat().c_str(), rc().c_str()), origin::user,
        F("lua error while loading rcfile '%s'") % rc);
      L(FL("'%s' is ok") % rc);
    }
}

void
lua_hooks::load_rcfile(any_path const & rc, bool required)
{
  I(st);
  bool exists;
  try
    {
      exists = path_exists(rc);
    }
  catch (recoverable_failure & e)
    {
      if (!required)
        {
          L(FL("skipping rcfile '%s': %s") % rc % e.what());
          return;
        }
      else
        throw;
    }

  if (exists)
    {
      L(FL("opening rcfile '%s'") % rc);
      E(run_file(st, rc.as_external().c_str()), origin::user,
        F("lua error while loading '%s'") % rc);
      L(FL("'%s' is ok") % rc);
    }
  else
    {
      E(!required, origin::user, F("rcfile '%s' does not exist") % rc);
      L(FL("skipping nonexistent rcfile '%s'") % rc);
    }
}

void
lua_hooks::load_rcfiles(options const & opts)
{
  // Built-in rc settings are defaults.
  if (!opts.nostd)
    add_std_hooks();

  // ~/.monotone/monotonerc overrides that, and
  // _MTN/monotonerc overrides *that*.

  if (!opts.norc)
    {
      if (opts.conf_dir_given || !opts.no_default_confdir)
        {
          load_rcfile(opts.conf_dir / "monotonerc", false);
        }
      load_rcfile(bookkeeping_root / "monotonerc", false);
    }

  // Command-line rcfiles override even that.

  for (args_vector::const_iterator i = opts.extra_rcfiles.begin();
       i != opts.extra_rcfiles.end(); ++i)
    load_rcfile(*i);
}

bool
lua_hooks::hook_exists(string const & func_name)
{
  return Lua(st)
    .func(func_name)
    .ok();
}

// concrete hooks

// nb: if you're hooking lua to return your passphrase, you don't care if we
// keep a couple extra temporaries of your passphrase around.
bool
lua_hooks::hook_get_passphrase(key_identity_info const & identity,
                               string & phrase)
{
  Lua ll(st);
  ll.func("get_passphrase");
  push_key_identity_info(ll, identity);
  ll.call(1,1)
    .extract_classified_str(phrase);
  return ll.ok();
}

bool
lua_hooks::hook_get_local_key_name(key_identity_info & info)
{
  string local_name;
  Lua ll(st);
  ll.func("get_local_key_name");
  push_key_identity_info(ll, info);
  ll.call(1, 1)
    .extract_str(local_name);
  if (ll.ok())
    {
      info.official_name = key_name(local_name, origin::user);
      return true;
    }
  else
    return false;
}

bool
lua_hooks::hook_persist_phrase_ok()
{
  bool persist_ok = false;
  bool executed_ok = Lua(st)
    .func("persist_phrase_ok")
    .call(0,1)
    .extract_bool(persist_ok)
    .ok();
  return executed_ok && persist_ok;
}

bool
lua_hooks::hook_expand_selector(string const & sel,
                                string & exp)
{
  return Lua(st)
    .func("expand_selector")
    .push_str(sel)
    .call(1,1)
    .extract_str(exp)
    .ok();
}

bool
lua_hooks::hook_expand_date(string const & sel,
                            string & exp)
{
  exp.clear();
  bool res= Lua(st)
    .func("expand_date")
    .push_str(sel)
    .call(1,1)
    .extract_str(exp)
    .ok();
  return res && exp.size();
}

bool
lua_hooks::hook_get_branch_key(branch_name const & branchname,
                               key_store & keys,
                               project_t & project,
                               key_id & k)
{
  string key;
  bool ok = Lua(st)
    .func("get_branch_key")
    .push_str(branchname())
    .call(1,1)
    .extract_str(key)
    .ok();

  if (!ok || key.empty())
    return false;
  else
    {
      key_identity_info identity;
      project.get_key_identity(keys, *this, external_key_name(key, origin::user), identity);
      k = identity.id;
      return true;
    }
}

bool
lua_hooks::hook_get_author(branch_name const & branchname,
                           key_identity_info const & identity,
                           string & author)
{
  Lua ll(st);
  ll.func("get_author")
    .push_str(branchname());
  push_key_identity_info(ll, identity);
  return ll.call(2,1)
    .extract_str(author)
    .ok();
}

bool
lua_hooks::hook_edit_comment(external const & user_log_message,
                             external & result)
{
  string result_str;
  bool is_ok = Lua(st)
                 .func("edit_comment")
                 .push_str(user_log_message())
                 .call(1,1)
                 .extract_str(result_str)
                 .ok();
  result = external(result_str, origin::user);
  return is_ok;
}

bool
lua_hooks::hook_ignore_file(file_path const & p)
{
  bool ignore_it = false;
  bool exec_ok = Lua(st)
    .func("ignore_file")
    .push_str(p.as_external())
    .call(1,1)
    .extract_bool(ignore_it)
    .ok();
  return exec_ok && ignore_it;
}

bool
lua_hooks::hook_ignore_branch(branch_name const & branch)
{
  bool ignore_it = false;
  bool exec_ok = Lua(st)
    .func("ignore_branch")
    .push_str(branch())
    .call(1,1)
    .extract_bool(ignore_it)
    .ok();
  return exec_ok && ignore_it;
}

namespace {
  template<typename ID>
  Lua & push_key_ident(Lua & ll, ID const & ident)
  {
    enum dummp { d = ( sizeof(struct not_a_key_id_type) == sizeof(ID))};
    return ll;
  }
  template<>
  Lua & push_key_ident(Lua & ll, key_identity_info const & ident)
  {
    return push_key_identity_info(ll, ident);
  }
  template<>
  Lua & push_key_ident(Lua & ll, key_name const & ident)
  {
    return ll.push_str(ident());
  }
  template<typename ID>
  bool
  shared_trust_function_body(Lua & ll,
                             std::set<ID> const & signers,
                             id const & hash,
                             cert_name const & name,
                             cert_value const & val)
  {
    ll.push_table();

    int k = 1;
    for (typename std::set<ID>::const_iterator v = signers.begin();
         v != signers.end(); ++v)
      {
        ll.push_int(k);
        push_key_ident(ll, *v);
        ll.set_table();
        ++k;
      }

    hexenc<id> hid(encode_hexenc(hash(), hash.made_from), hash.made_from);
    bool ok;
    bool exec_ok = ll
      .push_str(hid())
      .push_str(name())
      .push_str(val())
      .call(4, 1)
      .extract_bool(ok)
      .ok();

    return exec_ok && ok;
  }
}

bool
lua_hooks::hook_get_revision_cert_trust(std::set<key_identity_info> const & signers,
                                       id const & hash,
                                       cert_name const & name,
                                       cert_value const & val)
{
  Lua ll(st);
  ll.func("get_revision_cert_trust");
  return shared_trust_function_body(ll, signers, hash, name, val);
}

bool
lua_hooks::hook_get_manifest_cert_trust(std::set<key_name> const & signers,
                                        id const & hash,
                                        cert_name const & name,
                                        cert_value const & val)
{
  Lua ll(st);
  ll.func("get_manifest_cert_trust");
  return shared_trust_function_body(ll, signers, hash, name, val);
}

bool
lua_hooks::hook_accept_testresult_change(map<key_id, bool> const & old_results,
                                         map<key_id, bool> const & new_results)
{
  Lua ll(st);
  ll
    .func("accept_testresult_change")
    .push_table();

  for (map<key_id, bool>::const_iterator i = old_results.begin();
       i != old_results.end(); ++i)
    {
      ll.push_str(i->first.inner()());
      ll.push_bool(i->second);
      ll.set_table();
    }

  ll.push_table();

  for (map<key_id, bool>::const_iterator i = new_results.begin();
       i != new_results.end(); ++i)
    {
      ll.push_str(i->first.inner()());
      ll.push_bool(i->second);
      ll.set_table();
    }

  bool ok;
  bool exec_ok = ll
    .call(2, 1)
    .extract_bool(ok)
    .ok();

  return exec_ok && ok;
}



bool
lua_hooks::hook_merge3(file_path const & anc_path,
                       file_path const & left_path,
                       file_path const & right_path,
                       file_path const & merged_path,
                       data const & ancestor,
                       data const & left,
                       data const & right,
                       data & result)
{
  string res;
  bool ok = Lua(st)
    .func("merge3")
    .push_str(anc_path.as_external())
    .push_str(left_path.as_external())
    .push_str(right_path.as_external())
    .push_str(merged_path.as_external())
    .push_str(ancestor())
    .push_str(left())
    .push_str(right())
    .call(7,1)
    .extract_str(res)
    .ok();
  result = data(res, origin::user);
  return ok;
}

bool
lua_hooks::hook_external_diff(file_path const & path,
                              data const & data_old,
                              data const & data_new,
                              bool is_binary,
                              bool diff_args_provided,
                              string const & diff_args,
                              string const & oldrev,
                              string const & newrev)
{
  Lua ll(st);

  ll
    .func("external_diff")
    .push_str(path.as_external());

  if (oldrev.length() != 0)
    ll.push_str(data_old());
  else
    ll.push_nil();

  ll.push_str(data_new());

  ll.push_bool(is_binary);

  if (diff_args_provided)
    ll.push_str(diff_args);
  else
    ll.push_nil();

  ll.push_str(oldrev);
  ll.push_str(newrev);

  return ll.call(7,0).ok();
}

bool
lua_hooks::hook_get_encloser_pattern(file_path const & path,
                                     string & pattern)
{
  bool exec_ok
    = Lua(st)
    .func("get_encloser_pattern")
    .push_str(path.as_external())
    .call(1, 1)
    .extract_str(pattern)
    .ok();

  // If the hook fails, make sure pattern is set to something sane
  // (the empty string, which will disable enclosers for this file).
  if (!exec_ok)
    pattern = "";
  return exec_ok;
}

bool
lua_hooks::hook_get_default_command_options(commands::command_id const & cmd,
                                            args_vector & args)
{
  Lua ll(st);
  ll.func("get_default_command_options");

  ll.push_table();
  int k = 1;

  // skip the first ID part, the command group, since this is mostly
  // useless for the hook implementor
  vector<utf8>::const_iterator i = cmd.begin();
  i++;

  for ( ; i != cmd.end(); ++i)
    {
      ll.push_int(k);
      ll.push_str((*i)());
      ll.set_table();
      k++;
    }

  ll.call(1, 1);

  ll.begin();
  while (ll.next())
    {
      string arg;
      ll.extract_str(arg).pop();
      args.push_back(arg_type(arg, origin::user));
    }
  return ll.ok() && !args.empty();
}

bool
lua_hooks::hook_get_date_format_spec(date_format_spec in, string & out)
{
  string in_spec;
  switch (in)
  {
    case date_long:         in_spec = "date_long"; break;
    case date_short:        in_spec = "date_short"; break;
    case time_long:         in_spec = "time_long"; break;
    case time_short:        in_spec = "time_short"; break;
    case date_time_long:    in_spec = "date_time_long"; break;
    case date_time_short:   in_spec = "date_time_short"; break;
    default: I(false);
  }

  bool exec_ok
    = Lua(st)
    .func("get_date_format_spec")
    .push_str(in_spec)
    .call(1, 1)
    .extract_str(out)
    .ok();

  // If the hook fails, disable date formatting.
  if (!exec_ok)
    out = "";
  return exec_ok;
}

bool lua_hooks::hook_get_default_database_alias(string & alias)
{
   bool exec_ok
     = Lua(st)
     .func("get_default_database_alias")
     .call(0, 1)
     .extract_str(alias)
     .ok();

  return exec_ok;
}

bool lua_hooks::hook_get_default_database_locations(vector<system_path> & out)
{
  Lua ll(st);
  ll.func("get_default_database_locations");
  ll.call(0, 1);

  ll.begin();
  while (ll.next())
    {
      string path;
      ll.extract_str(path).pop();
      out.push_back(system_path(path, origin::user));
    }
  return ll.ok();
}

bool lua_hooks::hook_hook_wrapper(string const & func_name,
                                  vector<string> const & args,
                                  string & out)
{
  Lua ll(st);
  ll.func("hook_wrapper")
    .push_str(func_name);

  for (vector<string>::const_iterator i = args.begin();
        i != args.end(); ++i)
    {
      ll.push_str(*i);
    }

  ll.call(args.size() + 1, 1);
  ll.extract_str_nolog(out);
  return ll.ok();
}

bool
lua_hooks::hook_get_man_page_formatter_command(string & command)
{
  bool exec_ok
     = Lua(st)
     .func("get_man_page_formatter_command")
     .call(0, 1)
     .extract_str(command)
     .ok();

  return exec_ok;
}

bool
lua_hooks::hook_get_output_color(string const purpose, string & color)
{
  Lua ll = Lua(st);

  return ll.func("get_output_color")
    .push_str(purpose)
    .call(1, 1)
    .extract_str(color)
    .ok();
}

bool
lua_hooks::hook_use_inodeprints()
{
  bool use = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("use_inodeprints")
    .call(0, 1)
    .extract_bool(use)
    .ok();
  return use && exec_ok;
}

bool
lua_hooks::hook_get_netsync_key(utf8 const & server_address,
                                globish const & include,
                                globish const & exclude,
                                key_store & keys,
                                project_t & project,
                                key_id & k)
{
  string name;
  bool exec_ok
    = Lua(st)
    .func("get_netsync_key")
    .push_str(server_address())
    .push_str(include())
    .push_str(exclude())
    .call(3, 1)
    .extract_str(name)
    .ok();

  if (!exec_ok || name.empty())
    return false;
  else
    {
      key_identity_info identity;
      project.get_key_identity(keys, *this, external_key_name(name, origin::user), identity);
      k = identity.id;
      return true;
    }
}

static void
push_uri(uri_t const & uri, Lua & ll)
{
  ll.push_table();

  if (!uri.scheme.empty())
    {
      ll.push_str("scheme");
      ll.push_str(uri.scheme);
      ll.set_table();
    }

  if (!uri.user.empty())
    {
      ll.push_str("user");
      ll.push_str(uri.user);
      ll.set_table();
    }

  if (!uri.host.empty())
    {
      ll.push_str("host");
      ll.push_str(uri.host);
      ll.set_table();
    }

  if (!uri.port.empty())
    {
      ll.push_str("port");
      ll.push_str(uri.port);
      ll.set_table();
    }

  if (!uri.path.empty())
    {
      ll.push_str("path");
      ll.push_str(uri.path);
      ll.set_table();
    }

  if (!uri.query.empty())
    {
      ll.push_str("query");
      ll.push_str(uri.query);
      ll.set_table();
    }

  if (!uri.fragment.empty())
    {
      ll.push_str("fragment");
      ll.push_str(uri.fragment);
      ll.set_table();
    }
}

bool
lua_hooks::hook_get_netsync_connect_command(uri_t const & uri,
                                            globish const & include_pattern,
                                            globish const & exclude_pattern,
                                            bool debug,
                                            vector<string> & argv)
{
  bool cmd = false, exec_ok = false;
  Lua ll(st);
  ll.func("get_netsync_connect_command");

  push_uri(uri, ll);

  ll.push_table();

  if (!include_pattern().empty())
    {
      ll.push_str("include");
      ll.push_str(include_pattern());
      ll.set_table();
    }

  if (!exclude_pattern().empty())
    {
      ll.push_str("exclude");
      ll.push_str(exclude_pattern());
      ll.set_table();
    }

  if (debug)
    {
      ll.push_str("debug");
      ll.push_bool(debug);
      ll.set_table();
    }

  ll.call(2,1);

  ll.begin();

  argv.clear();
  while(ll.next())
    {
      string s;
      ll.extract_str(s).pop();
      argv.push_back(s);
    }
  return ll.ok() && !argv.empty();
}


bool
lua_hooks::hook_use_transport_auth(uri_t const & uri)
{
  bool use_auth = true;
  Lua ll(st);
  ll.func("use_transport_auth");
  push_uri(uri, ll);
  ll.call(1,1);
  ll.extract_bool(use_auth);

  // NB: we want to return *true* here if there's a failure.
  return use_auth;
}


bool
lua_hooks::hook_get_netsync_read_permitted(string const & branch,
                                           key_identity_info const & identity)
{
  bool permitted = false, exec_ok = false;

  Lua ll(st);
  ll.func("get_netsync_read_permitted")
    .push_str(branch);
  push_key_identity_info(ll, identity);
  exec_ok = ll.call(2,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;
}

// Anonymous no-key version
bool
lua_hooks::hook_get_netsync_read_permitted(string const & branch)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("get_netsync_read_permitted")
    .push_str(branch)
    .push_nil()
    .call(2,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;
}

bool
lua_hooks::hook_get_netsync_write_permitted(key_identity_info const & identity)
{
  bool permitted = false, exec_ok = false;

  Lua ll(st);
  ll.func("get_netsync_write_permitted");
  push_key_identity_info(ll, identity);
  exec_ok = ll.call(1,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;
}

bool
lua_hooks::hook_get_remote_automate_permitted(key_identity_info const & identity,
                                              vector<string> const & command_line,
                                              vector<pair<string, string> > const & command_opts)
{
  Lua ll(st);
  ll.func("get_remote_automate_permitted");
  push_key_identity_info(ll, identity);

  int k = 1;

  ll.push_table();
  vector<string>::const_iterator l;
  for (l = command_line.begin(), k = 1; l != command_line.end(); ++l, ++k)
    {
      ll.push_int(k);
      ll.push_str(*l);
      ll.set_table();
    }
  ll.push_table();
  k = 1;
  vector<pair<string, string> >::const_iterator o;
  for (o = command_opts.begin(), k = 1; o != command_opts.end(); ++o, ++k)
    {
      ll.push_int(k);

      {
        ll.push_table();

        ll.push_str("name");
        ll.push_str(o->first);
        ll.set_table();

        ll.push_str("value");
        ll.push_str(o->second);
        ll.set_table();
      }

      ll.set_table();
    }

  ll.call(3, 1);

  bool permitted(false);
  ll.extract_bool(permitted);
  return ll.ok() && permitted;
}

bool
lua_hooks::hook_init_attributes(file_path const & filename,
                                map<string, string> & attrs)
{
  Lua ll(st);

  ll
    .push_str("attr_init_functions")
    .get_tab();

  L(FL("calling attr_init_function for %s") % filename);
  ll.begin();
  while (ll.next())
    {
      L(FL("  calling an attr_init_function for %s") % filename);
      ll.push_str(filename.as_external());
      ll.call(1, 1);

      if (lua_isstring(st, -1))
        {
          string key, value;

          ll.extract_str(value);
          ll.pop();
          ll.extract_str(key);

          attrs[key] = value;
          L(FL("  added attr %s = %s") % key % value);
        }
      else
        {
          L(FL("  no attr added"));
          ll.pop();
        }
    }

  return ll.pop().ok();
}

bool
lua_hooks::hook_set_attribute(string const & attr,
                              file_path const & filename,
                              string const & value)
{
  return Lua(st)
    .push_str("attr_functions")
    .get_tab()
    .push_str(attr)
    .get_fn(-2)
    .push_str(filename.as_external())
    .push_str(value)
    .call(2,0)
    .ok();
}

bool
lua_hooks::hook_clear_attribute(string const & attr,
                                file_path const & filename)
{
  return Lua(st)
    .push_str("attr_functions")
    .get_tab()
    .push_str(attr)
    .get_fn(-2)
    .push_str(filename.as_external())
    .push_nil()
    .call(2,0)
    .ok();
}

bool
lua_hooks::hook_validate_changes(revision_data const & new_rev,
                                 branch_name const & branchname,
                                 bool & validated,
                                 string & reason)
{
  validated = true;
  return Lua(st)
    .func("validate_changes")
    .push_str(new_rev.inner()())
    .push_str(branchname())
    .call(2, 2)
    .extract_str(reason)
    // XXX When validated, the extra returned string is superfluous.
    .pop()
    .extract_bool(validated)
    .ok();
}

bool
lua_hooks::hook_validate_commit_message(utf8 const & message,
                                        revision_data const & new_rev,
                                        branch_name const & branchname,
                                        bool & validated,
                                        string & reason)
{
  validated = true;
  return Lua(st)
    .func("validate_commit_message")
    .push_str(message())
    .push_str(new_rev.inner()())
    .push_str(branchname())
    .call(3, 2)
    .extract_str(reason)
    // XXX When validated, the extra returned string is superfluous.
    .pop()
    .extract_bool(validated)
    .ok();
}

bool
lua_hooks::hook_note_commit(revision_id const & new_id,
                            revision_data const & rdat,
                            map<cert_name, cert_value> const & certs)
{
  Lua ll(st);
  ll
    .func("note_commit")
    .push_str(encode_hexenc(new_id.inner()(), new_id.inner().made_from))
    .push_str(rdat.inner()());

  ll.push_table();

  for (map<cert_name, cert_value>::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      ll.push_str(i->first());
      ll.push_str(i->second());
      ll.set_table();
    }

  ll.call(3, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_start(size_t session_id, string my_role,
                                   int sync_type, string remote_host,
                                   key_identity_info const & remote_key,
                                   globish include_pattern,
                                   globish exclude_pattern)
{
  string type;
  switch (sync_type)
    {
    case 1:
      type = "push";
      break;
    case 2:
      type = "pull";
      break;
    case 3:
      type = "sync";
      break;
    default:
      type = "unknown";
      break;
    }
  Lua ll(st);
  ll.func("note_netsync_start")
    .push_int(session_id)
    .push_str(my_role)
    .push_str(type)
    .push_str(remote_host);
  push_key_identity_info(ll, remote_key);
  return ll.push_str(include_pattern())
    .push_str(exclude_pattern())
    .call(7, 0)
    .ok();
}

bool
lua_hooks::hook_note_netsync_revision_received(revision_id const & new_id,
                                               revision_data const & rdat,
                                               std::set<pair<key_identity_info,
                                               pair<cert_name,
                                               cert_value> > > const & certs,
                                               size_t session_id)
{
  Lua ll(st);
  ll
    .func("note_netsync_revision_received")
    .push_str(encode_hexenc(new_id.inner()(), new_id.inner().made_from))
    .push_str(rdat.inner()());

  ll.push_table();

  typedef std::set<pair<key_identity_info, pair<cert_name, cert_value> > > cdat;

  int n = 1;
  for (cdat::const_iterator i = certs.begin(); i != certs.end(); ++i)
    {
      ll.push_int(n++);
      ll.push_table();
      push_key_identity_info(ll, i->first);
      ll.set_field("key");
      ll.push_str(i->second.first());
      ll.set_field("name");
      ll.push_str(i->second.second());
      ll.set_field("value");
      ll.set_table();
    }

  ll.push_int(session_id);
  ll.call(4, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_revision_sent(revision_id const & new_id,
                                           revision_data const & rdat,
                                           std::set<pair<key_identity_info,
                                           pair<cert_name,
                                           cert_value> > > const & certs,
                                           size_t session_id)
{
  Lua ll(st);
  ll
    .func("note_netsync_revision_sent")
    .push_str(encode_hexenc(new_id.inner()(), new_id.inner().made_from))
    .push_str(rdat.inner()());

  ll.push_table();

  typedef std::set<pair<key_identity_info, pair<cert_name, cert_value> > > cdat;

  int n = 1;
  for (cdat::const_iterator i = certs.begin(); i != certs.end(); ++i)
    {
      ll.push_int(n++);
      ll.push_table();
      push_key_identity_info(ll, i->first);
      ll.set_field("key");
      ll.push_str(i->second.first());
      ll.set_field("name");
      ll.push_str(i->second.second());
      ll.set_field("value");
      ll.set_table();
    }

  ll.push_int(session_id);
  ll.call(4, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_pubkey_received(key_identity_info const & identity,
                                             size_t session_id)
{
  Lua ll(st);
  ll
    .func("note_netsync_pubkey_received");
  push_key_identity_info(ll, identity);
  ll.push_int(session_id);

  ll.call(2, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_pubkey_sent(key_identity_info const & identity,
                                         size_t session_id)
{
  Lua ll(st);
  ll
    .func("note_netsync_pubkey_sent");
  push_key_identity_info(ll, identity);
  ll.push_int(session_id);

  ll.call(2, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_cert_received(revision_id const & rid,
                                           key_identity_info const & identity,
                                           cert_name const & name,
                                           cert_value const & value,
                                           size_t session_id)
{
  Lua ll(st);
  ll
    .func("note_netsync_cert_received")
    .push_str(encode_hexenc(rid.inner()(), rid.inner().made_from));
  push_key_identity_info(ll, identity);
  ll.push_str(name())
    .push_str(value())
    .push_int(session_id);

  ll.call(5, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_cert_sent(revision_id const & rid,
                                       key_identity_info const & identity,
                                       cert_name const & name,
                                       cert_value const & value,
                                       size_t session_id)
{
  Lua ll(st);
  ll
    .func("note_netsync_cert_sent")
    .push_str(encode_hexenc(rid.inner()(), rid.inner().made_from));
  push_key_identity_info(ll, identity);
  ll.push_str(name())
    .push_str(value())
    .push_int(session_id);

  ll.call(5, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_end(size_t session_id, int status,
                                 size_t bytes_in, size_t bytes_out,
                                 size_t certs_in, size_t certs_out,
                                 size_t revs_in, size_t revs_out,
                                 size_t keys_in, size_t keys_out)
{
  Lua ll(st);
  return ll
    .func("note_netsync_end")
    .push_int(session_id)
    .push_int(status)
    .push_int(bytes_in)
    .push_int(bytes_out)
    .push_int(certs_in)
    .push_int(certs_out)
    .push_int(revs_in)
    .push_int(revs_out)
    .push_int(keys_in)
    .push_int(keys_out)
    .call(10, 0)
    .ok();
}

bool
lua_hooks::hook_note_mtn_startup(args_vector const & args)
{
  Lua ll(st);

  ll.func("note_mtn_startup");

  for (args_vector::const_iterator i = args.begin(); i != args.end(); ++i)
    ll.push_str((*i)());

  ll.call(args.size(), 0);
  return ll.ok();
}

bool
lua_hooks::hook_unmapped_git_author(string const & unmapped_author, string & fixed_author)
{
  return Lua(st)
    .func("unmapped_git_author")
    .push_str(unmapped_author)
    .call(1,1)
    .extract_str(fixed_author)
    .ok();
}

bool
lua_hooks::hook_validate_git_author(string const & author)
{
  bool valid = false, exec_ok = false;
  exec_ok = Lua(st)
    .func("validate_git_author")
    .push_str(author)
    .call(1,1)
    .extract_bool(valid)
    .ok();
  return valid && exec_ok;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
