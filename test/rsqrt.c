
#include <stdio.h>
#include <math.h>
#include <float.h>

#include <intrin.h>

static int g_check_for_float_zero = 1;

static float Q_rsqrt_sse_precise(float number) {
	__m128 x_f, y_f;
	__m128 number_f;
	__m128 tmp0_f, tmp1_f;
	float ret;

	if (g_check_for_float_zero && number == 0.f)
		return 1.f / FLT_MIN;

	//number_f = _mm_load_ss(&number);
	number_f = _mm_set_ss(number);
	tmp0_f = _mm_set_ss(0.5f);
	tmp1_f = _mm_set_ss(1.5f);

	y_f = _mm_rsqrt_ss(number_f);

	x_f = _mm_mul_ss(tmp0_f, number_f);
	x_f = _mm_mul_ss(x_f, y_f);
	x_f = _mm_mul_ss(x_f, y_f);
	x_f = _mm_sub_ss(tmp1_f, x_f);
	x_f = _mm_mul_ss(x_f, y_f);

	_mm_store_ss(&ret, x_f);
	return ret;
}

static float Q_rsqrt_sse(float number)
{
	__m128 number_f;
	float ret;

	if (g_check_for_float_zero && number == 0.f)
		return 1.f / FLT_MIN;

	number_f = _mm_set_ss(number);

	number_f = _mm_rsqrt_ss(number_f);

	_mm_store_ss(&ret, number_f);
	return ret;
}

static float Q_rsqrt_math(float number)
{
	if (g_check_for_float_zero && number == 0.f)
		return 1.f / FLT_MIN;
	return 1.f / sqrtf(number);
}

float Q_rsqrt_q3( float number ) {
	long i;
	float x2, y;
	const float threehalfs = 1.5F;

	x2 = number * 0.5F;
	y  = number;
	i  = *( long * ) &y;                        // evil floating point bit level hacking
	i  = 0x5f3759df - ( i >> 1 );               // what the fuck?
	y  = *( float * ) &i;
	y  = y * ( threehalfs - ( x2 * y * y ) );   // 1st iteration
//	y  = y * ( threehalfs - ( x2 * y * y ) );   // 2nd iteration, this can be removed

	return y;
}

float Q_fabs( float f ) {
	int tmp = *( int * ) &f;
	tmp &= 0x7FFFFFFF;
	return *( float * ) &tmp;
}

//#define SQRTFAST( x ) ( 1.0f / Q_rsqrt( x ) )

// we know from the tests in perftesthelpers that the sse version is a bit more precise and faster
// than Q3 bitlevel hacking; even sse_precise is faster than Q3 version;
// so we are not going to test that; only some corner cases..
void test_rsqrt()
{
	printf("\n----- test rsqrt\n");

	g_check_for_float_zero = 1;
	printf("check for input == 0.0f\n");
	printf("sseprecise %g\n", Q_rsqrt_sse_precise(0.f));
	printf("sse %g\n", Q_rsqrt_sse(0.f));
	printf("math %g\n", Q_rsqrt_math(0.f));
	printf("q3 %g\n", Q_rsqrt_q3(0.f));

	g_check_for_float_zero = 0;
	printf("do not check input == 0.0f\n");
	printf("sseprecise %g\n", Q_rsqrt_sse_precise(0.f));
	printf("sse %g\n", Q_rsqrt_sse(0.f));
	printf("math %g\n", Q_rsqrt_math(0.f));
	printf("q3 %g\n", Q_rsqrt_q3(0.f));

	printf("----- test rsqrt end\n\n");
}