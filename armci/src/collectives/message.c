#if HAVE_CONFIG_H
#   include "config.h"
#endif

/* $Id: message.c,v 1.58.6.4 2007-04-24 10:08:26 vinod Exp $ */
#if defined(TCGMSG)
#   include <sndrcv.h>
static void tcg_brdcst(long type, void *buf, long lenbuf, long originator)
{
    long atype = type;
    long alenbuf = lenbuf;
    long aoriginator = originator;

    BRDCST_(&atype, buf, &alenbuf, &aoriginator);
}

static void tcg_synch(long type)
{
    long atype = type;

    SYNCH_(&atype);
}

static void tcg_snd(long type, void *buf, long lenbuf, long node, long sync)
{
    long atype = type;
    long alenbuf = lenbuf;
    long anode = node;
    long async = sync;

    SND_(&atype, buf, &alenbuf, &anode, &async);
}

static void tcg_rcv(long type, void *buf, long lenbuf, long *lenmes,
                long nodeselect, long *nodefrom, long sync)
{
    long atype = type;
    long alenbuf = lenbuf;
    long anodeselect = nodeselect;
    long async = sync;

    RCV_(&atype, buf, &alenbuf, lenmes, &anodeselect, nodefrom, &async);
}
#else
#   ifndef MSG_COMMS_MPI
#      define MSG_COMMS_MPI
#   endif
#   include <mpi.h>
#endif
#include "message.h"
#include "armcip.h"
#include "copy.h"
#if HAVE_STDIO_H
#   include <stdio.h>
#endif
#if HAVE_ASSERT_H
#   include <assert.h>
#endif
#ifdef _POSIX_PRIORITY_SCHEDULING
#  include <sched.h>
#endif
#include "armci.h"
#include "acc.h"

#define DEBUG_ 0
#if defined(SYSV) || defined(MMAP) ||defined (WIN32)
#    include "armci_shmem.h"
#endif

/* global operations are use buffer size of BUF_SIZE doubles */ 
#define BUF_SIZE  (4*2048)
#define INFO_BUF_SIZE  (BUF_SIZE*sizeof(BUF_SIZE) - sizeof(double))
#undef EMPTY
#define EMPTY 0
#define FULL 1

static double *work=NULL;
static long *lwork = NULL;
static long long *llwork = NULL;
static int *iwork = NULL;
static float *fwork = NULL;
static int _armci_gop_init=0;   /* tells us if we have a buffers allocated  */
static int _armci_gop_shmem =0; /* tells us to use shared memory for gops */
extern void armci_util_wait_int(volatile int *, int , int );
static int empty=EMPTY,full=FULL;
#if defined(SYSV) || defined(MMAP) || defined(WIN32)
static void **ptr_arr=NULL;
#endif

typedef struct {
        union {
           volatile int flag;
           double dummy[16];
        }a;
        union {
           volatile int flag;
           double dummy[16];
        }b;
        double array[BUF_SIZE];
} bufstruct;

static bufstruct *_gop_buffer; 

#define GOP_BUF(p)  (_gop_buffer+((p)-armci_master))

/*\ macro to set a flag includes mem barrier to assure that flag is not set 
 *  before any outstanding writes complete 
\*/
#ifdef NEED_MEM_SYNC
#  ifdef AIX
#    define SET_SHM_FLAG(_flg,_val) _clear_lock((int *)(_flg),_val);
#  elif defined(MACX)
#    if defined(__GNUC__)
#    define SET_SHM_FLAG(_flg,_val)\
           *(_flg)=(_val);__asm__ __volatile__ ("isync" ::: "memory")
#    endif
#  endif
#endif

#ifndef SET_SHM_FLAG
#   define SET_SHM_FLAG(_flg,_val) *(_flg)=_val;
#endif



/*\
 *  Variables/structures for use in Barrier and for Binomial tree
\*/
#if HAVE_MATH_H
#   include <math.h>
#endif
int barr_switch;
static int LnB=0,powof2nodes,Lp2;
typedef struct {
        volatile int flag1;
        double dum[16];
        volatile int flag2;
} barrier_struct;
barrier_struct *_bar_buff;
#define BAR_BUF(p) (_bar_buff+((p)))
void **barr_snd_ptr,**barr_rcv_ptr;
int _armci_barrier_init=0;
int _armci_barrier_shmem=0;


#if 0
/*\
 * Tree generation code
\*/
static void _dfs_bintree_parse(int *idlist, int index, int max, int *result)
{
int left = (int)2*index+1;
int right = (int) 2*index+2;
static int pos=0;
int r_end,l_end;
   l_end=pos++;
   result[pos++]=idlist[index];
   if(left<max)
     _dfs_bintree_parse(idlist,left,max,result);
   r_end=pos++;
   result[l_end]=r_end;
   if(right<max)
     _dfs_bintree_parse(idlist,right,max,result);
   result[r_end]=pos;
   result[pos++]=idlist[index];
}

static int tree_unique_id=0;
int armci_msg_generate_tree(int *idlist,int idlen,int *id_tree,int TREE)
{
   
    /*for now everything is binary tree*/
    _dfs_bintree_parse(idlist,0,idlen,id_tree);
    return tree_unique_id++;
}
#endif

/*\
 *  *************************************************************
\*/
char *mp_group_name = "mp_working_group";

static void _allocate_mem_for_work(){
    work = (double *)malloc(sizeof(double)*BUF_SIZE);
    if(!work)armci_die("malloc in _allocate_mem_for_work failed",0);
    lwork = (long *)work; iwork = (int *)work; fwork = (float *)work;
    llwork = (long long *)work;
}


static void _deallocate_mem_for_work(){
    if (work) free(work);
    work = NULL; lwork = NULL; iwork = NULL; fwork = NULL; llwork = NULL;
}


/*\ allocate and initialize buffers used for collective communication
\*/
void armci_msg_gop_init()
{
/*work was a static global array of doubles. It has been changed to get
  memory from malloc because of a problem with cc on SV1
*/
    if(work==NULL)_allocate_mem_for_work();
#if defined(SYSV) || defined(MMAP) || defined(WIN32)
    if(ARMCI_Uses_shm()){
       char *tmp;
       int size = sizeof(bufstruct);
       int bytes = size * armci_clus_info[armci_clus_me].nslave;

       bytes += size*2; /* extra for brdcst */

       ptr_arr = (void**)malloc(armci_nproc*sizeof(void*));
       if(armci_me==armci_master) bytes += 128;
       else bytes=0;
       PARMCI_Malloc(ptr_arr, bytes);
       tmp = (char*)ptr_arr[armci_master];

       if(DEBUG_){
          printf("%d: allocate gop buffer %p %d\n",armci_me,tmp,bytes);
          fflush(stdout);
       }

       if(!tmp) armci_die("armci_msg_gop_init: shm malloc failed\n",size);
       _gop_buffer = ( bufstruct *) tmp;
       GOP_BUF(armci_me)->a.flag=EMPTY;   /* initially buffer is empty */
       GOP_BUF(armci_me)->b.flag=EMPTY;  /* initially buffer is empty */
       if(armci_me == armci_master ){
          GOP_BUF(armci_clus_last+1)->a.flag=EMPTY;/*initially buffer is empty*/
          GOP_BUF(armci_clus_last+2)->a.flag=EMPTY;/*initially buffer is empty*/
          GOP_BUF(armci_clus_last+1)->b.flag=EMPTY;/*initially buffer is empty*/
          GOP_BUF(armci_clus_last+2)->b.flag=EMPTY;/*initially buffer is empty*/
       }
       _armci_gop_shmem = 1;
     }
#endif
    /*stuff needed for barrier and binomial bcast/reduce*/
    _armci_gop_init=1;
}


void armci_msg_gop_finalize() 
{ 
#if defined(SYSV) || defined(MMAP) || defined(WIN32) 
    if(ARMCI_Uses_shm()){
        PARMCI_Free(ptr_arr[armci_me]); 
        free(ptr_arr); 
    }
#endif 
    _deallocate_mem_for_work(); 
} 


void cpu_yield()
{
#if defined(SYSV) || defined(MMAP) || defined(WIN32)
#if defined(WIN32)
               Sleep(1);
#elif defined(_POSIX_PRIORITY_SCHEDULING)
               sched_yield();
#else
               usleep(1);
#endif
#endif
}

/*\ busy wait 
 *  n represents number of time delay units   
 *  notused is useful to fool compiler by passing address of sensitive variable 
\*/
#define DUMMY_INIT 1.0001
double _armci_dummy_work=DUMMY_INIT;
void armci_util_spin(int n, void *notused)
{
int i;
    for(i=0; i<n; i++)
        if(armci_msg_me()>-1)  _armci_dummy_work *=DUMMY_INIT;
    if(_armci_dummy_work>(double)armci_msg_nproc())_armci_dummy_work=DUMMY_INIT;
}


/***************************Barrier Code*************************************/

