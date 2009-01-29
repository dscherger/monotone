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
#include "http.hh"
#include "http_client.hh"
#include "json_io.hh"
#include "json_msgs.hh"
#include "netcmd.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"
#include "lexical_cast.hh"
#include "constants.hh"
#include "uri.hh"

#include "netxx/address.h"
#include "netxx/netbuf.h"
#include "netxx/stream.h"
#include "netxx/timeout.h"
#include "netxx_pipe.hh"

#include <map>
#include <vector>
#include <set>
#include <string>
#include <sstream>

#include <boost/shared_ptr.hpp>

using boost::shared_ptr;
using boost::lexical_cast;

using json_io::json_value_t;

using std::map;
using std::vector;
using std::set;
using std::string;
using std::iostream;
using std::istringstream;
using std::ostringstream;

using Netxx::Netbuf;
using Netxx::Timeout;
using Netxx::StreamBase;
using Netxx::Stream;
using Netxx::PipeStream;


http_client::http_client(options & opts, lua_hooks & lua,
                         netsync_connection_info const & info)
  : opts(opts),
    lua(lua),
    info(info),
    stream(build_stream_to_server(opts, lua, info,
              info.client.u.parse_port(constants::default_http_port),
              Netxx::Timeout(static_cast<long>(
                  constants::netsync_timeout_seconds)))),
    nb(new Netbuf<constants::bufsz>(*stream)),
    io(new iostream(&(*nb))),
    open(true)
{}

void
http_client::execute(http::request const & request, http::response & response)
{
  if (!open)
    {
      L(FL("reopening connection"));
      stream = build_stream_to_server(opts, lua, info,
                info.client.u.parse_port(constants::default_http_port),
                Netxx::Timeout(static_cast<long>(
                    constants::netsync_timeout_seconds)));
      nb = shared_ptr< Netbuf<constants::bufsz> >(
                            new Netbuf<constants::bufsz>(*stream));
      io = shared_ptr<iostream>(new iostream(&(*nb)));
      open = true;
    }

  I(stream);
  I(nb);
  I(io);
  I(open);

  L(FL("http_client: sending request [[POST %s HTTP/1.1]]")
    % (info.client.u.path));

  (*io) << request.method << " " 
        << info.client.u.path << " "
        << request.version << "\r\n";

  for (map<string, string>::const_iterator i = request.headers.begin();
       i != request.headers.end(); ++i)
    (*io) << i->first << ": " << i->second << "\r\n";

  (*io) << "\r\n";

  // FIXME: this used to set a Host header
  //L(FL("http_client: to [[Host: %s]]") % request.headers["Host"]);

  L(FL("http_client: sending %d-byte body") % request.body.size());
  (*io) << request.body;

  io->flush();
  L(FL("http_client: sent %d byte request") % request.body.size());

//   std::cerr << "http request" << std::endl
//             << out.buf.data() << std::endl;

  // Now read back the result
  parse_http_response(response);

  L(FL("http_client: received %d byte response") % response.body.size());

//   std::cerr << "http response" << std::endl
//             << data << std::endl;
}

void
http_client::parse_http_status_line(http::response & response)
{
  // We're only interested in 200-series responses
  L(FL("http_client: reading response..."));
  (*io) >> response.version >> response.status_code;
  std::getline(*io, response.status_message);
  response.status_message = trim(response.status_message, " \r\n");

  // sometimes we seem to get eof when reading the response -- not sure why yet
  if (io->good())
    L(FL("connection is good"));
  if (io->bad())
    L(FL("connection is bad"));
  if (io->fail())
    L(FL("connection is fail"));
  if (io->eof())
    L(FL("connection is eof"));

  L(FL("http_client: response: [[%s %d %s]]") 
    % response.version % response.status_code % response.status_message);
}

void
http_client::parse_http_header_line(http::response & response)
{
  string key, val;
  (*io) >> key;
  std::getline(*io, val);

  key = trim_right(key, ":");
  val = trim(val, " \r\n");

  L(FL("http_client: header: [[%s %s]]") % key % val);

  response.headers[key] = val;
}


void
http_client::crlf()
{
  E(io->get() == '\r', origin::network, F("expected CR in HTTP response"));
  E(io->get() == '\n', origin::network, F("expected LF in HTTP response"));
}


