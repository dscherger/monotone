/*
 * Copyright (C) 2014 Stephen Leake <stephen_leake@stephe-leake.org>
 * Copyright (C) 2001-2004 Peter J Jones (pjones@pmade.org)
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/** @file
 * This file contains the implementation of the Netxx::SockOpt class.
**/

// common header
#include "common.h"

// Netxx includes
#include "sockopt.h"
#include "types.h"

// standard includes
#include <cstring>

//####################################################################
struct Netxx::SockOpt::pimpl
{
    pimpl (int /* socket */ )
        : fcntl_flags_(0), fcntl_changed_(false), win_blocking_(false)
    {
#       ifndef WIN32
            fcntl_flags_ = fcntl(socket, F_GETFL, 0);
#       endif
    }

    int fcntl_flags_;
    bool fcntl_changed_;
    bool win_blocking_;
};
//####################################################################
Netxx::SockOpt::SockOpt (socket_type socketfd, bool revert)
    : socket_(socketfd), revert_(revert)
{
    pimpl_ = new pimpl(socket_);
}
//####################################################################
Netxx::SockOpt::~SockOpt (void)
{
    if (revert_ && (pimpl_->fcntl_changed_ || pimpl_->win_blocking_)) {
#       if defined(WIN32)
            unsigned long off = 0;
            ioctlsocket(socket_, FIONBIO, &off);
#       else
            fcntl(socket_, F_SETFL, pimpl_->fcntl_flags_);
#       endif
    }

    delete pimpl_;
}
//####################################################################
bool Netxx::SockOpt::set_non_blocking (void)
{
#   if defined(WIN32)
        /*
         * FIXME FIXME FIXME FIXME
         *
         * Call ioctlsocket to get the NBIO flag instead of using
         * win_blocking_
         *
         * FIXME FIXME FIXME FIXME
         */
        if (pimpl_->win_blocking_) return true;

        unsigned long on = 1;
        if (ioctlsocket(socket_, FIONBIO, &on) != 0) return false;
        pimpl_->win_blocking_ = true;
        return true;
#   else
        int flags = fcntl(socket_, F_GETFL, 0);
        if (flags & O_NONBLOCK) return true;

        if (fcntl(socket_, F_SETFL, pimpl_->fcntl_flags_ | O_NONBLOCK) == -1) return false;
        pimpl_->fcntl_changed_ = true;
        return true;
#   endif
}
//####################################################################
bool Netxx::SockOpt::set_reuse_address (void)
{
    int on = 1;
    char *val = reinterpret_cast<char*>(&on);

    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, val, sizeof(on)) != 0) {
        std::string error("setsockopt(2) failure: ");
        error += str_error(get_last_error());
        throw Exception(error);
    }

    return true;
}
//####################################################################
bool Netxx::SockOpt::check_for_error (std::string &message) const
{
    int so_error, so_return;
    char *val = reinterpret_cast<char*>(&so_error);
    os_socklen_type so_len(sizeof(so_error));

    if ( (so_return = getsockopt(socket_, SOL_SOCKET, SO_ERROR, val, &so_len)) < 0) {
        message = str_error(get_last_error());
        return false;
    }

    if (so_error) {
        message = str_error(so_error);
        return false;
    }

    return true;
}
//####################################################################
bool Netxx::SockOpt::set_ipv6_listen_for_v6_only (void) const
{
# ifndef NETXX_NO_INET6

# ifndef IPV6_V6ONLY
    return false;
# else
    int on = 1;
    char *val = reinterpret_cast<char*>(&on);

    if (setsockopt(socket_, IPPROTO_IPV6, IPV6_V6ONLY, val, sizeof(on)) != 0)
        return false;
# endif
# endif
    return true;
}
//####################################################################
