/*************************************************
* Hash Function Identification Source File       *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#include <botan/hash_id.h>
#include <botan/lookup.h>

namespace Botan {

namespace PKCS_IDS {

const byte MD2_ID[] = {
0x30, 0x20, 0x30, 0x0C, 0x06, 0x08, 0x2A, 0x86, 0x48, 0x86,
0xF7, 0x0D, 0x02, 0x02, 0x05, 0x00, 0x04, 0x10 };

const byte MD5_ID[] = {
0x30, 0x20, 0x30, 0x0C, 0x06, 0x08, 0x2A, 0x86, 0x48, 0x86,
0xF7, 0x0D, 0x02, 0x05, 0x05, 0x00, 0x04, 0x10 };

const byte RIPEMD_128_ID[] = {
0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2B, 0x24, 0x03, 0x02,
0x02, 0x05, 0x00, 0x04, 0x14 };

const byte RIPEMD_160_ID[] = {
0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2B, 0x24, 0x03, 0x02,
0x01, 0x05, 0x00, 0x04, 0x14 };

const byte SHA_160_ID[] = {
0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02,
0x1A, 0x05, 0x00, 0x04, 0x14 };

const byte SHA_256_ID[] = {
0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20 };

const byte SHA_384_ID[] = {
0x30, 0x41, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30 };

const byte SHA_512_ID[] = {
0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40 };

const byte TIGER_ID[] = {
0x30, 0x29, 0x30, 0x0D, 0x06, 0x09, 0x2B, 0x06, 0x01, 0x04,
0x01, 0xDA, 0x47, 0x0C, 0x02, 0x05, 0x00, 0x04, 0x18 };

}

/*************************************************
* Return the HashID, as specified by PKCS        *
*************************************************/
MemoryVector<byte> pkcs_hash_id(const std::string& name_or_alias)
   {
   const std::string name = deref_alias(name_or_alias);

   MemoryVector<byte> out;

   if(name == "Parallel(MD5,SHA-160)")
      return out;

   if(name == "MD2")
      out.set(PKCS_IDS::MD2_ID, sizeof(PKCS_IDS::MD2_ID));
   else if(name == "MD5")
      out.set(PKCS_IDS::MD5_ID, sizeof(PKCS_IDS::MD5_ID));
   else if(name == "RIPEMD-128")
      out.set(PKCS_IDS::RIPEMD_128_ID, sizeof(PKCS_IDS::RIPEMD_128_ID));
   else if(name == "RIPEMD-160")
      out.set(PKCS_IDS::RIPEMD_160_ID, sizeof(PKCS_IDS::RIPEMD_160_ID));
   else if(name == "SHA-160")
      out.set(PKCS_IDS::SHA_160_ID, sizeof(PKCS_IDS::SHA_160_ID));
   else if(name == "SHA-256")
      out.set(PKCS_IDS::SHA_256_ID, sizeof(PKCS_IDS::SHA_256_ID));
   else if(name == "SHA-384")
      out.set(PKCS_IDS::SHA_384_ID, sizeof(PKCS_IDS::SHA_384_ID));
   else if(name == "SHA-512")
      out.set(PKCS_IDS::SHA_512_ID, sizeof(PKCS_IDS::SHA_512_ID));
   else if(name == "Tiger(24,3)")
      out.set(PKCS_IDS::TIGER_ID, sizeof(PKCS_IDS::TIGER_ID));

   if(out.size())
      return out;

   throw Invalid_Argument("No PKCS #1 identifier for " + name_or_alias);
   }

/*************************************************
* Return the HashID, as specified by IEEE 1363   *
*************************************************/
byte ieee1363_hash_id(const std::string& name_or_alias)
   {
   const std::string name = deref_alias(name_or_alias);

   if(name == "RIPEMD-160") return 0x31;
   if(name == "RIPEMD-128") return 0x32;
   if(name == "SHA-160")    return 0x33;
   if(name == "SHA-256")    return 0x34;
   if(name == "SHA-512")    return 0x35;
   if(name == "SHA-384")    return 0x36;
   if(name == "Whirlpool")  return 0x37;
   return 0;
   }

}
