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

typedef struct cplane_s {
	vec3_t normal;
	float dist;
	byte type;              // for fast side tests: 0,1,2 = axial, 3 = nonaxial
	byte signbits;          // signx + (signy<<1) + (signz<<2), used as lookup during collision
	byte pad[2];
} cplane_t;

int BoxOnPlaneSide(vec3_t emins, vec3_t emaxs, struct cplane_s *p) {
	float dist1, dist2;
	int sides;

	// fast axial cases
	if (p->type < 3) {
		if (p->dist <= emins[p->type]) {
			return 1;
		}
		if (p->dist >= emaxs[p->type]) {
			return 2;
		}
		return 3;
	}

	// general case
	switch (p->signbits)
	{
	case 0:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		break;
	case 1:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		break;
	case 2:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		break;
	case 3:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		break;
	case 4:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		break;
	case 5:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		break;
	case 6:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		break;
	case 7:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		break;
	default:
		dist1 = dist2 = 0;      // shut up compiler
		break;
	}

	sides = 0;
	if (dist1 >= p->dist) {
		sides = 1;
	}
	if (dist2 < p->dist) {
		sides |= 2;
	}

	return sides;
}

#if id386
__declspec( naked ) int BoxOnPlaneSide_asm( vec3_t emins, vec3_t emaxs, struct cplane_s *p ) {
	static int bops_initialized;
	static int Ljmptab[8];

	__asm {

		push ebx

		cmp bops_initialized, 1
		je initialized
		mov bops_initialized, 1

		mov Ljmptab[0 * 4], offset Lcase0
		mov Ljmptab[1 * 4], offset Lcase1
		mov Ljmptab[2 * 4], offset Lcase2
		mov Ljmptab[3 * 4], offset Lcase3
		mov Ljmptab[4 * 4], offset Lcase4
		mov Ljmptab[5 * 4], offset Lcase5
		mov Ljmptab[6 * 4], offset Lcase6
		mov Ljmptab[7 * 4], offset Lcase7

initialized:

		mov edx,dword ptr[4 + 12 + esp]
		mov ecx,dword ptr[4 + 4 + esp]
		xor eax,eax
		mov ebx,dword ptr[4 + 8 + esp]
		mov al,byte ptr[17 + edx]
		cmp al,8
		jge Lerror
		fld dword ptr[0 + edx]
		fld st( 0 )
		jmp dword ptr[Ljmptab + eax * 4]
		Lcase0 :
			fmul dword ptr[ebx]
			fld dword ptr[0 + 4 + edx]
			fxch st( 2 )
			fmul dword ptr[ecx]
			fxch st( 2 )
			fld st( 0 )
			fmul dword ptr[4 + ebx]
			fld dword ptr[0 + 8 + edx]
			fxch st( 2 )
			fmul dword ptr[4 + ecx]
			fxch st( 2 )
			fld st( 0 )
			fmul dword ptr[8 + ebx]
			fxch st( 5 )
			faddp st( 3 ),st( 0 )
			fmul dword ptr[8 + ecx]
			fxch st( 1 )
			faddp st( 3 ),st( 0 )
			fxch st( 3 )
			faddp st( 2 ),st( 0 )
			jmp LSetSides
			Lcase1 :
				fmul dword ptr[ecx]
				fld dword ptr[0 + 4 + edx]
				fxch st( 2 )
				fmul dword ptr[ebx]
				fxch st( 2 )
				fld st( 0 )
				fmul dword ptr[4 + ebx]
				fld dword ptr[0 + 8 + edx]
				fxch st( 2 )
				fmul dword ptr[4 + ecx]
				fxch st( 2 )
				fld st( 0 )
				fmul dword ptr[8 + ebx]
				fxch st( 5 )
				faddp st( 3 ),st( 0 )
				fmul dword ptr[8 + ecx]
				fxch st( 1 )
				faddp st( 3 ),st( 0 )
				fxch st( 3 )
				faddp st( 2 ),st( 0 )
				jmp LSetSides
				Lcase2 :
					fmul dword ptr[ebx]
					fld dword ptr[0 + 4 + edx]
					fxch st( 2 )
					fmul dword ptr[ecx]
					fxch st( 2 )
					fld st( 0 )
					fmul dword ptr[4 + ecx]
					fld dword ptr[0 + 8 + edx]
					fxch st( 2 )
					fmul dword ptr[4 + ebx]
					fxch st( 2 )
					fld st( 0 )
					fmul dword ptr[8 + ebx]
					fxch st( 5 )
					faddp st( 3 ),st( 0 )
					fmul dword ptr[8 + ecx]
					fxch st( 1 )
					faddp st( 3 ),st( 0 )
					fxch st( 3 )
					faddp st( 2 ),st( 0 )
					jmp LSetSides
					Lcase3 :
						fmul dword ptr[ecx]
						fld dword ptr[0 + 4 + edx]
						fxch st( 2 )
						fmul dword ptr[ebx]
						fxch st( 2 )
						fld st( 0 )
						fmul dword ptr[4 + ecx]
						fld dword ptr[0 + 8 + edx]
						fxch st( 2 )
						fmul dword ptr[4 + ebx]
						fxch st( 2 )
						fld st( 0 )
						fmul dword ptr[8 + ebx]
						fxch st( 5 )
						faddp st( 3 ),st( 0 )
						fmul dword ptr[8 + ecx]
						fxch st( 1 )
						faddp st( 3 ),st( 0 )
						fxch st( 3 )
						faddp st( 2 ),st( 0 )
						jmp LSetSides
						Lcase4 :
							fmul dword ptr[ebx]
							fld dword ptr[0 + 4 + edx]
							fxch st( 2 )
							fmul dword ptr[ecx]
							fxch st( 2 )
							fld st( 0 )
							fmul dword ptr[4 + ebx]
							fld dword ptr[0 + 8 + edx]
							fxch st( 2 )
							fmul dword ptr[4 + ecx]
							fxch st( 2 )
							fld st( 0 )
							fmul dword ptr[8 + ecx]
							fxch st( 5 )
							faddp st( 3 ),st( 0 )
							fmul dword ptr[8 + ebx]
							fxch st( 1 )
							faddp st( 3 ),st( 0 )
							fxch st( 3 )
							faddp st( 2 ),st( 0 )
							jmp LSetSides
							Lcase5 :
								fmul dword ptr[ecx]
								fld dword ptr[0 + 4 + edx]
								fxch st( 2 )
								fmul dword ptr[ebx]
								fxch st( 2 )
								fld st( 0 )
								fmul dword ptr[4 + ebx]
								fld dword ptr[0 + 8 + edx]
								fxch st( 2 )
								fmul dword ptr[4 + ecx]
								fxch st( 2 )
								fld st( 0 )
								fmul dword ptr[8 + ecx]
								fxch st( 5 )
								faddp st( 3 ),st( 0 )
								fmul dword ptr[8 + ebx]
								fxch st( 1 )
								faddp st( 3 ),st( 0 )
								fxch st( 3 )
								faddp st( 2 ),st( 0 )
								jmp LSetSides
								Lcase6 :
									fmul dword ptr[ebx]
									fld dword ptr[0 + 4 + edx]
									fxch st( 2 )
									fmul dword ptr[ecx]
									fxch st( 2 )
									fld st( 0 )
									fmul dword ptr[4 + ecx]
									fld dword ptr[0 + 8 + edx]
									fxch st( 2 )
									fmul dword ptr[4 + ebx]
									fxch st( 2 )
									fld st( 0 )
									fmul dword ptr[8 + ecx]
									fxch st( 5 )
									faddp st( 3 ),st( 0 )
									fmul dword ptr[8 + ebx]
									fxch st( 1 )
									faddp st( 3 ),st( 0 )
									fxch st( 3 )
									faddp st( 2 ),st( 0 )
									jmp LSetSides
									Lcase7 :
										fmul dword ptr[ecx]
										fld dword ptr[0 + 4 + edx]
										fxch st( 2 )
										fmul dword ptr[ebx]
										fxch st( 2 )
										fld st( 0 )
										fmul dword ptr[4 + ecx]
										fld dword ptr[0 + 8 + edx]
										fxch st( 2 )
										fmul dword ptr[4 + ebx]
										fxch st( 2 )
										fld st( 0 )
										fmul dword ptr[8 + ecx]
										fxch st( 5 )
										faddp st( 3 ),st( 0 )
										fmul dword ptr[8 + ebx]
										fxch st( 1 )
										faddp st( 3 ),st( 0 )
										fxch st( 3 )
										faddp st( 2 ),st( 0 )
										LSetSides :
											faddp st( 2 ),st( 0 )
											fcomp dword ptr[12 + edx]
											xor ecx,ecx
											fnstsw ax
											fcomp dword ptr[12 + edx]
											and ah,1
											xor ah,1
											add cl,ah
											fnstsw ax
											and ah,1
											add ah,ah
											add cl,ah
											pop ebx
											mov eax,ecx
											ret
											Lerror :
												int 3
	}
}
#endif

void maintest_boxonplaneside()
{

}