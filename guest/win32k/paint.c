/*
 * window device contexts, the paint cycle and presentation of top level
 * window surfaces to the host display.
 */
#include "win32k.h"

/* ------------------------------------------------------------------ host channel */

/* hypercall buffers: physically contiguous pages, serialized by the user lock. */
static MimeWinConfig *g_config;
static MimeWinPresent *g_present;

static BOOLEAN host_buffers(void)
{
    if (!g_config) {
        PUCHAR page = MmAllocateContiguousPages(2 * 4096);
        if (!page)
            return FALSE;
        g_config = (MimeWinConfig *)page;
        g_present = (MimeWinPresent *)(page + 4096);
    }
    return TRUE;
}

static ULONG window_id(WND *w)
{
    return (ULONG)(ULONG_PTR)WndHandle(w);
}

static ULONG64 hypercall(ULONG64 nr, ULONG64 a1)
{
    return HalMimeHypercall(nr, a1, 0, 0, 0, 0, 0, NULL);
}

void HostWindowCreate(WND *w)
{
    hypercall(MIME_HC_WIN_CREATE, window_id(w));
}

void HostWindowDestroy(WND *w)
{
    hypercall(MIME_HC_WIN_DESTROY, window_id(w));
}

static MimeRect mime_rect(const RECT *r)
{
    return (MimeRect){r->left, r->top, r->right, r->bottom};
}

/*
 * the corner radius the dwm gives a window: none when maximized, the one
 * DWMWA_WINDOW_CORNER_PREFERENCE asks for, else the frame's. dwmapi keeps
 * the preference as a window property holding the value plus one.
 */
static uint32_t corner_radius(WND *w, const FRAME_METRICS *m)
{
    if (w->Style & WS_MAXIMIZE)
        return 0;
    switch ((ULONG_PTR)PropGetData(w, AtomFind(L"MimeDwmCorner"))) {
    case 1 + 1: /* DWMWCP_DONOTROUND */
        return 0;
    case 2 + 1: /* DWMWCP_ROUND */
        return (uint32_t)W32MulDiv(8, (LONG)WndGetDpi(w), 96);
    case 3 + 1: /* DWMWCP_ROUNDSMALL */
        return (uint32_t)W32MulDiv(4, (LONG)WndGetDpi(w), 96);
    }
    return (uint32_t)m->CornerRadius;
}

void HostConfigure(WND *w, BOOLEAN activate)
{
    if (!(w->Flags & WNDF_HOST_WINDOW) || !host_buffers())
        return;
    if (activate)
        w->Flags |= WNDF_ACTIVATE_PENDING;
    if (w->Flags & WNDF_HOST_MOVING)
        return;
    /* a layered window shows up once it has attributes or contents from UpdateLayeredWindow. */
    BOOLEAN visible = WndIsVisible(w) && !(w->Style & WS_MINIMIZE) && (w->Flags & WNDF_PRESENTED) &&
                      !(w->ExStyle & 0x80000000u) && (!(w->ExStyle & WS_EX_LAYERED) || w->LayerFlags);
    MimeWinConfig *c = g_config;
    memset(c, 0, offsetof(MimeWinConfig, title));
    c->window_id = window_id(w);
    if (visible)
        c->flags |= MIME_WIN_VISIBLE;
    if (w->ExStyle & WS_EX_TOPMOST)
        c->flags |= MIME_WIN_TOPMOST;
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
    c->rect = mime_rect(&w->WindowRect);
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    c->visible = mime_rect(&vis);
    c->input = c->visible;
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, WndGetDpi(w), &m);
    /* a sizable frame takes the mouse over its invisible resize border, like the dwm's. */
    if ((w->Style & WS_THICKFRAME) && !(w->Style & (WS_MAXIMIZE | WS_MINIMIZE)) && m.VisibleInset)
        c->input = (MimeRect){0, 0, w->WindowRect.right - w->WindowRect.left, w->WindowRect.bottom - w->WindowRect.top};
    c->corner_radius = corner_radius(w, &m);
    if (m.Border && !(w->Style & WS_MAXIMIZE))
        c->border_color = (w->Flags & WNDF_ACTIVE_FRAME) ? 0x66000000u : 0x40000000u;
    RECT drag[MIME_WIN_MAX_DRAG_RECTS];
    c->drag_count = FrameGetDragRects(w, drag, MIME_WIN_MAX_DRAG_RECTS);
    for (ULONG i = 0; i < c->drag_count; i++)
        c->drag[i] = mime_rect(&drag[i]);
    c->owner_id = w->Owner ? window_id(w->Owner) : 0;
    ULONG n = w->TextLength < 255 ? w->TextLength : 255;
    if (n)
        memcpy(c->title, w->Text, n * sizeof(WCHAR));
    c->title[n] = 0;
    c->title_chars = n;
    hypercall(MIME_HC_WIN_CONFIG, (ULONG64)MmGetPhysicalAddress(c).QuadPart);
}

