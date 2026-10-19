/*
 * the screen as programs read it. windows are not drawn on a shared
 * screen here, the host shows each top level window on its own, so the
 * screen and desktop dcs draw on a surface of the virtual screen's size
 * that is composed when something reads it: the desktop's background
 * color on each monitor, black where no monitor is, then the visible top
 * level windows from the bottom of the z order up, each window's surface
 * at its place, layered and shaped ones blended through their composed
 * copy, and over a window's client area the last image presented to it
 * through vulkan, which the host keeps.
 *
 * the surface starts at the top left of the virtual screen, which is
 * negative when a monitor is left of or above the primary one, while the
 * coordinates of programs have the primary monitor's corner at 0, 0. a dc
 * of the screen has the difference as its origin, and the area ScreenCompose
 * gets is in surface pixels.
 */
#include "win32k.h"

static SURFACE *g_screen;
static POINT g_screen_origin; /* the screen coordinates of the surface's top left pixel */
static MimeWinReadback *g_readback;
static PUCHAR g_pixels;
static SIZE_T g_pixels_capacity;

/* the surface screen dcs draw on, the virtual screen's size. */
SURFACE *ScreenSurface(void)
{
    RECT all = MonitorVirtualScreen();
    LONG width = all.right - all.left, height = all.bottom - all.top;
    if (g_screen && (g_screen->Width != width || g_screen->Height != height || g_screen_origin.x != all.left ||
                     g_screen_origin.y != all.top)) {
        if (g_screen->Header.SelectCount)
            g_screen->Flags |= SURF_RETIRED;
        else
            SurfaceDestroy(g_screen);
        g_screen = nullptr;
    }
    if (!g_screen) {
        g_screen = SurfaceCreate(width, height, 32, SURF_TOPDOWN);
        g_screen_origin = (POINT){all.left, all.top};
    }
    return g_screen;
}

/* where the surface's top left pixel is on the screen. */
POINT ScreenOrigin(void)
{
    ScreenSurface();
    return g_screen_origin;
}

/* a monitor's rectangle in surface pixels, false when there is no such monitor. */
BOOLEAN ScreenMonitorRect(ULONG index, RECT *out)
{
    MIME_MONITOR_INFO m;
    POINT origin = ScreenOrigin();
    if (!MonitorGet(index, &m))
        return FALSE;
    *out = m.Rect;
    RectOffset(out, -origin.x, -origin.y);
    return TRUE;
}

/* the part of the surface that is on a monitor, which is all of it but for the holes between monitors. */
void ScreenVisibleRegion(REGION *out)
{
    RgnClear(out);
    RECT r;
    for (ULONG i = 0; ScreenMonitorRect(i, &r); i++)
        RgnCombineRect(out, out, &r, RGN_OR);
}

BOOLEAN ScreenIsSurface(const SURFACE *s) { return s && s == g_screen; }

/* src over dst, both premultiplied. */
static inline ULONG over(ULONG s, ULONG d)
{
    ULONG a = 255 - (s >> 24);
    if (a == 0)
        return s;
    if (a == 255)
        return d;
    ULONG rb = ((d & 0xFF00FF) * a + 0x800080) >> 8 & 0xFF00FF;
    ULONG g = ((d & 0xFF00) * a + 0x8000) >> 8 & 0xFF00;
    return s + rb + g;
}

/* counts the changes of the guest that show on the screen: presents, window configurations, windows made and gone. */
ULONG ScreenSerial;

/* whether a window may present through the host's vulkan: its process has a connection. */
static BOOLEAN presents_through_host(WND *w)
{
    return w->Thread && w->Thread->Process && w->Thread->Process->Vk;
}

/*
 * the window's last vulkan image into g_pixels, its size, the client
 * rectangle it showed in, relative to the window, and the host's count of
 * the images it presented; false when it has none. the pixels are read
 * only when pixels is set.
 */
