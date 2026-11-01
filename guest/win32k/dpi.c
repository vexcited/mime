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

/* whether a window of this awareness is stretched by the host. */
BOOLEAN DpiVirtual(ULONG awareness)
{
    return awareness == DPI_AWARE_UNAWARE && W32SystemDpi() != 96;
}

LONG DpiScale(LONG value, UINT from, UINT to)
{
    return from == to ? value : W32MulDiv(value, (LONG)to, (LONG)from);
}

void DpiRectConvert(RECT *r, ULONG from, ULONG to)
{
    UINT a = DpiScaleOf(from), b = DpiScaleOf(to);
    if (a == b)
        return;
    r->left = DpiScale(r->left, a, b);
    r->top = DpiScale(r->top, a, b);
    r->right = DpiScale(r->right, a, b);
    r->bottom = DpiScale(r->bottom, a, b);
}

void DpiPointConvert(POINT *p, ULONG from, ULONG to)
{
    UINT a = DpiScaleOf(from), b = DpiScaleOf(to);
    p->x = DpiScale(p->x, a, b);
    p->y = DpiScale(p->y, a, b);
}

/* the pixel of the other grid a point is in, which is how a pointer position becomes a position in coarser pixels. */
static LONG floor_scale(LONG value, UINT from, UINT to)
{
    LONG64 v = (LONG64)value * to;
    LONG64 q = v / (LONG64)from;
    return (LONG)(v % (LONG64)from < 0 ? q - 1 : q);
}

void DpiPixelConvert(POINT *p, ULONG from, ULONG to)
{
    UINT a = DpiScaleOf(from), b = DpiScaleOf(to);
    if (a == b)
        return;
    p->x = floor_scale(p->x, a, b);
    p->y = floor_scale(p->y, a, b);
}

/* the work area of the primary monitor in the pixels of an awareness. */
RECT DpiWorkArea(ULONG awareness)
{
    RECT work = W32Display.Work;
    DpiRectConvert(&work, DPI_AWARE_SYSTEM, awareness);
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
    if (w->Awareness == DPI_AWARE_PER_MONITOR_V2)
        return TRUE;
    if (w->Awareness != DPI_AWARE_PER_MONITOR)
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
