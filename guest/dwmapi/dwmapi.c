/*
 * dwmapi: mime composes every window, like windows since 8, so composition
 * is always on. window attributes are kept as window properties and the
 * frame bounds are the window rectangle without its invisible resize
 * border.
 */
#define MIME_BUILD_DWMAPI
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/dwmapi.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/winuser.h>

WINUSERAPI BOOL WINAPI SetPropW(HWND hwnd, LPCWSTR name, HANDLE data);
WINUSERAPI HANDLE WINAPI GetPropW(HWND hwnd, LPCWSTR name);
WINUSERAPI HANDLE WINAPI RemovePropW(HWND hwnd, LPCWSTR name);

/* the attributes a window stores, each a DWORD, and the value they read back without one. */
static const struct {
    DWORD Attribute;
    LPCWSTR Prop;
    DWORD Default;
} g_attributes[] = {
    {DWMWA_NCRENDERING_POLICY, L"MimeDwmNcRenderingPolicy", DWMNCRP_USEWINDOWSTYLE},
    {DWMWA_TRANSITIONS_FORCEDISABLED, L"MimeDwmTransitionsDisabled", FALSE},
    {DWMWA_ALLOW_NCPAINT, L"MimeDwmAllowNcPaint", FALSE},
    {DWMWA_NONCLIENT_RTL_LAYOUT, L"MimeDwmRtlLayout", FALSE},
    {DWMWA_FORCE_ICONIC_REPRESENTATION, L"MimeDwmForceIconic", FALSE},
    {DWMWA_FLIP3D_POLICY, L"MimeDwmFlip3dPolicy", 0},
    {DWMWA_HAS_ICONIC_BITMAP, L"MimeDwmHasIconicBitmap", FALSE},
    {DWMWA_DISALLOW_PEEK, L"MimeDwmDisallowPeek", FALSE},
    {DWMWA_EXCLUDED_FROM_PEEK, L"MimeDwmExcludedFromPeek", FALSE},
    {DWMWA_CLOAK, L"MimeDwmCloak", FALSE},
    {DWMWA_FREEZE_REPRESENTATION, L"MimeDwmFreeze", FALSE},
    {DWMWA_PASSIVE_UPDATE_MODE, L"MimeDwmPassiveUpdate", FALSE},
    {DWMWA_USE_HOSTBACKDROPBRUSH, L"MimeDwmHostBackdrop", FALSE},
    {19, L"MimeDwmDarkMode", FALSE},
    {DWMWA_USE_IMMERSIVE_DARK_MODE, L"MimeDwmDarkMode", FALSE},
    {DWMWA_WINDOW_CORNER_PREFERENCE, L"MimeDwmCorner", 0},
    {DWMWA_BORDER_COLOR, L"MimeDwmBorderColor", DWMWA_COLOR_DEFAULT},
    {DWMWA_CAPTION_COLOR, L"MimeDwmCaptionColor", DWMWA_COLOR_DEFAULT},
    {DWMWA_TEXT_COLOR, L"MimeDwmTextColor", DWMWA_COLOR_DEFAULT},
    {DWMWA_SYSTEMBACKDROP_TYPE, L"MimeDwmBackdrop", 0},
};

static int find_attribute(DWORD attribute)
{
    for (int i = 0; i < (int)RTL_NUMBER_OF(g_attributes); i++)
        if (g_attributes[i].Attribute == attribute)
            return i;
    return -1;
}

/* a property holds the value plus one, so a stored 0 differs from no property. */
static DWORD read_attribute(HWND hwnd, int index)
{
    ULONG_PTR v = (ULONG_PTR)GetPropW(hwnd, g_attributes[index].Prop);
    return v ? (DWORD)(v - 1) : g_attributes[index].Default;
}

static BOOL is_top_level(HWND hwnd)
{
    return IsWindow(hwnd) && GetAncestor(hwnd, GA_ROOT) == hwnd;
}

/*
 * the window rectangle without its invisible resize border, as win32k
 * frames it: the sizing and padded borders around a caption, all but
 * their last pixel, on every side but the top, and the whole border when
 * maximized.
 */
