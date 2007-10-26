/*************************************************
* Public Key Interface Header File               *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#ifndef BOTAN_PUBKEY_H__
#define BOTAN_PUBKEY_H__

#include <botan/base.h>
#include <botan/pk_keys.h>
#include <botan/pk_util.h>

namespace Botan {

/*************************************************
* Public Key Encryptor                           *
*************************************************/
class PK_Encryptor
   {
   public:
      SecureVector<byte> encrypt(const byte[], u32bit) const;
      SecureVector<byte> encrypt(const MemoryRegion<byte>&) const;
      virtual u32bit maximum_input_size() const = 0;
      virtual ~PK_Encryptor() {}
   private:
      virtual SecureVector<byte> enc(const byte[], u32bit) const = 0;
   };

/*************************************************
* Public Key Decryptor                           *
*************************************************/
class PK_Decryptor
   {
   public:
      SecureVector<byte> decrypt(const byte[], u32bit) const;
      SecureVector<byte> decrypt(const MemoryRegion<byte>&) const;
      virtual ~PK_Decryptor() {}
   private:
      virtual SecureVector<byte> dec(const byte[], u32bit) const = 0;
   };

/*************************************************
* Public Key Signer                              *
*************************************************/
class PK_Signer
   {
   public:
      SecureVector<byte> sign_message(const byte[], u32bit);
      SecureVector<byte> sign_message(const MemoryRegion<byte>&);

      void update(byte);
      void update(const byte[], u32bit);
      void update(const MemoryRegion<byte>&);

      SecureVector<byte> signature();

      void set_output_format(Signature_Format);

      PK_Signer(const PK_Signing_Key&, const std::string&);
      ~PK_Signer() { delete emsa; }
   private:
      const PK_Signing_Key& key;
      Signature_Format sig_format;
      EMSA* emsa;
   };

/*************************************************
* Public Key Verifier                            *
*************************************************/
class PK_Verifier
   {
   public:
      bool verify_message(const byte[], u32bit, const byte[], u32bit);
      bool verify_message(const MemoryRegion<byte>&,
                          const MemoryRegion<byte>&);

      void update(byte);
      void update(const byte[], u32bit);
      void update(const MemoryRegion<byte>&);

      bool check_signature(const byte[], u32bit);
      bool check_signature(const MemoryRegion<byte>&);

      void set_input_format(Signature_Format);

      PK_Verifier(const std::string&);
      virtual ~PK_Verifier();
   protected:
      virtual bool validate_signature(const MemoryRegion<byte>&,
                                      const byte[], u32bit) = 0;
      virtual u32bit key_message_parts() const = 0;
      virtual u32bit key_message_part_size() const = 0;

      Signature_Format sig_format;
      EMSA* emsa;
   };

/*************************************************
* Key Agreement                                  *
*************************************************/
class PK_Key_Agreement
   {
   public:
      SymmetricKey derive_key(u32bit, const byte[], u32bit,
                              const std::string& = "") const;
      SymmetricKey derive_key(u32bit, const byte[], u32bit,
                                      const byte[], u32bit) const;

      PK_Key_Agreement(const PK_Key_Agreement_Key&, const std::string&);
   private:
      const PK_Key_Agreement_Key& key;
      const std::string kdf_name;
   };

/*************************************************
* Encryption with an MR algorithm and an EME     *
*************************************************/
class PK_Encryptor_MR_with_EME : public PK_Encryptor
   {
   public:
      u32bit maximum_input_size() const;
      PK_Encryptor_MR_with_EME(const PK_Encrypting_Key&, const std::string&);
      ~PK_Encryptor_MR_with_EME() { delete encoder; }
   private:
      SecureVector<byte> enc(const byte[], u32bit) const;
      const PK_Encrypting_Key& key;
      const EME* encoder;
   };

/*************************************************
* Decryption with an MR algorithm and an EME     *
*************************************************/
class PK_Decryptor_MR_with_EME : public PK_Decryptor
   {
   public:
      PK_Decryptor_MR_with_EME(const PK_Decrypting_Key&, const std::string&);
      ~PK_Decryptor_MR_with_EME() { delete encoder; }
   private:
      SecureVector<byte> dec(const byte[], u32bit) const;
      const PK_Decrypting_Key& key;
      const EME* encoder;
   };

/*************************************************
* Public Key Verifier with Message Recovery      *
*************************************************/
class PK_Verifier_with_MR : public PK_Verifier
   {
   public:
      PK_Verifier_with_MR(const PK_Verifying_with_MR_Key&, const std::string&);
   private:
      bool validate_signature(const MemoryRegion<byte>&, const byte[], u32bit);
      u32bit key_message_parts() const { return key.message_parts(); }
      u32bit key_message_part_size() const { return key.message_part_size(); }

      const PK_Verifying_with_MR_Key& key;
   };

/*************************************************
* Public Key Verifier without Message Recovery   *
*************************************************/
class PK_Verifier_wo_MR : public PK_Verifier
   {
   public:
      PK_Verifier_wo_MR(const PK_Verifying_wo_MR_Key&, const std::string&);
   private:
      bool validate_signature(const MemoryRegion<byte>&, const byte[], u32bit);
      u32bit key_message_parts() const { return key.message_parts(); }
      u32bit key_message_part_size() const { return key.message_part_size(); }

      const PK_Verifying_wo_MR_Key& key;
   };

}

#endif
