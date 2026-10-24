/* apartment context identities and callbacks. */
#include "orpc.h"

typedef struct OLE_CONTEXT OLE_CONTEXT;
typedef struct CALLBACK_INTERFACE CALLBACK_INTERFACE;
typedef struct THREADING_INTERFACE THREADING_INTERFACE;
typedef struct CALL_DATA { DWORD Dispid, Reserved; void *User; } CALL_DATA;
typedef HRESULT (WINAPI *CONTEXT_FUNCTION)(CALL_DATA *);
typedef struct CALLBACK_VTABLE {
    HRESULT (WINAPI *QueryInterface)(CALLBACK_INTERFACE *, REFIID, void **);
    ULONG (WINAPI *AddRef)(CALLBACK_INTERFACE *);
    ULONG (WINAPI *Release)(CALLBACK_INTERFACE *);
    HRESULT (WINAPI *ContextCallback)(CALLBACK_INTERFACE *, CONTEXT_FUNCTION, CALL_DATA *, REFIID, int, IUnknown *);
} CALLBACK_VTABLE;
struct CALLBACK_INTERFACE { const CALLBACK_VTABLE *lpVtbl; };
typedef struct THREADING_VTABLE {
    HRESULT (WINAPI *QueryInterface)(THREADING_INTERFACE *, REFIID, void **);
    ULONG (WINAPI *AddRef)(THREADING_INTERFACE *);
    ULONG (WINAPI *Release)(THREADING_INTERFACE *);
    HRESULT (WINAPI *GetCurrentApartmentType)(THREADING_INTERFACE *, APTTYPE *);
    HRESULT (WINAPI *GetCurrentThreadType)(THREADING_INTERFACE *, DWORD *);
    HRESULT (WINAPI *GetCurrentLogicalThreadId)(THREADING_INTERFACE *, GUID *);
    HRESULT (WINAPI *SetCurrentLogicalThreadId)(THREADING_INTERFACE *, REFGUID);
} THREADING_VTABLE;
struct THREADING_INTERFACE { const THREADING_VTABLE *lpVtbl; };
struct OLE_CONTEXT {
    IUnknown Unknown;
    CALLBACK_INTERFACE Callback;
    THREADING_INTERFACE Threading;
    LONG Refs;
    volatile LONG Dead;
    APARTMENT *Apartment;
};
static const GUID callback_iid = {0x1da,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const GUID threading_iid = {0x1ce,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static SRWLOCK context_lock = SRWLOCK_INIT;
static OLE_CONTEXT *mta_context;
static LONG mta_users;

static ULONG WINAPI context_addref(IUnknown *self)
{
    return InterlockedIncrement(&CONTAINING_RECORD(self, OLE_CONTEXT, Unknown)->Refs);
}
static ULONG WINAPI context_release(IUnknown *self)
{
    OLE_CONTEXT *context = CONTAINING_RECORD(self, OLE_CONTEXT, Unknown);
    ULONG refs = InterlockedDecrement(&context->Refs);
    if (!refs) OleFree(context);
    return refs;
}
static HRESULT WINAPI context_query(IUnknown *self, REFIID iid, void **result)
{
    if (!result) return E_POINTER;
    *result = nullptr;
    if (!iid) return E_INVALIDARG;
    OLE_CONTEXT *context = CONTAINING_RECORD(self, OLE_CONTEXT, Unknown);
    if (IsEqualIID(iid, &IID_IUnknown)) *result = self;
    else if (IsEqualIID(iid, &callback_iid)) *result = &context->Callback;
    else if (IsEqualIID(iid, &threading_iid)) *result = &context->Threading;
    else return E_NOINTERFACE;
    context_addref(self);
    return S_OK;
}

#define CONTEXT_INTERFACE_METHODS(prefix, type, member) \
static HRESULT WINAPI prefix##_query(type *self, REFIID iid, void **result) \
{ return context_query(&CONTAINING_RECORD(self, OLE_CONTEXT, member)->Unknown, iid, result); } \
static ULONG WINAPI prefix##_addref(type *self) \
{ return context_addref(&CONTAINING_RECORD(self, OLE_CONTEXT, member)->Unknown); } \
static ULONG WINAPI prefix##_release(type *self) \
{ return context_release(&CONTAINING_RECORD(self, OLE_CONTEXT, member)->Unknown); }

CONTEXT_INTERFACE_METHODS(callback, CALLBACK_INTERFACE, Callback)
CONTEXT_INTERFACE_METHODS(threading, THREADING_INTERFACE, Threading)

typedef struct CONTEXT_CALL {
    OLE_CONTEXT *Context;
    CONTEXT_FUNCTION Function;
    CALL_DATA *Data;
    HRESULT Result;
} CONTEXT_CALL;

static void run_callback(void *data)
{
    CONTEXT_CALL *call = data;
    call->Result = call->Context->Dead ? E_INVALIDARG : call->Function(call->Data);
}
static DWORD WINAPI mta_callback(void *data)
{
    CONTEXT_CALL *call = data;
    call->Result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(call->Result)) {
        run_callback(call);
        CoUninitialize();
    }
    return 0;
}
static HRESULT WINAPI context_callback(CALLBACK_INTERFACE *self, CONTEXT_FUNCTION function,
    CALL_DATA *data, REFIID iid, int method, IUnknown *unknown)
{
    if (!function) return E_INVALIDARG;
    OLE_CONTEXT *context = CONTAINING_RECORD(self, OLE_CONTEXT, Callback);
    if (context->Dead) return E_INVALIDARG;
    HRESULT hr = OleCheckApartment();
    if (FAILED(hr)) return hr;
    APARTMENT *caller = OrpcCurrentApartment();
    if (!caller) return E_OUTOFMEMORY;
    CONTEXT_CALL call = {context, function, data, RPC_E_DISCONNECTED_HR};
    context_addref(&context->Unknown);
    if (context->Apartment->Mta && !caller->Mta) {
        HANDLE worker = CreateThread(nullptr, 0, mta_callback, &call, 0, nullptr);
        if (!worker) hr = HRESULT_FROM_WIN32(GetLastError());
        else {
            DWORD waited = OrpcWait(1, &worker, INFINITE, caller);
            hr = waited == WAIT_OBJECT_0 ? S_OK : HRESULT_FROM_WIN32(GetLastError());
            /* the stack argument must outlive the worker even if the apartment wait failed. */
            if (waited != WAIT_OBJECT_0) WaitForSingleObject(worker, INFINITE);
            CloseHandle(worker);
        }
    } else hr = OrpcRunInApartment(context->Apartment, run_callback, &call);
    context_release(&context->Unknown);
    return FAILED(hr) ? hr : call.Result;
}
static HRESULT WINAPI threading_apartment(THREADING_INTERFACE *self, APTTYPE *type)
{
    if (!type) return E_INVALIDARG;
    APTTYPEQUALIFIER qualifier;
    HRESULT hr = CoGetApartmentType(type, &qualifier);
    return hr == CO_E_NOTINITIALIZED ? E_FAIL : hr;
}
static HRESULT WINAPI threading_type(THREADING_INTERFACE *self, DWORD *type)
{
    if (!type) return E_INVALIDARG;
    APTTYPE apartment;
    HRESULT hr = threading_apartment(self, &apartment);
    if (SUCCEEDED(hr)) *type = apartment == APTTYPE_STA || apartment == APTTYPE_MAINSTA;
    return hr;
}
static HRESULT WINAPI threading_get_guid(THREADING_INTERFACE *self, GUID *guid)
{
    return CoGetCurrentLogicalThreadId(guid);
}
static HRESULT WINAPI threading_set_guid(THREADING_INTERFACE *self, REFGUID guid)
{
    if (!guid) return E_INVALIDARG;
    OLE_THREAD *thread = OleThreadState(TRUE);
    if (!thread) return E_OUTOFMEMORY;
    thread->LogicalThread = *guid;
    thread->HasLogicalThread = TRUE;
    return S_OK;
}
static const IUnknownVtbl unknown_vtable = {context_query, context_addref, context_release};
static const CALLBACK_VTABLE callback_vtable = {callback_query, callback_addref, callback_release, context_callback};
static const THREADING_VTABLE threading_vtable = {threading_query, threading_addref, threading_release,
    threading_apartment, threading_type, threading_get_guid, threading_set_guid};

static OLE_CONTEXT *new_context(APARTMENT *apartment)
{
    OLE_CONTEXT *context = OleAllocZero(sizeof(*context));
    if (context) {
        context->Unknown.lpVtbl = &unknown_vtable;
        context->Callback.lpVtbl = &callback_vtable;
        context->Threading.lpVtbl = &threading_vtable;
        context->Refs = 1;
        context->Apartment = apartment;
    }
    return context;
}
static HRESULT current_context(OLE_CONTEXT **result)
{
    HRESULT hr = OleCheckApartment();
    if (FAILED(hr)) return hr;
    OLE_THREAD *thread = OleThreadState(TRUE);
    if (!thread) return E_OUTOFMEMORY;
    APARTMENT *apartment = OrpcCurrentApartment();
    if (!apartment) return E_OUTOFMEMORY;
    OLE_CONTEXT *context;
    if (apartment->Mta) {
        AcquireSRWLockExclusive(&context_lock);
        if (!mta_users) {
            ReleaseSRWLockExclusive(&context_lock);
            return CO_E_NOTINITIALIZED;
        }
        if (!mta_context) mta_context = new_context(apartment);
        context = mta_context;
        if (context) context_addref(&context->Unknown);
        ReleaseSRWLockExclusive(&context_lock);
    } else {
        context = thread->CurrentContext;
        if (!context) thread->CurrentContext = context = new_context(apartment);
        if (context) context_addref(&context->Unknown);
    }
    if (!context) return E_OUTOFMEMORY;
    thread->CachedContext = &context->Unknown;
    *result = context;
    return S_OK;
}
WINOLEAPI HRESULT WINAPI CoGetContextToken(ULONG_PTR *token)
{
    if (!token) return E_POINTER;
    OLE_CONTEXT *context;
    HRESULT hr = current_context(&context);
    if (SUCCEEDED(hr)) {
        *token = (ULONG_PTR)&context->Unknown;
        context_release(&context->Unknown);
    }
    return hr;
}
WINOLEAPI HRESULT WINAPI CoGetObjectContext(REFIID iid, void **result)
{
    if (!result) return E_POINTER;
    *result = nullptr;
    OLE_CONTEXT *context;
    HRESULT hr = current_context(&context);
    if (SUCCEEDED(hr)) {
        hr = context_query(&context->Unknown, iid, result);
        context_release(&context->Unknown);
    }
    return hr;
}
void OleContextLeave(OLE_THREAD *thread)
{
    OLE_CONTEXT *context = thread->CurrentContext;
    thread->CurrentContext = nullptr;
    thread->CachedContext = nullptr;
    if (context) {
        InterlockedExchange(&context->Dead, 1);
        context_release(&context->Unknown);
    }
}
void OleContextMtaUsers(LONG delta)
{
    OLE_CONTEXT *expired = nullptr;
    AcquireSRWLockExclusive(&context_lock);
    mta_users += delta;
    if (!mta_users && mta_context) {
        expired = mta_context;
        mta_context = nullptr;
        InterlockedExchange(&expired->Dead, 1);
    }
    ReleaseSRWLockExclusive(&context_lock);
    if (expired) context_release(&expired->Unknown);
}
