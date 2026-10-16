/*
 * the visual style of the session and theme handles: aero.msstyles loaded
 * once per process, OpenThemeData class matching and the window properties
 * SetWindowTheme stores.
 */
#include "uxtheme_private.h"

/* the window properties of uxtheme, global atoms of windows. */
#define UX_PROP_SUBIDLIST ((LPCWSTR)(ULONG_PTR)0xA910)
#define UX_PROP_SUBAPPNAME ((LPCWSTR)(ULONG_PTR)0xA911)
#define UX_PROP_THEME ((LPCWSTR)(ULONG_PTR)0xA912)
#define UX_PROP_DIALOGTEXTURE ((LPCWSTR)(ULONG_PTR)0xA913)

static void *ux_lib_alloc(void *ctx, size_t size)
{
    return UxAlloc(size);
}

static void ux_lib_free(void *ctx, void *p)
{
    UxFree(p);
}

const MimeAllocator UxAllocator = {ux_lib_alloc, ux_lib_free, nullptr};
MimeMsstyles UxStyles;

static struct {
    RTL_RUN_ONCE Once;
    BOOL Ready;
    RTL_CRITICAL_SECTION Lock;
    WCHAR Path[MAX_PATH];
    const void *File, *Mui;
} g_style;

/* theme handles are a slot index in the low word and a generation in the high word, like windows. */
typedef struct UX_SLOT {
    UX_THEME *Theme;
    WORD Generation;
    LONG Uses;
    BOOL Closed;
} UX_SLOT;

static struct {
    RTL_CRITICAL_SECTION Lock;
    UX_SLOT *Slots;
    ULONG Count, Capacity;
} g_handles;

static DWORD g_app_properties = STAP_VALIDBITS;

static const void *map_file(LPCWSTR path, SIZE_T *size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD high = 0, low = GetFileSize(file, &high);
    const void *view = nullptr;
    if (low && low != INVALID_FILE_SIZE && !high) {
        HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping) {
            view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
            CloseHandle(mapping);
        }
    }
    CloseHandle(file);
    if (view)
        *size = low;
    return view;
}

static ULONG NTAPI load_style(PRTL_RUN_ONCE once, PVOID param, PVOID *context)
{
    RtlInitializeCriticalSection(&g_handles.Lock);
    RtlInitializeCriticalSection(&g_style.Lock);
    WCHAR mui[MAX_PATH];
    UINT n = GetWindowsDirectoryW(g_style.Path, MAX_PATH - 64);
    if (!n || n >= MAX_PATH - 64)
        return TRUE;
    wcscpy(mui, g_style.Path);
    wcscpy(g_style.Path + n, L"\\Resources\\Themes\\aero\\aero.msstyles");
    wcscpy(mui + n, L"\\Resources\\Themes\\aero\\en-US\\aero.msstyles.mui");
    SIZE_T size = 0, mui_size = 0;
    g_style.File = map_file(g_style.Path, &size);
    if (!g_style.File)
        return TRUE;
    g_style.Mui = map_file(mui, &mui_size);
    g_style.Ready = mime_msstyles_open(&UxStyles, g_style.File, size, g_style.Mui, g_style.Mui ? mui_size : 0,
                                       &UxAllocator);
    return TRUE;
}

BOOL UxThemeLoaded(void)
{
    RtlRunOnceExecuteOnce(&g_style.Once, load_style, nullptr, nullptr);
    return g_style.Ready;
}

