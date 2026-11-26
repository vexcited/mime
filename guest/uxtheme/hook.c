#include "uxtheme_private.h"
#include <mime/ntuser.h>

/*
 * the api hook of the themes, which user32 asks win32k to load into every program and initializes through
 * ThemeInitApiHook, as it does with the uxtheme of windows. the table is tagUSERAPIHOOK of user32 26300, 232 bytes
 * on x64 and 116 on x86; user32 fills it with its own functions first, and keeps the hook only if the size is set
 * and ForceResetUserApiHook is left in place.
 *
 * DefWindowProc is taken for WM_NCHITTEST only: user32 tests the frame of a window itself with the classic layout,
 * where uxtheme answers for a child window with a caption from the parts of the style. win32k draws those frames,
 * so it answers their hit tests too, through its own DefWindowProc. the metrics are taken for the width of the
 * caption buttons, wider than the square ones of win32k, as ThemeGetSystemMetrics and ThemeSystemParametersInfoW
 * of uxtheme give it: SM_CXSIZE and the iCaptionWidth of the nonclient metrics.
 */
typedef struct MSGMASK {
    BYTE *Bits;
    DWORD Count;
} MSGMASK;

typedef struct USERAPIHOOK {
    DWORD Size;
    WNDPROC DefWindowProcA, DefWindowProcW;
    MSGMASK DefWindowProcMessages;
    void *GetScrollInfo, *SetScrollInfo, *EnableScrollBar, *AdjustWindowRectEx, *SetWindowRgn;
    void *PreWndProc, *PostWndProc;
    MSGMASK WndProcMessages;
    void *PreDefDlgProc, *PostDefDlgProc;
    MSGMASK DefDlgProcMessages;
    void *GetSystemMetrics, *SystemParametersInfoA, *SystemParametersInfoW, *ForceResetUserApiHook;
    void *DrawFrameControl, *DrawCaption, *MDIRedrawFrame, *Reserved, *GetSystemMetricsForDpi;
    void *SystemParametersInfoForDpi, *GetRealWindowOwner;
} USERAPIHOOK;

#ifdef _WIN64
MIME_STATIC_ASSERT(sizeof(USERAPIHOOK) == 232, "user api hook");
#else
MIME_STATIC_ASSERT(sizeof(USERAPIHOOK) == 116, "user api hook");
#endif

#define UIAH_INITIALIZE 0
/* the kind of NtUserMessageCall that runs the DefWindowProc of win32k. */
#define FNID_DEFWINDOWPROC 0x029E

typedef int(WINAPI *METRICS)(int index);
typedef int(WINAPI *METRICS_DPI)(int index, UINT dpi);
typedef BOOL(WINAPI *PARAMETERS)(UINT action, UINT param, void *data, UINT flags);
typedef BOOL(WINAPI *PARAMETERS_DPI)(UINT action, UINT param, void *data, UINT flags, UINT dpi);

static WNDPROC g_def_window_proc_a, g_def_window_proc_w;
static METRICS g_metrics;
static METRICS_DPI g_metrics_dpi;
static PARAMETERS g_parameters_a, g_parameters_w;
static PARAMETERS_DPI g_parameters_dpi;
static BYTE g_messages[(WM_NCHITTEST >> 3) + 1];

#define SPI_GETNONCLIENTMETRICS 0x0029
#define SPI_GETHIGHCONTRAST 0x0042

/* the style is on and the contrast not high, asked of the functions of user32 the hook stands in front of. */
static BOOL themed(void)
{
    if (!UxThemeLoaded())
        return FALSE;
    struct {
        UINT cbSize;
        DWORD dwFlags;
        LPWSTR lpszDefaultScheme;
    } hc = {sizeof(hc), 0, nullptr};
    return !g_parameters_w(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) || !(hc.dwFlags & 1);
}

/* what uxtheme draws a frame for: a child window with a caption. */
static BOOL themed_child_frame(HWND hwnd)
{
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    return (style & WS_CHILD) && (style & WS_CAPTION) == WS_CAPTION && themed();
}

