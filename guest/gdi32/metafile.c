/*
 * gdi32 windows metafiles. there is no metafile recorder or player yet: every
 * entry point fails cleanly, so no metafile handle can ever exist.
 */
#include "gdi32_private.h"
#include <mime/stub.h>

WINGDIAPI HDC WINAPI CreateMetaFileW(LPCWSTR file)
{
    MIME_STUB("gdi32");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return nullptr;
}

WINGDIAPI HMETAFILE WINAPI CloseMetaFile(HDC dc)
{
    MIME_STUB("gdi32");
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
}

WINGDIAPI BOOL WINAPI DeleteMetaFile(HMETAFILE metafile)
{
    MIME_STUB("gdi32");
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

WINGDIAPI BOOL WINAPI PlayMetaFile(HDC dc, HMETAFILE metafile)
{
    MIME_STUB("gdi32");
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

WINGDIAPI BOOL WINAPI PlayMetaFileRecord(HDC dc, LPHANDLETABLE table, LPMETARECORD record, UINT handles)
{
    MIME_STUB("gdi32");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

WINGDIAPI BOOL WINAPI EnumMetaFile(HDC dc, HMETAFILE metafile, MFENUMPROC proc, LPARAM param)
{
    MIME_STUB("gdi32");
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

WINGDIAPI HMETAFILE WINAPI CopyMetaFileW(HMETAFILE metafile, LPCWSTR file)
{
    MIME_STUB("gdi32");
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
}
