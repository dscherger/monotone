// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <netxx_pipe.hh>
#include "sanity.hh"
#include "platform.hh"
#include <netxx/streamserver.h>

Netxx::PipeStream::PipeStream(int _readfd, int _writefd)
    : readfd(_readfd), writefd(_writefd), child()
{}

#ifndef __WIN32__
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

// create pipes for stdio and fork subprocess
static pid_t
pipe_and_fork(int *fd1,int *fd2)
{
  pid_t result=-1;
  fd1[0]=-1;
  fd1[1]=-1;
  fd2[0]=-1;
  fd2[1]=-1;
  if (pipe(fd1))
    return -1;
  if (pipe(fd2))
    {
      close(fd1[0]);
      close(fd1[1]);
      return -1;
    }
  result=fork();
  if (result<0)
    {
      close(fd1[0]);
      close(fd1[1]);
      close(fd2[0]);
      close(fd2[1]);
      return -1;
    }
  else if (!result)
    { // fd1[1] for writing, fd2[0] for reading
      close(fd1[0]);
      close(fd2[1]);
      if (dup2(fd2[0],0)!=0 || dup2(fd1[1],1)!=1)
        {
          perror("dup2");
          exit(-1); // kill the useless child
        }
      close(fd1[1]);
      close(fd2[0]);
    }
  else
    { // fd1[0] for reading, fd2[1] for writing
      close(fd1[1]);
      close(fd2[0]);
    }
  return result;
}
#endif

