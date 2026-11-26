#include "../vulkan/display.h"
#include "../vulkan/gamma.h"
#include "ui_internal.h"
#include "wayland.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>

#define MAX_WINDOWS 256
#define MAX_VK_LAYERS 8

/* where the guest says a window that presents lies, in the screen pixels of the guest relative to its window. */
typedef struct LayerPlace {
    bool set;
    MimeRect client, visible;
    uint32_t order, stamp;
    /* what of the visible part other windows of the guest cover, and the count of the notice that told it. */
    uint32_t hidden_count, hidden_stamp;
    MimeRect hidden[MIME_LAYER_HIDDEN_MAX];
} LayerPlace;

/* a frame of a layer under its window, an axis at a time, in the same pixels. */
typedef struct LayerFrame {
    int32_t off[2];   /* the image's origin from the surface's. */
    int32_t lit[2];   /* where the surface starts to hold what the window shows, clear before it. */
    int32_t reach[2]; /* how far from its origin the surface holds what the window shows. */
    int32_t ext[2];   /* its size. */
} LayerFrame;

/*
 * a surface for each window of the guest that presents, stacked by the guest's paint order. the one of a top level
 * window lies over the window. the one of a child lies under the window, whose own pixels are drawn clear where the
 * child shows: the edge of the child is then part of the same frame as what the guest painted around it, as it is on
 * Windows, where both are pixels of one surface. a layer over the window is cut by its own size, which is the state
 * of another surface and lands a frame of the display apart from the window's pixels.
 */
typedef struct VkLayer {
    bool used;
    uint32_t source; /* the guest window it presents for. */
    uint32_t order;  /* place in the paint order of the window's tree, the front ones last. */
    struct wl_surface *surface;
    struct wl_subsurface *subsurface; /* none while nothing of it shows. */
    uint32_t users; /* presenters attached to it. one that goes may be detached after its successor attached. */
    struct wp_viewport *viewport;
    MimeRect place; /* where the subsurface is, in surface coordinates. */
    uint32_t frames;
    bool top;   /* of the top level window itself, always over the window. */
    /*
     * of a layer over the window, with the window's lock. on Windows a swapchain is copied into the surface of its
     * window (dxgi takes it with DwmpDxGetWindowSharedSurface and clamps where it copies to its size), so nothing of
     * it ever shows past the window. here the image is a surface of its own, which keeps the size of its last frame
     * while the window gets smaller: until the next frame comes it is cut where the window ends.
     */
    int32_t next_at[2], next_size[2], next_pixels[2]; /* of the frame that was sized last, in surface coordinates and in pixels. */
    int32_t have_at[2], have_size[2], have_pixels[2]; /* of the frame the display has. */
    int32_t cut_size[2];   /* what the frame the display has is cut to, 0 while it shows whole. */
    bool under; /* lies under the window. what follows is of such a layer, under the window's lock. */
    bool gone;  /* its presenter left: it goes once the window closed over it. */
    LayerPlace told, shown; /* the last place the guest told, and the one the window's pixels on screen go with. */
    bool waits;             /* the place told waits for the pixels of the window that were painted around it. */
    uint64_t told_ms;
    int32_t at[2];          /* where the surface starts as of the window's last frame. */
    int32_t cut[2];         /* how far from there a frame of the window that may be on screen is clear. */
    int32_t cut_next[2];
    uint64_t closed_ms;     /* when the window last closed over it. */
    LayerFrame framed, covered; /* the frame the presenter last sized, and the last one the display has. */
    uint32_t framed_serial;
    int32_t dest[2];        /* the size last given to the viewport, in surface coordinates. */
} VkLayer;

typedef struct UiWindow {
    uint32_t id;
    bool used;
    MimeWlWindow *window;
    uint64_t draw_key; /* what the frame on screen was laid out by, see draw. */
    MimeWinConfig config;
    bool configured;
    bool shown;
    bool popup;          /* created as a popup of its owner. */
    struct wl_subsurface *panel; /* and as a subsurface of it, which the compositor stacks with the owner. */
    uint32_t owner;
    bool fullscreen;
    bool vk;       /* a Vulkan presenter shows the client area. */
    VkLayer vk_layers[MAX_VK_LAYERS];
    bool state_pending;
    uint32_t state_request;
    int state_command;
    uint32_t display;      /* the display last reported to the guest. */
    bool maximized;        /* the compositor has the window maximized. */
    bool says_maximized;   /* the compositor calls it maximized whatever its state, see set_maximized. */
    uint64_t shown_ms;     /* when the host window was last shown. */
    bool bogus_size;       /* the compositor gave a size no display could show, which is not acted on. */
    int geometry_w, geometry_h; /* the window geometry last given to the compositor, in surface coordinates. */
    int want_w, want_h;    /* the last size asked of the compositor, in surface coordinates. */
    uint64_t want_ms;      /* time of the last size request. */
    uint64_t fullscreen_ms; /* when the guest last asked for or left fullscreen. */
    uint64_t interactive_until; /* the compositor sizes the window under the pointer, as long as sizes come. */
    bool self_sized; /* its program sizes it under the pointer with no sizing frame, so the compositor may too. */
    bool size_recheck;     /* check the size after the guest request has had time to complete. */
    double popup_scale;    /* a popup is shown at the scale of its owner. */
    char want_app_id[200]; /* requested application id and the id currently on the toplevel. */
    char set_app_id[200];
    double wheel_x, wheel_y;
    double motion_x, motion_y; /* fractions of guest pixels moved, kept for the next motion. */

    pthread_mutex_t lock;
    uint8_t *pixels; /* the latest guest surface, copied by the presenting vCPU. */
    size_t pixels_room; /* its bytes. a window that is dragged to a size takes a new size with each move of the pointer. */
    bool content_due;   /* the guest took a size while a border is dragged, its pixels of that size are to come. */
    uint32_t drag_edge; /* the WMSZ_* edge the pointer drags, as the guest asked. */
    bool size_answered; /* the guest gave the window a size since it was last told one. */
    uint64_t commit_ms; /* when the window last drew a frame. */
    bool hold_timer;    /* a frame that waits for the guest is drawn without it once the wait is over. */
    bool drag_live;     /* the guest waits to be told that the drag of the border ended. */
    bool drag_seen;     /* the compositor said that it sizes the window. */
    uint64_t drag_ms;   /* when the drag was asked for. */
    uint32_t width, height;
    uint32_t pixel_width, pixel_height;
    int32_t logical_width, logical_height; /* the host window in surface coordinates. */
    MimeRect dirty;
    bool has_dirty;
    bool present_queued;
    bool reap; /* a layer that is gone has nothing on screen left, see present_main. */
    MimeWinConfig pending_config;
    bool config_queued;
} UiWindow;

static UiWindow g_windows[MAX_WINDOWS];
/* guest processors take it, so the main thread never waits for the guest with it. */
static pthread_mutex_t g_windows_lock = PTHREAD_MUTEX_INITIALIZER;
static MimeUiInputSink g_sink;
static void *g_sink_ctx;
static bool g_video;          /* the compositor is there; without it the display is headless. */
static pthread_t g_main_thread;
static double g_scale = 1.0;  /* guest pixels per surface coordinate of the primary display, the system's. */
static MimeDisplayInfo g_display;
static void raise_window(UiWindow *w);
static bool g_says_maximized; /* the compositor calls every window maximized, see set_maximized. */

/* the displays of the host, the primary first, in the guest's virtual screen. */
typedef struct HostMonitor {
    uint32_t id;
    double scale;
    MimeMonitor info;
} HostMonitor;

static HostMonitor g_monitors[MIME_MAX_MONITORS];
static uint32_t g_monitor_count;
static pthread_mutex_t g_monitor_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_cursor;
static bool g_stop;
static pthread_mutex_t g_stop_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_stop_cond = PTHREAD_COND_INITIALIZER;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void emit(MimeInputEvent *ev)
{
    ev->time_ms = now_ms();
    if (g_sink)
        g_sink(g_sink_ctx, ev);
}

void mime_ui_emit(MimeInputEvent *ev)
{
    emit(ev);
}

bool mime_ui_has_video(void)
{
    return g_video;
}

/* runs fn on the main thread, at once when already there. */
static void on_main(void (*fn)(void *), void *arg)
{
    if (!g_video)
        return;
    if (pthread_equal(pthread_self(), g_main_thread)) {
        fn(arg);
        return;
    }
    mime_wl_post(fn, arg);
}

bool mime_ui_call_main(void (*fn)(void *), void *arg)
{
    if (!g_video)
        return false;
    on_main(fn, arg);
    return true;
}

static UiWindow *find_window(uint32_t id)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].id == id)
            return &g_windows[i];
    return nullptr;
}

static UiWindow *ui_of(const MimeWlWindow *window)
{
    return window ? find_window((uint32_t)(uintptr_t)mime_wl_window_user(window)) : nullptr;
}

static bool rect_empty(MimeRect r)
{
    return r.right <= r.left || r.bottom <= r.top;
}

static MimeRect rect_and(MimeRect a, MimeRect b)
{
    MimeRect r = {MIME_MAX(a.left, b.left), MIME_MAX(a.top, b.top), MIME_MIN(a.right, b.right),
                  MIME_MIN(a.bottom, b.bottom)};
    return rect_empty(r) ? (MimeRect){} : r;
}

/* visible rectangle relative to the guest window. */
static MimeRect visible_part(const MimeWinConfig *c)
{
    MimeRect v = c->visible;
    if (v.right <= v.left || v.bottom <= v.top)
        v = (MimeRect){0, 0, c->rect.right - c->rect.left, c->rect.bottom - c->rect.top};
    return v;
}

/* host window bounds: visible rectangle plus resize border. */
static MimeRect host_part(const MimeWinConfig *c)
{
    MimeRect v = visible_part(c), in = c->input;
    if (in.right <= in.left || in.bottom <= in.top)
        return v;
    return (MimeRect){MIME_MIN(v.left, in.left), MIME_MIN(v.top, in.top), MIME_MAX(v.right, in.right),
                      MIME_MAX(v.bottom, in.bottom)};
}

/*
 * maps guest window coordinates to surface coordinates with a scale and offset. ordinary windows use the display
 * scale. fullscreen windows preserve their aspect ratio and fill unused space with black, like a monitor
 * displaying a smaller mode.
 */
typedef struct Fit {
    double scale;
    double x, y;
} Fit;

/* guest pixels per surface coordinate, using the program's DPI. */
static double config_scale(const MimeWinConfig *c)
{
    return c->dpi ? c->dpi / 96.0 : g_scale;
}

/*
 * host pixels per surface pixel: a window that does not know the display's DPI draws at a lower one and is stretched,
 * as Windows' compositor does.
 */
static double surface_stretch(const MimeWinConfig *c)
{
    return c->surface_dpi && c->dpi && c->dpi > c->surface_dpi ? (double)c->dpi / c->surface_dpi : 1.0;
}

/*
 * a tiling compositor sizes a window below the smallest size its program accepts, which the guest refuses: the window
 * stays larger than its host window for good.
 */
static bool held_at_minimum(const UiWindow *w, double gw, double gh)
{
    const MimeWinConfig *c = &w->config;
    if (w->popup || (c->flags & MIME_WIN_MINIMIZED))
        return false;
    double scale = config_scale(c);
    bool larger = gw / scale > w->logical_width + 1 || gh / scale > w->logical_height + 1;
    bool smallest = (c->min_width > 0 && c->rect.right - c->rect.left <= c->min_width) ||
                    (c->min_height > 0 && c->rect.bottom - c->rect.top <= c->min_height);
    return larger && smallest;
}

static Fit window_fit(const UiWindow *w)
{
    Fit fit = {1.0 / config_scale(&w->config), 0, 0};
    if (w->popup && w->popup_scale > 0)
        fit.scale = w->popup_scale;
    if (w->popup || w->logical_width <= 0 || w->logical_height <= 0)
        return fit;
    MimeRect host = host_part(&w->config);
    double gw = host.right - host.left, gh = host.bottom - host.top;
    if (gw <= 0 || gh <= 0 || !(w->fullscreen || held_at_minimum(w, gw, gh)))
        return fit;
    fit.scale = fmin(w->logical_width / gw, w->logical_height / gh);
    fit.x = (w->logical_width - gw * fit.scale) / 2;
    fit.y = (w->logical_height - gh * fit.scale) / 2;
    return fit;
}

static bool point_outside(const UiWindow *w, float x, float y)
{
    return w->logical_width > 0 && (x < 0 || y < 0 || x >= (float)w->logical_width || y >= (float)w->logical_height);
}

/* converts host window coordinates to guest screen pixels. */
static void window_point_to_guest(UiWindow *w, float x, float y, int32_t *gx, int32_t *gy)
{
    MimeRect host = host_part(&w->config);
    Fit fit = window_fit(w);
    *gx = w->config.rect.left + host.left + (int32_t)lround((x - fit.x) / fit.scale);
    *gy = w->config.rect.top + host.top + (int32_t)lround((y - fit.y) / fit.scale);
}

/* text entry rectangle in screen pixels, shown by the window with keyboard focus. */
static struct {
    bool valid;
    uint32_t window;
    MimeRect screen;
    int32_t cursor;
} g_text_area;

/*
 * main thread: applies the text entry rectangle to the focused window, or its original window if none has
 * focus.
 */
static void apply_text_area(void)
{
    if (!g_text_area.valid)
        return;
    UiWindow *w = nullptr;
    w = ui_of(mime_wl_keyboard_focus());
    if (!w || !w->window)
        w = find_window(g_text_area.window);
    if (!w || !w->window)
        return;
    MimeRect host = host_part(&w->config);
    Fit fit = window_fit(w);
    int32_t left = w->config.rect.left + host.left, top = w->config.rect.top + host.top;
    MimeRect r = g_text_area.screen;
    /* the caret is an offset from the left of the area. */
    int caret = (int)lround((g_text_area.cursor - r.left) * fit.scale);
    mime_wl_window_text_area(w->window, (int)lround(fit.x + (r.left - left) * fit.scale) + caret,
                             (int)lround(fit.y + (r.top - top) * fit.scale), 1,
                             MIME_MAX(1, (int)lround((r.bottom - r.top) * fit.scale)));
}

/* the area is relative to the window it was made for, which may not be the one the keyboard is in. */
/* the scale of a buffer the compositor shows as is: whole, since a drag icon has no fractional scale. */
int mime_ui_buffer_scale(void)
{
    return g_scale >= 1.5 ? (int)lround(g_scale) : 1;
}

void mime_ui_set_text_area(uint32_t id, MimeRect r, int32_t cursor)
{
    UiWindow *w = find_window(id);
    if (!w || !w->window)
        return;
    g_text_area.window = id;
    g_text_area.screen = (MimeRect){r.left + w->config.rect.left, r.top + w->config.rect.top,
                                    r.right + w->config.rect.left, r.bottom + w->config.rect.top};
    g_text_area.cursor = cursor + w->config.rect.left;
    g_text_area.valid = true;
    apply_text_area();
}

void mime_ui_clear_composition(uint32_t id)
{
    mime_wl_text_reset();
}

uint32_t mime_ui_modifiers(void)
{
    /* the bits of MIME_WL_MOD_* are those of MIME_MOD_*. */
    return mime_wl_mods();
}

void *mime_ui_window_surface(uint32_t id)
{
    UiWindow *w = find_window(id);
    if (!w || !w->window)
        return nullptr;
    return mime_wl_window_surface(w->window);
}

bool mime_ui_surface_point(void *surface, double x, double y, uint32_t *window, int32_t *gx, int32_t *gy)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        UiWindow *w = &g_windows[i];
        if (!w->used || !w->window || !w->shown)
            continue;
        void *main_surface = mime_wl_window_surface(w->window);
        double ox = 0, oy = 0;
        if (surface == main_surface) {
        } else {
            const VkLayer *layer = nullptr;
            for (int k = 0; k < MAX_VK_LAYERS; k++)
                if (w->vk_layers[k].used && surface == (void *)w->vk_layers[k].surface)
                    layer = &w->vk_layers[k];
            if (!layer)
                continue;
            ox = layer->place.left;
            oy = layer->place.top;
        }
        *window = w->id;
        window_point_to_guest(w, (float)(x + ox), (float)(y + oy), gx, gy);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ display. */

typedef struct Box {
    int x, y, w, h;
} Box;

typedef struct Placed {
    uint32_t id;
    Box bounds, usable; /* in the compositor's coordinates. */
    double scale;
    int32_t width, height; /* guest pixels. */
    int32_t x, y;
    bool placed;
    float refresh;
    char name[32];
    bool hdr;
    MimeMonitorColor color;
} Placed;

static int compare_position(const void *a, const void *b)
{
    const Placed *l = a, *r = b;
    if (l->bounds.x != r->bounds.x)
        return l->bounds.x < r->bounds.x ? -1 : 1;
    return l->bounds.y == r->bounds.y ? 0 : l->bounds.y < r->bounds.y ? -1 : 1;
}

/*
 * arranges displays in guest pixels. each logical size is multiplied by its scale. touching displays stay
 * adjacent, with offsets along their shared edge using the first display's scale. disconnected displays use
 * their own scale for placement.
 */
static void layout_monitors(Placed *d, uint32_t count)
{
    d[0].x = d[0].y = 0;
    d[0].placed = true;
    for (bool progress = true; progress;) {
        progress = false;
        for (uint32_t i = 1; i < count; i++) {
            if (d[i].placed)
                continue;
            for (uint32_t j = 0; j < count && !d[i].placed; j++) {
                const Placed *p = &d[j];
                if (!p->placed)
                    continue;
                Box *m = &d[i].bounds;
                const Box *r = &p->bounds;
                bool vertical = m->y < r->y + r->h && r->y < m->y + m->h;
                bool horizontal = m->x < r->x + r->w && r->x < m->x + m->w;
                if (m->x == r->x + r->w && vertical) {
                    d[i].x = p->x + p->width;
                    d[i].y = p->y + (int32_t)lround((m->y - r->y) * p->scale);
                } else if (m->x + m->w == r->x && vertical) {
                    d[i].x = p->x - d[i].width;
                    d[i].y = p->y + (int32_t)lround((m->y - r->y) * p->scale);
                } else if (m->y == r->y + r->h && horizontal) {
                    d[i].y = p->y + p->height;
                    d[i].x = p->x + (int32_t)lround((m->x - r->x) * p->scale);
                } else if (m->y + m->h == r->y && horizontal) {
                    d[i].y = p->y - d[i].height;
                    d[i].x = p->x + (int32_t)lround((m->x - r->x) * p->scale);
                } else {
                    continue;
                }
                d[i].placed = true;
                progress = true;
            }
        }
    }
    for (uint32_t i = 1; i < count; i++) {
        if (!d[i].placed) {
            d[i].x = (int32_t)lround(d[i].bounds.x * d[i].scale);
            d[i].y = (int32_t)lround(d[i].bounds.y * d[i].scale);
        }
    }
}

static void displays_changed(void *arg);

static void color_changed(void)
{
    on_main(displays_changed, nullptr);
}

static void display_color(struct wl_output *output, Placed *m)
{
    mime_color_output(mime_wl_display(), output, m->name, &m->hdr, &m->color);
}