static BOOL visible_frame(HWND hwnd, RECT *r)
{
    if (!GetWindowRect(hwnd, r))
        return FALSE;
    LONG style = GetWindowLongW(hwnd, GWL_STYLE), ex_style = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (style & WS_CHILD)
        return TRUE;
    UINT dpi = GetDpiForWindow(hwnd);
    int border = 0;
    if ((style & WS_CAPTION) == WS_CAPTION)
        border = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    else if (style & WS_THICKFRAME)
        border = GetSystemMetricsForDpi(SM_CXFRAME, dpi);
    else if ((style & WS_DLGFRAME) || (ex_style & WS_EX_DLGMODALFRAME))
        border = GetSystemMetricsForDpi(SM_CXDLGFRAME, dpi);
    if (style & WS_MAXIMIZE) {
        InflateRect(r, -border, -border);
        return TRUE;
    }
    int inset = border > 1 ? border - 1 : 0;
    r->left += inset;
    r->right -= inset;
    r->bottom -= inset;
    return TRUE;
}

DWMAPI DwmIsCompositionEnabled(BOOL *enabled)
{
    if (!enabled)
        return E_INVALIDARG;
    *enabled = TRUE;
    return S_OK;
}

/* composition cannot be turned off since windows 8; the call is accepted and ignored. */
DWMAPI DwmEnableComposition(UINT action)
{
    return S_OK;
}

DWMAPI DwmGetWindowAttribute(HWND hwnd, DWORD attribute, PVOID value, DWORD size)
{
    if (!value || size < sizeof(DWORD))
        return E_INVALIDARG;
    if (!IsWindow(hwnd))
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    switch (attribute) {
    case DWMWA_NCRENDERING_ENABLED: {
        int policy = find_attribute(DWMWA_NCRENDERING_POLICY);
        *(BOOL *)value = read_attribute(hwnd, policy) != DWMNCRP_DISABLED;
        return S_OK;
    }
    case DWMWA_EXTENDED_FRAME_BOUNDS:
        if (size != sizeof(RECT))
            return E_INVALIDARG;
        return visible_frame(hwnd, value) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    case DWMWA_CAPTION_BUTTON_BOUNDS: {
        if (size != sizeof(RECT))
            return E_INVALIDARG;
        /* close, maximize and minimize at the right of the caption, in window coordinates. */
        RECT wr;
        if (!GetWindowRect(hwnd, &wr))
            return HRESULT_FROM_WIN32(GetLastError());
        int w = wr.right - wr.left, frame = GetSystemMetrics(SM_CXFRAME);
        RECT *r = value;
        r->right = w - frame;
        r->left = r->right - 3 * GetSystemMetrics(SM_CXSIZE);
        r->top = 0;
        r->bottom = GetSystemMetrics(SM_CYSIZE) + frame;
        if (r->left < 0)
            r->left = 0;
        return S_OK;
    }
    case DWMWA_CLOAKED: {
        if (size != sizeof(DWORD))
            return E_INVALIDARG;
        *(DWORD *)value = read_attribute(hwnd, find_attribute(DWMWA_CLOAK)) ? DWM_CLOAKED_APP : 0;
        return S_OK;
    }
    case DWMWA_VISIBLE_FRAME_BORDER_THICKNESS:
        if (size != sizeof(UINT))
            return E_INVALIDARG;
        *(UINT *)value = 1;
        return S_OK;
    }
    int index = find_attribute(attribute);
    if (index < 0 || size != sizeof(DWORD))
        return E_INVALIDARG;
    *(DWORD *)value = read_attribute(hwnd, index);
    return S_OK;
}

DWMAPI DwmSetWindowAttribute(HWND hwnd, DWORD attribute, LPCVOID value, DWORD size)
{
    if (!value || size < sizeof(DWORD))
        return E_INVALIDARG;
    int index = find_attribute(attribute);
    if (index < 0 || size != sizeof(DWORD))
        return E_INVALIDARG;
    if (!IsWindow(hwnd))
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    DWORD v = *(const DWORD *)value;
    if (attribute == DWMWA_NCRENDERING_POLICY && v >= DWMNCRP_LAST)
        return E_INVALIDARG;
    if (attribute == DWMWA_WINDOW_CORNER_PREFERENCE && v > 3)
        return E_INVALIDARG;
    if (v == g_attributes[index].Default)
        RemovePropW(hwnd, g_attributes[index].Prop);
    else
        SetPropW(hwnd, g_attributes[index].Prop, (HANDLE)(ULONG_PTR)((ULONG_PTR)v + 1));
    return S_OK;
}

DWMAPI DwmExtendFrameIntoClientArea(HWND hwnd, const MARGINS *margins)
{
    if (!is_top_level(hwnd) || !margins)
        return E_INVALIDARG;
    return S_OK;
}

DWMAPI DwmEnableBlurBehindWindow(HWND hwnd, const DWM_BLURBEHIND *blur)
{
    if (!is_top_level(hwnd) || !blur || !blur->dwFlags || (blur->dwFlags & ~7u))
        return E_INVALIDARG;
    return S_OK;
}

