#ifndef _COMEX_MEM_POOL_H_
#define _COMEX_MEM_POOL_H_

#include <stddef.h>

/*#define _COMEX_MEM_POOL_CHECK_ 123456789U*/

typedef struct _free_t {
    struct _free_t *next;   /**< next available memory in list */
    char *mem;              /**< starting address of memory */
#ifdef _COMEX_MEM_POOL_CHECK_
    size_t check;
#endif
} free_t;

typedef struct _pool_t {
    struct _free_t *free_list;
    size_t object_size;
    size_t objects_per_slab;
    char **slabs;
    size_t slab_count;
#ifdef _COMEX_MEM_POOL_CHECK_
    size_t counter;
    size_t hwm;
#endif
} pool_t;

pool_t* pool_init(size_t object_size, size_t initial_count);
void pool_destroy(pool_t *pool);
void* pool_acquire(pool_t *pool);
void pool_release(pool_t *pool, void *item);

#endif /* _COMEX_MEM_POOL_H_ */
