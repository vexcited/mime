/*
 * display names turned into monikers, and the small ole32 entry points that go with them. the class, file and
 * progid forms are parsed. the session and elevation forms, and ole1 classes, are not.
 */
#define IsEqualGUID mime_inline_IsEqualGUID
#include "ole32_private.h"
#undef IsEqualGUID

#define MK_E_UNAVAILABLE_ ((HRESULT)0x800401E3)
#define MK_E_SYNTAX_ ((HRESULT)0x800401E4)
#define MK_E_CANTOPENFILE_ ((HRESULT)0x800401EA)

WINOLEAPI BOOL WINAPI IsEqualGUID(REFGUID a, REFGUID b)
{
    return mime_inline_IsEqualGUID(a, b);
}

WINOLEAPI HRESULT WINAPI CoFileTimeNow(FILETIME *time)
{
    if (!time)
        return E_INVALIDARG;
    SYSTEMTIME now;
    GetSystemTime(&now);
    if (SystemTimeToFileTime(&now, time))
        return S_OK;
    time->dwLowDateTime = time->dwHighDateTime = 0;
    return HRESULT_FROM_WIN32(GetLastError());
}

/* the security of a proxy is asked of the proxy's own client security interface. */
WINOLEAPI HRESULT WINAPI CoQueryProxyBlanket(IUnknown *proxy, DWORD *authn_service, DWORD *authz_service,
                                             OLECHAR **server_principal, DWORD *authn_level, DWORD *imp_level,
                                             void **auth_info, DWORD *capabilities)
{
    if (!proxy)
        return E_INVALIDARG;
    IClientSecurity *security;
    HRESULT hr = proxy->lpVtbl->QueryInterface(proxy, &IID_IClientSecurity, (void **)&security);
    if (FAILED(hr))
        return hr;
    hr = security->lpVtbl->QueryBlanket(security, proxy, authn_service, authz_service, server_principal, authn_level,
                                        imp_level, auth_info, capabilities);
    security->lpVtbl->Release(security);
    return hr;
}

WINOLEAPI HRESULT WINAPI CoCopyProxy(IUnknown *proxy, IUnknown **copy)
{
    if (!proxy || !copy)
        return E_INVALIDARG;
    IClientSecurity *security;
    HRESULT hr = proxy->lpVtbl->QueryInterface(proxy, &IID_IClientSecurity, (void **)&security);
    if (FAILED(hr))
        return hr;
    hr = security->lpVtbl->CopyProxy(security, proxy, copy);
    security->lpVtbl->Release(security);
    return hr;
}

WINOLEAPI HRESULT WINAPI BindMoniker(IMoniker *moniker, DWORD options, REFIID iid, void **result)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    if (!moniker || options)
        return E_INVALIDARG;
    IBindCtx *context;
    HRESULT hr = CreateBindCtx(0, &context);
    if (FAILED(hr))
        return hr;
    hr = moniker->lpVtbl->BindToObject(moniker, context, nullptr, iid, result);
    context->lpVtbl->Release(context);
    return hr;
}

/* ------------------------------------------------------------------ display names */

static HRESULT parse_with_class(const CLSID *clsid, IBindCtx *context, LPOLESTR name, ULONG *eaten, IMoniker **moniker,
                                DWORD create_context)
{
    IParseDisplayName *parser;
    if (FAILED(CoGetClassObject(clsid, CLSCTX_INPROC | CLSCTX_LOCAL_SERVER | CLSCTX_NO_CODE_DOWNLOAD, nullptr,
                                &IID_IParseDisplayName_, (void **)&parser)) &&
        FAILED(CoCreateInstance(clsid, nullptr, create_context, &IID_IParseDisplayName_, (void **)&parser)))
        return MK_E_UNAVAILABLE_;
    HRESULT hr = parser->lpVtbl->ParseDisplayName(parser, context, name, eaten, moniker);
    parser->lpVtbl->Release(parser);
    return hr;
}

/* "{clsid}:rest" asks the class itself to parse the name. */
static HRESULT find_class_moniker(IBindCtx *context, LPOLESTR name, ULONG *eaten, IMoniker **moniker)
{
    *moniker = nullptr;
    *eaten = 0;
    const WCHAR *colon = wcschr(name, L':');
    if (!colon || colon - name < 2)
        return MK_E_UNAVAILABLE_;
    WCHAR text[64];
    SIZE_T length = (SIZE_T)(colon - name);
    if (length >= ARRAYSIZE(text))
        return MK_E_UNAVAILABLE_;
    memcpy(text, name, length * sizeof(WCHAR));
    text[length] = 0;
    CLSID clsid;
    if (FAILED(CLSIDFromString(text, &clsid)))
        return MK_E_UNAVAILABLE_;
    return parse_with_class(&clsid, context, name, eaten, moniker, CLSCTX_INPROC | CLSCTX_LOCAL_SERVER);
}

