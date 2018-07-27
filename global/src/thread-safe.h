#ifndef _GA_THREAD_SAFE_H_
#define _GA_THREAD_SAFE_H_

#define THREAD_SAFE 1

#define THREAD_LOCK_DEFAULT 1

#if defined(PTHREADS) && defined(THREAD_SAFE)

#ifdef __cplusplus
extern "C" {
#endif

extern void GA_Internal_Threadsafe_Lock(int lockFlag);
extern void GA_Internal_Threadsafe_Unlock(int lockFlag);

#ifdef __cplusplus
}
#endif

#else

#define GA_Internal_Threadsafe_Lock(int lockFlag)
#define GA_Internal_Threadsafe_Unlock(int lockFlag)

#endif

#endif /* _GA_THREAD_SAFE_H_ */
