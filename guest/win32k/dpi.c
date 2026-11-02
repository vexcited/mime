/*
 * DPI awareness. a process has an awareness and a thread can use another one, a window keeps the awareness it was created
 * with. an unaware window works in a screen of 96 dpi pixels that the host scales up to the system dpi, so its sizes,
 * metrics and fonts are the ones of a 96 dpi display. every other window works in physical pixels.
 */
#include "win32k.h"

ULONG DpiThreadAwareness(const W32THREAD *t)
{
    if (!t)
        return DPI_AWARE_UNAWARE;
    ULONG awareness = t->DpiOverride ? t->DpiOverride - 1 : t->Process->DpiAwareness;
    if (awareness == DPI_AWARE_GDISCALED)
        awareness = DPI_AWARE_UNAWARE;
    /* a host that cannot stretch windows shows every program at the display's dpi. */
    return awareness == DPI_AWARE_UNAWARE && !W32Display.Stretch ? DPI_AWARE_SYSTEM : awareness;
}

ULONG DpiCurrentAwareness(void)
{
    return DpiThreadAwareness(W32GetThread());
}

/* the dpi of the pixels a window of this awareness works in. */
UINT DpiScaleOf(ULONG awareness)
{
    return awareness == DPI_AWARE_UNAWARE ? 96 : W32SystemDpi();
}

LONG DpiScale(LONG value, UINT from, UINT to)
{
    return from == to ? value : W32MulDiv(value, (LONG)to, (LONG)from);
}

/*
 * the virtual screen of a program that works at a dpi. every monitor keeps its place next to the one it touches: its
 * size scales by the dpi over its own and the offset along the shared edge by the dpi over the one of the monitor it
 * touches, which is how the layout of CMonitorTopology in win32kbase gives the rectangles a monitor has for a dpi
 * (GetMonitorRectForDpi: the origin by one dpi, the size by the monitor's own). for the unaware dpi this is the
 * layout of the compositor in its logical units.
 */
typedef struct TOPOLOGY {
    ULONG Count;
    RECT Physical[MIME_USER_MAX_MONITORS];
    RECT Virtual[MIME_USER_MAX_MONITORS];
    UINT Dpi[MIME_USER_MAX_MONITORS];
} TOPOLOGY;

static BOOLEAN spans(LONG a0, LONG a1, LONG b0, LONG b1)
{
    return a0 < b1 && b0 < a1;
}

static void topology_build(UINT d, TOPOLOGY *t);

/* the layouts are kept for the few dpis programs work at, until the monitors change. the primary monitor follows the
   display mode, so its size is part of the key. */
static void topology(UINT d, TOPOLOGY *t)
{
    static struct {
        UINT Dpi;
        ULONG Generation;
        LONG Width, Height;
        TOPOLOGY Layout;
    } cache[4];
    static ULONG next;
    ULONG generation = MonitorGeneration();
    for (ULONG i = 0; i < RTL_NUMBER_OF(cache); i++)
        if (cache[i].Dpi == d && cache[i].Generation == generation && cache[i].Width == W32Display.Width &&
            cache[i].Height == W32Display.Height) {
            *t = cache[i].Layout;
            return;
        }
    topology_build(d, t);
    cache[next].Dpi = d;
    cache[next].Generation = generation;
    cache[next].Width = W32Display.Width;
    cache[next].Height = W32Display.Height;
    cache[next].Layout = *t;
    next = (next + 1) % RTL_NUMBER_OF(cache);
}

