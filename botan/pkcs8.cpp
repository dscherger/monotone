/*************************************************
* PKCS #8 Source File                            *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/pkcs8.h>
#include <botan/asn1_obj.h>
#include <botan/pk_algs.h>
#include <botan/conf.h>
#include <botan/oids.h>
#include <botan/pem.h>
#include <botan/pbe.h>
#include <memory>

namespace Botan {

namespace PKCS8 {

namespace {

/* XXX this is monotone specific XXX */
/*************************************************
* Get info from an RAW_BER pkcs8 key.            *
* Whether it is encrypted will be determined,    *
* returned in is_encrypted.                      *
*************************************************/
SecureVector<byte> PKCS8_maybe_enc_extract(DataSource& source,
                                 AlgorithmIdentifier& alg_id,
                                 bool& is_encrypted)
   {
   SecureVector<byte> enc_pkcs8_key;
   u32bit version = 0;

   is_encrypted = false;
   try {
      BER_Decoder decoder(source);
      BER_Decoder sequence = BER::get_subsequence(decoder);

      try {
         BER::decode(sequence, version);
         }
      catch(Decoding_Error) {
         is_encrypted = true;
         }

      BER::decode(sequence, alg_id);
      BER::decode(sequence, enc_pkcs8_key, OCTET_STRING);
      if (is_encrypted)
         sequence.discard_remaining();
      sequence.verify_end();
      }
   catch(Decoding_Error)
      {
      throw PKCS8_Exception("Private key decoding failed");
      }

   if (version != 0)
      throw Decoding_Error("PKCS #8: Unknown version number");


   return enc_pkcs8_key;
   }

/*************************************************
* Get info from an EncryptedPrivateKeyInfo       *
*************************************************/
SecureVector<byte> PKCS8_extract(DataSource& source,
                                 AlgorithmIdentifier& alg_id)
   {
   SecureVector<byte> enc_pkcs8_key;

   try {
      BER_Decoder decoder(source);
      BER_Decoder sequence = BER::get_subsequence(decoder);
      BER::decode(sequence, alg_id);
      BER::decode(sequence, enc_pkcs8_key, OCTET_STRING);
      sequence.verify_end();
      }
   catch(Decoding_Error)
      {
      throw PKCS8_Exception("Private key decoding failed");
      }

   return enc_pkcs8_key;
   }

/*************************************************
* PEM decode and/or decrypt a private key        *
*************************************************/
SecureVector<byte> PKCS8_decode(DataSource& source, const User_Interface& ui,
                                AlgorithmIdentifier& pk_alg_id)
   {
   AlgorithmIdentifier pbe_alg_id;
   SecureVector<byte> key_data, key;
   bool is_encrypted = true;

   try {
      if(BER::maybe_BER(source) && !PEM_Code::matches(source))
         {
         key_data = PKCS8_maybe_enc_extract(source, pbe_alg_id, is_encrypted);
         if(key_data.is_empty())
            throw Decoding_Error("PKCS #8 private key decoding failed");
         if(!is_encrypted)
            {
            pk_alg_id = pbe_alg_id;
            return key_data; // just plain unencrypted BER
            }
         }
      else
         {
         std::string label;
         key_data = PEM_Code::decode(source, label);
         if(label == "PRIVATE KEY")
            is_encrypted = false;
         else if(label == "ENCRYPTED PRIVATE KEY")
            {
            DataSource_Memory source(key_data);
            key_data = PKCS8_extract(source, pbe_alg_id);
            }
         else
            throw PKCS8_Exception("Unknown PEM label " + label);
         }

      if(key_data.is_empty())
         throw PKCS8_Exception("No key data found");
      }
   catch(Decoding_Error)
      {
      throw Decoding_Error("PKCS #8 private key decoding failed");
      }

   if(!is_encrypted)
      key = key_data;

   u32bit tries = 0;
   while(true)
      {
      try {
         if(tries >= Config::get_u32bit("base/pkcs8_tries"))
            break;

         if(is_encrypted)
            {
            DataSource_Memory params(pbe_alg_id.parameters);
            PBE* pbe = get_pbe(pbe_alg_id.oid, params);

            User_Interface::UI_Result result = User_Interface::OK;
            const std::string passphrase =
               ui.get_passphrase("PKCS #8 private key", source.id(), result);

            if(result == User_Interface::CANCEL_ACTION)
               break;

            pbe->set_key(passphrase);
            Pipe decryptor(pbe);
            decryptor.process_msg(key_data, key_data.size());
            key = decryptor.read_all();
            }

         u32bit version;
         BER_Decoder decoder(key);
         BER_Decoder sequence = BER::get_subsequence(decoder);
         BER::decode(sequence, version);
         if(version != 0)
            throw Decoding_Error("PKCS #8: Unknown version number");

         BER::decode(sequence, pk_alg_id);
         BER::decode(sequence, key, OCTET_STRING);
         sequence.discard_remaining();
         sequence.verify_end();

         break;
         }
      catch(Decoding_Error)
         {
         tries++;
         }
      }

   if(key.is_empty())
      throw Decoding_Error("PKCS #8 private key decoding failed");
   return key;
   }

}