static void update_display(void)
{
    MimeWlOutput outputs[MIME_MAX_MONITORS];
    uint32_t count = mime_wl_outputs(outputs, MIME_MAX_MONITORS);
    Placed d[MIME_MAX_MONITORS];
    for (uint32_t i = 0; i < count; i++) {
        const MimeWlOutput *o = &outputs[i];
        Placed *m = &d[i];
        memset(m, 0, sizeof(*m));
        m->id = o->id;
        /* the compositor tells nothing of its panels: the work area is the whole output. */
        m->bounds = m->usable = (Box){o->x, o->y, o->width, o->height};
        m->scale = o->scale;
        m->width = (int32_t)lround(m->bounds.w * m->scale);
        m->height = (int32_t)lround(m->bounds.h * m->scale);
        m->refresh = (float)o->refresh;
        strncpy(m->name, o->name, sizeof(m->name) - 1);
        display_color(o->output, m);
    }
    if (!count)
        return;
    /* the primary first, then left to right. */
    qsort(d + 1, count - 1, sizeof(Placed), compare_position);
    layout_monitors(d, count);

    HostMonitor monitors[MIME_MAX_MONITORS];
    memset(monitors, 0, sizeof(monitors));
    for (uint32_t i = 0; i < count; i++) {
        HostMonitor *h = &monitors[i];
        const Placed *m = &d[i];
        h->id = m->id;
        h->scale = m->scale;
        MimeMonitor *o = &h->info;
        o->flags = i == 0 ? MIME_MONITOR_PRIMARY : 0;
        if (m->hdr)
            o->flags |= MIME_MONITOR_HDR;
        o->color = m->color;
        o->dpi = (uint32_t)lround(96.0 * m->scale);
        o->refresh_hz = m->refresh > 0 ? (uint32_t)lround(m->refresh) : 60;
        o->rect = (MimeRect){m->x, m->y, m->x + m->width, m->y + m->height};
        o->work = (MimeRect){m->x + (int32_t)lround((m->usable.x - m->bounds.x) * m->scale),
                             m->y + (int32_t)lround((m->usable.y - m->bounds.y) * m->scale),
                             m->x + (int32_t)lround((m->usable.x + m->usable.w - m->bounds.x) * m->scale),
                             m->y + (int32_t)lround((m->usable.y + m->usable.h - m->bounds.y) * m->scale)};
        mime_utf8_to_utf16(o->name, 31, m->name);
    }
    pthread_mutex_lock(&g_monitor_lock);
    memcpy(g_monitors, monitors, sizeof(monitors));
    g_monitor_count = count;
    pthread_mutex_unlock(&g_monitor_lock);

    const MimeMonitor *p = &monitors[0].info;
    g_scale = monitors[0].scale;
    g_display.width = (uint32_t)(p->rect.right - p->rect.left);
    g_display.height = (uint32_t)(p->rect.bottom - p->rect.top);
    g_display.work_x = (uint32_t)(p->work.left - p->rect.left);
    g_display.work_y = (uint32_t)(p->work.top - p->rect.top);
    g_display.work_width = (uint32_t)(p->work.right - p->work.left);
    g_display.work_height = (uint32_t)(p->work.bottom - p->work.top);
    g_display.dpi = p->dpi;
    g_display.refresh_hz = p->refresh_hz;
}

void mime_ui_get_display(MimeDisplayInfo *info)
{
    if (!g_display.width) {
        g_display = (MimeDisplayInfo){.width = 1920, .height = 1080, .work_width = 1920, .work_height = 1032,
                                      .dpi = 96, .refresh_hz = 60};
        if (g_video)
            update_display();
    }
    *info = g_display;
    info->flags |= MIME_DISPLAY_STRETCH;
    if (g_video && !mime_wl_can_minimize())
        info->flags |= MIME_DISPLAY_NO_MINIMIZE;
}

uint32_t mime_ui_get_monitors(MimeMonitor *monitors, uint32_t capacity)
{
    MimeDisplayInfo ignored;
    mime_ui_get_display(&ignored);
    pthread_mutex_lock(&g_monitor_lock);
    uint32_t count = g_monitor_count;
    for (uint32_t i = 0; i < count && i < capacity; i++)
        monitors[i] = g_monitors[i].info;
    if (!count && capacity) {
        /* no video: the one display the boot information describes. */
        memset(monitors, 0, sizeof(*monitors));
        monitors->flags = MIME_MONITOR_PRIMARY;
        monitors->dpi = g_display.dpi;
        monitors->refresh_hz = g_display.refresh_hz;
        monitors->rect = (MimeRect){0, 0, (int32_t)g_display.width, (int32_t)g_display.height};
        monitors->work = (MimeRect){(int32_t)g_display.work_x, (int32_t)g_display.work_y,
                                    (int32_t)(g_display.work_x + g_display.work_width),
                                    (int32_t)(g_display.work_y + g_display.work_height)};
        count = 1;
    }
    pthread_mutex_unlock(&g_monitor_lock);
    return count < capacity ? count : capacity;
}

/* returns the display with the largest overlap. */
static uint32_t display_of_rect(const MimeRect *r)
{
    uint32_t best = 0;
    int64_t best_area = -1;
    pthread_mutex_lock(&g_monitor_lock);
    for (uint32_t i = 0; i < g_monitor_count; i++) {
        const MimeRect *m = &g_monitors[i].info.rect;
        int64_t w = (int64_t)MIME_MIN(r->right, m->right) - MIME_MAX(r->left, m->left);
        int64_t h = (int64_t)MIME_MIN(r->bottom, m->bottom) - MIME_MAX(r->top, m->top);
        int64_t area = w > 0 && h > 0 ? w * h : 0;
        if (area > best_area) {
            best_area = area;
            best = g_monitors[i].id;
        }
    }
    pthread_mutex_unlock(&g_monitor_lock);
    return best;
}

/* returns -1 when the display is absent from the guest list. */
static int monitor_index(uint32_t id)
{
    int index = -1;
    pthread_mutex_lock(&g_monitor_lock);
    for (uint32_t i = 0; i < g_monitor_count; i++)
        if (g_monitors[i].id == id)
            index = (int)i;
    pthread_mutex_unlock(&g_monitor_lock);
    return index;
}

/* ------------------------------------------------------------------ keyboard. */

typedef struct KeyMap {
    uint8_t vk;
    uint16_t scan; /* set 1, 0x100 marks extended keys. */
} KeyMap;

/* by the key code of linux/input-event-codes.h. */
#define KEY_CODES 256
static const KeyMap g_keymap[KEY_CODES] = {
    [KEY_A] = {'A', 0x1E}, [KEY_B] = {'B', 0x30}, [KEY_C] = {'C', 0x2E},
    [KEY_D] = {'D', 0x20}, [KEY_E] = {'E', 0x12}, [KEY_F] = {'F', 0x21},
    [KEY_G] = {'G', 0x22}, [KEY_H] = {'H', 0x23}, [KEY_I] = {'I', 0x17},
    [KEY_J] = {'J', 0x24}, [KEY_K] = {'K', 0x25}, [KEY_L] = {'L', 0x26},
    [KEY_M] = {'M', 0x32}, [KEY_N] = {'N', 0x31}, [KEY_O] = {'O', 0x18},
    [KEY_P] = {'P', 0x19}, [KEY_Q] = {'Q', 0x10}, [KEY_R] = {'R', 0x13},
    [KEY_S] = {'S', 0x1F}, [KEY_T] = {'T', 0x14}, [KEY_U] = {'U', 0x16},
    [KEY_V] = {'V', 0x2F}, [KEY_W] = {'W', 0x11}, [KEY_X] = {'X', 0x2D},
    [KEY_Y] = {'Y', 0x15}, [KEY_Z] = {'Z', 0x2C},
    [KEY_1] = {'1', 0x02}, [KEY_2] = {'2', 0x03}, [KEY_3] = {'3', 0x04},
    [KEY_4] = {'4', 0x05}, [KEY_5] = {'5', 0x06}, [KEY_6] = {'6', 0x07},
    [KEY_7] = {'7', 0x08}, [KEY_8] = {'8', 0x09}, [KEY_9] = {'9', 0x0A},
    [KEY_0] = {'0', 0x0B},
    [KEY_ENTER] = {0x0D, 0x1C}, [KEY_ESC] = {0x1B, 0x01},
    [KEY_BACKSPACE] = {0x08, 0x0E}, [KEY_TAB] = {0x09, 0x0F},
    [KEY_SPACE] = {0x20, 0x39}, [KEY_MINUS] = {0xBD, 0x0C},
    [KEY_EQUAL] = {0xBB, 0x0D}, [KEY_LEFTBRACE] = {0xDB, 0x1A},
    [KEY_RIGHTBRACE] = {0xDD, 0x1B}, [KEY_BACKSLASH] = {0xDC, 0x2B},
    [KEY_SEMICOLON] = {0xBA, 0x27},
    [KEY_APOSTROPHE] = {0xDE, 0x28}, [KEY_GRAVE] = {0xC0, 0x29},
    [KEY_COMMA] = {0xBC, 0x33}, [KEY_DOT] = {0xBE, 0x34},
    [KEY_SLASH] = {0xBF, 0x35}, [KEY_CAPSLOCK] = {0x14, 0x3A},
    [KEY_F1] = {0x70, 0x3B}, [KEY_F2] = {0x71, 0x3C}, [KEY_F3] = {0x72, 0x3D},
    [KEY_F4] = {0x73, 0x3E}, [KEY_F5] = {0x74, 0x3F}, [KEY_F6] = {0x75, 0x40},
    [KEY_F7] = {0x76, 0x41}, [KEY_F8] = {0x77, 0x42}, [KEY_F9] = {0x78, 0x43},
    [KEY_F10] = {0x79, 0x44}, [KEY_F11] = {0x7A, 0x57}, [KEY_F12] = {0x7B, 0x58},
    [KEY_F13] = {0x7C, 0x64}, [KEY_F14] = {0x7D, 0x65}, [KEY_F15] = {0x7E, 0x66},
    [KEY_F16] = {0x7F, 0x67}, [KEY_F17] = {0x80, 0x68}, [KEY_F18] = {0x81, 0x69},
    [KEY_F19] = {0x82, 0x6A}, [KEY_F20] = {0x83, 0x6B}, [KEY_F21] = {0x84, 0x6C},
    [KEY_F22] = {0x85, 0x6D}, [KEY_F23] = {0x86, 0x6E}, [KEY_F24] = {0x87, 0x76},
    [KEY_SYSRQ] = {0x2C, 0x137}, [KEY_SCROLLLOCK] = {0x91, 0x46},
    [KEY_PAUSE] = {0x13, 0x45}, [KEY_INSERT] = {0x2D, 0x152},
    [KEY_HOME] = {0x24, 0x147}, [KEY_PAGEUP] = {0x21, 0x149},
    [KEY_DELETE] = {0x2E, 0x153}, [KEY_END] = {0x23, 0x14F},
    [KEY_PAGEDOWN] = {0x22, 0x151}, [KEY_RIGHT] = {0x27, 0x14D},
    [KEY_LEFT] = {0x25, 0x14B}, [KEY_DOWN] = {0x28, 0x150}, [KEY_UP] = {0x26, 0x148},
    [KEY_NUMLOCK] = {0x90, 0x145}, [KEY_KPSLASH] = {0x6F, 0x135},
    [KEY_KPASTERISK] = {0x6A, 0x37}, [KEY_KPMINUS] = {0x6D, 0x4A},
    [KEY_KPPLUS] = {0x6B, 0x4E}, [KEY_KPENTER] = {0x0D, 0x11C},
    [KEY_KP1] = {0x61, 0x4F}, [KEY_KP2] = {0x62, 0x50}, [KEY_KP3] = {0x63, 0x51},
    [KEY_KP4] = {0x64, 0x4B}, [KEY_KP5] = {0x65, 0x4C}, [KEY_KP6] = {0x66, 0x4D},
    [KEY_KP7] = {0x67, 0x47}, [KEY_KP8] = {0x68, 0x48}, [KEY_KP9] = {0x69, 0x49},
    [KEY_KP0] = {0x60, 0x52}, [KEY_KPDOT] = {0x6E, 0x53},
    [KEY_102ND] = {0xE2, 0x56}, [KEY_COMPOSE] = {0x5D, 0x15D},
    [KEY_LEFTCTRL] = {0xA2, 0x1D}, [KEY_LEFTSHIFT] = {0xA0, 0x2A}, [KEY_LEFTALT] = {0xA4, 0x38},
    [KEY_LEFTMETA] = {0x5B, 0x15B}, [KEY_RIGHTCTRL] = {0xA3, 0x11D}, [KEY_RIGHTSHIFT] = {0xA1, 0x36},
    [KEY_RIGHTALT] = {0xA5, 0x138}, [KEY_RIGHTMETA] = {0x5C, 0x15C},
};

/*
 * virtual keys windows' layouts give keys that are not letters or digits, by scan code, where they differ from the US
 * positions: kbdfr for AZERTY, kbdgr for QWERTZ and kbduk.
 */
typedef struct LayoutKey {
    uint8_t scan, vk;
} LayoutKey;

static const LayoutKey g_azerty[] = {
    {0x29, 0xDE}, {0x0C, 0xDB}, {0x0D, 0xBB}, {0x1A, 0xDD}, {0x1B, 0xBA}, {0x28, 0xC0},
    {0x2B, 0xDC}, {0x32, 0xBC}, {0x33, 0xBE}, {0x34, 0xBF}, {0x35, 0xDF}, {0x56, 0xE2},
};
static const LayoutKey g_qwertz[] = {
    {0x0C, 0xDB}, {0x0D, 0xDD}, {0x1A, 0xBA}, {0x1B, 0xBB}, {0x27, 0xC0},
    {0x28, 0xDE}, {0x29, 0xDC}, {0x2B, 0xBF}, {0x35, 0xBD}, {0x56, 0xE2},
};
static const LayoutKey g_uk[] = {{0x28, 0xC0}, {0x29, 0xDF}, {0x2B, 0xDE}, {0x56, 0xDC}};

static MimeKeyboardLayout g_layout;
static bool g_layout_known;

/* the character a key types at a level of the layout, 0 for none. */
static uint16_t layout_char(uint32_t code, int level)
{
    uint32_t c = mime_wl_key_char(code, level);
    return c < 0x20 || c == 0x7F || c > 0xFFFF ? 0 : (uint16_t)c;
}

static bool lower_letter(uint16_t c)
{
    return c >= 'a' && c <= 'z';
}

/* reads the layout of the host from the compositor's keymap, and tells the guest when it changed. */
static void update_layout(void)
{
    MimeKeyboardLayout l = {};
    l.language = 0x0409;
    uint32_t by_scan[MIME_KEYBOARD_KEYS] = {};
    for (uint32_t code = 0; code < KEY_CODES; code++) {
        const KeyMap *k = &g_keymap[code];
        if (!k->vk || k->scan >= MIME_KEYBOARD_KEYS || by_scan[k->scan])
            continue;
        by_scan[k->scan] = code;
        l.vk[k->scan] = k->vk;
        l.chars[k->scan][0] = layout_char(code, 0);
        l.chars[k->scan][1] = layout_char(code, 1);
        l.chars[k->scan][2] = layout_char(code, 2);
        if (l.chars[k->scan][2] == l.chars[k->scan][0])
            l.chars[k->scan][2] = 0;
    }
    if (!by_scan[0x10] || !l.chars[0x10][0])
        return;
    const LayoutKey *keys = nullptr;
    size_t count = 0;
    uint16_t (*c)[3] = l.chars;
    if (c[0x10][0] == 'a' && c[0x11][0] == 'z') {
        keys = g_azerty;
        count = MIME_ARRAY_COUNT(g_azerty);
        l.language = c[0x28][0] == 0xF9 ? 0x040C : 0x080C;
    } else if (c[0x15][0] == 'z' && c[0x2C][0] == 'y') {
        keys = g_qwertz;
        count = MIME_ARRAY_COUNT(g_qwertz);
        l.language = 0x0407;
    } else if (c[0x2B][0] == '#' && c[0x03][1] == '"') {
        keys = g_uk;
        count = MIME_ARRAY_COUNT(g_uk);
        l.language = 0x0809;
    }
    for (size_t i = 0; i < count; i++)
        l.vk[keys[i].scan] = keys[i].vk;
    /* a letter key is the virtual key of its letter wherever the layout puts it, like windows layouts. */
    for (int scan = 0; scan < MIME_KEYBOARD_KEYS; scan++) {
        uint16_t ch = c[scan][0];
        if (lower_letter(ch))
            l.vk[scan] = (uint8_t)(ch - 32);
        else if (l.vk[scan] >= 'A' && l.vk[scan] <= 'Z')
            l.vk[scan] = ch == ',' ? 0xBC : ch == '.' ? 0xBE : ch == '-' ? 0xBD : ch == '=' || ch == '+' ? 0xBB : 0xDF;
    }
    if (g_layout_known && !memcmp(&l, &g_layout, sizeof(l)))
        return;
    g_layout = l;
    g_layout_known = true;
    mime_info("keyboard layout %04x, the key at the place of W types %c", l.language,
              c[0x11][0] < 0x80 ? (char)c[0x11][0] : '?');
    MimeInputEvent ev = {.type = MIME_INPUT_KEYMAP};
    emit(&ev);
}

/* plasma keeps the count for its programs in kdeglobals, three when it is not written. gnome has none. */
uint32_t mime_ui_wheel_lines(void)
{
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    if (!desktop || !strstr(desktop, "KDE"))
        return 0;
    char path[1024];
    const char *config = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (config && *config)
        snprintf(path, sizeof(path), "%s/kdeglobals", config);
    else
        snprintf(path, sizeof(path), "%s/.config/kdeglobals", home ? home : "");
    FILE *f = fopen(path, "r");
    if (!f)
        return 3;
    char line[512];
    bool group = false;
    long lines = 3;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '[')
            group = !strncmp(line, "[KDE]", 5);
        else if (group && !strncmp(line, "WheelScrollLines=", 17))
            lines = strtol(line + 17, nullptr, 10);
    }
    fclose(f);
    return lines >= 1 && lines <= 100 ? (uint32_t)lines : 3;
}

bool mime_ui_get_keyboard(MimeKeyboardLayout *out)
{
    if (!g_layout_known)
        return false;
    *out = g_layout;
    return true;
}

/* pending Alt character, discarded if the next event supplies typed text. */
static MimeInputEvent g_alt_char;
static bool g_alt_char_pending;

static void alt_char_flush(void)
{
    if (g_alt_char_pending) {
        g_alt_char_pending = false;
        emit(&g_alt_char);
    }
}

static void emit_key(UiWindow *w, uint32_t key, bool down, bool repeat)
{
    if (key >= KEY_CODES || !g_keymap[key].vk)
        return;
    const KeyMap *k = &g_keymap[key];
    uint32_t mods = mime_wl_mods();
    MimeInputEvent ev = {};
    ev.type = down ? MIME_INPUT_KEY_DOWN : MIME_INPUT_KEY_UP;
    ev.window_id = w->id;
    ev.vk = k->vk;
    /* letter keys take the virtual key of the letter they type in the host layout, like windows layouts do. */
    if (g_layout_known && k->scan < MIME_KEYBOARD_KEYS)
        ev.vk = g_layout.vk[k->scan];
    ev.scancode = k->scan;
    ev.modifiers = mods;
    if (repeat)
        ev.flags = 2;
    emit(&ev);
    /*
     * no text is typed while Alt is held. Windows uses WM_SYSCHAR for Alt+Space and Alt+letter menus. a layout may
     * have its third level on Alt, so the character waits until the next event tells whether text follows.
     */
    if (down && (mods & MIME_MOD_ALT) && !(mods & (MIME_MOD_CONTROL | MIME_MOD_WIN))) {
        uint32_t code = mime_wl_key_char(key, mods & MIME_MOD_SHIFT ? 1 : 0);
        if (code >= 0x20 && code < 0x7F) {
            alt_char_flush();
            g_alt_char = (MimeInputEvent){.type = MIME_INPUT_CHAR, .window_id = w->id, .ch = code,
                                          .modifiers = ev.modifiers, .scancode = ev.scancode};
            g_alt_char_pending = true;
        }
    }
}

