/*
 * ole32 exports that programs import and that the other files of ole32 do not carry: the error info calls that
 * ole32 forwards to combase on Windows, loading an object from a file, the default handler and auto conversion.
 */
/* the error info calls are declared with the automation headers, here they are ole32's own exports. */
#define WINOLEAPI DECLSPEC_EXPORT
#define WINOLEAUTAPI DECLSPEC_EXPORT
#include "ole32_private.h"
#include <mime/win32/oleauto.h>

#ifndef STG_E_ACCESSDENIED
#define STG_E_ACCESSDENIED ((HRESULT)0x80030005L)
#endif

#define SLOT(object, index) ((*(void ***)(object))[index])
#define CALL(type, object, index, ...) ((type)SLOT(object, index))(object __VA_OPT__(, ) __VA_ARGS__)

MIME_DEFINE_GUID(IID_FilePersist, 0x0000010b, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

/* ------------------------------------------------------------------ error info */

/*
 * the error object of a thread lives in oleaut32, where Mime keeps it for all three names: ole32 cannot import
 * oleaut32 because oleaut32 imports ole32.
 */
static FARPROC automation_export(const char *name)
{
    HMODULE library = GetModuleHandleW(L"oleaut32.dll");
    if (!library)
        library = LoadLibraryW(L"oleaut32.dll");
    return library ? GetProcAddress(library, name) : nullptr;
}

WINOLEAUTAPI HRESULT WINAPI CreateErrorInfo(ICreateErrorInfo **result)
{
    HRESULT(WINAPI * function)(ICreateErrorInfo **) = (void *)automation_export("CreateErrorInfo");
    if (!function) {
        if (result)
            *result = nullptr;
        return E_NOTIMPL;
    }
    return function(result);
}

WINOLEAUTAPI HRESULT WINAPI SetErrorInfo(ULONG reserved, IErrorInfo *info)
{
    HRESULT(WINAPI * function)(ULONG, IErrorInfo *) = (void *)automation_export("SetErrorInfo");
    return function ? function(reserved, info) : E_NOTIMPL;
}

WINOLEAUTAPI HRESULT WINAPI GetErrorInfo(ULONG reserved, IErrorInfo **result)
{
    HRESULT(WINAPI * function)(ULONG, IErrorInfo **) = (void *)automation_export("GetErrorInfo");
    if (!function) {
        if (result)
            *result = nullptr;
        return E_NOTIMPL;
    }
    return function(reserved, result);
}

/* ------------------------------------------------------------------ objects of a file */

/*
 * makes the object of a class, loads the file into it through IPersistFile and asks it for the interfaces. without a
 * class the file says which one, as GetClassFile does. a server on another machine is not reached.
 */
WINOLEAPI HRESULT WINAPI CoGetInstanceFromFile(COSERVERINFO *server, CLSID *clsid, IUnknown *outer, DWORD context,
                                               DWORD mode, OLECHAR *name, DWORD count, MULTI_QI *results)
{
    if (!count || !results || !name)
        return E_INVALIDARG;
    for (DWORD i = 0; i < count; i++) {
        results[i].pItf = nullptr;
        results[i].hr = E_NOINTERFACE;
    }
    CLSID file_class;
    HRESULT hr = S_OK;
    if (!clsid || IsEqualCLSID(clsid, &CLSID_NULL)) {
        hr = GetClassFile(name, &file_class);
        clsid = &file_class;
    }
    IUnknown *object = nullptr;
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(clsid, outer, context ? context : CLSCTX_ALL, &IID_FilePersist, (void **)&object);
    if (SUCCEEDED(hr))
        hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, LPCOLESTR, DWORD), object, 5, name, mode);
    if (FAILED(hr)) {
        if (object)
            object->lpVtbl->Release(object);
        for (DWORD i = 0; i < count; i++)
            results[i].hr = hr;
        return hr;
    }
    DWORD found = 0;
    for (DWORD i = 0; i < count; i++) {
        results[i].hr = object->lpVtbl->QueryInterface(object, results[i].pIID, (void **)&results[i].pItf);
        if (SUCCEEDED(results[i].hr))
            found++;
    }
    object->lpVtbl->Release(object);
    if (!found)
        return E_NOINTERFACE;
    return found == count ? S_OK : CO_S_NOTALLINTERFACES;
}

/* ------------------------------------------------------------------ handlers and conversion */

/*
 * Windows wraps a local server in the default handler, an object that answers for the server while it is not
 * running. Mime has no such object: the class is made as it is, by its in process or local server, which is what
 * the handler ends up asking for once a call needs the server.
 */
