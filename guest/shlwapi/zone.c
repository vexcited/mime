/*
 * security zones: asking the security manager what a url may do, the width of the zone list and the zone settings
 * sheet of the internet options.
 */
#include "shlwapi_dynamic.h"
#include <mime/win32/prsht.h>
#include <mime/win32/objbase.h>

HRESULT WINAPI SHCoCreateInstanceAC(REFCLSID clsid, IUnknown *outer, DWORD context, REFIID riid, void **out);

#define SLOT(object, index) ((*(void ***)(object))[index])
#define CALL(type, object, index, ...) ((type)SLOT(object, index))(object __VA_OPT__(, ) __VA_ARGS__)
#define RELEASE(object) ((ULONG(STDMETHODCALLTYPE *)(void *))SLOT(object, 2))(object)

MIME_DEFINE_GUID(IID_ZoneSecurityManager, 0x79eac9ee, 0xbaf9, 0x11ce, 0x8c, 0x82, 0x00, 0xaa, 0x00, 0x4b, 0xa9, 0x0b);
MIME_DEFINE_GUID(CLSID_ZoneSecurityManager, 0x7b8a2d94, 0x0ac9, 0x11d1, 0x89, 0x6c, 0x00, 0xc0, 0x4f, 0xb6, 0xbf, 0xc4);
MIME_DEFINE_GUID(CLSID_ZoneManager, 0x7b8a2d95, 0x0ac9, 0x11d1, 0x89, 0x6c, 0x00, 0xc0, 0x4f, 0xb6, 0xbf, 0xc4);
MIME_DEFINE_GUID(IID_ZoneManager, 0x79eac9ef, 0xbaf9, 0x11ce, 0x8c, 0x82, 0x00, 0xaa, 0x00, 0x4b, 0xa9, 0x0b);

#define URL_BUFFER 2084

typedef HRESULT(STDMETHODCALLTYPE *PROCESS_URL_ACTION)(void *, LPCWSTR, DWORD, BYTE *, DWORD, BYTE *, DWORD, DWORD,
                                                       DWORD);

HRESULT WINAPI ZoneCheckUrlExCacheW(LPCWSTR url, BYTE *policy, DWORD policy_size, BYTE *context, DWORD context_size,
                                    DWORD action, DWORD flags, IUnknown *site, IUnknown **cache);
HRESULT WINAPI ZoneCheckUrlExCacheW(LPCWSTR url, BYTE *policy, DWORD policy_size, BYTE *context, DWORD context_size,
                                    DWORD action, DWORD flags, IUnknown *site, IUnknown **cache)
{
    if (!url)
        return E_INVALIDARG;
    void *manager = nullptr;
    HRESULT hr;
    if (cache && *cache) {
        hr = (*cache)->lpVtbl->QueryInterface(*cache, &IID_ZoneSecurityManager, &manager);
        if (FAILED(hr))
            return hr;
    } else {
        hr = SHCoCreateInstanceAC(&CLSID_ZoneSecurityManager, nullptr, CLSCTX_INPROC_SERVER, &IID_ZoneSecurityManager,
                                  &manager);
        if (hr == REGDB_E_CLASSNOTREG) {
            /* the manager of this machine is not registered as a class, and is asked of the library that has it. */
            HRESULT(WINAPI * create)(IUnknown *, void **, DWORD) =
                (HRESULT(WINAPI *)(IUnknown *, void **, DWORD))ShlProc(L"urlmon.dll", "CoInternetCreateSecurityManager");
            hr = create ? create(nullptr, &manager, 0) : hr;
        }
        if (FAILED(hr))
            return hr;
        if (cache)
            ((IUnknown *)manager)->lpVtbl->QueryInterface(manager, &IID_ZoneSecurityManager, (void **)cache);
    }
    DWORD fallback_policy = 0, fallback_context = 0;
    if (site)
        CALL(HRESULT(STDMETHODCALLTYPE *)(void *, IUnknown *), manager, 3, site);
    if (!context)
        context_size = sizeof(fallback_context);
    if (!policy)
        policy_size = sizeof(fallback_policy);
    hr = CALL(PROCESS_URL_ACTION, manager, 7, url, action, policy ? policy : (BYTE *)&fallback_policy, policy_size,
              context ? context : (BYTE *)&fallback_context, context_size, flags, 0);
    if (site)
        CALL(HRESULT(STDMETHODCALLTYPE *)(void *, IUnknown *), manager, 3, nullptr);
    RELEASE(manager);
    return hr;
}

