#include <math.h>
#include <float.h>
#include <cassert>
#include <stdio.h>

#include "bdmpx.h"
#include "timing.h"
#include "platform.h"
#include "csv.h"
#include "intrin.h"

typedef float vec_t;
typedef vec_t vec2_t[2];
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];

typedef enum { qfalse = 0, qtrue } qboolean;

void Com_Memcpy(void* dest, const void* src, const size_t count) {
	memcpy(dest, src, count);
}

void Com_Memset(void* dest, const int val, const size_t count) {
	memset(dest, val, count);
}

typedef enum
{
	PRE_READ,                                   // prefetch assuming that buffer is used for reading only
	PRE_WRITE,                                  // prefetch assuming that buffer is used for writing only
	PRE_READ_WRITE                              // prefetch assuming that buffer is used for both reading and writing
} e_prefetch;

static void Com_Prefetch( const void *s, const unsigned int bytes, e_prefetch type );

#if id386
#define EMMS_INSTRUCTION    __asm emms

static void _copyDWord( unsigned int* dest, const unsigned int constant, const unsigned int count ) {
	__asm
	{
		mov edx,dest
		mov eax,constant
		mov ecx,count
		and     ecx,~7
		jz padding
		sub ecx,8
		jmp loopu
		align   16
loopu:
		test    [edx + ecx * 4 + 28],ebx        // fetch next block destination to L1 cache
		mov     [edx + ecx * 4 + 0],eax
		mov     [edx + ecx * 4 + 4],eax
		mov     [edx + ecx * 4 + 8],eax
		mov     [edx + ecx * 4 + 12],eax
		mov     [edx + ecx * 4 + 16],eax
		mov     [edx + ecx * 4 + 20],eax
		mov     [edx + ecx * 4 + 24],eax
		mov     [edx + ecx * 4 + 28],eax
		sub ecx,8
		jge loopu
padding:    mov ecx,count
		mov ebx,ecx
		and     ecx,7
		jz outta
		and     ebx,~7
		lea edx,[edx + ebx * 4]                 // advance dest pointer
		test    [edx + 0],eax                   // fetch destination to L1 cache
		cmp ecx,4
		jl skip4
		mov     [edx + 0],eax
		mov     [edx + 4],eax
		mov     [edx + 8],eax
		mov     [edx + 12],eax
		add edx,16
		sub ecx,4
skip4:      cmp ecx,2
		jl skip2
		mov     [edx + 0],eax
		mov     [edx + 4],eax
		add edx,8
		sub ecx,2
skip2:      cmp ecx,1
		jl outta
		mov     [edx + 0],eax
outta:
	}
}

// optimized memory copy routine that handles all alignment
// cases and block sizes efficiently
void Com_Memcpy_asm( void* dest, const void* src, const size_t count ) {
	Com_Prefetch( src, count, PRE_READ );
	__asm
	{
		push edi
		push esi
		mov ecx,count
		cmp ecx,0                           // count = 0 check (just to be on the safe side)
		je outta
		mov edx,dest
		mov ebx,src
		cmp ecx,32                          // padding only?
		jl padding

		mov edi,ecx
		and     edi,~31                 // edi = count&~31
		sub edi,32

		align 16
loopMisAligned:
		mov eax,[ebx + edi + 0 + 0 * 8]
		mov esi,[ebx + edi + 4 + 0 * 8]
		mov     [edx + edi + 0 + 0 * 8],eax
		mov     [edx + edi + 4 + 0 * 8],esi
		mov eax,[ebx + edi + 0 + 1 * 8]
		mov esi,[ebx + edi + 4 + 1 * 8]
		mov     [edx + edi + 0 + 1 * 8],eax
		mov     [edx + edi + 4 + 1 * 8],esi
		mov eax,[ebx + edi + 0 + 2 * 8]
		mov esi,[ebx + edi + 4 + 2 * 8]
		mov     [edx + edi + 0 + 2 * 8],eax
		mov     [edx + edi + 4 + 2 * 8],esi
		mov eax,[ebx + edi + 0 + 3 * 8]
		mov esi,[ebx + edi + 4 + 3 * 8]
		mov     [edx + edi + 0 + 3 * 8],eax
		mov     [edx + edi + 4 + 3 * 8],esi
		sub edi,32
		jge loopMisAligned

		mov edi,ecx
		and     edi,~31
		add ebx,edi                     // increase src pointer
		add edx,edi                     // increase dst pointer
		and     ecx,31                  // new count
		jz outta                        // if count = 0, get outta here

padding:
		cmp ecx,16
		jl skip16
		mov eax,dword ptr [ebx]
		mov dword ptr [edx],eax
		mov eax,dword ptr [ebx + 4]
		mov dword ptr [edx + 4],eax
		mov eax,dword ptr [ebx + 8]
		mov dword ptr [edx + 8],eax
		mov eax,dword ptr [ebx + 12]
		mov dword ptr [edx + 12],eax
		sub ecx,16
		add ebx,16
		add edx,16
skip16:
		cmp ecx,8
		jl skip8
		mov eax,dword ptr [ebx]
		mov dword ptr [edx],eax
		mov eax,dword ptr [ebx + 4]
		sub ecx,8
		mov dword ptr [edx + 4],eax
		add ebx,8
		add edx,8
skip8:
		cmp ecx,4
		jl skip4
		mov eax,dword ptr [ebx]     // here 4-7 bytes
		add ebx,4
		sub ecx,4
		mov dword ptr [edx],eax
		add edx,4
skip4:                          // 0-3 remaining bytes
		cmp ecx,2
		jl skip2
		mov ax,word ptr [ebx]       // two bytes
		cmp ecx,3                   // less than 3?
		mov word ptr [edx],ax
		jl outta
		mov al,byte ptr [ebx + 2]   // last byte
		mov byte ptr [edx + 2],al
		jmp outta
skip2:
		cmp ecx,1
		jl outta
		mov al,byte ptr [ebx]
		mov byte ptr [edx],al
outta:
		pop esi
		pop edi
	}
}

