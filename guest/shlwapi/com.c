/*
 * COM helpers: IUnknown_* calls through optional interfaces, connection points, property bags, streams,
 * data block lists, shared memory and threads.
 */
#include "shlwapi_private.h"

/* ------------------------------------------------------------------ IUnknown helpers */

static HRESULT query(IUnknown *unknown, REFIID riid, void **out)
{
    *out = nullptr;
    if (!unknown)
        return E_FAIL;
    return unknown->lpVtbl->QueryInterface(unknown, riid, out);
}

static HRESULT query_service(IUnknown *unknown, REFGUID service, REFIID riid, void **out)
{
    IServiceProvider *provider;
    *out = nullptr;
    HRESULT hr = query(unknown, &IID_IServiceProvider, (void **)&provider);
    if (FAILED(hr))
        return hr;
    hr = provider->lpVtbl->QueryService(provider, service, riid, out);
    provider->lpVtbl->Release(provider);
    return hr;
}

HRESULT WINAPI IUnknown_QueryStatus(IUnknown *unknown, const GUID *group, ULONG count, OLECMD *commands,
                                    OLECMDTEXT *text);
HRESULT WINAPI IUnknown_QueryStatus(IUnknown *unknown, const GUID *group, ULONG count, OLECMD *commands,
                                    OLECMDTEXT *text)
{
    IOleCommandTarget *target;
    HRESULT hr = query(unknown, &IID_IOleCommandTarget, (void **)&target);
    if (SUCCEEDED(hr)) {
        hr = target->lpVtbl->QueryStatus(target, group, count, commands, text);
        target->lpVtbl->Release(target);
    }
    return hr;
}

HRESULT WINAPI IUnknown_Exec(IUnknown *unknown, const GUID *group, DWORD id, DWORD options, VARIANT *in,
                             VARIANT *out);
HRESULT WINAPI IUnknown_Exec(IUnknown *unknown, const GUID *group, DWORD id, DWORD options, VARIANT *in, VARIANT *out)
{
    IOleCommandTarget *target;
    HRESULT hr = query(unknown, &IID_IOleCommandTarget, (void **)&target);
    if (SUCCEEDED(hr)) {
        hr = target->lpVtbl->Exec(target, group, id, options, in, out);
        target->lpVtbl->Release(target);
    }
    return hr;
}

/* tries four interfaces that each start with GetWindow. */
HRESULT WINAPI IUnknown_GetWindow(IUnknown *unknown, HWND *window)
{
    static const IID *const sources[] = {&IID_IOleWindow, &IID_IInternetSecurityMgrSite, &IID_IShellView,
                                         &IID_ShlWindowSource};
    if (!window)
        return E_INVALIDARG;
    *window = nullptr;
    if (!unknown)
        return E_FAIL;
    for (ULONG i = 0; i < RTL_NUMBER_OF(sources); i++) {
        IOleWindow *ole;
        if (SUCCEEDED(unknown->lpVtbl->QueryInterface(unknown, sources[i], (void **)&ole))) {
            HRESULT hr = ole->lpVtbl->GetWindow(ole, window);
            ole->lpVtbl->Release(ole);
            return hr;
        }
    }
    return E_FAIL;
}

HRESULT WINAPI IUnknown_SetOwner(IUnknown *unknown, IUnknown *owner);
HRESULT WINAPI IUnknown_SetOwner(IUnknown *unknown, IUnknown *owner)
{
    IShellService *service;
    HRESULT hr = query(unknown, &IID_IShellService, (void **)&service);
    if (SUCCEEDED(hr)) {
        hr = service->lpVtbl->SetOwner(service, owner);
        service->lpVtbl->Release(service);
    }
    return hr;
}

HRESULT WINAPI IUnknown_GetClassID(IUnknown *unknown, CLSID *clsid);
HRESULT WINAPI IUnknown_GetClassID(IUnknown *unknown, CLSID *clsid)
{
    if (!clsid)
        return E_INVALIDARG;
    IPersist *persist;
    HRESULT hr = query(unknown, &IID_IPersist, (void **)&persist);
    if (SUCCEEDED(hr)) {
        hr = persist->lpVtbl->GetClassID(persist, clsid);
        persist->lpVtbl->Release(persist);
    }
    return hr;
}

HRESULT WINAPI IUnknown_EnableModeless(IUnknown *unknown, BOOL enable);
HRESULT WINAPI IUnknown_EnableModeless(IUnknown *unknown, BOOL enable)
{
    IOleInPlaceActiveObject *active;
    HRESULT hr = query(unknown, &IID_IOleInPlaceActiveObject, (void **)&active);
    if (SUCCEEDED(hr)) {
        hr = active->lpVtbl->EnableModeless(active, enable);
        active->lpVtbl->Release(active);
    }
    return hr;
}

HRESULT WINAPI IUnknown_TranslateAcceleratorIO(IUnknown *unknown, MSG *msg);
HRESULT WINAPI IUnknown_TranslateAcceleratorIO(IUnknown *unknown, MSG *msg)
{
    IInputObject *input;
    HRESULT hr = query(unknown, &IID_IInputObject, (void **)&input);
    if (SUCCEEDED(hr)) {
        hr = input->lpVtbl->TranslateAcceleratorIO(input, msg);
        input->lpVtbl->Release(input);
    }
    return hr;
}

HRESULT WINAPI IUnknown_UIActivateIO(IUnknown *unknown, BOOL activate, MSG *msg);
HRESULT WINAPI IUnknown_UIActivateIO(IUnknown *unknown, BOOL activate, MSG *msg)
{
    IInputObject *input;
    HRESULT hr = query(unknown, &IID_IInputObject, (void **)&input);
    if (SUCCEEDED(hr)) {
        hr = input->lpVtbl->UIActivateIO(input, activate, msg);
        input->lpVtbl->Release(input);
    }
    return hr;
}

