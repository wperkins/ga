#include "config.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "ga.h"
#include "macdecls.h"
#include "mp3.h"

#include "omp.h"

#define N 1024          //dimension of matrices

int main( int argc, char **argv ) {
  
  int g_a;              //global array used
  int j;                //secondary index
  int size;             //total size (N*N)
  int size_me;          //size for one processor
  int ld;               //for GA value checks
  int n=N;              //dimension size
  int type=MT_C_INT;    //global array type indicator
  int one = 1;          //one = 1; only used in scatters/Dgop
  int *ptr;             //GA_Access, location/value checking
  int dims[2]={N,N};    //Global Array dimensions
  int lo[2];            //For GA value checks
  int hi[2];            //For GA value checks
  
  int heap=3000000;     //for memory allocator
  int stack=2000000;    //for memory allocator
  int me;               //processor id
  int nproc;            //number of processors
  
  int thread_count = 4; //number of threads (default 4)
  int ithread;          //thread index
  char *env_threads;    //for thread_count
  int provided;         //check for multiple threading

  int *values;          //identifies values to be scattered
  int **indices;        //identifies locations to be scattered to

  int tdx;              //thread index

  //performance tracking variables
  double *timescat;        
  double *timegat;         
  double *timeacc;         
  double *timebuf;         

  double timescat_total;   
  double timegat_total;    
  double timeacc_total;    
  double timebuf_total;    

  MPI_Init_thread(&argc,&argv, MPI_THREAD_MULTIPLE, &provided);
  
  GA_Initialize();

//is openmp active?  
#if defined(_OPENMP)
#else
  printf("OPENMP Disabled\n");
  return 1; 
#endif

  //identify nodes
  me=GA_Nodeid();
  nproc=GA_Nnodes(); 

  if(me==0) {
    if(GA_Uses_fapi())GA_Error("Program runs with C array API only",1); //api check
    
    printf("\nUsing %ld processes\n",(long)nproc);
    fflush(stdout);
  }
  
  //threading setup
  
  if (provided < MPI_THREAD_MULTIPLE && me==0)
    printf("\nMPI_THREAD_MULTIPLE not provided\n");
  
  if(env_threads = getenv("OMP_NUM_THREADS"))
    thread_count = atoi(env_threads);
  else
    omp_set_num_threads(thread_count);
  
  if (thread_count > 8) thread_count = 8;
  
  if (me == 0) {
    printf("\n[%d]Testing %d threads. \n", me, thread_count);
  }
  
  //memory allocation
  //heap and stack calculations
  heap /= nproc;
  stack /= nproc;
  
  //memory allocation specific to creating the GA itself
  if(! MA_init(MT_F_DBL, stack, heap))
    GA_Error("MA_init failed",stack+heap); //initialize memory allocator

  timescat = (double*)malloc(thread_count*sizeof(double)); 
  timegat = (double*)malloc(thread_count*sizeof(double));
  timeacc = (double*)malloc(thread_count*sizeof(double));
  timebuf = (double*)malloc(thread_count*sizeof(double));

  for (tdx = 0; tdx < thread_count; tdx++) {
  
    timescat[tdx] = 0.0;
    timegat[tdx] = 0.0;
    timeacc[tdx] = 0.0;
    timebuf[tdx] =0.0; 
  }
  
  ithread = 1;

 
while (ithread <= thread_count) {
//SPLIT THREADS HERE
#pragma omp parallel num_threads(ithread)
{
  int id;          //thread id
  double delta_t;  //time track variable
  int nthread;     //number of threads
  int meid;        //identity for processor/thread piece
  int ntotal;      //total number of processors/thread pieces
  int size_meid;   //size of each part of the  processor/thread piece
  int i;           //index variable
  int j;           //secondary index variable
  int idx;         
  int jdx;         
  int icnt;        
  int size_prev;   //size_meid of a previous processor/thread piece
  int start_loc = 0; 
  int k = 0;       
  int* p_values;
  int** p_indices;
  int icheck;
  int jcheck; 
 
  id = omp_get_thread_num();
  nthread = ithread;
  meid = (me * nthread) + id;
  ntotal = nproc * nthread;
 
  #pragma omp barrier
  
  if (id == 0) {
  // Create a regular matrix.
    g_a = NGA_Create(type, 2, dims, "A", NULL);
    if(!g_a) GA_Error("create failed: A",n);
  } 
  #pragma omp barrier
  
  // Fill matrix using scatter routines
  // split matrix into processor/thread based portions
  size = N*N;

  //size_meid
  if (size%ntotal == 0) {
    size_meid = size/ntotal;
  } else {
    icheck = size - size%ntotal;
    size_meid = icheck/ntotal;
    if (meid < size%ntotal) size_meid++;
  }

  #pragma omp barrier

  //size_me
  if (id == 0) {
    size_me = 0;
    while (k < nthread) {
      if (size%ntotal == 0) {
        size_prev = size/ntotal;
      } else {
        icheck = size - size%ntotal;
        size_prev = icheck/ntotal;
        if ((meid+k) < size%ntotal) size_prev++;
      }
      size_me += size_prev;
      k++;
    }
  }

  #pragma omp barrier

  k = 0;

  //start loc 
  while (k < id) {
    jcheck = size_me - size_me%nthread;
    size_prev = jcheck/nthread;
    if (k < size_me%nthread) size_prev++;
    start_loc += size_prev;
    k++;
  }

  #pragma omp barrier
  
  if (id == 0) { 
    values = (int*)malloc(size_me*sizeof(int));
    indices = (int**)malloc(size_me*sizeof(int*));
  }

  #pragma omp barrier

  icnt = meid;

  for (i=0; i<size_meid; i++) {
    values[start_loc+i] = icnt;
    idx = icnt%N;
    jdx = (icnt-idx)/N;
    if (idx >= N || idx < 0) {
      printf("p[%d] Bogus index i: %d:%d\n",meid,idx,jdx);
    }
    if (jdx >= N || jdx < 0) {
      printf("p[%d] Bogus index j: %d:%d   %d/%d\n",meid,idx,jdx,icnt,size_meid);
    }
    indices[start_loc+i] = (int*)malloc(2*sizeof(int));
    indices[start_loc+i][0] = idx;
    indices[start_loc+i][1] = jdx;

    icnt += ntotal;
  }

  #pragma omp barrier
 
  //START SCATTER
  //Scatter values into g_a

  p_values = &values[start_loc];
  p_indices = &indices[start_loc];

  #pragma omp barrier
  delta_t = GA_Wtime();
  NGA_Scatter(g_a, p_values, p_indices, size_meid);
  delta_t = GA_Wtime()-delta_t;
  
  timescat[id]  = delta_t;

  #pragma omp barrier

  NGA_Sync();

  #pragma omp barrier

  if (id == 0) {
    // Check to see if contents of g_a are correct
    NGA_Distribution(g_a, me, lo, hi );
    NGA_Access(g_a, lo, hi, &ptr, &ld);
    for (i=lo[0]; i<hi[0]; i++) {
      idx = i-lo[0];
      for (j=lo[1]; j<hi[1]; j++) {
        jdx = j-lo[1];
        if (ptr[idx*ld+jdx] != j*N+i) {
          printf("p[%d] (Scatter) expected: %d actual: %d\n",me,j*N+i,ptr[idx*ld+jdx]);
        }
      }
    }  
  }

  #pragma omp barrier
  
  //if (meid==0) printf("\nCompleted test of NGA_Scatter\n"); 

  #pragma omp barrier

  if (id == 0) {
    for (i=0; i<size_meid; i++) {
      values[i] = 0;
    }
  }

  GA_Sync();

  #pragma omp barrier

  delta_t = GA_Wtime();
  NGA_Gather(g_a, p_values, p_indices, size_meid);
  delta_t = GA_Wtime()- delta_t;

  timegat[id] = delta_t;

  //printf("Time: %12.6f\n", delta_t);

  #pragma omp barrier
  
  icnt = meid;
  for (i=0; i<size_meid; i++) {
    if (icnt != p_values[i]) {
      printf("p[%d] (Gather) expected: %d actual: %d\n",me,icnt,values[i]);
    }
    icnt += ntotal;
  }

  //if (meid==0) printf("\nCompleted test of NGA_Gather\n");

  GA_Sync(); 

  #pragma omp barrier

  one = 1;
  
  delta_t = GA_Wtime();
  NGA_Scatter_acc(g_a, p_values, p_indices, size_meid, &one);
  delta_t = GA_Wtime() - delta_t;

  timeacc[id] = delta_t;

  //printf("Time: %12.6f\n", delta_t);

  #pragma omp barrier

  GA_Sync();

  #pragma omp barrier

  // Check to see if contents of g_a are correct
  for (i=lo[0]; i<hi[0]; i++) {
    idx = i-lo[0];
    for (j=lo[1]; j<hi[1]; j++) {
      jdx = j-lo[1];
      if (ptr[idx*ld+jdx] != 2*(j*N+i)) {
        printf("p[%d] (Scatter_acc) expected: %d actual: %d\n",me,2*(j*N+i),ptr[idx*ld+jdx]);
      }
    }
  }

  //if (meid==0) printf("\nCompleted test of NGA_Scatter_acc\n");

  if (id == 0) NGA_Release(g_a, lo, hi);

  //Test fixed buffer size
  if (id == 0) NGA_Alloc_gatscat_buf(size_me);

  #pragma omp barrier

  //Scatter-accumulate values back into GA
  GA_Sync();

  #pragma omp barrier
  delta_t = GA_Wtime();
  NGA_Scatter_acc(g_a, p_values, p_indices, size_meid, &one);
  delta_t = GA_Wtime() - delta_t;

  timebuf[id] = delta_t;

  //printf("Time: %12.6f\n", delta_t);

  #pragma omp barrier

  GA_Sync();

  #pragma omp barrier

  // Check to see if contents of g_a are correct 
  for (i=lo[0]; i<hi[0]; i++) {
    idx = i-lo[0];
    for (j=lo[1]; j<hi[1]; j++) {
      jdx = j-lo[1];
      if (ptr[idx*ld+jdx] != 3*(j*N+i)) {
        printf("p[%d] (Scatter_acc) expected: %d actual: %d\n",me,3*(j*N+i),ptr[idx*ld+jdx]);
      }
    }
  }

  //if (meid == 0) printf("\nCompleted test of NGA_Scatter_acc using fixed buffers\n");
 
  //if (id == 0) GA_Print(g_a);

  #pragma omp barrier

  if (id == 0) {
    NGA_Release(g_a, lo, hi);
    NGA_Free_gatscat_buf();  //for fixed buffer test
  
    GA_Destroy(g_a);
  }
  
  } //end thread split

  for (tdx = 0; tdx < ithread; tdx++) {

  timescat_total += timescat[tdx];
  timegat_total += timegat[tdx];
  timeacc_total += timeacc[tdx];
  timebuf_total += timebuf[tdx];

  }

  //Calculate and print results

  GA_Dgop(&timescat_total,one,"+");
  GA_Dgop(&timegat_total,one,"+");
  GA_Dgop(&timeacc_total,one,"+");
  GA_Dgop(&timebuf_total,one,"+");

  if (me == 0 && ithread == 1) printf("\nTest type   msg size (bytes)   avg time (us)    avg b/w (MB/sec) N threads\n");
  if (me == 0) printf("Scatter              %7d    %12.6f        %12.6f       %3d\n", size, 1.0e6*(timescat_total/(double)size), (((double)(sizeof(int)*size))/timescat_total)/1.0e6, ithread);
  if (me == 0) printf("Gather               %7d    %12.6f        %12.6f       %3d\n", size, 1.0e6*(timegat_total/(double)size), (((double)(sizeof(int)*size))/timegat_total)/1.0e6, ithread);
  if (me == 0) printf("Accumulate           %7d    %12.6f        %12.6f       %3d\n", size, 1.0e6*(timeacc_total/(double)size), (((double)(sizeof(int)*size))/timeacc_total)/1.0e6, ithread);
  if (me == 0) printf("Acc (fixed)          %7d    %12.6f        %12.6f       %3d\n", size, 1.0e6*(timebuf_total/(double)size), (((double)(sizeof(int)*size))/timebuf_total)/1.0e6, ithread);

  //if (me == 0) printf("\n-%d thread complete-\n", ithread);

  ithread++;

  } //end number of threads loop

  GA_Terminate(); 

  MPI_Finalize(); 

  if (me == 0) printf("\nTEST COMPLETE");
  return 0;

}

