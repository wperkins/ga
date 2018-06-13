#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include "thread-safe.h"

#if defined(PTHREADS) && defined(THREAD_SAFE)
#include <pthread.h>
#include <stdio.h>

// FIXME - initialize within function called by GA initializer, allowing it to be re-initialized as neede
pthread_rwlock_t ga_readwrite_lock = PTHREAD_RWLOCK_INITIALIZER;
//pthread_rwlock_init(&ga_readwrite_lock, NULL);

// FIXME - Every read/write lock must eventually be destroyed with pthread_rwlock_destroy(). Always use pthread_rwlock_destroy() before freeing or reusing read/write lock storage.

void GA_Internal_Threadsafe_Lock()
{
    GA_Internal_Threadsafe_Write_Lock();
}

void GA_Internal_Threadsafe_Read_Lock()
{
    pthread_rwlock_rdlock(&ga_readwrite_lock);
}

void GA_Internal_Threadsafe_Write_Lock()
{
    pthread_rwlock_wrlock(&ga_readwrite_lock);
}

void GA_Internal_Threadsafe_Unlock()
{
    pthread_rwlock_unlock(&ga_readwrite_lock);
}

#endif
