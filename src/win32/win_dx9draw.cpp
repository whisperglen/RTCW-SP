
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

enum r_logfiletypes_e
{
	RLOGFILE_TEXT = 1,
	RLOGFILE_VERTEX = 1 << 1,
	RLOGFILE_TEXTURE = 1 << 2,
	RLOGFILE_MATRIX = 1 << 3,
	RLOGFILE_VATTSET = 1 << 4,
};

static void iniconf_first_init();
static void qdx_animationbuf_reset(BOOL release);

struct qdx_matrixes
{
	D3DXMATRIX view;
	D3DXMATRIX proj;
	D3DXMATRIX world;
	void init()
	{
		D3DXMatrixIdentity(&view);
		//D3DXMatrixLookAtRH(&view,
		//	&D3DXVECTOR3(0.0f, 0.0f, 0.0f),   // the camera position
		//	&D3DXVECTOR3(0.0f, 0.0f, -1.0f),    // the look-at position, in RH forward is -z and +z in LH 
		//	&D3DXVECTOR3(0.0f, 1.0f, 0.0f));    // the up direction
		D3DXMatrixIdentity(&proj);
		D3DXMatrixIdentity(&world);
	}
} qdx_mats;

void qdx_draw_init(void *hwnd, void *device)
{
	qdx_mats.init();
	qdx_animationbuf_reset(TRUE);
	iniconf_first_init();
	qdx_imgui_init(hwnd, device);
}

D3DTEXTUREADDRESS qdx_texture_wrapmode(int gl_mode)
{
	D3DTEXTUREADDRESS ret = D3DTADDRESS_WRAP;

	switch (gl_mode)
	{
	case GL_CLAMP:
		ret = D3DTADDRESS_CLAMP;
		break;
	case GL_REPEAT:
		ret = D3DTADDRESS_WRAP;
		break;
	}

	return ret;
}

UINT qdx_texture_fmtbits(D3DFORMAT fmt)
{
	UINT b = 0;

	switch (fmt)
	{
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
		b = 32;
		break;
	case D3DFMT_A4R4G4B4:
	case D3DFMT_R5G6B5:
		b = 16;
		break;
	case D3DFMT_DXT5:
		b = 8;
		break;
	}

	return b;
}

D3DFORMAT qdx_texture_format(UINT in)
{
	D3DFORMAT ret = D3DFMT_UNKNOWN;

	switch (in)
	{
	case GL_RGBA8:
		ret = D3DFMT_A8R8G8B8;
		break;
	case GL_RGBA4:
		ret = D3DFMT_A4R4G4B4;
		break;
	case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
		ret = D3DFMT_DXT5;
		//ret = D3DFMT_A8R8G8B8;
		break;
	case GL_RGB8:
		ret = D3DFMT_X8R8G8B8;
		break;
	case GL_RGB5:
		ret = D3DFMT_R5G6B5;
		break;
	case 3:
		ret = D3DFMT_X8R8G8B8;
		break;
	case 4:
		ret = D3DFMT_A8R8G8B8;
		break;
	}

	return ret;
}

void qdx_texobj_upload(BOOL createNew, int id, BOOL usemips, int miplvl, int format, int width, int height, const void *data)
{
	HRESULT res;
	IDirect3DTexture9 *tex = 0;
	int levels = 1;
	D3DFORMAT dxfmt = qdx_texture_format(format);
	UINT elembits = qdx_texture_fmtbits(dxfmt);

	if (usemips)
	{
		int mw = width, mh = height;
		while (mw > 1 || mh > 1)
		{
			mw >>= 1; mh >>= 1;
			if (mw < 1) mw = 1;
			if (mh < 1) mh = 1;
			levels++;
		}
	}
	tex = (IDirect3DTexture9*)qdx_texobj_get(id).obj;
	if (createNew || (miplvl == 0 && tex == 0))
	{
		res = qdx.device->CreateTexture(width, height, levels, 0, dxfmt, D3DPOOL_MANAGED, &tex, NULL);
		if (FAILED(res))
		{
			ri.Printf(PRINT_ERROR, "Texture creation fail: fmt%d w%d h%d l%d c%d\n", dxfmt, width, height, levels, HRESULT_CODE(res));
			return;
		}
		qdx_texobj_map(id, tex);
	}
	else
	{
		if (tex == 0)
		{
			ri.Printf(PRINT_ERROR, "Texture not found: fmt%d w%d h%d l%d\n", dxfmt, width, height, levels);
			return;
		}
	}

	if (dxfmt == D3DFMT_UNKNOWN)
	{
		D3DSURFACE_DESC desc;
		tex->GetLevelDesc(0, &desc);
		dxfmt = desc.Format;
		elembits = qdx_texture_fmtbits(dxfmt);
	}

	D3DLOCKED_RECT lr;
	res = tex->LockRect(miplvl, &lr, NULL, 0);
	if (SUCCEEDED(res))
	{
		int elems = width * height;
		if (dxfmt == D3DFMT_A8R8G8B8)
		{
			uint32_t *out = (uint32_t*)lr.pBits;
			const byte *in = (const byte*)data;
			for (int i = 0; i < elems; i++, out++, in += 4)
			{
				*out = D3DCOLOR_ARGB(in[3], in[0], in[1], in[2]);
			}
		}
		else if (dxfmt == D3DFMT_X8R8G8B8)
		{
			uint32_t *out = (uint32_t*)lr.pBits;
			const byte *in = (const byte*)data;
			for (int i = 0; i < elems; i++, out++, in += 4)
			{
				*out = D3DCOLOR_XRGB(in[0], in[1], in[2]);
			}
		}
		else if (dxfmt == D3DFMT_A4R4G4B4)
		{
			uint16_t *out = (uint16_t*)lr.pBits;
			const byte *in = (const byte*)data;
			for (int i = 0; i < elems; i++, out++, in += 4)
			{
				UINT r = (in[0] * 15 / 255) << 8;
				UINT g = (in[1] * 15 / 255) << 4;
				UINT b = (in[2] * 15 / 255) << 0;
				UINT a = (in[3] * 15 / 255) << 12;
				*out = (a | r | g | b);
			}
		}
		else if (dxfmt == D3DFMT_R5G6B5)
		{
			uint16_t *out = (uint16_t*)lr.pBits;
			const byte *in = (const byte*)data;
			for (int i = 0; i < elems; i++, out++, in += 4)
			{
				UINT r = (in[0] * 31 / 255) << 11;
				UINT g = (in[1] * 63 / 255) << 5;
				UINT b = (in[2] * 31 / 255) << 0;
				*out = (r | g | b);
			}
		}
		else if (dxfmt == D3DFMT_DXT5)
		{
			HRESULT hr = qdx_compress_texture(width, height, data, lr.pBits, elembits, lr.Pitch);
			if (FAILED(hr))
			{
				ri.Printf(PRINT_ERROR, "Texture compress fail: fmt%d w%d h%d l%d/%d m%d c%d\n", dxfmt, width, height,
					levels, tex->GetLevelCount(), miplvl, HRESULT_CODE(hr));
			}
		}
		else
		{
			qassert(FALSE);
			//well if we do not know the fmmt, elembits is zero, so nothing is set
			memset(lr.pBits, 0, (elems * elembits) / 8);
		}
		tex->UnlockRect(0);
	}
	else
	{
		ri.Printf(PRINT_ERROR, "Texture upload fail: fmt%d w%d h%d l%d/%d m%d c%d\n", dxfmt, width, height,
			levels, tex->GetLevelCount(), miplvl, HRESULT_CODE(res));
	}
}

/*
textureobject::textureobject(): obj(0), sampler(0), flt_min(D3DTEXF_POINT), flt_mip(D3DTEXF_NONE), flt_mag(D3DTEXF_POINT), anisotropy(1),
wrap_u(D3DTADDRESS_WRAP), wrap_v(D3DTADDRESS_WRAP), border(D3DCOLOR_ARGB(0, 0, 0, 0))
{}*/

static qdx_textureobj_t textures[MAX_DRAWIMAGES] = { 0 };
static qdx_textureobj_t g_cleartex = { 0, 0, D3DTEXF_POINT, D3DTEXF_NONE, D3DTEXF_POINT, 1, D3DTADDRESS_WRAP, D3DTADDRESS_WRAP, D3DCOLOR_ARGB(0, 0, 0, 0) };

void qdx_texobj_map(int id, textureptr_t tex)
{
	if (id >= 0 && id < ARRAYSIZE(textures))
	{
		if (textures[id].obj != 0)
		{
			textures[id].obj->Release();
		}
		textures[id] = g_cleartex;
		textures[id].obj = tex;
	}
}

qdx_textureobj_t qdx_texobj_get(int id)
{
	qdx_textureobj_t ret;

	if (id >= 0 && id < ARRAYSIZE(textures))
	{
		ret = textures[id];
	}
	else if (id == TEXID_NULL)
	{
		ret = g_cleartex;
	}
	else
	{
		qassert(FALSE);
		ret = g_cleartex;
	}

	return ret;
}

qdx_textureobj_t* qdx_texobj_acc(int id)
{
	qdx_textureobj_t *ret = 0;

	if (id >= 0 && id < ARRAYSIZE(textures))
	{
		ret = &textures[id];
	}
	else
	{
		qassert(FALSE);
	}

	return ret;
}

void qdx_texobj_setparam(int id, qdx_texparam_t par, int val)
{
	qdx_textureobj_t *obj = qdx_texobj_acc(id);
	if (obj)
	{
		if (par & TEXP_FLT_MIN)
		{
			obj->flt_min = val;
		}
		if (par & TEXP_FLT_MIP)
		{
			obj->flt_mip = (val > D3DTEXF_LINEAR) ? D3DTEXF_LINEAR : val;
		}
		if (par & TEXP_FLT_MAG)
		{
			obj->flt_mag = val;
		}
		if (par & TEXP_ANIS_LVL)
		{
			obj->anisotropy = val;
		}
		if (par  & TEXP_WRAP_U)
		{
			obj->wrap_u = val;
		}
		if (par  & TEXP_WRAP_V)
		{
			obj->wrap_v = val;
		}
		if (par & TEXP_BORDERC)
		{
			obj->border = val;
		}
	}
	else
	{
		qassert(FALSE);
	}
}

void qdx_texobj_delete(int id)
{
	textureptr_t ret = 0;

	if (id >= 0 && id < ARRAYSIZE(textures))
	{
		ret = textures[id].obj;
		if (ret)
		{
			textures[id] = g_cleartex;
			ret->Release();
		}
	}
	else
	{
		qassert(FALSE);
	}
}

void qdx_texobj_delete_all()
{
	for (int idx = 0; idx < ARRAYSIZE(textures); idx++)
	{
		if (textures[idx].obj != 0)
		{
			textures[idx].obj->Release();
		}
	}

	memset(textures, 0, sizeof(textures));
}

