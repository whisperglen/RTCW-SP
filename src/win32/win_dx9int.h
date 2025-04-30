
#ifndef _WIN_DX9INT_H
#define _WIN_DX9INT_H

#include "qdx9.h"
#include "remix/bridge_remix_api.h"

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

void qdx_lights_load(mINI::INIStructure &ini);

#define NUM_FLASHLIGHT_HND 3
float* qdx_4imgui_radiance_dynamic_1f();
float* qdx_4imgui_radiance_coronas_1f();
float* qdx_4imgui_flashlight_radiance_1f(int idx);
float* qdx_4imgui_flashlight_colors_3f(int idx);
float* qdx_4imgui_flashlight_coneangles_1f(int idx);
float* qdx_4imgui_flashlight_conesoft_1f(int idx);
const float* qdx_4imgui_flashlight_position_3f();
const float* qdx_4imgui_flashlight_direction_3f();
float* qdx_4imgui_flashlight_position_off_3f();
float* qdx_4imgui_flashlight_direction_off_3f();

#endif