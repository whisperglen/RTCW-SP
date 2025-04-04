/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of the Return to Castle Wolfenstein single player GPL Source Code ("RTCW SP Source Code").

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW SP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW SP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

/*
** modified from WIN_GLIMP.C
**
** This file contains ALL Win32 specific stuff having to do with the
** OpenGL refresh.  When a port is being made the following functions
** must be implemented by the port:
**
** GLimp_EndFrame
** GLimp_Init
** GLimp_LogComment
** GLimp_Shutdown
**
** Note that the GLW_xxx functions are Windows specific GL-subsystem
** related functions that are relevant ONLY to win_glimp.c
*/
#include "../renderer/qdx9.h"
extern "C"
{
#include "../renderer/tr_local.h"
#include "../qcommon/qcommon.h"
#include "resource.h"
#include "win_local.h"
}
#include <stdint.h>
#include "fnv.h"

#include "remix/bridge_remix_api.h"

#include <string>

typedef struct
{
	WNDPROC wndproc;

	HDC hDC;                // handle to device context
	//HGLRC hGLRC;            // handle to GL rendering context

	//HINSTANCE hinstOpenGL;  // HINSTANCE for the OpenGL library
	HMODULE instDX9;
	void* funcDX9Create;

	qboolean allowdisplaydepthchange;
	qboolean pixelFormatSet;

	int desktopBitsPixel;
	int desktopWidth, desktopHeight;

	qboolean cdsFullscreen;

	//FILE *log_fp;
} dx9state_t;

static void DX9imp_CheckHardwareGamma(void);
static void DX9imp_RestoreGamma(void);
static void qdx_log_comment(const char *name, UINT vattbits, const void *ptr);
static void qdx_log_matrix(const char *name, const float *mat);
static void qdx_log_dump_buffer(const char* name, const uint32_t* buffer, uint32_t size, int wide, uint32_t hashid, uint32_t hashbuf);
static void qdx_before_frame_end();
static void qdx_frame_ended();
static void qdx_clear_buffers();
static void qdx_texobj_delete_all();
static void qdx_lights_draw();

typedef enum {
	RSERR_OK,

	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,

	RSERR_UNKNOWN
} rserr_t;

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

#define TRY_PFD_SUCCESS     0
#define TRY_PFD_FAIL_SOFT   1
#define TRY_PFD_FAIL_HARD   2

#define WINDOW_CLASS_NAME   "Wolfenstein"

static void     GLW_InitExtensions(void);
static rserr_t  GLW_SetMode(const char *drivername,
	int mode,
	int colorbits,
	qboolean cdsFullscreen);
static qboolean DXGetModeInfo( int* width, int* height, float* windowAspect, int mode );

static qboolean s_classRegistered = qfalse;

//
// function declaration
//
extern "C" void     QGL_DMY_EnableLogging(qboolean enable);
extern "C" qboolean QGL_DMY_Init(const char *dllname);
extern "C" void     QGL_DMY_Shutdown(void);

//
// variable declarations
//
static dx9state_t dx9imp_state = { 0 };
struct qdx9_state qdx;
static remixapi_Interface remixInterface = { 0 };
static bool remixOnline = false;

cvar_t  *r_allowSoftwareGL;     // don't abort out if the pixelformat claims software
cvar_t  *r_maskMinidriver;      // allow a different dll name to be treated as if it were opengl32.dll
cvar_t  *r_systemdll;         // search for directx9 dll only in system folder



/*
** GLW_StartDriverAndSetMode
*/
static qboolean GLW_StartDriverAndSetMode(const char *drivername,
	int mode,
	int colorbits,
	qboolean cdsFullscreen) {
	rserr_t err;

	const int minwidth = 640;
	const int minheight = 480;

	if (dx9imp_state.instDX9 == NULL)
	{
		dx9imp_state.instDX9 = LoadLibraryEx("d3d9.dll", 0, r_systemdll->integer ? LOAD_LIBRARY_SEARCH_SYSTEM32 : 0);
		if (dx9imp_state.instDX9 == NULL)
		{
			ri.Printf(PRINT_ALL, "...Error: cannot load d3d9.dll, system cvar:%d\n", r_systemdll->integer);
			return qfalse;
		}
	}
	if (dx9imp_state.funcDX9Create == NULL)
	{
		dx9imp_state.funcDX9Create = GetProcAddress(dx9imp_state.instDX9, "Direct3DCreate9");
		if (dx9imp_state.funcDX9Create == NULL)
		{
			ri.Printf(PRINT_ALL, "...Error: cannot find Direct3DCreate9\n");
			return qfalse;
		}

		PFN_remixapi_InitializeLibrary remix_init = (PFN_remixapi_InitializeLibrary)GetProcAddress(dx9imp_state.instDX9, "remixapi_InitializeLibrary");
		if (remix_init == NULL)
		{
			ri.Printf(PRINT_WARNING, "Remix API not found\n");
		}
		else
		{
			remixapi_InitializeLibraryInfo info;
			ZeroMemory(&info, sizeof(info));
			{
				info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
				info.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR,
					REMIXAPI_VERSION_MINOR,
					REMIXAPI_VERSION_PATCH);
			}

			remixapi_ErrorCode status = remix_init(&info, &remixInterface);
			if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
				ri.Printf(PRINT_WARNING, "Remix API init error %d, did you set 'exposeRemixApi = True' in .trex\\bridge.conf ? \n", status);
			}
			else
			{
				remixOnline = true;
			}
		}
	}

	ZeroMemory(&qdx, sizeof(qdx));
	qdx.d3d = ((IDirect3D9 * (WINAPI *)(UINT))dx9imp_state.funcDX9Create)(D3D_SDK_VERSION);
	qdx.adapter_count = qdx.d3d->GetAdapterCount();
	qdx.adapter_num = D3DADAPTER_DEFAULT;
	qdx.d3d->GetAdapterIdentifier(qdx.adapter_num, 0, &qdx.adapter_info);
	qdx.d3d->GetDeviceCaps(qdx.adapter_num, D3DDEVTYPE_HAL, &qdx.caps);
	qdx.d3d->GetAdapterDisplayMode(qdx.adapter_num, &qdx.desktop);

	int nummodes = qdx.d3d->GetAdapterModeCount(qdx.adapter_num, qdx.desktop.Format);
	qdx.modes = (D3DDISPLAYMODE*)malloc(sizeof(D3DDISPLAYMODE) * (1 + nummodes)); //todo: use engine alloc functions
	if (qdx.modes == NULL)
	{
		ri.Printf(PRINT_ALL, "...Error: cannot alloc memory to enumerate videomodes\n");
		return qfalse;
	}
	ZeroMemory(qdx.modes, (sizeof(D3DDISPLAYMODE) * (1 + nummodes)));
	D3DDISPLAYMODE *m = qdx.modes;
	for (int amode = 0; amode < nummodes; amode++)
	{
		D3DDISPLAYMODE dispMode;
		qdx.d3d->EnumAdapterModes(qdx.adapter_num, qdx.desktop.Format, amode, &dispMode);
		if (dispMode.Width < minwidth || dispMode.Height < minheight || dispMode.RefreshRate != qdx.desktop.RefreshRate)
			continue;
		memcpy(m, &dispMode, sizeof(dispMode));
		qdx.modes_count++;
		m++;
	}
	if( qdx.modes_count )
	{
		char local[64];
		std::string modes_enum;
		modes_enum.assign( "{" );
		m = qdx.modes;
		for ( int i = 0; i < qdx.modes_count; i++, m++ )
		{
			snprintf( local, sizeof( local ), " \"%d*%d%s\" %d", m->Width, m->Height, ((float)m->Width / m->Height > 1.4f ? " (WS)" : ""), i );
			modes_enum.append( local );
		}
		int cwidth = 0, cheight = 0;
		float caspect = 0;
		if ( DXGetModeInfo( &cwidth, &cheight, &caspect, -1 ) && cwidth && cheight && (caspect > 0.0f) )
		{
			snprintf( local, sizeof( local ), " \"%d*%d%s\" %d", cwidth, cheight, " (Custom)", -1 );
			modes_enum.append( local );
		}
		if ( DXGetModeInfo( &cwidth, &cheight, &caspect, -2 ) && cwidth && cheight && (caspect > 0.0f) )
		{
			snprintf( local, sizeof( local ), " \"%d*%d%s\" %d", cwidth, cheight, " (Desktop)", -2 );
			modes_enum.append( local );
		}
		modes_enum.append( " }" );
		ri.Cvar_Set( "r_menu_modes", modes_enum.c_str() );
	}

	err = GLW_SetMode(drivername, mode, colorbits, cdsFullscreen);

	switch (err)
	{
	case RSERR_INVALID_FULLSCREEN:
		ri.Printf(PRINT_ALL, "...WARNING: fullscreen unavailable in this mode\n");
		return qfalse;
	case RSERR_INVALID_MODE:
		ri.Printf(PRINT_ALL, "...WARNING: could not set the given mode (%d)\n", mode);
		return qfalse;
	default:
		break;
	}
	return qtrue;
}

/*
** ChoosePFD
**
** Helper function that replaces ChoosePixelFormat.
*/
#define MAX_PFDS 256