void armci_msg_barr_init(){
#if defined(SYSV) || defined(MMAP) || defined(WIN32)
    int size=sizeof(barrier_struct)*armci_clus_info[armci_clus_me].nslave;
    char *tmp;
    void **ptr_arr;
    barr_switch=0;
    /*First allocate space for flags*/

    ptr_arr = (void**)malloc(armci_nproc*sizeof(void*));
    if(armci_me==armci_master) size = size+128;
    else size=0;
    PARMCI_Malloc(ptr_arr, size);
    tmp = (char*)ptr_arr[armci_master];
    size=2*sizeof(int);

    if(!tmp)armci_die("allocate barr shm failed",0);
    _bar_buff=(barrier_struct *)tmp;

    SET_SHM_FLAG(&(BAR_BUF(armci_me-armci_master)->flag1),empty);
    SET_SHM_FLAG(&(BAR_BUF(armci_me-armci_master)->flag2),empty);

    /*allocate memory to send/rcv data*/
    barr_snd_ptr = (void **)malloc(sizeof(void *)*armci_nproc);
    barr_rcv_ptr = (void **)malloc(sizeof(void *)*armci_nproc);

    if(PARMCI_Malloc(barr_snd_ptr,size))armci_die("malloc barr_init failed",0);
    if(PARMCI_Malloc(barr_rcv_ptr,size))armci_die("malloc barr_init failed",0);
    if(barr_rcv_ptr[armci_me]==NULL || barr_snd_ptr[armci_me]==NULL)
       armci_die("problems in malloc barr_init",0);

    /*we have to figure if we have power of ,two nodes*/
    powof2nodes=1;
    LnB = (int)floor(log(armci_nclus)/log(2))+1;
    if(pow(2,LnB-1)<armci_nclus){powof2nodes=0;}
    /*Lp2 is the largest pow-of-2 less than or equal to nclus(num of nodes)*/
    Lp2 = (int)pow(2,LnB);
    _armci_barrier_shmem = 1;
#endif
    _armci_barrier_init = 1;
}

/*\
 *armci barrier: implemented as a recursive doubling based pairwise exchange
 *algorithm with SMP barrier inside a node and msg_snd/rcv between the nodes.
 *NOTE::code for power or two nodes and non power of two nodes can be combined.
\*/

void parmci_msg_barrier()
{
#if defined(MSG_COMMS_MPI)
  MPI_Barrier(ARMCI_COMM_WORLD);
#else
  tcg_synch(ARMCI_TAG);
#endif
}
/***********************End Barrier Code*************************************/


void armci_msg_init(int *argc, char ***argv)
{
#if defined(TCGMSG)
    if (!TCGREADY_()) {
        tcgi_pbegin(*argc,*argv);
    }
#elif defined(MSG_COMMS_MPI)
    int flag=0;
    MPI_Initialized(&flag);
    if (!flag) {
#   if defined(MPI_MT)
        int provided;
        MPI_Init_thread(argc, argv, MPI_THREAD_MULTIPLE, &provided);
#   else
        MPI_Init(argc, argv);
#   endif
    }
    if (!PARMCI_Initialized()) {
        MPI_Comm_dup(MPI_COMM_WORLD, &ARMCI_COMM_WORLD);
    }
#endif
}

#ifdef MSG_COMMS_MPI
void armci_msg_init_comm(MPI_Comm comm)
{
    if (!PARMCI_Initialized()) {
        MPI_Comm_dup(comm, &ARMCI_COMM_WORLD);
    }
}
#endif


int armci_msg_me()
{
#if defined(MSG_COMMS_MPI)
    static int counter = 0;
    if (counter == 0) {
        int me;
        MPI_Comm_rank(ARMCI_COMM_WORLD, &me);
        armci_me = me;   
        counter = 1;
    }
    return armci_me;

#else
    return (int)NODEID_();
#endif
}


int armci_msg_nproc()
{
#if defined(MSG_COMMS_MPI)
    static int counter = 0;
    if (counter == 0) {
        int nproc;
        MPI_Comm_size(ARMCI_COMM_WORLD, &nproc);
        armci_nproc = nproc;
        counter = 1;
    }
    return armci_nproc;
#else
    return (int)NNODES_();
#endif
}

double armci_timer()
{
#if defined(MSG_COMMS_MPI)
    return MPI_Wtime();
#else
    return TCGTIME_();
#endif
}


void armci_msg_abort(int code)
{
#if defined(MSG_COMMS_MPI)
#    ifndef BROKEN_MPI_ABORT
    MPI_Abort(ARMCI_COMM_WORLD,code);
#    endif
#else
    Error("ARMCI aborting",(long)code);
#endif
    fprintf(stderr,"%d:aborting\n",armci_me);
    /* trap for broken abort in message passing libs */
    _exit(1);
}

void armci_msg_finalize()
{
#if defined(TCGMSG)
    PEND_();
#elif defined(MSG_COMMS_MPI)
    MPI_Finalize();
#endif
}

void armci_msg_bintree(int scope, int* Root, int *Up, int *Left, int *Right)
{
int root, up, left, right, index, nproc;
    if(scope == SCOPE_NODE){
       root  = armci_clus_info[armci_clus_me].master;
       nproc = armci_clus_info[armci_clus_me].nslave;
       index = armci_me - root;
       up    = (index-1)/2 + root; if( up < root) up = -1;
       left  = 2*index + 1 + root; if(left >= root+nproc) left = -1;
       right = 2*index + 2 + root; if(right >= root+nproc)right = -1;
    }else if(scope ==SCOPE_MASTERS){
       root  = armci_clus_info[0].master;
       nproc = armci_nclus;
       if(armci_me != armci_master){up = -1; left = -1; right = -1; }
       else{
               index = armci_clus_me - root;
               up    = (index-1)/2 + root;
               up = ( up < root)? -1: armci_clus_info[up].master;
               left  = 2*index + 1 + root;
               left = ( left >= root+nproc)? -1: armci_clus_info[left].master;
               right = 2*index + 2 + root;
               right =( right >= root+nproc)? -1: armci_clus_info[right].master;
       }
    }else{
       root  = 0;
       nproc = armci_nproc;
       index = armci_me - root;
       up    = (index-1)/2 + root; if( up < root) up = -1;
       left  = 2*index + 1 + root; if(left >= root+nproc) left = -1;
       right = 2*index + 2 + root; if(right >= root+nproc)right = -1;
    }

    *Up = up;
    *Left = left;
    *Right = right;
    *Root = root;
}

/*\ root broadcasts to everyone else
\*/
void armci_msg_bcast_scope(int scope, void *buf, int len, int root)
{
    int up, left, right, Root;

    if(!buf)armci_die("armci_msg_bcast: NULL pointer", len);
    armci_msg_bintree(scope, &Root, &up, &left, &right);

    if(root !=Root){
        if(armci_me == root) armci_msg_snd(ARMCI_TAG, buf,len, Root);
        if(armci_me ==Root) armci_msg_rcv(ARMCI_TAG, buf, len, NULL, root);
    }

    /* printf("%d: scope=%d left=%d right=%d up=%d\n",armci_me, scope,
    left, right, up);*/

    if(armci_me != Root && up!=-1) armci_msg_rcv(ARMCI_TAG, buf, len, NULL, up);
    if (left > -1)  armci_msg_snd(ARMCI_TAG, buf, len, left);
    if (right > -1) armci_msg_snd(ARMCI_TAG, buf, len, right);
}




/*\ shared memory based broadcast for a single SMP node
\*/
void armci_smp_bcast(void *x, int n , int root)
{
int ndo, len,i, bufsize = BUF_SIZE*sizeof(double);
static int bufid=1;

  if(armci_clus_info[armci_clus_me].nslave<2) return; /* nothing to do */

  if(!x)armci_die("armci_msg_bcast: NULL pointer", n);
  
  /* enable or balance pipeline for messages comparable to bufsize */
  if((n>bufsize/2) && (n <(2*bufsize-64))){
      bufsize = n/2; bufsize>>=3; bufsize<<=3; 
  }

  while ((ndo = (n<=bufsize) ? n : bufsize)) {
    len = ndo;
          
    if(armci_me==root){

       /* wait for the flag protecting the buffer to clear */
       armci_util_wait_int(&(GOP_BUF(armci_clus_last+bufid)->a.flag),EMPTY,100);
       SET_SHM_FLAG(&(GOP_BUF(armci_clus_last+bufid)->a.flag),full);
#if 0     
       for(i=armci_clus_first; i <= armci_clus_last; i++)
           if(i!=root)armci_util_wait_int(&GOP_BUF(i)->b.flag, EMPTY, 100);
       armci_copy(x,GOP_BUF(armci_clus_last+bufid+1)->array,len);
       for(i=armci_clus_first; i <= armci_clus_last; i++)
           if(i!=root) GOP_BUF(i)->b.flag=FULL;
#else   
       armci_copy(x,GOP_BUF(armci_clus_last+bufid)->array,len);
       for(i=armci_clus_first; i <= armci_clus_last; i++)
           if(i!=root){ 
                  armci_util_wait_int(&GOP_BUF(i)->b.flag, EMPTY, 100);
                  SET_SHM_FLAG(&(GOP_BUF(i)->b.flag),full);
           } 
#endif            
    }else{     
           armci_util_wait_int(&GOP_BUF(armci_me)->b.flag, FULL, 100);
           armci_copy(GOP_BUF(armci_clus_last+bufid)->array,x,len);
           SET_SHM_FLAG(&(GOP_BUF(armci_me)->b.flag),empty);
    }

    n -=ndo;
    x = len + (char*)x;
       
    bufid = (bufid)%2 +1;

    /* since root waited for everybody to check in the previous buffer is free*/
    if(armci_me==root){
          SET_SHM_FLAG(&(GOP_BUF(armci_clus_last+bufid)->a.flag),empty);
    }
  }    
}        