static HTHEME new_handle(UX_THEME *theme)
{
    RtlEnterCriticalSection(&g_handles.Lock);
    ULONG i;
    for (i = 0; i < g_handles.Count; i++)
        if (!g_handles.Slots[i].Theme)
            break;
    if (i == g_handles.Count) {
        if (g_handles.Count == g_handles.Capacity) {
            ULONG cap = g_handles.Capacity ? g_handles.Capacity * 2 : 64;
            UX_SLOT *slots = cap <= 0x10000 ? UxAlloc(cap * sizeof(*slots)) : nullptr;
            if (!slots) {
                RtlLeaveCriticalSection(&g_handles.Lock);
                return nullptr;
            }
            if (g_handles.Slots)
                memcpy(slots, g_handles.Slots, g_handles.Count * sizeof(*slots));
            UxFree(g_handles.Slots);
            g_handles.Slots = slots;
            g_handles.Capacity = cap;
        }
        g_handles.Count++;
    }
    UX_SLOT *slot = &g_handles.Slots[i];
    slot->Generation++;
    if (!slot->Generation)
        slot->Generation = 1;
    slot->Theme = theme;
    slot->Uses = 1;
    slot->Closed = FALSE;
    HTHEME handle = (HTHEME)(ULONG_PTR)(((ULONG)slot->Generation << 16) | i);
    RtlLeaveCriticalSection(&g_handles.Lock);
    return handle;
}

static UX_SLOT *find_slot(HTHEME handle)
{
    ULONG_PTR value = (ULONG_PTR)handle;
    ULONG i = (ULONG)(value & 0xFFFF);
    if ((ULONG64)value >> 32 || !g_handles.Slots || i >= g_handles.Count)
        return nullptr;
    UX_SLOT *slot = &g_handles.Slots[i];
    if (!slot->Theme || slot->Closed || slot->Generation != (WORD)(value >> 16))
        return nullptr;
    return slot;
}

UX_THEME *UxAcquire(HTHEME handle, HRESULT *hr)
{
    if (!UxThemeLoaded()) {
        *hr = E_HANDLE;
        return nullptr;
    }
    RtlEnterCriticalSection(&g_handles.Lock);
    UX_SLOT *slot = find_slot(handle);
    UX_THEME *theme = nullptr;
    if (slot) {
        slot->Uses++;
        theme = slot->Theme;
    }
    RtlLeaveCriticalSection(&g_handles.Lock);
    *hr = theme ? S_OK : E_HANDLE;
    return theme;
}

void UxRelease(UX_THEME *theme)
{
    RtlEnterCriticalSection(&g_handles.Lock);
    for (ULONG i = 0; i < g_handles.Count; i++) {
        UX_SLOT *slot = &g_handles.Slots[i];
        if (slot->Theme != theme)
            continue;
        if (--slot->Uses <= 0) {
            UxFree(slot->Theme);
            slot->Theme = nullptr;
        }
        break;
    }
    RtlLeaveCriticalSection(&g_handles.Lock);
}

/* the string an atom window property holds, NULL when unset. "$" stands for an empty string. */
static LPWSTR atom_prop(HWND hwnd, LPCWSTR prop, LPWSTR buf, int size)
{
    if (!hwnd)
        return nullptr;
    ATOM atom = (ATOM)(ULONG_PTR)GetPropW(hwnd, prop);
    if (!atom || !GetAtomNameW(atom, buf, size))
        return nullptr;
    if (buf[0] == '$' && !buf[1])
        buf[0] = 0;
    return buf;
}

static HTHEME open_theme(HWND hwnd, LPCWSTR class_list, DWORD flags, UINT dpi)
{
    if (hwnd && !IsWindow(hwnd)) {
        SetLastError((DWORD)E_HANDLE);
        return nullptr;
    }
    if (!class_list) {
        SetLastError((DWORD)E_POINTER);
        return nullptr;
    }
    SetLastError(0);
    if (hwnd)
        RemovePropW(hwnd, UX_PROP_THEME);
    DWORD allow = (flags & OTD_NONCLIENT) ? STAP_ALLOW_NONCLIENT : STAP_ALLOW_CONTROLS;
    if (!UxThemeLoaded() || !(g_app_properties & allow)) {
        SetLastError((DWORD)UX_E_NOTFOUND);
        return nullptr;
    }
    WCHAR app_buf[260], ids_buf[260];
    LPCWSTR app = atom_prop(hwnd, UX_PROP_SUBAPPNAME, app_buf, 260);
    LPCWSTR ids = atom_prop(hwnd, UX_PROP_SUBIDLIST, ids_buf, 260);
    int cls = -1;
    if (ids && *ids)
        cls = mime_msstyles_match_class(&UxStyles, (const uint16_t *)ids, app && *app ? (const uint16_t *)app : nullptr,
                                        0);
    if (cls < 0)
        cls = mime_msstyles_match_class(&UxStyles, (const uint16_t *)class_list,
                                        app && *app ? (const uint16_t *)app : nullptr, 0);
    if (cls < 0) {
        SetLastError((DWORD)UX_E_NOTFOUND);
        return nullptr;
    }
    UX_THEME *theme = UxAlloc(sizeof(*theme));
    if (!theme) {
        SetLastError((DWORD)E_OUTOFMEMORY);
        return nullptr;
    }
    theme->Class = cls;
    theme->Dpi = dpi ? dpi : GetDpiForSystem();
    if (!theme->Dpi)
        theme->Dpi = 96;
    theme->Window = hwnd;
    HTHEME handle = new_handle(theme);
    if (!handle) {
        UxFree(theme);
        SetLastError((DWORD)E_OUTOFMEMORY);
        return nullptr;
    }
    if (hwnd)
        SetPropW(hwnd, UX_PROP_THEME, handle);
    return handle;
}

