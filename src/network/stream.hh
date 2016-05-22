// Copyright (C) 2014-2016 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../base.hh"

#include <functional>

#include <asio.hpp>

#include "connection_info.hh"

class abstract_stream
{
protected:
  std::function<void()> conn_handler;
  std::string remote_name;
  bool connected;
public:
  static abstract_stream *
  create_stream_for(netsync_connection_info::Client const & client,
					asio::io_service & ios);
  abstract_stream();
  void set_conn_handler(std::function<void()>);

  bool is_connected() const;
  std::string const & get_remote_name() const;

  virtual void connection_established() = 0;

  virtual void close() = 0;
  virtual void async_read_some(asio::mutable_buffers_1 const & buffers,
							   std::function<void(asio::error_code const &,
                                                  std::size_t)> handler) = 0;
  virtual void async_write_some(asio::const_buffers_1 const & buffers,
							    std::function<void(asio::error_code const &,
                                                   std::size_t)> handler) = 0;
};

template<typename T>
class stream_base : public abstract_stream
{
protected:
  asio::basic_stream_socket<T> socket;

public:
  stream_base<T>(asio::io_service & ios)
  : abstract_stream(),
    socket(ios)
  { }

  stream_base<T>(asio::io_service & ios, T & proto, int fd)
  : abstract_stream(),
    socket(ios, proto, fd)
  { }

  virtual void connection_established()
  {
    I(!connected);
    connected = true;

    std::stringstream ss;
    ss << socket.remote_endpoint();
    remote_name = ss.str();

    if (conn_handler)
      conn_handler();
  }

  asio::basic_stream_socket<T> & get_socket()
  { return socket; }

  void close()
  {
	socket.close();
  }

  void async_read_some(asio::mutable_buffers_1 const & buffers,
					   std::function<void(asio::error_code const &,
                                          std::size_t)> handler)
  {
    socket.async_read_some(buffers, handler);
  }

  void async_write_some(asio::const_buffers_1 const & buffers,
						std::function<void(asio::error_code const &,
                                           std::size_t)> handler)
  {
    socket.async_write_some(buffers, handler);
  }
};

class tcp_stream : public stream_base<asio::ip::tcp>
{
public:
  tcp_stream(asio::io_service & ios)
	: stream_base<asio::ip::tcp>(ios)
  { };
  void connect_to_one_of(asio::ip::tcp::resolver::iterator ity);
private:
  void handle_connect(asio::ip::tcp::resolver::iterator ity,
                      asio::error_code const & err);
};

#ifndef WIN32
// For pipes, we cannot use stream_base, but need two underlying
// sockets of type asio::posix::stream_descriptor - named as seen from
// the parent process.
class unix_local_stream : public abstract_stream
{
  asio::posix::stream_descriptor in;
  asio::posix::stream_descriptor out;
public:
  static abstract_stream *
  create_stream_for(asio::io_service & ios,
					std::vector<std::string> const & args,
                    std::string && name);
  unix_local_stream(asio::io_service & ios, std::string && name,
                    int fd_in, int fd_out);

  virtual void connection_established()
  {
    I(!connected);
    connected = true;
    if (conn_handler)
      conn_handler();
  }

  void close();
  void async_read_some(asio::mutable_buffers_1 const & buffers,
					   std::function<void(asio::error_code const &,
                                          std::size_t)> handler);
  void async_write_some(asio::const_buffers_1 const & buffers,
						std::function<void(asio::error_code const &,
                                           std::size_t)> handler);
};
#else

// FIXME: yet to implement: A Windows variant of pipes!

// check out KB: 190351
// http://support.microsoft.com/kb/190351

#endif


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
