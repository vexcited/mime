/*
 * the state of the calls of a thread: the context of the call being served, the cancellation of calls, the logical thread
 * and the use of the multithreaded apartment by code that has no thread in it. The context of a call is what
 * CoSwitchCallContext put there, and the security of a call from another process is that call's.
 */
#include "orpc.h"

#define RPC_E_CALL_COMPLETE_HR ((HRESULT)0x80010117L)
#define RPC_E_NO_CONTEXT_HR ((HRESULT)0x8001011EL)
#define RPC_E_CANCEL_DISABLED_HR ((HRESULT)0x80010140L)

/* ------------------------------------------------------------------ the multithreaded apartment */

/* the users of the apartment are records in a list, so that a cookie that was not handed out can be told. */
typedef struct MTA_USER {
    struct MTA_USER *Next;
} MTA_USER;

static MTA_USER *g_mta_users;
static SRWLOCK g_mta_lock = SRWLOCK_INIT;

WINOLEAPI HRESULT WINAPI CoIncrementMTAUsage(void **cookie)
{
    if (!cookie)
        return E_INVALIDARG;
    *cookie = nullptr;
    MTA_USER *user = OleAllocZero(sizeof(*user));
    if (!user)
        return E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&g_mta_lock);
    user->Next = g_mta_users;
    g_mta_users = user;
    ReleaseSRWLockExclusive(&g_mta_lock);
    OleMtaUsage(1);
    *cookie = user;
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CoDecrementMTAUsage(void *cookie)
{
    AcquireSRWLockExclusive(&g_mta_lock);
    MTA_USER **at = &g_mta_users;
    while (*at && *at != cookie)
        at = &(*at)->Next;
    MTA_USER *user = *at;
    if (user)
        *at = user->Next;
    ReleaseSRWLockExclusive(&g_mta_lock);
    if (!user)
        return E_INVALIDARG;
    OleMtaUsage(-1);
    OleFree(user);
    return S_OK;
}

/* ------------------------------------------------------------------ the call being served */

/* the security of a call is only there for a call from another process: the thread takes on the caller's identity. */
WINOLEAPI HRESULT WINAPI CoImpersonateClient(void)
{
    OLE_THREAD *t = OleThreadState(FALSE);
    if (!t || !t->OrpcCall)
        return RPC_E_CALL_COMPLETE_HR;
    return OrpcImpersonate(t->OrpcCall, TRUE);
}

WINOLEAPI HRESULT WINAPI CoRevertToSelf(void)
{
    OLE_THREAD *t = OleThreadState(FALSE);
    if (!t || !t->OrpcCall)
        return RPC_E_CALL_COMPLETE_HR;
    return OrpcImpersonate(t->OrpcCall, FALSE);
}

/* S_FALSE and a thread of 0 when no call is being served. */
WINOLEAPI HRESULT WINAPI CoGetCallerTID(DWORD *thread)
{
    if (!thread)
        return E_INVALIDARG;
    OLE_THREAD *t = OleThreadState(TRUE);
    if (!t)
        return E_OUTOFMEMORY;
    *thread = t->CallerThread;
    return t->CallerThread ? S_OK : S_FALSE;
}

/* the object that stands for the call, handed back so that the caller can put it back when the call is done. */
WINOLEAPI HRESULT WINAPI CoSwitchCallContext(IUnknown *new_context, IUnknown **old_context)
{
    if (!old_context)
        return E_INVALIDARG;
    OLE_THREAD *t = OleThreadState(TRUE);
    if (!t)
        return E_OUTOFMEMORY;
    *old_context = t->CallContext;
    t->CallContext = new_context;
    return S_OK;
}

/* ------------------------------------------------------------------ cancelling calls */

/*
 * a call is cancellable while its thread has allowed it with CoEnableCallCancellation. The object of the call that can
 * be cancelled is the one the thread gave to CoSetCancelObject, which must have ICancelMethodCalls; the cancel is
 * handed to it. A thread with no such object has no call to cancel.
 */
