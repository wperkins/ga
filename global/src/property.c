#if HAVE_CONFIG_H
#   include "config.h"
#endif
 
/*#define PERMUTE_PIDS */
#define USE_GATSCAT_NEW

#if HAVE_STDIO_H
#   include <stdio.h>
#endif
#if HAVE_STRING_H
#   include <string.h>
#endif
#if HAVE_STDLIB_H
#   include <stdlib.h>
#endif
#if HAVE_STDINT_H
#   include <stdint.h>
#endif
#if HAVE_MATH_H
#   include <math.h>
#endif
#if HAVE_ASSERT_H
#   include <assert.h>
#endif
#if HAVE_STDDEF_H
#include <stddef.h>
#endif

#include "global.h"
#include "globalp.h"
#include "base.h"
#include "armci.h"
#include "macdecls.h"
#include "ga-papi.h"
#include "ga-wapi.h"
#include "thread-safe.h"
#include "gaproperty.h"

void NGA_Prop_Set(Integer g_a, int prop)
{
  Integer handle=GA_OFFSET + g_a;
  global_array_t * ga = &GA[handle];
  ga->prop |= prop;
}

void NGA_Prop_Clear(Integer g_a)
{
  Integer handle=GA_OFFSET + g_a;
  global_array_t * ga = &GA[handle]; 
  ga->prop = 0;
}

void NGA_Cache_Destroy(Integer g_a)
{
  Integer handle=GA_OFFSET + g_a;
  global_array_t * ga = &GA[handle]; 
  
  if(ga->prop_cached)
  {
    ga->prop_cached = 0;
    free(ga->prop_cache);
  }
}

int prop_check_cache(Integer g_a,
                   Integer *lo,
                   Integer *hi,
                   void    *buf,
                   Integer *ld,
		     Integer field_off,
		     Integer field_size,
                   Integer *nbhandle)
{
  Integer handle=GA_OFFSET + g_a;
  global_array_t * ga = &GA[handle]; 
  int i,j,k; 
  //Check if property bits are set for caching
  if( !(ga->prop & (GA_PROP_REPLI | GA_PROP_CACHE)) )
    return 0;
  

  if( ga->prop_cached == 0 )
  {
    // Create out cache and get the data in
    int size = 1;
    int hi[ga->ndim];
    int lo[ga->ndim];
    int ld[ga->ndim-1];

    for(i = 0, j = ga->ndim-1; i< ga->ndim; i++, j--)
    {
      size*=ga->dims[i];
      lo[i] = 0;
      hi[i] = ga->dims[j]-1;
      
      if(i>0)
        ld[i-1] = ga->dims[j];
    }
    
    ga->prop_cache = malloc(ga->elemsize*size);
    
    // Next hit to this function from NGA_Get will be ignored 
    ga->prop_cached = 2;
    NGA_Get(g_a, lo, hi, ga->prop_cache, ld);
    NGA_Sync(); 
    
    int * go = ga->prop_cache;

    ga->prop_cached = 1;
  }
  else if(ga->prop_cached == 2)
  {
    return 0;
  }
  
  Integer  idx, elems, size, p_handle;
  int stride[MAXDIM-1], ld_calc = 1;

  size = GA[handle].elemsize;
  int ndim = GA[handle].ndim;
  int offset[ndim];
  int patch_size = hi[0] - lo[0] + 1;
  offset[ndim-1] = 1;
  for(i = ndim - 2, j= 0; i >= 0; i--, j++ )
    offset[i] = offset[i+1] * ld[j];

  int blocks = 1; 
  int offset_pos = 0; 
  int offset_copy_pos = 0; 
  int pos[ndim];
  for(i = 0; i < ndim; i++ )
    pos[i] = 0;

  for(i = ndim-1; i >0; i--)
    blocks*= hi[i]-lo[i]+1;
  
  for(i = 0; i < blocks; i++ )
  {
    offset_pos=0;
    offset_copy_pos=0;
    for(j = 0, k = ndim-1; j < ndim-1; j++, k-- )
    {
      offset_pos += (pos[j])*offset[j];
      offset_copy_pos += (lo[k]-1+pos[j])*offset[j];
    }
    offset_copy_pos+= lo[0]-1;
    
    memcpy(((char *)buf)+(offset_pos)*size, ((char *)ga->prop_cache)+offset_copy_pos*size, patch_size*size);
    
    for(j = ndim-2; j >= 0; j-- )
    {
      if(j == ndim-2)
        pos[j]++;

      if(j!=0 && pos[j] == hi[j] - lo[j] +1)
      {
        pos[j] = 0;
        pos[j-1]++;
      }
      else
        break;
    }
  }

  return 1;
}