/*\ shared memory based broadcast for a single SMP node out of shmem buffer
\*/
void armci_smp_buf_bcast(void *x, int n, int root, void *shmbuf )
{
int i, nslave = armci_clus_info[armci_clus_me].nslave;

    if(nslave<2){
        armci_copy(shmbuf,x,n);
        return; /* nothing to do */
    }
    if(!x)armci_die("armci_msg_bcast: NULL pointer", n);
    if(!shmbuf)armci_die("armci_msg_bcast: NULL pointer", n);
 
    if(armci_me==root){
          /* notify others that the data in buffer is ready */
          for(i=armci_clus_first; i <= armci_clus_last; i++)
                if(i!=root){
                  armci_util_wait_int(&GOP_BUF(i)->b.flag, EMPTY, 100);
                  GOP_BUF(i)->b.flag=FULL;
                }
          /* root also needs to copy */
          armci_copy(shmbuf,x,n);
          /* wait until everybody is finished -- can reclaim buffer */
          for(i=armci_clus_first; i <= armci_clus_last; i++)
              if(i!=root)armci_util_wait_int(&GOP_BUF(i)->b.flag, EMPTY,100000);
          
    }else{
           /* spin until data in buffer is ready */
           armci_util_wait_int(&GOP_BUF(armci_me)->b.flag , FULL, 100000);
           armci_copy(shmbuf,x,n);       /* copy data */
           GOP_BUF(armci_me)->b.flag  = EMPTY; /* indicate we are done */
    }
}

void _armci_msg_binomial_bcast(void *buf, int len, int root){
    int Root = armci_master;
    int nslave = armci_clus_info[armci_clus_me].nslave;
    int i,next_node,next;
/*    int my_rank,root_rank,next_rank; */
    /* inter-node operation between masters */
    if(root !=armci_clus_info[0].master){
       Root = armci_clus_info[0].master;
       if(armci_me == root) armci_msg_snd(ARMCI_TAG, buf,len, Root);
       if(armci_me ==Root) armci_msg_rcv(ARMCI_TAG, buf, len, NULL, root);
       root = Root;
       Root = armci_master;
    }
    if(armci_nclus>1 &&armci_me==armci_master){/*the internode bcast, first*/
       /*first do the recv*/
       int rcv_proc=armci_clus_me,flag=1,diff=1;
       if(armci_me!=root){
         while(!(rcv_proc & flag)){
           diff=diff<<1;
           flag=flag<<1;
         }
         rcv_proc = armci_clus_info[armci_clus_me-diff].master;
         armci_msg_rcv(ARMCI_TAG, buf,len,NULL,rcv_proc);
         /*printf("\n%d: recv from %d \n",armci_me,rcv_proc);fflush(stdout);*/
       }    
       else
         diff = Lp2;

       /*printf("\n%d: %d diff>>1 = %d\n",armci_me,Lp2,diff>>1);*/
       for(i=diff>>1;i>=1;i=i>>1){
         next=i^armci_clus_me;
         if(next>=0 && next<armci_nclus){
           next_node = armci_clus_info[next].master;
           armci_msg_snd(ARMCI_TAG, buf,len,next_node);
           /*printf("\n%d: send to %d \n",armci_me,next_node);fflush(stdout);*/
         }
       }
    }
    else  Root = root;
    if(_armci_gop_shmem && nslave<33)
       armci_smp_bcast(buf, len, Root);
    else
       armci_msg_bcast_scope(SCOPE_NODE, buf, len, Root);
}



#ifndef armci_msg_bcast
/*\ SMP-aware global broadcast routine
\*/
void armci_msg_bcast(void *buf, int len, int root)
{
int Root = armci_master;
int nslave = armci_clus_info[armci_clus_me].nslave;

#if defined(MPI_SPAWN) || defined(MPI_MT)
    armci_msg_bcast_scope(SCOPE_ALL, (buf), (len), (root));
    return;
#endif
    /* inter-node operation between masters */
    if(armci_nclus>1)armci_msg_bcast_scope(SCOPE_MASTERS, buf, len, root);
    else  Root = root;

    /* intra-node operation */
#if 1
    if(_armci_gop_shmem && nslave<33)
       armci_smp_bcast(buf, len, Root);
    else
#endif
    armci_msg_bcast_scope(SCOPE_NODE, buf, len, Root);
}
#endif



void armci_msg_brdcst(void* buffer, int len, int root)
{
   if(!buffer)armci_die("armci_msg_brdcast: NULL pointer", len);

#if defined(MSG_COMMS_MPI)
   MPI_Bcast(buffer, len, MPI_CHAR, root, ARMCI_COMM_WORLD);
#else
   {
      long ttag=ARMCI_TAG, llen=len, rroot=root;
      tcg_brdcst(ttag, buffer, llen, rroot);
   }
#endif
}


void armci_msg_snd(int tag, void* buffer, int len, int to)
{
#ifdef MSG_COMMS_MPI
  MPI_Send(buffer, len, MPI_CHAR, to, tag, ARMCI_COMM_WORLD);
#else
  {
    long ttag=tag, llen=len, tto=to, block=1;
    tcg_snd(ttag, buffer, llen, tto, block);
  }
#endif
}


/*\ receive message of specified tag from proc and get its len if msglen!=NULL 
\*/
void armci_msg_rcv(int tag, void* buffer, int buflen, int *msglen, int from)
{
#ifdef MSG_COMMS_MPI
    MPI_Status status;
    MPI_Recv(buffer, buflen, MPI_CHAR, from, tag, ARMCI_COMM_WORLD, &status);
    if(msglen) MPI_Get_count(&status, MPI_CHAR, msglen);
#else
    long ttag=tag, llen=buflen, mlen, ffrom=from, sender, block=1;
    tcg_rcv(ttag, buffer, llen, &mlen, ffrom, &sender, block);
    if(msglen)*msglen = (int)mlen;
#endif
}


int armci_msg_rcvany(int tag, void* buffer, int buflen, int *msglen)
{
#if defined(MSG_COMMS_MPI)
      int ierr;
      MPI_Status status;

      ierr = MPI_Recv(buffer, buflen, MPI_CHAR, MPI_ANY_SOURCE, tag,
             ARMCI_COMM_WORLD, &status);
      if(ierr != MPI_SUCCESS) armci_die("armci_msg_rcvany: Recv failed ", tag);

      if(msglen)if(MPI_SUCCESS!=MPI_Get_count(&status, MPI_CHAR, msglen))
                       armci_die("armci_msg_rcvany: count failed ", tag);
      return (int)status.MPI_SOURCE;
#  else
      long ttag=tag, llen=buflen, mlen, ffrom=-1, sender, block=1;
      tcg_rcv(ttag, buffer, llen, &mlen, ffrom, &sender, block);
      if(msglen)*msglen = (int)mlen;
      return (int)sender;
#  endif
}


/*\ cluster master broadcasts to everyone else in the same cluster
\*/
void armci_msg_clus_brdcst(void *buf, int len)
{
int root, up, left, right;
int tag=ARMCI_TAG, lenmes;

    armci_msg_bintree(SCOPE_NODE, &root, &up, &left, &right);
    if(armci_me != root) armci_msg_rcv(tag, buf, len, &lenmes, up);
    if (left > -1)  armci_msg_snd(tag, buf, len, left);
    if (right > -1) armci_msg_snd(tag, buf, len, right);
}


/*\ reduce operation for long 
\*/
static void ldoop(int n, char *op, long *x, long* work)
{
  if (strncmp(op,"+",1) == 0)
    while(n--)
      *x++ += *work++;
  else if (strncmp(op,"*",1) == 0)
    while(n--)
      *x++ *= *work++;
  else if (strncmp(op,"max",3) == 0)
    while(n--) {
      *x = ARMCI_MAX(*x, *work);
      x++; work++;
    }
  else if (strncmp(op,"min",3) == 0)
    while(n--) {
      *x = ARMCI_MIN(*x, *work);
      x++; work++;
    }
  else if (strncmp(op,"absmax",6) == 0)
    while(n--) {
      register long x1 = ARMCI_ABS(*x), x2 = ARMCI_ABS(*work);
      *x = ARMCI_MAX(x1, x2);
      x++; work++;
    }
  else if (strncmp(op,"absmin",6) == 0)
    while(n--) {
      register long x1 = ARMCI_ABS(*x), x2 = ARMCI_ABS(*work);
      *x = ARMCI_MIN(x1, x2);
      x++; work++;
    }
  else if (strncmp(op,"or",2) == 0)
    while(n--) {
      *x |= *work;
      x++; work++;
    }
  /* these are new */
  else if ((strncmp(op, "&&", 2) == 0) || (strncmp(op, "land", 4) == 0)) {
    while(n--) {
      *x = *x && *work;
      x++; work++;
    }
  }
  else if ((strncmp(op, "||", 2) == 0) || (strncmp(op, "lor", 3) == 0)) {
    while(n--) {
      *x = *x || *work;
      x++; work++;
    }
  }
  else if ((strncmp(op, "&", 1) == 0) || (strncmp(op, "band", 4) == 0)) {
    while(n--) {
      *x &= *work;
      x++; work++;
    }
  }
  else if ((strncmp(op, "|", 1) == 0) || (strncmp(op, "bor", 3) == 0)) {
    while(n--) {
      *x |= *work;
      x++; work++;
    }
  }
  else
    armci_die("ldoop: unknown operation requested", n);
}

