
#include "qdx9.h"
#include "../win32/win_dx9int.h"
extern "C"
{
#include "tr_local.h"
//#include "resource.h"
//#include "win_local.h"
}
#include <stdint.h>

#include "tr_surface_mod.h"
#include "tr_surface_mod_int.h"

#include "fnv.h"

typedef struct shader_replace_s
{
	uint32_t hash;
	std::string name;
	int val;
	aabb_store_t box;
	image_t *image;
} shader_replace_t;

static std::map<uint32_t, shader_replace_t> g_shader_replacements;

static aabb_group_t g_aabb_groups[MAX_SHADERS];

BOOL qdx_surface_aabb_intersect( const aabb_store_t* boxa, const aabb_store_t* boxb );
void qdx_surface_aabb_merge( const aabb_store_t* boxa, const aabb_store_t* boxb, aabb_store_t* boxo );
void qdx_surface_aabb_switch_indexes( aabb_group_t* grp, int from, int to );
void qdx_surface_replacement_save( const char *hint, const char *name, int val, aabb_store_t *box, bool writeOut = true );

void qdx_surface_aabb_clearall()
{
	aabb_group_t* grp = &g_aabb_groups[0];
	for ( int i = 0; i < ARRAYSIZE( g_aabb_groups ); i++, grp++ )
	{
		grp->indexes.clear();
		grp->storage.clear();
		grp->marked_indexes.clear();
	}

	g_shader_replacements.clear();
}

void qdx_surface_aabb_prune_storage(aabb_group_t* grp)
{
	aabb_store_t *fast, *slow;
	int fast_idx, slow_idx;

	int steps = grp->storage.size();
	slow = &grp->storage[0];
	slow_idx = 0;
	for ( ; steps > 0  && slow->usage_count; steps-- )
	{
		slow++;
		slow_idx++;
	}
	fast = slow + 1;
	fast_idx = slow_idx + 1;
	steps--;
	for ( ; steps > 0; steps-- )
	{
		if ( fast->usage_count )
		{
			memcpy( slow, fast, sizeof( aabb_store_t ) );
			qdx_surface_aabb_switch_indexes( grp, fast_idx, slow_idx );
			slow++;
			slow_idx++;
		}
		fast++;
		fast_idx++;
	}

	int newsize = slow - &grp->storage[0];
	if ( newsize < grp->storage.size() )
	{
		grp->storage.resize( newsize );
	}
}

int qdx_surface_aabb_validate_mapping(aabb_group_t* grp)
{
	int maxindex = grp->indexes.size();

	for (auto it = grp->indexes.begin(); it != grp->indexes.end(); it++)
	{
		int index = it->second;
		qassert(index < maxindex);
		qassert(grp->storage[index].usage_count != 0);
		if (index >= maxindex || grp->storage[index].usage_count == 0)
		{
			__debugbreak();
			return index;
		}
	}

	return -1;
}

void qdx_surface_aabb_switch_indexes( aabb_group_t* grp, int from, int to )
{
	for (auto it = grp->indexes.begin(); it != grp->indexes.end(); it++)
	{
		if (it->second == from)
			it->second = to;
	}
}

int qdx_surface_aabb_generate( const void* surface, aabb_store_t* aabb )
{
	int ret = TRUE;
	int i;
	float* v;
	ClearBounds( aabb->vmin, aabb->vmax );
	aabb->usage_count = 0;

	surfaceType_t type = *(surfaceType_t*)surface;

	if ( type == SF_FACE )
	{
		srfSurfaceFace_t* face = (srfSurfaceFace_t*)surface;
		for ( i = 0, v = face->points[0]; i < face->numPoints; i++, v += VERTEXSIZE )
		{
			AddPointToBounds( v, aabb->vmin, aabb->vmax );
		}
	}
	else if ( type == SF_GRID )
	{
		srfGridMesh_t* grid = (srfGridMesh_t*)surface;
		VectorCopy( grid->meshBounds[0], aabb->vmin );
		VectorCopy( grid->meshBounds[1], aabb->vmax );
	}
	else if ( type == SF_TRIANGLES )
	{
		srfTriangles_t* tris = (srfTriangles_t*)surface;
		VectorCopy( tris->bounds[0], aabb->vmin );
		VectorCopy( tris->bounds[1], aabb->vmax );
	}
	else if ( type == SF_POLY )
	{
		srfPoly_t* poly = (srfPoly_t*)surface;
		for ( i = 1 ; i < poly->numVerts ; i++ ) {
			AddPointToBounds( poly->verts[i].xyz, aabb->vmin, aabb->vmax );
		}
	}
	else
	{
		aabb->vmin[0] = aabb->vmin[1] = aabb->vmin[2] = 99999;
		aabb->vmax[0] = aabb->vmax[1] = aabb->vmax[2] = 99999;
		ret = FALSE;
	}

	if ( ret )
		aabb->usage_count = 1;

	return ret;
}

