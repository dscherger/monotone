#include "base.hh"

#include "netio.hh"
#include "network.hh"
#include "sanity.hh"
#include "uri.hh"

#include <algorithm>
using std::min;

#include <map>
using std::make_pair;
using std::map;
using std::pair;

#include <queue>
using std::deque;

#include <set>
using std::set;

#include <string>
using std::string;

#include <vector>
using std::vector;

#include <boost/lexical_cast.hpp>
using boost::lexical_cast;


#include "netxx/address.h"
#include "netxx/peer.h"
#include "netxx/probe.h"
#include "netxx/socket.h"
#include "netxx/sockopt.h"
#include "netxx/stream.h"
#include "netxx/streamserver.h"
#include "netxx/timeout.h"
#include "netxx_pipe.hh"

typedef map<int, service*> service_map;
static service_map & get_service_map()
{
  static service_map m;
  return m;
}


class input_manager
{
  string_queue buffer;
  netcmd cmd;
  bool have_cmd;

  chained_hmac read_hmac;

public:
  input_manager(bool use_transport_auth)
    : have_cmd(false),
      read_hmac(netsync_session_key(constants::netsync_key_initializer),
                use_transport_auth)
  {}
  inline bool full() const
  {
    return buffer.size() >= constants::netcmd_maxsz;
  }
  inline bool have_netcmd()
  {
    if (!have_cmd)
      {
	have_cmd = cmd.read(buffer, read_hmac);
      }
    return have_cmd;
  }
  inline void discard_netcmd()
  {
    I(have_cmd);
    have_cmd = false;
  }
  inline void peek_netcmd(netcmd & c)
  {
    I(have_cmd);
    c = cmd;
  }
  inline void get_netcmd(netcmd & c)
  {
    peek_netcmd(c);
    discard_netcmd();
  }
  inline size_t size() const
  {
    return buffer.size() + (have_cmd ? cmd.encoded_size() : 0);
  }
  inline void set_hmac_key(netsync_session_key const & key)
  {
    read_hmac.set_key(key);
  }
  Netxx::signed_size_type read_some_from(shared_ptr<Netxx::StreamBase> str)
  {
    I(!full());
    char tmp[constants::bufsz];
    Netxx::signed_size_type count = str->read(tmp, sizeof(tmp));
    
    if (count > 0)
      {
        buffer.append(tmp,count);
      }

    return count;
  }
};

class output_manager
{
  // deque of pair<string data, size_t cur_pos>
  deque< pair<string,size_t> > buffer;
  // the total data stored in outbuf - this is
  // used as a valve to stop too much data
  // backing up
  size_t buffer_size;

  chained_hmac write_hmac;

public:
  output_manager(bool use_transport_auth)
    : buffer_size(0),
      write_hmac(netsync_session_key(constants::netsync_key_initializer),
                 use_transport_auth)
  {}
  inline bool full() const
  {
    return buffer_size > constants::bufsz * 10;
  }
  inline bool empty() const
  {
    return buffer_size == 0;
  }
  inline void set_hmac_key(netsync_session_key const & key)
  {
    write_hmac.set_key(key);
  }
  void queue_netcmd(netcmd const & cmd)
  {
    string buf;
    cmd.write(buf, write_hmac);
    buffer.push_back(make_pair(buf, 0));
    buffer_size += buf.size();
  }
  Netxx::signed_size_type write_some_to(shared_ptr<Netxx::StreamBase> str)
  {
    I(!buffer.empty());
    string & to_write(buffer.front().first);
    size_t & writepos(buffer.front().second);
    size_t writelen = to_write.size() - writepos;
    Netxx::signed_size_type count = str->write(to_write.data() + writepos,
                                               min(writelen,
                                                   constants::bufsz));
    if (count > 0)
      {
        if ((size_t)count == writelen)
          {
            buffer_size -= to_write.size();
            buffer.pop_front();
          }
        else
          {
            writepos += count;
          }
      }
    return count;
  }

};

class session
{
  session(session const & other)
    : my_voice(other.my_voice), input(false), output(false), app(other.app)
  { I(false); }
  session const & operator=(session const &)
  { I(false); }
public:
  protocol_voice const my_voice;
  input_manager input;
  output_manager output;

