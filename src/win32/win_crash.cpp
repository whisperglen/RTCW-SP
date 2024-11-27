
#include <stdio.h>
#include "StackWalker/Main/StackWalker/StackWalker.h"

class MyStackWalker : public StackWalker
{
	FILE *fout;
public:
	MyStackWalker() : StackWalker()
	{
		SYSTEMTIME now;
		ZeroMemory(&now, sizeof(now));
		GetLocalTime(&now);
		char fname[256];
		snprintf(fname, sizeof(fname), "Crash_%.4d-%.2d-%.2d_%.2d-%.2d-%.2d.txt", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
		fout = fopen(fname, "wt");
	}
	virtual ~MyStackWalker()
	{
		if (fout)
		{
			fclose(fout);
		}
	}
protected:
	virtual void OnOutput(LPCSTR szText) {
		//printf(szText); StackWalker::OnOutput(szText);
		if (fout)
		{
			fprintf(fout, "%s", szText);
			fflush(fout);
		}
	}
};

extern "C" LONG WINAPI ExpFilter(EXCEPTION_POINTERS* pExp, DWORD dwExpCode)
{
	MyStackWalker sw;
	sw.ShowCallstack(GetCurrentThread(), pExp->ContextRecord);
	return EXCEPTION_EXECUTE_HANDLER;
	//return EXCEPTION_CONTINUE_SEARCH;
}