static char printed[128] = { 0 };

void qdx_surface_aabb_mark_index( int grpid, int aabb_index )
{
	aabb_group_t* grp = &g_aabb_groups[grpid];

	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_AABB))
	{
		snprintf(printed, sizeof(printed), "mrk +:%d grp:%d idx:%d\n", !grp->marked_indexes.is_set(aabb_index), grpid, aabb_index);
		GPUimp_LogComment(printed);
	}

	if(grp->marked_indexes.set(aabb_index) == 0)
		grp->num_marks++;
}

qboolean qdx_surface_aabb_is_index_marked( int grpid, int aabb_index )
{
	qboolean ret = qfalse;

	aabb_group_t* grp = &g_aabb_groups[grpid];

	if(grp->marked_indexes.is_set(aabb_index))
	{
		ret = qtrue;
	}

	return ret;
}

static size_t g_num_starting_surfs_this_round = 0;
static size_t g_num_extra_surfs_this_round = 0;
static size_t g_num_total_marks_this_round = 0;

void qdx_surface_aabb_get_stats(size_t* data, int sz)
{
	if (sz >= 3)
	{
		data[0] = g_num_starting_surfs_this_round;
		data[1] = g_num_extra_surfs_this_round;
		data[2] = g_num_total_marks_this_round;
	}
}

void qdx_surface_aabb_add_all_marked_surfs()
{
	g_num_starting_surfs_this_round = tr.refdef.numDrawSurfs;

	aabb_group_t* grp = &g_aabb_groups[0];
	for (int i = 0; i < ARRAYSIZE(g_aabb_groups); i++, grp++)
	{
		bool grp1st = true;
		g_num_total_marks_this_round += grp->num_marks;
		auto it = grp->indexes.begin();
		while (it != grp->indexes.end())
		{
			if (grp->marked_indexes.is_set(it->second))
			{
				msurface_t* surf = (msurface_t*)it->first;

				if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_AABB))
				{
					if (grp1st)
					{
						grp1st = false;
						GPUimp_LogComment(surf->shader->name);
					}
					snprintf(printed, sizeof(printed), "add %p +:%d grp:%d idx:%d\n", surf, (surf->viewCount != tr.viewCount), i, it->second);
					GPUimp_LogComment(printed);
				}
				if (surf->viewCount != tr.viewCount) {
					surf->viewCount = tr.viewCount;
					R_AddDrawSurfEx(surf->data, surf->shader, it->second, surf->fogIndex, 0, ATI_TESS_NONE);
					g_num_extra_surfs_this_round++;
				}
			}
			it++;
		}
	}
}

void qdx_surface_aabb_clear_marked_indexes()
{
	aabb_group_t* grp = &g_aabb_groups[0];
	for (int i = 0; i < ARRAYSIZE(g_aabb_groups); i++, grp++)
	{
		grp->marked_indexes.clear();
		grp->num_marks = 0;
	}

	g_num_starting_surfs_this_round = 0;
	g_num_extra_surfs_this_round = 0;
	g_num_total_marks_this_round = 0;
}

