/*
 * per thread call state: the context of the call being served, call cancellation, the logical thread id and use of
 * the multithreaded apartment by code with no thread in it. the call context is what CoSwitchCallContext put there,
 * and a call from another process carries that call's security.
 */
#include "orpc.h"

#define RPC_E_CALL_COMPLETE_HR ((HRESULT)0x80010117L)
#define RPC_E_NO_CONTEXT_HR ((HRESULT)0x8001011EL)
#define RPC_E_CANCEL_DISABLED_HR ((HRESULT)0x80010140L)

/* ------------------------------------------------------------------ the multithreaded apartment */

/* apartment users are kept in a list so a cookie that was never handed out can be detected. */
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

/* only a call from another process has security: the thread takes on the caller's identity. */
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

/* returns the old context so the caller can put it back when the call is done. */
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
 * a call is cancellable while its thread allows it with CoEnableCallCancellation. the cancel goes to the object the
 * thread gave CoSetCancelObject, which must have ICancelMethodCalls. a thread with no such object has no call to
 * cancel.
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

/* a proxy is connected if its proxy manager says so. */
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

/*
 * a stub reports the application's result here. it is kept for the call being served, where nothing reads it yet.
 */
WINOLEAPI HRESULT WINAPI DcomChannelSetHResult(void *reserved1, ULONG *reserved2, HRESULT application)
{
    if (reserved1 || reserved2)
        return E_INVALIDARG;
    OLE_THREAD *t = OleThreadState(FALSE);
    if (t)
        t->CallResult = application;
    return S_OK;
}

/*
 * what variant marshaling asks of the other side of a call. the peer here speaks the current protocol, so it needs
 * no trailing padding after a variant, which older ones read, and accepts user defined type records. outside a call
 * nothing is asked: no padding, records allowed.
 */
WINOLEAPI BOOL WINAPI CoDoesOtherSideVariantMarshalingNeedTrailingPadding(ULONG *flags)
{
    return FALSE;
}

WINOLEAPI BOOL WINAPI CoDoesOtherSideSupportUDTMarshaling(ULONG *flags)
{
    return TRUE;
}

/*
 * the call context when the call being served is to the given object, else nothing and no error. guess: the object
 * is not compared, any served call counts.
 */
WINOLEAPI HRESULT WINAPI CoGetCallContextOfObject(IUnknown *server, REFIID iid, void **context)
{
    if (!context)
        return E_POINTER;
    *context = nullptr;
    OLE_THREAD *t = OleThreadState(FALSE);
    if (!t || !t->OrpcCall)
        return S_OK;
    return CoGetCallContext(iid, context);
}
