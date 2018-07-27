#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include "thread-safe.h"

#if defined(PTHREADS) && defined(THREAD_SAFE)
#include <pthread.h>
#include <stdio.h>

pthread_mutex_t ga_threadsafe_lock;

void GA_Internal_Threadsafe_Lock(int lockFlag)
{
   //"big fat lock"
   if (lockFlag == 1)
      pthread_mutex_lock(&ga_threadsafe_lock);
   
   //Other locks go here

}

void GA_Internal_Threadsafe_Unlock(int lockFlag)
{
   //"big fat unlock"
   if (lockFlag == 1)   
      pthread_mutex_unlock(&ga_threadsafe_lock);

   //Other locks go here

}

#endif
