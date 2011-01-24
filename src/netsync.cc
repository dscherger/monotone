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
#include "netsync.hh"

#include <queue>

#include "netxx/sockopt.h"
#include "netxx/stream.h"

#include "app_state.hh"
#include "database.hh"
#include "lua.hh"
#include "network/automate_session.hh"
#include "network/connection_info.hh"
#include "network/listener.hh"
#include "network/netsync_session.hh"
#include "network/reactor.hh"
#include "network/session.hh"
#include "options.hh"
#include "platform.hh"
#include "project.hh"

using std::deque;
using std::string;

using boost::lexical_cast;
using boost::shared_ptr;

deque<server_initiated_sync_request> server_initiated_sync_requests;
LUAEXT(server_request_sync, )
{
  char const * w = luaL_checkstring(LS, 1);
  char const * a = luaL_checkstring(LS, 2);
  char const * i = luaL_checkstring(LS, 3);
  char const * e = luaL_checkstring(LS, 4);

  server_initiated_sync_request request;
  request.address = string(a);
  request.include = string(i);
  request.exclude = string(e);

  request.role = source_and_sink_role;
  string what(w);
  if (what == "sync")
    request.role = source_and_sink_role;
  else if (what == "push")
    request.role = source_role;
  else if (what == "pull")
    request.role = sink_role;

  server_initiated_sync_requests.push_back(request);
  return 0;
}


static shared_ptr<Netxx::StreamBase>
build_stream_to_server(options & opts, lua_hooks & lua,
                       shared_conn_info const & info,
                       Netxx::Timeout timeout)
{
  shared_ptr<Netxx::StreamBase> server;

  if (info->client.get_use_argv())
    {
      vector<string> args = info->client.get_argv();
      I(!args.empty());
      string cmd = args[0];
      args.erase(args.begin());
      return shared_ptr<Netxx::StreamBase>
        (new Netxx::PipeStream(cmd, args));
    }
  else
    {
#ifdef USE_IPV6
      bool use_ipv6=true;
#else
      bool use_ipv6=false;
#endif
      string host(info->client.get_uri().host);
      I(!host.empty());
      Netxx::Address addr(host.c_str(),
                          info->client.get_port(),
                          use_ipv6);
      return shared_ptr<Netxx::StreamBase>
        (new Netxx::Stream(addr, timeout));
    }
}

static void
call_server(app_state & app,
            project_t & project,
            key_store & keys,
            protocol_role role,
            shared_conn_info const & info,
            shared_conn_counts const & counts)
{
  Netxx::PipeCompatibleProbe probe;
  transaction_guard guard(project.db);

  Netxx::Timeout timeout(static_cast<long>(constants::netsync_timeout_seconds)),
    instant(0,1);

  P(F("connecting to %s") % info->client.get_uri().resource());
  P(F("  include pattern  %s") % info->client.get_include_pattern());
  P(F("  exclude pattern  %s") % info->client.get_exclude_pattern());

  shared_ptr<Netxx::StreamBase> server
    = build_stream_to_server(app.opts, app.lua, info, timeout);

  // 'false' here means not to revert changes when the SockOpt
  // goes out of scope.
  Netxx::SockOpt socket_options(server->get_socketfd(), false);
  socket_options.set_non_blocking();

  shared_ptr<session> sess(new session(app, project, keys,
                                       client_voice,
                                       info->client.get_uri().resource(), server));
  shared_ptr<wrapped_session> wrapped;
  switch (info->client.get_connection_type())
    {
    case netsync_connection:
      wrapped.reset(new netsync_session(sess.get(),
                                        app.opts, app.lua, project,
                                        keys, role,
                                        info->client.get_include_pattern(),
                                        info->client.get_exclude_pattern(),
                                        counts));
      break;
    case automate_connection:
      wrapped.reset(new automate_session(app, sess.get(),
                                         &info->client.get_input_stream(),
                                         &info->client.get_output_stream()));
      break;
    }
  sess->set_inner(wrapped);

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
            % sess->get_peer());
          return;
        }

      bool io_ok = react.do_io();

      E(io_ok, origin::network,
        F("timed out waiting for I/O with peer %s, disconnecting")
        % sess->get_peer());

      if (react.size() == 0)
        {
          // Commit whatever work we managed to accomplish anyways.
          guard.commit();

          // ensure that the tickers have finished and write any last ticks
          ui.ensure_clean_line();

          // We had an I/O error. We must decide if this represents a
          // user-reported error or a clean disconnect. See protocol
          // state diagram in session::process_bye_cmd.

          if (sess->protocol_state == session_base::confirmed_state)
            {
              P(F("successful exchange with %s")
                % sess->get_peer());
              return;
            }
          else if (sess->encountered_error)
            {
              P(F("peer %s disconnected after we informed them of error")
                % sess->get_peer());
              return;
            }
          else
            E(false, origin::network,
              (F("I/O failure while talking to peer %s, disconnecting")
               % sess->get_peer()));
        }
    }
}

