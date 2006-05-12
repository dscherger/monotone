/*************************************************
* Mutex Header File                              *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#ifndef BOTAN_MUTEX_H__
#define BOTAN_MUTEX_H__

#include <botan/exceptn.h>

namespace Botan {

/*************************************************
* Mutex Base Class                               *
*************************************************/
class Mutex
   {
   public:
      virtual void lock() = 0;
      virtual void unlock() = 0;
      virtual ~Mutex() {}
   };

/*************************************************
* Mutex Holding Class                            *
*************************************************/
class Mutex_Holder
   {
   public:
      Mutex_Holder(Mutex*);
      ~Mutex_Holder();
   private:
      Mutex* mux;
   };

/*************************************************
* Mutex Factory                                  *
*************************************************/
class Mutex_Factory
   {
   public:
      virtual Mutex* make();
      virtual ~Mutex_Factory() {}
   };

}

#endif