#ifdef WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#define FAIL_IF(FUN,ARGS,CHECK) \
  E(!(FUN ARGS CHECK), F(#FUN " failed %d\n") % GetLastError())
#endif

Netxx::PipeStream::PipeStream (const std::string &cmd, const std::vector<std::string> &args)
    : readfd(), writefd(), child()
{
  const unsigned newsize=64;
  const char *newargv[newsize];
  I(args.size()<(sizeof(newargv)/sizeof(newargv[0])));
  unsigned newargc=0;
  newargv[newargc++]=cmd.c_str();
  for (std::vector<std::string>::const_iterator i=args.begin();i!=args.end();++i)
    newargv[newargc++]=i->c_str();
  newargv[newargc]=0;
#ifdef WIN32

  int fd1[2],fd2[2];
  fd1[0]=-1;
  fd1[1]=-1;
  fd2[0]=-1;
  fd2[1]=-1;
  E(_pipe(fd1,0,_O_BINARY)==0, F("first pipe failed"));
  // there are ways to ensure that the parent side does not get inherited
  // by the child (e.g. O_NOINHERIT), I don't use them for now
  if (_pipe(fd2,0,_O_BINARY))
    { ::close(fd1[0]);
      ::close(fd1[1]);
      E(false,F("second pipe failed"));
    }
  PROCESS_INFORMATION piProcInfo;
  STARTUPINFO siStartInfo;
  memset(&piProcInfo,0,sizeof piProcInfo);
  memset(&siStartInfo,0,sizeof siStartInfo);
  siStartInfo.cb = sizeof siStartInfo;
  siStartInfo.hStdError = (HANDLE)_get_osfhandle(2);
  siStartInfo.hStdOutput = (HANDLE)_get_osfhandle(fd1[1]);
  siStartInfo.hStdInput = (HANDLE)_get_osfhandle(fd2[0]);
  siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
  // unfortunately munge_argv_into_cmdline does not take a vector<string>
  // as its argument
  std::string cmdline=munge_argv_into_cmdline(newargv);
  L(F("cmdline '%s'\n") % cmdline);
  FAIL_IF(CreateProcess,(0,const_cast<CHAR*>(cmdline.c_str()),
                         0,0,TRUE,0,0,0,&siStartInfo,&piProcInfo),==0);
  ::close(fd1[1]);
  ::close(fd2[0]);
  child=long(piProcInfo.hProcess);
  readfd=fd1[0];
  writefd=fd2[1];

  memset(&overlap,0,sizeof overlap);
  overlap.hEvent=CreateEvent(0,FALSE,FALSE,0);
  bytes_available=0;
  I(overlap.hEvent!=0);
#else

  int fd1[2],fd2[2];
  child=pipe_and_fork(fd1,fd2);
  E(child>=0, F("pipe/fork failed %s") % strerror(errno));
  if (!child)
    {
      execvp(newargv[0],const_cast<char*const*>(newargv));
      perror(newargv[0]);
      exit(errno);
    }
  readfd=fd1[0];
  writefd=fd2[1];
  fcntl(readfd,F_SETFL,fcntl(readfd,F_GETFL)|O_NONBLOCK);
#endif
}

Netxx::signed_size_type Netxx::PipeStream::read (void *buffer, size_type length)
{
#ifdef WIN32
  if (length>bytes_available)
    length=bytes_available;
  if (length)
    {
      memcpy(buffer,readbuf,length);
      if (length<bytes_available)
        memmove(readbuf,readbuf+length,bytes_available-length);
      bytes_available-=length;
    }
  return length;
#else

  return ::read(readfd,buffer,length);
#endif
}

Netxx::signed_size_type Netxx::PipeStream::write(const void *buffer, size_type length)
{
  return ::write(writefd,buffer,length);
}

void Netxx::PipeStream::close (void)
{
  ::close(readfd);
  ::close(writefd);
  // wait for Process to end
#ifdef WIN32

  WaitForSingleObject((HANDLE)child, INFINITE);
#else

  if (child)
    waitpid(child,0,0);
#endif
}

Netxx::socket_type Netxx::PipeStream::get_socketfd (void) const
  {
    return Netxx::socket_type(-1);
  }

const Netxx::ProbeInfo* Netxx::PipeStream::get_probe_info (void) const
  {
    return 0;
  }

#ifdef WIN32


// to emulate the semantics of the select call we wait up to timeout for the
// first byte and ask for more bytes with no timeout
//   perhaps there is a more efficient/less complicated way (tell me if you know)
Netxx::Probe::result_type Netxx::PipeCompatibleProbe::ready(const Timeout &timeout, ready_type rt)
{
  if (!is_pipe)
    return Probe::ready(timeout,rt);
  if (rt==ready_none)
    rt=ready_t; // remembered from add
  if (rt&ready_write)
    return std::make_pair(pipe->get_writefd(),ready_write);
  if (rt&ready_read)
    {
      if (pipe->bytes_available)
        return std::make_pair(pipe->get_readfd(),ready_read);

      HANDLE h_read=(HANDLE)_get_osfhandle(pipe->get_readfd());
      DWORD bytes_read=0;
      FAIL_IF( ReadFile,(h_read,pipe->readbuf,1,&bytes_read,&pipe->overlap),==0);
      if (!bytes_read)
        {
	  int seconds=timeout.get_sec();
	  // WaitForSingleObject is inaccurate
	  if (!seconds && timeout.get_usec()) seconds=1;
	  L(F("WaitForSingleObject(,%d)\n") % seconds);
          FAIL_IF( WaitForSingleObject,(pipe->overlap.hEvent,seconds),==WAIT_FAILED);
          FAIL_IF( GetOverlappedResult,(h_read,&pipe->overlap,&bytes_read,FALSE),==0);
	  L(F("GetOverlappedResult(,,%d,)\n") % bytes_read);
          if (!bytes_read)
            {
              FAIL_IF( CancelIo,(h_read),==0);
              std::make_pair(socket_type(-1),ready_none);
            }
        }
      I(bytes_read==1);
      pipe->bytes_available=bytes_read;
      L(F("ReadFile\n"));
      FAIL_IF( ReadFile,(h_read,pipe->readbuf+1,sizeof pipe->readbuf-1,&bytes_read,&pipe->overlap),==0);
      L(F("CancelIo\n"));
      FAIL_IF( CancelIo,(h_read),==0);
      if (!bytes_read)
        {
          FAIL_IF( GetOverlappedResult,(h_read,&pipe->overlap,&bytes_read,FALSE),==0);
          I(!bytes_read);
        }
      else
        {
          pipe->bytes_available+=bytes_read;
        }
      return std::make_pair(pipe->get_readfd(),ready_read);
    }
  return std::make_pair(socket_type(-1),ready_none);
}

void Netxx::PipeCompatibleProbe::add(PipeStream &ps, ready_type rt)
  {
    assert(!is_pipe);
    assert(!pipe);
    is_pipe=true;
    pipe=&ps;
    ready_t=rt;
  }

void Netxx::PipeCompatibleProbe::add(const StreamBase &sb, ready_type rt)
  { 
    try
      {
        add(const_cast<PipeStream&>(dynamic_cast<const PipeStream&>(sb)),rt);
      }
    catch (...)
      {
        assert(!is_pipe);
        Probe::add(sb,rt);
      }
  }

void Netxx::PipeCompatibleProbe::add(const StreamServer &ss, ready_type rt)
  {
    assert(!ip_pipe);
    Probe::add(ss,rt);
  }
#else // unix
void
Netxx::PipeCompatibleProbe::add(PipeStream &ps, ready_type rt)
  {
    if (rt==ready_none || rt&ready_read)
      add_socket(ps.get_readfd(),ready_read);
    if (rt==ready_none || rt&ready_write)
      add_socket(ps.get_writefd(),ready_write);
  }

void
Netxx::PipeCompatibleProbe::add(const StreamBase &sb, ready_type rt)
  {
    try
      {
        add(const_cast<PipeStream&>(dynamic_cast<const PipeStream&>(sb)),rt);
      }
    catch (...)
      {
        Probe::add(sb,rt);
      }
  }

void
Netxx::PipeCompatibleProbe::add(const StreamServer &ss, ready_type rt)
  {
    Probe::add(ss,rt);
  }
#endif

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void
simple_pipe_test()
{
  Netxx::PipeStream pipe("cat",std::vector<std::string>());

  std::string result;
  Netxx::PipeCompatibleProbe probe;
  Netxx::Timeout timeout(2L), short_time(0,500);

  // time out because no data is available
  probe.clear();
  probe.add(pipe, Netxx::Probe::ready_read);
  Netxx::Probe::result_type res = probe.ready(short_time);
  I(res.second==Netxx::Probe::ready_none);

  // write should be possible
  probe.clear();
  probe.add(pipe, Netxx::Probe::ready_write);
  res = probe.ready(short_time);
  I(res.second&Netxx::Probe::ready_write);
  I(res.first==pipe.get_writefd());

  // try binary transparency
  for (int c=0; c<256; ++c)
    {
      char buf[1024];
      buf[0]=c;
      buf[1]=255-c;
      pipe.write(buf,2);

      std::string result;
      while (result.size()<2)
        { // wait for data to arrive
          probe.clear();
          probe.add(pipe, Netxx::Probe::ready_read);
          res = probe.ready(timeout);
          E(res.second&Netxx::Probe::ready_read, F("timeout reading data %d") % c);
          I(res.first==pipe.get_readfd());
          int bytes=pipe.read(buf,sizeof buf);
          result+=std::string(buf,bytes);
        }
      I(result.size()==2);
      I((unsigned char)(result[0])==c);
      I((unsigned char)(result[1])==255-c);
    }
  pipe.close();
}

void
add_pipe_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&simple_pipe_test));
}
#endif
