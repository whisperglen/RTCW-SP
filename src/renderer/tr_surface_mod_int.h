
#ifndef _TR_SURFACE_MOD_INT
#define _TR_SURFACE_MOD_INT

#include "tr_surface_mod.h"
#include "mINI/ini.h"
#include "tr_debughelpers.h"

#ifdef __cplusplus

#include <string>
#include <vector>
#include <unordered_map>

typedef struct aabb_group_s
{
	std::vector<aabb_store_t> storage;
	std::unordered_map<const void*, uint32_t> indexes;
	bitmaskx_t marked_indexes;
	size_t num_marks;
}  aabb_group_t;

void qdx_surface_aabb_prune_storage( aabb_group_t* grp );

void qdx_surface_replacements_load( mINI::INIStructure &ini, const char *mapname );

#endif
#endif
