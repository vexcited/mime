/*
 * window device contexts, the paint cycle and presentation of top level
 * window surfaces to the host display.
 */
#include "win32k.h"

/* host channel. */

/* hypercall buffers: physically contiguous pages, serialized by the user lock. */
static MimeWinConfig *g_config;
static MimeWinPresent *g_present;
static MimeWinIcon *g_icon;
static PUCHAR g_icon_pixels;
static SIZE_T g_icon_capacity;

static BOOLEAN host_buffers(void)
{
    if (!g_config) {
        PUCHAR page = MmAllocateContiguousPages(2 * 4096);
        if (!page)
            return FALSE;
        g_config = (MimeWinConfig *)page;
        g_present = (MimeWinPresent *)(page + 4096);
        g_icon = (MimeWinIcon *)(page + 4096 + sizeof(*g_present));
    }
    return TRUE;
}

static ULONG window_id(WND *w)
{
    return (ULONG)(ULONG_PTR)WndHandle(w);
}

static ULONG64 hypercall(ULONG64 nr, ULONG64 a1)
{
    return HalMimeHypercall(nr, a1, 0, 0, 0, 0, 0, nullptr);
}

void HostWindowCreate(WND *w)
{
    ScreenSerial++;
    hypercall(MIME_HC_WIN_CREATE, window_id(w));
}

void HostWindowDestroy(WND *w)
{
    ScreenSerial++;
    hypercall(MIME_HC_WIN_DESTROY, window_id(w));
}

static MimeRect mime_rect(const RECT *r)
{
    return (MimeRect){r->left, r->top, r->right, r->bottom};
}

/*
 * maximized windows have no rounded corners. otherwise use DWMWA_WINDOW_CORNER_PREFERENCE or the frame default.
 * dwmapi stores the preference plus one in a window property.
 */
static uint32_t corner_radius(WND *w, const FRAME_METRICS *m)
{
    if (w->Style & WS_MAXIMIZE)
        return 0;
    switch ((ULONG_PTR)PropGetData(w, PropAtom(L"DwmCorner"))) {
    case 1 + 1: /* DWMWCP_DONOTROUND */
        return 0;
    case 2 + 1: /* DWMWCP_ROUND */
        return (uint32_t)W32MulDiv(8, (LONG)WndGetDpi(w), 96);
    case 3 + 1: /* DWMWCP_ROUNDSMALL */
        return (uint32_t)W32MulDiv(4, (LONG)WndGetDpi(w), 96);
    }
    return (uint32_t)m->CornerRadius;
}

static void host_icon(WND *w)
{
    HICON handles[] = {w->Icon, w->IconSmall, w->Class ? w->Class->Icon : nullptr,
                       w->Class ? w->Class->IconSmall : nullptr};
    SURFACE *surface = nullptr;
    HICON icon = nullptr;
    for (ULONG i = 0; i < RTL_NUMBER_OF(handles); i++) {
        surface = CursorIconImage(handles[i]);
        if (surface) {
            icon = handles[i];
            break;
        }
    }
    if (icon == w->HostIcon)
        return;
    MimeWinIcon *data = g_icon;
    memset(data, 0, sizeof(*data));
    data->window_id = window_id(w);
    if (surface) {
        SIZE_T size = (SIZE_T)surface->Width * surface->Height * 4;
        if (size > g_icon_capacity) {
            PHYSICAL_ADDRESS highest;
            highest.QuadPart = -1;
            PUCHAR pixels = MmAllocateContiguousMemory(size, highest);
            if (!pixels)
                return;
            if (g_icon_pixels)
                MmFreeContiguousMemory(g_icon_pixels);
            g_icon_pixels = pixels;
            g_icon_capacity = size;
        }
        data->width = (ULONG)surface->Width;
        data->height = (ULONG)surface->Height;
        data->stride = data->width * 4;
        for (LONG y = 0; y < surface->Height; y++)
            memcpy(g_icon_pixels + (SIZE_T)y * data->stride, surface->Bits + (SIZE_T)y * surface->Stride,
                   data->stride);
        data->pixels_phys = (ULONG64)MmGetPhysicalAddress(g_icon_pixels).QuadPart;
    }
    if (hypercall(MIME_HC_WIN_ICON, (ULONG64)MmGetPhysicalAddress(data).QuadPart) == MIME_HC_STATUS_OK)
        w->HostIcon = icon;
}

