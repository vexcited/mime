/*
 * winspool internal interface: printer handles and string helpers.
 */
#ifndef MIME_WINSPOOL_PRIVATE_H
#define MIME_WINSPOOL_PRIVATE_H

#define MIME_BUILD_WINSPOOL
#include <mime/win32/winspool.h>
#include <mime/win32/winnls.h>

/*
 * there is no print spooler and no printer is installed. the only handle
 * OpenPrinter hands out is one on the local print server.
 */
typedef struct SpoolHandle {
    struct SpoolHandle *next;
    ACCESS_MASK access;
} SpoolHandle;

FORCEINLINE PVOID SpoolAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

FORCEINLINE void SpoolFree(PVOID p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* sets the last error and returns FALSE. */
FORCEINLINE BOOL SpoolFail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

/* the server handle behind a value, NULL with ERROR_INVALID_HANDLE when it is not one. */
SpoolHandle *SpoolLookup(HANDLE printer);

/* converts an ansi string with the ansi code page; the result comes from the heap. */
PWSTR SpoolToWide(LPCSTR s);
/* writes a utf-16 string as ansi into a buffer of size bytes; returns FALSE if it does not fit. */
BOOL SpoolToAnsi(PCWSTR s, LPSTR buffer, DWORD size);

/* the environment this build runs as, "Windows x64" or "Windows ARM64". */
PCWSTR SpoolNativeEnvironment(void);
/* maps an environment name to its spool subdirectory, NULL when unknown. */
PCWSTR SpoolEnvironmentDirectory(LPCWSTR environment);

#endif