static BOOLEAN read_presented(WND *w, BOOLEAN pixels, ULONG *width, ULONG *height, RECT *client, ULONG64 *serial)
{
    if (!g_readback && !(g_readback = MmAllocateContiguousPages(4096)))
        return FALSE;
    for (int attempt = 0; attempt < 2; attempt++) {
        MimeWinReadback *r = g_readback;
        memset(r, 0, sizeof(*r));
        r->window_id = (uint32_t)(ULONG_PTR)WndHandle(w);
        /* first the size, then the pixels into a buffer that holds them. */
        if (HalMimeHypercall(MIME_HC_WIN_READBACK, (ULONG64)MmGetPhysicalAddress(r).QuadPart, 0, 0, 0, 0, 0, nullptr))
            return FALSE;
        SIZE_T need = (SIZE_T)r->width * r->height * 4;
        if (!need)
            return FALSE;
        if (!pixels) {
            *width = r->width;
            *height = r->height;
            *client = (RECT){r->client.left, r->client.top, r->client.right, r->client.bottom};
            *serial = r->serial;
            return TRUE;
        }
        if (need > g_pixels_capacity) {
            if (g_pixels)
                MmFreeContiguousPages(g_pixels, g_pixels_capacity);
            g_pixels_capacity = (need + 0xFFFF) & ~(SIZE_T)0xFFFF;
            g_pixels = MmAllocateContiguousPages(g_pixels_capacity);
            if (!g_pixels) {
                g_pixels_capacity = 0;
                return FALSE;
            }
        }
        r->capacity_width = r->width;
        r->capacity_height = r->height;
        r->stride = r->width * 4;
        r->pixels_phys = (uint64_t)MmGetPhysicalAddress(g_pixels).QuadPart;
        ULONG want_w = r->width, want_h = r->height;
        if (HalMimeHypercall(MIME_HC_WIN_READBACK, (ULONG64)MmGetPhysicalAddress(r).QuadPart, 0, 0, 0, 0, 0, nullptr))
            return FALSE;
        /* a present in between may have changed the size: once more. */
        if (r->width != want_w || r->height != want_h)
            continue;
        *width = r->width;
        *height = r->height;
        *client = (RECT){r->client.left, r->client.top, r->client.right, r->client.bottom};
        *serial = r->serial;
        return TRUE;
    }
    return FALSE;
}

/*
 * a mix of the images the visible windows presented through the host, which the guest does not hear about: a
 * capture compares it with the last one to know whether the screen may have changed.
 */
ULONG64 ScreenHostSerial(void)
{
    ULONG64 mix = 0;
    for (WND *w = WndDesktop()->FirstChild; w; w = w->Next) {
        if (!presents_through_host(w) || !WndIsVisible(w) || (w->Style & WS_MINIMIZE))
            continue;
        ULONG width, height;
        RECT client;
        ULONG64 serial;
        if (read_presented(w, FALSE, &width, &height, &client, &serial))
            mix = (mix ^ (serial + (ULONG64)(ULONG_PTR)WndHandle(w) * 0x9E3779B97F4A7C15ull)) * 0xFF51AFD7ED558CCDull;
    }
    return mix;
}

/* one rectangle of a window's surface over a canvas, which has the screen coordinates origin at its first pixel. */
static void compose_surface(PUCHAR bits, LONG stride, POINT shift, const RECT *r, WND *w, SURFACE *s)
{
    LONG ox = w->WindowRect.left, oy = w->WindowRect.top;
    /* plain windows are opaque whatever alpha gdi left; layered and shaped ones have theirs. */
    BOOLEAN blend = w->Present || (w->LayerFlags & MIME_LAYER_UPDATE);
    LONG x0 = W32Max(r->left, ox), x1 = W32Min(r->right, ox + s->Width);
    for (LONG y = r->top; y < r->bottom; y++) {
        LONG sy = y - oy;
        if (sy < 0 || sy >= s->Height || x0 >= x1)
            continue;
        const ULONG *src = (const ULONG *)(s->Bits + (LONG_PTR)sy * s->Stride) + (x0 - ox);
        ULONG *dst = (ULONG *)(bits + (LONG_PTR)(y - shift.y) * stride) + (x0 - shift.x);
        if (blend)
            for (LONG x = x0; x < x1; x++, src++, dst++)
                *dst = over(*src, *dst);
        else
            for (LONG x = x0; x < x1; x++)
                *dst++ = *src++ | 0xFF000000;
    }
}

