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

/* a static string that needs no release, for runtime class names. */
HSTRING CbStaticString(HSTRING_HEADER *header, LPCWSTR text);

/* uisettings.c */
extern const WCHAR CbUISettingsClass[];
HRESULT CbUISettingsFactory(REFIID riid, void **factory);


/* apiinfo.c */
extern const WCHAR CbApiInformationClass[];
HRESULT CbApiInformationFactory(REFIID riid, void **factory);

/* apartment.c: factories a program registers for itself. */
HRESULT CbRegisteredFactory(HSTRING class_id, REFIID riid, void **factory);

/* regionpolicy.c */
extern const WCHAR CbRegionPolicyClass[];
HRESULT CbRegionPolicyFactory(REFIID riid, void **factory);

/* capture.c: Windows.Graphics.Capture. */
extern const WCHAR CbCaptureItemClass[], CbCaptureSessionClass[], CbCapturePoolClass[];
HRESULT CbCaptureItemFactory(REFIID riid, void **factory);
HRESULT CbCaptureSessionFactory(REFIID riid, void **factory);
HRESULT CbCapturePoolFactory(REFIID riid, void **factory);

#endif
