/* windows.storage entry point and class factory. Windows registers the shell shortcut class here. */
#include "windows_storage_private.h"
#include <mime/win32/shobjidl_ui.h>

#ifndef CLASS_E_CLASSNOTAVAILABLE
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#endif

static HRESULT STDMETHODCALLTYPE factory_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    return 1;
}

typedef struct _CLASS_FACTORY {
    IClassFactory Interface;
    const GUID *Clsid;
    HRESULT (*Create)(IUnknown *outer, REFIID riid, void **object);
} CLASS_FACTORY;

static HRESULT STDMETHODCALLTYPE factory_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid,
                                                         void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    return ((CLASS_FACTORY *)This)->Create(outer, riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};

static CLASS_FACTORY g_factories[] = {
    {{&g_factory_vtbl}, &CLSID_ShellLink, LinkCreate},
    {{&g_factory_vtbl}, &CLSID_DestinationList, DestinationListCreate},
    {{&g_factory_vtbl}, &CLSID_EnumerableObjectCollection, ObjectCollectionCreate},
    {{&g_factory_vtbl}, &CLSID_ApplicationDestinations, ApplicationDestinationsCreate},
    {{&g_factory_vtbl}, &CLSID_ApplicationDocumentLists, ApplicationDocumentListsCreate},
    {{&g_factory_vtbl}, &CLSID_FileOperation, FileOperationCreate},
};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!clsid || !riid)
        return E_INVALIDARG;
    for (SIZE_T i = 0; i < sizeof(g_factories) / sizeof(g_factories[0]); i++)
        if (IsEqualCLSID(clsid, g_factories[i].Clsid))
            return g_factories[i].Interface.lpVtbl->QueryInterface(&g_factories[i].Interface, riid, object);
    return CLASS_E_CLASSNOTAVAILABLE;
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}