/* one rectangle of the image a window presented through the host, stretched as the host stretches it. */
static void compose_presented(PUCHAR bits, LONG stride, POINT shift, const RECT *rect, const RECT *c, ULONG width,
                              ULONG height)
{
    RECT r;
    if (!RectIntersect(&r, rect, c))
        return;
    LONG cw = c->right - c->left, ch = c->bottom - c->top;
    for (LONG y = r.top; y < r.bottom; y++) {
        const ULONG *src = (const ULONG *)(g_pixels + (SIZE_T)((y - c->top) * (LONG)height / ch) * width * 4);
        ULONG *dst = (ULONG *)(bits + (LONG_PTR)(y - shift.y) * stride);
        /* the host made the pixels opaque. */
        if ((ULONG)cw == width) {
            memcpy(dst + (r.left - shift.x), src + (r.left - c->left), (SIZE_T)(r.right - r.left) * 4);
        } else {
            for (LONG x = r.left; x < r.right; x++)
                dst[x - shift.x] = src[(x - c->left) * (LONG)width / cw];
        }
    }
}

/*
 * a window over a canvas, within the rectangles of vis in screen coordinates: its surface at its place, and over
 * its client area the last vulkan image it presented. the image is read from the host once, whatever the rectangles.
 */
static void compose_window(PUCHAR bits, LONG stride, POINT shift, const REGION *vis, WND *w, BOOLEAN host_pixels)
{
    SURFACE *s = w->Present ? w->Present : w->Surface;
    ULONG affinity = w->DisplayAffinity;
    if (affinity == 0x11)
        return;
    if (affinity == 1) {
        /* a window shown on the monitor alone is black in captures. */
        for (ULONG i = 0; i < vis->Count; i++)
            for (LONG y = vis->Rects[i].top; y < vis->Rects[i].bottom; y++) {
                ULONG *dst = (ULONG *)(bits + (LONG_PTR)(y - shift.y) * stride) + (vis->Rects[i].left - shift.x);
                for (LONG x = vis->Rects[i].left; x < vis->Rects[i].right; x++)
                    *dst++ = 0xFF000000;
            }
        return;
    }
    if (s)
        for (ULONG i = 0; i < vis->Count; i++)
            compose_surface(bits, stride, shift, &vis->Rects[i], w, s);

    /* the client area shows the last vulkan present. */
    ULONG width, height;
    RECT client;
    ULONG64 serial;
    BOOLEAN got = host_pixels && presents_through_host(w) && read_presented(w, TRUE, &width, &height, &client, &serial);
    if (!got)
        return;
    RECT c = {w->WindowRect.left + client.left, w->WindowRect.top + client.top, w->WindowRect.left + client.right,
              w->WindowRect.top + client.bottom};
    if (c.right <= c.left || c.bottom <= c.top)
        return;
    for (ULONG i = 0; i < vis->Count; i++)
        compose_presented(bits, stride, shift, &vis->Rects[i], &c, width, height);
}

/* whether a window's pixels replace what is under them, and where: the part of its rectangle its surface covers. */
static BOOLEAN window_covers(WND *w, RECT *out)
{
    if (w->DisplayAffinity == 0x11)
        return FALSE;
    if (w->DisplayAffinity == 1) {
        *out = w->WindowRect;
        return TRUE;
    }
    SURFACE *s = w->Present ? w->Present : w->Surface;
    if (!s || w->Present || (w->LayerFlags & MIME_LAYER_UPDATE))
        return FALSE;
    RECT own = {w->WindowRect.left, w->WindowRect.top, w->WindowRect.left + s->Width, w->WindowRect.top + s->Height};
    return RectIntersect(out, &w->WindowRect, &own);
}

#define PLAN_MAX 1024

typedef struct PLAN {
    WND *Window;
    REGION Visible;
} PLAN;

/*
 * the windows that show in area, top of the z order first, with the part of each that nothing opaque covers; what
 * the windows cover together is covered. a window that is under others altogether is left out, and so is the part
 * of each that is: nothing of it would ever be seen, and a screen of a few windows under a browser fills pixels that
 * are overwritten at every frame.
 */
static ULONG plan_windows(PLAN *plans, const RECT *area, REGION *covered)
{
    WND *list[PLAN_MAX];
    ULONG count = 0, planned = 0;
    for (WND *w = WndDesktop()->FirstChild; w && count < PLAN_MAX; w = w->Next)
        list[count++] = w;
    for (ULONG i = 0; i < count; i++) {
        WND *w = list[i];
        RECT r, cover;
        if (!WndIsVisible(w) || (w->Style & WS_MINIMIZE) || !RectIntersect(&r, area, &w->WindowRect))
            continue;
        PLAN *plan = &plans[planned];
        RgnInit(&plan->Visible);
        RgnSetRect(&plan->Visible, &r);
        RgnCombine(&plan->Visible, &plan->Visible, covered, RGN_DIFF);
        if (RgnIsEmpty(&plan->Visible)) {
            RgnFree(&plan->Visible);
            continue;
        }
        plan->Window = w;
        planned++;
        if (window_covers(w, &cover) && RectIntersect(&cover, &cover, area))
            RgnCombineRect(covered, covered, &cover, RGN_OR);
    }
    return planned;
}

