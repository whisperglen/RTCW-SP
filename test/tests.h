#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void xassert_str(int success, const char* expression, const char* function, unsigned line, const char* file);
void xassert_int(int success, int printval, const char* function, unsigned line, const char* file);


#define assert(expression) (void)(                                                             \
            (xassert_str((!!(expression)), _CRT_STRINGIZE(#expression), (__func__), (unsigned)(__LINE__), (__FILE__)), 0) \
        )

#define assertloop(expression, iter) (void)(                                                             \
            (xassert_int((!!(expression)), (iter), (__func__), (unsigned)(__LINE__), (__FILE__)), 0) \
        )

#ifdef __cplusplus
}
#endif