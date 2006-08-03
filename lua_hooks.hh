#ifndef __LUA_HOOKS_HH__
#define __LUA_HOOKS_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// this file defines a typed C++ interface to the various hooks
// we expose to the user as lua functions or variables

#include <string>
#include <set>
#include <map>
#include "file_io.hh"
#include "vocab.hh"
#include "paths.hh"

struct uri;
class app_state;
struct lua_State;

class lua_hooks
{
  struct lua_State * st;

public:
  lua_hooks();
  ~lua_hooks();
#ifdef BUILD_UNIT_TESTS
  void add_test_hooks();
#endif
  void set_app(app_state *_app);
  void add_std_hooks();
  void workspace_rcfilename(bookkeeping_path & file);
  void default_rcfilename(system_path & file);
  void load_rcfile(utf8 const & file);
  void load_rcfile(any_path const & file, bool required);

  // cert hooks
  bool hook_expand_selector(std::string const & sel, std::string & exp);
  bool hook_expand_date(std::string const & sel, std::string & exp);
  bool hook_get_branch_key(cert_value const & branchname, rsa_keypair_id & k);
  bool hook_get_passphrase(rsa_keypair_id const & k, std::string & phrase);
  bool hook_get_author(cert_value const & branchname, std::string & author);
  bool hook_edit_comment(std::string const & commentary,
                         std::string const & user_log_message,
                         std::string & result);
  bool hook_persist_phrase_ok();
  bool hook_get_revision_cert_trust(std::set<rsa_keypair_id> const & signers,
                                   hexenc<id> const & id,
                                   cert_name const & name,
                                   cert_value const & val);
  bool hook_get_manifest_cert_trust(std::set<rsa_keypair_id> const & signers,
                                    hexenc<id> const & id,
                                    cert_name const & name,
                                    cert_value const & val);
  bool hook_accept_testresult_change(std::map<rsa_keypair_id, bool> const & old_results,
                                     std::map<rsa_keypair_id, bool> const & new_results);

  // network hooks
  bool hook_get_netsync_connect_command(uri const & u,
					std::string const & include_pattern,
					std::string const & exclude_pattern,
					bool debug,
					std::vector<std::string> & argv);
  bool hook_use_transport_auth(uri const & u);
			
  bool hook_get_netsync_read_permitted(std::string const & branch,
                                       rsa_keypair_id const & identity);
  // anonymous no-key version
  bool hook_get_netsync_read_permitted(std::string const & branch);
  bool hook_get_netsync_write_permitted(rsa_keypair_id const & identity);

  // local repo hooks
  bool hook_ignore_file(file_path const & p);
  bool hook_ignore_branch(std::string const & branch);
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

  // workspace hooks
  bool hook_use_inodeprints();

  // attribute hooks
  bool hook_init_attributes(file_path const & filename,
                            std::map<std::string, std::string> & attrs);
  bool hook_apply_attribute(std::string const & attr,
                            file_path const & filename,
                            std::string const & value);

  // conversion hooks
  bool hook_get_system_linesep(std::string & linesep);
  bool hook_get_charset_conv(file_path const & p,
                             std::string & db, std::string & ext);
  bool hook_get_linesep_conv(file_path const & p,
                             std::string & db, std::string & ext);

  // validation hooks
  bool hook_validate_commit_message(std::string const & message,
                                    std::string const & new_manifest_text,
                                    bool & validated,
                                    std::string & reason);

  // notification hooks
  bool hook_note_commit(revision_id const & new_id,
                        revision_data const & rdat,
                        std::map<cert_name, cert_value> const & certs);

  bool hook_note_netsync_start(std::string nonce);
  bool hook_note_netsync_revision_received(revision_id const & new_id,
                                           revision_data const & rdat,
                        std::set<std::pair<rsa_keypair_id,
                                         std::pair<cert_name,
                                                cert_value> > > const & certs,
					   std::string nonce);
  bool hook_note_netsync_pubkey_received(rsa_keypair_id const & kid,
					 std::string nonce);
  bool hook_note_netsync_cert_received(revision_id const & rid,
                                       rsa_keypair_id const & kid,
                                       cert_name const & name,
                                       cert_value const & value,
				       std::string nonce);
  bool hook_note_netsync_end(std::string nonce);

  // misc hooks
  int hook_get_vcache_size();
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __LUA_HOOKS_HH__
