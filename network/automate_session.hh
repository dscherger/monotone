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
#include "network/session_base.hh"

class automate_session : public session_base
{
  app_state & app;
  typedef commands::command_id command_id;
  typedef commands::command command;
  typedef commands::automate automate;
  struct Command
  {
    std::vector<std::pair<std::string, std::string> > opts;
    std::vector<std::string> args;
  };
  bool skip_ws(size_t & pos, size_t len);
  bool read_str(size_t & pos, size_t len, std::string & out);
  bool read_cmd(Command & cmd);
  bool armed;
  Command cmd;

  void note_bytes_in(int count);
  void note_bytes_out(int count);
  std::ostringstream oss;
  automate_ostream os;
public:
  automate_session(app_state & app,
                   protocol_voice voice,
                   std::string const & peer_id,
                   boost::shared_ptr<Netxx::StreamBase> str);
  bool arm();
  bool do_work(transaction_guard & guard);
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
