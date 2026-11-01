/*
 * WshShortcut and WshURLShortcut: the shortcut of a program or file as a .lnk file, and of an address as a .url file.
 */
#include "wshom_private.h"

static const WSH_CLASS g_shortcut_class = {&IID_IWshShortcut, &CLSID_WshShortcut, {&IID_IWshShortcut, nullptr}};
static const WSH_CLASS g_url_class = {&IID_IWshURLShortcut, &CLSID_WshURLShortcut, {&IID_IWshURLShortcut, nullptr}};

typedef struct WSH_SHORTCUT {
    WSH_OBJECT Object;
    IShellLinkW *Link;
    IPersistFile *File;
    BSTR Path;
} WSH_SHORTCUT;

WSH_COMMON_METHODS(shortcut, IWshShortcut)

static void shortcut_destroy(WSH_OBJECT *object)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)object;
    if (shortcut->File)
        shortcut->File->lpVtbl->Release(shortcut->File);
    if (shortcut->Link)
        shortcut->Link->lpVtbl->Release(shortcut->Link);
    SysFreeString(shortcut->Path);
}

static HRESULT full_path(const WCHAR *path, BSTR *out)
{
    WCHAR full[MAX_PATH];
    WCHAR *file_part;
    DWORD length = GetFullPathNameW(path, MAX_PATH, full, &file_part);
    if (!length)
        return HrFromLastError();
    if (length >= MAX_PATH)
        return E_INVALIDARG;
    *out = SysAllocString(full);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT shortcut_load(WSH_SHORTCUT *shortcut, const WCHAR *path)
{
    HRESULT hr = full_path(path, &shortcut->Path);
    if (FAILED(hr))
        return hr;
    hr = CoCreateInstance(&CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&shortcut->Link);
    if (FAILED(hr))
        return hr;
    hr = shortcut->Link->lpVtbl->QueryInterface(shortcut->Link, &IID_IPersistFile, (void **)&shortcut->File);
    if (FAILED(hr)) {
        shortcut->Link->lpVtbl->Release(shortcut->Link);
        shortcut->Link = nullptr;
        return hr;
    }
    /* a shortcut that is not there yet is made empty. */
    shortcut->File->lpVtbl->Load(shortcut->File, shortcut->Path, 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE shortcut_Load(IWshShortcut *This, BSTR path)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    if (!path)
        return E_POINTER;
    if (shortcut->Path)
        return E_UNEXPECTED;
    return shortcut_load(shortcut, path);
}

static HRESULT STDMETHODCALLTYPE shortcut_Save(IWshShortcut *This)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    if (!shortcut->File)
        return E_FAIL;
    if (!shortcut->Path)
        return E_UNEXPECTED;
    HRESULT hr = shortcut->File->lpVtbl->Save(shortcut->File, shortcut->Path, TRUE);
    if (FAILED(hr))
        WshSignal(&IID_IWshShortcut, L"WshShortcut.Save", MSG_SHORTCUT_SAVE, shortcut->Path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE shortcut_get_FullName(IWshShortcut *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    if (!shortcut->Link)
        return E_FAIL;
    *out = SysAllocString(shortcut->Path);
    return *out ? S_OK : E_OUTOFMEMORY;
}

#define LINK_TEXT(name, getter)                                                                                        \
    static HRESULT STDMETHODCALLTYPE shortcut_get_##name(IWshShortcut *This, BSTR *out)                                 \
    {                                                                                                                  \
        if (!out)                                                                                                      \
            return E_POINTER;                                                                                          \
        WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;                                                                 \
        if (!shortcut->Link)                                                                                           \
            return E_FAIL;                                                                                             \
        WCHAR buffer[1024];                                                                                            \
        HRESULT hr = getter;                                                                                           \
        if (FAILED(hr))                                                                                                \
            return hr;                                                                                                 \
        *out = SysAllocString(buffer);                                                                                 \
        return *out ? S_OK : E_OUTOFMEMORY;                                                                            \
    }

LINK_TEXT(Arguments, shortcut->Link->lpVtbl->GetArguments(shortcut->Link, buffer, 1024))
LINK_TEXT(Description, shortcut->Link->lpVtbl->GetDescription(shortcut->Link, buffer, 1024))
LINK_TEXT(WorkingDirectory, shortcut->Link->lpVtbl->GetWorkingDirectory(shortcut->Link, buffer, 1024))
LINK_TEXT(TargetPath, shortcut->Link->lpVtbl->GetPath(shortcut->Link, buffer, 1024, &(WIN32_FIND_DATAW){0}, 2))

static HRESULT STDMETHODCALLTYPE shortcut_get_IconLocation(IWshShortcut *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    if (!shortcut->Link)
        return E_FAIL;
    WCHAR buffer[1100];
    int index = 0;
    HRESULT hr = shortcut->Link->lpVtbl->GetIconLocation(shortcut->Link, buffer, 1024, &index);
    if (FAILED(hr))
        return hr;
    _snwprintf(buffer + wcslen(buffer), 1100 - wcslen(buffer), L",%d", index);
    *out = SysAllocString(buffer);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE shortcut_put_Arguments(IWshShortcut *This, BSTR text)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    return shortcut->Link ? shortcut->Link->lpVtbl->SetArguments(shortcut->Link, text ? text : L"") : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE shortcut_put_Description(IWshShortcut *This, BSTR text)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    return shortcut->Link ? shortcut->Link->lpVtbl->SetDescription(shortcut->Link, text ? text : L"") : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE shortcut_put_WorkingDirectory(IWshShortcut *This, BSTR text)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    return shortcut->Link ? shortcut->Link->lpVtbl->SetWorkingDirectory(shortcut->Link, text ? text : L"") : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE shortcut_put_TargetPath(IWshShortcut *This, BSTR text)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    return shortcut->Link ? shortcut->Link->lpVtbl->SetPath(shortcut->Link, text ? text : L"") : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE shortcut_put_RelativePath(IWshShortcut *This, BSTR text)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    return shortcut->Link ? shortcut->Link->lpVtbl->SetRelativePath(shortcut->Link, text ? text : L"", 0) : E_FAIL;
}

static int parse_int(const WCHAR *text)
{
    int sign = 1, value = 0;
    while (*text == ' ')
        text++;
    if (*text == '-' || *text == '+')
        sign = *text++ == '-' ? -1 : 1;
    while (*text >= '0' && *text <= '9')
        value = value * 10 + (*text++ - '0');
    return sign * value;
}

static HRESULT STDMETHODCALLTYPE shortcut_put_IconLocation(IWshShortcut *This, BSTR text)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    if (!shortcut->Link)
        return E_FAIL;
    if (!SysStringLen(text))
        return E_INVALIDARG;
    WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, (wcslen(text) + 1) * sizeof(WCHAR));
    if (!copy)
        return E_OUTOFMEMORY;
    wcscpy(copy, text);
    int index = 0;
    WCHAR *comma = wcsrchr(copy, ',');
    if (comma) {
        *comma = 0;
        index = parse_int(comma + 1);
    }
    HRESULT hr = shortcut->Link->lpVtbl->SetIconLocation(shortcut->Link, copy, index);
    HeapFree(GetProcessHeap(), 0, copy);
    return hr;
}

static HRESULT STDMETHODCALLTYPE shortcut_get_WindowStyle(IWshShortcut *This, int *style)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    if (!style)
        return E_POINTER;
    return shortcut->Link ? shortcut->Link->lpVtbl->GetShowCmd(shortcut->Link, style) : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE shortcut_put_WindowStyle(IWshShortcut *This, int style)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    return shortcut->Link ? shortcut->Link->lpVtbl->SetShowCmd(shortcut->Link, style) : E_FAIL;
}

