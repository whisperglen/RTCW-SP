
#include "../renderer/qdx9.h"
#include "win_dx9int.h"
extern "C"
{
#include "../renderer/tr_local.h"
#include "../qcommon/qcommon.h"
#include "resource.h"
#include "win_local.h"
}
#include <stdint.h>
#include "fnv.h"

#include <string>
#include <map>


#define LIGHTS_DYNAMIC_SZ 15
#define MAX_LIGHTS_DX9 8

typedef struct light_data
{
	//uint8_t dataValid; //moved as bitfield to g_lights_dynamic_bitmap
	uint8_t updatedThisFrame;
	uint8_t isRemix;
	union
	{
		DWORD number;
		remixapi_LightHandle handle;
	};
	uint64_t hash;
	vec3_t pos;
} light_data_t;

#define LIGHT_DATA_DO_COPY(D, S) \
	D->isRemix = S->isRemix; \
	D->handle = S->handle; \
	D->hash = S->hash; \
	D->pos[0] = S->pos[0]; \
	D->pos[1] = S->pos[1]; \
	D->pos[2] = S->pos[2];

static light_data_t g_lights_dynamic[LIGHTS_DYNAMIC_SZ] = { 0 };
static uint32_t g_lights_dynamic_bitmap = 0;
static std::map<uint64_t, light_data_t> g_lights_flares;
static remixapi_LightHandle g_flashlight_handle[NUM_FLASHLIGHT_HND] = { 0 };
#define FLASHLIGHT_HASH 0xF1A581168700ULL

//no idea what to choose here
static float LIGHT_RADIANCE_DYNAMIC_BASE = 150.0f;
static float LIGHT_RADIANCE_DYNAMIC_SCALE = 0.0f;
static float LIGHT_RADIANCE_CORONAS = 300.0f;
static float LIGHT_RADIUS[2] = { 2.0f, 2.0f };
static float LIGHT_RADIUS_DYNAMIC_SCALE = 0.03f;
static float LIGHT_RADIANCE_FLASHLIGHT[NUM_FLASHLIGHT_HND] = { 400.0f, 960.0f, 4000.0f };
static float FLASHLIGHT_COLORS[NUM_FLASHLIGHT_HND][3] = {
	{ 0.9f, 0.98f, 1.0f },
	{ 1.0f, 0.95f, 0.9f },
	{ 0.9f, 1.0f, 0.97f }
};
static float FLASHLIGHT_CONE_ANGLES[NUM_FLASHLIGHT_HND] = { 32.0f, 21.0f, 18.0f }; //{ 40.0f, 28.0f, 24.0f };
static float FLASHLIGHT_CONE_SOFTNESS[NUM_FLASHLIGHT_HND] = { 0.05f, 0.02f, 0.02f };
static float FLASHLIGHT_POSITION_CACHE[3] = { 0 };
static float FLASHLIGHT_DIRECTION_CACHE[3] = { 0 };
static float FLASHLIGHT_POSITION_OFFSET[3] = { -8, 1, -6 };
static float FLASHLIGHT_DIRECTION_OFFSET[3] = { 0.095f, -0.08f, 0 };

float* qdx_4imgui_radiance_dynamic_1f() { return &LIGHT_RADIANCE_DYNAMIC_BASE; }
float* qdx_4imgui_radiance_dynamic_scale_1f() { return &LIGHT_RADIANCE_DYNAMIC_SCALE; }
float* qdx_4imgui_radiance_coronas_1f() { return &LIGHT_RADIANCE_CORONAS; }
float* qdx_4imgui_radius_dynamic_1f() { return &LIGHT_RADIUS[0]; }
float* qdx_4imgui_radius_dynamic_scale_1f() { return &LIGHT_RADIUS_DYNAMIC_SCALE; }
float* qdx_4imgui_radius_coronas_1f() { return &LIGHT_RADIUS[1]; }
float* qdx_4imgui_flashlight_radiance_1f(int idx) { return &LIGHT_RADIANCE_FLASHLIGHT[idx]; }
float* qdx_4imgui_flashlight_colors_3f(int idx) { return &FLASHLIGHT_COLORS[idx][0]; }
float* qdx_4imgui_flashlight_coneangles_1f(int idx) { return &FLASHLIGHT_CONE_ANGLES[idx]; }
float* qdx_4imgui_flashlight_conesoft_1f(int idx) { return &FLASHLIGHT_CONE_SOFTNESS[idx]; }
const float* qdx_4imgui_flashlight_position_3f() { return &FLASHLIGHT_POSITION_CACHE[0]; }
const float* qdx_4imgui_flashlight_direction_3f() { return &FLASHLIGHT_DIRECTION_CACHE[0]; }
float* qdx_4imgui_flashlight_position_off_3f() { return &FLASHLIGHT_POSITION_OFFSET[0]; }
float* qdx_4imgui_flashlight_direction_off_3f() { return &FLASHLIGHT_DIRECTION_OFFSET[0]; }