/* visible and mouse-sensitive bounds relative to the window. */
static void host_rects(WND *w, const FRAME_METRICS *m, RECT *visible, RECT *input)
{
    FrameGetVisibleRect(w, visible);
    *input = *visible;
    /* sizable frames receive mouse input over the invisible resize border, like DWM frames. */
    if ((w->Style & WS_THICKFRAME) && !(w->Style & (WS_MAXIMIZE | WS_MINIMIZE)) && m->VisibleInset)
        *input = (RECT){0, 0, w->WindowRect.right - w->WindowRect.left, w->WindowRect.bottom - w->WindowRect.top};
}

/* a window relative rectangle in physical pixels, the window's own when it is not stretched. */
static void host_pixels(const WND *w, RECT *r)
{
    r->left = WndToPhysical(w, r->left);
    r->top = WndToPhysical(w, r->top);
    r->right = WndToPhysical(w, r->right);
    r->bottom = WndToPhysical(w, r->bottom);
}

/* host bounds include the visible window and its resize border, in physical pixels. */
void HostPart(WND *w, RECT *part)
{
    FRAME_METRICS m;
    RECT visible, input;
    FrameGetMetrics(w->Style, w->ExStyle, WndFrameDpi(w), &m);
    host_rects(w, &m, &visible, &input);
    if (visible.right <= visible.left || visible.bottom <= visible.top)
        visible = (RECT){0, 0, w->WindowRect.right - w->WindowRect.left, w->WindowRect.bottom - w->WindowRect.top};
    *part = visible;
    if (input.right > input.left && input.bottom > input.top) {
        part->left = W32Min(part->left, input.left);
        part->top = W32Min(part->top, input.top);
        part->right = W32Max(part->right, input.right);
        part->bottom = W32Max(part->bottom, input.bottom);
    }
    host_pixels(w, part);
}

/* the 1 pixel border the compositor draws over the visible frame, 0xAARRGGBB, 0 for none. */
ULONG HostBorderColor(WND *w)
{
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, WndFrameDpi(w), &m);
    if (!m.Border || (w->Style & WS_MAXIMIZE))
        return 0;
    BOOLEAN dark = PropGetData(w, PropAtom(L"DwmDarkMode")) != nullptr;
    ULONG color, themed;
    COLORREF frame = MetricsSysColor(COLOR_WINDOWFRAME);
    if (MetricsHighContrast())
        color = 0xFF000000u | ((frame & 0xFF) << 16) | (frame & 0xFF00) | ((frame >> 16) & 0xFF);
    else if (FrameBorderColor(dark, (w->Flags & WNDF_ACTIVE_FRAME) != 0, &themed))
        color = themed;
    else
        color = dark ? ((w->Flags & WNDF_ACTIVE_FRAME) ? 0x664C4C4Cu : 0x404C4C4Cu)
                     : ((w->Flags & WNDF_ACTIVE_FRAME) ? 0x3b000000u : 0x26000000u);
    ULONG_PTR v = (ULONG_PTR)PropGetData(w, PropAtom(L"DwmBorderColor"));
    if (v) {
        ULONG custom = (ULONG)(v - 1);
        if (custom == 0xFFFFFFFEu)
            color = 0;
        else if (custom != 0xFFFFFFFFu)
            color = 0xFF000000u | ((custom & 0xFF) << 16) | (custom & 0xFF00) | ((custom >> 16) & 0xFF);
    }
    return color;
}