void qdx_texobj_apply(int id, int sampler)
{
	qdx_textureobj_t opt = qdx_texobj_get(id);
	int sampler_id;
	if (sampler == TEXSAMPLER_USECFG)
		sampler_id = opt.sampler;
	else
		sampler_id = sampler;

	if(r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_TEXTURE))
		qdx_log_comment(__FUNCTION__, (sampler == 0 ? VATT_TEX0 : VATT_TEX1), opt.obj);

	if (qdx.device)
	{
		qdx.device->SetTexture(sampler_id, opt.obj);
		qdx.device->SetSamplerState(sampler_id, D3DSAMP_MINFILTER, opt.flt_min);
		qdx.device->SetSamplerState(sampler_id, D3DSAMP_MIPFILTER, opt.flt_mip);
		qdx.device->SetSamplerState(sampler_id, D3DSAMP_MAGFILTER, opt.flt_mag);
		qdx.device->SetSamplerState(sampler_id, D3DSAMP_MAXANISOTROPY, opt.anisotropy);
		qdx.device->SetSamplerState(sampler_id, D3DSAMP_ADDRESSU, opt.wrap_u);
		qdx.device->SetSamplerState(sampler_id, D3DSAMP_ADDRESSV, opt.wrap_v);
		qdx.device->SetSamplerState(sampler_id, D3DSAMP_BORDERCOLOR, opt.border);
	}
}

#define VATT_PARAM_ARRAYSZ 2
typedef struct vatt_buff_stats
{
	uint32_t num_entries;
	uint32_t checksum[VATT_PARAM_ARRAYSZ];
} vatt_buff_stats_t;

struct vatt_buffers
{
	DWORD owner;
	UINT usage;
	struct vatt_vert_buffer
	{
		LPDIRECT3DINDEXBUFFER9 i_buf;
		LPDIRECT3DVERTEXBUFFER9 v_buf;
		UINT ident;
		UINT hash;
		uint32_t usage;
		vatt_buff_stats_t stats;
	} v_buffs [5000];
} g_vatt_buffers[1];

static int g_used_vatt_buffers = 0;

void qdx_clear_buffers()
{
	int i = 0, j = 0;
	for (; i < g_used_vatt_buffers; i++)
	{
		for (j = 0; j < ARRAYSIZE(g_vatt_buffers[i].v_buffs); j++)
		{
			if (g_vatt_buffers[i].v_buffs[j].i_buf)
			{
				g_vatt_buffers[i].v_buffs[j].i_buf->Release();
			}

			if (g_vatt_buffers[i].v_buffs[j].v_buf)
			{
				g_vatt_buffers[i].v_buffs[j].v_buf->Release();
			}
		}
	}
	g_used_vatt_buffers = 0;
}

void qdx_objects_reset()
{
	qdx_animationbuf_reset(TRUE);
	qdx_clear_buffers();
	qdx_texobj_delete_all();
	qdx_lights_clear(LIGHT_ALL);
}

void qdx_frame_ended()
{
	qdx.skinned_mesh.bone_count = 1;
	keypress_frame_ended();
}

void qdx_before_frame_end()
{
	qdx_lights_draw();
	qdx_imgui_draw();
}

static int qdx_get_buffers(LPDIRECT3DINDEXBUFFER9 *index_buf, LPDIRECT3DVERTEXBUFFER9 *vertex_buf, UINT vatt_spec, UINT vatt_stride, UINT hash, vatt_buff_stats_t **stats)
{
	DWORD whoami = GetCurrentThreadId();
	int i = 0, j = 0;
	for (; i < g_used_vatt_buffers; i++)
	{
		if (g_vatt_buffers[i].owner == whoami)
		{
			for (j = 0; j < ARRAYSIZE(g_vatt_buffers[i].v_buffs); j++)
			{
				if (g_vatt_buffers[i].v_buffs[j].ident == vatt_spec && g_vatt_buffers[i].v_buffs[j].hash == hash)
				{
					if (index_buf)
						*index_buf = g_vatt_buffers[i].v_buffs[j].i_buf;
					if (vertex_buf)
						*vertex_buf = g_vatt_buffers[i].v_buffs[j].v_buf;
					if (stats)
						*stats = &g_vatt_buffers[i].v_buffs[j].stats;

					return 0;
				}
				else if (g_vatt_buffers[i].v_buffs[j].ident == 0)
				{
					LPDIRECT3DINDEXBUFFER9 ib;
					if (FAILED(qdx.device->CreateIndexBuffer(sizeof(qdxIndex_t) * SHADER_MAX_INDEXES, 0, QDX_INDEX_TYPE, D3DPOOL_MANAGED, &ib, NULL)))
					{
						return -1;
					}
					LPDIRECT3DVERTEXBUFFER9 vb;
					if (FAILED(qdx.device->CreateVertexBuffer(SHADER_MAX_VERTEXES * vatt_stride, 0, vatt_spec, D3DPOOL_MANAGED, &vb, NULL)))
					{
						ib->Release();
						return -1;
					}
					g_vatt_buffers[i].usage++;
					g_vatt_buffers[i].v_buffs[j].ident = vatt_spec;
					g_vatt_buffers[i].v_buffs[j].hash = hash;
					g_vatt_buffers[i].v_buffs[j].i_buf = ib;
					g_vatt_buffers[i].v_buffs[j].v_buf = vb;
					memset(&g_vatt_buffers[i].v_buffs[j].stats, 0, sizeof(g_vatt_buffers[i].v_buffs[j].stats));

					if (index_buf)
						*index_buf = ib;
					if (vertex_buf)
						*vertex_buf = vb;
					if (stats)
						*stats = &g_vatt_buffers[i].v_buffs[j].stats;

					return 0;
				}
			}

			return -2;
		}
	}

	if (g_used_vatt_buffers < ARRAYSIZE(g_vatt_buffers))
	{
		LPDIRECT3DINDEXBUFFER9 ib;
		if (FAILED(qdx.device->CreateIndexBuffer(sizeof(qdxIndex_t) * SHADER_MAX_INDEXES, 0, QDX_INDEX_TYPE, D3DPOOL_MANAGED, &ib, NULL)))
		{
			return -1;
		}
		LPDIRECT3DVERTEXBUFFER9 vb;
		if (FAILED(qdx.device->CreateVertexBuffer(SHADER_MAX_VERTEXES * vatt_stride, 0, vatt_spec, D3DPOOL_MANAGED, &vb, NULL)))
		{
			ib->Release();
			return -1;
		}

		struct vatt_buffers *p = &g_vatt_buffers[g_used_vatt_buffers];
		g_used_vatt_buffers++;
		p->owner = whoami;
		p->usage = 1;
		memset(p->v_buffs, 0, sizeof(p->v_buffs));
		p->v_buffs[0].ident = vatt_spec;
		p->v_buffs[0].hash = hash;
		p->v_buffs[0].i_buf = ib;
		p->v_buffs[0].v_buf = vb;

		if (index_buf)
			*index_buf = ib;
		if (vertex_buf)
			*vertex_buf = vb;
		if (stats)
			*stats = &p->v_buffs[0].stats;

		return 0;
	}

	return -2;
}

struct vatt_state
{
	BOOL is2dprojection;
	UINT active_vatts;
	UINT locked_vatts;
	UINT num_vatts;
	const float *vertexes;
	//UINT numverts;
	UINT strideverts;
	const float *normals;
	//UINT numnorms;
	UINT stridenorms;
	const byte *colors;
	//UINT numcols;
	UINT stridecols;
	const float *texcoord[2];
	//UINT numtexcoords[2];
	UINT stridetexcoords[2];
	INT textureids[2];
} g_vattribs = { 0 };

ID_INLINE void copy_two(float *dst, const float *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
}

ID_INLINE void copy_xyzrhv(float *dst, const float *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = VERT2D_ZVAL;
	dst[3] = VERT2D_RHVVAL;
}

ID_INLINE void copy_three(float *dst, const float *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
}

ID_INLINE DWORD abgr_to_argb(const byte *in)
{
	DWORD abgr = *((uint32_t*)in);
	DWORD axgx = abgr & 0xff00ff00;
	DWORD r = abgr & 0xff;
	DWORD b = (abgr & 0xff0000) >> 16;
	return (axgx | (r << 16) | b);
}

#define GVATT_VERTELEM(IDX) (g_vattribs.vertexes + (IDX) * g_vattribs.strideverts)
#define GVATT_INCVERTP(PTR) ((PTR) += g_vattribs.strideverts)
#define GVATT_NORMELEM(IDX) (g_vattribs.normals + (IDX) * g_vattribs.stridenorms)
#define GVATT_INCNORMP(PTR) ((PTR) += g_vattribs.stridenorms)
#define GVATT_COLRELEM(IDX) (g_vattribs.colors + (IDX) * g_vattribs.stridecols)
#define GVATT_INCCOLRP(PTR) ((PTR) += g_vattribs.stridecols)
#define GVATT_TEX0ELEM(IDX) (g_vattribs.texcoord[0] + (IDX) * g_vattribs.stridetexcoords[0])
#define GVATT_INCTEX0P(PTR) ((PTR) += g_vattribs.stridetexcoords[0])
#define GVATT_TEX1ELEM(IDX) (g_vattribs.texcoord[1] + (IDX) * g_vattribs.stridetexcoords[1])
#define GVATT_INCTEX1P(PTR) ((PTR) += g_vattribs.stridetexcoords[1])

void qdx_log_comment(const char *name, UINT vattbits, const void *ptr)
{
	int vattci = 0;
	char vattc[10];
	memset(vattc, 0, sizeof(vattc));
	if (vattbits & VATT_VERTEX) vattc[vattci++] = 'v';
	if (vattbits & VATT_2DVERTEX) vattc[vattci++] = 'V';
	if (vattbits & VATT_NORMAL) vattc[vattci++] = 'n';
	if (vattbits & VATT_COLOR) vattc[vattci++] = 'c';
	if (vattbits & VATT_TEX0) vattc[vattci++] = 't';
	if (vattbits & VATT_TEX1) vattc[vattci++] = 'T';
	if (vattbits & VATT_COLORVAL) vattc[vattci++] = 'C';
	char comment[256];
	snprintf(comment, sizeof(comment), "%s: %s %p\n", name, vattc, ptr);
	DX9imp_LogComment(comment);
}

void qdx_log_matrix(const char *name, const float *mat)
{
	char comment[256];
	snprintf(comment, sizeof(comment), "qdx_matrix[%s]:\n  %f %f %f %f\n  %f %f %f %f\n  %f %f %f %f\n  %f %f %f %f\n", name,
		mat[0], mat[1], mat[2], mat[3],
		mat[4], mat[5], mat[6], mat[7],
		mat[8], mat[9], mat[10], mat[11],
		mat[12], mat[13], mat[14], mat[15]);
	DX9imp_LogComment(comment);
}

