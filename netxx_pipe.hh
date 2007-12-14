#ifndef __NETXX_PIPE_HH__
#define __NETXX_PIPE_HH__

// Copyright (C) 2007 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2005 Christof Petig <christof@petig-baender.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vector.hh"
#include <netxx/socket.h>
#include <netxx/streambase.h>
#ifdef WIN32
#  include <windows.h>
#endif

/*
  Here we provide children of Netxx::StreamBase that work with stdin/stdout
  in two different ways.

  The use cases are 'mtn sync file:...', 'mtn sync ssh:...', and 'mtn serve
  --stdio'.

  The netsync code uses StreamBase objects to perform all communications
  between the local and server mtns.

  In 'mtn sync file:...', the local mtn spawns the server directly as 'mtn
  serve --stdio'. Thus the local mtn needs a StreamBase object that can
  serve as the stdin/stdout of a spawned process; SpawnedStream. The server
  mtn needs to construct a StreamBase object from the existing stdin/stdout;
  StdioStream.

  In 'mtn sync ssh:...' the local mtn spawns ssh, connecting to it via
  SpawnedStream. On the server, ssh spawns 'mtn serve stdio', which uses
  StdioStream.

  We also need StdioProbe objects that work with StdioStream objects, since
  Netxx::Probe doesn't. Netxx does not provide for child classes of Probe.
  We handle this by having the netsync code create the appropriate Probe
  object whenever it creates a StreamBase object.

  We use socket pairs to implement these objects. On Unix and Win32, a
  socket can serve as stdin and stdout for a spawned process.

  The sockets in the pair must be connected to each other; socketpair() does
  that nicely.

  An earlier implementation (a single class named PipeStream) tried to use
  Win32 overlapped IO via named pipes on Win32, and Unix select with Unix
  pipes on unix, but we couldn't make it work, because the semantics of the
  two implementations are too different. Now we always use socket select on
  sockets.
*/

namespace Netxx
  {
  class StdioProbe;
  class StreamServer;

  class SpawnedStream : public StreamBase
    {
      Socket    Parent_Socket;
      Socket    Child_Socket;
      ProbeInfo probe_info;
#ifdef WIN32
      HANDLE    child;
#else
      pid_t     child;
#endif

    public:
      explicit SpawnedStream (const std::string &cmd, const std::vector<std::string> &args);
      // Spawn a child process to run 'cmd args', connect its stdout and
      // stdin to this object.

      virtual ~SpawnedStream() { close(); }
      virtual signed_size_type read (void *buffer, size_type length);
      virtual signed_size_type write (const void *buffer, size_type length);
      virtual void close (void);
      virtual socket_type get_socketfd (void) const;
      virtual const ProbeInfo* get_probe_info (void) const;
    };

    class StdioStream : public StreamBase
    {
      friend class StdioProbe;

      int       readfd;
      int       writefd;
      ProbeInfo probe_info;

    public:
      explicit StdioStream (void);
      // Construct a Stream object from stdout and stdin of the current
      // process.

      virtual ~StdioStream() { close(); }
      virtual signed_size_type read (void *buffer, size_type length);
      virtual signed_size_type write (const void *buffer, size_type length);
      virtual void close (void);
      virtual socket_type get_socketfd (void) const;
      virtual const ProbeInfo* get_probe_info (void) const;
    };

    struct StdioProbe : Probe
    {
    public:
      // Note that Netxx::Probe::add is a template, not virtual; we provide
      // the versions needed by netsync code.
      void add(const StdioStream &ps, ready_type rt=ready_none);
      void add(const StreamBase &sb, ready_type rt=ready_none);
      void add(const StreamServer &ss, ready_type rt=ready_none);
    };
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __NETXX_PIPE_HH__
