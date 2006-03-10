/*************************************************
* Public Key Base Source File                    *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/pubkey.h>
#include <botan/parsing.h>
#include <botan/bit_ops.h>
#include <botan/lookup.h>
#include <botan/asn1.h>
#include <memory>

namespace Botan {

/*************************************************
* Encrypt a message                              *
*************************************************/
SecureVector<byte> PK_Encryptor::encrypt(const byte in[], u32bit len) const
   {
   return enc(in, len);
   }

/*************************************************
* Encrypt a message                              *
*************************************************/
SecureVector<byte> PK_Encryptor::encrypt(const MemoryRegion<byte>& in) const
   {
   return enc(in.begin(), in.size());
   }

/*************************************************
* Decrypt a message                              *
*************************************************/
SecureVector<byte> PK_Decryptor::decrypt(const byte in[], u32bit len) const
   {
   return dec(in, len);
   }

/*************************************************
* Decrypt a message                              *
*************************************************/
SecureVector<byte> PK_Decryptor::decrypt(const MemoryRegion<byte>& in) const
   {
   return dec(in.begin(), in.size());
   }

/*************************************************
* PK_Encryptor_MR_with_EME Constructor           *
*************************************************/
PK_Encryptor_MR_with_EME::PK_Encryptor_MR_with_EME(const PK_Encrypting_Key& k,
                                                   const std::string& eme) :
   key(k), encoder((eme == "Raw") ? 0 : get_eme(eme))
   {
   }

/*************************************************
* Encrypt a message                              *
*************************************************/
SecureVector<byte> PK_Encryptor_MR_with_EME::enc(const byte msg[],
                                                 u32bit length) const
   {
   SecureVector<byte> message;
   if(encoder) message = encoder->encode(msg, length, key.max_input_bits());
   else        message.set(msg, length);

   if(8*(message.size() - 1) + high_bit(message[0]) > key.max_input_bits())
      throw Exception("PK_Encryptor_MR_with_EME: Input is too large");

   return key.encrypt(message, message.size());
   }

/*************************************************
* Return the max size, in bytes, of a message    *
*************************************************/
u32bit PK_Encryptor_MR_with_EME::maximum_input_size() const
   {
   if(!encoder)
      return (key.max_input_bits() / 8);
   else
      return encoder->maximum_input_size(key.max_input_bits());
   }

/*************************************************
* PK_Decryptor_MR_with_EME Constructor           *
*************************************************/
PK_Decryptor_MR_with_EME::PK_Decryptor_MR_with_EME(const PK_Decrypting_Key& k,
                                                   const std::string& eme) :
   key(k), encoder((eme == "Raw") ? 0 : get_eme(eme))
   {
   }

/*************************************************
* Decrypt a message                              *
*************************************************/
SecureVector<byte> PK_Decryptor_MR_with_EME::dec(const byte msg[],
                                                 u32bit length) const
   {
   try {
      SecureVector<byte> decrypted = key.decrypt(msg, length);
      if(encoder)
         return encoder->decode(decrypted, key.max_input_bits());
      else
         return decrypted;
      }
   catch(Invalid_Argument)
      {
      throw Exception("PK_Decryptor_MR_with_EME: Input is invalid");
      }
   catch(Decoding_Error)
      {
      throw Exception("PK_Decryptor_MR_with_EME: Input is invalid");
      }
   }

/*************************************************
* PK_Signer Constructor                          *
*************************************************/
PK_Signer::PK_Signer(const PK_Signing_Key& k, const std::string& emsa_name) :
   key(k), emsa(get_emsa(emsa_name))
   {
   sig_format = IEEE_1363;
   }

/*************************************************
* Set the signature format                       *
*************************************************/
void PK_Signer::set_output_format(Signature_Format format)
   {
   if(key.message_parts() == 1 && format != IEEE_1363)
      throw Invalid_State("PK_Signer: Cannot set the output format for " +
                          key.algo_name() + " keys");
   sig_format = format;
   }

