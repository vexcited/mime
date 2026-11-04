/*
 * combase entry point and windows runtime activation. on windows combase holds the COM and runtime cores that ole32
 * forwards to; mime keeps COM in ole32, and combase only answers the runtime calls.
 */
#include "combase_private.h"
#include "xmldom.h"
#include <mime/stub.h>

/* the compiler references this symbol from code that uses floating point. */
int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/*
 * runtime apartments are COM apartments. ole32 is loaded at run time so combase does not link to it, since windows
 * forwards the other way.
 */
typedef HRESULT(WINAPI *CO_INITIALIZE_EX)(LPVOID, DWORD);
typedef void(WINAPI *CO_UNINITIALIZE)(void);

FARPROC CbOleProc(LPCSTR name)
{
    HMODULE ole32 = LoadLibraryW(L"ole32.dll");
    return ole32 ? GetProcAddress(ole32, name) : nullptr;
}

ROAPI HRESULT WINAPI RoInitialize(RO_INIT_TYPE type)
{
    if (type != RO_INIT_SINGLETHREADED && type != RO_INIT_MULTITHREADED)
        return E_INVALIDARG;
    CO_INITIALIZE_EX initialize = (CO_INITIALIZE_EX)(void *)CbOleProc("CoInitializeEx");
    if (!initialize)
        return E_UNEXPECTED;
    return initialize(nullptr, type == RO_INIT_MULTITHREADED ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
}

ROAPI void WINAPI RoUninitialize(void)
{
    CO_UNINITIALIZE uninitialize = (CO_UNINITIALIZE)(void *)CbOleProc("CoUninitialize");
    if (uninitialize)
        uninitialize();
}

static const struct {
    LPCWSTR Name;
    HRESULT (*Factory)(REFIID riid, void **factory);
} g_classes[] = {
    {CbUISettingsClass, CbUISettingsFactory},
    {CbApiInformationClass, CbApiInformationFactory},
    {CbCaptureItemClass, CbCaptureItemFactory},
    {CbCaptureSessionClass, CbCaptureSessionFactory},
    {CbCapturePoolClass, CbCapturePoolFactory},
    {CbXmlDocumentClass, CbXmlDocumentFactory},
    {CbPropertyValueClass, CbPropertyValueFactory},
    {CbToastManagerClass, CbToastManagerFactory},
    {CbToastClass, CbToastFactory},
    {CbRegionPolicyClass, CbRegionPolicyFactory},
    {CbNetworkInformationClass, CbNetworkInformationFactory},
    {CbKeyCredentialManagerClass, CbKeyCredentialManagerFactory},
#ifdef _WIN64
    {CbCompositorControllerClass, CbCompositorControllerFactory},
    {CbCompositorClass, CbCompositorFactory},
#endif
};

ROAPI HRESULT WINAPI RoGetActivationFactory(HSTRING class_id, REFIID riid, void **factory)
{
    if (!factory)
        return E_POINTER;
    *factory = nullptr;
    if (!class_id || !riid)
        return E_INVALIDARG;
    UINT32 length;
    LPCWSTR name = WindowsGetStringRawBuffer(class_id, &length);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_classes); i++)
        if (wcslen(g_classes[i].Name) == length && !memcmp(g_classes[i].Name, name, length * sizeof(WCHAR)))
            return g_classes[i].Factory(riid, factory);
    HRESULT registered = CbRegisteredFactory(class_id, riid, factory);
    if (registered != REGDB_E_CLASSNOTREG)
        return registered;
    registered = CbRegistryFactory(class_id, riid, factory);
    if (registered != REGDB_E_CLASSNOTREG)
        return registered;
    /* log missing classes, to see what programs want next. */
    char text[200];
    int n = _snprintf(text, sizeof(text), "combase: no runtime class %.*ls\n", (int)(length < 120 ? length : 120), name);
    if (n > 0)
        OutputDebugStringA(text);
    return REGDB_E_CLASSNOTREG;
}

/* the marshaler that carries a Windows.Storage.Streams buffer between apartments is not here. */
ROAPI HRESULT WINAPI RoGetBufferMarshaler(void **marshaler)
{
    if (!marshaler)
        return E_INVALIDARG;
    *marshaler = nullptr;
    return E_NOTIMPL;
}

ROAPI HRESULT WINAPI RoActivateInstance(HSTRING class_id, IInspectable **instance)
{
    if (!instance)
        return E_POINTER;
    *instance = nullptr;
    IActivationFactory *factory;
    HRESULT hr = RoGetActivationFactory(class_id, &IID_IActivationFactory, (void **)&factory);
    if (FAILED(hr))
        return hr;
    hr = factory->lpVtbl->ActivateInstance(factory, instance);
    factory->lpVtbl->Release(factory);
    return hr;
}

/* there is no restricted error information yet, the HRESULT alone describes an error. */
ROAPI BOOL WINAPI RoOriginateError(HRESULT error, HSTRING message)
{
    return FALSE;
}

ROAPI BOOL WINAPI RoOriginateErrorW(HRESULT error, UINT length, LPCWSTR message)
{
    return FALSE;
}

ROAPI BOOL WINAPI RoOriginateLanguageException(HRESULT error, HSTRING message, IUnknown *exception)
{
    return FALSE;
}

