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

http_client::http_client(options & opts, lua_hooks & lua,
                         netsync_connection_info const & info)
  : opts(opts),
    lua(lua),
    info(info),
    stream(build_stream_to_server(opts, lua, info,
              info.client.u.parse_port(constants::default_http_port),
              Timeout(static_cast<long>(
                  constants::netsync_timeout_seconds)))),
    nb(new Netbuf<constants::bufsz>(*stream)),
    io(new iostream(&(*nb))),
    open(true)
{}

string
http_client::resolve(string const & relative_uri)
{
  return info.client.u.path + relative_uri;
}

void
http_client::execute(http::request const & request, http::response & response)
{
  if (!open)
    {
      L(FL("reopening connection"));
      stream = build_stream_to_server(opts, lua, info,
                info.client.u.parse_port(constants::default_http_port),
                Timeout(static_cast<long>(
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

  // the uri in this request is relative to the server uri and needs to be
  // adjusted

  http::connection connection(*io);

  P(F("http request: %s %s %s")
    % request.method % request.uri % request.version);

  connection.write(request);

  if (io->good())
    L(FL("connection is good"));
  if (io->bad())
    L(FL("connection is bad"));
  if (io->fail())
    L(FL("connection is fail"));
  if (io->eof())
    L(FL("connection is eof"));

  // FIXME: this should really attempt to pipeline several requests

  I(connection.read(response) == http::status::ok);

  P(F("http response: %s %d %s")
    % response.version % response.status.value % response.status.message);

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

  E(response.status == http::status::ok, origin::network,
    F("request failed: %s %d %s") 
    % response.version % response.status.value % response.status.message);
}

/////////////////////////////////////////////////////////////////////
// raw_channel adaptor
/////////////////////////////////////////////////////////////////////

void
raw_channel::inquire_about_revs(set<revision_id> const & query_set,
                                set<revision_id> & theirs) const
{
  theirs.clear();
  ostringstream request_data;

  for (set<revision_id>::const_iterator i = query_set.begin(); 
       i != query_set.end(); ++i)
    request_data << *i << "\n";

  string body = request_data.str();

  http::request request;
  http::response response;

  request.method = http::post;
  request.uri = client.resolve("/inquire");
  request.version = http::version;
  request.headers["Content-Type"] = "text/plain";
  request.headers["Content-Length"] = lexical_cast<string>(body.size());

  request.body = body;

  client.execute(request, response);

  istringstream response_data(response.body);
  while (response_data.good())
    {
      string rev;
      std::getline(response_data, rev);
      theirs.insert(revision_id(decode_hexenc(rev, origin::network), origin::network));
    }
}

void
raw_channel::get_descendants(set<revision_id> const & common_revs,
                             vector<revision_id> & inbound_revs) const
{
  inbound_revs.clear();
  ostringstream request_data;

  for (set<revision_id>::const_iterator i = common_revs.begin(); 
       i != common_revs.end(); ++i)
    request_data << *i << "\n";

  string body = request_data.str();

  http::request request;
  http::response response;

  request.method = http::post;
  request.uri = client.resolve("/descendants");
  request.version = http::version;
  request.headers["Content-Type"] = "text/plain";
  request.headers["Content-Length"] = lexical_cast<string>(body.size());
  //request.headers["Accept"] = "text/plain";
  //request.headers["Accept-Encoding"] = "identity";
  // FIXME: put a real host value in here (lighttpd seems to require a host header)
  request.headers["Host"] = "localhost";
  request.body = body;

  client.execute(request, response);

  istringstream response_data(response.body);
  while (response_data.good())
    {
      string rev;
      std::getline(response_data, rev);
      inbound_revs.push_back(revision_id(decode_hexenc(rev, origin::network), origin::network));
    }
}

void
raw_channel::push_full_rev(revision_id const & rid,
                           revision_t const & rev,
                           vector<file_data_record> const & data_records,
                           vector<file_delta_record> const & delta_records) const
{
  // file data records
  // file delta records
  // revision text

  // name args length\n
  // data
}

void
raw_channel::pull_full_rev(revision_id const & rid,
                           revision_t & rev,
                           vector<file_data_record> & data_records,
                           vector<file_delta_record> & delta_records) const
{
  // file data records
  // file delta records
  // revision text

  // name args length\n
  // data
}

void
raw_channel::push_file_data(file_id const & id,
                            file_data const & data) const
{
}

void
raw_channel::push_file_delta(file_id const & old_id,
                             file_id const & new_id,
                             file_delta const & delta) const
{
}

void
raw_channel::push_rev(revision_id const & rid,
                      revision_t const & rev) const
{
}

void
raw_channel::pull_rev(revision_id const & rid, revision_t & rev) const
{
}

void
raw_channel::pull_file_data(file_id const & id,
                            file_data & data) const
{
}

void
raw_channel::pull_file_delta(file_id const & old_id,
                             file_id const & new_id,
                             file_delta & delta) const
{
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
  request.uri = client.resolve("/");
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
