
/**
* NOTE: the asm version is more precise because it also rounds the number toward the nearest int,
*  while the C version truncates the float
*/
#include <stdio.h>
#include <float.h>
#include <math.h>
#include <fenv.h>
#include <time.h>

#include "platform.h"
#include "bdmpx.h"
#include "timing.h"
#include "csv.h"
#include "intrin.h"

typedef float vec_t;
typedef vec_t vec2_t[2];
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];

#define SnapVector( v ) {v[0] = float( (int)( v[0] ) ); v[1] = float( (int)( v[1] ) ); v[2] = float( (int)( v[2] ) );}
#define SnapVector_rint( v ) {v[0] = rintf( ( v[0] ) ); v[1] = rintf( ( v[1] ) ); v[2] = rintf( ( v[2] ) );}

ID_INLINE void Sys_SnapVector(float *v) {
	SnapVector(v);
}

ID_INLINE void Sys_SnapVector_rint(float *v) {
	SnapVector_rint(v);
}

ID_INLINE void Sys_SnapVector_sse(float *v) {
	__m128 a, b, c;
	a = _mm_set1_ps(v[0]);
	b = _mm_set1_ps(v[1]);
	c = _mm_set1_ps(v[2]);
	v[0] = _mm_cvt_ss2si(a);
	v[1] = _mm_cvt_ss2si(b);
	v[2] = _mm_cvt_ss2si(c);
}

#define myftol( x ) ( (int)( x ) )
ID_INLINE long fastftol(float f) {
	return myftol(f);
}

ID_INLINE long fastftol_sse(float f) {
	__m128 a = _mm_set1_ps(f);
	return _mm_cvt_ss2si(a);
}

#if id386
ID_INLINE long fastftol_asm(float f) {
	static int tmp;
	__asm fld f
	__asm fistp tmp
	__asm mov eax, tmp
}

ID_INLINE void Sys_SnapVector_asm(float *v) {
	int i;
	float f;

	f = *v;
	__asm fld f;
	__asm fistp i;
	*v = i;
	v++;
	f = *v;
	__asm fld f;
	__asm fistp i;
	*v = i;
	v++;
	f = *v;
	__asm fld f;
	__asm fistp i;
	*v = i;
	/*
	*v = fastftol(*v);
	v++;
	*v = fastftol(*v);
	v++;
	*v = fastftol(*v);
	*/
}
#endif

#define FUNCTION_INLINED(NAME) \
void finline_##NAME(int reps, int inputsz, float *inputs, double *elapsed) \
{ \
	Timer timer; \
	for (int r = 0; r < reps; r++) \
		for (int i = 0; i < inputsz; i++) \
		{ \
			NAME(inputs + (i * 3)); \
		} \
	*elapsed = timer.elapsed_ms(); \
}
typedef void(*snapv_fn_inlined)(int reps, int inputsz, float *inputs, double* elapsed);
typedef void(*snapv_fn)(float *vector);

struct function_data
{
	void* fp;
	const char* fname;
};

static struct function_data data_fn[] =
{
	{ Sys_SnapVector,        "      snapv" },
	{ Sys_SnapVector_rint,   " snapv_rint" },
	{ Sys_SnapVector_sse,    "  snapv_sse" },
#if id386
	{ Sys_SnapVector_asm,    "  snapv_asm" },
#endif
	{ 0, 0 }
};

FUNCTION_INLINED(Sys_SnapVector);
FUNCTION_INLINED(Sys_SnapVector_rint);
FUNCTION_INLINED(Sys_SnapVector_sse);
#if id386
FUNCTION_INLINED(Sys_SnapVector_asm);
#endif

static struct function_data data_fn_inlined[] =
{
	{ finline_Sys_SnapVector,        "      inl_snapv" },
	{ finline_Sys_SnapVector_rint,   " inl_snapv_rint" },
	{ finline_Sys_SnapVector_sse,    "  inl_snapv_sse" },
#if id386
	{ finline_Sys_SnapVector_asm,    "  inl_snapv_asm" },
#endif
	{ 0, 0 }
};

#define ARRAY_SIZE(X) (sizeof(X)/sizeof(X[0]))

//#define TEST_SIZE 12 * 1000
#define TEST_SIZE 1000 * 1000

vec3_t input[5][TEST_SIZE];

C_ASSERT(ARRAY_SIZE(input) - 1 >= ARRAY_SIZE(data_fn_inlined) - 1);