void HostConfigure(WND *w, BOOLEAN activate)
{
    ScreenSerial++;
    if (!(w->Flags & WNDF_HOST_WINDOW) || !host_buffers())
        return;
    if (activate)
        w->Flags |= WNDF_ACTIVATE_PENDING;
    if (w->Flags & WNDF_HOST_MOVING)
        return;
    /* a layered window shows up once it has attributes or contents from UpdateLayeredWindow. */
    BOOLEAN visible = WndIsVisible(w) && ((w->Flags & WNDF_PRESENTED) || (w->Style & WS_MINIMIZE)) &&
                      !(w->ExStyle & 0x80000000u) && (!(w->ExStyle & WS_EX_LAYERED) || w->LayerFlags);
    MimeWinConfig *c = g_config;
    memset(c, 0, offsetof(MimeWinConfig, title));
    c->window_id = window_id(w);
    c->state_request = w->HostStateRequest;
    if (visible)
        c->flags |= MIME_WIN_VISIBLE;
    if (w->ExStyle & WS_EX_TOPMOST)
        c->flags |= MIME_WIN_TOPMOST;
    if (w->Style & WS_MINIMIZE)
        c->flags |= MIME_WIN_MINIMIZED;
    if (w->Style & WS_MAXIMIZE)
        c->flags |= MIME_WIN_MAXIMIZED;
    if (w->Style & WS_THICKFRAME)
        c->flags |= MIME_WIN_RESIZABLE;
    if (w->Style & WS_MINIMIZEBOX)
        c->flags |= MIME_WIN_MINIMIZABLE;
    if (w->Style & WS_MAXIMIZEBOX)
        c->flags |= MIME_WIN_MAXIMIZABLE;
    if ((w->ExStyle & WS_EX_APPWINDOW) || (!w->Owner && !(w->ExStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))))
        c->flags |= MIME_WIN_APPWINDOW;
    /* a thin border, as on Godot fullscreen windows, does not imply a title bar. */
    BOOLEAN fullscreen = (w->Flags & WNDF_FULLSCREEN) ||
                         ((w->Style & WS_CAPTION) != WS_CAPTION && !(w->Style & (WS_THICKFRAME | WS_MINIMIZE)) &&
                          WndCoversMonitor(w));
    if (fullscreen)
        c->flags |= MIME_WIN_FULLSCREEN;
    if (visible && (w->Flags & WNDF_ACTIVATE_PENDING)) {
        c->flags |= MIME_WIN_ACTIVATE;
        w->Flags &= ~WNDF_ACTIVATE_PENDING;
    }
    if (FrameHasCaption(w->Style, w->ExStyle) || (w->Class && (w->Class->Style & CS_DROPSHADOW)) ||
        (w->Style & (WS_THICKFRAME | WS_DLGFRAME | WS_BORDER)))
        c->flags |= MIME_WIN_SHADOW;
    if (w->ExStyle & (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) {
        if (!FrameHasCaption(w->Style, w->ExStyle))
            c->flags |= MIME_WIN_TOOL;
    }
    if (w->ExStyle & WS_EX_TRANSPARENT)
        c->flags |= MIME_WIN_NOINPUT;
    RECT physical;
    WndPhysicalRect(w, &physical);
    c->rect = mime_rect(&physical);
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, WndFrameDpi(w), &m);
    RECT vis, input;
    host_rects(w, &m, &vis, &input);
    host_pixels(w, &vis);
    host_pixels(w, &input);
    c->visible = mime_rect(&vis);
    c->input = mime_rect(&input);
    c->corner_radius = (uint32_t)WndToPhysical(w, (LONG)corner_radius(w, &m));
    if (w->Flags & WNDF_ACTIVE_FRAME)
        c->flags |= MIME_WIN_ACTIVE;
    c->border_color = HostBorderColor(w);
    if (fullscreen) {
        c->corner_radius = 0;
        c->border_color = 0;
        c->flags &= ~MIME_WIN_SHADOW;
    }
    RECT drag[MIME_WIN_MAX_DRAG_RECTS];
    c->drag_count = FrameGetDragRects(w, drag, MIME_WIN_MAX_DRAG_RECTS);
    for (ULONG i = 0; i < c->drag_count; i++) {
        host_pixels(w, &drag[i]);
        c->drag[i] = mime_rect(&drag[i]);
    }
    c->owner_id = w->Owner ? window_id(w->Owner) : 0;
    c->dpi = WndHostDpi(w);
    /* the surface of a window that is not per monitor aware is drawn at the dpi of its awareness and stretched by the host. */
    if (w->Awareness < DPI_AWARE_PER_MONITOR && c->dpi != DpiScaleOf(w->Awareness))
        c->surface_dpi = DpiScaleOf(w->Awareness);
    /* constrain compositor resizing to application limits; apply a maximum only if one was set. */
    if ((w->Style & WS_THICKFRAME) && !(w->Style & WS_MINIMIZE)) {
        UINT dpi = WndGetDpi(w);
        POINT min = w->MinTrack.x ? w->MinTrack : (POINT){MetricsGetFor(SM_CXMINTRACK, dpi, w->Awareness),
                                                          MetricsGetFor(SM_CYMINTRACK, dpi, w->Awareness)};
        POINT max = w->MaxTrack;
        min = (POINT){WndToPhysical(w, min.x), WndToPhysical(w, min.y)};
        max = (POINT){WndToPhysical(w, max.x), WndToPhysical(w, max.y)};
        c->min_width = min.x;
        c->min_height = min.y;
        if (w->MaxTrack.x && w->MaxTrack.x < MetricsGetFor(SM_CXMAXTRACK, dpi, w->Awareness))
            c->max_width = max.x;
        if (w->MaxTrack.y && w->MaxTrack.y < MetricsGetFor(SM_CYMAXTRACK, dpi, w->Awareness))
            c->max_height = max.y;
    }
    if (w->Thread && w->Thread->Process) {
        W32PROCESS *process = w->Thread->Process;
        c->process_id = (ULONG)(ULONG_PTR)process->Pid;
        c->image_chars = process->ImageChars;
        memcpy(c->image_path, process->ImagePath, process->ImageChars * sizeof(WCHAR));
        c->image_path[process->ImageChars] = 0;
    }
    ULONG n = w->TextLength < 255 ? w->TextLength : 255;
    if (n)
        memcpy(c->title, w->Text, n * sizeof(WCHAR));
    c->title[n] = 0;
    c->title_chars = n;
    hypercall(MIME_HC_WIN_CONFIG, (ULONG64)MmGetPhysicalAddress(c).QuadPart);
    if ((c->flags & (MIME_WIN_VISIBLE | MIME_WIN_APPWINDOW)) == (MIME_WIN_VISIBLE | MIME_WIN_APPWINDOW))
        host_icon(w);
    else
        w->HostIcon = nullptr;
}