  app_state & app;
  string const peer_id;
  shared_ptr<Netxx::StreamBase> str;
  service * srv;

  time_t last_io_time;


  session(protocol_voice voice,
          shared_ptr<Netxx::StreamBase> str,
          utf8 const & addr,
          app_state & app)
    : my_voice(voice), input(true), output(true),
      app(app), peer_id(addr()), str(str)
  {
  }


  void queue(netcmd const & cmd)
  {
    output.queue_netcmd(cmd);
  }

  bool can_send() const
  {
    return !output.full();
  }

  // at this level, process() includes to take from the input queue
  bool can_process();
  state process(transaction_guard & guard);

  state read_some()
  {
    input.read_some_from(str);
    return state::NONE;
  }

  state write_some()
  {
    output.write_some_to(str);
    return state::NONE;
  }

  Netxx::Probe::ready_type which_events();

  // This should be private.
  void set_session_key(netsync_session_key const & key)
  {
    input.set_hmac_key(key);
    output.set_hmac_key(key);
  }
private:
  state handle_ctrl_cmd(netcmd const & cmd);
};


state
run_network_loop(bool client,
                 shared_ptr<Netxx::StreamServer> server,
                 map<Netxx::socket_type, shared_ptr<session> > & sessions,
                 app_state & app);

state
run_network_loop(shared_ptr<session> sess)
{
  map<Netxx::socket_type, shared_ptr<session> > sessions;

  // Very similar to serve_single_on_stdio().
  if (sess->str->get_socketfd() == -1)
    {
      // Unix pipes are non-duplex, have two filedescriptors
      shared_ptr<Netxx::PipeStream> pipe =
        boost::dynamic_pointer_cast<Netxx::PipeStream, Netxx::StreamBase>(sess->str);
      I(pipe);
      sessions[pipe->get_writefd()]=sess;
      sessions[pipe->get_readfd()]=sess;
    }
  else
    sessions[sess->str->get_socketfd()]=sess;

  return run_network_loop(true,
                          shared_ptr<Netxx::StreamServer>(),
                          sessions,
                          sess->app);
}

service::service(int num)
  : sess(0)
{
  if (num)
    {
      service_map & m(get_service_map());
      pair<service_map::iterator, bool> r = m.insert(make_pair(num, this));
      I(r.second);
    }
}

service::~service()
{
}

shared_ptr<service>
service::get(int num)
{
  service_map & m(get_service_map());
  service_map::const_iterator i = m.find(num);
  I(i != m.end());
  return shared_ptr<service>(i->second->copy());
}

void
service::attach(session & s)
{
  sess = &s;
}

void
service::detach(bool received_error)
{
  sess = 0;
  detached(received_error);
}

void
service::detached(bool received_error)
{
}

void
service::_set_session_key(netsync_session_key const & key)
{
  I(sess);
  sess->set_session_key(key);
}

bool
service::can_receive()
{
  return true;
}

bool
service::can_process()
{
  return false;
}

void
service::send(netcmd const & cmd)
{
  I(sess);
  sess->queue(cmd);
}

bool
service::can_send() const
{
  I(sess);
  return sess->can_send();
}


client_session::client_session(utf8 const & address, app_state & app)
{
  ignore_sigpipe();

  shared_ptr<Netxx::StreamBase> server;
  uri u;
  vector<string> argv;
  parse_uri(address(), u);
  if (app.lua.hook_get_netsync_connect_command(u,
                                               global_sanity.debug_p(),
                                               argv))
    {
      I(argv.size() > 0);
      string cmd = argv[0];
      argv.erase(argv.begin());
      app.opts.use_transport_auth = app.lua.hook_use_transport_auth(u);
      server.reset(new Netxx::PipeStream(cmd, argv));
    }
  else
    {
#ifdef USE_IPV6
      bool use_ipv6=true;
#else
      bool use_ipv6=false;
#endif
      Netxx::Address addr(address().c_str(),
                          static_cast<Netxx::port_type>(constants::netsync_default_port),
                          use_ipv6);
      Netxx::Timeout timeout(static_cast<long>(constants::netsync_timeout_seconds));
      server.reset(new Netxx::Stream(addr,
                                     timeout));
    }

  impl.reset(new session(client_voice, server, address, app));
}

