
#ifndef __QDX9_H__
#define __QDX9_H__

#include <windows.h>
#include <windowsx.h>
#include <d3d9.h>
#include "d3dx9.h"

#ifdef __cplusplus
extern "C" {
#endif

#if 1
#define QDX_INDEX_TYPE D3DFMT_INDEX32
typedef UINT32 qdxIndex_t;
#else
#define QDX_INDEX_TYPE D3DFMT_INDEX16
typedef UINT16 qdxIndex_t;
#endif
struct qdx9_state
{
	LPDIRECT3D9 d3d;
	LPDIRECT3DDEVICE9 device;

	D3DCOLOR crt_color;
	D3DCULL cull_mode;
	float depth_clear;
	float znear, zfar;
	D3DVIEWPORT9 viewport;

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

inline void qdx_assign_viewport(D3DVIEWPORT9 *vp, DWORD X, DWORD Y, DWORD Width, DWORD Height, float MinZ, float MaxZ)
{
	vp->X = X;
	vp->Y = Y;
	vp->Width = Width;
	vp->Height = Height;
	vp->MinZ = MinZ;
	vp->MaxZ = MaxZ;
}

void qdx_depthrange(float znear, float zfar);

void qdx_matrix_set(D3DTRANSFORMSTATETYPE type, const float *matrix);
void qdx_matrix_mul(D3DTRANSFORMSTATETYPE type, const D3DMATRIX *matrix);
void qdx_matrix_push(D3DTRANSFORMSTATETYPE type);
void qdx_matrix_pop(D3DTRANSFORMSTATETYPE type);
void qdx_matrix_apply(void);

typedef LPDIRECT3DBASETEXTURE9 textureptr_t;
typedef struct qdx_textureobject
{
	textureptr_t obj;
	int sampler;
	int flt_min, flt_mip, flt_mag, anisotropy;
	int wrap_u, wrap_v;
	D3DCOLOR border;
} qdx_textureobj_t;

typedef enum qdx_texparam
{
	TEXP_FLT_MIN  = (1 << 0),
	TEXP_FLT_MIP  = (1 << 1),
	TEXP_FLT_MAG  = (1 << 2),
	TEXP_ANIS_LVL = (1 << 3),
	TEXP_WRAP_U   = (1 << 4),
	TEXP_WRAP_V   = (1 << 5),
	TEXP_BORDERC  = (1 << 6),
} qdx_texparam_t;

void qdx_texobj_upload(BOOL createNew, int id, BOOL usemips, int miplvl, int format, int width, int height, const void *data);
void qdx_texobj_apply(int id, int sampler);
void qdx_texobj_setparam(int id, qdx_texparam_t par, int val);
qdx_textureobj_t qdx_texobj_get(int id);
qdx_textureobj_t* qdx_texobj_acc(int id);
void qdx_texobj_delete(int id);
void qdx_texobj_map(int id, textureptr_t tex);
D3DFORMAT qdx_texture_format(UINT in);
D3DTEXTUREADDRESS qdx_texture_wrapmode(int gl_mode);

typedef enum fvf_param
{
	FVF_VERTEX   = 1u << 0,
	FVF_2DVERTEX = 1u << 1,
	FVF_NORMAL   = 1u << 2,
	FVF_COLOR    = 1u << 3,
	FVF_TEX0     = 1u << 4,
	FVF_TEX1     = 1u << 5,
	FVF_COLORVAL = 1u << 6
} fvf_param_t;

#define TEXID_NULL (-1)
#define TEXSAMPLER_USECFG (-1)

void qdx_fvf_buffer(fvf_param_t param, const void *buffer, UINT elems, UINT stride);
void qdx_fvf_enable(fvf_param_t param);
void qdx_fvf_disable(fvf_param_t param);
#define qdx_fvf_buffer_null(P) qdx_fvf_buffer((P), NULL, 0, 0)
void qdx_fvf_texid(int texid, int samplernum);
void qdx_set_color(DWORD color);
void qdx_fvf_set2d(BOOL state);
void qdx_fvf_assemble_and_draw(UINT numindexes, const qdxIndex_t *indexes);

#define VERT2D_ZVAL (0.5f)
#define VERT2D_RHVVAL (1.0f)

typedef struct fvf_2dvertcoltex
{
	FLOAT XYZRHV[4];    // from the D3DFVF_XYZRHW flag
	DWORD COLOR;    // from the D3DFVF_DIFFUSE flag
	FLOAT UV[2];
} fvf_2dvertcoltex_t;
#define FVFID_2DVERTCOLTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0))

typedef struct fvf_vertcol
{
	FLOAT XYZ[3];
	DWORD COLOR;
} fvf_vertcol_t;
#define FVFID_VERTCOL (D3DFVF_XYZ | D3DFVF_DIFFUSE)

typedef struct fvf_vertcoltex
{
	FLOAT XYZ[3];
	DWORD COLOR;
	FLOAT UV[2];
} fvf_vertcoltex_t;
#define FVFID_VERTCOLTEX (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0))

//typedef struct fvf_verttex
//{
//	FLOAT XYZ[3];
//	FLOAT UV[2];
//} fvf_verttex_t;
//#define FVFID_VERTTEX (D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0))

typedef struct fvf_vertcoltex2
{
	FLOAT XYZ[3];
	DWORD COLOR;
	FLOAT UV0[2];
	FLOAT UV1[2];
} fvf_vertcoltex2_t;
#define FVFID_VERTCOLTEX2 (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE2(1))

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


HRESULT qdx_compress_texture(int width, int height, const void *indata, void *outdata, int inbits, int outpitch);

#ifdef __cplusplus
} //extern "C"
#endif

#endif //__QDX9_H__
