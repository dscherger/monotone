/*************************************************
* SHA-160 Header File                            *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_SHA_160_H__
#define BOTAN_SHA_160_H__

#include <botan/mdx_hash.h>

namespace Botan {

/*************************************************
* SHA-160                                        *
*************************************************/
class SHA_160 : public MDx_HashFunction
   {
   public:
      void clear() throw();
      std::string name() const { return "SHA-160"; }
      HashFunction* clone() const { return new SHA_160; }
      SHA_160() : MDx_HashFunction(20, 64, true, true) { clear(); }
   private:
      friend class Gamma;
      friend class FIPS_186_RNG;

      void hash(const byte[]);
      void copy_out(byte[]);

      SecureBuffer<u32bit, 5> digest;
   };

}

#endif