int qdx_surface_aabb_get_index( int grpid, const struct msurface_s *msurf, int dbgpoi )
{
	const surfaceType_t* surfdata = msurf->data;

	if ( *surfdata != SF_FACE && *surfdata != SF_GRID && *surfdata != SF_TRIANGLES && *surfdata != SF_POLY )
	{
		return -1;
	}

	aabb_group_t* grp = &g_aabb_groups[grpid];
	{
		auto it = grp->indexes.find(msurf);
		if ( it != grp->indexes.end() )
		{
			return it->second;
		}
	}

	aabb_store_t aabb_local, *aabb = &aabb_local;
	BOOL merged = FALSE;
	if ( qdx_surface_aabb_generate( surfdata, aabb ) )
	{
		int i = 0, index = -1;
		for ( auto it = grp->storage.begin(); it != grp->storage.end(); i++, it++ )
		{
			if ( qdx_surface_aabb_intersect( it._Ptr, aabb ) )
			{
				index = i;
				qdx_surface_aabb_merge( aabb, it._Ptr, it._Ptr );
				grp->indexes[msurf] = index;
				aabb = it._Ptr;
				merged = TRUE;
				break;
			}
		}

		//when aabb is unique, add it to the list and finish
		if ( merged == FALSE || r_showaabbs->integer > 1 )
		{   //insert new
			index = grp->storage.size();
			grp->storage.push_back( aabb_local );
			grp->indexes[msurf] = index;
			return index;
		}
		//r_showaabbs > 1 keeps all aabbs even if merged to aid surf separation
		if (r_showaabbs->integer > 1)
		{   //insert dummy aabb
			aabb_local.usage_count = 0;
			grp->storage.push_back(aabb_local);
		}

		//now that aabb was merged we need to compound merges, i.e. this new aabb might merge two or more pre-existing aabbs
		do {
			merged = FALSE;
			i = 0;
			for ( auto it = grp->storage.begin(); it != grp->storage.end(); i++, it++ )
			{
				//skip over our own index and over dummy aabbs
				if ( i != index && it->usage_count && qdx_surface_aabb_intersect( it._Ptr, aabb ) )
				{
					//if ( i < index )
					//{
					//	qdx_surface_aabb_merge( aabb, it._Ptr, it._Ptr );
					//	aabb->usage_count = 0;
					//  qdx_surface_aabb_switch_indexes( grp, index, i );
					//	index = i;
					//	aabb = it._Ptr;
					//}
					//else
					{
						qdx_surface_aabb_merge( aabb, it._Ptr, aabb );
						it->usage_count = 0;
						qdx_surface_aabb_switch_indexes( grp, i, index );
						qdx_surface_aabb_validate_mapping(grp);
					}
					//in case of a merge, go back to the begining and recheck
					merged = TRUE;
					break;
				}
			}
		} while ( merged );

		if (r_showaabbs->integer <= 1)
		{
			//this looks expensive
			//qdx_surface_aabb_prune_storage(grp);
		}

		qdx_surface_aabb_validate_mapping(grp);

		return index; 
	}

	return -1;
}

int qdx_surface_aabb_get_index0( int grpid, const void *surf, int dbgpoi )
{
	aabb_group_t* grp = &g_aabb_groups[grpid];
	const surfaceType_t* surface = (const surfaceType_t*)surf;

	if ( *surface != SF_FACE && *surface == SF_GRID && *surface == SF_TRIANGLES /*&& *surfdata == SF_POLY*/ )
	{
		return -1;
	}

	{
		auto it = grp->indexes.find( surface );
		if ( it != grp->indexes.end() )
		{
			return it->second;
		}
	}

	aabb_store_t aabb_local, *aabb = &aabb_local;
	BOOL merged = FALSE;
	if ( qdx_surface_aabb_generate( surface, aabb ) )
	{
		if ( dbgpoi )
		{
			if ( aabb->vmax[0] < 80 && aabb->vmax[1] < 500 && aabb->vmax[2] < 1100 &&
				aabb->vmin[0] > -160 && aabb->vmin[1] > 30 && aabb->vmin[2] > -400 )
			{
				if ( grp == 0 )
					ri.Printf( 0, "" );
			}
			else
				return -1;
		}
		int i = 0, index = -1;
		for ( auto it = grp->storage.begin(); it != grp->storage.end(); i++, it++ )
		{
			if ( qdx_surface_aabb_intersect( it._Ptr, aabb ) )
			{
				if ( merged == FALSE )
				{
					index = i;
					qdx_surface_aabb_merge( aabb, it._Ptr, it._Ptr );
					aabb = it._Ptr;
					merged = TRUE;
				}
				else
				{
					qdx_surface_aabb_merge( aabb, it._Ptr, aabb );
					it->usage_count = 0;
					for ( auto it1 = grp->indexes.begin(); it1 != grp->indexes.end(); it1++ )
					{
						if ( it1->second == i )
						{
							it1->second = index;
						}
					}
				}
			}
		}

		if ( merged == FALSE )
		{
			index = grp->storage.size();
			aabb_local.usage_count = 1;
			grp->storage.push_back( aabb_local );
		}
		else
		{
			qdx_surface_aabb_prune_storage(grp);
		}
		grp->indexes[surface] = index;

		return index; 
	}

	return -1;
}

