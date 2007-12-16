// Copyright (C) 2007 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2005 Christof Petig <christof@petig-baender.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <netxx_pipe.hh>
#include "sanity.hh"
#include "platform.hh"
#include <netxx/osutil.h>
#include <netxx/streamserver.h>
#include <ostream> // for operator<<

#ifdef WIN32
#include <windows.h>
#include <winsock2.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <errno.h>
#endif

using std::vector;
using std::string;
using std::make_pair;
using std::exit;
using std::perror;
using std::strerror;

Netxx::StdioStream::StdioStream(void)
    :
#ifdef WIN32
  readfd ((int)GetStdHandle(STD_INPUT_HANDLE)),
  writefd ((int)GetStdHandle(STD_OUTPUT_HANDLE))
#else
  readfd (STDIN_FILENO),
  writefd (STDOUT_FILENO)
#endif
{
  // This allows netxx to call select() on these file descriptors. On Win32,
  // this will fail unless they are actually a socket (ie, we are spawned
  // from 'mtn sync file:...', or some other program that sets stdin, stdout
  // to a socket).
  probe_info.add_socket (readfd);
  probe_info.add_socket (writefd);

#ifdef WIN32
  {
    WSADATA wsdata;
    if (WSAStartup(MAKEWORD(2,2), &wsdata) != 0)
      L(FL("failed to load WinSock"));
  }

#endif
}

Netxx::signed_size_type
Netxx::StdioStream::read (void *buffer, size_type length)
{
  // based on netxx/socket.cxx read
  signed_size_type  rc;
  char             *buffer_ptr = static_cast<char*>(buffer);

  for (;;)
    {
#ifdef WIN32
      // readfd must be a socket, and 'read' doesn't work on sockets
      rc = recv(readfd, buffer_ptr, length, 0);
#else
      // On Unix, this works for sockets as well as files and pipes.
      rc = ::read(readfd, buffer, length);
#endif
      if (rc < 0)
        {
          error_type error_code = get_last_error();
          if (error_code == EWOULDBLOCK) error_code = EAGAIN;

          switch (error_code) {
          case ECONNRESET:
            return 0;

          case EINTR:
            continue;

          case EAGAIN:
            return -1;

#ifdef WIN32

          case WSAEMSGSIZE:
            return length;

          case WSAENETRESET:
          case WSAESHUTDOWN:
          case WSAECONNABORTED:
          case WSAETIMEDOUT: // timed out shouldn't happen
            return 0;

#endif

          default:
            {
              std::string error("recv failure: ");
              error += str_error(error_code);
              throw Exception(error);
            }
          }
	}

      break;
    }

    return rc;
}

Netxx::signed_size_type
Netxx::StdioStream::write (const void *buffer, size_type length)
{
  // based on netxx/socket.cxx write
  const char *buffer_ptr = static_cast<const char*>(buffer);
  signed_size_type rc, bytes_written=0;

  while (length)
    {

#ifdef WIN32
      // writefd must be a socket, and 'write' doesn't work on sockets
      rc = send(writefd, buffer_ptr, length, 0);
#else
      // On Unix, this works for sockets as well as files and pipes.
      rc = ::write(writefd, buffer, length);
#endif
      if (rc < 0)
        {
          Netxx::error_type error_code = get_last_error();
          if (error_code == EWOULDBLOCK) error_code = EAGAIN;

          switch (error_code) {
          case EPIPE:
          case ECONNRESET:
            return 0;

          case EINTR:
            continue;

          case EAGAIN:
            return -1;

#if defined(WIN32)
          case WSAENETRESET:
          case WSAESHUTDOWN:
          case WSAEHOSTUNREACH:
          case WSAECONNABORTED:
          case WSAETIMEDOUT:
            return 0;
#endif

          default:
            {
              std::string error("send failed: ");
              error += str_error(error_code);
              throw Exception(error);
            }
          }
        }

      buffer_ptr    += rc;
      bytes_written += rc;
      length        -= rc;
    }

  return bytes_written;
}

void
Netxx::StdioStream::close (void)
{
  // nothing to do here
}

Netxx::socket_type
Netxx::StdioStream::get_socketfd (void) const
{
  // This is used netsync only to register the session for deletion, so it
  // doesn't matter whether we return readfd or writefd. The unit test needs
  // readfd in netxx_pipe_stdio_main.cc
  return readfd;
}

const Netxx::ProbeInfo*
Netxx::StdioStream::get_probe_info (void) const
{
  return &probe_info;
}

void
Netxx::StdioStream::set_socketfd (socket_type sock)
{
  readfd = sock;
  writefd = sock;

  probe_info.clear();
  probe_info.add_socket (readfd);
  probe_info.add_socket (writefd);

  // We don't set binary on WIN32, because it is not necessary for send/recv
  // on a socket.
}