static void emit_text(UiWindow *w, const char *text)
{
    uint16_t buf[64];
    size_t n = mime_utf8_to_utf16(buf, MIME_ARRAY_COUNT(buf), text);
    for (size_t i = 0; i < n; i++) {
        MimeInputEvent ev = {};
        ev.type = MIME_INPUT_CHAR;
        ev.window_id = w->id;
        ev.ch = buf[i];
        emit(&ev);
    }
}

/* ------------------------------------------------------------------ mouse. */

static void emit_mouse(UiWindow *w, double x, double y, uint32_t type, uint32_t button)
{
    MimeInputEvent ev = {};
    ev.type = type;
    ev.window_id = w->id;
    window_point_to_guest(w, (float)x, (float)y, &ev.x, &ev.y);
    ev.button = button;
    ev.modifiers = mime_wl_mods();
    if (w->window && mime_wl_pointer_locked(w->window))
        ev.flags |= 4;
    if (point_outside(w, (float)x, (float)y))
        ev.flags |= 8;
    emit(&ev);
}

static bool g_pointer_known;
static int32_t g_pointer_x, g_pointer_y;

/* motion in guest pixels; locked, the device's own counts, as raw input reports them. */
static void emit_motion(UiWindow *w, double x, double y, double dx, double dy)
{
    MimeInputEvent ev = {};
    ev.type = MIME_INPUT_MOUSE_MOVE;
    ev.window_id = w->id;
    ev.modifiers = mime_wl_mods();
    double scale = 1.0;
    if (w->window && mime_wl_pointer_locked(w->window))
        ev.flags |= 4;
    else {
        window_point_to_guest(w, (float)x, (float)y, &ev.x, &ev.y);
        scale = window_fit(w).scale;
        if (point_outside(w, (float)x, (float)y))
            ev.flags |= 8;
    }
    w->motion_x += dx / scale;
    w->motion_y += dy / scale;
    ev.dx = (int32_t)w->motion_x;
    ev.dy = (int32_t)w->motion_y;
    w->motion_x -= ev.dx;
    w->motion_y -= ev.dy;
    if ((ev.flags & 4) && !ev.dx && !ev.dy)
        return;
    /* a window that appears under a pointer at rest reports it again, which is no move, as Windows posts none. */
    if (!(ev.flags & 4)) {
        if (g_pointer_known && ev.x == g_pointer_x && ev.y == g_pointer_y)
            return;
        g_pointer_known = true;
        g_pointer_x = ev.x;
        g_pointer_y = ev.y;
    }
    emit(&ev);
}

static uint32_t mouse_button(uint32_t button)
{
    switch (button) {
    case BTN_LEFT: return MIME_MOUSE_LEFT;
    case BTN_RIGHT: return MIME_MOUSE_RIGHT;
    case BTN_MIDDLE: return MIME_MOUSE_MIDDLE;
    case BTN_SIDE: return MIME_MOUSE_X1;
    case BTN_EXTRA: return MIME_MOUSE_X2;
    default: return 0;
    }
}

/* in notches, up and right positive. */
static void emit_wheel(UiWindow *w, double x, double y, double wheel_x, double wheel_y)
{
    MimeInputEvent ev = {};
    ev.type = MIME_INPUT_MOUSE_WHEEL;
    ev.window_id = w->id;
    window_point_to_guest(w, (float)x, (float)y, &ev.x, &ev.y);
    ev.modifiers = mime_wl_mods();
    /* fractions of a notch from smooth scrolling add up to whole wheel deltas of 120 per notch. */
    w->wheel_y += wheel_y * 120.0;
    w->wheel_x += wheel_x * 120.0;
    int32_t dy = (int32_t)w->wheel_y, dx = (int32_t)w->wheel_x;
    w->wheel_y -= dy;
    w->wheel_x -= dx;
    if (dy) {
        ev.wheel = dy;
        emit(&ev);
    }
    if (dx) {
        ev.wheel = dx;
        ev.flags = 1;
        emit(&ev);
    }
}

/* ------------------------------------------------------------------ windows. */

/*
 * a size past twice the largest display, or past 16384, is none a window should have. kwin gave such sizes, 100000
 * pixels more than the window, while the window geometry was left to the extent of the surfaces, see set_geometry.
 * a buffer of that size is tens of gigabytes, and the host runs out within a second.
 */
static int sane_limit(void)
{
    MimeWlOutput outputs[MIME_MAX_MONITORS];
    uint32_t count = mime_wl_outputs(outputs, MIME_MAX_MONITORS);
    int largest = 0;
    for (uint32_t i = 0; i < count; i++)
        largest = MIME_MAX(largest, (int)lround(MIME_MAX(outputs[i].width, outputs[i].height) * outputs[i].scale));
    return MIME_MIN(MIME_MAX(largest * 2, 4096), 16384);
}

static bool sane_size(int width, int height)
{
    int limit = sane_limit();
    return width <= limit && height <= limit;
}

/*
 * a host window tells the compositor its own rectangle. without it the window is the extent of the surface and its
 * subsurfaces, and a layer that reaches past the window would make the compositor configure it larger. it lands with
 * the next commit.
 */
static void set_geometry(UiWindow *w)
{
    if (!w->window)
        return;
    int width = 0, height = 0;
    mime_wl_window_get_size(w->window, &width, &height);
    if (width <= 0 || height <= 0 || !sane_size(width, height) || (width == w->geometry_w && height == w->geometry_h))
        return;
    w->geometry_w = width;
    w->geometry_h = height;
    mime_wl_window_geometry(w->window, width, height);
}

static void update_pixel_size(UiWindow *w)
{
    int width = 0, height = 0, logical_width = 0, logical_height = 0;
    if (w->window) {
        mime_wl_window_get_pixels(w->window, &width, &height);
        mime_wl_window_get_size(w->window, &logical_width, &logical_height);
    }
    bool bogus = !sane_size(width, height);
    if (bogus && !w->bogus_size)
        mime_info("window %u: the compositor sized it %dx%d, which no display shows, ignored", w->id, width, height);
    w->bogus_size = bogus;
    if (bogus)
        return;
    set_geometry(w);
    pthread_mutex_lock(&w->lock);
    w->pixel_width = (uint32_t)MIME_MAX(width, 0);
    w->pixel_height = (uint32_t)MIME_MAX(height, 0);
    w->logical_width = logical_width;
    w->logical_height = logical_height;
    pthread_mutex_unlock(&w->lock);
}

/* shows the host window, which is a new one to the compositor each time. */
static void show_host(UiWindow *w)
{
    mime_wl_window_show(w->window);
    w->geometry_w = w->geometry_h = 0;
    set_geometry(w);
}

static void destroy_layers(UiWindow *w);

static bool has_open_popup(const UiWindow *w)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].popup && g_windows[i].window && g_windows[i].shown &&
            g_windows[i].owner == w->id)
            return true;
    return false;
}

static void destroy_host_window(UiWindow *w)
{
    if (w->vk) {
        mime_vk_window_lost(w->id);
        destroy_layers(w);
        w->vk = false;
    }
    mime_gpu_teardown_enter();
    MimeWlWindow *window = w->window;
    w->window = nullptr;
    if (w->panel) {
        wl_subsurface_destroy(w->panel);
        w->panel = nullptr;
    }
    if (window)
        mime_wl_window_destroy(window);
    mime_gpu_teardown_leave();
    w->display = 0;
    w->draw_key = 0;
}

/* where a popup goes in its owner's host window, which shows the owner through its fit. */
static void popup_place(const UiWindow *owner, const MimeWinConfig *cfg, int *x, int *y)
{
    MimeRect host = host_part(cfg), oh = host_part(&owner->config);
    Fit fit = window_fit(owner);
    *x = (int)lround(fit.x + (cfg->rect.left + host.left - owner->config.rect.left - oh.left) * fit.scale);
    *y = (int)lround(fit.y + (cfg->rect.top + host.top - owner->config.rect.top - oh.top) * fit.scale);
}

static UiWindow *popup_anchor(MimeWlWindow *focus, const UiWindow *w, const MimeRect *rect, bool inside)
{
    UiWindow *a = ui_of(focus);
    while (a && a->popup)
        a = a->owner ? find_window(a->owner) : nullptr;
    if (!a || a == w || !a->window || !a->shown || (a->config.flags & MIME_WIN_MINIMIZED))
        return nullptr;
    const MimeRect *r = &a->config.rect;
    if (inside && (rect->left >= r->right || rect->right <= r->left || rect->top >= r->bottom || rect->bottom <= r->top))
        return nullptr;
    return a;
}

/*
 * the window a popup hangs from, none for a window that is no popup. a wayland compositor places a window with no
 * parent where it likes, a tiling one in a tile, and a program whose windows are owned by a hidden one, or by none,
 * has its menus and tips land there. such a popup hangs from the window it is over, the one the pointer is in first.
 */
static UiWindow *popup_owner(const UiWindow *w, const MimeWinConfig *cfg)
{
    /*
     * a program that draws its own menus and lists has them in bare topmost windows it owns, with none of the tool
     * styles. a wayland compositor would place such a top level window itself, hyprland in the middle of the screen.
     */
    bool bare = cfg->owner_id && (cfg->flags & (MIME_WIN_BARE | MIME_WIN_FULLSCREEN)) == MIME_WIN_BARE;
    bool menu = bare && (cfg->flags & MIME_WIN_TOPMOST);
    if (!(cfg->flags & MIME_WIN_TOOL) && !bare)
        return nullptr;
    UiWindow *owner = cfg->owner_id ? find_window(cfg->owner_id) : nullptr;
    if (owner && owner->window && owner->shown)
        return owner;
    /*
     * a bare window that is not topmost is a panel its program lays on its owner, as the command line of AutoCAD. one
     * whose owner does not show is a window of its own, as the borderless main form of a VCL program.
     */
    if (!(cfg->flags & MIME_WIN_TOOL) && !menu)
        return nullptr;
    /* the one found first is kept. */
    UiWindow *kept = w->popup && w->window && w->owner ? find_window(w->owner) : nullptr;
    if (kept && kept->window && kept->shown)
        return kept;
    MimeWlWindow *mouse = mime_wl_pointer_focus(), *keys = mime_wl_keyboard_focus();
    UiWindow *a = popup_anchor(mouse, w, &cfg->rect, true);
    if (!a)
        a = popup_anchor(keys, w, &cfg->rect, true);
    if (!a)
        a = popup_anchor(mouse, w, &cfg->rect, false);
    if (!a)
        a = popup_anchor(keys, w, &cfg->rect, false);
    return a;
}

static struct wl_surface *host_surface(const UiWindow *w)
{
    return mime_wl_window_surface(w->window);
}

/*
 * a bare window that is not topmost is a panel its program lays on its owner, as the command line of AutoCAD. windows
 * keeps an owned window right over its owner in the order of the screen and under the windows over the owner
 * (ZOrderByOwner in win32kfull). a compositor has its popups over every window, and places a top level window itself:
 * a subsurface is the one surface that is placed by its program and stacked with its parent.
 */
static bool is_panel(const MimeWinConfig *cfg, const UiWindow *owner)
{
    if (!owner || owner->id != cfg->owner_id ||
        (cfg->flags & (MIME_WIN_BARE | MIME_WIN_FULLSCREEN | MIME_WIN_TOPMOST | MIME_WIN_TOOL | MIME_WIN_NOINPUT)) !=
            MIME_WIN_BARE)
        return false;
    return mime_wl_subcompositor() != nullptr;
}

static void place_panel(UiWindow *w, const UiWindow *owner, const MimeWinConfig *cfg)
{
    int x, y;
    popup_place(owner, cfg, &x, &y);
    wl_subsurface_set_position(w->panel, x, y);
    /* the position lands with the owner's next commit. */
    mime_wl_window_commit_state(owner->window);
}

/* menus and tooltips become owner-relative popups on Wayland; other windows become toplevels. */
static bool create_host_window(UiWindow *w, const MimeWinConfig *cfg, UiWindow *owner, bool panel)
{
    MimeRect host = host_part(cfg);
    double scale = config_scale(cfg);
    bool popup = owner != nullptr;
    w->popup_scale = popup ? window_fit(owner).scale : 0;
    double shown = popup ? w->popup_scale : 1.0 / scale;
    int width = MIME_MAX(1, (int)lround((host.right - host.left) * shown));
    int height = MIME_MAX(1, (int)lround((host.bottom - host.top) * shown));
    void *user = (void *)(uintptr_t)w->id;
    if (panel) {
        /* a surface with no role, drawn on and read for its mouse like any other. */
        w->window = mime_wl_window_create(MIME_WL_BARE, nullptr, 0, 0, width, height, user);
        struct wl_surface *surface = w->window ? host_surface(w) : nullptr, *under = host_surface(owner);
        if (surface && under)
            w->panel = wl_subcompositor_get_subsurface(mime_wl_subcompositor(), surface, under);
        if (w->window && !w->panel) {
            mime_wl_window_destroy(w->window);
            w->window = nullptr;
        }
        if (w->panel) {
            wl_subsurface_set_desync(w->panel);
            place_panel(w, owner, cfg);
        }
    } else if (popup) {
        int x, y;
        popup_place(owner, cfg, &x, &y);
        /* neither takes the keyboard: it stays with the window the compositor gave it to, and so does its text input. */
        w->window = mime_wl_window_create((cfg->flags & MIME_WIN_NOINPUT) ? MIME_WL_TOOLTIP : MIME_WL_POPUP, owner->window,
                                          x, y, width, height, user);
    } else {
        w->window = mime_wl_window_create(MIME_WL_TOPLEVEL, nullptr, 0, 0, width, height, user);
    }
    if (!w->window) {
        mime_warn("cannot create a host window");
        return false;
    }
    w->popup = popup;
    w->says_maximized = g_says_maximized && !popup;
    w->owner = popup ? owner->id : cfg->owner_id;
    w->draw_key = 0;
    update_pixel_size(w);
    UiWindow *parent = !popup && cfg->owner_id ? find_window(cfg->owner_id) : nullptr;
    if (parent && parent->window)
        mime_wl_window_parent(w->window, parent->window);
    return true;
}

/* the whole of the window's pixels is to be drawn again, with its lock. */
static void mark_dirty(UiWindow *w)
{
    pthread_mutex_lock(&w->lock);
    w->dirty = (MimeRect){0, 0, (int32_t)w->width, (int32_t)w->height};
    w->has_dirty = w->pixels != nullptr;
    pthread_mutex_unlock(&w->lock);
}

#define MAX_HOLES (MAX_VK_LAYERS * 4 * MIME_LAYER_HIDDEN_MAX)

static int settle_layers(UiWindow *w, MimeRect *holes, bool *again);
static void cut_layers(UiWindow *w);
static void layers_drawn(UiWindow *w);
static void reap_layers(UiWindow *w);
static void present_main(void *arg);

/* how long a layer waits for something that should come with a frame of the window before one is drawn without. */
#define REDRAW_MS 50

/* where the pixels of the guest's surface go in the buffer of the host window, in pixels of each. */
typedef struct Blit {
    const uint8_t *src;
    int src_w, src_h;
    double u0, v0, step_u, step_v; /* the surface pixel under the centre of a buffer pixel is u0 + x * step_u. */
    MimeRect dst;                  /* the buffer pixels the surface covers. */
    bool linear, graded;
    uint8_t table[3][256];
    uint32_t back; /* around the surface. */
} Blit;

static uint64_t mix(uint64_t h, const void *data, size_t size)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < size; i++)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

static inline uint32_t sample(const Blit *b, int x, int y)
{
    x = x < 0 ? 0 : x >= b->src_w ? b->src_w - 1 : x;
    y = y < 0 ? 0 : y >= b->src_h ? b->src_h - 1 : y;
    uint32_t p;
    memcpy(&p, b->src + ((size_t)y * b->src_w + x) * 4, 4);
    return p;
}

static inline uint32_t blend(uint32_t a, uint32_t b, uint32_t t)
{
    uint32_t rb = ((a & 0xFF00FF) * (256 - t) + (b & 0xFF00FF) * t) >> 8 & 0xFF00FF;
    uint32_t ag = ((a >> 8 & 0xFF00FF) * (256 - t) + (b >> 8 & 0xFF00FF) * t) & 0xFF00FF00;
    return rb | ag;
}

/* through the display's ramp when it has one, blue, green, red then alpha in memory. */
static inline uint32_t grade(const Blit *b, uint32_t p)
{
    if (!b->graded)
        return p;
    return (p & 0xFF000000) | (uint32_t)b->table[0][p >> 16 & 0xFF] << 16 | (uint32_t)b->table[1][p >> 8 & 0xFF] << 8 |
           b->table[2][p & 0xFF];
}

/* draws a part of the buffer: the surface where it lies, stretched as the fit has it, and the ground around it. */
static void blit(const Blit *b, const MimeWlFrame *f, MimeRect part)
{
    bool same = fabs(b->step_u - 1.0) < 1e-6 && fabs(b->step_v - 1.0) < 1e-6 && fabs(b->u0 - 0.5 - round(b->u0 - 0.5)) < 1e-3 &&
                fabs(b->v0 - 0.5 - round(b->v0 - 0.5)) < 1e-3;
    int shift_x = (int)lround(b->u0 - 0.5), shift_y = (int)lround(b->v0 - 0.5);
    for (int y = part.top; y < part.bottom; y++) {
        uint32_t *row = (uint32_t *)(void *)(f->pixels + (size_t)y * f->stride);
        bool inside = y >= b->dst.top && y < b->dst.bottom;
        int left = inside ? MIME_MAX(part.left, b->dst.left) : part.right;
        int right = inside ? MIME_MIN(part.right, b->dst.right) : part.right;
        left = MIME_MIN(left, part.right);
        right = MIME_MAX(right, left);
        for (int x = part.left; x < left; x++)
            row[x] = b->back;
        for (int x = right; x < part.right; x++)
            row[x] = b->back;
        if (left >= right)
            continue;
        if (same) {
            /* a pixel for a pixel, the usual case. */
            int sy = y + shift_y;
            int from = MIME_MAX(left, -shift_x), to = MIME_MIN(right, b->src_w - shift_x);
            if (sy < 0 || sy >= b->src_h || from >= to) {
                for (int x = left; x < right; x++)
                    row[x] = grade(b, sample(b, x + shift_x, sy));
                continue;
            }
            for (int x = left; x < from; x++)
                row[x] = grade(b, sample(b, x + shift_x, sy));
            for (int x = to; x < right; x++)
                row[x] = grade(b, sample(b, x + shift_x, sy));
            const uint8_t *in = b->src + ((size_t)sy * b->src_w + (size_t)(from + shift_x)) * 4;
            if (!b->graded) {
                memcpy(row + from, in, (size_t)(to - from) * 4);
            } else {
                uint8_t *out = (uint8_t *)(row + from);
                for (int x = from; x < to; x++, in += 4, out += 4) {
                    out[0] = b->table[2][in[0]];
                    out[1] = b->table[1][in[1]];
                    out[2] = b->table[0][in[2]];
                    out[3] = in[3];
                }
            }
            continue;
        }
        double v = b->v0 + y * b->step_v;
        for (int x = left; x < right; x++) {
            double u = b->u0 + x * b->step_u;
            uint32_t p;
            if (!b->linear) {
                p = sample(b, (int)floor(u), (int)floor(v));
            } else {
                double fu = u - 0.5, fv = v - 0.5;
                int ix = (int)floor(fu), iy = (int)floor(fv);
                uint32_t tx = (uint32_t)((fu - ix) * 256), ty = (uint32_t)((fv - iy) * 256);
                p = blend(blend(sample(b, ix, iy), sample(b, ix + 1, iy), tx),
                          blend(sample(b, ix, iy + 1), sample(b, ix + 1, iy + 1), tx), ty);
            }
            row[x] = grade(b, p);
        }
    }
}

