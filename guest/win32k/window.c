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
    WND *top = WndTopLevel((WND *)w) ? WndTopLevel((WND *)w) : (WND *)w;
    /* a window told to follow another takes its monitor, a few links deep at most. */
    for (int depth = 0; depth < 4 && top->InheritMonitor; depth++) {
        WND *source = WndLookup(top->InheritMonitor);
        WND *next = source ? (WndTopLevel(source) ? WndTopLevel(source) : source) : nullptr;
        if (!next || next == top)
            break;
        top = next;
    }
    RECT physical;
    WndPhysicalRect(top, &physical);
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

/*
 * a message window is kept among the children of the desktop and marked. windows keeps those under a parent of their
 * own, so the two kinds never see each other as siblings, and the walks of the top level windows leave them out.
 */
static BOOLEAN is_message_window(const WND *w)
{
    return w->Parent && w->Parent == g_desktop && (w->ExStyle & 0x80000000u);
}

static BOOLEAN same_list(const WND *a, const WND *b)
{
    return a->Parent != g_desktop || is_message_window(a) == is_message_window(b);
}

static WND *first_top_level(void)
{
    WND *c = g_desktop ? g_desktop->FirstChild : nullptr;
    while (c && is_message_window(c))
        c = c->Next;
    return c;
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
/* the shell window of the desktop, SetShellWindow: always at the bottom of the top level windows. */
WND *WndShell;

static void link_window(WND *parent, WND *w, HWND after)
{
    WND *above = nullptr;
    BOOLEAN topmost_band = parent == g_desktop;
    if (topmost_band && w == WndShell)
        after = HWND_BOTTOM;
    else if (topmost_band && WndShell && after == WndHandle(WndShell))
        after = HWND_BOTTOM;
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
    /* nothing goes under the shell window. */
    if (topmost_band && WndShell && w != WndShell && above == WndShell)
        above = WndShell->Prev == w ? w->Prev : WndShell->Prev;
    link_after(parent, w, above);
}

/*
 * ZOrderByOwner of win32kfull: a top level window moves with the windows it owns, and with its owners when it leaves
 * their tree. measured on windows 11.
 */
#define OWNER_DEPTH 32

static BOOLEAN owned_by(const WND *c, const WND *owner)
{
    return c->Owner == owner && c != owner && c->Parent == g_desktop && !is_message_window(c) &&
           !(c->Flags & WNDF_DESTROYING);
}

static WND *owner_of(const WND *w)
{
    WND *o = w->Owner;
    return o && o != w && o->Parent == g_desktop && !is_message_window(o) ? o : nullptr;
}

static WND *tree_root(WND *w)
{
    for (ULONG depth = 0; owner_of(w) && depth < OWNER_DEPTH; depth++)
        w = owner_of(w);
    return w;
}

static BOOLEAN listed(WND *const *list, ULONG n, const WND *w)
{
    for (ULONG i = 0; i < n; i++)
        if (list[i] == w)
            return TRUE;
    return FALSE;
}

/* the windows w owns, each after those it owns itself, in the order they have, then w. */
static ULONG owned_group(WND *w, WND **out, ULONG n, ULONG max, ULONG depth)
{
    if (depth < OWNER_DEPTH)
        for (WND *c = g_desktop->FirstChild; c; c = c->Next)
            if (owned_by(c, w) && !listed(out, n, c))
                n = owned_group(c, out, n, max, depth + 1);
    if (n < max && !listed(out, n, w))
        out[n++] = w;
    return n;
}

/*
 * the tree of the owner at the top of the chain of w. the branch of w comes first when the tree goes to the top,
 * and last, next to its owners, when it goes to the bottom or after another window.
 */
static ULONG owner_tree(WND *w, WND **out, ULONG max, BOOLEAN branch_first)
{
    WND *chain[OWNER_DEPTH];
    ULONG links = 0, n = 0;
    for (WND *c = w; c && links < OWNER_DEPTH; c = owner_of(c))
        chain[links++] = c;
    if (branch_first)
        n = owned_group(w, out, n, max, 0);
    for (ULONG k = 1; k < links; k++) {
        ULONG i = branch_first ? k : links - k;
        for (WND *c = g_desktop->FirstChild; c; c = c->Next)
            if (owned_by(c, chain[i]) && c != chain[i - 1] && !listed(out, n, c))
                n = owned_group(c, out, n, max, 0);
        if (branch_first && n < max)
            out[n++] = chain[i];
    }
    if (!branch_first) {
        n = owned_group(w, out, n, max, 0);
        for (ULONG i = 1; i < links && n < max; i++)
            out[n++] = chain[i];
    }
    return n;
}

static void zorder_alone(WND *w, HWND after)
{
    if (after == HWND_TOPMOST)
        w->ExStyle |= WS_EX_TOPMOST;
    else if (after == HWND_NOTOPMOST)
        w->ExStyle &= ~WS_EX_TOPMOST;
    WND *parent = w->Parent;
    unlink_window(w);
    link_window(parent, w, after);
}

static void zorder_top_level(WND *w, HWND after)
{
    ULONG max = 0;
    for (WND *c = g_desktop->FirstChild; c; c = c->Next)
        max++;
    WND **list = W32Alloc(2 * max * sizeof(WND *));
    if (!list) {
        zorder_alone(w, after);
        return;
    }
    ULONG n = 0;
    WND *target = after && after != HWND_TOPMOST && after != HWND_NOTOPMOST && after != HWND_BOTTOM ? WndLookup(after)
                                                                                                  : nullptr;
    if (target && (target->Parent != g_desktop || is_message_window(target)))
        target = nullptr;
    if (after == HWND_NOTOPMOST) {
        /* a window that is not topmost stays where it is. its whole tree leaves the band with it, in its order. */
        if (w->ExStyle & WS_EX_TOPMOST) {
            WND **members = list + max;
            ULONG count = owned_group(tree_root(w), members, 0, max, 0);
            for (WND *c = g_desktop->FirstChild; c; c = c->Next)
                if (listed(members, count, c))
                    list[n++] = c;
            for (ULONG i = n; i; i--) {
                list[i - 1]->ExStyle &= ~WS_EX_TOPMOST;
                unlink_window(list[i - 1]);
                link_window(g_desktop, list[i - 1], HWND_TOP);
            }
        }
    } else if (after == HWND_BOTTOM) {
        n = owner_tree(w, list, max, FALSE);
        BOOLEAN leaves = (w->ExStyle & WS_EX_TOPMOST) != 0;
        for (ULONG i = 0; i < n; i++) {
            if (leaves)
                list[i]->ExStyle &= ~WS_EX_TOPMOST;
            unlink_window(list[i]);
            link_window(g_desktop, list[i], HWND_BOTTOM);
        }
    } else if (!target) {
        if (after == HWND_TOPMOST) {
            n = owned_group(w, list, 0, max, 0);
            for (ULONG i = 0; i < n; i++)
                list[i]->ExStyle |= WS_EX_TOPMOST;
        }
        n = owner_tree(w, list, max, TRUE);
        for (ULONG i = n; i; i--) {
            unlink_window(list[i - 1]);
            link_window(g_desktop, list[i - 1], HWND_TOP);
        }
    } else if (target != w) {
        /* a window already under the other one stays, and so does one told to go under a window it owns. */
        n = owned_group(w, list, 0, max, 0);
        if (w->Prev != target && !listed(list, n, target)) {
            /* inside its tree the window moves with what it owns, outside the whole tree follows. */
            if (tree_root(w) != tree_root(target))
                n = owner_tree(w, list, max, FALSE);
            for (ULONG i = 0; i < n; i++)
                unlink_window(list[i]);
            /* what goes under a window that is not topmost is not either, and between two topmost ones it is. */
            if (!(target->ExStyle & WS_EX_TOPMOST)) {
                for (ULONG i = 0; i < n; i++)
                    list[i]->ExStyle &= ~WS_EX_TOPMOST;
            } else if (target->Next && (target->Next->ExStyle & WS_EX_TOPMOST)) {
                for (ULONG i = 0; i < n; i++)
                    list[i]->ExStyle |= WS_EX_TOPMOST;
            }
            WND *above = target;
            for (ULONG pass = 0; pass < 2; pass++)
                for (ULONG i = 0; i < n; i++)
                    if (!(list[i]->ExStyle & WS_EX_TOPMOST) == pass) {
                        link_after(g_desktop, list[i], above);
                        above = list[i];
                    }
        }
    }
    W32Free(list);
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
        if ((s->Style & WS_VISIBLE) && !(s->ExStyle & WS_EX_TRANSPARENT))
            RgnCombineRect(rgn, rgn, &s->WindowRect, RGN_DIFF);
}

static void vis_rgn(WND *w, BOOLEAN client, BOOLEAN clip_siblings, BOOLEAN clip_children, REGION *out)
{
    RgnClear(out);
    if (!w || w == g_desktop || !WndIsVisible(w))
        return;
    WND *top = WndTopLevel(w);
    RgnSetRect(out, client ? &w->ClientRect : &w->WindowRect);
    for (WND *c = w; c != top; c = c->Parent) {
        WND *p = c->Parent;
        RgnCombineRect(out, out, &p->ClientRect, RGN_AND);
        if (c == w ? clip_siblings : (c->Style & WS_CLIPSIBLINGS) != 0)
            subtract_siblings_above(c, out);
    }
    RgnCombineRect(out, out, &top->WindowRect, RGN_AND);
    if (clip_children)
        for (WND *c = w->FirstChild; c; c = c->Next)
            if ((c->Style & WS_VISIBLE) && !(c->ExStyle & WS_EX_TRANSPARENT))
                RgnCombineRect(out, out, &c->WindowRect, RGN_DIFF);
    RgnOffset(out, -top->WindowRect.left, -top->WindowRect.top);
    /* the surface may lag behind the window when it could not grow; never draw past it. */
    RECT surface = {0, 0, top->Surface ? top->Surface->Width : 0, top->Surface ? top->Surface->Height : 0};
    RgnCombineRect(out, out, &surface, RGN_AND);
}

void WndComputeVisRgn(WND *w, BOOLEAN client, ULONG dcx_flags, REGION *out)
{
    vis_rgn(w, client, (dcx_flags & DCX_CLIPSIBLINGS) || (w->Style & WS_CLIPSIBLINGS),
            (dcx_flags & DCX_CLIPCHILDREN) || (w->Style & WS_CLIPCHILDREN), out);
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
    /* the parent erases under a transparent child, which draws itself again. */
    if (!children && (flags & RDW_INVALIDATE))
        for (WND *c = w->FirstChild; c; c = c->Next)
            if ((c->Style & WS_VISIBLE) && (c->ExStyle & WS_EX_TRANSPARENT))
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
        /* a transparent window waits for the siblings beneath it, which it draws over. */
        if ((w->ExStyle & WS_EX_TRANSPARENT) && w->Next && find_paint(w->Next, t))
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
    /* an empty queue is asked again and again with nothing changed in between. */
    ULONG serial = DeskCleanSerial();
    if (serial && t->NoPaintSerial == serial)
        return nullptr;
    WND *w = find_paint(g_desktop->FirstChild, t);
    if (!w && serial)
        t->NoPaintSerial = serial;
    return w;
}

/* host configuration. */

void WndUpdateHostConfig(WND *w, BOOLEAN activate)
{
    if (!WndIsTopLevel(w) || !(w->Flags & WNDF_HOST_WINDOW))
        return;
    HostConfigure(w, activate);
}

/* a border of its own, measured on windows. */
static BOOLEAN has_border(const WND *w)
{
    return (w->Style & (WS_BORDER | WS_DLGFRAME | WS_THICKFRAME)) ||
           (w->ExStyle & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME));
}

/* pixels as gdi would leave them: alpha 255 in a device surface, alpha 0 in one the dwm composes by its alpha. */
static void fill_surface(SURFACE *s, const RECT *r, ULONG pixel)
{
    if (!(s->Flags & SURF_DEVICE))
        pixel &= 0xFFFFFF;
    for (LONG y = r->top; y < r->bottom; y++) {
        PULONG row = (PULONG)(s->Bits + (SIZE_T)y * s->Stride);
        for (LONG x = r->left; x < r->right; x++)
            row[x] = pixel;
        SurfaceSetAlpha(s, r->left, y, r->right - r->left, 0);
    }
}

static REGION *unpainted(SURFACE *s)
{
    if (!s->Unpainted && (s->Unpainted = W32Alloc(sizeof(REGION))))
        RgnInit(s->Unpainted);
    return s->Unpainted;
}

/*
 * what a new redirection surface holds on windows 11, measured on windows: gdi's white over the whole of a window with
 * a border, nothing of one without, which shows what is under it but for the rectangles gdi draws, see
 * SurfaceMarkDrawn. windows makes a new one each time a hidden window is shown again.
 */
static void start_surface(WND *w, SURFACE *s)
{
    RECT all = {0, 0, s->Width, s->Height};
    W32Free(s->Alpha);
    s->Alpha = nullptr;
    if (has_border(w)) {
        SurfaceForgetWindow(s);
        fill_surface(s, &all, 0xFFFFFFFFu);
    } else {
        memset(s->Bits, 0, (SIZE_T)s->Stride * (SIZE_T)s->Height);
        if (unpainted(s))
            RgnSetRect(s->Unpainted, &all);
    }
    s->Dirty = all;
}