/* the compositor takes over the button during a move or resize; release it in the guest. */
BOOLEAN HostMoveSize(WND *w, ULONG edge)
{
    if (!(w->Flags & WNDF_HOST_WINDOW) || !host_buffers())
        return FALSE;
    if (HalMimeHypercall(MIME_HC_WIN_MOVESIZE, window_id(w), edge, 0, 0, 0, 0, nullptr) != MIME_HC_STATUS_OK)
        return FALSE;
    InputReleaseLeftButton();
    return TRUE;
}

void HostPresent(WND *w, const RECT *dirty)
{
    ScreenSerial++;
    /*
     * windows without a redirection bitmap show application-composed contents, never their GDI surface, unless
     * dcomp.dll composes them on the cpu into that surface for want of a gpu.
     */
    if ((w->ExStyle & WS_EX_NOREDIRECTIONBITMAP) && !PropGetData(w, MIME_PROP_COMPOSITION))
        return;
    SURFACE *s = LayerPrepare(w, dirty);
    if (!s || !host_buffers())
        return;
    MimeWinPresent *p = g_present;
    p->window_id = window_id(w);
    p->width = (uint32_t)s->Width;
    p->height = (uint32_t)s->Height;
    p->stride = (uint32_t)s->Stride;
    p->surface_phys = (uint64_t)MmGetPhysicalAddress(s->Bits).QuadPart;
    p->dirty = mime_rect(dirty);
    hypercall(MIME_HC_WIN_PRESENT, (ULONG64)MmGetPhysicalAddress(p).QuadPart);
}

BOOLEAN InputCursorHidden;

void HostSetCursor(ULONG cursor)
{
    static ULONG current = ~0u;
    if (cursor == current)
        return;
    current = cursor;
    CaptureCursorKind(cursor);
    hypercall(MIME_HC_WIN_CURSOR, cursor);
    BOOLEAN hidden = cursor == MIME_CURSOR_HIDDEN;
    if (hidden != InputCursorHidden) {
        InputCursorHidden = hidden;
        if (hidden)
            InputUpdatePointer();
        else
            InputCursorShown();
    }
}

void HostBeep(UINT type)
{
    hypercall(MIME_HC_BEEP, type);
}

BOOL NTAPI NtUserMessageBeep(UINT type)
{
    HostBeep(type);
    return TRUE;
}

/* presentation. */