/* an argb color with its alpha applied, as the buffers of the compositor have it. */
static uint32_t premultiplied(uint32_t c)
{
    uint32_t a = c >> 24;
    return a << 24 | ((c >> 16 & 0xFF) * a / 255) << 16 | ((c >> 8 & 0xFF) * a / 255) << 8 | (c & 0xFF) * a / 255;
}

/* the share of pixel x, y inside the circle of radius r around cx, cy, out of 16, from 4 by 4 samples. */
static int disk_cover(int x, int y, double cx, double cy, double r)
{
    int n = 0;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double dx = x + (i + 0.5) / 4 - cx, dy = y + (j + 0.5) / 4 - cy;
            n += dx * dx + dy * dy <= r * r;
        }
    return n;
}

/*
 * the corners of r rounded by radius: what lies out of the round is clear, and the border, premultiplied, is the ring
 * of a pixel along it.
 */
static void round_corners(const MimeWlFrame *f, MimeRect r, MimeRect part, int radius, uint32_t border)
{
    for (int k = 0; k < 4; k++) {
        bool right = k & 1, bottom = k & 2;
        MimeRect box = {right ? r.right - radius : r.left, bottom ? r.bottom - radius : r.top,
                        right ? r.right : r.left + radius, bottom ? r.bottom : r.top + radius};
        double cx = right ? r.right - radius : r.left + radius, cy = bottom ? r.bottom - radius : r.top + radius;
        box = rect_and(box, part);
        for (int y = box.top; y < box.bottom; y++) {
            uint32_t *row = (uint32_t *)(void *)(f->pixels + (size_t)y * f->stride);
            for (int x = box.left; x < box.right; x++) {
                int outer = disk_cover(x, y, cx, cy, radius), inner = border ? disk_cover(x, y, cx, cy, radius - 1) : outer;
                uint32_t p = row[x], out = 0;
                for (int shift = 0; shift < 32; shift += 8) {
                    uint32_t c = ((p >> shift & 0xFF) * inner + (border >> shift & 0xFF) * (outer - inner) + 8) / 16;
                    out |= (c > 255 ? 255 : c) << shift;
                }
                row[x] = out;
            }
        }
    }
}

static void fill(const MimeWlFrame *f, MimeRect r, MimeRect part, uint32_t color)
{
    r = rect_and(r, part);
    for (int y = r.top; y < r.bottom; y++) {
        uint32_t *row = (uint32_t *)(void *)(f->pixels + (size_t)y * f->stride);
        for (int x = r.left; x < r.right; x++)
            row[x] = color;
    }
}

/*
 * whether the pixels of the guest are of the size the guest told for the window, with the window's lock. the pixels
 * come from the processor that painted them and the size through the main thread, one a moment before the other.
 */
static bool pixels_fit(const UiWindow *w)
{
    const MimeWinConfig *c = &w->config;
    double stretch = surface_stretch(c);
    return w->pixels && labs(lround((c->rect.right - c->rect.left) / stretch) - (long)w->width) <= 1 &&
           labs(lround((c->rect.bottom - c->rect.top) / stretch) - (long)w->height) <= 1;
}

/* whether the guest window has the size the host window is drawn at. */
static bool host_fit(const UiWindow *w)
{
    const MimeWinConfig *c = &w->config;
    MimeRect host = host_part(c);
    double shown = 1.0 / config_scale(c);
    double gw = host.right - host.left, gh = host.bottom - host.top;
    return held_at_minimum(w, gw, gh) || (labs(lround(gw * shown) - (long)w->logical_width) <= 1 &&
                                         labs(lround(gh * shown) - (long)w->logical_height) <= 1);
}

static void draw(UiWindow *w);

static void hold_main(void *arg)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window((uint32_t)(uintptr_t)arg);
    pthread_mutex_unlock(&g_windows_lock);
    if (!w)
        return;
    w->hold_timer = false;
    if (w->shown)
        draw(w);
}

/* how long a frame waits for the guest to have drawn itself at the size of a border that is dragged. */
#define HOLD_MS 100

/* draws the visible surface inside the host window, leaving the resize border clear. */
static void draw(UiWindow *w)
{
    if (!w->window || w->bogus_size)
        return;
    /* the size may be newer than the one the window was last told of: its rectangle goes with the buffer of that size. */
    update_pixel_size(w);
    if (w->bogus_size)
        return;
    MimeWlFrame frame;
    pthread_mutex_lock(&w->lock);
    bool drawn = w->pixels != nullptr;
    /*
     * while a border is dragged a frame of a new size waits for the guest to have taken that size and drawn itself
     * at it, as windows holds the frame of a window between GreWindowResizeStarted and GreWindowResizeComplete of
     * each step of its drag loop (xxxDrawDragRectEx), and for no longer than a timer: a guest that does not answer,
     * or answers with another size, is shown as it is.
     */
    bool torn = drawn && mime_wl_window_resizing(w->window) &&
                (!pixels_fit(w) || (!w->size_answered && !host_fit(w))) && now_ms() - w->commit_ms < HOLD_MS;
    if (torn)
        w->present_queued = false;
    pthread_mutex_unlock(&w->lock);
    if (torn) {
        if (!w->hold_timer) {
            w->hold_timer = true;
            mime_wl_timer(HOLD_MS, hold_main, (void *)(uintptr_t)w->id);
        }
        return;
    }
    w->commit_ms = now_ms();
    /* a buffer of a new size takes its time, which a processor of the guest that presents would wait out. */
    if (drawn)
        mime_wl_window_ready(w->window);
    pthread_mutex_lock(&w->lock);
    w->present_queued = false;
    if (!w->pixels) {
        pthread_mutex_unlock(&w->lock);
        /* a window the guest draws nothing in, such as a popup its GPU process fills, is clear, not black. */
        if (!mime_wl_window_attached(w->window) && mime_wl_window_frame(w->window, nullptr, &frame)) {
            memset(frame.pixels, 0, (size_t)frame.stride * (size_t)frame.height);
            mime_wl_window_commit(w->window, &frame, nullptr);
        } else {
            mime_wl_window_commit_state(w->window);
        }
        return;
    }
    double density = w->logical_width > 0 ? (double)w->pixel_width / w->logical_width : 1.0;
    MimeRect vis = visible_part(&w->config), host = host_part(&w->config);
    double stretch = surface_stretch(&w->config);
    /* the buffer is in pixels, the fit in surface coordinates. */
    Fit fit = window_fit(w);
    fit.scale *= density;
    fit.x *= density;
    fit.y *= density;
    double left = fit.x + (vis.left - host.left) * fit.scale, top = fit.y + (vis.top - host.top) * fit.scale;
    double width = (vis.right - vis.left) * fit.scale, height = (vis.bottom - vis.top) * fit.scale;
    Blit b = {.src = w->pixels, .src_w = (int)w->width, .src_h = (int)w->height};
    b.dst = (MimeRect){(int32_t)ceil(left - 0.5), (int32_t)ceil(top - 0.5), (int32_t)ceil(left + width - 0.5),
                       (int32_t)ceil(top + height - 0.5)};
    b.step_u = width > 0 ? (vis.right - vis.left) / stretch / width : 1.0;
    b.step_v = height > 0 ? (vis.bottom - vis.top) / stretch / height : 1.0;
    b.u0 = vis.left / stretch + (0.5 - left) * b.step_u;
    b.v0 = vis.top / stretch + (0.5 - top) * b.step_v;
    /* whole multiples repeat pixels, any other stretch blends them, like the compositor does. */
    b.linear = stretch != floor(stretch);
    /* around a window the fit leaves smaller than the screen, black as a monitor's border. */
    b.back = w->fullscreen ? 0xFF000000 : 0;
    /* what the guest painted since the last frame, in the pixels of the buffer. */
    MimeRect damage = {};
    if (w->has_dirty) {
        MimeRect d = w->dirty;
        int margin = b.linear || fabs(b.step_u - 1.0) > 1e-6 ? 2 : 0;
        damage = (MimeRect){(int32_t)floor(left + (d.left * stretch - vis.left) * fit.scale) - margin,
                            (int32_t)floor(top + (d.top * stretch - vis.top) * fit.scale) - margin,
                            (int32_t)ceil(left + (d.right * stretch - vis.left) * fit.scale) + margin,
                            (int32_t)ceil(top + (d.bottom * stretch - vis.top) * fit.scale) + margin};
    }
    if (!mime_wl_window_frame(w->window, &damage, &frame)) {
        /* no buffer yet, or none free: what is dirty stays so for the frame that comes then. */
        pthread_mutex_unlock(&w->lock);
        return;
    }
    MimeRect whole = {0, 0, frame.width, frame.height};
    damage = rect_and(damage, whole);
    /* with the pixels that were just taken, so the places of the layers under them are the ones they go with. */
    MimeRect holes[MAX_HOLES];
    bool again = false;
    int hole_count = w->vk ? settle_layers(w, holes, &again) : 0;
    if (w->vk)
        cut_layers(w);
    b.graded = mime_gamma_table(b.table);
    /* a frame that lies otherwise than the one before is drawn whole. */
    uint64_t key = 1469598103934665603ull;
    key = mix(key, &b.dst, sizeof(b.dst));
    key = mix(key, &b.u0, 4 * sizeof(double));
    key = mix(key, &b.src_w, 2 * sizeof(int));
    key = mix(key, &b.back, sizeof(b.back));
    key = mix(key, &w->config.border_color, sizeof(w->config.border_color));
    key = mix(key, &whole, sizeof(whole));
    key = mix(key, holes, (size_t)hole_count * sizeof(MimeRect));
    MimeRect part = frame.stale;
    bool all = key != w->draw_key || !mime_wl_window_attached(w->window);
    if (all) {
        mime_wl_window_stale(w->window);
        part = whole;
    }
    if (!all && rect_empty(damage)) {
        /* nothing of the pixels changed: the state of the layers lands alone. */
        pthread_mutex_unlock(&w->lock);
        mime_wl_window_commit_state(w->window);
        if (w->vk)
            layers_drawn(w);
        if (again)
            mime_wl_timer(REDRAW_MS, present_main, (void *)(uintptr_t)w->id);
        return;
    }
    w->draw_key = key;
    w->has_dirty = false;
    blit(&b, &frame, part);
    pthread_mutex_unlock(&w->lock);
    /* clear where a layer under the window shows. */
    for (int i = 0; i < hole_count; i++) {
        MimeRect hole = {(int32_t)round(fit.x + (holes[i].left - host.left) * fit.scale),
                         (int32_t)round(fit.y + (holes[i].top - host.top) * fit.scale),
                         (int32_t)round(fit.x + (holes[i].right - host.left) * fit.scale),
                         (int32_t)round(fit.y + (holes[i].bottom - host.top) * fit.scale)};
        fill(&frame, hole, part, 0);
    }
    /* the corners of a window of windows 11 are round, its border follows them. */
    int radius = w->fullscreen ? 0 : (int)lround(w->config.corner_radius * fit.scale);
    MimeRect r = b.dst;
    radius = MIME_MIN(radius, MIME_MIN((r.right - r.left) / 2, (r.bottom - r.top) / 2));
    uint32_t border = premultiplied(w->config.border_color);
    if (border) {
        fill(&frame, (MimeRect){r.left + radius, r.top, r.right - radius, r.top + 1}, part, border);
        fill(&frame, (MimeRect){r.left + radius, r.bottom - 1, r.right - radius, r.bottom}, part, border);
        fill(&frame, (MimeRect){r.left, r.top + radius, r.left + 1, r.bottom - radius}, part, border);
        fill(&frame, (MimeRect){r.right - 1, r.top + radius, r.right, r.bottom - radius}, part, border);
    }
    if (radius > 0)
        round_corners(&frame, r, part, radius, border);
    mime_wl_window_commit(w->window, &frame, all ? nullptr : &damage);
    if (w->vk)
        layers_drawn(w);
    if (again)
        mime_wl_timer(REDRAW_MS, present_main, (void *)(uintptr_t)w->id);
}

/*
 * while a border is dragged a frame of the window is of the size the guest has, once the guest has drawn itself at
 * that size: the size and the pixels come one after the other, and a frame between the two would show the window cut.
 */
static void content_sync(UiWindow *w)
{
    if (!w->content_due || !w->window)
        return;
    const MimeWinConfig *c = &w->config;
    pthread_mutex_lock(&w->lock);
    bool drawn = pixels_fit(w);
    pthread_mutex_unlock(&w->lock);
    if (!drawn)
        return;
    MimeRect host = host_part(c);
    double shown = 1.0 / config_scale(c);
    w->content_due = false;
    mime_wl_window_content(w->window, MIME_MAX(1, (int)lround((host.right - host.left) * shown)),
                           MIME_MAX(1, (int)lround((host.bottom - host.top) * shown)));
    update_pixel_size(w);
}

static void present_main(void *arg)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window((uint32_t)(uintptr_t)arg);
    pthread_mutex_unlock(&g_windows_lock);
    if (w && w->shown) {
        content_sync(w);
        draw(w);
        if (w->reap)
            reap_layers(w);
    }
}

/*
 * a window with no sizing frame that can be maximized has a program that draws its frame and sizes the window itself.
 * the compositor may size it as well: sdl keeps a window that cannot be sized at the size asked, cut in a smaller tile.
 */
static bool can_size(const UiWindow *w, const MimeWinConfig *cfg)
{
    return (cfg->flags & MIME_WIN_RESIZABLE) || w->self_sized ||
           (cfg->flags & (MIME_WIN_BARE | MIME_WIN_MAXIMIZABLE)) == (MIME_WIN_BARE | MIME_WIN_MAXIMIZABLE);
}

static void request_window_state(UiWindow *w, int command)
{
    if (w->state_pending && w->state_command == command)
        return;
    if (++w->state_request == 0)
        ++w->state_request;
    w->state_pending = true;
    w->state_command = command;
    MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_STATE, .window_id = w->id, .x = command,
                         .state_request = w->state_request};
    emit(&ev);
}

/* converts a guest size limit to surface coordinates; 0 means no limit. */
static int limit_of(int32_t limit, int outside, double scale)
{
    return limit > 0 ? MIME_MAX(1, (int)lround((limit - outside) / scale)) : 0;
}

static void unminimize(UiWindow *w, bool activate);
static void size_recheck_main(void *arg);

/*
 * hyprland tells every window it is maximized, tiled or floating, so that none draws shadows. the state says nothing
 * there. hyprland also takes either request as a toggle: unset_maximized on a window that is not maximized maximizes
 * it. such a window is asked only for a change of the state asked before. it drops either request on a surface that
 * has no buffer yet, which the next one would then toggle the wrong way, so a frame goes first.
 */
static void set_maximized(UiWindow *w, bool maximized)
{
    if (w->says_maximized) {
        draw(w);
        w->maximized = maximized;
    }
    mime_wl_window_maximize(w->window, maximized);
}