HRESULT WINAPI IUnknown_HasFocusIO(IUnknown *unknown);
HRESULT WINAPI IUnknown_HasFocusIO(IUnknown *unknown)
{
    IInputObject *input;
    HRESULT hr = query(unknown, &IID_IInputObject, (void **)&input);
    if (SUCCEEDED(hr)) {
        hr = input->lpVtbl->HasFocusIO(input);
        input->lpVtbl->Release(input);
    }
    return hr;
}

HRESULT WINAPI IUnknown_OnFocusChangeIS(IUnknown *unknown, IUnknown *object, BOOL focus);
HRESULT WINAPI IUnknown_OnFocusChangeIS(IUnknown *unknown, IUnknown *object, BOOL focus)
{
    IInputObjectSite *site;
    HRESULT hr = query(unknown, &IID_IInputObjectSite, (void **)&site);
    if (SUCCEEDED(hr)) {
        hr = site->lpVtbl->OnFocusChangeIS(site, object, focus);
        site->lpVtbl->Release(site);
    }
    return hr;
}

/* goes through the site's IProfferService. a NULL provider revokes the service. */
HRESULT WINAPI IUnknown_ProfferService(IUnknown *site, REFGUID service, IServiceProvider *provider, DWORD *cookie);
HRESULT WINAPI IUnknown_ProfferService(IUnknown *site, REFGUID service, IServiceProvider *provider, DWORD *cookie)
{
    IProfferService *proffer;
    HRESULT hr = query_service(site, &IID_IProfferService, &IID_IProfferService, (void **)&proffer);
    if (FAILED(hr))
        return hr;
    if (provider) {
        hr = proffer->lpVtbl->ProfferService(proffer, service, provider, cookie);
    } else if (cookie) {
        hr = proffer->lpVtbl->RevokeService(proffer, *cookie);
        *cookie = 0;
    }
    proffer->lpVtbl->Release(proffer);
    return hr;
}

HRESULT WINAPI IUnknown_QueryServiceExec(IUnknown *unknown, REFGUID service, const GUID *group, DWORD id,
                                         DWORD options, VARIANT *in, VARIANT *out);
HRESULT WINAPI IUnknown_QueryServiceExec(IUnknown *unknown, REFGUID service, const GUID *group, DWORD id,
                                         DWORD options, VARIANT *in, VARIANT *out)
{
    IOleCommandTarget *target;
    HRESULT hr = query_service(unknown, service, &IID_IOleCommandTarget, (void **)&target);
    if (SUCCEEDED(hr)) {
        hr = target->lpVtbl->Exec(target, group, id, options, in, out);
        target->lpVtbl->Release(target);
    }
    return hr;
}

/* nothing here holds references back to its site. */
HRESULT WINAPI IUnknown_RemoveBackReferences(IUnknown *unknown);
HRESULT WINAPI IUnknown_RemoveBackReferences(IUnknown *unknown)
{
    return unknown ? E_NOINTERFACE : E_INVALIDARG;
}

/* the reference is counted on the outer object, not on the aggregated one. */
HRESULT WINAPI SHWeakQueryInterface(IUnknown *outer, IUnknown *inner, REFIID riid, void **out);
HRESULT WINAPI SHWeakQueryInterface(IUnknown *outer, IUnknown *inner, REFIID riid, void **out)
{
    if (!outer || !inner || !out)
        return E_INVALIDARG;
    HRESULT hr = inner->lpVtbl->QueryInterface(inner, riid, out);
    if (SUCCEEDED(hr))
        outer->lpVtbl->Release(outer);
    return hr;
}

void WINAPI SHWeakReleaseInterface(IUnknown *outer, IUnknown **pointer);
void WINAPI SHWeakReleaseInterface(IUnknown *outer, IUnknown **pointer)
{
    if (!outer || !pointer || !*pointer)
        return;
    outer->lpVtbl->AddRef(outer);
    (*pointer)->lpVtbl->Release(*pointer);
    *pointer = nullptr;
}

/* -1 when the key is missing. */
int WINAPI SHSearchMapInt(const int *keys, const int *values, int count, int key);
int WINAPI SHSearchMapInt(const int *keys, const int *values, int count, int key)
{
    for (int i = 0; keys && values && i < count; i++)
        if (keys[i] == key)
            return values[i];
    return -1;
}

HRESULT WINAPI ConnectToConnectionPoint(IUnknown *sink, REFIID riid, BOOL connect, IUnknown *source, DWORD *cookie,
                                        void **point)
{
    if (point)
        *point = nullptr;
    if (!source)
        return E_FAIL;
    IConnectionPointContainer *container;
    HRESULT hr = source->lpVtbl->QueryInterface(source, &IID_IConnectionPointContainer, (void **)&container);
    if (FAILED(hr))
        return hr;
    IConnectionPoint *cp;
    hr = container->lpVtbl->FindConnectionPoint(container, riid, &cp);
    container->lpVtbl->Release(container);
    if (FAILED(hr))
        return hr;
    if (connect) {
        hr = sink ? cp->lpVtbl->Advise(cp, sink, cookie) : E_INVALIDARG;
    } else if (cookie) {
        hr = cp->lpVtbl->Unadvise(cp, *cookie);
        *cookie = 0;
    }
    if (point && SUCCEEDED(hr))
        *point = cp;
    else
        cp->lpVtbl->Release(cp);
    return hr;
}

/* ------------------------------------------------------------------ ole32 on demand */

HRESULT WINAPI SHCoCreateInstanceAC(REFCLSID clsid, IUnknown *outer, DWORD context, REFIID riid, void **out);
HRESULT WINAPI SHCoCreateInstanceAC(REFCLSID clsid, IUnknown *outer, DWORD context, REFIID riid, void **out)
{
    HRESULT(WINAPI * create)(REFCLSID, IUnknown *, DWORD, REFIID, void **) =
        (HRESULT(WINAPI *)(REFCLSID, IUnknown *, DWORD, REFIID, void **))ShlProc(L"ole32.dll", "CoCreateInstance");
    if (out)
        *out = nullptr;
    return create ? create(clsid, outer, context, riid, out) : E_FAIL;
}