void qdx_log_dump_buffer(const char *name, const uint32_t *buffer, uint32_t size, int wide, uint32_t hashid, uint32_t hashbuf)
{
	char comment[256];
	snprintf(comment, sizeof(comment), "[%s] %p %d hashes: %08x %08x\n", name, buffer, size, hashid, hashbuf);
	DX9imp_LogComment(comment);
	const uint32_t* pos = buffer;
	int i = 0;
	for (; i + wide < size; i += wide, pos += wide)
	{
		//this is crazy, need a better idea
		switch (wide)
		{
		case 4:
			snprintf(comment, sizeof(comment), "%08x %08x %08x %08x\n",
				pos[0], pos[1], pos[2], pos[3]);
			break;
		case 5:
			snprintf(comment, sizeof(comment), "%08x %08x %08x %08x %08x\n",
				pos[0], pos[1], pos[2], pos[3], pos[4]);
			break;
		case 6:
			snprintf(comment, sizeof(comment), "%08x %08x %08x %08x %08x %08x\n",
				pos[0], pos[1], pos[2], pos[3], pos[4], pos[5]);
			break;
		case 7:
			snprintf(comment, sizeof(comment), "%08x %08x %08x %08x %08x %08x %08x\n",
				pos[0], pos[1], pos[2], pos[3], pos[4], pos[5], pos[6]);
			break;
		case 8:
			snprintf(comment, sizeof(comment), "%08x %08x %08x %08x %08x %08x %08x %08x\n",
				pos[0], pos[1], pos[2], pos[3], pos[4], pos[5], pos[6], pos[7]);
			break;
		case 9:
			snprintf(comment, sizeof(comment), "%08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
				pos[0], pos[1], pos[2], pos[3], pos[4], pos[5], pos[6], pos[7], pos[8]);
			break;
		case 10:
			snprintf(comment, sizeof(comment), "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
				pos[0], pos[1], pos[2], pos[3], pos[4], pos[5], pos[6], pos[7], pos[8], pos[9]);
			break;
		case 11:
			snprintf(comment, sizeof(comment), "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
				pos[0], pos[1], pos[2], pos[3], pos[4], pos[5], pos[6], pos[7], pos[8], pos[9], pos[10]);
			break;
		case 16:
			snprintf(comment, sizeof(comment), "%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n",
				pos[0], pos[1], pos[2], pos[3], pos[4], pos[5], pos[6], pos[7], pos[8], pos[9], pos[10], pos[11], pos[12], pos[13], pos[14], pos[15]);
			break;
		default:
			strcpy(comment, "please implement :D\n");
			break;
		}

		DX9imp_LogComment(comment);
	}
	for (; i < size; i++, pos++)
	{
		snprintf(comment, sizeof(comment), (wide == 16 ? "%x " : "%08x "), pos[0]);

		DX9imp_LogComment(comment);
	}
	if(size)
		DX9imp_LogComment("\n");
}

void qdx_vatt_set2d(BOOL state)
{
	g_vattribs.is2dprojection = state;
}

void qdx_vatt_attach_texture(int texid, int samplernum)
{
	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VATTSET))
		qdx_log_comment(__FUNCTION__, (samplernum == 0 ? VATT_TEX0 : VATT_TEX1), (const void*)texid);

	if (0 <= samplernum && samplernum <= 1)
	{
		g_vattribs.textureids[samplernum] = texid;
	}
	else
	{
		qassert(FALSE);
	}
}

void qdx_set_global_color(DWORD color)
{
	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VATTSET))
		qdx_log_comment(__FUNCTION__, 0, (const void*)color);

	qdx.crt_color = color;
}

void qdx_vatt_enable_buffer(vatt_param_t param)
{
	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VATTSET))
		qdx_log_comment(__FUNCTION__, param, NULL);

	g_vattribs.active_vatts |= param;
}

void qdx_vatt_disable_buffer(vatt_param_t param)
{
	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VATTSET))
		qdx_log_comment(__FUNCTION__, param, NULL);

	g_vattribs.active_vatts &= ~param;
}

void qdx_vatt_lock_buffers(int num_elements)
{
	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VATTSET))
		qdx_log_comment(__FUNCTION__, g_vattribs.active_vatts, (void*)num_elements);

	g_vattribs.locked_vatts = g_vattribs.active_vatts;
}

void qdx_vatt_unlock_buffers()
{
	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VATTSET))
		qdx_log_comment(__FUNCTION__, g_vattribs.locked_vatts, (void*)g_vattribs.active_vatts);

	g_vattribs.locked_vatts = 0;
}

int qdx_vattparam_to_index(vatt_param_t param)
{
	int ret = 0;

	switch (param)
	{
	case VATT_VERTEX:
		ret = 1;
		break;
	case VATT_NORMAL:
		ret = 2;
		break;
	case VATT_COLOR:
		ret = 3;
		break;
	case VATT_TEX0:
		ret = 4;
		break;
	case VATT_TEX1:
		ret = 5;
		break;
	case VATT_COLORVAL:
		ret = 6;
		break;
	default:
		qassert(FALSE);
	}
	C_ASSERT(VATT_PARAM_ARRAYSZ == 2); //make sure we keep these in sync

	return ret;
}

void qdx_vatt_set_buffer(vatt_param_t param, const void *buffer, UINT elems, UINT stride)
{
	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VATTSET))
		qdx_log_comment(__FUNCTION__, param, buffer);

	switch (param)
	{
	case VATT_VERTEX:
		g_vattribs.vertexes = (float*)buffer;
		//g_vattribs.numverts = elems;
		g_vattribs.strideverts = stride;
		break;
	case VATT_NORMAL:
		g_vattribs.normals = (float*)buffer;
		//g_vattribs.numnorms = elems;
		g_vattribs.stridenorms = stride;
		break;
	case VATT_COLOR:
		g_vattribs.colors = (byte*)buffer;
		//g_vattribs.numcols = elems;
		g_vattribs.stridecols = stride;
		break;
	case VATT_TEX0:
		g_vattribs.texcoord[0] = (float*)buffer;
		//g_vattribs.numtexcoords[0] = elems;
		g_vattribs.stridetexcoords[0] = stride;
		break;
	case VATT_TEX1:
		g_vattribs.texcoord[1] = (float*)buffer;
		//g_vattribs.numtexcoords[1] = elems;
		g_vattribs.stridetexcoords[1] = stride;
		break;
	case VATT_TEX0 | VATT_TEX1:
		g_vattribs.texcoord[0] = (float*)buffer;
		//g_vattribs.numtexcoords[0] = elems;
		g_vattribs.stridetexcoords[0] = stride;
		g_vattribs.texcoord[1] = (float*)buffer;
		//g_vattribs.numtexcoords[1] = elems;
		g_vattribs.stridetexcoords[1] = stride;
		break;
	default:
		return;
	}

	if (buffer == NULL)
	{
		g_vattribs.active_vatts &= ~param;
	}
	else
	{
		g_vattribs.active_vatts |= param;
	}
}

static int qdx_draw_process_vert(int lowindex, int highindex, byte *pVert);
static int qdx_draw_process_vertcol(int lowindex, int highindex, byte *pVert);
static int qdx_draw_process_vertnormcol(int lowindex, int highindex, byte *pVert);
static int qdx_draw_process_vertcoltex(int lowindex, int highindex, byte *pVert);
static int qdx_draw_process_vertnormcoltex(int lowindex, int highindex, byte *pVert);
static int qdx_draw_process_2dvertcoltex(int lowindex, int highindex, byte *pVert);
static int qdx_draw_process_verttex(int lowindex, int highindex, byte *pVert);
static int qdx_draw_process_vertnormtex(int lowindex, int highindex, byte *pVert);
static int qdx_draw_process_vertcoltex2(int lowindex, int highindex, byte *pVert);
static int qdx_draw_process_vertnormcoltex2(int lowindex, int highindex, byte *pVert);

#define ON_FAIL_RET_NULL(X) if(FAILED(X)) { qassert(FALSE); return NULL; }
#define ON_FAIL_RETURN(X) if(FAILED(X)) { qassert(FALSE); return; }

static void qdx_draw_process(uint32_t selectionbits, int index, byte* buf)
{
	switch (selectionbits)
	{
	case (VATT_VERTEX): {
		qdx_draw_process_vert(index, index, buf);
		break; }
	case (VATT_VERTEX | VATT_COLOR): {
		qdx_draw_process_vertcol(index, index, buf);
		break; }
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR): {
		qdx_draw_process_vertnormcol(index, index, buf);
		break; }
	case (VATT_VERTEX | VATT_COLOR | VATT_TEX0): {
		qdx_draw_process_vertcoltex(index, index, buf);
		break; }
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR | VATT_TEX0): {
		qdx_draw_process_vertnormcoltex(index, index, buf);
		break; }
	case (VATT_2DVERTEX | VATT_COLOR | VATT_TEX0): {
		qdx_draw_process_2dvertcoltex(index, index, buf);
		break; }
	case (VATT_VERTEX | VATT_TEX0): {
		qdx_draw_process_verttex(index, index, buf);
		break; }
	case (VATT_VERTEX | VATT_NORMAL | VATT_TEX0): {
		qdx_draw_process_vertnormtex(index, index, buf);
		break; }
	case (VATT_VERTEX | VATT_COLOR | VATT_TEX0 | VATT_TEX1): {
		qdx_draw_process_vertcoltex2(index, index, buf);
		break; }
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR | VATT_TEX0 | VATT_TEX1): {
		qdx_draw_process_vertnormcoltex2(index, index, buf);
		break; }
	default:
		break;
	}
}

