#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include "thread-safe.h"

#if defined(PTHREADS) && defined(THREAD_SAFE)
#include <pthread.h>
#include <stdio.h>

pthread_mutex_t ga_threadsafe_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ga_stat_threadsafe_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ga_prof_threadsafe_lock = PTHREAD_MUTEX_INITIALIZER;

uint64_t max_threads;
int32_t *tids; /* map of system ids to numerical ids */


int64_t get_tid(){
   if(tids == NULL) return -1;
   int i;
   int64_t id = GET_SWID() % max_threads;
   for(i = id; tids[i] != GET_SWID() && i <  max_threads; i++); 
   if(i < max_threads) return i;
   return -1;
}

#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/resource.h>

int set_thread_config(){
   int i;
   struct rlimit rlim;
   if(tids != NULL) return -2;
   //int64_t mthread = get_nprocs();
   getrlimit(RLIMIT_NPROC, &rlim);
   int64_t mthread = rlim.rlim_max;
   atomic_store(max_threads, mthread);
   tids = (int64_t *)malloc(max_threads * sizeof(*tids));
   if(tids == NULL) return -1;
   GA_Internal_Threadsafe_Lock(FAT_LOCK);
   for(i = 0; i < mthread; ++i) tids[i] = -1;
   int64_t ty = GET_SWID() % mthread;
   for(i = ty; tids[i] != -1 && i <  mthread; i++); 
   if(i < mthread) {
       tids[i] = GET_SWID();
       GA_Internal_Threadsafe_Unlock(FAT_LOCK);
       return 0;
   }
   GA_Internal_Threadsafe_Unlock(FAT_LOCK);
   return -3;
}

void GA_Internal_Threadsafe_Lock(enum GA_LOCK_TYPES lockFlag)
{
   if(lockFlag == NO_LOCK){
      // No Lock
   }
   //"big fat lock"
   else if (lockFlag == FAT_LOCK)
      pthread_mutex_lock(&ga_threadsafe_lock);
   
   //Other locks go here

}

void GA_Internal_Threadsafe_Unlock(enum GA_LOCK_TYPES lockFlag)
{
   if(lockFlag == NO_LOCK){
      // No Lock
   }
   //"big fat lock"
   else if (lockFlag == FAT_LOCK)   
      pthread_mutex_unlock(&ga_threadsafe_lock);
   //Other locks go here
}

void GA_STAT_Internal_Threadsafe_Lock(enum GA_LOCK_TYPES lockFlag)
{
   if(lockFlag == NO_LOCK){
      // No Lock
   }
   //"big fat lock"
   else if (lockFlag == FAT_LOCK)
      pthread_mutex_lock(&ga_stat_threadsafe_lock);
   //Other locks go here
}

void GA_STAT_Internal_Threadsafe_Unlock(enum GA_LOCK_TYPES lockFlag)
{
   if(lockFlag == NO_LOCK){
      // No Lock
   }
   else if (lockFlag == FAT_LOCK)
      pthread_mutex_unlock(&ga_stat_threadsafe_lock);
   //Other locks go here
}

void GA_PROF_Internal_Threadsafe_Lock(enum GA_LOCK_TYPES lockFlag)
{
   if(lockFlag == NO_LOCK){
      // No Lock
   }
   //"big fat lock"
   else if (lockFlag == FAT_LOCK)
      pthread_mutex_lock(&ga_prof_threadsafe_lock);
   //Other locks go here
}

void GA_PROF_Internal_Threadsafe_Unlock(enum GA_LOCK_TYPES lockFlag)
{
   if(lockFlag == NO_LOCK){
      // No Lock
   }
   else if (lockFlag == FAT_LOCK)
      pthread_mutex_unlock(&ga_prof_threadsafe_lock);

   //Other locks go here

}


#endif
