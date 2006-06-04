// Copyright (C) 2004 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This is split off into its own file to minimize recompilation time; it is
// the only .cc file that depends on the revision/full_revision header files,
// which change constantly.

#include "config.h"

#include <iostream>
#include <sstream>

#include <boost/version.hpp>
#include <boost/config.hpp>

#include "platform.hh"
#include "mt_version.hh"
#include "package_revision.h"
#include "package_full_revision.h"
#include "sanity.hh"

using std::cout;
using std::endl;
using std::ostringstream;
using std::string;

void
get_version(string & out)
{
  out = (F("%s (base revision: %s)")
         % PACKAGE_STRING % string(package_revision_constant)).str();
}

void
print_version()
{
  string s;
  get_version(s);
  cout << s << endl;
}

void
get_full_version(string & out)
{
  ostringstream oss;
  string s;
  get_version(s);
  oss << s << "\n";
  get_system_flavour(s);
  oss << F("Running on          : %s\n"
           "C++ compiler        : %s\n"
           "C++ standard library: %s\n"
           "Boost version       : %s\n"
           "Changes since base revision:\n"
           "%s")
    % s
    % BOOST_COMPILER
    % BOOST_STDLIB
    % BOOST_LIB_VERSION
    % string(package_full_revision_constant);
  out = oss.str();
}

void
print_full_version()
{
  string s;
  get_full_version(s);
  cout << s << endl;
}