void qdx_vatt_assemble_and_draw(UINT numindexes, const qdxIndex_t *indexes, const char *hint)
{
	DWORD selected_vatt = VATTID_VERTCOL;
	UINT stride_vatt = sizeof(vatt_vertcol_t);
	qdxIndex_t lowindex = SHADER_MAX_VERTEXES, highindex = 0, offindex = 0;
	UINT selectionsize = 0;
	UINT hash = 0;
	int ercd;

	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VERTEX))
		qdx_log_comment(__FUNCTION__, g_vattribs.active_vatts, (const void*)numindexes);

	if (0 == numindexes || 0 == (g_vattribs.active_vatts & VATT_VERTEX) /*|| 0 == (g_vattribs.active_vatts & (VATT_TEX0|VATT_TEX1))*/)
	{
		//see comment in RE_BeginRegistration for the call to RE_StretchPic
		//the vertex buffer pointer gets set when RB_StageIteratorGeneric is called, which is triggered by that RE_StretchPic 
		//todo: I guess this is something we can actually fix now, since we know why it happens
		return;
	}

	//if (hint)
	//{
	//	hash = fnv_32a_str(hint, hash);
	//}

	////hash = fnv_32a_buf(&numindexes, sizeof(numindexes), hash);

	//if (g_vattribs.active_vatts & VATT_TEX0)
	//{
	//	hash = fnv_32a_buf(&g_vattribs.textureids[0], sizeof(INT), hash);
	//}
	//if (g_vattribs.active_vatts & VATT_TEX1)
	//{
	//	hash = fnv_32a_buf(&g_vattribs.textureids[1], sizeof(INT), hash);
	//}

	for (int i = 0; i < numindexes; i++)
	{
		UINT index = indexes[i];
		if (index < lowindex)
		{
			lowindex = index;
		}
		if (index > highindex)
		{
			highindex = index;
		}
	}

	selectionsize = 1 + highindex - lowindex;

	UINT selectionbits = g_vattribs.active_vatts;
	//if (g_vattribs.is2dprojection && (0 != (g_vattribs.bits & VATT_VERTEX)))
	//{
	//	selectionbits &= ~VATT_VERTEX;
	//	selectionbits |= VATT_2DVERTEX;
	//}

	switch (selectionbits)
	{
	case (VATT_VERTEX):
	case (VATT_VERTEX | VATT_COLOR):
		selected_vatt = VATTID_VERTCOL;
		stride_vatt = sizeof(vatt_vertcol_t);
		break;
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR):
		selected_vatt = VATTID_VERTNORMCOL;
		stride_vatt = sizeof(vatt_vertnormcol_t);
		break;
	case (VATT_VERTEX | VATT_TEX0):
	case (VATT_VERTEX | VATT_COLOR | VATT_TEX0):
		selected_vatt = VATTID_VERTCOLTEX;
		stride_vatt = sizeof(vatt_vertcoltex_t);
		break;
	case (VATT_VERTEX | VATT_NORMAL | VATT_TEX0):
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR | VATT_TEX0):
		selected_vatt = VATTID_VERTNORMCOLTEX;
		stride_vatt = sizeof(vatt_vertnormcoltex_t);
		break;
	case (VATT_2DVERTEX | VATT_COLOR | VATT_TEX0):
		selected_vatt = VATTID_2DVERTCOLTEX;
		stride_vatt = sizeof(vatt_2dvertcoltex_t);
		break;
	case (VATT_VERTEX | VATT_COLOR | VATT_TEX0 | VATT_TEX1):
		selected_vatt = VATTID_VERTCOLTEX2;
		stride_vatt = sizeof(vatt_vertcoltex2_t);
		break;
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR | VATT_TEX0 | VATT_TEX1):
		selected_vatt = VATTID_VERTNORMCOLTEX2;
		stride_vatt = sizeof(vatt_vertnormcoltex2_t);
		break;
	default:
		qassert(FALSE);
		return;
	}

	LPDIRECT3DINDEXBUFFER9 i_buffer = 0;
	LPDIRECT3DVERTEXBUFFER9 v_buffer = 0;
	struct vatt_buff_stats *bufstats;

	ercd = qdx_get_buffers(&i_buffer, &v_buffer, selected_vatt, stride_vatt, hash, &bufstats);
	if (ercd != 0)
	{
		ri.Error(ERR_FATAL, "DrawPrimitives - not enough buffers\n");
	}

	bool skip_upload = false;
	uint32_t ihash = 0;
	uint32_t vhash = 0;

	//ihash =  fnv_32a_buf(indexes, numindexes * sizeof(qdxIndex_t), 0);

	//if ((VATT_VERTEX & selectionbits) != 0)
	//{
	//	vhash = fnv_32a_buf(GVATT_VERTELEM(lowindex), selectionsize * g_vattribs.strideverts, vhash);
	//}
	//if ((VATT_2DVERTEX & selectionbits) != 0)
	//{
	//	vhash = fnv_32a_buf(GVATT_VERTELEM(lowindex), selectionsize * g_vattribs.strideverts, vhash);
	//}
	//if ((VATT_NORMAL & selectionbits) != 0)
	//{
	//	vhash = fnv_32a_buf(GVATT_NORMELEM(lowindex), selectionsize * g_vattribs.stridenorms, vhash);
	//}
	//if ((VATT_COLOR & selectionbits) != 0)
	//{
	//	vhash = fnv_32a_buf(GVATT_COLRELEM(lowindex), selectionsize * g_vattribs.stridecols, vhash);
	//}
	//if ((VATT_TEX0 & selectionbits) != 0)
	//{
	//	vhash = fnv_32a_buf(GVATT_TEX0ELEM(lowindex), selectionsize * g_vattribs.stridetexcoords[0], vhash);
	//}
	//if ((VATT_TEX1 & selectionbits) != 0)
	//{
	//	vhash = fnv_32a_buf(GVATT_TEX1ELEM(lowindex), selectionsize * g_vattribs.stridetexcoords[0], vhash);
	//}

	//if ((ihash == bufstats->checksum[0]) && (vhash == bufstats->checksum[1]))
	//{
	//	skip_upload = true;
	//}

	if (skip_upload)
	{
		if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VERTEX))
			DX9imp_LogComment(">>>>>>>>>> HASHES MATCH <<<<<<<<<<\n");
	}
	else
	{

		offindex = lowindex;

		if (0 && ihash == bufstats->checksum[0])
		{
			qdx_log_dump_buffer("index", 0, 0, 16, hash, ihash);
		}
		else
		{
			qdxIndex_t* pInd;
			ON_FAIL_RETURN(i_buffer->Lock(0, numindexes * sizeof(qdxIndex_t), (void**)&pInd, D3DLOCK_DISCARD));//always discard old contents
			for (int i = 0; i < numindexes; i++)
			{
				pInd[i] = indexes[i] - offindex;
			}
			if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VERTEX))
				qdx_log_dump_buffer("index", pInd, numindexes, 16, hash, ihash);

			i_buffer->Unlock();
		}

		bufstats->checksum[0] = ihash;
		bufstats->checksum[1] = vhash;
		bufstats->num_entries = selectionsize;

		byte* pVert;
		ON_FAIL_RETURN(v_buffer->Lock((lowindex - offindex) * stride_vatt, selectionsize * stride_vatt, (void**)&pVert, D3DLOCK_DISCARD));

		int vpos = 0;

		switch (selectionbits)
		{
		case (VATT_VERTEX): {
			vpos = qdx_draw_process_vert(lowindex, highindex, pVert);
			break; }
		case (VATT_VERTEX | VATT_COLOR): {
			vpos = qdx_draw_process_vertcol(lowindex, highindex, pVert);
			break; }
		case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR): {
			vpos = qdx_draw_process_vertnormcol(lowindex, highindex, pVert);
			break; }
		case (VATT_VERTEX | VATT_COLOR | VATT_TEX0): {
			vpos = qdx_draw_process_vertcoltex(lowindex, highindex, pVert);
			break; }
		case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR | VATT_TEX0): {
			vpos = qdx_draw_process_vertnormcoltex(lowindex, highindex, pVert);
			break; }
		case (VATT_2DVERTEX | VATT_COLOR | VATT_TEX0): {
			vpos = qdx_draw_process_2dvertcoltex(lowindex, highindex, pVert);
			break; }
		case (VATT_VERTEX | VATT_TEX0): {
			vpos = qdx_draw_process_verttex(lowindex, highindex, pVert);
			break; }
		case (VATT_VERTEX | VATT_NORMAL | VATT_TEX0): {
			vpos = qdx_draw_process_vertnormtex(lowindex, highindex, pVert);
			break; }
		case (VATT_VERTEX | VATT_COLOR | VATT_TEX0 | VATT_TEX1): {
			vpos = qdx_draw_process_vertcoltex2(lowindex, highindex, pVert);
			break; }
		case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR | VATT_TEX0 | VATT_TEX1): {
			vpos = qdx_draw_process_vertnormcoltex2(lowindex, highindex, pVert);
			break; }
		default:
			qassert(FALSE);
		}

		qassert(vpos == selectionsize);
		if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VERTEX))
			qdx_log_dump_buffer("vertex", (const uint32_t*)pVert, selectionsize * stride_vatt / sizeof(uint32_t), stride_vatt / sizeof(uint32_t), hash, vhash);

		v_buffer->Unlock();
	}

	//qdx.device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0x33, 0x4d, 0x4d), 1.0f, 0);
	//qdx.device->Clear(0, NULL, D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
	//qdx.device->SetRenderState(D3DRS_LIGHTING, FALSE);
	//qdx.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	DX9_BEGIN_SCENE();

	qdx.device->SetFVF(selected_vatt);

	qdx.device->SetStreamSource(0, v_buffer, 0, stride_vatt);
	qdx.device->SetIndices(i_buffer);

	if (g_vattribs.active_vatts & VATT_TEX0)
	{
		qdx_texobj_apply(g_vattribs.textureids[0], 0);
	}
	else qdx_texobj_apply(TEXID_NULL, 0);
	if (g_vattribs.active_vatts & VATT_TEX1)
	{
		qdx_texobj_apply(g_vattribs.textureids[1], 1);
	}
	else qdx_texobj_apply(TEXID_NULL, 1);

	qdx_matrix_apply();

	qdx.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, selectionsize, 0, numindexes / 3);

	DX9_END_SCENE();
}

static void qdx_draw_process(uint32_t selectionbits, int index, byte* buf);

