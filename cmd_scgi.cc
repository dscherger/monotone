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
#include "json_io.hh"
#include "json_msgs.hh"
#include "keys.hh"
#include "key_store.hh"
#include "lexical_cast.hh"
#include "lua.hh"
#include "lua_hooks.hh"
#include "net_common.hh"
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
// header, and then a body consisting of a JSON object.
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
// a bare CRLF, and a JSON object.
//
// This response format is not specified by the SCGI "spec".
//


struct scgi_error
{
  string msg;
  scgi_error(string const & s): msg(s) {}
};

// Consume string until null or EOF. Consumes trailing null.
static string
parse_str(istream & in)
{
  string s;
  while (in.good())
    {
      char ch = static_cast<char>(in.get());
      if (ch == '\0')
        break;
      s += ch;
    }
  return s;
}

static inline bool
eat(istream & in, char c)
{
  if (!in.good())
    return false;
  int i = in.get();
  return c == static_cast<char>(i);
}

static bool
parse_scgi(istream & in, string & data)
{

  if (!in.good()) return false;

  size_t netstring_len;
  in >> netstring_len;
  if (!in.good()) return false;

  L(FL("scgi: netstring length: %d") % netstring_len);
  if (!eat(in, ':')) return false;

  // although requirements of the scgi spec,
  // the code below will allow requests that:
  // - don't have CONTENT_LENGTH as the first header
  // - don't have a CONTENT_LENGTH header at all
  // - don't have an SCGI header
  // perhaps this is just to "be liberal in what you accept"
  // but we may want to tighten it up a bit. at the very least
  // this is relying on the CONTENT_LENGTH header.

  size_t content_length = 0;
  while (netstring_len > 0)
    {
      string key = parse_str(in);
      string val = parse_str(in);

      L(FL("scgi: got header: %s -> %s") % key % val);
      if (key == "CONTENT_LENGTH")
        {
          content_length = lexical_cast<size_t,string>(val);
          L(FL("scgi: content length: %d") % content_length);
        }
      else if (key == "SCGI" && val != "1")
        return false;

      netstring_len -= key.size();
      netstring_len -= val.size();
      netstring_len -= 2;
    }

  if(!eat(in, ',')) return false;

  data.clear();
  data.reserve(content_length);
  L(FL("reading %d bytes") % content_length);

  while (in.good() && (content_length > 0))
    {
      data += static_cast<char>(in.get());
      content_length--;
    }

  L(FL("read %d bytes, content_length now %d") % data.size() % content_length);

  return (content_length == 0);
}

static json_io::json_value_t
do_cmd(database & db, json_io::json_object_t cmd_obj)
{
  set<revision_id> request_revs;

  revision_id rid;
  revision_t rev;
  file_id fid, old_id, new_id;
  file_data data;
  file_delta delta;

  if (decode_msg_inquire_request(cmd_obj, request_revs))
    {
      L(FL("inquiring %d revisions") % request_revs.size());
      db.ensure_open();
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
      db.ensure_open();
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
  else if (decode_msg_put_rev_request(cmd_obj, rid, rev))
    {
      revision_id check;
      calculate_ident(rev, check);
      I(rid == check);
      return encode_msg_put_rev_response();
    }
  else if (decode_msg_put_file_data_request(cmd_obj, fid, data))
    {
      // this will check that the id is correct
      // db.put_file(fid, data);
      return encode_msg_put_file_data_response();
    }
  else if (decode_msg_put_file_delta_request(cmd_obj, old_id, new_id, delta))
    {
      // this should also check that the delta applied to the data with old_id
      // produces data that matches the new_id. currently it looks like the database
      // does not enforce this though, so FIXME!
      // db.put_file_version(old_id, new_id, delta);
      return encode_msg_put_file_delta_response();
    }
  else
    {
      return encode_msg_error("unknown request");
    }
}


void
process_scgi_transaction(database & db,
                         std::istream & in,
                         std::ostream & out)
{
  string data;

  try
    {
      if (!parse_scgi(in, data))
        throw scgi_error("unable to parse SCGI request");

      L(FL("read %d-byte SCGI request") % data.size());

      json_io::input_source in(data, "scgi");
      json_io::tokenizer tok(in);
      json_io::parser p(tok);
      json_io::json_object_t obj = p.parse_object();

      if (static_cast<bool>(obj))
        {
          transaction_guard guard(db);
          L(FL("read JSON object"));

          json_io::json_value_t res = do_cmd(db, obj);
          if (static_cast<bool>(res))
            {
              json_io::printer out_data;
              res->write(out_data);
              L(FL("sending JSON %d-byte response") % (out_data.buf.size() + 1));

              out << "Status: 200 OK\r\n"
                  << "Content-Length: " << (out_data.buf.size() + 1) << "\r\n"
                  << "Content-Type: application/jsonrequest\r\n"
                  << "\r\n";

              out.write(out_data.buf.data(), out_data.buf.size());
              out << "\n";
              out.flush();
              return;
            }
        }
      else
        {
          // FIXME: do something better for reporting errors from the server
          std::cerr << "parse error" << std::endl;
        }
    }
  catch (scgi_error & e)
    {
      std::cerr << "scgi error -- " << e.msg << std::endl;
      out << "Status: 400 Bad request\r\n"
          << "Content-Type: application/jsonrequest\r\n"
          << "\r\n";
      out.flush();
    }
  catch (informative_failure & e)
    {
      std::cerr << "informative failure -- " << e.what() << std::endl;
      out << "Status: 400 Bad request\r\n"
          << "Content-Type: application/jsonrequest\r\n"
          << "\r\n";
      out.flush();
    }

}


CMD_NO_WORKSPACE(scgi,             // C
                 "scgi",           // name
                 "",               // aliases
                 CMD_REF(network), // parent
                 N_(""),                              // params
                 N_("Serves SCGI+JSON connections"),  // abstract
                 "",                                  // desc
                 options::opts::scgi_bind |
                 options::opts::pidfile |
                 options::opts::bind_stdio |
                 options::opts::no_transport_auth
                 )
{
  if (!args.empty())
    throw usage(execid);

  database db(app);
  key_store keys(app);

  if (app.opts.signing_key() == "")
    {
      rsa_keypair_id key;
      get_user_key(app.opts, app.lua, db, keys, key);
      app.opts.signing_key = key;
    }

  rsa_keypair_id key;  // still unused...
  if (app.opts.use_transport_auth)
    {
      N(app.lua.hook_persist_phrase_ok(),
        F("need permission to store persistent passphrase (see hook persist_phrase_ok())"));
      get_user_key(app.opts, app.lua, db, keys, key);
    }
  else if (!app.opts.bind_stdio)
    W(F("The --no-transport-auth option is usually only used in combination with --stdio"));

  if (app.opts.bind_stdio)
    process_scgi_transaction(db, std::cin, std::cout);
  else
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

              add_address_names(addr, app.opts.bind_uris, constants::default_scgi_port);

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
                      Netxx::Stream stream(peer.get_socketfd());
                      Netxx::Netbuf<constants::bufsz>  buf(stream);
                      std::iostream io(&buf);
                      process_scgi_transaction(db, io, io);
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
