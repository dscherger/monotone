// Copyright (C) 2008, 2014 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2014-2016 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include <functional>
#include <queue>

#include <asio.hpp>

#include "netsync.hh"
#include "app_state.hh"
#include "database.hh"
#include "lua.hh"
#include "key_store.hh"
#include "network/automate_session.hh"
#include "network/connection_info.hh"
#include "network/listener.hh"
#include "network/netsync_session.hh"
#include "network/stream.hh"
#include "network/session.hh"
#include "options.hh"
#include "platform.hh"
#include "project.hh"

using std::deque;
using std::make_shared;
using std::move;
using std::unique_ptr;
using std::shared_ptr;
using std::string;
using std::vector;

using boost::lexical_cast;

static std::function<void(server_initiated_sync_request && req)>
  trigger_server_initiated_sync;

/* FIXME: these shouldn't be quite as global... */
static unique_ptr<asio::io_service> global_ios;
static shared_ptr<transaction_guard> global_guard;
static std::function<bool()> global_is_listening;
static std::function<void()> global_start_listener;
static std::function<void()> global_stop_listener;

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

  I(trigger_server_initiated_sync);
  trigger_server_initiated_sync(move(request));
  return 0;
}

static void
call_server(app_state & app,
            project_t & project,
            key_store & keys,
            protocol_role role,
            shared_conn_info const & info,
            shared_conn_counts const & counts)
{
  shared_ptr<transaction_guard> guard(new transaction_guard(project.db));

  asio::io_service ios;

  // FIXME: apply timeout from constants::netsync_timeout_seconds

  P(F("connecting to '%s'") % info->client.get_uri().resource());
  P(F("  include pattern  '%s'") % info->client.get_include_pattern());
  P(F("  exclude pattern  '%s'") % info->client.get_exclude_pattern());

  shared_ptr<session> sess(new session(
    app, project, keys, guard,
    unique_ptr<abstract_stream>(
      abstract_stream::create_stream_for(info->client, ios)),
    client_voice));

  sess->set_base_uri(info->client.get_uri().resource());

  shared_ptr<wrapped_session> wrapped;
  switch (info->client.get_connection_type())
    {
    case netsync_connection:
      wrapped = make_shared<netsync_session>
        (sess.get(), app.opts, app.lua, project, keys, role,
         info->client.get_include_pattern(),
         info->client.get_exclude_pattern(),
         counts);
      break;
    case automate_connection:
      wrapped = make_shared<automate_session>
        (app, sess.get(),
         &info->client.get_input_stream(),
         &info->client.get_output_stream());
      break;
    }
  sess->set_inner(move(wrapped));

  sess->set_term_handler([sess](bool)
    {
      sess->stop();
    });

  // We set the connection handler just before starting the reactor, as this
  // may (in case of pipes) immediately trigger the connection handler.
  sess->set_stream_conn_handler();

  char const * err_info = NULL;
  try
    {
      ios.run();
    }
  catch (asio::system_error & e)
    {
      L(FL("caught network exception: %s") % e.what());
      err_info = e.what();
    }
  catch (std::runtime_error & e)
    {
      L(FL("caught runtime error: %s") % e.what());
      err_info = e.what();
    }

  // Commit whatever work we managed to accomplish anyways.
  guard->commit();

  // ensure that the tickers have finished and write any last ticks
  ui.ensure_clean_line();

  // Check the termination status of our session and report back to the
  // user. See the protocol state diagram in session::process_bye_cmd as
  // well.

  if (sess->protocol_state == session::confirmed_state)
    P(F("successful exchange with '%s'")
      % info->client.get_uri().host);
  else if (sess->encountered_error)
    P(F("peer '%s' disconnected after we informed them of error")
      % info->client.get_uri().host);
  else if (err_info)
    E(false, origin::network,
      F("I/O failure while talking to peer '%s' (%s), disconnecting")
      % info->client.get_uri().host % err_info);
  else
    E(false, origin::network,
      F("I/O failure while talking to peer '%s', disconnecting")
      % info->client.get_uri().host);

  // FIXME: in case of a decode exception or such, the old netxx code
  // committed work the client managed to accomplish anyways.
  //
  // Original comments for that case: We failed during processing. This
  // should only happen in client voice when we have a decode exception, or
  // received an error from our server (which is translated to a decode
  // exception). We call these cases E() errors.
  //
  // The error message was:
  // F("processing failure while talking to peer '%s', disconnecting")
  //   % sess->get_peer());
}