static int GLW_ChoosePFD(HDC hDC, PIXELFORMATDESCRIPTOR *pPFD) {
	PIXELFORMATDESCRIPTOR pfds[MAX_PFDS + 1];
	int maxPFD = 0;
	int i;
	int bestMatch = 0;

	ri.Printf(PRINT_ALL, "...GLW_ChoosePFD( %d, %d, %d )\n", (int)pPFD->cColorBits, (int)pPFD->cDepthBits, (int)pPFD->cStencilBits);

	// count number of PFDs
	if (glConfig.driverType > GLDRV_ICD) {
	}
	else
	{
		maxPFD = DescribePixelFormat(hDC, 1, sizeof(PIXELFORMATDESCRIPTOR), &pfds[0]);
	}
	if (maxPFD > MAX_PFDS) {
		ri.Printf(PRINT_WARNING, "...numPFDs > MAX_PFDS (%d > %d)\n", maxPFD, MAX_PFDS);
		maxPFD = MAX_PFDS;
	}

	ri.Printf(PRINT_ALL, "...%d PFDs found\n", maxPFD - 1);

	// grab information
	for (i = 1; i <= maxPFD; i++)
	{
		if (glConfig.driverType > GLDRV_ICD) {
		}
		else
		{
			DescribePixelFormat(hDC, i, sizeof(PIXELFORMATDESCRIPTOR), &pfds[i]);
		}
	}

	// look for a best match
	for (i = 1; i <= maxPFD; i++)
	{
		//
		// make sure this has hardware acceleration
		//
		if ((pfds[i].dwFlags & PFD_GENERIC_FORMAT) != 0) {
			if (!r_allowSoftwareGL->integer) {
				if (r_verbose->integer) {
					ri.Printf(PRINT_ALL, "...PFD %d rejected, software acceleration\n", i);
				}
				continue;
			}
		}

		// verify pixel type
		if (pfds[i].iPixelType != PFD_TYPE_RGBA) {
			if (r_verbose->integer) {
				ri.Printf(PRINT_ALL, "...PFD %d rejected, not RGBA\n", i);
			}
			continue;
		}

		// verify proper flags
		if (((pfds[i].dwFlags & pPFD->dwFlags) & pPFD->dwFlags) != pPFD->dwFlags) {
			if (r_verbose->integer) {
				ri.Printf(PRINT_ALL, "...PFD %d rejected, improper flags (%x instead of %x)\n", i, pfds[i].dwFlags, pPFD->dwFlags);
			}
			continue;
		}

		// verify enough bits
		if (pfds[i].cDepthBits < 15) {
			continue;
		}
		if ((pfds[i].cStencilBits < 4) && (pPFD->cStencilBits > 0)) {
			continue;
		}

		//
		// selection criteria (in order of priority):
		//
		//  PFD_STEREO
		//  colorBits
		//  depthBits
		//  stencilBits
		//
		if (bestMatch) {
			// check stereo
			if ((pfds[i].dwFlags & PFD_STEREO) && (!(pfds[bestMatch].dwFlags & PFD_STEREO)) && (pPFD->dwFlags & PFD_STEREO)) {
				bestMatch = i;
				continue;
			}

			if (!(pfds[i].dwFlags & PFD_STEREO) && (pfds[bestMatch].dwFlags & PFD_STEREO) && (pPFD->dwFlags & PFD_STEREO)) {
				bestMatch = i;
				continue;
			}

			// check color
			if (pfds[bestMatch].cColorBits != pPFD->cColorBits) {
				// prefer perfect match
				if (pfds[i].cColorBits == pPFD->cColorBits) {
					bestMatch = i;
					continue;
				}
				// otherwise if this PFD has more bits than our best, use it
				else if (pfds[i].cColorBits > pfds[bestMatch].cColorBits) {
					bestMatch = i;
					continue;
				}
			}

			// check depth
			if (pfds[bestMatch].cDepthBits != pPFD->cDepthBits) {
				// prefer perfect match
				if (pfds[i].cDepthBits == pPFD->cDepthBits) {
					bestMatch = i;
					continue;
				}
				// otherwise if this PFD has more bits than our best, use it
				else if (pfds[i].cDepthBits > pfds[bestMatch].cDepthBits) {
					bestMatch = i;
					continue;
				}
			}

			// check stencil
			if (pfds[bestMatch].cStencilBits != pPFD->cStencilBits) {
				// prefer perfect match
				if (pfds[i].cStencilBits == pPFD->cStencilBits) {
					bestMatch = i;
					continue;
				}
				// otherwise if this PFD has more bits than our best, use it
				else if ((pfds[i].cStencilBits > pfds[bestMatch].cStencilBits) &&
					(pPFD->cStencilBits > 0)) {
					bestMatch = i;
					continue;
				}
			}
		}
		else
		{
			bestMatch = i;
		}
	}

	if (!bestMatch) {
		return 0;
	}

	if ((pfds[bestMatch].dwFlags & PFD_GENERIC_FORMAT) != 0) {
		if (!r_allowSoftwareGL->integer) {
			ri.Printf(PRINT_ALL, "...no hardware acceleration found\n");
			return 0;
		}
		else
		{
			ri.Printf(PRINT_ALL, "...using software emulation\n");
		}
	}
	else if (pfds[bestMatch].dwFlags & PFD_GENERIC_ACCELERATED) {
		ri.Printf(PRINT_ALL, "...MCD acceleration found\n");
	}
	else
	{
		ri.Printf(PRINT_ALL, "...hardware acceleration found\n");
	}

	*pPFD = pfds[bestMatch];

	return bestMatch;
}

/*
** void GLW_CreatePFD
**
** Helper function zeros out then fills in a PFD
*/
static void GLW_CreatePFD(PIXELFORMATDESCRIPTOR *pPFD, int colorbits, int depthbits, int stencilbits, qboolean stereo) {
	PIXELFORMATDESCRIPTOR src =
	{
		sizeof(PIXELFORMATDESCRIPTOR),  // size of this pfd
		1,                              // version number
		PFD_DRAW_TO_WINDOW |            // support window
		PFD_SUPPORT_OPENGL |            // support OpenGL
		PFD_DOUBLEBUFFER,               // double buffered
		PFD_TYPE_RGBA,                  // RGBA type
		24,                             // 24-bit color depth
		0, 0, 0, 0, 0, 0,               // color bits ignored
		0,                              // no alpha buffer
		0,                              // shift bit ignored
		0,                              // no accumulation buffer
		0, 0, 0, 0,                     // accum bits ignored
		24,                             // 24-bit z-buffer
		8,                              // 8-bit stencil buffer
		0,                              // no auxiliary buffer
		PFD_MAIN_PLANE,                 // main layer
		0,                              // reserved
		0, 0, 0                         // layer masks ignored
	};

	src.cColorBits = colorbits;
	src.cDepthBits = depthbits;
	src.cStencilBits = stencilbits;

	if (stereo) {
		ri.Printf(PRINT_ALL, "...attempting to use stereo\n");
		src.dwFlags |= PFD_STEREO;
		glConfig.stereoEnabled = qtrue;
	}
	else
	{
		glConfig.stereoEnabled = qfalse;
	}

	*pPFD = src;
}

static int format_get_colorbits(D3DFORMAT fmt)
{
	int bits = 0;

	switch (fmt)
	{
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
		bits = 32;
		break;
	case D3DFMT_R5G6B5:
	case D3DFMT_A1R5G5B5:
	case D3DFMT_X1R5G5B5:
		bits = 16;
		break;
	};

	return bits;
}

static int format_get_depthbits(D3DFORMAT fmt)
{
	int bits = 0;

	switch (fmt)
	{
	case D3DFMT_D32:
	case D3DFMT_D32_LOCKABLE:
	case D3DFMT_D32F_LOCKABLE:
		bits = 32;
		break;
	case D3DFMT_D24S8:
	case D3DFMT_D24X4S4:
	case D3DFMT_D24X8:
		bits = 24;
		break;
	case D3DFMT_D16:
	case D3DFMT_D16_LOCKABLE:
		bits = 16;
		break;
	case D3DFMT_S8_LOCKABLE:
		bits = 8;
	};

	return bits;
}

static int format_get_stencilbits(D3DFORMAT fmt)
{
	int bits = 0;

	switch (fmt)
	{
	case D3DFMT_D24S8:
		bits = 8;
		break;
	case D3DFMT_D24X4S4:
		bits = 4;
		break;
	};

	return bits;
}

static int DX9_ChooseFormat(PIXELFORMATDESCRIPTOR *pPFD, int colorbits, int depthbits, int stencilbits, qboolean stereo)
{
	int ret = TRY_PFD_FAIL_SOFT;

	const D3DFORMAT fmt_bb[] =
	{//todo: only alpha?
		D3DFMT_A8R8G8B8,
		D3DFMT_X8R8G8B8,
		D3DFMT_R5G6B5,
		D3DFMT_A1R5G5B5,
		D3DFMT_X1R5G5B5,
		D3DFMT_UNKNOWN,
	};
	const D3DFORMAT fmt_ds[] =
	{
		//D3DFMT_D32_LOCKABLE,
		//D3DFMT_D32F_LOCKABLE,
		//D3DFMT_D16_LOCKABLE,
		//D3DFMT_S8_LOCKABLE,
		//D3DFMT_D32,
		D3DFMT_D24S8,
		D3DFMT_D24X4S4,
		D3DFMT_D24X8,
		D3DFMT_D16,
		D3DFMT_UNKNOWN,
	};

	int bb_match = ARRAYSIZE(fmt_bb) - 1;
	int ds_match = ARRAYSIZE(fmt_ds) - 1;

	for (int ibb = 0; ibb < ARRAYSIZE(fmt_bb) - 1; ibb++)
	{
		bool bbworks = SUCCEEDED(qdx.d3d->CheckDeviceFormat(qdx.adapter_num, D3DDEVTYPE_HAL, qdx.desktop.Format,
			0, D3DRTYPE_SURFACE, fmt_bb[ibb]));

		if (bbworks && (colorbits == format_get_colorbits(fmt_bb[ibb])))
		{
			for (int ids = 0; ids < ARRAYSIZE(fmt_ds) - 1; ids++)
			{
				bool dsworks = SUCCEEDED(qdx.d3d->CheckDeviceFormat(qdx.adapter_num, D3DDEVTYPE_HAL, qdx.desktop.Format,
					D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, fmt_ds[ids]));

				if (dsworks && (depthbits <= format_get_depthbits(fmt_ds[ids])))
				{
					if ((stencilbits == 0 && format_get_stencilbits(fmt_ds[ids]) == 0) ||
						(stencilbits > 0 && format_get_stencilbits(fmt_ds[ids]) >= stencilbits))
					{
						if (SUCCEEDED(qdx.d3d->CheckDepthStencilMatch(qdx.adapter_num, D3DDEVTYPE_HAL, qdx.desktop.Format,
							fmt_bb[ibb], fmt_ds[ids])))
						{
							if (ids < ds_match) {
								bb_match = ibb;
								ds_match = ids;
							}
							ret = TRY_PFD_SUCCESS;
						}
					}
				}
			}
		}
	}

	if (ret == TRY_PFD_SUCCESS)
	{
		qdx.fmt_backbuffer = fmt_bb[bb_match];
		qdx.fmt_depthstencil = fmt_ds[ds_match];
		pPFD->cColorBits = format_get_colorbits(qdx.fmt_backbuffer);
		pPFD->cDepthBits = format_get_depthbits(qdx.fmt_depthstencil);
		pPFD->cStencilBits = format_get_stencilbits(qdx.fmt_depthstencil);
		pPFD->dwFlags &= ~PFD_STEREO; //todo: we care about stereo?
		dx9imp_state.pixelFormatSet = qtrue;
	}

	return ret;
}

/*
** GLW_MakeContext
*/
static int GLW_MakeContext(PIXELFORMATDESCRIPTOR *pPFD) {
	int pixelformat;

	//
	// don't putz around with pixelformat if it's already set (e.g. this is a soft
	// reset of the graphics system)
	//
	if (!dx9imp_state.pixelFormatSet) {
		//
		// choose, set, and describe our desired pixel format.  If we're
		// using a minidriver then we need to bypass the GDI functions,
		// otherwise use the GDI functions.
		//
		if ((pixelformat = GLW_ChoosePFD(dx9imp_state.hDC, pPFD)) == 0) {
			ri.Printf(PRINT_ALL, "...GLW_ChoosePFD failed\n");
			return TRY_PFD_FAIL_SOFT;
		}
		ri.Printf(PRINT_ALL, "...PIXELFORMAT %d selected\n", pixelformat);

		if (glConfig.driverType > GLDRV_ICD) {
			ri.Printf(PRINT_ALL, "...qwglSetPixelFormat failed\n");
			return TRY_PFD_FAIL_SOFT;
		}
		else
		{
			DescribePixelFormat(dx9imp_state.hDC, pixelformat, sizeof(*pPFD), pPFD);

			if (SetPixelFormat(dx9imp_state.hDC, pixelformat, pPFD) == FALSE) {
				ri.Printf(PRINT_ALL, "...SetPixelFormat failed\n", dx9imp_state.hDC);
				return TRY_PFD_FAIL_SOFT;
			}
		}

		dx9imp_state.pixelFormatSet = qtrue;
	}

	//
	// startup the OpenGL subsystem by creating a context and making it current
	//
	//if (!dx9imp_state.hGLRC) {
	//	ri.Printf(PRINT_ALL, "...creating GL context: ");
	//	if ((dx9imp_state.hGLRC = qwglCreateContext(dx9imp_state.hDC)) == 0) {
	//		ri.Printf(PRINT_ALL, "failed\n");

	//		return TRY_PFD_FAIL_HARD;
	//	}
	//	ri.Printf(PRINT_ALL, "succeeded\n");

	//	ri.Printf(PRINT_ALL, "...making context current: ");
	//	if (!qwglMakeCurrent(dx9imp_state.hDC, dx9imp_state.hGLRC)) {
	//		qwglDeleteContext(dx9imp_state.hGLRC);
	//		dx9imp_state.hGLRC = NULL;
	//		ri.Printf(PRINT_ALL, "failed\n");
	//		return TRY_PFD_FAIL_HARD;
	//	}
	//	ri.Printf(PRINT_ALL, "succeeded\n");
	//}

	return TRY_PFD_SUCCESS;
}


