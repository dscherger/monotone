#ifndef __APP_STATE_HH__
#define __APP_STATE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

class app_state;
class lua_hooks;

#include <vector>

#include "database.hh"
#include "lua.hh"
#include "work.hh"
#include "vocab.hh"

// this class is supposed to hold all (or.. well, most) of the state of the
// application, barring some unfortunate static objects like the debugging /
// logging system and the command objects, for the time being. the vague intent
// being to make life easier for anyone who wants to embed this program as a
// library someday

class app_state
{
public:
  rsa_keypair_id signing_key;
  utf8 branch_name;
  database db;
  lua_hooks lua;
  bool stdhooks;
  bool rcfiles;
  options_map options;
  utf8 message;
  std::vector<utf8> revision_selectors;
  std::vector<utf8> extra_rcfiles;
  std::set<file_path> restrictions;
  file_path relative_directory;

  void initialize(bool working_copy);
  void initialize(std::string const & dir);

  file_path prefix(utf8 const & path);
  void add_restriction(utf8 const & path);
  bool in_restriction(file_path const & path);

  void set_branch(utf8 const & name);
  void set_database(utf8 const & filename);
  void set_signing_key(utf8 const & key);

  void set_message(utf8 const & message);
  void add_revision(utf8 const & selector);

  void set_stdhooks(bool b);
  void set_rcfiles(bool b);
  void add_rcfile(utf8 const & filename);

  explicit app_state();
  ~app_state();

private:
  void load_rcfiles();
  void read_options();
  void write_options();
};

#endif // __APP_STATE_HH__
