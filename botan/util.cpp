/*************************************************
* Utility Functions Source File                  *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/util.h>
#include <botan/exceptn.h>
#include <cmath>

namespace Botan {

/*************************************************
* XOR arrays together                            *
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
   for(u32bit j = 0; j != length; j++)
      data[j] ^= mask[j];
   }

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
   for(u32bit j = 0; j != length; j++)
      out[j] = in[j] ^ mask[j];
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
* Combine a two time values into a single one    *
*************************************************/
u64bit combine_timers(u32bit seconds, u32bit parts, u32bit parts_hz)
   {
   const u64bit NANOSECONDS_UNITS = 1000000000;
   parts *= (NANOSECONDS_UNITS / parts_hz);
   return ((seconds * NANOSECONDS_UNITS) + parts);
   }

/*************************************************
* Return the index of the highest set bit        *
*************************************************/
u32bit high_bit(u64bit n)
   {
   for(u32bit count = 64; count > 0; count--)
      if((n >> (count - 1)) & 0x01)
         return count;
   return 0;
   }

/*************************************************
* Return the index of the lowest set bit         *
*************************************************/
u32bit low_bit(u64bit n)
   {
   for(u32bit count = 0; count != 64; count++)
      if((n >> count) & 0x01)
         return (count + 1);
   return 0;
   }

/*************************************************
* Return the number of significant bytes in n    *
*************************************************/
u32bit significant_bytes(u64bit n)
   {
   for(u32bit j = 0; j != 8; j++)
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
   for(u32bit j = 0; j != 64; j++)
      if((n >> j) & 0x01)
         weight++;
   return weight;
   }

/*************************************************
* Round up n to multiple of align_to             *
*************************************************/
u32bit round_up(u32bit n, u32bit align_to)
   {
   if(n % align_to || n == 0)
      n += align_to - (n % align_to);
   return n;
   }

/*************************************************
* Round down n to multiple of align_to           *
*************************************************/
u32bit round_down(u32bit n, u32bit align_to)
   {
   return (n - (n % align_to));
   }

/*************************************************
* Return the work required for solving DL        *
*************************************************/
u32bit dl_work_factor(u32bit n_bits)
   {
   const u32bit MIN_ESTIMATE = 64;

   if(n_bits < 32)
      return 0;

   const double log_x = n_bits / 1.44;

   u32bit estimate = (u32bit)(2.76 * std::pow(log_x, 1.0/3.0) *
                                     std::pow(std::log(log_x), 2.0/3.0));

   return std::max(estimate, MIN_ESTIMATE);
   }

/*************************************************
* Convert an integer into a string               *
*************************************************/
std::string to_string(u64bit n, u32bit min_len)
   {
   std::string lenstr;
   if(n)
      {
      while(n > 0)
         {
         lenstr = digit2char(n % 10) + lenstr;
         n /= 10;
         }
      }
   else
      lenstr = "0";

   while(lenstr.size() < min_len)
      lenstr = "0" + lenstr;

   return lenstr;
   }

/*************************************************
* Convert an integer into a string               *
*************************************************/
u32bit to_u32bit(const std::string& number)
   {
   u32bit n = 0;

   for(std::string::const_iterator j = number.begin(); j != number.end(); j++)
      {
      const u32bit OVERFLOW_MARK = 0xFFFFFFFF / 10;

      byte digit = char2digit(*j);

      if((n > OVERFLOW_MARK) || (n == OVERFLOW_MARK && digit > 5))
         throw Decoding_Error("to_u32bit: Integer overflow");
      n *= 10;
      n += digit;
      }
   return n;
   }

/*************************************************
* Estimate the entropy of the buffer             *
*************************************************/
u32bit entropy_estimate(const byte buffer[], u32bit length)
   {
   if(length <= 4)
      return 0;

   u32bit estimate = 0;
   byte last = 0, last_delta = 0, last_delta2 = 0;

   for(u32bit j = 0; j != length; j++)
      {
      byte delta = last ^ buffer[j];
      last = buffer[j];

      byte delta2 = delta ^ last_delta;
      last_delta = delta;

      byte delta3 = delta2 ^ last_delta2;
      last_delta2 = delta2;

      byte min_delta = delta;
      if(min_delta > delta2) min_delta = delta2;
      if(min_delta > delta3) min_delta = delta3;

      estimate += hamming_weight(min_delta);
      }

   return (estimate / 2);
   }

}