/*
** GLW_InitDriver
**
** - get a DC if one doesn't exist
** - create an HGLRC if one doesn't exist
*/
static qboolean GLW_InitDriver(const char *drivername, int colorbits) {
	int tpfd;
	int depthbits, stencilbits;
	static PIXELFORMATDESCRIPTOR pfd;       // save between frames since 'tr' gets cleared

	ri.Printf(PRINT_ALL, "Initializing DX9 driver\n");

	//
	// get a DC for our window if we don't already have one allocated
	//
	if (dx9imp_state.hDC == NULL) {
		ri.Printf(PRINT_ALL, "...getting DC: ");

		if ((dx9imp_state.hDC = GetDC(g_wv.hWnd)) == NULL) {
			ri.Printf(PRINT_ALL, "failed\n");
			return qfalse;
		}
		ri.Printf(PRINT_ALL, "succeeded\n");
	}

	if (colorbits == 0) {
		colorbits = dx9imp_state.desktopBitsPixel;
	}

	//
	// implicitly assume Z-buffer depth == desktop color depth
	//
	if (r_depthbits->integer == 0) {
		if (colorbits > 16) {
			depthbits = 24;
		}
		else {
			depthbits = 16;
		}
	}
	else {
		depthbits = r_depthbits->integer;
	}

	//
	// do not allow stencil if Z-buffer depth likely won't contain it
	//
	stencilbits = r_stencilbits->integer;
	if (depthbits < 24) {
		stencilbits = 0;
	}

	//
	// make two attempts to set the PIXELFORMAT
	//

	//
	// first attempt: r_colorbits, depthbits, and r_stencilbits
	//
	if (!dx9imp_state.pixelFormatSet) {
		//GLW_CreatePFD(&pfd, colorbits, depthbits, stencilbits, (qboolean)r_stereo->integer);
		if ((tpfd = DX9_ChooseFormat(&pfd, colorbits, depthbits, stencilbits, (qboolean)r_stereo->integer)) != TRY_PFD_SUCCESS) {
			if (tpfd == TRY_PFD_FAIL_HARD) {
				ri.Printf(PRINT_WARNING, "...failed hard\n");
				return qfalse;
			}

			//
			// punt if we've already tried the desktop bit depth and no stencil bits
			//
			if ((r_colorbits->integer == dx9imp_state.desktopBitsPixel) &&
				(stencilbits == 0)) {
				ReleaseDC(g_wv.hWnd, dx9imp_state.hDC);
				dx9imp_state.hDC = NULL;

				ri.Printf(PRINT_ALL, "...failed to find an appropriate PIXELFORMAT\n");

				return qfalse;
			}

			//
			// second attempt: desktop's color bits and no stencil
			//
			if (colorbits > dx9imp_state.desktopBitsPixel) {
				colorbits = dx9imp_state.desktopBitsPixel;
			}
			//GLW_CreatePFD(&pfd, colorbits, depthbits, 0, (qboolean)r_stereo->integer);
			if (DX9_ChooseFormat(&pfd, colorbits, depthbits, stencilbits, (qboolean)r_stereo->integer) != TRY_PFD_SUCCESS) {
				if (dx9imp_state.hDC) {
					ReleaseDC(g_wv.hWnd, dx9imp_state.hDC);
					dx9imp_state.hDC = NULL;
				}

				ri.Printf(PRINT_ALL, "...failed to find an appropriate PIXELFORMAT\n");

				return qfalse;
			}
		}

		/*
		** report if stereo is desired but unavailable
		*/
		if (!(pfd.dwFlags & PFD_STEREO) && (r_stereo->integer != 0)) {
			ri.Printf(PRINT_ALL, "...failed to select stereo pixel format\n");
			glConfig.stereoEnabled = qfalse;
		}
	}

	/*
	** store PFD specifics
	*/
	glConfig.colorBits = (int)pfd.cColorBits;
	glConfig.depthBits = (int)pfd.cDepthBits;
	glConfig.stencilBits = (int)pfd.cStencilBits;

	return qtrue;
}

/*
** GLW_CreateWindow
**
** Responsible for creating the Win32 window and initializing the OpenGL driver.
*/
#define WINDOW_STYLE    ( WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_VISIBLE )
static qboolean GLW_CreateWindow(const char *drivername, int width, int height, int colorbits, qboolean cdsFullscreen) {
	RECT r;
	cvar_t          *vid_xpos, *vid_ypos;
	int stylebits;
	int x, y, w, h;
	int exstyle;

	//
	// register the window class if necessary
	//
	if (!s_classRegistered) {
		WNDCLASS wc;

		memset(&wc, 0, sizeof(wc));

		wc.style = 0;
		wc.lpfnWndProc = (WNDPROC)dx9imp_state.wndproc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = g_wv.hInstance;
		wc.hIcon = LoadIcon(g_wv.hInstance, MAKEINTRESOURCE(IDI_ICON1));
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)COLOR_GRAYTEXT;
		wc.lpszMenuName = 0;
		wc.lpszClassName = WINDOW_CLASS_NAME;

		if (!RegisterClass(&wc)) {
			ri.Error(ERR_FATAL, "GLW_CreateWindow: could not register window class");
		}
		s_classRegistered = qtrue;
		ri.Printf(PRINT_ALL, "...registered window class\n");
	}

	//
	// create the HWND if one does not already exist
	//
	if (!g_wv.hWnd) {
		//
		// compute width and height
		//
		r.left = 0;
		r.top = 0;
		r.right = width;
		r.bottom = height;

		if (cdsFullscreen) {
			exstyle = WS_EX_TOPMOST;
			stylebits = WS_POPUP | WS_VISIBLE | WS_SYSMENU;
		}
		else
		{
			exstyle = 0;
			stylebits = WINDOW_STYLE | WS_SYSMENU;
			AdjustWindowRect(&r, stylebits, FALSE);
		}

		w = r.right - r.left;
		h = r.bottom - r.top;

		if (cdsFullscreen) {
			x = 0;
			y = 0;
		}
		else
		{
			vid_xpos = ri.Cvar_Get("vid_xpos", "", 0);
			vid_ypos = ri.Cvar_Get("vid_ypos", "", 0);
			x = vid_xpos->integer;
			y = vid_ypos->integer;

			// adjust window coordinates if necessary
			// so that the window is completely on screen
			if (x < 0) {
				x = 0;
			}
			if (y < 0) {
				y = 0;
			}

			if (w < dx9imp_state.desktopWidth &&
				h < dx9imp_state.desktopHeight) {
				if (x + w > dx9imp_state.desktopWidth) {
					x = (dx9imp_state.desktopWidth - w);
				}
				if (y + h > dx9imp_state.desktopHeight) {
					y = (dx9imp_state.desktopHeight - h);
				}
			}
		}

		g_wv.hWnd = CreateWindowEx(
			exstyle,
			WINDOW_CLASS_NAME,
			"Wolfenstein",
			stylebits,
			x, y, w, h,
			NULL,
			NULL,
			g_wv.hInstance,
			NULL);

		if (!g_wv.hWnd) {
			ri.Error(ERR_FATAL, "GLW_CreateWindow() - Couldn't create window");
		}

		ShowWindow(g_wv.hWnd, SW_SHOW);
		UpdateWindow(g_wv.hWnd);
		ri.Printf(PRINT_ALL, "...created window@%d,%d (%dx%d)\n", x, y, w, h);
	}
	else
	{
		ri.Printf(PRINT_ALL, "...window already present, CreateWindowEx skipped\n");
	}

	if (!GLW_InitDriver(drivername, colorbits)) {
		ShowWindow(g_wv.hWnd, SW_HIDE);
		DestroyWindow(g_wv.hWnd);
		g_wv.hWnd = NULL;

		return qfalse;
	}

	SetForegroundWindow(g_wv.hWnd);
	SetFocus(g_wv.hWnd);

	return qtrue;
}

static void PrintCDSError(int value) {
	switch (value)
	{
	case DISP_CHANGE_RESTART:
		ri.Printf(PRINT_ALL, "restart required\n");
		break;
	case DISP_CHANGE_BADPARAM:
		ri.Printf(PRINT_ALL, "bad param\n");
		break;
	case DISP_CHANGE_BADFLAGS:
		ri.Printf(PRINT_ALL, "bad flags\n");
		break;
	case DISP_CHANGE_FAILED:
		ri.Printf(PRINT_ALL, "DISP_CHANGE_FAILED\n");
		break;
	case DISP_CHANGE_BADMODE:
		ri.Printf(PRINT_ALL, "bad mode\n");
		break;
	case DISP_CHANGE_NOTUPDATED:
		ri.Printf(PRINT_ALL, "not updated\n");
		break;
	default:
		ri.Printf(PRINT_ALL, "unknown error %d\n", value);
		break;
	}
}

static void fill_in_d3dpresentparams(D3DPRESENT_PARAMETERS &d3dpp)
{
	int samples = r_multisample->value;

	ZeroMemory(&d3dpp, sizeof(d3dpp));
	d3dpp.Windowed = dx9imp_state.cdsFullscreen ? FALSE : TRUE;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;    // discard old frames
	d3dpp.hDeviceWindow = g_wv.hWnd;    // set the window to be used by Direct3D
	d3dpp.BackBufferFormat = qdx.fmt_backbuffer;//D3DFMT_X8R8G8B8;    // set the back buffer format to 32-bit
	d3dpp.BackBufferWidth = glConfig.vidWidth;    // set the width of the buffer
	d3dpp.BackBufferHeight = glConfig.vidHeight;    // set the height of the buffer
	d3dpp.BackBufferCount = 1;
	d3dpp.EnableAutoDepthStencil = qdx.fmt_depthstencil != D3DFMT_UNKNOWN ? TRUE : FALSE;
	d3dpp.AutoDepthStencilFormat = qdx.fmt_depthstencil;// D3DFMT_D16;

	if (dx9imp_state.cdsFullscreen)
	{
		d3dpp.FullScreen_RefreshRateInHz = qdx.desktop.RefreshRate;
	}
	d3dpp.PresentationInterval = r_swapInterval->integer ? D3DPRESENT_INTERVAL_DEFAULT : D3DPRESENT_INTERVAL_IMMEDIATE;

	//check for multisample
	while (samples >= 2)
	{
		if (SUCCEEDED(qdx.d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL, qdx.fmt_backbuffer, dx9imp_state.cdsFullscreen ? FALSE : TRUE,
			(D3DMULTISAMPLE_TYPE)samples, NULL)))
		{
			d3dpp.MultiSampleType = (D3DMULTISAMPLE_TYPE)samples;
			ri.Printf(PRINT_ALL, "...multisample %dx was enabled\n", samples);
			break;
		}
		else
		{
			samples--;
		}
	}

	if (samples < 2 && r_multisample->value)
	{
		ri.Printf(PRINT_ALL, "...multisample not supported\n");
	}
}

