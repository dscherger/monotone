// Copyright (C) 2007 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iostream>
#include <sstream>
#include <map>

#include "app_state.hh"
#include "cmd.hh"
#include "constants.hh"
#include "database.hh"
#include "globish.hh"
#include "graph.hh"
#include "gsync.hh"
#include "http.hh"
#include "json_io.hh"
#include "json_msgs.hh"
#include "keys.hh"
#include "key_store.hh"
#include "lexical_cast.hh"
#include "lua.hh"
#include "lua_hooks.hh"
#include "netcmd.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "ui.hh"

#include "netxx/address.h"
#include "netxx/peer.h"
#include "netxx/netbuf.h"
#include "netxx/socket.h"
#include "netxx/stream.h"
#include "netxx/streamserver.h"

using std::istream;
using std::make_pair;
using std::map;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::lexical_cast;

// SCGI interface is pretty straightforward
//
// When we accept a connection, we read a netstring out of it covering the
// header, and then a body with the specified content length.
//
// The format of the headers is:
//
//        headers ::= header*
//        header ::= name NUL value NUL
//        name ::= notnull+
//        value ::= notnull+
//        notnull ::= <01> | <02> | <03> | ... | <ff>
//        NUL = <00>
//
// The format of the netstring containing the headers is:
//
// [len]":"[string]","
//
// Where [string] is the string and [len] is a nonempty sequence of ASCII
// digits giving the length of [string] in decimal.
//
// The response is a sequence of CRLF-separated of HTTP headers, followed by
// a bare CRLF, and the response body.
//
// This response format is not specified by the SCGI "spec".
//

namespace scgi
{
  static const string version("SCGI/1");

  class connection : public http::connection
  {
  public:
    connection(std::iostream & io) : http::connection(io) {}
    virtual ~connection() {}

    string version()
    {
      return scgi::version;
    }

    bool read(string & value)
    {
      while (io.good())
        {
          char ch = static_cast<char>(io.get());
          if (ch == '\0')
            break;
          value += ch;
        }
      return io.good();
    }

    bool read(http::request & r)
    {
      size_t len;
      if (!http::connection::read(len, ":")) return false;
      L(FL("read scgi netstring length: %d") % len);
      while (len > 0)
        {
          string key, val;
          if (!read(key)) return false;
          if (!read(val)) return false;
          len -= key.size();
          len -= val.size();
          len -= 2;

          L(FL("read scgi header: %s: %s") % key % val);

          if (key == "CONTENT_LENGTH")
            r.headers["Content-Length"] = val;
          else if (key == "SCGI" && val == "1")
            r.version = "SCGI/1";
          else if (key == "REQUEST_METHOD")
            r.method = val;
          else if (key == "REQUEST_URI")
            r.uri = val;
          else if (key == "CONTENT_TYPE")
            r.headers["Content-Type"] = val;
        }

      L(FL("read scgi request: %s %s %s") % r.method % r.uri % r.version);

      // this is a loose interpretation of the scgi "spec"
      if (r.version != scgi::version) return false;
      if (r.headers.find("Content-Length") == r.headers.end()) return false;

      if (!io.good()) return false;

      char comma = static_cast<char>(io.get());

      return http::connection::read_body(r);
    }

    void write(http::response const & r)
    {
      http::connection::write_headers(r);
      http::connection::write_body(r);
    }

  };
};


struct gserve_error
{
  string msg;
  gserve_error(string const & s): msg(s) {}
};

