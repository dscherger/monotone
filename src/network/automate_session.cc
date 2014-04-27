// Copyright (C) 2008 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../base.hh"
#include "../app_state.hh"
#include "../automate_reader.hh"
#include "../automate_stdio_helpers.hh"
#include "../simplestring_xform.hh"
#include "../ui.hh"
#include "../vocab_cast.hh"
#include "../work.hh"
#include "automate_session.hh"

using std::make_pair;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;
using boost::lexical_cast;


automate_session::automate_session(app_state & app,
                                   session * owner,
                                   std::istream * const is,
                                   automate_ostream * const os) :
  wrapped_session(owner),
  app(app),
  input_stream(is),
  output_stream(os),
  command_number(-1),
  is_done(false)
{ }

void automate_session::send_command()
{
  // read an automate command from the stream, then package it up and send it
  I(input_stream);
  automate_reader ar(*input_stream);
  vector<pair<string, string> > read_opts;
  vector<string> read_args;

  if (ar.get_command(read_opts, read_args))
    {
      netcmd cmd_out(get_version());
      cmd_out.write_automate_command_cmd(read_args, read_opts);
      write_netcmd(cmd_out);
    }
  else
    {
      is_done = true;
    }
}

bool automate_session::have_work() const
{
  return false;
}

void automate_session::request_service()
{
  if (get_version() < 8)
    throw bad_decode(F("server is too old for remote automate connections"));
  request_automate();
}

void automate_session::accept_service()
{
  netcmd cmd_out(get_version());
  cmd_out.write_automate_headers_request_cmd();
  write_netcmd(cmd_out);
}

string automate_session::usher_reply_data() const
{
  return string();
}

bool automate_session::finished_working() const
{
  return is_done;
}

void automate_session::prepare_to_confirm(key_identity_info const & remote_key,
                                          bool use_transport_auth)
{
  remote_identity = remote_key;
}

static void out_of_band_to_netcmd(char stream, std::string const & text, void * opaque)
{
  automate_session * sess = reinterpret_cast<automate_session*>(opaque);
  sess->write_automate_packet_cmd(stream, text);
}

void automate_session::write_automate_packet_cmd(char stream,
                                                 std::string const & text)
{
  netcmd net_cmd(get_version());
  net_cmd.write_automate_packet_cmd(command_number, stream, text);
  write_netcmd(net_cmd);
}


// lambda expressions would be really nice right about now
// even the ability to use local classes as template arguments would help
class remote_stdio_pre_fn {
  app_state & app;
  key_identity_info const & remote_identity;
  vector<string> const & cmdline;
  vector<pair<string,string> > const & params;
public:
  remote_stdio_pre_fn(app_state & app, key_identity_info const & ri,
                      vector<string> const & c,
                      vector<pair<string,string> > const & p)
    : app(app), remote_identity(ri), cmdline(c), params(p)
  { }
  void operator()() {
    E(app.lua.hook_get_remote_automate_permitted(remote_identity,
                                                 cmdline, params),
      origin::user,
      F("sorry, you aren't allowed to do that."));
  }
};
class remote_stdio_log_fn {
  string peer;
public:
  remote_stdio_log_fn(string const & p) : peer(p) { }
  void operator()(commands::command_id const & id) {
    L(FL("Executing %s for remote peer %s")
      % join_words(id) % peer);
  }
};
bool automate_session::do_work(transaction_guard & guard,
                               netcmd const * const cmd_in)
{
  if (!cmd_in)
    return true;

  switch(cmd_in->get_cmd_code())
    {
    case automate_headers_request_cmd:
      {
        netcmd net_cmd(get_version());
        vector<pair<string, string> > headers;
        commands::get_stdio_headers(headers);
        net_cmd.write_automate_headers_reply_cmd(headers);
        write_netcmd(net_cmd);
        return true;
      }
    case automate_headers_reply_cmd:
      {
        vector<pair<string, string> > headers;
        cmd_in->read_automate_headers_reply_cmd(headers);

        I(output_stream);
        output_stream->write_headers(headers);

        send_command();
        return true;
      }
    case automate_command_cmd:
      {
        vector<pair<string, string> > params;
        vector<string> cmdline;
        cmd_in->read_automate_command_cmd(cmdline, params);
        ++command_number;

        global_sanity.set_out_of_band_handler(&out_of_band_to_netcmd, this);

        ostringstream oss;

        pair<int, string> err = automate_stdio_helpers::
          automate_stdio_shared_body(app, cmdline, params, oss,
                                     remote_stdio_pre_fn(app, remote_identity, cmdline, params),
                                     remote_stdio_log_fn(get_peer()));
        if (err.first != 0)
          write_automate_packet_cmd('e', err.second);
        if (!oss.str().empty())
          write_automate_packet_cmd('m', oss.str());
        write_automate_packet_cmd('l', lexical_cast<string>(err.first));

        global_sanity.set_out_of_band_handler();

        return true;
      }
    case automate_packet_cmd:
      {
        int command_num;
        char stream;
        string packet_data;
        cmd_in->read_automate_packet_cmd(command_num, stream, packet_data);

        I(output_stream);

        if (stream == 'm')
            (*output_stream) << packet_data;
        else if (stream != 'l')
            output_stream->write_out_of_band(stream, packet_data);

        if (stream == 'l')
          {
            output_stream->end_cmd(lexical_cast<int>(packet_data));
            send_command();
          }

        return true;
      }
    default:
      E(false, origin::network,
        F("unexpected netcmd '%d' received on automate connection")
        % cmd_in->get_cmd_code());
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