int qdx_surface_aabb_intersect ( const aabb_store_t* boxa, const aabb_store_t* boxb )
{
	float allow = r_aabb_mergedist->value;
	return (
		boxa->vmin[0] -allow <= boxb->vmax[0] +allow && boxa->vmax[0] +allow >= boxb->vmin[0] -allow &&
		boxa->vmin[1] -allow <= boxb->vmax[1] +allow && boxa->vmax[1] +allow >= boxb->vmin[1] -allow &&
		boxa->vmin[2] -allow <= boxb->vmax[2] +allow && boxa->vmax[2] +allow >= boxb->vmin[2] -allow
		);
}

int qdx_surface_aabb_contains_point( const aabb_store_t* box, float point[3])
{
	return (
		point[0] >= box->vmin[0] && point[0] <= box->vmax[0] &&
		point[1] >= box->vmin[1] && point[1] <= box->vmax[1] &&
		point[2] >= box->vmin[2] && point[2] <= box->vmax[2]
		);
}

int qdx_surface_aabb_contains_box( const aabb_store_t* boxo, const aabb_store_t* boxi )
{
	return (
		boxi->vmin[0] >= boxo->vmin[0] && boxi->vmax[0] <= boxo->vmax[0] &&
		boxi->vmin[1] >= boxo->vmin[1] && boxi->vmax[1] <= boxo->vmax[1] &&
		boxi->vmin[2] >= boxo->vmin[2] && boxi->vmax[2] <= boxo->vmax[2]
		);
}

int qdx_surface_aabb_intersect_sphere( const aabb_store_t* box, float center[3], float radius )
{
	const float p0 = max( box->vmin[0], min( center[0], box->vmax[0] ) );
	const float p1 = max( box->vmin[1], min( center[1], box->vmax[1] ) );
	const float p2 = max( box->vmin[2], min( center[2], box->vmax[2] ) );

	const float d0 = p0 - center[0];
	const float d1 = p1 - center[1];
	const float d2 = p2 - center[2];

	const float distance = d0 * d0 + d1 * d1 + d2 * d2;

	return (distance < radius * radius);
}

void qdx_surface_aabb_merge (const aabb_store_t* boxa, const aabb_store_t* boxb, aabb_store_t* boxo)
{
	for ( int i = 0; i < 3; i++ )
	{
		boxo->vmin[i] = (boxa->vmin[i] < boxb->vmin[i]) ? boxa->vmin[i] : boxb->vmin[i];
		boxo->vmax[i] = (boxa->vmax[i] > boxb->vmax[i]) ? boxa->vmax[i] : boxb->vmax[i];
	}

	int count = boxa->usage_count + boxb->usage_count;
	boxo->usage_count = count;
}

static int g_selected_aabb = 0;
static int g_total_aabbs_in_group = 0;
static int g_selected_group = 0;
static char g_selected_shader_name[256] = { 0 };
static int g_selected_shader_value = -99;

