// Copyright (C) 2005 Christof Petig <christof@petig-baender.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "unit_tests.hh"
#include "netxx_pipe.hh"

using std::string;
using std::vector;

#ifndef WIN32

UNIT_TEST(simple_pipe)
{ try
  {
  Netxx::PipeStream pipe("cat",vector<string>());

  string result;
  Netxx::PipeCompatibleProbe probe;
  Netxx::Timeout timeout(2L), short_time(0,1000);

  // time out because no data is available
  probe.clear();
  probe.add(pipe, Netxx::Probe::ready_read);
  Netxx::Probe::result_type res = probe.ready(short_time);
  I(res.second==Netxx::Probe::ready_none);

  // write should be possible
  probe.clear();
  probe.add(pipe, Netxx::Probe::ready_write);
  res = probe.ready(short_time);
  I(res.second & Netxx::Probe::ready_write);
#ifdef WIN32
  I(res.first==pipe.get_socketfd());
#else
  I(res.first==pipe.get_writefd());
#endif

  // try binary transparency
  for (int c = 0; c < 256; ++c)
    {
      char buf[1024];
      buf[0] = c;
      buf[1] = 255 - c;
      pipe.write(buf, 2);

      string result;
      while (result.size() < 2)
        { // wait for data to arrive
          probe.clear();
          probe.add(pipe, Netxx::Probe::ready_read);
          res = probe.ready(timeout);
          E(res.second & Netxx::Probe::ready_read, origin::system,
            F("timeout reading data %d") % c);
#ifdef WIN32
          I(res.first == pipe.get_socketfd());
#else
          I(res.first == pipe.get_readfd());
#endif
          int bytes = pipe.read(buf, sizeof(buf));
          result += string(buf, bytes);
        }
      I(result.size() == 2);
      I(static_cast<unsigned char>(result[0]) == c);
      I(static_cast<unsigned char>(result[1]) == 255 - c);
    }

  pipe.close();

  }
catch (recoverable_failure &e)
  // for some reason boost does not provide
  // enough information
  {
    W(F("Failure %s") % e.what());
    throw;
  }
}
#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
