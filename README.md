# What is this?

I wanted to use rtx-remix with this game, but it was really glitchy using an OpenGL wrapper.  

Since the sources are available I decided to add a DX9 adapter, and then maybe, I could also add a camera, lights and modify the geometry to get rid of said glitches.  

Will it work? Who knows, let's find out!  

### WIP:  
- translate OGL calls to DX9: 90% (some debug drawcalls still remain)
- add camera: 100%
- tinker with the geometry to get stable hashes: 70% (we have stable hashes with r_novis, need optimisation)
- convert dynamic lights to T&L: 90% (dynamic lights [torches, fireplaces, flamethrower] are sent via DX9 lights [max. 8], coronas are sent as Remix sphere lights)

### Notable engine cvars:
- r_rmx_flashlight (default "0") activates a flashlight ingame; Usage: bind v "toggle r_rmx_flashlight"
- r_rmx_coronas (default "1") send coronas as remix lights
- r_rmx_dynamiclight (default "1") send torches/muzzleflash/fireplace flicker to remix (DX9 Lights atm, maybe remix light in the future)
- r_flares (default "1"): ~~set to "4" to convert flares into remix sphere lights~~ should be "0", superseeded by r_rmx_coronas
- cg_coronas (default "1"): set to "2" to send all coronas to renderer -since coronas are converted into remix lights, this avoids the pop-in effect
- cg_lightstyle_ms (default "100"): strobing light frametime (100ms is 10fps); I have set this to 200 (5fps) since I find the flashing effect annoying
- r_dynamiclight (default "1"): ~~keep on 1 to send dynamic light sources (torches, fireplaces, muzzle flashes) to remix as light sources (idtech3 renders them in software)~~ should be "0", superseeded by r_rmx_dynamiclight
- r_nocull (default "0"): set to "1" so that geometry in player proximity, which is not in camera-view, is still drawn -this prevents shadows from buildings to dissapear
- r_novis (default "0"): set to "1" so that all map surfaces(triangles) are drawn -this helps to stabilize the geometry hashes in remix
- r_wolffog (default "1"): set to "0" to disable ingame fog (there may still be geometry with fog texture applied, those will not be disabled)
- r_nomipmaps (default "0"): set to "1" to disable mipmap creation for all textures
- r_swapInterval (default "1"): controls VSYNC; "1" is VSYNC-ON, "0" is VSYNC-OFF
- r_multisample (default "4"): controls multisample antialiasing
- r_ext_texture_filter_anisotropic (default "0"): was disabled in OGL mode, it can now be set to 1,2,4,8,16 with Texture filter "trilinear" (r_textureMode "GL_LINEAR_MIPMAP_LINEAR") to allow anisotropy for texture minification filter (when textured object gets farther/smaller)
- r_systemdll (default "0"): set only on command line; a value of "1" loads the system's d3d9.dll, and ignores any d3d9.dll present in the executable's current dir

New console commands: increment/decrement can be used for integer cvars, pause will pause and unpause the rendering<br/>
ImGui can be used to change some game configs; activate with: toggle r_showimgui / Alt+C / F8

