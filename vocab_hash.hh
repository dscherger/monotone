// Copyright (C) 2007 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __VOCAB_HASH_HH__
#define __VOCAB_HASH_HH__

#include "vocab.hh"
#include "hash_map.hh"

#define ENCODING(enc)                                  \
  namespace hashmap {                                  \
    template<typename INNER>                           \
      struct hash<enc<INNER> >                         \
      {                                                \
        size_t operator()(enc<INNER> const & t) const  \
        {                                              \
          return hash<std::string>()(t());             \
        }                                              \
      };                                               \
  }
#define ENCODING_NOVERIFY(enc) ENCODING(enc)

#define DECORATE(dec)                                  \
  namespace hashmap {                                  \
    template<typename INNER>                           \
      struct hash<dec<INNER> >                         \
      {                                                \
        size_t operator()(dec<INNER> const & t) const  \
        {                                              \
          return hash<INNER>()(t.inner());             \
        }                                              \
      };                                               \
  }

#define ATOMIC(ty)                                     \
  namespace hashmap {                                  \
    template<>                                         \
      struct hash<ty>                                  \
      {                                                \
        size_t operator()(ty const & t) const          \
        {                                              \
          return hash<std::string>()(t());             \
        }                                              \
      };                                               \
  }

#define ATOMIC_BINARY(ty)                              \
  namespace hashmap {                                  \
    template<>                                         \
      struct hash<ty>                                  \
      {                                                \
        size_t operator()(ty const & t) const          \
        {                                              \
          return hash<std::string>()(t());             \
        }                                              \
      };                                               \
  }
#define ATOMIC_HOOKED(ty,hook) ATOMIC(ty)
#define ATOMIC_NOVERIFY(ty) ATOMIC(ty)

#include "vocab_terms.hh"

#undef ENCODING
#undef ENCODING_NOVERIFY
#undef DECORATE
#undef ATOMIC
#undef ATOMIC_HOOKED
#undef ATOMIC_NOVERIFY

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