/*************************************************
* Sign a message                                 *
*************************************************/
SecureVector<byte> PK_Signer::sign_message(const byte msg[], u32bit length)
   {
   update(msg, length);
   return signature();
   }

/*************************************************
* Sign a message                                 *
*************************************************/
SecureVector<byte> PK_Signer::sign_message(const MemoryRegion<byte>& msg)
   {
   return sign_message(msg, msg.size());
   }

/*************************************************
* Add more to the message to be signed           *
*************************************************/
void PK_Signer::update(const byte in[], u32bit length)
   {
   emsa->update(in, length);
   }

/*************************************************
* Add more to the message to be signed           *
*************************************************/
void PK_Signer::update(byte in)
   {
   update(&in, 1);
   }

/*************************************************
* Add more to the message to be signed           *
*************************************************/
void PK_Signer::update(const MemoryRegion<byte>& in)
   {
   update(in, in.size());
   }

/*************************************************
* Create a signature                             *
*************************************************/
SecureVector<byte> PK_Signer::signature()
   {
   SecureVector<byte> encoded = emsa->encoding_of(emsa->raw_data(),
                                                  key.max_input_bits());
   SecureVector<byte> plain_sig = key.sign(encoded, encoded.size());

   if(key.message_parts() == 1 || sig_format == IEEE_1363)
      return plain_sig;

   if(sig_format == DER_SEQUENCE)
      {
      if(plain_sig.size() % key.message_parts())
         throw Encoding_Error("PK_Signer: strange signature size found");
      const u32bit SIZE_OF_PART = plain_sig.size() / key.message_parts();

      std::vector<BigInt> sig_parts(key.message_parts());
      for(u32bit j = 0; j != sig_parts.size(); j++)
         sig_parts[j].binary_decode(plain_sig + SIZE_OF_PART*j, SIZE_OF_PART);

      DER_Encoder der_sig;
      der_sig.start_sequence();
      for(u32bit j = 0; j != sig_parts.size(); j++)
         DER::encode(der_sig, sig_parts[j]);
      der_sig.end_sequence();

      return der_sig.get_contents();
      }
   else
      throw Encoding_Error("PK_Signer: Unknown signature format " +
                           to_string(sig_format));
   }

/*************************************************
* PK_Verifier Constructor                        *
*************************************************/
PK_Verifier::PK_Verifier(const PK_Key& k, const std::string& emsa_name) :
   emsa(get_emsa(emsa_name)), key(k)
   {
   sig_format = IEEE_1363;
   }

/*************************************************
* Set the signature format                       *
*************************************************/
void PK_Verifier::set_input_format(Signature_Format format)
   {
   if(key.message_parts() == 1 && format != IEEE_1363)
      throw Invalid_State("PK_Verifier: This algorithm always uses IEEE 1363");
   sig_format = format;
   }

/*************************************************
* Verify a message                               *
*************************************************/
bool PK_Verifier::verify_message(const MemoryRegion<byte>& msg,
                                 const MemoryRegion<byte>& sig)
   {
   return verify_message(msg, msg.size(), sig, sig.size());
   }

/*************************************************
* Verify a message                               *
*************************************************/
bool PK_Verifier::verify_message(const byte msg[], u32bit msg_length,
                                 const byte sig[], u32bit sig_length)
   {
   update(msg, msg_length);
   return check_signature(sig, sig_length);
   }

/*************************************************
* Append to the message                          *
*************************************************/
void PK_Verifier::update(const byte in[], u32bit length)
   {
   emsa->update(in, length);
   }

/*************************************************
* Append to the message                          *
*************************************************/
void PK_Verifier::update(byte in)
   {
   update(&in, 1);
   }

/*************************************************
* Append to the message                          *
*************************************************/
void PK_Verifier::update(const MemoryRegion<byte>& in)
   {
   update(in, in.size());
   }

