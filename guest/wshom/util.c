/*
 * the pieces every object of wshom.ocx uses: the type information of the library, the base of the objects, the errors the
 * methods signal with the texts of the string table and the conversions of the arguments that scripts pass.
 */
#include "wshom_private.h"

static LONG g_locks;
static SRWLOCK g_library_lock = SRWLOCK_INIT;
static ITypeLib *g_library;

LONG WshLockCount(void)
{
    return g_locks;
}

void WshLockDll(void)
{
    InterlockedIncrement(&g_locks);
}

void WshUnlockDll(void)
{
    if (InterlockedDecrement(&g_locks))
        return;
    AcquireSRWLockExclusive(&g_library_lock);
    if (g_library)
        g_library->lpVtbl->Release(g_library);
    g_library = nullptr;
    ReleaseSRWLockExclusive(&g_library_lock);
}

HRESULT WshTypeInfo(REFIID iid, ITypeInfo **info)
{
    *info = nullptr;
    AcquireSRWLockExclusive(&g_library_lock);
    HRESULT hr = S_OK;
    if (!g_library) {
        WCHAR path[MAX_PATH];
        if (!GetModuleFileNameW(g_module, path, MAX_PATH))
            hr = HRESULT_FROM_WIN32(GetLastError());
        else
            hr = LoadTypeLibEx(path, REGKIND_NONE, &g_library);
    }
    if (SUCCEEDED(hr))
        hr = g_library->lpVtbl->GetTypeInfoOfGuid(g_library, iid, info);
    ReleaseSRWLockExclusive(&g_library_lock);
    return hr;
}

/* ------------------------------------------------------------------ the base of the objects */

void WshObjectInit(WSH_OBJECT *object, const void *vtbl, const WSH_CLASS *class_info, void (*destroy)(WSH_OBJECT *));

extern const IProvideClassInfoVtbl g_wsh_info_vtbl;

void WshObjectInit(WSH_OBJECT *object, const void *vtbl, const WSH_CLASS *class_info, void (*destroy)(WSH_OBJECT *))
{
    object->Vtbl = vtbl;
    object->Info.lpVtbl = &g_wsh_info_vtbl;
    object->Refs = 1;
    object->Class = class_info;
    object->Destroy = destroy;
    WshLockDll();
}

static WSH_OBJECT *object_of(void *This)
{
    return (WSH_OBJECT *)This;
}

HRESULT WshQueryInterface(void *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    WSH_OBJECT *object = object_of(This);
    *out = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch)) {
        *out = object;
    } else if (IsEqualIID(riid, &IID_IProvideClassInfo)) {
        *out = &object->Info;
    } else {
        for (int i = 0; i < 6 && object->Class->Accepted[i]; i++) {
            if (IsEqualIID(riid, object->Class->Accepted[i])) {
                *out = object;
                break;
            }
        }
    }
    if (!*out)
        return E_NOINTERFACE;
    InterlockedIncrement(&object->Refs);
    return S_OK;
}

ULONG WshAddRef(void *This)
{
    return (ULONG)InterlockedIncrement(&object_of(This)->Refs);
}

ULONG WshRelease(void *This)
{
    WSH_OBJECT *object = object_of(This);
    LONG refs = InterlockedDecrement(&object->Refs);
    if (!refs) {
        if (object->Destroy)
            object->Destroy(object);
        HeapFree(GetProcessHeap(), 0, object);
        WshUnlockDll();
    }
    return (ULONG)refs;
}

