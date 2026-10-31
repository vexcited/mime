/* unsupported marshaling paths fail before exposing an unusable interface. */
#include "orpc.h"

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
enum {
    WAIT_ALL = 1,
    WAIT_ALERTABLE = 2,
    WAIT_INPUT_AVAILABLE = 4,
    WAIT_DISPATCH_CALLS = 8,
    WAIT_DISPATCH_WINDOW_MESSAGES = 0x10,
};

static HRESULT wait_error(void)
{
    LONG error = (LONG)GetLastError();
    return error > 0 ? HRESULT_FROM_WIN32(error) : S_OK;
}

static HRESULT wait_multithreaded(DWORD flags, DWORD timeout, ULONG count, const HANDLE *handles, DWORD *index)
{
    DWORD result = WaitForMultipleObjectsEx(count, handles, (flags & WAIT_ALL) != 0, timeout,
                                            (flags & WAIT_ALERTABLE) != 0);
    *index = result;
    if (result == WAIT_TIMEOUT)
        return RPC_S_CALLPENDING;
    if (result >= count)
        return wait_error();
    return S_OK;
}

/* what the thread of a single threaded apartment takes from its queue while it waits. */
static void wait_dispatch(OLE_THREAD *t, DWORD flags)
{
    MSG msg;
    if (flags & WAIT_DISPATCH_WINDOW_MESSAGES) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage((int)msg.wParam);
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return;
    }
    /* the calls come as messages to the window of the apartment, the rest stays queued. */
    HWND window = t->Apartment ? t->Apartment->Window : nullptr;
    if (window)
        while (PeekMessageW(&msg, window, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
    /* a look at the queue, which also delivers the messages other threads sent. */
    PeekMessageW(&msg, nullptr, WM_NULL, WM_NULL, PM_NOREMOVE);
}

static HRESULT wait_single_threaded(OLE_THREAD *t, DWORD flags, DWORD timeout, ULONG count, const HANDLE *handles,
                                    DWORD *index)
{
    BOOL all = (flags & WAIT_ALL) != 0;
    ULONGLONG start = GetTickCount64();
    /* what is queued before the wait starts counts as input, later only new input does. */
    DWORD seen = MWMO_INPUTAVAILABLE;
    for (;;) {
        /* the handles go first, as they do on windows. */
        DWORD result = WaitForMultipleObjectsEx(count, handles, all, 0, (flags & WAIT_ALERTABLE) != 0);
        if (result != WAIT_TIMEOUT) {
            if (result == WAIT_FAILED)
                return wait_error();
            *index = result;
            return S_OK;
        }
        DWORD remaining = INFINITE;
        if (timeout != INFINITE) {
            ULONGLONG elapsed = GetTickCount64() - start;
            if (elapsed >= timeout)
                return RPC_S_CALLPENDING;
            remaining = (DWORD)(timeout - elapsed);
        }
        /* queued calls wake the thread by their event as well as by the message to the apartment's window. */
        HANDLE list[MAXIMUM_WAIT_OBJECTS];
        ULONG used = 0;
        HANDLE wake = OrpcApartmentWake(t->Apartment);
        if (!all) {
            memcpy(list, handles, count * sizeof(HANDLE));
            used = count;
        } else if (remaining > 5) {
            /* nothing tells which of the handles changed, so a wait for all looks again soon. */
            remaining = 5;
        }
        if (wake && used < MAXIMUM_WAIT_OBJECTS)
            list[used++] = wake;
        DWORD got = MsgWaitForMultipleObjectsEx(used, list, remaining, QS_ALLINPUT | QS_ALLPOSTMESSAGE | QS_SENDMESSAGE,
                                                ((flags & WAIT_ALERTABLE) ? MWMO_ALERTABLE : 0) |
                                                    ((flags & WAIT_INPUT_AVAILABLE) ? MWMO_INPUTAVAILABLE : seen));
        seen = 0;
        if (got == WAIT_FAILED)
            return wait_error();
        if (got == WAIT_IO_COMPLETION) {
            *index = got;
            return S_OK;
        }
        if (got == WAIT_TIMEOUT)
            continue;
        if (got == used) {
            wait_dispatch(t, flags);
            continue;
        }
        if (wake && got == used - 1) {
            OrpcApartmentRun(t->Apartment);
            continue;
        }
        if (!all) {
            *index = got;
            return S_OK;
        }
    }
}

WINOLEAPI HRESULT WINAPI CoWaitForMultipleHandles(DWORD flags, DWORD timeout, ULONG count,
    const HANDLE *handles, DWORD *index)
{
    if (!handles) {
        if (index)
            *index = 0;
        return E_INVALIDARG;
    }
    if (!index || (flags & ~0x1f))
        return E_INVALIDARG;
    /* calls of the apartment can only be dispatched with the window messages. */
    if ((flags & (WAIT_DISPATCH_CALLS | WAIT_DISPATCH_WINDOW_MESSAGES)) == WAIT_DISPATCH_WINDOW_MESSAGES)
        return CO_E_NOT_SUPPORTED;
    if (!count)
        return RPC_E_NO_SYNC;
    if (count > MAXIMUM_WAIT_OBJECTS)
        return E_INVALIDARG;
    *index = 0;
    APTTYPE type;
    APTTYPEQUALIFIER qualifier;
    HRESULT hr = CoGetApartmentType(&type, &qualifier);
    if (FAILED(hr)) return hr;
    OLE_THREAD *t = OleThreadState(FALSE);
    if ((type == APTTYPE_STA || type == APTTYPE_MAINSTA) && t)
        return wait_single_threaded(t, flags, timeout, count, handles, index);
    return wait_multithreaded(flags, timeout, count, handles, index);
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

/* the spy of the process is kept so a second one is refused; the allocations of this library do not call it. */
static IUnknown *g_malloc_spy;

WINOLEAPI HRESULT WINAPI CoRegisterMallocSpy(IUnknown *spy)
{
    if (!spy)
        return E_INVALIDARG;
    if (InterlockedCompareExchangePointer((PVOID *)&g_malloc_spy, spy, nullptr))
        return CO_E_OBJISREG;
    spy->lpVtbl->AddRef(spy);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CoRevokeMallocSpy(void)
{
    IUnknown *spy = (IUnknown *)InterlockedExchangePointer((PVOID *)&g_malloc_spy, nullptr);
    if (!spy)
        return CO_E_OBJNOTREG;
    spy->lpVtbl->Release(spy);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CoDisconnectObject(IUnknown *object, DWORD reserved)
{
    if (!object)
        return E_INVALIDARG;
    return OrpcDisconnectObject(object);
}
