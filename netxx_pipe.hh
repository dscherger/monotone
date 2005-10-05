// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <vector>
#include <netxx/socket.h>
#include <netxx/streambase.h>
#ifdef WIN32
#  include <windows.h>
#endif

/* What is this all for?
 
If you want to transparently handle a pipe and a socket on unix and windows
you have to abstract some difficulties:
 
 - sockets have a single filedescriptor for reading and writing
   pipes usually come in pairs (one for reading and one for writing)
   
 - process creation is different on unix and windows
 
 => so Netxx::PipeStream is a Netxx::StreamBase which abstracts two pipes to
   and from an external command
   
 - windows can select on a socket but not on a pipe
 
 => so Netxx::PipeCompatibleProbe is a Netxx::Probe like class which
   _can_ handle pipes on windows (emulating select is difficult at best!)
   (on unix Probe and PipeCompatibleProbe are identical)
   
*/

namespace Netxx
  {
  class PipeCompatibleProbe;
  class StreamServer;

  class PipeStream : public StreamBase
    {
      int readfd, writefd;
      int child;
#ifdef WIN32

      char readbuf[1024];
      unsigned bytes_available;
      OVERLAPPED overlap;

      friend class PipeCompatibleProbe;
#endif

    public:
      // do we need Timeout for symmetry with Stream?
      explicit PipeStream (int readfd, int writefd); 
      explicit PipeStream (const std::string &cmd, const std::vector<std::string> &args);
      virtual signed_size_type read (void *buffer, size_type length);
      virtual signed_size_type write (const void *buffer, size_type length);
      virtual void close (void);
      virtual socket_type get_socketfd (void) const;
      virtual const ProbeInfo* get_probe_info (void) const;
      int get_readfd(void) const
        {
          return readfd;
        }
      int get_writefd(void) const
        {
          return writefd;
        }
    };

#ifdef WIN32

  class PipeCompatibleProbe : public Probe
    { // We need to make sure that only pipes are connected, if Streams are
      // connected the old Probe functions still apply
      // use WriteFileEx/ReadFileEx with Overlap?
      bool is_pipe;
      PipeStream *pipe;
      ready_type ready_t;
    public:
      PipeCompatibleProbe() : is_pipe(), pipe(), ready_t()
      {}
      void clear()
      {
        if (is_pipe)
          {
            pipe=0;
            is_pipe=false;
          }
        else
          Probe::clear();
      }
      result_type ready(const Timeout &timeout=Timeout(), ready_type rt=ready_none);
      void add(PipeStream &ps, ready_type rt=ready_none);
      void add(const StreamBase &sb, ready_type rt=ready_none);
      void add(const StreamServer &ss, ready_type rt=ready_none);
      void remove(const PipeStream &ps);
    };
#else

  struct PipeCompatibleProbe : Probe
    {
      void add(PipeStream &ps, ready_type rt=ready_none);
      void add(const StreamBase &sb, ready_type rt=ready_none);
      void add(const StreamServer &ss, ready_type rt=ready_none);
    };
#endif

}
