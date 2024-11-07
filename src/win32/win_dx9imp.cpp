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
#include <assert.h>
#include "../renderer/qdx9.h"
extern "C"
{
#include "../renderer/tr_local.h"
#include "../qcommon/qcommon.h"
#include "resource.h"
#include "win_local.h"
}
#include <stdint.h>

typedef struct
{
	WNDPROC wndproc;

	HDC hDC;                // handle to device context
	HGLRC hGLRC;            // handle to GL rendering context

	HINSTANCE hinstOpenGL;  // HINSTANCE for the OpenGL library

	qboolean allowdisplaydepthchange;
	qboolean pixelFormatSet;

	int desktopBitsPixel;
	int desktopWidth, desktopHeight;

	qboolean cdsFullscreen;

	FILE *log_fp;
} dx9state_t;

static void DX9imp_CheckHardwareGamma(void);
static void DX9imp_RestoreGamma(void);

typedef enum {
	RSERR_OK,

	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,

	RSERR_UNKNOWN
} rserr_t;

#define TRY_PFD_SUCCESS     0
#define TRY_PFD_FAIL_SOFT   1
#define TRY_PFD_FAIL_HARD   2

#define WINDOW_CLASS_NAME   "Wolfenstein"

static void     GLW_InitExtensions(void);
static rserr_t  GLW_SetMode(const char *drivername,
	int mode,
	int colorbits,
	qboolean cdsFullscreen);

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
dx9state_t dx9imp_state;
struct qdx9_state qdx;

cvar_t  *r_allowSoftwareGL;     // don't abort out if the pixelformat claims software
cvar_t  *r_maskMinidriver;      // allow a different dll name to be treated as if it were opengl32.dll



