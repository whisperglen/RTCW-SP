
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


#define MAX_LIGHTS_REMIX 100
#define MAX_LIGHTS_DX9 8
typedef struct light_data
{
	uint64_t hash;
	bool isRemix;
	union
	{
		DWORD number;
		remixapi_LightHandle handle;
	};
	float distance;
} light_data_t;

static int MAX_LIGHTS = 0;
std::map<uint64_t, light_data_t> g_lights_dynamic;
std::map<uint64_t, light_data_t> g_lights_flares;
static int g_lights_number = 0;
#define NUM_FLASHLIGHT_HND 3
static remixapi_LightHandle g_flashlight_handle[NUM_FLASHLIGHT_HND] = { 0 };
#define FLASHLIGHT_HASH 0xF1A581168700ULL

//no idea what to choose here
#define LIGHT_RADIANCE_DYNAMIC 15000.0f
#define LIGHT_RADIANCE_FLARES 300.0f
static float LIGHT_RADIANCE_FLASHLIGHT[NUM_FLASHLIGHT_HND] = { 400.0f, 960.0f, 4000.0f };
#define LIGHT_RADIANCE_KILL_REDFLARES 1.8f

void qdx_lights_load( enum light_type type, mINI::INIMap<std::string> *opts )
{

}

static void qdx_light_color_to_radiance(remixapi_Float3D* rad, int light_type, uint64_t hash, const vec3_t color, float scale)
{
	float radiance = 1.0f;
	if (light_type == LIGHT_DYNAMIC)
	{
		radiance = LIGHT_RADIANCE_DYNAMIC * scale;
	}
	else if (light_type == LIGHT_CORONA)
	{
		if ((color[0] == 1.0f) && (color[1] == 0.0f) && (color[2] == 0.0f))
		{
			radiance = LIGHT_RADIANCE_KILL_REDFLARES;
		}
		else
		{
			radiance = LIGHT_RADIANCE_FLARES;
		}
	}
	else if ( light_type == LIGHT_FLASHLIGHT )
	{
		int idx = (hash & 0xFF) -1;
		if ( idx < 0 || idx >= NUM_FLASHLIGHT_HND ) idx = 0;
		radiance = LIGHT_RADIANCE_FLASHLIGHT[idx];
	}

	rad->x = radiance * color[0];
	rad->y = radiance * color[1];
	rad->z = radiance * color[2];
}

