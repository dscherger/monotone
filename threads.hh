#ifndef __THREADS_HH__
#define __THREADS_HH__

// Copyright (C) 2008  Markus Schiltknecht  <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <boost/shared_ptr.hpp>

class thread_functor
{
public:
  virtual void operator()() = 0;
};

struct
thread_context
{
  thread_functor * func;
};

extern void create_thread_for(thread_functor * func);

template <typename TASK, typename P1, typename P2>
class worker_pool
{
public:
  worker_pool()
    { };

  void add_job(boost::shared_ptr<P1> p1, boost::shared_ptr<P2> p2)
    {
      I(p1);
      I(p2);
      thread_functor *func(new TASK(p1, p2));
      func->operator()();
      //create_thread_for(func);
      delete func;
    }

  void wait(void)
    {
      //
      // sleep(10);
    }
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __THREADS_HH__
