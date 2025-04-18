
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#include <tchar.h>

#define PRINT_THIS_FUNCTION_INFO() printf("\nfunc: %s\nFUNCTION: %s\nFUNCDNAME: %s\nFUNCSIG: %s\n\n", __func__, __FUNCTION__, __FUNCDNAME__, __FUNCSIG__)

extern "C" unsigned char print_this_public_fname(float a, size_t b, void* c, LPDIRECT3D9 d, int e)
{
    PRINT_THIS_FUNCTION_INFO();
    return 0;
}
extern "C" static unsigned char print_this_static_fname(float a, size_t b, void* c, LPDIRECT3D9 d, int e)
{
    PRINT_THIS_FUNCTION_INFO();
    return 0;
}

namespace check_thisNamespace
{
    void __stdcall hello_stdcal_function(int* a)
    {
        PRINT_THIS_FUNCTION_INFO();
    }
    void __fastcall hello_fastcal_function(int* a)
    {
        PRINT_THIS_FUNCTION_INFO();
    }
    void __cdecl hello_cdecl_function(int* a)
    {
        PRINT_THIS_FUNCTION_INFO();
    }
    void *hello_nocc_function(int* a)
    {
        PRINT_THIS_FUNCTION_INFO();
        return 0;
    }
}

LPCTSTR DXErrorString(HRESULT nErrorCode, LPTSTR outstr, WORD outsz )
{
    static TCHAR local_str[512] = { 0 };
    if (outstr == NULL || outsz == 0)
    {
        outstr = local_str;
        outsz = sizeof(local_str) / sizeof(TCHAR) -1;
    }

    if (0 == FormatMessage(FORMAT_MESSAGE_FROM_HMODULE, //FORMAT_MESSAGE_FROM_SYSTEM,
        GetModuleHandle("d3d9.dll"), //NULL,
        nErrorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
        (LPTSTR)outstr,
        outsz,
        NULL))
    {
        _stprintf_s(outstr, outsz, _T("%u"), HRESULT_CODE(nErrorCode));
    }
    return outstr;
}

extern "C" void test_errorcode()
{
    printf("\n----- test errorcode\n");

    IDirect3D9* pd = Direct3DCreate9(D3D_SDK_VERSION);

    printf("errorcode %d - %s\n", HRESULT_CODE(D3DERR_NOTFOUND), DXErrorString(D3DERR_NOTFOUND, NULL, 0));
    printf("errorcode %d - %s\n", HRESULT_CODE(D3DERR_DEVICELOST), DXErrorString(D3DERR_DEVICELOST, NULL, 0));
    printf("errorcode %d - %s\n", HRESULT_CODE(D3D_OK), DXErrorString(D3D_OK, NULL, 0));
    printf("errorcode %d - %s\n", HRESULT_CODE(D3DERR_DEVICEHUNG), DXErrorString(D3DERR_DEVICEHUNG, NULL, 0));

    pd->Release();

    print_this_public_fname(0, 0, 0, 0, 0);
    print_this_static_fname(0, 0, 0, 0, 0);

    check_thisNamespace::hello_stdcal_function(0);
    check_thisNamespace::hello_fastcal_function(0);
    check_thisNamespace::hello_cdecl_function(0);
    check_thisNamespace::hello_nocc_function(0);

    printf("----- test errorcode end\n\n");
}