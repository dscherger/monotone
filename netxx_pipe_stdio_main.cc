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
  int fid = STDIN_FILENO;

  global_sanity.initialize(argc, argv, 0);

  // If an argument is given, it is a file to read instead of stdin, for debugging
  if (argc == 2)
    {
      fprintf (stderr, "opening %s\n", argv[1]);
      fid = open (argv[1], 0);
    }

  {
    Netxx::StdioStream        stream (fid, STDOUT_FILENO);
    Netxx::StdioProbe         probe;
    Netxx::Probe::result_type probe_result;
    Netxx::Timeout            short_time(0,1000);

    char                    buffer[256];
    Netxx::signed_size_type bytes_read;
    int                     i;
    int                     quit = 0;

    probe.add (stream, Netxx::Probe::ready_read);

    // Exit when ready returns none
    // But that never happens when reading a file, so exit on a count in that case
    for (i = 0; (!quit) && ((argc == 1) || (i < 100)); i++)
      try
      {
        probe_result = probe.ready(short_time);

        switch (probe_result.second)
          {
          case Netxx::Probe::ready_none:
            quit = 1;
            break;

          case Netxx::Probe::ready_read:
            bytes_read = stream.read (buffer, sizeof (buffer));
            stream.write (buffer, bytes_read);
            break;

          case Netxx::Probe::ready_write:
            break;

          case Netxx::Probe::ready_oobd:
            break;
          }
      }
    catch (std::runtime_error &e)
      {
        fprintf (stderr, "exception: %s\n", e.what());
        break;
      }

    stream.close();

    if (fid != STDIN_FILENO)
      close (fid);

  }
  return 1;
} // end main
// end of file
