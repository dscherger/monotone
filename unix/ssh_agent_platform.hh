// Copyright (C) 2007 Justin Patrin <papercrane@reversefold.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __SSH_AGENT_PLATFORM_HH__
#define __SSH_AGENT_PLATFORM_HH__

class ssh_agent_platform {
private:
  int sock;

public:
  ssh_agent_platform();
  ~ssh_agent_platform();
  bool connected() { return sock != -1; }

  void write_data(std::string const & data);
  void read_data(std::string::size_type len, std::string & out);
};

#endif
