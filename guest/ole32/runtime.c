/* unsupported marshaling paths fail before exposing an unusable interface. */
#include "orpc.h"

WINOLEAPI HRESULT WINAPI CoGetContextToken(ULONG_PTR *token)
{
    if (!token) return E_POINTER;
    *token = 0;
    HRESULT hr = OleCheckApartment();
    return FAILED(hr) ? hr : E_NOTIMPL;
}
WINOLEAPI HRESULT WINAPI CoGetObjectContext(REFIID iid, void **result)
{
    if (!result) return E_POINTER;
    *result = nullptr;
    HRESULT hr = OleCheckApartment();
    return FAILED(hr) ? hr : E_NOINTERFACE;
}
WINOLEAPI HRESULT WINAPI CoRegisterInitializeSpy(IUnknown *spy, ULARGE_INTEGER *cookie)
{
    if (!cookie || !spy) return E_INVALIDARG;
    cookie->QuadPart = 0;
    return E_NOTIMPL;
}
WINOLEAPI HRESULT WINAPI CoRevokeInitializeSpy(ULARGE_INTEGER cookie)
{
    return E_INVALIDARG;
}
WINOLEAPI HRESULT WINAPI CoWaitForMultipleHandles(DWORD flags, DWORD timeout, ULONG count,
    const HANDLE *handles, DWORD *index)
{
    if (!index || !count || count > MAXIMUM_WAIT_OBJECTS || !handles || (flags & ~0x1f))
        return E_INVALIDARG;
    *index = 0;
    APTTYPE type;
    APTTYPEQUALIFIER qualifier;
    HRESULT hr = CoGetApartmentType(&type, &qualifier);
    if (FAILED(hr)) return hr;
    if (type != APTTYPE_MTA || (flags & (8 | 16)))
        return E_NOTIMPL;
    DWORD result = WaitForMultipleObjectsEx(count, handles, (flags & 1) != 0, timeout, (flags & 2) != 0);
    if (result == WAIT_TIMEOUT) return (HRESULT)0x80010115L;
    if (result == WAIT_FAILED) return HRESULT_FROM_WIN32(GetLastError());
    *index = result;
    return S_OK;
}
WINOLEAPI HRESULT WINAPI RoInitialize(UINT type)
{
    if (type > 1) return E_INVALIDARG;
    return CoInitializeEx(nullptr, type ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
}
WINOLEAPI void WINAPI RoUninitialize(void)
{
    CoUninitialize();
}

/* ------------------------------------------------------------------ security and message filters */

static LONG g_security_initialized;

WINOLEAPI HRESULT WINAPI CoInitializeSecurity(PSECURITY_DESCRIPTOR descriptor, LONG service_count,
    SOLE_AUTHENTICATION_SERVICE *services, void *reserved1, DWORD authn_level, DWORD imp_level,
    void *auth_list, DWORD capabilities, void *reserved3)
{
    HRESULT hr = OleCheckApartment();
    if (FAILED(hr))
        return hr;
    /* there is no rpc layer to secure, only the once per process rule is kept. */
    return InterlockedExchange(&g_security_initialized, 1) ? RPC_E_TOO_LATE : S_OK;
}

WINOLEAPI HRESULT WINAPI CoSetProxyBlanket(IUnknown *proxy, DWORD authn_service, DWORD authz_service,
    OLECHAR *server_principal, DWORD authn_level, DWORD imp_level, void *auth_info, DWORD capabilities)
{
    if (!proxy)
        return E_INVALIDARG;
    IClientSecurity *security;
    if (FAILED(proxy->lpVtbl->QueryInterface(proxy, &IID_IClientSecurity, (void **)&security)))
        /* objects are always in process, so there is no proxy to configure. */
        return S_OK;
    HRESULT hr = security->lpVtbl->SetBlanket(security, proxy, authn_service, authz_service, server_principal,
        authn_level, imp_level, auth_info, capabilities);
    security->lpVtbl->Release(security);
    return hr;
}

WINOLEAPI HRESULT WINAPI CoRegisterMessageFilter(IMessageFilter *filter, IMessageFilter **previous)
{
    if (previous)
        *previous = nullptr;
    OLE_THREAD *t = OleThreadState(FALSE);
    if (!t || !t->ComInits || !t->Sta)
        return CO_E_NOT_SUPPORTED;
    if (filter)
        filter->lpVtbl->AddRef(filter);
    IMessageFilter *old = t->MessageFilter;
    t->MessageFilter = filter;
    if (previous)
        *previous = old;
    else if (old)
        old->lpVtbl->Release(old);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CoDisconnectObject(IUnknown *object, DWORD reserved)
{
    if (!object)
        return E_INVALIDARG;
    return OrpcDisconnectObject(object);
}