Netxx::SpawnedStream::SpawnedStream (const string & cmd, const vector<string> & args)
  :
#ifdef WIN32
  child(INVALID_HANDLE_VALUE)
#else
  child(-1)
#endif
{
  socket_type socks[2]; // 0 is for child, 1 is for parent

  // Unfortunately neither munge_argv_into_cmdline nor execvp take
  // a vector<string> as argument.

  const unsigned newsize = 64;
  const char *newargv[newsize];
  I(args.size() < (sizeof(newargv) / sizeof(newargv[0])));

  unsigned newargc = 0;
  newargv[newargc++]=cmd.c_str();
  for (vector<string>::const_iterator i = args.begin();
       i != args.end(); ++i)
    newargv[newargc++] = i->c_str();
  newargv[newargc] = 0;

  E(0 == dumb_socketpair (socks, 0), F("socketpair failed"));

  Child_Socket.set_socketfd (socks[0]);
  Parent_Socket.set_socketfd (socks[1]);

  probe_info.add_socket (socks[1]);

#ifdef WIN32

  PROCESS_INFORMATION piProcInfo;
  STARTUPINFO siStartInfo;

  memset(&piProcInfo, 0, sizeof(piProcInfo));
  memset(&siStartInfo, 0, sizeof(siStartInfo));

  siStartInfo.cb          = sizeof(siStartInfo);
  siStartInfo.hStdError   = (HANDLE)(_get_osfhandle(2));
  siStartInfo.hStdOutput  = (HANDLE)socks[0];
  siStartInfo.hStdInput   = (HANDLE)socks[0];
  siStartInfo.dwFlags    |= STARTF_USESTDHANDLES;

  string cmdline = munge_argv_into_cmdline(newargv);
  L(FL("Subprocess command line: '%s'") % cmdline);

  BOOL started = CreateProcess(NULL, // Application name
                               const_cast<CHAR*>(cmdline.c_str()),
                               NULL, // Process attributes
                               NULL, // Thread attributes
                               TRUE, // Inherit handles
                               0,    // Creation flags
                               NULL, // Environment
                               NULL, // Current directory
                               &siStartInfo,
                               &piProcInfo);

  if (!started)
    {
      closesocket(socks[0]);
      closesocket(socks[1]);
    }

  E(started,
    F("CreateProcess(%s,...) call failed: %s")
    % cmdline % win32_last_err_msg());

  child = piProcInfo.hProcess;

#else // !WIN32

  child = fork();

  if (child < 0)
    {
      // fork failed
      ::close (socks[0]);
      ::close (socks[1]);

      E(0, F("fork failed %s") % strerror(errno));
    }

  if (!child)
    {
      // We are in the child process; run the command, then exit.

      int old_stdio[2];

      // Set the child socket as stdin and stdout. dup2 clobbers its first
      // arg, so copy it first.

      old_stdio[0] = socks[0];
      old_stdio[1] = socks[0];

      if (dup2(old_stdio[0], 0) != 0 ||
          dup2(old_stdio[1], 1) != 1)
        {
          // We don't have the mtn error handling infrastructure here.
          perror("dup2 failed");
          exit(-1);
        }

      // old_stdio now holds the file descriptors for our old stdin, stdout, so close them
      ::close (old_stdio[0]);
      ::close (old_stdio[1]);

      execvp(newargv[0], const_cast<char * const *>(newargv));
      perror(newargv[0]);
      exit(errno);
    }
  // else we are in the parent process; continue.

#endif
}

Netxx::signed_size_type
Netxx::SpawnedStream::read (void *buffer, size_type length)
{
  return Parent_Socket.read (buffer, length, get_timeout());
}

Netxx::signed_size_type
Netxx::SpawnedStream::write (const void *buffer, size_type length)
{
  return Parent_Socket.write (buffer, length, get_timeout());
}

void
Netxx::SpawnedStream::close (void)
{
  // We assume the child process has exited
  Child_Socket.close();
  Parent_Socket.close();
}

Netxx::socket_type
Netxx::SpawnedStream::get_socketfd (void) const
{
  return Parent_Socket.get_socketfd ();
}

const Netxx::ProbeInfo*
Netxx::SpawnedStream::get_probe_info (void) const
{
  return &probe_info;
}

void
Netxx::StdioProbe::add(const StdioStream &ps, ready_type rt)
  {
    if (rt == ready_none || rt & ready_read)
      add_socket(ps.readfd, ready_read);
    if (rt == ready_none || rt & ready_write)
      add_socket(ps.writefd, ready_write);
  }

void
Netxx::StdioProbe::add(const StreamBase &sb, ready_type rt)
{
  try
    {
      add(const_cast<StdioStream&>(dynamic_cast<const StdioStream&>(sb)),rt);
    }
  catch (...)
    {
      Probe::add(sb,rt);
    }
}

