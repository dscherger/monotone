// Copyright (C) 2005 Patrick Mauritz <oxygene@studentenbude.ath.cx>
//               2014 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __HASHMAP_HH
#define __HASHMAP_HH

#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace hashmap {

  template<typename T>
  class equal_to : public std::equal_to<T>
  {
    // bool operator()(T const & b, T const & b) const;
  };

  template<typename T>
  class less : public std::less<T>
  {
    // bool operator()(T const & b, T const & b) const;
  };

  template<typename T>
  struct hash
  {
    // size_t operator()(T const & t) const;
  };

  template<>
  struct hash<unsigned int>
  {
    size_t operator()(unsigned int t) const
    {
      return t;
    }
  };

  template<>
  struct hash<unsigned long>
  {
    size_t operator()(unsigned long t) const
    {
      return t;
    }
  };
}

namespace hashmap {
  template<>
  struct hash<std::string>
  {
    size_t operator()(std::string const & s) const
    {
      return std::hash<std::string>()(s);
    }
  };

  template<typename _Key, typename _Value>
  class hash_map : public std::unordered_map<_Key,
                                             _Value,
                                             hash<_Key>,
                                             equal_to<_Key> >
  {};

template<typename _Key>
class hash_set : public std::unordered_set<_Key,
                                           hash<_Key>,
                                           equal_to<_Key> >
{};

  template<typename _Key, typename _Value>
  class hash_multimap : public std::unordered_multimap<_Key,
                                                       _Value,
                                                       hash<_Key>,
                                                       equal_to<_Key> >
  {};
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
