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

static FARPROC ole32_proc(LPCSTR name)
{
    HMODULE ole32 = LoadLibraryW(L"ole32.dll");
    return ole32 ? GetProcAddress(ole32, name) : nullptr;
}

ROAPI HRESULT WINAPI RoInitialize(RO_INIT_TYPE type)
{
    if (type != RO_INIT_SINGLETHREADED && type != RO_INIT_MULTITHREADED)
        return E_INVALIDARG;
    CO_INITIALIZE_EX initialize = (CO_INITIALIZE_EX)(void *)ole32_proc("CoInitializeEx");
    if (!initialize)
        return E_UNEXPECTED;
    return initialize(nullptr, type == RO_INIT_MULTITHREADED ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
}

ROAPI void WINAPI RoUninitialize(void)
{
    CO_UNINITIALIZE uninitialize = (CO_UNINITIALIZE)(void *)ole32_proc("CoUninitialize");
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
    /* log missing classes, to see what programs want next. */
    char text[200];
    int n = _snprintf(text, sizeof(text), "combase: no runtime class %.*ls\n", (int)(length < 120 ? length : 120), name);
    if (n > 0)
        OutputDebugStringA(text);
    return REGDB_E_CLASSNOTREG;
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
