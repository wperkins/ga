#include "thread-safe.h"

#if defined(PTHREADS) && defined(THREAD_SAFE)
#include <pthread.h>
#include <stdio.h>

pthread_mutex_t ga_threadsafe_lock;
__thread int locked = 0;

void GA_Internal_Threadsafe_Lock()
{
    if(!locked)
    {
        pthread_mutex_lock(&ga_threadsafe_lock);
        locked++;
    }
    else
        locked++;
}

void GA_Internal_Threadsafe_Unlock()
{
     locked--;
     if(locked == 0)
        pthread_mutex_unlock(&ga_threadsafe_lock);
}

#endif