void
Netxx::StdioProbe::add(const StreamServer &ss, ready_type rt)
{
  // Should not be a StdioStream here
  Probe::add(ss,rt);
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

namespace Netxx
{
  class StdioStreamTest : public StdioStream
  {
  public:
    StdioStreamTest (void) : StdioStream (1) {};
    void set_socket (socket_type sock);
  };
}

void
Netxx::StdioStreamTest::set_socket (socket_type sock)
{
  this->set_socketfd (sock);
}

UNIT_TEST(pipe, stdio_stream)
{
  Netxx::StdioStreamTest    stream;
  Netxx::StdioProbe         probe;
  Netxx::Probe::result_type probe_result;
  Netxx::Timeout            short_time(0,1000);
  Netxx::Timeout            timeout(2L);

  char                    write_buffer[2];
  char                    stream_read_buffer[2];
  char                    parent_read_buffer[2];
  Netxx::signed_size_type bytes_read;
  Netxx::signed_size_type bytes_written;

  Netxx::socket_type socks[2];

#ifdef WIN32
  {
    WSADATA wsdata;
    if (WSAStartup(MAKEWORD(2,2), &wsdata) != 0)
      L(FL("failed to load WinSock"));
  }
#endif

  E(0 == dumb_socketpair (socks, 0), F("socketpair failed"));

  // Test StdioStream read and write
  stream.set_socket (socks[0]);

  // test read time out
  probe.clear();
  probe.add(stream, Netxx::Probe::ready_read);
  probe_result = probe.ready(short_time);
  I(probe_result.first == -1); // timeout
  I(probe_result.second == Netxx::Probe::ready_none);

  // test StdioStream read
  write_buffer[0] = 42;
  write_buffer[1] = 43;

  bytes_written = ::send (socks[1], write_buffer, 2, 0);
  I(bytes_written == 2);

  probe.clear();
  probe.add(stream, Netxx::Probe::ready_read);
  probe_result = probe.ready(short_time);
  I(probe_result.second == Netxx::Probe::ready_read);
  I(probe_result.first == stream.get_socketfd());

  bytes_read = stream.read (stream_read_buffer, sizeof(stream_read_buffer));
  I(bytes_read == 2);
  I(stream_read_buffer[0] == 42);
  I(stream_read_buffer[1] == 43);

  // test StdioStream write
  probe.clear();
  probe.add(stream, Netxx::Probe::ready_write);
  probe_result = probe.ready(short_time);
  I(probe_result.second & Netxx::Probe::ready_write);
  I(probe_result.first == stream.get_socketfd());

  bytes_written = stream.write (write_buffer, 2);
  I(bytes_written == 2);

  bytes_read = ::recv (socks[1], parent_read_buffer, sizeof(parent_read_buffer), 0);

  I(bytes_read == 2);
  I(parent_read_buffer[0] == 42);
  I(parent_read_buffer[1] == 43);
}

void unit_test_spawn (char *cmd)
{ try
  {
  // netxx_pipe_stdio_main uses StdioStream, StdioProbe
  Netxx::SpawnedStream spawned (cmd, vector<string>());

  string result;
  Netxx::Probe probe;
  Netxx::Timeout timeout(2L), short_time(0,1000);

  // time out because no data is available
  probe.clear();
  probe.add(spawned, Netxx::Probe::ready_read);
  Netxx::Probe::result_type res = probe.ready(short_time);
  I(res.second==Netxx::Probe::ready_none);

  // write should be possible
  probe.clear();
  probe.add(spawned, Netxx::Probe::ready_write);
  res = probe.ready(short_time);
  I(res.second & Netxx::Probe::ready_write);
  I(res.first==spawned.get_socketfd());

  // test binary transparency, lots of cycles
  for (int c = 0; c < 256; ++c)
    {
      char write_buf[1024];
      char read_buf[1024];
      write_buf[0] = c;
      write_buf[1] = 255 - c;
      spawned.write(write_buf, 2);

      string result;
      while (result.size() < 2)
        { // wait for data to arrive
          probe.clear();
          probe.add(spawned, Netxx::Probe::ready_read);
          res = probe.ready(timeout);
          E(res.second & Netxx::Probe::ready_read, F("timeout reading data %d") % c);
          I(res.first == spawned.get_socketfd());

          int bytes = spawned.read(read_buf, sizeof(read_buf));
          result += string(read_buf, bytes);
        }
      I(result.size() == 2);
      I(static_cast<unsigned char>(result[0]) == c);
      I(static_cast<unsigned char>(result[1]) == 255 - c);
    }

  spawned.close();

  }
  catch (informative_failure &e)
  {
    W(F("Failure %s") % e.what());
    throw;
  }
}

UNIT_TEST(pipe, spawn_cat)
{
  unit_test_spawn ("cat");
}

UNIT_TEST(pipe, spawn_stdio)
{
  unit_test_spawn ("./netxx_pipe_stdio_main");
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
