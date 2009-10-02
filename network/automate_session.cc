// Copyright (C) 2008 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "network/automate_session.hh"

#include "app_state.hh"
#include "automate_reader.hh"
#include "work.hh"
#include "vocab_cast.hh"

using std::make_pair;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;

CMD_FWD_DECL(automate);

automate_session::automate_session(app_state & app,
                                   session * owner) :
  wrapped_session(owner),
  app(app),
  command_number(-1),
  is_done(false)
{ }

void automate_session::send_command()
{
  // read an automate command on stdin, then package it up and send it
  automate_reader ar(std::cin);
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
  send_command();
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

bool automate_session::do_work(transaction_guard & guard,
                               netcmd const * const cmd_in)
{
  if (!cmd_in)
    return true;

  switch(cmd_in->get_cmd_code())
    {
    case automate_command_cmd:
      {
        vector<string> in_args;
        vector<pair<string, string> > in_opts;
        cmd_in->read_automate_command_cmd(in_args, in_opts);
        ++command_number;

        ostringstream oss;
        bool have_err = false;
        string err;

        try
          {
            E(app.lua.hook_get_remote_automate_permitted(remote_identity,
                                                         in_args,
                                                         in_opts),
              origin::user,
              F("Sorry, you aren't allowed to do that."));

            args_vector args;
            for (vector<string>::iterator i = in_args.begin();
                 i != in_args.end(); ++i)
              {
                args.push_back(arg_type(*i, origin::user));
              }

            options::options_type opts;
            opts = options::opts::all_options() - options::opts::globals();
            opts.instantiate(&app.opts).reset();

            command_id id;
            for (args_vector::const_iterator iter = args.begin();
                 iter != args.end(); iter++)
              id.push_back(typecast_vocab<utf8>(*iter));

            set< command_id > matches =
              CMD_REF(automate)->complete_command(id);

            if (matches.empty())
              {
                E(false, origin::network,
                  F("no completions for this command"));
              }
            else if (matches.size() > 1)
              {
                E(false, origin::network,
                  F("multiple completions possible for this command"));
              }

            id = *matches.begin();

            I(args.size() >= id.size());
            string cmd_printable;
            for (command_id::size_type i = 0; i < id.size(); i++)
              {
                if (!cmd_printable.empty())
                  cmd_printable += " ";
                cmd_printable += (*args.begin())();
                args.erase(args.begin());
              }

            L(FL("Executing %s for remote peer %s")
              % cmd_printable % get_peer());

            command const * cmd = CMD_REF(automate)->find_command(id);
            I(cmd != NULL);
            automate const * acmd = dynamic_cast< automate const * >(cmd);
            I(acmd);

            E(acmd->can_run_from_stdio(), origin::network,
              F("sorry, that can't be run remotely or over stdio"));

            opts = options::opts::globals() | acmd->opts();

            if (cmd->use_workspace_options())
              {
                // Re-read the ws options file, rather than just copying
                // the options from the previous apts.opts object, because
                // the file may have changed due to user activity.
                workspace::check_format();
                workspace::get_options(app.opts);
              }

            opts.instantiate(&app.opts).from_key_value_pairs(in_opts);
            acmd->exec_from_automate(app, id, args, oss);
          }
        catch (recoverable_failure & f)
          {
            have_err = true;
            err = f.what();
            err += "\n";
          }

        if (!oss.str().empty() || !have_err)
          {
            netcmd out_cmd(get_version());
            out_cmd.write_automate_packet_cmd(command_number, 0,
                                              !have_err, oss.str());
            write_netcmd(out_cmd);
          }
        if (have_err)
          {
            netcmd err_cmd(get_version());
            err_cmd.write_automate_packet_cmd(command_number, 2,
                                              true, err);
            write_netcmd(err_cmd);
          }

        return true;

      }
    case automate_packet_cmd:
      {
        int command_num;
        int err_code;
        bool last;
        string packet_data;
        cmd_in->read_automate_packet_cmd(command_num, err_code,
                                         last, packet_data);

        std::cout<<command_num<<":"
                 <<err_code<<":"
                 <<(last?'l':'m')<<":"
                 <<packet_data.size()<<":"
                 <<packet_data;
        std::cout.flush();

        if (last)
          send_command();

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