void qdx_vatt_assemble_and_draw0(UINT numindexes, const qdxIndex_t *indexes, const char *hint)
{
	DWORD selected_vatt = VATTID_VERTCOL;
	UINT stride_vatt = sizeof(vatt_vertcol_t);
	qdxIndex_t lowindex = SHADER_MAX_INDEXES, highindex = 0;
	UINT selectionsize = 0;
	UINT hash = 0;

	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VERTEX))
		qdx_log_comment(__FUNCTION__, g_vattribs.active_vatts, (const void*)numindexes);

	if (0 == (g_vattribs.active_vatts & VATT_VERTEX))
	{
		//see comment in RE_BeginRegistration for the call to RE_StretchPic
		//the vertex buffer pointer gets set when RB_StageIteratorGeneric is called, which is triggered by that RE_StretchPic 
		//todo: I guess this is something we can actually fix now, since we know why it happens
		return;
	}

	if (hint)
	{
		hash = fnv_32a_str(hint, hash);
	}
	if (g_vattribs.active_vatts & VATT_TEX0)
	{
		hash = fnv_32a_buf(&g_vattribs.textureids[0], sizeof(INT), hash);
	}
	if (g_vattribs.active_vatts & VATT_TEX1)
	{
		hash = fnv_32a_buf(&g_vattribs.textureids[1], sizeof(INT), hash);
	}

	for (int i = 0; i < numindexes; i++)
	{
		UINT index = indexes[i];
		if (index < lowindex)
		{
			lowindex = index;
		}
		if (index > highindex)
		{
			highindex = index;
		}
	}

	selectionsize = 1 + highindex - lowindex;

	UINT selectionbits = g_vattribs.active_vatts;
	//if (g_vattribs.is2dprojection && (0 != (g_vattribs.bits & VATT_VERTEX)))
	//{
	//	selectionbits &= ~VATT_VERTEX;
	//	selectionbits |= VATT_2DVERTEX;
	//}

	switch (selectionbits)
	{
	case (VATT_VERTEX):
	case (VATT_VERTEX | VATT_COLOR):
		selected_vatt = VATTID_VERTCOL;
		stride_vatt = sizeof(vatt_vertcol_t);
		break;
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR):
		selected_vatt = VATTID_VERTNORMCOL;
		stride_vatt = sizeof(vatt_vertnormcol_t);
		break;
	case (VATT_VERTEX | VATT_TEX0):
	case (VATT_VERTEX | VATT_COLOR | VATT_TEX0):
		selected_vatt = VATTID_VERTCOLTEX;
		stride_vatt = sizeof(vatt_vertcoltex_t);
		break;
	case (VATT_VERTEX | VATT_NORMAL | VATT_TEX0):
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR | VATT_TEX0):
		selected_vatt = VATTID_VERTNORMCOLTEX;
		stride_vatt = sizeof(vatt_vertnormcoltex_t);
		break;
	case (VATT_2DVERTEX | VATT_COLOR | VATT_TEX0):
		selected_vatt = VATTID_2DVERTCOLTEX;
		stride_vatt = sizeof(vatt_2dvertcoltex_t);
		break;
	case (VATT_VERTEX | VATT_COLOR | VATT_TEX0 | VATT_TEX1):
		selected_vatt = VATTID_VERTCOLTEX2;
		stride_vatt = sizeof(vatt_vertcoltex2_t);
		break;
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR | VATT_TEX0 | VATT_TEX1):
		selected_vatt = VATTID_VERTNORMCOLTEX2;
		stride_vatt = sizeof(vatt_vertnormcoltex2_t);
		break;
	default:
		qassert(FALSE);
	}

	static void* drawbuf = 0;
	if(drawbuf == 0)
		drawbuf = malloc(SHADER_MAX_VERTEXES * sizeof(vatt_vertnormcoltex2_t));


	DX9_BEGIN_SCENE();

	//qdx.device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0x33, 0x4d, 0x4d), 1.0f, 0);
	//qdx.device->Clear(0, NULL, D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
	//qdx.device->SetRenderState(D3DRS_LIGHTING, FALSE);
	//qdx.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	qdx.device->SetFVF(selected_vatt);

	if (g_vattribs.active_vatts & VATT_TEX0)
	{
		qdx_texobj_apply(g_vattribs.textureids[0], 0);
	}
	else qdx_texobj_apply(TEXID_NULL, 0);
	if (g_vattribs.active_vatts & VATT_TEX1)
	{
		qdx_texobj_apply(g_vattribs.textureids[1], 1);
	}
	else qdx_texobj_apply(TEXID_NULL, 1);

	qdx_matrix_apply();


	byte* pVert = (byte*)drawbuf;

	int c_vertexes = 0;
	int last[3] = { -1, -1, -1 };
	qboolean even;

#define element( X ) qdx_draw_process(selectionbits, (X), pVert); pVert += stride_vatt;

	// prime the strip
	element( indexes[0] );
	element( indexes[1] );
	element( indexes[2] );
	c_vertexes += 3;

	last[0] = indexes[0];
	last[1] = indexes[1];
	last[2] = indexes[2];

	even = qfalse;

	for(int i = 3; i < numindexes; i+=3)
	{		// odd numbered triangle in potential strip
		if ( !even ) {
			// check previous triangle to see if we're continuing a strip
			if ( ( indexes[i + 0] == last[2] ) && ( indexes[i + 1] == last[1] ) ) {
				element( indexes[i + 2] );
				c_vertexes++;
				even = qtrue;
			}
			// otherwise we're done with this strip so finish it and start
			// a new one
			else
			{
				qdx.device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, c_vertexes - 2, drawbuf, stride_vatt);
				pVert = (byte*)drawbuf;
				c_vertexes = 0;

				element( indexes[i + 0] );
				element( indexes[i + 1] );
				element( indexes[i + 2] );

				c_vertexes += 3;

				even = qfalse;
			}
		} else
		{
			// check previous triangle to see if we're continuing a strip
			if ( ( last[2] == indexes[i + 1] ) && ( last[0] == indexes[i + 0] ) ) {
				element( indexes[i + 2] );
				c_vertexes++;

				even = qfalse;
			}
			// otherwise we're done with this strip so finish it and start
			// a new one
			else
			{
				qdx.device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, c_vertexes - 2, drawbuf, stride_vatt);
				pVert = (byte*)drawbuf;
				c_vertexes = 0;

				element( indexes[i + 0] );
				element( indexes[i + 1] );
				element( indexes[i + 2] );
				c_vertexes += 3;

				even = qfalse;
			}
		}

		// cache the last three vertices
		last[0] = indexes[i + 0];
		last[1] = indexes[i + 1];
		last[2] = indexes[i + 2];
	}

	if(c_vertexes > 2)
		qdx.device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, c_vertexes - 2, drawbuf, stride_vatt);

	DX9_END_SCENE();
}

void qdx_vatt_assemble_and_draw0a(UINT numindexes, const qdxIndex_t *indexes, const char *hint)
{
	DWORD selected_vatt = VATTID_VERTCOL;
	UINT stride_vatt = sizeof(vatt_vertcol_t);
	qdxIndex_t lowindex = SHADER_MAX_INDEXES, highindex = 0;
	UINT selectionsize = 0;
	UINT hash = 0;

	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_VERTEX))
		qdx_log_comment(__FUNCTION__, g_vattribs.active_vatts, (const void*)numindexes);

	if (0 == (g_vattribs.active_vatts & VATT_VERTEX))
	{
		//see comment in RE_BeginRegistration for the call to RE_StretchPic
		//the vertex buffer pointer gets set when RB_StageIteratorGeneric is called, which is triggered by that RE_StretchPic 
		//todo: I guess this is something we can actually fix now, since we know why it happens
		return;
	}

	if (hint)
	{
		hash = fnv_32a_str(hint, hash);
	}
	if (g_vattribs.active_vatts & VATT_TEX0)
	{
		hash = fnv_32a_buf(&g_vattribs.textureids[0], sizeof(INT), hash);
	}
	if (g_vattribs.active_vatts & VATT_TEX1)
	{
		hash = fnv_32a_buf(&g_vattribs.textureids[1], sizeof(INT), hash);
	}

	for (int i = 0; i < numindexes; i++)
	{
		UINT index = indexes[i];
		if (index < lowindex)
		{
			lowindex = index;
		}
		if (index > highindex)
		{
			highindex = index;
		}
	}

	selectionsize = 1 + highindex - lowindex;

	UINT selectionbits = g_vattribs.active_vatts;
	//if (g_vattribs.is2dprojection && (0 != (g_vattribs.bits & VATT_VERTEX)))
	//{
	//	selectionbits &= ~VATT_VERTEX;
	//	selectionbits |= VATT_2DVERTEX;
	//}

	switch (selectionbits)
	{
	case (VATT_VERTEX):
	case (VATT_VERTEX | VATT_COLOR):
		selected_vatt = VATTID_VERTCOL;
		stride_vatt = sizeof(vatt_vertcol_t);
		break;
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR):
		selected_vatt = VATTID_VERTNORMCOL;
		stride_vatt = sizeof(vatt_vertnormcol_t);
		break;
	case (VATT_VERTEX | VATT_TEX0):
	case (VATT_VERTEX | VATT_COLOR | VATT_TEX0):
		selected_vatt = VATTID_VERTCOLTEX;
		stride_vatt = sizeof(vatt_vertcoltex_t);
		break;
	case (VATT_VERTEX | VATT_NORMAL | VATT_TEX0):
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR | VATT_TEX0):
		selected_vatt = VATTID_VERTNORMCOLTEX;
		stride_vatt = sizeof(vatt_vertnormcoltex_t);
		break;
	case (VATT_2DVERTEX | VATT_COLOR | VATT_TEX0):
		selected_vatt = VATTID_2DVERTCOLTEX;
		stride_vatt = sizeof(vatt_2dvertcoltex_t);
		break;
	case (VATT_VERTEX | VATT_COLOR | VATT_TEX0 | VATT_TEX1):
		selected_vatt = VATTID_VERTCOLTEX2;
		stride_vatt = sizeof(vatt_vertcoltex2_t);
		break;
	case (VATT_VERTEX | VATT_NORMAL | VATT_COLOR | VATT_TEX0 | VATT_TEX1):
		selected_vatt = VATTID_VERTNORMCOLTEX2;
		stride_vatt = sizeof(vatt_vertnormcoltex2_t);
		break;
	default:
		qassert(FALSE);
	}

	static UINT* drawbuf = 0;
	if (drawbuf == 0)
	{
		drawbuf = (UINT*)malloc(SHADER_MAX_VERTEXES * 2 * sizeof(UINT));
	}
	int drawbuf_idx = 0;

	LPDIRECT3DVERTEXBUFFER9 v_buffer = 0;
	struct vatt_buff_stats *bufstats;

	int ercd = qdx_get_buffers(0, &v_buffer, selected_vatt, stride_vatt, 0, &bufstats);
	if (ercd != 0)
	{
		ri.Error(ERR_FATAL, "DrawPrimitives - not enough buffers\n");
	}

	byte* pVert = 0;
	ON_FAIL_RETURN(v_buffer->Lock(0, 0, (void**)&pVert, D3DLOCK_DISCARD));

	int c_vertexes = 0;
	int total_vertexes = 0;
	int last[3] = { -1, -1, -1 };
	qboolean even;

