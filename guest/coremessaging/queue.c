#include "coremessaging_private.h"

#define WM_DQ_WAKE (WM_APP + 1)
#define WM_DQ_IDLE (WM_APP + 2)
#define IDLE_TIMER 1

static const WCHAR QUEUE_CLASS[] = L"Windows.System.DispatcherQueue";
static const WCHAR CONTROLLER_CLASS[] = L"Windows.System.DispatcherQueueController";
static const WCHAR WINDOW_CLASS[] = L"Windows.System.DispatcherQueue";
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static CM_WORK *pop(CM_QUEUE *queue, int priority)
{
    CM_WORK *work = queue->Work[priority].Head;
    if (work) {
        queue->Work[priority].Head = work->Next;
        if (!work->Next)
            queue->Work[priority].Tail = nullptr;
    }
    return work;
}

static BOOL busy(CM_QUEUE *queue)
{
    return queue->Work[CM_HIGH].Head || queue->Work[CM_NORMAL].Head;
}

/* keeps one wake posted while high or normal work waits, and one idle wake while only low work does. */
static void schedule(CM_QUEUE *queue)
{
    if (!queue->Window)
        return;
    if (busy(queue)) {
        if (!queue->WakePosted && PostMessageW(queue->Window, WM_DQ_WAKE, 0, 0))
            queue->WakePosted = TRUE;
    } else if (queue->Work[CM_LOW].Head && !queue->IdlePosted && PostMessageW(queue->Window, WM_DQ_IDLE, 0, 0)) {
        queue->IdlePosted = TRUE;
    }
}

static void run(CM_QUEUE *queue, CM_WORK *work)
{
    if (work->Handler) {
        work->Handler->lpVtbl->Invoke(work->Handler);
        work->Handler->lpVtbl->Release(work->Handler);
    } else {
        work->Run(queue, work->Context, work->Value);
    }
    HeapFree(GetProcessHeap(), 0, work);
}

/* one item a message, so input and nested loops interleave with queued work. */
static void wake(CM_QUEUE *queue)
{
    AcquireSRWLockExclusive(&queue->Lock);
    queue->WakePosted = FALSE;
    CM_WORK *work = pop(queue, CM_HIGH);
    if (!work)
        work = pop(queue, CM_NORMAL);
    schedule(queue);
    ReleaseSRWLockExclusive(&queue->Lock);
    if (work)
        run(queue, work);
}

/* low priority work runs only when the thread has nothing else waiting. */
static void idle(CM_QUEUE *queue)
{
    CM_WORK *work = nullptr;
    AcquireSRWLockExclusive(&queue->Lock);
    queue->IdlePosted = FALSE;
    if (busy(queue)) {
        schedule(queue);
    } else if (HIWORD(GetQueueStatus(QS_INPUT | QS_POSTMESSAGE | QS_SENDMESSAGE))) {
        if (queue->Window && SetTimer(queue->Window, IDLE_TIMER, USER_TIMER_MINIMUM, nullptr))
            queue->IdlePosted = TRUE;
    } else {
        work = pop(queue, CM_LOW);
        schedule(queue);
    }
    ReleaseSRWLockExclusive(&queue->Lock);
    if (work)
        run(queue, work);
}

static LRESULT CALLBACK queue_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    CM_QUEUE *queue = (CM_QUEUE *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (queue && message == WM_DQ_WAKE) {
        wake(queue);
        return 0;
    }
    if (queue && message == WM_DQ_IDLE) {
        idle(queue);
        return 0;
    }
    if (queue && message == WM_TIMER && wparam == IDLE_TIMER) {
        KillTimer(window, IDLE_TIMER);
        idle(queue);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static BOOL CALLBACK register_class(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = queue_proc;
    wc.hInstance = CmModule;
    wc.lpszClassName = WINDOW_CLASS;
    return RegisterClassW(&wc) != 0;
}

BOOL CmQueuePush(CM_QUEUE *queue, int priority, CM_WORK *work, BOOL internal)
{
    work->Next = nullptr;
    AcquireSRWLockExclusive(&queue->Lock);
    if (!queue->Window || (queue->State == CM_SHUT && !internal)) {
        ReleaseSRWLockExclusive(&queue->Lock);
        return FALSE;
    }
    if (queue->Work[priority].Tail)
        queue->Work[priority].Tail->Next = work;
    else
        queue->Work[priority].Head = work;
    queue->Work[priority].Tail = work;
    schedule(queue);
    ReleaseSRWLockExclusive(&queue->Lock);
    return TRUE;
}

BOOL CmQueuePost(CM_QUEUE *queue, int priority, void (*run)(CM_QUEUE *, void *, UINT64), void *context, UINT64 value,
                 BOOL internal)
{
    CM_WORK *work = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*work));
    if (!work)
        return FALSE;
    work->Run = run;
    work->Context = context;
    work->Value = value;
    if (CmQueuePush(queue, priority, work, internal))
        return TRUE;
    HeapFree(GetProcessHeap(), 0, work);
    return FALSE;
}

