#include "coremessaging_private.h"
#include <mime/svcstub.h>

int _fltused = 0x9875;

HINSTANCE CmModule;
DWORD CmTls = TLS_OUT_OF_INDEXES;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        CmModule = instance;
        CmTls = TlsAlloc();
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH && !reserved && CmTls != TLS_OUT_OF_INDEXES) {
        TlsFree(CmTls);
    }
    return TRUE;
}

typedef struct DispatcherQueueOptions {
    DWORD dwSize;
    INT32 threadType;
    INT32 apartmentType;
} DispatcherQueueOptions;

#define DQTYPE_THREAD_DEDICATED 1
#define DQTYPE_THREAD_CURRENT 2

/* ------------------------------------------------------------------ activation factories */

/* the factories are static and never go away. */
typedef struct FACTORY {
    CM_OBJECT Object;
} FACTORY;

CM_INSPECTABLE_THUNKS(IActivationFactory)
CM_INSPECTABLE_THUNKS(IDispatcherQueueStatics)
CM_INSPECTABLE_THUNKS(IDispatcherQueueControllerStatics)

static void factory_destroy(WRT_OBJECT *object)
{
}

static HRESULT STDMETHODCALLTYPE factory_activate(IActivationFactory *This, IInspectable **instance)
{
    if (instance)
        *instance = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE statics_get_for_current_thread(IDispatcherQueueStatics *This,
                                                                IDispatcherQueue **result)
{
    if (!result)
        return E_POINTER;
    CM_QUEUE *queue = CmQueueForThread();
    if (queue)
        CmAddRef((WRT_IFACE *)CmIface(&queue->Object));
    *result = queue ? (IDispatcherQueue *)CmIface(&queue->Object) : nullptr;
    return S_OK;
}

/* the thread of a controller made here is an asta. */
static HRESULT STDMETHODCALLTYPE statics_create_on_dedicated_thread(IDispatcherQueueControllerStatics *This,
                                                                    IDispatcherQueueController **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    return CmControllerOnDedicatedThread(1, result);
}

static const IActivationFactoryVtbl factory_vtbl = {WRT_INSPECTABLE_ENTRIES(IActivationFactory), factory_activate};
static const IDispatcherQueueStaticsVtbl queue_statics_vtbl = {WRT_INSPECTABLE_ENTRIES(IDispatcherQueueStatics),
                                                               statics_get_for_current_thread};
static const IDispatcherQueueControllerStaticsVtbl controller_statics_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDispatcherQueueControllerStatics), statics_create_on_dedicated_thread};

static const WRT_CLASS queue_factory_class = {
    L"Windows.System.DispatcherQueue", 2, {&IID_IActivationFactory, &IID_IDispatcherQueueStatics}, 0, factory_destroy};
static const WRT_CLASS controller_factory_class = {L"Windows.System.DispatcherQueueController",
                                                   2,
                                                   {&IID_IActivationFactory, &IID_IDispatcherQueueControllerStatics},
                                                   0,
                                                   factory_destroy};

static FACTORY g_queue_factory, g_controller_factory;
static INIT_ONCE g_factories_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK init_factories(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const void *queue_vtables[2] = {&factory_vtbl, &queue_statics_vtbl};
    const void *controller_vtables[2] = {&factory_vtbl, &controller_statics_vtbl};
    CmInit(&g_queue_factory.Object, &queue_factory_class, queue_vtables);
    CmInit(&g_controller_factory.Object, &controller_factory_class, controller_vtables);
    return TRUE;
}

static BOOL name_is(HSTRING string, LPCWSTR name)
{
    UINT32 length;
    LPCWSTR text = WindowsGetStringRawBuffer(string, &length);
    return length == wcslen(name) && !memcmp(text, name, length * sizeof(WCHAR));
}

__declspec(dllexport) HRESULT WINAPI DllGetActivationFactory(HSTRING class_id, IActivationFactory **factory)
{
    if (!factory)
        return E_POINTER;
    *factory = nullptr;
    InitOnceExecuteOnce(&g_factories_once, init_factories, nullptr, nullptr);
    FACTORY *found = nullptr;
    if (name_is(class_id, L"Windows.System.DispatcherQueue"))
        found = &g_queue_factory;
    else if (name_is(class_id, L"Windows.System.DispatcherQueueController"))
        found = &g_controller_factory;
    if (!found)
        return CLASS_E_CLASSNOTAVAILABLE;
    CmAddRef((WRT_IFACE *)CmIface(&found->Object));
    *factory = (IActivationFactory *)CmIface(&found->Object);
    return S_OK;
}

/* queues keep threads and windows that cannot be unloaded from under them. */
__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

__declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* ------------------------------------------------------------------ exports */

/* the checks run in the same order as on windows 11. a thread has one queue, a current one ignores the apartment. */
__declspec(dllexport) HRESULT WINAPI CreateDispatcherQueueController(DispatcherQueueOptions options, void **result)
{
    if (!result)
        return E_POINTER;
    if (options.dwSize != sizeof(options))
        return E_INVALIDARG;
    if (options.threadType == DQTYPE_THREAD_DEDICATED)
        return CmControllerOnDedicatedThread(options.apartmentType, (IDispatcherQueueController **)result);
    if (options.threadType != DQTYPE_THREAD_CURRENT)
        return E_INVALIDARG;
    *result = nullptr;
    if (CmQueueForThread())
        return RPC_E_WRONG_THREAD_;
    CM_QUEUE *queue;
    HRESULT hr = CmQueueCreate(FALSE, &queue);
    if (FAILED(hr))
        return hr;
    CM_CONTROLLER *controller;
    hr = CmControllerCreate(queue, &controller);
    CmRelease((WRT_IFACE *)CmIface(&queue->Object));
    if (FAILED(hr))
        return hr;
    *result = CmIface(&controller->Object);
    return S_OK;
}

__declspec(dllexport) HRESULT WINAPI GetDispatcherQueueForCurrentThread(void **result)
{
    if (!result)
        return E_POINTER;
    CM_QUEUE *queue = CmQueueForThread();
    if (queue)
        CmAddRef((WRT_IFACE *)CmIface(&queue->Object));
    *result = queue ? CmIface(&queue->Object) : nullptr;
    return S_OK;
}

/* the queue of the thread, made without a controller if it has none: nothing can shut it down. */
__declspec(dllexport) HRESULT WINAPI CreateDispatcherQueueForCurrentThread(IDispatcherQueue **result)
{
    if (!result)
        return E_POINTER;
    CM_QUEUE *queue = CmQueueForThread();
    if (queue) {
        CmAddRef((WRT_IFACE *)CmIface(&queue->Object));
    } else {
        HRESULT hr = CmQueueCreate(FALSE, &queue);
        if (FAILED(hr))
            return hr;
    }
    *result = (IDispatcherQueue *)CmIface(&queue->Object);
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