static void apply_config(UiWindow *w, const MimeWinConfig *given)
{
    if (w->state_pending) {
        if (given->state_request != w->state_request)
            return;
        w->state_pending = false;
    }
    /*
     * a window with no size shows nothing on windows, as the one a VCL program keeps for its application. a host
     * window cannot be empty, and a compositor would give it a size of its own: a clear window over the others that
     * takes their clicks.
     */
    MimeWinConfig sized = *given;
    const MimeWinConfig *cfg = &sized;
    if (!(sized.flags & MIME_WIN_MINIMIZED) &&
        (sized.rect.right <= sized.rect.left || sized.rect.bottom <= sized.rect.top))
        sized.flags &= ~(uint32_t)MIME_WIN_VISIBLE;
    /* a popup sits on its owner from its creation: another owner or kind makes a new window. */
    UiWindow *owner = popup_owner(w, cfg);
    bool panel = is_panel(cfg, owner);
    /* a popup hangs from a window of the compositor, which a panel is not. */
    while (owner && !panel && owner->panel)
        owner = owner->owner ? find_window(owner->owner) : nullptr;
    bool want_popup = owner != nullptr;
    if (w->window &&
        (want_popup != w->popup || (w->popup && w->owner != owner->id) || panel != (w->panel != nullptr))) {
        destroy_host_window(w);
        /* the new host window is not shown yet. */
        w->shown = false;
        w->fullscreen = false;
    }
    bool size_changed = !w->configured || w->config.dpi != cfg->dpi || w->config.surface_dpi != cfg->surface_dpi || host_part(&w->config).right - host_part(&w->config).left !=
                                                  host_part(cfg).right - host_part(cfg).left ||
                        host_part(&w->config).bottom - host_part(&w->config).top !=
                            host_part(cfg).bottom - host_part(cfg).top;
    bool limits_changed = !w->configured || w->config.min_width != cfg->min_width ||
                          w->config.min_height != cfg->min_height || w->config.max_width != cfg->max_width ||
                          w->config.max_height != cfg->max_height || w->config.dpi != cfg->dpi ||
                          can_size(w, &w->config) != can_size(w, cfg) ||
                          host_part(&w->config).right - host_part(&w->config).left != host_part(cfg).right - host_part(cfg).left ||
                          host_part(&w->config).bottom - host_part(&w->config).top != host_part(cfg).bottom - host_part(cfg).top;
    pthread_mutex_lock(&w->lock);
    w->config = *cfg;
    pthread_mutex_unlock(&w->lock);
    if (size_changed)
        w->size_answered = true;
    w->configured = true;
    if (!w->window && !(cfg->flags & MIME_WIN_VISIBLE))
        return;
    bool fresh = !w->window;
    if (fresh && !create_host_window(w, cfg, owner, panel))
        return;
    if (fresh)
        set_geometry(w);

    char title[1024];
    mime_utf16_to_utf8(title, sizeof(title), cfg->title, MIME_MIN(cfg->title_chars, 256u));
    mime_wl_window_title(w->window, title);
    MimeRect host = host_part(cfg);
    double scale = config_scale(cfg);
    /*
     * during interactive resizing, use the compositor's size rather than the last size reported to the guest. a
     * minimized window keeps its size: the guest's is that of its icon, and kwin would place it elsewhere on restore.
     */
    bool minimized = (cfg->flags & MIME_WIN_MINIMIZED) != 0;
    double shown = 1.0 / scale;
    if (w->popup) {
        shown = window_fit(owner).scale;
        size_changed = size_changed || shown != w->popup_scale;
        w->popup_scale = shown;
    }
    if (size_changed && w->interactive_until <= now_ms() && !minimized) {
        w->want_w = MIME_MAX(1, (int)lround((host.right - host.left) * shown));
        w->want_h = MIME_MAX(1, (int)lround((host.bottom - host.top) * shown));
        w->want_ms = now_ms();
        w->content_due = false;
        mime_wl_window_size(w->window, w->want_w, w->want_h);
    } else if (size_changed && !minimized && !w->popup) {
        /* a border is dragged: the frames of the window are of the size the guest has taken. */
        /*
         * a border is dragged. the compositor keeps the corner across from it in place by the size of the buffer, so
         * with the left or the top border the buffer has the size the guest has taken, not the one the compositor
         * asked: the window would sit in a buffer of another size, its far corner off by the difference. with the
         * right or the bottom border alone the window sits at the start of the buffer anyway.
         */
        bool leading = w->drag_edge == 1 || w->drag_edge == 3 || w->drag_edge == 4 || w->drag_edge == 5 ||
                       w->drag_edge == 7;
        w->content_due = leading && mime_wl_window_resizing(w->window);
        content_sync(w);
    }
    /*
     * a compositor that tiles the window keeps its size and sends no event: the guest is told of the size kept, which
     * new limits may let it take.
     */
    if ((size_changed || limits_changed) && !fresh && !w->popup && !minimized && !w->size_recheck) {
        w->size_recheck = true;
        mime_wl_timer(260, size_recheck_main, (void *)(uintptr_t)w->id);
    }
    if (!w->popup && (limits_changed || fresh) && !minimized) {
        /* the compositor sizes a window the guest lets be sized, within the limits of its program. */
        int dw = (cfg->rect.right - cfg->rect.left) - (host.right - host.left);
        int dh = (cfg->rect.bottom - cfg->rect.top) - (host.bottom - host.top);
        int min_w = limit_of(cfg->min_width, dw, scale), min_h = limit_of(cfg->min_height, dh, scale);
        int max_w = limit_of(cfg->max_width, dw, scale), max_h = limit_of(cfg->max_height, dh, scale);
        /* no window is let grow past what a display could show, whatever the compositor proposes. */
        double density = mime_wl_window_scale(w->window);
        int cap = (int)(sane_limit() / (density > 1.0 ? density : 1.0));
        max_w = max_w > 0 ? MIME_MIN(max_w, cap) : cap;
        max_h = max_h > 0 ? MIME_MIN(max_h, cap) : cap;
        min_w = MIME_MIN(min_w, max_w);
        min_h = MIME_MIN(min_h, max_h);
        mime_wl_window_limits(w->window, min_w, min_h, max_w, max_h, can_size(w, cfg));
    }
    if (w->panel) {
        place_panel(w, owner, cfg);
    } else if (w->popup) {
        int x, y;
        popup_place(owner, cfg, &x, &y);
        mime_wl_window_position(w->window, x, y);
    }
    /* the compositor places a top level window itself, and stacks it. */

    if (cfg->flags & MIME_WIN_MINIMIZED) {
        if (cfg->flags & MIME_WIN_VISIBLE) {
            bool opening = !w->shown;
            mime_wl_window_minimize(w->window);
            if (opening) {
                w->geometry_w = w->geometry_h = 0;
                set_geometry(w);
            }
        } else {
            mime_wl_window_hide(w->window);
        }
        w->shown = (cfg->flags & MIME_WIN_VISIBLE) != 0;
        mime_vk_window_hidden(w->id);
        if (w->shown)
            mark_dirty(w);
        return;
    }
    if (cfg->flags & MIME_WIN_VISIBLE) {
        bool opening = !w->shown;
        if (opening) {
            mark_dirty(w);
            /* a window that opens maximized is told so before the compositor sizes it. */
            if (!w->says_maximized && !w->popup)
                mime_wl_window_maximize(w->window, (cfg->flags & (MIME_WIN_MAXIMIZED | MIME_WIN_FULLSCREEN)) ==
                                                       MIME_WIN_MAXIMIZED);
            show_host(w);
            w->shown_ms = now_ms();
        }
        if (mime_wl_window_minimized(w->window))
            unminimize(w, (cfg->flags & MIME_WIN_ACTIVATE) != 0);
        else if (cfg->flags & MIME_WIN_ACTIVATE)
            raise_window(w);
        w->shown = true;
    } else if (w->shown) {
        mime_wl_window_hide(w->window);
        w->shown = false;
        /* the window that shows again is a new one to the compositor. */
        if (w->says_maximized)
            w->maximized = false;
        mime_vk_window_hidden(w->id);
    }
    bool fullscreen = (cfg->flags & (MIME_WIN_FULLSCREEN | MIME_WIN_VISIBLE)) == (MIME_WIN_FULLSCREEN | MIME_WIN_VISIBLE);
    /* matches the guest's maximized state on the window's monitor. */
    bool maximize = !fullscreen && !w->popup && (cfg->flags & (MIME_WIN_MAXIMIZED | MIME_WIN_VISIBLE)) ==
                                                    (MIME_WIN_MAXIMIZED | MIME_WIN_VISIBLE);
    if (maximize && !w->maximized)
        set_maximized(w, true);
    else if (!maximize && w->maximized && !w->popup && !fullscreen)
        set_maximized(w, false);
    if (fullscreen != w->fullscreen && !w->popup) {
        w->fullscreen = fullscreen;
        w->fullscreen_ms = now_ms();
        if (fullscreen) {
            /* a window that covers a monitor goes fullscreen on that one. */
            uint32_t display = display_of_rect(&cfg->rect);
            mime_wl_window_fullscreen(w->window, true, display != mime_wl_window_output(w->window) ? display : 0);
        } else {
            mime_wl_window_fullscreen(w->window, false, 0);
        }
    }
    if (w->shown)
        draw(w);
}

/* a notification click token is consumed by the first window raised within a few seconds. */
static char g_activation_token[128];
static uint64_t g_activation_token_ms;
static pthread_mutex_t g_token_lock = PTHREAD_MUTEX_INITIALIZER;

void mime_ui_set_activation_token(const char *token)
{
    pthread_mutex_lock(&g_token_lock);
    mime_strlcpy(g_activation_token, token, sizeof(g_activation_token));
    g_activation_token_ms = now_ms();
    pthread_mutex_unlock(&g_token_lock);
}

static void raise_window(UiWindow *w)
{
    char token[128] = "";
    pthread_mutex_lock(&g_token_lock);
    if (g_activation_token[0] && now_ms() - g_activation_token_ms < 5000)
        mime_strlcpy(token, g_activation_token, sizeof(token));
    g_activation_token[0] = 0;
    pthread_mutex_unlock(&g_token_lock);
    struct wl_surface *surface = mime_wl_window_surface(w->window);
    if (token[0] && mime_activation_activate(mime_wl_display(), surface, token))
        return;
    /* the compositor gives the keyboard to a window whose program had the last input. */
    MimeWlWindow *focus = mime_wl_keyboard_focus();
    mime_activation_raise(mime_wl_display(), surface, mime_wl_seat(), mime_wl_serial(),
                          focus ? mime_wl_window_surface(focus) : nullptr, w->want_app_id);
}

static bool fresh_token(void)
{
    pthread_mutex_lock(&g_token_lock);
    bool fresh = g_activation_token[0] && now_ms() - g_activation_token_ms < 5000;
    pthread_mutex_unlock(&g_token_lock);
    return fresh;
}

static void apply_app_id(UiWindow *w);
static void commit_parent(UiWindow *w);

/*
 * xdg-shell has no way to unminimize: a window comes back when it is activated. kwin grants activation to the active
 * window's client, to the latest input or to a token the shell handed out, and otherwise only marks the window as
 * demanding attention, which left it minimized while the guest showed it. windows restores a window it does not
 * activate all the same, so such a window is mapped again: kwin shows a new window, if not on top.
 */
static void unminimize(UiWindow *w, bool activate)
{
    if (activate && (fresh_token() || mime_wl_keyboard_focus())) {
        raise_window(w);
        return;
    }
    bool maximized = w->maximized;
    mime_wl_window_hide(w->window);
    /* a hidden window keeps its minimized state for the next show, this drops it. */
    mime_wl_window_unminimize(w->window);
    if (!w->says_maximized)
        mime_wl_window_maximize(w->window, maximized);
    mark_dirty(w);
    show_host(w);
    if (maximized && w->says_maximized)
        set_maximized(w, true);
    w->set_app_id[0] = 0;
    apply_app_id(w);
    commit_parent(w);
}

static void apply_app_id(UiWindow *w)
{
    if (!w->window || !w->want_app_id[0] || !strcmp(w->want_app_id, w->set_app_id))
        return;
    mime_wl_window_app_id(w->window, w->want_app_id);
    mime_strlcpy(w->set_app_id, w->want_app_id, sizeof(w->set_app_id));
}

typedef struct AppIdCall {
    uint32_t window;
    char app_id[200];
} AppIdCall;

static void app_id_main(void *arg)
{
    AppIdCall *call = arg;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(call->window);
    if (w) {
        mime_strlcpy(w->want_app_id, call->app_id, sizeof(w->want_app_id));
        apply_app_id(w);
    }
    pthread_mutex_unlock(&g_windows_lock);
    free(call);
}

void mime_ui_window_app_id(uint32_t window, const char *app_id)
{
    if (!g_video)
        return;
    AppIdCall *call = mime_xmalloc(sizeof(*call));
    call->window = window;
    mime_strlcpy(call->app_id, app_id, sizeof(call->app_id));
    on_main(app_id_main, call);
}

typedef struct FlashCall {
    uint32_t window, mode;
} FlashCall;

static void flash_main(void *arg)
{
    FlashCall *call = arg;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(call->window);
    /* windows go on this thread alone, so w stays while the lock is let go for the compositor's roundtrips. */
    pthread_mutex_unlock(&g_windows_lock);
    /* nothing takes a request for attention back: the compositor drops it when the window gets the keyboard. */
    if (w && w->window && call->mode != 0)
        mime_activation_attention(mime_wl_display(), mime_wl_window_surface(w->window), w->want_app_id);
    free(call);
}

void mime_ui_window_flash(uint32_t window, uint32_t mode)
{
    if (!g_video)
        return;
    FlashCall *call = mime_xmalloc(sizeof(*call));
    call->window = window;
    call->mode = mode;
    on_main(flash_main, call);
}

static void config_main(void *arg)
{
    uint32_t id = (uint32_t)(uintptr_t)arg;
    MimeWinConfig cfg;
    bool have = false;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id);
    if (w) {
        pthread_mutex_lock(&w->lock);
        if (w->config_queued) {
            cfg = w->pending_config;
            w->config_queued = false;
            have = true;
        }
        pthread_mutex_unlock(&w->lock);
    }
    pthread_mutex_unlock(&g_windows_lock);
    if (have) {
        apply_config(w, &cfg);
        apply_app_id(w);
    }
}

void mime_ui_window_config(const MimeWinConfig *cfg)
{
    mime_launcher_window_seen(cfg->window_id, cfg->process_id);
    mime_presence_window(cfg->window_id, cfg->process_id, cfg->image_path, cfg->image_chars);
    bool queue = false;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(cfg->window_id);
    if (w) {
        pthread_mutex_lock(&w->lock);
        uint32_t activate = w->config_queued ? w->pending_config.flags & MIME_WIN_ACTIVATE : 0;
        w->pending_config = *cfg;
        w->pending_config.flags |= activate;
        queue = !w->config_queued;
        w->config_queued = true;
        pthread_mutex_unlock(&w->lock);
    }
    pthread_mutex_unlock(&g_windows_lock);
    if (queue)
        on_main(config_main, (void *)(uintptr_t)cfg->window_id);
}

void mime_ui_window_create(uint32_t id)
{
    pthread_mutex_lock(&g_windows_lock);
    if (!find_window(id)) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (!g_windows[i].used) {
                memset(&g_windows[i], 0, sizeof(g_windows[i]));
                g_windows[i].used = true;
                g_windows[i].id = id;
                pthread_mutex_init(&g_windows[i].lock, nullptr);
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_windows_lock);
}

static void destroy_main(void *arg)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window((uint32_t)(uintptr_t)arg);
    if (!w) {
        pthread_mutex_unlock(&g_windows_lock);
        return;
    }
    /* popups go first: they cannot outlive the window they hang from. */
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].popup && g_windows[i].owner == w->id) {
            destroy_host_window(&g_windows[i]);
            g_windows[i].shown = false;
        }
    destroy_host_window(w);
    pthread_mutex_lock(&w->lock);
    free(w->pixels);
    w->pixels = nullptr;
    w->pixels_room = 0;
    pthread_mutex_unlock(&w->lock);
    pthread_mutex_destroy(&w->lock);
    w->used = false;
    pthread_mutex_unlock(&g_windows_lock);
}

void mime_ui_window_destroy(uint32_t id)
{
    mime_launcher_window_gone(id);
    mime_presence_window_gone(id);
    if (!g_video) {
        pthread_mutex_lock(&g_windows_lock);
        UiWindow *w = find_window(id);
        if (w)
            w->used = false;
        pthread_mutex_unlock(&g_windows_lock);
        return;
    }
    on_main(destroy_main, (void *)(uintptr_t)id);
}

void mime_ui_process_exit(uint32_t process_id)
{
    mime_launcher_process_gone(process_id);
    mime_presence_process_gone(process_id);
}

typedef struct WindowIcon {
    uint32_t window_id, width, height;
    uint8_t pixels[];
} WindowIcon;

static void icon_main(void *arg)
{
    WindowIcon *icon = arg;
    UiWindow *w = find_window(icon->window_id);
    if (w && w->window && icon->width && icon->height)
        mime_wl_window_icon(w->window, icon->pixels, (int)icon->width, (int)icon->height);
    free(icon);
}

void mime_ui_window_icon(uint32_t id, const void *pixels, uint32_t width, uint32_t height, uint32_t stride)
{
    if (!g_video || width > 1024 || height > 1024 || stride < width * 4 || (!pixels && width && height))
        return;
    WindowIcon *icon = mime_xmalloc(sizeof(*icon) + (size_t)width * height * 4);
    *icon = (WindowIcon){.window_id = id, .width = width, .height = height};
    for (uint32_t y = 0; y < height; y++)
        memcpy(icon->pixels + (size_t)y * width * 4, (const uint8_t *)pixels + (size_t)y * stride, (size_t)width * 4);
    on_main(icon_main, icon);
}

/* copies the dirty part into the window's staging buffer; the main thread uploads it. */
void mime_ui_window_present(uint32_t id, const void *pixels, uint32_t width, uint32_t height, uint32_t stride,
                            MimeRect dirty)
{
    if (!g_video)
        return;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id);
    if (!w) {
        pthread_mutex_unlock(&g_windows_lock);
        return;
    }
    pthread_mutex_lock(&w->lock);
    pthread_mutex_unlock(&g_windows_lock);
    if (!w->pixels || w->width != width || w->height != height) {
        size_t need = (size_t)width * height * 4;
        if (!w->pixels || need > w->pixels_room || need < w->pixels_room / 2) {
            free(w->pixels);
            w->pixels_room = need + need / 4;
            w->pixels = malloc(w->pixels_room);
        }
        w->width = width;
        w->height = height;
        dirty = (MimeRect){0, 0, (int32_t)width, (int32_t)height};
        w->has_dirty = false;
    }
    if (!w->pixels) {
        pthread_mutex_unlock(&w->lock);
        return;
    }
    int32_t left = MIME_MAX(dirty.left, 0), top = MIME_MAX(dirty.top, 0);
    int32_t right = MIME_MIN(dirty.right, (int32_t)width), bottom = MIME_MIN(dirty.bottom, (int32_t)height);
    for (int32_t y = top; y < bottom && left < right; y++)
        memcpy(w->pixels + ((size_t)y * width + (size_t)left) * 4,
               (const uint8_t *)pixels + (size_t)y * stride + (size_t)left * 4, (size_t)(right - left) * 4);
    if (left < right && top < bottom) {
        if (w->has_dirty)
            w->dirty = (MimeRect){MIME_MIN(w->dirty.left, left), MIME_MIN(w->dirty.top, top),
                                  MIME_MAX(w->dirty.right, right), MIME_MAX(w->dirty.bottom, bottom)};
        else
            w->dirty = (MimeRect){left, top, right, bottom};
        w->has_dirty = true;
    }
    /* these are the pixels a layer under the window waited for: its place lands with them, in one frame. */
    for (int i = 0; i < MAX_VK_LAYERS; i++)
        if (w->vk_layers[i].used && w->vk_layers[i].waits) {
            w->vk_layers[i].shown = w->vk_layers[i].told;
            w->vk_layers[i].waits = false;
        }
    bool queue = !w->present_queued;
    w->present_queued = true;
    pthread_mutex_unlock(&w->lock);
    if (queue)
        on_main(present_main, (void *)(uintptr_t)id);
}

/* ------------------------------------------------------------------ Vulkan presentation. */

/* a rectangle of the window's own pixels in the pixels of the guest's screen, which differ for a window that is stretched. */
static MimeRect physical_rect(const UiWindow *w, MimeRect r)
{
    double k = surface_stretch(&w->config);
    if (k == 1.0)
        return r;
    return (MimeRect){(int32_t)lround(r.left * k), (int32_t)lround(r.top * k), (int32_t)lround(r.right * k),
                      (int32_t)lround(r.bottom * k)};
}

/*
 * a rectangle of the window's own pixels in surface coordinates. the guest gives it in the pixels of the program,
 * which a window that is not aware of the display's dpi has fewer of: the image is shown over the whole rectangle,
 * stretched as the rest of the window is.
 */
static MimeRect client_in_window(UiWindow *w, MimeRect client)
{
    MimeRect host = host_part(&w->config);
    Fit fit = window_fit(w);
    client = physical_rect(w, client);
    return (MimeRect){(int32_t)lround(fit.x + (client.left - host.left) * fit.scale),
                      (int32_t)lround(fit.y + (client.top - host.top) * fit.scale),
                      (int32_t)lround(fit.x + (client.right - host.left) * fit.scale),
                      (int32_t)lround(fit.y + (client.bottom - host.top) * fit.scale)};
}

static VkLayer *find_layer(UiWindow *w, uint32_t source)
{
    for (int i = 0; i < MAX_VK_LAYERS; i++)
        if (w->vk_layers[i].used && w->vk_layers[i].source == source)
            return &w->vk_layers[i];
    return nullptr;
}

static void drop_subsurface(VkLayer *l)
{
    if (l->subsurface)
        wl_subsurface_destroy(l->subsurface);
    l->subsurface = nullptr;
}

static void destroy_layer(UiWindow *w, VkLayer *l)
{
    pthread_mutex_lock(&w->lock);
    if (l->viewport)
        mime_wl_viewport_destroy(l->viewport);
    drop_subsurface(l);
    if (l->surface)
        wl_surface_destroy(l->surface);
    *l = (VkLayer){};
    pthread_mutex_unlock(&w->lock);
}

static void destroy_layers(UiWindow *w)
{
    for (int i = 0; i < MAX_VK_LAYERS; i++)
        if (w->vk_layers[i].used)
            destroy_layer(w, &w->vk_layers[i]);
}

static bool has_layers(const UiWindow *w)
{
    for (int i = 0; i < MAX_VK_LAYERS; i++)
        if (w->vk_layers[i].used)
            return true;
    return false;
}

static struct wl_surface *parent_surface(UiWindow *w)
{
    return w->window ? mime_wl_window_surface(w->window) : nullptr;
}

