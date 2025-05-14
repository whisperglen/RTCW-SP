
#include <stdio.h>
#include <stdlib.h>

extern "C" void test_gamevals()
{
	printf("\n----- test gamevals\n");

	{
		float radius = 30;
		float scale = 1.0f / radius;

		for (float dist = 0; dist <= radius; dist += 2.0f)
		{
			float mod = 2.0f * (radius - dist) * scale;
			printf(" %2.2f", mod);
		}
		printf("\n");
		for (float dist = 0; dist <= radius; dist += 2.0f)
		{
			float mod = 1.0f / (1.0f + scale * dist + 0.01f * dist * dist);
			//float mod = 1.0f / (1.0 + scale  * dist);
			//float mod = 1.0f / (0.5f + scale * scale * dist * dist);
			printf(" %2.2f", mod);
		}
		printf("\n");
	}

#define SHORT2ANGLE( x )  ( ( x ) * ( 360.0 / 65536 ) )
	
	for (unsigned short i = 0; i < 256; i++)
	{
		printf("%f %f\n", SHORT2ANGLE(i), (float)i * (360.0 / 65536));
	}
	printf("\n");

	printf("----- test gamevals end\n\n");
}