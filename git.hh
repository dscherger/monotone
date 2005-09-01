#ifndef __GIT_HH__
#define __GIT_HH__

// Copyright (C) 2005  Petr Baudis <pasky@suse.cz>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// GIT "library" interface

#include <iostream>
#include <string>

#include "vocab.hh"
#include "database.hh"

typedef hexenc<id> git_object_id;

struct
git_person
{
  std::string name, email;
};


extern std::string const gitcommit_id_cert_name;
extern std::string const gitcommit_committer_cert_name;


void set_git_env(std::string const &name, std::string const &value);
void stream_grabline(std::istream &stream, std::string &line);
int git_tmpfile(std::string &tmpfile);

void capture_git_cmd_output(boost::format const &fmt, std::filebuf &fbout);
void capture_git_cmd_io(boost::format const &fmt, data const &input, std::filebuf &fbout);

#endif // __GIT_HH__
