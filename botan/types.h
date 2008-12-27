/*************************************************
* Low Level Types Header File                    *
* (C) 1999-2007 Jack Lloyd                       *
*************************************************/

#ifndef BOTAN_TYPES_H__
#define BOTAN_TYPES_H__

#include <botan/build.h>

namespace Botan {

typedef unsigned char byte;
typedef unsigned short u16bit;
typedef unsigned int u32bit;

typedef signed int s32bit;

#if defined(_MSC_VER) || defined(__BORLANDC__)
   typedef unsigned __int64 u64bit;
#elif defined(__KCC)
   typedef unsigned __long_long u64bit;
#elif defined(__GNUG__)
   __extension__ typedef unsigned long long u64bit;
#else
   typedef unsigned long long u64bit;
#endif

}

namespace Botan_types {

using Botan::byte;
using Botan::u32bit;

}

#endif
