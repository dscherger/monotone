#ifndef __WORK_HH__
#define __WORK_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <set>
#include <map>

#include "change_set.hh"
#include "manifest.hh"
#include "vocab.hh"

//
// this file defines structures to deal with the "working copy" of a tree
//

//
// working copy book-keeping files are stored in a directory called MT, off
// the root of the working copy source tree (analogous to the CVS or .svn
// directories). there is no hierarchy of MT directories; only one exists,
// and it is always at the root. it contains the following files:
//
// MT/manifest       -- the check-out manifest, as defined in manifest.hh
// MT/work           -- (optional) a set of added, deleted or moved pathnames
//                      this file is, syntactically, a path_rearrangement
//
// as work proceeds, the files in the working directory either change their
// sha1 fingerprints from those listed in the manifest file, or else are
// added or deleted (and the paths of those changes recorded in 'MT/work').
// 
// when it comes time to commit, the change_set is calculated by applying
// the path_rearrangement to the manifest and then calculating the
// delta_set between the modified manifest and the files in the working
// copy.
//

typedef std::set<file_path> path_set;

extern std::string const work_file_name;

void extract_path_set(manifest_map const & man,
		      path_set & paths);

void 
build_addition(file_path const & path,
	       manifest_map const & m_old,
	       app_state & app,
	       change_set::path_rearrangement & pr);

void 
build_deletion(file_path const & path,
	       manifest_map const & m_old,
	       change_set::path_rearrangement & pr);

void 
build_rename(file_path const & src,
	     file_path const & dst,
	     manifest_map const & m_old,
	     change_set::path_rearrangement & pr);


// the "options map" is another administrative file, stored in
// MT/options. it keeps a list of name/value pairs which are considered
// "persistent options", associated with a particular the working copy and
// implied unless overridden on the command line. the main ones are
// --branch and --db, although some others may follow in the future.

typedef std::map<std::string, utf8> options_map;

void get_options_path(local_path & o_path);

void read_options_map(data const & dat, options_map & options);

void write_options_map(data & dat,
		       options_map const & options);

// the "attribute map" is part of a working copy. it is *not* stored in MT,
// because its contents are considered part of the "content" of a tree of
// files. it is therefore stored in .mt-attrs, in the root of your
// tree. you do not need a .mt-attrs file, it's just an extension
// mechanism.
//
// the contents of the .mt-attrs file is a list of [file, name, value]
// triples, each of which assigns a particular "extended attribute" to a
// file in your manifest. example "extended attributes" are things like
// "set the execute bit" or "this file is read-only" or whatnot. they are
// intrinsic properties of the files, but not actually part of the file's
// data stream. so they're kept here.

typedef std::map<std::pair<file_path, std::string>, std::string> attr_map;

void get_attr_path(file_path & a_path);

void read_attr_map(data const & dat, attr_map & attrs);

void write_attr_map(data & dat,
		    attr_map const & options);

void apply_attributes(app_state & app, 
		      attr_map const & attr);


#endif // __WORK_HH__
