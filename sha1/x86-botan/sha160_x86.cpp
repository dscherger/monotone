/*************************************************
* SHA-160 Source File                            *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include "sha160_x86.h"
#include <botan/bit_ops.h>
#include "sha1_engine.hh"

namespace Botan {

extern "C" void sha160_core(u32bit[5], const byte[64], u32bit[80]);

Botan::HashFunction * make_sha_160_x86()
{
 return new SHA_160_x86();
}
sha1_registerer register_sha_160_x86(10, "Botan x86", make_sha_160_x86);

/*************************************************
* SHA-160 Compression Function                   *
*************************************************/
void SHA_160_x86::hash(const byte input[])
   {
   sha160_core(digest, input, W);
   }

/*************************************************
* Copy out the digest                            *
*************************************************/
void SHA_160_x86::copy_out(byte output[])
   {
   for(u32bit j = 0; j != OUTPUT_LENGTH; ++j)
      output[j] = get_byte(j % 4, digest[j/4]);
   }

/*************************************************
* Clear memory of sensitive data                 *
*************************************************/
void SHA_160_x86::clear() throw()
   {
   MDx_HashFunction::clear();
   W.clear();
   digest[0] = 0x67452301;
   digest[1] = 0xEFCDAB89;
   digest[2] = 0x98BADCFE;
   digest[3] = 0x10325476;
   digest[4] = 0xC3D2E1F0;
   }

}
