#if !defined (GA_UTIL_H)
#define GA_UTIL_H

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <mpi.h>

#ifdef USE_DEVICE_MEM
#include "armci.h"

// #ifdef CUDA_DEVICE
#include <cuda.h>
#include <cuda_runtime.h>
// #endif

extern int _my_node_id;
extern int _my_local_rank;
#endif

/* basic linked list type for int value*/
typedef struct node {
  int val;
  struct node* next;
} llist_t;

extern bool llist_empty(llist_t * head);
extern int llist_size(llist_t * head);
extern void llist_insert_front(llist_t** head, int i);
extern void llist_insert_back(llist_t** head, int i);
extern void llist_insert(llist_t** head, int i);
extern bool llist_remove_val(llist_t** head, int i);
extern bool llist_value_exists(llist_t* head, int i);
extern void llist_print(llist_t* node);

#ifdef USE_DEVICE_MEM
// #ifdef CUDA_DEVICE

extern int getDeviceCount();
extern void gpu_device_bind();

// #endif
#endif

#endif /* GA_UTIL_H */
