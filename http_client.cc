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
#include "globish.hh"
#include "http_client.hh"
#include "json_io.hh"
#include "json_msgs.hh"
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
#include <set>
#include <string>

#include <boost/shared_ptr.hpp>

using json_io::json_value_t;
using boost::shared_ptr;
using boost::lexical_cast;
using std::vector;
using std::set;
using std::string;
using std::iostream;

using Netxx::Netbuf;
using Netxx::Timeout;
using Netxx::StreamBase;
using Netxx::Stream;
using Netxx::PipeStream;


http_client::http_client(options & opts, lua_hooks & lua,
                         uri const & u,
                         globish const & include_pattern,
                         globish const & exclude_pattern)
  : opts(opts),
    lua(lua),
    u(u),
    include_pattern(include_pattern),
    exclude_pattern(exclude_pattern),
    stream(build_stream_to_server(opts, lua, u, include_pattern, exclude_pattern,
                                  (u.port.empty() ? constants::default_http_port
                                                  : lexical_cast<size_t,string>(u.port)),
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
      stream = build_stream_to_server(opts, lua, u, include_pattern, exclude_pattern,
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

//   std::cerr << "json request" << std::endl
//             << out.buf.data() << std::endl;

  // Now read back the result
  string data;
  parse_http_response(data);

//   std::cerr << "json response" << std::endl
//             << data << std::endl;

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

  if (k == "Content-Length:" || k == "Content-length:" || k == "content-length:")
    content_length = lexical_cast<size_t>(v);
  else if (k == "Connection:" || k == "connection:")
    keepalive = (v == "Keep-Alive" || v == "Keep-alive" || v == "keep-alive");
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
      data += static_cast<char>(io->get());
      content_length--;
    }

  io->flush();

  // if we keep the connection alive, and we're limited to a single active
  // connection (as in the sample lighttpd.conf and required by the sqlite
  // database locking scheme) this will probably block all other clients.
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



/////////////////////////////////////////////////////////////////////
// http_channel adaptor
/////////////////////////////////////////////////////////////////////

void
http_channel::inquire_about_revs(set<revision_id> const & query_set,
                                 set<revision_id> & theirs) const
{
  theirs.clear();
  json_value_t request = encode_msg_inquire_request(query_set);
  json_value_t response = client.transact_json(request);
  E(decode_msg_inquire_response(response, theirs),
    F("received unexpected reply to 'inquire_request' message"));
}

void
http_channel::get_descendants(set<revision_id> const & common_revs,
                              vector<revision_id> & inbound_revs) const
{
  inbound_revs.clear();
  json_value_t request = encode_msg_descendants_request(common_revs);
  json_value_t response = client.transact_json(request);
  E(decode_msg_descendants_response(response, inbound_revs),
    F("received unexpected reply to 'descendants_request' message"));
}

void
http_channel::push_file_data(file_id const & id,
                             file_data const & data) const
{
}

void
http_channel::push_file_delta(file_id const & old_id,
                              file_id const & new_id,
                              file_delta const & delta) const
{
}

void
http_channel::push_rev(revision_id const & rid, revision_t const & rev) const
{
  json_value_t request = encode_msg_put_rev_request(rid, rev);
  json_value_t response = client.transact_json(request);
  E(decode_msg_put_rev_response(response),
    F("received unexpected reply to 'put_rev_request' message"));
}

void
http_channel::pull_rev(revision_id const & rid, revision_t & rev) const
{
}

void
http_channel::pull_file_data(file_id const & id,
                              file_data & data) const
{
}

void
http_channel::pull_file_delta(file_id const & old_id,
                               file_id const & new_id,
                               file_delta & delta) const
{
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