/* a change of the parent's stack or of a position lands with the parent's next commit. */
static void commit_parent(UiWindow *w)
{
    /* a frame that changes nothing of the pixels is a commit of the state alone, see draw. */
    if (w->window && w->shown)
        draw(w);
    else if (w->window)
        mime_wl_window_commit_state(w->window);
}

/*
 * the layers over the window's own pixels from the back of the guest's paint order to the front, and the ones under
 * them from the front to the back.
 */
static void restack_layers(UiWindow *w)
{
    struct wl_surface *parent = parent_surface(w);
    for (int under = 0; parent && under < 2; under++) {
        struct wl_surface *next = parent;
        bool done[MAX_VK_LAYERS] = {};
        for (int n = 0; n < MAX_VK_LAYERS; n++) {
            int best = -1;
            for (int k = 0; k < MAX_VK_LAYERS; k++) {
                const VkLayer *l = &w->vk_layers[k];
                if (!l->used || !l->subsurface || done[k] || l->under != (under != 0))
                    continue;
                if (best < 0 || (under ? l->order > w->vk_layers[best].order : l->order < w->vk_layers[best].order))
                    best = k;
            }
            if (best < 0)
                break;
            done[best] = true;
            if (under)
                wl_subsurface_place_below(w->vk_layers[best].subsurface, next);
            else
                wl_subsurface_place_above(w->vk_layers[best].subsurface, next);
            next = w->vk_layers[best].surface;
        }
    }
}

/* places a layer over the area; the position lands with the window's next frame. */
static void place_layer(UiWindow *w, VkLayer *l, MimeRect area, uint32_t order)
{
    MimeRect r = client_in_window(w, area);
    bool restack = l->order != order;
    l->order = order;
    if (r.right <= r.left || r.bottom <= r.top) {
        /*
         * nothing of it shows: its subsurface goes, which unmaps it at once. the surface is the driver's, and a commit
         * of ours between its sync points and its buffer ends the connection with an error of the compositor, so it
         * can neither lose its buffer nor shrink. it once moved 100000 pixels away instead, and kwin, which draws a
         * window offscreen at the extent of all its surfaces for its animations, took buffers of tens of gigabytes
         * from system memory for it until the host ran out.
         */
        l->place = r;
        if (l->subsurface) {
            drop_subsurface(l);
            commit_parent(w);
        }
        return;
    }
    bool fresh = !l->subsurface;
    if (fresh) {
        struct wl_surface *parent = parent_surface(w);
        l->subsurface = parent ? wl_subcompositor_get_subsurface(mime_wl_subcompositor(), l->surface, parent) : nullptr;
        if (!l->subsurface)
            return;
        wl_subsurface_set_desync(l->subsurface);
        restack = true;
    }
    if (!restack && !memcmp(&r, &l->place, sizeof(r)))
        return;
    if (fresh || memcmp(&r, &l->place, sizeof(r))) {
        l->place = r;
        wl_subsurface_set_position(l->subsurface, r.left, r.top);
    }
    if (restack)
        restack_layers(w);
    commit_parent(w);
}

/*
 * the layers of children lie under a window that has pixels of its own to cut them with. one of the top level window
 * covers those pixels, and the layers of its children then lie over it.
 */
static bool under_wanted(const UiWindow *w)
{
    if (!w->pixels || !w->window)
        return false;
    for (int i = 0; i < MAX_VK_LAYERS; i++)
        if (w->vk_layers[i].used && w->vk_layers[i].top)
            return false;
    return true;
}

/*
 * a subsurface lies on whole surface coordinates, which are several screen pixels of the guest on a display that
 * is scaled: the count of them, or 1 where that is no whole number.
 */
static int32_t layer_step(Fit fit)
{
    double s = 1.0 / fit.scale;
    bool whole = s >= 1.5 && fabs(s - round(s)) < 1e-6 && fit.x == floor(fit.x) && fit.y == floor(fit.y);
    return whole ? (int32_t)lround(s) : 1;
}

/* where the surface of a layer that shows from lo on starts: on a whole coordinate, inside what the window shows. */
static int32_t layer_start(int32_t lo, int32_t base, int32_t edge, int32_t step)
{
    int32_t from = base + (lo - base) / step * step;
    return from < edge ? from + step : from;
}

static bool layer_fits(const int32_t at[2], const LayerFrame *f, MimeRect vis)
{
    return at[0] >= vis.left && at[1] >= vis.top && at[0] + f->ext[0] <= vis.right && at[1] + f->ext[1] <= vis.bottom;
}

/*
 * moves a layer under the window or over it, with the window's lock. it shows again with its next frame, and keeps its
 * subsurface: the driver may wait for the display to take a frame, which one that is not on screen never does.
 */
static void set_under(VkLayer *l, bool under)
{
    l->under = under;
    l->framed = l->covered = (LayerFrame){};
    l->framed_serial = 0;
    l->cut[0] = l->cut[1] = l->cut_next[0] = l->cut_next[1] = 0;
    l->dest[0] = l->dest[1] = 0;
    l->waits = false;
    l->place = (MimeRect){};
    l->order = ~l->order;
}

/*
 * the layers under the window for the frame of it that is about to be drawn, with the window's lock: where each lies,
 * which is state of the window and lands with the frame, and the parts of the window to draw clear over them. a layer
 * shows through no further than the frame the display has of it reaches, so the window's own pixels show where its
 * frame is late, and the frame that comes next is no smaller than what is clear, see frame_under.
 */
/* takes a rectangle out of a list of them, a piece that would not fit staying whole. returns the new count. */
static int rect_cut(MimeRect *list, int count, int capacity, MimeRect out)
{
    for (int i = count - 1; i >= 0; i--) {
        MimeRect r = list[i], in = rect_and(r, out);
        if (rect_empty(in))
            continue;
        MimeRect parts[4] = {{r.left, r.top, r.right, in.top},
                             {r.left, in.bottom, r.right, r.bottom},
                             {r.left, in.top, in.left, in.bottom},
                             {in.right, in.top, r.right, in.bottom}};
        int made = 0;
        for (int k = 0; k < 4; k++)
            made += !rect_empty(parts[k]);
        if (count - 1 + made > capacity)
            continue;
        list[i] = list[--count];
        for (int k = 0; k < 4; k++)
            if (!rect_empty(parts[k]))
                list[count++] = parts[k];
    }
    return count;
}

/*
 * cuts the layers over the window where the frame of it that is about to be drawn ends, with the window's lock. the
 * surface of a layer is the driver's, and a commit of ours among those of a frame it hands over would end the
 * connection: the presenter holds the window's lock for as long as that takes, see mime_ui_vk_landing.
 */
/* where the part of the window that shows ends in the host window as it is now, in surface coordinates. */
static void window_end(const UiWindow *w, int32_t end[2])
{
    MimeRect vis = visible_part(&w->config), host = host_part(&w->config);
    Fit fit = window_fit(w);
    /* a host window that is smaller than the guest has it yet ends where its own border for sizing starts. */
    end[0] = MIME_MIN((int32_t)lround(fit.x + (vis.right - host.left) * fit.scale),
                      w->logical_width - (int32_t)lround((host.right - vis.right) * fit.scale));
    end[1] = MIME_MIN((int32_t)lround(fit.y + (vis.bottom - host.top) * fit.scale),
                      w->logical_height - (int32_t)lround((host.bottom - vis.bottom) * fit.scale));
}

/* what a frame of a size that starts at a place shows of itself inside the window. false when nothing or all of it. */
static bool layer_cut(const UiWindow *w, int32_t left, int32_t top, const int32_t size[2], int32_t want[2])
{
    int32_t end[2];
    window_end(w, end);
    want[0] = MIME_MIN(end[0] - left, size[0]);
    want[1] = MIME_MIN(end[1] - top, size[1]);
    if (want[0] <= 0 || want[1] <= 0) {
        memcpy(want, size, 2 * sizeof(int32_t));
        return false;
    }
    return want[0] != size[0] || want[1] != size[1];
}

static bool layer_cuts(const VkLayer *l)
{
    return !l->under && !l->gone && l->subsurface && l->viewport && l->have_size[0] > 0 && l->have_size[1] > 0 &&
           l->have_pixels[0] > 0 && l->have_pixels[1] > 0;
}

static void cut_layers(UiWindow *w)
{
    for (int i = 0; i < MAX_VK_LAYERS; i++) {
        VkLayer *l = &w->vk_layers[i];
        if (!l->used || !layer_cuts(l))
            continue;
        int32_t want[2], now[2];
        bool whole = !layer_cut(w, l->have_at[0], l->have_at[1], l->have_size, want);
        for (int k = 0; k < 2; k++)
            now[k] = l->cut_size[0] ? l->cut_size[k] : l->have_size[k];
        if (want[0] == now[0] && want[1] == now[1])
            continue;
        if (whole) {
            mime_wl_viewport_whole(l->viewport, want[0], want[1]);
            l->cut_size[0] = l->cut_size[1] = 0;
        } else {
            mime_wl_viewport_part(l->viewport, (double)l->have_pixels[0] * want[0] / l->have_size[0],
                                  (double)l->have_pixels[1] * want[1] / l->have_size[1], want[0], want[1]);
            memcpy(l->cut_size, want, sizeof(want));
        }
        wl_surface_commit(l->surface);
    }
}

static int settle_layers(UiWindow *w, MimeRect *holes, bool *again)
{
    bool under = under_wanted(w), restack = false;
    int count = 0;
    MimeRect vis = visible_part(&w->config), host = host_part(&w->config);
    Fit fit = window_fit(w);
    uint64_t now = now_ms();
    struct wl_surface *parent = parent_surface(w);
    for (int i = 0; i < MAX_VK_LAYERS; i++) {
        VkLayer *l = &w->vk_layers[i];
        if (!l->used || l->top)
            continue;
        if (l->under != under) {
            set_under(l, under);
            restack = true;
        }
        if (!l->under)
            continue;
        /* the pixels a place waited for never came. */
        if (l->waits && now - l->told_ms + 5 >= REDRAW_MS) {
            l->shown = l->told;
            l->waits = false;
        }
        const LayerFrame *c = &l->covered;
        bool has = l->shown.set && c->ext[0] > 0 && c->ext[1] > 0, showing = !rect_empty(l->shown.visible);
        if (has) {
            /*
             * where the frame the display has puts its image over the client area. a surface that would reach out of
             * the window there stays where it is: nothing covers it past the window, and its next frame fits.
             */
            int32_t want[2] = {l->shown.client.left - c->off[0], l->shown.client.top - c->off[1]};
            if (!l->subsurface || layer_fits(want, c, vis) || !layer_fits(l->at, c, vis))
                memcpy(l->at, want, sizeof(want));
        }
        /* one that shows nothing is unmapped once the frame that closed the window over it is on screen for sure. */
        bool closed = !showing && l->shown.set && !l->cut[0] && now - l->closed_ms >= REDRAW_MS;
        bool fresh = false;
        if (l->subsurface && closed) {
            drop_subsurface(l);
        } else if (!l->subsurface && showing && has && parent && layer_fits(l->at, c, vis)) {
            l->subsurface = wl_subcompositor_get_subsurface(mime_wl_subcompositor(), l->surface, parent);
            if (l->subsurface)
                wl_subsurface_set_desync(l->subsurface);
            fresh = restack = l->subsurface != nullptr;
        }
        MimeRect hole = {};
        if (l->subsurface) {
            int32_t x = (int32_t)lround(fit.x + (l->at[0] - host.left) * fit.scale);
            int32_t y = (int32_t)lround(fit.y + (l->at[1] - host.top) * fit.scale);
            if (fresh || x != l->place.left || y != l->place.top)
                wl_subsurface_set_position(l->subsurface, x, y);
            l->place = (MimeRect){x, y, x + l->dest[0], y + l->dest[1]};
            if (l->shown.set && l->order != l->shown.order) {
                l->order = l->shown.order;
                restack = true;
            }
            if (has)
                hole = rect_and(rect_and(l->shown.visible, vis),
                                (MimeRect){l->at[0] + MIME_MAX(c->lit[0], l->framed.lit[0]),
                                           l->at[1] + MIME_MAX(c->lit[1], l->framed.lit[1]),
                                           l->at[0] + MIME_MIN(c->reach[0], l->framed.reach[0]),
                                           l->at[1] + MIME_MIN(c->reach[1], l->framed.reach[1])});
        }
        bool open = !rect_empty(hole);
        if (l->cut[0] && !open)
            l->closed_ms = now;
        l->cut_next[0] = open ? hole.right - l->at[0] : 0;
        l->cut_next[1] = open ? hole.bottom - l->at[1] : 0;
        l->cut[0] = MIME_MAX(l->cut[0], l->cut_next[0]);
        l->cut[1] = MIME_MAX(l->cut[1], l->cut_next[1]);
        if (open && count < MAX_HOLES) {
            /* the window stays closed where another window of the guest covers the layer. */
            int first = count;
            holes[count++] = hole;
            for (uint32_t h = 0; h < l->shown.hidden_count; h++)
                count = first + rect_cut(holes + first, count - first, MAX_HOLES - first, l->shown.hidden[h]);
        }
        if (l->gone && !l->subsurface)
            w->reap = true;
        else if (l->waits || l->gone || (l->subsurface && !showing && l->shown.set))
            *again = true;
    }
    if (restack)
        restack_layers(w);
    return count;
}

/* the frame of the window that was drawn is the one that shows from now on. */
static void layers_drawn(UiWindow *w)
{
    pthread_mutex_lock(&w->lock);
    for (int i = 0; i < MAX_VK_LAYERS; i++) {
        VkLayer *l = &w->vk_layers[i];
        if (l->used && l->under)
            memcpy(l->cut, l->cut_next, sizeof(l->cut));
    }
    pthread_mutex_unlock(&w->lock);
}

/* the layers whose presenter left and that have nothing on screen left go. */
static void reap_layers(UiWindow *w)
{
    pthread_mutex_lock(&g_windows_lock);
    w->reap = false;
    bool any = false;
    for (int i = 0; i < MAX_VK_LAYERS; i++) {
        VkLayer *l = &w->vk_layers[i];
        if (l->used && l->gone && !l->subsurface) {
            destroy_layer(w, l);
            any = true;
        }
    }
    if (any && w->vk && !has_layers(w))
        w->vk = false;
    pthread_mutex_unlock(&g_windows_lock);
}

static VkLayer *make_layer(UiWindow *w, struct wl_surface *parent, uint32_t source, MimeRect area, uint32_t order)
{
    VkLayer *l = nullptr;
    for (int i = 0; i < MAX_VK_LAYERS && !l; i++)
        if (!w->vk_layers[i].used)
            l = &w->vk_layers[i];
    if (!l)
        return nullptr;
    struct wl_surface *surface = wl_compositor_create_surface(mime_wl_compositor());
    struct wl_subsurface *subsurface =
        surface ? wl_subcompositor_get_subsurface(mime_wl_subcompositor(), surface, parent) : nullptr;
    if (!subsurface) {
        if (surface)
            wl_surface_destroy(surface);
        return nullptr;
    }
    wl_subsurface_set_desync(subsurface);
    /* the mouse goes through, to the window. */
    struct wl_region *empty = wl_compositor_create_region(mime_wl_compositor());
    wl_surface_set_input_region(surface, empty);
    wl_region_destroy(empty);
    pthread_mutex_lock(&w->lock);
    *l = (VkLayer){.used = true, .source = source, .surface = surface, .subsurface = subsurface, .top = !order};
    l->viewport = mime_wl_viewport(l->surface);
    wl_surface_commit(l->surface);
    l->under = !l->top && under_wanted(w);
    l->order = l->under ? order : ~order;
    if (l->under) {
        /* it starts where it will show, under pixels of the window that are not clear yet. */
        MimeRect vis = visible_part(&w->config), host = host_part(&w->config);
        MimeRect r = rect_and(physical_rect(w, area), vis);
        Fit fit = window_fit(w);
        int32_t step = layer_step(fit);
        l->at[0] = layer_start(rect_empty(r) ? vis.left : r.left, host.left, vis.left, step);
        l->at[1] = layer_start(rect_empty(r) ? vis.top : r.top, host.top, vis.top, step);
        int32_t x = (int32_t)lround(fit.x + (l->at[0] - host.left) * fit.scale);
        int32_t y = (int32_t)lround(fit.y + (l->at[1] - host.top) * fit.scale);
        wl_subsurface_set_position(l->subsurface, x, y);
        l->place = (MimeRect){x, y, x, y};
    }
    pthread_mutex_unlock(&w->lock);
    if (l->under) {
        restack_layers(w);
        commit_parent(w);
    } else {
        place_layer(w, l, area, order);
    }
    return l;
}

typedef struct MoveSize {
    uint32_t window, edge;
    uint64_t status;
    bool done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} MoveSize;

/* xdg_toplevel's resize edges for the guest's WMSZ_* ones. */
static uint32_t xdg_edge(uint32_t edge)
{
    static const uint32_t edges[] = {0, 4, 8, 1, 5, 9, 2, 6, 10};
    return edge < sizeof(edges) / sizeof(edges[0]) ? edges[edge] : 0;
}

/* the window whose border the compositor drags for the guest, 0 for none. */
static uint32_t g_drag_window;

static void drag_ended(UiWindow *w)
{
    if (!w->drag_live)
        return;
    w->drag_live = false;
    if (g_drag_window == w->id)
        g_drag_window = 0;
    MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_SIZE, .window_id = w->id, .flags = 1};
    emit(&ev);
}

/*
 * the pointer is ours again. a compositor that says when it sizes a window says when it stops too, one that does not
 * has ended its drag by the time the pointer is back.
 */
static void drag_pointer_back(void)
{
    UiWindow *w = g_drag_window ? find_window(g_drag_window) : nullptr;
    if (!w) {
        g_drag_window = 0;
        return;
    }
    if (w->window && mime_wl_window_resizing(w->window))
        w->drag_seen = true;
    else if (w->drag_seen || now_ms() - w->drag_ms > 300)
        drag_ended(w);
}

static void movesize_main(void *arg)
{
    MoveSize *m = arg;
    UiWindow *w = find_window(m->window);
    void *seat;
    uint32_t serial;
    m->status = MIME_HC_STATUS_UNSUPPORTED;
    if (w && w->window && w->shown && !w->popup && !w->fullscreen && m->edge <= 8 &&
        mime_ui_pointer_press(&seat, &serial)) {
        bool ok;
        if (!m->edge) {
            ok = mime_wl_window_move(w->window, serial);
        } else {
            if (!can_size(w, &w->config)) {
                w->self_sized = true;
                mime_wl_window_resizable(w->window, true);
            }
            ok = mime_wl_window_resize(w->window, serial, xdg_edge(m->edge));
            if (ok) {
                w->interactive_until = now_ms() + 3000;
                w->drag_edge = m->edge;
                w->drag_live = true;
                w->drag_seen = false;
                w->drag_ms = now_ms();
                g_drag_window = w->id;
            }
        }
        if (ok)
            m->status = MIME_HC_STATUS_OK;
    }
    pthread_mutex_lock(&m->lock);
    m->done = true;
    pthread_cond_signal(&m->cond);
    pthread_mutex_unlock(&m->lock);
}