void PaintPresent(WND *top)
{
    SURFACE *s = top ? top->Surface : nullptr;
    if (!s || !(top->Flags & WNDF_HOST_WINDOW) || RectEmpty(&s->Dirty))
        return;
    if (!WndIsVisible(top) || (top->Style & WS_MINIMIZE))
        return;
    /* draw a pending frame before the first present. */
    if (!(top->Flags & WNDF_PRESENTED) && (top->Flags & WNDF_FRAME_DIRTY) && !(top->LayerFlags & MIME_LAYER_UPDATE))
        return;
    RECT dirty = s->Dirty;
    s->Dirty = (RECT){0, 0, 0, 0};
    /* the border only changes when the program painted against the client edge. */
    RECT client = top->ClientRect;
    RectOffset(&client, -top->WindowRect.left, -top->WindowRect.top);
    if (dirty.left <= client.left || dirty.right >= client.right || dirty.top <= client.top ||
        dirty.bottom >= client.bottom) {
        FrameBleed(top);
        RECT vis;
        FrameGetVisibleRect(top, &vis);
        dirty.left = W32Min(dirty.left, vis.left);
        dirty.top = W32Min(dirty.top, vis.top);
        dirty.right = W32Max(dirty.right, vis.right);
        dirty.bottom = W32Max(dirty.bottom, vis.bottom);
    }
    HostPresent(top, &dirty);
    if (!(top->Flags & WNDF_PRESENTED)) {
        top->Flags |= WNDF_PRESENTED;
        HostConfigure(top, FALSE);
    }
}

void PaintFlushThread(W32THREAD *t)
{
    WND *desktop = WndDesktop();
    if (!desktop)
        return;
    for (WND *c = desktop->FirstChild; c; c = c->Next)
        if (c->Thread == t && c->Surface)
            PaintPresent(c);
}

/* a surface may still be selected by window DCs when its window is resized or destroyed. */
void PaintRetireSurface(SURFACE *s)
{
    if (!s)
        return;
    s->Window = nullptr;
    if (s->Header.SelectCount)
        s->Flags |= SURF_RETIRED;
    else
        SurfaceDestroy(s);
}

/* window dcs. */

HDC PaintGetDC(WND *w, HRGN clip, ULONG flags)
{
    DC *dc = DcCreate(DC_WINDOW | DC_CACHE);
    if (!dc)
        return nullptr;
    dc->Window = w;
    if (w && w != WndDesktop()) {
        dc->Dpi = WndGetDpi(w);
        dc->Awareness = w->Awareness;
    }
    WND *top = w ? WndTopLevel(w) : nullptr;
    SURFACE *s = top && top != WndDesktop() ? top->Surface : nullptr;
    if (!top || top == WndDesktop()) {
        /* screen and desktop DCs use virtual screen coordinates and are composed on read. */
        BOOLEAN virtual_screen = DpiVirtual(dc->Awareness);
        POINT origin;
        SURFACE *screen = virtual_screen ? ScreenVirtualSurface(dc->Awareness, &origin) : ScreenSurface();
        if (screen) {
            if (!virtual_screen)
                origin = ScreenOrigin();
            DcSetSurface(dc, screen);
            screen->Header.SelectCount++;
            dc->Origin = (POINT){-origin.x, -origin.y};
            if (virtual_screen)
                ScreenVirtualVisibleRegion(dc->Awareness, &dc->VisRgn);
            else
                ScreenVisibleRegion(&dc->VisRgn);
            return (HDC)dc->Header.Handle;
        }
    }
    if (!s) {
        /* hidden windows and the desktop get a DC that measures without drawing. */
        dc->DefaultBitmap = SurfaceCreate(1, 1, 32, 0);
        if (!dc->DefaultBitmap) {
            DcFree(dc);
            return nullptr;
        }
        DcSetSurface(dc, dc->DefaultBitmap);
        RgnClear(&dc->VisRgn);
        return (HDC)dc->Header.Handle;
    }
    DcSetSurface(dc, s);
    s->Header.SelectCount++;
    BOOLEAN client = !(flags & DCX_WINDOW);
    const RECT *origin = client ? &w->ClientRect : &w->WindowRect;
    dc->Origin = (POINT){origin->left - top->WindowRect.left, origin->top - top->WindowRect.top};
    WndComputeVisRgn(w, client, flags, &dc->VisRgn);
    if (clip && (flags & (DCX_INTERSECTRGN | DCX_EXCLUDERGN))) {
        GDI_REGION *r = (GDI_REGION *)GdiLookup(clip, GDI_TYPE_REGION);
        if (r) {
            REGION tmp;
            RgnInit(&tmp);
            RgnCopy(&tmp, &r->Region);
            RgnOffset(&tmp, -top->WindowRect.left, -top->WindowRect.top);
            RgnCombine(&dc->VisRgn, &dc->VisRgn, &tmp, (flags & DCX_INTERSECTRGN) ? RGN_AND : RGN_DIFF);
            RgnFree(&tmp);
        }
    } else if (clip == (HRGN)1 && (flags & DCX_INTERSECTRGN)) {
        /* WM_NCPAINT passes 1 for the whole window. */
    }
    if (flags & DCX_INTERSECTUPDATE) {
        REGION upd;
        RgnInit(&upd);
        RgnCopy(&upd, &w->Update);
        RgnOffset(&upd, -top->WindowRect.left, -top->WindowRect.top);
        RgnCombine(&dc->VisRgn, &dc->VisRgn, &upd, RGN_AND);
        RgnFree(&upd);
    }
    return (HDC)dc->Header.Handle;
}