WINOLEAPI HRESULT WINAPI OleCreateDefaultHandler(REFCLSID clsid, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!clsid || !riid)
        return E_INVALIDARG;
    return CoCreateInstance(clsid, outer, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, riid, object);
}

/*
 * the class of a storage is changed to the one its class says it converts to automatically. the new class comes back
 * in the second argument, the old one when there is none, and S_FALSE says nothing was converted.
 */
WINOLEAPI HRESULT WINAPI OleDoAutoConvert(IStorage *storage, LPCLSID new_clsid)
{
    if (!storage || !new_clsid)
        return E_INVALIDARG;
    CLSID old_clsid;
    *new_clsid = CLSID_NULL;
    HRESULT hr = ReadClassStg(storage, &old_clsid);
    if (FAILED(hr))
        return hr;
    hr = OleGetAutoConvert(&old_clsid, new_clsid);
    if (hr != S_OK) {
        *new_clsid = old_clsid;
        return hr == REGDB_E_KEYMISSING || hr == REGDB_E_CLASSNOTREG ? S_FALSE : hr;
    }
    hr = storage->lpVtbl->SetClass(storage, new_clsid);
    if (FAILED(hr))
        return hr;
    return S_OK;
}

/* ------------------------------------------------------------------ property sets of a storage */

typedef struct PROPSET_STORAGE {
    const void *Vtbl;
    LONG Refs;
    IStorage *Storage;
} PROPSET_STORAGE;

MIME_DEFINE_GUID(IID_PropSetStorage, 0x0000013a, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

static HRESULT STDMETHODCALLTYPE propset_query(PROPSET_STORAGE *self, REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_PropSetStorage))
        return E_NOINTERFACE;
    InterlockedIncrement(&self->Refs);
    *object = self;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE propset_addref(PROPSET_STORAGE *self)
{
    return InterlockedIncrement(&self->Refs);
}

static ULONG STDMETHODCALLTYPE propset_release(PROPSET_STORAGE *self)
{
    LONG refs = InterlockedDecrement(&self->Refs);
    if (!refs) {
        self->Storage->lpVtbl->Release(self->Storage);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return refs;
}

/* a storage without a property set stream, which is all the sets this object knows. */
static HRESULT STDMETHODCALLTYPE propset_create(PROPSET_STORAGE *self, REFGUID format, const CLSID *clsid, DWORD flags,
                                                DWORD mode, void **storage)
{
    if (storage)
        *storage = nullptr;
    return STG_E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE propset_open(PROPSET_STORAGE *self, REFGUID format, DWORD mode, void **storage)
{
    if (storage)
        *storage = nullptr;
    return STG_E_FILENOTFOUND;
}

static HRESULT STDMETHODCALLTYPE propset_delete(PROPSET_STORAGE *self, REFGUID format)
{
    return STG_E_FILENOTFOUND;
}

static HRESULT STDMETHODCALLTYPE propset_enum(PROPSET_STORAGE *self, void **enumerator)
{
    if (enumerator)
        *enumerator = nullptr;
    return E_NOTIMPL;
}

static const struct {
    HRESULT(STDMETHODCALLTYPE *Query)(PROPSET_STORAGE *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(PROPSET_STORAGE *);
    ULONG(STDMETHODCALLTYPE *Release)(PROPSET_STORAGE *);
    HRESULT(STDMETHODCALLTYPE *Create)(PROPSET_STORAGE *, REFGUID, const CLSID *, DWORD, DWORD, void **);
    HRESULT(STDMETHODCALLTYPE *Open)(PROPSET_STORAGE *, REFGUID, DWORD, void **);
    HRESULT(STDMETHODCALLTYPE *Delete)(PROPSET_STORAGE *, REFGUID);
    HRESULT(STDMETHODCALLTYPE *Enum)(PROPSET_STORAGE *, void **);
} propset_vtable = {propset_query, propset_addref, propset_release, propset_create,
                    propset_open,  propset_delete, propset_enum};

/*
 * the property sets of a storage as IPropertySetStorage. the object only keeps the storage: it makes and finds no
 * property set, so a program that reads the summary information of a file gets nothing back from it.
 */
WINOLEAPI HRESULT WINAPI StgCreatePropSetStg(IStorage *storage, DWORD reserved, void **result)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    if (!storage || reserved)
        return STG_E_INVALIDPARAMETER;
    PROPSET_STORAGE *object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;
    object->Vtbl = &propset_vtable;
    object->Refs = 1;
    object->Storage = storage;
    storage->lpVtbl->AddRef(storage);
    *result = object;
    return S_OK;
}