void qdx_lights_clear(unsigned int light_types)
{
	if (qdx.device)
	{
		if (light_types & LIGHT_DYNAMIC)
		{
			for (auto it = g_lights_dynamic.begin(); it != g_lights_dynamic.end(); it++)
			{
				if (remixOnline && it->second.isRemix)
				{
					remixInterface.DestroyLight(it->second.handle);
				}
				else
				{
					qdx.device->LightEnable(it->second.number, FALSE);
				}
			}
			g_lights_dynamic.clear();
		}
		if (light_types & LIGHT_CORONA)
		{
			for (auto it = g_lights_flares.begin(); it != g_lights_flares.end(); it++)
			{
				if (remixOnline && it->second.isRemix)
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
		if ( light_types & LIGHT_FLASHLIGHT && remixOnline )
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
	for (auto it = g_lights_dynamic.begin(); it != g_lights_dynamic.end(); it++)
	{
		if (remixOnline && it->second.isRemix)
		{
			remixInterface.DrawLightInstance(it->second.handle);
		}
		else
		{
			qdx.device->LightEnable(it->second.number, TRUE);
		}
	}
	if ( remixOnline )
	{
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
}

#define DYNAMIC_LIGHTS_USE_DX9 1

void qdx_light_add(int light_type, int ord, float *position, float *direction, float *color, float radius, float scale)
{
	remixapi_ErrorCode rercd;
	uint64_t hash = 0;
	uint64_t hashpos = 0;
	uint64_t hashcolor = 0;
	uint64_t hashord = 0;
	bool found = false;

	//remixOnline = false;

	hashpos = fnv_32a_buf(position, 3*sizeof(float), 0x55FF);
	hashcolor = fnv_32a_buf(color, 3*sizeof(float), 0x55FF);
	hashord = fnv_32a_buf(&ord, sizeof(ord), 0x55FF);

	if (light_type == LIGHT_DYNAMIC)
	{
		hash = hashord;
		//return;
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
		if ( !remixOnline || !r_rmx_flashlight->integer )
		{
			return;
		}

		hash = FLASHLIGHT_HASH;
		const float flcolor[NUM_FLASHLIGHT_HND][3] = {
			{ 0.9f, 0.98f, 1.0f },
			{ 1.0f, 0.95f, 0.9f },
			{ 0.9f, 1.0f, 0.97f }
		};
		const float coneAngle[NUM_FLASHLIGHT_HND] = { 32.0f, 21.0f, 18.0f }; //{ 40.0f, 28.0f, 24.0f };
		const float conseSoft[NUM_FLASHLIGHT_HND] = { 0.05f, 0.02f, 0.02f };

		if ( 0 && g_flashlight_handle ) //destroying light creates artifacts; light props get updated regardless
		{
			qdx_lights_clear( LIGHT_FLASHLIGHT );
		}

		for ( int i = 0; i < NUM_FLASHLIGHT_HND; i++ )
		{

			remixapi_LightInfoSphereEXT light_sphere;
			ZeroMemory( &light_sphere, sizeof( light_sphere ) );

			light_sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
			light_sphere.position.x = position[0];
			light_sphere.position.y = position[1];
			light_sphere.position.z = position[2];
			light_sphere.radius = 1.0f;
			light_sphere.volumetricRadianceScale = 1.0f;

			light_sphere.shaping_hasvalue = 1;
			{
				light_sphere.shaping_value.direction.x = direction[0];
				light_sphere.shaping_value.direction.y = direction[1];
				light_sphere.shaping_value.direction.z = direction[2];
				light_sphere.shaping_value.coneAngleDegrees = coneAngle[i];
				light_sphere.shaping_value.coneSoftness = conseSoft[i];
				light_sphere.shaping_value.focusExponent = 0.0f;
			}

			uint64_t fhash = FLASHLIGHT_HASH + i + 1;

			remixapi_LightInfo lightinfo;
			ZeroMemory( &lightinfo, sizeof( lightinfo ) );

			lightinfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
			lightinfo.pNext = &light_sphere;
			lightinfo.hash = fhash;
			qdx_light_color_to_radiance( &lightinfo.radiance, light_type, fhash, flcolor[i], 1.0 );

			rercd = remixInterface.CreateLight( &lightinfo, &g_flashlight_handle[i] );
			if ( rercd != REMIXAPI_ERROR_CODE_SUCCESS )
			{
				ri.Printf( PRINT_ERROR, "RMX failed to create flashlight %d\n", rercd );
				return;
			}
		}

		return;
	}

	if (ord == 0 && light_type == LIGHT_DYNAMIC)
	{
		qdx_lights_clear(LIGHT_DYNAMIC);
	}

	light_data_t light_store;
	ZeroMemory(&light_store, sizeof(light_store));
	light_store.hash = hash;
	//light_store.distance = 0.0f;

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
		g_lights_dynamic[hash] = light_store;

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
		light_sphere.radius = 2.0f;
		light_sphere.shaping_hasvalue = 0;
		//light_sphere.volumetricRadianceScale = 1.0f;

		remixapi_LightInfo lightinfo;
		ZeroMemory(&lightinfo, sizeof(lightinfo));

		lightinfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
		lightinfo.pNext = &light_sphere;
		lightinfo.hash = hash;
		qdx_light_color_to_radiance(&lightinfo.radiance, light_type, hash, color, scale);

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
			g_lights_dynamic[hash] = light_store;
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
