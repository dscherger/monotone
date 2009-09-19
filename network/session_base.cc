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
#include "network/session_base.hh"

#include "constants.hh"

using std::min;
using std::string;
using std::vector;

using boost::shared_ptr;

session_base::session_base(string const & peer_id,
                           shared_ptr<Netxx::StreamBase> str) :
    outbuf_bytes(0),
    peer_id(peer_id), str(str),
    last_io_time(::time(NULL)),
    protocol_state(working_state),
    encountered_error(false)
{ }

session_base::~session_base()
{ }

void
session_base::mark_recent_io()
{
  last_io_time = ::time(NULL);
}

bool
session_base::timed_out(time_t now)
{
  return static_cast<unsigned long>(last_io_time + constants::netsync_timeout_seconds)
    < static_cast<unsigned long>(now);
}

bool
session_base::is_pipe_pair()
{
  return str->get_socketfd() == -1;
}

vector<Netxx::socket_type>
session_base::get_sockets()
{
  vector<Netxx::socket_type> out;
  Netxx::socket_type fd = str->get_socketfd();
  if (fd == -1)
    {
      shared_ptr<Netxx::PipeStream> pipe =
        boost::dynamic_pointer_cast<Netxx::PipeStream, Netxx::StreamBase>(str);
      I(pipe);
      out.push_back(pipe->get_readfd());
      out.push_back(pipe->get_writefd());
    }
  else
    out.push_back(fd);
  return out;
}

void
session_base::add_to_probe(Netxx::PipeCompatibleProbe & probe)
{
  probe.add(*str, which_events());
}

void
session_base::remove_from_probe(Netxx::PipeCompatibleProbe & probe)
{
  I(!is_pipe_pair());
  probe.remove(*str);
}

void
session_base::queue_output(string const & s)
{
  outbuf.push_back(make_pair(s, 0));
  outbuf_bytes += s.size();
}

bool
session_base::output_overfull() const
{
  return outbuf_bytes > constants::bufsz * 10;
}

bool
session_base::output_empty() const
{
  return outbuf.empty();
}

Netxx::Probe::ready_type
session_base::which_events()
{
  Netxx::Probe::ready_type ret = Netxx::Probe::ready_oobd;
  if (!outbuf.empty())
    {
      L(FL("probing write on %s") % peer_id);
      ret = ret | Netxx::Probe::ready_write;
    }
  // Only ask to read if we're not armed, don't go storing
  // 128 MB at a time unless we think we need to.
  if (inbuf.size() < constants::netcmd_maxsz && !arm())
    {
      L(FL("probing read on %s") % peer_id);
      ret = ret | Netxx::Probe::ready_read;
    }
  return ret;
}

void
session_base::read_some(bool & failed, bool & eof)
{
  I(inbuf.size() < constants::netcmd_maxsz);
  eof = false;
  failed = false;
  char tmp[constants::bufsz];
  Netxx::signed_size_type count = str->read(tmp, sizeof(tmp));
  if (count > 0)
    {
      L(FL("read %d bytes from fd %d (peer %s)")
        % count % str->get_socketfd() % peer_id);
      if (encountered_error)
        L(FL("in error unwind mode, so throwing them into the bit bucket"));

      inbuf.append(tmp,count);
      mark_recent_io();
      note_bytes_in(count);
    }
  else if (count == 0)
    {
      // Returning 0 bytes after select() marks the file descriptor as
      // ready for reading signifies EOF.

      switch (protocol_state)
        {
        case working_state:
          P(F("peer %s IO terminated connection in working state (error)")
            % peer_id);
          break;

        case shutdown_state:
          P(F("peer %s IO terminated connection in shutdown state "
              "(possibly client misreported error)")
            % peer_id);
          break;

        case confirmed_state:
          break;
        }

      eof = true;
    }
  else
    failed = true;
}

bool
session_base::write_some()
{
  I(!outbuf.empty());
  size_t writelen = outbuf.front().first.size() - outbuf.front().second;
  Netxx::signed_size_type count = str->write(outbuf.front().first.data() + outbuf.front().second,
                                            min(writelen,
                                            constants::bufsz));
  if (count > 0)
    {
      if ((size_t)count == writelen)
        {
          outbuf_bytes -= outbuf.front().first.size();
          outbuf.pop_front();
        }
      else
        {
          outbuf.front().second += count;
        }
      L(FL("wrote %d bytes to fd %d (peer %s)")
        % count % str->get_socketfd() % peer_id);
      mark_recent_io();
      note_bytes_out(count);
      if (encountered_error && outbuf.empty())
        {
          // we've flushed our error message, so it's time to get out.
          L(FL("finished flushing output queue in error unwind mode, disconnecting"));
          return false;
        }
      return true;
    }
  else
    return false;
}

bool
session_base::do_io(Netxx::Probe::ready_type what)
{
  bool ok = true;
  bool eof = false;
  try
    {
      if (what & Netxx::Probe::ready_read)
        {
          bool failed;
          read_some(failed, eof);
          if (failed)
            ok = false;
        }
      if (what & Netxx::Probe::ready_write)
        {
          if (!write_some())
            ok = false;
        }

      if (what & Netxx::Probe::ready_oobd)
        {
          P(F("got OOB from peer %s, disconnecting")
            % peer_id);
          ok = false;
        }
      else if (!ok)
        {
          switch (protocol_state)
            {
            case working_state:
              P(F("peer %s IO failed in working state (error)")
                % peer_id);
              break;

            case shutdown_state:
              P(F("peer %s IO failed in shutdown state "
                  "(possibly client misreported error)")
                % peer_id);
              break;

            case confirmed_state:
              P(F("peer %s IO failed in confirmed state (success)")
                % peer_id);
              break;
            }
        }
    }
  catch (Netxx::Exception & e)
    {
      P(F("Network error on peer %s, disconnecting")
        % peer_id);
      ok = false;
    }

  // Return false in case we reached EOF, so as to prevent further calls
  // to select()s on this stream, as recommended by the select_tut man
  // page.
  return ok && !eof;
}



// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
