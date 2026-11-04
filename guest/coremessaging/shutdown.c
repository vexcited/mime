#include "coremessaging_private.h"

/* ------------------------------------------------------------------ Windows.Foundation.Deferral */

typedef struct DEFERRAL {
    CM_OBJECT Object;
    CM_QUEUE *Queue;
    volatile LONG Done;
} DEFERRAL;

CM_INSPECTABLE_THUNKS(IDeferral)
CM_INSPECTABLE_THUNKS(ICmClosable)

static void deferral_destroy(WRT_OBJECT *object)
{
    DEFERRAL *deferral = (DEFERRAL *)object;
    CmRelease((WRT_IFACE *)CmIface(&deferral->Queue->Object));
    HeapFree(GetProcessHeap(), 0, deferral);
}

static HRESULT deferral_complete(DEFERRAL *deferral)
{
    if (!InterlockedExchange(&deferral->Done, 1) && !InterlockedDecrement(&deferral->Queue->Deferrals))
        CmQueueDeferralDone(deferral->Queue);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE deferral_complete_iface(IDeferral *This)
{
    return deferral_complete((DEFERRAL *)CmOwner(This));
}

static HRESULT STDMETHODCALLTYPE deferral_close(ICmClosable *This)
{
    return deferral_complete((DEFERRAL *)CmOwner(This));
}

static const IDeferralVtbl deferral_vtbl = {WRT_INSPECTABLE_ENTRIES(IDeferral), deferral_complete_iface};
static const ICmClosableVtbl closable_vtbl = {WRT_INSPECTABLE_ENTRIES(ICmClosable), deferral_close};
static const WRT_CLASS deferral_class = {
    L"Windows.Foundation.Deferral", 3, {&IID_IDeferral, &IID_CmClosable, &IID_CmWeakReferenceSource}, 1 << 2,
    deferral_destroy};

/* ------------------------------------------------------------------ DispatcherQueueShutdownStartingEventArgs */

typedef struct ARGS {
    CM_OBJECT Object;
    CM_QUEUE *Queue;
    volatile LONG Finished;
} ARGS;

CM_INSPECTABLE_THUNKS(IDispatcherQueueShutdownStartingEventArgs)

static void args_destroy(WRT_OBJECT *object)
{
    ARGS *args = (ARGS *)object;
    CmRelease((WRT_IFACE *)CmIface(&args->Queue->Object));
    HeapFree(GetProcessHeap(), 0, args);
}

/* a deferral is taken while the handlers run, later the shutdown no longer waits for one. */
static HRESULT STDMETHODCALLTYPE args_get_deferral(IDispatcherQueueShutdownStartingEventArgs *This, IDeferral **result)
{
    ARGS *args = (ARGS *)CmOwner(This);
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (args->Finished)
        return E_ILLEGAL_METHOD_CALL;
    DEFERRAL *deferral = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*deferral));
    if (!deferral)
        return E_OUTOFMEMORY;
    const void *vtables[3] = {&deferral_vtbl, &closable_vtbl, &CmWeakSourceVtbl};
    CmInit(&deferral->Object, &deferral_class, vtables);
    CmAddRef((WRT_IFACE *)CmIface(&args->Queue->Object));
    deferral->Queue = args->Queue;
    InterlockedIncrement(&args->Queue->Deferrals);
    *result = (IDeferral *)CmIface(&deferral->Object);
    return S_OK;
}

static const IDispatcherQueueShutdownStartingEventArgsVtbl args_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDispatcherQueueShutdownStartingEventArgs), args_get_deferral};
static const WRT_CLASS args_class = {L"Windows.System.DispatcherQueueShutdownStartingEventArgs",
                                     2,
                                     {&IID_IDispatcherQueueShutdownStartingEventArgs, &IID_CmWeakReferenceSource},
                                     1 << 1,
                                     args_destroy};

/* on the queue's thread, first of the shutdown: ShutdownStarting, then the wait for the deferrals it handed out. */
void CmShutdownStarting(CM_QUEUE *queue, void *context, UINT64 value)
{
    ARGS *args = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*args));
    queue->Deferrals = 1;
    if (args) {
        const void *vtables[2] = {&args_vtbl, &CmWeakSourceVtbl};
        CmInit(&args->Object, &args_class, vtables);
        CmAddRef((WRT_IFACE *)CmIface(&queue->Object));
        args->Queue = queue;
        CmEventInvoke(&queue->Starting, CmIface(&queue->Object), CmIface(&args->Object));
        args->Finished = TRUE;
    }
    AcquireSRWLockExclusive(&queue->Lock);
    queue->State = CM_DEFERRING;
    ReleaseSRWLockExclusive(&queue->Lock);
    if (!InterlockedDecrement(&queue->Deferrals))
        CmQueueDeferralDone(queue);
    if (args)
        CmRelease((WRT_IFACE *)CmIface(&args->Object));
}

/* ------------------------------------------------------------------ IAsyncAction */

struct CM_ASYNC {
    CM_OBJECT Object;
    SRWLOCK Lock;
    UINT32 Id;
    AsyncStatus Status;
    BOOL Closed;
    BOOL HandlerSet;
    IAsyncCompletedHandler *Handler;
};

static volatile LONG g_async_ids;

CM_INSPECTABLE_THUNKS(IAsyncAction)
CM_INSPECTABLE_THUNKS(IAsyncInfo)

static CM_ASYNC *async_of(void *iface)
{
    return (CM_ASYNC *)CmOwner(iface);
}

