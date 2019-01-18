/* comex util file */
#ifndef _COMEX_UTIL_H
#define _COMEX_UTIL_H

#ifdef USE_DEVICE_MEM
typedef struct {
    void * ptr;
    int is_malloced;
} local_ptr_t;

typedef struct {
    int on_device;
    int count; /* Device Count */
} device_info_t;
#endif

#endif /* _COMEX_UTIL_H */
