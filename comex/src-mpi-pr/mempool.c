#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include <stdlib.h>

#include "comex_impl.h"
#include "mempool.h"

/* add a slab, grow the free list */
static void pool_grow(pool_t *pool)
{
    size_t i;
    char *new_slab = NULL;
    size_t offset = 0;
    free_t *f = NULL;
    char *object = NULL;

    pool->slab_count += 1;

    new_slab = malloc(pool->objects_per_slab*
            (pool->object_size+sizeof(free_t)));
    pool->slabs = realloc(pool->slabs, pool->slab_count*sizeof(char*));
    pool->slabs[pool->slab_count-1] = new_slab;

    for (i=0; i<pool->objects_per_slab; ++i) {
        f = (free_t*)&new_slab[offset];
        offset += sizeof(free_t);
        object = &new_slab[offset];
        offset += pool->object_size;
        f->mem = object;
        f->next = (free_t*)&new_slab[offset];
#ifdef _COMEX_MEM_POOL_CHECK_
        f->check = _COMEX_MEM_POOL_CHECK_;
#endif
    }
    /* last free item should actually point to NULL */
    f->next = NULL;
    /* first free item is new free list head */
    pool->free_list = (free_t*)&new_slab[0];
}


pool_t* pool_init(size_t object_size, size_t initial_count)
{
    pool_t *pool = malloc(sizeof(pool_t));
    pool->object_size = object_size;
    pool->objects_per_slab = initial_count;
    pool->slabs = NULL;
    pool->slab_count = 0;
    pool_grow(pool);
#ifdef _COMEX_MEM_POOL_CHECK_
    pool->counter = 0;
    pool->hwm = 0;
#endif
    return pool;
}


void pool_destroy(pool_t *pool)
{
    size_t i;
    for (i=0; i<pool->slab_count; ++i) {
        free(pool->slabs[i]);
    }
    free(pool);
#ifdef _COMEX_MEM_POOL_CHECK_
    printf("pool->object_size=%lu\tpool->counter=%lu\tpool->hwm=%lu\n",
            (long unsigned)pool->object_size,
            (long unsigned)pool->counter,
            (long unsigned)pool->hwm);

#endif
}


void* pool_acquire(pool_t *pool)
{
    char *object = NULL;

    if (NULL == pool->free_list) {
        pool_grow(pool);
    }
    object = pool->free_list->mem;
    pool->free_list = pool->free_list->next;
#ifdef _COMEX_MEM_POOL_CHECK_
    pool->counter += 1;
    if (pool->counter > pool->hwm) {
        pool->hwm = pool->counter;
    }
#endif

    return object;
}


void pool_release(pool_t *pool, void *item)
{
    free_t *f = (free_t*)(((char*)item)-sizeof(free_t));
#ifdef _COMEX_MEM_POOL_CHECK_
    COMEX_ASSERT(f->check == _COMEX_MEM_POOL_CHECK_);
    pool->counter -= 1;
#endif
    f->next = pool->free_list;
    pool->free_list = f->next;
}

