/*************************************************
* Blinder Header File                            *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#ifndef BOTAN_BLINDER_H__
#define BOTAN_BLINDER_H__

#include <botan/bigint.h>

namespace Botan {

/*************************************************
* Blinding Function Object                       *
*************************************************/
class Blinder
   {
   public:
      BigInt blind(const BigInt&) const;
      BigInt unblind(const BigInt&) const;

      void initialize(const BigInt&, const BigInt&, const BigInt&);
      Blinder& operator=(const Blinder&);

      Blinder();
      Blinder(const Blinder&);
      ~Blinder();
   private:
      mutable BigInt e, d;
      BigInt n;
      class ModularReducer* reducer;
   };

}

#endif
