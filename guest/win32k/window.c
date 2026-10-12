/*
 * window objects: the tree, creation and destruction, positioning, z order,
 * visibility, activation, focus and the window services.
 *
 * rectangles are kept in screen coordinates. every visible top level window
 * owns a surface covering its window rectangle; child windows draw into the
 * surface of their top level window, clipped by their visible region.
 */
#include "win32k.h"

static WND *g_desktop;
static ULONG g_cascade;

WND *WndLookup(HWND hwnd)
{
    if (!hwnd)
        return NULL;
    WND *w = (WND *)UserLookup(hwnd, USER_TYPE_WINDOW);
    if (!w && g_desktop && (hwnd == WndHandle(g_desktop)))
        return g_desktop;
    return w;
}

WND *WndDesktop(void)
{
    if (g_desktop)
        return g_desktop;
    WND *w = W32Alloc(sizeof(WND));
    if (!w)
        return NULL;
    w->Class = ClassFind(NULL, NULL, 32769, NULL);
    w->Style = WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    w->WindowRect = (RECT){0, 0, W32Display.Width, W32Display.Height};
    w->ClientRect = w->WindowRect;
    w->Dpi = W32SystemDpi();
    RgnInit(&w->Update);
    if (w->Class) {
        w->Class->Windows++;
        w->Extra = W32Alloc((SIZE_T)w->Class->WindowExtra);
        w->ExtraSize = (ULONG)w->Class->WindowExtra;
    }
    if (!UserCreateHandle(&w->Header, USER_TYPE_WINDOW)) {
        W32Free(w);
        return NULL;
    }
    g_desktop = w;
    return w;
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
    return w != NULL;
}

BOOLEAN WndIsChildOf(WND *child, WND *parent)
{
    for (WND *w = child ? child->Parent : NULL; w; w = w->Parent)
        if (w == parent)
            return TRUE;
    return FALSE;
}

UINT WndGetDpi(WND *w)
{
    return w && w->Dpi ? w->Dpi : W32SystemDpi();
}

