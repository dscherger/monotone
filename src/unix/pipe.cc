// Copyright (C) 2014-2016 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../base.hh"

#include <vector>
#include <string>

#include <asio.hpp>

#include "../uri.hh"
#include "../globish.hh"
#include "../platform.hh"
#include "../network/stream.hh"

using std::string;
using std::vector;

using asio::local::connect_pair;
using asio::local::stream_protocol;

abstract_stream *
unix_local_stream::create_stream_for(asio::io_service & ios,
									 vector<string> const & args,
                                     string && name)
{
  // Transform into a list of arguments in C-style.
  int argc = 0;
  char const ** argv = new char const * [args.size() + 1];
  for (vector<string>::const_iterator ity = args.begin();
	   ity != args.end(); ++ity)
	{
	  argv[argc] = ity->c_str();
	  argc += 1;
	}
  argv[argc] = NULL;

  int child_in, child_out;
  pid_t pid = process_spawn_pipe(argv, &child_in, &child_out);
  I(pid != 0);
  if (pid == -1)
	throw oops("fork failed");

  delete [] argv;

  unix_local_stream * new_stream =
	new unix_local_stream(ios, move(name), child_out, child_in);
  return new_stream;
}

unix_local_stream::unix_local_stream(asio::io_service & ios,
                                     string && name,
									 int fd_in, int fd_out)
  : in(ios, fd_in),
	out(ios, fd_out)
{
  remote_name = move(name);
  // Streams based on existing unix file handles need to be considered
  // connected, already.
  connection_established();
}

void unix_local_stream::close()
{
  in.close();
  out.close();
}

void unix_local_stream::async_read_some(
  asio::mutable_buffers_1 const & buffers,
  std::function<void(asio::error_code const &,
                     std::size_t)> handler)
{
  in.async_read_some(buffers, handler);
}

void unix_local_stream::async_write_some(
  asio::const_buffers_1 const & buffers,
  std::function<void(asio::error_code const &,
                     std::size_t)> handler)
{
  out.async_write_some(buffers, handler);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
