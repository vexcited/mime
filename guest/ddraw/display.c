/*
 * cooperative levels, display modes and exclusive mode on top of user32, as
 * on Windows. modes are ChangeDisplaySettings modes, an exclusive fullscreen
 * window is made to cover the screen, and ddraw follows its activation by
 * subclassing it. where the display lacks them, 320x200 and 320x240 mode x
 * modes are emulated in a 640x480 mode, as Windows does.
 */
#include "ddraw_private.h"
#include <mime/display_modes.h>

/* exclusive mode is per process here, where Windows holds it machine wide with a mutex. */
static DDRAW *g_exclusive;
static const WCHAR WINDOW_PROC_PROP[] = L"MimeDDrawWindowProc";
static const WCHAR DEVICE_CLASS[] = L"DirectDrawDeviceWnd";

DDRAW *DdExclusiveOwner(void)
{
    return g_exclusive;
}

BOOL DdIsExclusiveOwner(DDRAW *dd)
{
    return g_exclusive == dd;
}

/* ------------------------------------------------------------------ modes */

void DdPixelFormatForBpp(DWORD bpp, DDPIXELFORMAT *pf)
{
    memset(pf, 0, sizeof(*pf));
    pf->dwSize = sizeof(*pf);
    pf->dwFlags = DDPF_RGB;
    pf->dwRGBBitCount = bpp;
    switch (bpp) {
    case 8:
        pf->dwFlags |= DDPF_PALETTEINDEXED8;
        break;
    case 15:
        pf->dwRGBBitCount = 16;
        pf->dwRBitMask = 0x7C00, pf->dwGBitMask = 0x03E0, pf->dwBBitMask = 0x001F;
        break;
    case 16:
        pf->dwRBitMask = 0xF800, pf->dwGBitMask = 0x07E0, pf->dwBBitMask = 0x001F;
        break;
    case 24:
    case 32:
        pf->dwRBitMask = 0x00FF0000, pf->dwGBitMask = 0x0000FF00, pf->dwBBitMask = 0x000000FF;
        break;
    }
}

/* the display's current mode, or the mode x mode a program emulates in it. */
void DdCurrentMode(DDMODE *mode)
{
    DEVMODEW dm = {.dmSize = sizeof(dm)};
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        dm.dmPelsWidth = GetSystemMetrics(SM_CXSCREEN);
        dm.dmPelsHeight = GetSystemMetrics(SM_CYSCREEN);
        dm.dmBitsPerPel = 32;
        dm.dmDisplayFrequency = 60;
    }
    *mode = (DDMODE){dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel, dm.dmDisplayFrequency};
    for (LIST_ENTRY *e = DdObjects.Flink; e != &DdObjects; e = e->Flink) {
        DDRAW *dd = CONTAINING_RECORD(e, DDRAW, links);
        if ((dd->flags & DDF_MODEX) && dd->mode_width == 320 && mode->width == 640 && mode->height == 480) {
            mode->width = dd->mode_width;
            mode->height = dd->mode_height;
            break;
        }
    }
}

/* as GetDisplayMode and EnumDisplayModes fill it: 120 bytes for the first interfaces. */
void DdFillModeDesc(DWORD version, DDSURFACEDESC2 *desc, DWORD width, DWORD height, DWORD bpp, DWORD refresh)
{
    memset(desc, 0, sizeof(*desc));
    desc->dwSize = version < DDV4 ? sizeof(DDSURFACEDESC) : sizeof(DDSURFACEDESC2);
    desc->dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
    desc->dwWidth = width;
    desc->dwHeight = height;
    desc->lPitch = (LONG)(width * bpp / 8);
    desc->dwRefreshRate = refresh;
    DdPixelFormatForBpp(bpp, &desc->ddpfPixelFormat);
}