CM_QUEUE *CmQueueForThread(void)
{
    return CmTls != TLS_OUT_OF_INDEXES ? TlsGetValue(CmTls) : nullptr;
}

/* ------------------------------------------------------------------ IDispatcherQueue */

CM_INSPECTABLE_THUNKS(IDispatcherQueue)
CM_INSPECTABLE_THUNKS(IDispatcherQueue2)

static CM_QUEUE *queue_of(void *iface)
{
    return (CM_QUEUE *)CmOwner(iface);
}

static void queue_destroy(WRT_OBJECT *object)
{
    CM_QUEUE *queue = (CM_QUEUE *)object;
    for (int i = 0; i < CM_PRIORITIES; i++) {
        CM_WORK *work;
        while ((work = pop(queue, i))) {
            if (work->Handler)
                work->Handler->lpVtbl->Release(work->Handler);
            HeapFree(GetProcessHeap(), 0, work);
        }
    }
    CmEventClear(&queue->Starting);
    CmEventClear(&queue->Completed);
    HeapFree(GetProcessHeap(), 0, queue);
}

static HRESULT STDMETHODCALLTYPE queue_create_timer(IDispatcherQueue *This, IDispatcherQueueTimer **result)
{
    if (!result)
        return E_POINTER;
    return CmTimerCreate(queue_of(This), result);
}

static HRESULT enqueue(CM_QUEUE *queue, INT32 priority, IDispatcherQueueHandler *callback, BOOLEAN *result)
{
    if (!result)
        return E_POINTER;
    *result = FALSE;
    if (!callback)
        return E_POINTER;
    int level;
    switch (priority) {
    case -10:
        level = CM_LOW;
        break;
    case 0:
        level = CM_NORMAL;
        break;
    case 10:
        level = CM_HIGH;
        break;
    default:
        return E_INVALIDARG;
    }
    IUnknown *agile;
    if (FAILED(callback->lpVtbl->QueryInterface(callback, &IID_IAgileObject, (void **)&agile)))
        return CM_E_NOT_AGILE;
    agile->lpVtbl->Release(agile);
    CM_WORK *work = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*work));
    if (!work)
        return E_OUTOFMEMORY;
    callback->lpVtbl->AddRef(callback);
    work->Handler = callback;
    if (!CmQueuePush(queue, level, work, FALSE)) {
        callback->lpVtbl->Release(callback);
        HeapFree(GetProcessHeap(), 0, work);
        return S_OK;
    }
    *result = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE queue_try_enqueue(IDispatcherQueue *This, IDispatcherQueueHandler *callback,
                                                   BOOLEAN *result)
{
    return enqueue(queue_of(This), 0, callback, result);
}

static HRESULT STDMETHODCALLTYPE queue_try_enqueue_priority(IDispatcherQueue *This, INT32 priority,
                                                            IDispatcherQueueHandler *callback, BOOLEAN *result)
{
    return enqueue(queue_of(This), priority, callback, result);
}

static HRESULT STDMETHODCALLTYPE queue_add_starting(IDispatcherQueue *This, ICmEventHandler *handler,
                                                    EventRegistrationToken *token)
{
    return CmEventAdd(&queue_of(This)->Starting, handler, token);
}

static HRESULT STDMETHODCALLTYPE queue_remove_starting(IDispatcherQueue *This, EventRegistrationToken token)
{
    return CmEventRemove(&queue_of(This)->Starting, token);
}

static HRESULT STDMETHODCALLTYPE queue_add_completed(IDispatcherQueue *This, ICmEventHandler *handler,
                                                     EventRegistrationToken *token)
{
    return CmEventAdd(&queue_of(This)->Completed, handler, token);
}

static HRESULT STDMETHODCALLTYPE queue_remove_completed(IDispatcherQueue *This, EventRegistrationToken token)
{
    return CmEventRemove(&queue_of(This)->Completed, token);
}

static HRESULT STDMETHODCALLTYPE queue_has_thread_access(IDispatcherQueue2 *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = queue_of(This)->Thread == GetCurrentThreadId();
    return S_OK;
}

