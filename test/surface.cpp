

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <map>
#include <stdint.h>
#include <Windows.h>

#include "renderer/tr_surface_mod.h"
#include "renderer/tr_surface_mod_int.h"

#include "tests.h"

static aabb_group_t g_aabb_groups[1];
static aabb_group_t* g_defgrp = &g_aabb_groups[0];

void qdx_surface_aabb_switch_indexes( aabb_group_t* grp, int from, int to )
{
	for ( auto it1 = grp->indexes.begin(); it1 != grp->indexes.end(); it1++ )
	{
		if ( it1->second == from )
		{
			it1->second = to;
		}
	}
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

static void fill_storage( int* invalid, int max )
{
	aabb_store_t local;
	memset( &local, 0, sizeof( local ) );
	
	g_defgrp->storage.clear();

	int j = 0;
	for ( int i = 0; i < max; i++ )
	{
		local.usage_count = i + 10;
		local.vmin[0] = i;
		if ( invalid[j] == i )
		{
			j++;
			local.usage_count = 0;
		}
		g_defgrp->storage.push_back( local );

		uc8_t count;
		random_bytes( &count, 1 );
		count = count & 3;
		count++;
		while ( count-- )
		{
			const void* ptr;
			random_bytes( (uc8_t*)&ptr, sizeof( void* ) );
			g_defgrp->indexes[ptr] = i;
		}
	}
}

/*

the drawsurf sort data is packed into a single 32 bit value so it can be
compared quickly during the qsorting process

the bits are allocated as follows:

(SA) modified for Wolf (11 bits of entity num)

old:

22 - 31	: sorted shader index
12 - 21	: entity index
3 - 7	: fog index
2		: used to be clipped flag
0 - 1	: dlightmap index

#define	QSORT_SHADERNUM_SHIFT	22
#define	QSORT_ENTITYNUM_SHIFT	12
#define	QSORT_FOGNUM_SHIFT		3

new:

22 - 31	: sorted shader index
11 - 21	: entity index
2 - 6	: fog index
removed	: used to be clipped flag
0 - 1	: dlightmap index

*/
#define QSORT_SHADERNUM_SHIFT   22
#define QSORT_ENTITYNUM_SHIFT   11
#define QSORT_FOGNUM_SHIFT      2

// GR - tessellation flag in bit 8
#define QSORT_ATI_TESS_SHIFT    8
// GR - TruForm flags
#define ATI_TESS_TRUFORM    1
#define ATI_TESS_NONE       0

#define MAX_SHADERS             2048
#define GENTITYNUM_BITS     10      // don't need to send any more
//#define	GENTITYNUM_BITS		11		// don't need to send any more		(SA) upped 4/21/2001 adjusted: tr_local.h (802-822), tr_main.c (1501), sv_snapshot (206)
#define MAX_GENTITIES       ( 1 << GENTITYNUM_BITS )

uint32_t pack_sort(int shaderNum, int entityNum, int fogIndex, int dlightMap, int atiTess)
{
	uint32_t sort = (shaderNum << QSORT_SHADERNUM_SHIFT)
		| (atiTess << QSORT_ATI_TESS_SHIFT)
		| (entityNum << QSORT_ENTITYNUM_SHIFT) | (fogIndex << QSORT_FOGNUM_SHIFT) | (int)dlightMap;
	return sort;
}

void unpack_sort(uint32_t sort, int *entityNum, int *shaderNum,
	int *fogNum, int *dlightMap, int *atiTess) {
	*fogNum = (sort >> QSORT_FOGNUM_SHIFT) & 31;
	*shaderNum = (sort >> QSORT_SHADERNUM_SHIFT) & (MAX_SHADERS - 1);
	//	*entityNum = ( sort >> QSORT_ENTITYNUM_SHIFT ) & 1023;
	*entityNum = (sort >> QSORT_ENTITYNUM_SHIFT) & (MAX_GENTITIES - 1);   // (SA) uppded entity count for Wolf to 11 bits
	*dlightMap = sort & 3;
	//GR - extract tessellation flag
	*atiTess = (sort >> QSORT_ATI_TESS_SHIFT) & 1;
}

typedef union sort_pack_u {
	struct sort_pack_s
	{
		uint32_t dlightMap : 2; //0
		uint32_t fogNum : 5; //2
		uint32_t unused0 : 1; //7
		uint32_t atiTess : 1; //8
		uint32_t unused1 : 2; //9
		uint32_t entityNum : 10; //11
		uint32_t unused2 : 1; //21
		uint32_t shaderNum : 10; //22
	} bits;
	uint32_t all;
} sort_pack_t;

static void gen_surf_img();

extern "C" void test_surface()
{
	printf("\n----- test surface\n");

	int invalid0[] = { 0, 4, 7, 8, -1 };
	fill_storage( invalid0, 9 );

	qdx_surface_aabb_prune_storage(g_defgrp);
	assert( g_defgrp->storage.size() == 5 );
	assert( g_defgrp->storage[0].usage_count == 11 );
	assert( g_defgrp->storage[1].usage_count == 12 );
	assert( g_defgrp->storage[2].usage_count == 13 );
	assert( g_defgrp->storage[3].usage_count == 15 );
	assert( g_defgrp->storage[4].usage_count == 16 );

	int invalid1[] = { 2, -1 };
	fill_storage( invalid1, 3 );

	qdx_surface_aabb_prune_storage(g_defgrp);
	assert( g_defgrp->storage.size() == 2 );
	assert( g_defgrp->storage[0].usage_count == 10 );
	assert( g_defgrp->storage[1].usage_count == 11 );

	int invalid2[] = { -1 };
	fill_storage( invalid2, 3 );

	qdx_surface_aabb_prune_storage(g_defgrp);
	assert( g_defgrp->storage.size() == 3 );
	assert( g_defgrp->storage[0].usage_count == 10 );
	assert( g_defgrp->storage[1].usage_count == 11 );
	assert( g_defgrp->storage[2].usage_count == 12 );

	sort_pack_t upack;

	//int shaderNum, int entityNum, int fogIndex, int dlightMap, int atiTess
	upack.all = pack_sort( 1023, 0, 0, 0, 0 );
	assert( upack.bits.shaderNum == 1023 );

	upack.all = pack_sort( 0, (MAX_GENTITIES - 1), 0, 0, 0 );
	assert( upack.bits.entityNum == (MAX_GENTITIES - 1) );

	upack.all = pack_sort( 0, 0, 31, 0, 0 );
	assert( upack.bits.fogNum == 31 );

	upack.all = pack_sort( 0, 0, 0, 3, 0 );
	assert( upack.bits.dlightMap == 3 );

	upack.all = pack_sort( 0, 0, 0, 0, 1 );
	assert( upack.bits.atiTess == 1 );

	gen_surf_img();

	printf("----- test surface end\n\n");
}

static int sample_dot( uint32_t code, int x, int y, int len );

static void gen_surf_img()
{
	uc8_t buf[5][5];
	uint16_t code, code0;

	for ( int i = 0; i < 5; i++ )
	{
		random_bytes( (uc8_t*)&code, 2 );
		code0 = code;

		for ( int c = 0; c < 2; c++ )
			for ( int r = 0; r < 5; r++ )
			{
				buf[r][c] = buf[r][4-c] = code & 1;
				code >>= 1;
			}

		for ( int r = 0; r < 5; r++ )
		{
			buf[r][2] = code & 1;
			code >>= 1;
		}


		for ( int r = 0; r < 5; r++ )
		{
			for ( int c = 0; c < 5; c++ )
				printf( "%c", (buf[r][c] ? 219 : ' ') );

			printf( "  " );

			for ( int c = 0; c < 5; c++ )
				printf( "%c", (sample_dot(code0, r, c, 5) ? 219 : ' ') );

			printf( "\n" );
		}
		printf( "\n" );
	}
}

int sample_dot( uint32_t code, int x, int y, int len )
{
	//int lc = (y * 9) / len;
	//int lr = (x * 9) / len;

	//if ( lc < 2 || lc > 6 )
	//	return 0;
	//if ( lr < 2 || lr > 6 )
	//	return 0;

	//int c = lc - 2;
	//int r = lr - 2;
	int c = y;
	int r = x;

	if( c > 2 )
		c = c & 1; // 3 -> 1, 4 -> 0
	
	uint32_t mask = 1 << (c * 5 + r);
	return (code & mask) != 0;
}