static qboolean DXGetModeInfo( int *width, int *height, float *windowAspect, int mode )
{
	if ( mode < -2 )
	{
		return qfalse;
	}
	if ( mode >= qdx.modes_count )
	{
		return qfalse;
	}

	if ( mode == -1 )
	{
		return R_GetModeInfo(width, height, windowAspect, mode);
	}
	else if ( mode == -2 )
	{
		*width = qdx.desktop.Width;
		*height = qdx.desktop.Height;
		*windowAspect = (float)qdx.desktop.Width / qdx.desktop.Height;
		return qtrue;
	}

	D3DDISPLAYMODE* dxmode = qdx.modes + mode;
	*width = dxmode->Width;
	*height = dxmode->Height;
	*windowAspect = (float)dxmode->Width / dxmode->Height;

	return qtrue;
}

/*
** GLW_SetMode
*/
// CDS is not needed, since CreateDevice will go into fullscreen automatically.
//   Window gets destroyed and created on each vid_restart, so we don't need to
//   adjust style either.
#ifdef ChangeDisplaySettings
#undef ChangeDisplaySettings
#endif
ID_INLINE LONG ChangeDisplaySettings(LPVOID, DWORD) { return DISP_CHANGE_SUCCESSFUL; }

static rserr_t GLW_SetMode(const char *drivername,
	int mode,
	int colorbits,
	qboolean cdsFullscreen) {
	HDC hDC;
	const char *win_fs[] = { "W", "FS" };
	int cdsRet;
	DEVMODE dm;

	//
	// print out informational messages
	//
	ri.Printf(PRINT_ALL, "...setting mode %d:", mode);
	if (!DXGetModeInfo(&glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, mode)) {
		ri.Printf(PRINT_ALL, " invalid mode\n");
		return RSERR_INVALID_MODE;
	}
	ri.Printf(PRINT_ALL, " %d %d %s\n", glConfig.vidWidth, glConfig.vidHeight, win_fs[cdsFullscreen]);

	//
	// check our desktop attributes
	//
	hDC = GetDC(GetDesktopWindow());
	dx9imp_state.desktopBitsPixel = GetDeviceCaps(hDC, BITSPIXEL);
	dx9imp_state.desktopWidth = GetDeviceCaps(hDC, HORZRES);
	dx9imp_state.desktopHeight = GetDeviceCaps(hDC, VERTRES);
	ReleaseDC(GetDesktopWindow(), hDC);

	//
	// verify desktop bit depth
	//
	if (dx9imp_state.desktopBitsPixel < 15 || dx9imp_state.desktopBitsPixel == 24) {
		if (colorbits == 0 || (!cdsFullscreen && colorbits >= 15)) {
			if (MessageBox(NULL,
				"It is highly unlikely that a correct\n"
				"windowed display can be initialized with\n"
				"the current desktop display depth.  Select\n"
				"'OK' to try anyway.  Press 'Cancel' if you\n"
				"have a 3Dfx Voodoo, Voodoo-2, or Voodoo Rush\n"
				"3D accelerator installed, or if you otherwise\n"
				"wish to quit.",
				"Low Desktop Color Depth",
				MB_OKCANCEL | MB_ICONEXCLAMATION) != IDOK) {
				return RSERR_INVALID_MODE;
			}
		}
	}

	// do a CDS if needed
	if (cdsFullscreen) {
		memset(&dm, 0, sizeof(dm));

		dm.dmSize = sizeof(dm);

		dm.dmPelsWidth = glConfig.vidWidth;
		dm.dmPelsHeight = glConfig.vidHeight;
		dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

		if (r_displayRefresh->integer != 0) {
			dm.dmDisplayFrequency = r_displayRefresh->integer;
			dm.dmFields |= DM_DISPLAYFREQUENCY;
		}

		// try to change color depth if possible
		if (colorbits != 0) {
			if (dx9imp_state.allowdisplaydepthchange) {
				dm.dmBitsPerPel = colorbits;
				dm.dmFields |= DM_BITSPERPEL;
				ri.Printf(PRINT_ALL, "...using colorsbits of %d\n", colorbits);
			}
			else
			{
				ri.Printf(PRINT_ALL, "WARNING:...changing depth not supported on Win95 < pre-OSR 2.x\n");
			}
		}
		else
		{
			ri.Printf(PRINT_ALL, "...using desktop display depth of %d\n", dx9imp_state.desktopBitsPixel);
		}

		//
		// if we're already in fullscreen then just create the window
		//
		if (dx9imp_state.cdsFullscreen) {
			ri.Printf(PRINT_ALL, "...already fullscreen, avoiding redundant CDS\n");

			if (!GLW_CreateWindow(drivername, glConfig.vidWidth, glConfig.vidHeight, colorbits, qtrue)) {
				ri.Printf(PRINT_ALL, "...restoring display settings\n");
				ChangeDisplaySettings(0, 0);
				return RSERR_INVALID_MODE;
			}
		}
		//
		// need to call CDS
		//
		else
		{
			ri.Printf(PRINT_ALL, "...calling CDS: ");

			// try setting the exact mode requested, because some drivers don't report
			// the low res modes in EnumDisplaySettings, but still work
			if ((cdsRet = ChangeDisplaySettings(&dm, CDS_FULLSCREEN)) == DISP_CHANGE_SUCCESSFUL) {
				ri.Printf(PRINT_ALL, "ok\n");

				if (!GLW_CreateWindow(drivername, glConfig.vidWidth, glConfig.vidHeight, colorbits, qtrue)) {
					ri.Printf(PRINT_ALL, "...restoring display settings\n");
					ChangeDisplaySettings(0, 0);
					return RSERR_INVALID_MODE;
				}

				dx9imp_state.cdsFullscreen = qtrue;
			}
			else
			{
				//
				// the exact mode failed, so scan EnumDisplaySettings for the next largest mode
				//
				DEVMODE devmode;
				int modeNum;

				ri.Printf(PRINT_ALL, "failed, ");

				PrintCDSError(cdsRet);

				ri.Printf(PRINT_ALL, "...trying next higher resolution:");

				// we could do a better matching job here...
				for (modeNum = 0; ; modeNum++) {
					if (!EnumDisplaySettings(NULL, modeNum, &devmode)) {
						modeNum = -1;
						break;
					}
					if (devmode.dmPelsWidth >= glConfig.vidWidth
						&& devmode.dmPelsHeight >= glConfig.vidHeight
						&& devmode.dmBitsPerPel >= 15) {
						break;
					}
				}

				if (modeNum != -1 && (cdsRet = ChangeDisplaySettings(&devmode, CDS_FULLSCREEN)) == DISP_CHANGE_SUCCESSFUL) {
					ri.Printf(PRINT_ALL, " ok\n");
					if (!GLW_CreateWindow(drivername, glConfig.vidWidth, glConfig.vidHeight, colorbits, qtrue)) {
						ri.Printf(PRINT_ALL, "...restoring display settings\n");
						ChangeDisplaySettings(0, 0);
						return RSERR_INVALID_MODE;
					}

					dx9imp_state.cdsFullscreen = qtrue;
				}
				else
				{
					ri.Printf(PRINT_ALL, " failed, ");

					PrintCDSError(cdsRet);

					ri.Printf(PRINT_ALL, "...restoring display settings\n");
					ChangeDisplaySettings(0, 0);

					dx9imp_state.cdsFullscreen = qfalse;
					glConfig.isFullscreen = qfalse;
					if (!GLW_CreateWindow(drivername, glConfig.vidWidth, glConfig.vidHeight, colorbits, qfalse)) {
						return RSERR_INVALID_MODE;
					}
					return RSERR_INVALID_FULLSCREEN;
				}
			}
		}
	}
	else
	{
		if (dx9imp_state.cdsFullscreen) {
			ChangeDisplaySettings(0, 0);
		}

		dx9imp_state.cdsFullscreen = qfalse;
		if (!GLW_CreateWindow(drivername, glConfig.vidWidth, glConfig.vidHeight, colorbits, qfalse)) {
			return RSERR_INVALID_MODE;
		}
	}

	D3DPRESENT_PARAMETERS d3dpp;
	fill_in_d3dpresentparams(d3dpp);

	if (qdx.device == 0)
	{

		HRESULT hr = qdx.d3d->CreateDevice(D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			g_wv.hWnd,
			D3DCREATE_HARDWARE_VERTEXPROCESSING,
			&d3dpp,
			&qdx.device);

		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "GLW_SetMode - Direct3D9 CreateDevice failed %d\n", HRESULT_CODE(hr));
		}
		qdx.devicelost = qfalse;

		//if (remixOnline)
		//{
		//	remixapi_ErrorCode rercd = remixInterface.dxvk_RegisterD3D9Device((IDirect3DDevice9Ex*)qdx.device);
		//	if (rercd != REMIXAPI_ERROR_CODE_SUCCESS)
		//	{
		//		ri.Printf(PRINT_ERROR, "RMX failed to register device %d\n", rercd);
		//	}
		//}
	}
	else
	{
		HRESULT hr = D3D_OK;

		for (int tries = 20; tries > 0; tries--) //2sec..
		{
			// get the current status of the device
			hr = qdx.device->TestCooperativeLevel();

			switch (hr)
			{
			case D3DERR_DEVICENOTRESET:
			case D3D_OK:
				hr = qdx.device->Reset(&d3dpp);
				if (SUCCEEDED(hr))
				{
					//finished
					break;
				}
				//fallthrough
			default:
			case D3DERR_DEVICELOST:
				// device is lost, wait some time
				Sleep(100);
				break;
			}
		}
	}

	//
	// success, now check display frequency, although this won't be valid on Voodoo(2)
	//
	memset(&dm, 0, sizeof(dm));
	dm.dmSize = sizeof(dm);
	if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
		glConfig.displayFrequency = dm.dmDisplayFrequency;
	}

	// NOTE: this is overridden later on standalone 3Dfx drivers
	glConfig.isFullscreen = cdsFullscreen;

	return RSERR_OK;
}

