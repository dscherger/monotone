/*************************************************
* Diffie-Hellman Header File                     *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_DIFFIE_HELLMAN_H__
#define BOTAN_DIFFIE_HELLMAN_H__

#include <botan/dl_algo.h>
#include <botan/pk_core.h>

namespace Botan {

/*************************************************
* Diffie-Hellman Public Key                      *
*************************************************/
class DH_PublicKey : public virtual DL_Scheme_PublicKey
   {
   public:
      MemoryVector<byte> public_value() const;
      DH_PublicKey(const DL_Group&, const BigInt&);
   protected:
      std::string algo_name() const { return "DH"; }
      DH_PublicKey() {}
   private:
      friend X509_PublicKey* get_public_key(const std::string&);
      DL_Group::Format group_format() const { return DL_Group::ANSI_X9_42; }
      void X509_load_hook();
   };

/*************************************************
* Diffie-Hellman Private Key                     *
*************************************************/
class DH_PrivateKey : public DH_PublicKey,
                      public PK_Key_Agreement_Key,
                      public virtual DL_Scheme_PrivateKey
   {
   public:
      SecureVector<byte> derive_key(const byte[], u32bit) const;
      SecureVector<byte> derive_key(const DH_PublicKey&) const;
      SecureVector<byte> derive_key(const BigInt&) const;

      MemoryVector<byte> public_value() const;

      DH_PrivateKey(const DL_Group&);
      DH_PrivateKey(const DL_Group&, const BigInt&, const BigInt& = 0);
   private:
      friend PKCS8_PrivateKey* get_private_key(const std::string&);
      void PKCS8_load_hook();
      DH_PrivateKey() {}

      DH_Core core;
   };

}

#endif