/*************************************************
* DER or PEM encode a PKCS #8 private key        *
*************************************************/
void encode(const PKCS8_PrivateKey& key, Pipe& pipe, X509_Encoding encoding)
   {
   AlgorithmIdentifier alg_id(key.get_oid(), key.DER_encode_params());

   DER_Encoder encoder;
   encoder.start_sequence();
     DER::encode(encoder, 0);
     DER::encode(encoder, alg_id);
     DER::encode(encoder, key.DER_encode_priv(), OCTET_STRING);
   encoder.end_sequence();

   if(encoding == PEM)
      pipe.write(PEM_Code::encode(encoder.get_contents(), "PRIVATE KEY"));
   else
      pipe.write(encoder.get_contents());
   }

/*************************************************
* Encode and encrypt a PKCS #8 private key       *
*************************************************/
void encrypt_key(const PKCS8_PrivateKey& key, Pipe& pipe,
                 const std::string& pass, const std::string& pbe_algo,
                 X509_Encoding encoding)
   {
   const std::string DEFAULT_PBE = Config::get_string("base/default_pbe");

   Pipe raw_key;
   raw_key.start_msg();
   encode(key, raw_key, RAW_BER);
   raw_key.end_msg();

   PBE* pbe = get_pbe(((pbe_algo != "") ? pbe_algo : DEFAULT_PBE));
   pbe->set_key(pass);
   AlgorithmIdentifier pbe_id(pbe->get_oid(), pbe->encode_params());
   Pipe key_encrytor(pbe);
   key_encrytor.process_msg(raw_key);

   DER_Encoder encoder;
   encoder.start_sequence();
   DER::encode(encoder, pbe_id);
   DER::encode(encoder, key_encrytor.read_all(), OCTET_STRING);
   encoder.end_sequence();
   SecureVector<byte> enc_key = encoder.get_contents();

   if(encoding == PEM)
      pipe.write(PEM_Code::encode(enc_key, "ENCRYPTED PRIVATE KEY"));
   else
      pipe.write(enc_key);
   }

/*************************************************
* PEM encode a PKCS #8 private key               *
*************************************************/
std::string PEM_encode(const PKCS8_PrivateKey& key)
   {
   Pipe pem;
   pem.start_msg();
   encode(key, pem, PEM);
   pem.end_msg();
   return pem.read_all_as_string();
   }

/*************************************************
* Encrypt and PEM encode a PKCS #8 private key   *
*************************************************/
std::string PEM_encode(const PKCS8_PrivateKey& key, const std::string& pass,
                       const std::string& pbe_algo)
   {
   if(pass == "")
      return PEM_encode(key);

   Pipe pem;
   pem.start_msg();
   encrypt_key(key, pem, pass, pbe_algo, PEM);
   pem.end_msg();
   return pem.read_all_as_string();
   }

/*************************************************
* Extract a private key and return it            *
*************************************************/
PKCS8_PrivateKey* load_key(DataSource& source, const User_Interface& ui)
   {
   AlgorithmIdentifier alg_id;

   SecureVector<byte> pkcs8_key = PKCS8_decode(source, ui, alg_id);

   const std::string alg_name = OIDS::lookup(alg_id.oid);
   if(alg_name == "")
      throw PKCS8_Exception("Unknown algorithm OID: " +
                            alg_id.oid.as_string());

   std::auto_ptr<PKCS8_PrivateKey> key(get_private_key(alg_name));

   if(!key.get())
      throw PKCS8_Exception("Unknown PK algorithm/OID: " + alg_name + ", " +
                           alg_id.oid.as_string());

   Pipe output;
   output.process_msg(alg_id.parameters);
   output.process_msg(pkcs8_key);
   key->BER_decode_params(output);
   output.set_default_msg(1);
   key->BER_decode_priv(output);

   return key.release();
   }

/*************************************************
* Extract a private key and return it            *
*************************************************/
PKCS8_PrivateKey* load_key(const std::string& fsname, const User_Interface& ui)
   {
   DataSource_Stream source(fsname);
   return PKCS8::load_key(source, ui);
   }

/*************************************************
* Extract a private key and return it            *
*************************************************/
PKCS8_PrivateKey* load_key(DataSource& source, const std::string& pass)
   {
   return PKCS8::load_key(source, User_Interface(pass));
   }

/*************************************************
* Extract a private key and return it            *
*************************************************/
PKCS8_PrivateKey* load_key(const std::string& fsname, const std::string& pass)
   {
   DataSource_Stream source(fsname);
   return PKCS8::load_key(source, User_Interface(pass));
   }

/*************************************************
* Make a copy of this private key                *
*************************************************/
PKCS8_PrivateKey* copy_key(const PKCS8_PrivateKey& key)
   {
   Pipe bits;

   bits.start_msg();
   PKCS8::encode(key, bits);
   bits.end_msg();

   DataSource_Memory source(bits.read_all());
   return PKCS8::load_key(source);
   }

}

}
