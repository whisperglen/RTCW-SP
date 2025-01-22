
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
	D3DCOLOR clearColor;
	D3DCULL cull_mode;
	float depth_clear;
	float znear, zfar;
	D3DVIEWPORT9 viewport;
	D3DMATRIX camera;
	D3DMATRIX world;

	WINDOWPLACEMENT wplacement;
	D3DDISPLAYMODE desktop;
	D3DDISPLAYMODE *modes;
	D3DFORMAT fmt_backbuffer;
	D3DFORMAT fmt_depthstencil;
	UINT adapter_count;
	UINT adapter_num;
	D3DADAPTER_IDENTIFIER9 adapter_info;
	D3DCAPS9 caps;
	BOOL devicelost;
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
int  qdx_matrix_equals(const float* a, const float* b);

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

typedef enum vatt_param
{
	VATT_VERTEX   = 1u << 0,
	VATT_2DVERTEX = 1u << 1,
	VATT_NORMAL   = 1u << 2,
	VATT_COLOR    = 1u << 3,
	VATT_TEX0     = 1u << 4,
	VATT_TEX1     = 1u << 5,
	VATT_COLORVAL = 1u << 6
} vatt_param_t;

#define TEXID_NULL (-1)
#define TEXSAMPLER_USECFG (-1)

void qdx_vatt_set_buffer(vatt_param_t param, const void *buffer, UINT elems, UINT stride);
void qdx_vatt_enable(vatt_param_t param);
void qdx_vatt_disable(vatt_param_t param);
void qdx_vatt_lock_buffers(int num_elements);
void qdx_vatt_unlock_buffers();
#define qdx_vatt_buffer_null(P) qdx_vatt_set_buffer((P), NULL, 0, 0)
void qdx_vatt_texid(int texid, int samplernum);
void qdx_set_global_color(DWORD color);
void qdx_vatt_set2d(BOOL state);
void qdx_vatt_assemble_and_draw(UINT numindexes, const qdxIndex_t *indexes, const char *hint);

#define VERT2D_ZVAL (0.5f)
#define VERT2D_RHVVAL (1.0f)

typedef struct vatt_2dvertcoltex
{
	FLOAT XYZRHV[4];    // from the D3DFVF_XYZRHW flag
	DWORD COLOR;    // from the D3DFVF_DIFFUSE flag
	FLOAT UV[2];
} vatt_2dvertcoltex_t;
#define VATTID_2DVERTCOLTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0))

typedef struct vatt_vertcol
{
	FLOAT XYZ[3];
	DWORD COLOR;
} vatt_vertcol_t;
#define VATTID_VERTCOL (D3DFVF_XYZ | D3DFVF_DIFFUSE)

typedef struct vatt_vertnormcol
{
	FLOAT XYZ[3];
	FLOAT NORM[3];
	DWORD COLOR;
} vatt_vertnormcol_t;
#define VATTID_VERTNORMCOL (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE)

typedef struct vatt_vertcoltex
{
	FLOAT XYZ[3];
	DWORD COLOR;
	FLOAT UV[2];
} vatt_vertcoltex_t;
#define VATTID_VERTCOLTEX (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0))

typedef struct vatt_vertnormcoltex
{
	FLOAT XYZ[3];
	FLOAT NORM[3];
	DWORD COLOR;
	FLOAT UV[2];
} vatt_vertnormcoltex_t;
#define VATTID_VERTNORMCOLTEX (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0))

//typedef struct vatt_verttex
//{
//	FLOAT XYZ[3];
//	FLOAT UV[2];
//} vatt_verttex_t;
//#define VATTID_VERTTEX (D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0))

typedef struct vatt_vertcoltex2
{
	FLOAT XYZ[3];
	DWORD COLOR;
	FLOAT UV0[2];
	FLOAT UV1[2];
} vatt_vertcoltex2_t;
#define VATTID_VERTCOLTEX2 (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE2(1))

typedef struct vatt_vertnormcoltex2
{
	FLOAT XYZ[3];
	FLOAT NORM[3];
	DWORD COLOR;
	FLOAT UV0[2];
	FLOAT UV1[2];
} vatt_vertnormcoltex2_t;
#define VATTID_VERTNORMCOLTEX2 (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE2(1))

#ifdef __cplusplus
#define DX9_BEGIN_SCENE() qdx.device->BeginScene()
#define DX9_END_SCENE()   qdx.device->EndScene()
#else
#define DX9_BEGIN_SCENE() IDirect3DDevice9_BeginScene(qdx.device)
#define DX9_END_SCENE()   IDirect3DDevice9_EndScene(qdx.device)
#endif

typedef LPDIRECT3DVERTEXBUFFER9 qdx_vbuffer_t;

qdx_vbuffer_t qdx_vbuffer_upload(qdx_vbuffer_t buf, UINT fvfid, UINT size, void *data);
void qdx_vbuffer_release(qdx_vbuffer_t buf);


HRESULT qdx_compress_texture(int width, int height, const void *indata, void *outdata, int inbits, int outpitch);


void qdx_assert_str(int success, const char* expression, const char* function, unsigned line, const char* file);


#define qassert(expression) (void)(                                                             \
            (qdx_assert_str((!!(expression)), _CRT_STRINGIZE(#expression), (__func__), (unsigned)(__LINE__), (__FILE__)), 0) \
        )

#ifdef __cplusplus
} //extern "C"
#endif

#endif //__QDX9_H__
