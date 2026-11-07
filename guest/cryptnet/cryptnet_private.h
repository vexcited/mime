#ifndef MIME_CRYPTNET_PRIVATE_H
#define MIME_CRYPTNET_PRIVATE_H

#define MIME_BUILD_CRYPTNET
#include <mime/win32/wincrypt.h>
#include <mime/win32/winhttp.h>

FORCEINLINE void *CnAlloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentTeb()->ProcessEnvironmentBlock->ProcessHeap, HEAP_ZERO_MEMORY, size);
}

FORCEINLINE void CnFree(void *p)
{
    if (p)
        RtlFreeHeap(NtCurrentTeb()->ProcessEnvironmentBlock->ProcessHeap, 0, p);
}

FORCEINLINE BOOL CnFail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

FORCEINLINE ULONGLONG CnTime(const FILETIME *ft)
{
    return (ULONGLONG)ft->dwHighDateTime << 32 | ft->dwLowDateTime;
}

FORCEINLINE ULONGLONG CnNow(void)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    return CnTime(&now);
}

/* ------------------------------------------------------------------ the url cache, cache.c */

/* the bytes cached for a url and when they were fetched, in a buffer to free with CnFree. */
BYTE *CnCacheRead(LPCWSTR url, DWORD *size, FILETIME *synced);
void CnCacheWrite(LPCWSTR url, const BYTE *data, DWORD size);
void CnCacheDelete(LPCWSTR url);

/* ------------------------------------------------------------------ retrieval, retrieve.c */

/* the body of a url from the wire: http, https, or a file when allowed. */
BYTE *CnFetch(LPCWSTR url, DWORD flags, DWORD timeout, DWORD limit, DWORD *size);

/* ------------------------------------------------------------------ urls, url.c */

/* the urls of a certificate's distribution points in one block to free with CnFree, or null. */
PCRYPT_URL_ARRAY CnCrlUrls(PCCERT_CONTEXT cert);

#endif
