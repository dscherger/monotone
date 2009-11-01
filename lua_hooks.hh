// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __LUA_HOOKS_HH__
#define __LUA_HOOKS_HH__

// this file defines a typed C++ interface to the various hooks
// we expose to the user as lua functions or variables

#include <set>
#include <map>
#include "branch_name.hh"
#include "file_io.hh"
#include "option.hh"
#include "vocab.hh"
#include "paths.hh"
#include "commands.hh"

struct uri_t;
class app_state;
class key_store;
struct lua_State;
struct globish;
struct options;
class project_t;
struct key_identity_info;

extern app_state* get_app_state(lua_State *LS);

class lua_hooks
{
  struct lua_State * st;

  void add_std_hooks();
  void load_rcfile(utf8 const & file);
  void load_rcfile(any_path const & file, bool required);

public:
  lua_hooks(app_state * app);
  ~lua_hooks();
  bool check_lua_state(lua_State * st) const;
  void load_rcfiles(options & opts);
  bool hook_exists(std::string const & func_name);

  // cert hooks
  bool hook_expand_selector(std::string const & sel, std::string & exp);
  bool hook_expand_date(std::string const & sel, std::string & exp);
  bool hook_get_branch_key(branch_name const & branchname,
                           key_store & keys,
                           project_t & project, key_id & k);
  bool hook_get_passphrase(key_identity_info const & info,
                           std::string & phrase);
  bool hook_get_local_key_name(key_identity_info & info);
  bool hook_get_author(branch_name const & branchname,
                       key_identity_info const & info,
                       std::string & author);
  bool hook_edit_comment(external const & commentary,
                         external const & user_log_message,
                         external & result);
  bool hook_persist_phrase_ok();
  bool hook_get_revision_cert_trust(std::set<key_identity_info> const & signers,
                                   id const & hash,
                                   cert_name const & name,
                                   cert_value const & val);
  bool hook_get_manifest_cert_trust(std::set<key_name> const & signers,
                                    id const & hash,
                                    cert_name const & name,
                                    cert_value const & val);
  bool hook_accept_testresult_change(std::map<key_id, bool> const & old_results,
                                     std::map<key_id, bool> const & new_results);


  bool hook_get_projects(std::map<std::string, data> & project_definitions);


  // network hooks
  bool hook_get_netsync_key(utf8 const & server_address,
                            globish const & include,
                            globish const & exclude,
                            key_store & keys,
                            project_t & project,
                            key_id & k);
  bool hook_get_netsync_connect_command(uri_t const & uri,
                                        globish const & include_pattern,
                                        globish const & exclude_pattern,
                                        bool debug,
                                        std::vector<std::string> & argv);
  bool hook_use_transport_auth(uri_t const & uri);

  bool hook_get_netsync_read_permitted(std::string const & branch,
                                       key_identity_info const & identity);
  // anonymous no-key version
  bool hook_get_netsync_read_permitted(std::string const & branch);
  bool hook_get_netsync_write_permitted(key_identity_info const & identity);

  bool hook_get_remote_automate_permitted(key_identity_info const & identity,
                                          std::vector<std::string> const & command_line,
                                          std::vector<std::pair<std::string, std::string> > const & command_opts);

  // local repo hooks
  bool hook_ignore_file(file_path const & p);
  bool hook_ignore_branch(branch_name const & branch);
  bool hook_merge3(file_path const & anc_path,
                   file_path const & left_path,
                   file_path const & right_path,
                   file_path const & merged_path,
                   data const & ancestor,
                   data const & left,
                   data const & right,
                   data & result);

  bool hook_external_diff(file_path const & path,
                          data const & data_old,
                          data const & data_new,
                          bool is_binary,
                          bool diff_args_provided,
                          std::string const & diff_args,
                          std::string const & oldrev,
                          std::string const & newrev);

  bool hook_get_encloser_pattern(file_path const & path,
                                 std::string & pattern);

  bool hook_get_default_command_options(commands::command_id const & cmd,
                                        args_vector & args);

  bool hook_get_date_format_spec(std::string & spec);

  // workspace hooks
  bool hook_use_inodeprints();

  // attribute hooks
  bool hook_init_attributes(file_path const & filename,
                            std::map<std::string, std::string> & attrs);
  bool hook_set_attribute(std::string const & attr,
                          file_path const & filename,
                          std::string const & value);
  bool hook_clear_attribute(std::string const & attr,
                            file_path const & filename);

  // validation hooks
  bool hook_validate_commit_message(utf8 const & message,
                                    revision_data const & new_rev,
                                    branch_name const & branchname,
                                    bool & validated,
                                    std::string & reason);

  // meta hooks
  bool hook_hook_wrapper(std::string const & func_name,
                         std::vector<std::string> const & args,
                         std::string & out);

  // notification hooks
  bool hook_note_commit(revision_id const & new_id,
                        revision_data const & rdat,
                        std::map<cert_name, cert_value> const & certs);

  bool hook_note_netsync_start(size_t session_id,
                               std::string my_role,
                               int sync_type,
                               std::string remote_host,
                               key_identity_info const & remote_key,
                               globish include_pattern,
                               globish exclude_pattern);
  bool hook_note_netsync_revision_received(revision_id const & new_id,
                                           revision_data const & rdat,
                                           std::set<std::pair<key_identity_info,
                                           std::pair<cert_name,
                                           cert_value> > > const & certs,
                                           size_t session_id);
  bool hook_note_netsync_revision_sent(revision_id const & new_id,
                                       revision_data const & rdat,
                                       std::set<std::pair<key_identity_info,
                                       std::pair<cert_name,
                                       cert_value> > > const & certs,
                                       size_t session_id);
  bool hook_note_netsync_pubkey_received(key_identity_info const & identity,
                                         size_t session_id);
  bool hook_note_netsync_pubkey_sent(key_identity_info const & identity,
                                     size_t session_id);
  bool hook_note_netsync_cert_received(revision_id const & rid,
                                       key_identity_info const & identity,
                                       cert_name const & name,
                                       cert_value const & value,
                                       size_t session_id);
  bool hook_note_netsync_cert_sent(revision_id const & rid,
                                   key_identity_info const & identity,
                                   cert_name const & name,
                                   cert_value const & value,
                                   size_t session_id);
  bool hook_note_netsync_end(size_t session_id, int status,
                             size_t bytes_in, size_t bytes_out,
                             size_t certs_in, size_t certs_out,
                             size_t revs_in, size_t revs_out,
                             size_t keys_in, size_t keys_out);
  bool hook_note_mtn_startup(args_vector const & args);
};

#endif // __LUA_HOOKS_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