/* a monitor DC exposes the screen through its display rectangle, with the monitor corner at DC origin 0, 0. */
HDC PaintGetMonitorDC(ULONG index)
{
    MIME_MONITOR_INFO m;
    SURFACE *screen = ScreenSurface();
    RECT visible;
    if (!screen || !MonitorGet(index, &m) || !ScreenMonitorRect(index, &visible))
        return nullptr;
    DC *dc = DcCreate(DC_WINDOW | DC_CACHE);
    if (!dc)
        return nullptr;
    /* a program that is not per monitor aware has the monitor in its own pixels. */
    if (DpiVirtual(dc->Awareness)) {
        POINT origin;
        RECT monitors[MIME_USER_MAX_MONITORS];
        SURFACE *virtual_screen = ScreenVirtualSurface(dc->Awareness, &origin);
        ULONG count = DpiVirtualMonitors(dc->Awareness, monitors);
        if (virtual_screen && index < count) {
            visible = monitors[index];
            RectOffset(&visible, -origin.x, -origin.y);
            screen = virtual_screen;
        }
    }
    dc->Window = WndDesktop();
    dc->Dpi = dc->Awareness == DPI_AWARE_UNAWARE ? 96 : m.Dpi ? m.Dpi : 96;
    dc->OfMonitor = TRUE;
    dc->Monitor = index;
    DcSetSurface(dc, screen);
    screen->Header.SelectCount++;
    dc->Origin = (POINT){visible.left, visible.top};
    RgnSetRect(&dc->VisRgn, &visible);
    return (HDC)dc->Header.Handle;
}

void PaintReleaseDC(DC *dc)
{
    SURFACE *s = dc->DefaultBitmap ? nullptr : dc->Surface;
    WND *top = dc->Window ? WndTopLevel(dc->Window) : nullptr;
    DcFree(dc);
    if (s && s->Header.SelectCount && --s->Header.SelectCount == 0 && (s->Flags & SURF_RETIRED))
        SurfaceDestroy(s);
    (void)top;
}

HDC NTAPI NtUserGetDCEx(HWND hwnd, HRGN clip, ULONG flags)
{
    UserEnter();
    WND *w = hwnd ? WndLookup(hwnd) : WndDesktop();
    HDC dc = w ? PaintGetDC(w, clip, flags) : nullptr;
    UserLeave();
    return dc;
}

HDC NTAPI NtUserGetDC(HWND hwnd)
{
    return NtUserGetDCEx(hwnd, nullptr, DCX_USESTYLE);
}

HDC NTAPI NtUserGetWindowDC(HWND hwnd)
{
    return NtUserGetDCEx(hwnd, nullptr, DCX_WINDOW | DCX_USESTYLE);
}

int NTAPI NtUserReleaseDC(HWND hwnd, HDC hdc)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    int ok = 0;
    if (dc && (dc->Flags & DC_WINDOW) && !(dc->Flags & DC_PAINT)) {
        WND *top = dc->Window ? WndTopLevel(dc->Window) : nullptr;
        PaintReleaseDC(dc);
        /*
         * present drawing outside the paint cycle from any thread. draw a pending frame even if the thread never
         * pumps messages.
         */
        if (top && !(top->Flags & WNDF_PRESENTED) && (top->Flags & WNDF_FRAME_DIRTY) && top->Thread == W32GetThread()) {
            UserReference(&top->Header);
            top->Flags &= ~WNDF_FRAME_DIRTY;
            MsgSend(top, WM_NCPAINT, 1, 0);
            BOOLEAN gone = top->Header.Destroyed;
            UserDereference(&top->Header);
            if (gone)
                top = nullptr;
        }
        if (top)
            PaintPresent(top);
        ok = 1;
    }
    UserLeave();
    (void)hwnd;
    return ok;
}

