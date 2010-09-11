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

#include "netxx/types.h"
#include "automate_ostream.hh"
#include "database.hh"
#include "options.hh"
#include <boost/shared_ptr.hpp>

struct globish;
struct server_initiated_sync_request;
struct uri_t;

struct netsync_connection_info;

typedef boost::shared_ptr<netsync_connection_info> shared_conn_info;

struct netsync_connection_info
{
  class Server
  {
  public:
    std::vector<utf8> addrs;
  } server;
  class Client
  {
    friend struct netsync_connection_info;

    bool connection_successful;

    bool use_argv;
    uri_t uri;
    std::vector<std::string> argv;

    globish include_pattern;
    globish exclude_pattern;

    connection_type conn_type;
    std::istream * input_stream;
    automate_ostream * output_stream;

    database db;
    options opts;

    Client(database & d, options const & o);
    ~Client();

    void set_raw_uri(std::string const & uri);
    void set_include_exclude_pattern(std::vector<arg_type> const & inc,
                                     std::vector<arg_type> const & pat);
    void maybe_set_argv(lua_hooks & lua);

    void ensure_completeness() const;

  public:
    std::istream & get_input_stream() const;
    automate_ostream & get_output_stream() const;
    void set_input_stream(std::istream & is);
    void set_output_stream(automate_ostream & os);
    Netxx::port_type get_port() const;
    globish get_include_pattern() const;
    globish get_exclude_pattern() const;
    uri_t get_uri() const;
    bool get_use_argv() const;
    std::vector<std::string> get_argv() const;
    connection_type get_connection_type() const;

    void set_connection_successful();

    size_t dryrun_incoming_revs;
    size_t dryrun_incoming_certs;
    size_t dryrun_incoming_keys;
    bool dryrun_incoming_keys_is_estimate;
    std::set<revision_id> dryrun_outgoing_revs;
    size_t dryrun_outgoing_certs;
    size_t dryrun_outgoing_keys;
  } client;

  static void
  setup_default(options const & opts,
                database & db,
                lua_hooks & lua,
                connection_type type,
                shared_conn_info & info);

  static void
  setup_from_sync_request(options const & opts,
                          database & db,
                          lua_hooks & lua,
                          server_initiated_sync_request const & request,
                          shared_conn_info & info);

  static void
  setup_from_uri(options const & opts,
                 database & db,
                 lua_hooks & lua,
                 connection_type type,
                 arg_type const & uri,
                 shared_conn_info & info);

  static void
  setup_from_server_and_pattern(options const & opts,
                                database & db,
                                lua_hooks & lua,
                                connection_type type,
                                arg_type const & host,
                                std::vector<arg_type> const & includes,
                                std::vector<arg_type> const & excludes,
                                shared_conn_info & info);

  static void
  setup_for_serve(options const & opts,
                  database & db,
                  lua_hooks & lua,
                  shared_conn_info & info);

private:
  netsync_connection_info(database & d, options const & o);

  static void
  parse_includes_excludes_from_query(std::string const & query,
                                     std::vector<arg_type> & includes,
                                     std::vector<arg_type> & excludes);
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
