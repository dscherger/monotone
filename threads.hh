#ifndef __THREADS_HH__
#define __THREADS_HH__

// Copyright (C) 2008  Markus Wanner  <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <stack>
#include <boost/shared_ptr.hpp>

class threaded_task
{
public:
  virtual void operator()() = 0;
};

class worker_pool
{
  int max_threads, num_threads;
  std::stack<threaded_task*> tstack;
public:
  worker_pool();
  void add_job(threaded_task* t);
  void wait();
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __THREADS_HH__
