// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "../base.hh"
#include "mtncvs_state.hh"

#define D(x) << #x " " << x << ' '
#include <iostream>

std::ostream& operator<<(std::ostream &o, std::vector<std::string> const& v);
#if 0
{ //std::copy(v.begin(),v.end(),output_iterator
  o << '{';
  for(std::vector<std::string>::const_iterator i=v.begin();i!=v.end();++i)
    o << (*i) << ',';
  return o << '}';
}
#endif

void mtncvs_state::dump()
{ std::cerr D(opts.full) D(opts.since) D(opts.mtn_binary) D(opts.branchname) << '\n';
  std::cerr D(opts.mtn_options) << '\n';
}

void mtncvs_state::open()
{ std::vector<std::string> const& args= opts.mtn_options;
  std::string binary=opts.mtn_binary;
  if (binary.empty()) binary="mtn";
  I(!is_open());
  L(FL("mtncvs_state: opening mtn binary %s") % binary);
  mtn_automate::open(binary,args);
  check_interface_revision("4.1");
}
