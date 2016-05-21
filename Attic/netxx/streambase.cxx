/*
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
 * This file contains the implementation of the Netxx::StreamBase class
 * along with some helper functions.
**/

// common header
#include "common.h"

// Netxx includes
#include "streambase.h"
#include "sockopt.h"
#include "address.h"
#include "peer.h"
#include "types.h"
#include "socket.h"

// standard includes
#include <cstring>
#include <algorithm>

//####################################################################
namespace 
{
    bool client_connect (Netxx::Socket &socket, const Netxx::Peer &peer,
	    const Netxx::Timeout &timeout, std::string &message);
}
//####################################################################
Netxx::StreamBase::StreamBase (const Timeout &timeout)
    : timeout_(timeout)
{ }
//####################################################################
Netxx::StreamBase::~StreamBase (void)
{ }
//####################################################################
void Netxx::StreamBase::make_connection (Socket &socket, const Address &address) 
{
    std::string message;

    // connect this socket to the given address
    Address::const_iterator ai(address.begin()), aend(address.end());

    // make sure there is at least one network address
    if (ai == aend) throw Exception("no address to connect to");

    // try all addresses
    bool connected = false;
    for (; ai != aend; ++ai) {
	if (client_connect(socket, *ai, timeout_, message)) {
	    connected = true;
	    break;
	}
    }

    // make sure we got connected
    if (!connected) {
	std::string error("failed to connect: ");
	error += message;
	throw NetworkException(error);
    }
}
//####################################################################
void Netxx::StreamBase::swap_base (StreamBase &other) 
{
    std::swap(timeout_, other.timeout_);
}
//####################################################################
void Netxx::StreamBase::set_timeout (const Timeout &timeout) 
{
    timeout_ = timeout;
}
//####################################################################
const Netxx::Timeout& Netxx::StreamBase::get_timeout (void) const 
{
    return timeout_;
}
//####################################################################
namespace 
{
    //####################################################################
    bool client_connect (Netxx::Socket &socket, const Netxx::Peer &peer,
	    const Netxx::Timeout &timeout, std::string &message)
    {
	const sockaddr *sa = static_cast<const sockaddr*>(peer.get_sa());
	Netxx::size_type sa_size = peer.get_sa_size();

	/*
	 * Get the socket type for this Peer
	 */
	Netxx::Socket::Type stype;
	switch (sa->sa_family) {
	    case AF_INET:
		stype = Netxx::Socket::TCP;
		break;


#ifndef NETXX_NO_INET6
	    case AF_INET6:
		stype = Netxx::Socket::TCP6;
		break;
#endif

#if !defined(_WIN32) && !defined(_WIN64)
	    case AF_LOCAL:
		stype = Netxx::Socket::LOCALSTREAM;
		break;
#endif

	    default:
		stype = Netxx::Socket::TCP;
	}

	/*
	 * Create a tmp socket so that the Socket class will do all the hard
	 * work for us. Then replace the socket given in the param list with
	 * the tmp socket.
	 */
	Netxx::Socket tmp_socket(stype);
	tmp_socket.swap(socket);

	Netxx::socket_type socketfd=socket.get_socketfd();
	Netxx::SockOpt socket_options(socketfd, true);
	if (timeout) socket_options.set_non_blocking();

	if (connect(socketfd, sa, sa_size) != 0) {
	    Netxx::error_type error_code = Netxx::get_last_error();

	    if (error_code == EINPROGRESS || error_code == EWOULDBLOCK || error_code == EINTR) {
		if (!socket.readable_or_writable(timeout)) {
		    message = "connection timed out";
		    return false;
		}

		return socket_options.check_for_error(message);
	    }

	    message = Netxx::str_error(error_code);
	    return false;
	}

	return true;
    }
    //####################################################################
} // end anonymous namespace