static json_io::json_value_t
do_cmd(database & db, json_io::json_object_t cmd_obj)
{
  set<revision_id> request_revs;

  revision_id rid;
  revision_t rev;
  file_id fid, old_id, new_id;
  file_data data;
  file_delta delta;
  vector<file_data_record> data_records;
  vector<file_delta_record> delta_records;

  db.ensure_open();
  if (decode_msg_inquire_request(cmd_obj, request_revs))
    {
      L(FL("inquiring %d revisions") % request_revs.size());
      set<revision_id> response_revs;
      for (set<revision_id>::const_iterator i = request_revs.begin();
           i != request_revs.end(); ++i)
        if (db.revision_exists(*i))
          response_revs.insert(*i);
      return encode_msg_inquire_response(response_revs);
    }
  else if (decode_msg_descendants_request(cmd_obj, request_revs))
    {
      L(FL("descendants %d revisions") % request_revs.size());
      rev_ancestry_map parent_to_child_map;
      db.get_revision_ancestry(parent_to_child_map);

      set<revision_id> descendant_set, response_set;
      // get_all_ancestors can be used as get_all_descendants if used with
      // the normal parent-to-child order ancestry map.  the resulting
      // ancestors include all those in the frontier we started from which
      // we don't want so remove these to arrive at the set of revs this
      // server has the the attached client does not.
      get_all_ancestors(request_revs, parent_to_child_map, descendant_set);
      set_difference(descendant_set.begin(), descendant_set.end(),
                     request_revs.begin(), request_revs.end(),
                     inserter(response_set, response_set.begin()));

      vector<revision_id> response_revs;
      toposort(db, response_set, response_revs);
      return encode_msg_descendants_response(response_revs);
    }
  else if (decode_msg_get_full_rev_request(cmd_obj, rid))
    {
      load_full_rev(db, rid, rev, data_records, delta_records);
      return encode_msg_get_full_rev_response(rev, data_records, delta_records);
    }
  else if (decode_msg_put_full_rev_request(cmd_obj, rid, rev,
                                           data_records, delta_records))
    {
      revision_id check;
      calculate_ident(rev, check);
      I(rid == check);
      store_full_rev(db, rid, rev, data_records, delta_records);
      return encode_msg_put_full_rev_response();
    }
  else if (decode_msg_get_rev_request(cmd_obj, rid))
    {
      db.get_revision(rid, rev);
      return encode_msg_get_rev_response(rev);
    }
  else if (decode_msg_put_rev_request(cmd_obj, rid, rev))
    {
      revision_id check;
      calculate_ident(rev, check);
      I(rid == check);
      db.put_revision(rid, rev); // FIXME: handle various return values
      return encode_msg_put_rev_response();
    }
  else if (decode_msg_get_file_data_request(cmd_obj, fid))
    {
      db.get_file_version(fid, data);
      return encode_msg_get_file_data_response(data);
    }
  else if (decode_msg_put_file_data_request(cmd_obj, fid, data))
    {
      // this will check that the id is correct
      db.put_file(fid, data);
      return encode_msg_put_file_data_response();
    }
  else if (decode_msg_get_file_delta_request(cmd_obj, old_id, new_id))
    {
      db.get_arbitrary_file_delta(old_id, new_id, delta);
      return encode_msg_get_file_delta_response(delta);
    }
  else if (decode_msg_put_file_delta_request(cmd_obj, old_id, new_id, delta))
    {
      // this should also check that the delta applied to the data with old_id
      // produces data that matches the new_id. currently it looks like the database
      // does not enforce this though, so FIXME!
      db.put_file_version(old_id, new_id, delta);
      return encode_msg_put_file_delta_response();
    }
  else
    {
      string type, vers;
      decode_msg_header(cmd_obj, type, vers);
      std::cerr << "unknown request type: " << type
                << " version: " << vers
                << std::endl;
      return encode_msg_error("unknown request");
    }
}


void
process_json_request(database & db, 
                     http::request const & request, http::response & response)
{
  json_io::input_source in(request.body, "json");
  json_io::tokenizer tok(in);
  json_io::parser p(tok);
  json_io::json_object_t obj = p.parse_object();

  if (static_cast<bool>(obj))
    {
      transaction_guard guard(db);
      L(FL("read JSON object"));

      json_io::json_value_t res = do_cmd(db, obj); // process json request

      if (static_cast<bool>(res))
        {
          json_io::printer out_data;
          res->write(out_data);
          L(FL("sending JSON %d-byte response") % (out_data.buf.size()));

          response.version = http::version;
          response.status_code = 200;
          response.status_message = "OK";
          response.headers["Connection"] = "close";
          response.headers["Status"] = "200 OK";
          response.headers["Content-Length"] = lexical_cast<string>(out_data.buf.size());
          response.headers["Content-Type"] = "application/jsonrequest";
          response.body = out_data.buf;
        }
    }
  else
    {
      // FIXME: do something better for reporting errors from the server
      std::cerr << "parse error" << std::endl;
    }
}

