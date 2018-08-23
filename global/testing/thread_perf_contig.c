#include <assert.h> //enforces errors?
#include <stdio.h>  //input/output
#include <stdlib.h> //standard for C 
#include <math.h>   //math specific functionality

#include "mpi.h"    //Message Passing Interface Library
#include "ga.h"     //Global Array Library

//If OPENMP isn't defined, this program is disabled
#if defined(_OPENMP)
#include "omp.h"
#endif

#define DEFAULT_DIM 1024*1024      //dimension, 1048576

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
  
   //do the numbers match up
   if (nprocs == tflag) return 1;
   return 0;
}

/********************************************
* Function to print out timing statistics per instance 
*********************************************/
void printTimes(double *time, int *ntime, int *nelems, int size, int nthread, int test_type)
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
      //message size, average time, avg b/w, number of threads
      printf("         %7d      %12.6f        %12.6f       %3d",
      size,optime,bandwdth,nthread);
      if (test_type == 0) printf("  PUT\n");
      if (test_type == 1) printf("  GET\n");
      if (test_type == 2) printf("  ACC\n");
   }
}

/*****************************************************
  * Function to print out timing statistics for read-increment 
******************************************************/
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
      printf("     %10d     %16.6f\n",l_ntime,optime);
   }
}

/****************************
 * main functionality starts here
 ****************************/