/*
 * composes the screen within area, in surface pixels, before a read.
 * pixels come out opaque: alpha 0xFF.
 */
void ScreenCompose(const RECT *area)
{
    SURFACE *screen = ScreenSurface();
    if (!screen)
        return;
    RECT bounds = {0, 0, screen->Width, screen->Height}, r;
    if (!RectIntersect(&r, area ? area : &bounds, &bounds))
        return;
    RECT screen_area = r;
    RectOffset(&screen_area, g_screen_origin.x, g_screen_origin.y);
    PLAN *plans = W32Alloc(PLAN_MAX * sizeof(PLAN));
    REGION covered, bare;
    RgnInit(&covered);
    RgnInit(&bare);
    ULONG planned = plans ? plan_windows(plans, &screen_area, &covered) : 0;

    /* what is not on a monitor is black, a guess, and the monitors show the desktop's color, where no window does. */
    RgnOffset(&covered, -g_screen_origin.x, -g_screen_origin.y);
    ULONG background = ColorToPixel(MetricsSysColor(COLOR_BACKGROUND)) | 0xFF000000;
    RgnSetRect(&bare, &r);
    RgnCombine(&bare, &bare, &covered, RGN_DIFF);
    for (ULONG i = 0; i < bare.Count; i++) {
        const RECT *b = &bare.Rects[i];
        for (LONG y = b->top; y < b->bottom; y++)
            memset(screen->Bits + (LONG_PTR)y * screen->Stride + (LONG_PTR)b->left * 4, 0, (SIZE_T)(b->right - b->left) * 4);
        for (ULONG m = 0; m < MonitorCount(); m++) {
            RECT mr, on;
            if (!ScreenMonitorRect(m, &mr) || !RectIntersect(&on, b, &mr))
                continue;
            for (LONG y = on.top; y < on.bottom; y++) {
                ULONG *dst = (ULONG *)(screen->Bits + (LONG_PTR)y * screen->Stride);
                for (LONG x = on.left; x < on.right; x++)
                    dst[x] = background;
            }
        }
    }
    /* the windows from the bottom of the z order up, each where it shows. */
    while (planned--) {
        compose_window(screen->Bits, screen->Stride, g_screen_origin, &plans[planned].Visible, plans[planned].Window, TRUE);
        RgnFree(&plans[planned].Visible);
    }
    W32Free(plans);
    RgnFree(&covered);
    RgnFree(&bare);
}

/*
 * a window's own pixels, whatever covers it: its surface with the image its process presented through the host in
 * the client area. window is the whole window or its client area, in a canvas of bgra rows that starts at the
 * window's corner or at the client area's. the pixels are opaque, as the layered ones blended over black.
 */
BOOLEAN ScreenWindowPixels(WND *w, BOOLEAN client_only, BOOLEAN host_pixels, PUCHAR bits, LONG stride, LONG width,
                           LONG height)
{
    WND *top = WndTopLevel(w);
    if (!top || top == WndDesktop())
        return FALSE;
    RECT area = client_only ? w->ClientRect : w->WindowRect;
    if (area.right - area.left != width || area.bottom - area.top != height)
        return FALSE;
    for (LONG y = 0; y < height; y++)
        memset(bits + (LONG_PTR)y * stride, 0, (SIZE_T)width * 4);
    /* a child has no surface of its own: it is cut out of its top level window's. */
    POINT shift = {area.left, area.top};
    REGION vis;
    RgnInit(&vis);
    RECT r;
    if (RectIntersect(&r, &area, &top->WindowRect) && RgnSetRect(&vis, &r))
        compose_window(bits, stride, shift, &vis, top, host_pixels);
    RgnFree(&vis);
    for (LONG y = 0; y < height; y++) {
        ULONG *row = (ULONG *)(bits + (LONG_PTR)y * stride);
        for (LONG x = 0; x < width; x++)
            row[x] |= 0xFF000000;
    }
    return TRUE;
}
