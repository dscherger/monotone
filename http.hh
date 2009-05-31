#ifndef __HTTP_HH__
#define __HTTP_HH__

// Copyright (C) 2009 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>
#include <map>

namespace http
{

  static std::string const version("HTTP/1.1");

  static std::string const post("POST");
  static std::string const get("GET");
  static std::string const put("PUT");

  namespace status
  {
    struct code
    {
      code() : value(0), message("") {}
      code(size_t value, std::string const message) : value(value), message(message) {}
      size_t value;
      std::string message;

      bool operator==(code const & other) const
      {
        return value == other.value;
      }

      bool operator!=(code const & other) const
      {
        return value != other.value;
      }
    };

    static const code ok(200, "OK");

    static const code bad_request(400, "Bad Request");
    static const code not_found(404, "Not Found");
    static const code method_not_allowed(405, "Method Not Allowed");
    static const code not_acceptable(406, "Not Acceptable");
    static const code length_required(411, "Length Required");

    static const code internal_server_error(500, "Internal Server Error");
    static const code not_implemented(501, "Not Implemented");
  }

  typedef std::map<std::string, std::string> header_map;
  typedef header_map::const_iterator header_iterator;

  struct message
  {
    header_map headers;
    std::string body;
  };

  // the first line of an http request is:
  // <method> <uri> <version> CR LF
  // GET /path HTTP/1.1

  struct request : public message
  {
    std::string method;
    std::string uri;
    std::string version;
  };

  // the first line of an http response is
  // <version> <status-code> <status-message> CR LF
  // HTTP/1.1 200 OK

  struct response : public message
  {
    std::string version;
    status::code status;
  };

  class connection
  {
  public:
    connection(std::iostream & io) : io(io) {}
    virtual ~connection() {};

    virtual std::string version();
    virtual status::code read(request & r);
    void write(request const & r);

    status::code read(response & r);
    virtual void write(response const & r);

  protected:
    std::iostream & io;
      
    bool read(std::string & value, std::string const & end);
    bool  read(size_t & value, std::string const & end);
      
    void write(std::string const & value, std::string const & end);
    virtual void write(size_t const value, std::string const & end);
      
    status::code read_headers(message & m);
    status::code read_body(message & m);
      
    void write_headers(message const & m);
    void write_body(message const & m);
  };

};

#endif // __HTTP_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