### Included in this project:
- Widescreen support: full credit goes to RealRTCW [https://github.com/wolfetplayer/RealRTCW] and Quake3e [https://github.com/ec-/Quake3e]
- The StackWalker [https://github.com/JochenKalmbach/StackWalker] project to print crash info (BSD-2-Clause license)
- part of Microsoft DirectXTex [https://github.com/microsoft/DirectXTex] for DXT5 texture compression (MIT license)
- FNV hash [http://isthe.com/chongo/tech/comp/fnv/] (CC0 1.0 Public Domain license)
- mINI library [https://github.com/metayeti/mINI] (MIT license)
- Dear ImGui [https://github.com/ocornut/imgui] (MIT license)

### *Original README text follows:*

Return to Castle Wolfenstein single player GPL source release
=============================================================

This file contains the following sections:

GENERAL NOTES
LICENSE

GENERAL NOTES
=============

Game data and patching:
-----------------------

This source release does not contain any game data, the game data is still
covered by the original EULA and must be obeyed as usual.

You must patch the game to the latest version.

Note that RTCW is available from the Steam store at
http://store.steampowered.com/app/9010/

Linux note: due to the game CD containing only a Windows version of the game,
you must install and update the game using WINE to get the game data.

Compiling on win32:
-------------------

A Visual C++ 2008 project is provided in src\wolf.sln.
The solution file is compatible with the Express release of Visual C++.

You will need to execute src\extractfuncs\extractfuncs.bat to generate src\game\g_save.c

You can test your binaries by replacing WolfSP.exe, qagamex86.dll, cgamex86.dll, uix86.dll at the top of the RTCW install

Compiling on GNU/Linux x86:
---------------------------

Go to the src/unix directory, and run the cons script
(cons is a perl based precursor to scons, this is what we were using at the time)

Run ./cons -h to review build options. Use ./cons -- release to compile in release mode.

If problems occur, consult the internet.

Other platforms, updated source code, security issues:
------------------------------------------------------

If you have obtained this source code several weeks after the time of release
(August 2010), it is likely that you can find modified and improved
versions of the engine in various open source projects across the internet.
Depending what is your interest with the source code, those may be a better
starting point.


LICENSE
=======

See COPYING.txt for the GNU GENERAL PUBLIC LICENSE

ADDITIONAL TERMS:  The Return to Castle Wolfenstein single player GPL Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU GPL which accompanied the RTCW SP Source Code.  If not, please request a copy in writing from id Software at id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

EXCLUDED CODE:  The code described below and contained in the Return to Castle Wolfenstein single player GPL Source Code release is not part of the Program covered by the GPL and is expressly excluded from its terms.  You are solely responsible for obtaining from the copyright holder a license for such code and complying with the applicable license terms.

IO on .zip files using portions of zlib
---------------------------------------------------------------------------
lines	file(s)
4301	src/qcommon/unzip.c
Copyright (C) 1998 Gilles Vollant
zlib is Copyright (C) 1995-1998 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

MD4 Message-Digest Algorithm
-----------------------------------------------------------------------------
lines   file(s)
289     src/qcommon/md4.c
Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All rights reserved.

License to copy and use this software is granted provided that it is identified
as the <93>RSA Data Security, Inc. MD4 Message-Digest Algorithm<94> in all mater
ial mentioning or referencing this software or this function.
License is also granted to make and use derivative works provided that such work
s are identified as <93>derived from the RSA Data Security, Inc. MD4 Message-Dig
est Algorithm<94> in all material mentioning or referencing the derived work.
RSA Data Security, Inc. makes no representations concerning either the merchanta
bility of this software or the suitability of this software for any particular p
urpose. It is provided <93>as is<94> without express or implied warranty of any
kind.

JPEG library
-----------------------------------------------------------------------------
src/jpeg-6
Copyright (C) 1991-1995, Thomas G. Lane

Permission is hereby granted to use, copy, modify, and distribute this
software (or portions thereof) for any purpose, without fee, subject to these
conditions:
(1) If any part of the source code for this software is distributed, then this
README file must be included, with this copyright and no-warranty notice
unaltered; and any additions, deletions, or changes to the original files
must be clearly indicated in accompanying documentation.
(2) If only executable code is distributed, then the accompanying
documentation must state that "this software is based in part on the work of
the Independent JPEG Group".
(3) Permission for use of this software is granted only if the user accepts
full responsibility for any undesirable consequences; the authors accept
NO LIABILITY for damages of any kind.

These conditions apply to any software derived from or based on the IJG code,
not just to the unmodified library.  If you use our work, you ought to
acknowledge us.

NOTE: unfortunately the README that came with our copy of the library has
been lost, so the one from release 6b is included instead. There are a few
'glue type' modifications to the library to make it easier to use from
the engine, but otherwise the dependency can be easily cleaned up to a
better release of the library.