void qdx_surface_store_shader_info( const char *name, int value )
{
	strncpy( g_selected_shader_name, name, sizeof( g_selected_shader_name ) -1 );
	g_selected_shader_value = value;
}

const char *qdx_4imgui_shader_info( int *value )
{
	if ( value ) *value = g_selected_shader_value;
	return g_selected_shader_name;
}

int* qdx_4imgui_surface_aabb_selection( int *total )
{
	if ( total ) *total = g_total_aabbs_in_group;

	return &g_selected_aabb;
}

void qdx_4imgui_surface_aabb_saveselection(const char *hint)
{
	if ( g_selected_shader_value == -99 || g_selected_shader_name[0] == 0 )
	{
		return;
	}

	aabb_group_t* grp = &g_aabb_groups[g_selected_group];
	if ( g_selected_aabb>= 0 && g_selected_aabb < grp->storage.size() )
	{
		aabb_store_t* aabb = &grp->storage[g_selected_aabb];

		qdx_surface_replacement_save( hint, g_selected_shader_name, g_selected_shader_value, aabb, true );
	}
}

void qdx_surface_aabb_draw( int grpid )
{
	qdx_vbuffer_t vbuf = NULL;
	qdx_vbuffer_steps( STEP_ALLOCATE, &vbuf, VATTID_VERTCOL, 0, 8*sizeof(vatt_vertcol), 0 );
	qdx_ibuffer_t ibuf = NULL;
	qdx_ibuffer_steps( STEP_ALLOCATE, &ibuf, D3DFMT_INDEX16, 0, 24 * sizeof(uint16_t), 0 );

	aabb_group_t* grp = &g_aabb_groups[grpid];
	g_total_aabbs_in_group = grp->storage.size();
	g_selected_group = grpid;

	DWORD white = D3DCOLOR_XRGB( 255, 255, 255 );

	int i = 0;
	for ( auto it = grp->storage.begin(); it != grp->storage.end(); it++, i++ )
	{
		aabb_store_t* aabb = it._Ptr;
		if ( aabb->usage_count == 0 && r_showaabbs->integer < 2 )
			continue;

		bool is_colored = (i == g_selected_aabb);

		vatt_vertcol* v;
		qdx_vbuffer_steps( STEP_GET_DATAPTR_CLEAR, &vbuf, VATTID_VERTCOL, 0, 8*sizeof( vatt_vertcol ), (void**)&v );

		v->COLOR = is_colored ? D3DCOLOR_XRGB( 0, 0, 0 ) : white;
		v->XYZ[0] = aabb->vmin[0]; v->XYZ[1] = aabb->vmin[1]; v->XYZ[2] = aabb->vmin[2];
		v++;
		v->COLOR = is_colored ? D3DCOLOR_XRGB( 0, 0, 255 ) : white;
		v->XYZ[0] = aabb->vmin[0]; v->XYZ[1] = aabb->vmin[1]; v->XYZ[2] = aabb->vmax[2];
		v++;
		v->COLOR = is_colored ? D3DCOLOR_XRGB( 255, 0, 255 ) : white;
		v->XYZ[0] = aabb->vmax[0]; v->XYZ[1] = aabb->vmin[1]; v->XYZ[2] = aabb->vmax[2];
		v++;
		v->COLOR = is_colored ? D3DCOLOR_XRGB( 255, 0, 0 ) : white;
		v->XYZ[0] = aabb->vmax[0]; v->XYZ[1] = aabb->vmin[1]; v->XYZ[2] = aabb->vmin[2];

		v++;
		v->COLOR = is_colored ? D3DCOLOR_XRGB( 128, 128, 128 ) : white;
		v->XYZ[0] = aabb->vmax[0]; v->XYZ[1] = aabb->vmax[1]; v->XYZ[2] = aabb->vmax[2];
		v++;
		v->COLOR = is_colored ? D3DCOLOR_XRGB( 0, 255, 255 ) : white;
		v->XYZ[0] = aabb->vmin[0]; v->XYZ[1] = aabb->vmax[1]; v->XYZ[2] = aabb->vmax[2];
		v++;
		v->COLOR = is_colored ? D3DCOLOR_XRGB( 0, 255, 0 ) : white;
		v->XYZ[0] = aabb->vmin[0]; v->XYZ[1] = aabb->vmax[1]; v->XYZ[2] = aabb->vmin[2];
		v++;
		v->COLOR = is_colored ? D3DCOLOR_XRGB( 255, 255, 0 ) : white;
		v->XYZ[0] = aabb->vmax[0]; v->XYZ[1] = aabb->vmax[1]; v->XYZ[2] = aabb->vmin[2];

		qdx_vbuffer_steps( STEP_FINALIZE, &vbuf, 0, 0, 0, 0 );


		const uint16_t lines[] = { 0, 1,  1, 2,  2, 3,  3, 0,
			4, 5,  5, 6,  6, 7,  7, 4,
			5, 1,  4, 2,  7, 3,  6, 0 };

		uint16_t* ip;
		qdx_ibuffer_steps( STEP_GET_DATAPTR_CLEAR, &ibuf, D3DFMT_INDEX16, 0, sizeof( lines ), (void**)&ip );
		memcpy( ip, lines, sizeof( lines ) );
		qdx_ibuffer_steps( STEP_FINALIZE, &ibuf, 0, 0, 0, 0 );

		DX9_BEGIN_SCENE();

		IDirect3DDevice9_SetFVF( qdx.device, VATTID_VERTCOL );

		qdx_matrix_apply();

		qdx.device->SetIndices( ibuf );
		qdx.device->SetStreamSource( 0, vbuf, 0, sizeof( vatt_vertcol ) );

		qdx_texobj_apply(TEXID_NULL, 0);
		qdx_texobj_apply(TEXID_NULL, 1);
		qdx.device->DrawIndexedPrimitive( D3DPT_LINELIST, 0, 0, 8, 0, 12 );

		DX9_END_SCENE();
	}

	qdx_vbuffer_release( vbuf );
	qdx_ibuffer_release( ibuf );
}

