// Copyright (C) 2005 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __CONNECTION_INFO_HH__
#define __CONNECTION_INFO_HH__

#include <list>
#include <vector>

#include "automate_ostream.hh"
#include "globish.hh"
#include "uri.hh"
#include "vocab.hh"

struct netsync_connection_info
{
  struct Server
  {
    std::list<utf8> addrs;
  } server;
  enum conn_type
    {
      netsync_connection,
      automate_connection
    };
  struct Client
  {
    globish include_pattern;
    globish exclude_pattern;
    uri_t uri;
    utf8 unparsed;
    std::vector<std::string> argv;
    bool use_argv;
    conn_type connection_type;
  private:
    std::istream * input_stream;
    automate_ostream * output_stream;
  public:
    std::istream & get_input_stream() const;
    automate_ostream & get_output_stream() const;
    void set_input_stream(std::istream & is);
    void set_output_stream(automate_ostream & os);
    std::size_t get_port() const;
    Client();
  } client;
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