HRESULT WINAPI CLSIDFromProgIDWrap(LPCWSTR progid, CLSID *clsid);
HRESULT WINAPI CLSIDFromProgIDWrap(LPCWSTR progid, CLSID *clsid)
{
    HRESULT(WINAPI * convert)(LPCWSTR, CLSID *) =
        (HRESULT(WINAPI *)(LPCWSTR, CLSID *))ShlProc(L"ole32.dll", "CLSIDFromProgID");
    return convert ? convert(progid, clsid) : E_FAIL;
}

HRESULT WINAPI CLSIDFromStringWrap(LPCWSTR text, CLSID *clsid);
HRESULT WINAPI CLSIDFromStringWrap(LPCWSTR text, CLSID *clsid)
{
    HRESULT(WINAPI * convert)(LPCWSTR, CLSID *) =
        (HRESULT(WINAPI *)(LPCWSTR, CLSID *))ShlProc(L"ole32.dll", "CLSIDFromString");
    return convert ? convert(text, clsid) : E_FAIL;
}

/* ------------------------------------------------------------------ dispatch parameters */

typedef struct {
    VARIANTARG *rgvarg;
    LONG *rgdispidNamedArgs;
    UINT cArgs;
    UINT cNamedArgs;
} SHL_DISPPARAMS;

/* the varargs are type and value pairs, stored last to first as IDispatch::Invoke wants. */
HRESULT WINAPI SHPackDispParamsV(SHL_DISPPARAMS *params, VARIANTARG *args, UINT count, va_list list);
HRESULT WINAPI SHPackDispParamsV(SHL_DISPPARAMS *params, VARIANTARG *args, UINT count, va_list list)
{
    if (!params)
        return E_INVALIDARG;
    params->rgvarg = args;
    params->rgdispidNamedArgs = nullptr;
    params->cArgs = count;
    params->cNamedArgs = 0;
    for (UINT i = count; i > 0; i--) {
        VARIANTARG *v = &args[i - 1];
        memset(v, 0, sizeof(*v));
        v->vt = (VARTYPE)va_arg(list, int);
        switch (v->vt & ~VT_BYREF) {
        case VT_I8:
        case VT_UI8:
        case VT_CY:
            if (v->vt & VT_BYREF)
                v->byref = va_arg(list, void *);
            else
                v->llVal = va_arg(list, LONGLONG);
            break;
        case VT_R8:
        case VT_DATE:
            if (v->vt & VT_BYREF)
                v->byref = va_arg(list, void *);
            else
                v->dblVal = va_arg(list, double);
            break;
        case VT_BSTR:
        case VT_DISPATCH:
        case VT_UNKNOWN:
        case VT_VARIANT:
            v->byref = va_arg(list, void *);
            break;
        default:
            if (v->vt & VT_BYREF)
                v->byref = va_arg(list, void *);
            else
                v->lVal = va_arg(list, LONG);
            break;
        }
    }
    return S_OK;
}

HRESULT WINAPIV SHPackDispParams(SHL_DISPPARAMS *params, VARIANTARG *args, UINT count, ...);
HRESULT WINAPIV SHPackDispParams(SHL_DISPPARAMS *params, VARIANTARG *args, UINT count, ...)
{
    va_list list;
    va_start(list, count);
    HRESULT hr = SHPackDispParamsV(params, args, count, list);
    va_end(list);
    return hr;
}

/* ------------------------------------------------------------------ property bags */

static void variant_clear(VARIANT *v)
{
    HRESULT(WINAPI * clear)(VARIANT *) = (HRESULT(WINAPI *)(VARIANT *))ShlProc(L"oleaut32.dll", "VariantClear");
    if (clear)
        clear(v);
}

static BSTR bstr(LPCWSTR text)
{
    BSTR(WINAPI * alloc)(const OLECHAR *) = (BSTR(WINAPI *)(const OLECHAR *))ShlProc(L"oleaut32.dll",
                                                                                      "SysAllocString");
    return alloc ? alloc(text) : nullptr;
}

static HRESULT bag_read(IPropertyBag *bag, LPCWSTR name, VARIANT *v, VARTYPE type)
{
    memset(v, 0, sizeof(*v));
    if (!bag || !name)
        return E_INVALIDARG;
    v->vt = type;
    HRESULT hr = bag->lpVtbl->Read(bag, name, v, nullptr);
    if (SUCCEEDED(hr) && v->vt != type) {
        variant_clear(v);
        hr = E_FAIL;
    }
    return hr;
}

static HRESULT bag_write(IPropertyBag *bag, LPCWSTR name, VARIANT *v)
{
    if (!bag || !name)
        return E_INVALIDARG;
    return bag->lpVtbl->Write(bag, name, v);
}

HRESULT WINAPI SHPropertyBag_ReadType(IPropertyBag *bag, LPCWSTR name, VARIANT *value, VARTYPE type);
HRESULT WINAPI SHPropertyBag_ReadType(IPropertyBag *bag, LPCWSTR name, VARIANT *value, VARTYPE type)
{
    if (!value)
        return E_INVALIDARG;
    return bag_read(bag, name, value, type);
}

#define BAG_NUMBER(read, write, type, variant_type, member)                                                                      \
    HRESULT WINAPI read(IPropertyBag *bag, LPCWSTR name, type *value);                                                 \
    HRESULT WINAPI read(IPropertyBag *bag, LPCWSTR name, type *value)                                                  \
    {                                                                                                                  \
        if (!value)                                                                                                    \
            return E_INVALIDARG;                                                                                       \
        VARIANT v;                                                                                                     \
        HRESULT hr = bag_read(bag, name, &v, variant_type);                                                                      \
        if (SUCCEEDED(hr))                                                                                             \
            *value = (type)v.member;                                                                                   \
        return hr;                                                                                                     \
    }                                                                                                                  \
    HRESULT WINAPI write(IPropertyBag *bag, LPCWSTR name, type value);                                                 \
    HRESULT WINAPI write(IPropertyBag *bag, LPCWSTR name, type value)                                                  \
    {                                                                                                                  \
        VARIANT v;                                                                                                     \
        memset(&v, 0, sizeof(v));                                                                                      \
        v.vt = variant_type;                                                                                                     \
        v.member = value;                                                                                              \
        return bag_write(bag, name, &v);                                                                               \
    }

