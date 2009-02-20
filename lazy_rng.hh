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

public:
  lazy_rng() : rng(0) {}
  ~lazy_rng() { delete rng; }

  Botan::RandomNumberGenerator & get() {
    if (!rng)
      rng = Botan::RandomNumberGenerator::make_rng();

    return *rng;
  }  
};

#endif /* botan >= 1.7.7 */

#endif /* lazy_rng.hh */
