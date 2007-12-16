// Copyright (C) 2007 Stephen Leake <stephen_leake@stephe-leake.org>
//
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.
//
// Provide a simple main program for use in netxx_pipe.cc unit test
//
// It uses StdioStream, and echos stdin to stdout, with debug messages
// on stderr.

#include "base.hh"
#include "netxx_pipe.hh"
#include "platform.hh"

#include <stdio.h>

#ifdef WIN32
#include <io.h>
#else
#include <fcntl.h>
#endif

struct tester_sanity : public sanity
{
  void inform_log(std::string const &msg)
  {fprintf(stdout, "%s", msg.c_str());}
  void inform_message(std::string const &msg)
  {fprintf(stdout, "%s", msg.c_str());};
  void inform_warning(std::string const &msg)
  {fprintf(stderr, "warning: %s", msg.c_str());};
  void inform_error(std::string const &msg)
  {fprintf(stderr, "error: %s", msg.c_str());};
};
tester_sanity real_sanity;
sanity & global_sanity = real_sanity;

int main (int argc, char *argv[])
{
  global_sanity.initialize(argc, argv, 0);

  {
    Netxx::StdioStream        stream;
    Netxx::StdioProbe         probe;
    Netxx::Probe::result_type probe_result;
    Netxx::Timeout            timeout(0, 1000);

    char                    buffer[256];
    Netxx::signed_size_type bytes_read;
    int                     i;
    int                     quit = 0;

    probe.add (stream, Netxx::Probe::ready_read);
    stream.set_timeout (timeout);

    // Exit when ready times out; socket has been closed
    for (;!quit;)
      try
      {
        probe_result = probe.ready(timeout, Netxx::Probe::ready_read);

        if (-1 == probe_result.first)
          {
            // timeout; assume we're running the probe:spawn_stdio unit test, and it's done (the socket closed)
            quit = 1;
            continue;
          }
        else if (stream.get_socketfd() != probe_result.first)
          {
            fprintf (stderr, "ready returned other socket\n");
            quit = 1;
            continue;
          }

        switch (probe_result.second)
          {
          case Netxx::Probe::ready_none:
            quit = 1;
            break;

          case Netxx::Probe::ready_read:
            bytes_read = stream.read (buffer, sizeof (buffer));
            if (-1 == bytes_read)
              {
                fprintf (stderr, "read timed out\n");
                quit = 1;
              }
            else if (0 == bytes_read)
              {
                fprintf (stderr, "socket closed\n");
                quit = 1;
              }
            else
              {
                stream.write (buffer, bytes_read);
              }
            break;

          case Netxx::Probe::ready_write:
            fprintf (stderr, "ready write\n");
            quit = 1;
            break;

          case Netxx::Probe::ready_oobd:
            fprintf (stderr, "ready oobd\n");
            quit = 1;
            break;
          }
      }
    catch (std::runtime_error &e)
      {
        fprintf (stderr, "exception: %s\n", e.what());
        break;
      }

    stream.close();

  }
  return 1;
} // end main
// end of file
