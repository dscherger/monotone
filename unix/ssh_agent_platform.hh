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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