uint64_t mime_ui_window_movesize(uint32_t window, uint32_t edge)
{
    if (!g_video)
        return MIME_HC_STATUS_UNSUPPORTED;
    MoveSize *m = calloc(1, sizeof(*m));
    if (!m)
        return MIME_HC_STATUS_UNSUPPORTED;
    m->window = window;
    m->edge = edge;
    pthread_mutex_init(&m->lock, nullptr);
    pthread_cond_init(&m->cond, nullptr);
    on_main(movesize_main, m);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    pthread_mutex_lock(&m->lock);
    while (!m->done && pthread_cond_timedwait(&m->cond, &m->lock, &ts) == 0)
        ;
    bool done = m->done;
    uint64_t status = m->status;
    pthread_mutex_unlock(&m->lock);
    if (done) {
        pthread_mutex_destroy(&m->lock);
        pthread_cond_destroy(&m->cond);
        free(m);
    }
    return done ? status : MIME_HC_STATUS_UNSUPPORTED;
}

typedef struct VkAttach {
    uint32_t id, source, order;
    MimeRect area;
    MimeUiNative *native;
    bool ok, done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} VkAttach;

bool mime_ui_vk_layers(void)
{
    return g_video;
}

static void attach_main(void *arg)
{
    VkAttach *a = arg;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(a->id);
    if (w && w->window) {
        *a->native = (MimeUiNative){0};
        struct wl_surface *parent = mime_wl_window_surface(w->window);
        VkLayer *layer = nullptr;
        if (parent && mime_wl_subcompositor() &&
            ((layer = find_layer(w, a->source)) || (layer = make_layer(w, parent, a->source, a->area, a->order)))) {
            /* one that was about to go shows again with the first frame of its new presenter. */
            pthread_mutex_lock(&w->lock);
            if (layer->gone) {
                layer->gone = false;
                layer->framed = layer->covered = (LayerFrame){};
                layer->framed_serial = 0;
                layer->dest[0] = layer->dest[1] = 0;
            }
            pthread_mutex_unlock(&w->lock);
            layer->users++;
            a->native->display = mime_wl_display();
            a->native->surface = layer->surface;
            a->native->subsurface = true;
            a->ok = true;
        }
        if (a->ok) {
            w->vk = true;
            update_pixel_size(w);
        }
    }
    pthread_mutex_unlock(&g_windows_lock);
    pthread_mutex_lock(&a->lock);
    a->done = true;
    pthread_cond_signal(&a->cond);
    pthread_mutex_unlock(&a->lock);
}

bool mime_ui_vk_attach(uint32_t id, uint32_t source, MimeRect area, uint32_t order, MimeUiNative *native)
{
    if (!g_video)
        return false;
    VkAttach *a = calloc(1, sizeof(*a));
    if (!a)
        return false;
    *a = (VkAttach){.id = id, .source = source, .order = order, .area = area, .native = native};
    pthread_mutex_init(&a->lock, nullptr);
    pthread_cond_init(&a->cond, nullptr);
    on_main(attach_main, a);
    /* a main loop that stopped leaves the call queued: the request then fails and leaks. */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;
    pthread_mutex_lock(&a->lock);
    while (!a->done && pthread_cond_timedwait(&a->cond, &a->lock, &ts) == 0)
        ;
    bool done = a->done, ok = a->ok;
    pthread_mutex_unlock(&a->lock);
    if (done) {
        pthread_mutex_destroy(&a->lock);
        pthread_cond_destroy(&a->cond);
        free(a);
    }
    return done && ok;
}

typedef struct VkDetach {
    uint32_t id, source;
} VkDetach;

static void detach_main(void *arg)
{
    VkDetach *d = arg;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(d->id);
    if (w && w->vk) {
        VkLayer *layer = find_layer(w, d->source);
        if (layer && layer->users > 1) {
            layer->users--;
            pthread_mutex_unlock(&g_windows_lock);
            free(d);
            return;
        }
        /*
         * a layer under the window goes once a frame of the window that is closed over it is on screen, or the desktop
         * would show through the window meanwhile.
         */
        bool later = layer && layer->under && layer->subsurface && w->shown && w->window && w->pixels &&
                     mime_wl_window_attached(w->window) &&
                     !w->bogus_size;
        if (later) {
            pthread_mutex_lock(&w->lock);
            layer->users = 0;
            layer->gone = true;
            layer->waits = false;
            layer->told.set = layer->shown.set = true;
            layer->told.visible = layer->shown.visible = (MimeRect){};
            pthread_mutex_unlock(&w->lock);
        } else if (layer) {
            destroy_layer(w, layer);
        }
        /* the window stays under the presenters while a layer is left. */
        if (later) {
        } else if (!has_layers(w)) {
            w->vk = false;
            pthread_mutex_lock(&w->lock);
            w->dirty = (MimeRect){0, 0, (int32_t)w->width, (int32_t)w->height};
            w->has_dirty = w->pixels != nullptr;
            pthread_mutex_unlock(&w->lock);
        } else {
            restack_layers(w);
        }
    }
    pthread_mutex_unlock(&g_windows_lock);
    if (w && w->shown) {
        draw(w);
        if (w->reap)
            reap_layers(w);
    }
    free(d);
}

void mime_ui_vk_detach(uint32_t id, uint32_t source)
{
    VkDetach *d = malloc(sizeof(*d));
    if (!d)
        return;
    *d = (VkDetach){id, source};
    on_main(detach_main, d);
}

typedef struct VkPlace {
    uint32_t id, source, order;
    MimeRect area;
} VkPlace;

static void place_main(void *arg)
{
    VkPlace *p = arg;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(p->id);
    VkLayer *layer = w && w->vk ? find_layer(w, p->source) : nullptr;
    if (layer && !layer->under)
        place_layer(w, layer, p->area, p->order);
    pthread_mutex_unlock(&g_windows_lock);
    free(p);
}

/*
 * takes a place the guest told for a layer under its window, with the window's lock, and returns whether the window
 * is to be drawn for it. a place that says the pixels of the window follow lands with them, see
 * mime_ui_window_present: the guest tells of a child that changed size right before the pixels it painted around it.
 */
static bool tell_layer(UiWindow *w, VkLayer *l, const MimeUiPlace *place)
{
    LayerPlace p = {.set = true,
                    .client = physical_rect(w, place->client),
                    .visible = physical_rect(w, place->visible),
                    .order = place->order,
                    .stamp = place->stamp};
    if (rect_empty(p.visible))
        p.visible = (MimeRect){};
    /* only a notice tells what is covered. a frame keeps what the last one told, and so does a notice older than it. */
    const LayerPlace *t = &l->told;
    bool covers = place->hidden && (!t->hidden_stamp || !p.stamp || (int32_t)(p.stamp - t->hidden_stamp) >= 0);
    if (covers) {
        p.hidden_stamp = p.stamp;
        p.hidden_count = MIME_MIN(place->hidden_count, MIME_LAYER_HIDDEN_MAX);
        for (uint32_t i = 0; i < p.hidden_count; i++)
            p.hidden[i] = physical_rect(w, place->hidden[i]);
    } else {
        p.hidden_stamp = t->hidden_stamp;
        p.hidden_count = t->hidden_count;
        memcpy(p.hidden, t->hidden, sizeof(p.hidden));
    }
    bool hides = p.hidden_count == t->hidden_count && !memcmp(p.hidden, t->hidden, p.hidden_count * sizeof(MimeRect));
    /* a frame comes with the place its window had when the guest made it, which a notice may have overtaken. */
    if (l->told.set && p.stamp && l->told.stamp && (int32_t)(p.stamp - l->told.stamp) < 0) {
        if (hides)
            return false;
        /* the notice a frame overtook still tells what is covered. */
        LayerPlace *to[2] = {&l->told, l->waits ? nullptr : &l->shown};
        for (int i = 0; i < 2; i++) {
            if (!to[i])
                continue;
            to[i]->hidden_stamp = p.hidden_stamp;
            to[i]->hidden_count = p.hidden_count;
            memcpy(to[i]->hidden, p.hidden, sizeof(p.hidden));
        }
        return !l->waits;
    }
    bool same = l->told.set && !memcmp(&p.client, &l->told.client, sizeof(p.client)) &&
                !memcmp(&p.visible, &l->told.visible, sizeof(p.visible)) && p.order == l->told.order && hides;
    l->told = p;
    if (same)
        return false;
    if (place->follows && !l->waits) {
        l->waits = true;
        l->told_ms = now_ms();
    }
    if (l->waits)
        return false;
    l->shown = p;
    return true;
}

/*
 * sizes the next frame of a layer under its window, with the window's lock. the surface takes the client area and
 * what the window shows of the layer past it, inside the window. it starts where that starts, and the image lies in
 * it where the client area is, at its own size: a frame that is late for a change of size is cut or shows its edge
 * past its end, where Windows leaves the pixels of the frame before. the surface is no smaller than what a frame of
 * the window that may be on screen is clear over, or the desktop would show through. one that would reach out of the
 * window from where it is to start keeps its start, the image moving inside it instead.
 */
static void frame_under(UiWindow *w, VkLayer *l, MimeRect image, Fit fit, double density, MimeUiFrame *frame)
{
    MimeRect vis = visible_part(&w->config), host = host_part(&w->config);
    const LayerPlace *p = &l->told;
    frame->client = image;
    frame->area = (MimeRect){};
    frame->pixel_width = frame->pixel_height = 0;
    if (rect_empty(p->visible))
        return;
    MimeRect want = rect_and((MimeRect){MIME_MIN(p->client.left, p->visible.left), MIME_MIN(p->client.top, p->visible.top),
                                        MIME_MAX(p->client.right, p->visible.right),
                                        MIME_MAX(p->client.bottom, p->visible.bottom)},
                             vis);
    if (rect_empty(want))
        return;
    int32_t step = layer_step(fit), start[2];
    bool placed = l->subsurface != nullptr;
    LayerFrame f = {};
    for (int i = 0; i < 2; i++) {
        int32_t lo = i ? want.top : want.left, hi = i ? want.bottom : want.right;
        int32_t edge = i ? vis.top : vis.left, end = i ? vis.bottom : vis.right, base = i ? host.top : host.left;
        int32_t at = l->at[i], cut = placed ? l->cut[i] : 0;
        int32_t from = layer_start(lo, base, edge, step);
        int32_t size = MIME_MAX(hi - from, cut);
        if (placed && from > at && from + size > end) {
            from = at;
            size = MIME_MAX(hi - from, cut);
        }
        size = (size + step - 1) / step * step;
        size = MIME_MIN(size, (end - (placed ? MIME_MAX(from, at) : from)) / step * step);
        if (size <= 0 || hi <= from)
            return;
        start[i] = from;
        f.ext[i] = size;
        f.reach[i] = MIME_MIN(hi - from, size);
        f.off[i] = (i ? image.top : image.left) - from;
        f.lit[i] = MIME_MAX(lo - from, 0);
    }
    frame->opaque = want;
    int32_t width = MIME_MAX((int32_t)lround(f.ext[0] * fit.scale), 1), height = MIME_MAX((int32_t)lround(f.ext[1] * fit.scale), 1);
    frame->area = (MimeRect){start[0], start[1], start[0] + f.ext[0], start[1] + f.ext[1]};
    frame->pixel_width = (uint32_t)MIME_MAX(lround(width * density), 1);
    frame->pixel_height = (uint32_t)MIME_MAX(lround(height * density), 1);
    /* the size the display shows the buffer at is state of the surface, and lands with the buffer of that size. */
    if (l->viewport && (width != l->dest[0] || height != l->dest[1]))
        mime_wl_viewport_size(l->viewport, width, height);
    l->dest[0] = width;
    l->dest[1] = height;
    l->framed = f;
    l->framed_serial = frame->serial;
}

/* the area of a layer over the window: what shows of it. */
static MimeRect area_over(const MimeUiPlace *place)
{
    return rect_empty(place->visible) ? (MimeRect){} : place->visible;
}

bool mime_ui_vk_frame(uint32_t id, uint32_t source, const MimeUiPlace *place, MimeRect image, MimeUiFrame *frame)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id);
    if (!w || !w->vk) {
        pthread_mutex_unlock(&g_windows_lock);
        return false;
    }
    pthread_mutex_lock(&w->lock);
    pthread_mutex_unlock(&g_windows_lock);
    frame->host = host_part(&w->config);
    frame->visible = visible_part(&w->config);
    frame->opaque = (MimeRect){};
    /* guest window bounds in host pixels. */
    Fit fit = window_fit(w);
    double density = w->logical_width > 0 ? (double)w->pixel_width / w->logical_width : 1.0;
    frame->scale = fit.scale * density;
    frame->origin_x = fit.x * density;
    frame->origin_y = fit.y * density;
    frame->surface_width = w->pixels ? w->width : 0;
    frame->surface_height = w->pixels ? w->height : 0;
    frame->shown = w->shown && !(w->config.flags & MIME_WIN_MINIMIZED);
    frame->subsurface = true;
    MimeRect area = area_over(place);
    frame->client = physical_rect(w, place->client);
    frame->area = physical_rect(w, area);
    bool moved = false, redraw = false;
    if (frame->subsurface) {
        VkLayer *layer = find_layer(w, source);
        if (!layer || layer->gone) {
            pthread_mutex_unlock(&w->lock);
            return false;
        }
        frame->serial = ++layer->frames;
        if (layer->under) {
            redraw = tell_layer(w, layer, place) && !w->present_queued;
            if (redraw)
                w->present_queued = true;
            frame_under(w, layer, physical_rect(w, image), fit, density, frame);
        } else {
            /* the layer's area at the window's density. */
            MimeRect r = client_in_window(w, area);
            frame->pixel_width = (uint32_t)MIME_MAX(lround((r.right - r.left) * density), 0);
            frame->pixel_height = (uint32_t)MIME_MAX(lround((r.bottom - r.top) * density), 0);
            moved = memcmp(&r, &layer->place, sizeof(r)) != 0 || layer->order != place->order;
            /* the size the display shows the buffer at is state of the surface, and lands with the buffer. */
            if (layer->viewport && frame->pixel_width && frame->pixel_height) {
                layer->next_at[0] = r.left;
                layer->next_at[1] = r.top;
                layer->next_size[0] = r.right - r.left;
                layer->next_size[1] = r.bottom - r.top;
                layer->next_pixels[0] = (int32_t)frame->pixel_width;
                layer->next_pixels[1] = (int32_t)frame->pixel_height;
                frame->landing = w;
            }
        }
    } else {
        frame->pixel_width = w->pixel_width;
        frame->pixel_height = w->pixel_height;
    }
    pthread_mutex_unlock(&w->lock);
    if (moved) {
        VkPlace *p = malloc(sizeof(*p));
        if (p) {
            *p = (VkPlace){id, source, place->order, area};
            on_main(place_main, p);
        }
    }
    if (redraw)
        on_main(present_main, (void *)(uintptr_t)id);
    return true;
}

void mime_ui_vk_place(uint32_t id, uint32_t source, const MimeUiPlace *place)
{
    if (!g_video)
        return;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id);
    if (!w || !w->vk) {
        pthread_mutex_unlock(&g_windows_lock);
        return;
    }
    pthread_mutex_lock(&w->lock);
    pthread_mutex_unlock(&g_windows_lock);
    VkLayer *l = find_layer(w, source);
    MimeRect area = area_over(place);
    bool moved = false, redraw = false, wait = false;
    if (l && l->under && !l->gone) {
        bool waited = l->waits;
        redraw = tell_layer(w, l, place) && !w->present_queued;
        if (redraw)
            w->present_queued = true;
        wait = l->waits && !waited;
    } else if (l && !l->gone) {
        MimeRect r = client_in_window(w, area);
        moved = memcmp(&r, &l->place, sizeof(r)) != 0 || l->order != place->order;
    }
    pthread_mutex_unlock(&w->lock);
    if (moved) {
        VkPlace *p = malloc(sizeof(*p));
        if (p) {
            *p = (VkPlace){id, source, place->order, area};
            on_main(place_main, p);
        }
    }
    if (redraw)
        on_main(present_main, (void *)(uintptr_t)id);
    /* should the pixels that were said to follow not come. */
    if (wait)
        mime_wl_timer(REDRAW_MS, present_main, (void *)(uintptr_t)id);
}

/*
 * the size the display shows the buffer of a layer at, and the part of it, are state of its surface that lands with
 * the buffer. the window of a frame that is late may be smaller by now than the one the guest made it for.
 */
bool mime_ui_vk_landing(const MimeUiFrame *frame, uint32_t source)
{
    UiWindow *w = frame->landing;
    if (!w)
        return false;
    pthread_mutex_lock(&w->lock);
    VkLayer *l = w->vk ? find_layer(w, source) : nullptr;
    if (!l || l->under || l->gone || !l->viewport || frame->serial != l->frames) {
        pthread_mutex_unlock(&w->lock);
        return false;
    }
    int32_t want[2];
    bool cut = layer_cut(w, l->next_at[0], l->next_at[1], l->next_size, want);
    if (cut)
        mime_wl_viewport_part(l->viewport, (double)l->next_pixels[0] * want[0] / l->next_size[0],
                              (double)l->next_pixels[1] * want[1] / l->next_size[1], want[0], want[1]);
    else
        mime_wl_viewport_whole(l->viewport, l->next_size[0], l->next_size[1]);
    memcpy(l->have_at, l->next_at, sizeof(l->have_at));
    memcpy(l->have_size, l->next_size, sizeof(l->have_size));
    memcpy(l->have_pixels, l->next_pixels, sizeof(l->have_pixels));
    l->cut_size[0] = cut ? want[0] : 0;
    l->cut_size[1] = cut ? want[1] : 0;
    return true;
}

void mime_ui_vk_landed(const MimeUiFrame *frame)
{
    UiWindow *w = frame->landing;
    pthread_mutex_unlock(&w->lock);
}

void mime_ui_vk_shown(uint32_t id, uint32_t source, uint32_t serial, bool shown)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id);
    if (!w) {
        pthread_mutex_unlock(&g_windows_lock);
        return;
    }
    pthread_mutex_lock(&w->lock);
    pthread_mutex_unlock(&g_windows_lock);
    VkLayer *l = w->vk ? find_layer(w, source) : nullptr;
    bool redraw = false;
    if (l && l->under && serial == l->framed_serial) {
        if (!shown) {
            l->framed = l->covered;
        } else if (memcmp(&l->covered, &l->framed, sizeof(l->covered))) {
            /* the window is clear over what the layer newly covers, and the layer moves to where this frame goes. */
            l->covered = l->framed;
            redraw = !w->present_queued;
            if (redraw)
                w->present_queued = true;
        }
    }
    pthread_mutex_unlock(&w->lock);
    if (redraw)
        on_main(present_main, (void *)(uintptr_t)id);
}

/* a window always presents through layers here, so none draws the frame of its window itself. */
bool mime_ui_vk_copy(uint32_t id, uint8_t *dst, uint32_t width, uint32_t height, MimeRect *dirty)
{
    return false;
}

/* ------------------------------------------------------------------ cursor. */

static void cursor_main(void *arg)
{
    uint32_t c = (uint32_t)(uintptr_t)arg;
    if (c == g_cursor)
        return;
    g_cursor = c;
    /* MIME_CURSOR_HIDDEN and past it hide the cursor. */
    mime_wl_cursor_set(nullptr, c);
}

void mime_ui_set_cursor(uint32_t cursor)
{
    on_main(cursor_main, (void *)(uintptr_t)cursor);
}

typedef struct CursorImage {
    uint32_t width, height, hot_x, hot_y, dpi;
    uint64_t hash;
    uint8_t pixels[];
} CursorImage;