/* "@progid:rest" is the same with the class named by its program identifier. */
static HRESULT find_progid_moniker(IBindCtx *context, LPOLESTR name, ULONG *eaten, IMoniker **moniker)
{
    *eaten = 0;
    *moniker = nullptr;
    const WCHAR *end = name + 1;
    if (*end >= L'0' && *end <= L'9')
        return MK_E_SYNTAX_;
    while ((*end >= L'0' && *end <= L'9') || *end == L'.' || (*end >= L'a' && *end <= L'z') ||
           (*end >= L'A' && *end <= L'Z'))
        end++;
    SIZE_T length = (SIZE_T)(end - name);
    WCHAR *progid = HeapAlloc(GetProcessHeap(), 0, length * sizeof(WCHAR));
    if (!progid)
        return E_OUTOFMEMORY;
    memcpy(progid, name + 1, (length - 1) * sizeof(WCHAR));
    progid[length - 1] = 0;
    CLSID clsid;
    HRESULT hr = CLSIDFromProgID(progid, &clsid);
    HeapFree(GetProcessHeap(), 0, progid);
    if (SUCCEEDED(hr))
        hr = parse_with_class(&clsid, context, name, eaten, moniker, CLSCTX_INPROC_SERVER | CLSCTX_NO_CODE_DOWNLOAD);
    return hr;
}

/* a name registered in the running object table is taken as it stands when it begins the text. */
static BOOL find_running(IBindCtx *context, const WCHAR *name, SIZE_T length, ULONG *eaten, IMoniker **moniker)
{
    IRunningObjectTable *table;
    IEnumMoniker *list;
    if (FAILED(GetRunningObjectTable(0, &table)))
        return FALSE;
    BOOL found = FALSE;
    if (SUCCEEDED(table->lpVtbl->EnumRunning(table, &list))) {
        IMoniker *item;
        SIZE_T best = 0;
        while (list->lpVtbl->Next(list, 1, &item, nullptr) == S_OK) {
            LPOLESTR text;
            if (SUCCEEDED(item->lpVtbl->GetDisplayName(item, context, nullptr, &text))) {
                SIZE_T n = wcslen(text);
                if (n > best && n <= length && !_wcsnicmp(text, name, n)) {
                    if (*moniker)
                        (*moniker)->lpVtbl->Release(*moniker);
                    *moniker = item;
                    item->lpVtbl->AddRef(item);
                    best = n;
                    found = TRUE;
                }
                CoTaskMemFree(text);
            }
            item->lpVtbl->Release(item);
        }
        list->lpVtbl->Release(list);
    }
    table->lpVtbl->Release(table);
    if (found) {
        LPOLESTR text;
        if (SUCCEEDED((*moniker)->lpVtbl->GetDisplayName(*moniker, context, nullptr, &text))) {
            *eaten = (ULONG)wcslen(text);
            CoTaskMemFree(text);
        }
    }
    return found;
}

/* the longest start of the name, cut at an item delimiter or the end, that is a file on disk. */
static HRESULT find_file_moniker(IBindCtx *context, const WCHAR *name, ULONG *eaten, IMoniker **moniker)
{
    SIZE_T length = wcslen(name);
    if (find_running(context, name, length, eaten, moniker))
        return S_OK;
    BOOL absolute = (length >= 2 && name[1] == L':') || (length >= 2 && name[0] == L'\\' && name[1] == L'\\');
    if (!absolute)
        return MK_E_SYNTAX_;
    WCHAR *path = HeapAlloc(GetProcessHeap(), 0, (length + 1) * sizeof(WCHAR));
    if (!path)
        return E_OUTOFMEMORY;
    HRESULT hr = MK_E_CANTOPENFILE_;
    for (SIZE_T end = length;; end--) {
        if (end == length || name[end] == L'!') {
            memcpy(path, name, end * sizeof(WCHAR));
            path[end] = 0;
            if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
                hr = CreateFileMoniker(path, moniker);
                if (SUCCEEDED(hr))
                    *eaten = (ULONG)end;
                break;
            }
        }
        if (!end)
            break;
    }
    HeapFree(GetProcessHeap(), 0, path);
    return hr;
}

WINOLEAPI HRESULT WINAPI MkParseDisplayName(IBindCtx *context, LPCOLESTR name, ULONG *eaten, IMoniker **moniker)
{
    if (!name || !*name || !context || !eaten || !moniker)
        return E_INVALIDARG;
    *moniker = nullptr;
    *eaten = 0;
    IMoniker *first = nullptr;
    ULONG used = 0;
    HRESULT hr = find_class_moniker(context, (LPOLESTR)name, &used, &first);
    if (hr == MK_E_UNAVAILABLE_)
        hr = find_file_moniker(context, name, &used, &first);
    if (FAILED(hr) && *name == L'@')
        hr = find_progid_moniker(context, (LPOLESTR)name, &used, &first);
    if (FAILED(hr)) {
        *moniker = first;
        return hr;
    }
    /* what is left of the name is parsed by the moniker found so far, and joined to it. */
    const WCHAR *rest = name + used;
    SIZE_T remaining = wcslen(rest);
    while (remaining) {
        IMoniker *next = nullptr, *joined;
        ULONG taken = 0;
        hr = first->lpVtbl->ParseDisplayName(first, context, nullptr, (LPOLESTR)rest, &taken, &next);
        if (FAILED(hr) || !next)
            break;
        hr = first->lpVtbl->ComposeWith(first, next, FALSE, &joined);
        if (SUCCEEDED(hr)) {
            first->lpVtbl->Release(first);
            first = joined;
            rest += taken;
            remaining -= taken;
        }
        next->lpVtbl->Release(next);
        if (FAILED(hr) || !taken)
            break;
    }
    *moniker = first;
    *eaten = (ULONG)(rest - name);
    return hr;
}