/*\ reduce operation for long x= op(work,work2)
\*/
static void ldoop2(int n, char *op, long *x, long* work, long* work2)
{
  if (strncmp(op,"+",1) == 0)
    while(n--)
      *x++ = *work++ + *work2++;
  else if (strncmp(op,"*",1) == 0)
    while(n--)
      *x++ = *work++ *  *work2++;
  else if (strncmp(op,"max",3) == 0)
    while(n--) {
      *x = ARMCI_MAX(*work2, *work);
      x++; work++; work2++;
    }
  else if (strncmp(op,"min",3) == 0)
    while(n--) {
      *x = ARMCI_MIN(*work2, *work);
      x++; work++; work2++;
    }
  else if (strncmp(op,"absmax",6) == 0)
    while(n--) {
      register long x1 = ARMCI_ABS(*work), x2 = ARMCI_ABS(*work2);
      *x = ARMCI_MAX(x1, x2);
      x++; work++; work2++;
    }
  else if (strncmp(op,"absmin",6) == 0)
    while(n--) {
      register long x1 = ARMCI_ABS(*work), x2 = ARMCI_ABS(*work2);
      *x = ARMCI_MIN(x1, x2);
      x++; work++; work2++;
    }
  else if (strncmp(op,"or",2) == 0)
    while(n--) {
      *x = *work | *work2;
      x++; work++; work2++;
    }
  /* these are new */
  else if ((strncmp(op, "&&", 2) == 0) || (strncmp(op, "land", 4) == 0)) {
    while(n--) {
      *x = *work && *work2;
      x++; work++; work2++;
    }
  }
  else if ((strncmp(op, "||", 2) == 0) || (strncmp(op, "lor", 3) == 0)) {
    while(n--) {
      *x = *work || *work2;
      x++; work++; work2++;
    }
  }
  else if ((strncmp(op, "&", 1) == 0) || (strncmp(op, "band", 4) == 0)) {
    while(n--) {
      *x = *work & *work2;
      x++; work++; work2++;
    }
  }
  else if ((strncmp(op, "|", 1) == 0) || (strncmp(op, "bor", 3) == 0)) {
    while(n--) {
      *x = *work | *work2;
      x++; work++; work2++;
    }
  }
  else
    armci_die("ldoop2: unknown operation requested", n);
}

/*\ reduce operation for long long
\*/
static void lldoop(int n, char *op, long long *x, long long* work)
{
  if (strncmp(op,"+",1) == 0)
    while(n--)
      *x++ += *work++;
  else if (strncmp(op,"*",1) == 0)
    while(n--)
      *x++ *= *work++;
  else if (strncmp(op,"max",3) == 0)
    while(n--) {
      *x = ARMCI_MAX(*x, *work);
      x++; work++;
    }
  else if (strncmp(op,"min",3) == 0)
    while(n--) {
      *x = ARMCI_MIN(*x, *work);
      x++; work++;
    }
  else if (strncmp(op,"absmax",6) == 0)
    while(n--) {
      register long long x1 = ARMCI_ABS(*x), x2 = ARMCI_ABS(*work);
      *x = ARMCI_MAX(x1, x2);
      x++; work++;
    }
  else if (strncmp(op,"absmin",6) == 0)
    while(n--) {
      register long long x1 = ARMCI_ABS(*x), x2 = ARMCI_ABS(*work);
      *x = ARMCI_MIN(x1, x2);
      x++; work++;
    }
  else if (strncmp(op,"or",2) == 0)
    while(n--) {
      *x |= *work;
      x++; work++;
    }
  /* these are new */
  else if ((strncmp(op, "&&", 2) == 0) || (strncmp(op, "land", 4) == 0)) {
    while(n--) {
      *x = *x && *work;
      x++; work++;
    }
  }
  else if ((strncmp(op, "||", 2) == 0) || (strncmp(op, "lor", 3) == 0)) {
    while(n--) {
      *x = *x || *work;
      x++; work++;
    }
  }
  else if ((strncmp(op, "&", 1) == 0) || (strncmp(op, "band", 4) == 0)) {
    while(n--) {
      *x &= *work;
      x++; work++;
    }
  }
  else if ((strncmp(op, "|", 1) == 0) || (strncmp(op, "bor", 3) == 0)) {
    while(n--) {
      *x |= *work;
      x++; work++;
    }
  }
  else
    armci_die("lldoop: unknown operation requested", n);
}

/*\ reduce operation for long long x= op(work,work2)
\*/
static void lldoop2(int n, char *op, long long *x, long long* work,
                    long long* work2)
{
  if (strncmp(op,"+",1) == 0)
    while(n--)
      *x++ = *work++ + *work2++;
  else if (strncmp(op,"*",1) == 0)
    while(n--)
      *x++ = *work++ *  *work2++;
  else if (strncmp(op,"max",3) == 0)
    while(n--) {
      *x = ARMCI_MAX(*work2, *work);
      x++; work++; work2++;
    }
  else if (strncmp(op,"min",3) == 0)
    while(n--) {
      *x = ARMCI_MIN(*work2, *work);
      x++; work++; work2++;
    }
  else if (strncmp(op,"absmax",6) == 0)
    while(n--) {
      register long long x1 = ARMCI_ABS(*work), x2 = ARMCI_ABS(*work2);
      *x = ARMCI_MAX(x1, x2);
      x++; work++; work2++;
    }
  else if (strncmp(op,"absmin",6) == 0)
    while(n--) {
      register long long x1 = ARMCI_ABS(*work), x2 = ARMCI_ABS(*work2);
      *x = ARMCI_MIN(x1, x2);
      x++; work++; work2++;
    }
  else if (strncmp(op,"or",2) == 0)
    while(n--) {
      *x = *work | *work2;
      x++; work++; work2++;
    }
  /* these are new */
  else if ((strncmp(op, "&&", 2) == 0) || (strncmp(op, "land", 4) == 0)) {
    while(n--) {
      *x = *work && *work2;
      x++; work++; work2++;
    }
  }
  else if ((strncmp(op, "||", 2) == 0) || (strncmp(op, "lor", 3) == 0)) {
    while(n--) {
      *x = *work || *work2;
      x++; work++; work2++;
    }
  }
  else if ((strncmp(op, "&", 1) == 0) || (strncmp(op, "band", 4) == 0)) {
    while(n--) {
      *x = *work & *work2;
      x++; work++; work2++;
    }
  }
  else if ((strncmp(op, "|", 1) == 0) || (strncmp(op, "bor", 3) == 0)) {
    while(n--) {
      *x = *work | *work2;
      x++; work++; work2++;
    }
  }
  else
    armci_die("ldoop2: unknown operation requested", n);
}

/*\ reduce operation for int 
\*/
static void idoop(int n, char *op, int *x, int* work)
{
  if (strncmp(op,"+",1) == 0)
    while(n--)
      *x++ += *work++;
  else if (strncmp(op,"*",1) == 0)
    while(n--)
      *x++ *= *work++;
  else if (strncmp(op,"max",3) == 0)
    while(n--) {
      *x = ARMCI_MAX(*x, *work);
      x++; work++;
    }
  else if (strncmp(op,"min",3) == 0)
    while(n--) {
      *x = ARMCI_MIN(*x, *work);
      x++; work++;
    }
  else if (strncmp(op,"absmax",6) == 0)
    while(n--) {
      register int x1 = ARMCI_ABS(*x), x2 = ARMCI_ABS(*work);
      *x = ARMCI_MAX(x1, x2);
      x++; work++;
    }
  else if (strncmp(op,"absmin",6) == 0)
    while(n--) {
      register int x1 = ARMCI_ABS(*x), x2 = ARMCI_ABS(*work);
      *x = ARMCI_MIN(x1, x2);
      x++; work++;
    }
  else if (strncmp(op,"or",2) == 0)
    while(n--) {
      *x |= *work;
      x++; work++;
    }
  /* these are new */
  else if ((strncmp(op, "&&", 2) == 0) || (strncmp(op, "land", 4) == 0)) {
    while(n--) {
      *x = *x && *work;
      x++; work++;
    }
  }
  else if ((strncmp(op, "||", 2) == 0) || (strncmp(op, "lor", 3) == 0)) {
    while(n--) {
      *x = *x || *work;
      x++; work++;
    }
  }
  else if ((strncmp(op, "&", 1) == 0) || (strncmp(op, "band", 4) == 0)) {
    while(n--) {
      *x &= *work;
      x++; work++;
    }
  }
  else if ((strncmp(op, "|", 1) == 0) || (strncmp(op, "bor", 3) == 0)) {
    while(n--) {
      *x |= *work;
      x++; work++;
    }
  }
  else
    armci_die("idoop: unknown operation requested", n);
}

