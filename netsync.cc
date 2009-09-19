// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include <queue>

#include "netxx/sockopt.h"

#include "app_state.hh"
#include "database.hh"
#include "lua.hh"
#include "network/automate_listener.hh"
#include "network/netsync_listener.hh"
#include "network/netsync_session.hh"
#include "network/reactor.hh"
#include "options.hh"
#include "platform.hh"
#include "project.hh"

using std::deque;
using std::string;

using boost::lexical_cast;
using boost::shared_ptr;

struct server_initiated_sync_request
{
  string what;
  string address;
  string include;
  string exclude;
};
deque<server_initiated_sync_request> server_initiated_sync_requests;
LUAEXT(server_request_sync, )
{
  char const * w = luaL_checkstring(LS, 1);
  char const * a = luaL_checkstring(LS, 2);
  char const * i = luaL_checkstring(LS, 3);
  char const * e = luaL_checkstring(LS, 4);
  server_initiated_sync_request request;
  request.what = string(w);
  request.address = string(a);
  request.include = string(i);
  request.exclude = string(e);
  server_initiated_sync_requests.push_back(request);
  return 0;
}


static shared_ptr<Netxx::StreamBase>
build_stream_to_server(options & opts, lua_hooks & lua,
                       netsync_connection_info info,
                       Netxx::port_type default_port,
                       Netxx::Timeout timeout)
{
  shared_ptr<Netxx::StreamBase> server;

  if (info.client.use_argv)
    {
      I(!info.client.argv.empty());
      string cmd = info.client.argv[0];
      info.client.argv.erase(info.client.argv.begin());
      return shared_ptr<Netxx::StreamBase>
        (new Netxx::PipeStream(cmd, info.client.argv));
    }
  else
    {
#ifdef USE_IPV6
      bool use_ipv6=true;
#else
      bool use_ipv6=false;
#endif
      string host(info.client.uri.host);
      if (host.empty())
        host = info.client.unparsed();
      if (!info.client.uri.port.empty())
        default_port = lexical_cast<Netxx::port_type>(info.client.uri.port);
      Netxx::Address addr(info.client.unparsed().c_str(),
                          default_port, use_ipv6);
      return shared_ptr<Netxx::StreamBase>
        (new Netxx::Stream(addr, timeout));
    }
}

static void
call_server(options & opts,
            lua_hooks & lua,
            project_t & project,
            key_store & keys,
            protocol_role role,
            netsync_connection_info const & info)
{
  Netxx::PipeCompatibleProbe probe;
  transaction_guard guard(project.db);

  Netxx::Timeout timeout(static_cast<long>(constants::netsync_timeout_seconds)),
    instant(0,1);

  P(F("connecting to %s") % info.client.unparsed);

  shared_ptr<Netxx::StreamBase> server
    = build_stream_to_server(opts, lua,
                             info, constants::netsync_default_port,
                             timeout);


  // 'false' here means not to revert changes when the SockOpt
  // goes out of scope.
  Netxx::SockOpt socket_options(server->get_socketfd(), false);
  socket_options.set_non_blocking();

  shared_ptr<session> sess(new session(opts, lua, project, keys,
                                       role, client_voice,
                                       info.client.include_pattern,
                                       info.client.exclude_pattern,
                                       info.client.unparsed(), server));

  reactor react;
  react.add(sess, guard);

  while (true)
    {
      react.ready(guard);

      if (react.size() == 0)
        {
          // Commit whatever work we managed to accomplish anyways.
          guard.commit();

          // We failed during processing. This should only happen in
          // client voice when we have a decode exception, or received an
          // error from our server (which is translated to a decode
          // exception). We call these cases E() errors.
          E(false, origin::network,
            F("processing failure while talking to peer %s, disconnecting")
            % sess->peer_id);
          return;
        }

      bool io_ok = react.do_io();

      E(io_ok, origin::network,
        F("timed out waiting for I/O with peer %s, disconnecting")
        % sess->peer_id);

      if (react.size() == 0)
        {
          // Commit whatever work we managed to accomplish anyways.
          guard.commit();

          // We had an I/O error. We must decide if this represents a
          // user-reported error or a clean disconnect. See protocol
          // state diagram in session::process_bye_cmd.

          if (sess->protocol_state == session::confirmed_state)
            {
              P(F("successful exchange with %s")
                % sess->peer_id);
              return;
            }
          else if (sess->encountered_error)
            {
              P(F("peer %s disconnected after we informed them of error")
                % sess->peer_id);
              return;
            }
          else
            E(false, origin::network,
              (F("I/O failure while talking to peer %s, disconnecting")
               % sess->peer_id));
        }
    }
}