static shared_ptr<session>
session_from_server_sync_item(app_state & app,
                              project_t & project,
                              key_store & keys,
                              server_initiated_sync_request const & request)
{
  shared_conn_info info;
  netsync_connection_info::setup_from_sync_request(app.opts, project.db,
                                                   app.lua, request,
                                                   info);

  try
    {
      P(F("connecting to %s") % info->client.get_uri().resource());
      shared_ptr<Netxx::StreamBase> server
        = build_stream_to_server(app.opts, app.lua, info,
                                 Netxx::Timeout(constants::netsync_timeout_seconds));

      // 'false' here means not to revert changes when
      // the SockOpt goes out of scope.
      Netxx::SockOpt socket_options(server->get_socketfd(), false);
      socket_options.set_non_blocking();

      shared_ptr<session>
        sess(new session(app, project, keys,
                         client_voice,
                         info->client.get_uri().resource(), server));
      shared_ptr<wrapped_session>
        wrapped(new netsync_session(sess.get(),
                                    app.opts, app.lua, project,
                                    keys, request.role,
                                    info->client.get_include_pattern(),
                                    info->client.get_exclude_pattern(),
                                    connection_counts::create(),
                                    true));
      sess->set_inner(wrapped);
      return sess;
    }
  catch (Netxx::NetworkException & e)
    {
      P(F("Network error: %s") % e.what());
      return shared_ptr<session>();
    }
}

enum listener_status { listener_listening, listener_not_listening };
listener_status desired_listener_status;
LUAEXT(server_set_listening, )
{
  if (lua_isboolean(LS, 1))
    {
      bool want_listen = lua_toboolean(LS, 1);
      if (want_listen)
        desired_listener_status = listener_listening;
      else
        desired_listener_status = listener_not_listening;
      return 0;
    }
  else
    {
      return luaL_error(LS, "bad argument (not a boolean)");
    }
}

static void
serve_connections(app_state & app,
                  options & opts,
                  lua_hooks & lua,
                  project_t & project,
                  key_store & keys,
                  protocol_role role,
                  std::vector<utf8> const & addresses)
{
#ifdef USE_IPV6
  bool use_ipv6=true;
#else
  bool use_ipv6=false;
#endif

  shared_ptr<transaction_guard> guard(new transaction_guard(project.db));

  reactor react;
  shared_ptr<listener> listen(new listener(app, project, keys,
                                           react, role, addresses,
                                           guard, use_ipv6));
  react.add(listen, *guard);
  desired_listener_status = listener_listening;
  listener_status actual_listener_status = listener_listening;

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
            = session_from_server_sync_item(app,  project, keys,
                                            request);

          if (sess)
            {
              react.add(sess, *guard);
              L(FL("Opened connection to %s") % sess->get_peer());
            }
        }

      if (desired_listener_status != actual_listener_status)
        {
          switch (desired_listener_status)
            {
            case listener_listening:
              react.add(listen, *guard);
              actual_listener_status = listener_listening;
              break;
            case listener_not_listening:
              react.remove(listen);
              actual_listener_status = listener_not_listening;
              break;
            }
        }
      if (!react.size())
        break;

      react.do_io();

      react.prune();

      int num_sessions;
      if (actual_listener_status == listener_listening)
        num_sessions = react.size() - 1;
      else
        num_sessions = react.size();
      if (num_sessions == 0)
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
  P(F("beginning service on %s") % sess->get_peer());

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
                     shared_conn_info & info,
                     shared_conn_counts const & counts)
{
  // We do not want to be killed by SIGPIPE from a network disconnect.
  ignore_sigpipe();

  try
    {
      if (voice == server_voice)
        {
          if (opts.bind_stdio)
            {
              shared_ptr<Netxx::PipeStream> str(new Netxx::PipeStream(0,1));

              shared_ptr<session>
                sess(new session(app, project, keys,
                                 server_voice,
                                 "stdio", str));
              serve_single_connection(project, sess);
            }
          else
            serve_connections(app, opts, lua, project, keys,
                              role, info->server.addrs);
        }
      else
        {
          I(voice == client_voice);
          call_server(app, project, keys, role, info, counts);
          info->client.set_connection_successful();
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