HRESULT WshGetTypeInfoCount(void *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

HRESULT WshGetTypeInfo(void *This, UINT index, LCID lcid, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    *info = nullptr;
    if (index)
        return DISP_E_BADINDEX;
    return WshTypeInfo(object_of(This)->Class->Iid, info);
}

HRESULT WshGetIDsOfNames(void *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    if (!IsEqualIID(riid, &IID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    ITypeInfo *info;
    HRESULT hr = WshTypeInfo(object_of(This)->Class->Iid, &info);
    if (FAILED(hr))
        return hr;
    hr = info->lpVtbl->GetIDsOfNames(info, names, count, ids);
    info->lpVtbl->Release(info);
    return hr;
}

HRESULT WshInvoke(void *This, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params, VARIANT *result,
                  EXCEPINFO *excep, UINT *arg_error)
{
    if (!IsEqualIID(riid, &IID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    ITypeInfo *info;
    HRESULT hr = WshTypeInfo(object_of(This)->Class->Iid, &info);
    if (FAILED(hr))
        return hr;
    SetErrorInfo(0, nullptr);
    hr = info->lpVtbl->Invoke(info, This, id, flags, params, result, excep, arg_error);
    info->lpVtbl->Release(info);
    return hr;
}

static HRESULT STDMETHODCALLTYPE info_query_interface(IProvideClassInfo *This, REFIID riid, void **object)
{
    WSH_OBJECT *owner = (WSH_OBJECT *)((char *)This - offsetof(WSH_OBJECT, Info));
    return WshQueryInterface(owner, riid, object);
}

static ULONG STDMETHODCALLTYPE info_add_ref(IProvideClassInfo *This)
{
    return WshAddRef((char *)This - offsetof(WSH_OBJECT, Info));
}

static ULONG STDMETHODCALLTYPE info_release(IProvideClassInfo *This)
{
    return WshRelease((char *)This - offsetof(WSH_OBJECT, Info));
}

static HRESULT STDMETHODCALLTYPE info_get_class_info(IProvideClassInfo *This, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    WSH_OBJECT *owner = (WSH_OBJECT *)((char *)This - offsetof(WSH_OBJECT, Info));
    return WshTypeInfo(owner->Class->Clsid, info);
}

const IProvideClassInfoVtbl g_wsh_info_vtbl = {info_query_interface, info_add_ref, info_release, info_get_class_info};

/* ------------------------------------------------------------------ errors */

BSTR WshLoadBstr(UINT id)
{
    WCHAR buffer[2049];
    int length = LoadStringW(g_module, id, buffer, 2048);
    if (!length)
        return nullptr;
    buffer[length] = 0;
    return SysAllocString(buffer);
}

HRESULT HrFromLastError(void)
{
    DWORD error = GetLastError();
    return (LONG)error > 0 ? (HRESULT)(0x80070000u | (error & 0xFFFF)) : (HRESULT)error;
}

static HRESULT raise_error_info(const GUID *iid, const WCHAR *source, const WCHAR *description)
{
    ICreateErrorInfo *create = nullptr;
    IErrorInfo *info = nullptr;
    HRESULT hr = CreateErrorInfo(&create);
    if (FAILED(hr))
        return hr;
    hr = create->lpVtbl->SetGUID(create, iid);
    if (SUCCEEDED(hr))
        hr = create->lpVtbl->SetSource(create, (LPOLESTR)source);
    if (SUCCEEDED(hr))
        hr = create->lpVtbl->SetDescription(create, (LPOLESTR)description);
    if (SUCCEEDED(hr))
        hr = create->lpVtbl->SetHelpFile(create, nullptr);
    if (SUCCEEDED(hr))
        hr = create->lpVtbl->SetHelpContext(create, 0);
    if (SUCCEEDED(hr))
        hr = create->lpVtbl->QueryInterface(create, &IID_IErrorInfo, (void **)&info);
    if (SUCCEEDED(hr))
        hr = SetErrorInfo(0, info);
    if (info)
        info->lpVtbl->Release(info);
    create->lpVtbl->Release(create);
    return hr;
}

HRESULT WshSignal(const GUID *iid, const WCHAR *source, UINT string_id, const WCHAR *argument)
{
    BSTR template = WshLoadBstr(string_id);
    if (!template)
        return E_OUTOFMEMORY;
    /* the text with %1 replaced by the argument. */
    SIZE_T template_length = SysStringLen(template), argument_length = argument ? wcslen(argument) : 0;
    WCHAR *text = HeapAlloc(GetProcessHeap(), 0, (template_length + argument_length * 4 + 2) * sizeof(WCHAR));
    if (!text) {
        SysFreeString(template);
        return E_OUTOFMEMORY;
    }
    SIZE_T out = 0;
    for (SIZE_T i = 0; i < template_length; i++) {
        if (template[i] == '%' && i + 1 < template_length && template[i + 1] == '1') {
            if (argument) {
                memcpy(text + out, argument, argument_length * sizeof(WCHAR));
                out += argument_length;
            }
            i++;
        } else {
            text[out++] = template[i];
        }
    }
    text[out] = 0;
    HRESULT hr = raise_error_info(iid, source, text);
    HeapFree(GetProcessHeap(), 0, text);
    SysFreeString(template);
    return hr;
}

HRESULT WshSignalHr(const GUID *iid, const WCHAR *source, HRESULT code)
{
    WCHAR *text = nullptr;
    if (!FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        nullptr, (DWORD)code, 0, (LPWSTR)&text, 0, nullptr))
        return HrFromLastError();
    HRESULT hr = raise_error_info(iid, source, text);
    LocalFree(text);
    return hr;
}

/* ------------------------------------------------------------------ variants */

BOOL WshArgumentGiven(const VARIANT *value)
{
    return value->vt != VT_ERROR || value->scode != DISP_E_PARAMNOTFOUND;
}

HRESULT WshVariantBstr(VARIANT *value, BSTR *out)
{
    VARIANT converted;
    VariantInit(&converted);
    HRESULT hr = VariantChangeType(&converted, value, 0, VT_BSTR);
    if (SUCCEEDED(hr))
        *out = converted.bstrVal;
    return hr;
}

HRESULT WshVariantLong(VARIANT *value, LONG *out)
{
    VARIANT converted;
    VariantInit(&converted);
    HRESULT hr = VariantChangeType(&converted, value, 0, VT_I4);
    if (SUCCEEDED(hr))
        *out = converted.lVal;
    return hr;
}

HRESULT WshVariantBool(VARIANT *value, int *out)
{
    VARIANT converted;
    VariantInit(&converted);
    HRESULT hr = VariantChangeType(&converted, value, 0, VT_BOOL);
    if (SUCCEEDED(hr))
        *out = converted.boolVal;
    return hr;
}
