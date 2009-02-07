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


#include "base.hh"

#include <boost/version.hpp>
#include <boost/config.hpp>
#include <iostream>

#include "app_state.hh"
#include "platform.hh"
#include "mt_version.hh"
#include "package_revision.h"
#include "package_full_revision.h"
#include "sanity.hh"

using std::cout;
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
  cout << s;
}

void
get_full_version(string & out)
{
  string base_version;
  get_version(base_version);
  string flavour;
  get_system_flavour(flavour);
  out = (F("%s\n"
           "Running on          : %s\n"
           "C++ compiler        : %s\n"
           "C++ standard library: %s\n"
           "Boost version       : %s\n"
           "Changes since base revision:\n"
           "%s")
         % base_version
         % flavour
         % BOOST_COMPILER
         % BOOST_STDLIB
         % BOOST_LIB_VERSION
         % package_full_revision_constant)
    .str();
}

void
print_full_version()
{
  string s;
  get_full_version(s);
  cout << s;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