static const IDispatcherQueueVtbl queue_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDispatcherQueue), queue_create_timer,    queue_try_enqueue,   queue_try_enqueue_priority,
    queue_add_starting,                        queue_remove_starting, queue_add_completed, queue_remove_completed,
};
static const IDispatcherQueue2Vtbl queue2_vtbl = {WRT_INSPECTABLE_ENTRIES(IDispatcherQueue2), queue_has_thread_access};
static const WRT_CLASS queue_class = {
    QUEUE_CLASS, 3, {&IID_IDispatcherQueue, &IID_IDispatcherQueue2, &IID_CmWeakReferenceSource}, 1 << 2, queue_destroy};

/*
 * a queue belongs to the thread that makes it, which keeps a reference until the queue shuts down. its work arrives
 * through a message window, so it runs from whatever loop the thread pumps.
 */
HRESULT CmQueueCreate(BOOL dedicated, CM_QUEUE **result)
{
    *result = nullptr;
    CM_QUEUE *queue = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*queue));
    if (!queue)
        return E_OUTOFMEMORY;
    const void *vtables[3] = {&queue_vtbl, &queue2_vtbl, &CmWeakSourceVtbl};
    CmInit(&queue->Object, &queue_class, vtables);
    InitializeSRWLock(&queue->Lock);
    InitializeSRWLock(&queue->Starting.Lock);
    InitializeSRWLock(&queue->Completed.Lock);
    queue->Dedicated = dedicated;
    queue->Thread = GetCurrentThreadId();
    InitOnceExecuteOnce(&g_once, register_class, nullptr, nullptr);
    queue->Window = CreateWindowExW(0, WINDOW_CLASS, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, CmModule, nullptr);
    if (!queue->Window) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CmRelease((WRT_IFACE *)CmIface(&queue->Object));
        return FAILED(hr) ? hr : E_FAIL;
    }
    SetWindowLongPtrW(queue->Window, GWLP_USERDATA, (LONG_PTR)queue);
    CmAddRef((WRT_IFACE *)CmIface(&queue->Object));
    TlsSetValue(CmTls, queue);
    *result = queue;
    return S_OK;
}

/* the last step of a shutdown, on the queue's thread once the work queued before it has run. */
static void queue_drained(CM_QUEUE *queue, void *context, UINT64 value)
{
    CmEventInvoke(&queue->Completed, CmIface(&queue->Object), nullptr);
    AcquireSRWLockExclusive(&queue->Lock);
    CM_ASYNC *async = queue->ShutdownAsync;
    CM_CONTROLLER *controller = queue->ShutdownController;
    HWND window = queue->Window;
    queue->ShutdownAsync = nullptr;
    queue->ShutdownController = nullptr;
    queue->Window = nullptr;
    queue->Exit = TRUE;
    ReleaseSRWLockExclusive(&queue->Lock);
    if (async) {
        CmAsyncComplete(async);
        IAsyncAction *iface = CmAsyncIface(async);
        iface->lpVtbl->Release(iface);
    }
    if (TlsGetValue(CmTls) == queue)
        TlsSetValue(CmTls, nullptr);
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    DestroyWindow(window);
    if (controller)
        CmRelease((WRT_IFACE *)CmIface(&controller->Object));
    CmRelease((WRT_IFACE *)CmIface(&queue->Object));
}

/* the deferrals of ShutdownStarting are done: timers stop, new work is refused and what is queued drains. */
void CmQueueDeferralDone(CM_QUEUE *queue)
{
    AcquireSRWLockExclusive(&queue->Lock);
    queue->State = CM_SHUT;
    CmTimersStopAll(queue);
    ReleaseSRWLockExclusive(&queue->Lock);
    CmQueuePost(queue, CM_LOW, queue_drained, nullptr, 0, TRUE);
}

/* ------------------------------------------------------------------ IDispatcherQueueController */

CM_INSPECTABLE_THUNKS(IDispatcherQueueController)

static void controller_destroy(WRT_OBJECT *object)
{
    CM_CONTROLLER *controller = (CM_CONTROLLER *)object;
    CmRelease((WRT_IFACE *)CmIface(&controller->Queue->Object));
    HeapFree(GetProcessHeap(), 0, controller);
}

static HRESULT STDMETHODCALLTYPE controller_get_queue(IDispatcherQueueController *This, IDispatcherQueue **value)
{
    CM_CONTROLLER *controller = (CM_CONTROLLER *)CmOwner(This);
    if (!value)
        return E_POINTER;
    CmAddRef((WRT_IFACE *)CmIface(&controller->Queue->Object));
    *value = (IDispatcherQueue *)CmIface(&controller->Queue->Object);
    return S_OK;
}