THEMEAPI_(HTHEME) OpenThemeData(HWND hwnd, LPCWSTR class_list)
{
    return open_theme(hwnd, class_list, 0, 0);
}

THEMEAPI_(HTHEME) OpenThemeDataEx(HWND hwnd, LPCWSTR class_list, DWORD flags)
{
    return open_theme(hwnd, class_list, flags, 0);
}

THEMEAPI_(HTHEME) OpenThemeDataForDpi(HWND hwnd, LPCWSTR class_list, UINT dpi)
{
    if (dpi > 480) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return open_theme(hwnd, class_list, 0, dpi);
}

THEMEAPI CloseThemeData(HTHEME handle)
{
    if (!UxThemeLoaded())
        return E_HANDLE;
    RtlEnterCriticalSection(&g_handles.Lock);
    UX_SLOT *slot = find_slot(handle);
    HRESULT hr = E_HANDLE;
    if (slot) {
        slot->Closed = TRUE;
        if (--slot->Uses <= 0) {
            UxFree(slot->Theme);
            slot->Theme = nullptr;
        }
        hr = S_OK;
    }
    RtlLeaveCriticalSection(&g_handles.Lock);
    return hr;
}

THEMEAPI_(HTHEME) GetWindowTheme(HWND hwnd)
{
    if (!IsWindow(hwnd)) {
        SetLastError((DWORD)E_HANDLE);
        return nullptr;
    }
    return GetPropW(hwnd, UX_PROP_THEME);
}

/* stores s as an atom property, "$" for an empty string; returns true when it changed. */
static BOOL apply_string_prop(HWND hwnd, LPCWSTR s, LPCWSTR prop)
{
    if (s && !*s)
        s = L"$";
    ATOM old = (ATOM)(ULONG_PTR)GetPropW(hwnd, prop);
    BOOL changed;
    if (!old) {
        changed = s != nullptr;
    } else if (s) {
        WCHAR buf[260];
        changed = !GetAtomNameW(old, buf, 260) || wcscmp(buf, s) != 0;
    } else {
        changed = TRUE;
    }
    if (!changed)
        return FALSE;
    if (old) {
        DeleteAtom(old);
        RemovePropW(hwnd, prop);
    }
    if (s) {
        ATOM atom = AddAtomW(s);
        if (atom)
            SetPropW(hwnd, prop, (HANDLE)(ULONG_PTR)atom);
    }
    return TRUE;
}

THEMEAPI SetWindowTheme(HWND hwnd, LPCWSTR sub_app, LPCWSTR sub_id_list)
{
    if (!IsWindow(hwnd))
        return E_HANDLE;
    apply_string_prop(hwnd, sub_app, UX_PROP_SUBAPPNAME);
    apply_string_prop(hwnd, sub_id_list, UX_PROP_SUBIDLIST);
    /* windows sends the theme change to the window alone: wparam -1, lparam WTC_... 0x80000001. */
    SendMessageW(hwnd, WM_THEMECHANGED, (WPARAM)-1, (LPARAM)0x80000001);
    return S_OK;
}

