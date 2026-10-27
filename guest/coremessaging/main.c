/*
 * CoreMessaging.dll: the Windows.System dispatcher queues behind the plain exports. a queue is a hidden window on
 * its thread and TryEnqueue posts it a message, so work runs from that thread's own message loop. a dedicated
 * queue gets a thread of its own running that loop. timers and asynchronous shutdown are not offered.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/winrt_object.h>
#include <mime/svcstub.h>

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

typedef struct DispatcherQueueOptions {
    DWORD dwSize;
    INT32 threadType;
    INT32 apartmentType;
} DispatcherQueueOptions;

#define DQTYPE_THREAD_DEDICATED 1
#define DQTYPE_THREAD_CURRENT 2

typedef struct IDispatcherQueueHandler IDispatcherQueueHandler;
#define IDispatcherQueueHandler_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Invoke)(T * This);
MIME_COM_INTERFACE(IDispatcherQueueHandler)

typedef struct IDispatcherQueue IDispatcherQueue;
#define IDispatcherQueue_METHODS(T)                                                                                    \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateTimer)(T * This, IInspectable * *result);                                         \
    HRESULT(STDMETHODCALLTYPE *TryEnqueue)(T * This, IDispatcherQueueHandler * callback, BOOLEAN * result);            \
    HRESULT(STDMETHODCALLTYPE *TryEnqueueWithPriority)(T * This, INT32 priority, IDispatcherQueueHandler * callback,   \
                                                       BOOLEAN *result);                                               \
    HRESULT(STDMETHODCALLTYPE *add_ShutdownStarting)(T * This, IUnknown * handler, EventRegistrationToken * token);    \
    HRESULT(STDMETHODCALLTYPE *remove_ShutdownStarting)(T * This, EventRegistrationToken token);                       \
    HRESULT(STDMETHODCALLTYPE *add_ShutdownCompleted)(T * This, IUnknown * handler, EventRegistrationToken * token);   \
    HRESULT(STDMETHODCALLTYPE *remove_ShutdownCompleted)(T * This, EventRegistrationToken token);
MIME_COM_INTERFACE(IDispatcherQueue)

typedef struct IDispatcherQueueController IDispatcherQueueController;
#define IDispatcherQueueController_METHODS(T)                                                                          \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_DispatcherQueue)(T * This, IDispatcherQueue * *value);                              \
    HRESULT(STDMETHODCALLTYPE *ShutdownQueueAsync)(T * This, IInspectable * *operation);
MIME_COM_INTERFACE(IDispatcherQueueController)

MIME_DEFINE_GUID(IID_IDispatcherQueue, 0x603E88E4, 0xA338, 0x4FFE, 0xA4, 0x57, 0xA5, 0xCF, 0xB9, 0xCE, 0xB8, 0x99);
MIME_DEFINE_GUID(IID_IDispatcherQueueController, 0x22F34E66, 0x50DB, 0x4E36, 0xA9, 0x8D, 0x61, 0xC0, 0x1B, 0x38, 0x4D,
                 0x20);

#define WM_RUN (WM_APP + 1)

typedef struct QUEUE {
    WRT_OBJECT Base;
    HWND Window;
    DWORD Thread;
    HANDLE Owner;
    HANDLE Ready;
    BOOL Dedicated;
} QUEUE;

typedef struct CONTROLLER {
    WRT_OBJECT Base;
    QUEUE *Queue;
} CONTROLLER;

static const WCHAR QUEUE_CLASS[] = L"Windows.System.DispatcherQueue";
static const WCHAR CONTROLLER_CLASS[] = L"Windows.System.DispatcherQueueController";
static const WCHAR WINDOW_CLASS[] = L"DispatcherQueue";
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static LRESULT CALLBACK queue_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_RUN) {
        IDispatcherQueueHandler *handler = (IDispatcherQueueHandler *)lparam;
        handler->lpVtbl->Invoke(handler);
        handler->lpVtbl->Release(handler);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static BOOL CALLBACK register_class(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = queue_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = WINDOW_CLASS;
    return RegisterClassW(&wc) != 0;
}

static HWND make_window(void)
{
    InitOnceExecuteOnce(&g_once, register_class, nullptr, nullptr);
    return CreateWindowExW(0, WINDOW_CLASS, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
}

/* creates the window, then runs posted work until the queue goes away. */
static DWORD WINAPI dedicated_thread(void *param)
{
    QUEUE *queue = (QUEUE *)param;
    queue->Window = make_window();
    SetEvent(queue->Ready);
    MSG msg;
    while (queue->Window && GetMessageW(&msg, nullptr, 0, 0) > 0)
        DispatchMessageW(&msg);
    return 0;
}

WRT_INSPECTABLE_THUNKS(IDispatcherQueue)
WRT_INSPECTABLE_THUNKS(IDispatcherQueueController)

static void queue_destroy(WRT_OBJECT *object)
{
    QUEUE *queue = (QUEUE *)object;
    if (queue->Window)
        PostMessageW(queue->Window, WM_QUIT, 0, 0);
    if (queue->Window && !queue->Dedicated)
        DestroyWindow(queue->Window);
    if (queue->Ready)
        CloseHandle(queue->Ready);
    HeapFree(GetProcessHeap(), 0, queue);
}

static HRESULT STDMETHODCALLTYPE queue_create_timer(IDispatcherQueue *This, IInspectable **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    return E_NOTIMPL;
}

