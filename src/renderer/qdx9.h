
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

struct animation_buff_s
{
	LPDIRECT3DVERTEXBUFFER9 vbuffer;
	LPDIRECT3DINDEXBUFFER9 ibuffer;
	UINT32 vertex_count;
	UINT32 index_count;
	UINT8 bone_count;
};

struct qdx9_state
{
	LPDIRECT3D9 d3d;
	LPDIRECT3DDEVICE9 device;

	D3DCOLOR crt_color;
	D3DCOLOR clearColor;
	D3DCULL cull_mode;
	float depth_clear;
	D3DVIEWPORT9 viewport;
	D3DMATRIX camera;
	D3DMATRIX world;

	struct animation_buff_s skinned_mesh;

	WINDOWPLACEMENT wplacement;
	D3DDISPLAYMODE desktop;
	D3DDISPLAYMODE *modes;
	INT modes_count;
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
void qdx_texture_apply();

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
void qdx_vatt_enable_buffer(vatt_param_t param);
void qdx_vatt_disable_buffer(vatt_param_t param);
void qdx_vatt_lock_buffers(int num_elements);
void qdx_vatt_unlock_buffers();
#define qdx_vatt_set_buffer_null(P) qdx_vatt_set_buffer((P), NULL, 0, 0)
void qdx_vatt_attach_texture(int texid, int samplernum);
void qdx_set_global_color(DWORD color);
void qdx_vatt_set2d(BOOL state);
void qdx_vatt_assemble_and_draw(UINT numindexes, const qdxIndex_t *indexes, const char *hint);

void qdx_objects_reset();
void qdx_begin_loading_map(const char* mapname);

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

typedef struct vatt_anim
{
	FLOAT XYZ[3];
	FLOAT WEIGHTS[3];
	union
	{
		DWORD MATIND;
		BYTE MATINDB[4];
	};
	FLOAT NORM[3];
	//DWORD COLOR;
	FLOAT UV[2];
} vatt_anim_t;
#define VATTID_ANIM (D3DFVF_XYZB4 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_NORMAL /*| D3DFVF_DIFFUSE*/ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0))

#ifdef __cplusplus
#define DX9_BEGIN_SCENE() D3D_OK
#define DX9_END_SCENE() D3D_OK
#else
#define DX9_BEGIN_SCENE() D3D_OK
#define DX9_END_SCENE() D3D_OK
#endif

#ifdef __cplusplus
#define DX9_BEGIN_SCENE_GG() qdx.device->BeginScene()
#define DX9_END_SCENE_GG()   qdx.device->EndScene()
#else
#define DX9_BEGIN_SCENE_GG() IDirect3DDevice9_BeginScene(qdx.device)
#define DX9_END_SCENE_GG()   IDirect3DDevice9_EndScene(qdx.device)
#endif

typedef LPDIRECT3DVERTEXBUFFER9 qdx_vbuffer_t;
typedef LPDIRECT3DINDEXBUFFER9 qdx_ibuffer_t;

typedef enum buffersteps_e
{
	STEP_ALLOCATE = 1 << 0,
	STEP_GET_DATAPTR_CLEAR = 1 << 1,
	STEP_GET_DATAPTR_APPEND = 1 << 2,
	STEP_FINALIZE = 1 << 3
} buffersteps_t;

qdx_vbuffer_t qdx_vbuffer_create_and_upload(qdx_vbuffer_t buf, UINT fvfid, UINT size, void *data);
BOOL qdx_vbuffer_steps(buffersteps_t step, qdx_vbuffer_t* buf, UINT vattid, UINT offset, UINT size, void** outmem);
void qdx_vbuffer_release(qdx_vbuffer_t buf);
BOOL qdx_ibuffer_steps(buffersteps_t step, qdx_ibuffer_t* buf, UINT format, UINT offset, UINT size, void** outmem);
void qdx_ibuffer_release(qdx_ibuffer_t buf);
void qdx_animation_process();
const void* qdx_anim_add_bone_mapping( const void* surface, const void* bones, int numbones );
const void* qdx_anim_get_bone_transforms( const void* surface );
#define QDX_ANIMATION_PROCESS() if(qdx.skinned_mesh.index_count) { qdx_animation_process(); }

enum light_type_e
{
	LIGHT_NONE = 0,
	LIGHT_DYNAMIC = 1,
	LIGHT_CORONA = 2,
	LIGHT_FLASHLIGHT = 4,

	LIGHT_ALL = (LIGHT_DYNAMIC|LIGHT_CORONA|LIGHT_FLASHLIGHT)
};

void qdx_light_add(int light_type_e, int ord, const float *position, const float *direction, const float *color, float radius);
void qdx_lights_clear(unsigned int light_types);

typedef union surfpartition_u
{
	UINT32 combined;
	struct surfaceid_s
	{
		INT32 x : 10;
		INT32 y : 10;
		INT32 z : 10;
	} p;
} surfpartition_t;

surfpartition_t qdx_surface_get_partition( const void* data );
void qdx_surface_add( const void* surf, surfpartition_t id );
void qdx_surface_get_members( surfpartition_t id, const void** surfs, int* count );

void qdx_screen_getxyz( float *xyz );

HRESULT qdx_compress_texture(int width, int height, const void *indata, void *outdata, int inbits, int outpitch);


void qdx_assert_failed_str(const char* expression, const char* function, unsigned line, const char* file);


#define qassert(expression) do {                                                             \
            if((!(expression))) { qdx_assert_failed_str(_CRT_STRINGIZE(#expression), (__func__), (unsigned)(__LINE__), (__FILE__)); } \
        } while(0)

#ifdef __cplusplus
} //extern "C"
#endif

#endif //__QDX9_H__
