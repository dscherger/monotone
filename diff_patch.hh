#ifndef __DIFF_PATCH_HH__
#define __DIFF_PATCH_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "app_state.hh"
#include "cert.hh"
#include "vocab.hh"

#include <string>
#include <vector>
#include <iostream>

struct conflict {};

// this file is to contain some stripped down, in-process implementations
// of GNU-diffutils-like things (diff, diff3, maybe patch..)

bool guess_binary(std::string const & s);

void unidiff(std::string const & filename1,
	     std::string const & filename2,
	     std::vector<std::string> const & lines1,
	     std::vector<std::string> const & lines2,
	     std::ostream & ost);

bool merge3(std::vector<std::string> const & ancestor,
	    std::vector<std::string> const & left,
	    std::vector<std::string> const & right,
	    std::vector<std::string> & merged);

struct merge_provider
{
  app_state & app;
  merge_provider(app_state & app);
  // merge3 on a file (line by line)
  virtual bool try_to_merge_files(file_path const & path,
				  file_id const & ancestor_id,
				  file_id const & left_id,
				  file_id const & right,
				  file_id & merged_id);

  // merge2 on a file (line by line)
  virtual bool try_to_merge_files(file_path const & path,
				  file_id const & left_id,
				  file_id const & right_id,
				  file_id & merged);

  virtual void record_merge(file_id const & left_ident, 
			    file_id const & right_ident, 
			    file_id const & merged_ident,
			    file_data const & left_data, 
			    file_data const & merged_data);
  
  virtual void get_version(file_path const & path,
			   file_id const & ident,			   
			   file_data & dat);

};

struct update_merge_provider : public merge_provider
{
  std::map<file_id, file_data> temporary_store;
  update_merge_provider(app_state & app);

  virtual void record_merge(file_id const & left_ident, 
			    file_id const & right_ident, 
			    file_id const & merged_ident,
			    file_data const & left_data, 
			    file_data const & merged_data);

  virtual void get_version(file_path const & path,
			   file_id const & ident,
			   file_data & dat);

  virtual ~update_merge_provider() {}
};


#endif // __DIFF_PATCH_HH__
