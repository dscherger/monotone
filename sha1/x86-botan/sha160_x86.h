/*************************************************
* SHA-160 Header File                            *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_SHA_160_X86_H__
#define BOTAN_SHA_160_X86_H__

#include <botan/mdx_hash.h>

namespace Botan {

/*************************************************
* SHA-160_x86                                        *
*************************************************/
class SHA_160_x86 : public MDx_HashFunction
   {
   public:
      void clear() throw();
      std::string name() const { return "SHA-160_x86"; }
      HashFunction* clone() const { return new SHA_160_x86; }
      SHA_160_x86() : MDx_HashFunction(20, 64, true, true) { clear(); }
   private:
      friend class Gamma;
      friend class FIPS_186_RNG;

      void hash(const byte[]);
      void copy_out(byte[]);

      SecureBuffer<u32bit, 5> digest;
      SecureBuffer<u32bit, 80> W;
   };

}

#endif