void
http_client::parse_http_response(http::response & response)
{
  size_t content_length = 0;

  response.headers.clear();
  response.body.clear();

  parse_http_status_line(response);
  while (io->good() && io->peek() != '\r')
    parse_http_header_line(response);
  crlf();

  content_length = lexical_cast<size_t>(response.headers["Content-Length"]);

  L(FL("http_client: receiving %d-byte body") % content_length);

  response.body.reserve(content_length);
  while (io->good() && content_length > 0)
    {
      response.body += static_cast<char>(io->get());
      content_length--;
    }

  io->flush();
  
  // something is wrong and the connection is sometimes closed by the server
  // even though it did not issue a Connection: close header
  if (io->good())
    L(FL("connection is good"));
  if (io->bad())
    L(FL("connection is bad"));
  if (io->fail())
    L(FL("connection is fail"));
  if (io->eof())
    L(FL("connection is eof"));

  // if we keep the connection alive, and we're limited to a single active
  // connection (as in the sample lighttpd.conf and required by the sqlite
  // database locking scheme) this will probably block all other clients.

  // According to the scgi spec the server side will close the connection
  // after processing each request. However, the connection being closed is
  // the SCGI connection between the webserver and the monotone server, not
  // the HTTP connection between the monotone client and the webserver,
  // which may allow for connections to be kept alive.

  // something is not working right so for now close the connection after
  // every request/response cycle

  if (true || response.headers["Connection"] == "close")
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
// json_channel adaptor
/////////////////////////////////////////////////////////////////////

json_value_t
json_channel::transact(json_value_t v) const
{
  json_io::printer out;
  v->write(out);

  http::request request;
  http::response response;

  request.method = http::post;
  request.uri = "";
  request.version = http::version;
  request.headers["Content-Type"] = "application/jsonrequest";
  request.headers["Content-Length"] = lexical_cast<string>(out.buf.size());
  request.headers["Accept"] = "application/jsonrequest";
  request.headers["Accept-Encoding"] = "identity";
  // FIXME: put a real host value in here (lighttpd seems to require a host header)
  request.headers["Host"] = "localhost";
  request.body = out.buf;

  client.execute(request, response);

  json_io::input_source in(response.body, "json");
  json_io::tokenizer tok(in);
  json_io::parser p(tok);
  return p.parse_object();
}

void
json_channel::inquire_about_revs(set<revision_id> const & query_set,
                                 set<revision_id> & theirs) const
{
  theirs.clear();
  json_value_t request = encode_msg_inquire_request(query_set);
  json_value_t response = transact(request);
  E(decode_msg_inquire_response(response, theirs),
    origin::network,
    F("received unexpected reply to 'inquire_request' message"));
}

void
json_channel::get_descendants(set<revision_id> const & common_revs,
                              vector<revision_id> & inbound_revs) const
{
  inbound_revs.clear();
  json_value_t request = encode_msg_descendants_request(common_revs);
  json_value_t response = transact(request);
  E(decode_msg_descendants_response(response, inbound_revs),
    origin::network,
    F("received unexpected reply to 'descendants_request' message"));
}


void
json_channel::push_full_rev(revision_id const & rid,
                            revision_t const & rev,
                            vector<file_data_record> const & data_records,
                            vector<file_delta_record> const & delta_records) const
{
  json_value_t request = encode_msg_put_full_rev_request(rid, rev,
                                                         data_records,
                                                         delta_records);
  json_value_t response = transact(request);
  E(decode_msg_put_full_rev_response(response),
    origin::network,
    F("received unexpected reply to 'put_full_rev_request' message"));
}

void
json_channel::pull_full_rev(revision_id const & rid,
                            revision_t & rev,
                            vector<file_data_record> & data_records,
                            vector<file_delta_record> & delta_records) const
{
  json_value_t request = encode_msg_get_full_rev_request(rid);
  json_value_t response = transact(request);
  E(decode_msg_get_full_rev_response(response, rev,
                                     data_records, delta_records),
    origin::network,
    F("received unexpected reply to 'get_full_rev_request' message"));
}

void
json_channel::push_file_data(file_id const & id,
                             file_data const & data) const
{
  json_value_t request = encode_msg_put_file_data_request(id, data);
  json_value_t response = transact(request);
  E(decode_msg_put_file_data_response(response),
    origin::network,
    F("received unexpected reply to 'put_file_data_request' message"));
}

void
json_channel::push_file_delta(file_id const & old_id,
                              file_id const & new_id,
                              file_delta const & delta) const
{
  json_value_t request = encode_msg_put_file_delta_request(
                                                  old_id, new_id, delta);
  json_value_t response = transact(request);
  E(decode_msg_put_file_delta_response(response),
    origin::network,
    F("received unexpected reply to 'put_file_delta_request' message"));
}

void
json_channel::push_rev(revision_id const & rid,
                       revision_t const & rev) const
{
  json_value_t request = encode_msg_put_rev_request(rid, rev);
  json_value_t response = transact(request);
  E(decode_msg_put_rev_response(response),
    origin::network,
    F("received unexpected reply to 'put_rev_request' message"));
}

void
json_channel::pull_rev(revision_id const & rid, revision_t & rev) const
{
  json_value_t request = encode_msg_get_rev_request(rid);
  json_value_t response = transact(request);
  E(decode_msg_get_rev_response(response, rev),
    origin::network,
    F("received unexpected reply to 'get_rev_request' message"));
}

void
json_channel::pull_file_data(file_id const & id,
                             file_data & data) const
{
  json_value_t request = encode_msg_get_file_data_request(id);
  json_value_t response = transact(request);
  E(decode_msg_get_file_data_response(response, data),
    origin::network,
    F("received unexpected reply to 'get_file_data_request' message"));
}

void
json_channel::pull_file_delta(file_id const & old_id,
                              file_id const & new_id,
                              file_delta & delta) const
{
  json_value_t request = encode_msg_get_file_delta_request(old_id, new_id);
  json_value_t response = transact(request);
  E(decode_msg_get_file_delta_response(response, delta),
    origin::network,
    F("received unexpected reply to 'get_file_delta_request' message"));
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