BAG_NUMBER(SHPropertyBag_ReadInt, SHPropertyBag_WriteInt, INT, VT_I4, lVal)
BAG_NUMBER(SHPropertyBag_ReadLONG, SHPropertyBag_WriteLONG, LONG, VT_I4, lVal)
BAG_NUMBER(SHPropertyBag_ReadDWORD, SHPropertyBag_WriteDWORD, DWORD, VT_UI4, ulVal)
BAG_NUMBER(SHPropertyBag_ReadSHORT, SHPropertyBag_WriteSHORT, SHORT, VT_I2, iVal)
BAG_NUMBER(SHPropertyBag_ReadULONGLONG, SHPropertyBag_WriteULONGLONG, ULONGLONG, VT_UI8, ullVal)

HRESULT WINAPI SHPropertyBag_ReadBOOL(IPropertyBag *bag, LPCWSTR name, BOOL *value);
HRESULT WINAPI SHPropertyBag_ReadBOOL(IPropertyBag *bag, LPCWSTR name, BOOL *value)
{
    if (!value)
        return E_INVALIDARG;
    VARIANT v;
    HRESULT hr = bag_read(bag, name, &v, VT_BOOL);
    if (SUCCEEDED(hr))
        *value = v.boolVal != 0;
    return hr;
}

HRESULT WINAPI SHPropertyBag_WriteBOOL(IPropertyBag *bag, LPCWSTR name, BOOL value);
HRESULT WINAPI SHPropertyBag_WriteBOOL(IPropertyBag *bag, LPCWSTR name, BOOL value)
{
    VARIANT v;
    memset(&v, 0, sizeof(v));
    v.vt = VT_BOOL;
    v.boolVal = value ? -1 : 0;
    return bag_write(bag, name, &v);
}

HRESULT WINAPI SHPropertyBag_ReadBSTR(IPropertyBag *bag, LPCWSTR name, BSTR *value);
HRESULT WINAPI SHPropertyBag_ReadBSTR(IPropertyBag *bag, LPCWSTR name, BSTR *value)
{
    if (!value)
        return E_INVALIDARG;
    *value = nullptr;
    VARIANT v;
    HRESULT hr = bag_read(bag, name, &v, VT_BSTR);
    if (SUCCEEDED(hr))
        *value = v.bstrVal;
    return hr;
}

HRESULT WINAPI SHPropertyBag_WriteBSTR(IPropertyBag *bag, LPCWSTR name, BSTR value);
HRESULT WINAPI SHPropertyBag_WriteBSTR(IPropertyBag *bag, LPCWSTR name, BSTR value)
{
    VARIANT v;
    memset(&v, 0, sizeof(v));
    v.vt = VT_BSTR;
    v.bstrVal = value;
    return bag_write(bag, name, &v);
}

HRESULT WINAPI SHPropertyBag_ReadStr(IPropertyBag *bag, LPCWSTR name, LPWSTR out, int size);
HRESULT WINAPI SHPropertyBag_ReadStr(IPropertyBag *bag, LPCWSTR name, LPWSTR out, int size)
{
    if (!out || size <= 0)
        return E_INVALIDARG;
    *out = 0;
    VARIANT v;
    HRESULT hr = bag_read(bag, name, &v, VT_BSTR);
    if (SUCCEEDED(hr)) {
        lstrcpynW(out, v.bstrVal ? v.bstrVal : L"", size);
        variant_clear(&v);
    }
    return hr;
}

HRESULT WINAPI SHPropertyBag_ReadStrAlloc(IPropertyBag *bag, LPCWSTR name, LPWSTR *out);
HRESULT WINAPI SHPropertyBag_ReadStrAlloc(IPropertyBag *bag, LPCWSTR name, LPWSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    VARIANT v;
    HRESULT hr = bag_read(bag, name, &v, VT_BSTR);
    if (SUCCEEDED(hr)) {
        LPCWSTR text = v.bstrVal ? v.bstrVal : L"";
        SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
        *out = ShlTaskAlloc(bytes);
        if (*out)
            memcpy(*out, text, bytes);
        else
            hr = E_OUTOFMEMORY;
        variant_clear(&v);
    }
    return hr;
}

HRESULT WINAPI SHPropertyBag_WriteStr(IPropertyBag *bag, LPCWSTR name, LPCWSTR value);
HRESULT WINAPI SHPropertyBag_WriteStr(IPropertyBag *bag, LPCWSTR name, LPCWSTR value)
{
    VARIANT v;
    memset(&v, 0, sizeof(v));
    v.vt = VT_BSTR;
    v.bstrVal = bstr(value ? value : L"");
    if (!v.bstrVal)
        return E_OUTOFMEMORY;
    HRESULT hr = bag_write(bag, name, &v);
    variant_clear(&v);
    return hr;
}

HRESULT WINAPI SHPropertyBag_ReadGUID(IPropertyBag *bag, LPCWSTR name, GUID *value);
HRESULT WINAPI SHPropertyBag_ReadGUID(IPropertyBag *bag, LPCWSTR name, GUID *value)
{
    BOOL WINAPI GUIDFromStringW(LPCWSTR text, GUID *guid);
    WCHAR text[40];
    HRESULT hr = SHPropertyBag_ReadStr(bag, name, text, RTL_NUMBER_OF(text));
    if (SUCCEEDED(hr) && (!value || !GUIDFromStringW(text, value)))
        hr = E_FAIL;
    return hr;
}

