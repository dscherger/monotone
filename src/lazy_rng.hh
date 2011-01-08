// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __LAZY_RNG_HH__
#define __LAZY_RNG_HH__

// This class instantiates a Botan::RandomNumberGenerator the first time
// its get() method is called.  Subsequent calls return the same object.
// It is expected that callers will not hang on to the reference.

#include <botan/version.h>

#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
#include <botan/rng.h>

class lazy_rng
{
  Botan::RandomNumberGenerator * rng;
  lazy_rng() { rng = Botan::RandomNumberGenerator::make_rng(); }
  ~lazy_rng() { delete rng; }

public:

  static Botan::RandomNumberGenerator & get()
  {
    static lazy_rng * instance = 0;
    if (!instance)
      instance = new lazy_rng();
    return *instance->rng;
  }
};

#endif /* botan >= 1.7.7 */

#endif /* lazy_rng.hh */

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
