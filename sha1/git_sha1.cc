// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This provides glue from the openssl-style SHA1 api (as used by git SHA1
// routines, for instance), to Botan.

#include "sha1_engine.hh"
#include "botan/base.h"

// If we ever have more than one openssl-style SHA-1 interfaces on a single
// architecture, there might be symbol conflicts here.  Deal with that if/when
// it happens.
extern "C" {
#include GIT_SHA1_HEADER
};

using Botan::HashFunction;
using Botan::byte;
using Botan::u32bit;

namespace
{
  class Git_SHA_160 : public HashFunction
  {
  private:
    SHA_CTX ctx;
  public:
    std::string name() const { return "SHA-160"; }
    HashFunction * clone() const { return new Git_SHA_160; }
    Git_SHA_160() : HashFunction(20, 64)
    {
      clear();
    }

    void clear() throw()
    {
      SHA1_Init(&ctx);
    }
    void add_data(const byte data[], u32bit size)
    {
      SHA1_Update(&ctx, data, size);
    }
    void final_result(byte output[])
    {
      SHA1_Final(output, &ctx);
      clear();
    }
  };

  HashFunction * make_git_sha_160()
  {
    return new Git_SHA_160();
  }
  sha1_registerer register_git_sha_160(10, "git", make_git_sha_160);
}
