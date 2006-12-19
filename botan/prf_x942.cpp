/*************************************************
* X9.42 PRF Source File                          *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/kdf.h>
#include <botan/der_enc.h>
#include <botan/oids.h>
#include <botan/lookup.h>
#include <botan/bit_ops.h>
#include <algorithm>
#include <memory>

namespace Botan {

namespace {

/*************************************************
* Encode an integer as an OCTET STRING           *
*************************************************/
MemoryVector<byte> encode_x942_int(u32bit n)
   {
   byte n_buf[4] = { 0 };
   for(u32bit j = 0; j != 4; ++j)
      n_buf[j] = get_byte(j, n);

   return DER_Encoder().encode(n_buf, 4, OCTET_STRING).get_contents();
   }

}

/*************************************************
* X9.42 PRF                                      *
*************************************************/
SecureVector<byte> X942_PRF::derive(u32bit key_len,
                                    const byte secret[], u32bit secret_len,
                                    const byte salt[], u32bit salt_len) const
   {
   std::auto_ptr<HashFunction> hash(get_hash("SHA-1"));
   const OID kek_algo(key_wrap_oid);

   SecureVector<byte> key;
   u32bit counter = 1;

   while(key.size() != key_len)
      {
      hash->update(secret, secret_len);

      hash->update(
         DER_Encoder().start_cons(SEQUENCE)

            .start_cons(SEQUENCE)
               .encode(kek_algo)
               .raw_bytes(encode_x942_int(counter))
            .end_cons()

            .encode_if(salt_len != 0,
               DER_Encoder()
                  .start_explicit(0)
                     .encode(salt, salt_len, OCTET_STRING)
                  .end_explicit()
               )

            .start_explicit(2)
               .raw_bytes(encode_x942_int(8 * key_len))
            .end_explicit()

         .end_cons().get_contents()
         );

      SecureVector<byte> digest = hash->final();
      key.append(digest, std::min(digest.size(), key_len - key.size()));

      ++counter;
      }

   return key;
   }

/*************************************************
* X9.42 Constructor                              *
*************************************************/
X942_PRF::X942_PRF(const std::string& oid)
   {
   if(OIDS::have_oid(oid))
      key_wrap_oid = OIDS::lookup(oid).as_string();
   else
      key_wrap_oid = oid;
   }

}
