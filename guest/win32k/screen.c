/*
 * the host presents windows separately, so screen and desktop DCs use a virtual-screen surface composed on read. fill
 * monitor areas with the desktop color and gaps with black, then draw visible windows from bottom to top. blend
 * layered and shaped surfaces and overlay the last host Vulkan image on each client area. the surface starts at the
 * virtual screen corner, possibly negative; program coordinates place the primary monitor at 0, 0. screen DC origins
 * account for that offset, while ScreenCompose takes surface coordinates.
 */
#include "win32k.h"

static SURFACE *g_screen;
static POINT g_screen_origin; /* the screen coordinates of the surface's top left pixel. */
static MimeWinReadback *g_readback;
static PUCHAR g_pixels;
static SIZE_T g_pixels_capacity;

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

/* monitor coverage excludes gaps between monitors. */
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

/* counts guest-visible screen changes: presents, configuration changes, window creation and destruction. */
ULONG ScreenSerial;

/* host Vulkan presentation requires a process connection. */
static BOOLEAN has_connection(const WND *w)
{
    return w->Thread && w->Thread->Process && w->Thread->Process->Vk;
}

/*
 * a swap chain shows in the top level window of the window it was made for, so the process that presents need not
 * own the top level window: the web view of a program is presented by the browser's graphics process, in a child
 * window it parented to the program's.
 */
static BOOLEAN tree_presents(const WND *w)
{
    if (has_connection(w))
        return TRUE;
    for (const WND *c = w->FirstChild; c; c = c->Next)
        if ((c->Style & WS_VISIBLE) && tree_presents(c))
            return TRUE;
    return FALSE;
}

static BOOLEAN presents_through_host(WND *w)
{
    return VkProcessCount && tree_presents(w);
}

/*
 * query the last Vulkan image: size, client rectangle relative to the window, and host present count. return false if
 * absent; read pixels into g_pixels only when requested.
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
        /* a present may have resized the image; retry once. */
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

/* combine visible windows' host present counts so captures detect changes that bypass the guest. */
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

/*
 * where a window is drawn on a canvas: the canvas coordinates of its top left corner and how many canvas pixels cover a
 * pixel of its surface, num per den. a window that is not aware is drawn at 96 dpi and covers more of the screen.
 */
typedef struct PLACE {
    POINT Origin;
    LONG Num, Den;
} PLACE;

/* the place of a window on the physical screen. */
static PLACE place_on_screen(const WND *w)
{
    RECT physical;
    WndPhysicalRect(w, &physical);
    PLACE p = {{physical.left, physical.top}, 1, 1};
    if (DpiVirtual(w->Awareness)) {
        p.Num = (LONG)W32SystemDpi();
        p.Den = (LONG)DpiScaleOf(w->Awareness);
    }
    return p;
}

/* the place of a window in its own pixels. */
static PLACE place_in_window(const WND *w)
{
    return (PLACE){{w->WindowRect.left, w->WindowRect.top}, 1, 1};
}

/* the size of a surface on the canvas. */
static LONG placed(LONG size, const PLACE *place)
{
    return (LONG)(((LONG64)size * place->Num + place->Den - 1) / place->Den);
}

/* draw a surface rectangle on a canvas whose first pixel has screen coordinates origin. */
static void compose_surface(PUCHAR bits, LONG stride, POINT shift, const RECT *r, const PLACE *place, WND *w,
                            SURFACE *s)
{
    LONG ox = place->Origin.x, oy = place->Origin.y;
    /* plain windows are opaque regardless of GDI alpha; layered and shaped windows retain their alpha. */
    BOOLEAN blend = w->Present || (w->LayerFlags & MIME_LAYER_UPDATE);
    LONG x0 = W32Max(r->left, ox), x1 = W32Min(r->right, ox + placed(s->Width, place));
    for (LONG y = r->top; y < r->bottom; y++) {
        LONG sy = (LONG)((LONG64)(y - oy) * place->Den / place->Num);
        if (y < oy || sy >= s->Height || x0 >= x1)
            continue;
        const ULONG *row = (const ULONG *)(s->Bits + (LONG_PTR)sy * s->Stride);
        ULONG *dst = (ULONG *)(bits + (LONG_PTR)(y - shift.y) * stride) + (x0 - shift.x);
        if (place->Num == place->Den) {
            const ULONG *src = row + (x0 - ox);
            if (blend)
                for (LONG x = x0; x < x1; x++, src++, dst++)
                    *dst = over(*src, *dst);
            else
                for (LONG x = x0; x < x1; x++)
                    *dst++ = *src++ | 0xFF000000;
            continue;
        }
        for (LONG x = x0; x < x1; x++, dst++) {
            ULONG pixel = row[(LONG)((LONG64)(x - ox) * place->Den / place->Num)];
            *dst = blend ? over(pixel, *dst) : pixel | 0xFF000000;
        }
    }
}

