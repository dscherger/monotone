// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "mtncvs_state.hh"

#define D(x) << #x " " << x << ' '
#include <iostream>

std::ostream& operator<<(std::ostream &o, std::vector<utf8> const& v)
{ //std::copy(v.begin(),v.end(),output_iterator
  o << '{';
  for(std::vector<utf8>::const_iterator i=v.begin();i!=v.end();++i)
    o << (*i) << ',';
  return o << '}';
}

void mtncvs_state::dump()
{ std::cerr D(full) D(since) D(mtn_binary) D(branch) << '\n';
  std::cerr D(mtn_options) << '\n';
}
