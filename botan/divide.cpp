/*************************************************
* Division Algorithm Source File                 *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/numthry.h>
#include <botan/mp_core.h>

namespace Botan {

/*************************************************
* Solve x = q * y + r                            *
*************************************************/
void divide(const BigInt& x, const BigInt& y_arg, BigInt& q, BigInt& r)
   {
   BigInt y = y_arg;
   r = x;

   r.set_sign(BigInt::Positive);
   y.set_sign(BigInt::Positive);

   modifying_divide(r, y, q);

   if(x.sign() == BigInt::Negative)
      {
      q.flip_sign();
      if(r.is_nonzero()) { --q; r = y_arg.abs() - r; }
      }
   if(y_arg.sign() == BigInt::Negative)
      q.flip_sign();
   }

/*************************************************
* Solve x = q * y + r                            *
*************************************************/
void positive_divide(const BigInt& x, const BigInt& y_arg,
                     BigInt& q, BigInt& r)
   {
   BigInt y = y_arg;
   r = x;
   modifying_divide(r, y, q);
   }

/*************************************************
* Solve x = q * y + r                            *
*************************************************/
void modifying_divide(BigInt& x, BigInt& y, BigInt& q)
   {
   if(y.is_zero())
      throw BigInt::DivideByZero();
   if(x.is_negative() || y.is_negative())
      throw Invalid_Argument("Arguments to modifying_divide must be positive");

   s32bit compare = x.cmp(y);
   if(compare == -1) { q = 0; return; }
   if(compare ==  0) { q = 1; x = 0; return; }

   u32bit shifts = 0;
   while(y[y.sig_words()-1] < MP_WORD_TOP_BIT)
      { x <<= 1; y <<= 1; shifts++; }

   u32bit n = x.sig_words() - 1, t = y.sig_words() - 1;
   q.reg.create(n - t + 1);
   if(n <= t)
      {
      while(x > y) { x -= y; q.add(1); }
      x >>= shifts;
      return;
      }

   BigInt temp = y << (MP_WORD_BITS * (n-t));

   while(x >= temp) { x -= temp; q[n-t]++; }

   for(u32bit j = n; j != t; j--)
      {
      const word x_j0  = x.word_at(j);
      const word x_j1 = x.word_at(j-1);
      const word y_t  = y.word_at(t);

      if(x_j0 == y_t)
         q[j-t-1] = MP_WORD_MAX;
      else
         q[j-t-1] = bigint_divop(x_j0, x_j1, y_t);

      while(bigint_divcore(q[j-t-1], y_t, y.word_at(t-1),
                           x_j0, x_j1, x.word_at(j-2)))
         q[j-t-1]--;

      x -= (q[j-t-1] * y) << (MP_WORD_BITS * (j-t-1));
      if(x.is_negative())
         {
         x += y << (MP_WORD_BITS * (j-t-1));
         q[j-t-1]--;
         }
      }
   x >>= shifts;
   }

}
