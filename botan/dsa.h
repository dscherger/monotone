/*************************************************
* DSA Header File                                *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_DSA_H__
#define BOTAN_DSA_H__

#include <botan/dl_algo.h>
#include <botan/pk_core.h>

namespace Botan {

/*************************************************
* DSA Public Key                                 *
*************************************************/
class DSA_PublicKey : public PK_Verifying_wo_MR_Key,
                      public virtual DL_Scheme_PublicKey
   {
   public:
      bool verify(const byte[], u32bit, const byte[], u32bit) const;
      u32bit max_input_bits() const;

      DSA_PublicKey(const DL_Group&, const BigInt&);
   protected:
      std::string algo_name() const { return "DSA"; }
      DSA_PublicKey() {}

      DSA_Core core;
   private:
      friend X509_PublicKey* get_public_key(const std::string&);
      DL_Group::Format group_format() const { return DL_Group::ANSI_X9_57; }
      u32bit message_parts() const { return 2; }
      u32bit message_part_size() const;
      void X509_load_hook();
   };

/*************************************************
* DSA Private Key                                *
*************************************************/
class DSA_PrivateKey : public DSA_PublicKey, public PK_Signing_Key,
                       public virtual DL_Scheme_PrivateKey
   {
   public:
      SecureVector<byte> sign(const byte[], u32bit) const;

      bool check_key(bool) const;

      DSA_PrivateKey(const DL_Group&);
      DSA_PrivateKey(const DL_Group&, const BigInt&, const BigInt& = 0);
   private:
      friend PKCS8_PrivateKey* get_private_key(const std::string&);
      void PKCS8_load_hook();
      DSA_PrivateKey() {}
   };

}

#endif
