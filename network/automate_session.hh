// Copyright (C) 2008 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __AUTOMATE_SESSION_HH__
#define __AUTOMATE_SESSION_HH__

#include "automate_ostream.hh"
#include "cmd.hh"
#include "network/wrapped_session.hh"
#include "project.hh" // key_identity_info

// A set of session innards that knows how to forward 'automate stdio'
// over the network.
class automate_session : public wrapped_session
{
  app_state & app;
  std::istream * const input_stream;
  automate_ostream * const output_stream;
  typedef commands::command_id command_id;
  typedef commands::command command;
  typedef commands::automate automate;
  size_t command_number;

  bool is_done;

  key_identity_info remote_identity;

  void send_command();
public:
  automate_session(app_state & app,
                   session * owner,
                   std::istream * const is,
                   automate_ostream * const os);

  void write_automate_packet_cmd(char stream,
                                 std::string const & text);
  bool do_work(transaction_guard & guard,
               netcmd const * const in_cmd);
  bool have_work() const;
  void request_service();
  void accept_service();
  std::string usher_reply_data() const;
  bool finished_working() const;
  void prepare_to_confirm(key_identity_info const & remote_key,
                          bool use_transport_auth);
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