/* the display's modes without duplicates, 15 bits reported as 16, then mode x. */
HRESULT DdEnumDisplayModes(DDRAW *dd, DWORD version, DWORD flags, const DDSURFACEDESC2 *filter, void *context,
                           void *callback)
{
    if (!callback || (flags & ~(DDEDM_REFRESHRATES | DDEDM_STANDARDVGAMODES)))
        return DDERR_INVALIDPARAMS;
    DDMODE seen[256];
    DWORD count = 0;
    BOOL vga = FALSE;
    DEVMODEW dm = {.dmSize = sizeof(dm)};
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm) && count < RTL_NUMBER_OF(seen); i++) {
        DWORD bpp = dm.dmBitsPerPel == 15 ? 16 : dm.dmBitsPerPel;
        if (bpp < 8)
            continue;
        if (bpp == 8 && dm.dmPelsWidth == 640 && dm.dmPelsHeight == 480)
            vga = TRUE;
        DWORD refresh = (flags & DDEDM_REFRESHRATES) ? dm.dmDisplayFrequency : 0;
        if (filter && (((filter->dwFlags & DDSD_WIDTH) && dm.dmPelsWidth != filter->dwWidth) ||
                       ((filter->dwFlags & DDSD_HEIGHT) && dm.dmPelsHeight != filter->dwHeight) ||
                       ((filter->dwFlags & DDSD_PIXELFORMAT) && bpp != filter->ddpfPixelFormat.dwRGBBitCount) ||
                       ((filter->dwFlags & DDSD_REFRESHRATE) && dm.dmDisplayFrequency != filter->dwRefreshRate)))
            continue;
        BOOL duplicate = FALSE;
        for (DWORD j = 0; j < count && !duplicate; j++)
            duplicate = seen[j].width == dm.dmPelsWidth && seen[j].height == dm.dmPelsHeight && seen[j].bpp == bpp &&
                        seen[j].refresh == refresh;
        if (!duplicate)
            seen[count++] = (DDMODE){dm.dmPelsWidth, dm.dmPelsHeight, bpp, refresh};
    }
    DDSURFACEDESC2 desc;
    DDSURFACEDESC old;
    for (DWORD i = 0; i <= count + 1; i++) {
        BOOL modex = i >= count;
        DDMODE m = modex ? (DDMODE){320, i == count ? 200u : 240u, 8, (flags & DDEDM_REFRESHRATES) ? 60u : 0u}
                         : seen[i];
        if (modex && (!(dd->flags & DDF_MODEX) || !vga))
            break;
        if (modex && filter &&
            (((filter->dwFlags & DDSD_WIDTH) && filter->dwWidth != 320) ||
             ((filter->dwFlags & DDSD_HEIGHT) && filter->dwHeight != m.height) ||
             ((filter->dwFlags & DDSD_PIXELFORMAT) && filter->ddpfPixelFormat.dwRGBBitCount != 8)))
            continue;
        DdFillModeDesc(version, &desc, m.width, m.height, m.bpp, m.refresh);
        if (modex)
            desc.ddsCaps.dwCaps |= DDSCAPS_MODEX;
        HRESULT result;
        RtlLeaveCriticalSection(&DdLock);
        if (version < DDV4) {
            DdSurfDescToV1(&desc, &old);
            result = ((LPDDENUMMODESCALLBACK)callback)(&old, context);
        } else {
            result = ((LPDDENUMMODESCALLBACK2)callback)(&desc, context);
        }
        RtlEnterCriticalSection(&DdLock);
        if (result == DDENUMRET_CANCEL)
            break;
    }
    return DD_OK;
}

/* ------------------------------------------------------------------ the window */

