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
#include "work.hh"
#include "vocab_cast.hh"

using std::make_pair;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;

CMD_FWD_DECL(automate);

bool automate_session::skip_ws(size_t & pos, size_t len)
{
  static string whitespace(" \r\n\t");
  while (pos < len && whitespace.find(inbuf[pos]) != string::npos)
    {
      ++pos;
    }
  if (pos == len)
    return false;
  return true;
}

bool automate_session::read_str(size_t & pos, size_t len, string & out)
{
  if (pos >= len)
    return false;
  if (!skip_ws(pos, len))
    return false;
  size_t size = 0;
  char c = inbuf[pos++];
  while (pos < len && c <= '9' && c >= '0')
    {
      size = (size * 10) + (c - '0');
      c = inbuf[pos++];
    }
  if (pos == len && c <= '9' && c >= '0')
    return false;

  if (c != ':')
    throw bad_decode(F("bad automate stdio input; cannot read string"));

  if (pos + size > len)
    return false;

  out = inbuf.substr(pos, size);
  pos += size;
  return true;
}

bool automate_session::read_cmd(Command & cmd)
{
  cmd.opts.clear();
  cmd.args.clear();

  size_t len = inbuf.size();
  if (len < 2)
    return false;
  size_t pos = 0;
  if (!skip_ws(pos, len))
    return false;
  if (inbuf[pos] == 'o')
    {
      ++pos;
      while (inbuf[pos] != 'e')
        {
          string opt, val;
          if (!read_str(pos, len, opt))
            return false;
          if (!read_str(pos, len, val))
            return false;
          cmd.opts.push_back(make_pair(opt, val));
          if (!skip_ws(pos, len))
            return false;
          if (pos == len)
            return false;
        };
      ++pos;
    }
  if (inbuf[pos] == 'l')
    {
      ++pos;
      while (inbuf[pos] != 'e')
        {
          string arg;
          if (!read_str(pos, len, arg))
            return false;
          cmd.args.push_back(arg);
          if (!skip_ws(pos, len))
            return false;
          if (pos == len)
            return false;
        }
      ++pos;
    }
  else
    throw bad_decode(F("bad automate stdio input; cannot find command"));

  if (cmd.args.empty())
    throw bad_decode(F("bad automate stdio input: empty command"));
  inbuf.pop_front(pos);
  return true;
}

void automate_session::note_bytes_in(int count)
{
  protocol_state = working_state;
}

void automate_session::note_bytes_out(int count)
{
  size_t len = inbuf.size();
  size_t pos = 0;
  if (output_empty() && !skip_ws(pos, len))
    {
      protocol_state = confirmed_state;
    }
}

automate_session::automate_session(app_state & app,
                                   string const & peer_id,
                                   shared_ptr<Netxx::StreamBase> str) :
  session_base(peer_id, str),
  app(app), armed(false),
  os(oss, app.opts.automate_stdio_size)
{ }

bool automate_session::arm()
{
  if (!armed)
    {
      if (output_overfull())
        {
          return false;
        }
      armed = read_cmd(cmd);
    }
  return armed;
}

bool automate_session::do_work(transaction_guard & guard)
{
  try
    {
      if (!arm())
        return true;
    }
  catch (bad_decode & bd)
    {
      W(F("stdio protocol error processing %s : '%s'")
        % peer_id % bd.what);
      return false;
    }
  armed = false;

  args_vector args;
  for (vector<string>::iterator i = cmd.args.begin();
       i != cmd.args.end(); ++i)
    {
      args.push_back(arg_type(*i, origin::user));
    }

  oss.str(string());

  try
    {
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
          E(false, origin::user,
            F("no completions for this command"));
        }
      else if (matches.size() > 1)
        {
          E(false, origin::user,
            F("multiple completions possible for this command"));
        }

      id = *matches.begin();

      I(args.size() >= id.size());
      for (command_id::size_type i = 0; i < id.size(); i++)
        args.erase(args.begin());

      command const * cmd = CMD_REF(automate)->find_command(id);
      I(cmd != NULL);
      automate const * acmd = reinterpret_cast< automate const * >(cmd);

      opts = options::opts::globals() | acmd->opts();

      if (cmd->use_workspace_options())
        {
          // Re-read the ws options file, rather than just copying
          // the options from the previous apts.opts object, because
          // the file may have changed due to user activity.
          workspace::check_format();
          workspace::get_options(app.opts);
        }

      opts.instantiate(&app.opts).from_key_value_pairs(this->cmd.opts);
      acmd->exec_from_automate(app, id, args, os);
    }
  catch (recoverable_failure & f)
    {
      os.set_err(2);
      os << f.what();
    }
  os.end_cmd();
  queue_output(oss.str());
  return true;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