#define element( X ) qdx_draw_process(selectionbits, (X), pVert); pVert += stride_vatt;

	// prime the strip
	element( indexes[0] );
	element( indexes[1] );
	element( indexes[2] );
	c_vertexes += 3;

	last[0] = indexes[0];
	last[1] = indexes[1];
	last[2] = indexes[2];

	even = qfalse;

	for(int i = 3; i < numindexes; i+=3)
	{		// odd numbered triangle in potential strip
		if ( !even ) {
			// check previous triangle to see if we're continuing a strip
			if ( ( indexes[i + 0] == last[2] ) && ( indexes[i + 1] == last[1] ) ) {
				element( indexes[i + 2] );
				c_vertexes++;
				even = qtrue;
			}
			// otherwise we're done with this strip so finish it and start
			// a new one
			else
			{
				drawbuf[drawbuf_idx] = total_vertexes;
				drawbuf[drawbuf_idx + 1] = c_vertexes - 2;
				total_vertexes += c_vertexes;
				drawbuf_idx += 2;
				//qdx.device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, c_vertexes - 2, drawbuf, stride_vatt);
				//pVert = (byte*)drawbuf;
				c_vertexes = 0;

				element( indexes[i + 0] );
				element( indexes[i + 1] );
				element( indexes[i + 2] );

				c_vertexes += 3;

				even = qfalse;
			}
		} else
		{
			// check previous triangle to see if we're continuing a strip
			if ( ( last[2] == indexes[i + 1] ) && ( last[0] == indexes[i + 0] ) ) {
				element( indexes[i + 2] );
				c_vertexes++;

				even = qfalse;
			}
			// otherwise we're done with this strip so finish it and start
			// a new one
			else
			{
				drawbuf[drawbuf_idx] = total_vertexes;
				drawbuf[drawbuf_idx + 1] = c_vertexes - 2;
				total_vertexes += c_vertexes;
				drawbuf_idx += 2;
				//qdx.device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, c_vertexes - 2, drawbuf, stride_vatt);
				//pVert = (byte*)drawbuf;
				c_vertexes = 0;

				element( indexes[i + 0] );
				element( indexes[i + 1] );
				element( indexes[i + 2] );
				c_vertexes += 3;

				even = qfalse;
			}
		}

		// cache the last three vertices
		last[0] = indexes[i + 0];
		last[1] = indexes[i + 1];
		last[2] = indexes[i + 2];
	}

	if (c_vertexes > 2)
	{
		//qdx.device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, c_vertexes - 2, drawbuf, stride_vatt);
		drawbuf[drawbuf_idx] = total_vertexes;
		drawbuf[drawbuf_idx + 1] = c_vertexes - 2;
		total_vertexes += c_vertexes;
		drawbuf_idx += 2;
	}

	v_buffer->Unlock();


	DX9_BEGIN_SCENE();

	qdx.device->SetFVF(selected_vatt);

	qdx.device->SetStreamSource(0, v_buffer, 0, stride_vatt);

	qdx_texture_apply();

	qdx_matrix_apply();

	for (int i = 0; i < drawbuf_idx; i+=2)
	{
		qdx.device->DrawPrimitive(D3DPT_TRIANGLESTRIP, drawbuf[i], drawbuf[i+1]);
	}

	DX9_END_SCENE();
}

BOOL qdx_vbuffer_steps( buffersteps_t step, qdx_vbuffer_t *buf, UINT vattid, UINT offset, UINT size, void **outmem )
{
	LPDIRECT3DVERTEXBUFFER9 v_buffer = *buf;

	if ( step & STEP_ALLOCATE )
	{
		if (v_buffer == NULL)
		{
			if (FAILED(
				qdx.device->CreateVertexBuffer(size,
					0,
					vattid,
					D3DPOOL_MANAGED,
					&v_buffer,
					NULL)))
			{
				return FALSE;
			}
			*buf = v_buffer;
		}
		//else return false?
	}
	if ( step & (STEP_GET_DATAPTR_CLEAR|STEP_GET_DATAPTR_APPEND) )
	{
		if (v_buffer != NULL)
		{
			if (outmem)
			{
				if (FAILED(v_buffer->Lock(offset, size, outmem, (step & STEP_GET_DATAPTR_CLEAR) ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE)))
				{
					return FALSE;
				}
			}
		}
	}
	if ( step & STEP_FINALIZE )
	{
		if (v_buffer != NULL)
		{
			v_buffer->Unlock();
		}
	}

	return TRUE;
}

qdx_vbuffer_t qdx_vbuffer_create_and_upload(qdx_vbuffer_t buf, UINT vattid, UINT size, void *data)
{
	LPDIRECT3DVERTEXBUFFER9 v_buffer = buf;

	if (buf == NULL)
	{
		ON_FAIL_RET_NULL(
			qdx.device->CreateVertexBuffer(size,
				0,
				vattid,
				D3DPOOL_MANAGED,
				&v_buffer,
				NULL));
	}

	if (size > 0 && data != NULL)
	{
		VOID* pVoid;

		ON_FAIL_RET_NULL(v_buffer->Lock(0, 0, (void**)&pVoid, D3DLOCK_DISCARD));
		memcpy(pVoid, data, size);
		v_buffer->Unlock();
	}

	return v_buffer;
}

void qdx_vbuffer_release(qdx_vbuffer_t buf)
{
	buf->Release();
}


BOOL qdx_ibuffer_steps( buffersteps_t step, qdx_ibuffer_t* buf, UINT format, UINT offset, UINT size, void** outmem )
{
	LPDIRECT3DINDEXBUFFER9 i_buffer = *buf;

	if ( step & STEP_ALLOCATE )
	{
		if (i_buffer == NULL)
		{
			if (FAILED(
				qdx.device->CreateIndexBuffer(size,
					0,
					(D3DFORMAT)format,
					D3DPOOL_MANAGED,
					&i_buffer,
					NULL)))
			{
				return FALSE;
			}
			*buf = i_buffer;
		}
		//else return false?
	}
	if ( step & (STEP_GET_DATAPTR_CLEAR|STEP_GET_DATAPTR_APPEND) )
	{
		if (i_buffer != NULL)
		{
			if (outmem)
			{
				if (FAILED(i_buffer->Lock(offset, size, outmem, (step & STEP_GET_DATAPTR_CLEAR) ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE)))
				{
					return FALSE;
				}
			}
		}
	}
	if ( step & STEP_FINALIZE )
	{
		if (i_buffer != NULL)
		{
			i_buffer->Unlock();
		}
	}

	return TRUE;
}

void qdx_ibuffer_release(qdx_ibuffer_t buf)
{
	buf->Release();
}

static void qdx_animationbuf_reset(BOOL release)
{
	struct qdx9_state::animation_buff_s* anim = &qdx.skinned_mesh;
	if ( release )
	{
		if ( anim->vbuffer )
		{
			anim->vbuffer->Release();
			anim->vbuffer = NULL;
		}
		if ( anim->ibuffer )
		{
			anim->ibuffer->Release();
			anim->ibuffer = NULL;
		}
	}
	anim->vertex_count = 0;
	anim->index_count = 0;
	anim->bone_count = 1;
	//memset( anim->bonemapping, 0xFF, sizeof( anim->bonemapping ) );
}

void qdx_animation_process()
{
	struct qdx9_state::animation_buff_s* anim = &qdx.skinned_mesh;
	if ( anim->index_count )
	{
		DX9_BEGIN_SCENE();

		IDirect3DDevice9_SetRenderState( qdx.device, D3DRS_VERTEXBLEND, D3DVBF_3WEIGHTS );
		IDirect3DDevice9_SetRenderState( qdx.device, D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE );
		IDirect3DDevice9_SetFVF( qdx.device, VATTID_ANIM );

		shaderStage_t* pStage = tess.xstages[0];
		qdx_vatt_attach_texture( pStage->bundle[0].image[0]->texnum - TEXNUM_OFFSET, 0 );
		
		D3DXMATRIX matid;
		D3DXMatrixIdentity( &matid );
		qdx_matrix_push( D3DTS_WORLD );
		qdx_matrix_set(D3DTS_WORLD, &matid.m[0][0]);
		qdx_matrix_apply();
		qdx_matrix_pop( D3DTS_WORLD );

		qdx.device->SetIndices( anim->ibuffer );
		qdx.device->SetStreamSource( 0, anim->vbuffer, 0, sizeof( vatt_anim_t ) );

		qdx_texture_apply();
		qdx.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, anim->vertex_count, 0, anim->index_count / 3 );

		IDirect3DDevice9_SetRenderState( qdx.device, D3DRS_VERTEXBLEND, D3DVBF_DISABLE );
		IDirect3DDevice9_SetRenderState( qdx.device, D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE );

		DX9_END_SCENE();

		qdx_animationbuf_reset(FALSE);
	}
}

static int qdx_draw_process_vert(int lowindex, int highindex, byte *pVert)
{
	int vpos = 0;
	const float* dv = GVATT_VERTELEM(lowindex);
	for (int i = lowindex; i <= highindex; i++)
	{
		vatt_vertcol_t *p = (vatt_vertcol_t *)pVert;
		copy_three(p->XYZ, dv);
		p->COLOR = qdx.crt_color;
		GVATT_INCVERTP(dv);
		pVert += sizeof(p[0]);

		vpos++;
	}

	return vpos;
}

static int qdx_draw_process_vertcol(int lowindex, int highindex, byte *pVert)
{
	int vpos = 0;
	const float* dv = GVATT_VERTELEM(lowindex);
	const byte* dc = GVATT_COLRELEM(lowindex);
	for (int i = lowindex; i <= highindex; i++)
	{
		vatt_vertcol_t *p = (vatt_vertcol_t *)pVert;
		copy_three(p->XYZ, dv);
		p->COLOR = abgr_to_argb(dc);
		GVATT_INCVERTP(dv);
		GVATT_INCCOLRP(dc);
		pVert += sizeof(p[0]);

		vpos++;
	}

	return vpos;
}

static int qdx_draw_process_vertnormcol(int lowindex, int highindex, byte *pVert)
{
	int vpos = 0;
	const float* dv = GVATT_VERTELEM(lowindex);
	const float* dn = GVATT_NORMELEM(lowindex);
	const byte* dc = GVATT_COLRELEM(lowindex);
	for (int i = lowindex; i <= highindex; i++)
	{
		vatt_vertnormcol_t* p = (vatt_vertnormcol_t*)pVert;
		copy_three(p->XYZ, dv);
		copy_three(p->NORM, dn);
		p->COLOR = abgr_to_argb(dc);
		GVATT_INCVERTP(dv);
		GVATT_INCNORMP(dn);
		GVATT_INCCOLRP(dc);
		pVert += sizeof(p[0]);

		vpos++;
	}

	return vpos;
}

static int qdx_draw_process_vertcoltex(int lowindex, int highindex, byte *pVert)
{
	int vpos = 0;
	const float* dv = GVATT_VERTELEM(lowindex);
	const byte* dc = GVATT_COLRELEM(lowindex);
	const float* dt0 = GVATT_TEX0ELEM(lowindex);
	for (int i = lowindex; i <= highindex; i++)
	{
		vatt_vertcoltex_t *p = (vatt_vertcoltex_t *)pVert;
		copy_three(p->XYZ, dv);
		p->COLOR = abgr_to_argb(dc);
		copy_two(p->UV, dt0);
		GVATT_INCVERTP(dv);
		GVATT_INCCOLRP(dc);
		GVATT_INCTEX0P(dt0);
		pVert += sizeof(p[0]);

		vpos++;
	}

	return vpos;
}