/* a window that takes a border shows all of itself, black where it was never drawn. */
static void border_taken(WND *w)
{
    SURFACE *s = w->Surface;
    if (!s || !s->Unpainted || !has_border(w))
        return;
    for (ULONG i = 0; i < s->Unpainted->Count; i++)
        fill_surface(s, &s->Unpainted->Rects[i], 0xFF000000u);
    RgnFree(s->Unpainted);
    W32Free(s->Unpainted);
    s->Unpainted = nullptr;
    RECT all = {0, 0, s->Width, s->Height};
    SurfaceMarkDirty(s, &all);
}

/* top level windows render into a surface the size of their window rectangle. */
void WndEnsureSurface(WND *w)
{
    if (!WndIsTopLevel(w) || w->Parent == nullptr || (w->ExStyle & 0x80000000u) || w == WndShell)
        return;
    LONG width = w->WindowRect.right - w->WindowRect.left;
    LONG height = w->WindowRect.bottom - w->WindowRect.top;
    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;
    SURFACE *old = w->Surface;
    if (old && old->Width == width && old->Height == height)
        return;
    SURFACE *s = SurfaceCreate(width, height, 32,
                               (!old || (old->Flags & SURF_DEVICE) ? SURF_DEVICE : 0) | SURF_CONTIGUOUS | SURF_TOPDOWN);
    if (!s)
        return;
    s->Window = w;
    RECT all = {0, 0, width, height};
    if (old) {
        RECT kept = {0, 0, W32Min(width, old->Width), W32Min(height, old->Height)};
        PUCHAR alpha = old->Alpha ? SurfaceAlphaPlane(s) : nullptr;
        for (LONG y = 0; y < kept.bottom; y++) {
            memcpy(s->Bits + (SIZE_T)y * s->Stride, old->Bits + (SIZE_T)y * old->Stride, (SIZE_T)kept.right * 4);
            if (alpha)
                memcpy(alpha + (SIZE_T)y * width, old->Alpha + (SIZE_T)y * old->Width, (SIZE_T)kept.right);
        }
        /* what the window grew by is black on windows, or not there yet in one that started without a border. */
        REGION grown;
        RgnInit(&grown);
        RgnSetRect(&grown, &all);
        RgnCombineRect(&grown, &grown, &kept, RGN_DIFF);
        if (w->LayerFlags & MIME_LAYER_UPDATE) {
            /* UpdateLayeredWindow gives the pixels of what grew. */
        } else if (old->Unpainted && unpainted(s)) {
            RgnCombineRect(s->Unpainted, old->Unpainted, &all, RGN_AND);
            RgnCombine(s->Unpainted, s->Unpainted, &grown, RGN_OR);
        } else {
            for (ULONG i = 0; i < grown.Count; i++)
                fill_surface(s, &grown.Rects[i], 0xFF000000u);
        }
        RgnFree(&grown);
        PaintRetireSurface(old);
    } else {
        start_surface(w, s);
    }
    w->Surface = s;
    s->Dirty = all;
    FrameSurfaceAlpha(w);
    if (!(w->Flags & WNDF_HOST_WINDOW)) {
        HostWindowCreate(w);
        w->Flags |= WNDF_HOST_WINDOW;
    }
}

/*
 * a window hidden by ShowWindow or SetWindowPos and shown again starts over, as windows frees the redirection surface of
 * a hidden window. clearing WS_VISIBLE with SetWindowLong keeps it.
 */