void HostPresent(WND *w, const RECT *dirty)
{
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

void HostSetCursor(ULONG cursor)
{
    static ULONG current = ~0u;
    if (cursor == current)
        return;
    current = cursor;
    hypercall(MIME_HC_WIN_CURSOR, cursor);
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

/* ------------------------------------------------------------------ presentation */

void PaintPresent(WND *top)
{
    SURFACE *s = top ? top->Surface : NULL;
    if (!s || !(top->Flags & WNDF_HOST_WINDOW) || RectEmpty(&s->Dirty))
        return;
    if (!WndIsVisible(top) || (top->Style & WS_MINIMIZE))
        return;
    /* a frame that still has to be drawn would show up as garbage on the first present. */
    if (!(top->Flags & WNDF_PRESENTED) && (top->Flags & WNDF_FRAME_DIRTY) && !(top->LayerFlags & MIME_LAYER_UPDATE))
        return;
    RECT dirty = s->Dirty;
    s->Dirty = (RECT){0, 0, 0, 0};
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

/* a surface may still be selected by window dcs when its window is resized or destroyed. */
void PaintRetireSurface(SURFACE *s)
{
    if (!s)
        return;
    s->Window = NULL;
    if (s->Header.SelectCount)
        s->Flags |= SURF_RETIRED;
    else
        SurfaceDestroy(s);
}

/* ------------------------------------------------------------------ window dcs */

HDC PaintGetDC(WND *w, HRGN clip, ULONG flags)
{
    DC *dc = DcCreate(DC_WINDOW | DC_CACHE);
    if (!dc)
        return NULL;
    dc->Window = w;
    dc->Dpi = WndGetDpi(w);
    WND *top = w ? WndTopLevel(w) : NULL;
    SURFACE *s = top && top != WndDesktop() ? top->Surface : NULL;
    if (!s) {
        /* hidden windows and the desktop: a dc that measures but draws nowhere. */
        dc->DefaultBitmap = SurfaceCreate(1, 1, 32, 0);
        if (!dc->DefaultBitmap) {
            DcFree(dc);
            return NULL;
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

void PaintReleaseDC(DC *dc)
{
    SURFACE *s = dc->DefaultBitmap ? NULL : dc->Surface;
    WND *top = dc->Window ? WndTopLevel(dc->Window) : NULL;
    DcFree(dc);
    if (s && s->Header.SelectCount && --s->Header.SelectCount == 0 && (s->Flags & SURF_RETIRED))
        SurfaceDestroy(s);
    (void)top;
}

HDC NTAPI NtUserGetDCEx(HWND hwnd, HRGN clip, ULONG flags)
{
    UserEnter();
    WND *w = hwnd ? WndLookup(hwnd) : WndDesktop();
    HDC dc = w ? PaintGetDC(w, clip, flags) : NULL;
    UserLeave();
    return dc;
}

HDC NTAPI NtUserGetDC(HWND hwnd)
{
    return NtUserGetDCEx(hwnd, NULL, DCX_USESTYLE);
}

HDC NTAPI NtUserGetWindowDC(HWND hwnd)
{
    return NtUserGetDCEx(hwnd, NULL, DCX_WINDOW | DCX_USESTYLE);
}

int NTAPI NtUserReleaseDC(HWND hwnd, HDC hdc)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    int ok = 0;
    if (dc && (dc->Flags & DC_WINDOW) && !(dc->Flags & DC_PAINT)) {
        WND *top = dc->Window ? WndTopLevel(dc->Window) : NULL;
        PaintReleaseDC(dc);
        /* drawing outside the paint cycle shows up at the next idle point; present now for animations. */
        if (top && top->Thread == W32GetThread())
            PaintPresent(top);
        ok = 1;
    }
    UserLeave();
    (void)hwnd;
    return ok;
}

/* ------------------------------------------------------------------ the paint cycle */

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
        return NULL;
    }
    RECT box = {0, 0, 0, 0};
    if (!RgnIsEmpty(&w->Update)) {
        box = w->Update.Extents;
        RectOffset(&box, -w->ClientRect.left, -w->ClientRect.top);
    }
    HDC hdc = PaintGetDC(w, NULL, DCX_INTERSECTUPDATE | DCX_USESTYLE);
    BOOLEAN erase = (w->Flags & WNDF_ERASE_PENDING) != 0;
    WndValidate(w, NULL);
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
    HDC hdc = NULL;
    if (w && w->Thread == W32GetThread()) {
        PAINTSTRUCT ps;
        hdc = PaintBegin(w, &ps);
        if (hdc && !NT_SUCCESS(MimeCopyToUser(user_ps, &ps, sizeof(ps)))) {
            DC *dc = DcLookup(hdc);
            if (dc)
                PaintReleaseDC(dc);
            hdc = NULL;
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

/* ------------------------------------------------------------------ invalidation services */

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
            /* a window procedure that does not validate would loop forever. */
            if (!p->Header.Destroyed && !RgnIsEmpty(&p->Update))
                WndValidate(p, NULL);
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
    WND *w = hwnd ? WndLookup(hwnd) : NULL;
    if (!hwnd) {
        /* the whole screen: every top level window. */
        WND *desktop = WndDesktop();
        for (WND *c = desktop ? desktop->FirstChild : NULL; c; c = c->Next)
            if (c->Style & WS_VISIBLE)
                WndInvalidateRegion(c, NULL, (flags | RDW_ALLCHILDREN | RDW_FRAME) & ~(RDW_UPDATENOW | RDW_ERASENOW));
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
        WndInvalidateRegion(w, whole ? NULL : &rgn, flags);
    }
    RgnFree(&rgn);
    if ((flags & (RDW_UPDATENOW | RDW_ERASENOW)) && w->Thread == W32GetThread())
        update_now(w, (flags & RDW_ALLCHILDREN) != 0 || !(flags & RDW_NOCHILDREN));
    UserLeave();
    return TRUE;
}

BOOL NTAPI NtUserInvalidateRect(HWND hwnd, const RECT *rect, BOOL erase)
{
    return NtUserRedrawWindow(hwnd, rect, NULL, RDW_INVALIDATE | (erase ? RDW_ERASE : 0));
}

BOOL NTAPI NtUserValidateRect(HWND hwnd, const RECT *rect)
{
    return NtUserRedrawWindow(hwnd, rect, NULL, RDW_VALIDATE | RDW_NOCHILDREN);
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
            HDC hdc = PaintGetDC(w, NULL, DCX_INTERSECTUPDATE | DCX_USESTYLE);
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
