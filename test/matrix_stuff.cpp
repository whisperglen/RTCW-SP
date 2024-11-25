
#include <stdio.h>
#include <d3d9.h>
#include "..\src\renderer\dx9sdk\include\d3dx9.h"

#ifndef M_PI
#define M_PI        3.14159265358979323846f // matches value in gcc v2 math.h
#endif

int matrix_compare(const float *a, const float *b)
{
	int ret = 1;
	int diff = 0;

	for (int i = 0; i < 16; i++)
	{
		if (fabsf(a[i] - b[i]) > 0.001f)
		{
			diff++;
		}
	}

	return diff < 2;
}

float zero_sign(float val)
{
	if (fabsf(val) < 0.001f)
		return 0.0f;
	else
		return val;
}

void print_matrix(D3DXMATRIX &mat)
{
	for (int i = 0; i < 4; i++)
		printf("% 2.2f % 2.2f % 2.2f % 2.2f\n", zero_sign(mat.m[i][0]), zero_sign(mat.m[i][1]), zero_sign(mat.m[i][2]), zero_sign(mat.m[i][3]));
}

void print_matrix(const float *mat)
{
	for (int i = 0; i < 4; i++)
		printf("% 2.2f % 2.2f % 2.2f % 2.2f\n", zero_sign(mat[4 * i + 0]), zero_sign(mat[4 * i + 1]), zero_sign(mat[4 * i + 2]), zero_sign(mat[4 * i + 3]));
}


#define WIDTH 800
#define HEIGHT 600
#define ZNEAR 4.0f
#define ZFAR 2300.0f

void SetupProjection(void) {
	float xmin, xmax, ymin, ymax;
	float width, height, depth;
	float zNear, zFar;
	float fov_y = 34.0f;
	float fov_x = 37.0f;
	float projectionMatrix[16];
	
	//
	// set up projection matrix
	//
	zNear = ZNEAR;
	zFar = ZFAR;

	//fov_x = 90.f;
	width = WIDTH;
	height = HEIGHT;
	float x = width / tanf(fov_x / 360 * M_PI);
	fov_y = atan2f(height, x);
	fov_y = fov_y * 360 / M_PI;

	ymax = zNear * tanf(fov_y * M_PI / 360.0f);
	ymin = -ymax;

	xmax = zNear * tanf(fov_x * M_PI / 360.0f);
	xmin = -xmax;

	width = xmax - xmin;
	height = ymax - ymin;
	depth = zFar - zNear;

	projectionMatrix[0] = 2 * zNear / width;
	projectionMatrix[4] = 0;
	projectionMatrix[8] = (xmax + xmin) / width; // normally 0
	projectionMatrix[12] = 0;

	projectionMatrix[1] = 0;
	projectionMatrix[5] = 2 * zNear / height;
	projectionMatrix[9] = (ymax + ymin) / height;    // normally 0
	projectionMatrix[13] = 0;

	projectionMatrix[2] = 0;
	projectionMatrix[6] = 0;
	projectionMatrix[10] = -(zFar + zNear) / depth;
	projectionMatrix[14] = -2 * zFar * zNear / depth;

	projectionMatrix[3] = 0;
	projectionMatrix[7] = 0;
	projectionMatrix[11] = -1;
	projectionMatrix[15] = 0;

	printf("wolf gl proj\n");
	print_matrix(projectionMatrix);
	printf("\n");
	printf("% 2.2f % 2.2f\n\n", projectionMatrix[2 * 4 + 2], projectionMatrix[3 * 4 + 2]);

	D3DXMATRIX mproj, minv;
	D3DXMatrixPerspectiveFovRH(&mproj,
		D3DXToRadian(37),    // the horizontal field of view
		WIDTH / HEIGHT, // aspect ratio
		ZNEAR,    // the near view-plane
		ZFAR);    // the far view-plane
	printf("dx perspective fov rh\n");
	print_matrix(mproj);
	printf("\n");
	
	D3DXMatrixPerspectiveLH(&mproj,
		WIDTH,
		HEIGHT,
		ZNEAR,
		ZFAR);
	printf("dx perspective lh\n");
	print_matrix(mproj);
	printf("\n");

	D3DXMatrixPerspectiveOffCenterRH(&mproj,
		0, WIDTH,
		0, HEIGHT,
		ZNEAR, ZFAR);
	printf("dx perspective offc rh\n");
	print_matrix(mproj);
	printf("\n");
}