IAsyncAction *CmAsyncIface(CM_ASYNC *async)
{
    return (IAsyncAction *)CmIface(&async->Object);
}

static void async_destroy(WRT_OBJECT *object)
{
    CM_ASYNC *async = (CM_ASYNC *)object;
    if (async->Handler)
        async->Handler->lpVtbl->Release(async->Handler);
    HeapFree(GetProcessHeap(), 0, async);
}

/* a handler set after completion runs at once, on the thread that sets it. */
static HRESULT STDMETHODCALLTYPE async_put_completed(IAsyncAction *This, IAsyncCompletedHandler *handler)
{
    CM_ASYNC *async = async_of(This);
    AcquireSRWLockExclusive(&async->Lock);
    if (async->HandlerSet) {
        ReleaseSRWLockExclusive(&async->Lock);
        return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    }
    if (async->Closed) {
        ReleaseSRWLockExclusive(&async->Lock);
        return E_ILLEGAL_METHOD_CALL;
    }
    async->HandlerSet = TRUE;
    AsyncStatus status = async->Status;
    if (handler && status == AsyncStatus_Started) {
        handler->lpVtbl->AddRef(handler);
        async->Handler = handler;
    }
    ReleaseSRWLockExclusive(&async->Lock);
    if (handler && status != AsyncStatus_Started)
        handler->lpVtbl->Invoke(handler, (IInspectable *)This, status);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE async_get_completed(IAsyncAction *This, IAsyncCompletedHandler **handler)
{
    CM_ASYNC *async = async_of(This);
    if (!handler)
        return E_POINTER;
    AcquireSRWLockShared(&async->Lock);
    *handler = async->Handler;
    if (*handler)
        (*handler)->lpVtbl->AddRef(*handler);
    ReleaseSRWLockShared(&async->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE async_get_results(IAsyncAction *This)
{
    CM_ASYNC *async = async_of(This);
    AcquireSRWLockShared(&async->Lock);
    HRESULT hr = async->Closed ? E_ILLEGAL_METHOD_CALL
                 : async->Status == AsyncStatus_Completed ? S_OK
                                                          : E_ILLEGAL_METHOD_CALL;
    ReleaseSRWLockShared(&async->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE async_get_id(IAsyncInfo *This, UINT32 *id)
{
    if (!id)
        return E_POINTER;
    *id = async_of(This)->Id;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE async_get_status(IAsyncInfo *This, AsyncStatus *status)
{
    CM_ASYNC *async = async_of(This);
    if (!status)
        return E_POINTER;
    AcquireSRWLockShared(&async->Lock);
    *status = async->Status;
    HRESULT hr = async->Closed ? E_ILLEGAL_METHOD_CALL : S_OK;
    ReleaseSRWLockShared(&async->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE async_get_error(IAsyncInfo *This, HRESULT *error)
{
    if (!error)
        return E_POINTER;
    *error = S_OK;
    return async_of(This)->Closed ? E_ILLEGAL_METHOD_CALL : S_OK;
}

/* the shutdown goes on whatever the caller asks. */
static HRESULT STDMETHODCALLTYPE async_cancel(IAsyncInfo *This)
{
    return async_of(This)->Closed ? E_ILLEGAL_METHOD_CALL : S_OK;
}

static HRESULT STDMETHODCALLTYPE async_close(IAsyncInfo *This)
{
    CM_ASYNC *async = async_of(This);
    AcquireSRWLockExclusive(&async->Lock);
    HRESULT hr = async->Status == AsyncStatus_Started ? E_ILLEGAL_STATE_CHANGE_ : S_OK;
    if (SUCCEEDED(hr))
        async->Closed = TRUE;
    ReleaseSRWLockExclusive(&async->Lock);
    return hr;
}

static const IAsyncActionVtbl async_vtbl = {WRT_INSPECTABLE_ENTRIES(IAsyncAction), async_put_completed,
                                            async_get_completed, async_get_results};
static const IAsyncInfoVtbl info_vtbl = {WRT_INSPECTABLE_ENTRIES(IAsyncInfo), async_get_id,   async_get_status,
                                         async_get_error,                     async_cancel,   async_close};
static const WRT_CLASS async_class = {L"Windows.Foundation.IAsyncAction",
                                      3,
                                      {&IID_IAsyncAction, &IID_IAsyncInfo, &IID_CmWeakReferenceSource},
                                      1 << 2,
                                      async_destroy};

HRESULT CmAsyncCreate(CM_ASYNC **result)
{
    CM_ASYNC *async = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*async));
    *result = async;
    if (!async)
        return E_OUTOFMEMORY;
    const void *vtables[3] = {&async_vtbl, &info_vtbl, &CmWeakSourceVtbl};
    CmInit(&async->Object, &async_class, vtables);
    InitializeSRWLock(&async->Lock);
    async->Id = (UINT32)InterlockedIncrement(&g_async_ids);
    async->Status = AsyncStatus_Started;
    return S_OK;
}

void CmAsyncComplete(CM_ASYNC *async)
{
    AcquireSRWLockExclusive(&async->Lock);
    async->Status = AsyncStatus_Completed;
    IAsyncCompletedHandler *handler = async->Handler;
    if (handler)
        handler->lpVtbl->AddRef(handler);
    ReleaseSRWLockExclusive(&async->Lock);
    if (handler) {
        handler->lpVtbl->Invoke(handler, CmIface(&async->Object), AsyncStatus_Completed);
        handler->lpVtbl->Release(handler);
    }
}
