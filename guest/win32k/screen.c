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
static REGION g_kept;         /* the part of the surface that shows the desktop, which a drawing in the screen's device context stays in. */
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
        RgnClear(&g_kept);
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

BOOLEAN ScreenIsSurface(const SURFACE *s) { return s && (s == g_screen || (s->Flags & SURF_VSCREEN)); }

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
    return w->Thread && w->Thread->Process && (w->Thread->Process->Vk || w->Thread->Process->Gpu);
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
static BOOLEAN read_presented(WND *top, WND *source, BOOLEAN pixels, ULONG *width, ULONG *height, RECT *client,
                             ULONG64 *serial)
{
    if (!g_readback && !(g_readback = MmAllocateContiguousPages(4096)))
        return FALSE;
    for (int attempt = 0; attempt < 2; attempt++) {
        MimeWinReadback *r = g_readback;
        memset(r, 0, sizeof(*r));
        r->window_id = (uint32_t)(ULONG_PTR)WndHandle(top);
        r->source = (uint32_t)(ULONG_PTR)WndHandle(source);
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

/* mixes the present counts of the windows of a tree that present. */
static ULONG64 tree_serial(WND *top, WND *w, ULONG64 mix)
{
    if (has_connection(w)) {
        ULONG width, height;
        RECT client;
        ULONG64 serial;
        if (read_presented(top, w, FALSE, &width, &height, &client, &serial))
            mix = (mix ^ (serial + (ULONG64)(ULONG_PTR)WndHandle(w) * 0x9E3779B97F4A7C15ull)) * 0xFF51AFD7ED558CCDull;
    }
    for (WND *c = w->FirstChild; c; c = c->Next)
        if (c->Style & WS_VISIBLE)
            mix = tree_serial(top, c, mix);
    return mix;
}

/* combine visible windows' host present counts so captures detect changes that bypass the guest. */
ULONG64 ScreenHostSerial(void)
{
    ULONG64 mix = 0;
    for (WND *w = WndDesktop()->FirstChild; w; w = w->Next) {
        if (!presents_through_host(w) || !WndIsVisible(w) || (w->Style & WS_MINIMIZE))
            continue;
        mix = tree_serial(w, w, mix);
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
    if (w->Awareness < DPI_AWARE_PER_MONITOR) {
        p.Num = (LONG)WndMonitorDpi(w);
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

/* a layered or shaped window shows what is under it; the frame the dwm extends into a client area does not. */
static BOOLEAN see_through(const WND *w)
{
    return w->Shape || (w->LayerFlags & (LWA_ALPHA | LWA_COLORKEY | MIME_LAYER_UPDATE));
}

/* the physical screen rectangle a window shows in, without the invisible resize border, as the host draws it. */
static void shown_rect(WND *w, RECT *out)
{
    WndPhysicalRect(w, out);
    if (w->Style & WS_CHILD)
        return;
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    LONG left = out->left, top = out->top;
    out->left = left + WndToPhysical(w, vis.left);
    out->top = top + WndToPhysical(w, vis.top);
    out->right = W32Min(out->right, left + WndToPhysical(w, vis.right));
    out->bottom = W32Min(out->bottom, top + WndToPhysical(w, vis.bottom));
}

/* draw a surface rectangle on a canvas whose first pixel has screen coordinates origin. */
static void compose_surface(PUCHAR bits, LONG stride, POINT shift, const RECT *r, const PLACE *place, WND *w,
                            SURFACE *s)
{
    LONG ox = place->Origin.x, oy = place->Origin.y;
    /* plain windows are opaque regardless of GDI alpha; layered and shaped windows retain their alpha. */
    BOOLEAN blend = see_through(w);
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

/* what a window occupies, its rectangle cut by the region set with SetWindowRgn. */
static void window_region(const WND *w, REGION *out)
{
    RgnInit(out);
    if (!w->Shape) {
        RgnSetRect(out, &w->WindowRect);
        return;
    }
    RgnCopy(out, w->Shape);
    RgnOffset(out, w->WindowRect.left, w->WindowRect.top);
    RgnCombineRect(out, out, &w->WindowRect, RGN_AND);
}

static void cut_window(REGION *out, const WND *w)
{
    if (!w->Shape) {
        RgnCombineRect(out, out, &w->WindowRect, RGN_DIFF);
        return;
    }
    REGION r;
    window_region(w, &r);
    RgnCombine(out, out, &r, RGN_DIFF);
    RgnFree(&r);
}

static void keep_window(REGION *out, const WND *w)
{
    if (!w->Shape)
        return;
    REGION r;
    window_region(w, &r);
    RgnCombine(out, out, &r, RGN_AND);
    RgnFree(&r);
}

/*
 * what of the top level window the image of a window shows over, in screen coordinates of the windows: its client area,
 * inside the client areas and the regions of its parents, under the windows in front of it and under its own children,
 * as the window manager composes the windows of a tree one over the other. a window with a region (SetWindowRgn) takes
 * only that out of the windows behind it and only that from the windows it holds. a transparent window, such as the
 * legacy window of a browser, hides nothing. the clip styles are for the drawing of the program and do not change that.
 */
static void layer_area(const WND *top, const WND *window, BOOLEAN children, REGION *out)
{
    RgnInit(out);
    RgnSetRect(out, &window->ClientRect);
    keep_window(out, window);
    for (const WND *n = window; n != top && n->Parent; n = n->Parent) {
        for (const WND *s = n->Parent->FirstChild; s && s != n; s = s->Next)
            if ((s->Style & WS_VISIBLE) && !(s->ExStyle & WS_EX_TRANSPARENT))
                cut_window(out, s);
        RgnCombineRect(out, out, &n->Parent->ClientRect, RGN_AND);
        keep_window(out, n->Parent);
    }
    for (const WND *c = children ? window->FirstChild : nullptr; c; c = c->Next)
        if ((c->Style & WS_VISIBLE) && !(c->ExStyle & WS_EX_TRANSPARENT))
            cut_window(out, c);
}

/*
 * the constant alpha and the color key of a layered top level window, which are the ones of everything it holds: the
 * window manager composes the window with the images in it first and then makes it transparent as it was asked.
 */
typedef struct LAYERFX {
    ULONG Alpha;
    BOOLEAN Keyed;
    ULONG Key;
} LAYERFX;

static void layer_effects(const WND *top, LAYERFX *fx)
{
    *fx = (LAYERFX){.Alpha = 255};
    if (!(top->ExStyle & WS_EX_LAYERED) || (top->LayerFlags & MIME_LAYER_UPDATE))
        return;
    if (top->LayerFlags & LWA_ALPHA)
        fx->Alpha = top->LayerAlpha;
    if (top->LayerFlags & LWA_COLORKEY) {
        fx->Keyed = TRUE;
        fx->Key = ColorToPixel(top->LayerKey) & 0xFFFFFF;
    }
}

static ULONG blend_constant(ULONG src, ULONG dst, ULONG alpha)
{
    ULONG b = ((src & 0xFF) * alpha + (dst & 0xFF) * (255 - alpha) + 127) / 255;
    ULONG g = ((src >> 8 & 0xFF) * alpha + (dst >> 8 & 0xFF) * (255 - alpha) + 127) / 255;
    ULONG r = ((src >> 16 & 0xFF) * alpha + (dst >> 16 & 0xFF) * (255 - alpha) + 127) / 255;
    return 0xFF000000 | r << 16 | g << 8 | b;
}

/* what was on the canvas under a layered window before it was composed, which its alpha and key show through to. */
typedef struct UNDER {
    ULONG *Bits;
    RECT Box; /* canvas pixels. */
} UNDER;

static ULONG under_pixel(const UNDER *under, LONG x, LONG y)
{
    return under->Bits[(SIZE_T)(y - under->Box.top) * (SIZE_T)(under->Box.right - under->Box.left) +
                       (SIZE_T)(x - under->Box.left)];
}

/*
 * stretch the host image as it appears on screen. through the alpha and the key of a layered window it is made over the
 * pixels under the window, not over the window's own, which the image replaces.
 */
static void compose_presented(PUCHAR bits, LONG stride, POINT shift, const RECT *rect, const RECT *c, ULONG width,
                              ULONG height, const LAYERFX *fx, const UNDER *under)
{
    RECT r;
    if (!RectIntersect(&r, rect, c))
        return;
    LONG cw = c->right - c->left, ch = c->bottom - c->top;
    for (LONG y = r.top; y < r.bottom; y++) {
        const ULONG *src = (const ULONG *)(g_pixels + (SIZE_T)((y - c->top) * (LONG)height / ch) * width * 4);
        ULONG *dst = (ULONG *)(bits + (LONG_PTR)(y - shift.y) * stride);
        /* the host made the pixels opaque. */
        if (!under && (ULONG)cw == width) {
            memcpy(dst + (r.left - shift.x), src + (r.left - c->left), (SIZE_T)(r.right - r.left) * 4);
            continue;
        }
        for (LONG x = r.left; x < r.right; x++) {
            ULONG pixel = src[(ULONG)cw == width ? x - c->left : (x - c->left) * (LONG)width / cw];
            if (!under) {
                dst[x - shift.x] = pixel;
                continue;
            }
            ULONG behind = under_pixel(under, x, y);
            dst[x - shift.x] = fx->Keyed && (pixel & 0xFFFFFF) == fx->Key ? behind
                               : fx->Alpha == 255                         ? pixel
                                                                          : blend_constant(pixel, behind, fx->Alpha);
        }
    }
}

typedef struct COMPOSE {
    PUCHAR Bits;
    LONG Stride;
    POINT Shift;
    const REGION *Visible; /* of the top level window, canvas pixels. */
    WND *Top;
    const PLACE *Place;
    const LAYERFX *Effects;
    const UNDER *Under; /* null when the window is not made transparent. */
} COMPOSE;

/* the image a window presented over what shows of its client area. */
static void compose_layer(const COMPOSE *cx, WND *window)
{
    WND *top = cx->Top;
    RECT own = window->ClientRect;
    RectOffset(&own, -top->WindowRect.left, -top->WindowRect.top);
    RECT c = to_canvas(own, cx->Place);
    RectOffset(&c, cx->Place->Origin.x, cx->Place->Origin.y);
    if (c.right <= c.left || c.bottom <= c.top)
        return;
    REGION area, shown;
    layer_area(top, window, TRUE, &area);
    RgnInit(&shown);
    for (ULONG i = 0; i < area.Count; i++) {
        RECT r = area.Rects[i];
        RectOffset(&r, -top->WindowRect.left, -top->WindowRect.top);
        r = to_canvas(r, cx->Place);
        RectOffset(&r, cx->Place->Origin.x, cx->Place->Origin.y);
        RgnCombineRect(&shown, &shown, &r, RGN_OR);
    }
    RgnFree(&area);
    RgnCombine(&shown, &shown, cx->Visible, RGN_AND);
    ULONG width, height;
    RECT client;
    ULONG64 serial;
    if (!RgnIsEmpty(&shown) && read_presented(top, window, TRUE, &width, &height, &client, &serial))
        for (ULONG i = 0; i < shown.Count; i++)
            compose_presented(cx->Bits, cx->Stride, cx->Shift, &shown.Rects[i], &c, width, height, cx->Effects,
                              cx->Under);
    RgnFree(&shown);
}

/* the images of a tree from the back to the front: a window's own, then its children's. */
static void compose_tree(const COMPOSE *cx, WND *parent)
{
    for (WND *c = parent->LastChild; c; c = c->Prev) {
        if (!(c->Style & WS_VISIBLE) || !tree_presents(c))
            continue;
        if (has_connection(c))
            compose_layer(cx, c);
        compose_tree(cx, c);
    }
}

/*
 * compose visible screen rectangles from the window surface and the images the windows of its tree presented through
 * the host, each in its client area. the host image of a window is read once for all rectangles.
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
    BOOLEAN layers = host_pixels && presents_through_host(w);
    /* a window that is made transparent shows the images in it over what is under it, which is kept from before. */
    LAYERFX effects;
    UNDER under = {};
    layer_effects(w, &effects);
    BOOLEAN transparent = layers && (effects.Alpha != 255 || effects.Keyed) && vis->Count;
    if (transparent) {
        under.Box = vis->Rects[0];
        for (ULONG i = 1; i < vis->Count; i++) {
            under.Box.left = W32Min(under.Box.left, vis->Rects[i].left);
            under.Box.top = W32Min(under.Box.top, vis->Rects[i].top);
            under.Box.right = W32Max(under.Box.right, vis->Rects[i].right);
            under.Box.bottom = W32Max(under.Box.bottom, vis->Rects[i].bottom);
        }
        LONG box_width = under.Box.right - under.Box.left;
        under.Bits = W32Alloc((SIZE_T)box_width * (under.Box.bottom - under.Box.top) * 4);
        if (under.Bits)
            for (LONG y = under.Box.top; y < under.Box.bottom; y++)
                memcpy(under.Bits + (SIZE_T)(y - under.Box.top) * box_width,
                       bits + (LONG_PTR)(y - shift.y) * stride + (LONG_PTR)(under.Box.left - shift.x) * 4,
                       (SIZE_T)box_width * 4);
        else
            transparent = FALSE;
    }
    if (s)
        for (ULONG i = 0; i < vis->Count; i++)
            compose_surface(bits, stride, shift, &vis->Rects[i], place, w, s);
    if (!layers) {
        W32Free(under.Bits);
        return;
    }

    /* the window's own image under its children's, cut by the window's region. */
    REGION cut;
    RgnInit(&cut);
    RgnCopy(&cut, vis);
    if (w->Shape) {
        REGION shape;
        RgnInit(&shape);
        for (ULONG i = 0; i < w->Shape->Count; i++) {
            RECT r = to_canvas(w->Shape->Rects[i], place);
            RectOffset(&r, place->Origin.x, place->Origin.y);
            RgnCombineRect(&shape, &shape, &r, RGN_OR);
        }
        RgnCombine(&cut, &cut, &shape, RGN_AND);
        RgnFree(&shape);
    }
    COMPOSE cx = {bits, stride, shift, &cut, w, place, &effects, transparent ? &under : nullptr};
    if (has_connection(w))
        compose_layer(&cx, w);
    compose_tree(&cx, w);
    RgnFree(&cut);
    W32Free(under.Bits);
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
    if (!s || see_through(w))
        return FALSE;
    shown_rect(w, &window);
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
        shown_rect(w, &window);
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


/*
 * the screen of a program that is not per monitor aware is the physical one in its own pixels. the surface of its
 * device context holds that picture: it is made from the screen when the program reads, and what the program draws in
 * it goes back into the screen, each of its pixels covering the pixels of the physical one that are its, which is
 * the way the compositor of Windows stretches what such a program draws.
 */
static struct {
    SURFACE *Surface;
    POINT Origin;
} g_virtual[2];

static ULONG virtual_index(ULONG awareness)
{
    return awareness == DPI_AWARE_UNAWARE ? 0 : 1;
}

static ULONG virtual_awareness(const SURFACE *s)
{
    for (ULONG i = 0; i < 2; i++)
        if (g_virtual[i].Surface == s)
            return i ? DPI_AWARE_SYSTEM : DPI_AWARE_UNAWARE;
    return DPI_AWARE_PER_MONITOR;
}

SURFACE *ScreenVirtualSurface(ULONG awareness, POINT *origin)
{
    ULONG i = virtual_index(awareness);
    RECT all = DpiVirtualScreen(awareness);
    LONG width = all.right - all.left, height = all.bottom - all.top;
    SURFACE *s = g_virtual[i].Surface;
    if (s && (s->Width != width || s->Height != height || g_virtual[i].Origin.x != all.left ||
              g_virtual[i].Origin.y != all.top)) {
        if (s->Header.SelectCount)
            s->Flags |= SURF_RETIRED;
        else
            SurfaceDestroy(s);
        g_virtual[i].Surface = s = nullptr;
    }
    if (!s) {
        s = SurfaceCreate(width, height, 32, SURF_TOPDOWN);
        if (s)
            s->Flags |= SURF_VSCREEN;
        g_virtual[i].Surface = s;
        g_virtual[i].Origin = (POINT){all.left, all.top};
    }
    *origin = g_virtual[i].Origin;
    return s;
}

/* the monitors of that screen in the coordinates of its surface. */
void ScreenVirtualVisibleRegion(ULONG awareness, REGION *out)
{
    RECT monitors[MIME_USER_MAX_MONITORS];
    POINT origin = g_virtual[virtual_index(awareness)].Origin;
    ULONG count = DpiVirtualMonitors(awareness, monitors);
    RgnClear(out);
    for (ULONG i = 0; i < count; i++) {
        RectOffset(&monitors[i], -origin.x, -origin.y);
        RgnCombineRect(out, out, &monitors[i], RGN_OR);
    }
}

/* the physical pixels, in the coordinates of the screen surface, that an area of a virtual screen is made of. */
static RECT physical_area(ULONG awareness, POINT origin, const RECT *area)
{
    POINT corners[4] = {{area->left, area->top}, {area->right, area->top}, {area->left, area->bottom},
                        {area->right, area->bottom}};
    RECT r = {};
    for (ULONG i = 0; i < 4; i++) {
        corners[i].x += origin.x;
        corners[i].y += origin.y;
        DpiPointConvert(&corners[i], awareness, DPI_PHYSICAL);
        corners[i].x -= g_screen_origin.x;
        corners[i].y -= g_screen_origin.y;
        if (!i || corners[i].x < r.left)
            r.left = corners[i].x;
        if (!i || corners[i].y < r.top)
            r.top = corners[i].y;
        if (!i || corners[i].x > r.right)
            r.right = corners[i].x;
        if (!i || corners[i].y > r.bottom)
            r.bottom = corners[i].y;
    }
    r.right++;
    r.bottom++;
    return r;
}

static void virtual_refresh(SURFACE *s, ULONG awareness, const RECT *area)
{
    SURFACE *screen = ScreenSurface();
    RECT r, bounds = {0, 0, s->Width, s->Height};
    if (!screen || !RectIntersect(&r, area, &bounds))
        return;
    POINT origin = g_virtual[virtual_index(awareness)].Origin;
    RECT physical = physical_area(awareness, origin, &r);
    ScreenCompose(&physical);
    POINT row[256];
    for (LONG y = r.top; y < r.bottom; y++) {
        ULONG *to = (ULONG *)(s->Bits + (LONG_PTR)y * s->Stride);
        for (LONG x = r.left; x < r.right; x += 256) {
            ULONG count = (ULONG)W32Min(256, r.right - x);
            DpiConvertRow(awareness, DPI_PHYSICAL, y + origin.y, x + origin.x, count, row);
            for (ULONG i = 0; i < count; i++) {
                LONG sx = row[i].x - g_screen_origin.x, sy = row[i].y - g_screen_origin.y;
                to[x + (LONG)i] = sx >= 0 && sx < screen->Width && sy >= 0 && sy < screen->Height
                                      ? ((const ULONG *)(screen->Bits + (LONG_PTR)sy * screen->Stride))[sx]
                                      : 0xFF000000;
            }
        }
    }
}

/* a drawing in the screen stays where no window covers it. */
void ScreenKeep(const RECT *area)
{
    RgnCombineRect(&g_kept, &g_kept, area, RGN_OR);
    ScreenSerial++;
}

/* what was drawn in the virtual screen goes into the physical one. */
void ScreenVirtualFlush(SURFACE *s, const RECT *dirty)
{
    ULONG awareness = virtual_awareness(s);
    SURFACE *screen = ScreenSurface();
    if (awareness == DPI_AWARE_PER_MONITOR || !screen)
        return;
    POINT origin = g_virtual[virtual_index(awareness)].Origin;
    RECT physical = physical_area(awareness, origin, dirty), bounds = {0, 0, screen->Width, screen->Height}, r;
    if (!RectIntersect(&r, &physical, &bounds))
        return;
    POINT row[256];
    for (LONG y = r.top; y < r.bottom; y++) {
        ULONG *to = (ULONG *)(screen->Bits + (LONG_PTR)y * screen->Stride);
        for (LONG x = r.left; x < r.right; x += 256) {
            ULONG count = (ULONG)W32Min(256, r.right - x);
            DpiConvertRow(DPI_PHYSICAL, awareness, y + g_screen_origin.y, x + g_screen_origin.x, count, row);
            for (ULONG i = 0; i < count; i++) {
                LONG vx = row[i].x - origin.x, vy = row[i].y - origin.y;
                if (vx >= dirty->left && vx < dirty->right && vy >= dirty->top && vy < dirty->bottom)
                    to[x + (LONG)i] = ((const ULONG *)(s->Bits + (LONG_PTR)vy * s->Stride))[vx] | 0xFF000000;
            }
        }
    }
    /* the desktop under it keeps it, windows over it do not. */
    SurfaceMarkDirty(screen, &r);
}

/* make the surface of a screen device context current before it is read. */
void ScreenComposeFor(const SURFACE *s, const RECT *area)
{
    if (s == g_screen) {
        ScreenCompose(area);
        return;
    }
    ULONG awareness = virtual_awareness(s);
    if (awareness != DPI_AWARE_PER_MONITOR)
        virtual_refresh((SURFACE *)s, awareness, area);
}

/* compose area in surface coordinates before a read; output alpha is 0xFF. */
/* the 1 pixel border of the visible frame, which the host draws over the window. */
static void draw_border(SURFACE *screen, const REGION *vis, WND *w)
{
    ULONG color = HostBorderColor(w);
    ULONG a = color >> 24;
    if (!a || (w->Flags & WNDF_FULLSCREEN))
        return;
    ULONG pixel = (a << 24) | ((((color >> 16) & 0xFF) * a / 255) << 16) | ((((color >> 8) & 0xFF) * a / 255) << 8) |
                  ((color & 0xFF) * a / 255);
    RECT r;
    shown_rect(w, &r);
    RectOffset(&r, -g_screen_origin.x, -g_screen_origin.y);
    RECT edges[4] = {{r.left, r.top, r.right, r.top + 1},
                     {r.left, r.bottom - 1, r.right, r.bottom},
                     {r.left, r.top + 1, r.left + 1, r.bottom - 1},
                     {r.right - 1, r.top + 1, r.right, r.bottom - 1}};
    REGION clip;
    RgnInit(&clip);
    RgnCopy(&clip, vis);
    RgnOffset(&clip, -g_screen_origin.x, -g_screen_origin.y);
    for (int e = 0; e < 4; e++) {
        REGION part;
        RgnInit(&part);
        RgnCopy(&part, &clip);
        RgnCombineRect(&part, &part, &edges[e], RGN_AND);
        for (ULONG i = 0; i < part.Count; i++)
            for (LONG y = part.Rects[i].top; y < part.Rects[i].bottom; y++) {
                ULONG *row = (ULONG *)(screen->Bits + (LONG_PTR)y * screen->Stride);
                for (LONG x = part.Rects[i].left; x < part.Rects[i].right; x++)
                    row[x] = over(pixel, row[x]);
            }
        RgnFree(&part);
    }
    RgnFree(&clip);
}

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
    /*
     * the desktop shows what was drawn on it until a window covers that part: only what was not bare before is painted,
     * and what a window covers is not kept.
     */
    REGION fresh, under, touched, below;
    RgnInit(&fresh);
    RgnInit(&under);
    RgnInit(&touched);
    RgnInit(&below);
    /*
     * a window that does not cover, a layered one, blends with what is under it, so the desktop is painted again under
     * it each time and is not kept: the blend would stay on the screen after the window and add up.
     */
    for (ULONG i = 0; i < planned; i++)
        RgnCombine(&touched, &touched, &plans[i].Visible, RGN_OR);
    RgnOffset(&touched, -g_screen_origin.x, -g_screen_origin.y);
    RgnCombine(&fresh, &bare, &g_kept, RGN_DIFF);
    RgnCombine(&below, &bare, &touched, RGN_AND);
    RgnCombine(&fresh, &fresh, &below, RGN_OR);
    RgnSetRect(&under, &r);
    RgnCombine(&under, &under, &bare, RGN_DIFF);
    RgnCombine(&g_kept, &g_kept, &under, RGN_DIFF);
    RgnCombine(&g_kept, &g_kept, &bare, RGN_OR);
    RgnCombine(&g_kept, &g_kept, &touched, RGN_DIFF);
    RgnFree(&below);
    RgnFree(&touched);
    RgnFree(&under);
    RgnFree(&bare);
    bare = fresh;
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
        draw_border(screen, &plans[planned].Visible, plans[planned].Window);
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

/* the places in the paint order of a tree before a window: its parent first, then the children from the back. */
static BOOLEAN paint_order(const WND *parent, const WND *window, ULONG *count)
{
    for (const WND *c = parent->LastChild; c; c = c->Prev) {
        if (!(c->Style & WS_VISIBLE))
            continue;
        ++*count;
        if (c == window || paint_order(c, window, count))
            return TRUE;
    }
    return FALSE;
}

/*
 * on Windows a child that presents shares the surface of its top level window, so what it leaves when it shrinks keeps
 * its last image until something draws there. here the surface never held that image, so the layer of a window that
 * shrank keeps the place it left until a draw into the surface takes it, and the host is told with the flush that
 * carries the draw. a layer that grew over what its parent uncovered waits the same way, for the surface still shows
 * the old frame there, and gives up waiting when nothing draws. window pixels relative to the top level window.
 */
typedef struct HELD {
    WND *Window, *Top;
    RECT Now;   /* where the window shows. */
    RECT Shown; /* where the host was told its layer is. */
    RECT Left;  /* what the draws since left of that. */
    BOOLEAN Grew;
    ULONGLONG Until; /* when a layer that grew stops waiting for a draw. */
} HELD;

#define HELD_GREW_MS 50

static HELD g_held[8];
static KSPIN_LOCK g_held_lock;
LONG ScreenHeldCount;
ULONG ScreenLayerStamp;

static HELD *held_of(const WND *w)
{
    for (LONG i = 0; i < ScreenHeldCount; i++)
        if (g_held[i].Window == w)
            return &g_held[i];
    return nullptr;
}

static void held_drop(HELD *h)
{
    *h = g_held[--ScreenHeldCount];
}

static BOOLEAN same_rect(const RECT *a, const RECT *b)
{
    return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}

static void hold_area(WND *top, WND *w, MIME_PRESENT_LAYER *out)
{
    RECT now = out->Visible;
    LONG width = top->WindowRect.right - top->WindowRect.left, height = top->WindowRect.bottom - top->WindowRect.top;
    KIRQL irql;
    KeAcquireSpinLock(&g_held_lock, &irql);
    HELD *h = held_of(w);
    RECT shown = h ? h->Shown : w->LayerShown;
    shown.right = W32Min(shown.right, width);
    shown.bottom = W32Min(shown.bottom, height);
    BOOLEAN shrank = !RectEmpty(&now) && now.left == shown.left && now.top == shown.top && now.right <= shown.right &&
                     now.bottom <= shown.bottom && !same_rect(&now, &shown);
    BOOLEAN grew = !RectEmpty(&now) && !RectEmpty(&shown) && now.left == shown.left && now.top == shown.top &&
                   now.right >= shown.right && now.bottom >= shown.bottom && !same_rect(&now, &shown);
    ULONGLONG time = KeQueryInterruptTime();
    if (h && h->Grew && time >= h->Until)
        grew = FALSE;
    if (h && !(h->Grew ? grew : shrank)) {
        held_drop(h);
        h = nullptr;
    } else if (!h && (shrank || grew) && ScreenHeldCount < (LONG)RTL_NUMBER_OF(g_held)) {
        h = &g_held[ScreenHeldCount++];
        *h = (HELD){.Window = w, .Left = shown, .Grew = grew, .Until = time + HELD_GREW_MS * 10000ull};
    }
    if (h && h->Grew) {
        h->Top = top;
        h->Now = now;
        h->Shown = shown;
        out->Visible = shown;
    } else if (h) {
        h->Top = top;
        h->Now = now;
        h->Shown = shown;
        h->Left.right = W32Max(W32Min(h->Left.right, shown.right), now.right);
        h->Left.bottom = W32Max(W32Min(h->Left.bottom, shown.bottom), now.bottom);
        /* the host fills what is held past the client area, see mime_ui_vk_frame. */
        out->Visible = shown;
    }
    KeReleaseSpinLock(&g_held_lock, irql);
    w->LayerShown = out->Visible;
}

/* a draw into the surface of a top level window, which takes what it covers from the layers that hold it. */
void ScreenHeldDrawn(const SURFACE *s, const RECT *drawn)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_held_lock, &irql);
    for (LONG i = 0; i < ScreenHeldCount; i++) {
        HELD *h = &g_held[i];
        RECT hit;
        if (h->Top != s->Window)
            continue;
        if (h->Grew) {
            /* the parent draws its frame past the edge the layer had, in the rows or columns of the layer. */
            if ((h->Now.right > h->Shown.right && drawn->right > h->Shown.right && drawn->top < h->Now.bottom &&
                 drawn->bottom > h->Now.top) ||
                (h->Now.bottom > h->Shown.bottom && drawn->bottom > h->Shown.bottom && drawn->left < h->Now.right &&
                 drawn->right > h->Now.left))
                h->Left = h->Now;
            continue;
        }
        if (!RectIntersect(&hit, drawn, &h->Left))
            continue;
        if (drawn->left >= h->Now.right) {
            h->Left.right = drawn->left;
        } else if (drawn->top >= h->Now.bottom) {
            h->Left.bottom = drawn->top;
        } else {
            if (drawn->right > h->Now.right)
                h->Left.right = h->Now.right;
            if (drawn->bottom > h->Now.bottom)
                h->Left.bottom = h->Now.bottom;
        }
    }
    KeReleaseSpinLock(&g_held_lock, irql);
}

void ScreenHeldGone(const WND *w)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_held_lock, &irql);
    for (LONG i = ScreenHeldCount - 1; i >= 0; i--)
        if (g_held[i].Window == w || g_held[i].Top == w)
            held_drop(&g_held[i]);
    KeReleaseSpinLock(&g_held_lock, irql);
}

/*
 * where the image a window presents lies in its top level window, which the host shows it in: the client area, the part
 * of it that shows (empty while the window is hidden or covered) and the place of the window in the paint order of
 * the tree. window pixels relative to the top level window.
 */
void ScreenLayer(WND *w, MIME_PRESENT_LAYER *out)
{
    *out = (MIME_PRESENT_LAYER){};
    if (!++ScreenLayerStamp)
        ScreenLayerStamp = 1;
    out->Stamp = ScreenLayerStamp;
    WND *top = WndTopLevel(w);
    if (!top || top == WndDesktop() || !(top->Flags & WNDF_HOST_WINDOW))
        return;
    out->Window = (ULONG)(ULONG_PTR)WndHandle(top);
    out->Client = w->ClientRect;
    RectOffset(&out->Client, -top->WindowRect.left, -top->WindowRect.top);
    if (w != top)
        paint_order(top, w, &out->Order);
    if (!WndIsVisible(w) || (top->Style & WS_MINIMIZE)) {
        hold_area(top, w, out);
        return;
    }
    /* a window only its children draw in has nothing of its own to show, yet its layers need a host window. */
    if (!(top->Flags & WNDF_PRESENTED)) {
        top->Flags |= WNDF_PRESENTED;
        HostConfigure(top, FALSE);
    }
    REGION area;
    layer_area(top, w, FALSE, &area);
    if (!RgnIsEmpty(&area)) {
        RECT bounds = area.Rects[0];
        for (ULONG i = 1; i < area.Count; i++) {
            bounds.left = W32Min(bounds.left, area.Rects[i].left);
            bounds.top = W32Min(bounds.top, area.Rects[i].top);
            bounds.right = W32Max(bounds.right, area.Rects[i].right);
            bounds.bottom = W32Max(bounds.bottom, area.Rects[i].bottom);
        }
        RectOffset(&bounds, -top->WindowRect.left, -top->WindowRect.top);
        out->Visible = bounds;
    }
    RgnFree(&area);
    hold_area(top, w, out);
}

static MimeWinLayer *g_layer;

/* tells the host where a window that presents lies now, and whether it shows. */
static void send_layer(WND *w, BOOLEAN follows)
{
    if ((w->Flags & WNDF_PRESENTS) && has_connection(w) && (g_layer || (g_layer = MmAllocateContiguousPages(4096)))) {
        MIME_PRESENT_LAYER where;
        ScreenLayer(w, &where);
        *g_layer = (MimeWinLayer){.window_id = where.Window,
                                  .source = (uint32_t)(ULONG_PTR)WndHandle(w),
                                  .order = where.Order,
                                  .stamp = ScreenLayerStamp,
                                  .follows = follows,
                                  .client = {where.Client.left, where.Client.top, where.Client.right, where.Client.bottom},
                                  .visible = {where.Visible.left, where.Visible.top, where.Visible.right,
                                              where.Visible.bottom}};
        HalMimeHypercall(MIME_HC_WIN_LAYER, (ULONG64)MmGetPhysicalAddress(g_layer).QuadPart, 0, 0, 0, 0, 0, nullptr);
    }
}

static void send_layers(WND *top, WND *w)
{
    send_layer(w, FALSE);
    for (WND *c = w->FirstChild; c; c = c->Next)
        send_layers(top, c);
}

/* the surface of a top level window is about to be shown: its layers give up what was drawn over since. */
void ScreenHeldFlush(WND *top)
{
    WND *told[RTL_NUMBER_OF(g_held)];
    ULONG count = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_held_lock, &irql);
    for (LONG i = ScreenHeldCount - 1; i >= 0; i--) {
        HELD *h = &g_held[i];
        if (h->Top != top)
            continue;
        if (h->Grew && KeQueryInterruptTime() >= h->Until)
            h->Left = h->Now;
        if (same_rect(&h->Left, &h->Shown))
            continue;
        h->Shown = h->Left;
        told[count++] = h->Window;
        if (same_rect(&h->Shown, &h->Now)) {
            h->Window->LayerShown = h->Now;
            held_drop(h);
        }
    }
    KeReleaseSpinLock(&g_held_lock, irql);
    for (ULONG i = 0; i < count; i++)
        send_layer(told[i], TRUE);
}

static BOOLEAN tree_has_connection(const WND *w)
{
    if ((w->Flags & WNDF_PRESENTS) && has_connection(w))
        return TRUE;
    for (const WND *c = w->FirstChild; c; c = c->Next)
        if (tree_has_connection(c))
            return TRUE;
    return FALSE;
}

/*
 * a window moved, changed size or shown, hidden or restacked: the windows of its tree that present are told to the host
 * again, which keeps their layers over the right parts of the window between their presents.
 */
void ScreenLayersChanged(WND *w)
{
    if (!VkProcessCount)
        return;
    WND *top = WndTopLevel(w);
    if (top && top != WndDesktop() && (top->Flags & WNDF_HOST_WINDOW) && tree_has_connection(top))
        send_layers(top, top);
}
