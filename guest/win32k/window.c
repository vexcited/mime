/*
 * window tree, lifetime, positioning, visibility and focus. rectangles use screen coordinates. each visible top level
 * window owns a surface; children draw into it through their visible regions.
 */
#include "win32k.h"

static WND *g_desktop;
static ULONG g_cascade;

WND *WndLookup(HWND hwnd)
{
    if (!hwnd)
        return nullptr;
    WND *w = (WND *)UserLookup(hwnd, USER_TYPE_WINDOW);
    if (!w && g_desktop && (hwnd == WndHandle(g_desktop)))
        return g_desktop;
    /* jobs can restrict processes to windows belonging to the job. */
    if (w && w->Thread && w->Thread->Process && w->Thread->Process->Process != PsGetCurrentProcess() &&
        !MimeProcessMayUseUserHandles(PsGetCurrentProcess(), w->Thread->Process->Process))
        return nullptr;
    return w;
}

WND *WndDesktop(void)
{
    if (g_desktop)
        return g_desktop;
    WND *w = W32Alloc(sizeof(WND));
    if (!w)
        return nullptr;
    w->Class = ClassFind(nullptr, nullptr, 32769, nullptr);
    w->Style = WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    w->WindowRect = (RECT){0, 0, W32Display.Width, W32Display.Height};
    w->ClientRect = w->WindowRect;
    w->Dpi = W32SystemDpi();
    w->Awareness = DPI_AWARE_SYSTEM;
    RgnInit(&w->Update);
    if (w->Class) {
        w->Class->Windows++;
        w->Extra = W32Alloc((SIZE_T)w->Class->WindowExtra);
        w->ExtraSize = (ULONG)w->Class->WindowExtra;
    }
    if (!UserCreateHandle(&w->Header, USER_TYPE_WINDOW)) {
        W32Free(w);
        return nullptr;
    }
    g_desktop = w;
    return w;
}

/* the dpi of the monitor a window is mostly on. */
UINT WndMonitorDpi(const WND *w)
{
    RECT physical;
    WndPhysicalRect(WndTopLevel((WND *)w) ? WndTopLevel((WND *)w) : w, &physical);
    return MonitorDpiOfRect(&physical);
}

/* a distance in the pixels of a window in physical ones, at the scale of the monitor the window is on. */
LONG WndToPhysical(const WND *w, LONG value)
{
    if (w->Awareness >= DPI_AWARE_PER_MONITOR)
        return value;
    return DpiLength(value, w->Awareness, DPI_PHYSICAL, WndMonitorDpi(w));
}

/* the window rectangle in physical pixels. */
void WndPhysicalRect(const WND *w, RECT *out)
{
    *out = w->WindowRect;
    DpiRectConvert(out, w->Awareness, DPI_PHYSICAL);
}

BOOLEAN WndCoversMonitor(WND *w)
{
    RECT physical;
    WndPhysicalRect(w, &physical);
    return MonitorCoveredBy(&physical);
}

/* coordinates between the awareness of a window and the one of the calling thread. */
static void rect_to_caller(const WND *w, RECT *r)
{
    DpiRectConvert(r, w->Awareness, DpiCurrentAwareness());
}

static void point_to_caller(const WND *w, POINT *p)
{
    DpiPointConvert(p, w->Awareness, DpiCurrentAwareness());
}

static void point_from_caller(const WND *w, POINT *p)
{
    DpiPointConvert(p, DpiCurrentAwareness(), w->Awareness);
}

/* a distance in the pixels of the calling thread and one of the window, which depend on the monitor of per monitor ones. */
static UINT scale_dpi(const WND *w, ULONG other)
{
    return w->Awareness != other && (w->Awareness >= DPI_AWARE_PER_MONITOR || other >= DPI_AWARE_PER_MONITOR)
               ? WndMonitorDpi(w)
               : 96;
}

static void size_from_caller(const WND *w, POINT *p)
{
    UINT dpi = scale_dpi(w, DpiCurrentAwareness());
    p->x = DpiLength(p->x, DpiCurrentAwareness(), w->Awareness, dpi);
    p->y = DpiLength(p->y, DpiCurrentAwareness(), w->Awareness, dpi);
}

static void size_to_caller(const WND *w, POINT *p)
{
    UINT dpi = scale_dpi(w, DpiCurrentAwareness());
    p->x = DpiLength(p->x, w->Awareness, DpiCurrentAwareness(), dpi);
    p->y = DpiLength(p->y, w->Awareness, DpiCurrentAwareness(), dpi);
}

BOOLEAN WndIsTopLevel(WND *w)
{
    return w && w->Parent && w->Parent == g_desktop;
}

WND *WndTopLevel(WND *w)
{
    while (w && w->Parent && w->Parent != g_desktop)
        w = w->Parent;
    return w;
}

BOOLEAN WndIsVisible(WND *w)
{
    for (; w && w != g_desktop; w = w->Parent)
        if (!(w->Style & WS_VISIBLE))
            return FALSE;
    return w != nullptr;
}

BOOLEAN WndIsChildOf(WND *child, WND *parent)
{
    for (WND *w = child ? child->Parent : nullptr; w; w = w->Parent)
        if (w == parent)
            return TRUE;
    return FALSE;
}

/* the dpi a window works at: the one its program was last told of while the changes are deferred. */
UINT WndGetDpi(WND *w)
{
    if (w && w->DeferredDpi)
        return w->DeferredDpi;
    return w && w->Dpi ? w->Dpi : W32SystemDpi();
}

/* the dpi a frame is drawn at. a per monitor window keeps the one of the system unless it scales its frame itself. */
UINT WndFrameDpi(WND *w)
{
    WND *top = WndTopLevel(w);
    if (top && top->Awareness == DPI_AWARE_PER_MONITOR && !(top->Flags & WNDF_NCDPI))
        return W32SystemDpi();
    return WndGetDpi(w);
}

/* tree links. */

static void unlink_window(WND *w)
{
    WND *p = w->Parent;
    if (!p)
        return;
    if (w->Prev)
        w->Prev->Next = w->Next;
    else
        p->FirstChild = w->Next;
    if (w->Next)
        w->Next->Prev = w->Prev;
    else
        p->LastChild = w->Prev;
    w->Next = w->Prev = nullptr;
}

static void link_after(WND *parent, WND *w, WND *above)
{
    w->Parent = parent;
    w->Prev = above;
    w->Next = above ? above->Next : parent->FirstChild;
    if (w->Next)
        w->Next->Prev = w;
    else
        parent->LastChild = w;
    if (above)
        above->Next = w;
    else
        parent->FirstChild = w;
}

/* places w below `after` in its parent, honoring the topmost band of top level windows. */
static void link_window(WND *parent, WND *w, HWND after)
{
    WND *above = nullptr;
    BOOLEAN topmost_band = parent == g_desktop;
    if (after == HWND_BOTTOM) {
        above = parent->LastChild;
        if (topmost_band && !(w->ExStyle & WS_EX_TOPMOST))
            ; /* the bottom of the whole list is fine for a normal window. */
        else if (topmost_band) {
            /* topmost windows stay above every normal window. */
            above = nullptr;
            for (WND *c = parent->FirstChild; c && (c->ExStyle & WS_EX_TOPMOST); c = c->Next)
                above = c;
        }
    } else if (after == HWND_TOP || after == HWND_TOPMOST || after == HWND_NOTOPMOST || !after) {
        above = nullptr;
        if (topmost_band && !(w->ExStyle & WS_EX_TOPMOST))
            for (WND *c = parent->FirstChild; c && (c->ExStyle & WS_EX_TOPMOST); c = c->Next)
                above = c;
    } else {
        WND *a = WndLookup(after);
        if (a && a->Parent == parent && a != w)
            above = a;
        else
            above = nullptr;
    }
    if (above == w)
        above = w->Prev;
    link_after(parent, w, above);
}

/* owned windows stay above their owner. */
static void raise_owned(WND *owner)
{
    for (WND *c = g_desktop->LastChild; c;) {
        WND *prev = c->Prev;
        if (c->Owner == owner && c->Prev != nullptr) {
            WND *o = owner;
            BOOLEAN below = FALSE;
            for (WND *x = owner->Next; x; x = x->Next)
                if (x == c)
                    below = TRUE;
            if (below) {
                unlink_window(c);
                link_after(g_desktop, c, o->Prev);
            }
        }
        c = prev;
    }
}

static void offset_tree(WND *w, LONG dx, LONG dy)
{
    RectOffset(&w->WindowRect, dx, dy);
    RectOffset(&w->ClientRect, dx, dy);
    RgnOffset(&w->Update, dx, dy);
    for (WND *c = w->FirstChild; c; c = c->Next)
        offset_tree(c, dx, dy);
}

/*
 * UpdateLayeredWindow moves and sizes the rectangles of the window and its client area by the same distances and
 * sends nothing: no WM_WINDOWPOSCHANGING, no WM_NCCALCSIZE, so a program may call it from a thread that does not own
 * the window while the owner is waiting for it. children move with the window and do not change size.
 * returns whether anything changed.
 */
BOOLEAN WndLayerRects(WND *w, const POINT *pos, const SIZE *size)
{
    LONG dx = pos ? pos->x - w->WindowRect.left : 0, dy = pos ? pos->y - w->WindowRect.top : 0;
    LONG dw = size ? size->cx - (w->WindowRect.right - w->WindowRect.left) : 0;
    LONG dh = size ? size->cy - (w->WindowRect.bottom - w->WindowRect.top) : 0;
    if (!dx && !dy && !dw && !dh)
        return FALSE;
    w->WindowRect.left += dx;
    w->WindowRect.top += dy;
    w->WindowRect.right += dx + dw;
    w->WindowRect.bottom += dy + dh;
    w->ClientRect.left += dx;
    w->ClientRect.top += dy;
    w->ClientRect.right += dx + dw;
    w->ClientRect.bottom += dy + dh;
    if (dx || dy)
        for (WND *c = w->FirstChild; c; c = c->Next)
            offset_tree(c, dx, dy);
    return TRUE;
}

/* the client origin of a parent in the pixels of a child, which may be of another awareness than its parent. */
static POINT parent_origin(const WND *parent, ULONG awareness)
{
    POINT origin = {parent->ClientRect.left, parent->ClientRect.top};
    DpiPointConvert(&origin, parent->Awareness, awareness);
    return origin;
}

/* screen rectangle to the coordinates used for positioning: parent client, or screen for top level windows. */
static void screen_to_parent(WND *w, RECT *r)
{
    if (w->Parent && w->Parent != g_desktop) {
        POINT origin = parent_origin(w->Parent, w->Awareness);
        RectOffset(r, -origin.x, -origin.y);
    }
}

static void parent_to_screen(WND *parent, ULONG awareness, RECT *r)
{
    if (parent && parent != g_desktop) {
        POINT origin = parent_origin(parent, awareness);
        RectOffset(r, origin.x, origin.y);
    }
}

/* text. */

void WndSetText(WND *w, PCWSTR text, ULONG chars)
{
    W32Free(w->Text);
    w->Text = nullptr;
    w->TextLength = 0;
    if (text) {
        w->Text = W32StrDup(text, chars);
        if (w->Text)
            w->TextLength = chars;
    }
    if (FrameHasCaption(w->Style, w->ExStyle)) {
        w->Flags |= WNDF_FRAME_DIRTY;
        if (w->Thread)
            QueueWake(w->Thread, QS_PAINT);
    }
    if (WndIsTopLevel(w))
        WndUpdateHostConfig(w, FALSE);
}

/* visible regions. */

static void subtract_siblings_above(WND *w, REGION *rgn)
{
    for (WND *s = w->Prev; s; s = s->Prev)
        if ((s->Style & WS_VISIBLE))
            RgnCombineRect(rgn, rgn, &s->WindowRect, RGN_DIFF);
}

void WndComputeVisRgn(WND *w, BOOLEAN client, ULONG dcx_flags, REGION *out)
{
    RgnClear(out);
    if (!w || w == g_desktop || !WndIsVisible(w))
        return;
    WND *top = WndTopLevel(w);
    RgnSetRect(out, client ? &w->ClientRect : &w->WindowRect);
    BOOLEAN clip_siblings = (dcx_flags & DCX_CLIPSIBLINGS) || (w->Style & WS_CLIPSIBLINGS);
    for (WND *c = w; c != top; c = c->Parent) {
        WND *p = c->Parent;
        RgnCombineRect(out, out, &p->ClientRect, RGN_AND);
        if (c == w ? clip_siblings : (c->Style & WS_CLIPSIBLINGS) != 0)
            subtract_siblings_above(c, out);
    }
    RgnCombineRect(out, out, &top->WindowRect, RGN_AND);
    if ((dcx_flags & DCX_CLIPCHILDREN) || (w->Style & WS_CLIPCHILDREN))
        for (WND *c = w->FirstChild; c; c = c->Next)
            if (c->Style & WS_VISIBLE)
                RgnCombineRect(out, out, &c->WindowRect, RGN_DIFF);
    RgnOffset(out, -top->WindowRect.left, -top->WindowRect.top);
    /* the surface may lag behind the window when it could not grow; never draw past it. */
    RECT surface = {0, 0, top->Surface ? top->Surface->Width : 0, top->Surface ? top->Surface->Height : 0};
    RgnCombineRect(out, out, &surface, RGN_AND);
}

/* invalidation. */

static void wake_paint(WND *w)
{
    if (w->Thread)
        QueueWake(w->Thread, QS_PAINT);
}

/* screen_rgn NULL invalidates the whole window. flags are RDW_*. */
void WndInvalidateRegion(WND *w, const REGION *screen_rgn, ULONG flags)
{
    if (!w || w == g_desktop || (w->Flags & WNDF_DESTROYING) || !WndIsVisible(w))
        return;
    REGION rgn;
    RgnInit(&rgn);
    if (screen_rgn) {
        RgnCopy(&rgn, screen_rgn);
        RgnCombineRect(&rgn, &rgn, &w->WindowRect, RGN_AND);
    } else {
        RgnSetRect(&rgn, &w->WindowRect);
    }
    if (RgnIsEmpty(&rgn)) {
        RgnFree(&rgn);
        return;
    }
    if (flags & RDW_INVALIDATE) {
        if (flags & RDW_FRAME) {
            /* touches the non client area unless it lies entirely in the client. */
            REGION nc;
            RgnInit(&nc);
            RgnCombineRect(&nc, &rgn, &w->ClientRect, RGN_DIFF);
            if (!RgnIsEmpty(&nc))
                w->Flags |= WNDF_FRAME_DIRTY;
            RgnFree(&nc);
        }
        REGION client;
        RgnInit(&client);
        RgnCombineRect(&client, &rgn, &w->ClientRect, RGN_AND);
        if (!RgnIsEmpty(&client)) {
            RgnCombine(&w->Update, &w->Update, &client, RGN_OR);
            if (flags & RDW_ERASE)
                w->Flags |= WNDF_NEEDS_ERASE | WNDF_ERASE_PENDING;
        }
        RgnFree(&client);
        if (flags & RDW_INTERNALPAINT)
            w->Flags |= WNDF_INTERNAL_PAINT;
        wake_paint(w);
    } else if (flags & RDW_VALIDATE) {
        RgnCombine(&w->Update, &w->Update, &rgn, RGN_DIFF);
        if (flags & RDW_NOFRAME)
            ;
        else if (flags & RDW_FRAME)
            w->Flags &= ~WNDF_FRAME_DIRTY;
        if (flags & RDW_NOERASE)
            w->Flags &= ~(WNDF_NEEDS_ERASE | WNDF_ERASE_PENDING);
        if (flags & RDW_NOINTERNALPAINT)
            w->Flags &= ~WNDF_INTERNAL_PAINT;
        if (RgnIsEmpty(&w->Update))
            w->Flags &= ~(WNDF_NEEDS_ERASE | WNDF_ERASE_PENDING);
    } else if (flags & RDW_INTERNALPAINT) {
        w->Flags |= WNDF_INTERNAL_PAINT;
        wake_paint(w);
    }
    BOOLEAN children = (flags & RDW_ALLCHILDREN) || (!(flags & RDW_NOCHILDREN) && !(w->Style & WS_CLIPCHILDREN));
    if (children)
        for (WND *c = w->FirstChild; c; c = c->Next)
            if (c->Style & WS_VISIBLE)
                WndInvalidateRegion(c, &rgn, flags | RDW_FRAME);
    RgnFree(&rgn);
}