/* a second shutdown fails, as on Windows. the controller lives until the shutdown completes. */
static HRESULT STDMETHODCALLTYPE controller_shutdown(IDispatcherQueueController *This, IAsyncAction **operation)
{
    CM_CONTROLLER *controller = (CM_CONTROLLER *)CmOwner(This);
    CM_QUEUE *queue = controller->Queue;
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    CM_ASYNC *async;
    HRESULT hr = CmAsyncCreate(&async);
    if (FAILED(hr))
        return hr;
    IAsyncAction *iface = CmAsyncIface(async);
    AcquireSRWLockExclusive(&queue->Lock);
    if (queue->State != CM_RUNNING) {
        ReleaseSRWLockExclusive(&queue->Lock);
        iface->lpVtbl->Release(iface);
        return E_UNEXPECTED;
    }
    queue->State = CM_STARTING;
    iface->lpVtbl->AddRef(iface);
    queue->ShutdownAsync = async;
    CmAddRef((WRT_IFACE *)CmIface(&controller->Object));
    queue->ShutdownController = controller;
    ReleaseSRWLockExclusive(&queue->Lock);
    CmQueuePost(queue, CM_HIGH, CmShutdownStarting, nullptr, 0, TRUE);
    *operation = iface;
    return S_OK;
}

static const IDispatcherQueueControllerVtbl controller_vtbl = {WRT_INSPECTABLE_ENTRIES(IDispatcherQueueController),
                                                               controller_get_queue, controller_shutdown};
static const WRT_CLASS controller_class = {
    CONTROLLER_CLASS, 2, {&IID_IDispatcherQueueController, &IID_CmWeakReferenceSource}, 1 << 1, controller_destroy};

HRESULT CmControllerCreate(CM_QUEUE *queue, CM_CONTROLLER **result)
{
    CM_CONTROLLER *controller = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*controller));
    *result = controller;
    if (!controller)
        return E_OUTOFMEMORY;
    const void *vtables[2] = {&controller_vtbl, &CmWeakSourceVtbl};
    CmInit(&controller->Object, &controller_class, vtables);
    CmAddRef((WRT_IFACE *)CmIface(&queue->Object));
    controller->Queue = queue;
    return S_OK;
}

/* ------------------------------------------------------------------ dedicated threads */

typedef struct START {
    HANDLE Ready;
    INT32 Apartment;
    HRESULT Result;
    CM_CONTROLLER *Controller;
} START;

#define DQTAT_COM_ASTA 1
#define DQTAT_COM_STA 2

/* an asta thread starts with RoInitialize, an sta one with CoInitializeEx, and runs until its queue shuts down. */
static DWORD WINAPI dedicated_thread(void *parameter)
{
    START *start = parameter;
    BOOL runtime = start->Apartment == DQTAT_COM_ASTA;
    HRESULT hr = runtime ? RoInitialize(RO_INIT_SINGLETHREADED) : CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CM_QUEUE *queue = nullptr;
    CM_CONTROLLER *controller = nullptr;
    if (SUCCEEDED(hr))
        hr = CmQueueCreate(TRUE, &queue);
    if (SUCCEEDED(hr)) {
        hr = CmControllerCreate(queue, &controller);
        CmRelease((WRT_IFACE *)CmIface(&queue->Object));
    }
    start->Result = hr;
    if (SUCCEEDED(hr)) {
        CmAddRef((WRT_IFACE *)CmIface(&controller->Object));
        start->Controller = controller;
    }
    SetEvent(start->Ready);
    if (FAILED(hr))
        return 0;
    MSG msg;
    while (!queue->Exit) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == -1)
            break;
        if (got) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CmRelease((WRT_IFACE *)CmIface(&controller->Object));
    if (runtime)
        RoUninitialize();
    else
        CoUninitialize();
    return 0;
}

HRESULT CmControllerOnDedicatedThread(INT32 apartment, IDispatcherQueueController **result)
{
    if (apartment != DQTAT_COM_ASTA && apartment != DQTAT_COM_STA)
        return E_INVALIDARG;
    START start = {CreateEventW(nullptr, TRUE, FALSE, nullptr), apartment, E_FAIL, nullptr};
    if (!start.Ready)
        return HRESULT_FROM_WIN32(GetLastError());
    HANDLE thread = CreateThread(nullptr, 0, dedicated_thread, &start, 0, nullptr);
    if (!thread) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(start.Ready);
        return hr;
    }
    HANDLE handles[2] = {start.Ready, thread};
    DWORD which = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    CloseHandle(start.Ready);
    CloseHandle(thread);
    if (which != WAIT_OBJECT_0)
        return E_UNEXPECTED;
    if (FAILED(start.Result))
        return start.Result;
    *result = (IDispatcherQueueController *)CmIface(&start.Controller->Object);
    return S_OK;
}
