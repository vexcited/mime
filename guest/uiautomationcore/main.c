/*
 * the provider side of ui automation on a machine where no client listens:
 * no screen reader, no inspector, no win event hook. events go nowhere and
 * succeed, a window's host provider describes the window itself.
 */
#define UIAAPI
#include <mime/win32/uiautomationcore.h>
#include <mime/win32/winuser.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

/* ------------------------------------------------------------------ the reserved values */

static HRESULT WINAPI reserved_query(IUnknown *iface, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown)) {
        *object = iface;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

/* process wide singletons, handed out without a reference as windows' are. */
static ULONG WINAPI reserved_add_ref(IUnknown *iface)
{
    return 2;
}

static ULONG WINAPI reserved_release(IUnknown *iface)
{
    return 1;
}

static const IUnknownVtbl reserved_vtbl = {reserved_query, reserved_add_ref, reserved_release};
static IUnknown g_mixed = {&reserved_vtbl}, g_not_supported = {&reserved_vtbl};

HRESULT WINAPI UiaGetReservedMixedAttributeValue(IUnknown **value)
{
    if (!value)
        return E_INVALIDARG;
    *value = &g_mixed;
    return S_OK;
}

HRESULT WINAPI UiaGetReservedNotSupportedValue(IUnknown **value)
{
    if (!value)
        return E_INVALIDARG;
    *value = &g_not_supported;
    return S_OK;
}

/* ------------------------------------------------------------------ the host provider of a window */

typedef struct {
    IRawElementProviderSimple iface;
    LONG refs;
    HWND hwnd;
} HOST;

static HRESULT WINAPI host_query(IRawElementProviderSimple *iface, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRawElementProviderSimple)) {
        *object = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI host_add_ref(IRawElementProviderSimple *iface)
{
    return (ULONG)InterlockedIncrement(&((HOST *)iface)->refs);
}

static ULONG WINAPI host_release(IRawElementProviderSimple *iface)
{
    HOST *host = (HOST *)iface;
    LONG refs = InterlockedDecrement(&host->refs);
    if (!refs)
        LocalFree(host);
    return (ULONG)refs;
}

static HRESULT WINAPI host_options(IRawElementProviderSimple *iface, ProviderOptions *options)
{
    if (!options)
        return E_INVALIDARG;
    *options = ProviderOptions_ServerSideProvider;
    return S_OK;
}

static HRESULT WINAPI host_pattern(IRawElementProviderSimple *iface, PATTERNID id, IUnknown **pattern)
{
    if (!pattern)
        return E_INVALIDARG;
    *pattern = nullptr;
    return S_OK;
}

static HRESULT WINAPI host_property(IRawElementProviderSimple *iface, PROPERTYID id, VARIANT *value)
{
    HOST *host = (HOST *)iface;
    if (!value)
        return E_INVALIDARG;
    memset(value, 0, sizeof(*value));
    if (!IsWindow(host->hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (id == UIA_NativeWindowHandlePropertyId) {
        value->vt = VT_I4;
        value->lVal = (LONG)(LONG_PTR)host->hwnd;
    }
    return S_OK;
}

static HRESULT WINAPI host_host(IRawElementProviderSimple *iface, IRawElementProviderSimple **host)
{
    if (!host)
        return E_INVALIDARG;
    *host = nullptr;
    return S_OK;
}

static const IRawElementProviderSimpleVtbl host_vtbl = {
    host_query, host_add_ref, host_release, host_options, host_pattern, host_property, host_host,
};

HRESULT WINAPI UiaHostProviderFromHwnd(HWND hwnd, IRawElementProviderSimple **provider)
{
    if (!provider)
        return E_INVALIDARG;
    *provider = nullptr;
    if (!hwnd)
        return E_INVALIDARG;
    if (!IsWindow(hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    HOST *host = LocalAlloc(LMEM_ZEROINIT, sizeof(*host));
    if (!host)
        return E_OUTOFMEMORY;
    host->iface.lpVtbl = &host_vtbl;
    host->refs = 1;
    host->hwnd = hwnd;
    *provider = &host->iface;
    return S_OK;
}

/* ------------------------------------------------------------------ servers and events */

/* WM_GETOBJECT only comes from clients, and none listen. */
LRESULT WINAPI UiaReturnRawElementProvider(HWND hwnd, WPARAM wparam, LPARAM lparam, IRawElementProviderSimple *element)
{
    return 0;
}

BOOL WINAPI UiaClientsAreListening(void)
{
    return FALSE;
}

HRESULT WINAPI UiaRaiseAutomationEvent(IRawElementProviderSimple *provider, EVENTID id)
{
    return provider ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI UiaRaiseAutomationPropertyChangedEvent(IRawElementProviderSimple *provider, PROPERTYID id,
                                                      VARIANT old_value, VARIANT new_value)
{
    return provider ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI UiaRaiseStructureChangedEvent(IRawElementProviderSimple *provider, StructureChangeType type,
                                             int *runtime_id, int length)
{
    return provider ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI UiaRaiseAsyncContentLoadedEvent(IRawElementProviderSimple *provider, int state, double percent)
{
    return provider ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI UiaRaiseNotificationEvent(IRawElementProviderSimple *provider, int kind, int processing,
                                         BSTR display, BSTR activity)
{
    return provider ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI UiaRaiseActiveTextPositionChangedEvent(IRawElementProviderSimple *provider, void *range)
{
    return provider ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI UiaRaiseTextEditTextChangedEvent(IRawElementProviderSimple *provider, int kind, SAFEARRAY *text)
{
    return provider ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI UiaRaiseChangesEvent(IRawElementProviderSimple *provider, int count, void *changes)
{
    return provider ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI UiaDisconnectProvider(IRawElementProviderSimple *provider)
{
    return provider ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI UiaDisconnectAllProviders(void)
{
    return S_OK;
}
