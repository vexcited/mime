/*
 * gdiplus.dll: startup, the allocator and loading images from files and
 * streams. gdi+ has no background thread to run here, so the notification
 * hooks only hand out tokens.
 */
#include "gdiplus_private.h"

/* encoded images larger than this fail with OutOfMemory. */
#define GP_MAX_ENCODED 0x10000000u

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

void *gp_alloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void *gp_alloc_zero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void gp_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* ------------------------------------------------------------------ startup */

static LONG volatile g_token;

static GpStatus WINAPI notification_hook(ULONG_PTR *token)
{
    if (!token)
        return InvalidParameter;
    *token = (ULONG_PTR)InterlockedIncrement(&g_token);
    return Ok;
}

static void WINAPI notification_unhook(ULONG_PTR token)
{
}

DECLSPEC_EXPORT GpStatus WINAPI GdiplusStartup(ULONG_PTR *token, const GdiplusStartupInput *input,
                                               GdiplusStartupOutput *output)
{
    if (!token || !input)
        return InvalidParameter;
    /* version 1 is gdi+ 1.0, 2 and 3 are the GdiplusStartupInputEx of gdi+ 1.1. */
    if (input->GdiplusVersion < 1 || input->GdiplusVersion > 3)
        return UnsupportedGdiplusVersion;
    if (input->SuppressBackgroundThread) {
        if (!output)
            return InvalidParameter;
        output->NotificationHook = notification_hook;
        output->NotificationUnhook = notification_unhook;
    }
    *token = (ULONG_PTR)InterlockedIncrement(&g_token);
    return Ok;
}

DECLSPEC_EXPORT void WINAPI GdiplusShutdown(ULONG_PTR token)
{
}

DECLSPEC_EXPORT void *WINAPI GdipAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

DECLSPEC_EXPORT void WINAPI GdipFree(void *p)
{
    gp_free(p);
}

/* ------------------------------------------------------------------ files and streams */

DECLSPEC_EXPORT GpStatus WINAPI GdipCreateBitmapFromFile(const WCHAR *file, GpBitmap **bitmap)
{
    if (!file || !bitmap)
        return InvalidParameter;
    HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? FileNotFound : Win32Error;
    }
    LARGE_INTEGER size;
    GpStatus status = Ok;
    BYTE *data = NULL;
    if (!GetFileSizeEx(h, &size))
        status = Win32Error;
    else if (size.QuadPart > GP_MAX_ENCODED)
        status = OutOfMemory;
    else if (!(data = gp_alloc((SIZE_T)size.QuadPart)))
        status = OutOfMemory;
    else {
        DWORD total = 0, want = (DWORD)size.QuadPart;
        while (total < want) {
            DWORD got = 0;
            if (!ReadFile(h, data + total, want - total, &got, NULL) || !got)
                break;
            total += got;
        }
        if (total != want)
            status = Win32Error;
    }
    CloseHandle(h);
    if (status == Ok)
        status = gp_decode(data, (SIZE_T)size.QuadPart, bitmap);
    gp_free(data);
    return status;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipCreateBitmapFromFileICM(const WCHAR *file, GpBitmap **bitmap)
{
    return GdipCreateBitmapFromFile(file, bitmap);
}

/* reads the stream from its current position to its end. */
DECLSPEC_EXPORT GpStatus WINAPI GdipCreateBitmapFromStream(IStream *stream, GpBitmap **bitmap)
{
    if (!stream || !bitmap)
        return InvalidParameter;
    SIZE_T capacity = 0x10000, size = 0;
    BYTE *data = gp_alloc(capacity);
    if (!data)
        return OutOfMemory;
    for (;;) {
        if (size == capacity) {
            if (capacity >= GP_MAX_ENCODED) {
                gp_free(data);
                return OutOfMemory;
            }
            BYTE *bigger = HeapReAlloc(GetProcessHeap(), 0, data, capacity * 2);
            if (!bigger) {
                gp_free(data);
                return OutOfMemory;
            }
            data = bigger;
            capacity *= 2;
        }
        ULONG got = 0;
        HRESULT hr = stream->lpVtbl->Read(stream, data + size, (ULONG)(capacity - size), &got);
        if (FAILED(hr)) {
            gp_free(data);
            return GenericError;
        }
        if (got > capacity - size)
            got = (ULONG)(capacity - size);
        size += got;
        if (!got || hr != S_OK)
            break;
    }
    GpStatus status = gp_decode(data, size, bitmap);
    gp_free(data);
    return status;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipCreateBitmapFromStreamICM(IStream *stream, GpBitmap **bitmap)
{
    return GdipCreateBitmapFromStream(stream, bitmap);
}