/* the paint cycle. */

HDC PaintBegin(WND *w, PAINTSTRUCT *ps)
{
    memset(ps, 0, sizeof(*ps));
    UserReference(&w->Header);
    if (w->Flags & WNDF_FRAME_DIRTY) {
        w->Flags &= ~WNDF_FRAME_DIRTY;
        MsgSend(w, WM_NCPAINT, 1, 0);
    }
    if (w->Header.Destroyed) {
        UserDereference(&w->Header);
        return nullptr;
    }
    RECT box = {0, 0, 0, 0};
    if (!RgnIsEmpty(&w->Update)) {
        box = w->Update.Extents;
        RectOffset(&box, -w->ClientRect.left, -w->ClientRect.top);
    }
    HDC hdc = PaintGetDC(w, nullptr, DCX_INTERSECTUPDATE | DCX_USESTYLE);
    BOOLEAN erase = (w->Flags & WNDF_ERASE_PENDING) != 0;
    WndValidate(w, nullptr);
    DC *dc = DcLookup(hdc);
    if (dc)
        dc->Flags |= DC_PAINT;
    ps->hdc = hdc;
    ps->rcPaint = box;
    ps->fErase = TRUE;
    if (erase && hdc) {
        LRESULT r = MsgSend(w, WM_ERASEBKGND, (WPARAM)hdc, 0);
        ps->fErase = r == 0;
    } else {
        ps->fErase = FALSE;
    }
    UserDereference(&w->Header);
    return hdc;
}

void PaintEnd(WND *w, const PAINTSTRUCT *ps)
{
    DC *dc = DcLookup(ps->hdc);
    WND *top = WndTopLevel(w);
    if (dc && (dc->Flags & DC_PAINT) && dc->Window == w)
        PaintReleaseDC(dc);
    /* children still waiting for paint will complete the picture before the next present. */
    if (top && top->Thread == W32GetThread() && !WndFindNeedsPaint(top->Thread, WndHandle(top)))
        PaintPresent(top);
}

HDC NTAPI NtUserBeginPaint(HWND hwnd, PAINTSTRUCT *user_ps)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    HDC hdc = nullptr;
    if (w && w->Thread == W32GetThread()) {
        PAINTSTRUCT ps;
        hdc = PaintBegin(w, &ps);
        if (hdc && !NT_SUCCESS(MimeCopyToUser(user_ps, &ps, sizeof(ps)))) {
            DC *dc = DcLookup(hdc);
            if (dc)
                PaintReleaseDC(dc);
            hdc = nullptr;
        }
    }
    UserLeave();
    return hdc;
}

BOOL NTAPI NtUserEndPaint(HWND hwnd, const PAINTSTRUCT *user_ps)
{
    PAINTSTRUCT ps;
    if (!NT_SUCCESS(MimeCopyFromUser(&ps, user_ps, sizeof(ps))))
        return FALSE;
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (w)
        PaintEnd(w, &ps);
    UserLeave();
    return TRUE;
}

/* invalidation services. */

static void client_rect_to_screen(WND *w, RECT *r)
{
    RectOffset(r, w->ClientRect.left, w->ClientRect.top);
}

static void update_now(WND *w, BOOLEAN children)
{
    W32THREAD *t = W32GetThread();
    for (int guard = 0; guard < 256; guard++) {
        WND *p = WndFindNeedsPaint(t, WndHandle(w));
        if (!p || (!children && p != w))
            break;
        UserReference(&p->Header);
        if (RgnIsEmpty(&p->Update) && !(p->Flags & WNDF_INTERNAL_PAINT)) {
            /* only the frame is dirty. */
            p->Flags &= ~WNDF_FRAME_DIRTY;
            MsgSend(p, WM_NCPAINT, 1, 0);
        } else {
            p->Flags &= ~WNDF_INTERNAL_PAINT;
            MsgSend(p, WM_PAINT, 0, 0);
            /* force validation to prevent an endless paint loop. */
            if (!p->Header.Destroyed && !RgnIsEmpty(&p->Update))
                WndValidate(p, nullptr);
        }
        UserDereference(&p->Header);
    }
    WND *top = WndTopLevel(w);
    if (top && top->Thread == t)
        PaintPresent(top);
}