void
process_request(database & db, http::connection & connection)
{
  http::request request;
  http::response response;

  if (connection.read(request))
    {
      try
        {
          if (request.method == http::post && 
              request.headers["Content-Type"] == "application/jsonrequest")
            process_json_request(db, request, response);
          else
            I(false);
        }
      catch (gserve_error & e)
        {
          std::cerr << "gserve error -- " << e.msg << std::endl;
          response.version = connection.version();
          response.status_code = 500;
          response.status_message = "Internal Server Error";
          response.headers["Status"] = "500 Internal Server Error";
        }
      catch (recoverable_failure & e)
        {
          std::cerr << "recoverable failure -- " << e.what() << std::endl;
          response.version = connection.version();
          response.status_code = 500;
          response.status_message = "Internal Server Error";
          response.headers["Status"] = "500 Internal Server Error";
        } 
    }
  else
    {
      std::cerr << "bad request" << std::endl;

      response.version = connection.version();
      response.status_code = 400;
      response.status_message = "Bad Request";
      response.headers["Status"] = "400 Bad Request";
    }

  connection.write(response);

}


CMD_NO_WORKSPACE(gserve,           // C
                 "gserve",         // name
                 "",               // aliases
                 CMD_REF(network), // parent
                 N_(""),                              // params
                 N_("Serves JSON connections over SCGI or HTTP"),  // abstract
                 "",                                  // desc
                 options::opts::pidfile |
                 options::opts::bind |
                 options::opts::bind_stdio |
                 options::opts::bind_http |
                 options::opts::no_transport_auth
                 )
{
  if (!args.empty())
    throw usage(execid);

  database db(app);
  key_store keys(app);

  size_t default_port = constants::default_scgi_port;
  if (app.opts.bind_http) 
    {
      default_port = constants::default_http_port;
    }

  if (app.opts.signing_key() == "")
    {
      rsa_keypair_id key;
      get_user_key(app.opts, app.lua, db, keys, key);
      app.opts.signing_key = key;
    }

  rsa_keypair_id key;  // still unused...
  if (app.opts.use_transport_auth)
    {
      E(app.lua.hook_persist_phrase_ok(), origin::user,
        F("need permission to store persistent passphrase (see hook persist_phrase_ok())"));
      get_user_key(app.opts, app.lua, db, keys, key);
    }
  else if (!app.opts.bind_stdio)
    W(F("The --no-transport-auth option is usually only used in combination with --stdio"));

//   if (app.opts.bind_stdio)
//     process_request(type, db, std::cin, std::cout);
//   else
    {

#ifdef USE_IPV6
      bool use_ipv6=true;
#else
      bool use_ipv6=false;
#endif
      // This will be true when we try to bind while using IPv6.  See comments
      // further down.
      bool try_again=false;

      do
        {
          try
            {
              try_again = false;

              Netxx::Address addr(use_ipv6);

              add_address_names(addr, app.opts.bind_uris, default_port);

              // If we use IPv6 and the initialisation of server fails, we want
              // to try again with IPv4.  The reason is that someone may have
              // downloaded a IPv6-enabled monotone on a system that doesn't
              // have IPv6, and which might fail therefore.
              // On failure, Netxx::NetworkException is thrown, and we catch
              // it further down.
              try_again=use_ipv6;

              Netxx::StreamServer server(addr);

              // If we came this far, whatever we used (IPv6 or IPv4) was
              // accepted, so we don't need to try again any more.
              try_again=false;

              while (true)
                {
                  Netxx::Peer peer = server.accept_connection();
                  if (peer)
                    {
                      P(F("connection from %s:%d:%d") 
                        % peer.get_address() % peer.get_port() % peer.get_local_port());
                      Netxx::Stream stream(peer.get_socketfd());
                      Netxx::Netbuf<constants::bufsz>  buf(stream);
                      std::iostream io(&buf);

                      // possibly this should loop until a Connection: close
                      // header is received although that's probably not
                      // right for scgi connections

                      if (app.opts.bind_http)
                        {
                          http::connection connection(io);
                          process_request(db, connection);
                        }
                      else
                        {
                          scgi::connection connection(io);
                          process_request(db, connection);
                        }
                      stream.close();
                    }
                  else
                    break;
                }
            }

          // Possibly loop around if we get exceptions from Netxx and we're
          // attempting to use ipv6, or have some other reason to try again.
          catch (Netxx::NetworkException &)
            {
              if (try_again)
                use_ipv6 = false;
              else
                throw;
            }
          catch (Netxx::Exception &)
            {
              if (try_again)
                use_ipv6 = false;
              else
                throw;
            }
        } while (try_again);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