extern "C" void test_matrix()
{
	printf("\ntest matrix\n");

	//int mat4[4] = { 0,1,2,3 };
	//int mat2x2[2][2] = { 0,1,2,3 };

	//int *ptr = mat2x2;
	//for (int i = 0; i < 4; i++)
	//	printf("%d %d\n", mat4[i], ptr[i]);

	//from gl to dx
	//qglOrtho(0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, 1);
	//void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) {
	//D3DXMatrixOrthoOffCenterRH(&m, left, right, top, bottom, zNear, zFar);

	D3DXMATRIX mproj, scratch;

	//D3DXMatrixIdentity(&mproj);
	////print_matrix(mproj);
	////printf("\n");
	//D3DXMatrixOrthoOffCenterRH(&scratch, 0, WIDTH, 0, HEIGHT, 0, 1);
	//print_matrix(scratch);
	//printf("\n");
	//D3DXMatrixOrthoOffCenterRH(&scratch, 0, WIDTH, HEIGHT, 0, 0, 1);
	//print_matrix(scratch);
	//printf("\n");

	//SetupProjection();

	//D3DXMATRIX mprojrh, mprojlh, minv, mx, mc;
	//D3DXMatrixPerspectiveFovRH(&mprojrh,
	//	D3DXToRadian(90),    // the horizontal field of view
	//	WIDTH / HEIGHT, // aspect ratio
	//	1.0f,    // the near view-plane
	//	100.0f);    // the far view-plane
	//print_matrix(mprojrh);
	//printf("\n");

	//D3DXMatrixPerspectiveFovLH(&mprojlh,
	//	D3DXToRadian(90),    // the horizontal field of view
	//	WIDTH / HEIGHT, // aspect ratio
	//	1.0f,    // the near view-plane
	//	100.0f);    // the far view-plane
	//print_matrix(mprojlh);
	//printf("\n");

	//D3DXMatrixInverse(&minv, NULL, &mprojrh);

	//mx = minv * mprojlh;
	//print_matrix(mx);
	//printf("\n");

	//mc = mprojrh * mx;
	//print_matrix(mc);
	//printf("\n");

	//D3DXMATRIX mview;
	//D3DXMatrixLookAtRH(&mview,
	//&D3DXVECTOR3(0.0f, 0.0f, 0.0f),   // the camera position
	//&D3DXVECTOR3(0.0f, 0.0f, -1.0f),    // the look-at position
	//&D3DXVECTOR3(0.0f, 1.0f, 0.0f));    // the up direction
	//print_matrix(mview);
	//printf("\n");
	static float s_flipMatrix[16] = {
		// convert from our coordinate system (looking down X)
		// to OpenGL's coordinate system (looking down -Z)
		0, 0, -1, 0,
		-1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 0, 1
	};
	//print_matrix(s_flipMatrix);
	//printf("\n");

	D3DXMatrixRotationX(&mproj, D3DXToRadian(-90));
	//print_matrix(mproj);
	//printf("\n");

	D3DXMatrixRotationY(&scratch, D3DXToRadian(-90));

	mproj = mproj * scratch;
	print_matrix(mproj);
	//print_matrix(D3DXMATRIX(s_flipMatrix) * scratch);
	printf("\n");

	D3DXMatrixTranspose(&mproj, &mproj);
	print_matrix(mproj);
	printf("\n");

	//float s_flipMatrix2[16] = {
	//	// convert from our coordinate system (looking down X)
	//	// to DX's coordinate system (looking down Z)
	//	0, 0, 1, 0,
	//	-1, 0, 0, 0,
	//	0, 1, 0, 0,
	//	0, 0, 0, 1
	//};

	//int anglemod[] = { 0, 1, 2, 3 };

	//for(int rota = 0; rota < 3; rota++)
	//	for(int rotb = 0; rotb < 3; rotb++)
	//		for(int signa = 0; signa <= 3; signa++)
	//			for (int signb = 0; signb <= 3; signb++)
	//			{
	//				switch (rota)
	//				{
	//				case 0:
	//					D3DXMatrixRotationX(&mproj, D3DXToRadian(90 * anglemod[signa]));
	//					break;
	//				case 1:
	//					D3DXMatrixRotationY(&mproj, D3DXToRadian(90 * anglemod[signa]));
	//					break;
	//				case 2:
	//					D3DXMatrixRotationZ(&mproj, D3DXToRadian(90 * anglemod[signa]));
	//					break;
	//				}

	//				switch (rotb)
	//				{
	//				case 0:
	//					D3DXMatrixRotationX(&scratch, D3DXToRadian(90 * anglemod[signb]));
	//					break;
	//				case 1:
	//					D3DXMatrixRotationY(&scratch, D3DXToRadian(90 * anglemod[signb]));
	//					break;
	//				case 2:
	//					D3DXMatrixRotationZ(&scratch, D3DXToRadian(90 * anglemod[signb]));
	//					break;
	//				}

	//				if (matrix_compare(s_flipMatrix2, mproj * scratch))
	//				{
	//					printf("%d %d %d %d\n", rota, rotb, signa, signb);
	//					print_matrix(mproj * scratch);
	//					//return;
	//				}
	//			}

	printf("test matrix end\n");
}