#include "winrt.h"

typedef struct ASYNC {
    WRT_AGILE Object;
    SRWLOCK Lock;
    const WRT_ASYNC_TYPE *Type;
    WRT_CLASS Class;
    UINT32 Id;
    AsyncStatus Status;
    HRESULT Error;
    BOOL Closed;
    IAsyncCompletedHandler *Handler;
    IUnknown *Progress;
    UINT32 Value;
    IUnknown *Result;
} ASYNC;

/* IAsyncOperationWithProgress: the progress handler comes before the completed one. */
typedef struct IWrtAsyncProgress IWrtAsyncProgress;
#define IWrtAsyncProgress_METHODS(T)                                                                                   \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *put_Progress)(T * This, IUnknown * handler);                                            \
    HRESULT(STDMETHODCALLTYPE *get_Progress)(T * This, IUnknown * *handler);                                           \
    HRESULT(STDMETHODCALLTYPE *put_Completed)(T * This, IAsyncCompletedHandler * handler);                             \
    HRESULT(STDMETHODCALLTYPE *get_Completed)(T * This, IAsyncCompletedHandler * *handler);                            \
    HRESULT(STDMETHODCALLTYPE *GetResults)(T * This, void *result);
MIME_COM_INTERFACE(IWrtAsyncProgress)

static volatile LONG g_ids;

static ASYNC *async_of(void *iface)
{
    return (ASYNC *)((WRT_IFACE *)iface)->Owner;
}

WRT_AGILE_THUNKS(IAsyncInfo)
WRT_AGILE_THUNKS(IAsyncAction)
WRT_AGILE_THUNKS(IAsyncOperation)
WRT_AGILE_THUNKS(IWrtAsyncProgress)

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

/* an operation that is over cannot be cancelled any more. */
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

/* the handler is given once, and called at once since the operation is over. */
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
        handler->lpVtbl->Invoke(handler, WrtAgileIface(&a->Object), status);
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

/* the error when the operation failed, then the result. */
static HRESULT get_results(ASYNC *a, void *result)
{
    WRT_RESULT_KIND kind = a->Type->Result;
    if (kind != WRT_RESULT_NONE && !result)
        return E_POINTER;
    if (kind == WRT_RESULT_BOOL)
        *(BOOLEAN *)result = FALSE;
    else if (kind == WRT_RESULT_UINT32)
        *(UINT32 *)result = 0;
    else if (kind == WRT_RESULT_OBJECT)
        *(void **)result = nullptr;
    AcquireSRWLockShared(&a->Lock);
    HRESULT hr = live(a);
    if (SUCCEEDED(hr))
        hr = a->Status == AsyncStatus_Error ? a->Error : S_OK;
    if (SUCCEEDED(hr)) {
        if (kind == WRT_RESULT_BOOL)
            *(BOOLEAN *)result = a->Value != 0;
        else if (kind == WRT_RESULT_UINT32)
            *(UINT32 *)result = a->Value;
        else if (kind == WRT_RESULT_OBJECT && a->Result) {
            a->Result->lpVtbl->AddRef(a->Result);
            *(void **)result = a->Result;
        }
    }
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
    return get_results(async_of(This), nullptr);
}

static HRESULT STDMETHODCALLTYPE op_put_completed(IAsyncOperation *This, IAsyncCompletedHandler *handler)
{
    return put_completed(async_of(This), handler);
}

static HRESULT STDMETHODCALLTYPE op_get_completed(IAsyncOperation *This, IAsyncCompletedHandler **handler)
{
    return get_completed(async_of(This), handler);
}

static HRESULT STDMETHODCALLTYPE op_get_results(IAsyncOperation *This, void *result)
{
    return get_results(async_of(This), result);
}

/* progress is never reported: the work is done before the handler can be given. */
static HRESULT STDMETHODCALLTYPE progress_put_progress(IWrtAsyncProgress *This, IUnknown *handler)
{
    ASYNC *a = async_of(This);
    AcquireSRWLockExclusive(&a->Lock);
    HRESULT hr = live(a);
    if (SUCCEEDED(hr)) {
        if (handler)
            handler->lpVtbl->AddRef(handler);
        if (a->Progress)
            a->Progress->lpVtbl->Release(a->Progress);
        a->Progress = handler;
    }
    ReleaseSRWLockExclusive(&a->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE progress_get_progress(IWrtAsyncProgress *This, IUnknown **handler)
{
    ASYNC *a = async_of(This);
    if (!handler)
        return E_POINTER;
    AcquireSRWLockShared(&a->Lock);
    HRESULT hr = live(a);
    *handler = SUCCEEDED(hr) ? a->Progress : nullptr;
    if (*handler)
        (*handler)->lpVtbl->AddRef(*handler);
    ReleaseSRWLockShared(&a->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE progress_put_completed(IWrtAsyncProgress *This, IAsyncCompletedHandler *handler)
{
    return put_completed(async_of(This), handler);
}

static HRESULT STDMETHODCALLTYPE progress_get_completed(IWrtAsyncProgress *This, IAsyncCompletedHandler **handler)
{
    return get_completed(async_of(This), handler);
}

static HRESULT STDMETHODCALLTYPE progress_get_results(IWrtAsyncProgress *This, void *result)
{
    return get_results(async_of(This), result);
}

static const IAsyncActionVtbl action_vtbl = {WRT_INSPECTABLE_ENTRIES(IAsyncAction), action_put_completed,
                                             action_get_completed, action_get_results};
static const IAsyncOperationVtbl op_vtbl = {WRT_INSPECTABLE_ENTRIES(IAsyncOperation), op_put_completed,
                                            op_get_completed, op_get_results};
static const IWrtAsyncProgressVtbl progress_vtbl = {WRT_INSPECTABLE_ENTRIES(IWrtAsyncProgress),
                                                    progress_put_progress,
                                                    progress_get_progress,
                                                    progress_put_completed,
                                                    progress_get_completed,
                                                    progress_get_results};

static void async_destroy(WRT_OBJECT *object)
{
    ASYNC *a = (ASYNC *)object;
    if (a->Handler)
        a->Handler->lpVtbl->Release(a->Handler);
    if (a->Progress)
        a->Progress->lpVtbl->Release(a->Progress);
    if (a->Result)
        a->Result->lpVtbl->Release(a->Result);
    HeapFree(GetProcessHeap(), 0, a);
}

HRESULT WrtAsyncCompleted(const WRT_ASYNC_TYPE *type, HRESULT error, UINT32 value, IUnknown *object,
                          void **operation)
{
    *operation = nullptr;
    ASYNC *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*a));
    if (!a) {
        if (object)
            object->lpVtbl->Release(object);
        return E_OUTOFMEMORY;
    }
    /* each object carries its class, which names the operation it is. */
    a->Class = (WRT_CLASS){type->Name, 2, {type->Iid, &IID_IAsyncInfo}, 0, async_destroy};
    const void *vtables[2] = {type->Progress                     ? (const void *)&progress_vtbl
                              : type->Result == WRT_RESULT_NONE ? (const void *)&action_vtbl
                                                                : (const void *)&op_vtbl,
                              &info_vtbl};
    WrtAgileInit(&a->Object, &a->Class, vtables);
    InitializeSRWLock(&a->Lock);
    a->Type = type;
    a->Id = (UINT32)InterlockedIncrement(&g_ids);
    a->Status = FAILED(error) ? AsyncStatus_Error : AsyncStatus_Completed;
    a->Error = error;
    a->Value = value;
    a->Result = object;
    *operation = WrtAgileIface(&a->Object);
    return S_OK;
}