/* the dwm never handles caption hit testing for the window itself. */
DWMAPI_(BOOL) DwmDefWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    if (result)
        *result = 0;
    return FALSE;
}

DWMAPI DwmFlush(void)
{
    return S_OK;
}

/* the default accent of windows 11 at the opacity dwm reports for it. guessed, there is no colorization store yet. */
DWMAPI DwmGetColorizationColor(DWORD *color, BOOL *opaque_blend)
{
    if (!color || !opaque_blend)
        return E_INVALIDARG;
    *color = 0xC40078D4;
    *opaque_blend = FALSE;
    return S_OK;
}

DWMAPI DwmEnableMMCSS(BOOL enable)
{
    return S_OK;
}

/*
 * the timing of the composition: the refresh rate of the display, the
 * last vblank on the performance counter, and frame counters that windows
 * itself only advances by one each call since composition went to dwm's
 * own statistics. only the desktop is accepted.
 */
#pragma pack(push, 1)
typedef struct DWM_TIMING_INFO_RAW {
    UINT32 cbSize;
    UINT32 RateNumerator, RateDenominator;
    ULONGLONG RefreshPeriod;
    UINT32 ComposeNumerator, ComposeDenominator;
    ULONGLONG VBlank, Refresh;
    UINT32 DXRefresh;
    ULONGLONG Compose, Frame;
    UINT32 DXPresent;
    ULONGLONG RefreshFrame, FrameSubmitted;
    UINT32 DXPresentSubmitted;
    ULONGLONG FrameConfirmed;
    UINT32 DXPresentConfirmed;
    ULONGLONG RefreshConfirmed;
    UINT32 DXRefreshConfirmed;
    ULONGLONG FramesLate;
    UINT32 FramesOutstanding;
    BYTE Rest[292 - 124];
} DWM_TIMING_INFO_RAW;
#pragma pack(pop)

static LONG64 g_frames = 1;
static LONG g_dx_frames = 1;

DWMAPI DwmGetCompositionTimingInfo(HWND hwnd, void *info)
{
    DWM_TIMING_INFO_RAW *t = info;
    if (!t)
        return E_INVALIDARG;
    if (t->cbSize != sizeof(*t))
        return (HRESULT)0x88980290;
    if (hwnd)
        return E_INVALIDARG;
    HDC screen = GetDC(nullptr);
    int refresh = screen ? GetDeviceCaps(screen, VREFRESH) : 0;
    if (screen)
        ReleaseDC(nullptr, screen);
    DWORD rate = refresh > 1 ? (DWORD)refresh : 60;
    LARGE_INTEGER frequency, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    ULONGLONG period = (ULONGLONG)frequency.QuadPart / rate;
    LONG64 frame = InterlockedIncrement64(&g_frames);
    LONG dx = InterlockedIncrement(&g_dx_frames);
    memset((BYTE *)t + sizeof(UINT32), 0, sizeof(*t) - sizeof(UINT32));
    t->RateNumerator = t->ComposeNumerator = rate;
    t->RateDenominator = t->ComposeDenominator = 1;
    t->RefreshPeriod = period;
    t->VBlank = t->Compose = (ULONGLONG)now.QuadPart - (ULONGLONG)now.QuadPart % period;
    t->Refresh = (ULONGLONG)frame;
    t->DXRefresh = (UINT32)dx;
    t->Frame = (ULONGLONG)frame - 1;
    t->DXPresent = (UINT32)dx - 1;
    t->RefreshFrame = t->FrameSubmitted = (ULONGLONG)frame - 1;
    t->DXPresentSubmitted = (UINT32)dx - 1;
    t->FrameConfirmed = t->RefreshConfirmed = (ULONGLONG)frame - 2;
    t->DXPresentConfirmed = (UINT32)dx - 2;
    t->FramesOutstanding = 1;
    return S_OK;
}

/* the thumbnail and the live preview of a minimized window are shown by the taskbar and the switcher, which are not here, so
   the bitmaps a window gives are accepted and kept by nobody. */
DWMAPI DwmSetIconicThumbnail(HWND hwnd, HBITMAP bitmap, DWORD flags)
{
    if (!IsWindow(hwnd) || !bitmap || (flags & ~1u))
        return E_INVALIDARG;
    return S_OK;
}

DWMAPI DwmInvalidateIconicBitmaps(HWND hwnd)
{
    return IsWindow(hwnd) ? S_OK : E_INVALIDARG;
}
