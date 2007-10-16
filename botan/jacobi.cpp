/*************************************************
* Jacobi Function Source File                    *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#include <botan/numthry.h>

namespace Botan {

/*************************************************
* Calculate the Jacobi symbol                    *
*************************************************/
s32bit jacobi(const BigInt& a, const BigInt& n)
   {
   if(a.is_negative())
      throw Invalid_Argument("jacobi: first argument must be non-negative");
   if(n.is_even() || n < 2)
      throw Invalid_Argument("jacobi: second argument must be odd and > 1");

   BigInt x = a, y = n;
   s32bit J = 1;

   while(y > 1)
      {
      x %= y;
      if(x > y / 2)
         {
         x = y - x;
         if(y % 4 == 3)
            J = -J;
         }
      if(x.is_zero())
         return 0;

      u32bit shifts = low_zero_bits(x);
      x >>= shifts;
      if(shifts % 2)
         {
         word y_mod_8 = y % 8;
         if(y_mod_8 == 3 || y_mod_8 == 5)
            J = -J;
         }

      if(x % 4 == 3 && y % 4 == 3)
         J = -J;
      std::swap(x, y);
      }
   return J;
   }

}