/*************************************************
* Check a signature                              *
*************************************************/
bool PK_Verifier::check_signature(const MemoryRegion<byte>& sig)
   {
   return check_signature(sig, sig.size());
   }

/*************************************************
* Check a signature                              *
*************************************************/
bool PK_Verifier::check_signature(const byte sig[], u32bit length)
   {
   try {
      if(sig_format == IEEE_1363)
         return validate_signature(emsa->raw_data(), sig, length);
      else if(sig_format == DER_SEQUENCE)
         {
         BER_Decoder decoder(sig, length);
         BER_Decoder ber_sig = BER::get_subsequence(decoder);

         u32bit count = 0;
         SecureVector<byte> real_sig;
         while(ber_sig.more_items())
            {
            BigInt sig_part;
            BER::decode(ber_sig, sig_part);
            real_sig.append(BigInt::encode_1363(sig_part,
                                                key.message_part_size()));
            count++;
            }
         if(count != key.message_parts())
            throw Decoding_Error("PK_Verifier: signature size invalid");

         return validate_signature(emsa->raw_data(),
                                   real_sig, real_sig.size());
         }
      else
         throw Decoding_Error("PK_Verifier: Unknown signature format " +
                              to_string(sig_format));
      }
   catch(Invalid_Argument) { return false; }
   catch(Decoding_Error) { return false; }
   }

/*************************************************
* PK_Verifier_with_MR Constructor                *
*************************************************/
PK_Verifier_with_MR::PK_Verifier_with_MR(const PK_Verifying_with_MR_Key& k,
                                         const std::string& emsa_name) :
   PK_Verifier(k, emsa_name), key(k)
   {
   }

/*************************************************
* Verify a signature                             *
*************************************************/
bool PK_Verifier_with_MR::validate_signature(const MemoryRegion<byte>& msg,
                                             const byte sig[], u32bit sig_len)
   {
   SecureVector<byte> output_of_key = key.verify(sig, sig_len);
   return emsa->verify(output_of_key, msg, key.max_input_bits());
   }

/*************************************************
* PK_Verifier_wo_MR Constructor                  *
*************************************************/
PK_Verifier_wo_MR::PK_Verifier_wo_MR(const PK_Verifying_wo_MR_Key& k,
                                     const std::string& emsa_name) :
   PK_Verifier(k, emsa_name), key(k)
   {
   }

/*************************************************
* Verify a signature                             *
*************************************************/
bool PK_Verifier_wo_MR::validate_signature(const MemoryRegion<byte>& msg,
                                           const byte sig[], u32bit sig_len)
   {
   SecureVector<byte> encoded = emsa->encoding_of(msg, key.max_input_bits());
   return key.verify(encoded, encoded.size(), sig, sig_len);
   }

/*************************************************
* PK_Key_Agreement Constructor                   *
*************************************************/
PK_Key_Agreement::PK_Key_Agreement(const PK_Key_Agreement_Key& k,
                                   const std::string& k_name) :
   key(k), kdf_name(k_name)
   {
   }

/*************************************************
* Perform Key Agreement Operation                *
*************************************************/
SymmetricKey PK_Key_Agreement::derive_key(u32bit key_len,
                                          const byte in[], u32bit in_len,
                                          const std::string& params) const
   {
   return derive_key(key_len, in, in_len,
                     (const byte*)params.c_str(), params.length());
   }

/*************************************************
* Perform Key Agreement Operation                *
*************************************************/
SymmetricKey PK_Key_Agreement::derive_key(u32bit key_len, const byte in[],
                                          u32bit in_len, const byte params[],
                                          u32bit params_len) const
   {
   std::auto_ptr<KDF> kdf((kdf_name == "Raw") ? 0 : get_kdf(kdf_name));
   OctetString z = key.derive_key(in, in_len);

   if(kdf.get())
      z = kdf->derive_key(key_len, z.bits_of(), params, params_len);

   return z;
   }

}
