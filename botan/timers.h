/*************************************************
* Timestamp Functions Header File                *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_TIMERS_H__
#define BOTAN_TIMERS_H__

#include <botan/types.h>

namespace Botan {

/*************************************************
* Timer Interface                                *
*************************************************/
class Timer
   {
   public:
      virtual u64bit clock() const = 0;
      virtual ~Timer() {}
   };

}

#endif