void Com_Memset_asm( void* dest, const int val, const size_t count ) {
	unsigned int fillval;

	if ( count < 8 ) {
		__asm
		{
			mov edx,dest
			mov eax, val
			mov ah,al
			mov ebx,eax
			and     ebx, 0xffff
			shl eax,16
			add eax,ebx                 // eax now contains pattern
			mov ecx,count
			cmp ecx,4
			jl skip4
			mov     [edx],eax           // copy first dword
			add edx,4
			sub ecx,4
skip4:  cmp ecx,2
			jl skip2
			mov word ptr [edx],ax       // copy 2 bytes
			add edx,2
			sub ecx,2
skip2:  cmp ecx,0
			je skip1
			mov byte ptr [edx],al       // copy single byte
skip1:
		}
		return;
	}

	fillval = val;

	fillval = fillval | ( fillval << 8 );
	fillval = fillval | ( fillval << 16 );        // fill dword with 8-bit pattern

	_copyDWord( (unsigned int*)( dest ),fillval, count / 4 );

	__asm                                   // padding of 0-3 bytes
	{
		mov ecx,count
		mov eax,ecx
		and     ecx,3
		jz skipA
		and     eax,~3
		mov ebx,dest
		add ebx,eax
		mov eax,fillval
		cmp ecx,2
		jl skipB
		mov word ptr [ebx],ax
		cmp ecx,2
		je skipA
		mov byte ptr [ebx + 2],al
		jmp skipA
skipB:
		cmp ecx,0
		je skipA
		mov byte ptr [ebx],al
skipA:
	}
}

qboolean Com_Memcmp_asm( const void *src0, const void *src1, const unsigned int count ) {
	unsigned int i;
	// MMX version anyone?

	if ( count >= 16 ) {
		unsigned int *dw = (unsigned int*)( src0 );
		unsigned int *sw = (unsigned int*)( src1 );

		unsigned int nm2 = count / 16;
		for ( i = 0; i < nm2; i += 4 )
		{
			unsigned int tmp = ( dw[i + 0] - sw[i + 0] ) | ( dw[i + 1] - sw[i + 1] ) |
							   ( dw[i + 2] - sw[i + 2] ) | ( dw[i + 3] - sw[i + 3] );
			if ( tmp ) {
				return qfalse;
			}
		}
	}
	if ( count & 15 ) {
		byte *d = (byte*)src0;
		byte *s = (byte*)src1;
		for ( i = count & 0xfffffff0; i < count; i++ )
			if ( d[i] != s[i] ) {
				return qfalse;
			}
	}

	return qtrue;
}

void Com_Prefetch( const void *s, const unsigned int bytes, e_prefetch type ) {
	// write buffer prefetching is performed only if
	// the processor benefits from it. Read and read/write
	// prefetching is always performed.

	switch ( type )
	{
	case PRE_WRITE: break;
	case PRE_READ:
	case PRE_READ_WRITE:

		__asm
		{
			mov ebx,s
			mov ecx,bytes
			cmp ecx,4096                    // clamp to 4kB
			jle skipClamp
			mov ecx,4096
skipClamp:
			add ecx,0x1f
			shr ecx,5                       // number of cache lines
			jz skip
			jmp loopie

			align 16
loopie: test byte ptr [ebx],al
			add ebx,32
			dec ecx
			jnz loopie
skip:
		}

		break;
	}
}

#endif