THEMEAPI SetWindowThemeAttribute(HWND hwnd, enum WINDOWTHEMEATTRIBUTETYPE type, PVOID attribute, DWORD size)
{
    if (!IsWindow(hwnd))
        return E_HANDLE;
    if (type != WTA_NONCLIENT || !attribute || size != sizeof(WTA_OPTIONS))
        return E_INVALIDARG;
    return S_OK;
}

THEMEAPI_(BOOL) IsThemeActive(void)
{
    SetLastError(0);
    return UxThemeLoaded();
}

THEMEAPI_(BOOL) IsAppThemed(void)
{
    if (!(g_app_properties & (STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS)))
        return FALSE;
    return IsThemeActive();
}

/* desktop composition is always on since windows 8. */
THEMEAPI_(BOOL) IsCompositionActive(void)
{
    return UxThemeLoaded();
}

THEMEAPI_(DWORD) GetThemeAppProperties(void)
{
    return g_app_properties;
}

THEMEAPI_(void) SetThemeAppProperties(DWORD flags)
{
    g_app_properties = flags;
}

THEMEAPI EnableTheming(BOOL enable)
{
    return enable ? S_OK : E_NOTIMPL;
}

THEMEAPI EnableThemeDialogTexture(HWND hwnd, DWORD flags)
{
    if (!IsWindow(hwnd))
        return E_HANDLE;
    if (flags & ETDT_DISABLE)
        RemovePropW(hwnd, UX_PROP_DIALOGTEXTURE);
    else if (flags & ETDT_ENABLE)
        SetPropW(hwnd, UX_PROP_DIALOGTEXTURE, (HANDLE)(ULONG_PTR)(flags | 0x80000000u));
    return S_OK;
}

THEMEAPI_(BOOL) IsThemeDialogTextureEnabled(HWND hwnd)
{
    return GetPropW(hwnd, UX_PROP_DIALOGTEXTURE) != nullptr;
}

static HRESULT copy_string(LPWSTR dst, int size, LPCWSTR src)
{
    if (!dst || size <= 0)
        return S_OK;
    int n = (int)wcslen(src);
    if (n >= size) {
        memcpy(dst, src, (SIZE_T)(size - 1) * sizeof(WCHAR));
        dst[size - 1] = 0;
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    memcpy(dst, src, (SIZE_T)(n + 1) * sizeof(WCHAR));
    return S_OK;
}

THEMEAPI GetCurrentThemeName(LPWSTR name, int name_size, LPWSTR color, int color_size, LPWSTR size, int size_size)
{
    if (name)
        *name = 0;
    else if (name_size)
        return E_POINTER;
    if (!UxThemeLoaded())
        return E_UNEXPECTED;
    HRESULT hr = copy_string(name, name_size, g_style.Path);
    if (SUCCEEDED(hr))
        hr = copy_string(color, color_size, L"NormalColor");
    if (SUCCEEDED(hr))
        hr = copy_string(size, size_size, L"NormalSize");
    return hr;
}

static const struct {
    LPCWSTR Name;
    int Prop;
} g_doc_props[] = {
    {L"DisplayName", TMT_DISPLAYNAME}, {L"ToolTip", TMT_TOOLTIP},   {L"Company", TMT_COMPANY},
    {L"Author", TMT_AUTHOR},           {L"Copyright", TMT_COPYRIGHT}, {L"Url", TMT_URL},
    {L"Version", TMT_VERSION},         {L"Description", TMT_DESCRIPTION},
};

THEMEAPI GetThemeDocumentationProperty(LPCWSTR theme_name, LPCWSTR prop_name, LPWSTR value, int value_size)
{
    if (!theme_name || !prop_name || !value)
        return E_POINTER;
    if (!UxThemeLoaded() || _wcsicmp(theme_name, g_style.Path))
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_doc_props); i++) {
        if (_wcsicmp(prop_name, g_doc_props[i].Name))
            continue;
        WCHAR buf[512];
        if (!mime_msstyles_doc_string(&UxStyles, g_doc_props[i].Prop, buf, RTL_NUMBER_OF(buf)))
            break;
        return copy_string(value, value_size, buf);
    }
    return UX_E_NOTFOUND;
}
