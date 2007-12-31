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

#include "cmd.hh"
#include "app_state.hh"
#include "ui.hh"
#include "lexical_cast.hh"
#include "lua.hh"
#include "lua_hooks.hh"
#include "json_io.hh"

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

namespace syms
{
  symbol const status("status");
  symbol const vers("vers");
  symbol const cmd("cmd");
  symbol const args("args");
  symbol const inquire("inquire");
  symbol const confirm("confirm");
  symbol const revs("revs");
  symbol const type("type");
};

static json_io::json_object_t
bad_req()
{
  json_io::builder b;

  b[syms::status].str("bad");

  return b.as_obj();
}

static json_io::json_object_t
do_cmd(app_state & app, json_io::json_object_t cmd_obj)
{

  string type, vers;
  json_io::query q(cmd_obj);

  if (! q[syms::type].get(type))
    return bad_req();

  L(FL("read JSON command type: %s") % type);

  if (type == "ping" && 
      q[syms::vers].get(vers) && 
      vers == "1")
    {
      json_io::builder b;
      json_io::builder args = b[syms::args].arr();
      
      size_t nargs = 0;
      if (q[syms::args].len(nargs)) 
        {
          for (size_t i = 0; i < nargs; ++i)
            args.add(q[syms::args][i].get());
        }
      return b.as_obj();
    }
  else if (type == syms::inquire() && 
           q[syms::vers].get(vers) && 
           vers == "1")
    {
      json_io::builder b;
      b[syms::type].str(syms::confirm());
      b[syms::vers].str("1");
      json_io::builder revs = b[syms::revs].arr();
      
      size_t nargs = 0;
      if (q[syms::revs].len(nargs)) 
        {
          app.db.ensure_open();
          std::string s;
          for (size_t i = 0; i < nargs; ++i)
            {
              if (q[syms::revs][i].get(s)) 
                {
                  if (app.db.revision_exists(revision_id(s)))
                    revs.add_str(s);
                }
            }
        }
      return b.as_obj();
    }
  else
    {
      return bad_req();
    }
  return cmd_obj;
}


CMD_NO_WORKSPACE(scgi,             // C
                 "scgi",           // name
                 "",               // aliases
                 CMD_REF(network), // parent
                 N_(""),                              // params
                 N_("Serves SCGI+JSON connections"),  // abstract
                 "",                                  // desc
                 options::opts::none                  // options
                 )
{
  // FIXME: expand this function to take a pathname for a win32 named pipe
  // or unix domain socket, for accept/read/dispatch loop.

  N(args.size() == 0,
    F("no arguments needed"));

  string data;

  if (parse_scgi(std::cin, data))
    {
      L(FL("read SCGI request: [[%s]]") % data);
      
      json_io::input_source in(data, "scgi");
      json_io::tokenizer tok(in);
      json_io::parser p(tok);
      json_io::json_object_t obj = p.parse_object();
      
      if (static_cast<bool>(obj)) 
        {
          L(FL("read JSON object"));
          
          json_io::json_object_t res = do_cmd(app, obj);
          if (static_cast<bool>(res))
            {
              L(FL("sending JSON response"));
              json_io::printer out;
              res->write(out);
              
              std::cout << "Status: 200 OK\r\n"
                        << "Content-Length: " << (out.buf.size() + 1) << "\r\n"
                        << "Content-Type: application/jsonrequest\r\n"
                        << "\r\n";
              
              std::cout.write(out.buf.data(), out.buf.size());  
              std::cout << "\n";
              return;
            }
        }
    }
  
  std::cout << "Status: 400 Bad request\r\n"
            << "Content-Type: application/jsonrequest\r\n"
            << "\r\n";
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
