/*
 * windows.storage entry point and class factory. the shortcut class of the
 * shell is registered to this dll, as it is on windows.
 */
#include "windows_storage_private.h"

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

static HRESULT STDMETHODCALLTYPE factory_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid,
                                                         void **object)
{
    return LinkCreate(outer, riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};
static IClassFactory g_factory = {&g_factory_vtbl};

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
    if (!IsEqualCLSID(clsid, &CLSID_ShellLink))
        return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.lpVtbl->QueryInterface(&g_factory, riid, object);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}