/* ------------------------------------------------------------------ tree links */

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
    w->Next = w->Prev = NULL;
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
    WND *above = NULL;
    BOOLEAN topmost_band = parent == g_desktop;
    if (after == HWND_BOTTOM) {
        above = parent->LastChild;
        if (topmost_band && !(w->ExStyle & WS_EX_TOPMOST))
            ; /* the bottom of the whole list is fine for a normal window */
        else if (topmost_band) {
            /* topmost windows stay above every normal window. */
            above = NULL;
            for (WND *c = parent->FirstChild; c && (c->ExStyle & WS_EX_TOPMOST); c = c->Next)
                above = c;
        }
    } else if (after == HWND_TOP || after == HWND_TOPMOST || after == HWND_NOTOPMOST || !after) {
        above = NULL;
        if (topmost_band && !(w->ExStyle & WS_EX_TOPMOST))
            for (WND *c = parent->FirstChild; c && (c->ExStyle & WS_EX_TOPMOST); c = c->Next)
                above = c;
    } else {
        WND *a = WndLookup(after);
        if (a && a->Parent == parent && a != w)
            above = a;
        else
            above = NULL;
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
        if (c->Owner == owner && c->Prev != NULL) {
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

/* screen rectangle to the coordinates used for positioning: parent client, or screen for top level windows. */
static void screen_to_parent(WND *w, RECT *r)
{
    if (w->Parent && w->Parent != g_desktop)
        RectOffset(r, -w->Parent->ClientRect.left, -w->Parent->ClientRect.top);
}

static void parent_to_screen(WND *parent, RECT *r)
{
    if (parent && parent != g_desktop)
        RectOffset(r, parent->ClientRect.left, parent->ClientRect.top);
}

/* ------------------------------------------------------------------ text */

void WndSetText(WND *w, PCWSTR text, ULONG chars)
{
    W32Free(w->Text);
    w->Text = NULL;
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

/* ------------------------------------------------------------------ visible regions */

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
}

/* ------------------------------------------------------------------ invalidation */

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
        if ((flags & RDW_FRAME) && !RgnRectIn(&rgn, &w->ClientRect)) {
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
    return NULL;
}

WND *WndFindNeedsPaint(W32THREAD *t, HWND filter)
{
    if (!g_desktop)
        return NULL;
    if (filter) {
        WND *f = WndLookup(filter);
        if (!f || !WndIsVisible(f))
            return NULL;
        if (f->Thread == t && needs_paint(f))
            return f;
        return find_paint(f->FirstChild, t);
    }
    return find_paint(g_desktop->FirstChild, t);
}

/* ------------------------------------------------------------------ host configuration */

void WndUpdateHostConfig(WND *w, BOOLEAN activate)
{
    if (!WndIsTopLevel(w) || !(w->Flags & WNDF_HOST_WINDOW))
        return;
    HostConfigure(w, activate);
}

/* top level windows render into a surface the size of their window rectangle. */
static void ensure_surface(WND *w)
{
    if (!WndIsTopLevel(w) || w->Parent == NULL || (w->ExStyle & 0x80000000u))
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
    if (w->Surface)
        PaintRetireSurface(w->Surface);
    w->Surface = s;
    s->Dirty = (RECT){0, 0, width, height};
    if (!(w->Flags & WNDF_HOST_WINDOW)) {
        HostWindowCreate(w);
        w->Flags |= WNDF_HOST_WINDOW;
    }
}

/* ------------------------------------------------------------------ messages helpers */

static BOOLEAN alive(WND *w)
{
    return w && !w->Header.Destroyed;
}

static LRESULT send_kernel(WND *w, UINT msg, WPARAM wparam, PVOID data, ULONG kind, ULONG size)
{
    return MsgSendKernel(w, msg, wparam, (LPARAM)data, kind, size);
}

static void get_minmax(WND *w, MINMAXINFO *mmi)
{
    UINT dpi = WndGetDpi(w);
    memset(mmi, 0, sizeof(*mmi));
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, dpi, &m);
    LONG work_w = W32Display.Work.right - W32Display.Work.left;
    LONG work_h = W32Display.Work.bottom - W32Display.Work.top;
    mmi->ptMaxSize = (POINT){work_w + 2 * m.Border, work_h + m.Border + m.Border};
    mmi->ptMaxPosition = (POINT){-m.Border, -m.Border};
    mmi->ptMinTrackSize = (POINT){MetricsGet(SM_CXMINTRACK, dpi), MetricsGet(SM_CYMINTRACK, dpi)};
    mmi->ptMaxTrackSize = (POINT){MetricsGet(SM_CXMAXTRACK, dpi), MetricsGet(SM_CYMAXTRACK, dpi)};
    if (w->Style & (WS_THICKFRAME | WS_CAPTION))
        send_kernel(w, WM_GETMINMAXINFO, 0, mmi, MIME_LPARAM_INOUT, sizeof(*mmi));
}

/* computes the client rectangle for a window rectangle given in parent coordinates. */
static void calc_client(WND *w, const RECT *window, const RECT *old_window, RECT *client, BOOLEAN full)
{
    if (!full) {
        RECT r = *window;
        send_kernel(w, WM_NCCALCSIZE, FALSE, &r, MIME_LPARAM_INOUT, sizeof(r));
        *client = r;
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
}

/* ------------------------------------------------------------------ positioning */

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

    if (moved || sized || (pos.flags & SWP_FRAMECHANGED)) {
        RECT client;
        calc_client(w, &new_window, &old_window, &client, TRUE);
        if (!alive(w))
            goto done;
        RECT win_screen = new_window, client_screen = client;
        parent_to_screen(w->Parent, &win_screen);
        parent_to_screen(w->Parent, &client_screen);
        LONG dx = client_screen.left - w->ClientRect.left;
        LONG dy = client_screen.top - w->ClientRect.top;
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
        if (visible)
            ensure_surface(w);
        if ((sized || (pos.flags & (SWP_FRAMECHANGED | SWP_SHOWWINDOW))) && visible) {
            w->Flags |= WNDF_FRAME_DIRTY;
            WndInvalidateRegion(w, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
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
                    WndInvalidateRegion(w, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            }
        }
    }
    (void)old_client_screen;

    if (!(pos.flags & SWP_NOACTIVATE) && WndIsTopLevel(w) && visible && !(w->ExStyle & WS_EX_NOACTIVATE) &&
        (pos.flags & SWP_SHOWWINDOW))
        WndActivate(w, FALSE);
    if ((pos.flags & SWP_HIDEWINDOW) && WndIsTopLevel(w) && InputForeground == w) {
        /* hand activation to the owner or the next visible window. */
        WND *next = w->Owner && WndIsVisible(w->Owner) ? w->Owner : NULL;
        for (WND *c = g_desktop->FirstChild; !next && c; c = c->Next)
            if (c != w && (c->Style & WS_VISIBLE) && c->Thread == w->Thread && !(c->ExStyle & WS_EX_NOACTIVATE))
                next = c;
        if (next)
            WndActivate(next, FALSE);
        else
            InputSetForeground(NULL);
    }
    if (!visible && w->Thread) {
        W32THREAD *t = w->Thread;
        WND *focus = WndLookup(t->Focus);
        if (focus && (focus == w || WndIsChildOf(focus, w)))
            WndSetFocus(w->Parent && w->Parent != g_desktop ? w->Parent : NULL);
    }

    if (!alive(w))
        goto done;
    pos.flags |= (moved ? 0 : SWP_NOMOVE) | (sized ? 0 : SWP_NOSIZE);
    RECT final = w->WindowRect;
    screen_to_parent(w, &final);
    pos.x = final.left;
    pos.y = final.top;
    pos.cx = final.right - final.left;
    pos.cy = final.bottom - final.top;
    if (!(pos.flags & SWP_NOSENDCHANGING) || moved || sized || (pos.flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)))
        send_kernel(w, WM_WINDOWPOSCHANGED, 0, &pos, MIME_LPARAM_IN, sizeof(pos));
done:
    UserDereference(&w->Header);
}

static void maximized_rect(WND *w, RECT *r)
{
    MINMAXINFO mmi;
    get_minmax(w, &mmi);
    r->left = W32Display.Work.left + mmi.ptMaxPosition.x;
    r->top = W32Display.Work.top + mmi.ptMaxPosition.y;
    r->right = r->left + mmi.ptMaxSize.x;
    r->bottom = r->top + mmi.ptMaxSize.y;
}

void WndShow(WND *w, int cmd)
{
    if (!w || w == g_desktop)
        return;
    BOOLEAN was_visible = (w->Style & WS_VISIBLE) != 0;
    UINT swp = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER;
    BOOLEAN show = TRUE;
    switch (cmd) {
    case SW_HIDE:
        if (!was_visible)
            return;
        show = FALSE;
        swp |= SWP_HIDEWINDOW | SWP_NOACTIVATE;
        break;
    case SW_SHOWNOACTIVATE:
    case SW_SHOWNA:
    case SW_SHOWMINNOACTIVE:
        swp |= SWP_SHOWWINDOW | SWP_NOACTIVATE;
        break;
    case SW_MAXIMIZE:
        if (!(w->Style & WS_MAXIMIZE)) {
            RECT r;
            if (!(w->Style & WS_MINIMIZE)) {
                w->NormalRect = w->WindowRect;
                screen_to_parent(w, &w->NormalRect);
            }
            maximized_rect(w, &r);
            w->Style = (w->Style & ~WS_MINIMIZE) | WS_MAXIMIZE;
            w->Flags |= WNDF_MAXIMIZED;
            if (!was_visible)
                MsgSend(w, WM_SHOWWINDOW, TRUE, 0);
            WndSetPos(w, (WND *)HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                      SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_STATECHANGED);
            return;
        }
        swp |= SWP_SHOWWINDOW;
        break;
    case SW_MINIMIZE:
    case SW_SHOWMINIMIZED:
    case SW_FORCEMINIMIZE:
        /* no taskbar: minimizing hides the host window but keeps the window visible to the app. */
        if (!(w->Style & WS_MINIMIZE)) {
            if (!(w->Style & WS_MAXIMIZE)) {
                w->NormalRect = w->WindowRect;
                screen_to_parent(w, &w->NormalRect);
            }
            w->Style |= WS_MINIMIZE;
            if (!was_visible) {
                MsgSend(w, WM_SHOWWINDOW, TRUE, 0);
                w->Style |= WS_VISIBLE;
            }
            WndUpdateHostConfig(w, FALSE);
            MsgSend(w, WM_SIZE, SIZE_MINIMIZED, 0);
            if (InputForeground == w)
                InputSetForeground(NULL);
        }
        return;
    case SW_RESTORE:
    case SW_SHOWNORMAL:
    case SW_SHOWDEFAULT:
        if (w->Style & (WS_MAXIMIZE | WS_MINIMIZE)) {
            BOOLEAN was_max = (w->Style & WS_MAXIMIZE) && !(w->Style & WS_MINIMIZE);
            BOOLEAN keep_max = (w->Style & WS_MINIMIZE) && (w->Flags & WNDF_MAXIMIZED) && cmd == SW_RESTORE;
            w->Style &= ~WS_MINIMIZE;
            if (keep_max) {
                WndUpdateHostConfig(w, TRUE);
                WndInvalidateRegion(w, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_ALLCHILDREN);
                MsgSend(w, WM_SIZE, SIZE_MAXIMIZED,
                        MAKELPARAM(w->ClientRect.right - w->ClientRect.left, w->ClientRect.bottom - w->ClientRect.top));
                WndActivate(w, FALSE);
                return;
            }
            w->Style &= ~WS_MAXIMIZE;
            w->Flags &= ~WNDF_MAXIMIZED;
            RECT r = w->NormalRect;
            (void)was_max;
            WndSetPos(w, (WND *)HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                      SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_STATECHANGED);
            return;
        }
        swp |= SWP_SHOWWINDOW;
        break;
    default:
        swp |= SWP_SHOWWINDOW;
        break;
    }
    if (show == was_visible && !(swp & SWP_SHOWWINDOW && !was_visible)) {
        if (show && !(swp & SWP_NOACTIVATE) && WndIsTopLevel(w))
            WndActivate(w, FALSE);
        return;
    }
    MsgSend(w, WM_SHOWWINDOW, show, 0);
    if (!alive(w))
        return;
    if (show && WndIsTopLevel(w))
        swp &= ~SWP_NOZORDER;
    WndSetPos(w, (WND *)HWND_TOP, 0, 0, 0, 0, swp);
    if (show && !(w->Flags & WNDF_SENT_CREATE)) {
        /* the first show of a window created hidden still owes its size notification. */
        w->Flags |= WNDF_SENT_CREATE;
    }
}

void WndCenterOnOwner(WND *w)
{
    RECT ref = W32Display.Work;
    if (w->Owner && (w->Owner->Style & WS_VISIBLE) && !(w->Owner->Style & WS_MINIMIZE))
        ref = w->Owner->WindowRect;
    LONG width = w->WindowRect.right - w->WindowRect.left;
    LONG height = w->WindowRect.bottom - w->WindowRect.top;
    LONG x = ref.left + (ref.right - ref.left - width) / 2;
    LONG y = ref.top + (ref.bottom - ref.top - height) / 2;
    x = W32Max(W32Display.Work.left, W32Min(x, W32Display.Work.right - width));
    y = W32Max(W32Display.Work.top, W32Min(y, W32Display.Work.bottom - height));
    WndSetPos(w, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* ------------------------------------------------------------------ activation and focus */

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
    if (old && alive(old)) {
        UserReference(&old->Header);
        MsgSend(old, WM_NCACTIVATE, FALSE, (LPARAM)WndHandle(w));
        MsgSend(old, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, (old->Style & WS_MINIMIZE) != 0), (LPARAM)WndHandle(w));
        if (old->Thread && w->Thread && old->Thread->Process != w->Thread->Process)
            MsgSend(old, WM_ACTIVATEAPP, FALSE, (LPARAM)w->Thread->Tid);
        if (old->Thread && old->Thread != w->Thread)
            old->Thread->Active = NULL;
        UserDereference(&old->Header);
    }
    if (!alive(w))
        goto done;
    InputSetForeground(w);
    if (w->Thread)
        w->Thread->Active = WndHandle(w);
    if (WndIsTopLevel(w)) {
        WND *parent = w->Parent;
        unlink_window(w);
        link_window(parent, w, HWND_TOP);
        raise_owned(w);
    }
    WndUpdateHostConfig(w, TRUE);
    if (!old || !old->Thread || !w->Thread || old->Thread->Process != w->Thread->Process)
        MsgSend(w, WM_ACTIVATEAPP, TRUE, old && old->Thread ? (LPARAM)old->Thread->Tid : 0);
    MsgSend(w, WM_NCACTIVATE, TRUE, (LPARAM)WndHandle(old));
    MsgSend(w, WM_ACTIVATE, MAKEWPARAM(click ? WA_CLICKACTIVE : WA_ACTIVE, (w->Style & WS_MINIMIZE) != 0),
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
    if (old)
        MsgSend(old, WM_KILLFOCUS, (WPARAM)WndHandle(w), 0);
    if (w && alive(w) && t->Focus == WndHandle(w))
        MsgSend(w, WM_SETFOCUS, (WPARAM)old_h, 0);
    if (w)
        UserDereference(&w->Header);
}

/* ------------------------------------------------------------------ hit testing */

WND *WndChildFromPoint(WND *parent, POINT pt, BOOLEAN skip_disabled)
{
    for (WND *c = parent->FirstChild; c; c = c->Next) {
        if (!(c->Style & WS_VISIBLE) || !RectContains(&c->WindowRect, pt.x, pt.y))
            continue;
        if (skip_disabled && (c->Style & WS_DISABLED))
            continue;
        return c;
    }
    return NULL;
}

/* deepest window under pt, starting at a top level window. */
WND *WndFromPoint(WND *top, POINT pt)
{
    WND *w = top;
    if (!w)
        return NULL;
    for (;;) {
        if (!RectContains(&w->ClientRect, pt.x, pt.y) || (w->Style & WS_MINIMIZE))
            return w;
        WND *c = WndChildFromPoint(w, pt, FALSE);
        if (!c)
            return w;
        if (c->Style & WS_DISABLED)
            return c;
        /* static controls and group boxes are transparent to the mouse via WM_NCHITTEST HTTRANSPARENT, handled by the caller. */
        w = c;
    }
}

/* ------------------------------------------------------------------ destruction */

static void free_window(WND *w)
{
    TimerRemoveWindow(w);
    W32THREAD *t = w->Thread;
    if (t) {
        HWND h = WndHandle(w);
        if (t->Focus == h)
            t->Focus = NULL;
        if (t->Active == h)
            t->Active = NULL;
        if (t->Capture == h)
            InputSetCapture(t, NULL, 0);
        if (t->MouseWindow == h)
            t->MouseWindow = NULL;
    }
    if (InputForeground == w)
        InputSetForeground(NULL);
    if (w->Flags & WNDF_HOST_WINDOW) {
        HostWindowDestroy(w);
        w->Flags &= ~WNDF_HOST_WINDOW;
    }
    if (w->Surface) {
        PaintRetireSurface(w->Surface);
        w->Surface = NULL;
    }
    unlink_window(w);
    for (WND *c = g_desktop ? g_desktop->FirstChild : NULL; c; c = c->Next)
        if (c->Owner == w)
            c->Owner = NULL;
    if (w->Class)
        w->Class->Windows--;
    W32Free(w->Text);
    w->Text = NULL;
    W32Free(w->Extra);
    w->Extra = NULL;
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
    return NULL;
}

static void destroy_tree(WND *w)
{
    w->Flags |= WNDF_DESTROYING;
    UserReference(&w->Header);
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
            WND *owned = NULL;
            for (WND *c = g_desktop->FirstChild; c; c = c->Next)
                if (c->Owner == w && !(c->Flags & WNDF_DESTROYING))
                    owned = c;
            if (!owned)
                break;
            WndDestroy(owned);
        }
    }
    if (w->Style & WS_VISIBLE)
        WndSetPos(w, NULL, 0, 0, 0, 0,
                  SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | (WndIsTopLevel(w) ? 0 : SWP_NOACTIVATE));
    if (alive(w) && !(w->Flags & WNDF_DESTROYING))
        destroy_tree(w);
done:
    UserDereference(&w->Header);
}