/*
** GLW_InitExtensions
*/
static void GLW_InitExtensions(void) {

	//----(SA)	moved these up
	glConfig.textureCompression = TC_NONE;
	glConfig.textureEnvAddAvailable = qfalse;
	qglMultiTexCoord2fARB = NULL;
	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	qglLockArraysEXT = NULL;
	qglUnlockArraysEXT = NULL;
	qwglGetDeviceGammaRamp3DFX = NULL;
	qwglSetDeviceGammaRamp3DFX = NULL;
	qglPNTrianglesiATI = NULL;
	qglPNTrianglesfATI = NULL;
	glConfig.anisotropicAvailable = qfalse;
	glConfig.NVFogAvailable = qfalse;
	glConfig.NVFogMode = 0;
	//----(SA)	end

	if (!r_allowExtensions->integer) {
		ri.Printf(PRINT_ALL, "*** IGNORING DX9 EXTENSIONS ***\n");
		return;
	}

	ri.Printf(PRINT_ALL, "Initializing DX9 extensions\n");

	// GL_S3_s3tc
	// RF, check for GL_EXT_texture_compression_s3tc
#define TEX_FMT(X) qdx.d3d->CheckDeviceFormat(qdx.adapter_num, D3DDEVTYPE_HAL, qdx.desktop.Format, 0, D3DRTYPE_TEXTURE, (X))
	if (/*SUCCEEDED(TEX_FMT(D3DFMT_DXT1)) && SUCCEEDED(TEX_FMT(D3DFMT_DXT3)) &&*/ SUCCEEDED(TEX_FMT(D3DFMT_DXT5))) {
		if (r_ext_compressed_textures->integer) {
			glConfig.textureCompression = TC_EXT_COMP_S3TC;
			ri.Printf(PRINT_ALL, "...using texture_compression_s3tc\n");
		}
		else
		{
			glConfig.textureCompression = TC_NONE;
			ri.Printf(PRINT_ALL, "...ignoring texture_compression_s3tc\n");
		}
	}
	else
	{
		ri.Printf(PRINT_ALL, "...texture_compression_s3tc not found\n");
	}

	// GL_EXT_texture_env_add
	if (0 != (qdx.caps.TextureOpCaps & D3DTEXOPCAPS_ADD)) {
		if (r_ext_texture_env_add->integer) {
			glConfig.textureEnvAddAvailable = qtrue;
			ri.Printf(PRINT_ALL, "...using texture_env_add\n");
		}
		else
		{
			glConfig.textureEnvAddAvailable = qfalse;
			ri.Printf(PRINT_ALL, "...ignoring texture_env_add\n");
		}
	}
	else
	{
		ri.Printf(PRINT_ALL, "...texture_env_add not found\n");
	}

	// WGL_EXT_swap_control
	//qwglSwapIntervalEXT = (BOOL(WINAPI *)(int))qwglGetProcAddress("wglSwapIntervalEXT");
	//todo: should this not be D3DPRESENT_INTERVAL_ONE for vsync active ?
	if (0 != (qdx.caps.PresentationIntervals & (D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_DEFAULT))) {
		if (r_swapInterval->integer)
		{
			ri.Printf(PRINT_ALL, "...using swap_control\n");
			r_swapInterval->modified = qtrue;   // force a set next frame
		}
		else
		{
			ri.Printf(PRINT_ALL, "...not using swap_control\n");
		}
	}
	else
	{
		ri.Printf(PRINT_ALL, "...swap_control not found\n");
		ri.Cvar_Set("r_swapInterval", "0");
	}

	// GL_ARB_multitexture
	if (qdx.caps.MaxSimultaneousTextures > 1) {
		if (r_ext_multitexture->integer) {
			//qglMultiTexCoord2fARB = (PFNGLMULTITEXCOORD2FARBPROC)qwglGetProcAddress("glMultiTexCoord2fARB");
			//qglActiveTextureARB = (PFNGLACTIVETEXTUREARBPROC)qwglGetProcAddress("glActiveTextureARB");
			//qglClientActiveTextureARB = (PFNGLCLIENTACTIVETEXTUREARBPROC)qwglGetProcAddress("glClientActiveTextureARB");

			glConfig.maxActiveTextures = qdx.caps.MaxSimultaneousTextures;
			if (glConfig.maxActiveTextures > 1) {
				ri.Printf(PRINT_ALL, "...using multitexture\n");
			}
			else
			{
				qglMultiTexCoord2fARB = NULL;
				qglActiveTextureARB = NULL;
				qglClientActiveTextureARB = NULL;
				ri.Printf(PRINT_ALL, "...not using multitexture, < 2 texture units\n");
			}
		}
		else
		{
			ri.Printf(PRINT_ALL, "...ignoring multitexture\n");
		}
	}
	else
	{
		ri.Printf(PRINT_ALL, "...multitexture not found\n");
	}

	// GL_EXT_compiled_vertex_array
	if (r_ext_compiled_vertex_array->integer) {
		ri.Printf(PRINT_ALL, "...compiled_vertex_array: n/a\n");
	}

	// WGL_3DFX_gamma_control
	//if (strstr(glConfig.extensions_string, "WGL_3DFX_gamma_control")) {
	//	if (!r_ignorehwgamma->integer && r_ext_gamma_control->integer) {
	//		qwglGetDeviceGammaRamp3DFX = (BOOL(WINAPI *)(HDC, LPVOID))qwglGetProcAddress("wglGetDeviceGammaRamp3DFX");
	//		qwglSetDeviceGammaRamp3DFX = (BOOL(WINAPI *)(HDC, LPVOID))qwglGetProcAddress("wglSetDeviceGammaRamp3DFX");

	//		if (qwglGetDeviceGammaRamp3DFX && qwglSetDeviceGammaRamp3DFX) {
	//			ri.Printf(PRINT_ALL, "...using WGL_3DFX_gamma_control\n");
	//		}
	//		else
	//		{
	//			qwglGetDeviceGammaRamp3DFX = NULL;
	//			qwglSetDeviceGammaRamp3DFX = NULL;
	//		}
	//	}
	//	else
	//	{
	//		ri.Printf(PRINT_ALL, "...ignoring WGL_3DFX_gamma_control\n");
	//	}
	//}
	//else
	//{
	//	ri.Printf(PRINT_ALL, "...WGL_3DFX_gamma_control not found\n");
	//}



	//----(SA)	added


	// GL_ATI_pn_triangles - ATI PN-Triangles
	if (0 != (qdx.caps.DevCaps2 & D3DDEVCAPS2_ADAPTIVETESSNPATCH)) {
		if (r_ext_ATI_pntriangles->integer) {
			ri.Printf(PRINT_ALL, "...adaptive tesselation is supported by gpu\n");

			//glConfig.ATIMaxTruformTess

			//qglPNTrianglesiATI = ( PFNGLPNTRIANGLESIATIPROC ) qwglGetProcAddress( "glPNTrianglesiATI" );
			//qglPNTrianglesfATI = ( PFNGLPNTRIANGLESFATIPROC ) qwglGetProcAddress( "glPNTrianglesfATI" );

			//if ( !qglPNTrianglesiATI || !qglPNTrianglesfATI ) {
			//	ri.Error( ERR_FATAL, "bad getprocaddress 0" );
			//}
		}
		else {
			ri.Printf(PRINT_ALL, "...ignoring adaptive tesselation\n");
			ri.Cvar_Set("r_ext_ATI_pntriangles", "0");
		}
	}
	else {
		ri.Printf(PRINT_ALL, "...adaptive tesselation not found\n");
		ri.Cvar_Set("r_ext_ATI_pntriangles", "0");
	}



	// GL_EXT_texture_filter_anisotropic
	glConfig.anisotropicAvailable = qfalse;
	if (0 != (qdx.caps.TextureFilterCaps & (D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MAGFANISOTROPIC)))
	{
		if (r_ext_texture_filter_anisotropic->integer) {
			glConfig.maxAnisotropy = qdx.caps.MaxAnisotropy;
			if (glConfig.maxAnisotropy <= 0) {
				ri.Printf(PRINT_ALL, "...texture_filter_anisotropic not properly supported!\n");
				ri.Cvar_Set("r_ext_texture_filter_anisotropic", "0");
				glConfig.maxAnisotropy = 0;
			}
			else
			{
				ri.Printf(PRINT_ALL, "...using texture_filter_anisotropic: %i (max: %i)\n",
					r_ext_texture_filter_anisotropic->integer, glConfig.maxAnisotropy);
				glConfig.anisotropicAvailable = qtrue;
			}
		}
		else
		{
			ri.Printf(PRINT_ALL, "...ignoring texture_filter_anisotropic\n");
		}
	}
	else
	{
		ri.Printf(PRINT_ALL, "...texture_filter_anisotropic not found\n");
		ri.Cvar_Set("r_ext_texture_filter_anisotropic", "0");
	}



	// GL_NV_fog_distance
	if (0 != (qdx.caps.RasterCaps & D3DPRASTERCAPS_WFOG))
	{
		ri.Printf(PRINT_ALL, "...pixel fog is supported by gpu\n");
	}
	if (0 != (qdx.caps.RasterCaps & (D3DPRASTERCAPS_FOGRANGE | D3DPRASTERCAPS_FOGVERTEX))) {
		ri.Printf(PRINT_ALL, "...eye radial vertex fog is supported (initially nv_fog)\n");
		if (r_ext_NV_fog_dist->integer) {
			glConfig.NVFogAvailable = qtrue;
			ri.Printf(PRINT_ALL, "...using eye radial fog_distance\n");
		}
		else {
			ri.Printf(PRINT_ALL, "...ignoring eye radial fog_distance\n");
			ri.Cvar_Set("r_ext_NV_fog_dist", "0");
		}
	}
	else {
		ri.Printf(PRINT_ALL, "...eye radial fog_distance not found\n");
		ri.Cvar_Set("r_ext_NV_fog_dist", "0");
	}

	//----(SA)	end

		// support?
	//	SGIS_generate_mipmap
	//	ARB_multisample
}


/*
** GLW_CheckOSVersion
*/
//static qboolean GLW_CheckOSVersion(void) {
//#define OSR2_BUILD_NUMBER 1111
//
//	OSVERSIONINFO vinfo;
//
//	vinfo.dwOSVersionInfoSize = sizeof(vinfo);
//
//	dx9imp_state.allowdisplaydepthchange = qfalse;
//
//	if (GetVersionEx(&vinfo)) {
//		if (vinfo.dwMajorVersion > 4) {
//			dx9imp_state.allowdisplaydepthchange = qtrue;
//		}
//		else if (vinfo.dwMajorVersion == 4) {
//			if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT) {
//				dx9imp_state.allowdisplaydepthchange = qtrue;
//			}
//			else if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
//				if (LOWORD(vinfo.dwBuildNumber) >= OSR2_BUILD_NUMBER) {
//					dx9imp_state.allowdisplaydepthchange = qtrue;
//				}
//			}
//		}
//	}
//	else
//	{
//		ri.Printf(PRINT_ALL, "GLW_CheckOSVersion() - GetVersionEx failed\n");
//		return qfalse;
//	}
//
//	return qtrue;
//}

/*
** GLW_LoadOpenGL
**
** GLimp_win.c internal function that attempts to load and use
** a specific OpenGL DLL.
*/
static qboolean GLW_LoadOpenGL(const char *drivername) {
	char buffer[1024];
	qboolean cdsFullscreen;

	Q_strncpyz(buffer, drivername, sizeof(buffer));
	Q_strlwr(buffer);

	glConfig.driverType = GLDRV_ICD;

	//
	// load the driver and bind our function pointers to it
	//
	if (QGL_DMY_Init(buffer)) {

		cdsFullscreen = (qboolean)r_fullscreen->integer;


		// create the window and set up the context
		if (!GLW_StartDriverAndSetMode(drivername, r_mode->integer, r_colorbits->integer, cdsFullscreen)) {
			// if we're on a 24/32-bit desktop and we're going fullscreen on an ICD,
			// try it again with desktop resolution
			if (glConfig.driverType == GLDRV_ICD) {
				if (r_colorbits->integer != 32 ||
					cdsFullscreen != qtrue ||
					r_mode->integer != -2) {
					if (!GLW_StartDriverAndSetMode(drivername, -2, 32, qtrue)) {
						goto fail;
					}
				}
			}
			else
			{
				goto fail;
			}
		}

		return qtrue;
	}
fail:

	QGL_DMY_Shutdown();

	return qfalse;
}

