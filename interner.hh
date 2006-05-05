#ifndef __INTERNER_HH__
#define __INTERNER_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>

#include "hash_map.hh"
#include "sanity.hh"

template <typename T>
struct 
interner 
{
  typedef typename hashmap::hash_map<std::string, T> hmap;

  hmap fwd;
  std::vector<std::string> rev;
  interner() {}
  interner(std::string const & init_str, T init_value)
  {
    I(intern(init_str) == init_value);
  }
  std::string lookup (T in) const
  {
    std::vector<std::string>::size_type k = static_cast<std::vector<std::string>::size_type>(in);
    I(k < rev.size());
    return rev[k];
  }
  T intern(std::string const & s)
  {
    bool is_new;
    return intern(s, is_new);
  }
  T intern(std::string const & s, bool & is_new) 
  {
    std::pair<typename hmap::iterator, bool> res;
    T t = rev.size();
    // if fwd already contains an entry with key s, this just finds
    // that and returns it
    res = fwd.insert(make_pair(s, t));
    is_new = res.second;
    if (is_new)
      rev.push_back(s);
    return res.first->second;
  }
};

#endif // __INTERNER_HH__