static void topology_build(UINT d, TOPOLOGY *t)
{
    MIME_MONITOR_INFO m;
    BOOLEAN placed[MIME_USER_MAX_MONITORS] = {};
    t->Count = 0;
    for (ULONG i = 0; i < MIME_USER_MAX_MONITORS && MonitorGet(i, &m); i++) {
        t->Physical[i] = m.Rect;
        t->Dpi[i] = m.Dpi ? m.Dpi : 96;
        t->Count++;
    }
    for (ULONG i = 0; i < t->Count; i++) {
        const RECT *p = &t->Physical[i];
        LONG w = DpiScale(p->right - p->left, t->Dpi[i], d), h = DpiScale(p->bottom - p->top, t->Dpi[i], d);
        t->Virtual[i] = (RECT){0, 0, w, h};
    }
    if (!t->Count)
        return;
    t->Virtual[0] = (RECT){0, 0, t->Virtual[0].right, t->Virtual[0].bottom};
    RectOffset(&t->Virtual[0], DpiScale(t->Physical[0].left, t->Dpi[0], d), DpiScale(t->Physical[0].top, t->Dpi[0], d));
    placed[0] = TRUE;
    for (BOOLEAN progress = TRUE; progress;) {
        progress = FALSE;
        for (ULONG i = 1; i < t->Count; i++) {
            if (placed[i])
                continue;
            const RECT *m_ = &t->Physical[i];
            LONG w = t->Virtual[i].right, h = t->Virtual[i].bottom, x, y;
            for (ULONG j = 0; j < t->Count && !placed[i]; j++) {
                if (!placed[j])
                    continue;
                const RECT *r = &t->Physical[j], *v = &t->Virtual[j];
                BOOLEAN vertical = spans(m_->top, m_->bottom, r->top, r->bottom);
                BOOLEAN horizontal = spans(m_->left, m_->right, r->left, r->right);
                if (m_->left == r->right && vertical) {
                    x = v->right;
                    y = v->top + DpiScale(m_->top - r->top, t->Dpi[j], d);
                } else if (m_->right == r->left && vertical) {
                    x = v->left - w;
                    y = v->top + DpiScale(m_->top - r->top, t->Dpi[j], d);
                } else if (m_->top == r->bottom && horizontal) {
                    y = v->bottom;
                    x = v->left + DpiScale(m_->left - r->left, t->Dpi[j], d);
                } else if (m_->bottom == r->top && horizontal) {
                    y = v->top - h;
                    x = v->left + DpiScale(m_->left - r->left, t->Dpi[j], d);
                } else {
                    continue;
                }
                t->Virtual[i] = (RECT){x, y, x + w, y + h};
                placed[i] = progress = TRUE;
            }
        }
    }
    /* a monitor that touches none keeps its corner at its own scale. */
    for (ULONG i = 1; i < t->Count; i++)
        if (!placed[i]) {
            LONG w = t->Virtual[i].right, h = t->Virtual[i].bottom;
            LONG x = DpiScale(t->Physical[i].left, t->Dpi[i], d), y = DpiScale(t->Physical[i].top, t->Dpi[i], d);
            t->Virtual[i] = (RECT){x, y, x + w, y + h};
        }
}

static LONG64 overlap_area(const RECT *a, const RECT *b)
{
    LONG w = W32Min(a->right, b->right) - W32Max(a->left, b->left);
    LONG h = W32Min(a->bottom, b->bottom) - W32Max(a->top, b->top);
    return w > 0 && h > 0 ? (LONG64)w * h : 0;
}

static LONG64 gap_distance(const RECT *r, const RECT *m)
{
    LONG dx = r->right <= m->left ? m->left - r->right : r->left >= m->right ? r->left - m->right + 1 : 0;
    LONG dy = r->bottom <= m->top ? m->top - r->bottom : r->top >= m->bottom ? r->top - m->bottom + 1 : 0;
    return (LONG64)dx * dx + (LONG64)dy * dy;
}

/* the monitor a rectangle belongs to: the largest overlap, a point in one, else the nearest. */
static ULONG pick(const RECT *monitors, ULONG count, const RECT *r)
{
    ULONG best = 0;
    LONG64 best_area = 0;
    for (ULONG i = 0; i < count; i++) {
        LONG64 area = overlap_area(r, &monitors[i]);
        if (r->left == r->right && r->top == r->bottom && r->left >= monitors[i].left && r->left < monitors[i].right &&
            r->top >= monitors[i].top && r->top < monitors[i].bottom)
            area = 1;
        if (area > best_area) {
            best_area = area;
            best = i;
        }
    }
    if (best_area)
        return best;
    LONG64 nearest = -1;
    for (ULONG i = 0; i < count; i++) {
        LONG64 d = gap_distance(r, &monitors[i]);
        if (nearest < 0 || d < nearest) {
            nearest = d;
            best = i;
        }
    }
    return best;
}

