
#ifndef _TR_SURFACE_MOD
#define _TR_SURFACE_MOD

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SRFT_TRIS 1
#define SRFT_FACE 2
#define SRFT_GRID 4
#define SRFT_UNK  0x8000

typedef struct aabb_store_s
{
	float vmin[3];
	float vmax[3];
	uint16_t usage_count;
	uint16_t surftype;
} aabb_store_t;

struct msurface_s;

int qdx_surface_aabb_get_index( int grpid, const struct msurface_s* msurf, int dbgpoi );
int qdx_surface_aabb_get_data(int grpid, int index, const aabb_store_t** out);
void qdx_surface_aabb_get_local_origin(const aabb_store_t* box, float* origin);
int qdx_surface_aabb_generate( const void* surface, aabb_store_t* aabb );
int qdx_surface_aabb_contains_point( const aabb_store_t* box, float point[3] );
int qdx_surface_aabb_contains_box( const aabb_store_t* boxo, const aabb_store_t* boxi );
int qdx_surface_aabb_intersect_sphere( const aabb_store_t* box, float center[3], float radius );

int qdx_surface_aabb_needs_priming();
void qdx_suface_aabb_set_primed();
void qdx_surface_aabb_clear_marked_indexes();
void qdx_surface_aabb_mark_index( int grpid, int aabb_index );
void qdx_surface_aabb_add_all_marked_surfs();

void *qdx_surface_handle_replacement( aabb_store_t *box, const char *match_name, int match_val );
void qdx_surface_aabb_draw( int grpid );
void qdx_surface_aabb_clearall();

void qdx_surface_store_shader_info( const char *name, int value );
void qdx_surface_aabb_get_stats(size_t* data, int sz);

#ifdef __cplusplus
}
#endif
#endif