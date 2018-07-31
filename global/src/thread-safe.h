#ifndef _GA_THREAD_SAFE_H_
#define _GA_THREAD_SAFE_H_

#define THREAD_SAFE 1

#include <sys/types.h>
#include <stdint.h>
#include <pthread.h>

enum GA_LOCK_TYPES {NO_LOCK = 0, FAT_LOCK, TOT_LOCK};

/* Multithreaded state */

extern uint64_t max_threads;
extern int32_t *tids; /* map of system ids to numerical ids */

#if defined (THREAD_SAFE)
   #define GET_SWID() (pthread_self())
#else
   #define GET_SWID() (0)
#endif

extern int64_t get_tid();
extern int set_thread_config();


#if defined(PTHREADS) && defined(THREAD_SAFE)

#ifdef __cplusplus
extern "C" {
#endif

#define THREAD_LOCK_DEFAULT FAT_LOCK

extern void GA_Internal_Threadsafe_Lock(enum GA_LOCK_TYPES l);
extern void GA_Internal_Threadsafe_Unlock(enum GA_LOCK_TYPES l);

extern void GA_STAT_Internal_Threadsafe_Lock(enum GA_LOCK_TYPES l);
extern void GA_STAT_Internal_Threadsafe_Unlock(enum GA_LOCK_TYPES l);

extern void GA_PROF_Internal_Threadsafe_Lock(enum GA_LOCK_TYPES l);
extern void GA_PROF_Internal_Threadsafe_Unlock(enum GA_LOCK_TYPES l);

#ifdef __GCC__

#define atomic_load(a)     (__atomic_load_n(&a, __ATOMIC_SEQ_CST))
#define atomic_store(a, v) (__atomic_store_n(&a, v, __ATOMIC_SEQ_CST))
#define atomic_add(a, v)   (__atomic_add_fetch(&a, v, __ATOMIC_SEQ_CST))
#define atomic_inc(a)      (__atomic_add_fetch(&a, 1, __ATOMIC_SEQ_CST))
#define atomic_pinc(a)      (__atomic_fetch_add(&a, 1, __ATOMIC_SEQ_CST))

#elif defined __clang__

#define atomic_load(a)     (__atomic_load_N(&a, __ATOMIC_SEQ_CST))
#define atomic_store(a, v) (__atomic_store_N(&a, v, __ATOMIC_SEQ_CST))
#define atomic_add(a, v)   (__atomic_add_fetch(&a, v, __ATOMIC_SEQ_CST))
#define atomic_inc(a)      (__atomic_add_fetch(&a, 1, __ATOMIC_SEQ_CST))
#define atomic_pinc(a)      (__atomic_fetch_add(&a, 1, __ATOMIC_SEQ_CST))

#else

#define atomic_load(a)     (a)
#define atomic_store(a, v) (a = v)
#define atomic_add(a, v)   (a += v)
#define atomic_inc(a)      (++a)
#define atomic_pinc(a)     (a++)

#endif

#ifdef __cplusplus
}
#endif

#else

#define THREAD_LOCK_DEFAULT NO_LOCK 

#define GA_Internal_Threadsafe_Lock(l)
#define GA_Internal_Threadsafe_Unlock(l)

#define GA_STAT_Internal_Threadsafe_Lock(l)
#define GA_STAT_Internal_Threadsafe_Unlock(l)

#define GA_STAT_Internal_Threadsafe_Lock(l)
#define GA_STAT_Internal_Threadsafe_Unlock(l)

#define atomic_load(a)     (a)
#define atomic_store(a, v) (a = v)
#define atomic_add(a, v)   (a += v)
#define atomic_inc(a)      (a++)

#endif

#endif /* _GA_THREAD_SAFE_H_ */