client_session::client_session(client_session const & other)
{
  I(false);
}
client_session const &
client_session::operator = (client_session const & other)
{
  I(false);
}


bool
client_session::authenticate_as(netsync_session_key const & key)
{
  // This will eventually involve running the network loop a couple times.
  I(impl);
  I(impl->my_voice == client_voice);
  impl->input.set_hmac_key(key);
  impl->output.set_hmac_key(key);
  return true;
}

state
client_session::request_service(service * newsrv)
{
  I(impl->my_voice == client_voice);
  if (impl->srv)
    {
      impl->srv->detach(false);
    }
  newsrv->attach(*impl);
  impl->srv = newsrv;
  impl->srv->request_service();

  return run_network_loop(impl);
}

bool
session::can_process()
{
  if (input.have_netcmd())
    return !srv || srv->can_receive();
  if (srv)
    return srv->can_process();
  return false;
}

bool netcmd_is_ctrl(netcmd const & cmd)
{
  return cmd.get_cmd_code() == anonymous_cmd
    || cmd.get_cmd_code() == auth_cmd;
}

state
session::handle_ctrl_cmd(netcmd const & cmd)
{
  service *s = get_service_map()[service_numbers::netsync];
  I(s);
  if (srv)
    {
      srv->detach(false);
    }
  srv = s;
  srv->attach(*this);
  srv->begin_service();
  return state::RUNNING;
}

state
session::process(transaction_guard & guard)
{
  bool have = input.have_netcmd();
  if (have)
    {
      netcmd cmd;
      input.get_netcmd(cmd);
      if (netcmd_is_ctrl(cmd))
        {
          return handle_ctrl_cmd(cmd);
        }
      else if (srv && srv->can_receive())
        return srv->received(cmd, guard);
      else if (!srv)
        return state::RUNNING;
      else
        I(false);
    }
  else if (srv && srv->can_process())
    {
      return srv->process(guard);
    }
  else
    I(false);
}

Netxx::Probe::ready_type
session::which_events()
{
  Netxx::Probe::ready_type which = Netxx::Probe::ready_oobd;
  // Don't ask to read if we still have unprocessed input.
  if (!input.full() && !input.have_netcmd())
    {
      which = which | Netxx::Probe::ready_read;
    }
  if (!output.empty())
    {
      which = which | Netxx::Probe::ready_write;
    }

  return which;
}
////////////////////////////////////////////////////////////////////////

static void
drop_session_associated_with_fd(map<Netxx::socket_type, shared_ptr<session> > & sessions,
                                Netxx::socket_type fd)
{
  // This is a bit of a hack. Initially all "file descriptors" in
  // netsync were full duplex, so we could get away with indexing
  // sessions by their file descriptor.
  //
  // When using pipes in unix, it's no longer true: a session gets
  // entered in the session map under its read pipe fd *and* its write
  // pipe fd. When we're in such a situation the socket fd is "-1" and
  // we downcast to a PipeStream and use its read+write fds.
  //
  // When using pipes in windows, we use a full duplex pipe (named
  // pipe) so the socket-like abstraction holds.

  I(fd != -1);
  map<Netxx::socket_type, shared_ptr<session> >::const_iterator i = sessions.find(fd);
  I(i != sessions.end());
  shared_ptr<session> sess = i->second;
  fd = sess->str->get_socketfd();
  if (fd != -1)
    {
      sessions.erase(fd);
    }
  else
    {
      shared_ptr<Netxx::PipeStream> pipe =
        boost::dynamic_pointer_cast<Netxx::PipeStream, Netxx::StreamBase>(sess->str);
      I(static_cast<bool>(pipe));
      I(pipe->get_writefd() != -1);
      I(pipe->get_readfd() != -1);
      sessions.erase(pipe->get_readfd());
      sessions.erase(pipe->get_writefd());
    }
}

