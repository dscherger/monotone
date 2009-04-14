// Copyright (C) 2009 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __INTRUSIVE_PTR_HH__
#define __INTRUSIVE_PTR_HH__

#include <boost/intrusive_ptr.hpp>
#include "sanity.hh"

// By convention, intrusively reference-counted objects should have a
// private member of type intrusive_refcnt_t, named "refcnt", and be
// friends with these functions.  Since monotone is single-threaded,
// we do not worry about locking.

typedef long intrusive_refcnt_t;

template <class T> inline void intrusive_ptr_add_ref(T * ptr)
{
  ptr->refcnt++;
  I(ptr->refcnt > 0);
}

template <class T> inline void intrusive_ptr_release(T * ptr)
{
  ptr->refcnt--;
  I(ptr->refcnt >= 0);
  if (ptr->refcnt == 0)
    delete ptr;
}

// This base class takes care of the above conventions for you.
// Note that intrusive_ptr_add_ref/release still need to be template
// functions, as one must apply delete to a pointer with the true type
// of the object.

class intrusively_refcounted
{
  intrusive_refcnt_t refcnt;
  // sadly, no way to say "any subclass of this"
  template <class T> friend void intrusive_ptr_add_ref(T *);
  template <class T> friend void intrusive_ptr_release(T *);
public:
  intrusively_refcounted() : refcnt(0) {}
};

#endif // intrusive_ptr.hh
