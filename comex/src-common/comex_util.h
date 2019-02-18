/* comex util file */
#ifndef _COMEX_UTIL_H
#define _COMEX_UTIL_H

#ifdef USE_DEVICE_MEM
typedef struct {
    void * ptr;
    int is_malloced;
} local_ptr_t;

typedef struct {
    // TODO: On Devices
    int on_device;
    int is_ga; /* is data a ga */
    int count; /* Device Count. TODO: Convert this to size_t? */
} device_info_t;
#endif

#endif /* _COMEX_UTIL_H */
