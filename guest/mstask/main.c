/*
 * mstask.dll: the task scheduler 1.0 as files. The scheduler class keeps the tasks as job files below the tasks folder
 * and the task class is the job read from one.
 */
#include "mstask_private.h"

LONG g_mstask_objects;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

typedef struct FACTORY {
    IClassFactory factory;
    BOOL scheduler;
} FACTORY;

static HRESULT STDMETHODCALLTYPE factory_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (((FACTORY *)This)->scheduler)
        return ScheduleCreate(riid, object);
    /* a task made on its own has no name yet. */
    return JobCreate(nullptr, riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};
static FACTORY g_task_factory = {{&g_factory_vtbl}, FALSE};
static FACTORY g_schedule_factory = {{&g_factory_vtbl}, TRUE};

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    FACTORY *factory;
    if (IsEqualCLSID(clsid, &CLSID_CTask))
        factory = &g_task_factory;
    else if (IsEqualCLSID(clsid, &CLSID_CTaskScheduler))
        factory = &g_schedule_factory;
    else
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory->factory.lpVtbl->QueryInterface(&factory->factory, riid, object);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return g_mstask_objects ? S_FALSE : S_OK;
}