static uint8_t sample_dot( uint32_t code, int x, int y, int len )
{
	//scale to 9 rows and columns
	int lc = (y * 9) % (len / 8);
	int lr = (x * 9) % (len / 8);

	//egdes are blank
	if ( lc < 2 || lc > 6 )
		return 0;
	if ( lr < 2 || lr > 6 )
		return 0;

	//5 rows and 5 columns
	int c = lc - 2;
	int r = lr - 2;

	//horizontal mirror
	if( c > 2 )
		c = c & 1; // 3 -> 1, 4 -> 0

	uint32_t mask = 1 << (c * 5 + r);
	return (code & mask) != 0;
}

union code_to_rgb_u
{
	struct code_to_rgb_s
	{
		uint32_t unused : 15;
		uint32_t b : 5;
		uint32_t g : 6;
		uint32_t r : 6;
	} bits;
	uint32_t all;
};

static void fill_texbuf( uint8_t *buf, int len, uint32_t code )
{
	union code_to_rgb_u toclr;
	toclr.all = code;
	uint8_t color[4];

	//rgba
	color[0] = (toclr.bits.r * 255) / 63;
	color[1] = (toclr.bits.g * 255) / 63;
	color[2] = (toclr.bits.b * 255) / 31;
	color[3] = 255;

	for ( int x = 0; x < len; x++ )
		for ( int y = 0; y < len; y++ )
		{
			uint8_t val = sample_dot( code, x, y, len );
			uint8_t *pixel = buf + (x * len + y) * 4;
			if ( val )
			{
				pixel[0] = color[0]; pixel[1] = color[1]; pixel[2] = color[2]; pixel[3] = color[3];
			}
			else
			{
				pixel[0] = pixel[1] = pixel[2] = pixel[3] = 255;
			}
		}
}

image_t *qdx_surface_create_texture(uint32_t hash)
{
	const int texsize = 64;
	char name[16];
	static uint8_t data[texsize][texsize][4];
	snprintf( name, sizeof( name ), "repl_0x%.8x", hash );

	fill_texbuf( (uint8_t*)data, texsize, hash );
	image_t *img = R_CreateImage( name, (byte *)data, texsize, texsize, qfalse, qfalse, GL_REPEAT );
	return img;
}

