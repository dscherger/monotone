// Copyright (C) 2009 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __WRAPPED_SESSION_HH__
#define __WRAPPED_SESSION_HH__

#include "netcmd.hh" // for protocol_voice
#include "numeric_vocab.hh"

struct key_identity_info;
class netcmd;
class session;
class transaction_guard;

struct netsync_error
{
  std::string msg;
  netsync_error(std::string const & s): msg(s) {}
};


class wrapped_session
{
  session * owner;
protected:
  void write_netcmd(netcmd const & cmd) const;
  u8 get_version() const;
  void error(int errcode, std::string const & message);
  protocol_voice get_voice() const;
  std::string get_peer() const;
  bool output_overfull() const;
  bool encountered_error() const;
  bool shutdown_confirmed() const;

  void request_netsync(protocol_role role,
                       globish const & include,
                       globish const & exclude);
  void request_automate();
public:
  wrapped_session();
  explicit wrapped_session(session * owner);
  void set_owner(session * owner);

  virtual bool do_work(transaction_guard & guard,
                       netcmd const * const in_cmd) = 0;
  // Can I do anything without waiting for more input?
  virtual bool have_work() const = 0;

  virtual void request_service() = 0;
  virtual void accept_service() = 0;
  virtual std::string usher_reply_data() const = 0;
  virtual bool finished_working() const = 0;

  virtual void on_begin(size_t ident, key_identity_info const & remote_key);
  virtual void on_end(size_t ident);
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