static void
arm_sessions_and_calculate_probe(Netxx::PipeCompatibleProbe & probe,
                                 map<Netxx::socket_type, shared_ptr<session> > & sessions,
                                 set<Netxx::socket_type> & armed_sessions)
{
  set<Netxx::socket_type> arm_failed;
  for (map<Netxx::socket_type,
         shared_ptr<session> >::const_iterator i = sessions.begin();
       i != sessions.end(); ++i)
    {
      try
        {
          if (i->second->can_process())
            {
              L(FL("fd %d is armed") % i->first);
              armed_sessions.insert(i->first);
            }
          probe.add(*i->second->str, i->second->which_events());
        }
      catch (bad_decode & bd)
        {
          W(F("protocol error while processing peer %s: '%s', marking as bad")
            % i->second->peer_id % bd.what);
          arm_failed.insert(i->first);
        }
    }
  for (set<Netxx::socket_type>::const_iterator i = arm_failed.begin();
       i != arm_failed.end(); ++i)
    {
      drop_session_associated_with_fd(sessions, *i);
    }
}

static void
handle_new_connection(Netxx::StreamServer & server,
                      Netxx::Timeout & timeout,
                      map<Netxx::socket_type, shared_ptr<session> > & sessions,
                      app_state & app)
{
  L(FL("accepting new connection"));
  Netxx::Peer client = server.accept_connection();

  if (!client)
    {
      L(FL("accept() returned a dead client"));
    }
  else
    {
      P(F("accepted new client connection from %s : %s")
        % client.get_address() % lexical_cast<string>(client.get_port()));

      // 'false' here means not to revert changes when the SockOpt
      // goes out of scope.
      Netxx::SockOpt socket_options(client.get_socketfd(), false);
      socket_options.set_non_blocking();

      shared_ptr<Netxx::StreamBase> str
        (new Netxx::Stream(client.get_socketfd(), timeout));

      /*
      shared_ptr<session> sess(new session(source_and_sink_role, server_voice,
                                           "*", "",
                                           app,
                                           ulexical_cast<string>(client), str));
      sess->begin_service();
      */
      shared_ptr<session> sess(new session(server_voice, str,
                                           utf8(lexical_cast<string>(client)),
                                           app));
      sessions.insert(make_pair(client.get_socketfd(), sess));
    }
}

static void
handle_read_available(Netxx::socket_type fd,
                      shared_ptr<session> sess,
                      map<Netxx::socket_type, shared_ptr<session> > & sessions,
                      set<Netxx::socket_type> & armed_sessions,
                      bool & live_p)
{
  if (sess->read_some())
    {
      try
        {
          if (sess->can_process())
            armed_sessions.insert(fd);
        }
      catch (bad_decode & bd)
        {
          W(F("protocol error while processing peer %s: '%s', disconnecting")
            % sess->peer_id % bd.what);
          drop_session_associated_with_fd(sessions, fd);
          live_p = false;
        }
    }
  else
    {
      /*
      switch (sess->protocol_state)
        {
        case session::working_state:
          P(F("peer %s read failed in working state (error)")
            % sess->peer_id);
          break;

        case session::shutdown_state:
          P(F("peer %s read failed in shutdown state "
              "(possibly client misreported error)")
            % sess->peer_id);
          break;

        case session::confirmed_state:
          P(F("peer %s read failed in confirmed state (success)")
            % sess->peer_id);
          break;
        }
      */
      P(F("peer %s read failed") % sess->peer_id);
      drop_session_associated_with_fd(sessions, fd);
      live_p = false;
    }
}


static void
handle_write_available(Netxx::socket_type fd,
                       shared_ptr<session> sess,
                       map<Netxx::socket_type, shared_ptr<session> > & sessions,
                       bool & live_p)
{
  if (!sess->write_some())
    {
      /*
      switch (sess->protocol_state)
        {
        case session::working_state:
          P(F("peer %s write failed in working state (error)")
            % sess->peer_id);
          break;

        case session::shutdown_state:
          P(F("peer %s write failed in shutdown state "
              "(possibly client misreported error)")
            % sess->peer_id);
          break;

        case session::confirmed_state:
          P(F("peer %s write failed in confirmed state (success)")
            % sess->peer_id);
          break;
        }
      */
      P(F("peer %s write failed") % sess->peer_id);

      drop_session_associated_with_fd(sessions, fd);
      live_p = false;
    }
}

