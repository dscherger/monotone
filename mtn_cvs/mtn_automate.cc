// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "mtn_automate.hh"
#include <sanity.hh>

void mtn_automate::check_interface_revision(std::string const& minimum)
{ std::string present=automate("interface_version");
  N(present>=minimum,
      F("your monotone automate interface revision %s does not match the "
          "requirements %s") % present % minimum);
}