void WndInvalidate(WND *w, const RECT *client_rect, BOOLEAN erase, BOOLEAN children)
{
    if (!w)
        return;
    RECT r = client_rect ? *client_rect : (RECT){0, 0, w->ClientRect.right - w->ClientRect.left,
                                                 w->ClientRect.bottom - w->ClientRect.top};
    RectOffset(&r, w->ClientRect.left, w->ClientRect.top);
    REGION rgn;
    RgnInit(&rgn);
    RgnSetRect(&rgn, &r);
    WndInvalidateRegion(w, &rgn, RDW_INVALIDATE | (erase ? RDW_ERASE : 0) | (children ? RDW_ALLCHILDREN : 0));
    RgnFree(&rgn);
}

void WndValidate(WND *w, const RECT *client_rect)
{
    if (!client_rect) {
        RgnClear(&w->Update);
        w->Flags &= ~(WNDF_NEEDS_ERASE | WNDF_ERASE_PENDING | WNDF_INTERNAL_PAINT);
        return;
    }
    RECT r = *client_rect;
    RectOffset(&r, w->ClientRect.left, w->ClientRect.top);
    RgnCombineRect(&w->Update, &w->Update, &r, RGN_DIFF);
    if (RgnIsEmpty(&w->Update))
        w->Flags &= ~(WNDF_NEEDS_ERASE | WNDF_ERASE_PENDING);
}

static BOOLEAN needs_paint(WND *w)
{
    if (w->LayerFlags & MIME_LAYER_UPDATE)
        return FALSE;
    return !RgnIsEmpty(&w->Update) || (w->Flags & (WNDF_FRAME_DIRTY | WNDF_INTERNAL_PAINT));
}

static WND *find_paint(WND *w, W32THREAD *t)
{
    for (; w; w = w->Next) {
        if (!(w->Style & WS_VISIBLE) || (w->Flags & WNDF_DESTROYING))
            continue;
        if (w->Thread == t && needs_paint(w))
            return w;
        WND *c = find_paint(w->FirstChild, t);
        if (c)
            return c;
    }
    return nullptr;
}

WND *WndFindNeedsPaint(W32THREAD *t, HWND filter)
{
    if (!g_desktop)
        return nullptr;
    if (filter) {
        WND *f = WndLookup(filter);
        if (!f || !WndIsVisible(f))
            return nullptr;
        if (f->Thread == t && needs_paint(f))
            return f;
        return find_paint(f->FirstChild, t);
    }
    return find_paint(g_desktop->FirstChild, t);
}

/* host configuration. */

void WndUpdateHostConfig(WND *w, BOOLEAN activate)
{
    if (!WndIsTopLevel(w) || !(w->Flags & WNDF_HOST_WINDOW))
        return;
    HostConfigure(w, activate);
}

/* top level windows render into a surface the size of their window rectangle. */
void WndEnsureSurface(WND *w)
{
    if (!WndIsTopLevel(w) || w->Parent == nullptr || (w->ExStyle & 0x80000000u))
        return;
    LONG width = w->WindowRect.right - w->WindowRect.left;
    LONG height = w->WindowRect.bottom - w->WindowRect.top;
    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;
    if (w->Surface && w->Surface->Width == width && w->Surface->Height == height)
        return;
    SURFACE *s = SurfaceCreate(width, height, 32, SURF_DEVICE | SURF_CONTIGUOUS | SURF_TOPDOWN);
    if (!s)
        return;
    s->Window = w;
    LONG copy_width = 0, copy_height = 0;
    if (w->Surface) {
        copy_width = W32Min(width, w->Surface->Width);
        copy_height = W32Min(height, w->Surface->Height);
        for (LONG y = 0; y < copy_height; y++)
            memcpy(s->Bits + (SIZE_T)y * s->Stride, w->Surface->Bits + (SIZE_T)y * w->Surface->Stride,
                   (SIZE_T)copy_width * 4);
        PaintRetireSurface(w->Surface);
    }
    /* the host blends by alpha, so what was never painted is black as on Windows and not see-through. */
    if (!(w->ExStyle & WS_EX_LAYERED))
        for (LONG y = 0; y < height; y++) {
            PULONG row = (PULONG)(s->Bits + (SIZE_T)y * s->Stride);
            for (LONG x = y < copy_height ? copy_width : 0; x < width; x++)
                row[x] = 0xFF000000u;
        }
    w->Surface = s;
    s->Dirty = (RECT){0, 0, width, height};
    FrameSurfaceAlpha(w);
    if (!(w->Flags & WNDF_HOST_WINDOW)) {
        HostWindowCreate(w);
        w->Flags |= WNDF_HOST_WINDOW;
    }
}

/* messages helpers. */

static BOOLEAN alive(WND *w)
{
    return w && !w->Header.Destroyed;
}

static LRESULT send_kernel(WND *w, UINT msg, WPARAM wparam, PVOID data, ULONG kind, ULONG size)
{
    return MsgSendKernel(w, msg, wparam, (LPARAM)data, kind, size);
}

/* minimized windows belong to the monitor containing their restore rectangle. */
static RECT monitor_rect(const WND *w)
{
    return (w->Style & WS_MINIMIZE) && WndIsTopLevel((WND *)w) ? w->NormalRect : w->WindowRect;
}

/* the work area of the monitor a window rectangle is on, in the pixels of the window's awareness. */
static RECT work_of(const WND *w, const RECT *rect)
{
    RECT physical = *rect;
    DpiRectConvert(&physical, w->Awareness, DPI_PHYSICAL);
    RECT work = MonitorWorkOfRect(&physical);
    DpiRectConvert(&work, DPI_PHYSICAL, w->Awareness);
    return work;
}

void WndGetMinMaxInfo(WND *w, MINMAXINFO *mmi)
{
    UINT dpi = WndFrameDpi(w);
    memset(mmi, 0, sizeof(*mmi));
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, dpi, &m);
    RECT mrect = monitor_rect(w);
    RECT work = work_of(w, &mrect);
    LONG work_w = work.right - work.left;
    LONG work_h = work.bottom - work.top;
    if (!WndIsTopLevel(w) && w->Parent) {
        work_w = w->Parent->ClientRect.right - w->Parent->ClientRect.left;
        work_h = w->Parent->ClientRect.bottom - w->Parent->ClientRect.top;
    }
    mmi->ptMaxSize = (POINT){work_w + 2 * m.Border, work_h + m.Border + m.Border};
    mmi->ptMaxPosition = (POINT){-m.Border, -m.Border};
    mmi->ptMinTrackSize = (POINT){MetricsGetFor(SM_CXMINTRACK, dpi, w->Awareness),
                                  MetricsGetFor(SM_CYMINTRACK, dpi, w->Awareness)};
    mmi->ptMaxTrackSize = (POINT){MetricsGetFor(SM_CXMAXTRACK, dpi, w->Awareness),
                                  MetricsGetFor(SM_CYMAXTRACK, dpi, w->Awareness)};
    if (w->Style & (WS_THICKFRAME | WS_CAPTION))
        send_kernel(w, WM_GETMINMAXINFO, 0, mmi, MIME_LPARAM_INOUT, sizeof(*mmi));
    mmi->ptMaxTrackSize.x = W32Max(mmi->ptMaxTrackSize.x, mmi->ptMinTrackSize.x);
    mmi->ptMaxTrackSize.y = W32Max(mmi->ptMaxTrackSize.y, mmi->ptMinTrackSize.y);
    w->MinTrack = mmi->ptMinTrackSize;
    w->MaxTrack = mmi->ptMaxTrackSize;
}

/* computes the client rectangle for a window rectangle given in parent coordinates. */
static void calc_client(WND *w, const RECT *window, const RECT *old_window, RECT *client, BOOLEAN full)
{
    /* the default processing of the message tells the window how much of the top the menu bar takes. */
    WND *saved = MenuBarCalcWindow;
    MenuBarCalcWindow = w;
    w->MenuBarHeight = 0;
    if (!full) {
        RECT r = *window;
        send_kernel(w, WM_NCCALCSIZE, FALSE, &r, MIME_LPARAM_INOUT, sizeof(r));
        *client = r;
        MenuBarCalcWindow = saved;
        return;
    }
    struct {
        NCCALCSIZE_PARAMS p;
        WINDOWPOS pos;
    } data;
    memset(&data, 0, sizeof(data));
    data.p.rgrc[0] = *window;
    data.p.rgrc[1] = *old_window;
    RECT old_client = w->ClientRect;
    screen_to_parent(w, &old_client);
    data.p.rgrc[2] = old_client;
    data.pos.hwnd = WndHandle(w);
    data.pos.x = window->left;
    data.pos.y = window->top;
    data.pos.cx = window->right - window->left;
    data.pos.cy = window->bottom - window->top;
    send_kernel(w, WM_NCCALCSIZE, TRUE, &data, MIME_LPARAM_INOUT, sizeof(data));
    *client = data.p.rgrc[0];
    MenuBarCalcWindow = saved;
}

/* positioning. */

static void notify_parent(WND *w, UINT event)
{
    if ((w->Style & WS_CHILD) && !(w->ExStyle & WS_EX_NOPARENTNOTIFY) && w->Parent && w->Parent != g_desktop)
        MsgSend(w->Parent, WM_PARENTNOTIFY, MAKEWPARAM(event, (WORD)w->Id), (LPARAM)WndHandle(w));
}