int main(int argc, char * argv[])
{
#if defined(_OPENMP)  //test is disabled without OPENMP
   
   //Essential prep before splitting
   //initialize variables
   int dim = DEFAULT_DIM;  //array dimension
   int g_array, g_count;   //global arrays of some sort
   int me, nproc;          //processor ID / number of processors
   int glo;                //for mismatched data checks / global low
   int ghi;                //for mismatched data checks / global high
   int gld;                //for mismatched data checks / global leading dimensions
   int tn;                 //? task number maybe?
   int i;                  // index value
   int j;                  // secondary index value
   int icnt;               // index for mismatched data checks
   int return_code = 0;    // for return
   int thread_count = 4;   // number of threads used
   int zero = 0;           // constant
   int one = 1;            // constant
   int ok;                 // is there no mismatched data
   int *ptr;               //for mismatched data checsk
   char *env_threads;      // get threads from enviornment
   int provided;           // check for multiple threading
   double *time;           // used for average time of single operations
   double *ritime;         // used for average time of all operations
   int *ntime;             // counts up for times per single operation
   int *nelems;            // used to find bandwidth
   int *rinc;              // counts total operations done
   int msg_size;           // size of tested message
   int ithread;            // thread index and total number of threads
   int buf_size = DEFAULT_DIM; //buffer size
   int test_type;          // PUT, GET, and ACC
   int *arr_ok;            // Used for GET 
   int miss;               // missmatched data display 

  //GA and MPI initiated
   MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
   GA_Initialize();
   
   //number of nodes nand their ID
   nproc = GA_Nnodes();  //number of processors
   me = GA_Nodeid();     //processor ID
   
   //What exactly does this mean?
   if (provided < MPI_THREAD_MULTIPLE && me==0)
      printf("\nMPI_THREAD_MULTIPLE not provided\n");
   
   if(env_threads = getenv("OMP_NUM_THREADS"))
      thread_count = atoi(env_threads);
   else
      omp_set_num_threads(thread_count);
   
   //8 is the max
   if (thread_count > 8) 
      thread_count = 8;
   
   if (me == 0) 
      printf("\n[%d]Testing %d threads.\n", me, thread_count);

   //threading begins
   for (ithread = 1; ithread<= thread_count; ithread++) {
   #pragma omp parallel num_threads(ithread)
   {
      //identify the thread
      int id;
      id = omp_get_thread_num();

      #pragma omp barrier      

      // only do on master thread
      if (id == 0)
      {
         if (ithread == 1) {
            // diagnostic display
            if (me == 0) {
               printf("\nTest running of %d processors\n",nproc);
               printf("\n  Array dimension is %d\n",dim);
            }
            
            // allocate room for the pointer variables
            time = (double*)malloc(thread_count*sizeof(double));
            ritime = (double*)malloc(thread_count*sizeof(double));
            ntime = (int*)malloc(thread_count*sizeof(int));
            nelems = (int*)malloc(thread_count*sizeof(int));
            rinc = (int*)malloc(thread_count*sizeof(int));
            arr_ok = (int*)malloc(thread_count*sizeof(int));
         
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
         g_array = NGA_Create(C_INT, 1, &dim, "source", NULL);
         g_count = NGA_Create(C_INT, 1, &one, "counter", NULL);
         GA_Zero(g_array);

         if (me == 0) printf("GA created!\n");

         ok = 1;
         miss = 0;
      }
     
      #pragma omp barrier   

      //display test lines that only need to be done once
      
      if (me == 0 && id == 0) {
         //printf("\nPerformance of GA_Put\n");
         printf("\nmsg size (bytes)     avg time (us)    avg b/w (MB/sec) N threads TEST\n");
      }

      msg_size = 1;      
      #pragma omp barrier

      while (msg_size <= buf_size) {

      //thread split specific threads
      int tlo;          //thread low
      int thi;          //thread high
      int m;            //index value
      int offset;       //for the buffer
      int *buf;         //buffer value / not used
      int lld;          //local leading dimension
      long task;        //output of NGA_Read_inc?
      long inc;         //increment constant
      double delta_t;   //time track variable
      int bsize;        //used to find bandwidth
      int k;
      int n;

      #pragma omp barrier
         //initialize and clean for each test
         if (id == 0) {
            for (i=0; i<ithread; i++) {
               time[i] = 0.0;
               ntime[i] = 0;
               nelems[i] = 0;
               arr_ok[i] = 0;
            }

         tn = dim/msg_size;
         if (tn*msg_size < dim) tn++;

         // Fill global array with data by having each thread write blocks to it
         GA_Zero(g_count);
         //GA_Zero(g_array);
         }
      
         // always threaded begins
         // line up the threads after each msg loop reset
         #pragma omp barrier

         //TEST GA_PUT
         // put test specific functionality
         
         inc = 1;
         delta_t = GA_Wtime();
         task = NGA_Read_inc(g_count, &zero, inc);
         delta_t = GA_Wtime()-delta_t;
         ritime[id] += delta_t;
         rinc[id] += one;
         buf = (int*)malloc(msg_size*sizeof(int));
         while (task < tn) {
            tlo = task*msg_size;
            thi = tlo + msg_size - 1;
            if (thi >= dim) thi = dim-1;
            lld = thi-tlo+1;
            bsize = thi-tlo+1;
            
            // Fill a portion of local buffer with correct values
            for (m=tlo; m<=thi; m++) {
               offset = m-tlo;
               buf[offset] = m;
            }
            
            delta_t = GA_Wtime();
            NGA_Put(g_array, &tlo, &thi, buf, &lld);
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
         
         //line up threads to prepare for print and syncing
         #pragma omp barrier


         if (id == 0) {
           printTimes(time,ntime,nelems,msg_size,ithread,0);

            NGA_Sync();

            // ""threads join back""
            //place all single threaded functions on master thread

            // Each process determines if it is holding the correct data
            NGA_Distribution(g_array,me,&glo,&ghi);
            NGA_Access(g_array,&glo,&ghi,&ptr,&gld);
            icnt = 0;
            for (i=glo; i<=ghi; i++) {

               if (ptr[icnt] != i) {  //mismatch condition
                  ok = 0;
                  if (miss = 0) {
                     printf("\nMISS:PUT\n");
                     miss++;
                  }
                  //printf("p[%d] (Put) mismatch at point [%d] actual: %d expected: %d\n",
                  //   me,i,ptr[icnt],i);
               }
            icnt++;
            }
            
            NGA_Release(g_array,&glo,&ghi);
         }

         #pragma omp barrier

         // END TEST GA_PUT

         // TEST GA_GET
            
         //initialize and clean for each test
         if (id == 0) {
            for (i=0; i<ithread; i++) {
               time[i] = 0.0;
               ntime[i] = 0;
               nelems[i] = 0;
               arr_ok[i] = 0;
            }

            tn = dim/msg_size;
            if (tn*msg_size < dim) tn++;

            // Fill global array with data by having each thread write blocks to it
            GA_Zero(g_count);
            //GA_Zero(g_array);
         }

         #pragma omp barrier

         inc = 1;
         arr_ok[id] = 1;
         delta_t = GA_Wtime();
         task = NGA_Read_inc(g_count, &zero, inc);
         delta_t = GA_Wtime()-delta_t;
         ritime[id] += delta_t;
         rinc[id] += one;
         buf = (int*)malloc(msg_size*sizeof(int));
         
         while (task < tn) {
            tlo = task*msg_size;
            thi = tlo+msg_size-1;
            if (thi >= dim) thi = dim-1;
            lld = thi-tlo+1;
            bsize = thi-tlo+1;

            delta_t = GA_Wtime();
            NGA_Get(g_array, &tlo, &thi, buf, &lld);
            delta_t = GA_Wtime()-delta_t;
            time[id] += delta_t;
            ntime[id] += one;
            nelems[id] += bsize;

            // Check that values in buffer are correct
            for (m=tlo; m<=thi; m++) {
               offset = m-tlo;
               if (buf[offset] != m) {
                 arr_ok[id] = 0;
                 if (miss == 0) {
                    printf("\nMISS:GET\n");
                    miss++;
                 }
                 //printf("p[%d] Read mismatch for [%d] expected: %d actual: %d\n",
                 //    me,m,m,buf[offset]);
               }
            }

            delta_t = GA_Wtime();
            task = NGA_Read_inc(g_count, &zero, inc);
            delta_t = GA_Wtime()-delta_t;
            ritime[id] += delta_t;
            rinc[id] += one;
         }

         free(buf);

        //end parallel
        #pragma omp barrier

        if (id == 0)
        {
           for (i=0; i<ithread; i++) if (arr_ok[i] == 0) ok = 0;
           printTimes(time,ntime,nelems,msg_size,ithread,1);
           // Sync all processors at end of initialization loop
           NGA_Sync();
        }

        #pragma omp barrier  
        // END TEST GA_GET

        //TEST GA_ACC

      //initialize and clean for each test
      if (id == 0) {
         for (i=0; i<ithread; i++) {
            time[i] = 0.0;
            ntime[i] = 0;
            nelems[i] = 0;
            arr_ok[i] = 0;
         }

      tn = dim/msg_size;
      if (tn*msg_size < dim) tn++;

      // Fill global array with data by having each thread write blocks to it
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
            buf = (int*)malloc(msg_size*sizeof(int));
            
            while (task < 2*tn) {
               k = task;
               if (task >= tn) k = k - tn;
               tlo = k*msg_size;
               thi = tlo+msg_size-1;
               if (thi >= dim) thi = dim-1;
               lld = thi-tlo+1;
               bsize = thi-tlo+1;
               
               // Fill a portion of local buffer with correct values
               for (m=tlo; m<=thi; m++) {
                  offset = m-tlo;
                  buf[offset] = m;
               }
               
               delta_t = GA_Wtime();
               NGA_Acc(g_array, &tlo, &thi, buf, &lld, &one);
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
            
            //end parallel
            #pragma omp barrier
            
            if (id == 0) {
               printTimes(time,ntime,nelems,msg_size,ithread,2);
               // Sync all processors at end of initialization loop
               NGA_Sync();
               // Each process determines if it is holding the correct data
               NGA_Distribution(g_array,me,&glo,&ghi);
               NGA_Access(g_array,&glo,&ghi,&ptr,&gld);
               icnt = 0;
               for (i=glo; i<=ghi; i++) {
                  if (ptr[icnt] != 2*i) {
                     ok = 0;
                     if (miss == 0) {
                        printf("\nMISS:ACC\n");
                        miss++;
                     }
                     //printf("p[%d] (Acc) mismatch at point [%d] actual: %d expected: %d\n",
                     //me,i,ptr[icnt],2*i);
                  }

                  icnt++;

               }

               NGA_Release(g_array,&glo,&ghi);
          
            }
     
       // END TEST GA_ACC
        

       //increment the loop, barriers needed for data integrity
       #pragma omp barrier
       if (id == 0) msg_size *= 2;
       #pragma omp barrier

     } //msg loop
     
     #pragma omp barrier
     if (id == 0) NGA_Sync();
     #pragma omp barrier

     //delete and reset both the GAs
     #pragma omp barrier
     if (id == 0) {
        if (me == 0) printf("\nDeleting..."); 
        GA_Destroy(g_array);
        GA_Destroy(g_count);
        if (me == 0) printf("GA deleted!\n");
     }
     #pragma omp barrier

     #pragma omp barrier
     if (id == 0) NGA_Sync();
     #pragma omp barrier

    } //multi threading omp
   } //ithread loop

   //loops and splitting stop here

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