#define MAX_ERRORS 10
#define REPETITIONS 1 //data is modified in place, the values are already truncated to int on repeat, although it may not matter
extern "C" void maintest_snapvector(void)
{
	int i, j, k, sz = 0;
	int tested = 0;
	int err = 0;
	int ercd = 0;
	double elapsed[ARRAY_SIZE(data_fn_inlined) - 1];

	//fesetround(FE_TOWARDZERO);
	//fesetround(FE_TONEAREST);
	
	ercd = csv_open("./results_snapvector.csv");
	if (ercd != 0)
	{
		printf("Could not open the csv file.\n\n");
	}

	//bdmpx_set_option(BDMPX_OPTION_PRECACHE_FILE_FOR_READ, 1);

	ercd = bdmpx_create(NULL, "bdmpx_snapvector.bin", BDMPX_OP_READ);
	if (ercd != 0)
	{
		printf("Could not open the test input file. Aborting.\n");
		return;
	}

	for (j = 0; j < TEST_SIZE; j++)
	{
		int vecrd = sizeof(vec3_t);
		if (1 != bdmpx_read(NULL, 1, &vecrd, &input[0][j]))
		{
			break;
		}
		assert(vecrd == sizeof(vec3_t));
	}
	printf("SNAPV Test file inputs %d (%d floats)\n", j, j*3);

#define TESTVAL_MODULO 1000000

	if (j < TEST_SIZE)
	{
		srand(time(NULL));
		for (; j < TEST_SIZE; j++)
		{
			input[0][j][0] = float(rand() % TESTVAL_MODULO) / 100;
			input[0][j][1] = float(rand() % TESTVAL_MODULO) / 100;
			input[0][j][2] = float(rand() % TESTVAL_MODULO) / 100;
		}
	}
	printf("SNAPV Total inputs (file + generated) %d (%d floats)\n", j, j*3);

	for (i = 1; i < ARRAY_SIZE(input); i++)
	{
		memcpy(input[i], input[0], sizeof(input[0]));
	}

	Timer timer;

	printf("\n>Force noinline results:\n");
	for (tested = 0; data_fn[tested].fp != 0; tested++)
	{
		snapv_fn callme = (snapv_fn)data_fn[tested].fp;
		const char* myinfo = data_fn[tested].fname;
		double elapsed;

		timer.reset();
		for (int r = 0; r < REPETITIONS; r++)
			for (i = 0; i < j; i++)
			{
				callme(input[tested + 1][i]);
			}
		elapsed = timer.elapsed_ms();
		printf("%d %s: %4.4f\n", tested, myinfo, elapsed);
		//csv_put_float(elapsed);
	}

	int truncation_errs;
	for (k = 1; k < tested + 1 && err < MAX_ERRORS; k++)
	{
		truncation_errs = 0;
		for (i = 0; i < j && err < MAX_ERRORS; i++)
		{
			for (int x = 0; x < 3; x++)
			{
				if (rintf(input[0][i][x]) != input[k][i][x])
				{
					if ((input[0][i][x] >= 0 && (rintf(input[0][i][x]) == (input[k][i][x] + 1.0f))) ||
						(input[0][i][x] < 0 && (rintf(input[0][i][x]) == (input[k][i][x] - 1.0f))))
					{
						//it's a truncation error
						truncation_errs++;
					}
					else
					{
						printf("id:%d samp:%d elem:%d\n orig:%4.4f here:%4.1f\n", k - 1, i, x,
							input[0][i][x], input[k][i][x]);
						err++;
					}
				}
			}
		}
		if (truncation_errs) printf("id:%d has %d truncation errors\n", k - 1, truncation_errs);
	}


	for (i = 1; i < ARRAY_SIZE(input); i++)
	{
		memcpy(input[i], input[0], sizeof(input[0]));
	}

	printf("\n>Inline results:\n");
	for (tested = 0; data_fn_inlined[tested].fp != 0; tested++)
	{
		snapv_fn_inlined callme = (snapv_fn_inlined)data_fn_inlined[tested].fp;
		const char* myinfo = data_fn_inlined[tested].fname;

		callme(REPETITIONS, j, input[tested + 1][0], &elapsed[tested]);
		printf("%d %s: %4.4f %4.4f\n", tested, myinfo, elapsed[tested], elapsed[tested] / elapsed[0]);
		csv_put_float(elapsed[tested]);
	}

	csv_put_string(",");
	for (k = 1; k < tested; k++)
		csv_put_float(elapsed[k] / elapsed[0]);

	csv_put_string("\n");
	csv_close();

	err = 0;
	for (k = 1; k < tested + 1 && err < MAX_ERRORS; k++)
	{
		truncation_errs = 0;
		for (i = 0; i < j && err < MAX_ERRORS; i++)
		{
			for (int x = 0; x < 3; x++)
			{
				if (rintf(input[0][i][x]) != input[k][i][x])
				{
					if ((input[0][i][x] >= 0 && (rintf(input[0][i][x]) == (input[k][i][x] + 1.0f))) ||
						(input[0][i][x] < 0 && (rintf(input[0][i][x]) == (input[k][i][x] - 1.0f))))
					{
						//it's a truncation error
						truncation_errs++;
					}
					else
					{
						printf("id:%d samp:%d elem:%d\n orig:%4.4f here:%4.1f\n", k - 1, i, x,
							input[0][i][x], input[k][i][x]);
						err++;
					}
				}
			}
		}
		if (truncation_errs) printf("id:%d has %d truncation errors\n", k - 1, truncation_errs);
	}

	printf("\n");
}