BOOL NTAPI NtUserRedrawWindow(HWND hwnd, const RECT *user_rect, HRGN region, UINT flags)
{
    RECT rect;
    BOOLEAN have_rect = FALSE;
    if (user_rect) {
        if (!NT_SUCCESS(MimeCopyFromUser(&rect, user_rect, sizeof(rect))))
            return FALSE;
        have_rect = TRUE;
    }
    UserEnter();
    WND *w = hwnd ? WndLookup(hwnd) : nullptr;
    if (!hwnd) {
        WND *desktop = WndDesktop();
        for (WND *c = desktop ? desktop->FirstChild : nullptr; c; c = c->Next)
            if (c->Style & WS_VISIBLE)
                WndInvalidateRegion(c, nullptr, (flags | RDW_ALLCHILDREN | RDW_FRAME) & ~(RDW_UPDATENOW | RDW_ERASENOW));
        UserLeave();
        return TRUE;
    }
    if (!w) {
        UserLeave();
        return FALSE;
    }
    REGION rgn;
    RgnInit(&rgn);
    BOOLEAN whole = FALSE;
    if (region) {
        GDI_REGION *r = (GDI_REGION *)GdiLookup(region, GDI_TYPE_REGION);
        if (r) {
            RgnCopy(&rgn, &r->Region);
            RgnOffset(&rgn, w->ClientRect.left, w->ClientRect.top);
        }
    } else if (have_rect) {
        client_rect_to_screen(w, &rect);
        RgnSetRect(&rgn, &rect);
    } else {
        whole = TRUE;
    }
    if (flags & (RDW_INVALIDATE | RDW_VALIDATE | RDW_INTERNALPAINT | RDW_NOINTERNALPAINT)) {
        if (whole && !(flags & RDW_FRAME)) {
            RECT c = w->ClientRect;
            RgnSetRect(&rgn, &c);
            whole = FALSE;
        }
        WndInvalidateRegion(w, whole ? nullptr : &rgn, flags);
    }
    RgnFree(&rgn);
    if ((flags & (RDW_UPDATENOW | RDW_ERASENOW)) && w->Thread == W32GetThread())
        update_now(w, (flags & RDW_ALLCHILDREN) != 0 || !(flags & RDW_NOCHILDREN));
    UserLeave();
    return TRUE;
}

BOOL NTAPI NtUserInvalidateRect(HWND hwnd, const RECT *rect, BOOL erase)
{
    return NtUserRedrawWindow(hwnd, rect, nullptr, RDW_INVALIDATE | (erase ? RDW_ERASE : 0));
}

BOOL NTAPI NtUserValidateRect(HWND hwnd, const RECT *rect)
{
    return NtUserRedrawWindow(hwnd, rect, nullptr, RDW_VALIDATE | RDW_NOCHILDREN);
}

BOOL NTAPI NtUserGetUpdateRect(HWND hwnd, LPRECT user_rect, BOOL erase)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL nonempty = FALSE;
    if (w) {
        RECT box = {0, 0, 0, 0};
        if (!RgnIsEmpty(&w->Update)) {
            box = w->Update.Extents;
            RectOffset(&box, -w->ClientRect.left, -w->ClientRect.top);
            nonempty = TRUE;
        }
        if (user_rect)
            MimeCopyToUser(user_rect, &box, sizeof(box));
        if (erase && nonempty && (w->Flags & WNDF_ERASE_PENDING) && w->Thread == W32GetThread()) {
            HDC hdc = PaintGetDC(w, nullptr, DCX_INTERSECTUPDATE | DCX_USESTYLE);
            w->Flags &= ~WNDF_ERASE_PENDING;
            if (hdc) {
                MsgSend(w, WM_ERASEBKGND, (WPARAM)hdc, 0);
                DC *dc = DcLookup(hdc);
                if (dc)
                    PaintReleaseDC(dc);
            }
        }
    }
    UserLeave();
    return nonempty;
}