static HRESULT check_ansi(LPCSTR url, BYTE *policy, DWORD policy_size, BYTE *context, DWORD context_size,
                          DWORD action, DWORD flags, IUnknown *site, IUnknown **cache)
{
    WCHAR wide[URL_BUFFER];
    wide[0] = 0;
    if (url)
        MultiByteToWideChar(CP_ACP, 0, url, -1, wide, URL_BUFFER - 1);
    wide[URL_BUFFER - 1] = 0;
    return ZoneCheckUrlExCacheW(wide, policy, policy_size, context, context_size, action, flags, site, cache);
}

HRESULT WINAPI ZoneCheckUrlA(LPCSTR url, DWORD action, DWORD flags, IUnknown *site);
HRESULT WINAPI ZoneCheckUrlA(LPCSTR url, DWORD action, DWORD flags, IUnknown *site)
{
    return check_ansi(url, nullptr, 0, nullptr, 0, action, flags, site, nullptr);
}

HRESULT WINAPI ZoneCheckUrlExA(LPCSTR url, BYTE *policy, DWORD policy_size, BYTE *context, DWORD context_size,
                               DWORD action, DWORD flags, IUnknown *site);
HRESULT WINAPI ZoneCheckUrlExA(LPCSTR url, BYTE *policy, DWORD policy_size, BYTE *context, DWORD context_size,
                               DWORD action, DWORD flags, IUnknown *site)
{
    return check_ansi(url, policy, policy_size, context, context_size, action, flags, site, nullptr);
}

HRESULT WINAPI ZoneCheckUrlExCacheA(LPCSTR url, BYTE *policy, DWORD policy_size, BYTE *context, DWORD context_size,
                                    DWORD action, DWORD flags, IUnknown *site, IUnknown **cache);
HRESULT WINAPI ZoneCheckUrlExCacheA(LPCSTR url, BYTE *policy, DWORD policy_size, BYTE *context, DWORD context_size,
                                    DWORD action, DWORD flags, IUnknown *site, IUnknown **cache)
{
    return check_ansi(url, policy, policy_size, context, context_size, action, flags, site, cache);
}

/* a host security manager answers for the host and not for a url. */
HRESULT WINAPI ZoneCheckHostEx(void *host, BYTE *policy, DWORD policy_size, BYTE *context, DWORD context_size,
                               DWORD action, DWORD flags);
HRESULT WINAPI ZoneCheckHostEx(void *host, BYTE *policy, DWORD policy_size, BYTE *context, DWORD context_size,
                               DWORD action, DWORD flags)
{
    typedef HRESULT(STDMETHODCALLTYPE * PROCESS)(void *, DWORD, BYTE *, DWORD, BYTE *, DWORD, DWORD, DWORD);
    DWORD fallback_policy = 0, fallback_context = 0;
    if (!host)
        return E_INVALIDARG;
    if (!policy)
        policy_size = sizeof(fallback_policy);
    if (!context)
        context_size = sizeof(fallback_context);
    return CALL(PROCESS, host, 4, action, policy ? policy : (BYTE *)&fallback_policy, policy_size,
                context ? context : (BYTE *)&fallback_context, context_size, flags, 0);
}

/* the width of the widest zone name in the font of the window, with the room around it. */
static int zone_names_width(HWND window, HFONT font)
{
    typedef HRESULT(WINAPI * INITIALIZE)(void *, DWORD);
    HDC dc = GetDC(window);
    HGDIOBJ old = SelectObject(dc, font);
    WCHAR text[260];
    SIZE size = {}, measured = {};
    int widest = 0;
    wcscpy(text, L" (Mixed)");
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);

    INITIALIZE init = (INITIALIZE)ShlProc(L"ole32.dll", "CoInitializeEx");
    void(WINAPI * uninit)(void) = (void(WINAPI *)(void))ShlProc(L"ole32.dll", "CoUninitialize");
    HRESULT init_result = init ? init(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) : E_FAIL;
    if (init_result == RPC_E_CHANGED_MODE) {
        init_result = init(nullptr, COINIT_DISABLE_OLE1DDE);
        init_result = init_result == RPC_E_CHANGED_MODE ? S_FALSE : init_result;
    }
    void *manager = nullptr;
    if (SUCCEEDED(SHCoCreateInstanceAC(&CLSID_ZoneManager, nullptr, CLSCTX_INPROC_SERVER, &IID_ZoneManager,
                                       &manager))) {
        DWORD list = 0, count = 0;
        if (SUCCEEDED(CALL(HRESULT(STDMETHODCALLTYPE *)(void *, DWORD *, DWORD *, DWORD), manager, 11, &list, &count,
                           0))) {
            for (DWORD i = 0; i < count; i++) {
                struct {
                    DWORD size;
                    WCHAR name[260];
                    BYTE rest[1460 - 4 - 520];
                } zone = {};
                DWORD id = 0;
                zone.size = sizeof(zone);
                CALL(HRESULT(STDMETHODCALLTYPE *)(void *, DWORD, DWORD, DWORD *), manager, 12, list, i, &id);
                CALL(HRESULT(STDMETHODCALLTYPE *)(void *, DWORD, void *), manager, 3, id, &zone);
                GetTextExtentPoint32W(dc, zone.name, lstrlenW(zone.name), &measured);
                if (widest < measured.cx)
                    widest = measured.cx;
            }
            CALL(HRESULT(STDMETHODCALLTYPE *)(void *, DWORD), manager, 13, list);
        }
        RELEASE(manager);
    }
    if (init_result == S_OK && uninit)
        uninit();
    SelectObject(dc, old);
    ReleaseDC(window, dc);
    return widest ? widest + size.cx : 220;
}

