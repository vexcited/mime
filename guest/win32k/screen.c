/*
 * the screen as programs read it. windows are not drawn on a shared
 * screen here, the host shows each top level window on its own, so the
 * screen and desktop dcs draw on a surface of the display's size that
 * is composed when something reads it: the desktop's background color,
 * then the visible top level windows from the bottom of the z order up,
 * each window's surface at its place (layered and shaped ones blended
 * through their composed copy), and over a window's client area the
 * last image presented to it through vulkan, which the host keeps.
 */
#include "win32k.h"

static SURFACE *g_screen;
static MimeWinReadback *g_readback;
static PUCHAR g_pixels;
static SIZE_T g_pixels_capacity;

/* the surface screen dcs draw on, the display's size. */
SURFACE *ScreenSurface(void)
{
    if (g_screen && (g_screen->Width != W32Display.Width || g_screen->Height != W32Display.Height)) {
        if (g_screen->Header.SelectCount)
            g_screen->Flags |= SURF_RETIRED;
        else
            SurfaceDestroy(g_screen);
        g_screen = NULL;
    }
    if (!g_screen)
        g_screen = SurfaceCreate(W32Display.Width, W32Display.Height, 32, SURF_TOPDOWN);
    return g_screen;
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

/*
 * the window's last vulkan image into g_pixels, its size and the client
 * rectangle it showed in, relative to the window; false when it has none.
 */
static BOOLEAN read_presented(WND *w, ULONG *width, ULONG *height, RECT *client)
{
    if (!g_readback && !(g_readback = MmAllocateContiguousPages(4096)))
        return FALSE;
    for (int attempt = 0; attempt < 2; attempt++) {
        MimeWinReadback *r = g_readback;
        memset(r, 0, sizeof(*r));
        r->window_id = (uint32_t)(ULONG_PTR)WndHandle(w);
        /* first the size, then the pixels into a buffer that holds them. */
        if (HalMimeHypercall(MIME_HC_WIN_READBACK, (ULONG64)MmGetPhysicalAddress(r).QuadPart, 0, 0, 0, 0, 0, NULL))
            return FALSE;
        SIZE_T need = (SIZE_T)r->width * r->height * 4;
        if (!need)
            return FALSE;
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
        if (HalMimeHypercall(MIME_HC_WIN_READBACK, (ULONG64)MmGetPhysicalAddress(r).QuadPart, 0, 0, 0, 0, 0, NULL))
            return FALSE;
        /* a present in between may have changed the size: once more. */
        if (r->width != want_w || r->height != want_h)
            continue;
        *width = r->width;
        *height = r->height;
        *client = (RECT){r->client.left, r->client.top, r->client.right, r->client.bottom};
        return TRUE;
    }
    return FALSE;
}

/* a window's surface over the screen at its place, within area. */
static void compose_window(SURFACE *screen, const RECT *area, WND *w)
{
    SURFACE *s = w->Present ? w->Present : w->Surface;
    RECT r;
    if (!s || !RectIntersect(&r, area, &w->WindowRect))
        return;
    /* plain windows are opaque whatever alpha gdi left; layered and shaped ones have theirs. */
    BOOLEAN blend = w->Present || (w->LayerFlags & MIME_LAYER_UPDATE);
    LONG ox = w->WindowRect.left, oy = w->WindowRect.top;
    for (LONG y = r.top; y < r.bottom; y++) {
        LONG sy = y - oy;
        if (sy < 0 || sy >= s->Height)
            continue;
        const ULONG *src = (const ULONG *)(s->Bits + (LONG_PTR)sy * s->Stride);
        ULONG *dst = (ULONG *)(screen->Bits + (LONG_PTR)y * screen->Stride);
        for (LONG x = r.left; x < r.right; x++) {
            LONG sx = x - ox;
            if (sx < 0 || sx >= s->Width)
                continue;
            dst[x] = blend ? over(src[sx], dst[x]) : src[sx] | 0xFF000000;
        }
    }

    /* the client area shows the last vulkan present, stretched as the host stretches it. */
    ULONG width, height;
    RECT client;
    if (!read_presented(w, &width, &height, &client))
        return;
    RECT c = {ox + client.left, oy + client.top, ox + client.right, oy + client.bottom};
    LONG cw = c.right - c.left, ch = c.bottom - c.top;
    if (cw <= 0 || ch <= 0 || !RectIntersect(&r, &r, &c))
        return;
    for (LONG y = r.top; y < r.bottom; y++) {
        const ULONG *src = (const ULONG *)(g_pixels + (SIZE_T)((y - c.top) * (LONG)height / ch) * width * 4);
        ULONG *dst = (ULONG *)(screen->Bits + (LONG_PTR)y * screen->Stride);
        for (LONG x = r.left; x < r.right; x++)
            dst[x] = src[(x - c.left) * (LONG)width / cw] | 0xFF000000;
    }
}

/*
 * composes the screen within area, in screen coordinates, before a read.
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
    ULONG background = ColorToPixel(MetricsSysColor(COLOR_BACKGROUND)) | 0xFF000000;
    for (LONG y = r.top; y < r.bottom; y++) {
        ULONG *dst = (ULONG *)(screen->Bits + (LONG_PTR)y * screen->Stride);
        for (LONG x = r.left; x < r.right; x++)
            dst[x] = background;
    }
    /* the top level windows, top of the z order first: drawn from the last. */
    WND *list[1024];
    ULONG count = 0;
    for (WND *w = WndDesktop()->FirstChild; w && count < RTL_NUMBER_OF(list); w = w->Next)
        list[count++] = w;
    while (count--) {
        WND *w = list[count];
        if (!WndIsVisible(w) || (w->Style & WS_MINIMIZE))
            continue;
        compose_window(screen, &r, w);
    }
}
