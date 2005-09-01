// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// vim:sw=2:
// Copyright (C) 2005  Petr Baudis <pasky@suse.cz>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// Common utility functions for manipulating GIT-related stuff
// and communicating with GIT itself.
// Sponsored by Google's Summer of Code and SuSE

#include <algorithm>
#include <iostream>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>
#include <queue>
#include <stdlib.h>

#ifndef WIN32

#include <unistd.h>

#include <stdio.h>
#include <string.h> // strdup(), woo-hoo!

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "app_state.hh"
#include "cert.hh"
#include "constants.hh"
#include "database.hh"
#include "file_io.hh"
#include "git.hh"
#include "mkstemp.hh"

using namespace std;
using boost::shared_ptr;
using boost::scoped_ptr;


string const gitcommit_id_cert_name = "gitcommit-id";
string const gitcommit_committer_cert_name = "gitcommit-committer";


void
set_git_env(string const & name, string const & value)
{
  char *env_entry = strdup((name + "=" + value).c_str());
  putenv(env_entry);
}

void
stream_grabline(istream &stream, string &line)
{
  // You can't hate C++ as much as I do.
  char linebuf[256];
  stream.getline(linebuf, 256);
  line = linebuf;
}

int
git_tmpfile(string &tmpfile)
{
  char *tmpdir = getenv("TMPDIR");
  if (!tmpdir)
    tmpdir = "/tmp";

  tmpfile = string(tmpdir);
  tmpfile += "/mtgit.XXXXXX";
  return monotone_mkstemp(tmpfile);
}


void
capture_git_cmd_output(boost::format const & fmt, filebuf &fb)
{
  string str;
  try
    {
      str = fmt.str();
    }
  catch (std::exception & e)
    {
      P(F("capture_git_cmd_output() formatter failed: %s") % e.what());
      throw e;
    }

  string tmpfile;
  int fd = git_tmpfile(tmpfile);

  string cmdline("(" + str + ") >" + tmpfile);
  L(F("Capturing cmd output: %s") % cmdline);
  N(system(cmdline.c_str()) == 0,
    F("git command %s failed") % str);
  fb.open(tmpfile.c_str(), ios::in);
  close(fd);
  delete_file(system_path(tmpfile));
}

void
capture_git_cmd_io(boost::format const & fmt, data const &input, filebuf &fbout)
{
  string str;
  try
    {
      str = fmt.str();
    }
  catch (std::exception & e)
    {
      P(F("capture_git_cmd_io() formatter failed: %s") % e.what());
      throw e;
    }

  string intmpfile;
  {
    int fd = git_tmpfile(intmpfile);
    filebuf fb;
    fb.open(intmpfile.c_str(), ios::out);
    close(fd);
    ostream stream(&fb);
    stream << input();
  }

  string outtmpfile;
  int fd = git_tmpfile(outtmpfile);
  string cmdline("(" + str + ") <" + intmpfile + " >" + outtmpfile);
  L(F("Feeding cmd input and grabbing output: %s") % cmdline);
  N(system(cmdline.c_str()) == 0,
    F("git command %s failed") % str);
  fbout.open(outtmpfile.c_str(), ios::in);
  close(fd);
  delete_file(system_path(outtmpfile));
  delete_file(system_path(intmpfile));
}

#endif
