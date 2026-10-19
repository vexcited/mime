/*
 * combase internal interface shared by its source files.
 */
#ifndef MIME_COMBASE_PRIVATE_H
#define MIME_COMBASE_PRIVATE_H

#define ROAPI DECLSPEC_EXPORT
#include <mime/win32/roapi.h>
#include <rtl.h>

FORCEINLINE void *CbAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

FORCEINLINE void CbFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* a static string, which needs no release, for the runtime class names. */
HSTRING CbStaticString(HSTRING_HEADER *header, LPCWSTR text);

/* uisettings.c */
extern const WCHAR CbUISettingsClass[];
HRESULT CbUISettingsFactory(REFIID riid, void **factory);


/* capture.c: Windows.Graphics.Capture. */
extern const WCHAR CbCaptureItemClass[], CbCaptureSessionClass[], CbCapturePoolClass[];
HRESULT CbCaptureItemFactory(REFIID riid, void **factory);
HRESULT CbCaptureSessionFactory(REFIID riid, void **factory);
HRESULT CbCapturePoolFactory(REFIID riid, void **factory);

#endif