static shared_ptr<session>
session_from_server_sync_item(options & opts,
                              lua_hooks & lua,
                              project_t & project,
                              key_store & keys,
                              server_initiated_sync_request const & request)
{
  netsync_connection_info info;
  info.client.unparsed = utf8(request.address, origin::user);
  info.client.include_pattern = globish(request.include, origin::user);
  info.client.exclude_pattern = globish(request.exclude, origin::user);
  info.client.use_argv = false;
  parse_uri(info.client.unparsed(), info.client.uri,
            origin::user /* from lua hook */);

  try
    {
      P(F("connecting to %s") % info.client.unparsed);
      shared_ptr<Netxx::StreamBase> server
        = build_stream_to_server(opts, lua,
                                 info, constants::netsync_default_port,
                                 Netxx::Timeout(constants::netsync_timeout_seconds));

      // 'false' here means not to revert changes when
      // the SockOpt goes out of scope.
      Netxx::SockOpt socket_options(server->get_socketfd(), false);
      socket_options.set_non_blocking();

      protocol_role role = source_and_sink_role;
      if (request.what == "sync")
        role = source_and_sink_role;
      else if (request.what == "push")
        role = source_role;
      else if (request.what == "pull")
        role = sink_role;

      shared_ptr<session> sess(new session(opts, lua,
                                           project, keys,
                                           role, client_voice,
                                           info.client.include_pattern,
                                           info.client.exclude_pattern,
                                           info.client.unparsed(),
                                           server, true));

      return sess;
    }
  catch (Netxx::NetworkException & e)
    {
      P(F("Network error: %s") % e.what());
      return shared_ptr<session>();
    }
}

static void
serve_connections(app_state & app,
                  options & opts,
                  lua_hooks & lua,
                  project_t & project,
                  key_store & keys,
                  protocol_role role,
                  std::list<utf8> const & addresses)
{
#ifdef USE_IPV6
  bool use_ipv6=true;
#else
  bool use_ipv6=false;
#endif

  shared_ptr<transaction_guard> guard(new transaction_guard(project.db));

  reactor react;
  shared_ptr<listener> listen(new listener(opts, lua, project, keys,
                                           react, role, addresses,
                                           guard, use_ipv6));
  react.add(listen, *guard);

  if (!app.opts.bind_automate_uris.empty())
    {
      shared_ptr<automate_listener> al(new automate_listener(app, guard,
                                                             react, use_ipv6));
      react.add(al, *guard);
    }


  while (true)
    {
      if (!guard)
        guard = shared_ptr<transaction_guard>
          (new transaction_guard(project.db));
      I(guard);

      react.ready(*guard);

      while (!server_initiated_sync_requests.empty())
        {
          server_initiated_sync_request request
            = server_initiated_sync_requests.front();
          server_initiated_sync_requests.pop_front();
          shared_ptr<session> sess
            = session_from_server_sync_item(opts, lua,  project, keys,
                                            request);

          if (sess)
            {
              react.add(sess, *guard);
              L(FL("Opened connection to %s") % sess->peer_id);
            }
        }

      react.do_io();

      react.prune();

      if (react.size() == 1 /* 1 listener + 0 sessions */)
        {
          // Let the guard die completely if everything's gone quiet.
          guard->commit();
          guard.reset();
        }
    }
}

static void
serve_single_connection(project_t & project,
                        shared_ptr<session> sess)
{
  sess->begin_service();
  P(F("beginning service on %s") % sess->peer_id);

  transaction_guard guard(project.db);

  reactor react;
  react.add(sess, guard);

  while (react.size() > 0)
    {
      react.ready(guard);
      react.do_io();
      react.prune();
    }
  guard.commit();
}




void
run_netsync_protocol(app_state & app,
                     options & opts, lua_hooks & lua,
                     project_t & project, key_store & keys,
                     protocol_voice voice,
                     protocol_role role,
                     netsync_connection_info const & info)
{
  if (info.client.include_pattern().find_first_of("'\"") != string::npos)
    {
      W(F("include branch pattern contains a quote character:\n"
          "%s") % info.client.include_pattern());
    }

  if (info.client.exclude_pattern().find_first_of("'\"") != string::npos)
    {
      W(F("exclude branch pattern contains a quote character:\n"
          "%s") % info.client.exclude_pattern());
    }

  // We do not want to be killed by SIGPIPE from a network disconnect.
  ignore_sigpipe();

  try
    {
      if (voice == server_voice)
        {
          if (opts.bind_stdio)
            {
              shared_ptr<Netxx::PipeStream> str(new Netxx::PipeStream(0,1));
              shared_ptr<session> sess(new session(opts, lua, project, keys,
                                                   role, server_voice,
                                                   globish("*", origin::internal),
                                                   globish("", origin::internal),
                                                   "stdio", str));
              serve_single_connection(project, sess);
            }
          else
            serve_connections(app, opts, lua, project, keys,
                              role, info.server.addrs);
        }
      else
        {
          I(voice == client_voice);
          call_server(opts, lua, project, keys,
                      role, info);
        }
    }
  catch (Netxx::NetworkException & e)
    {
      throw recoverable_failure(origin::network,
                                (F("network error: %s") % e.what()).str());
    }
  catch (Netxx::Exception & e)
    {
      throw oops((F("network error: %s") % e.what()).str());;
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
