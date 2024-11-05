
#ifndef __QDX9_H__
#define __QDX9_H__

#include <windows.h>
#include <windowsx.h>
#include <d3d9.h>
#include "d3dx9.h"

#ifdef __cplusplus
extern "C" {
#endif

struct qdx9
{
	LPDIRECT3D9 d3d;
	LPDIRECT3DDEVICE9 device;

	D3DCOLOR crt_color;
	D3DCULL cull_mode;

	WINDOWPLACEMENT wplacement;
	D3DDISPLAYMODE desktop;
	D3DDISPLAYMODE *modes;
	D3DFORMAT fmt_backbuffer;
	D3DFORMAT fmt_depthstencil;
	UINT adapter_count;
	UINT adapter_num;
	D3DADAPTER_IDENTIFIER9 adapter_info;
	D3DCAPS9 caps;
};

extern struct qdx9 qdx;

typedef LPDIRECT3DBASETEXTURE9 textureobj_t;

void textureobj_upload(int id, int usemips, int miplvl, int format, int width, int height, const byte *data);
textureobj_t textureobj_get(int id);
void textureobj_delete(int id);
void textureobj_map(int id, textureobj_t tex);
D3DFORMAT texture_format(UINT in, UINT *bits);

#ifdef __cplusplus
} //extern "C"
#endif

#endif //__QDX9_H__