/* the window covers the screen, topmost while the program is in front. */
static void make_fullscreen(DDRAW *dd, HWND window)
{
    if (!window || !IsWindow(window))
        return;
    DDMODE mode;
    DdCurrentMode(&mode);
    int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(window, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    if (GetForegroundWindow() == dd->focus) {
        if (!IsWindowVisible(window))
            ShowWindow(window, SW_SHOW);
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
        if (IsZoomed(window))
            ShowWindow(window, SW_RESTORE);
    }
    if (dd->flags & DDF_ACTIVE) {
        RECT screen = {0, 0, width, height};
        dd->flags |= DDF_CLIPPED;
        ClipCursor(&screen);
    }
}

static void unclip_cursor(DDRAW *dd)
{
    if (dd->flags & DDF_CLIPPED) {
        dd->flags &= ~DDF_CLIPPED;
        ClipCursor(nullptr);
    }
}

/* an exclusive program losing the foreground gives back the display. */
static void activate_app(DDRAW *dd, BOOL active)
{
    HWND window = dd->device;
    if (active) {
        dd->flags |= DDF_ACTIVE;
        DdShowGamma(dd);
        if (dd->mode_width && !(dd->flags & DDF_MODESET))
            DdSetDisplayMode(dd, dd->mode_width, dd->mode_height, dd->mode_bpp, dd->mode_refresh, 0);
        if ((dd->level & (DDSCL_FULLSCREEN | DDSCL_NOWINDOWCHANGES)) == DDSCL_FULLSCREEN && IsWindowVisible(window)) {
            ShowWindow(window, SW_SHOWNOACTIVATE);
            make_fullscreen(dd, window);
        }
        return;
    }
    dd->flags &= ~DDF_ACTIVE;
    if (dd->primary)
        DdHideGamma(dd->primary);
    DdLoseSurfaces(dd);
    if (dd->flags & DDF_MODESET) {
        ChangeDisplaySettingsW(nullptr, 0);
        dd->flags &= ~DDF_MODESET;
    }
    unclip_cursor(dd);
    if ((dd->level & (DDSCL_FULLSCREEN | DDSCL_NOWINDOWCHANGES)) == DDSCL_FULLSCREEN && IsWindowVisible(window))
        ShowWindow(window, SW_SHOWMINNOACTIVE);
}

static DDRAW *object_of_window(HWND window)
{
    for (LIST_ENTRY *e = DdObjects.Flink; e != &DdObjects; e = e->Flink) {
        DDRAW *dd = CONTAINING_RECORD(e, DDRAW, links);
        if ((dd->flags & DDF_SUBCLASSED) && dd->focus == window)
            return dd;
    }
    return nullptr;
}

static LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WNDPROC original = (WNDPROC)GetPropW(window, WINDOW_PROC_PROP);
    RtlEnterCriticalSection(&DdLock);
    DDRAW *dd = object_of_window(window);
    if (dd && (dd->flags & DDF_EXCLUSIVE)) {
        switch (msg) {
        case WM_ACTIVATEAPP:
            if (!wparam || (!IsIconic(window) && GetForegroundWindow() == window))
                activate_app(dd, wparam != 0);
            break;
        case WM_DISPLAYCHANGE:
            if ((dd->level & (DDSCL_FULLSCREEN | DDSCL_NOWINDOWCHANGES)) == DDSCL_FULLSCREEN)
                make_fullscreen(dd, dd->device);
            break;
        case WM_SYSCOMMAND:
            /* no screen saver over an exclusive program. */
            if ((wparam & 0xFFF0) == SC_SCREENSAVE) {
                RtlLeaveCriticalSection(&DdLock);
                return 1;
            }
            break;
        }
    }
    RtlLeaveCriticalSection(&DdLock);
    if (!original)
        return DefWindowProcW(window, msg, wparam, lparam);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)original);
        RemovePropW(window, WINDOW_PROC_PROP);
    }
    return CallWindowProcW(original, window, msg, wparam, lparam);
}

static void subclass(DDRAW *dd, HWND window)
{
    if (!window || (dd->flags & DDF_SUBCLASSED) || GetPropW(window, WINDOW_PROC_PROP))
        return;
    LONG_PTR original = GetWindowLongPtrW(window, GWLP_WNDPROC);
    SetPropW(window, WINDOW_PROC_PROP, (HANDLE)original);
    SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)window_proc);
    dd->flags |= DDF_SUBCLASSED;
}

static void unsubclass(DDRAW *dd)
{
    if (!(dd->flags & DDF_SUBCLASSED))
        return;
    dd->flags &= ~DDF_SUBCLASSED;
    HWND window = dd->focus;
    LONG_PTR original = (LONG_PTR)GetPropW(window, WINDOW_PROC_PROP);
    if (window && IsWindow(window) && original) {
        if (GetWindowLongPtrW(window, GWLP_WNDPROC) == (LONG_PTR)window_proc)
            SetWindowLongPtrW(window, GWLP_WNDPROC, original);
        RemovePropW(window, WINDOW_PROC_PROP);
    }
}

