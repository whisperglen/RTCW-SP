
#include <d3d9.h>
#include <Windows.h>
#include <stdint.h>
#include <cassert>

#define SHADER_MAX_VERTEXES 10
#define SHADER_MAX_INDEXES (6*SHADER_MAX_VERTEXES)

typedef uint32_t qdxIndex_t;
#define QDX_INDEX_TYPE D3DFMT_INDEX32

struct qdx9_state
{
	LPDIRECT3D9 d3d;
	LPDIRECT3DDEVICE9 device;
};

static struct qdx9_state qdx;

struct fvf_buffers
{
	DWORD owner;
	LPDIRECT3DINDEXBUFFER9 i_buf;
	struct fvf_vert_buffer
	{
		LPDIRECT3DVERTEXBUFFER9 data;
		UINT fvf;
	} v_buffs[2];
} g_fvf_buffers[2];

static int g_used_fvf_buffers = 0;

static int get_buffers(LPDIRECT3DINDEXBUFFER9 *index_buf, LPDIRECT3DVERTEXBUFFER9 *vertex_buf, UINT fvf_spec, UINT fvf_stride)
{
	int ret = 0;
	DWORD whoami = GetCurrentThreadId();
	int i = 0, j = 0;
	for (; i < g_used_fvf_buffers; i++)
	{
		if (g_fvf_buffers[i].owner == whoami)
		{
			if (index_buf)
				*index_buf = g_fvf_buffers[i].i_buf;

			for (j = 0; j < ARRAYSIZE(g_fvf_buffers[i].v_buffs); j++)
			{
				if (g_fvf_buffers[i].v_buffs[j].fvf == fvf_spec)
				{
					if (vertex_buf)
						*vertex_buf = g_fvf_buffers[i].v_buffs[j].data;

					return 0;
				}
				else if (g_fvf_buffers[i].v_buffs[j].fvf == 0)
				{
					LPDIRECT3DVERTEXBUFFER9 b;
					if (FAILED(qdx.device->CreateVertexBuffer(SHADER_MAX_VERTEXES * fvf_stride, 0, fvf_spec, D3DPOOL_MANAGED, &b, NULL)))
					{
						return -1;
					}
					g_fvf_buffers[i].v_buffs[j].fvf = fvf_spec;
					g_fvf_buffers[i].v_buffs[j].data = b;

					if (vertex_buf)
						*vertex_buf = b;

					return 0;
				}
			}

			return -2;
		}
	}

	if (g_used_fvf_buffers + 1 < ARRAYSIZE(g_fvf_buffers))
	{
		LPDIRECT3DINDEXBUFFER9 ib;
		if (FAILED(qdx.device->CreateIndexBuffer(sizeof(qdxIndex_t) * SHADER_MAX_INDEXES, 0, QDX_INDEX_TYPE, D3DPOOL_MANAGED, &ib, NULL)))
		{
			return -1;
		}
		LPDIRECT3DVERTEXBUFFER9 vb;
		if (FAILED(qdx.device->CreateVertexBuffer(SHADER_MAX_VERTEXES * fvf_stride, 0, fvf_spec, D3DPOOL_MANAGED, &vb, NULL)))
		{
			ib->Release();
			return -1;
		}

		struct fvf_buffers *p = &g_fvf_buffers[g_used_fvf_buffers];
		g_used_fvf_buffers++;
		p->owner = whoami;
		p->i_buf = ib;
		memset(p->v_buffs, 0, sizeof(p->v_buffs));
		p->v_buffs[0].fvf = fvf_spec;
		p->v_buffs[0].data = vb;

		if (index_buf)
			*index_buf = ib;
		if (vertex_buf)
			*vertex_buf = vb;

		return 0;
	}

	return -2;
}

extern "C" void test_buffers()
{
	qdx.d3d = Direct3DCreate9(D3D_SDK_VERSION);

	HWND hwindow = CreateWindow(TEXT("STATIC"),
		TEXT("HelloTest"),
		WS_OVERLAPPEDWINDOW,    // window style
		0,    // x-position of the window
		0,    // y-position of the window
		CW_USEDEFAULT,    // width of the window
		CW_USEDEFAULT,    // height of the window
		NULL,    // parent window
		NULL,    // menu
		(HINSTANCE)GetModuleHandle(NULL),    // application handle
		NULL);    // multiple windows

	D3DPRESENT_PARAMETERS d3dpp;

	ZeroMemory(&d3dpp, sizeof(d3dpp));    // clear out the struct for use
	d3dpp.Windowed = TRUE;    // program windowed, not fullscreen
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;    // discard old frames
	d3dpp.hDeviceWindow = hwindow;    // set the window to be used by Direct3D
	d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;    // set the back buffer format to 32-bit
	d3dpp.BackBufferWidth = 320;    // set the width of the buffer
	d3dpp.BackBufferHeight = 240;    // set the height of the buffer
	d3dpp.BackBufferCount = 1;
	d3dpp.EnableAutoDepthStencil = TRUE;
	d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
	d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

	HRESULT hr = qdx.d3d->CreateDevice(D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL,
		hwindow,
		D3DCREATE_HARDWARE_VERTEXPROCESSING,
		&d3dpp,
		&qdx.device);

	////test code here

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

	typedef struct fvf_2dvertcoltex
	{
		FLOAT XYZRHV[4];    // from the D3DFVF_XYZRHW flag
		DWORD COLOR;    // from the D3DFVF_DIFFUSE flag
		FLOAT UV[2];
	} fvf_2dvertcoltex_t;
#define FVFID_2DVERTCOLTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0))

	int ercd[5], test[5] = { 0 };

	LPDIRECT3DINDEXBUFFER9 index_buf[5];
	LPDIRECT3DVERTEXBUFFER9 vertex_buf[5];

	ercd[0] = get_buffers(&index_buf[0], &vertex_buf[0], FVFID_VERTCOL, sizeof(fvf_vertcol_t));
	ercd[1] = get_buffers(&index_buf[1], &vertex_buf[1], FVFID_VERTCOLTEX, sizeof(fvf_vertcoltex));


	ercd[2] = get_buffers(&index_buf[2], &vertex_buf[2], FVFID_VERTCOL, sizeof(fvf_vertcol_t));
	ercd[3] = get_buffers(&index_buf[3], &vertex_buf[3], FVFID_VERTCOLTEX, sizeof(fvf_vertcoltex));

	assert((index_buf[0] == index_buf[2]) && (vertex_buf[0] == vertex_buf[2]));
	assert((index_buf[1] == index_buf[3]) && (vertex_buf[1] == vertex_buf[3]));

	assert(0 == memcmp(ercd, test, sizeof(int) * 4));

	ercd[4] = get_buffers(&index_buf[4], &vertex_buf[5], FVFID_2DVERTCOLTEX, sizeof(fvf_2dvertcoltex));
	assert(ercd[4] != 0);

	////end test code

	qdx.device->Release();
	DestroyWindow(hwindow);
}