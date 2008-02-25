/*************************************************
* Bit/Word Operations Source File                *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#include <botan/bit_ops.h>
#include <botan/loadstor.h>

namespace Botan {

/*************************************************
* Array XOR                                      *
*************************************************/
void xor_buf(byte data[], const byte mask[], u32bit length)
   {
   while(length >= 8)
      {
      data[0] ^= mask[0]; data[1] ^= mask[1];
      data[2] ^= mask[2]; data[3] ^= mask[3];
      data[4] ^= mask[4]; data[5] ^= mask[5];
      data[6] ^= mask[6]; data[7] ^= mask[7];
      data += 8; mask += 8; length -= 8;
      }
   for(u32bit j = 0; j != length; ++j)
      data[j] ^= mask[j];
   }

/*************************************************
* Array XOR                                      *
*************************************************/
void xor_buf(byte out[], const byte in[], const byte mask[], u32bit length)
   {
   while(length >= 8)
      {
      out[0] = in[0] ^ mask[0]; out[1] = in[1] ^ mask[1];
      out[2] = in[2] ^ mask[2]; out[3] = in[3] ^ mask[3];
      out[4] = in[4] ^ mask[4]; out[5] = in[5] ^ mask[5];
      out[6] = in[6] ^ mask[6]; out[7] = in[7] ^ mask[7];
      in += 8; out += 8; mask += 8; length -= 8;
      }
   for(u32bit j = 0; j != length; ++j)
      out[j] = in[j] ^ mask[j];
   }

/*************************************************
* Reverse bytes                                   *
*************************************************/
u16bit reverse_bytes(u16bit input)
   {
   return rotate_left(input, 8);
   }

/*************************************************
* Reverse bytes                                   *
*************************************************/
u32bit reverse_bytes(u32bit input)
   {
   input = ((input & 0xFF00FF00) >> 8) | ((input & 0x00FF00FF) << 8);
   return rotate_left(input, 16);
   }

/*************************************************
* Reverse bytes                                   *
*************************************************/
u64bit reverse_bytes(u64bit input)
   {
   input = ((input & 0xFF00FF00FF00FF00ull) >>  8) |
           ((input & 0x00FF00FF00FF00FFull) <<  8);
   input = ((input & 0xFFFF0000FFFF0000ull) >> 16) |
           ((input & 0x0000FFFF0000FFFFull) << 16);
   return rotate_left(input, 32);
   }

/*************************************************
* Return true iff arg is 2**n for some n > 0     *
*************************************************/
bool power_of_2(u64bit arg)
   {
   if(arg == 0 || arg == 1)
      return false;
   if((arg & (arg-1)) == 0)
      return true;
   return false;
   }

/*************************************************
* Return the index of the highest set bit        *
*************************************************/
u32bit high_bit(u64bit n)
   {
   for(u32bit count = 64; count > 0; --count)
      if((n >> (count - 1)) & 0x01)
         return count;
   return 0;
   }

/*************************************************
* Return the index of the lowest set bit         *
*************************************************/
u32bit low_bit(u64bit n)
   {
   for(u32bit count = 0; count != 64; ++count)
      if((n >> count) & 0x01)
         return (count + 1);
   return 0;
   }

/*************************************************
* Return the number of significant bytes in n    *
*************************************************/
u32bit significant_bytes(u64bit n)
   {
   for(u32bit j = 0; j != 8; ++j)
      if(get_byte(j, n))
         return 8-j;
   return 0;
   }

/*************************************************
* Return the Hamming weight of n                 *
*************************************************/
u32bit hamming_weight(u64bit n)
   {
   u32bit weight = 0;
   for(u32bit j = 0; j != 64; ++j)
      if((n >> j) & 0x01)
         ++weight;
   return weight;
   }

}