static void
process_armed_sessions(map<Netxx::socket_type, shared_ptr<session> > & sessions,
                       set<Netxx::socket_type> & armed_sessions,
                       transaction_guard & guard)
{
  for (set<Netxx::socket_type>::const_iterator i = armed_sessions.begin();
       i != armed_sessions.end(); ++i)
    {
      map<Netxx::socket_type, shared_ptr<session> >::iterator j;
      j = sessions.find(*i);
      if (j == sessions.end())
        continue;
      else
        {
          shared_ptr<session> sess = j->second;
          if (!sess->process(guard))
            {
              P(F("peer %s processing finished, disconnecting")
                % sess->peer_id);
              drop_session_associated_with_fd(sessions, *i);
            }
        }
    }
}

static void
reap_dead_sessions(map<Netxx::socket_type, shared_ptr<session> > & sessions,
                   time_t timeout_seconds)
{
  // Kill any clients which haven't done any i/o inside the timeout period
  // or who have exchanged all items and flushed their output buffers.
  set<Netxx::socket_type> dead_clients;
  time_t now = ::time(NULL);
  for (map<Netxx::socket_type, shared_ptr<session> >::const_iterator
         i = sessions.begin(); i != sessions.end(); ++i)
    {
      if (i->second->last_io_time + timeout_seconds < now)
        {
          P(F("fd %d (peer %s) has been idle too long, disconnecting")
            % i->first % i->second->peer_id);
          dead_clients.insert(i->first);
        }
    }
  for (set<Netxx::socket_type>::const_iterator i = dead_clients.begin();
       i != dead_clients.end(); ++i)
    {
      drop_session_associated_with_fd(sessions, *i);
    }
}





state
run_network_loop(bool client,
                 shared_ptr<Netxx::StreamServer> server,
                 map<Netxx::socket_type, shared_ptr<session> > & sessions,
                 app_state & app)
{
  I(!client || sessions.size() == 1);

  unsigned long timeout_seconds
    = static_cast<unsigned long>(constants::netsync_timeout_seconds);

  Netxx::PipeCompatibleProbe probe;

  Netxx::Timeout
    forever,
    timeout(static_cast<long>(timeout_seconds)),
    instant(0,1);

  unsigned long session_limit =
    static_cast<unsigned long>(constants::netsync_connection_limit);

  shared_ptr<transaction_guard> guard;

  set<Netxx::socket_type> armed_sessions;
  while(true)
    {
      probe.clear();
      armed_sessions.clear();

      if (server)
        {
          if (sessions.size() >= session_limit)
            W(F("session limit %d reached, some connections "
                "will be refused") % session_limit);
          else
            probe.add(*server);
        }

      arm_sessions_and_calculate_probe(probe, sessions, armed_sessions);

      L(FL("i/o probe with %d armed") % armed_sessions.size());
      Netxx::socket_type fd;
      Netxx::Timeout how_long;
      if (sessions.empty())
        how_long = forever;
      else if (armed_sessions.empty())
        how_long = timeout;
      else
        how_long = instant;

      do
        {
          Netxx::Probe::result_type res = probe.ready(how_long);
          how_long = instant;
          Netxx::Probe::ready_type event = res.second;
          fd = res.first;

          if (!guard)
            guard = shared_ptr<transaction_guard>(new transaction_guard(app.db));

          I(guard);

          if (fd == -1)
            {
              if (armed_sessions.empty())
                L(FL("timed out waiting for I/O"));
            }
          // we either got a new connection
          else if (server && fd == *server)
            {
              handle_new_connection(*server, timeout,
                                    sessions, app);
            }
          // or an existing session woke up
          else
            {
              map<Netxx::socket_type, shared_ptr<session> >::iterator i;
              i = sessions.find(fd);
              if (i == sessions.end())
                {
                  L(FL("got woken up for action on unknown fd %d") % fd);
                }
              else
                {
                  probe.remove(*(i->second->str));
                  shared_ptr<session> sess = i->second;
                  bool live_p = true;

                  try
                    {
                      if (event & Netxx::Probe::ready_read)
                        handle_read_available(fd, sess, sessions,
                                              armed_sessions, live_p);

                      if (live_p && (event & Netxx::Probe::ready_write))
                        handle_write_available(fd, sess, sessions, live_p);
                    }
                  catch (Netxx::Exception &)
                    {
                      P(F("Network error on peer %s, disconnecting")
                        % sess->peer_id);
                      drop_session_associated_with_fd(sessions, fd);
                    }
                  if (live_p && (event & Netxx::Probe::ready_oobd))
                    {
                      P(F("got OOB from peer %s, disconnecting")
                        % sess->peer_id);
                      drop_session_associated_with_fd(sessions, fd);
                    }
                }
            }
        }
      while(fd != -1);

      process_armed_sessions(sessions, armed_sessions, *guard);
      reap_dead_sessions(sessions, timeout_seconds);

      if (sessions.empty())
        {
          // Let the guard die completely if everything's gone quiet.
          guard->commit();
          guard.reset();
        }

      if (client)
        {
          if (sessions.empty())
            break;
          I(sessions.size() == 1);
          map<Netxx::socket_type, shared_ptr<session> >::iterator i = sessions.begin();
          if (!i->second->srv)
            break;
        }
    }
  return state::NONE;
}

