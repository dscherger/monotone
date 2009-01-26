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
#include "gsync.hh"
#include "json_io.hh"

#include <iostream>

#include <boost/shared_ptr.hpp>

struct options;
struct netsync_connection_info;
class lua_hooks;

struct http_client
{

  options & opts;
  lua_hooks & lua;
  netsync_connection_info const & info;

  boost::shared_ptr<Netxx::StreamBase> stream;
  boost::shared_ptr< Netxx::Netbuf<constants::bufsz> > nb;
  boost::shared_ptr<std::iostream> io;
  bool open;

  http_client(options & opts, lua_hooks & lua,
              netsync_connection_info const & info);

  void execute(std::string const & request, std::string & response);

  void parse_http_status_line();
  void parse_http_header_line(size_t & content_length,
                              bool & keepalive);
  void parse_http_response(std::string & data);
  void crlf();
};

class json_channel
  : public channel
{
  http_client & client;
public:
  json_channel(http_client & c)
    : client(c)
    { };

  json_io::json_value_t transact(json_io::json_value_t v) const;
  
  virtual void inquire_about_revs(std::set<revision_id> const & query_set,
                                    std::set<revision_id> & theirs) const;
  virtual void get_descendants(std::set<revision_id> const & common_revs,
                               std::vector<revision_id> & inbound_revs) const;

  virtual void push_full_rev(revision_id const & rid,
                             revision_t const & rev,
                             std::vector<file_data_record> const & data_records,
                             std::vector<file_delta_record> const & delta_records) const;

  virtual void pull_full_rev(revision_id const & rid,
                             revision_t & rev,
                             std::vector<file_data_record> & data_records,
                             std::vector<file_delta_record> & delta_records) const;

  virtual void push_file_data(file_id const & id,
                              file_data const & data) const;
  virtual void push_file_delta(file_id const & old_id,
                               file_id const & new_id,
                               file_delta const & delta) const;

  virtual void push_rev(revision_id const & rid, revision_t const & rev) const;
  virtual void pull_rev(revision_id const & rid, revision_t & rev) const;

  virtual void pull_file_data(file_id const & id,
                              file_data & data) const;
  virtual void pull_file_delta(file_id const & old_id,
                               file_id const & new_id,
                               file_delta & delta) const;
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __HTTP_CLIENT__