/*
** GLW_StartDriverAndSetMode
*/
static qboolean GLW_StartDriverAndSetMode(const char *drivername,
	int mode,
	int colorbits,
	qboolean cdsFullscreen) {
	rserr_t err;

	const int minwidth = 800;
	const int minheight = 600;

	ZeroMemory(&qdx, sizeof(qdx));
	qdx.d3d = Direct3DCreate9(D3D_SDK_VERSION);
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
	ZeroMemory(qdx.modes,(sizeof(D3DDISPLAYMODE) * (1 + nummodes)));
	D3DDISPLAYMODE *m = qdx.modes;
	for (int mode = 0; mode < nummodes; mode++)
	{
		D3DDISPLAYMODE dispMode;
		qdx.d3d->EnumAdapterModes(qdx.adapter_num, qdx.desktop.Format, mode, &dispMode);
		if (dispMode.Width < minwidth || dispMode.Height < minheight)
			continue;
		memcpy(m, &dispMode, sizeof(dispMode));
		m++;
	}

	err = GLW_SetMode(drivername, r_mode->integer, colorbits, cdsFullscreen);

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
		bits = 32;
		break;
	case D3DFMT_D24S8:
	case D3DFMT_D24X4S4:
	case D3DFMT_D24X8:
		bits = 24;
		break;
	case D3DFMT_D16:
		bits = 16;
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
					if (SUCCEEDED(qdx.d3d->CheckDepthStencilMatch(qdx.adapter_num, D3DDEVTYPE_HAL, qdx.desktop.Format,
						fmt_bb[ibb], fmt_ds[ids])))
					{
						if (ids < ds_match) {
							bb_match = ibb;
							ds_match = ids;
						}
						//todo: print format supported available
						ret = TRY_PFD_SUCCESS;
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
inline LONG ChangeDisplaySettings(LPVOID, DWORD) { return DISP_CHANGE_SUCCESSFUL; }

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
	if (!R_GetModeInfo(&glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, mode)) {
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

	if (qdx.device == 0)
	{
		D3DPRESENT_PARAMETERS d3dpp;
		fill_in_d3dpresentparams(d3dpp);

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
	if (SUCCEEDED(TEX_FMT(D3DFMT_DXT1)) && SUCCEEDED(TEX_FMT(D3DFMT_DXT3)) && SUCCEEDED(TEX_FMT(D3DFMT_DXT5))) {
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
		ri.Printf(PRINT_ALL, "...using swap_control\n");
		r_swapInterval->modified = qtrue;   // force a set next frame
	}
	else
	{
		ri.Printf(PRINT_ALL, "...swap_control not found\n");
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
		ri.Printf(PRINT_ALL, "...compiled_vertex_array is set\n");
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
			ri.Printf(PRINT_ALL, "...using GL_ATI_pn_triangles\n");

			//glConfig.ATIMaxTruformTess

			//qglPNTrianglesiATI = ( PFNGLPNTRIANGLESIATIPROC ) qwglGetProcAddress( "glPNTrianglesiATI" );
			//qglPNTrianglesfATI = ( PFNGLPNTRIANGLESFATIPROC ) qwglGetProcAddress( "glPNTrianglesfATI" );

			//if ( !qglPNTrianglesiATI || !qglPNTrianglesfATI ) {
			//	ri.Error( ERR_FATAL, "bad getprocaddress 0" );
			//}
		}
		else {
			ri.Printf(PRINT_ALL, "...ignoring ATI_pn_triangles\n");
			ri.Cvar_Set("r_ext_ATI_pntriangles", "0");
		}
	}
	else {
		ri.Printf(PRINT_ALL, "...ATI_pn_triangles not found\n");
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
				ri.Printf(PRINT_ALL, "...using texture_filter_anisotropic (max: %i)\n", glConfig.maxAnisotropy);
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
	if (0 != (qdx.caps.RasterCaps & (D3DPRASTERCAPS_FOGRANGE| D3DPRASTERCAPS_FOGVERTEX))) {
		if (r_ext_NV_fog_dist->integer) {
			glConfig.NVFogAvailable = qtrue;
			ri.Printf(PRINT_ALL, "...using fog_distance\n");
		}
		else {
			ri.Printf(PRINT_ALL, "...ignoring fog_distance\n");
			ri.Cvar_Set("r_ext_NV_fog_dist", "0");
		}
	}
	else {
		ri.Printf(PRINT_ALL, "...fog_distance not found\n");
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
static qboolean GLW_CheckOSVersion(void) {
#define OSR2_BUILD_NUMBER 1111

	OSVERSIONINFO vinfo;

	vinfo.dwOSVersionInfoSize = sizeof(vinfo);

	dx9imp_state.allowdisplaydepthchange = qfalse;

	if (GetVersionEx(&vinfo)) {
		if (vinfo.dwMajorVersion > 4) {
			dx9imp_state.allowdisplaydepthchange = qtrue;
		}
		else if (vinfo.dwMajorVersion == 4) {
			if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT) {
				dx9imp_state.allowdisplaydepthchange = qtrue;
			}
			else if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
				if (LOWORD(vinfo.dwBuildNumber) >= OSR2_BUILD_NUMBER) {
					dx9imp_state.allowdisplaydepthchange = qtrue;
				}
			}
		}
	}
	else
	{
		ri.Printf(PRINT_ALL, "GLW_CheckOSVersion() - GetVersionEx failed\n");
		return qfalse;
	}

	return qtrue;
}

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
			// try it again but with a 16-bit desktop
			if (glConfig.driverType == GLDRV_ICD) {
				if (r_colorbits->integer != 16 ||
					cdsFullscreen != qtrue ||
					r_mode->integer != 3) {
					if (!GLW_StartDriverAndSetMode(drivername, 3, 16, qtrue)) {
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
			if (qwglSwapIntervalEXT) {
				qwglSwapIntervalEXT(r_swapInterval->integer);
			}
		}
	}


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
			qdx.device->Present(NULL, NULL, NULL, NULL);
		}
	}

	// check logging
	QGL_DMY_EnableLogging((qboolean)r_logFile->integer);
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
	if (!GLW_CheckOSVersion()) {
		ri.Error(ERR_FATAL, "GLimp_Init() - incorrect operating system\n");
	}

	// save off hInstance and wndproc
	cv = ri.Cvar_Get("win_hinstance", "", 0);
	sscanf(cv->string, "%i", (int *)&g_wv.hInstance);

	cv = ri.Cvar_Get("win_wndproc", "", 0);
	sscanf(cv->string, "%i", (int *)&dx9imp_state.wndproc);

	r_allowSoftwareGL = ri.Cvar_Get("r_allowSoftwareGL", "0", CVAR_LATCH);
	r_maskMinidriver = ri.Cvar_Get("r_maskMinidriver", "0", CVAR_LATCH);

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
		qdx.device->Release();
		qdx.device = 0;
	}
	if (qdx.d3d)
	{
		qdx.d3d->Release();
		qdx.d3d = 0;
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
	if (dx9imp_state.log_fp) {
		fclose(dx9imp_state.log_fp);
		dx9imp_state.log_fp = 0;
	}

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
void DX9imp_LogComment(char *comment) {
	if (dx9imp_state.log_fp) {
		fprintf(dx9imp_state.log_fp, "%s", comment);
	}
}

static D3DGAMMARAMP s_oldHardwareGamma;
static bool gammaCalibrate = false;
void DX9imp_CheckHardwareGamma(void) {

	// non-3Dfx standalone drivers don't support gamma changes, period
	if (glConfig.driverType == GLDRV_STANDALONE) {
		return;
	}

	if (!r_ignorehwgamma->integer) {
		if (0 != (qdx.caps.Caps2 & D3DCAPS2_FULLSCREENGAMMA))
		{
			glConfig.deviceSupportsGamma = qtrue;
			if (0 != (qdx.caps.Caps2 & D3DCAPS2_CANCALIBRATEGAMMA))
			{
				gammaCalibrate = true;
			}

			qdx.device->GetGammaRamp(0, &s_oldHardwareGamma);

			if (glConfig.deviceSupportsGamma) {
				//
				// do a sanity check on the gamma values
				//
				if ((s_oldHardwareGamma.red[255] <= s_oldHardwareGamma.red[0]) ||
					(s_oldHardwareGamma.green[255] <= s_oldHardwareGamma.green[0]) ||
					(s_oldHardwareGamma.blue[255] <= s_oldHardwareGamma.blue[0])) {
					glConfig.deviceSupportsGamma = qfalse;
					ri.Printf(PRINT_WARNING, "WARNING: device has broken gamma support, generated gamma.dat\n");
				}

				//
				// make sure that we didn't have a prior crash in the game, and if so we need to
				// restore the gamma values to at least a linear value
				//
				if ((s_oldHardwareGamma.red[181] == 255)) {
					int g;

					ri.Printf(PRINT_WARNING, "WARNING: suspicious gamma tables, using linear ramp for restoration\n");

					for (g = 0; g < 255; g++)
					{
						s_oldHardwareGamma.red[g] = g;
						s_oldHardwareGamma.green[g] = g;
						s_oldHardwareGamma.blue[g] = g;
					}
				}
			}
		}
	}
}
#undef HIBYTE

void DX9imp_SetGamma(unsigned char red[256], unsigned char green[256], unsigned char blue[256]) {
	D3DGAMMARAMP table;
	int i;

	if (!glConfig.deviceSupportsGamma || r_ignorehwgamma->integer) {
		return;
	}

	//mapGammaMax();

	for (i = 0; i < 256; i++) {
		table.red[i] = /*(((unsigned short)red[i]) << 8) |*/ red[i];
		table.green[i] = /*(((unsigned short)green[i]) << 8) |*/ green[i];
		table.blue[i] = /*(((unsigned short)blue[i]) << 8) |*/ blue[i];
	}

#if 0
	// Win2K puts this odd restriction on gamma ramps...
	OSVERSIONINFO vinfo;
	vinfo.dwOSVersionInfoSize = sizeof(vinfo);
	GetVersionEx(&vinfo);
	if (vinfo.dwMajorVersion == 5 && vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT) {
		Com_DPrintf("performing W2K gamma clamp.\n");
		for (j = 0; j < 3; j++) {
			for (i = 0; i < 128; i++) {
				if (table[j][i] > ((128 + i) << 8)) {
					table[j][i] = (128 + i) << 8;
				}
			}
			if (table[j][127] > 254 << 8) {
				table[j][127] = 254 << 8;
			}
		}
	}
	else {
		Com_DPrintf("skipping W2K gamma clamp.\n");
	}
#endif

	// enforce constantly increasing
	for (i = 1; i < 256; i++) {
		if (table.red[i] < table.red[i - 1]) {
			table.red[i] = table.red[i - 1];
		}
		if (table.green[i] < table.green[i - 1]) {
			table.green[i] = table.green[i - 1];
		}
		if (table.blue[i] < table.blue[i - 1]) {
			table.blue[i] = table.blue[i - 1];
		}
	}

	qdx.device->SetGammaRamp(0, gammaCalibrate ? D3DSGR_CALIBRATE : D3DSGR_NO_CALIBRATION, &table);
}

void DX9imp_RestoreGamma(void) {
	if (glConfig.deviceSupportsGamma) {
		if (qdx.device)
		{
			qdx.device->SetGammaRamp(0, gammaCalibrate ? D3DSGR_CALIBRATE : D3DSGR_NO_CALIBRATION, &s_oldHardwareGamma);
		}
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
	UINT b = 0;

	switch (in)
	{
	case GL_RGBA8:
		ret = D3DFMT_A8R8G8B8;
		b = 32;
		break;
	case GL_RGBA4:
		ret = D3DFMT_A4R4G4B4;
		b = 16;
		break;
	case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
		//todo: compress texture
		//ret = D3DFMT_DXT5;
		//b = 8;
		ret = D3DFMT_A8R8G8B8;
		b = 32;
		break;
	case GL_RGB8:
		ret = D3DFMT_X8R8G8B8;
		b = 32;
		break;
	case GL_RGB5:
		ret = D3DFMT_R5G6B5;
		b = 16;
		break;
	case 3:
		ret = D3DFMT_X8R8G8B8;
		b = 24;
		break;
	case 4:
		ret = D3DFMT_A8R8G8B8;
		b = 32;
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
			for (int i = 0; i < elems; i++, out++, in+=4)
			{
				*out = D3DCOLOR_ARGB(in[3], in[0], in[1], in[2]);
			}
		}
		if (dxfmt == D3DFMT_X8R8G8B8)
		{
			uint32_t *out = (uint32_t*)lr.pBits;
			const byte *in = (const byte*)data;
			for (int i = 0; i < elems; i++, out++, in+=4)
			{
				*out = D3DCOLOR_XRGB(in[0], in[1], in[2]);
			}
		}
		else if (dxfmt == D3DFMT_A4R4G4B4)
		{
			uint16_t *out = (uint16_t*)lr.pBits;
			const byte *in = (const byte*)data;
			for (int i = 0; i < elems; i++, out++, in+=4)
			{
				UINT r = (in[0] * 15 / 255) << 8;
				UINT g = (in[1] * 15 / 255) << 4;
				UINT b = (in[2] * 15 / 255) << 0;
				UINT a = (in[3] * 15 / 255) << 12;
				*out = ( a | r | g | b);
			}
		}
		else if (dxfmt == D3DFMT_R5G6B5)
		{
			uint16_t *out = (uint16_t*)lr.pBits;
			const byte *in = (const byte*)data;
			for (int i = 0; i < elems; i++, out++, in+=4)
			{
				UINT r = (in[0] * 31 / 255) << 11;
				UINT g = (in[1] * 63 / 255) << 5;
				UINT b = (in[2] * 31 / 255) << 0;
				*out = (r | g | b);
			}
		}
		else
		{
			//todo DXT5 encoder
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
			textures[id] = g_cleartex;
		}
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
	else
	{
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

	return ret;
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
}

void qdx_texobj_apply(int id, int sampler)
{
	qdx_textureobj_t opt = qdx_texobj_get(id);
	int sampler_id = sampler >= 0 ? sampler : opt.sampler;

	if (qdx.device)
	{
		qdx.device->SetTexture(sampler, opt.obj);
		qdx.device->SetSamplerState(sampler, D3DSAMP_MINFILTER, opt.flt_min);
		qdx.device->SetSamplerState(sampler, D3DSAMP_MIPFILTER, opt.flt_mip);
		qdx.device->SetSamplerState(sampler, D3DSAMP_MAGFILTER, opt.flt_mag);
		qdx.device->SetSamplerState(sampler, D3DSAMP_MAXANISOTROPY, opt.anisotropy);
		qdx.device->SetSamplerState(sampler, D3DSAMP_ADDRESSU, opt.wrap_u);
		qdx.device->SetSamplerState(sampler, D3DSAMP_ADDRESSV, opt.wrap_v);
		qdx.device->SetSamplerState(sampler, D3DSAMP_BORDERCOLOR, opt.border);
	}
}

struct fvf_state
{
	UINT bits;
	void *vertexes;
	void *normals;
	void *colors;
	void *texcoord[2];
} g_fvfs = { 0 };

void qdx_fvf_enable(int bits)
{
	g_fvfs.bits |= bits;
}

void qdx_fvf_disable(int bits)
{
	g_fvfs.bits &= ~bits;
}

void qdx_fvf_assemble()
{
	switch (g_fvfs.bits)
	{
	case FVF_VERTCOL:
		break;
	}
}

#define ON_FAIL_RET_NULL(X) if(FAILED(X)) { return NULL; }
#define ON_FAIL_RETURN(X) if(FAILED(X)) { return; }

qdx_vbuffer_t qdx_vbuffer_upload(UINT fvfid, UINT size, void *data)
{
	LPDIRECT3DVERTEXBUFFER9 v_buffer;

	ON_FAIL_RET_NULL(
	qdx.device->CreateVertexBuffer(size,
		0,
		fvfid,
		D3DPOOL_MANAGED,
		&v_buffer,
		NULL));

	VOID* pVoid;

	ON_FAIL_RET_NULL(v_buffer->Lock(0, 0, (void**)&pVoid, 0));
	memcpy(pVoid, data, size);
	v_buffer->Unlock();

	return v_buffer;
}

void qdx_vbuffer_release(qdx_vbuffer_t buf)
{
	buf->Release();
}