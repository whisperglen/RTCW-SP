
#include <stdio.h>
#include <stdlib.h>
#include <search.h>

static int *base = 0;

static int qsort_compare( const void *arg1, const void *arg2 )
{
	int ret = 0;
	int* s1 = (int*)arg1;
	int* s2 = (int*)arg2;
	printf("cmp %d %d\n", s1 - base, s2 - base);
	if (*s1 > *s2)
	{
		ret = 1;
	}
	else if (*s1 < *s2)
	{
		ret = -1;
	}
	else //equals
	{
	}

	return ret;
}

static void qsortFast(
	void* base,
	unsigned num,
	unsigned width
)
{
	qsort(base, num, width, qsort_compare);
}

#define ARRAY_SIZE(X) (sizeof(X)/sizeof(X[0]))

extern "C" void test_sorting()
{

    printf("\n----- test sorting\n");

	int sort_ints[20];
	printf("first: %p last: %p\n", sort_ints, sort_ints + ARRAY_SIZE(sort_ints));
	for (int i = 0, j = 100; i < ARRAY_SIZE(sort_ints); i++, j--)
	{
		sort_ints[i] = j;
		printf("%d ", sort_ints[i]);
	}
	printf("\n");

	base = sort_ints;
	qsort(sort_ints, ARRAY_SIZE(sort_ints), sizeof(sort_ints[0]), qsort_compare);
	for (int i = 0, j = 100; i < ARRAY_SIZE(sort_ints); i++, j--)
	{
		printf("%d ", sort_ints[i]);
	}
	printf("\n");

    printf("----- test sorting end\n\n");
}