ROAPI HRESULT WINAPI GetRestrictedErrorInfo(void **info)
{
    if (!info)
        return E_POINTER;
    *info = nullptr;
    return S_FALSE;
}

ROAPI HRESULT WINAPI SetRestrictedErrorInfo(void *info)
{
    return S_OK;
}

ROAPI HRESULT WINAPI RoCaptureErrorContext(HRESULT error)
{
    return S_OK;
}

ROAPI void WINAPI RoFailFastWithErrorContext(HRESULT error)
{
    RaiseFailFastException(nullptr, nullptr, 0);
}

/* nobody keeps restricted error information, so a matching one never exists. */
ROAPI BOOL WINAPI RoTransformErrorW(HRESULT old_error, HRESULT new_error, UINT length, LPCWSTR message)
{
    return FALSE;
}

ROAPI BOOL WINAPI IsErrorPropagationEnabled(void)
{
    return TRUE;
}

ROAPI HRESULT WINAPI RoReportFailedDelegate(IUnknown *delegate, void *info)
{
    return S_OK;
}

ROAPI HRESULT WINAPI RoGetMatchingRestrictedErrorInfo(HRESULT error, void **info)
{
    if (!info)
        return E_POINTER;
    *info = nullptr;
    return E_UNEXPECTED;
}

/*
 * an hstring crosses a process as a pad to 8 bytes, a marker, the length of the text in bytes and the characters, or
 * as the count of characters, the length in bytes and the characters. inside the process the handle itself travels
 * after the marker, as an extra reference.
 */
#define MSHCTX_INPROC_BYTE 3

static ULONG HstringAlign(ULONG offset, ULONG alignment)
{
    return (offset + alignment - 1) & ~(alignment - 1);
}

static BYTE *HstringAlignPointer(BYTE *p)
{
    return (BYTE *)(((ULONG_PTR)p + 7) & ~(ULONG_PTR)7);
}

ROAPI ULONG WINAPI HSTRING_UserSize(ULONG *flags, ULONG start, HSTRING *string)
{
    if (!string)
        return start;
    ULONG offset = HstringAlign(start, 8);
    if ((BYTE)*flags == MSHCTX_INPROC_BYTE)
        return HstringAlign(offset + 4, 8) + 8;
    UINT32 length = 0;
    if (*string)
        WindowsGetStringRawBuffer(*string, &length);
    return offset + 8 + length * 2;
}

ROAPI ULONG WINAPI HSTRING_UserSize64(ULONG *flags, ULONG start, HSTRING *string)
{
    return HSTRING_UserSize(flags, start, string);
}

ROAPI BYTE *WINAPI HSTRING_UserMarshal(ULONG *flags, BYTE *buffer, HSTRING *string)
{
    if (!string)
        return buffer;
    BYTE *p = HstringAlignPointer(buffer);
    memset(buffer, 0, (SIZE_T)(p - buffer));
    if ((BYTE)*flags == MSHCTX_INPROC_BYTE) {
        HSTRING copy = nullptr;
        if (*string)
            WindowsDuplicateString(*string, &copy);
        *(ULONG *)p = 0;
        *(ULONG *)(p + 4) = 0;
        *(HSTRING *)(p + 8) = copy;
        return p + 16;
    }
    UINT32 length = 0;
    LPCWSTR text = *string ? WindowsGetStringRawBuffer(*string, &length) : nullptr;
    *(ULONG *)p = length;
    *(ULONG *)(p + 4) = length * 2;
    if (length)
        memcpy(p + 8, text, (SIZE_T)length * sizeof(WCHAR));
    return p + 8 + (SIZE_T)length * sizeof(WCHAR);
}

ROAPI BYTE *WINAPI HSTRING_UserMarshal64(ULONG *flags, BYTE *buffer, HSTRING *string)
{
    return HSTRING_UserMarshal(flags, buffer, string);
}

ROAPI BYTE *WINAPI HSTRING_UserUnmarshal(ULONG *flags, BYTE *buffer, HSTRING *string)
{
    BYTE *p = HstringAlignPointer(buffer);
    HSTRING old = *string;
    if ((BYTE)*flags == MSHCTX_INPROC_BYTE) {
        *string = *(HSTRING *)(p + 8);
        if (old)
            WindowsDeleteString(old);
        return p + 16;
    }
    ULONG bytes = *(ULONG *)(p + 4);
    HSTRING created = nullptr;
    if (bytes / 2)
        WindowsCreateString((LPCWSTR)(p + 8), bytes / 2, &created);
    *string = created;
    if (old)
        WindowsDeleteString(old);
    return p + 8 + bytes;
}

ROAPI BYTE *WINAPI HSTRING_UserUnmarshal64(ULONG *flags, BYTE *buffer, HSTRING *string)
{
    return HSTRING_UserUnmarshal(flags, buffer, string);
}

ROAPI void WINAPI HSTRING_UserFree(ULONG *flags, HSTRING *string)
{
    if (string) {
        WindowsDeleteString(*string);
        *string = nullptr;
    }
}

ROAPI void WINAPI HSTRING_UserFree64(ULONG *flags, HSTRING *string)
{
    HSTRING_UserFree(flags, string);
}
