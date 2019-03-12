#if HAVE_CONFIG_H
#   include "config.h"
#endif

#if HAVE_STDIO_H
#   include <stdio.h>
#endif
#if HAVE_STDLIB_H
#   include <stdlib.h>
#endif
#if HAVE_MATH_H
#   include <math.h>
#endif

#include "macdecls.h"
#include "ga.h"
#include "mp3.h"

/* utilities for GA test programs */
#include "testutil.h"

#define BLOCKSIZE 4          /* first dimension  */

#define MAX_FACTOR 512

/**
 * Factor p processors into 2D processor grid of dimensions px, py
 */
void grid_factor(int p, int *idx, int *idy) {
  int i, j;
  int ip, ifac, pmax, prime[MAX_FACTOR];
  int fac[MAX_FACTOR];
  int ix, iy, ichk;

  /**
   *   factor p completely
   *   first, find all prime numbers, besides 1, less than or equal to 
   *   the square root of p
   */
  ip = (int)(sqrt((double)p))+1;
  pmax = 0;
  for (i=2; i<=ip; i++) {
    ichk = 1;
    for (j=0; j<pmax; j++) {
      if (i%prime[j] == 0) {
        ichk = 0;
        break;
      }
    }
    if (ichk) {
      pmax = pmax + 1;
      if (pmax > MAX_FACTOR) printf("Overflow in grid_factor\n");
      prime[pmax-1] = i;
    }
  }
  /**
   *   find all prime factors of p
   */
  ip = p;
  ifac = 0;
  for (i=0; i<pmax; i++) {
    while(ip%prime[i] == 0) {
      ifac = ifac + 1;
      fac[ifac-1] = prime[i];
      ip = ip/prime[i];
    }
  }
  /**
   *  p is prime
   */
  if (ifac==0) {
    ifac++;
    fac[0] = p;
  }
  /**
   *    find two factors of p of approximately the
   *    same size
   */
  *idx = 1;
  *idy = 1;
  for (i = ifac-1; i >= 0; i--) {
    ix = *idx;
    iy = *idy;
    if (ix <= iy) {
      *idx = fac[i]*(*idx);
    } else {
      *idy = fac[i]*(*idy);
    }
  }
}