/* the pixel of the other grid, rounding down, which is how a pointer position becomes a position in coarser pixels. */
static LONG floor_scale(LONG value, UINT from, UINT to)
{
    LONG64 v = (LONG64)value * to;
    LONG64 q = v / (LONG64)from;
    return (LONG)(v % (LONG64)from < 0 ? q - 1 : q);
}

/* from the screen of a program at a dpi to the physical one, or back. only the monitor a rectangle is on matters. */
static void screen_convert(RECT *r, UINT d, BOOLEAN to_physical, BOOLEAN floor)
{
    TOPOLOGY t;
    topology(d, &t);
    if (!t.Count)
        return;
    ULONG i = pick(to_physical ? t.Virtual : t.Physical, t.Count, r);
    const RECT *from = to_physical ? &t.Virtual[i] : &t.Physical[i], *to = to_physical ? &t.Physical[i] : &t.Virtual[i];
    UINT num = to_physical ? t.Dpi[i] : d, den = to_physical ? d : t.Dpi[i];
    LONG width = r->right - r->left, height = r->bottom - r->top;
    LONG x = floor ? floor_scale(r->left - from->left, den, num) : DpiScale(r->left - from->left, den, num);
    LONG y = floor ? floor_scale(r->top - from->top, den, num) : DpiScale(r->top - from->top, den, num);
    r->left = to->left + x;
    r->top = to->top + y;
    r->right = r->left + DpiScale(width, den, num);
    r->bottom = r->top + DpiScale(height, den, num);
}

/* the dpi of the coordinate space of an awareness, 0 for the physical one a per monitor program works in. */
static UINT space_dpi(ULONG awareness)
{
    return awareness == DPI_AWARE_UNAWARE || awareness == DPI_AWARE_GDISCALED ? 96
           : awareness == DPI_AWARE_SYSTEM                                    ? W32SystemDpi()
                                                                              : 0;
}

/* whether the screen of this awareness differs from the physical one, which it does when a monitor has another dpi. */
BOOLEAN DpiVirtual(ULONG awareness)
{
    UINT d = space_dpi(awareness);
    if (!d)
        return FALSE;
    TOPOLOGY t;
    topology(d, &t);
    for (ULONG i = 0; i < t.Count; i++)
        if (t.Dpi[i] != d)
            return TRUE;
    return FALSE;
}

static void convert(RECT *r, ULONG from, ULONG to, BOOLEAN floor)
{
    UINT a = space_dpi(from), b = space_dpi(to);
    if (a == b)
        return;
    if (a)
        screen_convert(r, a, TRUE, floor);
    if (b)
        screen_convert(r, b, FALSE, floor);
}

/* a screen rectangle between awarenesses, with its size scaled by the dpi of the monitor it is on. */
void DpiRectConvert(RECT *r, ULONG from, ULONG to)
{
    convert(r, from, to, FALSE);
}

void DpiPointConvert(POINT *p, ULONG from, ULONG to)
{
    RECT r = {p->x, p->y, p->x, p->y};
    convert(&r, from, to, FALSE);
    *p = (POINT){r.left, r.top};
}

void DpiPixelConvert(POINT *p, ULONG from, ULONG to)
{
    RECT r = {p->x, p->y, p->x, p->y};
    convert(&r, from, to, TRUE);
    *p = (POINT){r.left, r.top};
}

/* a distance between awarenesses at the scale of the monitor it is measured on. */
LONG DpiLength(LONG value, ULONG from, ULONG to, UINT monitor_dpi)
{
    UINT a = space_dpi(from) ? space_dpi(from) : monitor_dpi, b = space_dpi(to) ? space_dpi(to) : monitor_dpi;
    return DpiScale(value, a, b);
}

