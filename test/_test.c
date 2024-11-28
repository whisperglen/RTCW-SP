// test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <stdio.h>
#include <stdlib.h>

static int tests_total = 0;
static int tests_ok = 0;

extern void test_rsqrt();
extern void test_matrix();
extern void test_buffers();
extern void maintest_snapvector();
extern void maintest_boxonplaneside();

int main()
{
    printf("Hello Test!\n\n");

	test_rsqrt();
	//test_matrix();
	test_buffers();
	maintest_snapvector();
	maintest_boxonplaneside();

	printf("Tests results: %d/%d\n", tests_ok, tests_total);
	system("pause");
}

void xassert_str(int success, const char* expression, const char* function, unsigned line, const char* file)
{
	tests_total++;
	if (success) tests_ok++;
	else
	{
		const char* fn = strrchr(file, '\\');
		if (!fn) fn = strrchr(file, '/');
		if (!fn) fn = "fnf";

		printf("assert failed: %s in %s:%d %s\n", expression, function, line, fn);
	}
}

void xassert_int(int success, int printval, const char* function, unsigned line, const char* file)
{
	tests_total++;
	if (success) tests_ok++;
	else
	{
		const char* fn = strrchr(file, '\\');
		if (!fn) fn = strrchr(file, '/');
		if (!fn) fn = "fnf";

		printf("assert failed: 0x%x in %s:%d %s\n", printval, function, line, fn);
	}
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
