#ifndef _GAPROPERTY_H_
#define _GAPROPERTY_H_

#define GA_PROP_REPLI 1
#define GA_PROP_CACHE 2

#ifdef __cplusplus
extern "C" {
#endif
extern void          NGA_Prop_Set(Integer g_a, int prop);
extern void          NGA_Prop_Clear(Integer g_a);
extern void          NGA_Cache_Destroy(Integer g_a);
int prop_check_cache(Integer g_a,
                   Integer *lo,
                   Integer *hi,
                   void    *buf,
                   Integer *ld,
		     Integer field_off,
		     Integer field_size,
                   Integer *nbhandle);

#ifdef __cplusplus
}
#endif

#endif