#define SECTION_SURF_REPLACE "srfovr"
#define SECTION_SURF_REPLACE_PRINT "srfovr.0x%x"

void qdx_surface_replacements_load( mINI::INIStructure &ini, const char *mapname )
{
	for ( auto const& it : ini )
	{
		auto const& section = it.first;
		if ( str_starts_with( section, SECTION_SURF_REPLACE ) && str_ends_with( section, mapname ) )
		{
			const char* tmp = section.c_str() + sizeof( SECTION_SURF_REPLACE );
			uint32_t savehash = strtoul( tmp, NULL, 16 );
			while ( *tmp != '.' )
				tmp++;

			std::string repname;
			repname.assign( section.c_str(), tmp - section.c_str() );

			char match_name[128];
			qdx_readmapconfstr( repname.c_str(), "shader_name", match_name, sizeof( match_name ) );
			int match_val = qdx_readmapconf( repname.c_str(), "shader_val", 0 );
			
			uint32_t genhash = fnv_32a_str( match_name, match_val );

			shader_replace_t *rep = &g_shader_replacements[savehash];
			rep->hash = genhash;
			rep->name.assign( match_name );
			rep->val = match_val;
			rep->image = NULL;
			rep->box.usage_count = 0;
			rep->box.vmin[0] = qdx_readmapconfflt( repname.c_str(), "vmin0", 0 );
			rep->box.vmin[1] = qdx_readmapconfflt( repname.c_str(), "vmin1", 0 );
			rep->box.vmin[2] = qdx_readmapconfflt( repname.c_str(), "vmin2", 0 );
			rep->box.vmax[0] = qdx_readmapconfflt( repname.c_str(), "vmax0", 0 );
			rep->box.vmax[1] = qdx_readmapconfflt( repname.c_str(), "vmax1", 0 );
			rep->box.vmax[2] = qdx_readmapconfflt( repname.c_str(), "vmax2", 0 );
		}
	}
}

void qdx_surface_replacement_save( const char *hint, const char *name, int val, aabb_store_t *box, bool writeOut )
{
	char section[64];
	uint32_t genhash = fnv_32a_str( name, val );
	genhash = fnv_32a_buf( box->vmin, sizeof( box->vmin ), genhash );
	genhash = fnv_32a_buf( box->vmax, sizeof( box->vmax ), genhash );

	snprintf( section, sizeof( section ), SECTION_SURF_REPLACE_PRINT, genhash );
	if ( hint && hint[0] )
	{
		qdx_storemapconfstr( section, "hint", hint, FALSE );
	}
	qdx_storemapconfstr( section, "shader_name", name, FALSE );
	qdx_storemapconfint( section, "shader_val", val, FALSE );
	qdx_storemapconfflt( section, "vmin0", box->vmin[0], FALSE );
	qdx_storemapconfflt( section, "vmin1", box->vmin[1], FALSE );
	qdx_storemapconfflt( section, "vmin2", box->vmin[2], FALSE );
	qdx_storemapconfflt( section, "vmax0", box->vmax[0], FALSE );
	qdx_storemapconfflt( section, "vmax1", box->vmax[1], FALSE );
	qdx_storemapconfflt( section, "vmax2", box->vmax[2], FALSE );

	if ( writeOut )
	{
		qdx_save_iniconf();
	}
}

void *qdx_surface_handle_replacement(aabb_store_t *box, const char *match_name, int match_val)
{
	uint32_t genhash = fnv_32a_str( match_name, match_val );

	for (auto it = g_shader_replacements.begin();
		it != g_shader_replacements.end(); it++ )
	{
		shader_replace_t *rep = &it->second;

		if ( rep->hash == genhash && qdx_surface_aabb_contains_box( &rep->box, box ) )
		{
			if( rep->image == NULL )
				rep->image = qdx_surface_create_texture( it->first ); //use the unique hash

			return rep->image;
		}
	}

	return NULL;
}
