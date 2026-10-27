/*
 * the visual style for the controls of user32: the parts of a class drawn by uxtheme, which is loaded when the first
 * control asks for it, as Windows does. uxtheme imports user32, so user32 cannot import it.
 */
#include "user32_private.h"

/* a handle of uxtheme, which user32 does not include. */
typedef HANDLE HTHEME;

typedef HTHEME(WINAPI *OPEN_THEME_DATA_FOR_DPI)(HWND window, LPCWSTR classes, UINT dpi);
typedef HRESULT(WINAPI *CLOSE_THEME_DATA)(HTHEME theme);
typedef HRESULT(WINAPI *DRAW_THEME_BACKGROUND)(HTHEME theme, HDC dc, int part, int state, const RECT *rect,
                                               const RECT *clip);
typedef HRESULT(WINAPI *GET_THEME_CONTENT_RECT)(HTHEME theme, HDC dc, int part, int state, const RECT *bounds,
                                                RECT *content);
typedef HRESULT(WINAPI *GET_THEME_PART_SIZE)(HTHEME theme, HDC dc, int part, int state, const RECT *rect, int type,
                                             SIZE *size);
typedef HRESULT(WINAPI *GET_THEME_COLOR)(HTHEME theme, int part, int state, int prop, COLORREF *color);
typedef BOOL(WINAPI *IS_THEME_ACTIVE)(void);
typedef BOOL(WINAPI *IS_PART_TRANSPARENT)(HTHEME theme, int part, int state);

#define TS_DRAW_SIZE 2
#define TMT_TEXTCOLOR_ID 3803

static struct {
    RTL_RUN_ONCE Once;
    RTL_CRITICAL_SECTION Lock;
    OPEN_THEME_DATA_FOR_DPI Open;
    CLOSE_THEME_DATA Close;
    DRAW_THEME_BACKGROUND Draw;
    GET_THEME_CONTENT_RECT ContentRect;
    GET_THEME_PART_SIZE PartSize;
    GET_THEME_COLOR Color;
    IS_THEME_ACTIVE Active;
    IS_PART_TRANSPARENT Transparent;
    BOOL Ready;
} g_ux;

#define CACHE_SIZE 24

/* the opened classes by name and dpi. */
static struct {
    WCHAR Name[32];
    UINT Dpi;
    HTHEME Handle;
} g_cache[CACHE_SIZE];

static ULONG NTAPI load_uxtheme(PRTL_RUN_ONCE once, PVOID param, PVOID *context)
{
    RtlInitializeCriticalSection(&g_ux.Lock);
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux)
        return TRUE;
    g_ux.Open = (OPEN_THEME_DATA_FOR_DPI)(void *)GetProcAddress(ux, "OpenThemeDataForDpi");
    g_ux.Close = (CLOSE_THEME_DATA)(void *)GetProcAddress(ux, "CloseThemeData");
    g_ux.Draw = (DRAW_THEME_BACKGROUND)(void *)GetProcAddress(ux, "DrawThemeBackground");
    g_ux.ContentRect = (GET_THEME_CONTENT_RECT)(void *)GetProcAddress(ux, "GetThemeBackgroundContentRect");
    g_ux.PartSize = (GET_THEME_PART_SIZE)(void *)GetProcAddress(ux, "GetThemePartSize");
    g_ux.Color = (GET_THEME_COLOR)(void *)GetProcAddress(ux, "GetThemeColor");
    g_ux.Active = (IS_THEME_ACTIVE)(void *)GetProcAddress(ux, "IsThemeActive");
    g_ux.Transparent = (IS_PART_TRANSPARENT)(void *)GetProcAddress(ux, "IsThemeBackgroundPartiallyTransparent");
    g_ux.Ready = g_ux.Open && g_ux.Draw && g_ux.ContentRect && g_ux.PartSize && g_ux.Color && g_ux.Active;
    return TRUE;
}

BOOL UserThemeActive(void)
{
    RtlRunOnceExecuteOnce(&g_ux.Once, load_uxtheme, nullptr, nullptr);
    return g_ux.Ready && g_ux.Active();
}

