// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <netxx/socket.h>

namespace Netxx {

class PipeStream : public StreamBase {
    int readfd, writefd;
public:
    explicit PipeStream (int readfd, int writefd);
//    explicit PipeStream (const std::string &cmd, const std::vector<std::string> &args);
    virtual signed_size_type read (void *buffer, size_type length);
    virtual signed_size_type write (const void *buffer, size_type length);
    virtual void close (void);
    virtual socket_type get_socketfd (void) const;
    virtual const ProbeInfo* get_probe_info (void) const;
    int get_readfd(void) const { return readfd; }
    int get_writefd(void) const { return writefd; }
};
}
