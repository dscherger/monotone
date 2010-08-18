#ifndef SCED3B84_7C2B_4232_9930_D13432613560
#define SCED3B84_7C2B_4232_9930_D13432613560

// Copyright (C) 2006 Christof Petig <christof@petig-baender.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "netxx_pipe.hh"

class mtn_pipe
{ Netxx::PipeStream *pipe;
  int cmdnum;
  bool first_reaction;
  int format_version;

public:
  mtn_pipe() : pipe(),cmdnum(),first_reaction(),format_version(1) {}
  ~mtn_pipe() { close(); }
  void open(std::string const& command="mtn",
        std::vector<std::string> const& options=std::vector<std::string>());
  void close();
  std::string automate(std::string const& command,
        std::vector<std::string> const& args=std::vector<std::string>())
        throw (std::runtime_error);
  bool is_open() { return pipe; }
};
#endif
