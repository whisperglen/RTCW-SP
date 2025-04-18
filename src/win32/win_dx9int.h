
#ifndef _WIN_DX9INT_H
#define _WIN_DX9INT_H

#include "qdx9.h"
#include "remix/bridge_remix_api.h"

extern remixapi_Interface remixInterface;
extern BOOL remixOnline;

extern void qdx_draw_init();
extern void qdx_before_frame_end();
extern void qdx_frame_ended();
extern void qdx_clear_buffers();
extern void qdx_texobj_delete_all();
extern void qdx_lights_draw();

extern void qdx_log_comment(const char *name, UINT vattbits, const void *ptr);
extern void qdx_log_matrix(const char *name, const float *mat);
extern void qdx_log_dump_buffer(const char* name, const uint32_t* buffer, uint32_t size, int wide, uint32_t hashid, uint32_t hashbuf);

#define MINI_CASE_SENSITIVE
#include "ini.h"

extern void qdx_lights_load(enum light_type type, mINI::INIMap<std::string> *opts);

#endif