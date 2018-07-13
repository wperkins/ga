/* This file allocates memory on GPU device/s */
#if HAVE_STDIO_H
#   include <stdio.h>
#endif

extern "C" void gpu_mem_alloc(int *d_a, int d_size) {

  cudaMalloc((void **)&d_a, d_size*sizeof(int));
  // return 1;  // replace with CUDA_ERROR code
}