/* ------------------------------------------------------------------ the hotkey */

#define HOTKEY_STRING_ALT 9000
#define HOTKEY_STRING_CTRL 9001
#define HOTKEY_STRING_EXT 9002
#define HOTKEY_STRING_SHIFT 9003
#define HOTKEY_STRING_KEY 10000

static BOOL load_text(UINT id, WCHAR *buffer, int count)
{
    buffer[0] = 0;
    return LoadStringW(g_module, id, buffer, count) > 0;
}

static HRESULT STDMETHODCALLTYPE shortcut_get_Hotkey(IWshShortcut *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    if (!shortcut->Link)
        return E_FAIL;
    WORD hotkey = 0;
    HRESULT hr = shortcut->Link->lpVtbl->GetHotkey(shortcut->Link, &hotkey);
    if (FAILED(hr))
        return hr;
    WCHAR text[0x400], part[256];
    text[0] = 0;
    BYTE modifiers = (BYTE)(hotkey >> 8);
    static const struct {
        BYTE Bit;
        UINT Id;
    } order[] = {{HOTKEYF_ALT, HOTKEY_STRING_ALT}, {HOTKEYF_CONTROL, HOTKEY_STRING_CTRL},
                 {HOTKEYF_EXT, HOTKEY_STRING_EXT}, {HOTKEYF_SHIFT, HOTKEY_STRING_SHIFT}};
    for (int i = 0; i < 4; i++) {
        if ((modifiers & order[i].Bit) && load_text(order[i].Id, part, 256))
            wcscat(text, part);
    }
    load_text(HOTKEY_STRING_KEY + (hotkey & 0xFF), part, 256);
    wcscat(text, part);
    *out = SysAllocString(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE shortcut_put_Hotkey(IWshShortcut *This, BSTR text)
{
    WSH_SHORTCUT *shortcut = (WSH_SHORTCUT *)This;
    if (!shortcut->Link)
        return E_FAIL;
    if (!text)
        return E_OUTOFMEMORY;
    static const struct {
        UINT Id;
        WORD Bit;
    } names[] = {{HOTKEY_STRING_ALT, HOTKEYF_ALT << 8}, {HOTKEY_STRING_CTRL, HOTKEYF_CONTROL << 8},
                 {HOTKEY_STRING_SHIFT, HOTKEYF_SHIFT << 8}, {HOTKEY_STRING_EXT, HOTKEYF_EXT << 8}};
    const WCHAR *p = text;
    WORD flags = 0;
    WCHAR part[256];
    for (;;) {
        BOOL found = FALSE;
        for (int i = 0; i < 4 && !found; i++) {
            if (!load_text(names[i].Id, part, 256))
                continue;
            SIZE_T length = wcslen(part);
            if (length && !_wcsnicmp(p, part, length)) {
                flags |= names[i].Bit;
                p += length;
                found = TRUE;
            }
        }
        if (!found)
            break;
    }
    if (!*p)
        return shortcut->Link->lpVtbl->SetHotkey(shortcut->Link, 0);
    for (UINT key = 1; key < 256; key++) {
        part[0] = 0;
        LoadStringW(g_module, HOTKEY_STRING_KEY + key, part, 256);
        if (part[0] && !_wcsicmp(p, part))
            return shortcut->Link->lpVtbl->SetHotkey(shortcut->Link, (WORD)(flags | key));
    }
    return E_INVALIDARG;
}

static const IWshShortcutVtbl g_shortcut_vtbl = {
    shortcut_QueryInterface, shortcut_AddRef, shortcut_Release, shortcut_GetTypeInfoCount, shortcut_GetTypeInfo,
    shortcut_GetIDsOfNames, shortcut_Invoke, shortcut_get_FullName, shortcut_get_Arguments, shortcut_put_Arguments,
    shortcut_get_Description, shortcut_put_Description, shortcut_get_Hotkey, shortcut_put_Hotkey,
    shortcut_get_IconLocation, shortcut_put_IconLocation, shortcut_put_RelativePath, shortcut_get_TargetPath,
    shortcut_put_TargetPath, shortcut_get_WindowStyle, shortcut_put_WindowStyle, shortcut_get_WorkingDirectory,
    shortcut_put_WorkingDirectory, shortcut_Load, shortcut_Save,
};

HRESULT WshShortcutCreate(BSTR path, IDispatch **out)
{
    WSH_SHORTCUT *shortcut = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*shortcut));
    if (!shortcut)
        return E_OUTOFMEMORY;
    WshObjectInit(&shortcut->Object, &g_shortcut_vtbl, &g_shortcut_class, shortcut_destroy);
    HRESULT hr = shortcut_load(shortcut, path);
    if (FAILED(hr)) {
        WshRelease(shortcut);
        return hr;
    }
    *out = (IDispatch *)shortcut;
    return S_OK;
}

