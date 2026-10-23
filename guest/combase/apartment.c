/*
 * what a program with its own runtime classes asks of the runtime: registered factories, apartment shutdown, apartment
 * ids and error transforms.
 */
#include "combase_private.h"

typedef struct REGISTRATION {
    struct REGISTRATION *Next;
    UINT32 Count;
    struct {
        HSTRING Name;
        PFNGETACTIVATIONFACTORY Factory;
    } Classes[1];
} REGISTRATION;

static SRWLOCK g_lock = SRWLOCK_INIT;
static REGISTRATION *g_registrations;

HRESULT CbRegisteredFactory(HSTRING class_id, REFIID riid, void **factory)
{
    UINT32 length;
    LPCWSTR name = WindowsGetStringRawBuffer(class_id, &length);
    PFNGETACTIVATIONFACTORY found = nullptr;
    AcquireSRWLockShared(&g_lock);
    for (REGISTRATION *r = g_registrations; r && !found; r = r->Next) {
        for (UINT32 i = 0; i < r->Count && !found; i++) {
            UINT32 other_length;
            LPCWSTR other = WindowsGetStringRawBuffer(r->Classes[i].Name, &other_length);
            if (other_length == length && !memcmp(name, other, length * sizeof(WCHAR)))
                found = r->Classes[i].Factory;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    if (!found)
        return REGDB_E_CLASSNOTREG;
    IActivationFactory *object = nullptr;
    HRESULT hr = found(class_id, &object);
    if (FAILED(hr))
        return hr;
    if (!object)
        return E_POINTER;
    hr = object->lpVtbl->QueryInterface(object, riid, factory);
    object->lpVtbl->Release(object);
    return hr;
}

ROAPI HRESULT WINAPI RoRegisterActivationFactories(HSTRING *class_ids, PFNGETACTIVATIONFACTORY *factories, UINT32 count,
                                                   RO_REGISTRATION_COOKIE *cookie)
{
    if (!cookie)
        return E_INVALIDARG;
    *cookie = nullptr;
    if (!count || !class_ids || !factories)
        return E_INVALIDARG;
    for (UINT32 i = 0; i < count; i++)
        if (!class_ids[i] || !factories[i])
            return E_INVALIDARG;
    REGISTRATION *r = CbAlloc(sizeof(*r) + (SIZE_T)(count - 1) * sizeof(r->Classes[0]));
    if (!r)
        return E_OUTOFMEMORY;
    r->Count = count;
    for (UINT32 i = 0; i < count; i++) {
        HRESULT hr = WindowsDuplicateString(class_ids[i], &r->Classes[i].Name);
        if (FAILED(hr)) {
            while (i--)
                WindowsDeleteString(r->Classes[i].Name);
            CbFree(r);
            return hr;
        }
        r->Classes[i].Factory = factories[i];
    }
    AcquireSRWLockExclusive(&g_lock);
    r->Next = g_registrations;
    g_registrations = r;
    ReleaseSRWLockExclusive(&g_lock);
    *cookie = r;
    return S_OK;
}

ROAPI void WINAPI RoRevokeActivationFactories(RO_REGISTRATION_COOKIE cookie)
{
    AcquireSRWLockExclusive(&g_lock);
    for (REGISTRATION **link = &g_registrations; *link; link = &(*link)->Next) {
        if (*link == cookie) {
            *link = (*link)->Next;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    REGISTRATION *r = cookie;
    if (r && r->Count) {
        for (UINT32 i = 0; i < r->Count; i++)
            WindowsDeleteString(r->Classes[i].Name);
        CbFree(r);
    }
}

/*
 * the thread id for a single threaded apartment, one fixed value for the process's multithreaded apartment.
 */
ROAPI HRESULT WINAPI RoGetApartmentIdentifier(UINT64 *identifier)
{
    if (!identifier)
        return E_INVALIDARG;
    *identifier = 0;
    typedef HRESULT(WINAPI * GET_TYPE)(APTTYPE *, APTTYPEQUALIFIER *);
    HMODULE ole32 = LoadLibraryW(L"ole32.dll");
    GET_TYPE get_type = ole32 ? (GET_TYPE)(void *)GetProcAddress(ole32, "CoGetApartmentType") : nullptr;
    APTTYPE type;
    APTTYPEQUALIFIER qualifier;
    if (!get_type)
        return E_UNEXPECTED;
    HRESULT hr = get_type(&type, &qualifier);
    if (FAILED(hr))
        return CO_E_NOTINITIALIZED;
    *identifier = type == APTTYPE_MTA || type == APTTYPE_NA ? 0x100000000ull : (UINT64)GetCurrentThreadId();
    return S_OK;
}

/* callbacks are kept but never called: an apartment here ends with its thread or its process. */
ROAPI HRESULT WINAPI RoRegisterForApartmentShutdown(IApartmentShutdown *callback, UINT64 *identifier,
                                                    APARTMENT_SHUTDOWN_REGISTRATION_COOKIE *cookie)
{
    if (!callback || !identifier || !cookie)
        return E_INVALIDARG;
    *cookie = nullptr;
    HRESULT hr = RoGetApartmentIdentifier(identifier);
    if (FAILED(hr))
        return hr;
    callback->lpVtbl->AddRef(callback);
    *cookie = callback;
    return S_OK;
}

ROAPI HRESULT WINAPI RoUnregisterForApartmentShutdown(APARTMENT_SHUTDOWN_REGISTRATION_COOKIE cookie)
{
    IApartmentShutdown *callback = cookie;
    if (!callback)
        return E_INVALIDARG;
    callback->lpVtbl->Release(callback);
    return S_OK;
}

/* an error without restricted error information has nothing to transform. */
ROAPI BOOL WINAPI RoTransformError(HRESULT old_error, HRESULT new_error, HSTRING message)
{
    return FALSE;
}