/* the height of a caption button in the ratio of the buttons of the style, 36 to 22. */
static int button_width(int height)
{
    return MulDiv(height, 36, 22);
}

static int WINAPI get_system_metrics(int index)
{
    if (index == SM_CXSIZE && themed())
        return button_width(g_metrics(SM_CYSIZE));
    return g_metrics(index);
}

static int WINAPI get_system_metrics_for_dpi(int index, UINT dpi)
{
    if (index == SM_CXSIZE && (int)dpi > 0 && themed())
        return button_width(g_metrics_dpi(SM_CYSIZE, dpi));
    return g_metrics_dpi(index, dpi);
}

/* iCaptionWidth follows the four sizes of the border, the scroll bars and the caption in either charset. */
static void widen_caption(UINT action, void *data, BOOL ok, int height)
{
    if (ok && action == SPI_GETNONCLIENTMETRICS && data && themed())
        ((int *)data)[4] = button_width(height);
}

static BOOL WINAPI system_parameters_info_a(UINT action, UINT param, void *data, UINT flags)
{
    BOOL ok = g_parameters_a(action, param, data, flags);
    widen_caption(action, data, ok, g_metrics(SM_CYSIZE));
    return ok;
}

static BOOL WINAPI system_parameters_info_w(UINT action, UINT param, void *data, UINT flags)
{
    BOOL ok = g_parameters_w(action, param, data, flags);
    widen_caption(action, data, ok, g_metrics(SM_CYSIZE));
    return ok;
}

static BOOL WINAPI system_parameters_info_for_dpi(UINT action, UINT param, void *data, UINT flags, UINT dpi)
{
    BOOL ok = g_parameters_dpi(action, param, data, flags, dpi);
    widen_caption(action, data, ok, g_metrics_dpi(SM_CYSIZE, dpi));
    return ok;
}

static LRESULT def_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi)
{
    if (msg == WM_NCHITTEST && themed_child_frame(hwnd))
        return NtUserMessageCall(hwnd, msg, wparam, lparam, 0, FNID_DEFWINDOWPROC, ansi);
    return (ansi ? g_def_window_proc_a : g_def_window_proc_w)(hwnd, msg, wparam, lparam);
}

static LRESULT WINAPI def_window_proc_a(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return def_window_proc(hwnd, msg, wparam, lparam, TRUE);
}

static LRESULT WINAPI def_window_proc_w(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return def_window_proc(hwnd, msg, wparam, lparam, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI ThemeInitApiHook(DWORD command, void *data)
{
    if (command != UIAH_INITIALIZE)
        return TRUE;
    USERAPIHOOK *hook = data;
    if (!hook || !hook->DefWindowProcA || !hook->DefWindowProcW || !hook->GetSystemMetrics ||
        !hook->GetSystemMetricsForDpi || !hook->SystemParametersInfoA || !hook->SystemParametersInfoW ||
        !hook->SystemParametersInfoForDpi)
        return FALSE;
    g_def_window_proc_a = hook->DefWindowProcA;
    g_def_window_proc_w = hook->DefWindowProcW;
    g_metrics = hook->GetSystemMetrics;
    g_metrics_dpi = hook->GetSystemMetricsForDpi;
    g_parameters_a = hook->SystemParametersInfoA;
    g_parameters_w = hook->SystemParametersInfoW;
    g_parameters_dpi = hook->SystemParametersInfoForDpi;
    hook->GetSystemMetrics = get_system_metrics;
    hook->GetSystemMetricsForDpi = get_system_metrics_for_dpi;
    hook->SystemParametersInfoA = system_parameters_info_a;
    hook->SystemParametersInfoW = system_parameters_info_w;
    hook->SystemParametersInfoForDpi = system_parameters_info_for_dpi;
    g_messages[WM_NCHITTEST >> 3] |= 1 << (WM_NCHITTEST & 7);
    hook->DefWindowProcA = def_window_proc_a;
    hook->DefWindowProcW = def_window_proc_w;
    hook->DefWindowProcMessages.Bits = g_messages;
    hook->DefWindowProcMessages.Count = sizeof(g_messages);
    return TRUE;
}