static HTHEME theme_handle(const WCHAR *name, UINT dpi)
{
    if (!UserThemeActive())
        return nullptr;
    if (!dpi)
        dpi = 96;
    RtlEnterCriticalSection(&g_ux.Lock);
    int free_slot = -1;
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!g_cache[i].Handle) {
            if (free_slot < 0)
                free_slot = i;
        } else if (g_cache[i].Dpi == dpi && !wcscmp(g_cache[i].Name, name)) {
            HTHEME h = g_cache[i].Handle;
            RtlLeaveCriticalSection(&g_ux.Lock);
            return h;
        }
    }
    HTHEME h = wcslen(name) < RTL_NUMBER_OF(g_cache[0].Name) ? g_ux.Open(nullptr, name, dpi) : nullptr;
    if (h && free_slot >= 0) {
        wcscpy(g_cache[free_slot].Name, name);
        g_cache[free_slot].Dpi = dpi;
        g_cache[free_slot].Handle = h;
    } else if (h) {
        /* a full cache gives the handle away uncached, it is closed with the process. */
    }
    RtlLeaveCriticalSection(&g_ux.Lock);
    return h;
}

BOOL UserThemeDraw(const WCHAR *name, HDC dc, int part, int state, const RECT *rect, UINT dpi)
{
    HTHEME h = theme_handle(name, dpi);
    return h && SUCCEEDED(g_ux.Draw(h, dc, part, state, rect, nullptr));
}

BOOL UserThemeContent(const WCHAR *name, int part, int state, const RECT *rect, RECT *content, UINT dpi)
{
    HTHEME h = theme_handle(name, dpi);
    *content = *rect;
    return h && SUCCEEDED(g_ux.ContentRect(h, nullptr, part, state, rect, content));
}

BOOL UserThemeSize(const WCHAR *name, int part, int state, UINT dpi, SIZE *size)
{
    HTHEME h = theme_handle(name, dpi);
    return h && SUCCEEDED(g_ux.PartSize(h, nullptr, part, state, nullptr, TS_DRAW_SIZE, size));
}

COLORREF UserThemeColor(const WCHAR *name, int part, int state, int prop, COLORREF fallback)
{
    HTHEME h = theme_handle(name, 96);
    COLORREF c;
    return h && SUCCEEDED(g_ux.Color(h, part, state, prop, &c)) ? c : fallback;
}

/* the Button class, for push buttons, check boxes, radio buttons and group boxes. */

BOOL UserThemeDrawBackground(HDC dc, int part, int state, const RECT *rect, UINT dpi)
{
    return UserThemeDraw(L"Button", dc, part, state, rect, dpi);
}

BOOL UserThemeContentRect(int part, int state, const RECT *rect, RECT *content, UINT dpi)
{
    return UserThemeContent(L"Button", part, state, rect, content, dpi);
}

BOOL UserThemePartSize(int part, int state, UINT dpi, SIZE *size)
{
    return UserThemeSize(L"Button", part, state, dpi, size);
}

COLORREF UserThemeTextColor(int part, int state, COLORREF fallback)
{
    return UserThemeColor(L"Button", part, state, TMT_TEXTCOLOR_ID, fallback);
}

/*
 * the client edge of a field window, an edit or a list box, as the border of the style. the 2 pixel edge takes a
 * border of one pixel in the color of the state and a pixel of the field color inside it.
 */
BOOL UserThemeFieldEdge(HWND hwnd, const WCHAR *name, int part, int state)
{
    if (!(GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE) || !UserThemeActive())
        return FALSE;
    UINT dpi = GetDpiForWindow(hwnd);
    RECT w;
    GetWindowRect(hwnd, &w);
    OffsetRect(&w, -w.left, -w.top);
    HDC dc = GetWindowDC(hwnd);
    if (!dc)
        return FALSE;
    int edge = GetSystemMetricsForDpi(SM_CXEDGE, dpi), edge_y = GetSystemMetricsForDpi(SM_CYEDGE, dpi);
    ExcludeClipRect(dc, w.left + edge, w.top + edge_y, w.right - edge, w.bottom - edge_y);
    BOOL ok = UserThemeDraw(name, dc, part, state, &w, dpi);
    ReleaseDC(hwnd, dc);
    return ok;
}