static SRWLOCK pane_lock = SRWLOCK_INIT;
static LOGFONTW pane_font;
static int pane_width;

int WINAPI ZoneComputePaneSize(HWND window);
int WINAPI ZoneComputePaneSize(HWND window)
{
    int names;
    LOGFONTW font = {};
    HFONT handle = (HFONT)SendMessageW(window, WM_GETFONT, 0, 0);
    if (GetObjectW(handle, sizeof(font), &font)) {
        int clear = lstrlenW(font.lfFaceName);
        memset(font.lfFaceName + clear, 0, (LF_FACESIZE - clear) * sizeof(WCHAR));
        BOOL cached = FALSE;
        names = 0;
        AcquireSRWLockShared(&pane_lock);
        if (!memcmp(&font, &pane_font, sizeof(font))) {
            cached = TRUE;
            names = pane_width;
        }
        ReleaseSRWLockShared(&pane_lock);
        if (!cached) {
            names = zone_names_width(window, handle);
            AcquireSRWLockExclusive(&pane_lock);
            pane_font = font;
            pane_width = names;
            ReleaseSRWLockExclusive(&pane_lock);
        }
    } else {
        names = zone_names_width(window, handle);
    }
    return names + 4 * GetSystemMetrics(SM_CXEDGE) + GetSystemMetrics(SM_CXVSCROLL) + GetSystemMetrics(SM_CXBORDER);
}

/* what the internet options page adder takes and gives. */
typedef struct {
    DWORD size, flags;
    LPCWSTR url;
    BYTE rest[48];
} SECURITY_PAGE_INFO;

#define ZONE_SHEET_PAGES 16

static BOOL CALLBACK add_sheet_page(HPROPSHEETPAGE page, LPARAM param)
{
    PROPSHEETHEADERW *header = (PROPSHEETHEADERW *)param;
    if (header->nPages >= ZONE_SHEET_PAGES)
        return FALSE;
    ((HPROPSHEETPAGE *)header->phpage)[header->nPages++] = page;
    return TRUE;
}

BOOL WINAPI ZoneConfigureW(HWND window, LPCWSTR url);
BOOL WINAPI ZoneConfigureW(HWND window, LPCWSTR url)
{
    typedef void(WINAPI * ADD)(BOOL(CALLBACK *)(HPROPSHEETPAGE, LPARAM), PROPSHEETHEADERW *, ULONG_PTR, ULONG_PTR,
                               SECURITY_PAGE_INFO *);
    HMODULE module = LoadLibraryW(L"inetcpl.cpl");
    if (!module)
        return FALSE;
    ADD add = (ADD)GetProcAddress(module, "AddInternetPropertySheetsEx");
    if (add) {
        WCHAR address[URL_BUFFER];
        HPROPSHEETPAGE pages[ZONE_SHEET_PAGES] = {};
        SECURITY_PAGE_INFO info = {};
        PROPSHEETHEADERW header = {};
        lstrcpynW(address, url, URL_BUFFER);
        info.size = 64;
        info.flags = 0x100;
        info.url = address;
        header.dwSize = sizeof(header);
        header.dwFlags = PSH_PROPTITLE;
        header.hwndParent = window;
        header.hInstance = GetModuleHandleW(L"shlwapi.dll");
        header.pszCaption = L"Internet Security";
        header.phpage = pages;
        add(add_sheet_page, &header, 0, 0, &info);
        if (header.nPages) {
            INT_PTR(WINAPI * sheet)(PROPSHEETHEADERW *) =
                (INT_PTR(WINAPI *)(PROPSHEETHEADERW *))ShlProc(L"comctl32.dll", "PropertySheetW");
            if (sheet)
                sheet(&header);
        } else {
            MessageBoxW(window,
                        L"This operation has been cancelled due to restrictions in effect on this computer. Please "
                        L"contact your system administrator.",
                        L"Restrictions", MB_OK | MB_ICONERROR);
        }
    }
    return FreeLibrary(module);
}
