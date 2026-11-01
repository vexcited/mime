/*
 * msado15.dll: ADO, the connection and the recordset, on the ODBC drivers that are installed.
 */
#include "adoprivate.h"

/* the compiler references this symbol from code that uses floating point. */
int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

typedef struct FACTORY {
    IClassFactory Factory;
    BOOL Connection;
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
    return ((FACTORY *)This)->Connection ? ConnectionCreate(riid, object) : RecordsetCreateEmpty(riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};
static FACTORY g_connection_factory = {{&g_factory_vtbl}, TRUE};
static FACTORY g_recordset_factory = {{&g_factory_vtbl}, FALSE};

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    FACTORY *factory;
    if (IsEqualCLSID(clsid, &CLSID_CadoConnection))
        factory = &g_connection_factory;
    else if (IsEqualCLSID(clsid, &CLSID_CadoRecordset))
        factory = &g_recordset_factory;
    else
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory->Factory.lpVtbl->QueryInterface(&factory->Factory, riid, object);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return g_ado_objects ? S_FALSE : S_OK;
}
