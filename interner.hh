#ifndef __INTERNER_HH__
#define __INTERNER_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <string>
#include <functional>

#include "sanity.hh"

template <typename T>
struct 
interner 
{
  std::map<std::string, T> fwd;
  std::map<T, std::string> rev;    
  T max;
  interner() : max(0) {}
  std::string lookup (T in) const
  {
    typename std::map<T, std::string>::const_iterator i = rev.find(in);
    I(i != rev.end());
    return i->second;
  }
  T intern(std::string const & s) 
  {
    typename std::map<std::string, T>::const_iterator i = fwd.find(s);
    if (i == fwd.end())
      {
	++max;
	I(rev.find(max) == rev.end());
	fwd.insert(make_pair(s, max));
	rev.insert(make_pair(max, s));
	return max;
      }
    else
      return i->second;
  }
};

template <typename T>
struct intern : public std::unary_function<std::string, T>
{
  interner<T> & _interner;
  intern(interner<T> & i) : _interner(i) {}
  T operator()(std::string const & s) { return _interner.intern(s); }
};

#endif // __INTERNER_HH__