/* ------------------------------------------------------------------ URL shortcuts */

typedef struct WSH_URL {
    WSH_OBJECT Object;
    BSTR Path, Url;
    BOOL Loaded;
} WSH_URL;

WSH_COMMON_METHODS(url, IWshURLShortcut)

static void url_destroy(WSH_OBJECT *object)
{
    WSH_URL *url = (WSH_URL *)object;
    SysFreeString(url->Path);
    SysFreeString(url->Url);
}

/* the address of an internet shortcut file: the URL entry of the section. */
static void url_read(WSH_URL *url)
{
    WCHAR address[2048];
    if (GetPrivateProfileStringW(L"InternetShortcut", L"URL", L"", address, 2048, url->Path) && address[0])
        url->Url = SysAllocString(address);
}

static HRESULT STDMETHODCALLTYPE url_Load(IWshURLShortcut *This, BSTR path)
{
    WSH_URL *url = (WSH_URL *)This;
    if (!path)
        return E_POINTER;
    if (url->Path)
        return E_UNEXPECTED;
    HRESULT hr = full_path(path, &url->Path);
    if (SUCCEEDED(hr)) {
        url->Loaded = TRUE;
        url_read(url);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE url_Save(IWshURLShortcut *This)
{
    WSH_URL *url = (WSH_URL *)This;
    if (!url->Loaded)
        return E_FAIL;
    if (!url->Path)
        return E_UNEXPECTED;
    BOOL ok = WritePrivateProfileStringW(L"InternetShortcut", L"URL", url->Url ? url->Url : L"", url->Path);
    HRESULT hr = ok ? S_OK : HrFromLastError();
    if (FAILED(hr))
        WshSignal(&IID_IWshURLShortcut, L"WshURLShortcut.Save", MSG_SHORTCUT_SAVE, url->Path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE url_get_FullName(IWshURLShortcut *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    WSH_URL *url = (WSH_URL *)This;
    if (!url->Loaded)
        return E_FAIL;
    *out = SysAllocString(url->Path);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE url_get_TargetPath(IWshURLShortcut *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    WSH_URL *url = (WSH_URL *)This;
    if (!url->Loaded)
        return E_FAIL;
    *out = SysAllocString(url->Url ? url->Url : L"");
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* whether the scheme of an address is one the system has a handler for. */
static HRESULT check_address(const WCHAR *address)
{
    const WCHAR *colon = wcschr(address, ':');
    if (!colon || colon == address || !IsCharAlphaW(address[0]))
        return (HRESULT)0x80041001;
    WCHAR scheme[64];
    SIZE_T length = (SIZE_T)(colon - address);
    if (length >= 64)
        return (HRESULT)0x80041001;
    for (SIZE_T i = 0; i < length; i++) {
        WCHAR c = address[i];
        if (!(IsCharAlphaW(c) || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'))
            return (HRESULT)0x80041001;
        scheme[i] = c;
    }
    scheme[length] = 0;
    /* the schemes that Windows registers by default are known even when the registry has no entry for them. */
    static const WCHAR *const known[] = {L"http", L"https", L"ftp", L"file", L"mailto", L"news", L"nntp", L"telnet", L"gopher"};
    for (SIZE_T i = 0; i < sizeof(known) / sizeof(known[0]); i++)
        if (!_wcsicmp(scheme, known[i]))
            return S_OK;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, scheme, 0, KEY_QUERY_VALUE, &key))
        return (HRESULT)0x80041002;
    DWORD type;
    LONG status = RegQueryValueExW(key, L"URL Protocol", nullptr, &type, nullptr, nullptr);
    RegCloseKey(key);
    return status ? (HRESULT)0x80041002 : S_OK;
}

static HRESULT STDMETHODCALLTYPE url_put_TargetPath(IWshURLShortcut *This, BSTR text)
{
    WSH_URL *url = (WSH_URL *)This;
    if (!url->Loaded)
        return E_FAIL;
    HRESULT hr = check_address(text ? text : L"");
    if (FAILED(hr)) {
        if (hr == (HRESULT)0x80041001)
            WshSignal(&IID_IWshURLShortcut, L"WshURLShortcut.TargetPath", MSG_BAD_URL, text);
        else
            WshSignal(&IID_IWshURLShortcut, L"WshURLShortcut.TargetPath", MSG_NO_HANDLER, text);
        return hr;
    }
    BSTR copy = SysAllocString(text);
    if (!copy)
        return E_OUTOFMEMORY;
    SysFreeString(url->Url);
    url->Url = copy;
    return S_OK;
}

static const IWshURLShortcutVtbl g_url_vtbl = {
    url_QueryInterface, url_AddRef, url_Release, url_GetTypeInfoCount, url_GetTypeInfo, url_GetIDsOfNames, url_Invoke,
    url_get_FullName, url_get_TargetPath, url_put_TargetPath, url_Load, url_Save,
};

HRESULT WshUrlShortcutCreate(BSTR path, IDispatch **out)
{
    WSH_URL *url = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*url));
    if (!url)
        return E_OUTOFMEMORY;
    WshObjectInit(&url->Object, &g_url_vtbl, &g_url_class, url_destroy);
    HRESULT hr = url_Load((IWshURLShortcut *)url, path);
    if (FAILED(hr)) {
        WshRelease(url);
        return hr;
    }
    *out = (IDispatch *)url;
    return S_OK;
}
