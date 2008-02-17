/*************************************************
* KDF Header File                                *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#ifndef BOTAN_KDF_H__
#define BOTAN_KDF_H__

#include <botan/pk_util.h>

namespace Botan {

/*************************************************
* KDF1                                           *
*************************************************/
class KDF1 : public KDF
   {
   public:
      KDF1(const std::string&);
   private:
      SecureVector<byte> derive(u32bit, const byte[], u32bit,
                                const byte[], u32bit) const;

      const std::string hash_name;
   };

/*************************************************
* KDF2                                           *
*************************************************/
class KDF2 : public KDF
   {
   public:

      KDF2(const std::string&);
   private:
      SecureVector<byte> derive(u32bit, const byte[], u32bit,
                                const byte[], u32bit) const;
      const std::string hash_name;
   };

/*************************************************
* X9.42 PRF                                      *
*************************************************/
class X942_PRF : public KDF
   {
   public:
      X942_PRF(const std::string&);
   private:
      SecureVector<byte> derive(u32bit, const byte[], u32bit,
                                const byte[], u32bit) const;

      std::string key_wrap_oid;
   };

}

#endif
