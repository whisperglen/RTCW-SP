
#ifndef __QDX9_H__
#define __QDX9_H__

#include <windows.h>
#include <windowsx.h>
#include <d3d9.h>
#include "d3dx9.h"

#ifdef __cplusplus
extern "C" {
#endif

struct qdx9_state
{
	LPDIRECT3D9 d3d;
	LPDIRECT3DDEVICE9 device;

	D3DCOLOR crt_color;
	D3DCULL cull_mode;
	float depth_clear;
	float znear;

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

extern struct qdx9_state qdx;

typedef LPDIRECT3DBASETEXTURE9 textureptr_t;
typedef struct qdx_textureobject
{
	textureptr_t obj;
	int sampler;
	int flt_min, flt_mip, flt_mag, anisotropy;
	int wrap_u, wrap_v;
	D3DCOLOR border;
} qdx_textureobj_t;

void qdx_texobj_upload(BOOL createNew, int id, BOOL usemips, int miplvl, int format, int width, int height, const void *data);
void qdx_texobj_apply(int id, int sampler);
qdx_textureobj_t qdx_texobj_get(int id);
qdx_textureobj_t* qdx_texobj_acc(int id);
void qdx_texobj_delete(int id);
void qdx_texobj_map(int id, textureptr_t tex);
D3DFORMAT qdx_texture_format(UINT in);
D3DTEXTUREADDRESS qdx_texture_wrapmode(int gl_mode);

void qdx_fvf_enable(int bits);
void qdx_fvf_disable(int bits);

#define VERT2D_ZVAL (0.5f)
#define VERT2D_RHVVAL (1.0f)

typedef struct fvf_2dvertcoltex
{
	FLOAT x, y, z, rhw;    // from the D3DFVF_XYZRHW flag
	DWORD color;    // from the D3DFVF_DIFFUSE flag
	FLOAT U, V;
} fvf_2dvertcoltex_t;
#define FVF_2DVERTCOLTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0))

typedef struct fvf_vertcol
{
	FLOAT X, Y, Z;
	DWORD COLOR;
} fvf_vertcol_t;
#define FVF_VERTCOL (D3DFVF_XYZ | D3DFVF_DIFFUSE)

typedef struct fvf_vertcoltex
{
	FLOAT X, Y, Z;
	DWORD COLOR;
	FLOAT U, V;
} fvf_vertcoltex_t;
#define FVF_VERTCOLTEX (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0))

#ifdef __cplusplus
#define DX9_BEGIN_SCENE() qdx.device->BeginScene()
#define DX9_END_SCENE()   qdx.device->EndScene()
#else
#define DX9_BEGIN_SCENE() IDirect3DDevice9_BeginScene(qdx.device)
#define DX9_END_SCENE()   IDirect3DDevice9_EndScene(qdx.device)
#endif

typedef LPDIRECT3DVERTEXBUFFER9 qdx_vbuffer_t;

qdx_vbuffer_t qdx_vbuffer_upload(UINT fvfid, UINT size, void *data);
void qdx_vbuffer_release(qdx_vbuffer_t buf);

#ifdef __cplusplus
} //extern "C"
#endif

#endif //__QDX9_H__
