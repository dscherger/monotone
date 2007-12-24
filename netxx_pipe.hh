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
  --stdio'. These are simple on Unix, but pose challenges on Win32.

  The netsync code uses StreamBase objects to perform all communications
  between the local and server mtns.

  In 'mtn sync file:...', the client mtn spawns the server as 'mtn serve
  --stdio'. Thus the client mtn needs a StreamBase object that can serve as
  the stdin/stdout of a spawned process; that is provided by SpawnedStream
  (which also performs the spawn operation). The server mtn needs to
  construct a StreamBase object from the existing stdin/stdout; that is
  provided by StdioStream.

  In 'mtn sync ssh:...' the client mtn spawns ssh, connecting to it via
  SpawnedStream. On the server machine, ssh spawns 'mtn serve --stdio',
  which uses StdioStream.

  The original netsync code performed five operations on streams; read
  available with timeout, write available with timeout, detecting a closed
  connection with timeout, immediate read, and immediate write. For normal
  client-server connections (not via stdio), Netxx::Sockets provides the
  connection and the immediate read/write on both Win32 and Unix (via
  'recv', 'send'), and Netxx::Probe supports read/write available with
  timeout and detect closed connection with timeout on both Win32 and Unix
  (via 'select').

  For stdio connections using SpawnedStream, there are two problem cases
  with sockets. First, closing a server stdio file descriptor that is
  actually a socket does not cause the client 'select' to return with a
  'socket closed' indication on either Unix or Win32. Second, in 'mtn sync
  ssh:...', the spawned ssh process uses the 'read' and 'write' functions on
  stdio; these do not work with sockets on Win32, so the stdio file
  descriptors cannot actually be sockets.

  The first problem can be worked around by changing the netsync protocol
  slightly to not require detecting closed connections; thus 'mtn sync
  file:...' can be provided on Win32 and Unix using a SpawnedStream
  implemented with a socket. However, there are then failure situations
  where the client does not terminate. See the
  'net.venge.monotone.experimental.win32_pipes' branch for an example of
  this.

  However, to support 'mtn sync ssh:...' on Win32 clients, we must use named
  pipes to implement SpawnedStream; they support the 'read' and 'write'
  functions. They do not directly support 'select', but the essential
  operation of a Probe can be supported via overlapped operations.

  We also use pipes on Unix for SpawnedStream, to allow detecting closed
  stdio connections. The original netsync code generally assumes there is
  only one file descriptor for a connection; we modify it where necessary to
  allow for two file descriptors. Note that Win32 named pipes need only one
  file descriptor.

  A Probe object provides three operations; read available, write available,
  and closed connection detection - all with optional timeout. 'select'
  performs all three operations on stdio or sockets Unix, but only on
  sockets on Win32. If a Probe object reports either read or write is
  available within the specified timeout, netsync then performs a read or
  write which is expected to complete immediately. In order to implement the
  timeout without a busy wait, we must use Win32 overlapped operations.

  Overlapped operations can provide a read available with timeout. There is
  no corresponding operation for write; we must simply assume write is
  always available. Since pipes are only used in a SpawnedStream, there are
  two cases where write would not actually be available, and the write call
  would block.

  The first case is when the client is on Win32, and the server (either
  local via 'mtn sync file:...' or remote via 'mtn sync ssh:...') falls far
  behind in reading, so that the pipe buffer fills up. In this case, it is
  safe for the client to block on a write until the server catches up. If
  the connection to the server terminates due to error, the write will
  terminate with an error.

  The second case is a local or remote server on Win32, when the client
  falls far behind in reading. In that case, it is safe for the server to
  block on write until the client catches up, since the server is not
  serving other connections.

  Note that we are assuming it is not possible for both the server and the
  client to fall far enough behind in reading for write to block on both
  sides simultaneously.

  This implementation does not support the case of an ssh server on Win32;
  ssh will spawn mtn with stdio being normal pipes, and PeekNamedPipe will
  fail. In that case, the Cygwin implementation of mtn is recommended.

  Netxx does not provide for child classes of Probe, nor of ProbeInfo; none
  of the methods are virtual. We handle this by having the netsync code
  always use a StdioProbe object; it behaves identically to Probe when the
  connection is a socket. IMPROVEME: should fix Netxx to allow for derived
  classes.

*/

namespace Netxx
  {
  class StdioProbe;
  class StreamServer;

#ifdef WIN32
  class PipeStream
    {
      HANDLE     named_pipe;
      char       readbuf[1024];
      DWORD      bytes_available;
      bool       read_in_progress;
      OVERLAPPED overlap;

    public:
      // Nothing is constructed
      PipeStream (void);

      // Get the named pipe from the existing file descriptor
      explicit PipeStream (int readfd);

      // Use an existing named pipe
      void set_named_pipe (HANDLE named_pipe);

      Netxx::socket_type get_pipefd();
      signed_size_type read (void *buffer, size_type length);
      void close (void);
    };
#endif

  class SpawnedStream : public StreamBase
    {
#ifdef WIN32
      PipeStream pipe;
      HANDLE     child;
#else
      int        readfd;
      int        writefd;
      ProbeInfo  probe_info;
      pid_t      child;
#endif

    public:
      explicit SpawnedStream (const std::string &cmd, const std::vector<std::string> &args);
      // Spawn a child process to run 'cmd args', connect its stdout and
      // stdin to this object.

      virtual ~SpawnedStream() { close(); }
      virtual signed_size_type read (void *buffer, size_type length);
      virtual signed_size_type write (const void *buffer, size_type length);
      virtual void close (void);
      virtual const ProbeInfo* get_probe_info (void) const;

      // get_socketfd will return -1 on Unix
      virtual socket_type get_socketfd (void) const;
      virtual socket_type get_writefd (void) const;
      virtual socket_type get_readfd (void) const;
    };

    class StdioStream : public StreamBase
    {
      friend class StdioProbe;

#ifdef WIN32
      PipeStream readfd;
      int        writefd;
#else
      int        readfd;
      int        writefd;
      ProbeInfo  probe_info;
#endif

    public:
      explicit StdioStream (int const readfd; int const writefd);
      // Construct a Stream object from the given file descriptors

      virtual ~StdioStream() { close(); }
      virtual signed_size_type read (void *buffer, size_type length);
      virtual signed_size_type write (const void *buffer, size_type length);
      virtual void close (void);
      virtual const ProbeInfo* get_probe_info (void) const;

      // get_socketfd will return -1
      virtual socket_type get_socketfd (void) const;
      virtual socket_type get_writefd (void) const;
      virtual socket_type get_readfd (void) const;
    };

    struct StdioProbe : Probe
    {
    public:
      result_type ready (const Timeout &timeout=Timeout(), ready_type rt=ready_none);

      // Note that Netxx::Probe::add is a template, not virtual; we provide
      // the versions needed by netsync code.
      void add(const SpawnedStream &ps, ready_type rt=ready_none);
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