static void
session_from_sync_request(app_state & app,
                          project_t & project,
                          key_store & keys,
                          server_initiated_sync_request const & request)
{
  shared_conn_info info;
  netsync_connection_info::setup_from_sync_request(app.opts, project.db,
                                                   app.lua, request,
                                                   info);
  I(info->client.get_connection_type() == netsync_connection);

  P(F("connecting to '%s'") % info->client.get_uri().resource());

  /* FIXME: timeouts? */

  /* FIXME: copied from call_server... merge? */
  P(F("connecting to '%s'") % info->client.get_uri().resource());
  P(F("  include pattern  '%s'") % info->client.get_include_pattern());
  P(F("  exclude pattern  '%s'") % info->client.get_exclude_pattern());

  I(global_ios.get() != nullptr);
  I(global_guard.get() != nullptr);
  shared_ptr<session> sess(new session
    (app, project, keys, global_guard,
     unique_ptr<abstract_stream>
       (abstract_stream::create_stream_for(info->client, *global_ios)),
     client_voice));

  sess->set_base_uri(info->client.get_uri().resource());
  shared_ptr<wrapped_session> wrapped
    = make_shared<netsync_session>
        (sess.get(), app.opts, app.lua, project, keys, request.role,
         info->client.get_include_pattern(),
         info->client.get_exclude_pattern(),
         connection_counts::create(),
         true);
  sess->set_inner(move(wrapped));
  sess->set_stream_conn_handler();

  sess->set_term_handler([sess, info](bool success)
    {
      L(FL("netsync session with '%s' terminated.")
        % info->client.get_uri().resource());
      sess->stop();
    });

  sess->start();
  L(FL("Opened connection to %s") % sess->get_peer_name());
}

LUAEXT(server_set_listening, )
{
  if (lua_isboolean(LS, 1))
    {
      bool want_listen = lua_toboolean(LS, 1);
      if (want_listen && !global_is_listening())
        global_start_listener();
      else if (!want_listen && global_is_listening())
        global_stop_listener();
      return 0;
    }
  else
    return luaL_error(LS, "bad argument (not a boolean)");
}

static void
run_netsync_server(app_state & app,
                   options & opts, lua_hooks & lua,
                   project_t & project, key_store & keys,
                   protocol_role role,
                   shared_conn_info & info,
                   shared_conn_counts const & counts)
{
  global_ios = unique_ptr<asio::io_service>(new asio::io_service());
  global_guard = make_shared<transaction_guard>(project.db);

  // Install a handler for lua to call and request server initiated
  // runs of the netsync protocol.
  trigger_server_initiated_sync
    = [&app, &project, &keys]
    (server_initiated_sync_request && req)
    {
      session_from_sync_request(app,  project, keys, move(req));
    };

  if (opts.bind_stdio)
    {
      asio::local::stream_protocol proto;

      unique_ptr<abstract_stream> stream;
      try
        {
          stream = unique_ptr<abstract_stream>(
            new unix_local_stream(*global_ios, "stdio",
                                  STDIN_FILENO, STDOUT_FILENO));
        }
      catch (asio::system_error & e)
        {
          throw recoverable_failure(origin::network,
            (F("cannot open stdio: %s") % e.what()).str());
        }

      shared_ptr<session>
        sess(new session(app, project, keys, global_guard,
                         move(stream), server_voice));
      sess->start();

      try
        {
          global_ios->run();
          sess->stop();
        }
      catch (...)
        {
          // Commit whatever work we managed to accomplish anyways.
          global_guard->commit();
          global_guard.reset();
          throw;
        }

      global_guard->commit();
      global_guard.reset();
    }
  else
    {
      vector<host_port_pair> addrs;
      for (utf8 const & addr : info->server.addrs)
        addrs.push_back(split_address(addr()));

      listener listen(app, project, keys, global_guard,
                      *global_ios, role, move(addrs));

      global_start_listener = [&listen]()
        { listen.start_listening(); };

      global_stop_listener = [&listen]()
        { listen.stop_listening(); };

      global_is_listening = [&listen]() -> bool
        { return listen.is_listening(); };

      global_ios->run();
    }
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
        run_netsync_server(app, opts, lua, project, keys, role, info, counts);
      else
        {
          I(voice == client_voice);
          call_server(app, project, keys, role, info, counts);
          info->client.set_connection_successful();
        }
    }
  catch (asio::system_error & e)
    {
      throw recoverable_failure(origin::network,
                                (F("network error: %s") % e.what()).str());
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