void do_work() {
  int me=GA_Nodeid();
  int nproc = GA_Nnodes();
  int g_a;
  int ndim = 2;
  int dims[2];
  int lo[2];
  int hi[2];
  int block_idx[2];
  int proc_grid[2];
  int i,j,l,k,m,n,ld,ldl;
  int nghbr;
  int *mapc;
  double *ptr, *lptr;
  double rone;

  /* create processor grid */
  grid_factor(nproc, &proc_grid[0], &proc_grid[1]);
  if(me==0) printf("Processor grid is %d X %d\n\n",proc_grid[0],proc_grid[1]);
  for (i=0; i<ndim; i++) dims[i] = BLOCKSIZE*proc_grid[i];
  if (me==0) printf("\nGlobal array dimensions are %d X %d\n",dims[0],dims[1]);
  block_idx[1] = me%proc_grid[1];
  block_idx[0] = (me-block_idx[1])/proc_grid[1];
  ld = 0;
  /* find block that is owned by this process */
  for (i=0; i<ndim; i++) {
    lo[i] = block_idx[i]*BLOCKSIZE;
    hi[i] = (block_idx[i]+1)*BLOCKSIZE-1;
    ld += proc_grid[i];
  }
  /* set up map array for the global array */
  mapc = (int*)malloc(ld*sizeof(int));
  n = 0;
  for (i=0; i<ndim; i++) {
    for (j=0; j<proc_grid[i]; j++) {
      mapc[n] = j*BLOCKSIZE;
      n++;
    }
  }
  g_a = NGA_Create_handle();
  NGA_Set_data(g_a,ndim,dims,C_DBL);
  NGA_Set_array_name(g_a,"array A");
  NGA_Set_irreg_distr(g_a,mapc,proc_grid);
  free(mapc);
  /* allocate local buffer that holds global array data */
  ptr = (double*)malloc(BLOCKSIZE*BLOCKSIZE*sizeof(double));
  NGA_Attach(g_a,lo,hi,ptr);
  /* zero data in global array */
  GA_Zero(g_a);

  /* allocate a second local array and fill it with values for the
   * portion of the the global array owned by the next highest processor */
  nghbr = me + 1;
  if (nghbr >= nproc) nghbr = 0;
  NGA_Distribution(g_a,nghbr,lo,hi);
  lptr = (double*)malloc(BLOCKSIZE*BLOCKSIZE*sizeof(double));
  l = 0;
  ld = BLOCKSIZE*proc_grid[1];
  for (i = lo[0]; i<=hi[0]; i++) {
    for (j = lo[1]; j<=hi[1]; j++) {
      lptr[l] = (double)(j + i*ld);
      l++;
    }
  }
  /* Use put operation to transfer values from local buffer to global array */
  ldl = BLOCKSIZE;
  NGA_Put(g_a,lo,hi,lptr,&ldl);
  GA_Sync();
  /* Check to see if values are correct */
  NGA_Distribution(g_a,me,lo,hi);
  NGA_Access(g_a,lo,hi,&ptr,&ldl);
  l=0;
  for (i = lo[0]; i<=hi[0]; i++) {
    for (j = lo[1]; j<=hi[1]; j++) {
      if (ptr[l] != (double)(j + i*ld)) {
        GA_Error("Incorrect value from put",lptr[l]);
      }
      l++;
    }
  }
  NGA_Release(g_a,lo,hi);
  if (me == 0) {
    printf("\nPut test using attached memory OK\n");
  }
  /* Test get operation by grabbing values from a neighboring processor */
  nghbr = me-1;
  if (nghbr<0) nghbr = nproc-1;
  NGA_Distribution(g_a,nghbr,lo,hi);
  NGA_Get(g_a,lo,hi,lptr,&ldl);
  l=0;
  for (i = lo[0]; i<=hi[0]; i++) {
    for (j = lo[1]; j<=hi[1]; j++) {
      if (lptr[l] != (double)(j + i*ld)) {
        GA_Error("Incorrect value from get",lptr[l]);
      }
      l++;
    }
  }
  if (me == 0) {
    printf("\nGet test using attached memory OK\n");
  }
  GA_Sync();
  /* Test accumulate operation by incrementing values */
  rone = 1.0;
  NGA_Acc(g_a,lo,hi,lptr,&ldl,&rone);
  GA_Sync();
  /* Check values */
  NGA_Distribution(g_a,me,lo,hi);
  NGA_Access(g_a,lo,hi,&ptr,&ldl);
  l=0;
  for (i = lo[0]; i<=hi[0]; i++) {
    for (j = lo[1]; j<=hi[1]; j++) {
      if (ptr[l] != (double)(2*(j + i*ld))) {
        GA_Error("Incorrect value from accumulate",ptr[l]);
      }
      l++;
    }
  }
  if (me == 0) {
    printf("\nAccumulate test using attached memory OK\n");
  }
  NGA_Release(g_a,lo,hi);
  GA_Destroy(g_a);
  /* Free local buffers */
  free(ptr);
  free(lptr);
}

int 
main(int argc, char **argv) {

Integer heap=9000000, stack=9000000;
int me, nproc;

    MP_INIT(argc,argv);

    GA_INIT(argc,argv);                           /* initialize GA */

    nproc = GA_Nnodes();
    me = GA_Nodeid();

    if(me==0) printf("Testing global arrays using attached data\n");
    if(me==0) printf("\nUsing %d processes\n\n",nproc);

    /*
    if(!MA_init((Integer)MT_F_DBL, stack/nproc, heap/nproc))
       GA_Error("MA_init failed bytes= %d",stack+heap);   
       */

    do_work();

    if(me==0)printf("\nSuccess\n\n");
    GA_Terminate();

    MP_FINALIZE();

    return 0;
}