/*
** GLimp_EndFrame
*/
void DX9imp_EndFrame(void) {
	//
	// swapinterval stuff
	//
	if (r_swapInterval->modified) {
		r_swapInterval->modified = qfalse;

		if (!glConfig.stereoEnabled) {    // why?
			//if (qwglSwapIntervalEXT) {
			//	qwglSwapIntervalEXT(r_swapInterval->integer);
			//}
		}
	}

	if (qdx.devicelost)
	{
		// here we get the current status of the device
		HRESULT hr = qdx.device->TestCooperativeLevel();

		switch (hr)
		{
		case D3D_OK:
			//qdx.devicelost = qfalse;
			break;

		case D3DERR_DEVICELOST:
			// device is still lost
			break;

		case D3DERR_DEVICENOTRESET:
			// device is ready to be reset
			break;

		default:
			break;
		}

		Sleep( 10 );
		return;
	}

	qdx_before_frame_end();

	// don't flip if drawing to front buffer
	if (Q_stricmp(r_drawBuffer->string, "GL_FRONT") != 0) {
		if (glConfig.driverType > GLDRV_ICD) {
			//if (!qwglSwapBuffers(dx9imp_state.hDC)) {
			ri.Error(ERR_FATAL, "GLimp_EndFrame() - SwapBuffers() failed!\n");
			//}
		}
		else
		{
			//SwapBuffers(dx9imp_state.hDC);
			HRESULT hr = qdx.device->Present(NULL, NULL, NULL, NULL);
			if (FAILED(hr))
			{
				if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET) //D3DERR_DEVICEHUNG?
				{
					qdx.devicelost = qtrue;
				}
			}
		}
	}

	qdx_frame_ended();

	// check logging
	QGL_DMY_EnableLogging((qboolean)r_logFile->integer);
}

extern "C" qboolean DX9imp_requires_restart()
{
	return (qboolean)qdx.devicelost;
}

static void GLW_StartOpenGL(void) {

	//
	// load and initialize the specific OpenGL driver
	//
	if (!GLW_LoadOpenGL(r_glDriver->string)) {
		ri.Error(ERR_FATAL, "GLW_StartOpenGL() - could not load OpenGL subsystem\n");
	}
}

static const char *getVendorName(UINT vendor)
{
	switch (vendor)
	{
	case 0x8086:
		return "Intel Corporation";
	case 0x10DE:
		return "NVIDIA Corporation";
	case 0x1022:
	case 0x1002:
		return "Advanced Micro Devices, Inc.";
	}
	return "Unrecognized Vendor";
}

/*
** GLimp_Init
**
** This is the platform specific OpenGL initialization function.  It
** is responsible for loading OpenGL, initializing it, setting
** extensions, creating a window of the appropriate size, doing
** fullscreen manipulations, etc.  Its overall responsibility is
** to make sure that a functional OpenGL subsystem is operating
** when it returns to the ref.
*/
void DX9imp_Init(void) {
	char buf[1024];
	cvar_t *lastValidRenderer = ri.Cvar_Get("r_lastValidRenderer", "(uninitialized)", CVAR_ARCHIVE);
	cvar_t  *cv;

	ri.Printf(PRINT_ALL, "Initializing DX9 subsystem\n");

	//
	// check OS version to see if we can do fullscreen display changes
	//
	//if (!GLW_CheckOSVersion()) {
	//	ri.Error(ERR_FATAL, "GLimp_Init() - incorrect operating system\n");
	//}
	dx9imp_state.allowdisplaydepthchange = qtrue;

	// save off hInstance and wndproc
	cv = ri.Cvar_Get("win_hinstance", "", 0);
	sscanf(cv->string, "%i", (int *)&g_wv.hInstance);

	cv = ri.Cvar_Get("win_wndproc", "", 0);
	sscanf(cv->string, "%i", (int *)&dx9imp_state.wndproc);

	r_allowSoftwareGL = ri.Cvar_Get("r_allowSoftwareGL", "0", CVAR_LATCH);
	r_maskMinidriver = ri.Cvar_Get("r_maskMinidriver", "0", CVAR_LATCH);

	//Com_StartupVariable("r_systemdll");
	r_systemdll = ri.Cvar_Get("r_systemdll", "0", CVAR_INIT);

	// load appropriate DLL and initialize subsystem
	GLW_StartOpenGL();

	// get our config strings
	Q_strncpyz(glConfig.vendor_string, getVendorName(qdx.adapter_info.VendorId), sizeof(glConfig.vendor_string));
	Q_strncpyz(glConfig.renderer_string, qdx.adapter_info.Description, sizeof(glConfig.renderer_string));
	LARGE_INTEGER nDriverVersion;
	nDriverVersion.QuadPart = qdx.adapter_info.DriverVersion.QuadPart;
	WORD nProduct = HIWORD(nDriverVersion.HighPart);
	WORD nVersion = LOWORD(nDriverVersion.HighPart);
	WORD nSubVersion = HIWORD(nDriverVersion.LowPart);
	WORD nBuild = LOWORD(nDriverVersion.LowPart);
	Com_sprintf(glConfig.version_string, sizeof(glConfig.version_string), "%d.%d.%d.%d", nProduct, nVersion, nSubVersion, nBuild);
	Q_strncpyz(glConfig.extensions_string, "", sizeof(glConfig.extensions_string));

	//
	// chipset specific configuration
	//
	Q_strncpyz(buf, glConfig.renderer_string, sizeof(buf));
	Q_strlwr(buf);

	//
	// NOTE: if changing cvars, do it within this block.  This allows them
	// to be overridden when testing driver fixes, etc. but only sets
	// them to their default state when the hardware is first installed/run.
	//
	if (Q_stricmp(lastValidRenderer->string, glConfig.renderer_string)) {
		glConfig.hardwareType = GLHW_GENERIC;

		ri.Cvar_Set("r_textureMode", "GL_LINEAR_MIPMAP_NEAREST");
	}

	//
	// this is where hardware specific workarounds that should be
	// detected/initialized every startup should go.
	//

	if (strstr(buf, "geforce") || strstr(buf, "ge-force") || strstr(buf, "radeon") || strstr(buf, "nv20") || strstr(buf, "nv30")
		|| strstr(buf, "quadro")) {
		ri.Cvar_Set("r_highQualityVideo", "1");
	}
	else {
		ri.Cvar_Set("r_highQualityVideo", "1");
	}


	ri.Cvar_Set("r_lastValidRenderer", glConfig.renderer_string);

	GLW_InitExtensions();
	DX9imp_CheckHardwareGamma();

	qdx_mats.init();
}

/*
** GLimp_Shutdown
**
** This routine does all OS specific shutdown procedures for the OpenGL
** subsystem.
*/
void DX9imp_Shutdown(void) {
	//	const char *strings[] = { "soft", "hard" };
	const char *success[] = { "failed", "success" };
	int retVal;

	// FIXME: Brian, we need better fallbacks from partially initialized failures
	//if (!qwglMakeCurrent) {
	//	return;
	//}

	ri.Printf(PRINT_ALL, "Shutting down DX9 subsystem\n");

	// restore gamma.  We do this first because 3Dfx's extension needs a valid OGL subsystem
	DX9imp_RestoreGamma();

	// delete HGLRC
	//if (dx9imp_state.hGLRC) {
	//	retVal = qwglDeleteContext(dx9imp_state.hGLRC) != 0;
	//	ri.Printf(PRINT_ALL, "...deleting GL context: %s\n", success[retVal]);
	//	dx9imp_state.hGLRC = NULL;
	//}
	if (qdx.device)
	{
		qdx_objects_reset();
#if 1
		//if (dx9imp_state.cdsFullscreen)
		{
			//there seems to be a problem with releasing and recreating the device if fullscreen edit: or windowed
			//todo: am I doing something wrong with the objects? not releasing everything?
			dx9imp_state.cdsFullscreen = qfalse;
			D3DPRESENT_PARAMETERS d3dpp;
			fill_in_d3dpresentparams(d3dpp);
			HRESULT hr = qdx.device->Reset(&d3dpp);
			if (FAILED(hr))
			{
				ri.Printf(PRINT_ERROR, "Shutdown: failed to reset device: %d\n", HRESULT_CODE(hr));
			}
			hr = qdx.device->TestCooperativeLevel();
			if (hr == D3DERR_DEVICELOST) {
				ri.Printf(PRINT_ERROR, "Shutdown: D3DERR_DEVICELOST\n");
				qdx.devicelost = qtrue;
			} else if(hr != D3D_OK) {
				ri.Printf(PRINT_ERROR, "Shutdown: tcl code  %d\n", hr);
			}
		}
#endif
		qdx.device->Release();
		qdx.device = 0;
	}
	if (qdx.d3d)
	{
		qdx.d3d->Release();
		qdx.d3d = 0;
	}
	if ( qdx.modes )
	{
		free( qdx.modes );
		qdx.modes = 0;
		qdx.modes_count = 0;
	}

	// release DC
	if (dx9imp_state.hDC) {
		retVal = ReleaseDC(g_wv.hWnd, dx9imp_state.hDC) != 0;
		ri.Printf(PRINT_ALL, "...releasing DC: %s\n", success[retVal]);
		dx9imp_state.hDC = NULL;
	}

	// destroy window
	if (g_wv.hWnd) {
		ri.Printf(PRINT_ALL, "...destroying window\n");
		ShowWindow(g_wv.hWnd, SW_HIDE);
		DestroyWindow(g_wv.hWnd);
		g_wv.hWnd = NULL;
		dx9imp_state.pixelFormatSet = qfalse;
	}

	// close the r_logFile
	//if (dx9imp_state.log_fp) {
	//	fclose(dx9imp_state.log_fp);
	//	dx9imp_state.log_fp = 0;
	//}

	// reset display settings
	if (dx9imp_state.cdsFullscreen) {
		ri.Printf(PRINT_ALL, "...resetting display\n");
		ChangeDisplaySettings(0, 0);
		dx9imp_state.cdsFullscreen = qfalse;
	}

	// shutdown QGL subsystem
	QGL_DMY_Shutdown();

	memset(&glConfig, 0, sizeof(glConfig));
	memset(&glState, 0, sizeof(glState));
}

/*
** GLimp_LogComment
*/
//void DX9imp_LogComment(char *comment) {
//	if (dx9imp_state.log_fp) {
//		fprintf(dx9imp_state.log_fp, "%s", comment);
//	}
//}

#define FILEOP_RETRIES 10
static unsigned short s_oldHardwareGamma[3][256];

