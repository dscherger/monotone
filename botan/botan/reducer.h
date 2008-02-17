/*************************************************
* Modular Reducer Header File                    *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#ifndef BOTAN_MODARITH_H__
#define BOTAN_MODARITH_H__

#include <botan/bigint.h>

namespace Botan {

/*************************************************
* Modular Reducer                                *
*************************************************/
class Modular_Reducer
   {
   public:
      BigInt multiply(const BigInt&, const BigInt&) const;
      BigInt square(const BigInt&) const;
      BigInt reduce(const BigInt&) const;

      bool initialized() const { return (mod_words != 0); }

      Modular_Reducer() { mod_words = 0; }
      Modular_Reducer(const BigInt&);
   private:
      BigInt modulus, modulus_2, mu;
      u32bit mod_words, mod2_words, mu_words;
   };

}

#endif
