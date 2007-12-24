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

#ifdef WIN32

signed_size_type PipeStream::read (void *buffer, size_type length)
{

} // read

signed_size_type PipeStream::write (const void *buffer, size_type length)
{

} // write

void PipeStream::close (void)
{

} // close

#endif

Netxx::StdioStream::StdioStream (int const readfd; int const writefd)
    :
#ifdef WIN32
  named_pipe (readfd),
  writefd (writefd)
#else
  readfd (readfd),
  writefd (writefd)
#endif
{
#ifndef WIN32
  // This allows Netxx::Probe::ready to call 'select' on these file
  // descriptors. On Win32, 'select' would fail.
  probe_info.add_socket (readfd);
  probe_info.add_socket (writefd);
#endif
}

Netxx::signed_size_type
Netxx::StdioStream::read (void *buffer, size_type length)
{
#ifdef WIN32
  return named_pipe.read (buffer, length);

#else
  // based on netxx/socket.cxx read
  signed_size_type  rc;
  char             *buffer_ptr = static_cast<char*>(buffer);

  for (;;)
    {
      rc = ::read(readfd, buffer, length);

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

      rc = ::write(writefd, buffer, length);

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
  // close stdio so client knows we disconnected
  L(FL("closing StdioStream"));
#ifdef WIN32
  named_pipe.close();
#else
  if (readfd != -1)
    {
      ::close (readfd);
      readfd = -1;
    }
#endif

  if (writefd != -1)
    {
      ::close (writefd);
      writefd = -1;
    }
}

Netxx::socket_type
Netxx::StdioStream::get_socketfd (void) const
{
  return -1;
}

Netxx::socket_type
Netxx::StdioStream::get_readfd (void) const
{
  return readfd;
}

Netxx::socket_type
Netxx::StdioStream::get_writefd (void) const
{
  return writefd;
}

const Netxx::ProbeInfo*
Netxx::StdioStream::get_probe_info (void) const
{
#ifdef Win32
  I(0); // FIXME: not clear what to do here yet
#else
  return &probe_info;
#endif
}

Netxx::SpawnedStream::SpawnedStream (const string & cmd, const vector<string> & args)
  :
#ifdef WIN32
  child(INVALID_HANDLE_VALUE)
#else
  child(-1)
#endif
{
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

#ifdef WIN32

  // Create a named pipe to serve as the child process stdio; it must have a
  // unique name.

  static unsigned long serial = 0;
  string pipename = (F("\\\\.\\pipe\\netxx_pipe_%ld_%d")
                     % GetCurrentProcessId()
                     % (++serial)).str();

  HANDLE named_pipe =
    CreateNamedPipe (pipename.c_str(),
                    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, // dwOpenMode
                    PIPE_TYPE_BYTE | PIPE_WAIT,                // dwPipeMode
                    1,                                         // nMaxInstances
                    readfd.buffer_size(),                      // nOutBufferSize
                    readfd.buffer_size(),                      //nInBufferSize
                    1000,                                      //nDefaultTimeout, milliseconds
                    0);                                        // lpSecurityAttributes

  E(named_pipe != INVALID_HANDLE_VALUE,
    F("CreateNamedPipe(%s,...) call failed: %s")
    % pipename % win32_last_err_msg());

  // Open the child's handle to the named pipe.

  SECURITY_ATTRIBUTES      inherit;
  memset (&inherit,0,sizeof inherit);
  inherit.nLength        = sizeof inherit;
  inherit.bInheritHandle = TRUE;

  HANDLE child_pipe =
    CreateFile(pipename.c_str(),
               GENERIC_READ|GENERIC_WRITE, 0,
               &inherit,
               OPEN_EXISTING,
               FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED,0);

  E(hpipe != INVALID_HANDLE_VALUE,
    F("CreateFile(%s,...) call failed: %s")
    % pipename % win32_last_err_msg());

  // Spawn the child process
  PROCESS_INFORMATION piProcInfo;
  STARTUPINFO         siStartInfo;

  memset(&piProcInfo, 0, sizeof(piProcInfo));
  memset(&siStartInfo, 0, sizeof(siStartInfo));

  siStartInfo.cb          = sizeof(siStartInfo);
  siStartInfo.hStdError   = (HANDLE)(_get_osfhandle(STDERR_FILENO)); // parent stderr
  siStartInfo.hStdOutput  = child_pipe;
  siStartInfo.hStdInput   = child_pipe;
  siStartInfo.dwFlags    |= STARTF_USESTDHANDLES;

  string cmdline = munge_argv_into_cmdline(newargv);
  L(FL("Subprocess command line: '%s'") % cmdline);

  BOOL started =
    CreateProcess(NULL, // Application name
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
      ::close (named_pipe);
      ::close (childe_pipe);
    }

  E(started,
    F("CreateProcess(%s,...) call failed: %s")
    % cmdline % win32_last_err_msg());

  readfd.set_named_pipe (named_pipe);
  writefd = named_pipe;
  child   = piProcInfo.hProcess;

#else // !WIN32

  // Create pipes to serve as stdio for child process

  // These are unidirectional pipes; child_*[0] accepts read, child_*[1] accepts write.
  int child_stdin[2];
  int child_stdout[2];

  E(!pipe(child_stdin),
    F("pipe call failed: %s") % strerror (errno));

  if (pipe(child_stdout))
    {
      ::close(child_stdin[0]);
      ::close(child_stdin[1]);
      E(0, F("pipe call failed: %s") % strerror (errno));
    }

  //  Create the child process

  child = fork();

  if (child < 0)
    {
      // fork failed
      ::close (child_stdin[0]);
      ::close (child_stdin[1]);
      ::close (child_stdout[0]);
      ::close (child_stdout[1]);

      E(0, F("fork failed %s") % strerror(errno));
    }

  if (!child)
    {
      // We are in the child process; run the command, then exit.

      // Close the end of the pipes not used in the child
      ::close (child_stdin[1]);
      ::close (child_stdout[0]);

      // Replace our stdin, stdout with the pipes
      if (dup2 (child_stdin[0], STDIN_FILENO) != STDIN_FILENO ||
          dup2(child_stdout[1], STDOUT_FILENO) != STDOUT_FILENO)
        {
          // We don't have the mtn error handling infrastructure yet.
          perror("dup2 failed");
          exit(-1);
        }

      // close our old stdin, stdout
      ::close (child_stdin[0]);
      ::close (child_stdout[1]);

      // Run the command
      execvp(newargv[0], const_cast<char * const *>(newargv));
      perror(newargv[0]);
      exit(errno);
    }

  else
    {
      // we are in the parent process
      readfd  = child_stdout[0];
      writefd = child_stdin[1];
    }
#endif
}

Netxx::signed_size_type
Netxx::SpawnedStream::read (void *buffer, size_type length)
{
#ifdef WIN32
  return readfd.read (buffer, length);
#else
  return ::read (readfd, buffer, length);
#endif
}

Netxx::signed_size_type
Netxx::SpawnedStream::write (const void *buffer, size_type length)
{
#ifdef WIN32
  return writefd.write (buffer, length);
#else
  return ::write (writefd, buffer, length);
#endif
}

void
Netxx::SpawnedStream::close (void)
{
  // We need to wait for the child to exit, so it reads our last message,
  // releases the database, closes redirected output files etc. before we do
  // whatever is next.
  L(FL("waiting for spawned child ..."));
#ifdef WIN32
  if (child != INVALID_HANDLE_VALUE)
    WaitForSingleObject(child, INFINITE);
  child = INVALID_HANDLE_VALUE;

  readfd.close();
  writefd = -1;
#else
  if (child != -1)
    while (waitpid(child,0,0) == -1 && errno == EINTR);
  child = -1;

  if (readfd != -1)
    ::close(readfd);
  readfd = -1;

  if (writefd != -1)
    ::close(writefd);
  writefd = -1;
#endif
  L(FL("waiting for spawned child ... done"));
}

Netxx::socket_type
Netxx::SpawnedStream::get_socketfd (void) const
{
#ifdef WIN32
  return pipe.get_pipefd ();
#else
  return -1;
#endif;
}

Netxx::socket_type
Netxx::SpawnedStream::get_readfd (void) const
{
#ifdef WIN32
  return -1;
#else
  return readfd;
#endif;
}

Netxx::socket_type
Netxx::SpawnedStream::get_writefd (void) const
{
#ifdef WIN32
  return -1;
#else
  return writefd;
#endif;
}

const Netxx::ProbeInfo*
Netxx::SpawnedStream::get_probe_info (void) const
{
#ifdef WIN32
  I(0);
#else
  return &probe_info;
#endif
}

void
Netxx::StdioProbe::ready(const StreamBase &sb, ready_type rt)
{
  try
    {
      // See if it's a StdioStream
      add(const_cast<StdioStream&>(dynamic_cast<const StdioStream&>(sb)),rt);
    }
  catch (...)
    {
      // Not a StdioStream; see if it's a SpawnedStream. YUCK! fix Netxx::Probe to be dispatching.
      Probe::add(sb,rt);
    }
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
  I(probe_result.first == stream.get_readfd());

  bytes_read = stream.read (stream_read_buffer, sizeof(stream_read_buffer));
  I(bytes_read == 2);
  I(stream_read_buffer[0] == 42);
  I(stream_read_buffer[1] == 43);

  // test StdioStream write
  probe.clear();
  probe.add(stream, Netxx::Probe::ready_write);
  probe_result = probe.ready(short_time);
  I(probe_result.second & Netxx::Probe::ready_write);
  I(probe_result.first == stream.get_writefd());

  bytes_written = stream.write (write_buffer, 2);
  I(bytes_written == 2);

  bytes_read = ::recv (socks[1], parent_read_buffer, sizeof(parent_read_buffer), 0);

  I(bytes_read == 2);
  I(parent_read_buffer[0] == 42);
  I(parent_read_buffer[1] == 43);

  // test StdioStream close; recv on parent should detect disconnect, not block
  stream.close();
  bytes_read = ::recv (socks[1], parent_read_buffer, sizeof(parent_read_buffer), 0);

  I(bytes_read == 0);

}

void unit_test_spawn (char *cmd, int will_disconnect)
{
  try
    {
      Netxx::SpawnedStream spawned (cmd, vector<string>());

      char           write_buf[1024];
      char           read_buf[1024];
      int            bytes;
      Netxx::Probe   probe;
      Netxx::Timeout timeout(2L), short_time(0,1000);

      // time out because no data is available
      probe.clear();
      probe.add(spawned, Netxx::Probe::ready_read);
      Netxx::Probe::result_type res = probe.ready(short_time);
      I(res.second==Netxx::Probe::ready_none);
      I(res.first == -1);

      // write should be possible
      probe.clear();
      probe.add(spawned, Netxx::Probe::ready_write);
      res = probe.ready(short_time);
      I(res.second & Netxx::Probe::ready_write);
      I(res.first==spawned.get_socketfd());

      // test binary transparency, lots of cycles
      for (int c = 0; c < 256; ++c)
        {
          string result;
          write_buf[0] = c;
          write_buf[1] = 255 - c;
          spawned.write(write_buf, 2);

          while (result.size() < 2)
            { // wait for data to arrive
              probe.clear();
              probe.add(spawned, Netxx::Probe::ready_read);
              res = probe.ready(timeout);
              E(res.second & Netxx::Probe::ready_read, F("timeout reading data %d") % c);
              I(res.first == spawned.get_socketfd());

              bytes = spawned.read(read_buf, sizeof(read_buf));
              result += string(read_buf, bytes);
            }
          I(result.size() == 2);
          I(static_cast<unsigned char>(result[0]) == c);
          I(static_cast<unsigned char>(result[1]) == 255 - c);
        }

      if (will_disconnect)
        {
          // Tell netxx_pipe_stdio_main to quit, closing its stdin
          write_buf[0] = 'q';
          write_buf[1] = 'u';
          write_buf[2] = 'i';
          write_buf[3] = 't';
          spawned.write(write_buf, 4);

          // Wait for stdin to close; should be reported as ready_read, _not_ as timeout
          probe.clear();
          probe.add(spawned, Netxx::Probe::ready_read);
          res = probe.ready(timeout);
          I(res.second==Netxx::Probe::ready_read);
          I(res.first == spawned.get_socketfd());

          // now read should return 0 bytes
          bytes = spawned.read(read_buf, sizeof(read_buf));
          I(bytes == 0);
        }

      // This waits for the child to exit
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
  // We must be able to spawn a "normal" program, that uses 'read' and
  // 'write' on stdio; the real use case is 'mtn sync ssh:...'.
  unit_test_spawn ("cat", 0);
}

UNIT_TEST(pipe, spawn_stdio)
{
  // We must also be able to spawn a program that uses StdioStream; this
  // also tests StdioStream.
  unit_test_spawn ("./netxx_pipe_stdio_main", 1);
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