HRESULT WINAPI SHPropertyBag_WriteGUID(IPropertyBag *bag, LPCWSTR name, const GUID *value);
HRESULT WINAPI SHPropertyBag_WriteGUID(IPropertyBag *bag, LPCWSTR name, const GUID *value)
{
    int WINAPI SHStringFromGUIDW(REFGUID guid, LPWSTR out, int size);
    WCHAR text[40];
    if (!value || !SHStringFromGUIDW(value, text, RTL_NUMBER_OF(text)))
        return E_INVALIDARG;
    return SHPropertyBag_WriteStr(bag, name, text);
}

HRESULT WINAPI SHPropertyBag_ReadUnknown(IPropertyBag *bag, LPCWSTR name, REFIID riid, void **out);
HRESULT WINAPI SHPropertyBag_ReadUnknown(IPropertyBag *bag, LPCWSTR name, REFIID riid, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    VARIANT v;
    HRESULT hr = bag_read(bag, name, &v, VT_UNKNOWN);
    if (SUCCEEDED(hr)) {
        hr = v.punkVal ? v.punkVal->lpVtbl->QueryInterface(v.punkVal, riid, out) : E_FAIL;
        variant_clear(&v);
    }
    return hr;
}

HRESULT WINAPI SHPropertyBag_WriteUnknown(IPropertyBag *bag, LPCWSTR name, IUnknown *value);
HRESULT WINAPI SHPropertyBag_WriteUnknown(IPropertyBag *bag, LPCWSTR name, IUnknown *value)
{
    VARIANT v;
    memset(&v, 0, sizeof(v));
    v.vt = VT_UNKNOWN;
    v.punkVal = value;
    return bag_write(bag, name, &v);
}

HRESULT WINAPI SHPropertyBag_ReadStream(IPropertyBag *bag, LPCWSTR name, IStream **out);
HRESULT WINAPI SHPropertyBag_ReadStream(IPropertyBag *bag, LPCWSTR name, IStream **out)
{
    return SHPropertyBag_ReadUnknown(bag, name, &IID_IStream, (void **)out);
}

HRESULT WINAPI SHPropertyBag_WriteStream(IPropertyBag *bag, LPCWSTR name, IStream *value);
HRESULT WINAPI SHPropertyBag_WriteStream(IPropertyBag *bag, LPCWSTR name, IStream *value)
{
    return SHPropertyBag_WriteUnknown(bag, name, (IUnknown *)value);
}

/* writes an empty variant over the property. */
HRESULT WINAPI SHPropertyBag_Delete(IPropertyBag *bag, LPCWSTR name);
HRESULT WINAPI SHPropertyBag_Delete(IPropertyBag *bag, LPCWSTR name)
{
    VARIANT v;
    memset(&v, 0, sizeof(v));
    return bag_write(bag, name, &v);
}

/* points and rectangles as one LONG property per member, "name.x" and so on. */
static HRESULT bag_longs(IPropertyBag *bag, LPCWSTR name, const PCWSTR *suffixes, LONG *values, int count, BOOL write)
{
    if (!name || !values || wcslen(name) > 200)
        return E_INVALIDARG;
    for (int i = 0; i < count; i++) {
        WCHAR member[220];
        wsprintfW(member, L"%s%s", name, suffixes[i]);
        HRESULT hr = write ? SHPropertyBag_WriteLONG(bag, member, values[i])
                           : SHPropertyBag_ReadLONG(bag, member, &values[i]);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static const PCWSTR point_members[] = {L".x", L".y"};
static const PCWSTR rect_members[] = {L".left", L".top", L".right", L".bottom"};

HRESULT WINAPI SHPropertyBag_ReadPOINTL(IPropertyBag *bag, LPCWSTR name, POINTL *value);
HRESULT WINAPI SHPropertyBag_ReadPOINTL(IPropertyBag *bag, LPCWSTR name, POINTL *value)
{
    return bag_longs(bag, name, point_members, (LONG *)value, 2, FALSE);
}

HRESULT WINAPI SHPropertyBag_WritePOINTL(IPropertyBag *bag, LPCWSTR name, const POINTL *value);
HRESULT WINAPI SHPropertyBag_WritePOINTL(IPropertyBag *bag, LPCWSTR name, const POINTL *value)
{
    return bag_longs(bag, name, point_members, (LONG *)value, 2, TRUE);
}

HRESULT WINAPI SHPropertyBag_ReadRECTL(IPropertyBag *bag, LPCWSTR name, RECTL *value);
HRESULT WINAPI SHPropertyBag_ReadRECTL(IPropertyBag *bag, LPCWSTR name, RECTL *value)
{
    return bag_longs(bag, name, rect_members, (LONG *)value, 4, FALSE);
}

HRESULT WINAPI SHPropertyBag_WriteRECTL(IPropertyBag *bag, LPCWSTR name, const RECTL *value);
HRESULT WINAPI SHPropertyBag_WriteRECTL(IPropertyBag *bag, LPCWSTR name, const RECTL *value)
{
    return bag_longs(bag, name, rect_members, (LONG *)value, 4, TRUE);
}

HRESULT WINAPI SHPropertyBag_ReadPOINTS(IPropertyBag *bag, LPCWSTR name, POINTS *value);
HRESULT WINAPI SHPropertyBag_ReadPOINTS(IPropertyBag *bag, LPCWSTR name, POINTS *value)
{
    LONG values[2];
    if (!value)
        return E_INVALIDARG;
    HRESULT hr = bag_longs(bag, name, point_members, values, 2, FALSE);
    if (SUCCEEDED(hr)) {
        value->x = (SHORT)values[0];
        value->y = (SHORT)values[1];
    }
    return hr;
}

HRESULT WINAPI SHPropertyBag_WritePOINTS(IPropertyBag *bag, LPCWSTR name, const POINTS *value);
HRESULT WINAPI SHPropertyBag_WritePOINTS(IPropertyBag *bag, LPCWSTR name, const POINTS *value)
{
    if (!value)
        return E_INVALIDARG;
    LONG values[2] = {value->x, value->y};
    return bag_longs(bag, name, point_members, values, 2, TRUE);
}

/* ------------------------------------------------------------------ streams */

BOOL WINAPI SHIsEmptyStream(IStream *stream);
BOOL WINAPI SHIsEmptyStream(IStream *stream)
{
    if (!stream)
        return TRUE;
    STATSTG stat;
    if (SUCCEEDED(stream->lpVtbl->Stat(stream, &stat, STATFLAG_NONAME)))
        return !stat.cbSize.QuadPart;
    ULARGE_INTEGER position, end;
    LARGE_INTEGER zero = {0};
    if (FAILED(stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_CUR, &position)) ||
        FAILED(stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_END, &end)))
        return TRUE;
    LARGE_INTEGER back;
    back.QuadPart = (LONGLONG)position.QuadPart;
    stream->lpVtbl->Seek(stream, back, STREAM_SEEK_SET, nullptr);
    return !end.QuadPart;
}

