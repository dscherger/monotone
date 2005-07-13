// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <netxx_pipe.h>

Netxx::PipeStream::PipeStream(int _readfd, int _writefd)
  : readfd(_readfd), writefd(_writefd)
{ pi_.add_socket(readfd);
  pi_.add_socket(writefd);
}

Netxx::PipeStream::signed_size_type Netxx::PipeStream::read (void *buffer, size_type length)
{ return ::read(readfd,buffer,length);
}

Netxx::PipeStream::signed_size_type Netxx::PipeStream::write(const void *buffer, size_type length)
{ return ::write(writefd,buffer,length);
}

void Netxx::PipeStream::close (void)
{ ::close(readfd);
  ::close(writefd);
}

Netxx::socket_type Netxx::PipeStream::get_socketfd (void) const
{ return Netxx::socket_type(-1);
}

namespace {
class PipeProbe : public Netxx::ProbeInfo
{public:
  virtual bool needs_pending_check (void) const;
  virtual pending_type check_pending (socket_type, pending_type) const;
};
}

const Netxx::ProbeInfo* Netxx::PipeStream::get_probe_info (void) const
{ return &pi_;
}
