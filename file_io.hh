#ifndef __FILE_IO_H__
#define __FILE_IO_H__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "boost/format.hpp"

#include "vocab.hh"
#include "paths.hh"

// this layer deals with talking to the filesystem, loading and saving
// files, walking trees, etc.

// this code mostly deals in any_path's, because these operations are too low
// level for us to say whether applying them in any given case is valid or
// not.

struct lua_hooks;

// use I()
void assert_path_is_nonexistent(any_path const & path);
void assert_path_is_file(any_path const & path);
void assert_path_is_directory(any_path const & path);

// use N()
void require_path_is_nonexistent(any_path const & path,
                                 boost::format const & message);
void require_path_is_file(any_path const & path,
                          boost::format const & message_if_nonexistent,
                          boost::format const & message_if_directory);
void require_path_is_directory(any_path const & path,
                               boost::format const & message_if_nonexistent,
                               boost::format const & message_if_file);

// returns true if there is a file or directory at 'path'
bool path_exists(any_path const & path);
// returns true if there is a directory at 'path'
bool directory_exists(any_path const & path);
// returns true if there is a file at 'path'
bool file_exists(any_path const & path);

bool ident_existing_file(file_path const & p, file_id & ident, lua_hooks & lua);

// returns true if the string content is binary according to monotone heuristic
bool guess_binary(std::string const & s);

void mkdir_p(any_path const & path);
void make_dir_for(any_path const & p);

void delete_file(any_path const & path);
void delete_dir_recursive(any_path const & path);

void move_file(any_path const & old_path,
               any_path const & new_path);

void move_dir(any_path const & old_path,
              any_path const & new_path);

// calls move_file or move_dir as appropriate
void move_path(any_path const & old_path,
               any_path const & new_path);

void read_data(any_path const & path, data & data);
void read_localized_data(file_path const & path, 
                         data & dat, 
                         lua_hooks & lua);

// This function knows that "-" means "stdin".
void read_data_for_command_line(utf8 const & path, data & dat);

// These are not any_path's because we make our write somewhat atomic -- we
// first write to a temp file in MT/ (and it must be in MT/, not like /tmp or
// something, because we can't necessarily atomic rename from /tmp to the
// working copy).  But that means we can't use it in general, only for the
// working copy.
void write_data(file_path const & path, data const & data);
void write_data(bookkeeping_path const & path, data const & data);
void write_localized_data(file_path const & path, 
                          data const & dat, 
                          lua_hooks & lua);

class tree_walker
{
public:
  virtual void visit_file(file_path const & path) = 0;
  virtual ~tree_walker();
};

// from some safe sub-dir of cwd
// file_path of "" means cwd
void walk_tree(file_path const & path,
               tree_walker & walker,
               bool require_existing_path = true);

// from anywhere, with native path
void
walk_tree_absolute(fs::path const & path,
		   tree_walker & walker);



#endif // __FILE_IO_H__
