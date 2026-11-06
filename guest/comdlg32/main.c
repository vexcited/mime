/*
 * comdlg32 entry point and the class factory of the common item dialogs.
 */
#include "comdlg32_private.h"
#include <mime/win32/shobjidl.h>

HINSTANCE g_comdlg32;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_comdlg32 = instance;
        DlgProcessAttach();
    } else if (reason == DLL_PROCESS_DETACH && !reserved) {
        DlgProcessFree();
    }
    return TRUE;
}

typedef struct _FACTORY {
    IClassFactory Interface;
    BOOL Save;
} FACTORY;

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
    return ItemDialogCreate(((FACTORY *)This)->Save, outer, riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};
static FACTORY g_open_factory = {{&g_factory_vtbl}, FALSE};
static FACTORY g_save_factory = {{&g_factory_vtbl}, TRUE};

/* the open and save dialogs, plus the legacy classes Windows keeps for pre-Vista dialogs. */
CDAPI HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!clsid || !riid)
        return E_INVALIDARG;
    FACTORY *factory = nullptr;
    if (IsEqualCLSID(clsid, &CLSID_FileOpenDialog) || IsEqualCLSID(clsid, &CLSID_FileOpenDialogLegacy))
        factory = &g_open_factory;
    else if (IsEqualCLSID(clsid, &CLSID_FileSaveDialog) || IsEqualCLSID(clsid, &CLSID_FileSaveDialogLegacy))
        factory = &g_save_factory;
    if (!factory)
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory->Interface.lpVtbl->QueryInterface(&factory->Interface, riid, object);
}

CDAPI HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}
