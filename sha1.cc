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
  int mebibytes = 100;
  string test_str(mebibytes << 20, 'a');
  data test_data(test_str);
  id foo;
  double start = cpu_now();
  calculate_ident(test_data, foo);
  double end = cpu_now();
  double mebibytes_per_sec = mebibytes / (end - start);
  P(F("%s MiB/s") % mebibytes_per_sec);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

