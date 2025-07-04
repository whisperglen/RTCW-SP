
#ifndef _TR_SURFACE_MOD
#define _TR_SURFACE_MOD

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct aabb_store_s
{
	uint32_t usage_count;
	float vmin[3];
	float vmax[3];
} aabb_store_t;

int qdx_surface_aabb_get_index( int grpid, const void* surf, int dbgpoi );
int qdx_surface_aabb_generate( const void* surface, aabb_store_t* aabb );
int qdx_surface_aabb_contains_point( const aabb_store_t* box, float point[3] );
int qdx_surface_aabb_contains_box( const aabb_store_t* boxo, const aabb_store_t* boxi );
int qdx_surface_aabb_intersect_sphere( const aabb_store_t* box, float center[3], float radius );
void *qdx_surface_handle_replacement( aabb_store_t *box, const char *match_name, int match_val );
void qdx_surface_aabb_draw( int grpid );
void qdx_surface_aabb_clearall();

void qdx_surface_store_shader_info( const char *name, int value );

#ifdef __cplusplus
}
#endif
#endif