void WndEnsureSurfaceShown(WND *w)
{
    WndEnsureSurface(w);
    if (!(w->Flags & WNDF_SURFACE_GONE))
        return;
    w->Flags &= ~WNDF_SURFACE_GONE;
    if (w->Surface && !(w->LayerFlags & MIME_LAYER_UPDATE)) {
        start_surface(w, w->Surface);
        FrameSurfaceAlpha(w);
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
static void calc_client(WND *w, const RECT *window, const RECT *old_window, RECT *client, BOOLEAN full,
                        const WINDOWPOS *pos)
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
    /* the flags of the change so far: what does not move or change size is marked as such before the answer. */
    if (pos) {
        data.pos.hwndInsertAfter = pos->hwndInsertAfter;
        data.pos.flags = pos->flags | SWP_NOCLIENTMOVE | SWP_NOCLIENTSIZE;
        if (window->left == old_window->left && window->top == old_window->top)
            data.pos.flags |= SWP_NOMOVE;
        if (window->right - window->left == old_window->right - old_window->left &&
            window->bottom - window->top == old_window->bottom - old_window->top)
            data.pos.flags |= SWP_NOSIZE;
    }
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

/*
 * moves the pixels of a region of a surface by dx,dy, dst being where they land. the rectangles are taken in the order
 * that never reads one already written.
 */
static void copy_surface_bits(SURFACE *s, const REGION *dst, LONG dx, LONG dy)
{
    for (ULONG n = 0; n < dst->Count; n++) {
        ULONG i = dy > 0 || (!dy && dx > 0) ? dst->Count - 1 - n : n;
        if (dy && dx) {
            /* bands go in the order of dy, and the rectangles of one band in the order of dx. */
            ULONG first = i, last = i;
            while (first && dst->Rects[first - 1].top == dst->Rects[i].top)
                first--;
            while (last + 1 < dst->Count && dst->Rects[last + 1].top == dst->Rects[i].top)
                last++;
            ULONG k = dy > 0 ? last - i : i - first;
            i = dx > 0 ? last - k : first + k;
        }
        const RECT *r = &dst->Rects[i];
        SIZE_T bytes = (SIZE_T)(r->right - r->left) * 4;
        for (LONG row = 0; row < r->bottom - r->top; row++) {
            LONG y = dy > 0 ? r->bottom - 1 - row : r->top + row;
            memmove(s->Bits + (SIZE_T)y * s->Stride + (SIZE_T)r->left * 4,
                    s->Bits + (SIZE_T)(y - dy) * s->Stride + (SIZE_T)(r->left - dx) * 4, bytes);
            if (s->Alpha)
                memmove(s->Alpha + (SIZE_T)y * s->Width + (SIZE_T)r->left,
                        s->Alpha + (SIZE_T)(y - dy) * s->Width + (SIZE_T)(r->left - dx), bytes / 4);
        }
    }
    SurfaceMarkDrawn(s, &dst->Extents, dst);
}

/* the window DestroyWindow hides, whose activation and focus leave it after it is hidden. */
static WND *g_hiding_destroyed;
static void WndDeactivate(void);

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
    /* WM_WINDOWPOSCHANGING gets the place and size as the caller gave them, whatever the flags keep. */
    if ((pos.flags & SWP_SHOWWINDOW) && (w->Style & WS_VISIBLE))
        pos.flags &= ~SWP_SHOWWINDOW;
    if ((pos.flags & SWP_HIDEWINDOW) && !(w->Style & WS_VISIBLE))
        pos.flags &= ~SWP_HIDEWINDOW;
    if (!(pos.flags & SWP_NOSENDCHANGING)) {
        send_kernel(w, WM_WINDOWPOSCHANGING, 0, &pos, MIME_LPARAM_INOUT, sizeof(pos));
        if (!alive(w))
            goto done;
        /*
         * the program may have moved the window meanwhile. what it wrote over a position or size the flags keep is
         * ignored: delphi forms write their own bounds there whatever the flags say.
         */
        old_window = w->WindowRect;
        screen_to_parent(w, &old_window);
        if (pos.flags & SWP_NOMOVE) {
            pos.x = old_window.left;
            pos.y = old_window.top;
        }
        if (pos.flags & SWP_NOSIZE) {
            pos.cx = old_window.right - old_window.left;
            pos.cy = old_window.bottom - old_window.top;
        }
    }
    if (pos.flags & SWP_NOSENDCHANGING) {
        if (pos.flags & SWP_NOMOVE) {
            pos.x = old_window.left;
            pos.y = old_window.top;
        }
        if (pos.flags & SWP_NOSIZE) {
            pos.cx = old_window.right - old_window.left;
            pos.cy = old_window.bottom - old_window.top;
        }
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
    /* a child that only moves keeps what it shows: its pixels follow it and only what the move uncovers is painted. */
    BOOLEAN copy_bits = moved && !sized && was_visible && !WndIsTopLevel(w) &&
                        !(pos.flags & (SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_HIDEWINDOW));
    REGION old_vis;
    RgnInit(&old_vis);
    if (copy_bits)
        vis_rgn(w, FALSE, TRUE, FALSE, &old_vis);

    if (moved || sized || (pos.flags & SWP_FRAMECHANGED)) {
        RECT client;
        if (sized || (pos.flags & SWP_FRAMECHANGED)) {
            calc_client(w, &new_window, &old_window, &client, TRUE, &pos);
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
        if (w->Parent != g_desktop || is_message_window(w) || (pos.flags & SWP_NOOWNERZORDER))
            zorder_alone(w, pos.hwndInsertAfter);
        else
            zorder_top_level(w, pos.hwndInsertAfter);
    }

    if (pos.flags & SWP_SHOWWINDOW)
        w->Style |= WS_VISIBLE;
    else if (pos.flags & SWP_HIDEWINDOW)
        w->Style &= ~WS_VISIBLE;
    BOOLEAN visible = WndIsVisible(w);

    if (WndIsTopLevel(w)) {
        if ((pos.flags & SWP_HIDEWINDOW) && w->Surface)
            w->Flags |= WNDF_SURFACE_GONE;
        if (pos.flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW))
            w->ShownInactive = (pos.flags & SWP_SHOWWINDOW) && (pos.flags & SWP_NOACTIVATE) && InputForeground != w;
        if (visible && !(w->Style & WS_MINIMIZE))
            WndEnsureSurfaceShown(w);
        if ((sized || (pos.flags & (SWP_FRAMECHANGED | SWP_SHOWWINDOW))) && visible && !(w->Style & WS_MINIMIZE)) {
            w->Flags |= WNDF_FRAME_DIRTY;
            WndInvalidateRegion(w, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        if (w->Flags & WNDF_HOST_WINDOW)
            WndUpdateHostConfig(w, FALSE);
    } else if (!(pos.flags & SWP_NOREDRAW)) {
        BOOLEAN changed = moved || sized || (pos.flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW | SWP_FRAMECHANGED)) ||
                          !(pos.flags & SWP_NOZORDER);
        WND *top = WndTopLevel(w);
        if (changed && copy_bits && visible && top->Surface && top->Surface->Bpp == 32) {
            LONG dx = w->WindowRect.left - old_screen.left, dy = w->WindowRect.top - old_screen.top;
            REGION new_vis, kept;
            RgnInit(&new_vis);
            RgnInit(&kept);
            vis_rgn(w, FALSE, TRUE, FALSE, &new_vis);
            /* the surface may have been made again since. */
            RECT surface = {0, 0, top->Surface->Width, top->Surface->Height};
            RgnCombineRect(&old_vis, &old_vis, &surface, RGN_AND);
            RgnOffset(&old_vis, dx, dy);
            RgnCombine(&kept, &new_vis, &old_vis, RGN_AND);
            copy_surface_bits(top->Surface, &kept, dx, dy);
            RgnCombine(&new_vis, &new_vis, &kept, RGN_DIFF);
            if (!RgnIsEmpty(&new_vis)) {
                RgnOffset(&new_vis, top->WindowRect.left, top->WindowRect.top);
                WndInvalidateRegion(w, &new_vis, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            }
            REGION left;
            RgnInit(&left);
            RgnSetRect(&left, &old_screen);
            RgnCombineRect(&left, &left, &w->WindowRect, RGN_DIFF);
            if (!RgnIsEmpty(&left))
                WndInvalidateRegion(w->Parent, &left, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            RgnFree(&left);
            RgnFree(&kept);
            RgnFree(&new_vis);
        } else if (changed) {
            /*
             * the parent draws again what the window uncovered, not what it still covers: a combo box of user32
             * takes the height of its field, draws itself at once with UpdateWindow, and is not drawn again after
             * its parent.
             */
            if (was_visible && visible && !(w->ExStyle & WS_EX_TRANSPARENT) && w->Parent && w->Parent != g_desktop) {
                REGION left;
                RgnInit(&left);
                RgnSetRect(&left, &old_screen);
                RgnCombineRect(&left, &left, &w->WindowRect, RGN_DIFF);
                if (!RgnIsEmpty(&left))
                    WndInvalidateRegion(w->Parent, &left, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
                RgnFree(&left);
            } else if (was_visible) {
                invalidate_screen_rect(w->Parent, &old_screen);
            }
            if (visible) {
                if (sized || (pos.flags & (SWP_SHOWWINDOW | SWP_FRAMECHANGED)) || !(pos.flags & SWP_NOZORDER) ||
                    !(pos.flags & SWP_NOCOPYBITS) || moved)
                    WndInvalidateRegion(w, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            }
        }
    }
    (void)old_client_screen;
    RgnFree(&old_vis);

    /* a visible top level window placed without SWP_NOACTIVATE becomes active, as BringWindowToTop makes it. */
    if (!(pos.flags & SWP_NOACTIVATE) && WndIsTopLevel(w) && visible && !(w->ExStyle & WS_EX_NOACTIVATE) &&
        ((pos.flags & SWP_SHOWWINDOW) || w->Thread == W32GetThread()))
        WndActivate(w, FALSE);
    if ((pos.flags & SWP_HIDEWINDOW) && WndIsTopLevel(w) && InputForeground == w && w != g_hiding_destroyed) {
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
    /*
     * a window that is hidden gives the focus it or a child holds to its parent. one that was hidden already keeps
     * it: a dialog page sets the focus to one of its controls, is moved in place and only then shown.
     */
    if (((was_visible && !visible) || (w->Style & WS_MINIMIZE)) && w->Thread && w != g_hiding_destroyed) {
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
    if (WndIsTopLevel(w) && !(pos.flags & SWP_DEFERERASE) &&
        ((pos.flags & SWP_SHOWWINDOW) || sized || (pos.flags & SWP_FRAMECHANGED))) {
        PaintSync(w);
        if (!alive(w))
            goto done;
    }
    /* Windows does not tell a window of a call that changed nothing. */
    BOOLEAN same = !moved && !sized && !client_moved && !client_sized && (pos.flags & SWP_NOZORDER) &&
                   !(pos.flags & (SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_HIDEWINDOW));
    if (!same && (!(pos.flags & SWP_NOSENDCHANGING) || moved || sized || (pos.flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW))))
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

void WndSystemChanged(BOOLEAN theme)
{
    OemBitmapsInvalidate();
    WND *desktop = WndDesktop();
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c; c = c->Next) {
        if (c->Flags & WNDF_DESTROYING)
            continue;
        if (c->Thread)
            QueuePost(c->Thread, WndHandle(c), WM_SYSCOLORCHANGE, 0, 0);
        if (theme)
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
    UINT dpi = WndMonitorDpi(w), old = w->Dpi ? w->Dpi : W32SystemDpi();
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

/*
 * NtUserInheritWindowMonitor: a window takes the monitor of another and follows it from then on, as the list of a
 * combo box takes the monitor of its box, or its own again for none. xxxInheritWindowMonitor of win32kfull 26300
 * keeps the other window in a property of the window; the desktop and message windows are left alone.
 */
BOOL NTAPI NtUserInheritWindowMonitor(HWND hwnd, HWND from)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    ULONG fnid = w && w->Class ? w->Class->Fnid & 0x2FFF : 0;
    if (!w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    } else if (w == g_desktop || fnid == FNID_DESKTOP || fnid == FNID_MESSAGEWND) {
    } else if (!w->Thread || w->Thread->Process != W32GetProcess()) {
        W32SetLastError(ERROR_ACCESS_DENIED);
    } else if (from && !WndLookup(from)) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    } else {
        w->InheritMonitor = from != hwnd ? from : nullptr;
        UserReference(&w->Header);
        WndCheckMonitorDpi(w);
        UserDereference(&w->Header);
        ok = TRUE;
    }
    UserLeave();
    return ok;
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

static BOOLEAN fits(const WND *w, LONG cx, LONG cy)
{
    LONG dx = w->WindowRect.right - w->WindowRect.left - cx, dy = w->WindowRect.bottom - w->WindowRect.top - cy;
    return dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2;
}

/*
 * a window the host does not place has a made up position, and one the host made larger must not hang off the
 * screen: the pointer stops at the edge of the screen and would never reach the border there.
 */
static void keep_on_monitor(WND *w)
{
    MIME_MONITOR_INFO m;
    if ((w->Flags & WNDF_HOST_PLACED) || (w->Style & (WS_MAXIMIZE | WS_MINIMIZE)))
        return;
    RECT rect = w->WindowRect;
    DpiRectConvert(&rect, w->Awareness, DPI_PHYSICAL);
    LONG index = MonitorFromRectIndex(&rect, FALSE);
    if (!MonitorGet(index < 0 ? 0 : (ULONG)index, &m))
        return;
    POINT at = {W32Max(m.Rect.left, W32Min(rect.left, m.Rect.right - (rect.right - rect.left))),
                W32Max(m.Rect.top, W32Min(rect.top, m.Rect.bottom - (rect.bottom - rect.top)))};
    if (at.x == rect.left && at.y == rect.top)
        return;
    DpiPointConvert(&at, DPI_PHYSICAL, w->Awareness);
    WndSetPos(w, nullptr, at.x, at.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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
    cx = window.right - window.left;
    cy = window.bottom - window.top;
    UserReference(&w->Header);
    /* the drag loop of windows places the window with its position and these flags, seen in WM_WINDOWPOSCHANGING. */
    if (MoveSizeHostStep(w, &cx, &cy)) {
        if (alive(w))
            WndSetPos(w, nullptr, window.left, window.top, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    } else {
        WndSetPos(w, nullptr, 0, 0, cx, cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (alive(w))
        keep_on_monitor(w);
    /*
     * a step of a border that is dragged. windows paints the windows of the thread right after it gives the window
     * its size (xxxDrawDragRectEx: xxxMoveSizeSetWindowPos, then xxxUpdateThreadsWindows), so a program draws itself
     * at each size and not when its queue is next empty, which it is not while the sizes keep coming.
     */
    if (alive(w))
        PaintUpdateNow(w);
    /*
     * a maximized window that keeps the size of the work area, as FL Studio's does, does not fit the place the
     * compositor gave it, a tile. it is restored there, as a maximized window dragged off the top of the screen is.
     */
    if (alive(w) && (w->Style & WS_MAXIMIZE) && !fits(w, cx, cy)) {
        MsgSend(w, WM_SYSCOMMAND, SC_RESTORE, 0);
        if (alive(w) && !(w->Style & (WS_MAXIMIZE | WS_MINIMIZE)) && !fits(w, cx, cy))
            WndSetPos(w, nullptr, 0, 0, cx, cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    UserDereference(&w->Header);
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

BOOLEAN WndCanMinimize(WND *w)
{
    return !(W32Display.NoMinimize && WndIsTopLevel(w));
}

/*
 * xxxShowOwnedWindows: the top level windows a window owns are told that it closes or opens, and the default
 * processing of WM_SHOWWINDOW hides the visible ones and shows the ones it hid.
 */
void WndShowOwned(WND *owner, UINT cmd)
{
    if ((owner->Style & (WS_CHILD | WS_POPUP)) == WS_CHILD)
        return;
    ULONG count = 0;
    for (WND *c = g_desktop->FirstChild; c; c = c->Next)
        count++;
    HWND *list = count ? W32Alloc(count * sizeof(HWND)) : nullptr;
    if (!list)
        return;
    count = 0;
    for (WND *c = g_desktop->FirstChild; c; c = c->Next)
        list[count++] = WndHandle(c);
    BOOLEAN show = cmd >= SW_PARENTOPENING;
    UserReference(&owner->Header);
    for (ULONG i = 0; i < count && alive(owner); i++) {
        WND *c = (WND *)UserLookup(list[i], USER_TYPE_WINDOW);
        if (!c || c == owner || (c->Flags & WNDF_DESTROYING))
            continue;
        /* the owner at the top of the chain, which stops at an overlapped window. */
        WND *top = c->Owner;
        while (top && top != owner && top->Owner) {
            ULONG kind = top->Style & (WS_CHILD | WS_POPUP);
            if (!kind || kind == (WS_CHILD | WS_POPUP))
                break;
            top = top->Owner;
        }
        if (top != owner)
            continue;
        if (show ? (c->Flags & WNDF_HIDDENPOPUP) != 0 : (c->Style & WS_VISIBLE) != 0)
            MsgSend(c, WM_SHOWWINDOW, show, cmd);
    }
    UserDereference(&owner->Header);
    W32Free(list);
}

void WndShow(WND *w, int cmd)
{
    if (!w || w == g_desktop || (UINT)cmd > SW_FORCEMINIMIZE)
        return;
    if (cmd == SW_SHOWDEFAULT)
        cmd = SW_SHOWNORMAL;
    if (!WndCanMinimize(w) && !(w->Style & WS_MINIMIZE) &&
        (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_SHOWMINNOACTIVE || cmd == SW_FORCEMINIMIZE)) {
        /* a window that is there stays as it is, one that opens as an icon opens as a window. */
        if (w->Style & WS_VISIBLE)
            return;
        cmd = cmd == SW_SHOWMINIMIZED ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE;
    }
    UserReference(&w->Header);
    if ((w->Flags & WNDF_FULLSCREEN) && cmd != SW_SHOW && cmd != SW_SHOWNA) {
        WndFullscreen(w, FALSE, nullptr);
        if (!alive(w))
            goto done;
    }
    BOOLEAN was_visible = (w->Style & WS_VISIBLE) != 0;
    BOOLEAN show = cmd != SW_HIDE;
    /* a child is shown where it is in the z order and does not become active. */
    BOOLEAN noactivate = cmd == SW_HIDE || cmd == SW_SHOWNOACTIVATE || cmd == SW_SHOWNA ||
                         cmd == SW_SHOWMINNOACTIVE || cmd == SW_MINIMIZE || cmd == SW_FORCEMINIMIZE ||
                         (w->Style & WS_CHILD);
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
    /*
     * xxxShowWindowEx: a window that is already shown is left as it is and does not become active, by SW_SHOW
     * whatever its state, and by the commands that restore when it is neither an icon nor maximized.
     */
    if (was_visible && (cmd == SW_SHOW || (!old_state && (cmd == SW_SHOWNORMAL || cmd == SW_RESTORE ||
                                                          cmd == SW_SHOWNOACTIVATE))))
        goto done;
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
            WndShowOwned(w, SW_PARENTCLOSING);
            if (!alive(w))
                goto done;
            /* a window that goes to an icon gives its focus to its parent, or to none. */
            W32THREAD *self = W32GetThread();
            for (WND *f = self ? WndLookup(self->Focus) : nullptr; f; f = f->Parent)
                if (f == w) {
                    WndSetFocus(WndIsTopLevel(w) ? nullptr : w->Parent);
                    break;
                }
            if (!alive(w))
                goto done;
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
        /* a child of a hidden window only changes its style: nothing of it is on the screen to place. */
        if (!state_changed && !WndIsTopLevel(w) && w->Parent && !WndIsVisible(w->Parent)) {
            if (show)
                w->Style |= WS_VISIBLE;
            else
                w->Style &= ~WS_VISIBLE;
            goto done;
        }
    }
    BOOLEAN restored = state_changed && (old_state & WS_MINIMIZE);
    if (state_changed || show != was_visible) {
        /* a top level window comes to the top when it is activated, shown by SW_SHOWNA, or restored from an icon. */
        if (show && WndIsTopLevel(w) && (!noactivate || cmd == SW_SHOWNA || restored))
            flags &= ~SWP_NOZORDER;
        /* what SetWindowPos is told to keep reaches WM_WINDOWPOSCHANGING as 0, as windows gives it. */
        LONG x = flags & SWP_NOMOVE ? 0 : rect.left, y = flags & SWP_NOMOVE ? 0 : rect.top;
        LONG cx = flags & SWP_NOSIZE ? 0 : rect.right - rect.left, cy = flags & SWP_NOSIZE ? 0 : rect.bottom - rect.top;
        WndSetPos(w, (WND *)HWND_TOP, x, y, cx, cy, flags);
        if (!alive(w))
            goto done;
        if (state_changed && (old_state & WS_MINIMIZE)) {
            WndShowOwned(w, SW_PARENTOPENING);
            if (!alive(w))
                goto done;
        }
    }
    if (show && (w->Flags & WNDF_SENDSIZEMOVE)) {
        w->Flags &= ~WNDF_SENDSIZEMOVE;
        WPARAM kind = (w->Style & WS_MINIMIZE) ? SIZE_MINIMIZED : (w->Style & WS_MAXIMIZE) ? SIZE_MAXIMIZED : SIZE_RESTORED;
        MsgSend(w, WM_SIZE, kind, kind == SIZE_MINIMIZED ? 0 :
                MAKELPARAM(w->ClientRect.right - w->ClientRect.left, w->ClientRect.bottom - w->ClientRect.top));
        if (!alive(w))
            goto done;
        RECT c = w->ClientRect;
        screen_to_parent(w, &c);
        MsgSend(w, WM_MOVE, 0, MAKELPARAM(c.left, c.top));
        if (!alive(w))
            goto done;
    }
    if (show && new_state == WS_MINIMIZE && noactivate)
        deactivate_minimized(w);
    else if (show && !noactivate && WndIsTopLevel(w))
        WndActivate(w, FALSE);
    /* xxxMinMaximizeEx: a window restored from an icon takes the focus, and a top level one is told it is active. */
    if (restored && !noactivate && alive(w)) {
        WndSetFocus(w);
        if (alive(w) && !(w->Style & WS_CHILD))
            MsgSend(w, WM_ACTIVATE, WA_ACTIVE, 0);
    }
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
/*
 * the window the host last said it focused, and one the program activated since that the host has not focused yet: a
 * focus the host gives a window before it handled the request for the next one must not take the activation back.
 */
WND *WndHostFocus, *WndHostPending;

/*
 * WM_ACTIVATEAPP goes to every top level window of the thread that loses the activation and of the one that gets it,
 * and only when the active thread changes, as windows does.
 */
void WndSendActivateApp(W32THREAD *t, BOOLEAN active, HANDLE other)
{
    if (!t || !g_desktop || t->AppActive == active)
        return;
    t->AppActive = active;
    HWND list[256];
    UINT count = 0;
    for (WND *c = g_desktop->FirstChild; c && count < sizeof(list) / sizeof(list[0]); c = c->Next) {
        if (c->Thread == t && !(c->Flags & WNDF_DESTROYING))
            list[count++] = WndHandle(c);
    }
    for (UINT i = 0; i < count; i++) {
        WND *c = WndLookup(list[i]);
        if (c && alive(c))
            MsgSendEvent(c, WM_ACTIVATEAPP, active, (LPARAM)other);
    }
}

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
        if (old->Thread && old->Thread != w->Thread)
            WndSendActivateApp(old->Thread, FALSE, w->Thread ? w->Thread->Tid : nullptr);
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
        zorder_top_level(w, HWND_TOP);
    }
    WndUpdateHostConfig(w, !WndActivatedByHost);
    WndHostPending = WndActivatedByHost || WndHostFocus == w ? nullptr : w;
    w->ShownInactive = FALSE;
    if (!old || old->Thread != w->Thread)
        WndSendActivateApp(w->Thread, TRUE, old && old->Thread ? old->Thread->Tid : nullptr);
    MsgSendEvent(w, WM_NCACTIVATE, TRUE, (LPARAM)WndHandle(old));
    MsgSendEvent(w, WM_ACTIVATE, MAKEWPARAM(click ? WA_CLICKACTIVE : WA_ACTIVE, (w->Style & WS_MINIMIZE) != 0),
                 (LPARAM)WndHandle(old));
    /* an icon that becomes active takes no focus, and a focus left in another window goes. */
    if (alive(w) && (w->Style & WS_MINIMIZE) && w->Thread && w->Thread == W32GetThread()) {
        WND *focus = WndLookup(w->Thread->Focus);
        if (focus && WndTopLevel(focus) != w)
            WndSetFocus(nullptr);
    }
done:
    UserDereference(&w->Header);
}

/* the foreground goes to no window: the one that had it is told, as when the host takes its focus away. */
static void WndDeactivate(void)
{
    WND *old = InputForeground;
    W32THREAD *t = W32GetThread();
    if (t && (!old || old->Thread != t))
        t->Active = nullptr;
    if (!old || !alive(old))
        return;
    UserReference(&old->Header);
    InputSetForeground(nullptr);
    if (old->Thread)
        old->Thread->Active = nullptr;
    ImeFocusChanged();
    MsgSendEvent(old, WM_NCACTIVATE, FALSE, 0);
    if (alive(old))
        MsgSendEvent(old, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, (old->Style & WS_MINIMIZE) != 0), 0);
    if (alive(old))
        WndSendActivateApp(old->Thread, FALSE, nullptr);
    UserDereference(&old->Header);
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
    InputKeysChanged();
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

/*
 * ChildWindowFromPoint and ChildWindowFromPointEx, as win32kfull 26300 does it. the point is in the client area of the
 * parent, packed in one argument like the one of WindowFromPoint. outside that area or the shape of the parent there
 * is no window. inside, the answer is the first direct child from the top whose window rectangle holds the point and
 * the flags let through, else the parent itself.
 */
HWND NTAPI NtUserChildWindowFromPointEx(HWND hwnd, ULONG_PTR point, UINT flags)
{
    POINT pt = {(LONG)(ULONG)point, (LONG)(ULONG)(point >> 32)};
    UserEnterRead();
    WND *w = WndLookup(hwnd);
    HWND found = nullptr;
    if (w) {
        size_from_caller(w, &pt);
        if (w != g_desktop) {
            pt.x = (w->ExStyle & WS_EX_LAYOUTRTL) ? w->ClientRect.right - pt.x : w->ClientRect.left + pt.x;
            pt.y += w->ClientRect.top;
        }
        if (RectContains(&w->ClientRect, pt.x, pt.y) && LayerHitTest(w, pt)) {
            found = WndHandle(w);
            for (WND *c = w->FirstChild; c; c = c->Next) {
                POINT at = pt;
                if (w == g_desktop)
                    DpiPixelConvert(&at, DPI_PHYSICAL, c->Awareness);
                if (((flags & CWP_SKIPINVISIBLE) && !(c->Style & WS_VISIBLE)) ||
                    ((flags & CWP_SKIPDISABLED) && (c->Style & WS_DISABLED)) ||
                    ((flags & CWP_SKIPTRANSPARENT) && (c->ExStyle & WS_EX_TRANSPARENT)))
                    continue;
                if (RectContains(&c->WindowRect, at.x, at.y) && LayerHitTest(c, at)) {
                    found = WndHandle(c);
                    break;
                }
            }
        }
    }
    UserLeave();
    if (!w)
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return found;
}

/* the processes SetAdditionalForegroundBoostProcesses named for the window, which it lets go of. */
static void release_boost(WND *w)
{
    for (ULONG i = 0; i < w->BoostCount; i++)
        ObDereferenceObject(w->BoostProcesses[i]);
    W32Free(w->BoostProcesses);
    w->BoostProcesses = nullptr;
    w->BoostCount = 0;
}

/* destruction. */

static void free_window(WND *w)
{
    ScreenHeldGone(w);
    GdiWindowDestroyed(w);
    TimerRemoveWindow(w);
    HotkeyRemoveWindow(w);
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
    if (WndHostFocus == w)
        WndHostFocus = nullptr;
    if (WndHostPending == w)
        WndHostPending = nullptr;
    if (WndShell == w)
        WndShell = nullptr;
    MenuWindowDestroyed(w);
    ScrollWindowDestroyed(w);
    ClipboardWindowDestroyed(w);
    DndWindowDestroyed(w);
    TrayWindowDestroyed(w);
    ImeWindowDestroyed(w);
    PropFreeWindow(w);
    W32Free(w->ShutdownReason);
    W32Free(w->MessageFilter);
    w->MessageFilter = nullptr;
    w->MessageFilterCount = w->MessageFilterCapacity = 0;
    release_boost(w);
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
    for (WND *c = g_desktop ? g_desktop->FirstChild : nullptr; c; c = c->Next)
        if (c->HostOwner == w)
            c->HostOwner = nullptr;
    if (w->Class)
        w->Class->Windows--;
    W32Free(w->Text);
    w->Text = nullptr;
    W32Free(w->Extra);
    w->Extra = nullptr;
    W32Free(w->Scroll);
    w->Scroll = nullptr;
    /*
     * user32 frees its copy of the extra bytes when its own thread lets the window go. a thread that ends is not
     * called back: a call that never returns would leave the window half freed, with the thread gone under it.
     */
    PVOID user_extra = w->UserExtra;
    w->UserExtra = nullptr;
    if (user_extra && w->Thread == PsGetCurrentThreadWin32Thread() && !w->Thread->Destroying)
        ClientFreeExtraBytes(w, user_extra);
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
    /*
     * xxxDestroyWindow: the menu bar of a top level window is let go first, then the window is hidden without
     * activating anything, and only then does the activation and the focus leave it.
     */
    if (WndIsTopLevel(w)) {
        MsgSend(w, 0x0090, 0, 0); /* WM_UAHDESTROYWINDOW */
        if (!alive(w) || (w->Flags & WNDF_DESTROYING))
            goto done;
    }
    if (w->Style & WS_VISIBLE) {
        g_hiding_destroyed = w;
        WndSetPos(w, nullptr, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        g_hiding_destroyed = nullptr;
        if (!alive(w) || (w->Flags & WNDF_DESTROYING))
            goto done;
    }
    if (WndIsTopLevel(w) && InputForeground == w) {
        WND *next = w->Owner && WndIsVisible(w->Owner) ? w->Owner : nullptr;
        for (WND *c = g_desktop->FirstChild; !next && c; c = c->Next)
            if (c != w && (c->Style & WS_VISIBLE) && c->Thread == w->Thread && !(c->ExStyle & WS_EX_NOACTIVATE) &&
                !(c->Flags & WNDF_DESTROYING))
                next = c;
        if (next)
            WndActivate(next, FALSE);
        else
            WndDeactivate();
    }
    if (alive(w) && w->Thread) {
        WND *focus = WndLookup(w->Thread->Focus);
        if (focus && (focus == w || WndIsChildOf(focus, w)))
            WndSetFocus(w->Parent && w->Parent != g_desktop ? w->Parent : nullptr);
    }
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

/*
 * the strings of NtUserCreateWindowEx: a LARGE_STRING, a length and a room in bytes of which the top bit says the
 * buffer is ansi, then the buffer, or an atom in place of its address. a name of a resource is 0xffff and its
 * number, or 0xff and its number in an ansi one.
 */
typedef struct LARGE_STRING_X {
    ULONG Length;
    ULONG MaximumLength; /* bit 31 is bAnsi. */
    ULONG64 Buffer;
} LARGE_STRING_X;

static NTSTATUS capture_large_string(const void *user, PWSTR *out, SIZE_T *chars, ATOM *atom)
{
    *out = nullptr;
    if (chars)
        *chars = 0;
    if (atom)
        *atom = 0;
    if ((ULONG_PTR)user < 0x10000) {
        if (atom)
            *atom = (ATOM)(ULONG_PTR)user;
        return user || atom ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    }
    LARGE_STRING_X s;
    if (!NT_SUCCESS(MimeCopyFromUser(&s, user, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    BOOLEAN ansi = (s.MaximumLength & 0x80000000u) != 0;
    ULONG bytes = s.Length;
    if (bytes > 0x10000)
        bytes = 0x10000;
    if (!s.Buffer)
        bytes = 0;
    UCHAR *raw = W32Alloc(bytes + 2);
    PWSTR text = W32Alloc(((SIZE_T)bytes + 2) * sizeof(WCHAR));
    if (!raw || !text) {
        W32Free(raw);
        W32Free(text);
        return STATUS_NO_MEMORY;
    }
    if (bytes && !NT_SUCCESS(MimeCopyFromUser(raw, (PVOID)(ULONG_PTR)s.Buffer, bytes))) {
        W32Free(raw);
        W32Free(text);
        return STATUS_ACCESS_VIOLATION;
    }
    SIZE_T n;
    if (ansi && bytes >= 3 && raw[0] == 0xFF) {
        text[0] = 0xFFFF;
        text[1] = (WCHAR)(raw[1] | raw[2] << 8);
        n = 2;
    } else if (ansi) {
        ULONG written = 0;
        RtlMultiByteToUnicodeN(text, bytes * sizeof(WCHAR), &written, (const CHAR *)raw, bytes);
        n = written / sizeof(WCHAR);
    } else {
        n = bytes / sizeof(WCHAR);
        memcpy(text, raw, n * sizeof(WCHAR));
    }
    text[n] = 0;
    W32Free(raw);
    *out = text;
    if (chars)
        *chars = n;
    return STATUS_SUCCESS;
}

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
    /* a class given by its atom is that atom in the structure. */
    BOOLEAN class_atom = (ULONG_PTR)class_name < 0x10000;
    SIZE_T name_bytes = name ? (wcslen(name) + 1) * sizeof(WCHAR) : 0;
    SIZE_T class_bytes = class_atom ? 0 : (wcslen(class_name) + 1) * sizeof(WCHAR);
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
    cs->style = (LONG)w->CreateStyle;
    cs->dwExStyle = w->ExStyle;
    SIZE_T off = sizeof(CREATESTRUCTW);
    if (name) {
        memcpy(buf + off, name, name_bytes);
        cs->lpszName = (LPCWSTR)off;
        off += name_bytes;
    }
    if (class_atom) {
        cs->lpszClass = class_name;
    } else {
        memcpy(buf + off, class_name, class_bytes);
        cs->lpszClass = (LPCWSTR)off;
    }
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

/*
 * the creation as user32 of 26300.9457 asks for it: the class as the caller named it and the name user32 found for
 * its version in the activation context, each a LARGE_STRING or an atom, the name of the window, the usual
 * parameters, then the band of the window, the version of windows the caller was made for with the flags of
 * CreateWindowInternal in its top bits, a value user32 always gives as 0 and the activation context. bit 31 of the
 * extended style says the caller is ansi.
 */
HWND NTAPI NtUserCreateWindowEx(DWORD ex_style, const void *user_class, const void *user_version,
                                const void *user_name, DWORD style, int x, int y, int cx, int cy, HWND parent_h,
                                HMENU menu, HINSTANCE instance, LPVOID param, DWORD band, DWORD flags, DWORD unknown,
                                PVOID actctx)
{
    PWSTR class_name = nullptr, version_name = nullptr, name = nullptr;
    ATOM atom, version_atom;
    SIZE_T name_chars = 0;
    HWND result = nullptr;
    (void)band, (void)unknown, (void)actctx, (void)flags;
    ex_style &= ~0x80000000u;
    DWORD requested_style = style;
    if (!NT_SUCCESS(capture_large_string(user_class, &class_name, nullptr, &atom)) ||
        !NT_SUCCESS(capture_large_string(user_version ? user_version : user_class, &version_name, nullptr,
                                         &version_atom))) {
        DbgPrint("win32k: CreateWindowEx pid %u: bad class pointer\n", (ULONG)(ULONG_PTR)PsGetCurrentProcessId());
        W32Free(class_name);
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (user_name && !NT_SUCCESS(capture_large_string(user_name, &name, &name_chars, nullptr))) {
        W32Free(class_name);
        W32Free(version_name);
        W32SetLastError(ERROR_NOACCESS);
        return nullptr;
    }
    /* the caption of a window is drawn here, with a font no program asked for. */
    if ((style & WS_CAPTION) == WS_CAPTION && !(style & WS_CHILD)) {
        LOGFONTW caption = W32CaptionFontForDpi(96);
        FontAskFor(caption.lfFaceName);
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    WND *desktop = WndDesktop();
    WND_CLASS *cls = t ? ClassFind(t->Process, version_name, version_atom, instance) : nullptr;
    if (!cls || !desktop) {
        W32SetLastError(1407); /* ERROR_CANNOT_FIND_WND_CLASS */
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
    /*
     * the list of a combo box has a class of its own, yet ListBoxWndProcWorker of user32 26300 takes a window of
     * no kind or a list box alone, and refuses WM_NCCREATE for any other.
     */
    w->Fnid = (USHORT)(cls->Fnid == FNID_COMBOLISTBOX ? FNID_LISTBOX : cls->Fnid);
    if (w->ExtraSize) {
        w->UserExtra = ClientAllocExtraBytes(w->ExtraSize);
        WndExtraChanged(w, 0, w->ExtraSize);
    }
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
    if (cls->Fnid == FNID_DIALOG)
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
    /* the structures of WM_NCCREATE and WM_CREATE carry the style the caller asked for. */
    w->CreateStyle = requested_style;
    if (name)
        w->Flags |= WNDF_HASNAME;
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
    /* a name that is 0xffff and the number of a resource reaches WM_CREATE and is not the text of the window. */
    if (name && name[0] != 0xFFFF)
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
        PCWSTR hook_class = class_name ? class_name : (PCWSTR)(ULONG_PTR)atom;
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
    PCWSTR real_class = class_name ? class_name : (PCWSTR)(ULONG_PTR)atom;
    PVOID cs = build_createstruct(w, param, parent_h, x, y, cx, cy, name, real_class, &cs_size);
    if (!cs)
        goto fail;
    LRESULT r = send_kernel(w, WM_NCCREATE, 0, cs, MIME_LPARAM_CREATESTRUCT, cs_size);
    if (!alive(w) || !r) {
        W32Free(cs);
        goto fail;
    }

    /*
     * the window may have been placed again while it handled WM_NCCREATE, the client area follows where it is now.
     * windows gives this first WM_NCCALCSIZE the rectangle on the screen, a child's included.
     */
    RECT window = w->WindowRect, client;
    calc_client(w, &window, &window, &client, FALSE, nullptr);
    if (!alive(w)) {
        W32Free(cs);
        goto fail;
    }
    w->ClientRect = client;

    r = send_kernel(w, WM_CREATE, 0, cs, MIME_LPARAM_CREATESTRUCT, cs_size);
    W32Free(cs);
    if (!alive(w) || r == -1)
        goto fail;
    w->Flags |= WNDF_SENT_CREATE;

    /* an overlapped window is told of its size and place when it is first shown, the others now. */
    if (!(style & (WS_CHILD | WS_POPUP)) && !message_only)
        w->Flags |= WNDF_SENDSIZEMOVE;
    else if (!(w->Flags & WNDF_DESTROYING)) {
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
    W32Free(version_name);
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

static BOOLEAN follows(LONG moved, LONG pointer)
{
    LONG d = moved - pointer;
    return d > -8 && d < 8;
}

/*
 * a program that draws its own frame moves and sizes its window by placing it at each move of the mouse while the
 * button is held, as FL Studio does. a host whose compositor alone places windows, as on Wayland, takes the drag over
 * once a step of the window is the step of the pointer, and the button is released for the program. TRUE when it did.
 */
static BOOLEAN host_takes_drag(WND *w, POINT at, POINT size, UINT flags)
{
    static ULONG press;
    static HWND window;
    static POINT last;
    /* a window with an owner or a tool style is no window of the compositor to move: a menu, or what a drag carries. */
    if (!InputHostPress || !WndIsTopLevel(w) || !(w->Flags & WNDF_HOST_WINDOW) || w->Thread != W32GetThread() ||
        (w->Style & (WS_VISIBLE | WS_MINIMIZE | WS_MAXIMIZE)) != WS_VISIBLE || w->Owner ||
        (w->ExStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)))
        return FALSE;
    if (press != InputHostPress || window != WndHandle(w)) {
        press = InputHostPress;
        window = WndHandle(w);
        last = InputPressPos;
    }
    LONG dx = InputCursorPos.x - last.x, dy = InputCursorPos.y - last.y;
    last = InputCursorPos;
    if (!dx && !dy)
        return FALSE;
    RECT now = w->WindowRect, r = now;
    if (!(flags & SWP_NOMOVE))
        r = (RECT){at.x, at.y, at.x + r.right - r.left, at.y + r.bottom - r.top};
    if (!(flags & SWP_NOSIZE)) {
        r.right = r.left + size.x;
        r.bottom = r.top + size.y;
    }
    LONG left = r.left - now.left, top = r.top - now.top, right = r.right - now.right, bottom = r.bottom - now.bottom;
    ULONG edge = 0;
    if (left == right && top == bottom) {
        if ((!left && !top) || !follows(left, dx) || !follows(top, dy))
            return FALSE;
    } else {
        /* one side stays where it was on each axis that changes. */
        if ((left && right) || (top && bottom) || (left && !follows(left, dx)) || (right && !follows(right, dx)) ||
            (top && !follows(top, dy)) || (bottom && !follows(bottom, dy)))
            return FALSE;
        static const UCHAR edges[3][3] = {{0, WMSZ_LEFT, WMSZ_RIGHT},
                                          {WMSZ_TOP, WMSZ_TOPLEFT, WMSZ_TOPRIGHT},
                                          {WMSZ_BOTTOM, WMSZ_BOTTOMLEFT, WMSZ_BOTTOMRIGHT}};
        edge = edges[top ? 1 : bottom ? 2 : 0][left ? 1 : right ? 2 : 0];
    }
    /* a host that refuses is asked again at the next step: a window leaves the whole screen as it starts to move. */
    return HostMoveSize(w, edge, FALSE);
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
        if ((flags & (SWP_NOMOVE | SWP_NOSIZE)) == (SWP_NOMOVE | SWP_NOSIZE) || !host_takes_drag(w, at, size, flags))
            WndSetPos(w, (WND *)after, at.x, at.y, size.x, size.y, flags);
        ok = TRUE;
    }
    UserLeave();
    return ok;
}

/* MoveWindow: a SetWindowPos that keeps the z order and the activation, and leaves the painting out without repaint. */
BOOL NTAPI NtUserMoveWindow(HWND hwnd, int x, int y, int cx, int cy, BOOL repaint)
{
    return NtUserSetWindowPos(hwnd, nullptr, x, y, cx, cy,
                              SWP_NOZORDER | SWP_NOACTIVATE | (repaint ? 0 : SWP_NOREDRAW));
}

/*
 * BeginDeferWindowPos, DeferWindowPos and EndDeferWindowPos, like win32kfull 26300: a list of positions a thread
 * owns, which the end applies in the order they came and frees. the end answers TRUE whatever it applied. a
 * position for a window band is for the shell alone, as a program has no access to bands.
 */
#define ERROR_INVALID_DWP_HANDLE_ 1405
#define ERROR_SCREEN_ALREADY_LOCKED_ 1440
#define DWP_MAX 0xC30C30

typedef struct DEFER_POS {
    HWND Hwnd, After;
    int X, Y, Cx, Cy;
    UINT Flags;
} DEFER_POS;

typedef struct DEFER_LIST {
    USER_OBJECT Header;
    W32THREAD *Thread;
    ULONG Count, Size;
    DEFER_POS *Pos;
} DEFER_LIST;

static void defer_free(DEFER_LIST *d)
{
    if (d->Header.Destroyed)
        return;
    d->Header.Destroyed = TRUE;
    W32Free(d->Pos);
    d->Pos = nullptr;
    UserDereference(&d->Header);
}

HANDLE NTAPI NtUserBeginDeferWindowPos(int count)
{
    ULONG size = count ? (ULONG)count : 8;
    if (count < 0 || size > DWP_MAX) {
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    DEFER_LIST *d = W32Alloc(sizeof(*d));
    DEFER_POS *pos = W32Alloc(size * sizeof(DEFER_POS));
    HANDLE h = nullptr;
    if (d && pos) {
        UserEnter();
        d->Thread = W32GetThread();
        d->Size = size;
        d->Pos = pos;
        h = UserCreateHandle(&d->Header, USER_TYPE_SETWINDOWPOS);
        UserLeave();
    }
    if (!h) {
        W32Free(pos);
        W32Free(d);
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    return h;
}

static int clamp_coord(int v, int low)
{
    return v > 0x7FFF ? 0x7FFF : v < low ? low : v;
}

HANDLE NTAPI NtUserDeferWindowPosAndBand(HANDLE hdwp, HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags,
                                         ULONG band, ULONG band_options)
{
    if (flags & 0xFFFD9800) {
        W32SetLastError(ERROR_INVALID_FLAGS);
        return nullptr;
    }
    UserEnter();
    DEFER_LIST *d = (DEFER_LIST *)UserLookup(hdwp, USER_TYPE_SETWINDOWPOS);
    HANDLE result = nullptr;
    ULONG error = 0;
    WND *w = d ? WndLookup(hwnd) : nullptr;
    if (!d) {
        error = ERROR_INVALID_DWP_HANDLE_;
    } else if (!can_touch(w) ||
               (after && after != HWND_BOTTOM && after != HWND_TOPMOST && after != HWND_NOTOPMOST && !WndLookup(after))) {
        error = ERROR_INVALID_WINDOW_HANDLE;
    } else if (band_options) {
        error = ERROR_ACCESS_DENIED;
    } else if (flags & ~0x3FFu) {
        error = ERROR_INVALID_PARAMETER;
    } else {
        if (!(flags & SWP_NOMOVE)) {
            x = clamp_coord(x, -0x8000);
            y = clamp_coord(y, -0x8000);
        }
        if (!(flags & SWP_NOSIZE)) {
            cx = clamp_coord(cx, 0);
            cy = clamp_coord(cy, 0);
        }
        if (d->Count == d->Size) {
            ULONG size = d->Size + 4;
            DEFER_POS *pos = size <= DWP_MAX ? W32Alloc(size * sizeof(DEFER_POS)) : nullptr;
            if (pos) {
                memcpy(pos, d->Pos, d->Count * sizeof(DEFER_POS));
                W32Free(d->Pos);
                d->Pos = pos;
                d->Size = size;
            }
        }
        if (d->Count < d->Size) {
            d->Pos[d->Count++] = (DEFER_POS){hwnd, after, x, y, cx, cy, flags};
            result = hdwp;
        } else {
            defer_free(d);
            error = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    if (error && d && band_options)
        defer_free(d);
    UserLeave();
    if (error)
        W32SetLastError(error);
    return result;
}

BOOL NTAPI NtUserEndDeferWindowPosEx(HANDLE hdwp, BOOL async)
{
    UserEnter();
    DEFER_LIST *d = (DEFER_LIST *)UserLookup(hdwp, USER_TYPE_SETWINDOWPOS);
    if (!d) {
        UserLeave();
        W32SetLastError(ERROR_INVALID_DWP_HANDLE_);
        return FALSE;
    }
    /* the list leaves the handle table first, so a procedure called by a move cannot end it again. */
    DEFER_POS *pos = d->Pos;
    ULONG count = d->Count;
    d->Pos = nullptr;
    defer_free(d);
    UserLeave();
    (void)async;
    for (ULONG i = 0; i < count; i++) {
        /* every position is worked out from the window as it was, so of two for one window the last one counts. */
        ULONG later = i + 1;
        while (later < count && pos[later].Hwnd != pos[i].Hwnd)
            later++;
        if (later == count)
            NtUserSetWindowPos(pos[i].Hwnd, pos[i].After, pos[i].X, pos[i].Y, pos[i].Cx, pos[i].Cy, pos[i].Flags);
    }
    W32Free(pos);
    return TRUE;
}

/*
 * LockWindowUpdate, like zzzLockWindowUpdate2 of win32kfull 26300: one window of the screen at a time, locked by one
 * thread, and a lock or an unlock that changes nothing fails with ERROR_INVALID_PARAMETER. drawing goes on while it is
 * locked, so the unlock paints the window and its children again, which is what windows does with what was drawn.
 */
static WND *g_update_locked;
static W32THREAD *g_update_locker;

static void unlock_update(void)
{
    WND *w = g_update_locked;
    g_update_locked = nullptr;
    g_update_locker = nullptr;
    if (w) {
        if (alive(w))
            WndInvalidateRegion(w, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        UserDereference(&w->Header);
    }
}

BOOL NTAPI NtUserLockWindowUpdate(HWND hwnd)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    WND *w = hwnd ? WndLookup(hwnd) : nullptr;
    ULONG error = 0;
    if (hwnd && !w)
        error = ERROR_INVALID_WINDOW_HANDLE;
    else if (g_update_locker && g_update_locker != t)
        error = ERROR_SCREEN_ALREADY_LOCKED_;
    else if ((w != nullptr) == (g_update_locked != nullptr))
        error = ERROR_INVALID_PARAMETER;
    else if (w) {
        UserReference(&w->Header);
        g_update_locked = w;
        g_update_locker = t;
    } else {
        unlock_update();
    }
    UserLeave();
    if (error)
        W32SetLastError(error);
    return !error;
}

void WndPosThreadGone(W32THREAD *t)
{
    if (g_update_locker == t)
        unlock_update();
    ULONG index = 0;
    for (USER_OBJECT *o; (o = UserNextObject(&index, USER_TYPE_SETWINDOWPOS));)
        if (((DEFER_LIST *)o)->Thread == t)
            defer_free((DEFER_LIST *)o);
}

/*
 * TransformPoint and TransformRect of user32, when the dpi awareness context of the thread is not the one of the
 * window: the coordinates go from the dpi of one context, in bits 8 to 16, to the dpi of the other.
 */
static LONG context_dpi(DWORD context)
{
    LONG dpi = (LONG)((context >> 8) & 0x1FF);
    return dpi ? dpi : 96;
}

BOOL NTAPI NtUserTransformPoint(POINT *user_point, DWORD from, DWORD to, HANDLE monitor)
{
    POINT p;
    if (!NT_SUCCESS(MimeCopyFromUser(&p, user_point, sizeof(p))))
        return FALSE;
    (void)monitor;
    p.x = W32MulDiv(p.x, context_dpi(to), context_dpi(from));
    p.y = W32MulDiv(p.y, context_dpi(to), context_dpi(from));
    return NT_SUCCESS(MimeCopyToUser(user_point, &p, sizeof(p)));
}

BOOL NTAPI NtUserTransformRect(RECT *user_rect, DWORD from, DWORD to, HANDLE monitor)
{
    RECT r;
    if (!NT_SUCCESS(MimeCopyFromUser(&r, user_rect, sizeof(r))))
        return FALSE;
    (void)monitor;
    r.left = W32MulDiv(r.left, context_dpi(to), context_dpi(from));
    r.top = W32MulDiv(r.top, context_dpi(to), context_dpi(from));
    r.right = W32MulDiv(r.right, context_dpi(to), context_dpi(from));
    r.bottom = W32MulDiv(r.bottom, context_dpi(to), context_dpi(from));
    return NT_SUCCESS(MimeCopyToUser(user_rect, &r, sizeof(r)));
}

/*
 * SetWindowState and ClearWindowState of user32: the high byte of the flag is a byte of the window, 8 of state, 4 of
 * extended style and 4 of style, and the low byte the bits in it. a control changes its own styles this way with no
 * message, as a combo box gives up the scroll bar and the edge asked for it, which are those of its list. the
 * state bytes are made anew for each window that is mirrored, so only the styles are kept.
 */
static BOOL window_state(HWND hwnd, DWORD flag, BOOLEAN set)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = w && w->Thread && w->Thread->Process == W32GetProcess();
    if (ok) {
        ULONG index = (flag >> 8) & 0xFF;
        DWORD bits = (flag & 0xFF) << ((index & 3) * 8);
        DWORD *word = index >= 8 && index < 12 ? &w->ExStyle : index >= 12 && index < 16 ? &w->Style : nullptr;
        if (word)
            *word = set ? *word | bits : *word & ~bits;
    } else {
        W32SetLastError(w ? ERROR_ACCESS_DENIED : ERROR_INVALID_WINDOW_HANDLE);
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserSetWindowState(HWND hwnd, DWORD flag)
{
    return window_state(hwnd, flag, TRUE);
}

BOOL NTAPI NtUserClearWindowState(HWND hwnd, DWORD flag)
{
    return window_state(hwnd, flag, FALSE);
}

/*
 * AlterWindowStyle of user32 26300: the scroll bar bits and the low control bits of the style, 0x30023f, change by
 * the mask with no message. a window of another process stays as it is and the call still succeeds.
 */
BOOL NTAPI NtUserAlterWindowStyle(HWND hwnd, DWORD mask, DWORD flags)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (w && w->Thread && w->Thread->Process == W32GetProcess()) {
        mask &= 0x30023F;
        w->Style = (w->Style & ~mask) | (flags & mask);
    } else if (!w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    }
    UserLeave();
    return w != nullptr;
}

/*
 * user32 names the procedure a window runs with when it takes over a window of no class of its own, as DefDlgProc
 * makes a window a dialog. a window keeps the first one, of its own process; 0x4000 marks it cleaned up.
 */
BOOL NTAPI NtUserSetWindowFNID(HWND hwnd, WORD fnid)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    if (!w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    } else if (!w->Thread || w->Thread->Process != W32GetProcess()) {
        W32SetLastError(ERROR_ACCESS_DENIED);
    } else if (fnid == 0x4000 || (!(w->Fnid & 0x3FFF) && fnid >= FNID_FIRST && fnid <= FNID_LAST_CLIENT)) {
        w->Fnid = fnid == 0x4000 ? (USHORT)(w->Fnid | 0x4000) : fnid;
        ok = TRUE;
    } else {
        W32SetLastError(ERROR_ACCESS_DENIED);
    }
    UserLeave();
    return ok;
}

/*
 * SetDialogPointer: user32 keeps its structure of a dialog in the second pointer of the extra bytes of the window,
 * which must hold DLGWINDOWEXTRA. the window becomes a dialog, or one cleaned up when the pointer is taken away.
 */
BOOL NTAPI NtUserSetDialogPointer(HWND hwnd, PVOID pointer)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (w && w->Extra && w->ExtraSize >= 30 && w->Proc && w->Thread && w->Thread->Process == W32GetProcess()) {
        memcpy(w->Extra + 8, &pointer, sizeof(pointer));
        WndExtraChanged(w, 8, sizeof(pointer));
        if (pointer) {
            if (!w->Fnid)
                w->Fnid = FNID_DIALOG;
            w->Flags |= WNDF_DLGPOINTER;
        } else {
            w->Fnid |= 0x4000;
            w->Flags &= ~WNDF_DLGPOINTER;
        }
    }
    UserLeave();
    return TRUE;
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
        w->HostOwner = p == g_desktop && old_parent != g_desktop ? WndTopLevel(old_parent) : nullptr;
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
    UserEnterRead();
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
            /*
             * like xxxSetWindowStyle, WS_VISIBLE is only the bit: no show, position or activation messages. frame
             * procedures clear it around DefWindowProc to keep the caption from being drawn.
             */
            BOOLEAN shown = !(w->Style & WS_VISIBLE) && (ss.styleNew & WS_VISIBLE);
            w->Style = ss.styleNew;
            /* restoring the application frame ends its fullscreen host space. */
            if ((w->Flags & WNDF_APP_FULLSCREEN) &&
                ((w->Style & WS_CAPTION) == WS_CAPTION || (w->Style & WS_THICKFRAME)))
                w->Flags &= ~(WNDF_FULLSCREEN | WNDF_APP_FULLSCREEN);
            /* a top level window shown this way is composed as one shown by ShowWindow. */
            if (shown && WndIsTopLevel(w) && WndIsVisible(w) && !(w->Style & WS_MINIMIZE))
                WndEnsureSurfaceShown(w);
            border_taken(w);
        } else {
            w->ExStyle = (ss.styleNew & 0x7FFFFFFFu) | (w->ExStyle & 0x80000000u);
            LayerStyleChanged(w);
            border_taken(w);
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
        WndExtraChanged(w, (ULONG)index, (ULONG)size);
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

/*
 * as xxxSetWindowWord of win32kfull: a word of the extra bytes, or the low words of the user data and of the style
 * with no message. any other index is ERROR_INVALID_INDEX.
 */
WORD NTAPI NtUserSetWindowWord(HWND hwnd, int index, WORD value)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    WORD old = 0;
    if (can_touch(w)) {
        if (index == GWLP_USERDATA) {
            old = (WORD)w->UserData;
            w->UserData = (w->UserData & ~(LONG_PTR)0xFFFF) | value;
        } else if (index == GWL_STYLE) {
            old = (WORD)w->Style;
            w->Style = (w->Style & 0xFFFF0000u) | value;
        } else if (index >= 0 && w->Extra && (ULONG)index + sizeof(WORD) <= w->ExtraSize) {
            memcpy(&old, w->Extra + index, sizeof(WORD));
            memcpy(w->Extra + index, &value, sizeof(WORD));
            WndExtraChanged(w, (ULONG)index, sizeof(WORD));
        } else {
            W32SetLastError(ERROR_INVALID_INDEX);
        }
    } else if (!w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    }
    UserLeave();
    return old;
}

BOOL NTAPI NtUserSetWindowContextHelpId(HWND hwnd, DWORD id)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (w)
        w->ContextHelpId = id;
    else
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    UserLeave();
    return w != nullptr;
}

DWORD NTAPI NtUserGetWindowContextHelpId(HWND hwnd)
{
    UserEnterRead();
    WND *w = WndLookup(hwnd);
    DWORD id = w ? w->ContextHelpId : 0;
    if (!w)
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    UserLeave();
    return id;
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
    if (!atom)
        atom = ClassIntegerAtom(name);
    if (atom)
        return w->Class->Atom == atom || w->Class->PlainAtom == atom;
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
    BOOLEAN message = parent_h == HWND_MESSAGE;
    if (message)
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
            /* the message windows are found under HWND_MESSAGE alone, and nothing else is found there. */
            if (parent == g_desktop && is_message_window(c) != message)
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

/*
 * windows' shape, with the flag that leaves out immersive windows, which mime has none of. the list is the z order
 * from the top, hidden, disabled and owned windows included, message windows left out. one entry past the windows
 * closes it and is counted: user32 refuses a count of one and walks all but the last.
 */
NTSTATUS NTAPI NtUserBuildHwndList(HDESK desktop, HWND parent_h, BOOL children, BOOL remove_immersive, ULONG thread,
                                   ULONG max, HWND *list, ULONG *count)
{
    UserEnter();
    WND *parent = parent_h ? WndLookup(parent_h) : WndDesktop();
    ULONG n = 0;
    NTSTATUS status = STATUS_SUCCESS;
    if (!parent) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        UserLeave();
        return STATUS_INVALID_HANDLE;
    }
    if (thread && !W32LookupThread((HANDLE)(ULONG_PTR)thread)) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        UserLeave();
        return STATUS_INVALID_PARAMETER;
    }
    /* walk depth first when children are requested, siblings otherwise. */
    WND *c = parent->FirstChild;
    while (c) {
        if (is_message_window(c)) {
            c = c->Next;
            continue;
        }
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
    if (NT_SUCCESS(status)) {
        HWND end = (HWND)(ULONG_PTR)1;
        if (n < max && !NT_SUCCESS(MimeCopyToUser(&list[n], &end, sizeof(end))))
            status = STATUS_ACCESS_VIOLATION;
        n++;
    }
    if (NT_SUCCESS(status) && n > max)
        status = STATUS_BUFFER_TOO_SMALL;
    MimeCopyToUser(count, &n, sizeof(n));
    UserLeave();
    (void)desktop;
    (void)remove_immersive;
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
        /* measured on windows: a handle that is no window is 1400, a disabled window is 87. */
        if (!w)
            W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        else if (w->Style & WS_DISABLED)
            W32SetLastError(ERROR_INVALID_PARAMETER);
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

/* SetForegroundWindow, which user32 26300 passes straight through. the rule is the one MIME_HWND_SETFOREGROUND had. */
BOOL NTAPI NtUserSetForegroundWindow(HWND hwnd)
{
    UserEnter();
    WND *w = hwnd ? WndLookup(hwnd) : nullptr;
    BOOL ok = FALSE;
    if (!w) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    } else if (can_touch(w)) {
        WndActivate(w, FALSE);
        ok = TRUE;
    } else {
        /* the desktop takes the foreground and no window of the caller is active after it. */
        WndDeactivate();
        ok = TRUE;
    }
    UserLeave();
    return ok;
}

/*
 * AllowSetForegroundWindow and LockSetForegroundWindow, which user32 26300 passes straight through. every process may
 * take the foreground here, so what is left is the answers windows 11 26300 gives a caller that may, measured on
 * windows: any process or one that has windows, and a lock that is taken or given once.
 */
#define ASFW_ANY ((DWORD)-1)
#define LSFW_LOCK 1
#define LSFW_UNLOCK 2

static LONG g_foreground_locked;

BOOL NTAPI NtUserAllowSetForegroundWindow(DWORD pid)
{
    if (pid == ASFW_ANY)
        return TRUE;
    PEPROCESS process = nullptr;
    /* the system process of windows has no part in the window manager; the one of mime keeps the desktop. */
    BOOL ok = pid && NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &process)) &&
              process != PsInitialSystemProcess && PsGetProcessWin32Process(process) != nullptr;
    if (process)
        ObDereferenceObject(process);
    if (!ok)
        W32SetLastError(ERROR_INVALID_PARAMETER);
    return ok;
}

BOOL NTAPI NtUserLockSetForegroundWindow(UINT code)
{
    LONG expected = code == LSFW_LOCK ? 0 : 1;
    BOOL ok = (code == LSFW_LOCK || code == LSFW_UNLOCK) &&
              __atomic_compare_exchange_n(&g_foreground_locked, &expected, code == LSFW_LOCK, FALSE, __ATOMIC_SEQ_CST,
                                          __ATOMIC_SEQ_CST);
    if (!ok)
        W32SetLastError(ERROR_ACCESS_DENIED);
    return ok;
}

/*
 * SetProcessLaunchForegroundPolicy, as win32kbase 26300 checks it. 0 keeps the rule of a program a click launched, 1
 * refuses the process the foreground and 2 grants it, and 4 added keeps the entry past the first start. the caller
 * needs PROCESS_SET_INFORMATION and the process must not have started its windows. windows keeps ten entries for the
 * start of the windows of their processes; every process may take the foreground here, so an entry only waits for
 * that start or the end of its process.
 */
#define LAUNCH_POLICY_SLOTS 10
#define SE_DEBUG_PRIVILEGE 20

static struct LAUNCH_POLICY {
    PEPROCESS Process; /* referenced. */
    ULONG Policy;
    BOOLEAN Keep;
} g_launch_policies[LAUNCH_POLICY_SLOTS];

BOOL NTAPI NtUserSetProcessLaunchForegroundPolicy(DWORD pid, DWORD policy)
{
    DWORD base = policy & ~4u;
    if ((policy & 3) == 3 || base > 2) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PEPROCESS process = nullptr;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &process);
    if (NT_SUCCESS(status) && !MimeCurrentHasPrivilege(SE_DEBUG_PRIVILEGE)) {
        ACCESS_MASK granted;
        status = ObCheckOpenAccess(process, PROCESS_SET_INFORMATION, UserMode, &granted);
    }
    if (!NT_SUCCESS(status)) {
        if (process)
            ObDereferenceObject(process);
        W32SetLastError(status == STATUS_ACCESS_DENIED ? ERROR_ACCESS_DENIED : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    BOOL ok = PsGetProcessWin32Process(process) == nullptr;
    if (ok) {
        struct LAUNCH_POLICY *slot = nullptr;
        for (ULONG i = 0; i < LAUNCH_POLICY_SLOTS && !slot; i++)
            if (!g_launch_policies[i].Process || g_launch_policies[i].Process == process)
                slot = &g_launch_policies[i];
        /* with every entry taken windows keeps nothing and still says yes. */
        if (slot) {
            if (slot->Process)
                ObDereferenceObject(slot->Process);
            slot->Process = process;
            slot->Policy = base;
            slot->Keep = (policy & 4) != 0;
            process = nullptr;
        }
    }
    UserLeave();
    if (process)
        ObDereferenceObject(process);
    if (!ok)
        W32SetLastError(ERROR_ACCESS_DENIED);
    return ok;
}

/* a process started its windows, or ended: its entry is used up, or gone. the lock is held. */
void WndLaunchPolicyDone(PEPROCESS process, BOOLEAN exited)
{
    for (ULONG i = 0; i < LAUNCH_POLICY_SLOTS; i++) {
        struct LAUNCH_POLICY *slot = &g_launch_policies[i];
        if (slot->Process == process && (exited || !slot->Keep)) {
            ObDereferenceObject(slot->Process);
            slot->Process = nullptr;
        }
    }
}

/*
 * SetAdditionalForegroundBoostProcesses, as win32kbase 26300 checks it: a top level window of the caller names up to 32
 * processes that share its foreground boost, each opened for PROCESS_SET_INFORMATION, and any failure is a wrong
 * parameter. a new list replaces the old one and an empty list clears it, which fails when there was none. mime gives
 * no process a foreground boost, so the list is only kept.
 */
#define BOOST_MAX 32

BOOL NTAPI NtUserSetAdditionalForegroundBoostProcesses(HWND hwnd, DWORD count, HANDLE *user_handles)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    ULONG error = !w ? ERROR_INVALID_WINDOW_HANDLE
                  : !w->Thread || w->Thread->Process != W32GetProcess() || !WndIsTopLevel(w) || count > BOOST_MAX
                      ? ERROR_INVALID_PARAMETER
                      : 0;
    if (!error && w)
        UserReference(&w->Header);
    UserLeave();
    if (error) {
        W32SetLastError(error);
        return FALSE;
    }
    HANDLE handles[BOOST_MAX];
    PEPROCESS *processes = nullptr;
    ULONG taken = 0;
    /* measured on windows 11: a list that cannot be read is a wrong parameter too. */
    if (count && !NT_SUCCESS(MimeCopyFromUser(handles, user_handles, count * sizeof(HANDLE))))
        error = ERROR_INVALID_PARAMETER;
    if (!error && count && !(processes = W32Alloc(count * sizeof(PEPROCESS))))
        error = ERROR_NOT_ENOUGH_MEMORY;
    for (; !error && taken < count; taken++) {
        if (!NT_SUCCESS(ObReferenceObjectByHandle(handles[taken], PROCESS_SET_INFORMATION, PsProcessType, UserMode,
                                                  (PVOID *)&processes[taken], nullptr)))
            error = ERROR_INVALID_PARAMETER;
    }
    if (error && processes) {
        /* the one that failed was not referenced. */
        for (ULONG i = 0; i + 1 < taken; i++)
            ObDereferenceObject(processes[i]);
        W32Free(processes);
        processes = nullptr;
    }
    UserEnter();
    if (!error && !count && !w->BoostCount)
        error = ERROR_INVALID_PARAMETER;
    if (!error) {
        release_boost(w);
        /* a process named twice counts once. */
        ULONG kept = 0;
        for (ULONG i = 0; i < count; i++) {
            BOOLEAN seen = FALSE;
            for (ULONG k = 0; k < kept && !seen; k++)
                seen = processes[k] == processes[i];
            if (seen)
                ObDereferenceObject(processes[i]);
            else
                processes[kept++] = processes[i];
        }
        w->BoostProcesses = kept ? processes : nullptr;
        w->BoostCount = kept;
        if (!kept)
            W32Free(processes);
        processes = nullptr;
    }
    UserDereference(&w->Header);
    UserLeave();
    if (processes) {
        for (ULONG i = 0; i < count; i++)
            ObDereferenceObject(processes[i]);
        W32Free(processes);
    }
    if (error)
        W32SetLastError(error);
    return !error;
}

/*
 * SetWindowDisplayAffinity and GetWindowDisplayAffinity, as win32kfull 26300 checks them. a top level window of the
 * caller's process takes 0 or a value with WDA_MONITOR or the bit of WDA_EXCLUDEFROMCAPTURE, and keeps it whole.
 * windows also needs the window composed, which every top level window is here. any process may read the value of a
 * top level window.
 */
#define WDA_BITS 0x11

BOOL NTAPI NtUserSetWindowDisplayAffinity(HWND hwnd, DWORD affinity)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    ULONG error = !w ? ERROR_INVALID_WINDOW_HANDLE
                  : !w->Thread || w->Thread->Process != W32GetProcess() ? ERROR_ACCESS_DENIED
                  : !WndIsTopLevel(w) || (affinity && !(affinity & WDA_BITS)) ? ERROR_INVALID_PARAMETER
                                                                               : 0;
    if (!error && (w->DisplayAffinity & WDA_BITS) != (affinity & WDA_BITS))
        ScreenSerial++;
    if (!error)
        w->DisplayAffinity = affinity;
    UserLeave();
    if (error)
        W32SetLastError(error);
    return !error;
}

BOOL NTAPI NtUserGetWindowDisplayAffinity(HWND hwnd, DWORD *user_affinity)
{
    UserEnterRead();
    WND *w = WndLookup(hwnd);
    ULONG error = !w ? ERROR_INVALID_WINDOW_HANDLE : !WndIsTopLevel(w) ? ERROR_INVALID_PARAMETER : 0;
    DWORD affinity = error ? 0 : w->DisplayAffinity;
    UserLeave();
    if (!error && !NT_SUCCESS(MimeCopyToUser(user_affinity, &affinity, sizeof(affinity))))
        error = ERROR_NOACCESS;
    if (error)
        W32SetLastError(error);
    return !error;
}

/*
 * ChangeWindowMessageFilterEx, as win32kfull 26300 checks it. a window of the caller's process lets a message in from
 * a process of lower integrity, stops letting it in, or forgets every message it let in. a caller of low integrity may
 * not. the answer's status tells what the window already did, and when a message is refused but still comes in by a
 * rule of the system. mime does not hold messages back between integrities, so the list is only kept.
 */
#define SECURITY_MANDATORY_LOW_RID 0x1000

/* the messages every process may send across integrities, IsMessageAlwaysAllowedAcrossIL of win32kfull 26300. */
static BOOLEAN always_allowed(UINT msg)
{
    static const USHORT list[] = {WM_NULL, WM_MOVE, WM_SIZE, WM_GETTEXT, WM_GETTEXTLENGTH, WM_GETHOTKEY, WM_GETICON,
                                  0x305, 0x308, 0x30D, 0x313, 0x31A, 0x31B, 0x31F, 0x309, 0x30A, 0x30B, 0x30C, 0x30E};
    for (ULONG i = 0; i < RTL_NUMBER_OF(list); i++)
        if (list[i] == msg)
            return TRUE;
    return FALSE;
}

/* where the message is in the list of the window, or where it goes. */
static ULONG filter_find(const WND *w, USHORT msg, BOOLEAN *found)
{
    ULONG low = 0, high = w->MessageFilterCount;
    while (low < high) {
        ULONG mid = (low + high) / 2;
        if (w->MessageFilter[mid] < msg)
            low = mid + 1;
        else
            high = mid;
    }
    *found = low < w->MessageFilterCount && w->MessageFilter[low] == msg;
    return low;
}

BOOL NTAPI NtUserChangeWindowMessageFilterEx(HWND hwnd, UINT msg, DWORD action, CHANGEFILTERSTRUCT *user_status)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    /* measured on windows 11: a handle that is no window fails with no error. */
    if (!w) {
        UserLeave();
        return FALSE;
    }
    ULONG error = !w ? ERROR_INVALID_WINDOW_HANDLE
                  : !w->Thread || w->Thread->Process != W32GetProcess() ? ERROR_ACCESS_DENIED
                  : (w->Flags & WNDF_DESTROYING) ? ERROR_INVALID_WINDOW_HANDLE
                  : action > MSGFLT_DISALLOW || (action == MSGFLT_RESET) != (msg == 0) ? ERROR_INVALID_PARAMETER
                                                                                       : 0;
    BOOLEAN silent = FALSE;
    CHANGEFILTERSTRUCT status = {sizeof(status), MSGFLTINFO_NONE};
    if (!error && user_status) {
        if (!NT_SUCCESS(MimeCopyFromUser(&status, user_status, sizeof(status))))
            silent = TRUE;
        else if (status.cbSize != sizeof(status))
            error = ERROR_INVALID_PARAMETER;
        status.ExtStatus = MSGFLTINFO_NONE;
    }
    if (!error && !silent)
        error = SeThreadIntegrityRid() <= SECURITY_MANDATORY_LOW_RID ? ERROR_ACCESS_DENIED
                : HIWORD(msg)                                         ? ERROR_INVALID_PARAMETER
                                                                      : 0;
    if (!error && !silent && action == MSGFLT_RESET) {
        W32Free(w->MessageFilter);
        w->MessageFilter = nullptr;
        w->MessageFilterCount = w->MessageFilterCapacity = 0;
    } else if (!error && !silent) {
        BOOLEAN found;
        ULONG at = filter_find(w, (USHORT)msg, &found);
        if (action == MSGFLT_ALLOW && found) {
            status.ExtStatus = MSGFLTINFO_ALREADYALLOWED_FORWND;
        } else if (action == MSGFLT_ALLOW) {
            if (w->MessageFilterCount == w->MessageFilterCapacity) {
                ULONG capacity = w->MessageFilterCapacity ? w->MessageFilterCapacity * 2 : 8;
                USHORT *grown = W32Alloc(capacity * sizeof(USHORT));
                if (!grown) {
                    error = ERROR_OUTOFMEMORY;
                } else {
                    if (w->MessageFilterCount)
                        memcpy(grown, w->MessageFilter, w->MessageFilterCount * sizeof(USHORT));
                    W32Free(w->MessageFilter);
                    w->MessageFilter = grown;
                    w->MessageFilterCapacity = capacity;
                }
            }
            if (!error) {
                memmove(&w->MessageFilter[at + 1], &w->MessageFilter[at],
                        (w->MessageFilterCount - at) * sizeof(USHORT));
                w->MessageFilter[at] = (USHORT)msg;
                w->MessageFilterCount++;
            }
        } else {
            if (found) {
                memmove(&w->MessageFilter[at], &w->MessageFilter[at + 1],
                        (w->MessageFilterCount - at - 1) * sizeof(USHORT));
                w->MessageFilterCount--;
            }
            status.ExtStatus = always_allowed(msg) ? MSGFLTINFO_ALLOWED_HIGHER
                               : !found            ? MSGFLTINFO_ALREADYDISALLOWED_FORWND
                                                   : MSGFLTINFO_NONE;
        }
    }
    UserLeave();
    if (error) {
        W32SetLastError(error);
        return FALSE;
    }
    /* windows sets no error for a status it could not read or write, and keeps what it changed. */
    if (silent || (user_status && !NT_SUCCESS(MimeCopyToUser(user_status, &status, sizeof(status)))))
        return FALSE;
    return TRUE;
}

/* a window of the calling thread that answers HTTRANSPARENT lets the point through to what is under it. */
static BOOLEAN transparent_to(WND *w, POINT at)
{
    if (w->Thread != W32GetThread())
        return FALSE;
    UserReference(&w->Header);
    LONG hit = (LONG)MsgSend(w, WM_NCHITTEST, 0, MAKELPARAM(at.x, at.y));
    BOOLEAN gone = w->Header.Destroyed;
    UserDereference(&w->Header);
    return gone || hit == HTTRANSPARENT;
}

/*
 * WindowFromPoint, which user32 26300 passes straight through: the point comes by value, both halves in the first
 * argument of a 64-bit caller. the answer is the deepest visible window there, in the units of the caller. as
 * xxxDCEWindowHitTestIndirect does, the windows of the calling thread are asked with WM_NCHITTEST, and one that answers
 * HTTRANSPARENT passes the point to the sibling under it, or to its parent, top level windows included. measured on
 * windows.
 */
HWND NTAPI NtUserWindowFromPoint(ULONG_PTR point, ULONG_PTR second)
{
#ifdef _WIN64
    (void)second;
    POINT pt = {(LONG)(ULONG)point, (LONG)(ULONG)(point >> 32)};
#else
    POINT pt = {(LONG)point, (LONG)second};
#endif
    UserEnter();
    HWND found = nullptr;
    DpiPixelConvert(&pt, DpiCurrentAwareness(), DPI_PHYSICAL);
    WND *desktop = WndDesktop();
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c && !found; c = c->Next) {
        POINT at = pt;
        DpiPixelConvert(&at, DPI_PHYSICAL, c->Awareness);
        if (!(c->Style & WS_VISIBLE) || (c->Style & WS_MINIMIZE) || (c->ExStyle & WS_EX_TRANSPARENT) ||
            (c->ExStyle & 0x80000000u) || !RectContains(&c->WindowRect, at.x, at.y) || !LayerHitTest(c, at))
            continue;
        WND *w = WndFromPoint(c, at);
        while (w && transparent_to(w, at)) {
            if (w == c || w->Header.Destroyed) {
                w = nullptr;
                break;
            }
            WND *below = nullptr;
            for (WND *s = w->Next; s && !below; s = s->Next)
                if ((s->Style & WS_VISIBLE) && RectContains(&s->WindowRect, at.x, at.y))
                    below = s;
            w = below ? WndFromPoint(below, at) : w->Parent;
        }
        if (w)
            found = WndHandle(w);
    }
    UserLeave();
    return found;
}

/* HasCaptionIcon of win32kfull 26300: any caption but a tool window's, a dialog frame only with an icon of its own. */
static BOOLEAN caption_icon(const WND *w)
{
    if (w->ExStyle & WS_EX_TOOLWINDOW)
        return FALSE;
    if ((w->Style & WS_CAPTION) != WS_DLGFRAME && !(w->ExStyle & WS_EX_DLGMODALFRAME))
        return TRUE;
    return w->Icon || w->IconSmall || (w->Class && (w->Class->Icon || w->Class->IconSmall));
}

/*
 * GetTitleBarInfo, as NtUserGetTitleBarInfo and xxxCommonGetTitleBarInfo of win32kfull 26300: the 44 bytes of
 * TITLEBARINFO, ERROR_INVALID_PARAMETER for another size. the title bar is the window less its border, one caption
 * high, after the icon, in the units of the caller. a button the style has not is invisible, or unavailable when the
 * other one of minimize and maximize is there. measured on windows.
 */
BOOL NTAPI NtUserGetTitleBarInfo(HWND hwnd, TITLEBARINFO *user)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    TITLEBARINFO info = {};
    BOOL ok = FALSE;
    if (!w)
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    else if (!NT_SUCCESS(MimeCopyFromUser(&info.cbSize, user, sizeof(info.cbSize))))
        ;
    else if (info.cbSize != sizeof(TITLEBARINFO))
        W32SetLastError(ERROR_INVALID_PARAMETER);
    else {
        DWORD style = w->Style;
        info.rgstate[0] = STATE_SYSTEM_FOCUSABLE;
        if ((style & WS_CAPTION) != WS_CAPTION) {
            info.rgstate[0] |= STATE_SYSTEM_INVISIBLE;
        } else {
            FRAME_METRICS m;
            FrameGetMetrics(style, w->ExStyle, WndFrameDpi(w), &m);
            RECT *r = &info.rcTitleBar;
            *r = w->WindowRect;
            r->left += m.Border;
            r->top += m.Border;
            r->right -= m.Border;
            r->bottom = r->top + m.Caption;
            if ((style & WS_SYSMENU) && caption_icon(w)) {
                if (w->ExStyle & WS_EX_LAYOUTRTL)
                    r->right -= r->bottom - r->top - 1;
                else
                    r->left += r->bottom - r->top - 1;
            }
            rect_to_caller(w, r);
            BOOLEAN menu = (style & WS_SYSMENU) != 0, min = (style & WS_MINIMIZEBOX) != 0, max = (style & WS_MAXIMIZEBOX) != 0;
            info.rgstate[1] = STATE_SYSTEM_INVISIBLE;
            info.rgstate[2] = !menu ? STATE_SYSTEM_INVISIBLE : min ? 0 : max ? STATE_SYSTEM_UNAVAILABLE : STATE_SYSTEM_INVISIBLE;
            info.rgstate[3] = !menu ? STATE_SYSTEM_INVISIBLE : max ? 0 : min ? STATE_SYSTEM_UNAVAILABLE : STATE_SYSTEM_INVISIBLE;
            info.rgstate[4] = !(w->ExStyle & WS_EX_CONTEXTHELP) || min || max ? STATE_SYSTEM_INVISIBLE : 0;
            info.rgstate[5] = !menu ? STATE_SYSTEM_INVISIBLE : (w->Flags & WNDF_NOCLOSE) ? STATE_SYSTEM_UNAVAILABLE : 0;
        }
        ok = NT_SUCCESS(MimeCopyToUser(user, &info, sizeof(info)));
    }
    UserLeave();
    return ok;
}

/*
 * SetShellWindow and SetShellWindowEx, as NtUserSetShellWindowEx and RegisterShell of win32kfull 26300 do them: a window
 * of the calling process becomes the shell window of the desktop if there is none, top level, without an owner and not
 * topmost, and goes to the bottom for good. GetShellWindow of user32 reads it from the desktop information, see
 * deskheap.c. the desktop is what the machine shows, so the shell window has no surface and no window of the host.
 * measured on windows.
 */
BOOL NTAPI NtUserSetShellWindowEx(HWND hwnd, HWND list_view)
{
    (void)list_view;
    UserEnter();
    WND *w = WndLookup(hwnd);
    W32THREAD *t = W32GetThread();
    ULONG error = 0;
    if (!w)
        error = ERROR_INVALID_WINDOW_HANDLE;
    else if (w == g_desktop || is_message_window(w) || !t || !w->Thread || w->Thread->Process != t->Process)
        error = ERROR_ACCESS_DENIED;
    else if (WndShell && alive(WndShell))
        error = ERROR_ACCESS_DENIED;
    else if (!WndIsTopLevel(w) || w->Owner || (w->ExStyle & WS_EX_TOPMOST))
        error = ERROR_INVALID_PARAMETER;
    if (!error) {
        WndShell = w;
        if (w->Flags & WNDF_HOST_WINDOW) {
            HostWindowDestroy(w);
            w->Flags &= ~WNDF_HOST_WINDOW;
        }
        if (w->Surface) {
            PaintRetireSurface(w->Surface);
            w->Surface = nullptr;
        }
        WndSetPos(w, (WND *)HWND_BOTTOM, 0, 0, 0, 0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
        DeskTouch();
    }
    UserLeave();
    if (error)
        W32SetLastError(error);
    return !error;
}

HWND NTAPI NtUserSetActiveWindow(HWND hwnd)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    HWND old = t ? t->Active : nullptr;
    WND *w = hwnd ? WndLookup(hwnd) : nullptr;
    /*
     * measured on windows: a null window does nothing and answers null, a handle that is no window is 1400 and null,
     * a child changes nothing and answers the active window, and a hidden or a disabled window is activated.
     */
    if (!w) {
        if (hwnd)
            W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        old = nullptr;
    } else if (can_touch(w) && WndIsTopLevel(w) && w->Thread == t) {
        WndActivate(w, FALSE);
    }
    UserLeave();
    return old;
}

/*
 * what EndDialog calls once an ownerless dialog is hidden, as win32kfull 26300 does it: the input of the caller, and so
 * every thread attached to it, has no active window and no focus any more, and no window is told.
 */
BOOL NTAPI NtUserZapActiveAndFocus(void)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    if (t) {
        W32THREAD *owner = W32InputOwner(t);
        for (W32THREAD *x = W32NextThread(nullptr); x; x = W32NextThread(x)) {
            if (W32InputOwner(x) == owner) {
                x->Active = nullptr;
                x->Focus = nullptr;
            }
        }
        if (InputFocusOwner && W32InputOwner(InputFocusOwner) == owner)
            InputFocusOwner = nullptr;
        InputKeysChanged();
        ImeFocusChanged();
    }
    UserLeave();
    return TRUE;
}

HWND NTAPI NtUserGetForegroundWindow(void)
{
    UserEnterRead();
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
    /* the queries user32 answers after validating the handle, which is what sets the error. */
    if (!w && (routine == MIME_HWND_GETPARENT || routine == MIME_HWND_ISVISIBLE || routine == MIME_HWND_ISENABLED ||
               routine == MIME_HWND_SETFOREGROUND))
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    if (w) {
        switch (routine) {
        case MIME_HWND_GETPARENT:
            /* GetParent: the parent of a child, the owner of a popup. */
            /* a window with both styles counts as a popup, as user32 reads the two bits. */
            if ((w->Style & (WS_CHILD | WS_POPUP)) == WS_CHILD)
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
            } else {
                /* the desktop takes the foreground and no window of the caller is active after it. */
                WndDeactivate();
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
        case MIME_HWND_PAINTDISPATCHED:
            MsgPaintDispatched(w);
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

/*
 * GetWindow as user32 walks it: the desktop answers GW_CHILD alone, the siblings are the z order of the parent from
 * the top, and nothing is skipped for being hidden, disabled or owned. a command past the known ones is an error.
 */
static WND *get_window_rel(WND *w, UINT cmd)
{
    if (w == g_desktop && cmd != GW_CHILD)
        return nullptr;
    WND *c;
    switch (cmd) {
    case GW_HWNDFIRST:
        for (c = w->Parent ? w->Parent->FirstChild : nullptr; c && !same_list(c, w); c = c->Next)
            ;
        return c;
    case GW_HWNDLAST:
        for (c = w->Parent ? w->Parent->LastChild : nullptr; c && !same_list(c, w); c = c->Prev)
            ;
        return c;
    case GW_HWNDNEXT:
        for (c = w->Next; c && !same_list(c, w); c = c->Next)
            ;
        return c;
    case GW_HWNDPREV:
        for (c = w->Prev; c && !same_list(c, w); c = c->Prev)
            ;
        return c;
    case GW_OWNER:
        return w->Owner;
    case GW_CHILD:
        return w == g_desktop ? first_top_level() : w->FirstChild;
    default:
        if (cmd != GW_ENABLEDPOPUP) {
            W32SetLastError(1443); /* ERROR_INVALID_GW_COMMAND */
            return nullptr;
        }
        break;
    }
    switch (cmd) {
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

/*
 * like win32kfull's NtUserQueryWindow: what the classes of WINDOWINFOCLASS ask of a window's owner. the process and
 * thread are the creating thread's, the active and focus windows its queue's, and the default ime window and input
 * context are none here. any other class, or a window that does not exist, answers zero.
 */
ULONG_PTR NTAPI NtUserQueryWindow(HWND hwnd, ULONG klass)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    ULONG_PTR r = 0;
    W32THREAD *t = w ? w->Thread : nullptr;
    if (w == g_desktop) {
        /* the window of the desktop is the subsystem process's, csrss on windows. */
        HANDLE pid, tid;
        PsQuerySubsystemIds(&pid, &tid);
        if (klass == 0 || klass == 1)
            r = (ULONG_PTR)pid;
        else if (klass == 2)
            r = (ULONG_PTR)tid;
    } else if (t) {
        switch (klass) {
        case 0: /* WindowProcess */
        case 1: /* WindowRealProcess */
            r = (ULONG_PTR)t->Process->Pid;
            break;
        case 2: /* WindowThread */
            r = (ULONG_PTR)t->Tid;
            break;
        case 3: /* WindowActiveWindow */
            r = (ULONG_PTR)t->Active;
            break;
        case 4: /* WindowFocusWindow */
            r = (ULONG_PTR)t->Focus;
            break;
        case 5: /* WindowIsHung */
            r = MsgThreadHung(t);
            break;
        case 7: /* WindowIsForegroundThread */
            r = InputForeground && InputForeground->Thread == t;
            break;
        default:
            break;
        }
    }
    UserLeave();
    return r;
}

ULONG_PTR NTAPI NtUserCallHwndParam(HWND hwnd, ULONG_PTR param, ULONG routine)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    ULONG_PTR r = 0;
    if (!w && !((routine == MIME_HWNDPARAM_MAPPOINTS || routine == MIME_HWNDPARAM_DPICONVERT) && !hwnd)) {
        if (routine == MIME_HWNDPARAM_GETWINDOW)
            W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        goto out;
    }
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
    case MIME_HWNDPARAM_DIRECTPROC: {
        MIME_DIRECT_PROC direct;
        WNDPROC proc;
        BOOLEAN ansi;
        if (!NT_SUCCESS(MimeCopyFromUser(&direct, (PVOID)param, sizeof(direct))) ||
            !MsgDirectProc(w, direct.Msg, (direct.Flags & MIME_DIRECT_SEND) != 0, &proc, &ansi))
            break;
        direct.Proc = (ULONG64)(ULONG_PTR)proc;
        direct.Flags = ansi ? MIME_DIRECT_ANSI : 0;
        r = NT_SUCCESS(MimeCopyToUser((PVOID)param, &direct, sizeof(direct)));
        break;
    }
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
        if (w == g_desktop) {
            HANDLE owner, tid;
            PsQuerySubsystemIds(&owner, &tid);
            DWORD pid = (DWORD)(ULONG_PTR)owner;
            if (param)
                MimeCopyToUser((PVOID)param, &pid, sizeof(pid));
            r = (ULONG_PTR)tid;
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
        w->Flags |= WNDF_PRESENTS;
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
    if (!w)
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    /* the flags are only given for a window that takes touches. */
    if (user_flags && (flags & MIME_TOUCH_REGISTERED)) {
        ULONG out = flags & ~MIME_TOUCH_REGISTERED;
        MimeCopyToUser(user_flags, &out, sizeof(out));
    }
    return (flags & MIME_TOUCH_REGISTERED) != 0;
}

/* RegisterTouchWindow and UnregisterTouchWindow of user32, on a window of the calling thread. */
BOOL NTAPI NtUserModifyWindowTouchCapability(HWND hwnd, ULONG flags, BOOL enable)
{
    BOOL ok = FALSE;
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (!w)
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    else if (flags & ~(TWF_FINETOUCH | TWF_WANTPALM))
        W32SetLastError(ERROR_INVALID_FLAGS);
    else if (w->Thread != W32GetThread())
        W32SetLastError(ERROR_ACCESS_DENIED);
    else {
        w->TouchFlags = enable ? flags | MIME_TOUCH_REGISTERED : 0;
        ok = TRUE;
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtUserShowOwnedPopups(HWND hwnd, BOOL show)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    if (w)
        WndShowOwned(w, show ? SW_PARENTOPENING : SW_PARENTCLOSING);
    else
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    UserLeave();
    return w != nullptr;
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
