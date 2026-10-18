/*
 * the shell light weight utilities kernelbase hosts for shlwapi: paths, strings, urls and the
 * user specific registry helpers, shared by their source files.
 */
#ifndef MIME_KERNELBASE_SHLWAPI_KB_H
#define MIME_KERNELBASE_SHLWAPI_KB_H

#define LWSTDAPI DECLSPEC_EXPORT
#include "kernelbase_private.h"
#include <mime/win32/shlwapi.h>
#include <rtl.h>

FORCEINLINE BOOL KbIsDriveLetter(WCHAR c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

/* ansi characters of the ansi code page, one or two bytes. */
FORCEINLINE LPSTR KbShNextA(LPCSTR p)
{
    if (!*p)
        return (LPSTR)p;
    return (LPSTR)p + (IsDBCSLeadByte((BYTE)*p) && p[1] ? 2 : 1);
}

FORCEINLINE LPSTR KbShPrevA(LPCSTR start, LPCSTR p)
{
    LPCSTR last = start;
    for (LPCSTR q = start; q < p; q = KbShNextA(q))
        last = q;
    return (LPSTR)last;
}

/* the ansi forms convert through the ansi code page into a heap buffer and back. */
PWSTR KbShAnsiToWide(LPCSTR s);
BOOL KbShWideToAnsiBuffer(LPCWSTR wide, LPSTR out, int size);

/* the character after the root of a path, NULL for a relative one; the forms PathCchSkipRoot knows. */
LPCWSTR KbShSkipRoot(LPCWSTR path);

/* copies a string into a counted buffer, failing as StringCchCopy does. */
HRESULT KbShCopy(LPWSTR out, SIZE_T size, LPCWSTR in);

#endif