/*\ reduce operation for int x= op(work,work2)
\*/
static void idoop2(int n, char *op, int *x, int* work, int* work2)
{
  if (strncmp(op,"+",1) == 0)
    while(n--)
      *x++ = *work++ + *work2++;
  else if (strncmp(op,"*",1) == 0)
    while(n--)
      *x++ = *work++ *  *work2++;
  else if (strncmp(op,"max",3) == 0)
    while(n--) {
      *x = ARMCI_MAX(*work2, *work);
      x++; work++; work2++;
    }
  else if (strncmp(op,"min",3) == 0)
    while(n--) {
      *x = ARMCI_MIN(*work2, *work);
      x++; work++; work2++;
    }
  else if (strncmp(op,"absmax",6) == 0)
    while(n--) {
      register int x1 = ARMCI_ABS(*work), x2 = ARMCI_ABS(*work2);
      *x = ARMCI_MAX(x1, x2);
      x++; work++; work2++;
    }
  else if (strncmp(op,"absmin",6) == 0)
    while(n--) {
      register int x1 = ARMCI_ABS(*work), x2 = ARMCI_ABS(*work2);
      *x = ARMCI_MIN(x1, x2);
      x++; work++; work2++;
    }
  else if (strncmp(op,"or",2) == 0)
    while(n--) {
      *x = *work | *work2;
      x++; work++; work2++;
    }
  /* these are new */
  else if ((strncmp(op, "&&", 2) == 0) || (strncmp(op, "land", 4) == 0)) {
    while(n--) {
      *x = *work && *work2;
      x++; work++; work2++;
    }
  }
  else if ((strncmp(op, "||", 2) == 0) || (strncmp(op, "lor", 3) == 0)) {
    while(n--) {
      *x = *work || *work2;
      x++; work++; work2++;
    }
  }
  else if ((strncmp(op, "&", 1) == 0) || (strncmp(op, "band", 4) == 0)) {
    while(n--) {
      *x = *work & *work2;
      x++; work++; work2++;
    }
  }
  else if ((strncmp(op, "|", 1) == 0) || (strncmp(op, "bor", 3) == 0)) {
    while(n--) {
      *x = *work | *work2;
      x++; work++; work2++;
    }
  }
  else
    armci_die("idoop2: unknown operation requested", n);
}

/*\ reduce operation for double 
\*/
static void ddoop(int n, char* op, double* x, double* work)
{
  if (strncmp(op,"+",1) == 0){
    if(n>63) FORT_DADD(&n,x,work);
    else while(n--) *x++ += *work++;
  }else if (strncmp(op,"*",1) == 0){
    if(n>63) FORT_DMULT(&n,x,work);
    else while(n--) *x++ *= *work++;
  }else if (strncmp(op,"max",3) == 0)
    while(n--) {
      *x = ARMCI_MAX(*x, *work);
      x++; work++;
    }
  else if (strncmp(op,"min",3) == 0)
    while(n--) {
      *x = ARMCI_MIN(*x, *work);
      x++; work++;
    }
  else if (strncmp(op,"absmax",6) == 0)
    while(n--) {
      register double x1 = ARMCI_ABS(*x), x2 = ARMCI_ABS(*work);
      *x = ARMCI_MAX(x1, x2);
      x++; work++;
    }
  else if (strncmp(op,"absmin",6) == 0)
    while(n--) {
      register double x1 = ARMCI_ABS(*x), x2 = ARMCI_ABS(*work);
      *x = ARMCI_MIN(x1, x2);
      x++; work++;
    }
  else
    armci_die("ddoop: unknown operation requested", n);
}

/*\ reduce operation for double x= op(work,work2)
\*/
static void ddoop2(int n, char *op, double *x, double* work, double* work2)
{
  if (strncmp(op,"+",1) == 0){
    if(n>63) FORT_DADD2(&n,x,work,work2);
    else while(n--) *x++ = *work++ + *work2++;
  }else if (strncmp(op,"*",1) == 0){
    if(n>63) FORT_DMULT2(&n,x,work,work2);
    while(n--) *x++ = *work++ *  *work2++;
  }else if (strncmp(op,"max",3) == 0)
    while(n--) {
      *x = ARMCI_MAX(*work2, *work);
      x++; work++; work2++;
    }
  else if (strncmp(op,"min",3) == 0)
    while(n--) {
      *x = ARMCI_MIN(*work2, *work);
      x++; work++; work2++;
    }
  else if (strncmp(op,"absmax",6) == 0)
    while(n--) {
      register double x1 = ARMCI_ABS(*work), x2 = ARMCI_ABS(*work2);
      *x = ARMCI_MAX(x1, x2);
      x++; work++; work2++;
    }
  else if (strncmp(op,"absmin",6) == 0)
    while(n--) {
      register double x1 = ARMCI_ABS(*work), x2 = ARMCI_ABS(*work2);
      *x = ARMCI_MIN(x1, x2);
      x++; work++; work2++;
    }
  else
    armci_die("ddoop2: unknown operation requested", n);
}


/*\ reduce operation for float 
\*/
static void fdoop(int n, char* op, float* x, float* work)
{
  if (strncmp(op,"+",1) == 0)
    while(n--)
      *x++ += *work++;
  else if (strncmp(op,"*",1) == 0)
    while(n--)
      *x++ *= *work++;
  else if (strncmp(op,"max",3) == 0)
    while(n--) {
      *x = ARMCI_MAX(*x, *work);
      x++; work++;
    }
  else if (strncmp(op,"min",3) == 0)
    while(n--) {
      *x = ARMCI_MIN(*x, *work);
      x++; work++;
    }
  else if (strncmp(op,"absmax",6) == 0)
    while(n--) {
      register float x1 = ARMCI_ABS(*x), x2 = ARMCI_ABS(*work);
      *x = ARMCI_MAX(x1, x2);
      x++; work++;
    }
  else if (strncmp(op,"absmin",6) == 0)
    while(n--) {
      register float x1 = ARMCI_ABS(*x), x2 = ARMCI_ABS(*work);
      *x = ARMCI_MIN(x1, x2);
      x++; work++;
    }
  else
    armci_die("fdoop: unknown operation requested", n);
}

/*\ reduce operation for float x= op(work,work2)
\*/
static void fdoop2(int n, char *op, float *x, float* work, float* work2)
{
  if (strncmp(op,"+",1) == 0)
    while(n--)
      *x++ = *work++ + *work2++;
  else if (strncmp(op,"*",1) == 0)
    while(n--)
      *x++ = *work++ *  *work2++;
  else if (strncmp(op,"max",3) == 0)
    while(n--) {
      *x = ARMCI_MAX(*work2, *work);
      x++; work++; work2++;
    }
  else if (strncmp(op,"min",3) == 0)
    while(n--) {
      *x = ARMCI_MIN(*work2, *work);
      x++; work++; work2++;
    }
  else if (strncmp(op,"absmax",6) == 0)
    while(n--) {
      register float x1 = ARMCI_ABS(*work), x2 = ARMCI_ABS(*work2);
      *x = ARMCI_MAX(x1, x2);
      x++; work++; work2++;
    }
  else if (strncmp(op,"absmin",6) == 0)
    while(n--) {
      register float x1 = ARMCI_ABS(*work), x2 = ARMCI_ABS(*work2);
      *x = ARMCI_MIN(x1, x2);
      x++; work++; work2++;
    }
  else
    armci_die("fdoop2: unknown operation requested", n);
}

/*\ combine array of longs/ints accross all processes
\*/
void armci_msg_gop_scope(int scope, void *x, int n, char* op, int type)
{
  int root, up, left, right, size;
  int tag=ARMCI_TAG;
  int ndo, len, lenmes, orign =n, ratio;
  void *origx =x;
  if(!x)armci_die("armci_msg_gop: NULL pointer", n);
  if(work==NULL)_allocate_mem_for_work();
  {
    armci_msg_bintree(scope, &root, &up, &left, &right);

    if(type==ARMCI_INT) size = sizeof(int);
    else if(type==ARMCI_LONG) size = sizeof(long);
    else if(type==ARMCI_LONG_LONG) size = sizeof(long long);
    else if(type==ARMCI_FLOAT) size = sizeof(float);
    else size = sizeof(double);

    ratio = sizeof(double)/size;

    while ((ndo = (n<=BUF_SIZE*ratio) ? n : BUF_SIZE*ratio)) {
         len = lenmes = ndo*size;

         if (left > -1) {
           armci_msg_rcv(tag, lwork, len, &lenmes, left);
           if(type==ARMCI_INT) idoop(ndo, op, (int*)x, iwork);
           else if(type==ARMCI_LONG) ldoop(ndo, op, (long*)x, lwork);
           else if(type==ARMCI_LONG_LONG) lldoop(ndo, op,(long long*)x,llwork);
	   else if(type==ARMCI_FLOAT) fdoop(ndo, op, (float*)x, fwork);
           else ddoop(ndo, op, (double*)x, work);
         }

         if (right > -1) {
           armci_msg_rcv(tag, lwork, len, &lenmes, right);
           if(type==ARMCI_INT) idoop(ndo, op, (int*)x, iwork);
           else if(type==ARMCI_LONG) ldoop(ndo, op, (long*)x, lwork);
           else if(type==ARMCI_LONG_LONG) lldoop(ndo, op,(long long*)x,llwork);
	   else if(type==ARMCI_FLOAT) fdoop(ndo, op, (float*)x, fwork);
           else ddoop(ndo, op, (double*)x, work);
         }
         if (armci_me != root && up!=-1) armci_msg_snd(tag, x, len, up);

         n -=ndo;
         x = len + (char*)x;
     }

     /* Now, root broadcasts the result down the binary tree */
     len = orign*size;
     armci_msg_bcast_scope(scope, origx, len, root);
  }
}


