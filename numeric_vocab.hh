#ifndef __NUMERIC_VOCAB__
#define __NUMERIC_VOCAB__

// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <cstddef>
#include <climits>              // Some architectures need this for CHAR_BIT
                                // The lack of this was reported as bug #19984
#include <limits>
#include <boost/static_assert.hpp>

typedef TYPE_U8  u8;
typedef TYPE_U16 u16;
typedef TYPE_U32 u32;
typedef TYPE_U64 u64;

typedef TYPE_S8  s8;
typedef TYPE_S16 s16;
typedef TYPE_S32 s32;
typedef TYPE_S64 s64;

// This is similar to static_cast<T>(v).  The difference is that when T is
// unsigned, this cast does not sign-extend:
//   static_cast<u32>((signed char) -1) = 4294967295
//   widen<u32,signed char>(-1) == 255
template <typename T, typename V>
inline T
widen(V const & v)
{
  BOOST_STATIC_ASSERT(sizeof(T) >= sizeof(V));
  if (std::numeric_limits<T>::is_signed)
    return static_cast<T>(v);
  else if (!std::numeric_limits<V>::is_signed)
    return static_cast<T>(v);
  else
    {
      const size_t char_bit = std::numeric_limits<unsigned char>::digits;
      T mask = std::numeric_limits<T>::max();
      size_t shift = (sizeof(T) - sizeof(V)) * char_bit;
      mask >>= shift;
      return static_cast<T>(v) & mask;
    }
}

// Writing a 64-bit constant is tricky.  We cannot use the macros that
// <stdint.h> provides in C99 (INT64_C, or even INT64_MAX) because those
// macros are not in C++'s version of <stdint.h>.  std::numeric_limits<s64>
// cannot be used directly, so we have to resort to #ifdef chains on the old
// skool C limits macros.  BOOST_STATIC_ASSERT is defined in a way that
// doesn't let us use std::numeric_limits<s64>::max(), so we rely on
// one of the users (the unit tests for dates.cc) to check it at runtime.

#if defined LONG_MAX && LONG_MAX > UINT_MAX
  #define PROBABLE_S64_MAX LONG_MAX
  #define s64_C(x) x##L
#elif defined LLONG_MAX && LLONG_MAX > UINT_MAX
  #define PROBABLE_S64_MAX LLONG_MAX
  #define s64_C(x) x##LL
#elif defined LONG_LONG_MAX && LONG_LONG_MAX > UINT_MAX
  #define PROBABLE_S64_MAX LONG_LONG_MAX
  #define s64_C(x) x##LL
#else
  #error "How do I write a constant of type s64?"
#endif

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
