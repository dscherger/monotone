// Copyright (C) 2008  Markus Schiltknecht  <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


// This file provides very basic threading support, just enough to allow
// multiple worker threads to process small, enclosed jobs concurrently.
// These jobs need to be separated very well. They get an input and
// should provide some output, but may not interfere with the rest of
// monotone in any other way, because we don't want to buy into complex
// locking problems.
//
// When writing jobs for a threaded task, please be aware, that these
// tasks MUST NOT throw exceptions. Additionally, you have to take care
// and make sure all objects used by that thread are valid until the job
// is done. And of course you need to avoid concurrent access to objects.

#include "base.hh"
#include <pthread.h>

#include "sanity.hh"
#include "threads.hh"

struct
thread_context
{
  threaded_task * task;
};

void *threaded_call(void *c)
{
  thread_context * ctx = (thread_context*) c;

  (*ctx->task)();

  pthread_exit(NULL);
}

void
create_thread_for(threaded_task * task)
{
  int rc;
  void *status;
  pthread_t thread;

  thread_context * ctx = new thread_context();
  ctx->task = task;

  rc = pthread_create(&thread, NULL, threaded_call, (void*) &ctx);
  I(!rc);

  rc = pthread_join(thread, &status);
  I(!rc);

  delete ctx;
  delete task;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