void armci_msg_reduce_scope(int scope, void *x, int n, char* op, int type)
{
int root, up, left, right, size;
int tag=ARMCI_TAG;
int ndo, len, lenmes, ratio;
    if(!x)armci_die("armci_msg_gop: NULL pointer", n);
    if(work==NULL)_allocate_mem_for_work();

    armci_msg_bintree(scope, &root, &up, &left, &right);

    if(type==ARMCI_INT) size = sizeof(int);
    else if(type==ARMCI_LONG) size = sizeof(long);
    else if(type==ARMCI_LONG_LONG) size = sizeof(long long);
    else if(type==ARMCI_FLOAT) size = sizeof(float);
    else size = sizeof(double);

    ratio = sizeof(double)/size;
   
    while ((ndo = (n<=BUF_SIZE*ratio) ? n : BUF_SIZE*ratio)) {
         len = lenmes = ndo*size;

         if (left > -1) {
           armci_msg_rcv(tag, lwork, len, &lenmes, left);
           if(type==ARMCI_INT) idoop(ndo, op, (int*)x, iwork);
           else if(type==ARMCI_LONG) ldoop(ndo, op, (long*)x, lwork);
           else if(type==ARMCI_LONG_LONG) lldoop(ndo, op,(long long*)x,llwork);
	   else if(type==ARMCI_FLOAT) fdoop(ndo, op, (float*)x, fwork);
           else ddoop(ndo, op, (double*)x, work);
         }

         if (right > -1) {
           armci_msg_rcv(tag, lwork, len, &lenmes, right);
           if(type==ARMCI_INT) idoop(ndo, op, (int*)x, iwork);
           else if(type==ARMCI_LONG) ldoop(ndo, op, (long*)x, lwork);
           else if(type==ARMCI_LONG_LONG) lldoop(ndo, op,(long long*)x,llwork);
	   else if(type==ARMCI_FLOAT) fdoop(ndo, op, (float*)x, fwork);
           else ddoop(ndo, op, (double*)x, work);
         }
         if (armci_me != root && up!=-1) armci_msg_snd(tag, x, len, up);

         n -=ndo;
         x = len + (char*)x;
     }
}

static void gop(int type, int ndo, char* op, void *x, void *work)
{
     if(type==ARMCI_INT) idoop(ndo, op, (int*)x, (int*)work);
     else if(type==ARMCI_LONG) ldoop(ndo, op, (long*)x, (long*)work);
     else if(type==ARMCI_LONG_LONG) lldoop(ndo, op, (long long*)x, (long long*)work);
     else if(type==ARMCI_FLOAT) fdoop(ndo, op, (float*)x, (float*)work);
     else ddoop(ndo, op, (double*)x, (double*)work);
}


static void gop2(int type, int ndo, char* op, void *x, void *work, void *work2)
{
#if 0
     int size;
     if(type==ARMCI_INT) size = sizeof(int);
     else if(type==ARMCI_LONG) size = sizeof(long);
     else if(type==ARMCI_LONG_LONG) size = sizeof(long long);
     else if(type==ARMCI_FLOAT) size = sizeof(float);
     else size = sizeof(double);

     armci_copy(work2,x,ndo*size);

     if(type==ARMCI_INT) idoop(ndo, op, (int*)x, (int*)work);
     else if(type==ARMCI_LONG) ldoop(ndo, op, (long*)x, (long*)work);
     else if(type==ARMCI_LONG_LONG) lldoop(ndo, op, (long long*)x, (long long*)work);
     else if(type==ARMCI_FLOAT) fdoop(ndo, op, (float*)x, (float*)work);
     else ddoop(ndo, op, (double*)x, (double*)work);
#else
     if(type==ARMCI_INT) idoop2(ndo, op, (int*)x, (int*)work, (int*)work2);
     else if(type==ARMCI_LONG)ldoop2(ndo,op,(long*)x,(long*)work,(long*)work2);
     else if(type==ARMCI_LONG_LONG) lldoop2(ndo,op,(long long*)x,(long long*)work,(long long*)work2);
     else if(type==ARMCI_FLOAT)fdoop2(ndo,op,(float*)x,(float*)work,(float*)work2);
     else ddoop2(ndo, op, (double*)x, (double*)work,(double*)work2);
#endif
}




/*\ shared memory based reduction for a single SMP node
\*/
static void armci_smp_reduce(void *x, int n, char* op, int type)
{
int root, up, left, right, size;
int ndo, len, lenmes, ratio;
int nslave = armci_clus_info[armci_clus_me].nslave; 

    if(nslave<2) return; /* nothing to do */

    if(!x)armci_die("armci_msg_gop: NULL pointer", n);

    armci_msg_bintree(SCOPE_NODE, &root, &up, &left, &right);

    if(type==ARMCI_INT) size = sizeof(int);
    else if(type==ARMCI_LONG) size = sizeof(long);
    else if(type==ARMCI_LONG_LONG) size = sizeof(long long);
    else if(type==ARMCI_FLOAT) size = sizeof(float); 
    else size = sizeof(double);
    ratio = sizeof(double)/size;
    
    while ((ndo = (n<=BUF_SIZE*ratio) ? n : BUF_SIZE*ratio)) {
       len = lenmes = ndo*size;

       armci_util_wait_int(&GOP_BUF(armci_me)->a.flag, EMPTY, 100);

#if 1
        if(left<0 && right<0) armci_copy(x,GOP_BUF(armci_me)->array,len); 
 
       /*  version oblivious to the order of data arrival */
       {
          int need_left = left >-1;
          int need_right = right >-1;
          int from, first =1, maxspin=100, count=0;
          bufstruct *b;
          
          while(need_left || need_right){
               from =-1;
               if(need_left && GOP_BUF(left)->a.flag == FULL){
                  from =left;
                  need_left =0;
               }else if(need_right && GOP_BUF(right)->a.flag == FULL) {
                  from =right;
                  need_right =0;
               }
               if(from != -1){
                  b = GOP_BUF(from);
#if 1
                  if(armci_me == root) gop(type, ndo, op, x, b->array);
                  else {
                    if(first)
                      gop2(type, ndo, op, GOP_BUF(armci_me)->array, b->array,x);
                    else
                      gop(type, ndo, op, GOP_BUF(armci_me)->array, b->array);
                  }
                  first =0;
#else
                  gop(type, ndo, op, GOP_BUF(armci_me)->array, b->array);
#endif
                  SET_SHM_FLAG(&( b->a.flag),empty);
               }else  if((++count)<maxspin) armci_util_spin(count,_gop_buffer);
                      else{cpu_yield();count =0; }
          }
       }
#else
               
       armci_copy(x,GOP_BUF(armci_me)->array,len);

       /*  this version requires a specific order of data arrival */
       if (left >-1) {
         while(GOP_BUF(left)->a.flag != FULL) cpu_yield();
         gop(type, ndo, op, GOP_BUF(armci_me)->array, GOP_BUF(left)->array);
         SET_SHM_FLAG(&( GOP_BUF(left)->a.flag),empty);
       }
       if (right >-1 ) {
         while(GOP_BUF(right)->a.flag != FULL) cpu_yield();
         gop(type, ndo, op, GOP_BUF(armci_me)->array, GOP_BUF(right)->array);
         GOP_BUF(right)->a.flag = EMPTY;
       }
#endif

       if (armci_me != root ) {
           SET_SHM_FLAG(&(GOP_BUF(armci_me)->a.flag),full);
       }
#if 0
        else
           /* NOTE:  this copy can be eliminated in a cluster configuration */
           armci_copy(GOP_BUF(armci_me)->array,x,len);
#endif

       n -=ndo;
       x = len + (char*)x;
    }  
}

