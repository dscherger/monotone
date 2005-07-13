// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <netxx_pipe.hh>
#include "sanity.hh"

Netxx::PipeStream::PipeStream(int _readfd, int _writefd)
  : readfd(_readfd), writefd(_writefd), child()
{ pi_.add_socket(readfd);
  pi_.add_socket(writefd);
}

#ifndef __WIN32__
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

// copied from netsync.cc from the ssh branch
static pid_t pipe_and_fork(int *fd1,int *fd2)
{ pid_t result=-1;
  fd1[0]=-1; fd1[1]=-1;
  fd2[0]=-1; fd2[1]=-1;
  if (pipe(fd1)) return -1;
  if (pipe(fd2)) 
  { close(fd1[0]); close(fd1[1]); return -1; }
  result=fork();
  if (result<0)
  { close(fd1[0]); close(fd1[1]);
    close(fd2[0]); close(fd2[1]);
    return -1;
  }
  else if (!result)
  { // fd1[1] for writing, fd2[0] for reading
    close(fd1[0]);
    close(fd2[1]);
    if (dup2(fd2[0],0)!=0 || dup2(fd1[1],1)!=1) 
    { perror("dup2");
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

Netxx::PipeStream::PipeStream (const std::string &cmd, const std::vector<std::string> &args)
  : readfd(), writefd(), child()
{ 
#ifdef WIN32
#else
    int fd1[2],fd2[2];
    child=pipe_and_fork(fd1,fd2);
    if (child<0) 
    {  throw oops("pipe/fork failed "+std::string(strerror(errno)));
    }
    else if (!child)
    { const unsigned newsize=64;
      const char *newargv[newsize];
      unsigned newargc=0;
      newargv[newargc++]=cmd.c_str();
      for (std::vector<std::string>::const_iterator i=args.begin();i!=args.end();++i)
        newargv[newargc++]=cmd.c_str();
      newargv[newargc]=0;
      execvp(newargv[0],const_cast<char*const*>(newargv));
      perror(newargv[0]);
      exit(errno);
    }
    readfd=fd1[0];
    writefd=fd2[1];
    fcntl(readfd,F_SETFL,fcntl(readfd,F_GETFL)|O_NONBLOCK);
#endif
  pi_.add_socket(readfd);
  pi_.add_socket(writefd);
}

Netxx::signed_size_type Netxx::PipeStream::read (void *buffer, size_type length)
{ return ::read(readfd,buffer,length);
}

Netxx::signed_size_type Netxx::PipeStream::write(const void *buffer, size_type length)
{ return ::write(writefd,buffer,length);
}

void Netxx::PipeStream::close (void)
{ ::close(readfd);
  ::close(writefd);
// wait for Process to end (before???)
#ifdef WIN32
#else
  if (child) waitpid(child,0,0);
#endif
}

Netxx::socket_type Netxx::PipeStream::get_socketfd (void) const
{ return Netxx::socket_type(-1);
}

#if 0
namespace {
class PipeProbe : public Netxx::ProbeInfo
{public:
  virtual bool needs_pending_check (void) const;
  virtual pending_type check_pending (socket_type, pending_type) const;
};
}
#endif

const Netxx::ProbeInfo* Netxx::PipeStream::get_probe_info (void) const
{ return &pi_;
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void
simple_pipe_test()
{ std::vector<std::string> args;
#ifdef WIN32
  args.push_back("\\");
  Netxx::PipeStream pipe("dir",args);
#else
  args.push_back("/");
  Netxx::PipeStream pipe("ls",args);
#endif  
  std::string result;
  char buf[1024];
  Netxx::signed_size_type bytes;
  do
  { bytes=pipe.read(buf,sizeof buf);
    if (bytes<=0) break;
    result+=std::string(buf,bytes);
  } while (true);
  pipe.close();
  BOOST_CHECK(!result.empty());
  L(F("command output is: %s\n") % result);
}

void
add_pipe_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&simple_pipe_test));
}
#endif