shared_ptr<Netxx::StreamServer>
make_server(bool use_ipv6, app_state & app,
            std::list<utf8> const & addresses)
{
  Netxx::port_type default_port
    = static_cast<Netxx::port_type>(constants::netsync_default_port);

  Netxx::Timeout timeout(static_cast<long>(constants::netsync_timeout_seconds));

  bool retry = true;
  while(retry)
    {
      retry = false;
      try
        {
          Netxx::Address addr(use_ipv6);

          if (addresses.empty())
            addr.add_all_addresses(default_port);
          else
            {
              for (std::list<utf8>::const_iterator it = addresses.begin();
                   it != addresses.end(); ++it)
                {
                  string const & address = (*it)();
                  if (!address.empty())
                    {
                      size_t l_colon = address.find(':');
                      size_t r_colon = address.rfind(':');

                      if (l_colon == r_colon && l_colon == 0)
                        addr.add_all_addresses(lexical_cast<int>(address.substr(1)));
                      else
                        addr.add_address(address.c_str(), default_port);
                    }
                }
            }

          shared_ptr<Netxx::StreamServer> srv
            (new Netxx::StreamServer(addr, timeout));

          P(F("beginning service on %s") % addr.get_name());

          return srv;
        }
      catch(Netxx::NetworkException &)
        {
          if (use_ipv6)
            {
              use_ipv6 = false;
              retry = true;
            }
          else
            throw;
        }
      catch(Netxx::Exception &)
        {
          if (use_ipv6)
            {
              use_ipv6 = false;
              retry = true;
            }
          else
            throw;
        }
    }

  I(false);
  return shared_ptr<Netxx::StreamServer>();
}

void
serve_connections_forever(std::list<utf8> const & addrs,
                          app_state & app)
{
#ifdef USE_IPV6
  bool use_ipv6 = true;
#else
  bool use_ipv6 = false;
#endif
  ignore_sigpipe();
  shared_ptr<Netxx::StreamServer> srv = make_server(use_ipv6, app, addrs);
  map<Netxx::socket_type, shared_ptr<session> > sessions;
  run_network_loop(false, srv, sessions, app);
}

void
serve_single_on_stdio(app_state & app)
{
  ignore_sigpipe();
  shared_ptr<Netxx::StreamBase> str(new Netxx::PipeStream(0,1));
  shared_ptr<session> sess(new session(server_voice, str, utf8("stdio"), app));

  map<Netxx::socket_type, shared_ptr<session> > sessions;

  if (sess->str->get_socketfd() == -1)
    {
      // Unix pipes are non-duplex, have two filedescriptors
      shared_ptr<Netxx::PipeStream> pipe =
        boost::dynamic_pointer_cast<Netxx::PipeStream, Netxx::StreamBase>(sess->str);
      I(pipe);
      sessions[pipe->get_writefd()]=sess;
      sessions[pipe->get_readfd()]=sess;
    }
  else
    sessions[sess->str->get_socketfd()]=sess;

  run_network_loop(false, shared_ptr<Netxx::StreamServer>(), sessions, app);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
