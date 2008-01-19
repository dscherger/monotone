// Copyright (C) 2007 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


// micro http client implementation
#include "base.hh"
#include "app_state.hh"
#include "globish.hh"
#include "http_client.hh"
#include "json_io.hh"
#include "net_common.hh"
#include "sanity.hh"
#include "lexical_cast.hh"
#include "constants.hh"
#include "uri.hh"

#include "netxx/address.h"
#include "netxx/netbuf.h"
#include "netxx/stream.h"
#include "netxx/timeout.h"
#include "netxx_pipe.hh"

#include <vector>
#include <string>

#include <boost/shared_ptr.hpp>

using json_io::json_value_t;
using boost::shared_ptr;
using boost::lexical_cast;
using std::vector;
using std::string;
using std::iostream;


using Netxx::Netbuf;
using Netxx::Timeout;
using Netxx::StreamBase;
using Netxx::Stream;
using Netxx::PipeStream;


http_client::http_client(app_state & app,
			 uri const & u, 	      
			 globish const & include_pattern,
			 globish const & exclude_pattern)
  : app(app), 
    u(u), 
    include_pattern(include_pattern), 
    exclude_pattern(exclude_pattern), 
    stream(build_stream_to_server(app, u, include_pattern, exclude_pattern, 
                                  constants::default_http_port,
                                  Netxx::Timeout(static_cast<long>(constants::netsync_timeout_seconds)))), 
    nb(new Netbuf<constants::bufsz>(*stream)), 
    io(new iostream(&(*nb))),
    open(true)
{}

json_value_t
http_client::transact_json(json_value_t v)
{
  if (!open)
    {
      L(FL("reopening connection"));
      stream = build_stream_to_server(app, u, include_pattern, exclude_pattern, 
                                      constants::default_http_port,
                                      Netxx::Timeout(static_cast<long>(constants::netsync_timeout_seconds)));
      nb = shared_ptr< Netbuf<constants::bufsz> >(new Netbuf<constants::bufsz>(*stream));
      io = shared_ptr<iostream>(new iostream(&(*nb)));
      open = true;
    }

  I(stream);
  I(nb);
  I(io);
  I(open);

  json_io::printer out;
  v->write(out);
  string header = (F("POST %s HTTP/1.0\r\n"
		     "Host: %s\r\n"
		     "Content-Length: %s\r\n"
		     "Content-Type: application/jsonrequest\r\n"
		     "Accept: application/jsonrequest\r\n"
		     "Accept-Encoding: identity\r\n"
		     "Connection: Keep-Alive\r\n"
		     "\r\n") 
		   % (u.path.empty() ? "/" : u.path)
		   % u.host 
		   % lexical_cast<string>(out.buf.size())).str();
  
  L(FL("http_client: sending request [[POST %s HTTP/1.0]]") 
    % (u.path.empty() ? "/" : u.path));
  L(FL("http_client: to [[Host: %s]]") % u.host);
  L(FL("http_client: sending %d-byte body") % out.buf.size());
  io->write(header.data(), header.size());
  io->write(out.buf.data(), out.buf.length());
  io->flush();
  L(FL("http_client: sent %d-byte body") % out.buf.size());


  // Now read back the result
  string data;
  parse_http_response(data);
  json_io::input_source in(data, "scgi");
  json_io::tokenizer tok(in);
  json_io::parser p(tok);
  return p.parse_object();
}


void 
http_client::parse_http_status_line()
{
  // We're only interested in 200-series responses
  string tmp;
  string pat("HTTP/1.0 200");
  L(FL("http_client: reading response..."));
  while (io->good() && tmp.empty())
    std::getline(*io, tmp);
  L(FL("http_client: response: [[%s]]") % tmp);
  E(tmp.substr(0,pat.size()) == pat, F("HTTP status line: %s") % tmp);
}

void 
http_client::parse_http_header_line(size_t & content_length, 
				    bool & keepalive)
{
  string k, v, rest;
  (*io) >> k >> v;
  L(FL("http_client: header: [[%s %s]]") % k % v);
  std::getline(*io, rest);
  
  if (k == "Content-Length:" 
      || k == "Content-length:"
      || k == "content-length:")
    content_length = lexical_cast<size_t>(v);
  else if (k == "Connection:" 
	   || k == "connection:")
    keepalive = (v == "Keep-Alive" 
		 || v == "Keep-alive" 
		 || v == "keep-alive");
}


void 
http_client::crlf()
{
  E(io->get() == '\r', F("expected CR in HTTP response"));
  E(io->get() == '\n', F("expected LF in HTTP response"));
}


void 
http_client::parse_http_response(std::string & data)
{
  size_t content_length = 0;
  bool keepalive = false;
  data.clear();
  parse_http_status_line();
  while (io->good() && io->peek() != '\r')
    parse_http_header_line(content_length, keepalive);
  crlf();

  L(FL("http_client: receiving %d-byte body") % content_length);

  while (io->good() && content_length > 0)
    {
      data += static_cast<char>(io->get());;
      content_length--;
    }

  io->flush();

  if (!keepalive) 
    {
      L(FL("http_client: closing connection"));
      stream->close();
      io.reset();
      nb.reset();
      stream.reset();
      open = false;
    }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
