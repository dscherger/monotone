// Copyright (C) 2009 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include "http.hh"
#include "lexical_cast.hh"
#include "simplestring_xform.hh"

using std::getline;
using std::iostream;
using std::string;

using boost::lexical_cast;

typedef http::header_map::const_iterator header_iterator;

namespace http
{

  string
  connection::version()
  {
    return http::version;
  }

  bool
  connection::read(request & r)
  {
    bool good = 
      read(r.method, " ") &&
      read(r.uri, " ") &&
      read(r.version, "\r\n");
      
    if (good)
      L(FL("read http request: %s %s %s") % r.method % r.uri % r.version);
      
    return good && read_headers(r) && read_body(r);
  }

  void
  connection::write(request const & r)
  {
    L(FL("write http request: %s %s %s") % r.method % r.uri % r.version);
    write(r.method, " ");
    write(r.uri, " ");
    write(r.version, "\r\n");

    write_headers(r);
    write_body(r);
  }

  bool
  connection::read(response & r)
  {
    bool good =
      read(r.version, " ") &&
      read(r.status_code, " ") &&
      read(r.status_message, "\r\n");

    if (good)
      L(FL("read http response: %s %s %s") 
        % r.version % r.status_code % r.status_message);

    return good && read_headers(r) && read_body(r);
  }

  void
  connection::write(response const & r)
  {
    L(FL("write http response: %s %s %s") % r.version % r.status_code % r.status_message);
    write(r.version, " ");
    write(r.status_code, " ");
    write(r.status_message, "\r\n");

    write_headers(r);
    write_body(r);
  }

  bool
  connection::read(string & value, string const & end)
  {
    while (io.good() && value.rfind(end) == string::npos)
      value += static_cast<char>(io.get());

    if (value.rfind(end) == value.size() - end.size())
      {
        value.resize(value.size() - end.size());
        return true;
      }
    else
      return false;
  }

  bool
  connection::read(size_t & value, string const & end)
  {
    string tmp;
    bool good = read(tmp, end);
    value = lexical_cast<size_t>(tmp);
    return good;
  }

  void
  connection::write(string const & value, string const & end)
  {
    io << value << end;
  }

  void
  connection::write(size_t const value, string const & end)
  {
    io << value << end;
  }

  bool
  connection::read_headers(message & m)
  {
    m.headers.clear();
    while (io.good() && io.peek() != '\r')
      {
        string key, val;
        if (!read(key, ": ")) return false;
        if (!read(val, "\r\n")) return false;

        m.headers[key] = val;

        L(FL("read http header: %s: %s") % key % val);
      }

    L(FL("read http header end"));

    if (!io.good()) return false;

    char cr = static_cast<char>(io.get());
    char lf = static_cast<char>(io.get());

    return cr == '\r' && lf == '\n';

  }

  bool
  connection::read_body(message & m)
  {
    if (m.headers.find("Content-Length") == m.headers.end())
      return false;

    size_t length = lexical_cast<size_t>(m.headers["Content-Length"]);
    L(FL("reading http body: %d bytes") % length);

    m.body.clear();
    m.body.reserve(length);

    while (io.good() && (length > 0))
      {
        m.body += static_cast<char>(io.get());
        length--;
      }

    L(FL("read %d bytes, content length now %d") 
      % m.body.size() % length);

    L(FL("%s") % m.body);

    return (length == 0);
  }

  void
  connection::write_headers(message const & m)
  {
    for (header_iterator i = m.headers.begin(); i != m.headers.end(); ++i)
      {
        L(FL("write http header: %s: %s") % i->first % i->second);
        write(i->first, ": ");
        write(i->second, "\r\n");
      }

    L(FL("write http header end"));
    io << "\r\n";
  }

  void
  connection::write_body(message const & m)
  {
    L(FL("writing http body: %d bytes") % m.body.size());
    L(FL("%s") % m.body);

    io << m.body;
    io.flush();
  }

};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
