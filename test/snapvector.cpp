
#define SnapVector( v ) {v[0] = float( (int)( v[0] ) ); v[1] = float( (int)( v[1] ) ); v[2] = float( (int)( v[2] ) );}
void Sys_SnapVector(float *v) {
	SnapVector(v);
}

#define myftol( x ) ( (int)( x ) )
long fastftol(float f) {
	return myftol(f);
}

long fastftol_asm(float f) {
	static int tmp;
	__asm fld f
	__asm fistp tmp
	__asm mov eax, tmp
}

void Sys_SnapVector_asm(float *v) {
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