// Copyright (C) 2007 Justin Patrin <papercrane@reversefold.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "sanity.hh"
#include "ssh_agent_platform.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#ifndef MSG_WAITALL
#define MSG_WAITALL 0
#endif

using std::min;
using std::string;

// helper function for constructor
static int
connect_to_agent()
{
  const char *authsocket = getenv("SSH_AUTH_SOCK");
  
  if (!authsocket || !*authsocket)
    {
      L(FL("ssh_agent: no agent"));
      return -1;
    }

  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0)
    {
      W(F("ssh_agent: failed to create a socket: %s")
	% strerror(errno));
      return -1;
    }
  if (fcntl(sock, F_SETFD, FD_CLOEXEC))
    {
      close(sock);
      W(F("ssh_agent: failed to set socket as close-on-exec: %s")
	% strerror(errno));
      return -1;
    }

  struct sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, authsocket, sizeof(addr.sun_path));

  if (::connect(sock, (struct sockaddr *)&addr, sizeof addr))
    {
      close(sock);
      W(F("ssh_agent: failed to connect to agent: %s")
	% strerror(errno));
      return -1;
    }

  return sock;
}

ssh_agent_platform::ssh_agent_platform()
  : sock(connect_to_agent())
{}

ssh_agent_platform::~ssh_agent_platform()
{
  close(sock);
}

void
ssh_agent_platform::write_data(string const & data)
{
  I(connected());

  size_t put = data.length();
  const char *buf = data.data();
  int deadcycles = 0;

  L(FL("ssh_agent: write_data: asked to send %u bytes") % put);
  while (put > 0)
    {
      ssize_t sent = ::send(sock, buf, put, MSG_NOSIGNAL);

      E(sent >= 0, origin::system,
	F("ssh_agent: error during send: %s") % strerror(errno));
      if (sent == 0)
	E(++deadcycles < 8, origin::system,
	  F("ssh_agent: giving up after %d ineffective sends to agent")
	  % deadcycles);

      buf += sent;
      put -= sent;
    }
  E(put == 0, origin::system,
    F("ssh_agent: sent %u extra bytes to agent") % -put);
}

void 
ssh_agent_platform::read_data(string::size_type len, string & out)
{
  I(connected());

  size_t get = len;
  const size_t bufsize = 4096;
  char buf[bufsize];
  int deadcycles = 0;

  L(FL("ssh_agent: read_data: asked to read %u bytes") % len);

  while (get > 0)
    {
      ssize_t recvd = ::recv(sock, buf, min(get, bufsize), MSG_WAITALL);

      E(recvd >= 0, origin::system,
	F("ssh_agent: error during recieve: %s") % strerror(errno));
      if (recvd == 0)
	E(++deadcycles < 8, origin::system,
	  F("ssh_agent: giving up after %d ineffective receives from agent"));

      out.append(buf, recvd);
      get -= recvd;
    }
  E(get == 0, origin::system,
    F("ssh_agent: received %u extra bytes from agent") % -get);
}