static HRESULT read_exact(IStream *stream, void *data, ULONG size)
{
    ULONG read = 0;
    HRESULT hr = stream->lpVtbl->Read(stream, data, size, &read);
    if (SUCCEEDED(hr) && read != size)
        hr = E_FAIL;
    return hr;
}

static HRESULT write_exact(IStream *stream, const void *data, ULONG size)
{
    ULONG written = 0;
    HRESULT hr = stream->lpVtbl->Write(stream, data, size, &written);
    if (SUCCEEDED(hr) && written != size)
        hr = E_FAIL;
    return hr;
}

/* a DWORD character count, then the characters without a terminator. */
HRESULT WINAPI IStream_ReadStrLong(IStream *stream, LPWSTR *out);
HRESULT WINAPI IStream_ReadStrLong(IStream *stream, LPWSTR *out)
{
    if (!stream || !out)
        return E_INVALIDARG;
    *out = nullptr;
    DWORD count;
    HRESULT hr = read_exact(stream, &count, sizeof(count));
    if (FAILED(hr))
        return hr;
    if (count > 0x7FFFFFF)
        return E_FAIL;
    *out = ShlTaskAlloc(((SIZE_T)count + 1) * sizeof(WCHAR));
    if (!*out)
        return E_OUTOFMEMORY;
    hr = count ? read_exact(stream, *out, count * sizeof(WCHAR)) : S_OK;
    if (FAILED(hr)) {
        ShlTaskFree(*out);
        *out = nullptr;
        return hr;
    }
    (*out)[count] = 0;
    return S_OK;
}

HRESULT WINAPI IStream_WriteStrLong(IStream *stream, LPCWSTR text);
HRESULT WINAPI IStream_WriteStrLong(IStream *stream, LPCWSTR text)
{
    if (!stream)
        return E_INVALIDARG;
    DWORD count = text ? (DWORD)wcslen(text) : 0;
    HRESULT hr = write_exact(stream, &count, sizeof(count));
    if (SUCCEEDED(hr) && count)
        hr = write_exact(stream, text, count * sizeof(WCHAR));
    return hr;
}

static ULONG pidl_size(const BYTE *pidl)
{
    ULONG size = 0;
    while (pidl && *(const USHORT *)(pidl + size))
        size += *(const USHORT *)(pidl + size);
    return size + sizeof(USHORT);
}

/* the byte size, then the item id list. */
HRESULT WINAPI IStream_ReadPidl(IStream *stream, struct _ITEMIDLIST **out);
HRESULT WINAPI IStream_ReadPidl(IStream *stream, struct _ITEMIDLIST **out)
{
    if (!stream || !out)
        return E_INVALIDARG;
    *out = nullptr;
    ULONG size;
    HRESULT hr = read_exact(stream, &size, sizeof(size));
    if (FAILED(hr))
        return hr;
    if (size < sizeof(USHORT) || size > 0x100000)
        return E_FAIL;
    BYTE *pidl = ShlTaskAlloc(size);
    if (!pidl)
        return E_OUTOFMEMORY;
    hr = read_exact(stream, pidl, size);
    if (FAILED(hr) || pidl_size(pidl) > size) {
        ShlTaskFree(pidl);
        return FAILED(hr) ? hr : E_FAIL;
    }
    *out = (struct _ITEMIDLIST *)pidl;
    return S_OK;
}

HRESULT WINAPI IStream_WritePidl(IStream *stream, const struct _ITEMIDLIST *pidl);
HRESULT WINAPI IStream_WritePidl(IStream *stream, const struct _ITEMIDLIST *pidl)
{
    if (!stream || !pidl)
        return E_INVALIDARG;
    ULONG size = pidl_size((const BYTE *)pidl);
    HRESULT hr = write_exact(stream, &size, sizeof(size));
    if (SUCCEEDED(hr))
        hr = write_exact(stream, pidl, size);
    return hr;
}

HRESULT WINAPI MapWin32ErrorToSTG(HRESULT hr);
HRESULT WINAPI MapWin32ErrorToSTG(HRESULT hr)
{
    static const struct {
        DWORD error;
        HRESULT storage;
    } map[] = {
        {ERROR_FILE_NOT_FOUND, STG_E_FILENOTFOUND},
        {ERROR_PATH_NOT_FOUND, (HRESULT)0x80030003L},
        {ERROR_ACCESS_DENIED, STG_E_ACCESSDENIED},
        {ERROR_ALREADY_EXISTS, (HRESULT)0x80030050L},
        {ERROR_FILE_EXISTS, (HRESULT)0x80030050L},
        {ERROR_INVALID_NAME, STG_E_INVALIDNAME},
        {ERROR_SHARING_VIOLATION, (HRESULT)0x80030020L},
        {ERROR_LOCK_VIOLATION, (HRESULT)0x80030021L},
        {ERROR_DISK_FULL, STG_E_MEDIUMFULL},
        {ERROR_NOT_ENOUGH_MEMORY, (HRESULT)0x80030008L},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(map); i++)
        if (hr == HRESULT_FROM_WIN32(map[i].error))
            return map[i].storage;
    return hr;
}