#define LIGHT_RADIANCE_KILL_REDFLARES 1.8f

struct color_override_data_s
{
	float src[3];
	float dst[3];
};

std::vector<struct color_override_data_s> color_overrides;

#define SECTION_LIGHTS "lights"
#define SECTION_FLASHLIGHT "flashlight"
#define SECTION_LIGHTS_COLOR_OVERRIDE "lights_color_override"

void qdx_flashlight_save()
{
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "PositionOff_0", FLASHLIGHT_POSITION_OFFSET[0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "PositionOff_1", FLASHLIGHT_POSITION_OFFSET[1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "PositionOff_2", FLASHLIGHT_POSITION_OFFSET[2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "DirectionOff_0", FLASHLIGHT_DIRECTION_OFFSET[0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "DirectionOff_1", FLASHLIGHT_DIRECTION_OFFSET[1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "DirectionOff_2", FLASHLIGHT_DIRECTION_OFFSET[2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_1", LIGHT_RADIANCE_FLASHLIGHT[0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_2", LIGHT_RADIANCE_FLASHLIGHT[1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_3", LIGHT_RADIANCE_FLASHLIGHT[2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_1", FLASHLIGHT_COLORS[0][0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_1", FLASHLIGHT_COLORS[0][1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_1", FLASHLIGHT_COLORS[0][2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_2", FLASHLIGHT_COLORS[1][0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_2", FLASHLIGHT_COLORS[1][1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_2", FLASHLIGHT_COLORS[1][2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_3", FLASHLIGHT_COLORS[2][0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_3", FLASHLIGHT_COLORS[2][1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_3", FLASHLIGHT_COLORS[2][2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_1", FLASHLIGHT_CONE_ANGLES[0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_2", FLASHLIGHT_CONE_ANGLES[1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_3", FLASHLIGHT_CONE_ANGLES[2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_1", FLASHLIGHT_CONE_SOFTNESS[0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_2", FLASHLIGHT_CONE_SOFTNESS[1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_3", FLASHLIGHT_CONE_SOFTNESS[2] );

	qdx_save_iniconf();
}

void qdx_radiance_save( bool inGlobal )
{
	qdx_storemapconfflt( SECTION_LIGHTS, "DynamicRadiance", LIGHT_RADIANCE_DYNAMIC_BASE, inGlobal );
	qdx_storemapconfflt( SECTION_LIGHTS, "DynamicRadianceScale", LIGHT_RADIANCE_DYNAMIC_SCALE, inGlobal );
	qdx_storemapconfflt( SECTION_LIGHTS, "DynamicRadius", LIGHT_RADIUS[0], inGlobal );
	qdx_storemapconfflt( SECTION_LIGHTS, "DynamicRadiusScale", LIGHT_RADIUS_DYNAMIC_SCALE, inGlobal );
	qdx_storemapconfflt( SECTION_LIGHTS, "CoronaRadiance", LIGHT_RADIANCE_CORONAS, inGlobal );
	qdx_storemapconfflt( SECTION_LIGHTS, "CoronaRadius", LIGHT_RADIUS[1], inGlobal );

	qdx_save_iniconf();
}

void qdx_lights_load( mINI::INIStructure &ini, const char *mapname )
{
	LIGHT_RADIANCE_DYNAMIC_BASE = qdx_readmapconfflt( SECTION_LIGHTS, "DynamicRadiance", LIGHT_RADIANCE_DYNAMIC_BASE );
	LIGHT_RADIANCE_DYNAMIC_SCALE = qdx_readmapconfflt( SECTION_LIGHTS, "DynamicRadianceScale", LIGHT_RADIANCE_DYNAMIC_SCALE );
	LIGHT_RADIUS[0] = qdx_readmapconfflt( SECTION_LIGHTS, "DynamicRadius", LIGHT_RADIUS[0] );
	LIGHT_RADIUS_DYNAMIC_SCALE = qdx_readmapconfflt( SECTION_LIGHTS, "DynamicRadiusScale", LIGHT_RADIUS_DYNAMIC_SCALE );
	LIGHT_RADIANCE_CORONAS = qdx_readmapconfflt( SECTION_LIGHTS, "CoronaRadiance", LIGHT_RADIANCE_CORONAS );
	LIGHT_RADIUS[1] = qdx_readmapconfflt( SECTION_LIGHTS, "CoronaRadius", LIGHT_RADIUS[1] );

	LIGHT_RADIANCE_FLASHLIGHT[0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_1", LIGHT_RADIANCE_FLASHLIGHT[0] );
	LIGHT_RADIANCE_FLASHLIGHT[1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_2", LIGHT_RADIANCE_FLASHLIGHT[1] );
	LIGHT_RADIANCE_FLASHLIGHT[2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_3", LIGHT_RADIANCE_FLASHLIGHT[2] );
	FLASHLIGHT_COLORS[0][0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_1", FLASHLIGHT_COLORS[0][0] );
	FLASHLIGHT_COLORS[0][1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_1", FLASHLIGHT_COLORS[0][1] );
	FLASHLIGHT_COLORS[0][2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_1", FLASHLIGHT_COLORS[0][2] );
	FLASHLIGHT_COLORS[1][0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_2", FLASHLIGHT_COLORS[1][0] );
	FLASHLIGHT_COLORS[1][1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_2", FLASHLIGHT_COLORS[1][1] );
	FLASHLIGHT_COLORS[1][2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_2", FLASHLIGHT_COLORS[1][2] );
	FLASHLIGHT_COLORS[2][0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_3", FLASHLIGHT_COLORS[2][0] );
	FLASHLIGHT_COLORS[2][1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_3", FLASHLIGHT_COLORS[2][1] );
	FLASHLIGHT_COLORS[2][2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_3", FLASHLIGHT_COLORS[2][2] );
	FLASHLIGHT_CONE_ANGLES[0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_1", FLASHLIGHT_CONE_ANGLES[0] );
	FLASHLIGHT_CONE_ANGLES[1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_2", FLASHLIGHT_CONE_ANGLES[1] );
	FLASHLIGHT_CONE_ANGLES[2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_3", FLASHLIGHT_CONE_ANGLES[2] );
	FLASHLIGHT_CONE_SOFTNESS[0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_1", FLASHLIGHT_CONE_SOFTNESS[0] );
	FLASHLIGHT_CONE_SOFTNESS[1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_2", FLASHLIGHT_CONE_SOFTNESS[1] );
	FLASHLIGHT_CONE_SOFTNESS[2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_3", FLASHLIGHT_CONE_SOFTNESS[2] );
	FLASHLIGHT_POSITION_OFFSET[0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "PositionOff_0", FLASHLIGHT_POSITION_OFFSET[0] );
	FLASHLIGHT_POSITION_OFFSET[1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "PositionOff_1", FLASHLIGHT_POSITION_OFFSET[1] );
	FLASHLIGHT_POSITION_OFFSET[2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "PositionOff_2", FLASHLIGHT_POSITION_OFFSET[2] );
	FLASHLIGHT_DIRECTION_OFFSET[0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "DirectionOff_0", FLASHLIGHT_DIRECTION_OFFSET[0] );
	FLASHLIGHT_DIRECTION_OFFSET[1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "DirectionOff_1", FLASHLIGHT_DIRECTION_OFFSET[1] );
	FLASHLIGHT_DIRECTION_OFFSET[2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "DirectionOff_2", FLASHLIGHT_DIRECTION_OFFSET[2] );

	if ( ini.has( SECTION_LIGHTS_COLOR_OVERRIDE ) )
	{
		int i = 0; int ercd;
		struct color_override_data_s data;
		mINI::INIMap<std::string> &ovr = ini[ SECTION_LIGHTS_COLOR_OVERRIDE ];

		color_overrides.reserve( ovr.size() / 2 );
		for ( auto it = ovr.begin(); it != ovr.end(); it++ )
		{
			if ( it->first.find( "source" ) == std::string::npos ) continue;
			ercd = sscanf( it->second.c_str(), "%f, %f, %f", &data.src[0], &data.src[1], &data.src[2] );
			if ( ercd != 3 ) continue;

			it++;
			if ( it->first.find( "new" ) == std::string::npos ) continue;
			ercd = sscanf( it->second.c_str(), "%f, %f, %f", &data.dst[0], &data.dst[1], &data.dst[2] );
			if ( ercd != 3 ) continue;

			color_overrides.push_back( data );
			i++;
		}
	}
}

static void qdx_light_color_to_radiance(remixapi_Float3D* rad, int light_type, uint64_t hash, const vec3_t color, float scale)
{
	float radiance = 1.0f;
	if (light_type == LIGHT_DYNAMIC)
	{
		radiance = LIGHT_RADIANCE_DYNAMIC_BASE + LIGHT_RADIANCE_DYNAMIC_SCALE * scale;

		rad->x = radiance * color[0];
		rad->y = radiance * color[1];
		rad->z = radiance * color[2];
	}
	else if (light_type == LIGHT_CORONA)
	{
		radiance = LIGHT_RADIANCE_CORONAS;

		for ( auto it = color_overrides.begin(); it != color_overrides.end(); it++ )
		{
			if ( (color[0] == it->src[0]) && (color[1] == it->src[1]) && (color[2] == it->src[2]) )
			{
				rad->x = radiance * it->dst[0];
				rad->y = radiance * it->dst[1];
				rad->z = radiance * it->dst[2];

				return;
			}
		}

		rad->x = radiance * color[0];
		rad->y = radiance * color[1];
		rad->z = radiance * color[2];
	}
	else if ( light_type == LIGHT_FLASHLIGHT )
	{
		int idx = (hash & 0xFF) -1;
		if ( idx < 0 || idx >= NUM_FLASHLIGHT_HND ) idx = 0;
		radiance = LIGHT_RADIANCE_FLASHLIGHT[idx];

		rad->x = radiance * color[0];
		rad->y = radiance * color[1];
		rad->z = radiance * color[2];
	}
}

static float qdx_light_radius( int light_type, int radius )
{
	float ret = 2.0f;
	
	if ( light_type == LIGHT_DYNAMIC )
	{
		ret = LIGHT_RADIUS[0] + LIGHT_RADIUS_DYNAMIC_SCALE * radius;
	}
	else if ( light_type )
	{
		ret = LIGHT_RADIUS[1];
	}

	return ret;
}

void qdx_store_dynamic_light( const light_data_t* data )
{
	uint32_t keep_searching = g_lights_dynamic_bitmap;
	light_data_t* l = &g_lights_dynamic[0];
	light_data_t* sto = NULL;
	uint32_t sto_bit = 0;
	uint32_t bit = 1;
	for ( int i = 0; (keep_searching || sto == NULL) && i < LIGHTS_DYNAMIC_SZ; i++, l++, bit <<= 1 )
	{
		if ( g_lights_dynamic_bitmap & bit )
		{
			if ( l->hash == data->hash )
			{
				l->updatedThisFrame = TRUE;
				LIGHT_DATA_DO_COPY( l, data );
				return;
			}
			keep_searching &= ~bit;
		}
		else
		{
			if ( sto == NULL )
			{
				sto = l;
				sto_bit = bit;
			}
		}
	}
	if ( sto != NULL )
	{
		g_lights_dynamic_bitmap |= sto_bit;
		sto->updatedThisFrame = TRUE;
		LIGHT_DATA_DO_COPY( sto, data );
	}
	else
	{
		qassert( 0 && "Not enough Dynamic light slots!" );
	}
}

void qdx_lights_clear(unsigned int light_types)
{
	if (qdx.device)
	{
		if (light_types & LIGHT_DYNAMIC)
		{
			light_data_t* l = &g_lights_dynamic[0];
			uint32_t keep_searching = g_lights_dynamic_bitmap;
			uint32_t bit = 1;
			for (int i = 0; keep_searching && i < LIGHTS_DYNAMIC_SZ; i++, l++, bit <<= 1)
			{
				if ( g_lights_dynamic_bitmap & bit )
				{
					if ( l->isRemix )
					{
						remixInterface.DestroyLight( l->handle );
					}
					else
					{
						qdx.device->LightEnable( l->number, FALSE );
					}
					g_lights_dynamic_bitmap &= ~bit;
					keep_searching &= ~bit;
				}
			}
		}
		if (light_types & LIGHT_CORONA)
		{
			for (auto it = g_lights_flares.begin(); it != g_lights_flares.end(); it++)
			{
				if (it->second.isRemix)
				{
					remixInterface.DestroyLight(it->second.handle);
				}
				else
				{
					qdx.device->LightEnable(it->second.number, FALSE);
				}
			}
			g_lights_flares.clear();
		}
		if ( light_types & LIGHT_FLASHLIGHT )
		{
			for ( int i = 0; i < NUM_FLASHLIGHT_HND; i++ )
			{
				if ( g_flashlight_handle[i] )
				{
					remixInterface.DestroyLight( g_flashlight_handle[i] );
					g_flashlight_handle[i] = 0;
				}
			}
		}
	}
}

void qdx_lights_draw()
{	
	//draw dynamic lights, handle both DX9 case or remix
	light_data_t* l = &g_lights_dynamic[0];
	uint32_t keep_searching = g_lights_dynamic_bitmap;
	uint32_t bit = 1;
	for (int i = 0; keep_searching && i < LIGHTS_DYNAMIC_SZ; i++, l++, bit <<= 1)
	{
		if ( g_lights_dynamic_bitmap & bit )
		{
			if ( l->updatedThisFrame )
			{
				l->updatedThisFrame = FALSE;
				if ( l->isRemix )
				{
					remixInterface.DrawLightInstance( l->handle );
				}
				else
				{
					qdx.device->LightEnable( l->number, TRUE );
				}
			}
			else
			{
				g_lights_dynamic_bitmap &= ~bit;
				if ( l->isRemix )
				{
					remixInterface.DestroyLight( l->handle );
				}
				else
				{
					qdx.device->LightEnable( l->number, FALSE );
				}
			}
			keep_searching &= ~bit;
		}
	}

	//draw corona lights and flashlight
	for ( auto it = g_lights_flares.begin(); it != g_lights_flares.end(); it++ )
	{
		remixInterface.DrawLightInstance( it->second.handle );
	}
	if ( r_rmx_flashlight->integer )
	{
		for ( int i = 0; i < NUM_FLASHLIGHT_HND; i++ )
		{
			if ( g_flashlight_handle[i] )
			{
				remixInterface.DrawLightInstance( g_flashlight_handle[i] );
			}
		}
	}
}

#define DYNAMIC_LIGHTS_USE_DX9 0

void qdx_light_add(int light_type, int ord, const float *position, const float *direction, const float *color, float radius, float scale)
{
	remixapi_ErrorCode rercd;
	uint64_t hash = 0;
	uint64_t hashpos = 0;
	uint64_t hashcolor = 0;
	uint64_t hashord = 0;

	//remixOnline = false;

	hashpos = fnv_32a_buf(position, 3*sizeof(float), 0x55FF);
	hashcolor = fnv_32a_buf(color, 3*sizeof(float), 0x55FF);
	hashord = fnv_32a_buf(&ord, sizeof(ord), 0x55FF);

	if (light_type == LIGHT_DYNAMIC)
	{
#if DYNAMIC_LIGHTS_USE_DX9
		hash = hashord;
#else
		hash = hashpos;
#endif
	}
	if (light_type == LIGHT_CORONA)
	{
		hash = hashpos;
		auto itm = g_lights_flares.find(hash);
		if (itm != g_lights_flares.end())
		{
			//flare already exists, nothing to do
			return;
		}
	}
	if ( light_type == LIGHT_FLASHLIGHT )
	{
		//stuff for imgui
		FLASHLIGHT_POSITION_CACHE[0] = position[0];
		FLASHLIGHT_POSITION_CACHE[1] = position[1];
		FLASHLIGHT_POSITION_CACHE[2] = position[2];
		FLASHLIGHT_DIRECTION_CACHE[0] = direction[0];
		FLASHLIGHT_DIRECTION_CACHE[1] = direction[1];
		FLASHLIGHT_DIRECTION_CACHE[2] = direction[2];

		if ( !remixOnline || !r_rmx_flashlight->integer )
		{
			return;
		}

		hash = FLASHLIGHT_HASH;

		if ( 0 && g_flashlight_handle ) //destroying a light creates artifacts; light props get updated regardless
		{
			qdx_lights_clear( LIGHT_FLASHLIGHT );
		}

		for ( int i = 0; i < NUM_FLASHLIGHT_HND; i++ )
		{

			remixapi_LightInfoSphereEXT light_sphere;
			ZeroMemory( &light_sphere, sizeof( light_sphere ) );

			D3DXVECTOR3 tpos, tdir;
			D3DXMATRIX invview, viewrot(qdx.camera);
			viewrot._41 = 0.0;
			viewrot._42 = 0.0;
			viewrot._43 = 0.0;
			D3DXMatrixInverse( &invview, NULL, (D3DXMATRIX*)&viewrot );
			D3DXVec3TransformCoord( &tpos, (D3DXVECTOR3*)FLASHLIGHT_POSITION_OFFSET, &invview );
			D3DXVec3TransformNormal( &tdir, (D3DXVECTOR3*)FLASHLIGHT_DIRECTION_OFFSET, &invview );

			light_sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
			light_sphere.position.x = tpos.x + position[0];
			light_sphere.position.y = tpos.y + position[1];
			light_sphere.position.z = tpos.z + position[2];
			light_sphere.radius = 1.0f;
			light_sphere.volumetricRadianceScale = 1.0f;

			light_sphere.shaping_hasvalue = 1;
			{
				tdir.x += direction[0];
				tdir.y += direction[1];
				tdir.z += direction[2];
				D3DXVec3Normalize( (D3DXVECTOR3*)&light_sphere.shaping_value.direction, &tdir );
				light_sphere.shaping_value.coneAngleDegrees = FLASHLIGHT_CONE_ANGLES[i];
				light_sphere.shaping_value.coneSoftness = FLASHLIGHT_CONE_SOFTNESS[i];
				light_sphere.shaping_value.focusExponent = 0.0f;
			}

			uint64_t fhash = FLASHLIGHT_HASH + i + 1;

			remixapi_LightInfo lightinfo;
			ZeroMemory( &lightinfo, sizeof( lightinfo ) );

			lightinfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
			lightinfo.pNext = &light_sphere;
			lightinfo.hash = fhash;
			qdx_light_color_to_radiance( &lightinfo.radiance, light_type, fhash, FLASHLIGHT_COLORS[i], 1.0 );

			rercd = remixInterface.CreateLight( &lightinfo, &g_flashlight_handle[i] );
			if ( rercd != REMIXAPI_ERROR_CODE_SUCCESS )
			{
				ri.Printf( PRINT_ERROR, "RMX failed to create flashlight %d\n", rercd );
				return;
			}
		}

		return;
	}

	light_data_t light_store;
	ZeroMemory(&light_store, sizeof(light_store));
	light_store.hash = hash;

#if DYNAMIC_LIGHTS_USE_DX9
	if (light_type == LIGHT_DYNAMIC)
	{
		D3DLIGHT9 light;
		ZeroMemory(&light, sizeof(light));

		light.Type = D3DLIGHT_POINT;
		light.Diffuse.r = color[0];
		light.Diffuse.g = color[1];
		light.Diffuse.b = color[2];
		light.Diffuse.a = 1.0f;
		light.Specular.r = 1.0f;
		light.Specular.g = 1.0f;
		light.Specular.b = 1.0f;
		light.Specular.a = 1.0f;
		light.Ambient.r = 1.0f;
		light.Ambient.g = 1.0f;
		light.Ambient.b = 1.0f;
		light.Ambient.a = 1.0f;
		light.Position.x = position[0];
		light.Position.y = position[1];
		light.Position.z = position[2];

		light.Range = 2 * radius;
		light.Attenuation0 = 1.75f;
		light.Attenuation1 = scale;
		light.Attenuation2 = 0.0f;

		qdx.device->SetLight(ord, &light);
		qdx.device->LightEnable(ord, TRUE);

		light_store.isRemix = false;
		light_store.number = ord;
		qdx_store_dynamic_light(&light_store);

		return;
	}
#endif

	if (remixOnline)
	{
		remixapi_LightInfoSphereEXT light_sphere;
		ZeroMemory(&light_sphere, sizeof(light_sphere));

		light_sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
		light_sphere.position.x = position[0];
		light_sphere.position.y = position[1];
		light_sphere.position.z = position[2];
		light_sphere.radius = qdx_light_radius(light_type, radius);
		light_sphere.shaping_hasvalue = 0;
		//light_sphere.volumetricRadianceScale = 1.0f;

		remixapi_LightInfo lightinfo;
		ZeroMemory(&lightinfo, sizeof(lightinfo));

		lightinfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
		lightinfo.pNext = &light_sphere;
		lightinfo.hash = hash;
		qdx_light_color_to_radiance(&lightinfo.radiance, light_type, hash, color, radius);

		light_store.isRemix = true;
		rercd = remixInterface.CreateLight(&lightinfo, &light_store.handle);
		if (rercd != REMIXAPI_ERROR_CODE_SUCCESS)
		{
			ri.Printf(PRINT_ERROR, "RMX failed to create light %d\n", rercd);
			return;
		}
		switch (light_type)
		{
		case LIGHT_DYNAMIC:
			qdx_store_dynamic_light(&light_store);
			break;
		case LIGHT_CORONA:
			g_lights_flares[hash] = light_store;
			break;
		default:
			qassert(FALSE && "Unknown light type");
		}
	}
	else
	{
		//D3DLIGHT9 light;
		//ZeroMemory(&light, sizeof(light));

		//light.Type = D3DLIGHT_POINT;
		//light.Diffuse.r = color[0];
		//light.Diffuse.g = color[1];
		//light.Diffuse.b = color[2];
		//light.Diffuse.a = 1.0f;
		//light.Specular.r = 1.0f;
		//light.Specular.g = 1.0f;
		//light.Specular.b = 1.0f;
		//light.Specular.a = 1.0f;
		//light.Ambient.r = 1.0f;
		//light.Ambient.g = 1.0f;
		//light.Ambient.b = 1.0f;
		//light.Ambient.a = 1.0f;
		//light.Position.x = transformed[0];
		//light.Position.y = transformed[1];
		//light.Position.z = transformed[2];

		//switch (light_type)
		//{
		//case LIGHT_DYNAMIC:
		//	light.Range = 2 * radius;
		//	light.Attenuation0 = 0.5f;
		//	light.Attenuation1 = scale;
		//	light.Attenuation2 = 0.01f;

		//	break;
		//case LIGHT_CORONA:
		//	light.Range = 6;
		//	light.Attenuation0 = 1.5f;
		//	light.Attenuation1 = 0.5;
		//	light.Attenuation2 = 0.0f;

		//	break;
		//}

		//qdx.device->SetLight(light_num, &light);
		//qdx.device->LightEnable(light_num, TRUE);
	}
}
