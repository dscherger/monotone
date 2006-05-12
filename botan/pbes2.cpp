/*************************************************
* PKCS #5 PBES2 Source File                      *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/pbe_pkcs.h>
#include <botan/parsing.h>
#include <botan/lookup.h>
#include <botan/rng.h>
#include <botan/asn1_obj.h>
#include <botan/oids.h>
#include <algorithm>
#include <memory>

namespace Botan {

/*************************************************
* Encrypt some bytes using PBES2                 *
*************************************************/
void PBE_PKCS5v20::write(const byte input[], u32bit length)
   {
   while(length)
      {
      u32bit put = std::min(DEFAULT_BUFFERSIZE, length);
      pipe.write(input, length);
      flush_pipe(true);
      length -= put;
      }
   }

/*************************************************
* Start encrypting with PBES2                    *
*************************************************/
void PBE_PKCS5v20::start_msg()
   {
   pipe.append(get_cipher(cipher, key, iv, direction));
   pipe.start_msg();
   if(pipe.message_count() > 1)
      pipe.set_default_msg(pipe.default_msg() + 1);
   }

/*************************************************
* Finish encrypting with PBES2                   *
*************************************************/
void PBE_PKCS5v20::end_msg()
   {
   pipe.end_msg();
   flush_pipe(false);
   pipe.reset();
   }

/*************************************************
* Flush the pipe                                 *
*************************************************/
void PBE_PKCS5v20::flush_pipe(bool safe_to_skip)
   {
   if(safe_to_skip && pipe.remaining() < 64)
      return;

   SecureVector<byte> buffer(DEFAULT_BUFFERSIZE);
   while(pipe.remaining())
      {
      u32bit got = pipe.read(buffer, buffer.size());
      send(buffer, got);
      }
   }

/*************************************************
* Set the passphrase to use                      *
*************************************************/
void PBE_PKCS5v20::set_key(const std::string& passphrase)
   {
   std::auto_ptr<S2K> pbkdf(get_s2k("PBKDF2(" + digest + ")"));
   pbkdf->set_iterations(iterations);
   pbkdf->change_salt(salt, salt.size());
   key = pbkdf->derive_key(key_length, passphrase).bits_of();
   }

/*************************************************
* Create a new set of PBES2 parameters           *
*************************************************/
void PBE_PKCS5v20::new_params()
   {
   iterations = 2048;
   key_length = max_keylength_of(cipher_algo);
   salt.create(8);
   iv.create(block_size_of(cipher_algo));
   Global_RNG::randomize(salt, salt.size());
   Global_RNG::randomize(iv, iv.size());
   }

/*************************************************
* Encode PKCS#5 PBES2 parameters                 *
*************************************************/
MemoryVector<byte> PBE_PKCS5v20::encode_params() const
   {
   AlgorithmIdentifier kdf_algo, enc_algo;

   DER_Encoder encoder;
   encoder.start_sequence();
   DER::encode(encoder, salt, OCTET_STRING);
   DER::encode(encoder, iterations);
   DER::encode(encoder, key_length);
   encoder.end_sequence();
   kdf_algo.parameters = encoder.get_contents();
   kdf_algo.oid = OIDS::lookup("PKCS5.PBKDF2");

   enc_algo.oid = OIDS::lookup(cipher);
   DER::encode(encoder, iv, OCTET_STRING);
   enc_algo.parameters = encoder.get_contents();

   encoder.start_sequence();
   DER::encode(encoder, kdf_algo);
   DER::encode(encoder, enc_algo);
   encoder.end_sequence();
   return encoder.get_contents();
   }

/*************************************************
* Decode PKCS#5 PBES2 parameters                 *
*************************************************/
void PBE_PKCS5v20::decode_params(DataSource& source)
   {
   AlgorithmIdentifier kdf_algo, enc_algo;

   BER_Decoder decoder(source);
   BER_Decoder sequence = BER::get_subsequence(decoder);
   BER::decode(sequence, kdf_algo);
   BER::decode(sequence, enc_algo);
   sequence.verify_end();

   if(kdf_algo.oid == OIDS::lookup("PKCS5.PBKDF2"))
      {
      digest = "SHA-160";
      BER_Decoder pbkdf2_params(kdf_algo.parameters);
      BER_Decoder algo_params = BER::get_subsequence(pbkdf2_params);
      BER::decode(algo_params, salt, OCTET_STRING);
      BER::decode(algo_params, iterations);
      BER::decode_optional(algo_params, key_length, INTEGER, UNIVERSAL);

      algo_params.verify_end();
      }
   else
      throw Decoding_Error("PBE-PKCS5 v2.0: Unknown KDF algorithm " +
                           kdf_algo.oid.as_string());

   cipher = OIDS::lookup(enc_algo.oid);
   std::vector<std::string> cipher_spec = split_on(cipher, '/');
   if(cipher_spec.size() != 2)
      throw Decoding_Error("PBE-PKCS5 v2.0: Invalid cipher spec " + cipher);
   cipher_algo = deref_alias(cipher_spec[0]);

   if(!known_cipher(cipher_algo) || cipher_spec[1] != "CBC")
      throw Decoding_Error("PBE-PKCS5 v2.0: Don't know param format for " +
                           cipher);

   BER_Decoder algo_params(enc_algo.parameters);
   BER::decode(algo_params, iv, OCTET_STRING);

   if(key_length == 0)
      key_length = max_keylength_of(cipher_algo);

   if(salt.size() < 8)
      throw Decoding_Error("PBE-PKCS5 v2.0: Encoded salt is too small");
   }

/*************************************************
* Return an OID for PBES2                        *
*************************************************/
OID PBE_PKCS5v20::get_oid() const
   {
   return OIDS::lookup("PBE-PKCS5v20");
   }

/*************************************************
* Check if this is a known PBES2 cipher          *
*************************************************/
bool PBE_PKCS5v20::known_cipher(const std::string& algo) const
   {
   if(algo == "AES-128" || algo == "AES-192" || algo == "AES-256")
      return true;
   if(algo == "DES" || algo == "TripleDES")
      return true;
   return false;
   }

/*************************************************
* PKCS#5 v2.0 PBE Constructor                    *
*************************************************/
PBE_PKCS5v20::PBE_PKCS5v20(const std::string& d_algo,
                           const std::string& c_algo) :
   direction(ENCRYPTION), digest(deref_alias(d_algo)), cipher(c_algo)
   {
   std::vector<std::string> cipher_spec = split_on(cipher, '/');
   if(cipher_spec.size() != 2)
      throw Invalid_Argument("PBE-PKCS5 v2.0: Invalid cipher spec " + cipher);
   cipher_algo = deref_alias(cipher_spec[0]);
   const std::string cipher_mode = cipher_spec[1];

   if(!have_block_cipher(cipher_algo))
      throw Algorithm_Not_Found(cipher_algo);
   if(!have_hash(digest))
      throw Algorithm_Not_Found(digest);

   if(!known_cipher(cipher_algo))
      throw Invalid_Argument("PBE-PKCS5 v2.0: Invalid cipher " + cipher);
   if(cipher_mode != "CBC")
      throw Invalid_Argument("PBE-PKCS5 v2.0: Invalid cipher " + cipher);
   if(digest != "SHA-160")
      throw Invalid_Argument("PBE-PKCS5 v2.0: Invalid digest " + digest);
   }

/*************************************************
* PKCS#5 v2.0 PBE Constructor                    *
*************************************************/
PBE_PKCS5v20::PBE_PKCS5v20(DataSource& params) : direction(DECRYPTION)
   {
   decode_params(params);
   }

}