static void invalidate_screen_rect(WND *parent, const RECT *r)
{
    if (!parent || parent == g_desktop)
        return;
    REGION rgn;
    RgnInit(&rgn);
    RgnSetRect(&rgn, r);
    WndInvalidateRegion(parent, &rgn, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    RgnFree(&rgn);
}

void WndSetPos(WND *w, WND *after_wnd, LONG x, LONG y, LONG cx, LONG cy, UINT flags)
{
    if (!w || w == g_desktop)
        return;
    ScreenSerial++;
    UserReference(&w->Header);
    HWND after = (HWND)after_wnd;
    WINDOWPOS pos = {WndHandle(w), after, x, y, cx, cy, flags};
    RECT old_window = w->WindowRect;
    screen_to_parent(w, &old_window);
    if (pos.flags & SWP_NOMOVE) {
        pos.x = old_window.left;
        pos.y = old_window.top;
    }
    if (pos.flags & SWP_NOSIZE) {
        pos.cx = old_window.right - old_window.left;
        pos.cy = old_window.bottom - old_window.top;
    }
    if ((pos.flags & SWP_SHOWWINDOW) && (w->Style & WS_VISIBLE))
        pos.flags &= ~SWP_SHOWWINDOW;
    if ((pos.flags & SWP_HIDEWINDOW) && !(w->Style & WS_VISIBLE))
        pos.flags &= ~SWP_HIDEWINDOW;
    if (!(pos.flags & SWP_NOSENDCHANGING)) {
        send_kernel(w, WM_WINDOWPOSCHANGING, 0, &pos, MIME_LPARAM_INOUT, sizeof(pos));
        if (!alive(w))
            goto done;
    }
    if (pos.cx < 0)
        pos.cx = 0;
    if (pos.cy < 0)
        pos.cy = 0;

    RECT new_window = {pos.x, pos.y, pos.x + pos.cx, pos.y + pos.cy};
    BOOLEAN moved = new_window.left != old_window.left || new_window.top != old_window.top;
    BOOLEAN sized = (new_window.right - new_window.left) != (old_window.right - old_window.left) ||
                    (new_window.bottom - new_window.top) != (old_window.bottom - old_window.top);
    RECT old_screen = w->WindowRect;
    RECT old_client_screen = w->ClientRect;
    BOOLEAN was_visible = WndIsVisible(w);
    /* what the window procedure is told of the client area: it is told of a move or a size that happened to it. */
    BOOLEAN client_moved = FALSE, client_sized = FALSE;

    if (moved || sized || (pos.flags & SWP_FRAMECHANGED)) {
        RECT client;
        if (sized || (pos.flags & SWP_FRAMECHANGED)) {
            calc_client(w, &new_window, &old_window, &client, TRUE);
            if (!alive(w))
                goto done;
        } else {
            /* a move alone does not ask the window for its client area, which moves with it. */
            client = w->ClientRect;
            screen_to_parent(w, &client);
            RectOffset(&client, new_window.left - old_window.left, new_window.top - old_window.top);
        }
        if (w->Style & WS_MINIMIZE)
            client = (RECT){new_window.left, new_window.top, new_window.left, new_window.top};
        RECT win_screen = new_window, client_screen = client;
        parent_to_screen(w->Parent, w->Awareness, &win_screen);
        parent_to_screen(w->Parent, w->Awareness, &client_screen);
        LONG dx = client_screen.left - w->ClientRect.left;
        LONG dy = client_screen.top - w->ClientRect.top;
        client_moved = dx || dy;
        client_sized = (client_screen.right - client_screen.left) != (w->ClientRect.right - w->ClientRect.left) ||
                       (client_screen.bottom - client_screen.top) != (w->ClientRect.bottom - w->ClientRect.top);
        /* children follow the client origin. */
        for (WND *c = w->FirstChild; c; c = c->Next)
            offset_tree(c, dx, dy);
        RgnOffset(&w->Update, dx, dy);
        w->WindowRect = win_screen;
        w->ClientRect = client_screen;
        RgnCombineRect(&w->Update, &w->Update, &w->ClientRect, RGN_AND);
    }

    if (!(pos.flags & SWP_NOZORDER) && w->Parent) {
        if (pos.hwndInsertAfter == HWND_TOPMOST)
            w->ExStyle |= WS_EX_TOPMOST;
        else if (pos.hwndInsertAfter == HWND_NOTOPMOST)
            w->ExStyle &= ~WS_EX_TOPMOST;
        WND *parent = w->Parent;
        unlink_window(w);
        link_window(parent, w, pos.hwndInsertAfter);
        if (parent == g_desktop)
            raise_owned(w);
    }

    if (pos.flags & SWP_SHOWWINDOW)
        w->Style |= WS_VISIBLE;
    else if (pos.flags & SWP_HIDEWINDOW)
        w->Style &= ~WS_VISIBLE;
    BOOLEAN visible = WndIsVisible(w);

    if (WndIsTopLevel(w)) {
        if (visible && !(w->Style & WS_MINIMIZE))
            WndEnsureSurface(w);
        if ((sized || (pos.flags & (SWP_FRAMECHANGED | SWP_SHOWWINDOW))) && visible && !(w->Style & WS_MINIMIZE)) {
            w->Flags |= WNDF_FRAME_DIRTY;
            WndInvalidateRegion(w, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        if (w->Flags & WNDF_HOST_WINDOW)
            WndUpdateHostConfig(w, FALSE);
    } else if (!(pos.flags & SWP_NOREDRAW)) {
        BOOLEAN changed = moved || sized || (pos.flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW | SWP_FRAMECHANGED)) ||
                          !(pos.flags & SWP_NOZORDER);
        if (changed) {
            if (was_visible)
                invalidate_screen_rect(w->Parent, &old_screen);
            if (visible) {
                if (sized || (pos.flags & (SWP_SHOWWINDOW | SWP_FRAMECHANGED)) || !(pos.flags & SWP_NOZORDER) ||
                    !(pos.flags & SWP_NOCOPYBITS) || moved)
                    WndInvalidateRegion(w, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            }
        }
    }
    (void)old_client_screen;

    if (!(pos.flags & SWP_NOACTIVATE) && WndIsTopLevel(w) && visible && !(w->ExStyle & WS_EX_NOACTIVATE) &&
        (pos.flags & SWP_SHOWWINDOW))
        WndActivate(w, FALSE);
    if ((pos.flags & SWP_HIDEWINDOW) && WndIsTopLevel(w) && InputForeground == w) {
        /* hand activation to the owner or the next visible window. */
        WND *next = w->Owner && WndIsVisible(w->Owner) ? w->Owner : nullptr;
        for (WND *c = g_desktop->FirstChild; !next && c; c = c->Next)
            if (c != w && (c->Style & WS_VISIBLE) && c->Thread == w->Thread && !(c->ExStyle & WS_EX_NOACTIVATE))
                next = c;
        if (next)
            WndActivate(next, FALSE);
        else
            InputSetForeground(nullptr);
    }
    if ((!visible || (w->Style & WS_MINIMIZE)) && w->Thread) {
        W32THREAD *t = w->Thread;
        WND *focus = WndLookup(t->Focus);
        if (focus && (focus == w || WndIsChildOf(focus, w)))
            WndSetFocus(w->Parent && w->Parent != g_desktop ? w->Parent : nullptr);
    }

    /* a child that is positioned without SWP_NOACTIVATE is told so before its window position is final: Windows sends
     * WM_CHILDACTIVATE from SetWindowPos (xxxSwpActivate), which is how an MDI child that is brought to the top, moved or
     * sized becomes the active document, and how a program that does the activation itself has it done below. */
    if (!(pos.flags & SWP_NOACTIVATE) && (w->Style & (WS_CHILD | WS_POPUP)) == WS_CHILD && alive(w))
        MsgSend(w, WM_CHILDACTIVATE, 0, 0);
    if (!alive(w))
        goto done;
    pos.flags |= (moved ? 0 : SWP_NOMOVE) | (sized ? 0 : SWP_NOSIZE) | (client_moved ? 0 : SWP_NOCLIENTMOVE) |
                 (client_sized ? 0 : SWP_NOCLIENTSIZE);
    RECT final = w->WindowRect;
    screen_to_parent(w, &final);
    pos.x = final.left;
    pos.y = final.top;
    pos.cx = final.right - final.left;
    pos.cy = final.bottom - final.top;
    if (!(pos.flags & SWP_NOSENDCHANGING) || moved || sized || (pos.flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)))
        send_kernel(w, WM_WINDOWPOSCHANGED, 0, &pos, MIME_LPARAM_IN, sizeof(pos));
    if ((moved || sized) && alive(w))
        WndCheckMonitorDpi(w);
    if (alive(w))
        ScreenLayersChanged(w);
done:
    UserDereference(&w->Header);
}

static void maximized_rect(WND *w, RECT *r)
{
    MINMAXINFO mmi;
    WndGetMinMaxInfo(w, &mmi);
    RECT mrect = monitor_rect(w);
    RECT work = work_of(w, &mrect);
    r->left = (WndIsTopLevel(w) ? work.left : 0) + mmi.ptMaxPosition.x;
    r->top = (WndIsTopLevel(w) ? work.top : 0) + mmi.ptMaxPosition.y;
    r->right = r->left + mmi.ptMaxSize.x;
    r->bottom = r->top + mmi.ptMaxSize.y;
}

/* a theme change goes to the window and all of its children, a system color change only to the top level ones. */
static void post_theme_change(WND *w)
{
    if (w->Thread)
        QueuePost(w->Thread, WndHandle(w), WM_THEMECHANGED, 0, 0);
    for (WND *c = w->FirstChild; c; c = c->Next)
        if (!(c->Flags & WNDF_DESTROYING))
            post_theme_change(c);
}

void WndSystemChanged(void)
{
    WND *desktop = WndDesktop();
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c; c = c->Next) {
        if (c->Flags & WNDF_DESTROYING)
            continue;
        if (c->Thread)
            QueuePost(c->Thread, WndHandle(c), WM_SYSCOLORCHANGE, 0, 0);
        post_theme_change(c);
        if (c->Style & WS_VISIBLE)
            WndInvalidateRegion(c, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    }
}

/* resize the desktop to the display and maximized windows to the work area, then notify top level windows. */
void WndDisplayChanged(void)
{
    if (!g_desktop)
        return;
    g_desktop->WindowRect = (RECT){0, 0, W32Display.Width, W32Display.Height};
    g_desktop->ClientRect = g_desktop->WindowRect;
    for (WND *c = g_desktop->FirstChild; c; c = c->Next) {
        if (c->Flags & WNDF_DESTROYING)
            continue;
        if ((c->Style & WS_MAXIMIZE) && !(c->Flags & WNDF_FULLSCREEN)) {
            RECT r;
            maximized_rect(c, &r);
            WndSetPos(c, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (c->Thread)
            QueuePost(c->Thread, WndHandle(c), WM_DISPLAYCHANGE, W32Display.Bits,
                      MAKELPARAM((WORD)W32Display.Width, (WORD)W32Display.Height));
    }
    /* look up windows again after each DPI notification; application callbacks may destroy them. */
    HWND top[64];
    ULONG count = 0;
    for (WND *c = g_desktop->FirstChild; c && count < RTL_NUMBER_OF(top); c = c->Next)
        if (!(c->Flags & WNDF_DESTROYING))
            top[count++] = WndHandle(c);
    for (ULONG i = 0; i < count; i++) {
        WND *c = WndLookup(top[i]);
        if (c)
            WndCheckMonitorDpi(c);
    }
}

/* per-monitor-aware windows use their own DPI; others use system DPI. */
UINT WndHostDpi(WND *w)
{
    WND *top = WndTopLevel(w);
    if (top && top->Awareness >= DPI_AWARE_PER_MONITOR && top->Dpi)
        return top->Dpi;
    return top ? WndMonitorDpi(top) : W32SystemDpi();
}

static void set_tree_dpi(WND *w, UINT dpi)
{
    w->Dpi = dpi;
    for (WND *c = w->FirstChild; c; c = c->Next)
        if (c->Awareness >= DPI_AWARE_PER_MONITOR)
            set_tree_dpi(c, dpi);
}

/* whether the dpi of the window reaches its children: always for per monitor v2, on request for per monitor. */
BOOLEAN WndBroadcastsDpi(const WND *w)
{
    return w->Awareness >= DPI_AWARE_PER_MONITOR && (w->Flags & WNDF_DPI_CHILDREN);
}

/* the descendants in the order of a tree walk, parents first. */
static ULONG collect_descendants(WND *w, HWND *out, ULONG capacity, ULONG count)
{
    for (WND *c = w->FirstChild; c; c = c->Next) {
        if (c->Flags & WNDF_DESTROYING)
            continue;
        if (count < capacity)
            out[count] = WndHandle(c);
        count = collect_descendants(c, out, capacity, count + 1);
    }
    return count;
}

static void send_child_dpi(HWND handle, BOOLEAN before)
{
    WND *c = WndLookup(handle);
    if (!c || c->Awareness < DPI_AWARE_PER_MONITOR || (c->Flags & WNDF_DESTROYING))
        return;
    UserReference(&c->Header);
    MsgSend(c, before ? WM_DPICHANGED_BEFOREPARENT : WM_DPICHANGED_AFTERPARENT, 0, 0);
    /* the frame is recalculated at the new dpi once the child knows. */
    if (before && alive(c))
        WndSetPos(c, nullptr, 0, 0, 0, 0,
                  SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    UserDereference(&c->Header);
}

/*
 * the rectangle a window of the dpi it is on gets at another one: the program is asked for the size first, as
 * WM_GETDPISCALEDSIZE, when the dpi reaches its children. else the size scales and the corner stays.
 */
static RECT dpi_candidate_rect(WND *w, UINT old, UINT dpi)
{
    RECT current = w->WindowRect;
    if (WndBroadcastsDpi(w)) {
        SIZE size = {current.right - current.left, current.bottom - current.top};
        UserReference(&w->Header);
        LRESULT handled = send_kernel(w, WM_GETDPISCALEDSIZE, dpi, &size, MIME_LPARAM_INOUT, sizeof(size));
        UserDereference(&w->Header);
        if (handled)
            return (RECT){current.left, current.top, current.left + size.cx, current.top + size.cy};
    }
    return (RECT){current.left, current.top, current.left + W32MulDiv(current.right - current.left, (LONG)dpi, (LONG)old),
                  current.top + W32MulDiv(current.bottom - current.top, (LONG)dpi, (LONG)old)};
}

/*
 * the dpi of a window tree has changed and the windows tell their programs, in the order of windows: the children
 * of the tree hear WM_DPICHANGED_BEFOREPARENT from the last to the first, the top level window gets
 * WM_DPICHANGED with the rectangle it should have, and the children hear WM_DPICHANGED_AFTERPARENT from the
 * first to the last. a window that does not reach its children is told alone.
 */
void WndNotifyDpiChanged(WND *w, UINT old, const RECT *suggested)
{
    UINT dpi = w->Dpi;
    BOOLEAN top = WndIsTopLevel(w), broadcast = WndBroadcastsDpi(w);
    RECT rect = {};
    if (top)
        rect = suggested ? *suggested : dpi_candidate_rect(w, old, dpi);
    HWND small[32];
    HWND *list = small;
    ULONG count = 0, capacity = RTL_NUMBER_OF(small);
    UserReference(&w->Header);
    if (broadcast) {
        count = collect_descendants(w, small, capacity, 0);
        if (count > capacity) {
            capacity = count;
            list = W32Alloc(capacity * sizeof(HWND));
            if (!list) {
                list = small;
                count = capacity = RTL_NUMBER_OF(small);
            } else {
                count = collect_descendants(w, list, capacity, 0);
            }
        }
    }
    for (ULONG i = count; i > 0 && alive(w); i--)
        send_child_dpi(list[i - 1], TRUE);
    if (alive(w) && top) {
        send_kernel(w, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), &rect, MIME_LPARAM_IN, sizeof(rect));
        if (alive(w))
            WndSetPos(w, nullptr, 0, 0, 0, 0,
                      SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else if (alive(w) && broadcast) {
        /* a child that moved under a parent of another dpi hears both about itself. */
        send_child_dpi(WndHandle(w), TRUE);
        send_child_dpi(WndHandle(w), FALSE);
    }
    for (ULONG i = 0; i < count && alive(w); i++)
        send_child_dpi(list[i], FALSE);
    if (list != small)
        W32Free(list);
    UserDereference(&w->Header);
}

/*
 * a per monitor aware window that moved mostly onto a monitor of another dpi follows it. the program is asked for
 * the size at the new dpi while the window still has the old one.
 */
void WndCheckMonitorDpi(WND *w)
{
    if (!WndIsTopLevel(w) || w->Awareness < DPI_AWARE_PER_MONITOR ||
        (w->Flags & WNDF_DESTROYING) || (w->Style & WS_MINIMIZE) || (w->ExStyle & 0x80000000u))
        return;
    UINT dpi = MonitorDpiOfRect(&w->WindowRect), old = w->Dpi ? w->Dpi : W32SystemDpi();
    /* only a move to a monitor of another dpi counts, not the dpi a test gave the window. */
    if (!dpi || dpi == w->MonitorDpi)
        return;
    w->MonitorDpi = dpi;
    if (dpi == old)
        return;
    UserReference(&w->Header);
    /* a program that deferred the changes is told when it restores them. */
    if (w->DeferredDpi) {
        set_tree_dpi(w, dpi);
        w->Flags |= WNDF_DPI_PENDING;
    } else {
        RECT suggested = dpi_candidate_rect(w, old, dpi);
        if (alive(w)) {
            set_tree_dpi(w, dpi);
            WndNotifyDpiChanged(w, old, &suggested);
        }
    }
    if (alive(w))
        WndUpdateHostConfig(w, FALSE);
    UserDereference(&w->Header);
}

/* the scales a display can have: 100 to 250 percent in steps of 25, then 300 to 500 in steps of 50. */
static BOOLEAN valid_plateau(UINT dpi)
{
    static const UINT plateaus[] = {96, 120, 144, 168, 192, 216, 240, 288, 336, 384, 432, 480};
    for (ULONG i = 0; i < RTL_NUMBER_OF(plateaus); i++)
        if (plateaus[i] == dpi)
            return TRUE;
    return FALSE;
}

/*
 * the dpi of the window tree is forced, as if the window moved to a monitor of that dpi. the program is asked for
 * the size while the window still has the old dpi, as a move does it.
 */
BOOLEAN WndForceDpi(WND *w, UINT dpi)
{
    if (!WndIsTopLevel(w) || w->Awareness < DPI_AWARE_PER_MONITOR || !valid_plateau(dpi))
        return FALSE;
    UINT old = w->Dpi ? w->Dpi : W32SystemDpi();
    UserReference(&w->Header);
    if (w->DeferredDpi) {
        set_tree_dpi(w, dpi);
        w->Flags |= WNDF_DPI_PENDING;
    } else {
        RECT suggested = dpi_candidate_rect(w, old, dpi);
        if (alive(w)) {
            set_tree_dpi(w, dpi);
            WndNotifyDpiChanged(w, old, &suggested);
        }
    }
    if (alive(w))
        WndUpdateHostConfig(w, FALSE);
    UserDereference(&w->Header);
    return TRUE;
}

/* the dpi the windows of a tree last told their programs of, kept while the changes wait. a window whose program
   deferred its own changes keeps its state, and its windows with it. */
static void set_tree_deferred(const WND *root, WND *w, UINT dpi)
{
    if (w != root && (w->Flags & WNDF_DPI_DEFERRED))
        return;
    w->DeferredDpi = (USHORT)dpi;
    for (WND *c = w->FirstChild; c; c = c->Next)
        set_tree_deferred(root, c, dpi);
}

/* SetDeferredDpiStateForWindowAndChildren of win32kfull. */
static BOOLEAN set_deferred_dpi(WND *w, BOOLEAN defer, BOOLEAN by_program)
{
    if ((w->Flags & WNDF_DPI_DEFERRED) && !by_program)
        return FALSE;
    if (defer == (w->DeferredDpi != 0)) {
        if (!(w->Flags & WNDF_DPI_DEFERRED) && by_program) {
            w->Flags |= WNDF_DPI_DEFERRED;
            return TRUE;
        }
        return FALSE;
    }
    set_tree_deferred(w, w, defer ? WndGetDpi(w) : 0);
    if (defer && by_program)
        w->Flags |= WNDF_DPI_DEFERRED;
    else
        w->Flags &= ~WNDF_DPI_DEFERRED;
    return TRUE;
}

/* the windows of the tree keep the dpi their program knows until it restores the changes. */
BOOLEAN WndDeferDpiChanges(WND *w)
{
    if (w->Awareness < DPI_AWARE_PER_MONITOR) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return set_deferred_dpi(w, TRUE, TRUE);
}

/* the deferral ends and the program hears of what it missed, from the dpi it knew. */
BOOLEAN WndRestoreDpiChanges(WND *w)
{
    if (w->Awareness < DPI_AWARE_PER_MONITOR) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UINT known = w->DeferredDpi;
    if (!known)
        return FALSE;
    if (!WndIsTopLevel(w) && w->Parent->DeferredDpi) {
        w->Flags &= ~WNDF_DPI_DEFERRED;
        return TRUE;
    }
    UserReference(&w->Header);
    /* the program is asked for the size while the window still has the dpi it knows. */
    BOOLEAN changed = known != w->Dpi;
    RECT suggested = {};
    if (changed && WndIsTopLevel(w))
        suggested = dpi_candidate_rect(w, known, w->Dpi);
    BOOLEAN ok = alive(w) && set_deferred_dpi(w, FALSE, TRUE);
    w->Flags &= ~WNDF_DPI_PENDING;
    if (changed && alive(w)) {
        WndNotifyDpiChanged(w, known, WndIsTopLevel(w) ? &suggested : nullptr);
        if (alive(w))
            WndUpdateHostConfig(w, FALSE);
    }
    UserDereference(&w->Header);
    return ok;
}

/*
 * move to the new monitor, preserving the offset from the monitor corner within its work area. maximized windows fill
 * the new work area.
 */
void WndHostDisplay(WND *w, ULONG monitor)
{
    MIME_MONITOR_INFO to, from;
    if (!w || !WndIsTopLevel(w) || (w->Style & WS_MINIMIZE) || (w->Flags & WNDF_DESTROYING) ||
        !MonitorGet(monitor, &to))
        return;
    /* the monitors are in physical pixels. */
    RECT rect = w->WindowRect;
    DpiRectConvert(&rect, w->Awareness, DPI_PHYSICAL);
    LONG now = MonitorFromRectIndex(&rect, FALSE);
    if (now == (LONG)monitor)
        return;
    /* a window that is not per monitor aware keeps its size, which takes the pixels of the monitor it goes to. */
    LONG width = rect.right - rect.left, height = rect.bottom - rect.top;
    if (w->Awareness < DPI_AWARE_PER_MONITOR) {
        UINT dpi = to.Dpi ? to.Dpi : 96;
        width = DpiLength(w->WindowRect.right - w->WindowRect.left, w->Awareness, DPI_PHYSICAL, dpi);
        height = DpiLength(w->WindowRect.bottom - w->WindowRect.top, w->Awareness, DPI_PHYSICAL, dpi);
    }
    LONG dx = 0, dy = 0;
    if (MonitorGet(now < 0 ? 0 : (ULONG)now, &from)) {
        dx = W32Max(rect.left - from.Rect.left, 0);
        dy = W32Max(rect.top - from.Rect.top, 0);
    }
    LONG x = to.Rect.left + dx, y = to.Rect.top + dy;
    x = W32Max(to.Work.left, W32Min(x, to.Work.right - width));
    y = W32Max(to.Work.top, W32Min(y, to.Work.bottom - height));
    POINT at = {x, y};
    DpiPointConvert(&at, DPI_PHYSICAL, w->Awareness);
    WndSetPos(w, nullptr, at.x, at.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (alive(w) && (w->Style & WS_MAXIMIZE) && !(w->Flags & WNDF_FULLSCREEN)) {
        RECT max;
        maximized_rect(w, &max);
        WndSetPos(w, nullptr, max.left, max.top, max.right - max.left, max.bottom - max.top,
                  SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

/* the compositor reports its window size in physical pixels. add the window bounds outside the host-covered area. */
void WndHostSize(WND *w, LONG width, LONG height)
{
    if (!w || !WndIsTopLevel(w) || (w->Style & WS_MINIMIZE) || (w->Flags & (WNDF_DESTROYING | WNDF_FULLSCREEN)) ||
        width < 1 || height < 1)
        return;
    RECT part, window = w->WindowRect;
    HostPart(w, &part);
    DpiRectConvert(&window, w->Awareness, DPI_PHYSICAL);
    LONG cx = width + (window.right - window.left) - (part.right - part.left);
    LONG cy = height + (window.bottom - window.top) - (part.bottom - part.top);
    if (cx == window.right - window.left && cy == window.bottom - window.top)
        return;
    window.right = window.left + cx;
    window.bottom = window.top + cy;
    DpiRectConvert(&window, DPI_PHYSICAL, w->Awareness);
    WndSetPos(w, nullptr, 0, 0, window.right - window.left, window.bottom - window.top,
              SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void deactivate_minimized(WND *w)
{
    if (InputForeground != w)
        return;
    WND *next = w->Owner;
    if (next && (!WndIsVisible(next) || (next->Style & (WS_MINIMIZE | WS_DISABLED))))
        next = nullptr;
    for (WND *c = g_desktop->FirstChild; !next && c; c = c->Next)
        if (c != w && (c->Style & WS_VISIBLE) && !(c->Style & (WS_MINIMIZE | WS_DISABLED)) &&
            c->Thread == w->Thread && !(c->ExStyle & WS_EX_NOACTIVATE))
            next = c;
    if (next) {
        WndActivate(next, FALSE);
        return;
    }
    InputSetForeground(nullptr);
    if (w->Thread)
        w->Thread->Active = nullptr;
    MsgSendEvent(w, WM_NCACTIVATE, FALSE, 0);
    if (alive(w))
        MsgSendEvent(w, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, TRUE), 0);
}

void WndShow(WND *w, int cmd)
{
    if (!w || w == g_desktop || (UINT)cmd > SW_FORCEMINIMIZE)
        return;
    if (cmd == SW_SHOWDEFAULT)
        cmd = SW_SHOWNORMAL;
    UserReference(&w->Header);
    if ((w->Flags & WNDF_FULLSCREEN) && cmd != SW_SHOW && cmd != SW_SHOWNA) {
        WndFullscreen(w, FALSE, nullptr);
        if (!alive(w))
            goto done;
    }
    BOOLEAN was_visible = (w->Style & WS_VISIBLE) != 0;
    BOOLEAN show = cmd != SW_HIDE;
    BOOLEAN noactivate = cmd == SW_HIDE || cmd == SW_SHOWNOACTIVATE || cmd == SW_SHOWNA ||
                         cmd == SW_SHOWMINNOACTIVE || cmd == SW_MINIMIZE || cmd == SW_FORCEMINIMIZE;
    DWORD old_state = w->Style & (WS_MINIMIZE | WS_MAXIMIZE);
    DWORD new_state = old_state;
    switch (cmd) {
    case SW_MINIMIZE:
    case SW_SHOWMINIMIZED:
    case SW_SHOWMINNOACTIVE:
    case SW_FORCEMINIMIZE:
        new_state = WS_MINIMIZE;
        break;
    case SW_MAXIMIZE:
        new_state = WS_MAXIMIZE;
        break;
    case SW_SHOWNORMAL:
        new_state = 0;
        break;
    case SW_RESTORE:
    case SW_SHOWNOACTIVATE:
        new_state = (old_state & WS_MINIMIZE) && (w->Flags & WNDF_RESTORETOMAX) ? WS_MAXIMIZE : 0;
        break;
    }
    BOOLEAN state_changed = old_state != new_state;
    if (state_changed && (HookCbt(w, HCBT_MINMAX, (WPARAM)WndHandle(w), cmd, 0) || !alive(w)))
        goto done;
    if (state_changed && (old_state & WS_MINIMIZE)) {
        LRESULT restore = MsgSend(w, WM_QUERYOPEN, 0, 0);
        if (!restore || !alive(w)) {
            if (alive(w))
                WndUpdateHostConfig(w, FALSE);
            goto done;
        }
    }
    RECT rect = w->WindowRect;
    screen_to_parent(w, &rect);
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER;
    if (noactivate)
        flags |= SWP_NOACTIVATE;
    if (state_changed) {
        if (!old_state)
            w->NormalRect = rect;
        if (new_state == WS_MINIMIZE) {
            WndEnsureSurface(w);
            if (old_state & WS_MAXIMIZE)
                w->Flags |= WNDF_RESTORETOMAX;
            else
                w->Flags &= ~WNDF_RESTORETOMAX;
            LONG x = WndIsTopLevel(w) ? -32000 : 0;
            LONG y = WndIsTopLevel(w) ? -32000 : 0;
            rect = (RECT){x, y, x + MetricsGetFor(SM_CXMINIMIZED, WndFrameDpi(w), w->Awareness),
                         y + MetricsGetFor(SM_CYMINIMIZED, WndFrameDpi(w), w->Awareness)};
            flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        } else if (new_state == WS_MAXIMIZE) {
            maximized_rect(w, &rect);
            if (!alive(w))
                goto done;
            w->Flags |= WNDF_RESTORETOMAX;
            flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        } else {
            rect = w->NormalRect;
            w->Flags &= ~WNDF_RESTORETOMAX;
            flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        }
        w->Style = (w->Style & ~(WS_MINIMIZE | WS_MAXIMIZE)) | new_state;
        MenuRefreshWindowState(w);
        flags |= SWP_FRAMECHANGED | SWP_STATECHANGED;
    }
    if (show != was_visible) {
        MsgSend(w, WM_SHOWWINDOW, show, 0);
        if (!alive(w))
            goto done;
        flags |= show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
    }
    if (state_changed || show != was_visible) {
        if (show && !noactivate && WndIsTopLevel(w))
            flags &= ~SWP_NOZORDER;
        WndSetPos(w, (WND *)HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, flags);
        if (!alive(w))
            goto done;
    }
    if (show && new_state == WS_MINIMIZE && noactivate)
        deactivate_minimized(w);
    else if (show && !noactivate && WndIsTopLevel(w))
        WndActivate(w, FALSE);
done:
    UserDereference(&w->Header);
}

void WndFullscreen(WND *w, BOOLEAN enter, const RECT *rect)
{
    if (!WndIsTopLevel(w) || enter == ((w->Flags & WNDF_FULLSCREEN) != 0))
        return;
    UserReference(&w->Header);
    RECT target;
    if (enter) {
        if (w->Style & WS_MINIMIZE)
            WndShow(w, SW_RESTORE);
        if (!alive(w) || (w->Style & WS_MINIMIZE))
            goto done;
        w->FullscreenRect = w->WindowRect;
        w->FullscreenNormalRect = (w->Style & WS_MAXIMIZE) ? w->NormalRect : w->WindowRect;
        w->FullscreenStyle = w->Style;
        w->FullscreenExStyle = w->ExStyle;
        w->FullscreenRestoreFlags = w->Flags & WNDF_RESTORETOMAX;
        w->Flags |= WNDF_FULLSCREEN;
        /*
         * preserve application-managed fullscreen styles and bounds. Godot leaves fullscreen if its window no longer
         * matches the screen.
         */
        if ((w->Style & WS_CAPTION) != WS_CAPTION && !(w->Style & WS_THICKFRAME)) {
            w->Flags |= WNDF_APP_FULLSCREEN;
            if (WndCoversMonitor(w)) {
                MenuRefreshWindowState(w);
                goto done;
            }
        } else {
            w->Style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZE);
            w->ExStyle &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
        }
        target = *rect;
    } else {
        w->Flags = (w->Flags & ~(WNDF_FULLSCREEN | WNDF_APP_FULLSCREEN | WNDF_RESTORETOMAX)) | w->FullscreenRestoreFlags;
        w->Style = (w->FullscreenStyle & ~WS_VISIBLE) | (w->Style & WS_VISIBLE);
        w->ExStyle = w->FullscreenExStyle;
        w->NormalRect = w->FullscreenNormalRect;
        target = w->FullscreenRect;
    }
    MenuRefreshWindowState(w);
    WndSetPos(w, nullptr, target.left, target.top, target.right - target.left, target.bottom - target.top,
              SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
done:
    UserDereference(&w->Header);
}

void WndCenterOnOwner(WND *w)
{
    RECT screen = DpiWorkArea(w->Awareness);
    RECT ref = screen;
    if (w->Owner && (w->Owner->Style & WS_VISIBLE) && !(w->Owner->Style & WS_MINIMIZE)) {
        ref = w->Owner->WindowRect;
        DpiRectConvert(&ref, w->Owner->Awareness, w->Awareness);
        screen = work_of(w, &ref);
    }
    LONG width = w->WindowRect.right - w->WindowRect.left;
    LONG height = w->WindowRect.bottom - w->WindowRect.top;
    LONG x = ref.left + (ref.right - ref.left - width) / 2;
    LONG y = ref.top + (ref.bottom - ref.top - height) / 2;
    x = W32Max(screen.left, W32Min(x, screen.right - width));
    y = W32Max(screen.top, W32Min(y, screen.bottom - height));
    WndSetPos(w, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* activation and focus. */

/* the host already focused and raised this window; do not ask it to raise again. */
BOOLEAN WndActivatedByHost;

void WndActivate(WND *w, BOOLEAN click)
{
    if (w)
        w = WndTopLevel(w);
    if (!w || w == g_desktop || (w->Flags & WNDF_DESTROYING))
        return;
    WND *old = InputForeground;
    if (old == w) {
        if (w->Thread)
            w->Thread->Active = WndHandle(w);
        return;
    }
    UserReference(&w->Header);
    CBTACTIVATESTRUCT activate = {click, WndHandle(old)};
    if (HookCbt(w, HCBT_ACTIVATE, (WPARAM)WndHandle(w), (LPARAM)&activate, sizeof(activate)) || !alive(w))
        goto done;
    old = InputForeground;
    if (old && alive(old)) {
        UserReference(&old->Header);
        MsgSendEvent(old, WM_NCACTIVATE, FALSE, (LPARAM)WndHandle(w));
        MsgSendEvent(old, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, (old->Style & WS_MINIMIZE) != 0), (LPARAM)WndHandle(w));
        if (old->Thread && w->Thread && old->Thread->Process != w->Thread->Process)
            MsgSendEvent(old, WM_ACTIVATEAPP, FALSE, (LPARAM)w->Thread->Tid);
        if (old->Thread && old->Thread != w->Thread)
            old->Thread->Active = nullptr;
        UserDereference(&old->Header);
    }
    if (!alive(w))
        goto done;
    InputSetForeground(w);
    if (w->Thread)
        w->Thread->Active = WndHandle(w);
    ImeFocusChanged();
    if (WndIsTopLevel(w)) {
        WND *parent = w->Parent;
        unlink_window(w);
        link_window(parent, w, HWND_TOP);
        raise_owned(w);
    }
    WndUpdateHostConfig(w, !WndActivatedByHost);
    if (!old || !old->Thread || !w->Thread || old->Thread->Process != w->Thread->Process)
        MsgSendEvent(w, WM_ACTIVATEAPP, TRUE, old && old->Thread ? (LPARAM)old->Thread->Tid : 0);
    MsgSendEvent(w, WM_NCACTIVATE, TRUE, (LPARAM)WndHandle(old));
    MsgSendEvent(w, WM_ACTIVATE, MAKEWPARAM(click ? WA_CLICKACTIVE : WA_ACTIVE, (w->Style & WS_MINIMIZE) != 0),
                 (LPARAM)WndHandle(old));
done:
    UserDereference(&w->Header);
}

void WndSetFocus(WND *w)
{
    W32THREAD *t = w ? w->Thread : W32GetThread();
    if (!t)
        return;
    HWND old_h = t->Focus;
    if (old_h == WndHandle(w))
        return;
    if (w)
        UserReference(&w->Header);
    BOOLEAN refused = HookCbt(w, HCBT_SETFOCUS, (WPARAM)WndHandle(w), (LPARAM)old_h, 0) || (w && !alive(w));
    if (w)
        UserDereference(&w->Header);
    if (refused)
        return;
    old_h = t->Focus;
    if (w) {
        UserReference(&w->Header);
        WND *top = WndTopLevel(w);
        if (top != InputForeground && top->Thread == t)
            WndActivate(top, FALSE);
        if (!alive(w)) {
            UserDereference(&w->Header);
            return;
        }
    }
    WND *old = WndLookup(old_h);
    t->Focus = WndHandle(w);
    if (w)
        InputFocusOwner = t;
    else if (InputFocusOwner == t)
        InputFocusOwner = nullptr;
    ImeFocusChanged();
    if (old)
        MsgSend(old, WM_KILLFOCUS, (WPARAM)WndHandle(w), 0);
    if (w && alive(w) && t->Focus == WndHandle(w))
        MsgSend(w, WM_SETFOCUS, (WPARAM)old_h, 0);
    if (w)
        UserDereference(&w->Header);
}

/* hit testing. */

WND *WndChildFromPoint(WND *parent, POINT pt, BOOLEAN skip_disabled)
{
    for (WND *c = parent->FirstChild; c; c = c->Next) {
        /* the desktop is in physical pixels and a top level window in its own. */
        POINT at = pt;
        if (parent == g_desktop)
            DpiPixelConvert(&at, DPI_PHYSICAL, c->Awareness);
        if (!(c->Style & WS_VISIBLE) || !RectContains(&c->WindowRect, at.x, at.y) || !LayerHitTest(c, at))
            continue;
        if (skip_disabled && (c->Style & WS_DISABLED))
            continue;
        return c;
    }
    return nullptr;
}

/*
 * find the deepest window under pt. a disabled top level window is returned even over its children; disabled children
 * are skipped so input passes to the window below or their parent.
 */
WND *WndFromPoint(WND *top, POINT pt)
{
    WND *w = top;
    if (!w)
        return nullptr;
    for (;;) {
        if (!RectContains(&w->ClientRect, pt.x, pt.y) || (w->Style & WS_MINIMIZE) || (w == top && (w->Style & WS_DISABLED) && WndIsTopLevel(w)))
            return w;
        WND *c = WndChildFromPoint(w, pt, TRUE);
        if (!c)
            return w;
        /* static controls and group boxes are transparent to the mouse via WM_NCHITTEST HTTRANSPARENT, handled by the caller. */
        w = c;
    }
}

/* destruction. */

static void free_window(WND *w)
{
    TimerRemoveWindow(w);
    W32THREAD *t = w->Thread;
    if (t) {
        HWND h = WndHandle(w);
        if (t->Focus == h)
            t->Focus = nullptr;
        if (t->Active == h)
            t->Active = nullptr;
        if (t->Capture == h)
            InputSetCapture(t, nullptr, 0);
        if (t->MouseWindow == h)
            t->MouseWindow = nullptr;
    }
    if (InputForeground == w)
        InputSetForeground(nullptr);
    MenuWindowDestroyed(w);
    ClipboardWindowDestroyed(w);
    DndWindowDestroyed(w);
    TrayWindowDestroyed(w);
    ImeWindowDestroyed(w);
    PropFreeWindow(w);
    W32Free(w->ShutdownReason);
    if (w->Flags & WNDF_HOST_WINDOW) {
        HostWindowDestroy(w);
        w->Flags &= ~WNDF_HOST_WINDOW;
    }
    LayerWindowDestroyed(w);
    if (w->Surface) {
        PaintRetireSurface(w->Surface);
        w->Surface = nullptr;
    }
    unlink_window(w);
    for (WND *c = g_desktop ? g_desktop->FirstChild : nullptr; c; c = c->Next)
        if (c->Owner == w)
            c->Owner = nullptr;
    if (w->Class)
        w->Class->Windows--;
    W32Free(w->Text);
    w->Text = nullptr;
    W32Free(w->Extra);
    w->Extra = nullptr;
    RgnFree(&w->Update);
    w->Header.Destroyed = TRUE;
    UserFreeHandle(&w->Header);
    UserDereference(&w->Header);
}

static WND *first_live_child(WND *w)
{
    for (WND *c = w->FirstChild; c; c = c->Next)
        if (!(c->Flags & WNDF_DESTROYING))
            return c;
    return nullptr;
}

static void destroy_tree(WND *w)
{
    w->Flags |= WNDF_DESTROYING;
    UserReference(&w->Header);
    ClipboardDisown(w);
    MsgSend(w, WM_DESTROY, 0, 0);
    WND *c;
    while ((c = first_live_child(w)))
        destroy_tree(c);
    MsgSend(w, WM_NCDESTROY, 0, 0);
    free_window(w);
    UserDereference(&w->Header);
}

void WndDestroy(WND *w)
{
    if (!w || w == g_desktop || (w->Flags & WNDF_DESTROYING))
        return;
    UserReference(&w->Header);
    notify_parent(w, WM_DESTROY);
    if (!alive(w) || (w->Flags & WNDF_DESTROYING))
        goto done;
    /* owned windows go first. */
    if (WndIsTopLevel(w)) {
        for (;;) {
            WND *owned = nullptr;
            for (WND *c = g_desktop->FirstChild; c; c = c->Next)
                if (c->Owner == w && !(c->Flags & WNDF_DESTROYING))
                    owned = c;
            if (!owned)
                break;
            WndDestroy(owned);
        }
    }
    if (w->Style & WS_VISIBLE)
        WndSetPos(w, nullptr, 0, 0, 0, 0,
                  SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | (WndIsTopLevel(w) ? 0 : SWP_NOACTIVATE));
    if (alive(w) && !(w->Flags & WNDF_DESTROYING))
        destroy_tree(w);
done:
    UserDereference(&w->Header);
}

/* count top level and child windows as process USER objects. */
static ULONG count_tree(WND *w, W32PROCESS *p)
{
    ULONG n = 0;
    for (WND *c = w->FirstChild; c; c = c->Next)
        n += (c->Thread && c->Thread->Process == p) + count_tree(c, p);
    return n;
}

ULONG WndCountProcessObjects(W32PROCESS *p)
{
    return g_desktop ? count_tree(g_desktop, p) : 0;
}

BOOL WndThreadHasWindows(W32THREAD *t)
{
    if (!g_desktop)
        return FALSE;
    for (WND *c = g_desktop->FirstChild; c; c = c->Next)
        if (c->Thread == t && !(c->Flags & WNDF_DESTROYING))
            return TRUE;
    return FALSE;
}

/*
 * find every window owned by the thread, including children of other threads' windows and partially destroyed
 * windows. the thread may have exited during a destruction callback, leaving cleanup unfinished.
 */
static WND *find_thread_window(WND *parent, W32THREAD *t)
{
    for (WND *c = parent->FirstChild; c; c = c->Next) {
        if (c->Thread == t)
            return c;
        WND *inner = find_thread_window(c, t);
        if (inner)
            return inner;
    }
    return nullptr;
}

void WndDestroyThreadWindows(W32THREAD *t)
{
    if (!g_desktop)
        return;
    for (;;) {
        WND *found = find_thread_window(g_desktop, t);
        if (!found)
            break;
        /* the thread is exiting; tear down its windows without callbacks. */
        found->Flags |= WNDF_DESTROYING;
        WND *stack[64];
        int depth = 0;
        stack[depth++] = found;
        while (depth) {
            WND *w = stack[depth - 1];
            WND *c = w->FirstChild;
            if (c && depth < 64) {
                c->Flags |= WNDF_DESTROYING;
                stack[depth++] = c;
                continue;
            }
            depth--;
            while (w->FirstChild)
                free_window(w->FirstChild);
            free_window(w);
        }
    }
}

/* creation. */

static NTSTATUS capture_class_name(PUNICODE_STRING user, PWSTR *name, ATOM *atom)
{
    UNICODE_STRING s;
    *name = nullptr;
    *atom = 0;
    if (!user)
        return STATUS_INVALID_PARAMETER;
    if (!NT_SUCCESS(MimeCopyFromUser(&s, user, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    if (!s.Length && (ULONG_PTR)s.Buffer < 0x10000) {
        *atom = (ATOM)(ULONG_PTR)s.Buffer;
        return STATUS_SUCCESS;
    }
    return W32CaptureString(user, name, nullptr);
}

/* the CREATESTRUCTW given to WM_NCCREATE and WM_CREATE, with its strings appended. */
static PVOID build_createstruct(WND *w, LPVOID param, HWND parent, int x, int y, int cx, int cy, PCWSTR name,
                                PCWSTR class_name, ULONG *size)
{
    SIZE_T name_bytes = name ? (wcslen(name) + 1) * sizeof(WCHAR) : 0;
    SIZE_T class_bytes = (wcslen(class_name) + 1) * sizeof(WCHAR);
    SIZE_T total = sizeof(CREATESTRUCTW) + name_bytes + class_bytes;
    PUCHAR buf = W32Alloc(total);
    if (!buf)
        return nullptr;
    CREATESTRUCTW *cs = (CREATESTRUCTW *)buf;
    cs->lpCreateParams = param;
    cs->hInstance = w->Instance;
    cs->hMenu = (HMENU)w->Id;
    cs->hwndParent = parent;
    cs->x = x;
    cs->y = y;
    cs->cx = cx;
    cs->cy = cy;
    cs->style = (LONG)w->Style;
    cs->dwExStyle = w->ExStyle;
    SIZE_T off = sizeof(CREATESTRUCTW);
    if (name) {
        memcpy(buf + off, name, name_bytes);
        cs->lpszName = (LPCWSTR)off;
        off += name_bytes;
    }
    memcpy(buf + off, class_name, class_bytes);
    cs->lpszClass = (LPCWSTR)off;
    *size = (ULONG)total;
    return buf;
}

/*
 * HCBT_CREATEWND comes before the first message. the hook may change the place, the size and the window it goes
 * after, or refuse the window.
 */
static BOOLEAN cbt_create(WND *w, LPVOID param, HWND parent, int placed[4], PCWSTR name, PCWSTR class_name,
                          HWND *after)
{
    ULONG cs_size;
    PUCHAR cs = build_createstruct(w, param, parent, placed[0], placed[1], placed[2], placed[3], name, class_name,
                                   &cs_size);
    if (!cs)
        return FALSE;
    ULONG size = sizeof(CBT_CREATEWNDW) + cs_size;
    PUCHAR data = W32Alloc(size);
    if (!data) {
        W32Free(cs);
        return FALSE;
    }
    CBT_CREATEWNDW *cbt = (CBT_CREATEWNDW *)data;
    cbt->lpcs = nullptr;
    cbt->hwndInsertAfter = *after;
    memcpy(cbt + 1, cs, cs_size);
    W32Free(cs);
    BOOLEAN refused = HookCbt(w, HCBT_CREATEWND, (WPARAM)WndHandle(w), (LPARAM)data, size);
    const CREATESTRUCTW *out = (const CREATESTRUCTW *)(cbt + 1);
    placed[0] = out->x, placed[1] = out->y, placed[2] = out->cx, placed[3] = out->cy;
    *after = cbt->hwndInsertAfter;
    W32Free(data);
    return !refused;
}

HWND NTAPI NtUserCreateWindowEx(DWORD ex_style, PUNICODE_STRING user_class, PUNICODE_STRING user_name, DWORD style,
                                int x, int y, int cx, int cy, HWND parent_h, HMENU menu, HINSTANCE instance,
                                LPVOID param, DWORD flags)
{
    PWSTR class_name = nullptr, name = nullptr;
    ATOM atom;
    SIZE_T name_chars = 0;
    HWND result = nullptr;
    if (!NT_SUCCESS(capture_class_name(user_class, &class_name, &atom)))
        return nullptr;
    if (user_name) {
        UNICODE_STRING s;
        if (!NT_SUCCESS(MimeCopyFromUser(&s, user_name, sizeof(s)))) {
            W32Free(class_name);
            return nullptr;
        }
        if (!s.Length && s.Buffer && (ULONG_PTR)s.Buffer < 0x10000) {
            /* dialog templates may name a resource with 0xFFFF followed by its ordinal. */
            name = W32Alloc(3 * sizeof(WCHAR));
            if (name) {
                name[0] = 0xFFFF;
                name[1] = (WCHAR)(ULONG_PTR)s.Buffer;
                name_chars = 2;
            }
        } else if (!NT_SUCCESS(W32CaptureString(user_name, &name, &name_chars))) {
            W32Free(class_name);
            return nullptr;
        }
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    WND *desktop = WndDesktop();
    WND_CLASS *cls = t ? ClassFind(t->Process, class_name, atom, instance) : nullptr;
    if (!cls || !desktop) {
        DbgPrint("win32k: CreateWindowEx: class %ws (%x) not found\n", class_name ? class_name : L"", atom);
        goto out;
    }

    BOOLEAN message_only = parent_h == HWND_MESSAGE;
    WND *parent = desktop, *owner = nullptr;
    if (parent_h && !message_only) {
        WND *p = WndLookup(parent_h);
        if (!p)
            goto out;
        if (style & WS_CHILD)
            parent = p;
        else
            owner = p == desktop ? nullptr : WndTopLevel(p);
    } else if (style & WS_CHILD) {
        if (!message_only)
            goto out; /* children need a parent. */
        style &= ~WS_CHILD;
    }

    WND *w = W32Alloc(sizeof(WND));
    if (!w)
        goto out;
    w->Extra = cls->WindowExtra > 0 ? W32Alloc((SIZE_T)cls->WindowExtra) : nullptr;
    w->ExtraSize = cls->WindowExtra > 0 ? (ULONG)cls->WindowExtra : 0;
    if (!UserCreateHandle(&w->Header, USER_TYPE_WINDOW)) {
        W32Free(w->Extra);
        W32Free(w);
        goto out;
    }
    RgnInit(&w->Update);
    w->Thread = t;
    w->Class = cls;
    cls->Windows++;
    w->Proc = ClassProc(cls, t->Process);
    w->Ansi = cls->Ansi;
    w->Instance = instance;
    w->Owner = owner;
    /*
     * a child works in the pixels of its parent, unless the parent was made by a thread that hosts windows of any
     * awareness and the child is not more aware than it, and a top level window in those of its thread.
     */
    w->Hosting = (UCHAR)t->DpiHosting;
    w->Awareness = (UCHAR)(parent != desktop && !DpiChildIsolated(parent, DpiThreadAwareness(t)) ? parent->Awareness
                                                                                                : DpiThreadAwareness(t));
    w->Dpi = parent != desktop && w->Awareness == parent->Awareness ? parent->Dpi : DpiScaleOf(w->Awareness);
    if (w->Awareness == DPI_AWARE_PER_MONITOR_V2)
        w->Flags |= WNDF_DPI_CHILDREN;
    w->Icon = cls->Icon;
    w->IconSmall = cls->IconSmall;
    if (cls->Fnid == MIME_FNID_DIALOG)
        w->Flags |= WNDF_DIALOG;
    if (message_only)
        ex_style |= 0x80000000u; /* private marker: message only window, never visible. */
    if (!(style & WS_CHILD)) {
        style |= WS_CLIPSIBLINGS;
        if (!(style & WS_POPUP))
            style |= WS_CAPTION;
    }
    if ((style & WS_CAPTION) == WS_CAPTION || (style & WS_THICKFRAME))
        ex_style |= WS_EX_WINDOWEDGE;
    if (ex_style & WS_EX_DLGMODALFRAME)
        ex_style |= WS_EX_WINDOWEDGE;
    w->Style = style & ~(WS_VISIBLE | WS_MINIMIZE | WS_MAXIMIZE);
    w->MinPosition = (POINT){-1, -1};
    w->MaxPosition = (POINT){-1, -1};
    w->ExStyle = ex_style;
    if (style & WS_CHILD) {
        w->Id = (UINT_PTR)menu;
    } else {
        MenuWindowSetMenu(w, menu);
        /* the keyboard cues of a top level window are hidden until the keyboard is used. */
        w->UiState = MetricsKeyboardCues() ? 0 : UISF_HIDEFOCUS | UISF_HIDEACCEL;
    }
    if (name)
        w->Text = W32StrDup(name, name_chars), w->TextLength = w->Text ? (ULONG)name_chars : 0;

    /* default positions for overlapped windows. */
    UINT dpi = w->Dpi;
    if (!(style & (WS_CHILD | WS_POPUP)) && !message_only) {
        RECT work = DpiWorkArea(w->Awareness);
        if (x == (int)CW_USEDEFAULT) {
            LONG step = W32MulDiv(26, (LONG)dpi, 96);
            ULONG n = g_cascade++ % 8;
            x = work.left + step * (LONG)(n + 1);
            if (y == (int)CW_USEDEFAULT || y == SW_SHOW || !y)
                y = work.top + step * (LONG)(n + 1);
        }
        if (cx == (int)CW_USEDEFAULT) {
            cx = (work.right - work.left) * 3 / 4;
            cy = (work.bottom - work.top) * 3 / 4;
        }
    } else {
        if (x == (int)CW_USEDEFAULT)
            x = y = 0;
        if (cx == (int)CW_USEDEFAULT)
            cx = cy = 0;
    }
    if (cx < 0)
        cx = 0;
    if (cy < 0)
        cy = 0;
    /* a child that took the awareness of its parent is placed in the pixels of its parent. */
    ULONG creator = DpiThreadAwareness(t);
    if (w->Awareness != creator) {
        x = DpiScale(x, DpiScaleOf(creator), DpiScaleOf(w->Awareness));
        y = DpiScale(y, DpiScaleOf(creator), DpiScaleOf(w->Awareness));
        cx = DpiScale(cx, DpiScaleOf(creator), DpiScaleOf(w->Awareness));
        cy = DpiScale(cy, DpiScaleOf(creator), DpiScaleOf(w->Awareness));
    }

    HWND after = (style & WS_CHILD) ? HWND_BOTTOM : HWND_TOP;
    link_window(parent, w, after);
    RECT rect = {x, y, x + cx, y + cy};
    parent_to_screen(parent, w->Awareness, &rect);
    w->WindowRect = rect;
    w->ClientRect = rect;
    /* per-monitor-aware windows start with their monitor's DPI. */
    if (parent == desktop && w->Awareness >= DPI_AWARE_PER_MONITOR && !message_only)
        w->Dpi = w->MonitorDpi = MonitorDpiOfRect(&rect);
    UserReference(&w->Header);

    if (HookActive(WH_CBT, t)) {
        PCWSTR hook_class = cls->Name ? cls->Name : L"";
        int placed[4] = {x, y, cx, cy};
        if (!cbt_create(w, param, parent_h, placed, name, hook_class, &after) || !alive(w))
            goto fail;
        if (after != ((style & WS_CHILD) ? HWND_BOTTOM : HWND_TOP)) {
            unlink_window(w);
            link_window(parent, w, after);
        }
        x = placed[0], y = placed[1], cx = W32Max(placed[2], 0), cy = W32Max(placed[3], 0);
        rect = (RECT){x, y, x + cx, y + cy};
        parent_to_screen(parent, w->Awareness, &rect);
        w->WindowRect = rect;
        w->ClientRect = rect;
    }

    if ((style & WS_THICKFRAME) || !(style & (WS_POPUP | WS_CHILD))) {
        MINMAXINFO mmi;
        WndGetMinMaxInfo(w, &mmi);
        if (!alive(w))
            goto fail;
        if (cx > mmi.ptMaxTrackSize.x)
            cx = mmi.ptMaxTrackSize.x;
        if (cy > mmi.ptMaxTrackSize.y)
            cy = mmi.ptMaxTrackSize.y;
        if ((style & WS_THICKFRAME)) {
            cx = W32Max(cx, mmi.ptMinTrackSize.x);
            cy = W32Max(cy, mmi.ptMinTrackSize.y);
        }
        rect = (RECT){x, y, x + cx, y + cy};
        parent_to_screen(parent, w->Awareness, &rect);
        w->WindowRect = rect;
        w->ClientRect = rect;
    }

    ULONG cs_size;
    PCWSTR real_class = cls->Name ? cls->Name : L"";
    PVOID cs = build_createstruct(w, param, parent_h, x, y, cx, cy, name, real_class, &cs_size);
    if (!cs)
        goto fail;
    LRESULT r = send_kernel(w, WM_NCCREATE, 0, cs, MIME_LPARAM_CREATESTRUCT, cs_size);
    if (!alive(w) || !r) {
        W32Free(cs);
        goto fail;
    }

    RECT window = {x, y, x + cx, y + cy}, client;
    calc_client(w, &window, &window, &client, FALSE);
    if (!alive(w)) {
        W32Free(cs);
        goto fail;
    }
    parent_to_screen(parent, w->Awareness, &client);
    w->ClientRect = client;

    r = send_kernel(w, WM_CREATE, 0, cs, MIME_LPARAM_CREATESTRUCT, cs_size);
    W32Free(cs);
    if (!alive(w) || r == -1)
        goto fail;
    w->Flags |= WNDF_SENT_CREATE;

    if (!(w->Flags & WNDF_DESTROYING)) {
        MsgSend(w, WM_SIZE, SIZE_RESTORED,
                MAKELPARAM(w->ClientRect.right - w->ClientRect.left, w->ClientRect.bottom - w->ClientRect.top));
        RECT c = w->ClientRect;
        screen_to_parent(w, &c);
        MsgSend(w, WM_MOVE, 0, MAKELPARAM(c.left, c.top));
    }
    if (!alive(w))
        goto fail;
    notify_parent(w, WM_CREATE);
    if (!alive(w))
        goto fail;

    if (style & WS_MINIMIZE) {
        WndShow(w, SW_SHOWMINIMIZED);
    } else if (style & WS_MAXIMIZE) {
        WndShow(w, SW_MAXIMIZE);
    } else if (style & WS_VISIBLE) {
        if (style & WS_CHILD)
            WndSetPos(w, nullptr, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        else
            WndShow(w, SW_SHOW);
    }
    if (alive(w))
        result = WndHandle(w);
    UserDereference(&w->Header);
    goto out;

fail:
    if (alive(w) && !(w->Flags & WNDF_DESTROYING))
        WndDestroy(w);
    UserDereference(&w->Header);
out:
    UserLeave();
    W32Free(class_name);
    W32Free(name);
    return result;
}

/* services. */

static BOOLEAN can_touch(WND *w)
{
    return w && w != g_desktop;
}

BOOL NTAPI NtUserDestroyWindow(HWND hwnd)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    if (can_touch(w)) {
        W32THREAD *t = W32GetThread();
        if (w->Thread == t) {
            UserReference(&w->Header);
            if (!HookCbt(w, HCBT_DESTROYWND, (WPARAM)hwnd, 0, 0) && alive(w)) {
                WndDestroy(w);
                ok = TRUE;
            }
            UserDereference(&w->Header);
        }
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserShowWindow(HWND hwnd, int cmd)
{
    if ((UINT)cmd > SW_FORCEMINIMIZE) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL was = FALSE;
    if (can_touch(w)) {
        was = (w->Style & WS_VISIBLE) != 0;
        WndShow(w, cmd);
    }
    UserLeave();
    return was;
}

BOOL NTAPI NtUserShowWindowAsync(HWND hwnd, int cmd)
{
    if ((UINT)cmd > SW_FORCEMINIMIZE) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL queued = FALSE;
    if (can_touch(w)) {
        queued = QueueWindowOperation(w->Thread, hwnd, (UINT)cmd, nullptr);
        if (!queued)
            W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } else {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    }
    UserLeave();
    return queued;
}

BOOL NTAPI NtUserSetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    if (can_touch(w)) {
        POINT at = {x, y}, size = {cx, cy};
        /* a top level window is placed on the screen and a child in its parent. */
        if (WndIsTopLevel(w))
            point_from_caller(w, &at);
        else
            size_from_caller(w, &at);
        size_from_caller(w, &size);
        WndSetPos(w, (WND *)after, at.x, at.y, size.x, size.y, flags);
        ok = TRUE;
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserEnableWindow(HWND hwnd, BOOL enable)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL was_disabled = FALSE;
    if (can_touch(w)) {
        was_disabled = (w->Style & WS_DISABLED) != 0;
        if (enable && was_disabled) {
            w->Style &= ~WS_DISABLED;
            MsgSend(w, WM_ENABLE, TRUE, 0);
        } else if (!enable && !was_disabled) {
            MsgSend(w, WM_CANCELMODE, 0, 0);
            if (alive(w)) {
                w->Style |= WS_DISABLED;
                W32THREAD *t = w->Thread;
                if (t && (t->Focus == WndHandle(w) || WndIsChildOf(WndLookup(t->Focus), w)))
                    WndSetFocus(nullptr);
                if (alive(w))
                    MsgSend(w, WM_ENABLE, FALSE, 0);
            }
        }
        if (alive(w) && WndIsTopLevel(w))
            WndUpdateHostConfig(w, FALSE);
    }
    UserLeave();
    return was_disabled;
}

HWND NTAPI NtUserSetParent(HWND hwnd, HWND parent_h)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    WND *p = parent_h ? WndLookup(parent_h) : WndDesktop();
    HWND old = nullptr;
    /* a window of the same program cannot go under one of another awareness unless the parent hosts mixed ones. */
    if (w && p && p != g_desktop && w->Thread && p->Thread && w->Thread->Process == p->Thread->Process &&
        W32Min(w->Awareness, DPI_AWARE_PER_MONITOR) != W32Min(p->Awareness, DPI_AWARE_PER_MONITOR) &&
        !DpiChildIsolated(p, w->Awareness)) {
        W32SetLastError(5023);
        UserLeave();
        return nullptr;
    }
    if (can_touch(w) && p && p != w && !WndIsChildOf(p, w)) {
        old = WndHandle(w->Parent);
        BOOLEAN visible = (w->Style & WS_VISIBLE) != 0;
        if (visible)
            WndSetPos(w, nullptr, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT r = w->WindowRect;
        screen_to_parent(w, &r);
        WND *old_parent = w->Parent;
        unlink_window(w);
        if (old_parent == g_desktop && p != g_desktop && w->Surface) {
            HostWindowDestroy(w);
            w->Flags &= ~WNDF_HOST_WINDOW;
            PaintRetireSurface(w->Surface);
            w->Surface = nullptr;
        }
        link_window(p, w, HWND_TOP);
        /* keep the position relative to the new parent. */
        RECT nr = r;
        parent_to_screen(p, w->Awareness, &nr);
        offset_tree(w, nr.left - w->WindowRect.left, nr.top - w->WindowRect.top);
        if (visible)
            WndSetPos(w, nullptr, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        /* the window takes the monitor of its new parent, and tells its program when the dpi is not the same. */
        if (alive(w) && w->Awareness >= DPI_AWARE_PER_MONITOR) {
            if (p == g_desktop) {
                WndCheckMonitorDpi(w);
            } else if (p->Awareness >= DPI_AWARE_PER_MONITOR && p->Dpi && p->Dpi != w->Dpi) {
                UINT before = w->Dpi;
                set_tree_dpi(w, p->Dpi);
                WndNotifyDpiChanged(w, before, nullptr);
            }
        }
    }
    UserLeave();
    return old;
}

static WND *root_owner(WND *w)
{
    w = WndTopLevel(w);
    while (w && w->Owner)
        w = w->Owner;
    return w;
}

HWND NTAPI NtUserGetAncestor(HWND hwnd, UINT flags)
{
    UserEnter();
    WND *w = WndLookup(hwnd), *r = nullptr;
    if (w && w != g_desktop) {
        switch (flags) {
        case GA_PARENT:
            r = w->Parent;
            break;
        case GA_ROOT:
            r = WndTopLevel(w);
            break;
        case GA_ROOTOWNER:
            r = root_owner(w);
            break;
        }
    }
    HWND h = WndHandle(r);
    UserLeave();
    return h;
}

static LONG_PTR get_long(WND *w, int index, BOOL ansi, BOOLEAN *ok)
{
    *ok = TRUE;
    switch (index) {
    case GWLP_WNDPROC:
        return (LONG_PTR)w->Proc;
    case GWLP_HINSTANCE:
        return (LONG_PTR)w->Instance;
    case GWLP_HWNDPARENT:
        return (LONG_PTR)((w->Style & WS_CHILD) ? WndHandle(w->Parent) : WndHandle(w->Owner));
    case GWLP_ID:
        return (LONG_PTR)w->Id;
    case GWL_STYLE:
        return (LONG)w->Style;
    case GWL_EXSTYLE:
        return (LONG)(w->ExStyle & 0x7FFFFFFFu);
    case GWLP_USERDATA:
        return w->UserData;
    }
    if (index >= 0 && w->Extra && (ULONG)index + sizeof(LONG) <= w->ExtraSize) {
        LONG_PTR v = 0;
        SIZE_T n = (ULONG)index + sizeof(LONG_PTR) <= w->ExtraSize ? sizeof(LONG_PTR) : sizeof(LONG);
        memcpy(&v, w->Extra + index, n);
        return v;
    }
    (void)ansi;
    *ok = FALSE;
    return 0;
}

static LONG_PTR set_long(WND *w, int index, LONG_PTR value, BOOL ansi, SIZE_T size)
{
    LONG_PTR old;
    switch (index) {
    case GWLP_WNDPROC:
        old = (LONG_PTR)w->Proc;
        w->Proc = (WNDPROC)value;
        w->Ansi = ansi != 0;
        return old;
    case GWLP_HINSTANCE:
        old = (LONG_PTR)w->Instance;
        w->Instance = (HINSTANCE)value;
        return old;
    case GWLP_HWNDPARENT:
        if (w->Style & WS_CHILD)
            return 0;
        old = (LONG_PTR)WndHandle(w->Owner);
        w->Owner = value ? WndTopLevel(WndLookup((HWND)value)) : nullptr;
        if (w->Owner == w)
            w->Owner = nullptr;
        return old;
    case GWLP_ID:
        old = (LONG_PTR)w->Id;
        if (w->Style & WS_CHILD)
            w->Id = (UINT_PTR)value;
        else
            MenuWindowSetMenu(w, (HMENU)value);
        return old;
    case GWLP_USERDATA:
        old = w->UserData;
        w->UserData = value;
        return old;
    case GWL_STYLE:
    case GWL_EXSTYLE: {
        STYLESTRUCT ss;
        BOOLEAN is_style = index == GWL_STYLE;
        old = (LONG)(is_style ? w->Style : w->ExStyle & 0x7FFFFFFFu);
        ss.styleOld = (DWORD)old;
        ss.styleNew = (DWORD)value;
        MsgSendKernel(w, WM_STYLECHANGING, (WPARAM)index, (LPARAM)&ss, MIME_LPARAM_INOUT, sizeof(ss));
        if (!alive(w))
            return old;
        if (is_style) {
            DWORD visible = w->Style & WS_VISIBLE;
            w->Style = (ss.styleNew & ~WS_VISIBLE) | visible;
            /* restoring the application frame ends its fullscreen host space. */
            if ((w->Flags & WNDF_APP_FULLSCREEN) &&
                ((w->Style & WS_CAPTION) == WS_CAPTION || (w->Style & WS_THICKFRAME)))
                w->Flags &= ~(WNDF_FULLSCREEN | WNDF_APP_FULLSCREEN);
            if ((ss.styleNew & WS_VISIBLE) != visible)
                WndShow(w, (ss.styleNew & WS_VISIBLE) ? SW_SHOWNA : SW_HIDE);
        } else {
            w->ExStyle = (ss.styleNew & 0x7FFFFFFFu) | (w->ExStyle & 0x80000000u);
            LayerStyleChanged(w);
        }
        if (alive(w))
            MsgSendKernel(w, WM_STYLECHANGED, (WPARAM)index, (LPARAM)&ss, MIME_LPARAM_IN, sizeof(ss));
        if (alive(w) && WndIsTopLevel(w))
            WndUpdateHostConfig(w, FALSE);
        return old;
    }
    }
    if (index >= 0 && w->Extra && (ULONG)index + size <= w->ExtraSize) {
        old = 0;
        memcpy(&old, w->Extra + index, size);
        memcpy(w->Extra + index, &value, size);
        return old;
    }
    return 0;
}

LONG_PTR NTAPI NtUserSetWindowLongPtr(HWND hwnd, int index, LONG_PTR value, BOOL ansi)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    LONG_PTR old = can_touch(w) ? set_long(w, index, value, ansi, sizeof(LONG_PTR)) : 0;
    UserLeave();
    return old;
}

LONG NTAPI NtUserSetWindowLong(HWND hwnd, int index, LONG value, BOOL ansi)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    LONG old = 0;
    if (can_touch(w)) {
        if (index < 0)
            old = (LONG)set_long(w, index, (LONG_PTR)value, ansi, sizeof(LONG));
        else
            old = (LONG)set_long(w, index, (LONG_PTR)(ULONG)value, ansi, sizeof(LONG));
    }
    UserLeave();
    return old;
}

BOOL NTAPI NtUserDefSetText(HWND hwnd, PUNICODE_STRING user_text)
{
    PWSTR text = nullptr;
    SIZE_T chars = 0;
    if (user_text && !NT_SUCCESS(W32CaptureString(user_text, &text, &chars)))
        return FALSE;
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (can_touch(w))
        WndSetText(w, text, (ULONG)chars);
    UserLeave();
    W32Free(text);
    return w != nullptr;
}

int NTAPI NtUserInternalGetWindowText(HWND hwnd, LPWSTR buffer, int count)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    int n = 0;
    if (w && buffer && count > 0) {
        n = (int)W32Min((LONG)w->TextLength, count - 1);
        WCHAR zero = 0;
        if (n && !NT_SUCCESS(MimeCopyToUser(buffer, w->Text, (SIZE_T)n * sizeof(WCHAR))))
            n = 0;
        MimeCopyToUser(buffer + n, &zero, sizeof(zero));
    }
    UserLeave();
    return n;
}

static BOOLEAN class_name_matches(WND *w, PCWSTR name, ATOM atom)
{
    if (!w->Class)
        return FALSE;
    if (atom)
        return w->Class->Atom == atom;
    return w->Class->Name && !_wcsicmp(w->Class->Name, name);
}

HWND NTAPI NtUserFindWindowEx(HWND parent_h, HWND after_h, PUNICODE_STRING user_class, PUNICODE_STRING user_name,
                              DWORD type)
{
    PWSTR class_name = nullptr, name = nullptr;
    ATOM atom = 0;
    BOOLEAN have_class = FALSE, have_name = FALSE;
    if (user_class) {
        UNICODE_STRING s;
        if (NT_SUCCESS(MimeCopyFromUser(&s, user_class, sizeof(s))) && (s.Buffer || s.Length)) {
            capture_class_name(user_class, &class_name, &atom);
            have_class = class_name || atom;
        }
    }
    if (user_name) {
        UNICODE_STRING s;
        if (NT_SUCCESS(MimeCopyFromUser(&s, user_name, sizeof(s))) && s.Buffer) {
            SIZE_T chars;
            W32CaptureString(user_name, &name, &chars);
            have_name = name != nullptr;
        }
    }
    UserEnter();
    HWND result = nullptr;
    WND *parent = parent_h ? WndLookup(parent_h) : WndDesktop();
    if (parent_h == HWND_MESSAGE)
        parent = WndDesktop();
    if (parent) {
        WND *c = parent->FirstChild;
        if (after_h) {
            WND *a = WndLookup(after_h);
            c = a && a->Parent == parent ? a->Next : nullptr;
        }
        for (; c; c = c->Next) {
            if (c->Flags & WNDF_DESTROYING)
                continue;
            if (have_class && !class_name_matches(c, class_name, atom))
                continue;
            if (have_name) {
                PCWSTR text = c->Text ? c->Text : L"";
                if (wcscmp(text, name))
                    continue;
            }
            result = WndHandle(c);
            break;
        }
    }
    UserLeave();
    W32Free(class_name);
    W32Free(name);
    (void)type;
    return result;
}

NTSTATUS NTAPI NtUserBuildHwndList(HDESK desktop, HWND parent_h, BOOL children, ULONG thread, ULONG max, HWND *list,
                                   ULONG *count)
{
    UserEnter();
    WND *parent = parent_h ? WndLookup(parent_h) : WndDesktop();
    ULONG n = 0;
    NTSTATUS status = STATUS_SUCCESS;
    if (!parent) {
        UserLeave();
        return STATUS_INVALID_HANDLE;
    }
    /* walk depth first when children are requested, siblings otherwise. */
    WND *c = parent->FirstChild;
    while (c) {
        if (!(c->Flags & WNDF_DESTROYING) && (!thread || (c->Thread && (ULONG)(ULONG_PTR)c->Thread->Tid == thread))) {
            if (n < max) {
                HWND h = WndHandle(c);
                if (!NT_SUCCESS(MimeCopyToUser(&list[n], &h, sizeof(h)))) {
                    status = STATUS_ACCESS_VIOLATION;
                    break;
                }
            }
            n++;
        }
        if (children && c->FirstChild) {
            c = c->FirstChild;
            continue;
        }
        while (c && !c->Next && c->Parent != parent)
            c = c->Parent;
        c = c ? c->Next : nullptr;
    }
    if (NT_SUCCESS(status) && n > max)
        status = STATUS_BUFFER_TOO_SMALL;
    MimeCopyToUser(count, &n, sizeof(n));
    UserLeave();
    (void)desktop;
    return status;
}

static void placement_offset(WND *w, RECT *rect, BOOLEAN to_screen)
{
    if (WndIsTopLevel(w) && !(w->ExStyle & WS_EX_TOOLWINDOW)) {
        RECT work = DpiWorkArea(w->Awareness);
        RectOffset(rect, to_screen ? work.left : -work.left, to_screen ? work.top : -work.top);
    }
}

BOOL NTAPI NtUserGetWindowPlacement(HWND hwnd, WINDOWPLACEMENT *user_wp)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    if (can_touch(w)) {
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(wp);
        wp.showCmd = (w->Style & WS_MINIMIZE) ? SW_SHOWMINIMIZED
                     : (w->Style & WS_MAXIMIZE) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        if ((w->Style & WS_MAXIMIZE) || (w->Flags & WNDF_RESTORETOMAX))
            wp.flags |= WPF_RESTORETOMAXIMIZED;
        if (w->Style & (WS_MINIMIZE | WS_MAXIMIZE))
            wp.rcNormalPosition = w->NormalRect;
        else if (w->Flags & WNDF_FULLSCREEN)
            wp.rcNormalPosition = w->FullscreenNormalRect;
        else {
            wp.rcNormalPosition = w->WindowRect;
            screen_to_parent(w, &wp.rcNormalPosition);
        }
        placement_offset(w, &wp.rcNormalPosition, FALSE);
        rect_to_caller(w, &wp.rcNormalPosition);
        wp.ptMinPosition = w->MinPosition;
        wp.ptMaxPosition = w->MaxPosition;
        point_to_caller(w, &wp.ptMinPosition);
        point_to_caller(w, &wp.ptMaxPosition);
        ok = NT_SUCCESS(MimeCopyToUser(user_wp, &wp, sizeof(wp)));
        if (!ok)
            W32SetLastError(ERROR_NOACCESS);
    } else {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    }
    UserLeave();
    return ok;
}

BOOL WndSetPlacement(WND *w, const WINDOWPLACEMENT *placement)
{
    WINDOWPLACEMENT wp = *placement;
    UserReference(&w->Header);
    if (w->Flags & WNDF_FULLSCREEN) {
        WndFullscreen(w, FALSE, nullptr);
        if (!alive(w)) {
            UserDereference(&w->Header);
            return FALSE;
        }
    }
    placement_offset(w, &wp.rcNormalPosition, TRUE);
    w->NormalRect = wp.rcNormalPosition;
    w->MaxPosition = wp.ptMaxPosition;
    if (wp.flags & WPF_SETMINPOSITION)
        w->MinPosition = wp.ptMinPosition;
    if (!(w->Style & (WS_MINIMIZE | WS_MAXIMIZE))) {
        RECT r = wp.rcNormalPosition;
        WndSetPos(w, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                  SWP_NOZORDER | SWP_NOACTIVATE);
    }
    BOOL ok = alive(w);
    if (ok) {
        WndShow(w, (int)wp.showCmd);
        if (alive(w) && (w->Style & WS_MINIMIZE)) {
            if (wp.flags & WPF_RESTORETOMAXIMIZED)
                w->Flags |= WNDF_RESTORETOMAX;
            else
                w->Flags &= ~WNDF_RESTORETOMAX;
        }
    }
    UserDereference(&w->Header);
    return ok;
}

BOOL NTAPI NtUserSetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT *user_wp)
{
    WINDOWPLACEMENT wp;
    if (!NT_SUCCESS(MimeCopyFromUser(&wp, user_wp, sizeof(wp)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    if (wp.length != sizeof(wp) || wp.showCmd > SW_FORCEMINIMIZE) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    if (can_touch(w)) {
        DpiRectConvert(&wp.rcNormalPosition, DpiCurrentAwareness(), w->Awareness);
        DpiPointConvert(&wp.ptMinPosition, DpiCurrentAwareness(), w->Awareness);
        DpiPointConvert(&wp.ptMaxPosition, DpiCurrentAwareness(), w->Awareness);
        if ((wp.flags & WPF_ASYNCWINDOWPLACEMENT) && w->Thread != W32GetThread()) {
            ok = QueueWindowOperation(w->Thread, hwnd, wp.showCmd, &wp);
            if (!ok)
                W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        } else {
            ok = WndSetPlacement(w, &wp);
        }
    } else {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    }
    UserLeave();
    return ok;
}

HWND NTAPI NtUserSetFocus(HWND hwnd)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    HWND old = t ? t->Focus : nullptr;
    WND *w = hwnd ? WndLookup(hwnd) : nullptr;
    if (hwnd && (!can_touch(w) || (w->Style & WS_DISABLED) || w->Thread != t)) {
        UserLeave();
        return nullptr;
    }
    if (w && (w->Style & WS_MINIMIZE)) {
        UserLeave();
        return old;
    }
    WndSetFocus(w);
    UserLeave();
    return old;
}

HWND NTAPI NtUserSetActiveWindow(HWND hwnd)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    HWND old = t ? t->Active : nullptr;
    WND *w = hwnd ? WndLookup(hwnd) : nullptr;
    if (w && can_touch(w) && WndIsTopLevel(w) && w->Thread == t)
        WndActivate(w, FALSE);
    else if (!hwnd && t && InputForeground && InputForeground->Thread == t)
        InputSetForeground(nullptr);
    UserLeave();
    return old;
}

HWND NTAPI NtUserGetForegroundWindow(void)
{
    UserEnter();
    HWND h = WndHandle(InputForeground);
    UserLeave();
    return h;
}

/* hwnd multiplexers. */

static ULONG text_length(WND *w)
{
    return w->TextLength;
}

ULONG_PTR NTAPI NtUserCallHwnd(HWND hwnd, ULONG routine)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    ULONG_PTR r = 0;
    if (w) {
        switch (routine) {
        case MIME_HWND_GETPARENT:
            /* GetParent: the parent of a child, the owner of a popup. */
            if (w->Style & WS_CHILD)
                r = (ULONG_PTR)WndHandle(w->Parent);
            else if (w->Style & WS_POPUP)
                r = (ULONG_PTR)WndHandle(w->Owner);
            break;
        case MIME_HWND_ISWINDOW:
            r = 1;
            break;
        case MIME_HWND_ISVISIBLE:
            r = WndIsVisible(w);
            break;
        case MIME_HWND_ISENABLED:
            r = !(w->Style & WS_DISABLED);
            break;
        case MIME_HWND_GETDPI:
            r = WndGetDpi(w);
            break;
        case MIME_HWND_SETFOREGROUND:
            if (can_touch(w)) {
                WndActivate(w, FALSE);
                r = 1;
            }
            break;
        case MIME_HWND_GETTEXTLENGTH:
            r = text_length(w);
            break;
        case MIME_HWND_UPDATEWINDOW:
            r = 1;
            break;
        case MIME_HWND_ISICONIC:
            r = (w->Style & WS_MINIMIZE) != 0;
            break;
        case MIME_HWND_ISZOOMED:
            r = (w->Style & WS_MAXIMIZE) != 0;
            break;
        case MIME_HWND_MONITOR: {
            RECT physical = w->WindowRect;
            DpiRectConvert(&physical, w->Awareness, DPI_PHYSICAL);
            LONG index = MonitorFromRectIndex(&physical, TRUE);
            r = (ULONG_PTR)MonitorHandle(index < 0 ? 0 : (ULONG)index);
            break;
        }
        case MIME_HWND_ISUNICODE:
            r = !w->Ansi;
            break;
        case MIME_HWND_GETDPIAWARENESS:
            r = w->Awareness;
            break;
        case MIME_HWND_GETDPIHOSTING:
            r = w->Hosting;
            break;
        case MIME_HWND_ISDIALOG:
            r = (w->Flags & WNDF_DIALOG) != 0;
            break;
        case MIME_HWND_GETCONTROLDPIBEHAVIOR:
            r = w->ControlDpi;
            break;
        case MIME_HWND_ISCHILDDPIMESSAGE:
            r = DpiChildMessageEnabled(w);
            break;
        case MIME_HWND_GETWINDOWDPI:
            r = DpiWindowDpi(w);
            break;
        }
    }
    UserLeave();
    if (routine == MIME_HWND_UPDATEWINDOW && w)
        NtUserRedrawWindow(hwnd, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
    return r;
}

static WND *get_window_rel(WND *w, UINT cmd)
{
    switch (cmd) {
    case GW_HWNDFIRST:
        return w->Parent ? w->Parent->FirstChild : nullptr;
    case GW_HWNDLAST:
        return w->Parent ? w->Parent->LastChild : nullptr;
    case GW_HWNDNEXT:
        return w->Next;
    case GW_HWNDPREV:
        return w->Prev;
    case GW_OWNER:
        return w->Owner;
    case GW_CHILD:
        return w->FirstChild;
    case GW_ENABLEDPOPUP: {
        for (WND *c = g_desktop->FirstChild; c; c = c->Next)
            if (c->Owner == w && (c->Style & WS_VISIBLE) && !(c->Style & WS_DISABLED) && (c->Style & WS_POPUP))
                return c;
        return nullptr;
    }
    }
    return nullptr;
}

static WND *find_dlg_item(WND *w, UINT_PTR id)
{
    for (WND *c = w->FirstChild; c; c = c->Next)
        if (c->Id == id && !(c->Flags & WNDF_DESTROYING))
            return c;
    return nullptr;
}

/*
 * the points are in the pixels of the calling thread. a point of a window is a distance from its client corner and
 * scales by the awareness, a screen point is placed by the monitor it is on, so it goes through the physical screen.
 */
static void map_points(WND *from, WND *to, POINT *pts, UINT count)
{
    ULONG caller = DpiCurrentAwareness();
    BOOLEAN from_window = from && from != g_desktop, to_window = to && to != g_desktop;
    ULONG from_aw = from_window ? from->Awareness : caller;
    ULONG to_aw = to_window ? to->Awareness : caller;
    for (UINT i = 0; i < count; i++) {
        POINT p = pts[i];
        if (from_window) {
            UINT mdpi = scale_dpi(from, caller);
            p = (POINT){DpiLength(p.x, caller, from_aw, mdpi), DpiLength(p.y, caller, from_aw, mdpi)};
            p.x += from->ClientRect.left;
            p.y += from->ClientRect.top;
        }
        DpiPointConvert(&p, from_aw, to_aw);
        if (to_window) {
            p.x -= to->ClientRect.left;
            p.y -= to->ClientRect.top;
            UINT mdpi = scale_dpi(to, caller);
            p = (POINT){DpiLength(p.x, to_aw, caller, mdpi), DpiLength(p.y, to_aw, caller, mdpi)};
        }
        pts[i] = p;
    }
}

ULONG_PTR NTAPI NtUserCallHwndParam(HWND hwnd, ULONG_PTR param, ULONG routine)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    ULONG_PTR r = 0;
    if (!w && !((routine == MIME_HWNDPARAM_MAPPOINTS || routine == MIME_HWNDPARAM_DPICONVERT) && !hwnd))
        goto out;
    switch (routine) {
    case MIME_HWNDPARAM_GETWINDOWLONG: {
        BOOLEAN ok;
        r = (ULONG_PTR)get_long(w, (int)param, FALSE, &ok);
        break;
    }
    case MIME_HWNDPARAM_GETCLASSLONG: {
        WND_CLASS *c = w->Class;
        if (!c)
            break;
        switch ((int)param) {
        case GCLP_WNDPROC:
            r = (ULONG_PTR)ClassProc(c, W32GetProcess());
            break;
        case GCLP_HBRBACKGROUND:
            r = (ULONG_PTR)c->Background;
            break;
        case GCLP_HCURSOR:
            r = (ULONG_PTR)c->Cursor;
            break;
        case GCLP_HICON:
            r = (ULONG_PTR)c->Icon;
            break;
        case GCLP_HICONSM:
            r = (ULONG_PTR)c->IconSmall;
            break;
        case GCLP_HMODULE:
            r = (ULONG_PTR)c->Instance;
            break;
        case GCL_STYLE:
            r = c->Style;
            break;
        case GCL_CBWNDEXTRA:
            r = (ULONG_PTR)c->WindowExtra;
            break;
        case GCL_CBCLSEXTRA:
            r = (ULONG_PTR)c->ClassExtra;
            break;
        case GCW_ATOM:
            r = c->Atom;
            break;
        default:
            if ((int)param >= 0 && c->Extra && (int)param + (int)sizeof(LONG_PTR) <= c->ClassExtra)
                memcpy(&r, c->Extra + param, sizeof(LONG_PTR));
            break;
        }
        break;
    }
    case MIME_HWNDPARAM_GETWINDOW:
        r = (ULONG_PTR)WndHandle(get_window_rel(w, (UINT)param));
        break;
    case MIME_HWNDPARAM_GETWINDOWRECT: {
        RECT rect = w->WindowRect;
        rect_to_caller(w, &rect);
        r = NT_SUCCESS(MimeCopyToUser((PVOID)param, &rect, sizeof(RECT)));
        break;
    }
    case MIME_HWNDPARAM_GETCLIENTRECT: {
        POINT size = {w->ClientRect.right - w->ClientRect.left, w->ClientRect.bottom - w->ClientRect.top};
        size_to_caller(w, &size);
        RECT c = {0, 0, size.x, size.y};
        r = NT_SUCCESS(MimeCopyToUser((PVOID)param, &c, sizeof(RECT)));
        break;
    }
    case MIME_HWNDPARAM_ISCHILD: {
        WND *p = WndLookup((HWND)param);
        WND *c = w;
        r = 0;
        while (p && c && (c->Style & WS_CHILD)) {
            c = c->Parent;
            if (c == p) {
                r = 1;
                break;
            }
        }
        break;
    }
    case MIME_HWNDPARAM_GETDLGITEM:
        r = (ULONG_PTR)WndHandle(find_dlg_item(w, param));
        break;
    case MIME_HWNDPARAM_GETTHREADPROCESSID:
        if (w == g_desktop && InputThreadId) {
            DWORD pid = (DWORD)(ULONG_PTR)InputProcessId;
            if (param)
                MimeCopyToUser((PVOID)param, &pid, sizeof(pid));
            r = (ULONG_PTR)InputThreadId;
        } else if (w->Thread) {
            DWORD pid = (DWORD)(ULONG_PTR)w->Thread->Process->Pid;
            if (param)
                MimeCopyToUser((PVOID)param, &pid, sizeof(pid));
            r = (ULONG_PTR)w->Thread->Tid;
        }
        break;
    case MIME_HWNDPARAM_MAPPOINTS: {
        MIME_MAP_POINTS mp;
        if (!NT_SUCCESS(MimeCopyFromUser(&mp, (PVOID)param, sizeof(mp))) || mp.Count > 4096)
            break;
        WND *to = mp.To ? WndLookup(mp.To) : nullptr;
        POINT local[64];
        POINT *pts = mp.Count <= 64 ? local : W32Alloc(mp.Count * sizeof(POINT));
        if (!pts)
            break;
        if (NT_SUCCESS(MimeCopyFromUser(pts, mp.Points, mp.Count * sizeof(POINT)))) {
            map_points(w, to, pts, mp.Count);
            if (NT_SUCCESS(MimeCopyToUser(mp.Points, pts, mp.Count * sizeof(POINT)))) {
                LONG fx = w && w != g_desktop ? w->ClientRect.left : 0, fy = w && w != g_desktop ? w->ClientRect.top : 0;
                LONG tx = to && to != g_desktop ? to->ClientRect.left : 0, ty = to && to != g_desktop ? to->ClientRect.top : 0;
                r = (ULONG_PTR)MAKELONG((WORD)(fx - tx), (WORD)(fy - ty));
                if (!r)
                    r = 0x80000000u; /* nonzero success marker for a zero offset. */
            }
        }
        if (pts != local)
            W32Free(pts);
        break;
    }
    case MIME_HWNDPARAM_GETWINDOWINFO: {
        WINDOWINFO wi;
        memset(&wi, 0, sizeof(wi));
        wi.cbSize = sizeof(wi);
        wi.rcWindow = w->WindowRect;
        wi.rcClient = w->ClientRect;
        rect_to_caller(w, &wi.rcWindow);
        rect_to_caller(w, &wi.rcClient);
        wi.dwStyle = w->Style;
        wi.dwExStyle = w->ExStyle & 0x7FFFFFFFu;
        wi.dwWindowStatus = InputForeground == w ? 1 /* WS_ACTIVECAPTION */ : 0;
        FRAME_METRICS m;
        FrameGetMetrics(w->Style, w->ExStyle, WndFrameDpi(w), &m);
        wi.cxWindowBorders = (UINT)(wi.rcClient.left - wi.rcWindow.left);
        wi.cyWindowBorders = (UINT)(wi.rcWindow.bottom - wi.rcClient.bottom);
        wi.atomWindowType = w->Class ? w->Class->Atom : 0;
        wi.wCreatorVersion = 0x0400;
        r = NT_SUCCESS(MimeCopyToUser((PVOID)param, &wi, sizeof(wi)));
        break;
    }
    case MIME_HWNDPARAM_GETTITLEBARINFO: {
        TITLEBARINFOEX info;
        if (!NT_SUCCESS(MimeCopyFromUser(&info.cbSize, (PVOID)param, sizeof(info.cbSize))))
            break;
        /* GetTitleBarInfo wants the short structure, WM_GETTITLEBARINFOEX the one with the rectangles. */
        if (info.cbSize != sizeof(TITLEBARINFO) && info.cbSize != sizeof(TITLEBARINFOEX)) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            break;
        }
        FrameGetTitleBarInfo(w, &info);
        rect_to_caller(w, &info.rcTitleBar);
        for (int i = 0; i < 6; i++)
            if (!RectEmpty(&info.rgrect[i]))
                rect_to_caller(w, &info.rgrect[i]);
        r = NT_SUCCESS(MimeCopyToUser((PVOID)param, &info, info.cbSize));
        break;
    }
    case MIME_HWNDPARAM_SETICON: {
        MIME_SET_ICON si;
        if (!NT_SUCCESS(MimeCopyFromUser(&si, (PVOID)param, sizeof(si))))
            break;
        if (si.Which == ICON_BIG) {
            r = (ULONG_PTR)w->Icon;
            w->Icon = si.Icon;
        } else {
            r = (ULONG_PTR)w->IconSmall;
            w->IconSmall = si.Icon;
        }
        if (FrameHasCaption(w->Style, w->ExStyle)) {
            w->Flags |= WNDF_FRAME_DIRTY;
            wake_paint(w);
        }
        break;
    }
    case MIME_HWNDPARAM_GETICON:
        r = (ULONG_PTR)(param == ICON_BIG ? w->Icon : param == ICON_SMALL ? w->IconSmall
                                                                           : (w->IconSmall ? w->IconSmall : w->Icon));
        break;
    case MIME_HWNDPARAM_SETDIALOGINFO:
        if (param)
            w->Flags |= WNDF_DIALOG;
        else
            w->Flags &= ~WNDF_DIALOG;
        r = 1;
        break;
    case MIME_HWNDPARAM_ADJUSTRECT: {
        MIME_ADJUST_RECT ar;
        if (!NT_SUCCESS(MimeCopyFromUser(&ar, (PVOID)param, sizeof(ar))))
            break;
        FrameAdjustRect(&ar.Rect, ar.Style, ar.ExStyle, ar.Dpi ? ar.Dpi : WndFrameDpi(w));
        if (ar.Menu)
            ar.Rect.top -= MetricsGet(SM_CYMENU, ar.Dpi ? ar.Dpi : WndFrameDpi(w));
        r = NT_SUCCESS(MimeCopyToUser((PVOID)param, &ar, sizeof(ar)));
        break;
    }
    case MIME_HWNDPARAM_CHILDFROMPOINT: {
        POINT pt = {(SHORT)LOWORD(param), (SHORT)HIWORD(param)};
        size_from_caller(w, &pt);
        pt.x += w->ClientRect.left;
        pt.y += w->ClientRect.top;
        WND *c = WndChildFromPoint(w, pt, FALSE);
        r = (ULONG_PTR)WndHandle(c ? c : (RectContains(&w->ClientRect, pt.x, pt.y) ? w : nullptr));
        break;
    }
    case MIME_HWNDPARAM_SETCONTEXTHELPID:
        w->ContextHelpId = (DWORD)param;
        r = 1;
        break;
    case MIME_HWNDPARAM_GETCONTEXTHELPID:
        r = w->ContextHelpId;
        break;
    case MIME_HWNDPARAM_SETWINDOWWORD: {
        ULONG offset = (ULONG)(param >> 16);
        r = (ULONG_PTR)-1;
        if (can_touch(w) && w->Extra && offset + sizeof(WORD) <= w->ExtraSize)
            r = (WORD)set_long(w, (int)offset, (WORD)param, FALSE, sizeof(WORD));
        break;
    }
    case MIME_HWNDPARAM_DPICONVERT: {
        MIME_DPI_CONVERT dc;
        if (!NT_SUCCESS(MimeCopyFromUser(&dc, (PVOID)param, sizeof(dc))))
            break;
        ULONG thread = DpiCurrentAwareness(), own = w ? w->Awareness : thread;
        POINT pt = dc.Point;
        BOOLEAN ok = TRUE;
        switch (dc.Op) {
        case MIME_DPI_WINDOW_TO_PHYSICAL:
            DpiPointConvert(&pt, own, DPI_PHYSICAL);
            break;
        case MIME_DPI_PHYSICAL_TO_WINDOW:
            DpiPointConvert(&pt, DPI_PHYSICAL, own);
            break;
        case MIME_DPI_THREAD_TO_PHYSICAL:
            DpiPointConvert(&pt, thread, own);
            ok = !w || RectContains(&w->WindowRect, pt.x, pt.y);
            DpiPointConvert(&pt, own, DPI_PHYSICAL);
            break;
        case MIME_DPI_PHYSICAL_TO_THREAD:
            DpiPointConvert(&pt, DPI_PHYSICAL, own);
            ok = !w || RectContains(&w->WindowRect, pt.x, pt.y);
            break;
        default:
            ok = FALSE;
        }
        dc.Point = pt;
        r = ok && NT_SUCCESS(MimeCopyToUser((PVOID)param, &dc, sizeof(dc)));
        break;
    }
    case MIME_HWNDPARAM_PRESENTLAYER: {
        MIME_PRESENT_LAYER layer;
        ScreenLayer(w, &layer);
        r = NT_SUCCESS(MimeCopyToUser((PVOID)param, &layer, sizeof(layer)));
        break;
    }
    case MIME_HWNDPARAM_ENABLENCDPI:
        /* a per monitor window scales its frame itself from now on, a per monitor v2 one always did. */
        if (!w->Thread || w->Thread->Process != W32GetProcess()) {
            W32SetLastError(ERROR_ACCESS_DENIED);
        } else if (WndIsTopLevel(w) && w->Awareness >= DPI_AWARE_PER_MONITOR) {
            if (w->Awareness == DPI_AWARE_PER_MONITOR && !(w->Flags & WNDF_NCDPI)) {
                w->Flags |= WNDF_NCDPI;
                /* the frame of a window that is still being created is calculated after its creation. */
                if (w->Flags & WNDF_SENT_CREATE)
                    WndSetPos(w, nullptr, 0, 0, 0, 0,
                              SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            r = 1;
        }
        break;
    case MIME_HWNDPARAM_CONTROLDPIBEHAVIOR:
        /* only a window of the program can be changed. */
        if (w->Thread && w->Thread->Process == W32GetProcess()) {
            UCHAR mask = (UCHAR)(param & 3);
            w->ControlDpi = (UCHAR)(((param >> 8) & mask) | (w->ControlDpi & ~mask));
            r = 1;
        } else {
            W32SetLastError(ERROR_ACCESS_DENIED);
        }
        break;
    case MIME_HWNDPARAM_DPICHILDREN:
        /* only a per monitor top level window decides, the v2 ones always tell their children. */
        if (WndIsTopLevel(w) && w->Awareness == DPI_AWARE_PER_MONITOR) {
            if (param)
                w->Flags |= WNDF_DPI_CHILDREN;
            else
                w->Flags &= ~WNDF_DPI_CHILDREN;
            r = 1;
        }
        break;
    case MIME_HWNDPARAM_SETTOUCH:
        w->TouchFlags = (ULONG)param;
        r = 1;
        break;
    case MIME_HWNDPARAM_SETAFFINITY:
        w->DisplayAffinity = (ULONG)param;
        ScreenSerial++;
        r = 1;
        break;
    case MIME_HWNDPARAM_GETAFFINITY:
        r = w->DisplayAffinity;
        break;
    case MIME_HWNDPARAM_MENUBAR:
        r = MenuBarCall(w, (PVOID)param);
        break;
    case MIME_HWNDPARAM_GETCLASSWORD: {
        /* the atom, or a whole word within the extra bytes. */
        WND_CLASS *c = w->Class;
        int index = (int)param;
        if (!c)
            break;
        if (index == GCW_ATOM) {
            r = c->Atom;
        } else if (index >= 0 && c->Extra && (SIZE_T)index + 2 <= (SIZE_T)c->ClassExtra) {
            USHORT word;
            memcpy(&word, c->Extra + index, sizeof(word));
            r = word;
        } else {
            W32SetLastError(ERROR_INVALID_INDEX);
        }
        break;
    }
    case MIME_HWNDPARAM_SYSTIMER: {
        MIME_SYS_TIMER st;
        if (!NT_SUCCESS(MimeCopyFromUser(&st, (PVOID)param, sizeof(st))))
            break;
        W32THREAD *t = W32GetThread();
        r = st.Elapse ? TimerSet(t, hwnd, st.Id, st.Elapse, nullptr, TRUE) : TimerKill(t, hwnd, st.Id, TRUE);
        break;
    }
    case MIME_HWNDPARAM_ENABLECLOSE: {
        ULONG old = w->Flags;
        if (param)
            w->Flags &= ~WNDF_NOCLOSE;
        else
            w->Flags |= WNDF_NOCLOSE;
        if (old != w->Flags && FrameHasCaption(w->Style, w->ExStyle)) {
            w->Flags |= WNDF_FRAME_DIRTY;
            wake_paint(w);
        }
        r = (old & WNDF_NOCLOSE) ? 0 : 1;
        break;
    }
    }
out:
    UserLeave();
    return r;
}

BOOL NTAPI NtUserIsTouchWindow(HWND hwnd, PULONG user_flags)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    ULONG flags = w ? w->TouchFlags : 0;
    UserLeave();
    if (user_flags) {
        ULONG out = flags & ~MIME_TOUCH_REGISTERED;
        MimeCopyToUser(user_flags, &out, sizeof(out));
    }
    return (flags & MIME_TOUCH_REGISTERED) != 0;
}

BOOL NTAPI NtUserForceWindowToDpiForTest(HWND hwnd, UINT dpi)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL r = w && can_touch(w) && WndForceDpi(w, dpi);
    UserLeave();
    return r;
}

BOOL NTAPI NtUserDeferWindowDpiChanges(HWND hwnd)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL r = w && WndDeferDpiChanges(w);
    UserLeave();
    return r;
}

BOOL NTAPI NtUserRestoreWindowDpiChanges(HWND hwnd)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL r = w && WndRestoreDpiChanges(w);
    UserLeave();
    return r;
}

/* the window decides its dpi in a later Windows, here nothing can be set. */
BOOL NTAPI NtUserSetDpiForWindow(HWND hwnd, UINT dpi)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (w)
        W32SetLastError(ERROR_NOT_SUPPORTED);
    UserLeave();
    return FALSE;
}

/* a per monitor top level window decides whether its children hear of the dpi, and the frame follows. */
BOOL NTAPI NtUserEnableChildWindowDpiMessage(HWND hwnd, BOOL enable)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL r = FALSE;
    if (w && can_touch(w) && WndIsTopLevel(w) && w->Awareness >= DPI_AWARE_PER_MONITOR) {
        if (enable)
            w->Flags |= WNDF_DPI_CHILDREN | WNDF_NCDPI;
        else
            w->Flags &= ~(WNDF_DPI_CHILDREN | WNDF_NCDPI);
        WndSetPos(w, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        r = TRUE;
    }
    UserLeave();
    return r;
}

BOOL NTAPI NtUserIsChildWindowDpiMessageEnabled(HWND hwnd)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL r = w && DpiChildMessageEnabled(w);
    UserLeave();
    return r;
}

BOOL NTAPI NtUserIsWindowBroadcastingDpiToChildren(HWND hwnd)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL r = w && WndIsTopLevel(w) && WndBroadcastsDpi(w);
    UserLeave();
    return r;
}