/*
 * the pixels of one row of a screen, count of them from x, in the screen of another awareness, each rounded down as a
 * pointer position is. a row crosses few monitors, so the one the pixel before was on is tried first.
 */
void DpiConvertRow(ULONG from, ULONG to, LONG y, LONG x, ULONG count, POINT *out)
{
    UINT a = space_dpi(from), b = space_dpi(to);
    if (a == b || (a && b)) {
        for (ULONG i = 0; i < count; i++) {
            out[i] = (POINT){x + (LONG)i, y};
            if (a != b)
                DpiPixelConvert(&out[i], from, to);
        }
        return;
    }
    TOPOLOGY t;
    topology(a ? a : b, &t);
    const RECT *source = a ? t.Virtual : t.Physical;
    ULONG m = 0;
    for (ULONG i = 0; i < count; i++) {
        POINT p = {x + (LONG)i, y};
        if (p.x < source[m].left || p.x >= source[m].right || p.y < source[m].top || p.y >= source[m].bottom)
            m = pick(source, t.Count, &(RECT){p.x, p.y, p.x, p.y});
        UINT monitor_dpi = t.Dpi[m];
        if (a) {
            p.x = t.Physical[m].left + floor_scale(p.x - t.Virtual[m].left, a, monitor_dpi);
            p.y = t.Physical[m].top + floor_scale(p.y - t.Virtual[m].top, a, monitor_dpi);
        } else {
            p.x = t.Virtual[m].left + floor_scale(p.x - t.Physical[m].left, monitor_dpi, b);
            p.y = t.Virtual[m].top + floor_scale(p.y - t.Physical[m].top, monitor_dpi, b);
        }
        out[i] = p;
    }
}

/* the monitors of the virtual screen of an awareness, one rectangle each. */
ULONG DpiVirtualMonitors(ULONG awareness, RECT *out)
{
    UINT d = space_dpi(awareness);
    TOPOLOGY t;
    if (!d) {
        MIME_MONITOR_INFO m;
        ULONG n = 0;
        for (; n < MIME_USER_MAX_MONITORS && MonitorGet(n, &m); n++)
            out[n] = m.Rect;
        return n;
    }
    topology(d, &t);
    for (ULONG i = 0; i < t.Count; i++)
        out[i] = t.Virtual[i];
    return t.Count;
}

/* the virtual screen of an awareness. */
RECT DpiVirtualScreen(ULONG awareness)
{
    UINT d = space_dpi(awareness);
    if (!d)
        return MonitorVirtualScreen();
    TOPOLOGY t;
    topology(d, &t);
    RECT all = t.Count ? t.Virtual[0] : (RECT){0, 0, W32Display.Width, W32Display.Height};
    for (ULONG i = 1; i < t.Count; i++) {
        all.left = W32Min(all.left, t.Virtual[i].left);
        all.top = W32Min(all.top, t.Virtual[i].top);
        all.right = W32Max(all.right, t.Virtual[i].right);
        all.bottom = W32Max(all.bottom, t.Virtual[i].bottom);
    }
    return all;
}

/* the work area of the primary monitor in the pixels of an awareness. */
RECT DpiWorkArea(ULONG awareness)
{
    RECT work = W32Display.Work;
    DpiRectConvert(&work, DPI_PHYSICAL, awareness);
    return work;
}

/* the awareness changes for every window of the process and its coordinates follow. */
static void convert_tree(WND *w, ULONG from, ULONG to, UINT dpi)
{
    DpiRectConvert(&w->WindowRect, from, to);
    DpiRectConvert(&w->ClientRect, from, to);
    DpiRectConvert(&w->NormalRect, from, to);
    DpiRectConvert(&w->FullscreenRect, from, to);
    DpiRectConvert(&w->FullscreenNormalRect, from, to);
    w->MinTrack = (POINT){DpiScale(w->MinTrack.x, DpiScaleOf(from), DpiScaleOf(to)),
                          DpiScale(w->MinTrack.y, DpiScaleOf(from), DpiScaleOf(to))};
    w->MaxTrack = (POINT){DpiScale(w->MaxTrack.x, DpiScaleOf(from), DpiScaleOf(to)),
                          DpiScale(w->MaxTrack.y, DpiScaleOf(from), DpiScaleOf(to))};
    RgnClear(&w->Update);
    w->Awareness = (UCHAR)to;
    w->Dpi = w->MonitorDpi = dpi;
    for (WND *c = w->FirstChild; c; c = c->Next)
        convert_tree(c, from, to, dpi);
}