static int qdx_draw_process_vertnormcoltex(int lowindex, int highindex, byte *pVert)
{
	int vpos = 0;
	const float* dv = GVATT_VERTELEM(lowindex);
	const float* dn = GVATT_NORMELEM(lowindex);
	const byte* dc = GVATT_COLRELEM(lowindex);
	const float* dt0 = GVATT_TEX0ELEM(lowindex);
	for (int i = lowindex; i <= highindex; i++)
	{
		vatt_vertnormcoltex_t* p = (vatt_vertnormcoltex_t*)pVert;
		copy_three(p->XYZ, dv);
		copy_three(p->NORM, dn);
		p->COLOR = abgr_to_argb(dc);
		copy_two(p->UV, dt0);
		GVATT_INCVERTP(dv);
		GVATT_INCNORMP(dn);
		GVATT_INCCOLRP(dc);
		GVATT_INCTEX0P(dt0);
		pVert += sizeof(p[0]);

		vpos++;
	}

	return vpos;
}

static int qdx_draw_process_2dvertcoltex(int lowindex, int highindex, byte *pVert)
{
	int vpos = 0;
	const float* dv = GVATT_VERTELEM(lowindex);
	const byte* dc = GVATT_COLRELEM(lowindex);
	const float* dt0 = GVATT_TEX0ELEM(lowindex);
	for (int i = lowindex; i <= highindex; i++)
	{
		vatt_2dvertcoltex_t *p = (vatt_2dvertcoltex_t *)pVert;
		copy_xyzrhv(p->XYZRHV, dv);
		p->COLOR = abgr_to_argb(dc);
		copy_two(p->UV, dt0);
		GVATT_INCVERTP(dv);
		GVATT_INCCOLRP(dc);
		GVATT_INCTEX0P(dt0);
		pVert += sizeof(p[0]);

		vpos++;
	}

	return vpos;
}

static int qdx_draw_process_verttex(int lowindex, int highindex, byte *pVert)
{
	int vpos = 0;
	const float* dv = GVATT_VERTELEM(lowindex);
	const float* dt0 = GVATT_TEX0ELEM(lowindex);
	for (int i = lowindex; i <= highindex; i++)
	{
		vatt_vertcoltex_t *p = (vatt_vertcoltex_t *)pVert;
		copy_three(p->XYZ, dv);
		p->COLOR = qdx.crt_color;
		copy_two(p->UV, dt0);
		GVATT_INCVERTP(dv);
		GVATT_INCTEX0P(dt0);
		pVert += sizeof(p[0]);

		vpos++;
	}

	return vpos;
}

static int qdx_draw_process_vertnormtex(int lowindex, int highindex, byte *pVert)
{
	int vpos = 0;
	const float* dv = GVATT_VERTELEM(lowindex);
	const float* dn = GVATT_NORMELEM(lowindex);
	const float* dt0 = GVATT_TEX0ELEM(lowindex);
	for (int i = lowindex; i <= highindex; i++)
	{
		vatt_vertnormcoltex_t* p = (vatt_vertnormcoltex_t*)pVert;
		copy_three(p->XYZ, dv);
		copy_three(p->NORM, dn);
		p->COLOR = qdx.crt_color;
		copy_two(p->UV, dt0);
		GVATT_INCVERTP(dv);
		GVATT_INCNORMP(dn);
		GVATT_INCTEX0P(dt0);
		pVert += sizeof(p[0]);

		vpos++;
	}

	return vpos;
}

static int qdx_draw_process_vertcoltex2(int lowindex, int highindex, byte *pVert)
{
	int vpos = 0;
	const float* dv = GVATT_VERTELEM(lowindex);
	const byte* dc = GVATT_COLRELEM(lowindex);
	const float* dt0 = GVATT_TEX0ELEM(lowindex);
	const float* dt1 = GVATT_TEX1ELEM(lowindex);
	for (int i = lowindex; i <= highindex; i++)
	{
		vatt_vertcoltex2_t *p = (vatt_vertcoltex2_t *)pVert;
		copy_three(p->XYZ, dv);
		p->COLOR = abgr_to_argb(dc);
		copy_two(p->UV0, dt0);
		copy_two(p->UV1, dt1);
		GVATT_INCVERTP(dv);
		GVATT_INCCOLRP(dc);
		GVATT_INCTEX0P(dt0);
		GVATT_INCTEX1P(dt1);
		pVert += sizeof(p[0]);

		vpos++;
	}

	return vpos;
}

static int qdx_draw_process_vertnormcoltex2(int lowindex, int highindex, byte *pVert)
{
	int vpos = 0;
	const float* dv = GVATT_VERTELEM(lowindex);
	const float* dn = GVATT_NORMELEM(lowindex);
	const byte* dc = GVATT_COLRELEM(lowindex);
	const float* dt0 = GVATT_TEX0ELEM(lowindex);
	const float* dt1 = GVATT_TEX1ELEM(lowindex);
	for (int i = lowindex; i <= highindex; i++)
	{
		vatt_vertnormcoltex2_t* p = (vatt_vertnormcoltex2_t*)pVert;
		copy_three(p->XYZ, dv);
		copy_three(p->NORM, dn);
		p->COLOR = abgr_to_argb(dc);
		copy_two(p->UV0, dt0);
		copy_two(p->UV1, dt1);
		GVATT_INCVERTP(dv);
		GVATT_INCNORMP(dn);
		GVATT_INCCOLRP(dc);
		GVATT_INCTEX0P(dt0);
		GVATT_INCTEX1P(dt1);
		pVert += sizeof(p[0]);

		vpos++;
	}

	return vpos;
}

void qdx_texture_apply()
{
	if (g_vattribs.active_vatts & VATT_TEX0)
	{
		qdx_texobj_apply(g_vattribs.textureids[0], 0);
	}
	else qdx_texobj_apply(TEXID_NULL, 0);
	if (g_vattribs.active_vatts & VATT_TEX1)
	{
		qdx_texobj_apply(g_vattribs.textureids[1], 1);
	}
	else qdx_texobj_apply(TEXID_NULL, 1);
}

#include <stack>

std::stack<D3DXMATRIX> g_matview_stack;
std::stack<D3DXMATRIX> g_matproj_stack;
std::stack<D3DXMATRIX> g_matworld_stack;

void qdx_matrix_set(D3DTRANSFORMSTATETYPE type, const float *matrix)
{
	switch (type)
	{
	case D3DTS_VIEW:
		qdx_mats.view = D3DXMATRIX(matrix);
		break;
	case D3DTS_PROJECTION:
		qdx_mats.proj = D3DXMATRIX(matrix);
		break;
	case D3DTS_WORLD:
		qdx_mats.world = D3DXMATRIX(matrix);
		break;
	}
}

void qdx_matrix_mul(D3DTRANSFORMSTATETYPE type, const D3DMATRIX *matrix)
{
	switch (type)
	{
	case D3DTS_VIEW:
		qdx_mats.view = D3DXMATRIX(*matrix) * qdx_mats.view;
		break;
	case D3DTS_PROJECTION:
		qdx_mats.proj = D3DXMATRIX(*matrix) * qdx_mats.proj;
		break;
	case D3DTS_WORLD:
		qdx_mats.world = D3DXMATRIX(*matrix) * qdx_mats.world;
		break;
	}
}

void qdx_matrix_push(D3DTRANSFORMSTATETYPE type)
{
	switch (type)
	{
	case D3DTS_VIEW:
		g_matview_stack.push(qdx_mats.view);
		break;
	case D3DTS_PROJECTION:
		g_matproj_stack.push(qdx_mats.proj);
		break;
	case D3DTS_WORLD:
		g_matworld_stack.push(qdx_mats.world);
		break;
	}
}

void qdx_matrix_pop(D3DTRANSFORMSTATETYPE type)
{
	switch (type)
	{
	case D3DTS_VIEW:
		qdx_mats.view = g_matview_stack.top();
		g_matview_stack.pop();
		break;
	case D3DTS_PROJECTION:
		qdx_mats.proj = g_matproj_stack.top();
		g_matproj_stack.pop();
		break;
	case D3DTS_WORLD:
		qdx_mats.world = g_matworld_stack.top();
		g_matworld_stack.pop();
		break;
	}
}

void qdx_matrix_apply(void)
{
	qdx.device->SetTransform(D3DTS_WORLD, &qdx_mats.world);
	qdx.device->SetTransform(D3DTS_VIEW, &qdx_mats.view);
	qdx.device->SetTransform(D3DTS_PROJECTION, &qdx_mats.proj);

	if (r_logFile->integer && (r_logFileTypes->integer & RLOGFILE_MATRIX))
	{
		qdx_log_matrix("world", (float*)qdx_mats.world.m);
		qdx_log_matrix("view", (float*)qdx_mats.view.m);
		qdx_log_matrix("proj", (float*)qdx_mats.proj.m);
		qdx_log_matrix("all", (float*)(&(qdx_mats.world * qdx_mats.view * qdx_mats.proj))->m);
	}
}

int qdx_matrix_equals(const float *a, const float *b)
{
	int diff0 = 0;

	for (int i = 0; i < 16; i++)
	{
		if (fabsf(a[i] - b[i]) > 0.001f)
		{
			diff0++;
		}
	}

	return diff0 == 0;
}

void qdx_depthrange(float znear, float zfar)
{
	D3DVIEWPORT9 vp = qdx.viewport;
	vp.MinZ = znear;
	vp.MaxZ = zfar;

	qdx.device->SetViewport(&vp);
}

static std::map<std::string, int> asserted_fns;
#define ASSERT_MAX_PRINTS 5

void qdx_assert_failed_str(const char* expression, const char* function, unsigned line, const char* file)
{
	const char* fn = strrchr(file, '\\');
	if (!fn) fn = strrchr(file, '/');
	if (!fn) fn = "fnf";

	bool will_print = false;
	bool supressed_msg = false;

	std::string mykey = std::string(function);
	auto searched = asserted_fns.find(mykey);
	if (asserted_fns.end() != searched)
	{
		int nums = searched->second;
		if (nums <= ASSERT_MAX_PRINTS)
		{
			will_print = true;
			supressed_msg = (nums == ASSERT_MAX_PRINTS);
		}
		searched->second = nums + 1;
	}
	else
	{
		asserted_fns[mykey] = 1;
		will_print = true;
	}

	if (will_print)
	{
#ifdef NDEBUG
		if(supressed_msg)
			ri.Printf(PRINT_ERROR, "assert failed and supressing: %s in %s:%d %s\n", expression, function, line, fn);
		else
			ri.Printf(PRINT_ERROR, "assert failed: %s in %s:%d %s\n", expression, function, line, fn);
#else
		ri.Error(ERR_FATAL, "assert failed: %s in %s:%d %s\n", expression, function, line, fn);
#endif
	}
}

#define MINI_CASE_SENSITIVE
#include "ini.h"

