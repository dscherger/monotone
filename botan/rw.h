/*************************************************
* Rabin-Williams Header File                     *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_RW_H__
#define BOTAN_RW_H__

#include <botan/if_algo.h>

namespace Botan {

/*************************************************
* Rabin-Williams Public Key                      *
*************************************************/
class RW_PublicKey : public PK_Verifying_with_MR_Key,
                     public virtual IF_Scheme_PublicKey
   {
   public:
      SecureVector<byte> verify(const byte[], u32bit) const;

      RW_PublicKey(const BigInt&, const BigInt&);
   protected:
      BigInt public_op(const BigInt&) const;
      std::string algo_name() const { return "RW"; }
      RW_PublicKey() {}
   private:
      friend X509_PublicKey* get_public_key(const std::string&);
   };

/*************************************************
* Rabin-Williams Private Key                     *
*************************************************/
class RW_PrivateKey : public RW_PublicKey, public PK_Signing_Key,
                      public IF_Scheme_PrivateKey
   {
   public:
      SecureVector<byte> sign(const byte[], u32bit) const;

      bool check_key(bool) const;

      RW_PrivateKey(const BigInt&, const BigInt&, const BigInt&,
                    const BigInt& = 0, const BigInt& = 0);
      RW_PrivateKey(u32bit, u32bit = 2);
   private:
      friend PKCS8_PrivateKey* get_private_key(const std::string&);
      RW_PrivateKey() {}
   };

}

#endif