/* stretch the host image as it appears on screen. */
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

/* a rectangle of window pixels relative to its top level window, as the canvas has it. */
static RECT to_canvas(RECT r, const PLACE *place)
{
    if (place->Num != place->Den) {
        r.left = (LONG)((LONG64)r.left * place->Num / place->Den);
        r.top = (LONG)((LONG64)r.top * place->Num / place->Den);
        r.right = (LONG)(((LONG64)r.right * place->Num + place->Den - 1) / place->Den);
        r.bottom = (LONG)(((LONG64)r.bottom * place->Num + place->Den - 1) / place->Den);
    }
    return r;
}

static LONG distance(LONG a, LONG b)
{
    return a > b ? a - b : b - a;
}

/* the window below top that a connection's swap chain was made for: its client area is where the image shows. */
static WND *presented_window(WND *parent, const WND *top, const RECT *client)
{
    for (WND *c = parent->FirstChild; c; c = c->Next) {
        if (!(c->Style & WS_VISIBLE))
            continue;
        WND *below = presented_window(c, top, client);
        if (below)
            return below;
        if (!has_connection(c))
            continue;
        RECT r = c->ClientRect;
        RectOffset(&r, -top->WindowRect.left, -top->WindowRect.top);
        if (distance(r.left, client->left) <= 1 && distance(r.top, client->top) <= 1 &&
            distance(r.right, client->right) <= 1 && distance(r.bottom, client->bottom) <= 1)
            return c;
    }
    return nullptr;
}

/*
 * what of the top level window an image shows over: the client area of the window it was presented for, inside the
 * client areas of its parents and under the siblings in front of it, as the window manager clips a child window. a
 * transparent sibling, such as the legacy window of a browser, hides nothing. canvas pixels of the window.
 */
static void presented_clip(WND *top, WND *window, const PLACE *place, REGION *out)
{
    REGION area;
    RgnInit(&area);
    RgnSetRect(&area, &window->ClientRect);
    for (WND *n = window; n != top && n->Parent; n = n->Parent) {
        for (WND *s = n->Parent->FirstChild; s && s != n; s = s->Next)
            if ((s->Style & WS_VISIBLE) && !(s->ExStyle & WS_EX_TRANSPARENT))
                RgnCombineRect(&area, &area, &s->WindowRect, RGN_DIFF);
        RgnCombineRect(&area, &area, &n->Parent->ClientRect, RGN_AND);
    }
    RgnInit(out);
    for (ULONG i = 0; i < area.Count; i++) {
        RECT r = area.Rects[i];
        RectOffset(&r, -top->WindowRect.left, -top->WindowRect.top);
        r = to_canvas(r, place);
        RectOffset(&r, place->Origin.x, place->Origin.y);
        RgnCombineRect(out, out, &r, RGN_OR);
    }
    RgnFree(&area);
}

/*
 * compose visible screen rectangles from the window surface and last Vulkan client image. read the host image once
 * for all rectangles.
 */