void _armci_msg_binomial_reduce(void *x, int n, char* op, int type){
    int root = armci_clus_info[0].master;
    int i,next_node,next;
    int size, ratio, ndo, lenmes,len;
/*    int my_rank,root_rank,next_rank; */
    if(work==NULL)_allocate_mem_for_work();
    if(armci_me!=armci_master)return;
    if(type==ARMCI_INT) size = sizeof(int);
    else if(type==ARMCI_LONG) size = sizeof(long);
    else if(type==ARMCI_LONG_LONG) size = sizeof(long long);
    else if(type==ARMCI_FLOAT) size = sizeof(float);
    else size = sizeof(double);
    ratio = sizeof(double)/size;
   
    while ((ndo = (n<=BUF_SIZE*ratio) ? n : BUF_SIZE*ratio)) {
       int snd_proc=armci_clus_me,flag=1,diff=1;

       len = lenmes = ndo*size;
       if(armci_me!=root){
         while(!(snd_proc & flag)){
           diff=diff<<1;
           flag=flag<<1;
         }
         snd_proc = armci_clus_info[armci_clus_me-diff].master;
       }    
       else
         diff = Lp2;

       /*printf("\n%d: %d diff>>1 = %d\n",armci_me,Lp2,diff>>1);*/
       for(i=diff>>1;i>=1;i=i>>1){
         next=i^armci_clus_me;
         if(next>=0 && next<armci_nclus){
           next_node = armci_clus_info[next].master;
           armci_msg_rcv(ARMCI_TAG, lwork,len,&lenmes,next_node);
           if(type==ARMCI_INT) idoop(ndo, op, (int*)x, iwork);
           else if(type==ARMCI_LONG) ldoop(ndo, op, (long*)x, lwork);
           else if(type==ARMCI_LONG_LONG) lldoop(ndo, op,(long long*)x,llwork);
	   else if(type==ARMCI_FLOAT) fdoop(ndo, op, (float*)x, fwork);
           else ddoop(ndo, op, (double*)x, work);
           /*printf("\n%d: recvd from %d \n",armci_me,next_node);*/
         }
       }
       /*if(armci_me!=root){
          printf("\n%d: send to  %d \n",armci_me,snd_proc);
          fflush(stdout);
       }*/
       if(armci_me!=root)
         armci_msg_snd(ARMCI_TAG,x,len,snd_proc);
       n -=ndo;
       x = len + (char*)x;
    }
}

void armci_msg_reduce(void *x, int n, char* op, int type)
{
    if(DEBUG_)printf("%d reduce  %d\n",armci_me, n);
    /* intra-node operation */

#if 1
    if(_armci_gop_shmem) 
       armci_smp_reduce(x, n, op, type);
    else
#endif
    armci_msg_reduce_scope(SCOPE_NODE, x, n, op, type);

    /* inter-node operation between masters */
    if(armci_nclus>1){
      armci_msg_reduce_scope(SCOPE_MASTERS, x, n, op, type);
    }
}


static void armci_msg_gop2(void *x, int n, char* op, int type)
{
    int size, root=0;
    if (work==NULL) {
        _allocate_mem_for_work();
    }
    if (type==ARMCI_INT) {
        size = sizeof(int);
    } else if (type==ARMCI_LONG) {
        size = sizeof(long);
    } else if (type==ARMCI_LONG_LONG) {
        size = sizeof(long long);
    } else if (type==ARMCI_FLOAT) {
        size = sizeof(float);
    } else {
        size = sizeof(double);
    }
    armci_msg_reduce(x, n, op, type);
    armci_msg_bcast(x, size*n, root);
}


static void armci_sel(int type, char *op, void *x, void* work, int n)
{
  int selected=0;
  switch (type) {
  case ARMCI_INT:
     if(strncmp(op,"min",3) == 0){ 
        if(*(int*)x > *(int*)work) selected=1;
     }else
        if(*(int*)x < *(int*)work) selected=1;
     break;
  case ARMCI_LONG:
     if(strncmp(op,"min",3) == 0){ 
        if(*(long*)x > *(long*)work) selected=1;
     }else
        if(*(long*)x < *(long*)work) selected=1;
     break;
  case ARMCI_LONG_LONG:
     if(strncmp(op,"min",3) == 0){ 
        if(*(long long*)x > *(long long*)work) selected=1;
     }else
        if(*(long long*)x < *(long long*)work) selected=1;
     break;
  case ARMCI_FLOAT:
     if(strncmp(op,"min",3) == 0){ 
        if(*(float*)x > *(float*)work) selected=1;
     }else
        if(*(float*)x < *(float*)work) selected=1;
     break;
  default:
     if(strncmp(op,"min",3) == 0){
        if(*(double*)x > *(double*)work) selected=1;
     }else
        if(*(double*)x < *(double*)work) selected=1;
  }
  if(selected)armci_copy(work,x, n);
}
   
 

/*\ global for  op with extra info 
\*/
void armci_msg_sel_scope(int scope, void *x, int n, char* op, int type, int contribute)
{
    int root, up, left, right;
    int tag=ARMCI_TAG;
    int len, lenmes, min;

    min = (strncmp(op,"min",3) == 0);
    if(!min && (strncmp(op,"max",3) != 0))
            armci_die("armci_msg_gop_info: operation not supported ", 0);

    if(!x)armci_die("armci_msg_gop_info: NULL pointer", n);

    if(n>((int)INFO_BUF_SIZE))armci_die("armci_msg_gop_info: info too large",n);

    len = lenmes = n;

    armci_msg_bintree(scope, &root, &up, &left, &right);

    if (left > -1) {

        /* receive into work if contributing otherwise into x */
        if(contribute)armci_msg_rcv(tag, work, len, &lenmes, left);
        else armci_msg_rcv(tag, x, len, &lenmes, left);

        if(lenmes){
           if(contribute) armci_sel(type, op, x, work, n);
           else contribute =1; /* now we got data to pass */ 
        }
    }

    if (right > -1) {
        /* receive into work if contributing otherwise into x */
        if(contribute) armci_msg_rcv(tag, work, len, &lenmes, right);
        else armci_msg_rcv(tag, x, len, &lenmes, right);

        if(lenmes){
           if(contribute) armci_sel(type, op, x, work, n);
           else contribute =1; /* now we got data to pass */ 
        }
    }

    if (armci_me != root){
       if(contribute) armci_msg_snd(tag, x, len, up);
       else armci_msg_snd(tag, x, 0, up); /* send 0 bytes */
    }

    /* Now, root broadcasts the result down the binary tree */
    armci_msg_bcast_scope(scope, x, n, root);
}


/*\ combine array of longs/ints/doubles accross all processes
\*/

void armci_msg_igop(int *x, int n, char* op) { armci_msg_gop2(x, n, op, ARMCI_INT); }
void armci_msg_lgop(long *x, int n, char* op) { armci_msg_gop2(x, n, op, ARMCI_LONG); }
void armci_msg_llgop(long long *x, int n, char* op) { armci_msg_gop2(x, n, op, ARMCI_LONG_LONG); }
void armci_msg_fgop(float *x, int n, char* op) { armci_msg_gop2(x, n, op, ARMCI_FLOAT); }
void armci_msg_dgop(double *x, int n, char* op) { armci_msg_gop2(x, n, op, ARMCI_DOUBLE); }


/*\ add array of longs/ints within the same cluster node
\*/
void armci_msg_clus_igop(int *x, int n, char* op)
{ armci_msg_gop_scope(SCOPE_NODE,x, n, op, ARMCI_INT); }

void armci_msg_clus_lgop(long *x, int n, char* op)
{ armci_msg_gop_scope(SCOPE_NODE,x, n, op, ARMCI_LONG); }

void armci_msg_clus_llgop(long long *x, int n, char* op)
{ armci_msg_gop_scope(SCOPE_NODE,x, n, op, ARMCI_LONG_LONG); }

void armci_msg_clus_fgop(float *x, int n, char* op)
{ armci_msg_gop_scope(SCOPE_NODE,x, n, op, ARMCI_FLOAT); }

void armci_msg_clus_dgop_scope(double *x, int n, char* op)
{ armci_msg_gop_scope(SCOPE_NODE,x, n, op, ARMCI_DOUBLE); }



void armci_exchange_address(void *ptr_ar[], int n)
{
  int ratio = sizeof(void*)/sizeof(int);
/*
  armci_msg_lgop((long*)ptr_ar, n, "+");
*/
  if(DEBUG_)printf("%d: exchanging %ld ratio=%d\n",armci_me,(long)ptr_ar[armci_me],ratio);

  armci_msg_gop2(ptr_ar, n*ratio, "+",ARMCI_INT);
}

/**
 * ********************* Begin ARMCI Groups Code ****************************
 * NOTE: This part is MPI dependent (i.e. ifdef MSG_COMMS_MPI)
 */
#ifdef MSG_COMMS_MPI
MPI_Comm armci_group_comm(ARMCI_Group *group)
{
#ifdef ARMCI_GROUP
    return MPI_COMM_NULL;
#else
    ARMCI_iGroup *igroup = armci_get_igroup_from_group(group);
    return (MPI_Comm)igroup->icomm;
#endif
}

void parmci_msg_group_barrier(ARMCI_Group *group)
{
    ARMCI_iGroup *igroup = armci_get_igroup_from_group(group);
    
#ifdef ARMCI_GROUP
 {
   int val=0;
   armci_msg_group_igop(&val, 1, "+", group);
 }
#else
    MPI_Barrier((MPI_Comm)(igroup->icomm));
#endif
}