HRESULT WINAPI ModeToCreateFileFlags(DWORD mode, BOOL create, DWORD *access, DWORD *share, DWORD *disposition);
HRESULT WINAPI ModeToCreateFileFlags(DWORD mode, BOOL create, DWORD *access, DWORD *share, DWORD *disposition)
{
    if (!access || !share || !disposition)
        return E_INVALIDARG;
    switch (mode & 3) {
    case STGM_READ:
        *access = GENERIC_READ;
        break;
    case STGM_WRITE:
        *access = GENERIC_WRITE;
        break;
    case STGM_READWRITE:
        *access = GENERIC_READ | GENERIC_WRITE;
        break;
    default:
        return E_INVALIDARG;
    }
    switch (mode & 0x70) {
    case 0:
    case STGM_SHARE_DENY_NONE:
        *share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        break;
    case STGM_SHARE_DENY_READ:
        *share = FILE_SHARE_WRITE;
        break;
    case STGM_SHARE_DENY_WRITE:
        *share = FILE_SHARE_READ;
        break;
    case STGM_SHARE_EXCLUSIVE:
        *share = 0;
        break;
    default:
        return E_INVALIDARG;
    }
    if (mode & STGM_CREATE)
        *disposition = CREATE_ALWAYS;
    else
        *disposition = create ? CREATE_NEW : OPEN_EXISTING;
    return S_OK;
}

HRESULT WINAPI SHCreateStreamOnModuleResourceW(HMODULE module, LPCWSTR name, LPCWSTR type, IStream **out);
HRESULT WINAPI SHCreateStreamOnModuleResourceW(HMODULE module, LPCWSTR name, LPCWSTR type, IStream **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRSRC resource = FindResourceW(module, name, type);
    HGLOBAL data = resource ? LoadResource(module, resource) : nullptr;
    if (!data)
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    IStream *(WINAPI * create)(const BYTE *, UINT) =
        (IStream * (WINAPI *)(const BYTE *, UINT)) ShlProc(L"shcore.dll", "SHCreateMemStream");
    *out = create ? create(LockResource(data), SizeofResource(module, resource)) : nullptr;
    return *out ? S_OK : E_OUTOFMEMORY;
}

HRESULT WINAPI SHCreateStreamOnDllResourceW(LPCWSTR dll, LPCWSTR name, LPCWSTR type, IStream **out);
HRESULT WINAPI SHCreateStreamOnDllResourceW(LPCWSTR dll, LPCWSTR name, LPCWSTR type, IStream **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HMODULE module = LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!module)
        return HRESULT_FROM_WIN32(GetLastError());
    HRESULT hr = SHCreateStreamOnModuleResourceW(module, name, type, out);
    FreeLibrary(module);
    return hr;
}

/* ------------------------------------------------------------------ data block lists */

typedef struct {
    DWORD cbSize;
    DWORD dwSignature;
} DATABLOCK_HEADER;

static DWORD list_size(const BYTE *list)
{
    DWORD size = 0;
    while (list && *(const DWORD *)(list + size) >= sizeof(DATABLOCK_HEADER))
        size += *(const DWORD *)(list + size);
    return size;
}

DATABLOCK_HEADER *WINAPI SHFindDataBlock(void *list, DWORD signature);
DATABLOCK_HEADER *WINAPI SHFindDataBlock(void *list, DWORD signature)
{
    BYTE *p = list;
    while (p && ((DATABLOCK_HEADER *)p)->cbSize >= sizeof(DATABLOCK_HEADER)) {
        if (((DATABLOCK_HEADER *)p)->dwSignature == signature)
            return (DATABLOCK_HEADER *)p;
        p += ((DATABLOCK_HEADER *)p)->cbSize;
    }
    return nullptr;
}

BOOL WINAPI SHRemoveDataBlock(void **list, DWORD signature);
BOOL WINAPI SHRemoveDataBlock(void **list, DWORD signature)
{
    if (!list)
        return FALSE;
    BYTE *block = (BYTE *)SHFindDataBlock(*list, signature);
    if (!block)
        return FALSE;
    DWORD size = ((DATABLOCK_HEADER *)block)->cbSize, total = list_size(*list);
    BYTE *end = (BYTE *)*list + total;
    memmove(block, block + size, (SIZE_T)(end - block - size) + sizeof(DWORD));
    return TRUE;
}

/* appends a copy, replacing any block with the same signature. */
BOOL WINAPI SHAddDataBlock(void **list, const DATABLOCK_HEADER *block);
BOOL WINAPI SHAddDataBlock(void **list, const DATABLOCK_HEADER *block)
{
    if (!list || !block || block->cbSize < sizeof(DATABLOCK_HEADER))
        return FALSE;
    SHRemoveDataBlock(list, block->dwSignature);
    DWORD used = list_size(*list);
    BYTE *grown = *list ? LocalReAlloc(*list, used + block->cbSize + sizeof(DWORD), LMEM_MOVEABLE)
                        : LocalAlloc(LMEM_FIXED, block->cbSize + sizeof(DWORD));
    if (!grown)
        return FALSE;
    memcpy(grown + used, block, block->cbSize);
    *(DWORD *)(grown + used + block->cbSize) = 0;
    *list = grown;
    return TRUE;
}

void WINAPI SHFreeDataBlockList(void *list);
void WINAPI SHFreeDataBlockList(void *list)
{
    if (list)
        LocalFree(list);
}

HRESULT WINAPI SHWriteDataBlockList(IStream *stream, void *list);
HRESULT WINAPI SHWriteDataBlockList(IStream *stream, void *list)
{
    if (!stream)
        return E_INVALIDARG;
    DWORD size = list_size(list), end = 0;
    HRESULT hr = size ? write_exact(stream, list, size) : S_OK;
    if (SUCCEEDED(hr))
        hr = write_exact(stream, &end, sizeof(end));
    return hr;
}

