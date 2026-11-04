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

/* the entry of a function of ole32, which is loaded at run time. */
FARPROC CbOleProc(LPCSTR name);

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
/* apartment.c: in process classes of the registry. */
HRESULT CbRegistryFactory(HSTRING class_id, REFIID riid, void **factory);

/* regionpolicy.c */
extern const WCHAR CbRegionPolicyClass[];
HRESULT CbRegionPolicyFactory(REFIID riid, void **factory);

/* capture.c: Windows.Graphics.Capture. */
extern const WCHAR CbCaptureItemClass[], CbCaptureSessionClass[], CbCapturePoolClass[];
HRESULT CbCaptureItemFactory(REFIID riid, void **factory);
HRESULT CbCaptureSessionFactory(REFIID riid, void **factory);
HRESULT CbCapturePoolFactory(REFIID riid, void **factory);

/* async.c: operations that are finished before they are handed out. */
typedef enum CB_ASYNC_KIND {
    CB_ASYNC_ACTION,
    CB_ASYNC_BOOL,
    CB_ASYNC_KEY_RETRIEVAL,
    CB_ASYNC_PROFILE,
    CB_ASYNC_KINDS,
} CB_ASYNC_KIND;
/* a failed operation carries the error, a finished one the flag or the object, which the operation then owns. */
HRESULT CbAsyncFinished(CB_ASYNC_KIND kind, HRESULT error, BOOLEAN flag, IUnknown *object, void **operation);

/* collections.c: views of items fixed at creation. the view owns the items it is given. */
typedef enum CB_VECTOR_KIND {
    CB_VECTOR_PROFILES,
    CB_VECTOR_STRINGS,
    CB_VECTOR_KINDS,
} CB_VECTOR_KIND;
HRESULT CbNewVectorView(CB_VECTOR_KIND kind, void **items, UINT32 count, void **view);

/* netinfo.c: Windows.Networking.Connectivity.NetworkInformation. */
extern const WCHAR CbNetworkInformationClass[];
HRESULT CbNetworkInformationFactory(REFIID riid, void **factory);

/* keycred.c: Windows.Security.Credentials.KeyCredentialManager. */
extern const WCHAR CbKeyCredentialManagerClass[];
HRESULT CbKeyCredentialManagerFactory(REFIID riid, void **factory);

#endif