static mINI::INIFile g_inifile("wolf_customise.ini");
static mINI::INIStructure g_iniconf;
static BOOL g_ini_first_init = FALSE;
static std::string active_map( "" );

static void iniconf_first_init()
{
	if ( g_ini_first_init == FALSE )
	{
		g_ini_first_init = TRUE;
		g_inifile.read( g_iniconf );
		qdx_lights_load( g_iniconf, "");
	}
}

const char* qdx_get_active_map()
{
	return active_map.c_str();
}

mINI::INIStructure& qdx_get_iniconf()
{
	return g_iniconf;
}

void qdx_save_iniconf()
{
	g_inifile.write( g_iniconf, true );
}

#define INICONF_GLOBAL "global"
#define INICONF_SETTINGS "Settings"

int qdx_readsetting( const char* valname, int default )
{
	if (g_iniconf.has(INICONF_SETTINGS) && g_iniconf[INICONF_SETTINGS].has(valname))
	{
		return strtoul( g_iniconf[INICONF_SETTINGS][valname].c_str(), NULL, 10 );
	}

	return default;
}

intptr_t qdx_readmapconf_ex(const char* base, const char* valname, int radix)
{
	intptr_t ret = 0;
	int tries = 0;
	std::string section( base );
	section.append( "." );
	if ( active_map.length() )
	{
		section.append( active_map.c_str() );
	}
	else
	{
		section.append( INICONF_GLOBAL );
		tries = 1;
	}

	for(; tries < 2; tries++)
	{
		if (g_iniconf.has(section) && g_iniconf[section].has(valname))
		{
			ret = strtoul( g_iniconf[section][valname].c_str(), NULL, radix );
			break;
		}
		else
		{
			section.assign( base );
			section.append( "." );
			section.append( INICONF_GLOBAL );
		}
	}
	return ret;
}

int qdx_readmapconf( const char* base, const char* valname )
{
	return (int) qdx_readmapconf_ex( base, valname, 10 );
}

void* qdx_readmapconfptr( const char* base, const char* valname)
{
	return (void*) qdx_readmapconf_ex( base, valname, 16 );
}

float qdx_readmapconfflt(const char* base, const char* valname, float default)
{
	float ret = default;
	int tries = 0;
	std::string section( base );
	section.append( "." );
	if ( active_map.length() )
	{
		section.append( active_map.c_str() );
	}
	else
	{
		section.append( INICONF_GLOBAL );
		tries = 1;
	}

	for(; tries < 2; tries++)
	{
		if (g_iniconf.has(section) && g_iniconf[section].has(valname))
		{
			ret = strtof( g_iniconf[section][valname].c_str(), NULL );
			break;
		}
		else
		{
			section.assign( base );
			section.append( "." );
			section.append( INICONF_GLOBAL );
		}
	}
	return ret;
}

void qdx_storemapconfflt( const char* base, const char* valname, float value, bool inGlobal )
{
	if ( inGlobal == false && active_map.length() == 0 )
	{
		return;
	}
	char data[32];
	snprintf( data, sizeof( data ), "%.4f", value );

	std::string section( base );
	section.append( "." );
	if ( inGlobal )
	{
		section.append( INICONF_GLOBAL );
	}
	else
	{
		section.append( active_map.c_str() );
	}
	g_iniconf[section][valname] = data;
}

int qdx_readmapconfstr( const char* base, const char* valname, char *out, int outsz )
{
	int tries = 0;
	std::string section( base );
	section.append( "." );
	if ( active_map.length() )
	{
		section.append( active_map.c_str() );
	}
	else
	{
		section.append( INICONF_GLOBAL );
		tries = 1;
	}

	for(; tries < 2; tries++)
	{
		if (g_iniconf.has(section) && g_iniconf[section].has(valname))
		{
			errno_t ercd = strncpy_s(out, outsz, g_iniconf[section][valname].c_str(), _TRUNCATE);
			if ( ercd == STRUNCATE )
			{
				ri.Printf( PRINT_ALL, "readmapconfstr: string truncation for %s, expected max %d bytes.\n", valname, outsz );
				return 0;
			}
			if ( ercd == 0 )
			{
				return 1;
			}
		}
		else
		{
			section.assign( base );
			section.append( "." );
			section.append( INICONF_GLOBAL );
		}
	}
	return 0;
}

void qdx_begin_loading_map(const char* mapname)
{
	static char section[256];

	const char *name = strrchr(mapname, '/');
	if (name == NULL) return;
	name++;

	const char* ext = strrchr(mapname, '.');
	int namelen = 0;
	if(ext)
	{
		namelen = ext - name;
	}
	else
	{
		namelen = strlen(name);
	}

	active_map.assign( name, namelen );

	if (g_inifile.read(g_iniconf))
	{
		if ( remixOnline )
		{
			mINI::INIMap<std::string>* opts;

			// Apply RTX.conf options
			snprintf( section, sizeof( section ), "rtxconf.%.*s", namelen, name );
			if ( g_iniconf.has( section ) )
			{
				opts = &g_iniconf[ section ];
			}
			else
			{
				opts = &g_iniconf[ "rtxconf.default" ];
			}

			for ( auto it = opts->begin(); it != opts->end(); it++ )
			{
				const char* key = it->first.c_str();
				const char* value = it->second.c_str();
				remixapi_ErrorCode rercd = remixInterface.SetConfigVariable( key, value );
				if ( REMIXAPI_ERROR_CODE_SUCCESS != rercd )
				{
					ri.Printf( PRINT_ERROR, "RMX failed to set config var %d\n", rercd );
				}
			}
		}

		// Load Light settings
		qdx_lights_load( g_iniconf, active_map.c_str() );
	}
}

static std::map<UINT32, std::vector<const void*>> g_surfaces;

void qdx_surface_clear()
{
	g_surfaces.clear();
}

void qdx_surface_add( const void* surf, surfpartition_t id )
{
	auto grp = g_surfaces.find( id.combined );
	if ( grp != g_surfaces.end() )
	{
		grp->second.push_back( surf );
	}
	else
	{
		g_surfaces[id.combined].push_back( surf );
	}
}

void qdx_surface_get_members( surfpartition_t id, const void** surfs, int* count )
{
	auto grp = g_surfaces.find( id.combined );
	if ( grp != g_surfaces.end() )
	{
		*surfs = *grp->second.data();
		*count = grp->second.size();
	}
	else
	{
		*surfs = 0;
		*count = 0;
	}
}

#define QDX_SURFACE_GRID_VAL 500.0f

surfpartition_t qdx_surface_get_partition( const void* data )
{
	surfpartition_t sid = { 0 };
	float* vert;
	srfSurfaceFace_t* face;
	srfGridMesh_t* grid;
	srfTriangles_t* tris;
	switch ( *(surfaceType_t*)data )
	{
	case SF_FACE:
		face = (srfSurfaceFace_t*)data;
		vert = face->points[0];
		break;
	case SF_GRID:
		grid = (srfGridMesh_t*)data;
		vert = grid->verts[0].xyz;
		break;
	case SF_TRIANGLES:
		tris = (srfTriangles_t*)data;
		vert = tris->verts->xyz;
		break;
	default:
		vert = 0;
	}
	if ( vert )
	{
		sid.p.x = (INT32)(vert[0] / QDX_SURFACE_GRID_VAL);
		sid.p.y = (INT32)(vert[1] / QDX_SURFACE_GRID_VAL);
		sid.p.z = (INT32)(vert[2] / QDX_SURFACE_GRID_VAL);
	}
	return sid;
}

void qdx_screen_getxyz( float *xyz )
{
	xyz[0] = 0.0f; xyz[1] = 0.0f; xyz[2] = 0.0f;

	IDirect3DSurface9* ssrc;
	if ( qdx.device )
	{
		HRESULT hr = 0;

		hr = qdx.device->GetDepthStencilSurface( &ssrc );

		D3DSURFACE_DESC sdesc;
		hr = ssrc->GetDesc( &sdesc );


		IDirect3DSurface9* sdest = 0;
		hr = qdx.device->CreateOffscreenPlainSurface(
			sdesc.Width,
			sdesc.Height,
			sdesc.Format,
			D3DPOOL_SCRATCH, //D3DPOOL_DEFAULT
			&sdest,
			NULL );
		if ( FAILED( hr ) || !sdest )
		{
			ri.Printf( PRINT_WARNING, "Unable to create surf: %d\n", HRESULT_CODE( hr ) );
			return;
		}

		hr = qdx.device->GetRenderTargetData( ssrc, sdest );
		if ( FAILED( hr ) )
		{
			ri.Printf( PRINT_WARNING, "Unable to copy surf: %d\n", HRESULT_CODE(hr) );
			return;
		}

		D3DLOCKED_RECT lr;
		RECT re;
		re.left = sdesc.Width / 2 -1;
		re.right = sdesc.Width / 2 + 1;
		re.top = sdesc.Height / 2 - 1;
		re.bottom = sdesc.Height / 2 + 1;
		hr = sdest->LockRect( &lr, &re, 0 );
		if ( FAILED( hr ) )
		{
			ri.Printf( PRINT_WARNING, "Unable to lock surf: %d\n", HRESULT_CODE(hr) );
			return;
		}

		int center = sdesc.Height / 2 * lr.Pitch + sdesc.Width * 4 / 2;
		for ( int i = center - 10; i < center +10; i++ )
		{
			//ri.Printf( PRINT_ALL, " %x\n", *(UINT32*)((BYTE*)lr.pBits + i) );
		}

		sdest->UnlockRect();

		ssrc->Release();
		sdest->Release();
	}
}

/**
* This will load dxgi and dx11 and dx12 headers and contaminate the namespace with all that new directx info;
*  implement the bare minimum here, or move it to another cpp file.
*/
#include "../renderer/DirectXTex/DirectXTex.h"

namespace DirectX {
	HRESULT CompressBC(
		const Image& image,
		const Image& result,
		uint32_t bcflags,
		TEX_FILTER_FLAGS srgb,
		float threshold,
		const std::function<bool __cdecl(size_t, size_t)>& statusCallback) noexcept;
}

HRESULT qdx_compress_texture(int width, int height, const void *indata, void *outdata, int inbits, int outpitch)
{
	DirectX::Image in, out;
	in.width = out.width = width;
	in.height = out.height = height;
	in.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	out.format = DXGI_FORMAT_BC3_UNORM;
	in.rowPitch = 4 * width;
	in.slicePitch = 4 * width * height;
	out.rowPitch = outpitch;
	out.slicePitch = (width * height * inbits) / 8;
	in.pixels = (uint8_t*)indata;
	out.pixels = (uint8_t*)outdata;
	return DirectX::CompressBC(in, out, 0, DirectX::TEX_FILTER_DEFAULT, 0, nullptr);
}
