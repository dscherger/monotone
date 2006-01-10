/*************************************************
* MD5 Header File                                *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_MD5_H__
#define BOTAN_MD5_H__

#include <botan/mdx_hash.h>

namespace Botan {

/*************************************************
* MD5                                            *
*************************************************/
class MD5 : public MDx_HashFunction
   {
   public:
      void clear() throw();
      std::string name() const { return "MD5"; }
      HashFunction* clone() const { return new MD5; }
      MD5() : MDx_HashFunction(16, 64, false, true) { clear(); }
   private:
      void hash(const byte[]);
      void copy_out(byte[]);

      static void FF(u32bit&, u32bit, u32bit, u32bit, u32bit, byte, u32bit);
      static void GG(u32bit&, u32bit, u32bit, u32bit, u32bit, byte, u32bit);
      static void HH(u32bit&, u32bit, u32bit, u32bit, u32bit, byte, u32bit);
      static void II(u32bit&, u32bit, u32bit, u32bit, u32bit, byte, u32bit);
      SecureBuffer<u32bit, 16> M;
      SecureBuffer<u32bit, 4> digest;
   };

}

#endif
