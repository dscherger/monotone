// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file holds a registry of different SHA-1 implementations, and lets us
// benchmark them.

#include "base.hh"
#include <botan/botan.h>
#include <botan/sha160.h>

// Botan 1.7.23+ and 1.8.x specific sha1 benchmarking code uses botan's
// own timer and measures botan's different SHA1 providers, instead of
// only measuring one.
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,23)
#include <botan/libstate.h>
#include <botan/benchmark.h>
#endif

#include "sanity.hh"
#include "ui.hh"
#include "platform.hh"
#include "cmd.hh"
#include "transforms.hh"

using std::string;

CMD_HIDDEN(benchmark_sha1, "benchmark_sha1", "", CMD_REF(debug), "",
           N_("Benchmarks botan's SHA-1 core"),
           "",
           options::opts::none)
{
  P(F("Benchmarking botan's SHA-1 core"));

#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,23)

  Botan::AutoSeeded_RNG rng;
  Botan::Algorithm_Factory & af =
    Botan::global_state().algorithm_factory();

  const int milliseconds = 5000;

  // timer argument was removed in 1.9.4
#if BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,9,4)
  Botan::Default_Benchmark_Timer timer;
  std::map<std::string, double> results =
    Botan::algorithm_benchmark("SHA-1",  milliseconds, timer, rng, af);
#else
  std::map<std::string, double> results =
    Botan::algorithm_benchmark("SHA-1",  milliseconds, rng, af);
#endif

  for(std::map<std::string, double>::const_iterator i = results.begin();
      i != results.end(); ++i)
    {
      P(F("SHA-1 provider '%s': %s MiB/s") % i->first % i->second);
    }

#else
  int mebibytes = 100;
  string test_str(mebibytes << 20, 'a');
  data test_data(test_str, origin::internal);
  id foo;
  double start = cpu_now();
  calculate_ident(test_data, foo);
  double end = cpu_now();
  double mebibytes_per_sec = mebibytes / (end - start);
  P(F("%s MiB/s") % mebibytes_per_sec);
#endif
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
