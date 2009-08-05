// Copyright (C) 2007 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __VOCAB_CAST_HH
#define __VOCAB_CAST_HH

#include <algorithm>

// You probably won't use this yourself, but it's needed by...
template<typename To, typename From>
To typecast_vocab(From const & from)
{ return To(from(), from.made_from); }

// There are a few places where we want to typecast an entire
// container full of vocab types.
template<typename From, typename To>
void typecast_vocab_container(From const & from, To & to)
{
  std::transform(from.begin(), from.end(), std::inserter(to, to.end()),
                 &typecast_vocab<typename To::value_type,
                 typename From::value_type>);
}

// You won't use this directly either.
template<typename To, typename From>
To add_decoration(From const & from)
{
  return To(from);
}

// There are also some places that want to decorate a container full
// of vocab types.
template<typename From, typename To>
void add_decoration_to_container(From const & from, To & to)
{
  std::transform(from.begin(), from.end(), std::inserter(to, to.end()),
                 &add_decoration<typename To::value_type,
                 typename From::value_type>);
}

template<typename From, typename To>
void vocabify_container(From const & from, To & to)
{
  add_decoration_to_container(from, to);
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