static HWND create_device_window(DDRAW *dd)
{
    WNDCLASSW wc = {CS_DBLCLKS, DefWindowProcW, 0, sizeof(void *), DdModule};
    wc.hbrBackground = GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = DEVICE_CLASS;
    RegisterClassW(&wc);
    return CreateWindowExW(0, DEVICE_CLASS, DEVICE_CLASS, WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                           GetSystemMetrics(SM_CYSCREEN), dd->focus, nullptr, DdModule, nullptr);
}

/* a first version object keeps its mode until it goes away. */
void DdReleaseExclusive(DDRAW *dd)
{
    if (g_exclusive != dd)
        return;
    g_exclusive = nullptr;
    dd->flags &= ~(DDF_EXCLUSIVE | DDF_ACTIVE);
    if (dd->primary)
        DdHideGamma(dd->primary);
    if (!dd->level_v1 && (dd->flags & DDF_MODESET))
        DdRestoreDisplayMode(dd);
    unclip_cursor(dd);
}

/* ------------------------------------------------------------------ SetCooperativeLevel */

/* the checks run in the same order as on Windows. */
HRESULT DdSetCooperativeLevel(DDRAW *dd, DWORD version, HWND window, DWORD flags)
{
    if ((flags & 0xFFFF0000) || (flags & (DDSCL_FPUSETUP | DDSCL_FPUPRESERVE)) == (DDSCL_FPUSETUP | DDSCL_FPUPRESERVE))
        return DDERR_INVALIDPARAMS;
    if (!(flags & (DDSCL_NORMAL | DDSCL_EXCLUSIVE | DDSCL_SETFOCUSWINDOW)) ||
        ((flags & DDSCL_EXCLUSIVE) && !(flags & DDSCL_FULLSCREEN)) ||
        ((flags & DDSCL_ALLOWMODEX) && !(flags & DDSCL_FULLSCREEN)))
        return DDERR_INVALIDPARAMS;
    if (window && !IsWindow(window))
        return DDERR_INVALIDPARAMS;
    if ((flags & 0x30) == 0x30)
        return DDERR_INVALIDPARAMS;
    if (flags & DDSCL_SETFOCUSWINDOW) {
        if ((flags & ~(DDSCL_SETFOCUSWINDOW | DDSCL_NOWINDOWCHANGES | DDSCL_ALLOWMODEX | 0x2000)) &&
            !(flags & DDSCL_CREATEDEVICEWINDOW))
            return DDERR_INVALIDPARAMS;
        if (dd->flags & DDF_EXCLUSIVE)
            return DDERR_HWNDALREADYSET;
    }
    if (flags & DDSCL_SETDEVICEWINDOW) {
        if (flags & (DDSCL_CREATEDEVICEWINDOW | DDSCL_SETFOCUSWINDOW))
            return DDERR_INVALIDPARAMS;
        if (window && !dd->focus)
            return DDERR_NOFOCUSWINDOW;
    }
    if ((flags & DDSCL_CREATEDEVICEWINDOW) && dd->device && !(dd->flags & DDF_DEVICEWINDOW))
        return DDERR_INVALIDPARAMS;
    if (flags & DDSCL_CREATEDEVICEWINDOW) {
        if (!(flags & DDSCL_EXCLUSIVE))
            return DDERR_INVALIDPARAMS;
        if (!dd->device && !(flags & DDSCL_SETFOCUSWINDOW)) {
            if (window)
                return DDERR_INVALIDPARAMS;
            if (!dd->focus)
                return DDERR_NOFOCUSWINDOW;
        }
    }
    if ((flags & DDSCL_EXCLUSIVE) && !(flags & DDSCL_CREATEDEVICEWINDOW)) {
        if (!window || (GetWindowLongW(window, GWL_STYLE) & WS_CHILD))
            return DDERR_INVALIDPARAMS;
    }
    /* a window without a caption keeps its place and size. */
    if (window && !(flags & (DDSCL_SETDEVICEWINDOW | DDSCL_CREATEDEVICEWINDOW)) &&
        (GetWindowLongW(window, GWL_STYLE) & WS_CAPTION) == WS_DLGFRAME)
        flags |= DDSCL_NOWINDOWCHANGES;
    if ((flags & DDSCL_EXCLUSIVE) && g_exclusive && g_exclusive != dd)
        return DDERR_EXCLUSIVEMODEALREADYSET;
    if (flags & DDSCL_SETFOCUSWINDOW) {
        dd->focus = window;
        if (!(flags & DDSCL_CREATEDEVICEWINDOW)) {
            dd->flags |= DDF_LEVELSET;
            return DD_OK;
        }
    }
    if ((flags & DDSCL_CREATEDEVICEWINDOW) && !dd->device) {
        window = create_device_window(dd);
        if (!window)
            return DDERR_INVALIDPARAMS;
        dd->flags |= DDF_DEVICEWINDOW;
    } else if (flags & DDSCL_CREATEDEVICEWINDOW) {
        window = dd->device;
    }
    /* a device window ddraw made goes when another takes its place. */
    if (window != dd->device && (dd->flags & DDF_DEVICEWINDOW) && dd->device && IsWindow(dd->device)) {
        DestroyWindow(dd->device);
        dd->flags &= ~DDF_DEVICEWINDOW;
    }
    HWND previous = dd->device;
    dd->device = window;
    if (!(flags & (DDSCL_SETDEVICEWINDOW | DDSCL_CREATEDEVICEWINDOW))) {
        if (dd->focus != window)
            unsubclass(dd);
        dd->focus = window;
    }
    if ((flags & DDSCL_ALLOWMODEX) && (flags & DDSCL_FULLSCREEN))
        dd->flags |= DDF_MODEX;
    else
        dd->flags &= ~DDF_MODEX;
    dd->level = flags;
    dd->level_v1 = version == DDV1;
    if (flags & DDSCL_EXCLUSIVE) {
        if (flags & DDSCL_FULLSCREEN)
            dd->flags |= DDF_FULLSCREEN;
        subclass(dd, dd->focus);
        if (g_exclusive != dd) {
            g_exclusive = dd;
            dd->flags |= DDF_EXCLUSIVE | DDF_ACTIVE;
            DdShowGamma(dd);
            if (window)
                SetForegroundWindow(window);
        }
        if ((flags & (DDSCL_FULLSCREEN | DDSCL_NOWINDOWCHANGES)) == DDSCL_FULLSCREEN)
            make_fullscreen(dd, window);
    } else if (dd->flags & DDF_EXCLUSIVE) {
        if (dd->primary)
            DdPresentFlip(dd->primary);
        DdReleaseExclusive(dd);
        dd->flags &= ~DDF_FULLSCREEN;
        unsubclass(dd);
        if (!(flags & DDSCL_NOWINDOWCHANGES) && previous && IsWindow(previous))
            SetWindowPos(previous, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
    }
    dd->flags |= DDF_LEVELSET;
    return DD_OK;
}

/* ------------------------------------------------------------------ SetDisplayMode */

/*
 * a ChangeDisplaySettings mode change that loses every video memory surface.
 * any refresh rate takes the display's: emulated modes have no other, and a
 * program asking for 60 hz on a faster panel would fail for nothing.
 */
HRESULT DdSetDisplayMode(DDRAW *dd, DWORD width, DWORD height, DWORD bpp, DWORD refresh, DWORD flags)
{
    if (flags & ~DDSDM_STANDARDVGAMODE)
        return DDERR_INVALIDPARAMS;
    if (g_exclusive && g_exclusive != dd)
        return DDERR_NOEXCLUSIVEMODE;
    for (LIST_ENTRY *e = dd->surfaces.Flink; e != &dd->surfaces; e = e->Flink) {
        DDSURF *s = CONTAINING_RECORD(e, DDSURF, links);
        if (s->usage && (s->desc.ddsCaps.dwCaps & (DDSCAPS_VIDEOMEMORY | DDSCAPS_SYSTEMMEMORY)) ==
                                 DDSCAPS_VIDEOMEMORY)
            return DDERR_SURFACEBUSY;
    }
    DEVMODEW dm = {.dmSize = sizeof(dm), .dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL};
    dm.dmPelsWidth = width;
    dm.dmPelsHeight = height;
    dm.dmBitsPerPel = bpp;
    BOOL modex = FALSE;
    LONG result = ChangeDisplaySettingsExW(nullptr, &dm, nullptr, CDS_FULLSCREEN, nullptr);
    if (result != DISP_CHANGE_SUCCESSFUL && (dd->flags & DDF_MODEX) && bpp == 8 && width == 320 &&
        (height == 200 || height == 240)) {
        dm.dmPelsWidth = 640;
        dm.dmPelsHeight = 480;
        result = ChangeDisplaySettingsExW(nullptr, &dm, nullptr, CDS_FULLSCREEN, nullptr);
        modex = result == DISP_CHANGE_SUCCESSFUL;
    }
    if (result != DISP_CHANGE_SUCCESSFUL)
        return DDERR_UNSUPPORTED;
    dd->mode_width = width;
    dd->mode_height = height;
    dd->mode_bpp = bpp;
    dd->mode_refresh = refresh;
    dd->flags |= DDF_MODESET;
    if (!modex && width == 320)
        dd->flags &= ~DDF_MODEX;
    for (LIST_ENTRY *e = DdObjects.Flink; e != &DdObjects; e = e->Flink)
        DdLoseSurfaces(CONTAINING_RECORD(e, DDRAW, links));
    if ((dd->flags & DDF_EXCLUSIVE) && (dd->level & (DDSCL_FULLSCREEN | DDSCL_NOWINDOWCHANGES)) == DDSCL_FULLSCREEN)
        make_fullscreen(dd, dd->device);
    return DD_OK;
}

HRESULT DdRestoreDisplayMode(DDRAW *dd)
{
    if (g_exclusive && g_exclusive != dd)
        return DDERR_NOEXCLUSIVEMODE;
    if (!(dd->flags & DDF_MODESET))
        return DD_OK;
    if (ChangeDisplaySettingsW(nullptr, 0) != DISP_CHANGE_SUCCESSFUL)
        return DDERR_UNSUPPORTED;
    dd->flags &= ~DDF_MODESET;
    dd->mode_width = dd->mode_height = dd->mode_bpp = dd->mode_refresh = 0;
    for (LIST_ENTRY *e = DdObjects.Flink; e != &DdObjects; e = e->Flink)
        DdLoseSurfaces(CONTAINING_RECORD(e, DDRAW, links));
    RedrawWindow(nullptr, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    return DD_OK;
}

/* ------------------------------------------------------------------ vertical blank */

/* the display's refresh period on the performance counter clock, as dxgi waits for it. */
static LONGLONG refresh_period(LARGE_INTEGER *now)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(now);
    DDMODE mode;
    DdCurrentMode(&mode);
    return frequency.QuadPart / (mode.refresh > 1 ? mode.refresh : 60);
}

/* the blank starts after the active lines and lasts a 20th of them. */
static LONGLONG blank_start(LONGLONG period)
{
    DDMODE mode;
    DdCurrentMode(&mode);
    return period * mode.height / (mode.height + mode.height / 20);
}

static void wait_until(LONGLONG when)
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    if (when > now.QuadPart) {
        DWORD ms = (DWORD)((when - now.QuadPart) * 1000 / frequency.QuadPart);
        if (ms)
            Sleep(ms);
    }
    while (QueryPerformanceCounter(&now), now.QuadPart < when)
        YieldProcessor();
}

/* waits for the blank, returning at once when already in it, then for its end with BLOCKEND. */
HRESULT DdWaitForVerticalBlank(DWORD flags)
{
    LARGE_INTEGER now;
    LONGLONG period = refresh_period(&now);
    LONGLONG frame = now.QuadPart - now.QuadPart % period, start = blank_start(period);
    if (now.QuadPart - frame < start)
        wait_until(frame + start);
    if (flags & DDWAITVB_BLOCKEND)
        wait_until(frame + period);
    return DD_OK;
}

/* the beam's line, from the clock's position in the frame. there is none during the blank. */
HRESULT DdGetScanLine(DWORD *line)
{
    LARGE_INTEGER now;
    LONGLONG period = refresh_period(&now);
    DDMODE mode;
    DdCurrentMode(&mode);
    DWORD total = mode.height + mode.height / 20;
    DWORD at = (DWORD)((now.QuadPart % period) * total / period);
    if (at >= mode.height)
        return DDERR_VERTICALBLANKINPROGRESS;
    *line = at;
    return DD_OK;
}
