/*
 * asynchronous operations of the windows runtime that are over when they are handed out. a runtime class that has
 * nothing to wait for reports its result through the same interfaces as one that does: the id, the status, the error,
 * the completed handler and the result.
 */
#include "combase_private.h"
#include <mime/winrt_object.h>
#include <mime/win32/winrt_async.h>

typedef struct ASYNC {
    WRT_OBJECT Base;
    SRWLOCK Lock;
    UINT32 Id;
    AsyncStatus Status;
    HRESULT Error;
    BOOL Closed;
    IAsyncCompletedHandler *Handler;
    BOOLEAN Flag;
    IUnknown *Object;
} ASYNC;

static volatile LONG g_ids;

static ASYNC *async_of(void *iface)
{
    return (ASYNC *)((WRT_IFACE *)iface)->Owner;
}

WRT_INSPECTABLE_THUNKS(IAsyncInfo)
WRT_INSPECTABLE_THUNKS(IAsyncAction)
WRT_INSPECTABLE_THUNKS(IAsyncOperation)

/* the same checks for every call that reads the state. */
static HRESULT live(ASYNC *a)
{
    return a->Closed ? E_ILLEGAL_METHOD_CALL : S_OK;
}

static HRESULT STDMETHODCALLTYPE info_get_id(IAsyncInfo *This, UINT32 *id)
{
    ASYNC *a = async_of(This);
    if (!id)
        return E_POINTER;
    AcquireSRWLockShared(&a->Lock);
    HRESULT hr = live(a);
    *id = SUCCEEDED(hr) ? a->Id : 0;
    ReleaseSRWLockShared(&a->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE info_get_status(IAsyncInfo *This, AsyncStatus *status)
{
    ASYNC *a = async_of(This);
    if (!status)
        return E_POINTER;
    AcquireSRWLockShared(&a->Lock);
    HRESULT hr = live(a);
    *status = SUCCEEDED(hr) ? a->Status : AsyncStatus_Started;
    ReleaseSRWLockShared(&a->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE info_get_error(IAsyncInfo *This, HRESULT *error)
{
    ASYNC *a = async_of(This);
    if (!error)
        return E_POINTER;
    AcquireSRWLockShared(&a->Lock);
    HRESULT hr = live(a);
    *error = SUCCEEDED(hr) ? a->Error : S_OK;
    ReleaseSRWLockShared(&a->Lock);
    return hr;
}

/* an operation that has finished cannot be cancelled any more. */
static HRESULT STDMETHODCALLTYPE info_cancel(IAsyncInfo *This)
{
    ASYNC *a = async_of(This);
    AcquireSRWLockShared(&a->Lock);
    HRESULT hr = live(a);
    ReleaseSRWLockShared(&a->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE info_close(IAsyncInfo *This)
{
    ASYNC *a = async_of(This);
    AcquireSRWLockExclusive(&a->Lock);
    a->Closed = TRUE;
    ReleaseSRWLockExclusive(&a->Lock);
    return S_OK;
}

static const IAsyncInfoVtbl info_vtbl = {WRT_INSPECTABLE_ENTRIES(IAsyncInfo), info_get_id, info_get_status,
                                         info_get_error, info_cancel, info_close};

/* the handler is given once, and called at once as the operation is over. */
static HRESULT put_completed(ASYNC *a, IAsyncCompletedHandler *handler)
{
    AcquireSRWLockExclusive(&a->Lock);
    HRESULT hr = live(a);
    if (SUCCEEDED(hr) && a->Handler)
        hr = E_ILLEGAL_DELEGATE_ASSIGNMENT;
    if (SUCCEEDED(hr) && !handler)
        hr = E_INVALIDARG;
    AsyncStatus status = a->Status;
    if (SUCCEEDED(hr)) {
        handler->lpVtbl->AddRef(handler);
        a->Handler = handler;
    }
    ReleaseSRWLockExclusive(&a->Lock);
    if (SUCCEEDED(hr))
        handler->lpVtbl->Invoke(handler, (IInspectable *)&a->Base.Ifaces[0], status);
    return hr;
}

static HRESULT get_completed(ASYNC *a, IAsyncCompletedHandler **handler)
{
    if (!handler)
        return E_POINTER;
    AcquireSRWLockShared(&a->Lock);
    HRESULT hr = live(a);
    *handler = SUCCEEDED(hr) ? a->Handler : nullptr;
    if (*handler)
        (*handler)->lpVtbl->AddRef(*handler);
    ReleaseSRWLockShared(&a->Lock);
    return hr;
}

/* the error when the operation failed, and no result unless it is over. */
static HRESULT results_state(ASYNC *a)
{
    AcquireSRWLockShared(&a->Lock);
    HRESULT hr = live(a);
    if (SUCCEEDED(hr))
        hr = a->Status == AsyncStatus_Error ? a->Error : a->Status == AsyncStatus_Completed ? S_OK : E_ILLEGAL_METHOD_CALL;
    ReleaseSRWLockShared(&a->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE action_put_completed(IAsyncAction *This, IAsyncCompletedHandler *handler)
{
    return put_completed(async_of(This), handler);
}

static HRESULT STDMETHODCALLTYPE action_get_completed(IAsyncAction *This, IAsyncCompletedHandler **handler)
{
    return get_completed(async_of(This), handler);
}

static HRESULT STDMETHODCALLTYPE action_get_results(IAsyncAction *This)
{
    return results_state(async_of(This));
}

static HRESULT STDMETHODCALLTYPE op_put_completed(IAsyncOperation *This, IAsyncCompletedHandler *handler)
{
    return put_completed(async_of(This), handler);
}

static HRESULT STDMETHODCALLTYPE op_get_completed(IAsyncOperation *This, IAsyncCompletedHandler **handler)
{
    return get_completed(async_of(This), handler);
}

static HRESULT STDMETHODCALLTYPE bool_get_results(IAsyncOperation *This, void *result)
{
    ASYNC *a = async_of(This);
    if (!result)
        return E_POINTER;
    *(BOOLEAN *)result = FALSE;
    HRESULT hr = results_state(a);
    if (SUCCEEDED(hr))
        *(BOOLEAN *)result = a->Flag;
    return hr;
}

static HRESULT STDMETHODCALLTYPE object_get_results(IAsyncOperation *This, void *result)
{
    ASYNC *a = async_of(This);
    if (!result)
        return E_POINTER;
    *(void **)result = nullptr;
    HRESULT hr = results_state(a);
    if (SUCCEEDED(hr) && a->Object) {
        a->Object->lpVtbl->AddRef(a->Object);
        *(void **)result = a->Object;
    }
    return hr;
}

static const IAsyncActionVtbl action_vtbl = {WRT_INSPECTABLE_ENTRIES(IAsyncAction), action_put_completed,
                                             action_get_completed, action_get_results};
static const IAsyncOperationVtbl bool_vtbl = {WRT_INSPECTABLE_ENTRIES(IAsyncOperation), op_put_completed,
                                              op_get_completed, bool_get_results};
static const IAsyncOperationVtbl object_vtbl = {WRT_INSPECTABLE_ENTRIES(IAsyncOperation), op_put_completed,
                                                op_get_completed, object_get_results};

static void async_destroy(WRT_OBJECT *object)
{
    ASYNC *a = (ASYNC *)object;
    if (a->Handler)
        a->Handler->lpVtbl->Release(a->Handler);
    if (a->Object)
        a->Object->lpVtbl->Release(a->Object);
    CbFree(a);
}

static const WRT_CLASS g_classes[CB_ASYNC_KINDS] = {
    [CB_ASYNC_ACTION] = {L"Windows.Foundation.IAsyncAction", 2, {&IID_IAsyncAction, &IID_IAsyncInfo}, 0, async_destroy},
    [CB_ASYNC_BOOL] = {L"Windows.Foundation.IAsyncOperation`1<Boolean>",
                       2,
                       {&IID_IAsyncOperationBool, &IID_IAsyncInfo},
                       0,
                       async_destroy},
    [CB_ASYNC_KEY_RETRIEVAL] = {L"Windows.Foundation.IAsyncOperation`1<Windows.Security.Credentials.KeyCredentialRetrievalResult>",
                                2,
                                {&IID_IAsyncOperationKeyRetrieval, &IID_IAsyncInfo},
                                0,
                                async_destroy},
    [CB_ASYNC_PROFILE] = {L"Windows.Foundation.IAsyncOperation`1<Windows.Networking.Connectivity.ConnectionProfile>",
                          2,
                          {&IID_IAsyncOperationConnectionProfile, &IID_IAsyncInfo},
                          0,
                          async_destroy},
};

HRESULT CbAsyncFinished(CB_ASYNC_KIND kind, HRESULT error, BOOLEAN flag, IUnknown *object, void **operation)
{
    *operation = nullptr;
    ASYNC *a = CbAlloc(sizeof(*a));
    if (!a) {
        if (object)
            object->lpVtbl->Release(object);
        return E_OUTOFMEMORY;
    }
    memset(a, 0, sizeof(*a));
    const void *vtables[2] = {kind == CB_ASYNC_ACTION ? (const void *)&action_vtbl
                              : kind == CB_ASYNC_BOOL ? (const void *)&bool_vtbl
                                                      : (const void *)&object_vtbl,
                              &info_vtbl};
    WrtInit(&a->Base, &g_classes[kind], vtables);
    InitializeSRWLock(&a->Lock);
    a->Id = (UINT32)InterlockedIncrement(&g_ids);
    a->Status = FAILED(error) ? AsyncStatus_Error : AsyncStatus_Completed;
    a->Error = error;
    a->Flag = flag;
    a->Object = object;
    *operation = &a->Base.Ifaces[0];
    return S_OK;
}
