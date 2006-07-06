// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "config.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

#include <set>
#include <map>
#include <fstream>

#include "lua.hh"

#include "app_state.hh"
#include "file_io.hh"
#include "lua_hooks.hh"
#include "sanity.hh"
#include "vocab.hh"
#include "transforms.hh"
#include "paths.hh"
#include "uri.hh"

// defined in {std,test}_hooks.lua, converted
#include "test_hooks.h"
#include "std_hooks.h"

using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::sort;
using std::string;
using std::vector;

using boost::lexical_cast;

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
  monotone_get_confdir_for_lua(lua_State *L)
  {
    map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(L);
    if (i != map_of_lua_to_app.end())
      {
        system_path dir = i->second->get_confdir();
        string confdir = dir.as_external();
        lua_pushstring(L, confdir.c_str());
      }
    else
      lua_pushnil(L);
    return 1;
  }
}

lua_hooks::lua_hooks()
{
  st = lua_open ();
  I(st);

  lua_atpanic (st, &panic_thrower);

  luaopen_base(st);
  luaopen_io(st);
  luaopen_string(st);
  luaopen_math(st);
  luaopen_table(st);
  luaopen_debug(st);

  lua_register(st, "get_confdir", monotone_get_confdir_for_lua);
  add_functions(st);

  // Disable any functions we don't want. This is easiest
  // to do just by running a lua string.
  if (!run_string(st,
                  "os.execute = nil "
                  "io.popen = nil ",
                  string("<disabled dangerous functions>")))
    throw oops("lua error while disabling existing functions");
}

lua_hooks::~lua_hooks()
{
  map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(st);
  if (st)
    lua_close (st);
  if (i != map_of_lua_to_app.end())
    map_of_lua_to_app.erase(i);
}

void
lua_hooks::set_app(app_state *_app)
{
  map_of_lua_to_app.insert(make_pair(st, _app));
}



#ifdef BUILD_UNIT_TESTS
void
lua_hooks::add_test_hooks()
{
  if (!run_string(st, test_hooks_constant, string("<test hooks>")))
    throw oops("lua error while setting up testing hooks");
}
#endif

void
lua_hooks::add_std_hooks()
{
  if (!run_string(st, std_hooks_constant, string("<std hooks>")))
    throw oops("lua error while setting up standard hooks");
}

void
lua_hooks::default_rcfilename(system_path & file)
{
  map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(st);
  I(i != map_of_lua_to_app.end());
  file = i->second->get_confdir() / "monotonerc";
}

void
lua_hooks::workspace_rcfilename(bookkeeping_path & file)
{
  file = bookkeeping_root / "monotonerc";
}


void
lua_hooks::load_rcfile(utf8 const & rc)
{
  I(st);
  if (rc() != "-")
    {
      fs::path locpath(system_path(rc).as_external(), fs::native);
      if (fs::exists(locpath) && fs::is_directory(locpath))
        {
          // directory, iterate over it, skipping subdirs, taking every filename,
          // sorting them and loading in sorted order
          fs::directory_iterator it(locpath);
          vector<fs::path> arr;
          while (it != fs::directory_iterator())
            {
              if (!fs::is_directory(*it))
                arr.push_back(*it);
              ++it;
            }
          sort(arr.begin(), arr.end());
          for (vector<fs::path>::iterator i= arr.begin(); i != arr.end(); ++i)
            {
              load_rcfile(system_path(i->native_directory_string()), true);
            }
          return; // directory read, skip the rest ...
        }
    }
  data dat;
  L(FL("opening rcfile '%s'") % rc);
  read_data_for_command_line(rc, dat);
  N(run_string(st, dat(), rc().c_str()),
    F("lua error while loading rcfile '%s'") % rc);
  L(FL("'%s' is ok") % rc);
}

void
lua_hooks::load_rcfile(any_path const & rc, bool required)
{
  I(st);
  if (path_exists(rc))
    {
      L(FL("opening rcfile '%s'") % rc);
      N(run_file(st, rc.as_external()),
        F("lua error while loading '%s'") % rc);
      L(FL("'%s' is ok") % rc);
    }
  else
    {
      N(!required, F("rcfile '%s' does not exist") % rc);
      L(FL("skipping nonexistent rcfile '%s'") % rc);
    }
}

// utility function, not really a hook
bool
lua_hooks::patternmatch(const string & haystack, const string & needle)
{
  int matchstart;

  // If string.find returned nil, extract_int will fail, so the ok() return
  // is the result we want.
  return Lua(st)
    .push_str("string")
    .get_tab()
    .push_str("find")
    .get_fn(-2)
    .push_str(haystack)
    .push_str(needle)
    .call(2,1)
    .extract_int(matchstart)
    .ok();
}

// concrete hooks

