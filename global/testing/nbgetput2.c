#if HAVE_CONFIG_H
#   include "config.h"
#endif

#if HAVE_STDIO_H
#   include <stdio.h>
#endif
#if HAVE_MATH_H
#   include <math.h>
#endif
#include <stdlib.h>

#include "ga.h"
#include "macdecls.h"
#include "mp3.h"

#include <unistd.h>

// #define NN 8192            /* dimension of matrices */
#define NN 32768            /* dimension of matrices */
#define WINDOWSIZE 2
#define min(a,b) (((a)<(b))?(a):(b))

/// Comment following line to run get and accumulate tests
#define GET_PUT
#define TIMER GA_Wtime
int main( int argc, char **argv ) {
  int g_a_nb, g_a_b;
  int g_b_nb, g_b_b;
  int g_c_nb, g_c_b;
  int lo[2], hi[2];
  int dims[2];
  int me, nproc;
  int  ld, isize, jsize;
  int type=MT_C_INT;
  int nelems, ok;
  int *buf_a_nb, *buf_a_b; 
  int *buf_b_nb, *buf_b_b; 
  int *buf_c_nb, *buf_c_b; 
  int *ptr_a;
  int *ptr_b;
  int *ptr_c;
  ga_nbhdl_t *nbhdl_a;
  ga_nbhdl_t *nbhdl_b;
  ga_nbhdl_t *nbhdl_c;

  int heap=3000000, stack=2000000;

  MP_INIT(argc,argv);

  GA_INIT(argc,argv);                            /* initialize GA */
  me=GA_Nodeid(); 
  nproc=GA_Nnodes();
  if(me==0) {
    if(GA_Uses_fapi())GA_Error("Program runs with C array API only",1);
    printf("\nUsing %d processes\n",nproc);
    fflush(stdout);
  }

  heap /= nproc;
  stack /= nproc;
  if(! MA_init(MT_F_DBL, stack, heap)) 
    GA_Error("MA_init failed",stack+heap);  /* initialize memory allocator*/ 
  int N;
for (N = 8; N < NN; N*=2) {

  /* Create a regular matrix. */
  if(me==0)printf("\nCreating matrix A of size %d x %d\n",N,N);
  if(me==0)printf("\nCreating matrix B of size %d x %d\n",N,N);
  if(me==0)printf("\nCreating matrix C of size %d x %d\n",N,N);
  dims[0] = N;
  dims[1] = N;
  
  int n;
  g_a_nb = NGA_Create(type, 2, dims, "A", NULL);
  if(!g_a_nb) GA_Error("create failed: A",n); 
  g_a_b = NGA_Create(type, 2, dims, "A", NULL);
  if(!g_a_b) GA_Error("create failed: A",n); 

  g_b_nb = NGA_Create(type, 2, dims, "B", NULL);
  if(!g_b_nb) GA_Error("create failed: B",n); 
  g_b_b = NGA_Create(type, 2, dims, "B", NULL);
  if(!g_b_b) GA_Error("create failed: B",n); 

  g_c_nb = NGA_Create(type, 2, dims, "C", NULL);
  if(!g_c_nb) GA_Error("create failed: C",n); 
  g_c_b = NGA_Create(type, 2, dims, "C", NULL);
  if(!g_c_b) GA_Error("create failed: C",n); 

  /* Fill matrix from process 0 using non-blocking puts */
  nelems = N*N;
  GA_Sync();
  /* Copy matrix to process 0 using non-blocking gets */
  if (me == 0) {

    buf_a_nb = (int*)malloc(nelems*sizeof(int));
    buf_b_nb = (int*)malloc(nelems*sizeof(int));
    buf_c_nb = (int*)malloc(nelems*sizeof(int));
    buf_a_b = (int*)malloc(nelems*sizeof(int));
    buf_b_b = (int*)malloc(nelems*sizeof(int));
    buf_c_b = (int*)malloc(nelems*sizeof(int));

    nbhdl_a = (ga_nbhdl_t*)malloc(nproc*sizeof(ga_nbhdl_t));
    nbhdl_b = (ga_nbhdl_t*)malloc(nproc*sizeof(ga_nbhdl_t));
    nbhdl_c = (ga_nbhdl_t*)malloc(nproc*sizeof(ga_nbhdl_t));

    //TODO: set this value
    //int alpha=1;

    double nbput_timings = 0;
    double nbget_timings = 0;
    double nbacc_timings = 0;
    double wait_timings = 0;
    double start_time, end_time;
    long scale = 1;
    ptr_c = buf_c_nb;
    int i;
#if 1
    for (i=0; i<nproc; i+=WINDOWSIZE) {
        ld = N;
        int j;
        for(j=i; j <min(nproc, i+WINDOWSIZE); j++)
        {
            if(j==me) continue;

            NGA_Distribution(g_a_nb, j, lo, hi);
            ptr_a = buf_a_nb + lo[1] + N*lo[0];
            start_time = TIMER();
            NGA_NbGet(g_a_nb, lo, hi, ptr_a, &ld, &nbhdl_a[j]);
            end_time = TIMER();
            nbget_timings += end_time - start_time;

            NGA_Distribution(g_b_nb, j, lo, hi);
            ptr_b = buf_b_nb + lo[1] + N*lo[0];
            start_time = TIMER();
            NGA_NbGet(g_b_nb, lo, hi, ptr_b, &ld, &nbhdl_b[j]);
            end_time = TIMER();
            nbget_timings += end_time - start_time;


            NGA_Distribution(g_c_nb, j, lo, hi);
            isize = (hi[0]-lo[0]+1);
            jsize = (hi[1]-lo[1]+1);
            ld = jsize;

            start_time = TIMER();
#ifdef GET_PUT
            NGA_NbPut(g_c_nb, lo, hi, ptr_c, &ld, &nbhdl_c[j]);  
            end_time = TIMER();
            nbput_timings += end_time - start_time;
            ptr_c += isize*jsize;
#else
            NGA_NbAcc(g_c_nb, lo, hi, ptr_c, &ld, &scale, &nbhdl_c[j]);  
            end_time = TIMER();
            nbacc_timings += end_time - start_time;
            ptr_c += isize*jsize;
#endif
        }

        for(j=i; j <min(nproc, i+WINDOWSIZE); j++)
        {
            if(j==me) continue;

            start_time = TIMER();
            NGA_NbWait(&nbhdl_a[j]);
            end_time = TIMER();
            wait_timings += end_time - start_time;

            start_time = TIMER();
            NGA_NbWait(&nbhdl_b[j]);
            end_time = TIMER();
            wait_timings += end_time - start_time;
            // if(j==me) continue;

            start_time = TIMER();
            NGA_NbWait(&nbhdl_c[j]);
            end_time = TIMER();
            wait_timings += end_time - start_time;
        }
    }

    printf("\n\n");
    printf("NbGet timings: %lf sec\n", nbget_timings);
#ifdef GET_PUT
    printf("NbPut timings: %lf sec\n", nbput_timings);
#else
    printf("NbAcc timings: %lf sec\n", nbacc_timings);
#endif
    printf("NbWait timings: %lf sec\n", wait_timings);
    printf("================================\n");
    printf("NbTotal time: %lf sec\n", 
       nbget_timings + nbput_timings + nbacc_timings + wait_timings);
    printf("================================\n");
    printf("\n\n");
#endif
#if 1
    double put_timings = 0;
    double get_timings = 0;
    double acc_timings = 0;

    ptr_c = buf_c_b;

    for (i=0; i<nproc; i+=WINDOWSIZE) {
        ld = N;
        int j;
        for(j=i; j <min(nproc, i+WINDOWSIZE); j++)
        {
            if(j==me) continue;

            NGA_Distribution(g_a_b, j, lo, hi);
            ptr_a = buf_a_b + lo[1] + N*lo[0];
            start_time = TIMER();
            NGA_Get(g_a_b, lo, hi, ptr_a, &ld);
            end_time = TIMER();
            get_timings += end_time - start_time;

            NGA_Distribution(g_b_b, j, lo, hi);
            ptr_b = buf_b_b + lo[1] + N*lo[0];
            start_time = TIMER();
            NGA_Get(g_b_b, lo, hi, ptr_b, &ld);
            end_time = TIMER();
            get_timings += end_time - start_time;

        // }

        // for(int j=i; j <min(nproc, i+WINDOWSIZE); j++)
        // {
        //     if(j==me) continue;
            NGA_Distribution(g_c_b, j, lo, hi);
            isize = (hi[0]-lo[0]+1);
            jsize = (hi[1]-lo[1]+1);
            ld = jsize;

            start_time = TIMER();
#ifdef GET_PUT
            NGA_Put(g_c_b, lo, hi, ptr_c, &ld);  
            end_time = TIMER();
            put_timings += end_time - start_time;
#else
            NGA_Acc(g_c_b, lo, hi, ptr_c, &ld, &scale);  
            end_time = TIMER();
            acc_timings += end_time - start_time;
#endif
            ptr_c += isize*jsize;
        }
    }
    printf("\n\n");
    printf("Get timings: %lf sec\n", get_timings);
#ifdef GET_PUT
    printf("Put timings: %lf sec\n", put_timings);
#else
    printf("Acc timings: %lf sec\n", acc_timings);
#endif
    printf("================================\n");
    printf("Total time: %lf sec\n", 
       get_timings + put_timings + acc_timings);
    printf("================================\n");
    printf("\n\n");
#endif

    free(buf_a_nb);
    free(buf_b_nb);
    free(buf_c_nb);
    free(buf_a_b);
    free(buf_b_b);
    free(buf_c_b);
    free(nbhdl_a);
    free(nbhdl_b);
    free(nbhdl_c);
  }
  GA_Sync();

  GA_Destroy(g_a_nb);
  GA_Destroy(g_b_nb);
  GA_Destroy(g_c_nb);
  GA_Destroy(g_a_b);
  GA_Destroy(g_b_b);
  GA_Destroy(g_c_b);
}
  if(me==0)printf("\nSuccess\n");
  GA_Terminate();

  MP_FINALIZE();

 return 0;
}
