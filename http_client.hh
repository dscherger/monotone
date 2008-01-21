#ifndef __HTTP_CLIENT__
#define __HTTP_CLIENT__

// Copyright (C) 2007 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "netxx/netbuf.h"
#include "netxx/streambase.h"

#include "base.hh"
#include "constants.hh"
#include "json_io.hh"

#include <iostream>

#include <boost/shared_ptr.hpp>

struct app_state;
struct uri;
struct globish;


struct
http_client
{
  app_state & app;
  uri const & u;
  globish const & include_pattern;
  globish const & exclude_pattern;

  boost::shared_ptr<Netxx::StreamBase> stream;
  boost::shared_ptr< Netxx::Netbuf<constants::bufsz> > nb;
  boost::shared_ptr<std::iostream> io;
  bool open;

  http_client(app_state & app,
	      uri const & u, 	      
	      globish const & include_pattern,
	      globish const & exclude_pattern); 

  json_io::json_value_t transact_json(json_io::json_value_t v);
  void parse_http_status_line();
  void parse_http_header_line(size_t & content_length,
			      bool & keepalive);
  void parse_http_response(std::string & data);
  void crlf();  
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __HTTP_CLIENT__