#ifdef ARMCI_GROUP
extern void ARMCI_Bcast_(void *buffer, int len, int root, ARMCI_Group *group);
#else
extern void ARMCI_Bcast_(void *buffer, int len, int root, ARMCI_Comm comm);
#endif
void armci_grp_clus_brdcst(void *buf, int len, int grp_master,
                           int grp_clus_nproc, ARMCI_Group *mastergroup) {
    ARMCI_iGroup *igroup = armci_get_igroup_from_group(mastergroup);
    int i, *pid_list, root=0;
#ifdef ARMCI_GROUP
    ARMCI_Group group;
#else
    MPI_Group group_world;
    MPI_Group group;
    MPI_Comm comm;
#endif
 
    /* create a communicator for the processes with in a node */
    pid_list = (int *)malloc(grp_clus_nproc*sizeof(int));
    for(i=0; i<grp_clus_nproc; i++)  pid_list[i] = grp_master+i;
 

#ifdef ARMCI_GROUP
    ARMCI_Group_create_child(grp_clus_nproc, pid_list, &group, mastergroup);
    ARMCI_Bcast_(buf, len, root, &group);
    ARMCI_Group_free(&group);
#else
    /* why?*/
    MPI_Comm_group((MPI_Comm)(igroup->icomm), &group_world);
    MPI_Group_incl(group_world, grp_clus_nproc, pid_list, &group);
 
    MPI_Comm_create((MPI_Comm)(igroup->icomm), (MPI_Group)group,
                    (MPI_Comm*)&comm);
 
    /* Broadcast within the node (for this sub group of processes) */
    ARMCI_Bcast_(buf, len, root, comm);
 
    free(pid_list);
    MPI_Comm_free(&comm);     /* free the temporary communicator */
    MPI_Group_free(&group);
#endif
}
 

/* to avoid warning */
extern int ARMCI_Absolute_id(ARMCI_Group *group,int group_rank);
 
void armci_msg_group_bintree(int scope, int* Root, int *Up, int *Left, int *Right,
			     ARMCI_Group *group)
{
    int root, up, left, right, index, nproc,grp_clus_me,grp_me,grp_master,grp_nproc;
    armci_grp_attr_t *grp_attr=ARMCI_Group_getattr(group);
    grp_me = grp_attr->grp_me;
    grp_clus_me = grp_attr->grp_clus_me;
    grp_master = grp_attr->grp_clus_info[grp_clus_me].master;
    ARMCI_Group_size(group, &grp_nproc);
    if(scope == SCOPE_NODE){
       root  = grp_attr->grp_clus_info[grp_clus_me].master;
       nproc = grp_attr->grp_clus_info[grp_clus_me].nslave;
       index = grp_me - root;
       up    = (index-1)/2 + root; if( up < root) up = -1;
       left  = 2*index + 1 + root; if(left >= root+nproc) left = -1;
       right = 2*index + 2 + root; if(right >= root+nproc)right = -1;
    }else if(scope ==SCOPE_MASTERS){
       root  = grp_attr->grp_clus_info[0].master;
       nproc = grp_attr->grp_nclus;
       if(grp_me != grp_master){up = -1; left = -1; right = -1; }
       else{
	  index = grp_clus_me - root;
	  up    = (index-1)/2 + root;
	  up = ( up < root)? -1: grp_attr->grp_clus_info[up].master;
	  left = 2*index + 1 + root;
	  left =( left >= root+nproc)?-1:grp_attr->grp_clus_info[left].master;
	  right= 2*index + 2 + root;
	  right=( right>=root+nproc)?-1:grp_attr->grp_clus_info[right].master;
       }
    }else{
       root  = 0;
       nproc = grp_nproc;
       index = grp_me - root;
       up    = (index-1)/2 + root; if( up < root) up = -1;
       left  = 2*index + 1 + root; if(left >= root+nproc) left = -1;
       right = 2*index + 2 + root; if(right >= root+nproc)right = -1;
    }
    
    *Up = (up==-1)?up:ARMCI_Absolute_id(group,up);
    *Left = (left==-1)?left:ARMCI_Absolute_id(group,left);
    *Right = (right==-1)?right:ARMCI_Absolute_id(group,right);
    *Root = (root==-1)?root:ARMCI_Absolute_id(group,root);
}

void armci_msg_group_bcast_scope(int scope, void *buf, int len, int root,
				 ARMCI_Group *group)
{
    int up, left, right, Root;

    if(!buf)armci_die("armci_msg_bcast: NULL pointer", len);
 
    if(!group)armci_msg_bcast_scope(scope,buf,len,root);
    armci_msg_group_bintree(scope, &Root, &up, &left, &right,group);
 
    if(root !=Root){
       if(armci_me == root) armci_msg_snd(ARMCI_TAG, buf,len, Root);
       if(armci_me ==Root) armci_msg_rcv(ARMCI_TAG, buf, len, NULL, root);
    }
 
    /* printf("%d: scope=%d left=%d right=%d up=%d\n",armci_me, scope,
       left, right, up);*/
 
    if(armci_me != Root && up!=-1) armci_msg_rcv(ARMCI_TAG, buf, len, NULL, up);
				     if (left > -1)  armci_msg_snd(ARMCI_TAG, buf, len, left);
				     if (right > -1) armci_msg_snd(ARMCI_TAG, buf, len, right);
}

void
armci_msg_group_gop_scope(int scope, void *x, int n, char* op, int type,
			  ARMCI_Group *group)
{
    int root, up, left, right, size;
    int tag=ARMCI_TAG;
    int ndo, len, lenmes, orign =n, ratio;
    void *origx =x;
 
    if(!group)armci_msg_gop_scope(scope,x,n,op,type);
    if(!x)armci_die("armci_msg_gop: NULL pointer", n);
    if(work==NULL)_allocate_mem_for_work();
 
    armci_msg_group_bintree(scope, &root, &up, &left, &right,group);
 
    if(type==ARMCI_INT) size = sizeof(int);
    else if(type==ARMCI_LONG) size = sizeof(long);
    else if(type==ARMCI_LONG_LONG) size = sizeof(long long);
    else if(type==ARMCI_FLOAT) size = sizeof(float);
    else size = sizeof(double);
 
    ratio = sizeof(double)/size;
 
    while ((ndo = (n<=BUF_SIZE*ratio) ? n : BUF_SIZE*ratio)) {
       len = lenmes = ndo*size;
 
       if (left > -1) {
	  armci_msg_rcv(tag, lwork, len, &lenmes, left);
	  if(type==ARMCI_INT) idoop(ndo, op, (int*)x, iwork);
	  else if(type==ARMCI_LONG) ldoop(ndo, op, (long*)x, lwork);
	  else if(type==ARMCI_LONG_LONG) lldoop(ndo, op, (long long*)x,llwork);
	  else if(type==ARMCI_FLOAT) fdoop(ndo, op, (float*)x, fwork);
	  else ddoop(ndo, op, (double*)x, work);
       }
 
       if (right > -1) {
	  armci_msg_rcv(tag, lwork, len, &lenmes, right);
	  if(type==ARMCI_INT) idoop(ndo, op, (int*)x, iwork);
	  else if(type==ARMCI_LONG) ldoop(ndo, op, (long*)x, lwork);
	  else if(type==ARMCI_LONG_LONG) lldoop(ndo, op,(long long*)x, llwork);
	  else if(type==ARMCI_FLOAT) fdoop(ndo, op, (float*)x, fwork);
	  else ddoop(ndo, op, (double*)x, work);
       }
       if (armci_me != root && up!=-1) armci_msg_snd(tag, x, len, up);
 
       n -=ndo;
       x = len + (char*)x;
    }
 
    /* Now, root broadcasts the result down the binary tree */
    len = orign*size;
    armci_msg_group_bcast_scope(scope, origx, len, root,group);
}

void armci_exchange_address_grp(void *ptr_arr[], int n, ARMCI_Group *group)
{
    int ratio = sizeof(void*)/sizeof(int);
    ARMCI_iGroup *igroup = armci_get_igroup_from_group(group);
    int grp_me = igroup->grp_attr.grp_me;
    if(DEBUG_){
       printf("%d: exchanging %ld ratio=%d\n",armci_me,
	      (long)ptr_arr[grp_me], ratio);
    }
    armci_msg_group_gop_scope(SCOPE_ALL, ptr_arr, n*ratio,
			      "+", ARMCI_INT, group);
}

/*\ combine array of longs/ints/doubles accross all processes
\*/
void armci_msg_group_igop(int *x, int n, char* op, ARMCI_Group *group)
{ armci_msg_group_gop_scope(SCOPE_ALL,x, n, op, ARMCI_INT,group); }
 
void armci_msg_group_lgop(long *x, int n, char* op,ARMCI_Group *group)
{ armci_msg_group_gop_scope(SCOPE_ALL,x, n, op, ARMCI_LONG,group); }

void armci_msg_group_llgop(long long *x, int n, char* op,ARMCI_Group *group)
{ armci_msg_group_gop_scope(SCOPE_ALL,x, n, op, ARMCI_LONG_LONG,group); }
 
void armci_msg_group_fgop(float *x, int n, char* op,ARMCI_Group *group)
{ armci_msg_group_gop_scope(SCOPE_ALL,x, n, op, ARMCI_FLOAT,group); }
 
void armci_msg_group_dgop(double *x, int n, char* op,ARMCI_Group *group)
{ armci_msg_group_gop_scope(SCOPE_ALL,x, n, op, ARMCI_DOUBLE,group); }

#  endif /* ifdef MSG_COMMS_MPI */
/*********************** End ARMCI Groups Code ****************************/

