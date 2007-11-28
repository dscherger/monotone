#ifndef __NETWORK_HH__
#define __NETWORK_HH__

#include "app_state.hh"
#include "netcmd.hh"
#include "vocab.hh"

#include <boost/shared_ptr.hpp>
using boost::shared_ptr;

namespace service_numbers
{
  const int none = 0;
  const int netsync = 1;
}

class transaction_guard;

class session;

struct state
{
  enum _state {SERVICE_DONE, SESSION_DONE, ERROR, RUNNING, NONE};
  _state val;
  state(_state from) : val(from) {}
  operator _state() { return val; }
};

class service
{
protected:
  session * sess;
public:

  service(int num);
  virtual ~service();
  virtual service * copy(app_state & app) = 0;

  void attach(session & s);
  void detach(bool received_error);
private:
  virtual void detached(bool received_error); // default do nothing
public:

  // Because this is currently intertwined in the netsync protocol.
  // This really ought to go away sometime.
  void _set_session_key(netsync_session_key const & key);

  virtual void begin_service() = 0;   // called on the server
  virtual void request_service() = 0; // called on the client

  // do we have work to do, even without receiving anything?
  virtual bool can_process() = 0; // default false
  // do work
  virtual state process(transaction_guard & guard) = 0;

  // are we willing to accept input?
  virtual bool can_receive(); // default true

  virtual state received(netcmd const & cmd,
                         transaction_guard & guard) = 0;
protected:
  void send(netcmd const & cmd);
  bool can_send() const;
};


class client_session
{
  shared_ptr<session> impl;
  client_session(client_session const & other);//I(false)
  client_session const & operator=(client_session const & other);//I(false)
public:
  // client
  client_session(utf8 const & addr, app_state & app);

  // client
  bool authenticate_as(netsync_session_key const & key);
  state request_service(service * newsrv);
};

void serve_connections_forever(std::list<utf8> const & addrs, app_state & app);
void serve_single_on_stdio(app_state & app);



// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif

