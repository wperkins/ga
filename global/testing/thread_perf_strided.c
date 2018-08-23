#include <assert.h>  //enforces errors?
#include <stdio.h>   //input/output
#include <stdlib.h>  //standard for C
#include <math.h>    //math specific functionality

#include "mpi.h" //Message Passing Interface Library
#include "ga.h"  //Global Array Library

//If OPENMP isn't defined, this program is disabled
#if defined(_OPENMP)
#include "omp.h"
#endif

#define DEFAULT_DIM 1024           //dimensions
#define MAX_MESSAGE_SIZE DEFAULT_DIM*DEFAULT_DIM //1048576
#define MAX_FACTOR 256             //for the array of factors to a number

/************************************************************************
 * Factor p processors into 2D processor grid of dimensions px, py
 *************************************************************************/
void grid_factor(int p, int *idx, int *idy) {  
  int i, j;                              //indexes                               
  int ip;                                //comparison value to find factors/primes
  int ifac;                              //how many are in the factor array 
  int pmax;                              //how many are in the prime array?           
  int prime[MAX_FACTOR];                 //prime numbers between 1 and the square root of p
  int fac[MAX_FACTOR];                   //prime factors of p
  int ix, iy;                            //used to find closest factor numbers
  int ichk;                              //is prime bool                      

  i = 1;

 //find all prime numbers, besides 1, less than or equal to the square root of p
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

 //find all prime factors of p
  ip = p;
  ifac = 0;
  for (i=0; i<pmax; i++) {
    while(ip%prime[i] == 0) {
      ifac = ifac + 1;
      fac[ifac-1] = prime[i];
      ip = ip/prime[i];
    }
  }

 //when p itself is prime
  if (ifac==0) {
    ifac++;
    fac[0] = p;
  }
  
 //find two factors of p of approximately the same size
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


/****************************************************************
* Convenience function to check that something is true on all processors 
*****************************************************************/
int trueEverywhere(int flag)
{
  int tflag, nprocs;
  if (flag) tflag = 1;
  else tflag = 0;
  nprocs = GA_Nnodes();
  
  //adds up the number of threads
  GA_Igop(&tflag,1,"+");
  
  //does the numbers match up
  if (nprocs == tflag) return 1;
  return 0;
}

/********************************************
* Function to print out timing statistics per instance 
*********************************************/
void printTimes(double *time, int *ntime, int *nelems, int size,
    int nthread, int x, int y, int test_type)
{
  //initialize variables
  int me = GA_Nodeid();
  int nproc = GA_Nnodes();
  double l_time;
  int l_ntime;
  int l_nelems;
  int one = 1;
  double bandwdth;
  double optime;
  int i;

  l_time = 0.0;
  l_ntime = 0;
  l_nelems = 0;
  
  //adds and combines all the times values together from all the threads
  for (i=0; i<nthread; i++) {
    l_time += time[i];
    l_ntime += ntime[i];
    l_nelems += nelems[i];
  }
  GA_Dgop(&l_time,one,"+");
  GA_Igop(&l_ntime,one,"+");
  GA_Igop(&l_nelems,one,"+");
  
  //each output value faces its specific related calculation
  l_nelems *= sizeof(int);
  bandwdth = ((double)l_nelems)/l_time;
  bandwdth /= 1.0e6;
  optime = 1.0e6*(l_time)/((double)l_ntime);
  
  //resulting output
  if (me==0) { 
    //message size, average time, avg b/w, number of threads, xDim, YDim
    printf("         %7d      %12.6f        %12.6f       %3d  %6d  %6d",
      size,optime,bandwdth,nthread, x, y);
    if (test_type == 0) printf("  PUT\n");
    if (test_type == 1) printf("  GET\n");
    if (test_type == 2) printf("  ACC\n");
  }
}


/* Function to print out timing statistics for read-increment */
void printRITimes(double *time, int *ntime, int nthread)
{
  //initialze variables
  int me = GA_Nodeid();
  int nproc = GA_Nnodes();
  double l_time;
  int l_ntime;
  int one = 1;
  double optime;
  int i;

  l_time = 0.0;
  l_ntime = 0;
  
  //adds and combines all the times values together from all the threads
  for (i=0; i<nthread; i++) {
    l_time += time[i];
    l_ntime += ntime[i];
  }
  GA_Dgop(&l_time,one,"+");
  GA_Igop(&l_ntime,one,"+");
  
  //each output value faces its specific related calculation
  optime = 1.0e6*(l_time)/((double)l_ntime);
  
  //resulting output
  if (me==0) {
    printf("\nStatistics for Read-Increment\n");
    printf("\nTotal operations     Time per op (us)\n");
    printf("     %10d     %16.6f\n",
      l_ntime,optime);
  }
}

int main(int argc, char * argv[])
{
#if defined(_OPENMP) //program is disabled without OPENMP
   //variables initialized
   int x = DEFAULT_DIM;          // array dimension
   int y = DEFAULT_DIM;          // array dimension
   int block_x;                  // size of a block of data x?
   int block_y;                  // size of a block of data y?
   int g_array;                  // global array itself
   int g_count;                  // another global array
   int me;                       // processor ID
   int nproc;                    // number of processors
   int px, py;                   // processor grid dimensions
   int glo[2], ghi[2], gld[2];   // for mismatched data checks
   int tx, ty;                   // task number by dimension?
   int i,j,icnt;                 // index values
   int return_code = 0;          // for return
   int dims[2];                  // dimensions (see x and y)
   int thread_count = 4;         // number of threads used
   int zero = 0, one = 1;        // constants
   int ok;                       // is there no mismatched data?
   int *ptr;                     // for mismatched data check
   char *env_threads;            // get threads from enviornment
   int provided;                 // check for multiple threading
   double *time;                 // average time for single operations
   double *ritime;               // average time for all operations
   int *ntime;                   // counts up for times per single operation
   int *nelems;                  // used to find bandwidth
   int *rinc;                    // counts total operations done
   int *arr_ok;                  // no mismatches? (GET test)
   int msg_size;                 // size of tested message
   int ithread;                  // threads currently used
   int buf_size = MAX_MESSAGE_SIZE;   //buffer size
   int ulim;                     // message size limit and dimension size
   int miss;                     //mismatched data display
    
   //MPI and GA and initiated
   MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);  
   GA_Initialize();
    
   //the the number of nodes and their id
   nproc = GA_Nnodes();  //number of processors
   me = GA_Nodeid();     //processorID
    
   //what is this doing actually?
   if (provided < MPI_THREAD_MULTIPLE && me==0) {
      printf("\nMPI_THREAD_MULTIPLE not provided\n");
   }
    
   //gets the number of omp threads
   if (env_threads = getenv("OMP_NUM_THREADS"))
      thread_count = atoi(env_threads);
   else
      omp_set_num_threads(thread_count);
    
   //8 is the max
   if (thread_count > 8) thread_count = 8;

   if (me==0) {
      printf("\n[%d]Testing %d threads.\n", me, thread_count);
   }
   
   //thread split phase begins 
   for (ithread = 1; ithread <= thread_count; ithread++) {
   #pragma omp parallel num_threads(ithread)
   {

   //identify the thread
   int id;
   id = omp_get_thread_num();
      
   if (id == 0) {
      if (ithread == 1) {
         // Find processor grid dimensions and processor grid coordinates
         grid_factor(nproc, &px, &py); 
            
         if (me == 0) {
            printf("\nTest running of %d processors\n",nproc);
            printf("\n  Array dimension is %d X %d\n",x,y);
            printf("\n  Processor grid is %d X %d\n\n",px,py);
            }
            
         dims[0] = x;
         dims[1] = y;
            
         //allocate room for the pointer variables
         time = (double*)malloc(thread_count*sizeof(double));
         ritime = (double*)malloc(thread_count*sizeof(double));
         ntime = (int*)malloc(thread_count*sizeof(int));
         nelems = (int*)malloc(thread_count*sizeof(int));
         rinc = (int*)malloc(thread_count*sizeof(int));
         arr_ok = (int*)malloc(thread_count*sizeof(int));
            
         //create data for each thread in a pointer array
         for (i=0; i<thread_count; i++) {
            time[i] = 0.0;
            ritime[i] = 0.0;
            ntime[i] = 0;
            nelems[i] = 0;
            rinc[i] = 0;
            arr_ok[i] = 0;
         }
      }
    
      if (me == 0) printf("\nCreating...");
         
      //Create GA and set all elements to zero
      g_array = NGA_Create(C_INT, 2, dims, "source", NULL);
      g_count = NGA_Create(C_INT, 1, &one, "counter", NULL);
      GA_Zero(g_array);
         
      if (me == 0) printf("GA created!\n");
         
         ok = 1;
         miss = 0;
   }
 
   #pragma omp barrier
     
   //display other test lines that only need to be done once
   if (me == 0 && id == 0) {
      printf("\nPerformance of GA functions");
      printf("\nmsg size (bytes)     avg time (us)    avg b/w (MB/sec) N threads   X-DIM   Y-DIM TEST\n");
   } 
      
   //msg_size loop
   msg_size = 1;
   #pragma omp barrier      
      
   while (msg_size <= buf_size) {     
      if (id == 0) {
         ulim = DEFAULT_DIM;
         if (ulim > msg_size) ulim = msg_size;
      }
         
      //block_x loop
      block_x = 1;

      //barrier here
      #pragma omp barrier

      while (block_x <= ulim) {   
         #pragma omp barrier
 
         int lo[2];
         int hi[2];
         int tlo[2];
         int thi[2];
         int ld[2];
         int k;
         int m;
         int n;
         int xinc, yinc;
         int itx, ity;
         int offset;
         int *buf;
         int lld;
         long task, inc;
         double delta_t;
         int bsize;
         
         if (id == 0) block_y = msg_size / block_x;
         #pragma omp barrier

         if (block_y <= DEFAULT_DIM) {           
            if (id == 0) {
               for (i=0; i<ithread; i++) {
                  time[i] = 0.0;
                  ntime[i] = 0;
                  nelems[i] = 0;
                  arr_ok[i] = 0;
               }
            }
            #pragma omp barrier
          
            if (id == 0) {
               tx = x/block_x;
               if (tx*block_x < x) tx++;
               ty = y/block_y;
               if (ty*block_y < y) ty++;
               // Fill global array with data by having each thread write blocks to it
               GA_Zero(g_count);
            }
        
            #pragma omp barrier
          
            //PUT TEST BEGINS
            
            inc = 1;
            delta_t = GA_Wtime();
            task = NGA_Read_inc(g_count, &zero, inc);
            delta_t = GA_Wtime()-delta_t;
            ritime[id] += delta_t;
            rinc[id] += one;
            buf = (int*)malloc(block_x*block_y*sizeof(int));
            while (task < tx*ty) {
               ity = task%ty;
               itx = (task-ity)/ty;
               tlo[0] = itx*block_x;
               tlo[1] = ity*block_y;
               // printf("j: %d k: %d tlo[0]: %d tlo[1]: %d xinc: %d yinc: %d\n", j,k,tlo[0],tlo[1],xinc,yinc);
               thi[0] = tlo[0] + block_x - 1;
               if (thi[0] >= dims[0]) thi[0] = dims[0]-1;
               thi[1] = tlo[1] + block_y - 1;
               if (thi[1] >= dims[1]) thi[1] = dims[1]-1;
               lld = thi[1]-tlo[1]+1;
               bsize = (thi[0]-tlo[0]+1)*(thi[1]-tlo[1]+1);

               // Fill a portion of local buffer with correct values
               for (m=tlo[0]; m<=thi[0]; m++) {
                  for (n=tlo[1]; n<=thi[1]; n++) {
                     offset = (m-tlo[0])*lld + (n-tlo[1]);
                     buf[offset] = m*dims[1]+n;
                  }
               }

               delta_t = GA_Wtime();
               NGA_Put(g_array, tlo, thi, buf, &lld);
               delta_t = GA_Wtime()-delta_t;
               time[id] += delta_t;
               ntime[id] += one;
               nelems[id] += bsize;

               delta_t = GA_Wtime();
               task = NGA_Read_inc(g_count, &zero, inc);
               delta_t = GA_Wtime()-delta_t;
               ritime[id] += delta_t;
               rinc[id] += one;
               }
              
            free(buf);
             
            //"threads join back"
            #pragma omp barrier
          
          if (id == 0) {
             //display results for that line
             printTimes(time,ntime,nelems,block_x*block_y,ithread,block_x,block_y,0);
             
             // Sync all processors at end of initialization loop
             NGA_Sync();
          
             // Each process determines if it is holding the correct data
             NGA_Distribution(g_array,me,glo,ghi);
             NGA_Access(g_array,glo,ghi,&ptr,gld);
             icnt = 0;
             for (i=glo[0]; i<=ghi[0]; i++) {
                for (j=glo[1]; j<=ghi[1]; j++) {
                   if (ptr[icnt] != i*dims[1]+j) {  //mismatch condition
                      ok = 0;
                      if (miss == 0) {
                         printf("\nMISS:PUT\n");
                         miss++;
                      }
                      printf("p[%d] (Put) mismatch at point [%d,%d] actual: %d expected: %d\n",
                          me,i,j,ptr[icnt],i*dims[1]+j);
                   } 
                icnt++;
                }
             }
             NGA_Release(g_array,glo,ghi);
          }
          
          //END PUT TEST
          #pragma omp barrier
          
          //START GET TEST
          
          if (id == 0) {
             for (i=0; i<ithread; i++) {
                time[i] = 0.0;
                ntime[i] = 0;
                nelems[i] = 0;
                arr_ok[i] = 0;
             }
             
             tx = x/block_x;
             if (tx*block_x < x) tx++;
             ty = y/block_y;
             if (ty*block_y < y) ty++;
             // Fill global array with data by having each thread write blocks to it
             GA_Zero(g_count);
          } 
          
          #pragma omp barrier
          
          arr_ok[id] = 1;
          delta_t = GA_Wtime();
          task = NGA_Read_inc(g_count, &zero, inc);
          delta_t = GA_Wtime()-delta_t;
          ritime[id] += delta_t;
          rinc[id] += one;
          buf = (int*)malloc(block_x*block_y*sizeof(int));
          while (task < tx*ty) {
             ity = task%ty;
             itx = (task-ity)/ty;
             tlo[0] = itx*block_x;
             tlo[1] = ity*block_y;
             thi[0] = tlo[0] + block_x - 1;
             if (thi[0] >= dims[0]) thi[0] = dims[0]-1;
             thi[1] = tlo[1] + block_y - 1;
             if (thi[1] >= dims[1]) thi[1] = dims[1]-1;
             lld = thi[1]-tlo[1]+1;
             bsize = (thi[0]-tlo[0]+1)*(thi[1]-tlo[1]+1);

             delta_t = GA_Wtime();
             NGA_Get(g_array, tlo, thi, buf, &lld);
             delta_t = GA_Wtime()-delta_t;
             time[id] += delta_t;
             ntime[id] += one;
             nelems[id] += bsize;

             //check that values in buffer are correct
             for (m=tlo[0]; m<=thi[0]; m++) {
                for (n=tlo[1]; n<=thi[1]; n++) {
                   offset = (m-tlo[0])*lld + (n-tlo[1]);
                   if (buf[offset] != m*dims[1]+n) {
                      arr_ok[id] = 0;
                      if (miss == 0) {
                         printf("\nMISS:GET\n");
                         miss++;
                      }
                      //Mismatch Error
                      //printf("Read mismatch for [%d,%d] expected: %d actual: %d\n", m,n,m*dims[1]+n,lld);
                   }
                }
             }
              
             delta_t = GA_Wtime();
             task = NGA_Read_inc(g_count, &zero, inc);
             delta_t = GA_Wtime()-delta_t;
             ritime[id] += delta_t;
             rinc[id] += one;
          }

          free(buf);
          
          #pragma omp barrier
          
          if (id == 0) {
             for (i=0; i<ithread; i++) if (arr_ok[i] == 0) ok = 0;
             printTimes(time,ntime,nelems,block_x*block_y,ithread,block_x,block_y,1);
             // Sync all processors at end of initialization loop
             NGA_Sync();
          }

          //END GET TEST
          
          #pragma omp barrier
          
          //START ACC TEST
          
          if (id == 0) {
             for (i=0; i<ithread; i++) {
                time[i] = 0.0;
                ntime[i] = 0;
                nelems[i] = 0;
             }
          
             tx = x/block_x;
             if (tx*block_x < x) tx++;
             ty = y/block_y;
             if (ty*block_y < y) ty++;
             GA_Zero(g_count);
             GA_Zero(g_array);
          }
          
          #pragma omp barrier
          inc = 1;
          delta_t = GA_Wtime();
          task = NGA_Read_inc(g_count, &zero, inc);
          delta_t = GA_Wtime()-delta_t;
          ritime[id] += delta_t;
          rinc[id] += one;
          buf = (int*)malloc(block_x*block_y*sizeof(int));
          while (task < 2*tx*ty) {
             k = task;
             if (task >= tx*ty) k = k - tx*ty;
             ity = k%ty;
             itx = (k-ity)/ty;
             tlo[0] = itx*block_x;
             tlo[1] = ity*block_y;

             thi[0] = tlo[0] + block_x - 1;
             if (thi[0] >= dims[0]) thi[0] = dims[0]-1;
             thi[1] = tlo[1] + block_y - 1;
             if (thi[1] >= dims[1]) thi[1] = dims[1]-1;
             lld = thi[1]-tlo[1]+1;
             bsize = (thi[0]-tlo[0]+1)*(thi[1]-tlo[1]+1);

             /* Fill a portion of local buffer with correct values */
             for (m=tlo[0]; m<=thi[0]; m++) {
                for (n=tlo[1]; n<=thi[1]; n++) {
                   offset = (m-tlo[0])*lld + (n-tlo[1]);
                   buf[offset] = m*dims[1]+n;
                }
             }

             delta_t = GA_Wtime();
             NGA_Acc(g_array, tlo, thi, buf, &lld, &one);
             delta_t = GA_Wtime()-delta_t;
             time[id] += delta_t;
             ntime[id] += one;
             nelems[id] += bsize;

             delta_t = GA_Wtime();
             task = NGA_Read_inc(g_count, &zero, inc);
             delta_t = GA_Wtime()-delta_t;
             ritime[id] += delta_t;
             rinc[id] += one;
          }
          
          free(buf);
            
          #pragma omp barrier
          
          if (id == 0) {
             printTimes(time,ntime,nelems,block_x*block_y,ithread,block_x,block_y,2);
             /* Sync all processors at end of initialization loop */
             NGA_Sync();
             /* Each process determines if it is holding the correct data */
             NGA_Distribution(g_array,me,glo,ghi);
             NGA_Access(g_array,glo,ghi,&ptr,gld);
             icnt = 0;
             for (i=glo[0]; i<=ghi[0]; i++) {
                for (j=glo[1]; j<=ghi[1]; j++) {
                  if (ptr[icnt] != 2*(i*dims[1]+j)) {
                    if (miss == 0) {
                       printf("\nMISS:PUT\n");
                       miss++;
                    }
                    ok = 0;
                    printf("p[%d] (Acc) mismatch at point [%d,%d] actual: %d expected: %d\n",
                       me,i,j,ptr[icnt],2*(i*dims[1]+j));
                  }
                  icnt++;
                }
             }
             NGA_Release(g_array,glo,ghi);
          }
          
          //END ACC TEST 
          #pragma omp barrier

         } //END y_block            

         //increment
         #pragma omp barrier
         if (id == 0) { 
            block_x *= 2;
         }
         
         #pragma omp barrier
          
        } //end x_block/ulim
       
        //increment
        #pragma omp barrier
        if (id == 0) msg_size *= 2;
        #pragma omp barrier
       
     } //end msg_size
    
     #pragma omp barrier
     if (id == 0) {
        if (me == 0) printf("\nDeleting...");
        GA_Destroy(g_array);
        GA_Destroy(g_count);
        if (me == 0) printf("GA deleted!\n");
     }
     #pragma omp barrier
   
    #pragma omp barrier
    if (id == 0) GA_Sync();
    #pragma omp barrier
 
    } //end multithreading
   } //end ithread
       
   //print final results
   printRITimes(ritime, rinc, thread_count);

   //free up pointed variables
   free(time);
   free(ritime);
   free(ntime);
   free(nelems);
   free(rinc);
   free(arr_ok);

   //End GA and MPI
   GA_Terminate();
   MPI_Finalize();

   return return_code;
#else
   printf("OPENMP Disabled\n");
   return 1;
#endif
}
