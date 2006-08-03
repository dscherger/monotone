/*************************************************
* SHA-160 Header File                            *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_SHA_160_H__
#define BOTAN_SHA_160_H__

#include <botan/mdx_hash.h>
#if WITH_CRYPTO
#include <openssl/sha.h>
#endif

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

#if WITH_CRYPTO
      virtual void add_data(const byte[], u32bit);
      virtual void final_result(byte[]);
      virtual void hash(const byte[]);
      virtual void copy_out(byte[]);
      virtual void write_count(byte[]);

      SHA_CTX ctx;
#else
      virtual void hash(const byte[]);
      virtual void copy_out(byte[]);
      SecureBuffer<u32bit, 5> digest;
      SecureBuffer<u32bit, 80> W;
#endif
   };

}

#endif
