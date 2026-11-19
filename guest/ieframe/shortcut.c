/*
 * CLSID_InternetShortcut, the object the shell makes for a web address: ShellExecute of a url sets the address and
 * invokes the open verb on it. the address goes to the program its scheme names under HKCR, which for http and https
 * is explorer.exe here, and explorer gives it to the browser of the host.
 */
#include "ieframe_private.h"

static const IID IID_UrlW = {0xCABB0DA0, 0xDA57, 0x11CF, {0x99, 0x74, 0x00, 0x20, 0xAF, 0xD7, 0x97, 0x62}};
static const IID IID_UrlA = {0xFBF23B80, 0xE3F0, 0x101B, {0x84, 0x88, 0x00, 0xAA, 0x00, 0x3E, 0x56, 0xF8}};

typedef struct {
    DWORD dwcbSize;
    DWORD dwFlags;
    HWND hwndParent;
    LPCWSTR pcszVerb;
} URL_INVOKE_W;

typedef struct {
    DWORD dwcbSize;
    DWORD dwFlags;
    HWND hwndParent;
    LPCSTR pcszVerb;
} URL_INVOKE_A;

typedef struct UrlW UrlW;
typedef struct UrlA UrlA;

typedef struct {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(UrlW *self, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(UrlW *self);
    ULONG(STDMETHODCALLTYPE *Release)(UrlW *self);
    HRESULT(STDMETHODCALLTYPE *SetURL)(UrlW *self, LPCWSTR url, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *GetURL)(UrlW *self, LPWSTR *url);
    HRESULT(STDMETHODCALLTYPE *InvokeCommand)(UrlW *self, URL_INVOKE_W *info);
} UrlWVtbl;

typedef struct {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(UrlA *self, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(UrlA *self);
    ULONG(STDMETHODCALLTYPE *Release)(UrlA *self);
    HRESULT(STDMETHODCALLTYPE *SetURL)(UrlA *self, LPCSTR url, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *GetURL)(UrlA *self, LPSTR *url);
    HRESULT(STDMETHODCALLTYPE *InvokeCommand)(UrlA *self, URL_INVOKE_A *info);
} UrlAVtbl;

struct UrlW {
    const UrlWVtbl *lpVtbl;
};

struct UrlA {
    const UrlAVtbl *lpVtbl;
};

typedef struct {
    UrlW wide;
    UrlA ansi;
    IShellLinkW link;
    IDataObject data;
    /* what the shell leaves on the object for the verb it invokes next. */
    struct {
        CLIPFORMAT format;
        HGLOBAL global;
    } kept[8];
    LONG refs;
    WCHAR *url;
} Shortcut;

static Shortcut *of_wide(UrlW *self) { return CONTAINING_RECORD(self, Shortcut, wide); }
static Shortcut *of_ansi(UrlA *self) { return CONTAINING_RECORD(self, Shortcut, ansi); }

static HRESULT STDMETHODCALLTYPE w_query(UrlW *self, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    Shortcut *s = of_wide(self);
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_UrlW))
        *object = &s->wide;
    else if (IsEqualIID(riid, &IID_UrlA))
        *object = &s->ansi;
    else if (IsEqualIID(riid, &IID_IShellLinkW))
        *object = &s->link;
    else if (IsEqualIID(riid, &IID_IDataObject))
        *object = &s->data;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&s->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE w_add_ref(UrlW *self)
{
    return InterlockedIncrement(&of_wide(self)->refs);
}

static ULONG STDMETHODCALLTYPE w_release(UrlW *self)
{
    Shortcut *s = of_wide(self);
    ULONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        for (int i = 0; i < 8; i++)
            if (s->kept[i].global)
                GlobalFree(s->kept[i].global);
        HeapFree(GetProcessHeap(), 0, s->url);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE w_set_url(UrlW *self, LPCWSTR url, DWORD flags)
{
    Shortcut *s = of_wide(self);
    WCHAR *copy = nullptr;
    if (url && url[0]) {
        SIZE_T bytes = (lstrlenW(url) + 1) * sizeof(WCHAR);
        copy = HeapAlloc(GetProcessHeap(), 0, bytes);
        if (!copy)
            return E_OUTOFMEMORY;
        memcpy(copy, url, bytes);
    }
    HeapFree(GetProcessHeap(), 0, s->url);
    s->url = copy;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE w_get_url(UrlW *self, LPWSTR *url)
{
    Shortcut *s = of_wide(self);
    if (!url)
        return E_INVALIDARG;
    *url = nullptr;
    if (!s->url)
        return S_FALSE;
    SIZE_T bytes = (lstrlenW(s->url) + 1) * sizeof(WCHAR);
    *url = CoTaskMemAlloc(bytes);
    if (!*url)
        return E_OUTOFMEMORY;
    memcpy(*url, s->url, bytes);
    return S_OK;
}

/* the command of the scheme's open verb with the address in place of %1, %l or %L, or after it when it names none. */
static HRESULT open_url(const WCHAR *url)
{
    WCHAR scheme[64];
    UINT n = 0;
    for (; url[n] && url[n] != L':' && n < RTL_NUMBER_OF(scheme) - 1; n++)
        scheme[n] = url[n];
    if (url[n] != L':' || !n)
        return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
    scheme[n] = 0;
    WCHAR key[128], command[1024], expanded[1024];
    _snwprintf(key, RTL_NUMBER_OF(key), L"%ls\\shell\\open\\command", scheme);
    key[RTL_NUMBER_OF(key) - 1] = 0;
    DWORD size = sizeof(command) - sizeof(WCHAR), type = 0;
    HKEY handle;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &handle) != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
    LONG error = RegQueryValueExW(handle, nullptr, nullptr, &type, (BYTE *)command, &size);
    RegCloseKey(handle);
    if (error != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || !size)
        return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
    command[size / sizeof(WCHAR)] = 0;
    if (type == REG_EXPAND_SZ) {
        if (!ExpandEnvironmentStringsW(command, expanded, RTL_NUMBER_OF(expanded)))
            return HRESULT_FROM_WIN32(GetLastError());
    } else {
        lstrcpynW(expanded, command, RTL_NUMBER_OF(expanded));
    }
    SIZE_T room = lstrlenW(expanded) + lstrlenW(url) + 8;
    WCHAR *line = HeapAlloc(GetProcessHeap(), 0, room * sizeof(WCHAR));
    if (!line)
        return E_OUTOFMEMORY;
    SIZE_T at = 0;
    BOOL placed = FALSE;
    for (const WCHAR *p = expanded; *p; p++) {
        if (p[0] == L'%' && (p[1] == L'1' || p[1] == L'l' || p[1] == L'L') && !placed) {
            for (const WCHAR *u = url; *u; u++)
                line[at++] = *u;
            placed = TRUE;
            p++;
        } else {
            line[at++] = *p;
        }
    }
    if (!placed) {
        line[at++] = L' ';
        for (const WCHAR *u = url; *u; u++)
            line[at++] = *u;
    }
    line[at] = 0;
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    HRESULT hr = S_OK;
    if (CreateProcessW(nullptr, line, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else {
        hr = HRESULT_FROM_WIN32(GetLastError());
    }
    HeapFree(GetProcessHeap(), 0, line);
    return hr;
}

static HRESULT STDMETHODCALLTYPE w_invoke(UrlW *self, URL_INVOKE_W *info)
{
    Shortcut *s = of_wide(self);
    if (!info || info->dwcbSize < sizeof(*info))
        return E_INVALIDARG;
    if (!s->url)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    /* IURL_INVOKECOMMAND_FL_USE_DEFAULT_VERB is 2: the open verb is the only one a scheme has here. */
    if (!(info->dwFlags & 2) && info->pcszVerb && lstrcmpiW(info->pcszVerb, L"open"))
        return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
    return open_url(s->url);
}

static const UrlWVtbl g_wide_vtbl = {w_query, w_add_ref, w_release, w_set_url, w_get_url, w_invoke};

static HRESULT STDMETHODCALLTYPE a_query(UrlA *self, REFIID riid, void **object)
{
    return w_query(&of_ansi(self)->wide, riid, object);
}

static ULONG STDMETHODCALLTYPE a_add_ref(UrlA *self)
{
    return w_add_ref(&of_ansi(self)->wide);
}

static ULONG STDMETHODCALLTYPE a_release(UrlA *self)
{
    return w_release(&of_ansi(self)->wide);
}

static HRESULT STDMETHODCALLTYPE a_set_url(UrlA *self, LPCSTR url, DWORD flags)
{
    WCHAR wide[2084];
    if (url && !MultiByteToWideChar(CP_ACP, 0, url, -1, wide, RTL_NUMBER_OF(wide)))
        return E_INVALIDARG;
    return w_set_url(&of_ansi(self)->wide, url ? wide : nullptr, flags);
}

static HRESULT STDMETHODCALLTYPE a_get_url(UrlA *self, LPSTR *url)
{
    Shortcut *s = of_ansi(self);
    if (!url)
        return E_INVALIDARG;
    *url = nullptr;
    if (!s->url)
        return S_FALSE;
    int bytes = WideCharToMultiByte(CP_ACP, 0, s->url, -1, nullptr, 0, nullptr, nullptr);
    *url = CoTaskMemAlloc((SIZE_T)bytes);
    if (!*url)
        return E_OUTOFMEMORY;
    WideCharToMultiByte(CP_ACP, 0, s->url, -1, *url, bytes, nullptr, nullptr);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE a_invoke(UrlA *self, URL_INVOKE_A *info)
{
    Shortcut *s = of_ansi(self);
    if (!info || info->dwcbSize < sizeof(*info))
        return E_INVALIDARG;
    if (!s->url)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if (!(info->dwFlags & 2) && info->pcszVerb && lstrcmpiA(info->pcszVerb, "open"))
        return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
    return open_url(s->url);
}

static const UrlAVtbl g_ansi_vtbl = {a_query, a_add_ref, a_release, a_set_url, a_get_url, a_invoke};

/* ------------------------------------------------------------------ IShellLinkW */

static Shortcut *of_link(IShellLinkW *self) { return CONTAINING_RECORD(self, Shortcut, link); }


static HRESULT STDMETHODCALLTYPE l_query(IShellLinkW *self, REFIID riid, void **object)
{
    return w_query(&of_link(self)->wide, riid, object);
}

static ULONG STDMETHODCALLTYPE l_add_ref(IShellLinkW *self)
{
    return w_add_ref(&of_link(self)->wide);
}

static ULONG STDMETHODCALLTYPE l_release(IShellLinkW *self)
{
    return w_release(&of_link(self)->wide);
}

static HRESULT STDMETHODCALLTYPE l_get_path(IShellLinkW *self, LPWSTR file, int chars, WIN32_FIND_DATAW *data, DWORD flags)
{
    Shortcut *s = of_link(self);
    if (!file || chars <= 0)
        return E_INVALIDARG;
    if (data)
        memset(data, 0, sizeof(*data));
    lstrcpynW(file, s->url ? s->url : L"", chars);
    return s->url ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE l_get_idlist(IShellLinkW *self, PIDLIST_ABSOLUTE *pidl)
{
    if (pidl)
        *pidl = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE l_set_idlist(IShellLinkW *self, PCIDLIST_ABSOLUTE pidl) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE l_get_text(IShellLinkW *self, LPWSTR text, int chars)
{
    if (text && chars > 0)
        text[0] = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE l_set_text(IShellLinkW *self, LPCWSTR text) { return S_OK; }
static HRESULT STDMETHODCALLTYPE l_get_hotkey(IShellLinkW *self, WORD *hotkey) { if (hotkey) *hotkey = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE l_set_hotkey(IShellLinkW *self, WORD hotkey) { return S_OK; }
static HRESULT STDMETHODCALLTYPE l_get_show(IShellLinkW *self, int *show) { if (show) *show = SW_SHOWNORMAL; return S_OK; }
static HRESULT STDMETHODCALLTYPE l_set_show(IShellLinkW *self, int show) { return S_OK; }
static HRESULT STDMETHODCALLTYPE l_get_icon(IShellLinkW *self, LPWSTR path, int chars, int *index)
{
    if (path && chars > 0)
        path[0] = 0;
    if (index)
        *index = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE l_set_icon(IShellLinkW *self, LPCWSTR path, int index) { return S_OK; }
static HRESULT STDMETHODCALLTYPE l_set_relative(IShellLinkW *self, LPCWSTR path, DWORD reserved) { return S_OK; }
static HRESULT STDMETHODCALLTYPE l_resolve(IShellLinkW *self, HWND hwnd, DWORD flags) { return S_OK; }
static HRESULT STDMETHODCALLTYPE l_set_path(IShellLinkW *self, LPCWSTR file)
{
    return w_set_url(&of_link(self)->wide, file, 0);
}

static const IShellLinkWVtbl g_link_vtbl = {
    l_query,    l_add_ref,  l_release,  l_get_path, l_get_idlist, l_set_idlist, l_get_text, l_set_text,
    l_get_text, l_set_text, l_get_text, l_set_text, l_get_hotkey, l_set_hotkey, l_get_show, l_set_show,
    l_get_icon, l_set_icon, l_set_relative, l_resolve, l_set_path,
};

/* ------------------------------------------------------------------ IDataObject */

static Shortcut *of_data(IDataObject *self) { return CONTAINING_RECORD(self, Shortcut, data); }

static HRESULT STDMETHODCALLTYPE d_query(IDataObject *self, REFIID riid, void **object)
{
    return w_query(&of_data(self)->wide, riid, object);
}

static ULONG STDMETHODCALLTYPE d_add_ref(IDataObject *self)
{
    return w_add_ref(&of_data(self)->wide);
}

static ULONG STDMETHODCALLTYPE d_release(IDataObject *self)
{
    return w_release(&of_data(self)->wide);
}

/* 1 for the address as wide text, 2 as ansi text, 0 for a format the object does not have. */
static int url_format(const FORMATETC *format)
{
    if (!format || !(format->tymed & TYMED_HGLOBAL))
        return 0;
    static UINT url_w, url_a;
    if (!url_w) {
        url_w = RegisterClipboardFormatW(L"UniformResourceLocatorW");
        url_a = RegisterClipboardFormatW(L"UniformResourceLocator");
    }
    if (format->cfFormat == url_w || format->cfFormat == CF_UNICODETEXT)
        return 1;
    if (format->cfFormat == url_a || format->cfFormat == CF_TEXT)
        return 2;
    return 0;
}

static HRESULT STDMETHODCALLTYPE d_get_data(IDataObject *self, FORMATETC *format, STGMEDIUM *medium)
{
    Shortcut *s = of_data(self);
    if (!medium)
        return E_INVALIDARG;
    memset(medium, 0, sizeof(*medium));
    int kind = url_format(format);
    for (int i = 0; format && (format->tymed & TYMED_HGLOBAL) && i < 8; i++) {
        if (!s->kept[i].global || s->kept[i].format != format->cfFormat)
            continue;
        SIZE_T size = GlobalSize(s->kept[i].global);
        HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size ? size : 1);
        void *to = copy ? GlobalLock(copy) : nullptr, *from = GlobalLock(s->kept[i].global);
        if (!to || !from) {
            if (copy)
                GlobalFree(copy);
            return E_OUTOFMEMORY;
        }
        memcpy(to, from, size);
        GlobalUnlock(copy);
        GlobalUnlock(s->kept[i].global);
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = copy;
        return S_OK;
    }
    if (!kind || !s->url)
        return DV_E_FORMATETC;
    SIZE_T bytes = kind == 1 ? (lstrlenW(s->url) + 1) * sizeof(WCHAR)
                             : (SIZE_T)WideCharToMultiByte(CP_ACP, 0, s->url, -1, nullptr, 0, nullptr, nullptr);
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void *at = global ? GlobalLock(global) : nullptr;
    if (!at) {
        if (global)
            GlobalFree(global);
        return E_OUTOFMEMORY;
    }
    if (kind == 1)
        memcpy(at, s->url, bytes);
    else
        WideCharToMultiByte(CP_ACP, 0, s->url, -1, at, (int)bytes, nullptr, nullptr);
    GlobalUnlock(global);
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = global;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_data_here(IDataObject *self, FORMATETC *format, STGMEDIUM *medium) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_query_get(IDataObject *self, FORMATETC *format)
{
    Shortcut *s = of_data(self);
    for (int i = 0; format && i < 8; i++)
        if (s->kept[i].global && s->kept[i].format == format->cfFormat)
            return S_OK;
    return url_format(format) && s->url ? S_OK : DV_E_FORMATETC;
}
static HRESULT STDMETHODCALLTYPE d_canonical(IDataObject *self, FORMATETC *in, FORMATETC *out)
{
    if (out)
        out->ptd = nullptr;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE d_set_data(IDataObject *self, FORMATETC *format, STGMEDIUM *medium, BOOL release)
{
    Shortcut *s = of_data(self);
    if (!format || !medium || medium->tymed != TYMED_HGLOBAL || !medium->hGlobal)
        return DV_E_TYMED;
    HGLOBAL global = medium->hGlobal;
    if (!release) {
        SIZE_T bytes = GlobalSize(medium->hGlobal);
        global = GlobalAlloc(GMEM_MOVEABLE, bytes ? bytes : 1);
        void *to = global ? GlobalLock(global) : nullptr, *from = GlobalLock(medium->hGlobal);
        if (!to || !from) {
            if (global)
                GlobalFree(global);
            return E_OUTOFMEMORY;
        }
        memcpy(to, from, bytes);
        GlobalUnlock(global);
        GlobalUnlock(medium->hGlobal);
    }
    int slot = -1;
    for (int i = 0; i < 8; i++) {
        if (s->kept[i].global && s->kept[i].format == format->cfFormat) {
            GlobalFree(s->kept[i].global);
            slot = i;
            break;
        }
        if (!s->kept[i].global && slot < 0)
            slot = i;
    }
    if (slot < 0) {
        if (!release)
            GlobalFree(global);
        return E_OUTOFMEMORY;
    }
    s->kept[slot].format = format->cfFormat;
    s->kept[slot].global = global;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE d_enum(IDataObject *self, DWORD direction, IEnumFORMATETC **enumerator)
{
    if (enumerator)
        *enumerator = nullptr;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE d_advise(IDataObject *self, FORMATETC *format, DWORD flags, IAdviseSink *sink, DWORD *connection) { return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT STDMETHODCALLTYPE d_unadvise(IDataObject *self, DWORD connection) { return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT STDMETHODCALLTYPE d_enum_advise(IDataObject *self, IEnumSTATDATA **enumerator) { return OLE_E_ADVISENOTSUPPORTED; }

static const IDataObjectVtbl g_data_vtbl = {d_query, d_add_ref, d_release, d_get_data, d_get_data_here, d_query_get,
                                            d_canonical, d_set_data, d_enum, d_advise, d_unadvise, d_enum_advise};

/* ------------------------------------------------------------------ the class */

static HRESULT STDMETHODCALLTYPE f_query(IClassFactory *self, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *object = self;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE f_add_ref(IClassFactory *self)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE f_release(IClassFactory *self)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE f_create(IClassFactory *self, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    Shortcut *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->wide.lpVtbl = &g_wide_vtbl;
    s->ansi.lpVtbl = &g_ansi_vtbl;
    s->link.lpVtbl = &g_link_vtbl;
    s->data.lpVtbl = &g_data_vtbl;
    s->refs = 1;
    HRESULT hr = w_query(&s->wide, riid, object);
    w_release(&s->wide);
    return hr;
}

static HRESULT STDMETHODCALLTYPE f_lock(IClassFactory *self, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {f_query, f_add_ref, f_release, f_create, f_lock};
static IClassFactory g_factory = {&g_factory_vtbl};

IClassFactory *ShortcutFactory(void)
{
    return &g_factory;
}