/* programs switch between a few cursors of their own on every move, so the last ones made are kept. */
static struct {
    uint64_t hash;
    MimeWlCursor *cursor;
    uint64_t used;
} g_image_cursors[16];
static uint64_t g_image_cursor_clock;

static void cursor_image_main(void *arg)
{
    CursorImage *image = arg;
    int slot = 0;
    for (int i = 0; i < (int)MIME_ARRAY_COUNT(g_image_cursors); i++) {
        if (g_image_cursors[i].cursor && g_image_cursors[i].hash == image->hash) {
            slot = i;
            goto show;
        }
        if (g_image_cursors[i].used < g_image_cursors[slot].used)
            slot = i;
    }
    /* the compositor sizes a cursor in points: the image of a guest that is scaled is the sharp one of a smaller cursor. */
    MimeWlCursor *cursor = mime_wl_cursor_create(image->pixels, (int)image->width, (int)image->height, (int)image->hot_x,
                                                 (int)image->hot_y, image->dpi / 96.0);
    if (!cursor) {
        free(image);
        cursor_main((void *)(uintptr_t)MIME_CURSOR_ARROW);
        return;
    }
    MimeWlCursor *old = g_image_cursors[slot].cursor;
    g_image_cursors[slot].cursor = cursor;
    g_image_cursors[slot].hash = image->hash;
    mime_wl_cursor_set(cursor, 0);
    mime_wl_cursor_destroy(old);
show:
    free(image);
    g_image_cursors[slot].used = ++g_image_cursor_clock;
    g_cursor = UINT32_MAX;
    mime_wl_cursor_set(g_image_cursors[slot].cursor, 0);
}

void mime_ui_set_cursor_image(const void *pixels, uint32_t width, uint32_t height, uint32_t stride, uint32_t hot_x,
                              uint32_t hot_y, uint32_t dpi)
{
    if (!g_video)
        return;
    CursorImage *image = mime_xmalloc(sizeof(*image) + (size_t)width * height * 4);
    *image = (CursorImage){.width = width, .height = height, .hot_x = hot_x, .hot_y = hot_y, .dpi = dpi ? dpi : 96};
    uint64_t hash = 1469598103934665603ull ^ width ^ (uint64_t)height << 16 ^ (uint64_t)hot_x << 32 ^ (uint64_t)hot_y << 48;
    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row = image->pixels + (size_t)y * width * 4;
        memcpy(row, (const uint8_t *)pixels + (size_t)y * stride, (size_t)width * 4);
        for (uint32_t x = 0; x < width * 4; x++)
            hash = (hash ^ row[x]) * 1099511628211ull;
    }
    image->hash = hash ^ dpi;
    on_main(cursor_image_main, image);
}

typedef struct PointerCall {
    uint32_t mode, window_id;
    MimeRect rect;
} PointerCall;

static void pointer_main(void *arg)
{
    PointerCall *c = arg;
    static uint32_t held;
    UiWindow *w = find_window(c->window_id), *before = held ? find_window(held) : nullptr;
    MimeWlWindow *window = w && c->mode != MIME_POINTER_FREE ? w->window : nullptr;
    if (before && before->window && before->window != window) {
        mime_wl_pointer_lock(before->window, false);
        mime_wl_pointer_confine(before->window, nullptr);
    }
    held = window ? w->id : 0;
    if (window) {
        mime_wl_pointer_lock(window, c->mode == MIME_POINTER_RELATIVE);
        if (c->mode == MIME_POINTER_CONFINED) {
            MimeRect host = host_part(&w->config);
            Fit fit = window_fit(w);
            double left = fit.x + (c->rect.left - w->config.rect.left - host.left) * fit.scale;
            double top = fit.y + (c->rect.top - w->config.rect.top - host.top) * fit.scale;
            int width = (int)floor((c->rect.right - c->rect.left) * fit.scale);
            int height = (int)floor((c->rect.bottom - c->rect.top) * fit.scale);
            MimeRect r = {(int)ceil(left), (int)ceil(top), (int)ceil(left) + width, (int)ceil(top) + height};
            mime_wl_pointer_confine(window, width > 0 && height > 0 ? &r : nullptr);
        } else {
            mime_wl_pointer_confine(window, nullptr);
        }
    }
    free(c);
}

void mime_ui_pointer(uint32_t mode, uint32_t window_id, const MimeRect *rect)
{
    PointerCall *c = malloc(sizeof(*c));
    if (!c)
        return;
    *c = (PointerCall){mode, window_id, *rect};
    if (!g_video) {
        free(c);
        return;
    }
    on_main(pointer_main, c);
}

void mime_ui_beep(uint32_t type)
{
}

/* a new ramp shows on every window drawn here at once; swapchains take it at their next present. */
static void regrade_main(void *arg)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        UiWindow *w = &g_windows[i];
        if (!w->used || !w->window || !w->pixels || !w->shown)
            continue;
        mark_dirty(w);
        draw(w);
    }
}

void mime_ui_gamma(const uint16_t *ramp)
{
    mime_gamma_set(ramp);
    if (g_video)
        on_main(regrade_main, nullptr);
}

static void report_size(UiWindow *w);

static void size_recheck_main(void *arg)
{
    UiWindow *w = find_window((uint32_t)(uintptr_t)arg);
    if (w) {
        w->size_recheck = false;
        report_size(w);
    }
}

/*
 * reports compositor-initiated maximization, tiling and interactive resizing. ignores events that echo a
 * guest size request.
 */
static void report_size(UiWindow *w)
{
    if (!w->window || w->popup || w->fullscreen || !w->configured || !w->shown || w->bogus_size)
        return;
    uint64_t now = now_ms();
    int logical_w, logical_h;
    mime_wl_window_get_told(w->window, &logical_w, &logical_h);
    bool interactive = w->interactive_until > now;
    if (!w->maximized && !interactive) {
        if (logical_w == w->want_w && logical_h == w->want_h)
            return;
        /* this may predate the guest request; check again after the request has had time to complete. */
        if (now - w->want_ms < 250) {
            if (!w->size_recheck) {
                w->size_recheck = true;
                mime_wl_timer(260, size_recheck_main, (void *)(uintptr_t)w->id);
            }
            return;
        }
    }
    if (interactive)
        w->interactive_until = now + 500;
    MimeRect host = host_part(&w->config);
    double scale = config_scale(&w->config);
    int32_t width = (int32_t)lround(logical_w * scale), height = (int32_t)lround(logical_h * scale);
    /*
     * a tiling compositor goes below the smallest size the program accepts. the guest gets the smallest size of the
     * same shape, which the fit shows over the whole host window.
     */
    int32_t min_w = w->config.min_width - (w->config.rect.right - w->config.rect.left - (host.right - host.left));
    int32_t min_h = w->config.min_height - (w->config.rect.bottom - w->config.rect.top - (host.bottom - host.top));
    if (w->config.min_width <= 0)
        min_w = 0;
    if (w->config.min_height <= 0)
        min_h = 0;
    if (width > 0 && height > 0 && (width < min_w || height < min_h)) {
        double grow = fmax((double)min_w / width, (double)min_h / height);
        width = MIME_MAX(min_w, (int32_t)lround(width * grow));
        height = MIME_MAX(min_h, (int32_t)lround(height * grow));
    }
    if (abs(width - (host.right - host.left)) <= 1 && abs(height - (host.bottom - host.top)) <= 1)
        return;
    w->size_answered = false;
    MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_SIZE, .window_id = w->id, .width = width, .height = height};
    emit(&ev);
}

/* tells the guest which monitor a window is on, as the compositor shows it. */
static void report_window_display(UiWindow *w, uint32_t display)
{
    if (w->popup || !display || w->display == display)
        return;
    int index = monitor_index(display);
    if (index < 0)
        return;
    w->display = display;
    MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_DISPLAY, .window_id = w->id, .x = index};
    emit(&ev);
}

/* tells the guest of the displays when they are no longer what it knows. */
static void displays_changed(void *arg)
{
    MimeDisplayInfo before = g_display;
    HostMonitor old[MIME_MAX_MONITORS];
    pthread_mutex_lock(&g_monitor_lock);
    uint32_t old_count = g_monitor_count;
    memcpy(old, g_monitors, sizeof(old));
    pthread_mutex_unlock(&g_monitor_lock);
    update_display();
    bool changed = memcmp(&before, &g_display, sizeof(before)) != 0 || old_count != g_monitor_count;
    for (uint32_t i = 0; !changed && i < old_count; i++)
        changed = old[i].id != g_monitors[i].id || memcmp(&old[i].info, &g_monitors[i].info, sizeof(MimeMonitor));
    if (changed) {
        MimeInputEvent ev = {.type = MIME_INPUT_DISPLAY, .x = (int32_t)g_display.width,
                             .y = (int32_t)g_display.height};
        emit(&ev);
        /* the indices of the windows' displays moved with the list. */
        for (int i = 0; i < MAX_WINDOWS; i++) {
            UiWindow *w = &g_windows[i];
            if (w->used && w->window && w->shown && !w->popup) {
                w->display = 0;
                report_window_display(w, mime_wl_window_output(w->window));
            }
        }
    }
}

/* ------------------------------------------------------------------ what the compositor tells. */

static uint64_t g_events;

/* an empty composition followed by no other text means cancellation, and an alt character waits for text too. */
static void before_event(bool text, bool editing)
{
    g_events++;
    if (!text && !editing)
        mime_ime_flush();
    if (text)
        g_alt_char_pending = false;
    else
        alt_char_flush();
}

static void on_key(MimeWlWindow *window, uint32_t key, bool down, bool repeat)
{
    before_event(false, false);
    if (!g_layout_known)
        update_layout();
    UiWindow *w = ui_of(window);
    if (w)
        emit_key(w, key, down, repeat);
}

static void on_keymap(void)
{
    update_layout();
}

static void on_text(MimeWlWindow *window, const char *text)
{
    before_event(true, false);
    UiWindow *w = ui_of(window);
    if (w && !mime_ime_commit(w->id, text))
        emit_text(w, text);
}

static void on_editing(MimeWlWindow *window, const char *text, int start, int length)
{
    before_event(false, true);
    UiWindow *w = ui_of(window);
    if (w)
        mime_ime_editing(w->id, text, start, length);
}

static void on_motion(MimeWlWindow *window, double x, double y, double dx, double dy)
{
    before_event(false, false);
    if (g_drag_window)
        drag_pointer_back();
    UiWindow *w = ui_of(window);
    if (w)
        emit_motion(w, x, y, dx, dy);
}

static void on_button(MimeWlWindow *window, double x, double y, uint32_t button, bool down)
{
    before_event(false, false);
    if (g_drag_window)
        drag_pointer_back();
    UiWindow *w = ui_of(window);
    uint32_t which = mouse_button(button);
    if (w && which)
        emit_mouse(w, x, y, down ? MIME_INPUT_MOUSE_DOWN : MIME_INPUT_MOUSE_UP, which);
}

static void on_wheel(MimeWlWindow *window, double x, double y, double wx, double wy)
{
    before_event(false, false);
    UiWindow *w = ui_of(window);
    if (w)
        emit_wheel(w, x, y, wx, wy);
}

static void on_leave(MimeWlWindow *window)
{
    before_event(false, false);
    UiWindow *w = ui_of(window);
    if (w) {
        MimeInputEvent ev = {.type = MIME_INPUT_MOUSE_LEAVE, .window_id = w->id};
        emit(&ev);
    }
}

static void on_focus(MimeWlWindow *window, bool gained)
{
    before_event(false, false);
    UiWindow *w = ui_of(window);
    /* a popup that grabs the keyboard takes it from its owner without the guest's window losing the activation. */
    if (!w || w->popup || (!gained && has_open_popup(w)))
        return;
    MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_FOCUS, .window_id = w->id, .x = gained};
    emit(&ev);
    /* the input method of the window that has the keyboard needs to know where text is entered. */
    if (gained)
        apply_text_area();
}

static void on_close(MimeWlWindow *window)
{
    before_event(false, false);
    UiWindow *w = ui_of(window);
    if (w) {
        MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_CLOSE, .window_id = w->id};
        emit(&ev);
    }
}

static void on_restored(UiWindow *w)
{
    if (w->config.flags & MIME_WIN_MINIMIZED) {
        request_window_state(w, 9);
    } else if (w->maximized && !mime_wl_window_maximized(w->window)) {
        /* the compositor took the window out of the maximized state, as it does when it is dragged. */
        w->maximized = false;
        if (w->config.flags & MIME_WIN_MAXIMIZED) {
            request_window_state(w, 9);
        } else if (!w->popup && !w->fullscreen) {
            /* the guest restored it. kwin leaves the size of a window it restores to the client. */
            MimeRect host = host_part(&w->config);
            double scale = config_scale(&w->config);
            w->want_w = MIME_MAX(1, (int)lround((host.right - host.left) / scale));
            w->want_h = MIME_MAX(1, (int)lround((host.bottom - host.top) / scale));
            w->want_ms = now_ms();
            mime_wl_window_size(w->window, w->want_w, w->want_h);
        }
    }
}

static void on_fullscreen(UiWindow *w, bool enter)
{
    /* report compositor changes, skipping echoes of guest requests. */
    if (w->popup || w->fullscreen == enter || now_ms() - w->fullscreen_ms <= 500)
        return;
    w->fullscreen = enter;
    MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_FULLSCREEN, .window_id = w->id, .flags = enter};
    MimeRect rect = {0, 0, 0, 0};
    if (enter) {
        uint32_t display = mime_wl_window_output(w->window);
        pthread_mutex_lock(&g_monitor_lock);
        for (uint32_t i = 0; i < g_monitor_count; i++)
            if (g_monitors[i].id == display)
                rect = g_monitors[i].info.rect;
        pthread_mutex_unlock(&g_monitor_lock);
        if (rect.right <= rect.left)
            rect = (MimeRect){0, 0, (int32_t)g_display.width, (int32_t)g_display.height};
    }
    ev.x = rect.left;
    ev.y = rect.top;
    ev.width = rect.right - rect.left;
    ev.height = rect.bottom - rect.top;
    emit(&ev);
}

static void on_state(MimeWlWindow *window, MimeWlState state)
{
    before_event(false, false);
    UiWindow *w = ui_of(window);
    if (!w)
        return;
    switch (state) {
    case MIME_WL_MINIMIZED:
        if (!(w->config.flags & MIME_WIN_MINIMIZED))
            request_window_state(w, 6);
        break;
    case MIME_WL_RESTORED:
        on_restored(w);
        break;
    case MIME_WL_RESIZE_ENDED:
        drag_ended(w);
        break;
    case MIME_WL_MAXIMIZED:
        /*
         * a window nobody maximized, called so as it opens or as it leaves the whole screen: the compositor says it of
         * every window.
         */
        if (!w->popup && !w->maximized && !(w->config.flags & MIME_WIN_MAXIMIZED) &&
            (now_ms() - w->shown_ms < 1000 || now_ms() - w->fullscreen_ms < 1000))
            w->says_maximized = g_says_maximized = true;
        if (!w->popup && !w->says_maximized) {
            w->maximized = true;
            if (!(w->config.flags & MIME_WIN_MAXIMIZED) && !w->fullscreen)
                request_window_state(w, 3);
        }
        break;
    case MIME_WL_FULLSCREEN_ENTERED:
    case MIME_WL_FULLSCREEN_LEFT:
        on_fullscreen(w, state == MIME_WL_FULLSCREEN_ENTERED);
        break;
    }
}

static void on_resized(MimeWlWindow *window, bool sized)
{
    before_event(false, false);
    UiWindow *w = ui_of(window);
    if (!w)
        return;
    update_pixel_size(w);
    if (sized)
        report_size(w);
    if (w->shown)
        draw(w);
}

static void on_redraw(MimeWlWindow *window)
{
    UiWindow *w = ui_of(window);
    if (w && w->shown)
        draw(w);
}

static void on_output(MimeWlWindow *window, uint32_t output)
{
    before_event(false, false);
    UiWindow *w = ui_of(window);
    if (w)
        report_window_display(w, output);
}

static void on_outputs(bool listed)
{
    before_event(false, false);
    if (listed)
        mime_color_forget();
    displays_changed(nullptr);
}

/* ------------------------------------------------------------------ main loop. */

void mime_ui_init(MimeUiInputSink sink, void *ctx)
{
    g_sink = sink;
    g_sink_ctx = ctx;
    g_main_thread = pthread_self();
    static const MimeWlHandlers handlers = {
        .key = on_key,       .keymap = on_keymap, .text = on_text,     .editing = on_editing, .motion = on_motion,
        .button = on_button, .wheel = on_wheel,   .leave = on_leave,   .focus = on_focus,     .close = on_close,
        .state = on_state,   .resized = on_resized, .redraw = on_redraw, .output = on_output, .outputs = on_outputs,
    };
    if (!mime_wl_init(&handlers)) {
        mime_info("no wayland display: windows are not shown");
        return;
    }
    g_video = true;
    /* seen on a window that opens, which is too late for a program whose first window opens maximized. */
    g_says_maximized = getenv("HYPRLAND_INSTANCE_SIGNATURE") != nullptr;
    mime_color_watch(color_changed);
    update_display();
    update_layout();
    mime_dnd_init();
    mime_clipboard_init();
}

void mime_ui_run(void)
{
    mime_gamepad_start();
    if (!g_video) {
        pthread_mutex_lock(&g_stop_lock);
        while (!g_stop)
            pthread_cond_wait(&g_stop_cond, &g_stop_lock);
        pthread_mutex_unlock(&g_stop_lock);
        return;
    }
    while (!g_stop) {
        /* defer composition clears and Alt characters until the next event shows whether text follows. */
        int timeout = mime_ime_pending() || g_alt_char_pending ? (g_alt_char_pending ? 10 : 30) : -1;
        uint64_t seen = g_events, began = now_ms();
        if (!mime_wl_wait(timeout)) {
            mime_warn("the compositor closed the connection");
            break;
        }
        if (timeout >= 0 && g_events == seen && now_ms() - began >= (uint64_t)timeout) {
            mime_ime_flush();
            alt_char_flush();
        }
    }
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used)
            destroy_host_window(&g_windows[i]);
    mime_color_stop();
    mime_wl_quit();
}

static void stop_main(void *arg)
{
    g_stop = true;
}

void mime_ui_stop(void)
{
    pthread_mutex_lock(&g_stop_lock);
    g_stop = true;
    pthread_cond_broadcast(&g_stop_cond);
    pthread_mutex_unlock(&g_stop_lock);
    if (g_video)
        mime_wl_post(stop_main, nullptr);
}

/* macOS uses this entry for per-program app bundles; Linux needs no companion processes. */
int mime_app_proxy_main(int argc, char **argv)
{
    return -1;
}

/* vkwsi.c supplies this entry without exposing Vulkan headers here. */
bool mvk_wsi_readback(uint32_t window, uint32_t source, void *pixels, uint32_t capacity_width, uint32_t capacity_height,
                      uint32_t stride, uint32_t *width, uint32_t *height, MimeRect *client, uint64_t *serial,
                      uint32_t *flags);

bool mime_ui_window_readback(MimeWinReadback *r, void *pixels)
{
    return mvk_wsi_readback(r->window_id, r->source, pixels, r->capacity_width, r->capacity_height, r->stride, &r->width,
                            &r->height, &r->client, &r->serial, &r->flags);
}