/*
** DX9imp_CheckHardwareGamma
**
** Determines if the underlying hardware supports the Win32 gamma correction API.
*/
void DX9imp_CheckHardwareGamma( void ) {
	HDC hDC;

	glConfig.deviceSupportsGamma = qfalse;

	// non-3Dfx standalone drivers don't support gamma changes, period
	if ( glConfig.driverType == GLDRV_STANDALONE ) {
		return;
	}

	if ( !r_ignorehwgamma->integer ) {
		hDC = GetDC( GetDesktopWindow() );
		glConfig.deviceSupportsGamma = (qboolean)GetDeviceGammaRamp( hDC, s_oldHardwareGamma );
		ReleaseDC( GetDesktopWindow(), hDC );

		if ( glConfig.deviceSupportsGamma ) {
			//
			// do a sanity check on the gamma values
			//
			if ( ( HIBYTE( s_oldHardwareGamma[0][255] ) <= HIBYTE( s_oldHardwareGamma[0][0] ) ) ||
				( HIBYTE( s_oldHardwareGamma[1][255] ) <= HIBYTE( s_oldHardwareGamma[1][0] ) ) ||
				( HIBYTE( s_oldHardwareGamma[2][255] ) <= HIBYTE( s_oldHardwareGamma[2][0] ) ) ) {
				glConfig.deviceSupportsGamma = qfalse;
				ri.Printf( PRINT_WARNING, "WARNING: device has broken gamma support, generated gamma.dat\n" );
			}

			cvar_t  *basedir;
			char filename[1024];
			void* filedata = 0;

			basedir = ri.Cvar_Get( "fs_basepath", "", 0 );
			Com_sprintf( filename, sizeof( filename ), "%s/oldHardwareGamma.bin", basedir->string );
			bool filepresent = false;
			bool samedata = false;
			FILE* fp;
			fp = fopen(filename, "rb");
			if (fp)
			{
				filepresent = true;
				filedata = malloc(sizeof(s_oldHardwareGamma));
				if (filedata)
				{
					int retries = FILEOP_RETRIES;
					do { retries--; } while(1 != fread(filedata, sizeof(s_oldHardwareGamma), 1, fp) && retries > 0);
					if (retries > 0)
					{
						samedata = (0 == memcmp(filedata, s_oldHardwareGamma, sizeof(s_oldHardwareGamma)));
					}
				}
				fclose(fp);
			}

			if (!filepresent)
			{
				fp = fopen(filename, "wb");
				int remain = sizeof(s_oldHardwareGamma);
				int retries = FILEOP_RETRIES;
				do
				{
					size_t written = fwrite(s_oldHardwareGamma, 1, sizeof(s_oldHardwareGamma), fp);
					retries--;
					remain -= int(written);
				} while (remain > 0 && retries > 0);
				fclose(fp);
			}

			//
			// make sure that we didn't have a prior crash in the game, and if so we need to
			// restore the gamma values to at least a linear value
			//
			if (filepresent && !samedata && filedata)
			{
				memcpy(s_oldHardwareGamma, filedata, sizeof(s_oldHardwareGamma));
				ri.Printf( PRINT_WARNING, "WARNING: restoring gamma tables from backup file\n" );
			}
			//if ( ( HIBYTE( s_oldHardwareGamma[0][181] ) == 255 ) ) {
			//	int g;

			//	ri.Printf( PRINT_WARNING, "WARNING: suspicious gamma tables, using linear ramp for restoration\n" );

			//	for ( g = 0; g < 255; g++ )
			//	{
			//		s_oldHardwareGamma[0][g] = g << 8;
			//		s_oldHardwareGamma[1][g] = g << 8;
			//		s_oldHardwareGamma[2][g] = g << 8;
			//	}
			//}
			if (filedata)
			{
				free(filedata);
			}
		}
	}
}

/*
** DX9imp_SetGamma
**
** This routine should only be called if glConfig.deviceSupportsGamma is TRUE
*/
void DX9imp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] ) {
	unsigned short table[3][256];
	int i, j;
	int ret;
	OSVERSIONINFO vinfo;

	if ( !glConfig.deviceSupportsGamma || r_ignorehwgamma->integer || !dx9imp_state.hDC ) {
		return;
	}

	//mapGammaMax();

	for ( i = 0; i < 256; i++ ) {
		table[0][i] = ( ( ( unsigned short ) red[i] ) << 8 ) | red[i];
		table[1][i] = ( ( ( unsigned short ) green[i] ) << 8 ) | green[i];
		table[2][i] = ( ( ( unsigned short ) blue[i] ) << 8 ) | blue[i];
	}

	// Win2K puts this odd restriction on gamma ramps...
	vinfo.dwOSVersionInfoSize = sizeof( vinfo );
	GetVersionEx( &vinfo );
	if ( vinfo.dwMajorVersion == 5 && vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT ) {
		Com_DPrintf( "performing W2K gamma clamp.\n" );
		for ( j = 0 ; j < 3 ; j++ ) {
			for ( i = 0 ; i < 128 ; i++ ) {
				if ( table[j][i] > ( ( 128 + i ) << 8 ) ) {
					table[j][i] = ( 128 + i ) << 8;
				}
			}
			if ( table[j][127] > 254 << 8 ) {
				table[j][127] = 254 << 8;
			}
		}
	} else {
		Com_DPrintf( "skipping W2K gamma clamp.\n" );
	}

	// enforce constantly increasing
	for ( j = 0 ; j < 3 ; j++ ) {
		for ( i = 1 ; i < 256 ; i++ ) {
			if ( table[j][i] < table[j][i - 1] ) {
				table[j][i] = table[j][i - 1];
			}
		}
	}

	ret = SetDeviceGammaRamp( dx9imp_state.hDC, table );
	if ( !ret ) {
		Com_Printf( "SetDeviceGammaRamp failed.\n" );
	}
}

/*
** DX9imp_RestoreGamma
*/
void DX9imp_RestoreGamma( void ) {
	if ( glConfig.deviceSupportsGamma )
	{
		HDC hDC;

		hDC = GetDC( GetDesktopWindow() );
		SetDeviceGammaRamp( hDC, s_oldHardwareGamma );
		ReleaseDC( GetDesktopWindow(), hDC );
	}
}

/*
===========================================================

SMP acceleration

===========================================================
*/

HANDLE renderCommandsEvent;
HANDLE renderCompletedEvent;
HANDLE renderActiveEvent;

void(*glimpRenderThread)(void);

void GLimp_RenderThreadWrapper(void) {
	glimpRenderThread();

	// unbind the context before we die
	//qwglMakeCurrent(dx9imp_state.hDC, NULL);
}

/*
=======================
GLimp_SpawnRenderThread
=======================
*/
HANDLE renderThreadHandle;
DWORD renderThreadId;
qboolean DX9imp_SpawnRenderThread(void(*function)(void)) {

	renderCommandsEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	renderCompletedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	renderActiveEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	glimpRenderThread = function;

	renderThreadHandle = CreateThread(
		NULL,   // LPSECURITY_ATTRIBUTES lpsa,
		0,      // DWORD cbStack,
		(LPTHREAD_START_ROUTINE)GLimp_RenderThreadWrapper,  // LPTHREAD_START_ROUTINE lpStartAddr,
		0,          // LPVOID lpvThreadParm,
		0,          //   DWORD fdwCreate,
		&renderThreadId);

	if (!renderThreadHandle) {
		return qfalse;
	}

	return qtrue;
}

static void    *smpData;
static int wglErrors;

void *DX9imp_RendererSleep(void) {
	void    *data;

	//if (!qwglMakeCurrent(dx9imp_state.hDC, NULL)) {
	//	wglErrors++;
	//}

	ResetEvent(renderActiveEvent);

	// after this, the front end can exit GLimp_FrontEndSleep
	SetEvent(renderCompletedEvent);

	WaitForSingleObject(renderCommandsEvent, INFINITE);

	//if (!qwglMakeCurrent(dx9imp_state.hDC, dx9imp_state.hGLRC)) {
	//	wglErrors++;
	//}

	ResetEvent(renderCompletedEvent);
	ResetEvent(renderCommandsEvent);

	data = smpData;

	// after this, the main thread can exit GLimp_WakeRenderer
	SetEvent(renderActiveEvent);

	return data;
}


void DX9imp_FrontEndSleep(void) {
	WaitForSingleObject(renderCompletedEvent, INFINITE);

	//if (!qwglMakeCurrent(dx9imp_state.hDC, dx9imp_state.hGLRC)) {
	//	wglErrors++;
	//}
}


void DX9imp_WakeRenderer(void *data) {
	smpData = data;

	//if (!qwglMakeCurrent(dx9imp_state.hDC, NULL)) {
	//	wglErrors++;
	//}

	// after this, the renderer can continue through GLimp_RendererSleep
	SetEvent(renderCommandsEvent);

	WaitForSingleObject(renderActiveEvent, INFINITE);
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

static void qdx_texobj_delete_all()
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

	if(r_logFile->integer)
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

static void qdx_clear_buffers()
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
	qdx_clear_buffers();
	qdx_texobj_delete_all();
	qdx_lights_clear(LIGHT_DYNAMIC | LIGHT_FLARE);
}

static void qdx_frame_ended()
{
}

static void qdx_before_frame_end()
{
	qdx_lights_draw();
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

static void qdx_log_comment(const char *name, UINT vattbits, const void *ptr)
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

static void qdx_log_matrix(const char *name, const float *mat)
{
	char comment[256];
	snprintf(comment, sizeof(comment), "qdx_matrix[%s]:\n  %f %f %f %f\n  %f %f %f %f\n  %f %f %f %f\n  %f %f %f %f\n", name,
		mat[0], mat[1], mat[2], mat[3],
		mat[4], mat[5], mat[6], mat[7],
		mat[8], mat[9], mat[10], mat[11],
		mat[12], mat[13], mat[14], mat[15]);
	DX9imp_LogComment(comment);
}

static void qdx_log_dump_buffer(const char *name, const uint32_t *buffer, uint32_t size, int wide, uint32_t hashid, uint32_t hashbuf)
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
	if (r_logFile->integer)
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
	if (r_logFile->integer)
		qdx_log_comment(__FUNCTION__, 0, (const void*)color);

	qdx.crt_color = color;
}

void qdx_vatt_enable_buffer(vatt_param_t param)
{
	if (r_logFile->integer)
		qdx_log_comment(__FUNCTION__, param, NULL);

	g_vattribs.active_vatts |= param;
}

void qdx_vatt_disable_buffer(vatt_param_t param)
{
	if (r_logFile->integer)
		qdx_log_comment(__FUNCTION__, param, NULL);

	g_vattribs.active_vatts &= ~param;
}

void qdx_vatt_lock_buffers(int num_elements)
{
	if (r_logFile->integer)
		qdx_log_comment(__FUNCTION__, g_vattribs.active_vatts, (void*)num_elements);

	g_vattribs.locked_vatts = g_vattribs.active_vatts;
}

void qdx_vatt_unlock_buffers()
{
	if (r_logFile->integer)
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
	if (r_logFile->integer)
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

	if (r_logFile->integer)
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
		if (r_logFile->integer)
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
			if (r_logFile->integer)
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
		if (r_logFile->integer)
			qdx_log_dump_buffer("vertex", (const uint32_t*)pVert, selectionsize * stride_vatt / sizeof(uint32_t), stride_vatt / sizeof(uint32_t), hash, vhash);

		v_buffer->Unlock();
	}

	//qdx.device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0x33, 0x4d, 0x4d), 1.0f, 0);
	//qdx.device->Clear(0, NULL, D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
	//qdx.device->SetRenderState(D3DRS_LIGHTING, FALSE);
	//qdx.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	qdx.device->BeginScene();

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
	qdx.device->EndScene();
}

static void qdx_draw_process(uint32_t selectionbits, int index, byte* buf);

void qdx_vatt_assemble_and_draw0(UINT numindexes, const qdxIndex_t *indexes, const char *hint)
{
	DWORD selected_vatt = VATTID_VERTCOL;
	UINT stride_vatt = sizeof(vatt_vertcol_t);
	qdxIndex_t lowindex = SHADER_MAX_INDEXES, highindex = 0, offindex = 0;
	UINT selectionsize = 0;
	UINT hash = 0;

	if (r_logFile->integer)
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


	qdx.device->BeginScene();

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

	qdx.device->EndScene();
}

