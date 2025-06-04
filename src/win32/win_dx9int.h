
#ifndef _WIN_DX9INT_H
#define _WIN_DX9INT_H

#include "qdx9.h"
#include "remix/bridge_remix_api.h"

typedef union uint64_comp_u
{
	uint64_t ll;
	uint32_t u32[2];
} uint64_comp_t;

typedef struct light_override_s
{
	uint64_t hash;
	float position_offset[3];
	float color[3];
	float radiance_base;
	float radiance_scale;
	float radius_base;
	float radius_scale;
	float volumetric_scale;
	BOOL updated;
} light_override_t;

extern remixapi_Interface remixInterface;
extern BOOL remixOnline;

extern void qdx_draw_init(void *hwnd, void *device);
extern void qdx_before_frame_end();
extern void qdx_frame_ended();
extern void qdx_clear_buffers();
extern void qdx_texobj_delete_all();
extern void qdx_lights_draw();
extern void qdx_imgui_init(void *hwnd, void *device);
extern void qdx_imgui_draw();
extern void qdx_imgui_deinit();

extern void qdx_log_comment(const char *name, UINT vattbits, const void *ptr);
extern void qdx_log_matrix(const char *name, const float *mat);
extern void qdx_log_dump_buffer(const char* name, const uint32_t* buffer, uint32_t size, int wide, uint32_t hashid, uint32_t hashbuf);

#define MINI_CASE_SENSITIVE
#include "ini.h"

const char* qdx_get_active_map();
mINI::INIStructure& qdx_get_iniconf();
void qdx_save_iniconf();

int qdx_readsetting( const char* valname, int default );
int qdx_readmapconf( const char* base, const char* valname );
void* qdx_readmapconfptr( const char* base, const char* valname );
float qdx_readmapconfflt( const char* base, const char* valname, float default );
int qdx_readmapconfstr( const char* base, const char* valname, char *out, int outsz );

void qdx_storemapconfflt( const char* base, const char* valname, float value, bool inGlobal = true );

void qdx_lights_load(mINI::INIStructure &ini, const char *mapname);
void qdx_flashlight_save();
void qdx_radiance_save( bool inGlobal );
void qdx_light_override_save( light_override_t* ovr );

#define NUM_FLASHLIGHT_HND 3
float* qdx_4imgui_radiance_dynamic_1f();
float* qdx_4imgui_radiance_dynamic_scale_1f();
float* qdx_4imgui_radiance_coronas_1f();
float* qdx_4imgui_radius_dynamic_1f();
float* qdx_4imgui_radius_dynamic_scale_1f();
float* qdx_4imgui_radius_coronas_1f();
float* qdx_4imgui_flashlight_radiance_1f(int idx);
float* qdx_4imgui_flashlight_colors_3f(int idx);
float* qdx_4imgui_flashlight_coneangles_1f(int idx);
float* qdx_4imgui_flashlight_conesoft_1f(int idx);
float* qdx_4imgui_flashlight_volumetric_1f( int idx );
const float* qdx_4imgui_flashlight_position_3f();
const float* qdx_4imgui_flashlight_direction_3f();
float* qdx_4imgui_flashlight_position_off_3f();
float* qdx_4imgui_flashlight_direction_off_3f();

void REMIXAPI_PTR qdx_light_pick_callback( const uint32_t* objectPickingValues_values, uint32_t objectPickingValues_count, void* callbackUserData );
void qdx_light_scan_closest_lights( light_type_e type );
uint64_t qdx_4imgui_light_picking_id( uint32_t idx );
int qdx_4imgui_light_picking_count();
void qdx_4imgui_light_picking_clear();
light_override_t* qdx_4imgui_light_get_override( uint64_t hash, light_type_e type );
void qdx_4imgui_light_clear_override( uint64_t hash );

#endif