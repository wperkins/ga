/*$Id: ga_sparse.array.h,v 1.40.2.4 2007/12/18 18:41:27 d3g293 Exp $ */
#include "gaconfig.h"
#include "typesf2c.h"

/**
 * Struct containing all data needed to keep track of iterator state
 */
typedef struct {
  Integer idim,jdim;/* dimensions of sparse array */
  Integer g_data;   /* global array containing data values of sparse matrix */
  Integer g_j;      /* global array containing j indices of sparse matrix */
  Integer g_i;      /* global array containing first j index for row i */
  Integer g_blk;    /* global array containing information on storage of
                     * each block of sparse array */
  Integer idx_size; /* size of integer indices */
  Integer grp;      /* handle for process group on which array is defined */
  Integer ilo, ihi; /* minimum and maximum row indices contained on this process */
                    /* ilo and ihi are zero-based */
  Integer nprocs;   /* number of processors containing this array */
  Integer nblocks;  /* number of non-zero sparse blocks contained on this process */
  Integer *blkidx;  /* array containing indices of non-zero blocks */
  Integer *blksize; /* array containing sizes of non-zero blocks */
  Integer *offset;  /* array containing starting index in g_j for each block in
                     * row block (relative to starting index of row block */
  Integer max_nnz;  /* maximum number of non-zeros per row */
  Integer *idx;     /* local buffer containing i indices */
  Integer *jdx;     /* local buffer containing j indices */
  void    *val;     /* local buffer containing values */
  Integer nval;     /* number of values currently stored in local buffers */
  Integer maxval;   /* maximum number of values that can currently
                     * be stored in local buffers */
  Integer type;     /* type of data stored in array */
  Integer size;     /* size of data element */
  Integer active;   /* array is currently in use */
  Integer ready;    /* array data has been distributed */
} _sparse_array;

extern _sparse_array *SPA;

extern void sai_init_sparse_arrays();
extern void sai_terminate_sparse_arrays();
