/* windows runtime basics of combase.dll: strings, activation and IInspectable. */
#ifndef MIME_WIN32_ROAPI_H
#define MIME_WIN32_ROAPI_H

#include <mime/win32/objbase.h>

#ifndef ROAPI
#define ROAPI DECLSPEC_IMPORT
#endif

/* ------------------------------------------------------------------ strings */

typedef struct HSTRING__ *HSTRING;

/* caller-provided string reference header, opaque like on windows. */
typedef struct HSTRING_HEADER {
    union {
        PVOID Reserved1;
        char Reserved2[sizeof(void *) == 8 ? 24 : 20];
    } Reserved;
} HSTRING_HEADER;

ROAPI HRESULT WINAPI WindowsCreateString(LPCWSTR source, UINT32 length, HSTRING *string);
ROAPI HRESULT WINAPI WindowsCreateStringReference(LPCWSTR source, UINT32 length, HSTRING_HEADER *header,
                                                  HSTRING *string);
ROAPI HRESULT WINAPI WindowsDeleteString(HSTRING string);
ROAPI HRESULT WINAPI WindowsDuplicateString(HSTRING string, HSTRING *copy);
ROAPI LPCWSTR WINAPI WindowsGetStringRawBuffer(HSTRING string, UINT32 *length);
ROAPI UINT32 WINAPI WindowsGetStringLen(HSTRING string);
ROAPI BOOL WINAPI WindowsIsStringEmpty(HSTRING string);
ROAPI HRESULT WINAPI WindowsCompareStringOrdinal(HSTRING a, HSTRING b, INT32 *result);
ROAPI HRESULT WINAPI WindowsConcatString(HSTRING a, HSTRING b, HSTRING *result);
ROAPI HRESULT WINAPI WindowsStringHasEmbeddedNull(HSTRING string, BOOL *has_null);
ROAPI HRESULT WINAPI WindowsSubstring(HSTRING string, UINT32 start, HSTRING *result);
ROAPI HRESULT WINAPI WindowsSubstringWithSpecifiedLength(HSTRING string, UINT32 start, UINT32 length,
                                                         HSTRING *result);
ROAPI HRESULT WINAPI WindowsTrimStringStart(HSTRING string, HSTRING trim, HSTRING *result);
ROAPI HRESULT WINAPI WindowsTrimStringEnd(HSTRING string, HSTRING trim, HSTRING *result);
ROAPI HRESULT WINAPI WindowsReplaceString(HSTRING string, HSTRING from, HSTRING to, HSTRING *result);
typedef struct HSTRING_BUFFER__ *HSTRING_BUFFER;
ROAPI HRESULT WINAPI WindowsPreallocateStringBuffer(UINT32 length, WCHAR **buffer, HSTRING_BUFFER *handle);
ROAPI HRESULT WINAPI WindowsPromoteStringBuffer(HSTRING_BUFFER handle, HSTRING *string);
ROAPI HRESULT WINAPI WindowsDeleteStringBuffer(HSTRING_BUFFER handle);

/* ------------------------------------------------------------------ IInspectable */

typedef enum TrustLevel {
    BaseTrust = 0,
    PartialTrust = 1,
    FullTrust = 2,
} TrustLevel;

typedef struct IInspectable IInspectable;
typedef struct IInspectableVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IInspectable *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IInspectable *This);
    ULONG(STDMETHODCALLTYPE *Release)(IInspectable *This);
    HRESULT(STDMETHODCALLTYPE *GetIids)(IInspectable *This, ULONG *count, IID **iids);
    HRESULT(STDMETHODCALLTYPE *GetRuntimeClassName)(IInspectable *This, HSTRING *name);
    HRESULT(STDMETHODCALLTYPE *GetTrustLevel)(IInspectable *This, TrustLevel *level);
} IInspectableVtbl;
struct IInspectable {
    const IInspectableVtbl *lpVtbl;
};

typedef struct IActivationFactory IActivationFactory;
typedef struct IActivationFactoryVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IActivationFactory *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IActivationFactory *This);
    ULONG(STDMETHODCALLTYPE *Release)(IActivationFactory *This);
    HRESULT(STDMETHODCALLTYPE *GetIids)(IActivationFactory *This, ULONG *count, IID **iids);
    HRESULT(STDMETHODCALLTYPE *GetRuntimeClassName)(IActivationFactory *This, HSTRING *name);
    HRESULT(STDMETHODCALLTYPE *GetTrustLevel)(IActivationFactory *This, TrustLevel *level);
    HRESULT(STDMETHODCALLTYPE *ActivateInstance)(IActivationFactory *This, IInspectable **instance);
} IActivationFactoryVtbl;
struct IActivationFactory {
    const IActivationFactoryVtbl *lpVtbl;
};

MIME_DEFINE_GUID(IID_IInspectable, 0xAF86E2E0, 0xB12D, 0x4C6A, 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90);
MIME_DEFINE_GUID(IID_IActivationFactory, 0x00000035, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
#ifndef MIME_HAVE_IID_IAGILEOBJECT
#define MIME_HAVE_IID_IAGILEOBJECT
MIME_DEFINE_GUID(IID_IAgileObject, 0x94EA2B94, 0xE9CC, 0x49E0, 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90);
#endif

/* ------------------------------------------------------------------ activation */

typedef enum RO_INIT_TYPE {
    RO_INIT_SINGLETHREADED = 0,
    RO_INIT_MULTITHREADED = 1,
} RO_INIT_TYPE;

ROAPI HRESULT WINAPI RoInitialize(RO_INIT_TYPE type);
ROAPI void WINAPI RoUninitialize(void);
ROAPI HRESULT WINAPI RoGetActivationFactory(HSTRING class_id, REFIID riid, void **factory);
ROAPI HRESULT WINAPI RoActivateInstance(HSTRING class_id, IInspectable **instance);

/* factories a program serves from its own code, and the apartment it asks about. */
typedef HRESULT(WINAPI *PFNGETACTIVATIONFACTORY)(HSTRING class_id, IActivationFactory **factory);
typedef void *RO_REGISTRATION_COOKIE;
typedef struct IApartmentShutdown IApartmentShutdown;
typedef struct IApartmentShutdownVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IApartmentShutdown *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IApartmentShutdown *This);
    ULONG(STDMETHODCALLTYPE *Release)(IApartmentShutdown *This);
    void(STDMETHODCALLTYPE *OnUncontrolledShutdown)(IApartmentShutdown *This, UINT64 apartment);
} IApartmentShutdownVtbl;
struct IApartmentShutdown {
    const IApartmentShutdownVtbl *lpVtbl;
};
typedef void *APARTMENT_SHUTDOWN_REGISTRATION_COOKIE;

ROAPI HRESULT WINAPI RoRegisterActivationFactories(HSTRING *class_ids, PFNGETACTIVATIONFACTORY *factories, UINT32 count,
                                                   RO_REGISTRATION_COOKIE *cookie);
ROAPI void WINAPI RoRevokeActivationFactories(RO_REGISTRATION_COOKIE cookie);
ROAPI HRESULT WINAPI RoGetApartmentIdentifier(UINT64 *identifier);
ROAPI HRESULT WINAPI RoRegisterForApartmentShutdown(IApartmentShutdown *callback, UINT64 *identifier,
                                                    APARTMENT_SHUTDOWN_REGISTRATION_COOKIE *cookie);
ROAPI HRESULT WINAPI RoUnregisterForApartmentShutdown(APARTMENT_SHUTDOWN_REGISTRATION_COOKIE cookie);
ROAPI BOOL WINAPI RoTransformError(HRESULT old_error, HRESULT new_error, HSTRING message);

#endif