// nb: if you're hooking lua to return your passphrase, you don't care if we
// keep a couple extra temporaries of your passphrase around.
bool
lua_hooks::hook_get_passphrase(rsa_keypair_id const & k, string & phrase)
{
  return Lua(st)
    .func("get_passphrase")
    .push_str(k())
    .call(1,1)
    .extract_classified_str(phrase)
    .ok();
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
lua_hooks::hook_get_branch_key(cert_value const & branchname,
                               rsa_keypair_id & k)
{
  string key;
  bool ok = Lua(st)
    .func("get_branch_key")
    .push_str(branchname())
    .call(1,1)
    .extract_str(key)
    .ok();

  k = key;
  return ok;
}

bool
lua_hooks::hook_get_author(cert_value const & branchname,
                           string & author)
{
  return Lua(st)
    .func("get_author")
    .push_str(branchname())
    .call(1,1)
    .extract_str(author)
    .ok();
}

bool
lua_hooks::hook_edit_comment(string const & commentary,
                             string const & user_log_message,
                             string & result)
{
  return Lua(st)
    .func("edit_comment")
    .push_str(commentary)
    .push_str(user_log_message)
    .call(2,1)
    .extract_str(result)
    .ok();
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
lua_hooks::hook_ignore_branch(string const & branch)
{
  bool ignore_it = false;
  bool exec_ok = Lua(st)
    .func("ignore_branch")
    .push_str(branch)
    .call(1,1)
    .extract_bool(ignore_it)
    .ok();
  return exec_ok && ignore_it;
}

static inline bool
shared_trust_function_body(Lua & ll,
                           set<rsa_keypair_id> const & signers,
                           hexenc<id> const & id,
                           cert_name const & name,
                           cert_value const & val)
{
  ll.push_table();

  int k = 1;
  for (set<rsa_keypair_id>::const_iterator v = signers.begin();
       v != signers.end(); ++v)
    {
      ll.push_int(k);
      ll.push_str((*v)());
      ll.set_table();
      ++k;
    }

  bool ok;
  bool exec_ok = ll
    .push_str(id())
    .push_str(name())
    .push_str(val())
    .call(4, 1)
    .extract_bool(ok)
    .ok();

  return exec_ok && ok;
}

bool
lua_hooks::hook_get_revision_cert_trust(set<rsa_keypair_id> const & signers,
                                       hexenc<id> const & id,
                                       cert_name const & name,
                                       cert_value const & val)
{
  Lua ll(st);
  ll.func("get_revision_cert_trust");
  return shared_trust_function_body(ll, signers, id, name, val);
}

bool
lua_hooks::hook_get_manifest_cert_trust(set<rsa_keypair_id> const & signers,
                                        hexenc<id> const & id,
                                        cert_name const & name,
                                        cert_value const & val)
{
  Lua ll(st);
  ll.func("get_manifest_cert_trust");
  return shared_trust_function_body(ll, signers, id, name, val);
}

bool
lua_hooks::hook_accept_testresult_change(map<rsa_keypair_id, bool> const & old_results,
                                         map<rsa_keypair_id, bool> const & new_results)
{
  Lua ll(st);
  ll
    .func("accept_testresult_change")
    .push_table();

  for (map<rsa_keypair_id, bool>::const_iterator i = old_results.begin();
       i != old_results.end(); ++i)
    {
      ll.push_str(i->first());
      ll.push_bool(i->second);
      ll.set_table();
    }

  ll.push_table();

  for (map<rsa_keypair_id, bool>::const_iterator i = new_results.begin();
       i != new_results.end(); ++i)
    {
      ll.push_str(i->first());
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
  result = res;
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
lua_hooks::hook_get_encloser_pattern(std::string const & path,
                                     std::string & pattern)
{
  bool exec_ok
    = Lua(st)
    .func("get_encloser_pattern")
    .push_str(path)
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

static void
push_uri(uri const & u, Lua & ll)
{
  ll.push_table();

  if (!u.scheme.empty())
    {
      ll.push_str("scheme");
      ll.push_str(u.scheme);
      ll.set_table();
    }

  if (!u.user.empty())
    {
      ll.push_str("user");
      ll.push_str(u.user);
      ll.set_table();
    }

  if (!u.host.empty())
    {
      ll.push_str("host");
      ll.push_str(u.host);
      ll.set_table();
    }

  if (!u.port.empty())
    {
      ll.push_str("port");
      ll.push_str(u.port);
      ll.set_table();
    }

  if (!u.path.empty())
    {
      ll.push_str("path");
      ll.push_str(u.path);
      ll.set_table();
    }

  if (!u.query.empty())
    {
      ll.push_str("query");
      ll.push_str(u.query);
      ll.set_table();
    }

  if (!u.fragment.empty())
    {
      ll.push_str("fragment");
      ll.push_str(u.fragment);
      ll.set_table();
    }
}

bool
lua_hooks::hook_get_netsync_connect_command(uri const & u,
                                            std::string const & include_pattern,
                                            std::string const & exclude_pattern,
                                            bool debug,
                                            std::vector<std::string> & argv)
{
  bool cmd = false, exec_ok = false;
  Lua ll(st);
  ll.func("get_netsync_connect_command");

  push_uri(u, ll);

  ll.push_table();

  if (!include_pattern.empty())
    {
      ll.push_str("include");
      ll.push_str(include_pattern);
      ll.set_table();
    }

  if (!exclude_pattern.empty())
    {
      ll.push_str("exclude");
      ll.push_str(exclude_pattern);
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
      std::string s;
      ll.extract_str(s).pop();
      argv.push_back(s);
    }
  return ll.ok() && !argv.empty();
}


bool
lua_hooks::hook_use_transport_auth(uri const & u)
{
  bool use_auth = true;
  Lua ll(st);
  ll.func("use_transport_auth");
  push_uri(u, ll);
  ll.call(1,1);
  ll.extract_bool(use_auth);

  // NB: we want to return *true* here if there's a failure.
  return use_auth;
}


bool
lua_hooks::hook_get_netsync_read_permitted(string const & branch,
                                           rsa_keypair_id const & identity)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("get_netsync_read_permitted")
    .push_str(branch)
    .push_str(identity())
    .call(2,1)
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
lua_hooks::hook_get_netsync_write_permitted(rsa_keypair_id const & identity)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("get_netsync_write_permitted")
    .push_str(identity())
    .call(1,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;
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
lua_hooks::hook_apply_attribute(string const & attr,
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
lua_hooks::hook_get_system_linesep(string & linesep)
{
  return Lua(st)
    .func("get_system_linesep")
    .call(0,1)
    .extract_str(linesep)
    .ok();
}

bool
lua_hooks::hook_get_charset_conv(file_path const & p,
                                 string & db,
                                 string & ext)
{
  Lua ll(st);
  ll
    .func("get_charset_conv")
    .push_str(p.as_external())
    .call(1,1)
    .begin();

  ll.next();
  ll.extract_str(db).pop();

  ll.next();
  ll.extract_str(ext).pop();
  return ll.ok();
}

bool
lua_hooks::hook_get_linesep_conv(file_path const & p,
                                 string & db,
                                 string & ext)
{
  Lua ll(st);
  ll
    .func("get_linesep_conv")
    .push_str(p.as_external())
    .call(1,1)
    .begin();

  ll.next();
  ll.extract_str(db).pop();

  ll.next();
  ll.extract_str(ext).pop();
  return ll.ok();
}

bool
lua_hooks::hook_validate_commit_message(string const & message,
                                        string const & new_manifest_text,
                                        bool & validated,
                                        string & reason)
{
  validated = true;
  return Lua(st)
    .func("validate_commit_message")
    .push_str(message)
    .push_str(new_manifest_text)
    .call(2, 2)
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
    .push_str(new_id.inner()())
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
lua_hooks::hook_note_netsync_start(string nonce)
{
  Lua ll(st);
  return ll
    .func("note_netsync_start")
    .push_str(nonce)
    .call(1, 0)
    .ok();
}

bool
lua_hooks::hook_note_netsync_revision_received(revision_id const & new_id,
                                               revision_data const & rdat,
                            set<pair<rsa_keypair_id,
                                     pair<cert_name,
                                          cert_value> > > const & certs,
                                               string nonce)
{
  Lua ll(st);
  ll
    .func("note_netsync_revision_received")
    .push_str(new_id.inner()())
    .push_str(rdat.inner()());

  ll.push_table();

  typedef set<pair<rsa_keypair_id, pair<cert_name, cert_value> > > cdat;

  int n=0;
  for (cdat::const_iterator i = certs.begin(); i != certs.end(); ++i)
    {
      ll.push_int(n++);
      ll.push_table();
      ll.push_str("key");
      ll.push_str(i->first());
      ll.set_table();
      ll.push_str("name");
      ll.push_str(i->second.first());
      ll.set_table();
      ll.push_str("value");
      ll.push_str(i->second.second());
      ll.set_table();
      ll.set_table();
    }

  ll.push_str(nonce);
  ll.call(4, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_pubkey_received(rsa_keypair_id const & kid,
                                             string nonce)
{
  Lua ll(st);
  ll
    .func("note_netsync_pubkey_received")
    .push_str(kid())
    .push_str(nonce);

  ll.call(2, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_cert_received(revision_id const & rid,
                                           rsa_keypair_id const & kid,
                                           cert_name const & name,
                                           cert_value const & value,
                                           string nonce)
{
  Lua ll(st);
  ll
    .func("note_netsync_cert_received")
    .push_str(rid.inner()())
    .push_str(kid())
    .push_str(name())
    .push_str(value())
    .push_str(nonce);

  ll.call(5, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_end(string nonce)
{
  Lua ll(st);
  return ll
    .func("note_netsync_end")
    .push_str(nonce)
    .call(1, 0)
    .ok();
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
