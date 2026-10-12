/*
 * BSTR strings. a BSTR points to the characters; the DWORD before them
 * holds the length in bytes without the terminating null. on 64 bit
 * windows the block starts 8 bytes before the characters, the length
 * taking the upper half, so the string stays 8 byte aligned.
 */
#include "oleaut32_private.h"

#define BSTR_HEADER 8
/* lengths above this cannot be represented with the header and terminator. */
#define BSTR_MAX_BYTES 0x7FFFFFF0u

static BSTR allocate(UINT bytes)
{
    if (bytes > BSTR_MAX_BYTES)
        return NULL;
    BYTE *block = AutAlloc(BSTR_HEADER + bytes + sizeof(WCHAR));
    if (!block)
        return NULL;
    *(DWORD *)(block + 4) = bytes;
    BYTE *text = block + BSTR_HEADER;
    text[bytes] = 0;
    text[bytes + 1] = 0;
    return (BSTR)text;
}

WINOLEAUTAPI BSTR WINAPI SysAllocStringByteLen(LPCSTR string, UINT length)
{
    BSTR bstr = allocate(length);
    if (!bstr)
        return NULL;
    if (string)
        memcpy(bstr, string, length);
    else
        memset(bstr, 0, length);
    return bstr;
}

WINOLEAUTAPI BSTR WINAPI SysAllocStringLen(const OLECHAR *string, UINT length)
{
    if (length > BSTR_MAX_BYTES / sizeof(WCHAR))
        return NULL;
    return SysAllocStringByteLen((LPCSTR)string, length * sizeof(WCHAR));
}

WINOLEAUTAPI BSTR WINAPI SysAllocString(const OLECHAR *string)
{
    if (!string)
        return NULL;
    return SysAllocStringLen(string, (UINT)wcslen(string));
}

WINOLEAUTAPI void WINAPI SysFreeString(BSTR bstr)
{
    if (bstr)
        AutFree((BYTE *)bstr - BSTR_HEADER);
}

WINOLEAUTAPI UINT WINAPI SysStringByteLen(BSTR bstr)
{
    return bstr ? *(const DWORD *)((const BYTE *)bstr - 4) : 0;
}

WINOLEAUTAPI UINT WINAPI SysStringLen(BSTR bstr)
{
    return SysStringByteLen(bstr) / sizeof(WCHAR);
}

/* the new string may point into the old one, so it is copied before the old one is freed. */
WINOLEAUTAPI INT WINAPI SysReAllocStringLen(BSTR *bstr, const OLECHAR *string, UINT length)
{
    if (!bstr)
        return FALSE;
    BSTR fresh = SysAllocStringLen(string, length);
    if (!fresh)
        return FALSE;
    SysFreeString(*bstr);
    *bstr = fresh;
    return TRUE;
}

WINOLEAUTAPI INT WINAPI SysReAllocString(BSTR *bstr, const OLECHAR *string)
{
    if (!bstr)
        return FALSE;
    if (!string) {
        SysFreeString(*bstr);
        *bstr = NULL;
        return TRUE;
    }
    return SysReAllocStringLen(bstr, string, (UINT)wcslen(string));
}
