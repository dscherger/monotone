// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "mtn_pipe.hh"
#include <iterator>

void mtn_pipe::open(std::string const& database, std::vector<std::string> const& options)
{ std::vector<std::string> args;
  args.push_back("--db="+database);
  std::copy(options.begin(),options.end(),std::back_inserter(args));
  args.push_back("automate");
  args.push_back("stdio");
  pipe=new Netxx::PipeStream("mtn",args);
}

void mtn_pipe::close()
{ if (pipe)
  { delete pipe;
    pipe=0;
  }
}
