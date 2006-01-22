/*************************************************
* Lowest Level MPI Algorithms Header File        *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#ifndef BOTAN_MP_ASM_H__
#define BOTAN_MP_ASM_H__

#include <botan/mp_types.h>

#if (BOTAN_MP_WORD_BITS == 8)
  typedef Botan::u16bit dword;
#elif (BOTAN_MP_WORD_BITS == 16)
  typedef Botan::u32bit dword;
#elif (BOTAN_MP_WORD_BITS == 32)
  typedef Botan::u64bit dword;
#elif (BOTAN_MP_WORD_BITS == 64)
  #error BOTAN_MP_WORD_BITS can only be 64 with the mp_asm64 module
#else
  #error BOTAN_MP_WORD_BITS must be 8, 16, 32, or 64
#endif

namespace Botan {

/*************************************************
* Multiply-Carry Operation                       *
*************************************************/
inline word word_mul(word a, word b, word* carry)
   {
   dword z = (dword)a * b + (*carry);
   *carry = (word)((z >> BOTAN_MP_WORD_BITS) & MP_WORD_MAX);
   return (word)(z & MP_WORD_MAX);
   }

/*************************************************
* Multiply-Add Operation                         *
*************************************************/
inline void word_madd(word a, word b, word c, word d,
                      word* out_low, word* out_high)
   {
   dword z = (dword)a * b + c + d;
   *out_low =  (word)((z                      ) & MP_WORD_MAX);
   *out_high = (word)((z >> BOTAN_MP_WORD_BITS) & MP_WORD_MAX);
   }

}

#endif