static void compose_window(PUCHAR bits, LONG stride, POINT shift, const REGION *vis, WND *w, const PLACE *place,
                           BOOLEAN host_pixels)
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
            compose_surface(bits, stride, shift, &vis->Rects[i], place, w, s);

    /* the client area shows the last Vulkan present. */
    ULONG width, height;
    RECT client;
    ULONG64 serial;
    BOOLEAN got = host_pixels && presents_through_host(w) && read_presented(w, TRUE, &width, &height, &client, &serial);
    if (!got)
        return;
    /* the client area comes in the pixels of the presenting program, the same as the window's own, so the canvas scales it. */
    RECT c = to_canvas(client, place);
    RectOffset(&c, place->Origin.x, place->Origin.y);
    if (c.right <= c.left || c.bottom <= c.top)
        return;
    /* a child window of another process is clipped by its parents, whose client areas are smaller than its own. */
    REGION shown, clip;
    RgnInit(&shown);
    RgnCopy(&shown, vis);
    WND *presenter = presented_window(w, w, &client);
    if (presenter) {
        presented_clip(w, presenter, place, &clip);
        RgnCombine(&shown, &shown, &clip, RGN_AND);
        RgnFree(&clip);
    }
    for (ULONG i = 0; i < shown.Count; i++)
        compose_presented(bits, stride, shift, &shown.Rects[i], &c, width, height);
    RgnFree(&shown);
}

/* find the surface-backed rectangle that occludes lower windows. */
static BOOLEAN window_covers(WND *w, RECT *out)
{
    if (w->DisplayAffinity == 0x11)
        return FALSE;
    RECT window;
    WndPhysicalRect(w, &window);
    if (w->DisplayAffinity == 1) {
        *out = window;
        return TRUE;
    }
    SURFACE *s = w->Present ? w->Present : w->Surface;
    if (!s || w->Present || (w->LayerFlags & MIME_LAYER_UPDATE))
        return FALSE;
    PLACE place = place_on_screen(w);
    RECT own = {place.Origin.x, place.Origin.y, place.Origin.x + placed(s->Width, &place),
                place.Origin.y + placed(s->Height, &place)};
    return RectIntersect(out, &window, &own);
}

#define PLAN_MAX 1024

typedef struct PLAN {
    WND *Window;
    REGION Visible;
} PLAN;

/*
 * collect visible window regions from top to bottom, excluding areas hidden by opaque windows. covered is their
 * combined coverage. skipping occluded pixels avoids drawing them again on every frame.
 */
static ULONG plan_windows(PLAN *plans, const RECT *area, REGION *covered)
{
    WND *list[PLAN_MAX];
    ULONG count = 0, planned = 0;
    for (WND *w = WndDesktop()->FirstChild; w && count < PLAN_MAX; w = w->Next)
        list[count++] = w;
    for (ULONG i = 0; i < count; i++) {
        WND *w = list[i];
        RECT r, cover, window;
        WndPhysicalRect(w, &window);
        if (!WndIsVisible(w) || (w->Style & WS_MINIMIZE) || !RectIntersect(&r, area, &window))
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

/* compose area in surface coordinates before a read; output alpha is 0xFF. */
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

    /* gaps between monitors are black, a guess; uncovered monitor areas use the desktop color. */
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
    while (planned--) {
        PLACE place = place_on_screen(plans[planned].Window);
        compose_window(screen->Bits, screen->Stride, g_screen_origin, &plans[planned].Visible, plans[planned].Window,
                       &place, TRUE);
        RgnFree(&plans[planned].Visible);
    }
    W32Free(plans);
    RgnFree(&covered);
    RgnFree(&bare);
}

/*
 * capture a window regardless of occlusion: its surface plus the host image in its client area. window selects the
 * full window or client area in a BGRA canvas starting at that corner. output is opaque; layered pixels blend over
 * black.
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
    /* children use a region of their top level window surface. */
    POINT shift = {area.left, area.top};
    REGION vis;
    RgnInit(&vis);
    RECT r;
    PLACE place = place_in_window(top);
    if (RectIntersect(&r, &area, &top->WindowRect) && RgnSetRect(&vis, &r))
        compose_window(bits, stride, shift, &vis, top, &place, host_pixels);
    RgnFree(&vis);
    for (LONG y = 0; y < height; y++) {
        ULONG *row = (ULONG *)(bits + (LONG_PTR)y * stride);
        for (LONG x = 0; x < width; x++)
            row[x] |= 0xFF000000;
    }
    return TRUE;
}