HRESULT WINAPI SHReadDataBlockList(IStream *stream, void **list);
HRESULT WINAPI SHReadDataBlockList(IStream *stream, void **list)
{
    if (!stream || !list)
        return E_INVALIDARG;
    *list = nullptr;
    for (;;) {
        DWORD size;
        HRESULT hr = read_exact(stream, &size, sizeof(size));
        if (FAILED(hr))
            return hr;
        if (!size)
            return S_OK;
        if (size < sizeof(DATABLOCK_HEADER) || size > 0x100000)
            return E_FAIL;
        DATABLOCK_HEADER *block = ShlAlloc(size);
        if (!block)
            return E_OUTOFMEMORY;
        block->cbSize = size;
        hr = read_exact(stream, &block->dwSignature, size - sizeof(DWORD));
        if (SUCCEEDED(hr) && !SHAddDataBlock(list, block))
            hr = E_OUTOFMEMORY;
        ShlFree(block);
        if (FAILED(hr))
            return hr;
    }
}

/* ------------------------------------------------------------------ shared memory */

/* the data size comes before the data. the mapping belongs to the target process. */
typedef struct {
    DWORD size;
    DWORD reserved;
} SHARED_HEADER;

HANDLE WINAPI SHMapHandle(HANDLE handle, DWORD source, DWORD target, DWORD access, DWORD options);
HANDLE WINAPI SHMapHandle(HANDLE handle, DWORD source, DWORD target, DWORD access, DWORD options)
{
    DWORD self = GetCurrentProcessId();
    HANDLE from = source == self ? GetCurrentProcess() : OpenProcess(PROCESS_DUP_HANDLE, FALSE, source);
    HANDLE to = target == self ? GetCurrentProcess() : OpenProcess(PROCESS_DUP_HANDLE, FALSE, target);
    HANDLE result = nullptr;
    if (from && to && !DuplicateHandle(from, handle, to, &result, access, FALSE, options))
        result = nullptr;
    if (from && source != self)
        CloseHandle(from);
    if (to && target != self)
        CloseHandle(to);
    return result;
}

HANDLE WINAPI SHAllocShared(const void *data, DWORD size, DWORD process)
{
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        size + sizeof(SHARED_HEADER), nullptr);
    if (!mapping)
        return nullptr;
    SHARED_HEADER *view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0);
    if (!view) {
        CloseHandle(mapping);
        return nullptr;
    }
    view->size = size;
    if (data)
        memcpy(view + 1, data, size);
    UnmapViewOfFile(view);
    if (!process || process == GetCurrentProcessId())
        return mapping;
    return SHMapHandle(mapping, GetCurrentProcessId(), process, 0, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
}

void *WINAPI SHLockSharedEx(HANDLE shared, DWORD process, BOOL write);
void *WINAPI SHLockSharedEx(HANDLE shared, DWORD process, BOOL write)
{
    HANDLE mapping = shared;
    if (process && process != GetCurrentProcessId())
        mapping = SHMapHandle(shared, process, GetCurrentProcessId(), FILE_MAP_ALL_ACCESS, 0);
    if (!mapping)
        return nullptr;
    SHARED_HEADER *view = MapViewOfFile(mapping, write ? FILE_MAP_WRITE : FILE_MAP_READ, 0, 0, 0);
    if (mapping != shared)
        CloseHandle(mapping);
    return view ? view + 1 : nullptr;
}

void *WINAPI SHLockShared(HANDLE shared, DWORD process)
{
    return SHLockSharedEx(shared, process, TRUE);
}

BOOL WINAPI SHUnlockShared(void *view)
{
    if (!view)
        return FALSE;
    return UnmapViewOfFile((SHARED_HEADER *)view - 1);
}

DWORD WINAPI SHGetSizeShared(const void *view);
DWORD WINAPI SHGetSizeShared(const void *view)
{
    return view ? ((const SHARED_HEADER *)view - 1)->size : 0;
}

BOOL WINAPI SHFreeShared(HANDLE shared, DWORD process)
{
    if (!shared)
        return FALSE;
    if (process && process != GetCurrentProcessId()) {
        HANDLE local = SHMapHandle(shared, process, GetCurrentProcessId(), 0,
                                   DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
        return local && CloseHandle(local);
    }
    return CloseHandle(shared);
}

/* ------------------------------------------------------------------ threads */

BOOL WINAPI SHQueueUserWorkItem(LPTHREAD_START_ROUTINE function, void *context, LONG priority, DWORD_PTR tag,
                                DWORD_PTR *id, LPCSTR module, DWORD flags);
BOOL WINAPI SHQueueUserWorkItem(LPTHREAD_START_ROUTINE function, void *context, LONG priority, DWORD_PTR tag,
                                DWORD_PTR *id, LPCSTR module, DWORD flags)
{
    if (id)
        *id = 0;
    if (module)
        LoadLibraryA(module);
    return QueueUserWorkItem(function, context, WT_EXECUTEDEFAULT);
}

/* keeps dispatching sent messages, and the ones in mask, while waiting. */
static DWORD wait_pumping(HANDLE handle, DWORD timeout, DWORD mask)
{
    DWORD start = GetTickCount();
    for (;;) {
        DWORD left = INFINITE;
        if (timeout != INFINITE) {
            DWORD spent = GetTickCount() - start;
            left = spent >= timeout ? 0 : timeout - spent;
        }
        DWORD result = MsgWaitForMultipleObjects(1, &handle, FALSE, left, mask);
        if (result != WAIT_OBJECT_0 + 1)
            return result;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

DWORD WINAPI SHWaitForSendMessageThread(HANDLE thread, DWORD timeout);
DWORD WINAPI SHWaitForSendMessageThread(HANDLE thread, DWORD timeout)
{
    return wait_pumping(thread, timeout, QS_SENDMESSAGE);
}

DWORD WINAPI SHWaitForThreadWithWakeMask(HANDLE thread, DWORD timeout, DWORD mask);
DWORD WINAPI SHWaitForThreadWithWakeMask(HANDLE thread, DWORD timeout, DWORD mask)
{
    return wait_pumping(thread, timeout, mask | QS_SENDMESSAGE);
}
