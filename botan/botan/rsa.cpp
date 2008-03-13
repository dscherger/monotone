/*************************************************
* RSA Source File                                *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#include <botan/rsa.h>
#include <botan/numthry.h>
#include <botan/keypair.h>
#include <botan/parsing.h>

namespace Botan {

/*************************************************
* RSA_PublicKey Constructor                      *
*************************************************/
RSA_PublicKey::RSA_PublicKey(const BigInt& mod, const BigInt& exp)
   {
   n = mod;
   e = exp;
   X509_load_hook();
   }

/*************************************************
* RSA Public Operation                           *
*************************************************/
BigInt RSA_PublicKey::public_op(const BigInt& i) const
   {
   if(i >= n)
      throw Invalid_Argument(algo_name() + "::public_op: input is too large");
   return core.public_op(i);
   }

/*************************************************
* RSA Encryption Function                        *
*************************************************/
SecureVector<byte> RSA_PublicKey::encrypt(const byte in[], u32bit len) const
   {
   BigInt i(in, len);
   return BigInt::encode_1363(public_op(i), n.bytes());
   }

/*************************************************
* RSA Verification Function                      *
*************************************************/
SecureVector<byte> RSA_PublicKey::verify(const byte in[], u32bit len) const
   {
   BigInt i(in, len);
   return BigInt::encode(public_op(i));
   }

/*************************************************
* Create a RSA private key                       *
*************************************************/
RSA_PrivateKey::RSA_PrivateKey(u32bit bits, u32bit exp)
   {
   if(bits < 128)
      throw Invalid_Argument(algo_name() + ": Can't make a key that is only " +
                             to_string(bits) + " bits long");
   if(exp < 3 || exp % 2 == 0)
      throw Invalid_Argument(algo_name() + ": Invalid encryption exponent");

   e = exp;
   p = random_prime((bits + 1) / 2, e);
   q = random_prime(bits - p.bits(), e);
   d = inverse_mod(e, lcm(p - 1, q - 1));

   PKCS8_load_hook(true);

   if(n.bits() != bits)
      throw Self_Test_Failure(algo_name() + " private key generation failed");
   }

/*************************************************
* RSA_PrivateKey Constructor                     *
*************************************************/
RSA_PrivateKey::RSA_PrivateKey(const BigInt& prime1, const BigInt& prime2,
                               const BigInt& exp, const BigInt& d_exp,
                               const BigInt& mod)
   {
   p = prime1;
   q = prime2;
   e = exp;
   d = d_exp;
   n = mod;

   if(d == 0)
      d = inverse_mod(e, lcm(p - 1, q - 1));

   PKCS8_load_hook();
   }

/*************************************************
* RSA Private Operation                          *
*************************************************/
BigInt RSA_PrivateKey::private_op(const byte in[], u32bit length) const
   {
   BigInt i(in, length);
   if(i >= n)
      throw Invalid_Argument(algo_name() + "::private_op: input is too large");

   BigInt r = core.private_op(i);
   if(i != public_op(r))
      throw Self_Test_Failure(algo_name() + " private operation check failed");
   return r;
   }

/*************************************************
* RSA Decryption Operation                       *
*************************************************/
SecureVector<byte> RSA_PrivateKey::decrypt(const byte in[], u32bit len) const
   {
   return BigInt::encode(private_op(in, len));
   }

/*************************************************
* RSA Signature Operation                        *
*************************************************/
SecureVector<byte> RSA_PrivateKey::sign(const byte in[], u32bit len) const
   {
   return BigInt::encode_1363(private_op(in, len), n.bytes());
   }

/*************************************************
* Check Private RSA Parameters                   *
*************************************************/
bool RSA_PrivateKey::check_key(bool strong) const
   {
   if(!IF_Scheme_PrivateKey::check_key(strong))
      return false;

   if(!strong)
      return true;

   if((e * d) % lcm(p - 1, q - 1) != 1)
      return false;

   try {
      KeyPair::check_key(get_pk_encryptor(*this, "EME1(SHA-1)"),
                         get_pk_decryptor(*this, "EME1(SHA-1)")
         );

      KeyPair::check_key(get_pk_signer(*this, "EMSA4(SHA-1)"),
                         get_pk_verifier(*this, "EMSA4(SHA-1)")
         );
      }
   catch(Self_Test_Failure)
      {
      return false;
      }

   return true;
   }

}
