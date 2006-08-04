/* 

> enter fake_pthread.c

The stench of incredible wrongness coming from this room is nearly
overpowering.  There is a sign on the wall

> look at sign

The sign says, "But it gives us a >10% performance boost for programs that
don't really nead pthreads but are forced to link to it by a shared library
dependency."

> retch on floor

You add to the decor.

*/

#include <stdio.h>
#include <pthread.h>

int  
pthread_mutex_init(pthread_mutex_t  *mutex,  const  pthread_mutexattr_t *mutexattr)
{
  return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
  return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) 
{
  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
  return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
  return 0;
}

int pthread_cond_init(pthread_cond_t *cond, 
		      const pthread_condattr_t *cond_attr)
{
  return 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
  return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
  return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
  fprintf(stderr,"invalid call to %s\n", __PRETTY_FUNCTION__);
  abort();
}


int   pthread_cond_timedwait(pthread_cond_t   *cond,    pthread_mutex_t
       *mutex, const struct timespec *abstime)
{
  fprintf(stderr,"invalid call to %s\n", __PRETTY_FUNCTION__);
  abort();
}


int pthread_cond_destroy(pthread_cond_t *cond)
{
  return 0;
}

int pthread_join(pthread_t th, void **thread_return)
{
  fprintf(stderr,"invalid call to %s\n", __PRETTY_FUNCTION__);
  abort();
}

int  pthread_create(pthread_t  *  thread, const pthread_attr_t * attr, void *
       (*start_routine)(void *), void * arg) 
{
  fprintf(stderr,"invalid call to %s\n", __PRETTY_FUNCTION__);
  abort();
}

int  pthread_once(pthread_once_t  *once_control,  void  (*init_routine)
		  (void))
{
  if (PTHREAD_ONCE_INIT == *once_control) {
    *once_control += 1;
    init_routine();
  }
}

struct keys {
  void *value;
  void (*destr_function) (void *);
};

#define PTHREAD_KEYS_MAX 1024
struct keys tsd_keys[PTHREAD_KEYS_MAX];
unsigned int tsd_keys_allocated;

int pthread_key_create(pthread_key_t *key, void (*destr_function) (void *))
{
  if (tsd_keys_allocated == PTHREAD_KEYS_MAX) {
    return -1;
  }
  *key = tsd_keys_allocated;
  ++tsd_keys_allocated;
  tsd_keys[*key].destr_function = destr_function;
  // oughta setup an atexit
}

int pthread_key_delete(pthread_key_t key)
{
}

int pthread_setspecific(pthread_key_t key, const void *pointer)
{
  if (key < 0 || key >= tsd_keys_allocated) {
    return -1;
  }
  tsd_keys[key].value = pointer;
}

void * pthread_getspecific(pthread_key_t key)
{
  if (key < 0 || key >= tsd_keys_allocated) {
    abort();
  }
  return tsd_keys[key].value;
}