static HRESULT enqueue(IDispatcherQueue *This, IDispatcherQueueHandler *callback, BOOLEAN *result)
{
    QUEUE *queue = (QUEUE *)((WRT_IFACE *)This)->Owner;
    if (!result)
        return E_POINTER;
    *result = FALSE;
    if (!callback)
        return E_INVALIDARG;
    callback->lpVtbl->AddRef(callback);
    if (!queue->Window || !PostMessageW(queue->Window, WM_RUN, 0, (LPARAM)callback)) {
        callback->lpVtbl->Release(callback);
        return S_OK;
    }
    *result = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE queue_try_enqueue(IDispatcherQueue *This, IDispatcherQueueHandler *callback, BOOLEAN *result)
{
    return enqueue(This, callback, result);
}

static HRESULT STDMETHODCALLTYPE queue_try_enqueue_priority(IDispatcherQueue *This, INT32 priority,
                                                            IDispatcherQueueHandler *callback, BOOLEAN *result)
{
    /* low, normal and high. */
    if (priority < -1 || priority > 1) {
        if (result)
            *result = FALSE;
        return E_INVALIDARG;
    }
    return enqueue(This, callback, result);
}

static HRESULT STDMETHODCALLTYPE queue_add_event(IDispatcherQueue *This, IUnknown *handler, EventRegistrationToken *token)
{
    if (!handler || !token)
        return E_POINTER;
    token->value = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE queue_remove_event(IDispatcherQueue *This, EventRegistrationToken token)
{
    return S_OK;
}

static const IDispatcherQueueVtbl queue_vtbl = {WRT_INSPECTABLE_ENTRIES(IDispatcherQueue), queue_create_timer,
                                                queue_try_enqueue, queue_try_enqueue_priority, queue_add_event,
                                                queue_remove_event, queue_add_event, queue_remove_event};
static const WRT_CLASS queue_class = {QUEUE_CLASS, 1, {&IID_IDispatcherQueue}, 0, queue_destroy};

static void controller_destroy(WRT_OBJECT *object)
{
    CONTROLLER *controller = (CONTROLLER *)object;
    if (controller->Queue)
        WrtRelease(&controller->Queue->Base.Ifaces[0]);
    HeapFree(GetProcessHeap(), 0, controller);
}

static HRESULT STDMETHODCALLTYPE controller_get_queue(IDispatcherQueueController *This, IDispatcherQueue **value)
{
    CONTROLLER *controller = (CONTROLLER *)((WRT_IFACE *)This)->Owner;
    if (!value)
        return E_POINTER;
    *value = (IDispatcherQueue *)&controller->Queue->Base.Ifaces[0];
    WrtAddRef(&controller->Queue->Base.Ifaces[0]);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE controller_shutdown(IDispatcherQueueController *This, IInspectable **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    return E_NOTIMPL;
}

static const IDispatcherQueueControllerVtbl controller_vtbl = {WRT_INSPECTABLE_ENTRIES(IDispatcherQueueController),
                                                               controller_get_queue, controller_shutdown};
static const WRT_CLASS controller_class = {CONTROLLER_CLASS, 1, {&IID_IDispatcherQueueController}, 0,
                                           controller_destroy};

static QUEUE *queue_make(BOOL dedicated)
{
    QUEUE *queue = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*queue));
    if (!queue)
        return nullptr;
    const void *vtables[1] = {&queue_vtbl};
    WrtInit(&queue->Base, &queue_class, vtables);
    queue->Dedicated = dedicated;
    if (dedicated) {
        queue->Ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE thread = CreateThread(nullptr, 0, dedicated_thread, queue, 0, &queue->Thread);
        if (!queue->Ready || !thread) {
            WrtRelease(&queue->Base.Ifaces[0]);
            return nullptr;
        }
        CloseHandle(thread);
        WaitForSingleObject(queue->Ready, INFINITE);
    } else {
        queue->Window = make_window();
        queue->Thread = GetCurrentThreadId();
    }
    if (!queue->Window) {
        WrtRelease(&queue->Base.Ifaces[0]);
        return nullptr;
    }
    return queue;
}

/* the checks run in the same order as on windows 11. */
__declspec(dllexport) HRESULT WINAPI CreateDispatcherQueueController(DispatcherQueueOptions options, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (options.dwSize != sizeof(options))
        return E_INVALIDARG;
    if (options.threadType != DQTYPE_THREAD_DEDICATED && options.threadType != DQTYPE_THREAD_CURRENT)
        return E_INVALIDARG;
    /* the apartment is none, single threaded or multithreaded. */
    if (options.apartmentType < 0 || options.apartmentType > 2)
        return E_INVALIDARG;
    QUEUE *queue = queue_make(options.threadType == DQTYPE_THREAD_DEDICATED);
    CONTROLLER *controller = queue ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*controller)) : nullptr;
    if (!controller) {
        if (queue)
            WrtRelease(&queue->Base.Ifaces[0]);
        return E_OUTOFMEMORY;
    }
    const void *vtables[1] = {&controller_vtbl};
    WrtInit(&controller->Base, &controller_class, vtables);
    controller->Queue = queue;
    *result = &controller->Base.Ifaces[0];
    return S_OK;
}

/* the calling thread's queue is not tracked, so this answers as for a thread that made none. */
__declspec(dllexport) HRESULT WINAPI GetDispatcherQueueForCurrentThread(void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    return S_OK;
}

/*
 * the CoreMessagingRegistrar service. queues here never cross processes, so it has nothing to register and only
 * reports that it runs: its failure action restarts the machine.
 */
void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    svc_stub_run(argc, argv);
}