void WndDestroyThreadWindows(W32THREAD *t)
{
    if (!g_desktop)
        return;
    for (;;) {
        WND *found = NULL;
        for (WND *c = g_desktop->FirstChild; c && !found; c = c->Next)
            if (c->Thread == t && !(c->Flags & WNDF_DESTROYING))
                found = c;
        if (!found)
            break;
        /* the thread is going away: no callbacks, just tear the tree down. */
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

/* ------------------------------------------------------------------ creation */

static NTSTATUS capture_class_name(PUNICODE_STRING user, PWSTR *name, ATOM *atom)
{
    UNICODE_STRING s;
    *name = NULL;
    *atom = 0;
    if (!user)
        return STATUS_INVALID_PARAMETER;
    if (!NT_SUCCESS(MimeCopyFromUser(&s, user, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    if (!s.Length && (ULONG_PTR)s.Buffer < 0x10000) {
        *atom = (ATOM)(ULONG_PTR)s.Buffer;
        return STATUS_SUCCESS;
    }
    return W32CaptureString(user, name, NULL);
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
        return NULL;
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

HWND NTAPI NtUserCreateWindowEx(DWORD ex_style, PUNICODE_STRING user_class, PUNICODE_STRING user_name, DWORD style,
                                int x, int y, int cx, int cy, HWND parent_h, HMENU menu, HINSTANCE instance,
                                LPVOID param, DWORD flags)
{
    PWSTR class_name = NULL, name = NULL;
    ATOM atom;
    SIZE_T name_chars = 0;
    HWND result = NULL;
    if (!NT_SUCCESS(capture_class_name(user_class, &class_name, &atom)))
        return NULL;
    if (user_name) {
        UNICODE_STRING s;
        if (!NT_SUCCESS(MimeCopyFromUser(&s, user_name, sizeof(s)))) {
            W32Free(class_name);
            return NULL;
        }
        if (!s.Length && s.Buffer && (ULONG_PTR)s.Buffer < 0x10000) {
            /* a resource ordinal as window name, as dialog templates give it: 0xFFFF then the ordinal. */
            name = W32Alloc(3 * sizeof(WCHAR));
            if (name) {
                name[0] = 0xFFFF;
                name[1] = (WCHAR)(ULONG_PTR)s.Buffer;
                name_chars = 2;
            }
        } else if (!NT_SUCCESS(W32CaptureString(user_name, &name, &name_chars))) {
            W32Free(class_name);
            return NULL;
        }
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    WND *desktop = WndDesktop();
    WND_CLASS *cls = t ? ClassFind(t->Process, class_name, atom, instance) : NULL;
    if (!cls || !desktop) {
        DbgPrint("win32k: CreateWindowEx: class %ws (%x) not found\n", class_name ? class_name : L"", atom);
        goto out;
    }

    BOOLEAN message_only = parent_h == HWND_MESSAGE;
    WND *parent = desktop, *owner = NULL;
    if (parent_h && !message_only) {
        WND *p = WndLookup(parent_h);
        if (!p)
            goto out;
        if (style & WS_CHILD)
            parent = p;
        else
            owner = p == desktop ? NULL : WndTopLevel(p);
    } else if (style & WS_CHILD) {
        if (!message_only)
            goto out; /* children need a parent */
        style &= ~WS_CHILD;
    }

    WND *w = W32Alloc(sizeof(WND));
    if (!w)
        goto out;
    w->Extra = cls->WindowExtra > 0 ? W32Alloc((SIZE_T)cls->WindowExtra) : NULL;
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
    w->Proc = cls->Proc;
    w->Ansi = cls->Ansi;
    w->Instance = instance;
    w->Owner = owner;
    w->Dpi = parent != desktop ? parent->Dpi : W32SystemDpi();
    w->Icon = cls->Icon;
    w->IconSmall = cls->IconSmall;
    if (cls->Fnid == MIME_FNID_DIALOG)
        w->Flags |= WNDF_DIALOG;
    if (message_only)
        ex_style |= 0x80000000u; /* private marker: message only window, never visible */
    if (!(style & WS_CHILD)) {
        style |= WS_CLIPSIBLINGS;
        if (!(style & WS_POPUP))
            style |= WS_CAPTION;
    }
    if ((style & WS_CAPTION) == WS_CAPTION || (style & WS_THICKFRAME))
        ex_style |= WS_EX_WINDOWEDGE;
    if (ex_style & WS_EX_DLGMODALFRAME)
        ex_style |= WS_EX_WINDOWEDGE;
    w->Style = style & ~WS_VISIBLE;
    w->ExStyle = ex_style;
    w->Id = (UINT_PTR)menu;
    if (name)
        w->Text = W32StrDup(name, name_chars), w->TextLength = w->Text ? (ULONG)name_chars : 0;

    /* default positions for overlapped windows. */
    UINT dpi = w->Dpi;
    if (!(style & (WS_CHILD | WS_POPUP)) && !message_only) {
        if (x == (int)CW_USEDEFAULT) {
            LONG step = W32MulDiv(26, (LONG)dpi, 96);
            ULONG n = g_cascade++ % 8;
            x = W32Display.Work.left + step * (LONG)(n + 1);
            if (y == (int)CW_USEDEFAULT || y == SW_SHOW || !y)
                y = W32Display.Work.top + step * (LONG)(n + 1);
        }
        if (cx == (int)CW_USEDEFAULT) {
            cx = (W32Display.Work.right - W32Display.Work.left) * 3 / 4;
            cy = (W32Display.Work.bottom - W32Display.Work.top) * 3 / 4;
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

    link_window(parent, w, (style & WS_CHILD) ? HWND_BOTTOM : HWND_TOP);
    RECT rect = {x, y, x + cx, y + cy};
    parent_to_screen(parent, &rect);
    w->WindowRect = rect;
    w->ClientRect = rect;
    UserReference(&w->Header);

    if ((style & WS_THICKFRAME) || !(style & (WS_POPUP | WS_CHILD))) {
        MINMAXINFO mmi;
        get_minmax(w, &mmi);
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
        parent_to_screen(parent, &rect);
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
    parent_to_screen(parent, &client);
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

    if (style & WS_MAXIMIZE) {
        w->Style &= ~WS_MAXIMIZE;
        WndShow(w, SW_MAXIMIZE);
    } else if (style & WS_VISIBLE) {
        if (style & WS_CHILD)
            WndSetPos(w, NULL, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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

/* ------------------------------------------------------------------ services */

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
            WndDestroy(w);
            ok = TRUE;
        }
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserShowWindow(HWND hwnd, int cmd)
{
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

BOOL NTAPI NtUserSetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    if (can_touch(w)) {
        WndSetPos(w, (WND *)after, x, y, cx, cy, flags);
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
                    WndSetFocus(NULL);
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
    HWND old = NULL;
    if (can_touch(w) && p && p != w && !WndIsChildOf(p, w)) {
        old = WndHandle(w->Parent);
        BOOLEAN visible = (w->Style & WS_VISIBLE) != 0;
        if (visible)
            WndSetPos(w, NULL, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT r = w->WindowRect;
        screen_to_parent(w, &r);
        WND *old_parent = w->Parent;
        unlink_window(w);
        if (old_parent == g_desktop && p != g_desktop && w->Surface) {
            HostWindowDestroy(w);
            w->Flags &= ~WNDF_HOST_WINDOW;
            PaintRetireSurface(w->Surface);
            w->Surface = NULL;
        }
        link_window(p, w, HWND_TOP);
        /* keep the position relative to the new parent. */
        RECT nr = r;
        parent_to_screen(p, &nr);
        offset_tree(w, nr.left - w->WindowRect.left, nr.top - w->WindowRect.top);
        if (visible)
            WndSetPos(w, NULL, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
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
    WND *w = WndLookup(hwnd), *r = NULL;
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
        w->Owner = value ? WndTopLevel(WndLookup((HWND)value)) : NULL;
        if (w->Owner == w)
            w->Owner = NULL;
        return old;
    case GWLP_ID:
        old = (LONG_PTR)w->Id;
        w->Id = (UINT_PTR)value;
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
            if ((ss.styleNew & WS_VISIBLE) != visible)
                WndShow(w, (ss.styleNew & WS_VISIBLE) ? SW_SHOWNA : SW_HIDE);
        } else {
            w->ExStyle = (ss.styleNew & 0x7FFFFFFFu) | (w->ExStyle & 0x80000000u);
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
    PWSTR text = NULL;
    SIZE_T chars = 0;
    if (user_text && !NT_SUCCESS(W32CaptureString(user_text, &text, &chars)))
        return FALSE;
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (can_touch(w))
        WndSetText(w, text, (ULONG)chars);
    UserLeave();
    W32Free(text);
    return w != NULL;
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
    PWSTR class_name = NULL, name = NULL;
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
            have_name = name != NULL;
        }
    }
    UserEnter();
    HWND result = NULL;
    WND *parent = parent_h ? WndLookup(parent_h) : WndDesktop();
    if (parent_h == HWND_MESSAGE)
        parent = WndDesktop();
    if (parent) {
        WND *c = parent->FirstChild;
        if (after_h) {
            WND *a = WndLookup(after_h);
            c = a && a->Parent == parent ? a->Next : NULL;
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
        c = c ? c->Next : NULL;
    }
    if (NT_SUCCESS(status) && n > max)
        status = STATUS_BUFFER_TOO_SMALL;
    MimeCopyToUser(count, &n, sizeof(n));
    UserLeave();
    (void)desktop;
    return status;
}

BOOL NTAPI NtUserGetWindowPlacement(HWND hwnd, WINDOWPLACEMENT *user_wp)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    if (can_touch(w)) {
        WINDOWPLACEMENT wp;
        memset(&wp, 0, sizeof(wp));
        wp.length = sizeof(wp);
        wp.showCmd = (w->Style & WS_MINIMIZE)   ? SW_SHOWMINIMIZED
                     : (w->Style & WS_MAXIMIZE) ? SW_SHOWMAXIMIZED
                     : (w->Style & WS_VISIBLE)  ? SW_SHOWNORMAL
                                                : SW_HIDE;
        if (w->Style & (WS_MINIMIZE | WS_MAXIMIZE)) {
            wp.rcNormalPosition = w->NormalRect;
        } else {
            wp.rcNormalPosition = w->WindowRect;
            screen_to_parent(w, &wp.rcNormalPosition);
        }
        wp.ptMinPosition = (POINT){-1, -1};
        wp.ptMaxPosition = (POINT){-1, -1};
        ok = NT_SUCCESS(MimeCopyToUser(user_wp, &wp, sizeof(wp)));
    }
    UserLeave();
    return ok;
}

HWND NTAPI NtUserSetFocus(HWND hwnd)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    HWND old = t ? t->Focus : NULL;
    WND *w = hwnd ? WndLookup(hwnd) : NULL;
    if (hwnd && (!can_touch(w) || (w->Style & WS_DISABLED) || w->Thread != t)) {
        UserLeave();
        return NULL;
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
    HWND old = t ? t->Active : NULL;
    WND *w = hwnd ? WndLookup(hwnd) : NULL;
    if (w && can_touch(w) && WndIsTopLevel(w) && w->Thread == t)
        WndActivate(w, FALSE);
    else if (!hwnd && t && InputForeground && InputForeground->Thread == t)
        InputSetForeground(NULL);
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

/* ------------------------------------------------------------------ hwnd multiplexers */

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
        case MIME_HWND_MONITOR:
            r = 0x00010001;
            break;
        }
    }
    UserLeave();
    if (routine == MIME_HWND_UPDATEWINDOW && w)
        NtUserRedrawWindow(hwnd, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);
    return r;
}

static WND *get_window_rel(WND *w, UINT cmd)
{
    switch (cmd) {
    case GW_HWNDFIRST:
        return w->Parent ? w->Parent->FirstChild : NULL;
    case GW_HWNDLAST:
        return w->Parent ? w->Parent->LastChild : NULL;
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
        return NULL;
    }
    }
    return NULL;
}

static WND *find_dlg_item(WND *w, UINT_PTR id)
{
    for (WND *c = w->FirstChild; c; c = c->Next)
        if (c->Id == id && !(c->Flags & WNDF_DESTROYING))
            return c;
    return NULL;
}

static void map_points(WND *from, WND *to, POINT *pts, UINT count)
{
    LONG fx = from && from != g_desktop ? from->ClientRect.left : 0;
    LONG fy = from && from != g_desktop ? from->ClientRect.top : 0;
    LONG tx = to && to != g_desktop ? to->ClientRect.left : 0;
    LONG ty = to && to != g_desktop ? to->ClientRect.top : 0;
    for (UINT i = 0; i < count; i++) {
        pts[i].x += fx - tx;
        pts[i].y += fy - ty;
    }
}

ULONG_PTR NTAPI NtUserCallHwndParam(HWND hwnd, ULONG_PTR param, ULONG routine)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    ULONG_PTR r = 0;
    if (!w && !(routine == MIME_HWNDPARAM_MAPPOINTS && !hwnd))
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
            r = (ULONG_PTR)c->Proc;
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
    case MIME_HWNDPARAM_GETWINDOWRECT:
        r = NT_SUCCESS(MimeCopyToUser((PVOID)param, &w->WindowRect, sizeof(RECT)));
        break;
    case MIME_HWNDPARAM_GETCLIENTRECT: {
        RECT c = {0, 0, w->ClientRect.right - w->ClientRect.left, w->ClientRect.bottom - w->ClientRect.top};
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
        if (w->Thread) {
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
        WND *to = mp.To ? WndLookup(mp.To) : NULL;
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
                    r = 0x80000000u; /* nonzero success marker for a zero offset */
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
        wi.dwStyle = w->Style;
        wi.dwExStyle = w->ExStyle & 0x7FFFFFFFu;
        wi.dwWindowStatus = InputForeground == w ? 1 /* WS_ACTIVECAPTION */ : 0;
        FRAME_METRICS m;
        FrameGetMetrics(w->Style, w->ExStyle, WndGetDpi(w), &m);
        wi.cxWindowBorders = (UINT)(w->ClientRect.left - w->WindowRect.left);
        wi.cyWindowBorders = (UINT)(w->WindowRect.bottom - w->ClientRect.bottom);
        wi.atomWindowType = w->Class ? w->Class->Atom : 0;
        wi.wCreatorVersion = 0x0400;
        r = NT_SUCCESS(MimeCopyToUser((PVOID)param, &wi, sizeof(wi)));
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
        FrameAdjustRect(&ar.Rect, ar.Style, ar.ExStyle, ar.Dpi ? ar.Dpi : WndGetDpi(w));
        if (ar.Menu)
            ar.Rect.top -= MetricsGet(SM_CYMENU, ar.Dpi ? ar.Dpi : WndGetDpi(w));
        r = NT_SUCCESS(MimeCopyToUser((PVOID)param, &ar, sizeof(ar)));
        break;
    }
    case MIME_HWNDPARAM_CHILDFROMPOINT: {
        POINT pt = {(SHORT)LOWORD(param), (SHORT)HIWORD(param)};
        pt.x += w->ClientRect.left;
        pt.y += w->ClientRect.top;
        WND *c = WndChildFromPoint(w, pt, FALSE);
        r = (ULONG_PTR)WndHandle(c ? c : (RectContains(&w->ClientRect, pt.x, pt.y) ? w : NULL));
        break;
    }
    case MIME_HWNDPARAM_SETCONTEXTHELPID:
        w->ContextHelpId = (DWORD)param;
        r = 1;
        break;
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