void qdx_vatt_assemble_and_draw0a(UINT numindexes, const qdxIndex_t *indexes, const char *hint)
{
	DWORD selected_vatt = VATTID_VERTCOL;
	UINT stride_vatt = sizeof(vatt_vertcol_t);
	qdxIndex_t lowindex = SHADER_MAX_INDEXES, highindex = 0, offindex = 0;
	UINT selectionsize = 0;
	UINT hash = 0;

	if (r_logFile->integer)
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


	qdx.device->BeginScene();

	qdx.device->SetFVF(selected_vatt);

	qdx.device->SetStreamSource(0, v_buffer, 0, stride_vatt);

	qdx_texture_apply();

	qdx_matrix_apply();

	for (int i = 0; i < drawbuf_idx; i+=2)
	{
		qdx.device->DrawPrimitive(D3DPT_TRIANGLESTRIP, drawbuf[i], drawbuf[i+1]);
	}

	qdx.device->EndScene();
}

BOOL qdx_vbuffer_steps(qdx_vbuffer_t *buf, UINT vattid, UINT size, void **outmem)
{
	LPDIRECT3DVERTEXBUFFER9 v_buffer = *buf;

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

	if (v_buffer != NULL)
	{
		if (outmem)
		{
			if (FAILED(v_buffer->Lock(0, 0, outmem, D3DLOCK_DISCARD)))
			{
				return FALSE;
			}
		}
		else
		{
			v_buffer->Unlock();
		}
	}

	return TRUE;
}

qdx_vbuffer_t qdx_vbuffer_upload(qdx_vbuffer_t buf, UINT vattid, UINT size, void *data)
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

	if (0 && r_logFile->integer)
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

#include <map>

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


#define MAX_LIGHTS_REMIX 100
#define MAX_LIGHTS_DX9 8
typedef struct light_data
{
	uint64_t hash;
	bool isRemix;
	union
	{
		DWORD number;
		remixapi_LightHandle handle;
	};
	float distance;
} light_data_t;

static int MAX_LIGHTS = 0;
std::map<uint64_t, light_data_t> g_lights_dynamic;
std::map<uint64_t, light_data_t> g_lights_flares;
static int g_lights_number = 0;

//no idea what to choose here
#define LIGHT_RADIANCE_DYNAMIC 15000.0f
#define LIGHT_RADIANCE_FLARES 50.0f
#define LIGHT_RADIANCE_KILL_REDFLARES 0.3f

static void qdx_ligt_color_to_radiance(remixapi_Float3D* rad, const vec3_t color, float scale, int light_type)
{
	float radiance = 1.0f;
	if (light_type == LIGHT_DYNAMIC)
	{
		radiance = LIGHT_RADIANCE_DYNAMIC * scale;
	}
	else if (light_type == LIGHT_FLARE)
	{
		if ((color[0] == 1.0f) && (color[1] == 0.0f) && (color[2] == 0.0f))
		{
			radiance = LIGHT_RADIANCE_KILL_REDFLARES;
		}
		else
		{
			radiance = LIGHT_RADIANCE_FLARES;
		}
	}

	rad->x = radiance * color[0];
	rad->y = radiance * color[1];
	rad->z = radiance * color[2];
}

void qdx_lights_clear(unsigned int light_types)
{
	if (qdx.device)
	{
		if (light_types & LIGHT_DYNAMIC)
		{
			for (auto it = g_lights_dynamic.begin(); it != g_lights_dynamic.end(); it++)
			{
				if (remixOnline && it->second.isRemix)
				{
					remixInterface.DestroyLight(it->second.handle);
				}
				else
				{
					qdx.device->LightEnable(it->second.number, FALSE);
				}
			}
			g_lights_dynamic.clear();
		}
		if (light_types & LIGHT_FLARE)
		{
			for (auto it = g_lights_flares.begin(); it != g_lights_flares.end(); it++)
			{
				if (remixOnline && it->second.isRemix)
				{
					remixInterface.DestroyLight(it->second.handle);
				}
				else
				{
					qdx.device->LightEnable(it->second.number, FALSE);
				}
			}
			g_lights_flares.clear();
		}
	}
}

static void qdx_lights_draw()
{
	for (auto it = g_lights_dynamic.begin(); it != g_lights_dynamic.end(); it++)
	{
		if (remixOnline && it->second.isRemix)
		{
			remixInterface.DrawLightInstance(it->second.handle);
		}
		else
		{
			qdx.device->LightEnable(it->second.number, TRUE);
		}
	}
	for (auto it = g_lights_flares.begin(); it != g_lights_flares.end(); it++)
	{
		remixInterface.DrawLightInstance(it->second.handle);
	}
}

#define DYNAMIC_LIGHTS_USE_DX9 1

void qdx_light_add(int light_type, int ord, float *position, float *transformed, float *color, float radius, float scale)
{
	remixapi_ErrorCode rercd;
	uint64_t hash = 0;
	uint64_t hashpos = 0;
	uint64_t hashclr = 0;
	uint64_t hashord = 0;
	bool found = false;

	if (0 != memcmp(position, transformed, sizeof(float)*3))
	{
		if (position == transformed)
		{
			ri.Printf(PRINT_WARNING, "light %d %d\n", light_type, ord);
		}
	}

	//remixOnline = false;

	hashpos = fnv_32a_buf(position, 3*sizeof(float), 0x55FF);
	//hashclr = fnv_32a_buf(color, 3*sizeof(float), 0x55FF);
	hashord = fnv_32a_buf(&ord, sizeof(ord), 0x55FF);

	if (light_type == LIGHT_DYNAMIC)
	{
		hash = hashord;
		//return;
	}
	if (light_type == LIGHT_FLARE)
	{
		hash = hashpos;
		auto itm = g_lights_flares.find(hash);
		if (itm != g_lights_flares.end())
		{
			//flare already exists, nothing to do
			return;
		}
	}

	if (ord == 0 && light_type == LIGHT_DYNAMIC)
	{
		qdx_lights_clear(LIGHT_DYNAMIC);
	}

	light_data_t light_store;
	ZeroMemory(&light_store, sizeof(light_store));
	light_store.hash = hash;
	//light_store.distance = 0.0f;

#if DYNAMIC_LIGHTS_USE_DX9
	if (light_type == LIGHT_DYNAMIC)
	{
		D3DLIGHT9 light;
		ZeroMemory(&light, sizeof(light));

		light.Type = D3DLIGHT_POINT;
		light.Diffuse.r = color[0];
		light.Diffuse.g = color[1];
		light.Diffuse.b = color[2];
		light.Diffuse.a = 1.0f;
		light.Specular.r = 1.0f;
		light.Specular.g = 1.0f;
		light.Specular.b = 1.0f;
		light.Specular.a = 1.0f;
		light.Ambient.r = 1.0f;
		light.Ambient.g = 1.0f;
		light.Ambient.b = 1.0f;
		light.Ambient.a = 1.0f;
		light.Position.x = position[0];
		light.Position.y = position[1];
		light.Position.z = position[2];

		light.Range = 2 * radius;
		light.Attenuation0 = 1.75f;
		light.Attenuation1 = scale;
		light.Attenuation2 = 0.0f;

		qdx.device->SetLight(ord, &light);
		qdx.device->LightEnable(ord, TRUE);

		light_store.isRemix = false;
		light_store.number = ord;
		g_lights_dynamic[hash] = light_store;

		return;
	}
#endif

	if (remixOnline)
	{
		remixapi_LightInfoSphereEXT light_sphere;
		ZeroMemory(&light_sphere, sizeof(light_sphere));

		light_sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
		light_sphere.position.x = position[0];
		light_sphere.position.y = position[1];
		light_sphere.position.z = position[2];
		light_sphere.radius = /*(light_type == LIGHT_DYNAMIC) ? radius :*/ 6.0f;
		light_sphere.shaping_hasvalue = 0;

		remixapi_LightInfo lightinfo;
		ZeroMemory(&lightinfo, sizeof(lightinfo));

		lightinfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
		lightinfo.pNext = &light_sphere;
		lightinfo.hash = hash;
		qdx_ligt_color_to_radiance(&lightinfo.radiance, color, scale, light_type);

		light_store.isRemix = true;
		rercd = remixInterface.CreateLight(&lightinfo, &light_store.handle);
		if (rercd != REMIXAPI_ERROR_CODE_SUCCESS)
		{
			ri.Printf(PRINT_ERROR, "RMX failed to create light %d\n", rercd);
			return;
		}
		switch (light_type)
		{
		case LIGHT_DYNAMIC:
			g_lights_dynamic[hash] = light_store;
			break;
		case LIGHT_FLARE:
			g_lights_flares[hash] = light_store;
			break;
		default:
			qassert(FALSE && "Unknown light type");
		}
	}
	else
	{
		//D3DLIGHT9 light;
		//ZeroMemory(&light, sizeof(light));

		//light.Type = D3DLIGHT_POINT;
		//light.Diffuse.r = color[0];
		//light.Diffuse.g = color[1];
		//light.Diffuse.b = color[2];
		//light.Diffuse.a = 1.0f;
		//light.Specular.r = 1.0f;
		//light.Specular.g = 1.0f;
		//light.Specular.b = 1.0f;
		//light.Specular.a = 1.0f;
		//light.Ambient.r = 1.0f;
		//light.Ambient.g = 1.0f;
		//light.Ambient.b = 1.0f;
		//light.Ambient.a = 1.0f;
		//light.Position.x = transformed[0];
		//light.Position.y = transformed[1];
		//light.Position.z = transformed[2];

		//switch (light_type)
		//{
		//case LIGHT_DYNAMIC:
		//	light.Range = 2 * radius;
		//	light.Attenuation0 = 0.5f;
		//	light.Attenuation1 = scale;
		//	light.Attenuation2 = 0.01f;

		//	break;
		//case LIGHT_FLARE:
		//	light.Range = 6;
		//	light.Attenuation0 = 1.5f;
		//	light.Attenuation1 = 0.5;
		//	light.Attenuation2 = 0.0f;

		//	break;
		//}

		//qdx.device->SetLight(light_num, &light);
		//qdx.device->LightEnable(light_num, TRUE);
	}
}

#define MINI_CASE_SENSITIVE
#include "ini.h"

mINI::INIFile g_inifile("wolf_customise.ini");
mINI::INIStructure g_iniconf;

void qdx_begin_loading_map(const char* mapname)
{
	static char section[256];
	if (g_inifile.read(g_iniconf) && remixOnline)
	{
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

		mINI::INIMap<std::string> opts;

		snprintf(section, sizeof(section), "rtxconf.%.*s", namelen, name);
		if (g_iniconf.has(section))
		{
			opts = g_iniconf.get(section);
		}
		else
		{
			opts = g_iniconf.get("rtxconf.default");
		}

		for (auto it = opts.begin(); it != opts.end(); it++)
		{
			const char* key = it->first.c_str();
			const char* value = it->second.c_str();
			remixapi_ErrorCode rercd = remixInterface.SetConfigVariable(key, value);
			if (REMIXAPI_ERROR_CODE_SUCCESS != rercd)
			{
				ri.Printf(PRINT_ERROR, "RMX failed to set config var %d\n", rercd);
			}
		}
	}
}

std::map<UINT32, std::vector<const void*>> g_surfaces;

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