static const IID iid_cancel = {0x00000029, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID iid_proxy_manager = {0x00000008, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

typedef struct CANCEL_ENTRY {
    struct CANCEL_ENTRY *Next;
    DWORD Thread;
    IUnknown *Object;
} CANCEL_ENTRY;

static CANCEL_ENTRY *g_cancel_entries;
static SRWLOCK g_cancel_lock = SRWLOCK_INIT;

WINOLEAPI HRESULT WINAPI CoEnableCallCancellation(void *reserved)
{
    if (reserved)
        return E_INVALIDARG;
    OLE_THREAD *t = OleThreadState(TRUE);
    if (!t)
        return E_OUTOFMEMORY;
    t->CancelEnabled++;
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CoDisableCallCancellation(void *reserved)
{
    if (reserved)
        return E_INVALIDARG;
    OLE_THREAD *t = OleThreadState(TRUE);
    if (!t)
        return E_OUTOFMEMORY;
    if (!t->CancelEnabled)
        return RPC_E_CANCEL_DISABLED_HR;
    t->CancelEnabled--;
    return S_OK;
}

static void forget_cancel_entry(DWORD thread)
{
    AcquireSRWLockExclusive(&g_cancel_lock);
    CANCEL_ENTRY **at = &g_cancel_entries;
    while (*at && (*at)->Thread != thread)
        at = &(*at)->Next;
    CANCEL_ENTRY *entry = *at;
    if (entry)
        *at = entry->Next;
    ReleaseSRWLockExclusive(&g_cancel_lock);
    if (entry) {
        entry->Object->lpVtbl->Release(entry->Object);
        OleFree(entry);
    }
}

WINOLEAPI HRESULT WINAPI CoSetCancelObject(IUnknown *object)
{
    DWORD thread = GetCurrentThreadId();
    if (!object) {
        forget_cancel_entry(thread);
        return S_OK;
    }
    IUnknown *cancel;
    HRESULT hr = object->lpVtbl->QueryInterface(object, &iid_cancel, (void **)&cancel);
    if (FAILED(hr))
        return hr;
    forget_cancel_entry(thread);
    CANCEL_ENTRY *entry = OleAllocZero(sizeof(*entry));
    if (!entry) {
        cancel->lpVtbl->Release(cancel);
        return E_OUTOFMEMORY;
    }
    entry->Thread = thread;
    entry->Object = cancel;
    AcquireSRWLockExclusive(&g_cancel_lock);
    entry->Next = g_cancel_entries;
    g_cancel_entries = entry;
    ReleaseSRWLockExclusive(&g_cancel_lock);
    return S_OK;
}

/* the cancel object of a thread, referenced, with the interface asked for. */
WINOLEAPI HRESULT WINAPI CoGetCancelObject(DWORD thread, REFIID iid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!thread)
        thread = GetCurrentThreadId();
    IUnknown *found = nullptr;
    AcquireSRWLockShared(&g_cancel_lock);
    for (CANCEL_ENTRY *e = g_cancel_entries; e; e = e->Next) {
        if (e->Thread == thread) {
            found = e->Object;
            found->lpVtbl->AddRef(found);
            break;
        }
    }
    ReleaseSRWLockShared(&g_cancel_lock);
    if (!found)
        return RPC_E_NO_CONTEXT_HR;
    HRESULT hr = found->lpVtbl->QueryInterface(found, iid, object);
    found->lpVtbl->Release(found);
    return hr;
}

typedef struct {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *);
    ULONG(STDMETHODCALLTYPE *Release)(void *);
    HRESULT(STDMETHODCALLTYPE *Cancel)(void *, ULONG);
    HRESULT(STDMETHODCALLTYPE *TestCancel)(void *);
} CANCEL_VTBL;

WINOLEAPI HRESULT WINAPI CoCancelCall(DWORD thread, ULONG timeout)
{
    void *cancel;
    HRESULT hr = CoGetCancelObject(thread, &iid_cancel, &cancel);
    if (FAILED(hr))
        return hr;
    const CANCEL_VTBL *vtbl = *(const CANCEL_VTBL **)cancel;
    hr = vtbl->Cancel(cancel, timeout);
    vtbl->Release(cancel);
    return hr;
}

/* asks the call being served if it was cancelled; with no call there is nothing to ask. */
WINOLEAPI HRESULT WINAPI CoTestCancel(void)
{
    void *cancel;
    HRESULT hr = CoGetCallContext(&iid_cancel, &cancel);
    if (FAILED(hr))
        return hr;
    const CANCEL_VTBL *vtbl = *(const CANCEL_VTBL **)cancel;
    hr = vtbl->TestCancel(cancel);
    vtbl->Release(cancel);
    return hr;
}

/* an object that is a proxy for an object of another process is connected if its proxy manager says so. */
WINOLEAPI BOOL WINAPI CoIsHandlerConnected(IUnknown *object)
{
    if (!object || IsBadReadPtr(object, sizeof(void *)))
        return FALSE;
    IUnknown *manager;
    if (FAILED(object->lpVtbl->QueryInterface(object, &iid_proxy_manager, (void **)&manager)))
        return TRUE;
    /* IProxyManager::IsConnected is the seventh method after the three of IUnknown. */
    typedef BOOL(STDMETHODCALLTYPE * IS_CONNECTED)(void *);
    IS_CONNECTED is_connected = ((IS_CONNECTED *)manager->lpVtbl)[3 + 6];
    BOOL connected = is_connected(manager);
    manager->lpVtbl->Release(manager);
    return connected;
}

/* ------------------------------------------------------------------ small things */

WINOLEAPI HRESULT WINAPI CoMarshalHresult(IStream *stream, HRESULT value)
{
    if (!stream || IsBadReadPtr(stream, sizeof(void *)))
        return E_INVALIDARG;
    return stream->lpVtbl->Write(stream, &value, sizeof(value), nullptr);
}

WINOLEAPI HRESULT WINAPI CoUnmarshalHresult(IStream *stream, HRESULT *value)
{
    if (!stream || IsBadReadPtr(stream, sizeof(void *)) || !value || IsBadWritePtr(value, sizeof(*value)))
        return E_INVALIDARG;
    HRESULT read = E_UNEXPECTED;
    HRESULT hr = stream->lpVtbl->Read(stream, &read, sizeof(read), nullptr);
    if (hr == S_OK)
        *value = read;
    return hr;
}

/* the thread's guid for the chain of calls it takes part in, made when first asked for. */
WINOLEAPI HRESULT WINAPI CoGetCurrentLogicalThreadId(GUID *guid)
{
    if (!guid)
        return E_INVALIDARG;
    OLE_THREAD *t = OleThreadState(TRUE);
    if (!t)
        return E_OUTOFMEMORY;
    if (!t->HasLogicalThread) {
        HRESULT hr = CoCreateGuid(&t->LogicalThread);
        if (FAILED(hr))
            return hr;
        t->HasLogicalThread = TRUE;
    }
    *guid = t->LogicalThread;
    return S_OK;
}
