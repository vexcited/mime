/* explicit locales follow the c locale supported by this runtime. */
#include "msvcrt_private.h"

int __cdecl strcoll(const char *a, const char *b);
int __cdecl wcscoll(const wchar_t *a, const wchar_t *b);
size_t __cdecl strxfrm(char *dst, const char *src, size_t count);
size_t __cdecl wcsxfrm(wchar_t *dst, const wchar_t *src, size_t count);

__declspec(dllexport) int __cdecl _strcoll_l(const char *a, const char *b, void *locale)
{
    return strcoll(a, b);
}

__declspec(dllexport) int __cdecl _wcscoll_l(const wchar_t *a, const wchar_t *b, void *locale)
{
    return wcscoll(a, b);
}

__declspec(dllexport) size_t __cdecl _strxfrm_l(char *dst, const char *src, size_t count, void *locale)
{
    return strxfrm(dst, src, count);
}

__declspec(dllexport) size_t __cdecl _wcsxfrm_l(wchar_t *dst, const wchar_t *src, size_t count, void *locale)
{
    return wcsxfrm(dst, src, count);
}