void DpiProcessAwarenessChanged(W32PROCESS *p, ULONG old)
{
    ULONG now = p->DpiAwareness;
    if (old == now)
        return;
    WND *desktop = WndDesktop();
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c; c = c->Next) {
        if (!c->Thread || c->Thread->Process != p || (c->Flags & WNDF_DESTROYING) || c->Awareness != old)
            continue;
        convert_tree(c, old, now, now >= DPI_AWARE_PER_MONITOR ? MonitorDpiOfRect(&c->WindowRect) : DpiScaleOf(now));
    }
    /* the windows redraw at the new size, as a frame change does. */
    HWND tops[64];
    ULONG count = 0;
    for (WND *c = desktop ? desktop->FirstChild : nullptr; c && count < RTL_NUMBER_OF(tops); c = c->Next)
        if (c->Thread && c->Thread->Process == p && !(c->Flags & WNDF_DESTROYING))
            tops[count++] = WndHandle(c);
    for (ULONG i = 0; i < count; i++) {
        WND *w = WndLookup(tops[i]);
        if (!w)
            continue;
        if (w->Thread)
            QueuePost(w->Thread, WndHandle(w), WM_DISPLAYCHANGE, W32Display.Bits,
                      MAKELPARAM((WORD)DpiScale(W32Display.Width, W32SystemDpi(), DpiScaleOf(now)),
                                 (WORD)DpiScale(W32Display.Height, W32SystemDpi(), DpiScaleOf(now))));
        WndSetPos(w, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

/* an awareness can be chosen once; the manifest forces it before anything else could. */
UINT DpiProcessSetAwareness(W32PROCESS *p, ULONG awareness, BOOLEAN force)
{
    if (awareness > DPI_AWARE_PER_MONITOR_V2)
        return 0;
    if (p->DpiSet && !force)
        return 0;
    ULONG old = p->DpiAwareness;
    p->DpiAwareness = awareness;
    p->DpiSet = TRUE;
    DpiProcessAwarenessChanged(p, old);
    return 1;
}

/* a thread context is an awareness plus one, 0 for the process' own. returns the one in effect before. */
ULONG DpiThreadSetContext(W32THREAD *t, ULONG context)
{
    ULONG old = t->DpiOverride ? t->DpiOverride - 1 : t->Process->DpiAwareness;
    t->DpiOverride = context;
    return old;
}

/* a child takes the awareness of its thread, not the one of its parent, when the parent allows mixed hosting. */
BOOLEAN DpiChildIsolated(const WND *parent, ULONG awareness)
{
    if (!parent || parent->Hosting != 1)
        return FALSE;
    return parent->Awareness >= DPI_AWARE_PER_MONITOR || awareness < DPI_AWARE_PER_MONITOR;
}

/* whether the window hears about the dpi of its tree: the top level one does, a child as its top level window allows. */
BOOLEAN DpiChildMessageEnabled(WND *w)
{
    if (w->Awareness < DPI_AWARE_PER_MONITOR)
        return FALSE;
    WND *top = WndTopLevel(w);
    return top == w || (top && (top->Flags & WNDF_DPI_CHILDREN));
}

/*
 * the dpi controls work at. an unaware window is at 96 and a system aware one at the system dpi, a per monitor window
 * follows its monitor, but a per monitor window that does not tell its children stays at the dpi of the system.
 */
UINT DpiWindowDpi(WND *w)
{
    if (w->Awareness < DPI_AWARE_PER_MONITOR)
        return DpiScaleOf(w->Awareness);
    return DpiChildMessageEnabled(w) ? WndGetDpi(w) : W32SystemDpi();
}
