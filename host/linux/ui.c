/*
 * Linux display through SDL3 on Wayland or X11. each guest top level window has a borderless host window
 * showing the guest's pixels, including its frame. without a display server, the machine runs headless.
 *
 * SDL video calls run on the main thread, which initializes it. vCPU threads copy presented pixels into
 * staging buffers and queue other work for the main loop.
 */
#include "../vulkan/display.h"
#include "../vulkan/gamma.h"
#include "ui_internal.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <wayland-client.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_WINDOWS 256
#define MAX_VK_LAYERS 8

/* a surface over the window for each window of the guest that presents, stacked by the guest's paint order. */
typedef struct VkLayer {
    bool used;
    uint32_t source; /* the guest window it presents for. */
    uint32_t order;  /* place in the paint order of the window's tree, the front ones last. */
    struct wl_surface *surface;
    struct wl_subsurface *subsurface; /* none while nothing of it shows. */
    struct wl_proxy *viewport;
    MimeRect place; /* where the subsurface is, in SDL window coordinates. */
} VkLayer;

typedef struct UiWindow {
    uint32_t id;
    bool used;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t texture_width, texture_height;
    MimeWinConfig config;
    bool configured;
    bool shown;
    bool popup;          /* created as a popup of its owner. */
    uint32_t owner;
    bool fullscreen;
    bool vk;       /* a Vulkan presenter shows the client area. */
    bool vk_whole; /* and the frame too, the renderer gone: the window has no subsurface. */
    VkLayer vk_layers[MAX_VK_LAYERS];
    bool state_pending;
    uint32_t state_request;
    int state_command;
    SDL_DisplayID display; /* the display last reported to the guest. */
    bool maximized;        /* the compositor has the window maximized. */
    bool says_maximized;   /* the compositor calls it maximized whatever its state, see set_maximized. */
    uint64_t shown_ms;     /* when the host window was last shown. */
    bool bogus_size;       /* the compositor gave a size no display could show, which is not acted on. */
    uint64_t renderer_retry_ms; /* a renderer that failed, as with the gpu's memory full, is tried again then. */
    int geometry_w, geometry_h; /* the window geometry last given to the compositor, in surface coordinates. */
    int want_w, want_h;    /* the last size asked of the compositor, in SDL window coordinates. */
    uint64_t want_ms;      /* time of the last size request. */
    uint64_t fullscreen_ms; /* when the guest last asked for or left fullscreen. */
    uint64_t interactive_until; /* the compositor sizes the window under the pointer, as long as sizes come. */
    bool size_recheck;     /* check the size after the guest request has had time to complete. */
    double popup_scale;    /* a popup is shown at the scale of its owner. */
    char want_app_id[200]; /* requested application id and the id currently on the toplevel. */
    char set_app_id[200];
    double wheel_x, wheel_y;
    double motion_x, motion_y; /* fractions of guest pixels moved, kept for the next motion. */

    pthread_mutex_t lock;
    uint8_t *pixels; /* the latest guest surface, copied by the presenting vCPU. */
    uint32_t width, height;
    uint32_t pixel_width, pixel_height;
    int32_t logical_width, logical_height; /* the host window in SDL window coordinates. */
    MimeRect dirty;
    bool has_dirty;
    bool present_queued;
    MimeWinConfig pending_config;
    bool config_queued;
} UiWindow;

static UiWindow g_windows[MAX_WINDOWS];
/* guest processors take it, so the main thread lets go of it before an SDL call that can dispatch the compositor's events. */
static pthread_mutex_t g_windows_lock = PTHREAD_MUTEX_INITIALIZER;
static MimeUiInputSink g_sink;
static void *g_sink_ctx;
static bool g_video;          /* SDL runs; without it the display is headless. */
static Uint32 g_call_event;   /* the event type of calls posted to the main thread. */
static pthread_t g_main_thread;
static double g_scale = 1.0;  /* guest pixels per SDL window coordinate of the primary display, the system's. */
static MimeDisplayInfo g_display;
static void raise_window(UiWindow *w);
static bool g_wayland;        /* the video driver is Wayland's. */
static bool g_gl;             /* gl stays loaded for the windows' renderers. */

/* the displays of the host, the primary first, in the guest's virtual screen. */
typedef struct HostMonitor {
    SDL_DisplayID id;
    double scale;
    MimeMonitor info;
} HostMonitor;

static HostMonitor g_monitors[MIME_MAX_MONITORS];
static uint32_t g_monitor_count;
static pthread_mutex_t g_monitor_lock = PTHREAD_MUTEX_INITIALIZER;
static SDL_Cursor *g_cursors[MIME_CURSOR_HIDDEN];
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
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = g_call_event;
    ev.user.data1 = (void *)fn;
    ev.user.data2 = arg;
    SDL_PushEvent(&ev);
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

static UiWindow *find_by_sdl(SDL_WindowID id)
{
    SDL_Window *window = SDL_GetWindowFromID(id);
    if (!window)
        return nullptr;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].window == window)
            return &g_windows[i];
    return nullptr;
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
 * maps guest window coordinates to SDL coordinates with a scale and offset. ordinary windows use the display
 * scale. fullscreen windows preserve their aspect ratio and fill unused space with black, like a monitor
 * displaying a smaller mode.
 */
typedef struct Fit {
    double scale;
    double x, y;
} Fit;

/* guest pixels per SDL coordinate, using the program's DPI. */
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

static uint32_t modifiers_of(SDL_Keymod mod)
{
    uint32_t m = 0;
    if (mod & SDL_KMOD_SHIFT)
        m |= MIME_MOD_SHIFT;
    if (mod & SDL_KMOD_CTRL)
        m |= MIME_MOD_CONTROL;
    if (mod & SDL_KMOD_ALT)
        m |= MIME_MOD_ALT;
    if (mod & SDL_KMOD_GUI)
        m |= MIME_MOD_WIN;
    if (mod & SDL_KMOD_CAPS)
        m |= MIME_MOD_CAPS;
    return m;
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
    SDL_Window *focus = SDL_GetKeyboardFocus();
    if (focus)
        w = find_by_sdl(SDL_GetWindowID(focus));
    if (!w || !w->window)
        w = find_window(g_text_area.window);
    if (!w || !w->window)
        return;
    MimeRect host = host_part(&w->config);
    Fit fit = window_fit(w);
    int32_t left = w->config.rect.left + host.left, top = w->config.rect.top + host.top;
    MimeRect r = g_text_area.screen;
    SDL_Rect rect = {(int)lround(fit.x + (r.left - left) * fit.scale), (int)lround(fit.y + (r.top - top) * fit.scale),
                     MIME_MAX(1, (int)lround((r.right - r.left) * fit.scale)),
                     MIME_MAX(1, (int)lround((r.bottom - r.top) * fit.scale))};
    /* the caret is an offset from the left of the area. */
    SDL_SetTextInputArea(w->window, &rect, (int)lround((g_text_area.cursor - r.left) * fit.scale));
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

/*
 * SDL has no Wayland composition cancellation call. restarting text input resets the input method's state as
 * required by the protocol.
 */
void mime_ui_clear_composition(uint32_t id)
{
    UiWindow *w = find_window(id);
    SDL_Window *focus = SDL_GetKeyboardFocus();
    SDL_Window *window = focus ? focus : (w ? w->window : nullptr);
    if (!window)
        return;
    if (g_wayland && SDL_TextInputActive(window)) {
        SDL_StopTextInput(window);
        SDL_StartTextInput(window);
    } else {
        SDL_ClearComposition(window);
    }
}

uint32_t mime_ui_modifiers(void)
{
    return modifiers_of(SDL_GetModState());
}

void *mime_ui_window_surface(uint32_t id)
{
    UiWindow *w = find_window(id);
    if (!w || !w->window)
        return nullptr;
    return SDL_GetPointerProperty(SDL_GetWindowProperties(w->window), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
}

bool mime_ui_surface_point(void *surface, double x, double y, uint32_t *window, int32_t *gx, int32_t *gy)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        UiWindow *w = &g_windows[i];
        if (!w->used || !w->window || !w->shown)
            continue;
        void *main_surface = SDL_GetPointerProperty(SDL_GetWindowProperties(w->window),
                                                    SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
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

typedef struct Placed {
    SDL_DisplayID id;
    SDL_Rect bounds, usable; /* in the compositor's coordinates. */
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
                SDL_Rect *m = &d[i].bounds;
                const SDL_Rect *r = &p->bounds;
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

/*
 * MIME_HDR=off tells the guest of standard dynamic range outputs only, MIME_HDR=force of a 1000 nit one whatever the
 * output is, which the compositor then maps to what it has.
 */
static void display_color(SDL_DisplayID id, Placed *m)
{
    const char *env = getenv("MIME_HDR");
    if (env && !strcmp(env, "off"))
        return;
#ifdef SDL_PROP_DISPLAY_WAYLAND_WL_OUTPUT_POINTER
    if (g_wayland) {
        struct wl_display *display = SDL_GetPointerProperty(SDL_GetGlobalProperties(),
                                                            SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER, nullptr);
        struct wl_output *output = SDL_GetPointerProperty(SDL_GetDisplayProperties(id),
                                                          SDL_PROP_DISPLAY_WAYLAND_WL_OUTPUT_POINTER, nullptr);
        mime_color_output(display, output, &m->hdr, &m->color);
    }
#endif
    if (env && !strcmp(env, "force") && !m->hdr) {
        m->hdr = true;
        m->color = (MimeMonitorColor){708000, 292000, 170000, 797000, 131000, 46000, 312700, 329000,
                                      50, 1000 * 10000, 600 * 10000, 203 * 10000};
    }
}

static void update_display(void)
{
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    int n = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&n);
    Placed d[MIME_MAX_MONITORS];
    uint32_t count = 0;
    for (int i = 0; ids && i < n && count < MIME_MAX_MONITORS; i++) {
        Placed *m = &d[count];
        memset(m, 0, sizeof(*m));
        m->id = ids[i];
        if (!SDL_GetDisplayBounds(ids[i], &m->bounds))
            continue;
        if (!SDL_GetDisplayUsableBounds(ids[i], &m->usable))
            m->usable = m->bounds;
        /* Wayland tells a display's scale as the pixel density of its mode, other systems as a content scale. */
        float scale = SDL_GetDisplayContentScale(ids[i]);
        const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(ids[i]);
        if (desktop && desktop->pixel_density >= 1.0f)
            scale *= desktop->pixel_density;
        m->scale = scale >= 1.0f ? scale : 1.0;
        m->width = (int32_t)lround(m->bounds.w * m->scale);
        m->height = (int32_t)lround(m->bounds.h * m->scale);
        const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(ids[i]);
        m->refresh = mode ? mode->refresh_rate : 0;
        const char *name = SDL_GetDisplayName(ids[i]);
        strncpy(m->name, name ? name : "", sizeof(m->name) - 1);
        display_color(ids[i], m);
        count++;
    }
    SDL_free(ids);
    if (!count)
        return;
    /* the primary first, then left to right. */
    uint32_t first = 0;
    for (uint32_t i = 0; i < count; i++)
        if (d[i].id == primary)
            first = i;
    Placed tmp = d[0];
    d[0] = d[first];
    d[first] = tmp;
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
static SDL_DisplayID display_of_rect(const MimeRect *r)
{
    SDL_DisplayID best = 0;
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
static int monitor_index(SDL_DisplayID id)
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

/* by USB usage, the SDL scancode. */
static const KeyMap g_keymap[SDL_SCANCODE_COUNT] = {
    [SDL_SCANCODE_A] = {'A', 0x1E}, [SDL_SCANCODE_B] = {'B', 0x30}, [SDL_SCANCODE_C] = {'C', 0x2E},
    [SDL_SCANCODE_D] = {'D', 0x20}, [SDL_SCANCODE_E] = {'E', 0x12}, [SDL_SCANCODE_F] = {'F', 0x21},
    [SDL_SCANCODE_G] = {'G', 0x22}, [SDL_SCANCODE_H] = {'H', 0x23}, [SDL_SCANCODE_I] = {'I', 0x17},
    [SDL_SCANCODE_J] = {'J', 0x24}, [SDL_SCANCODE_K] = {'K', 0x25}, [SDL_SCANCODE_L] = {'L', 0x26},
    [SDL_SCANCODE_M] = {'M', 0x32}, [SDL_SCANCODE_N] = {'N', 0x31}, [SDL_SCANCODE_O] = {'O', 0x18},
    [SDL_SCANCODE_P] = {'P', 0x19}, [SDL_SCANCODE_Q] = {'Q', 0x10}, [SDL_SCANCODE_R] = {'R', 0x13},
    [SDL_SCANCODE_S] = {'S', 0x1F}, [SDL_SCANCODE_T] = {'T', 0x14}, [SDL_SCANCODE_U] = {'U', 0x16},
    [SDL_SCANCODE_V] = {'V', 0x2F}, [SDL_SCANCODE_W] = {'W', 0x11}, [SDL_SCANCODE_X] = {'X', 0x2D},
    [SDL_SCANCODE_Y] = {'Y', 0x15}, [SDL_SCANCODE_Z] = {'Z', 0x2C},
    [SDL_SCANCODE_1] = {'1', 0x02}, [SDL_SCANCODE_2] = {'2', 0x03}, [SDL_SCANCODE_3] = {'3', 0x04},
    [SDL_SCANCODE_4] = {'4', 0x05}, [SDL_SCANCODE_5] = {'5', 0x06}, [SDL_SCANCODE_6] = {'6', 0x07},
    [SDL_SCANCODE_7] = {'7', 0x08}, [SDL_SCANCODE_8] = {'8', 0x09}, [SDL_SCANCODE_9] = {'9', 0x0A},
    [SDL_SCANCODE_0] = {'0', 0x0B},
    [SDL_SCANCODE_RETURN] = {0x0D, 0x1C}, [SDL_SCANCODE_ESCAPE] = {0x1B, 0x01},
    [SDL_SCANCODE_BACKSPACE] = {0x08, 0x0E}, [SDL_SCANCODE_TAB] = {0x09, 0x0F},
    [SDL_SCANCODE_SPACE] = {0x20, 0x39}, [SDL_SCANCODE_MINUS] = {0xBD, 0x0C},
    [SDL_SCANCODE_EQUALS] = {0xBB, 0x0D}, [SDL_SCANCODE_LEFTBRACKET] = {0xDB, 0x1A},
    [SDL_SCANCODE_RIGHTBRACKET] = {0xDD, 0x1B}, [SDL_SCANCODE_BACKSLASH] = {0xDC, 0x2B},
    [SDL_SCANCODE_NONUSHASH] = {0xDC, 0x2B}, [SDL_SCANCODE_SEMICOLON] = {0xBA, 0x27},
    [SDL_SCANCODE_APOSTROPHE] = {0xDE, 0x28}, [SDL_SCANCODE_GRAVE] = {0xC0, 0x29},
    [SDL_SCANCODE_COMMA] = {0xBC, 0x33}, [SDL_SCANCODE_PERIOD] = {0xBE, 0x34},
    [SDL_SCANCODE_SLASH] = {0xBF, 0x35}, [SDL_SCANCODE_CAPSLOCK] = {0x14, 0x3A},
    [SDL_SCANCODE_F1] = {0x70, 0x3B}, [SDL_SCANCODE_F2] = {0x71, 0x3C}, [SDL_SCANCODE_F3] = {0x72, 0x3D},
    [SDL_SCANCODE_F4] = {0x73, 0x3E}, [SDL_SCANCODE_F5] = {0x74, 0x3F}, [SDL_SCANCODE_F6] = {0x75, 0x40},
    [SDL_SCANCODE_F7] = {0x76, 0x41}, [SDL_SCANCODE_F8] = {0x77, 0x42}, [SDL_SCANCODE_F9] = {0x78, 0x43},
    [SDL_SCANCODE_F10] = {0x79, 0x44}, [SDL_SCANCODE_F11] = {0x7A, 0x57}, [SDL_SCANCODE_F12] = {0x7B, 0x58},
    [SDL_SCANCODE_F13] = {0x7C, 0x64}, [SDL_SCANCODE_F14] = {0x7D, 0x65}, [SDL_SCANCODE_F15] = {0x7E, 0x66},
    [SDL_SCANCODE_F16] = {0x7F, 0x67}, [SDL_SCANCODE_F17] = {0x80, 0x68}, [SDL_SCANCODE_F18] = {0x81, 0x69},
    [SDL_SCANCODE_F19] = {0x82, 0x6A}, [SDL_SCANCODE_F20] = {0x83, 0x6B}, [SDL_SCANCODE_F21] = {0x84, 0x6C},
    [SDL_SCANCODE_F22] = {0x85, 0x6D}, [SDL_SCANCODE_F23] = {0x86, 0x6E}, [SDL_SCANCODE_F24] = {0x87, 0x76},
    [SDL_SCANCODE_PRINTSCREEN] = {0x2C, 0x137}, [SDL_SCANCODE_SCROLLLOCK] = {0x91, 0x46},
    [SDL_SCANCODE_PAUSE] = {0x13, 0x45}, [SDL_SCANCODE_INSERT] = {0x2D, 0x152},
    [SDL_SCANCODE_HOME] = {0x24, 0x147}, [SDL_SCANCODE_PAGEUP] = {0x21, 0x149},
    [SDL_SCANCODE_DELETE] = {0x2E, 0x153}, [SDL_SCANCODE_END] = {0x23, 0x14F},
    [SDL_SCANCODE_PAGEDOWN] = {0x22, 0x151}, [SDL_SCANCODE_RIGHT] = {0x27, 0x14D},
    [SDL_SCANCODE_LEFT] = {0x25, 0x14B}, [SDL_SCANCODE_DOWN] = {0x28, 0x150}, [SDL_SCANCODE_UP] = {0x26, 0x148},
    [SDL_SCANCODE_NUMLOCKCLEAR] = {0x90, 0x145}, [SDL_SCANCODE_KP_DIVIDE] = {0x6F, 0x135},
    [SDL_SCANCODE_KP_MULTIPLY] = {0x6A, 0x37}, [SDL_SCANCODE_KP_MINUS] = {0x6D, 0x4A},
    [SDL_SCANCODE_KP_PLUS] = {0x6B, 0x4E}, [SDL_SCANCODE_KP_ENTER] = {0x0D, 0x11C},
    [SDL_SCANCODE_KP_1] = {0x61, 0x4F}, [SDL_SCANCODE_KP_2] = {0x62, 0x50}, [SDL_SCANCODE_KP_3] = {0x63, 0x51},
    [SDL_SCANCODE_KP_4] = {0x64, 0x4B}, [SDL_SCANCODE_KP_5] = {0x65, 0x4C}, [SDL_SCANCODE_KP_6] = {0x66, 0x4D},
    [SDL_SCANCODE_KP_7] = {0x67, 0x47}, [SDL_SCANCODE_KP_8] = {0x68, 0x48}, [SDL_SCANCODE_KP_9] = {0x69, 0x49},
    [SDL_SCANCODE_KP_0] = {0x60, 0x52}, [SDL_SCANCODE_KP_PERIOD] = {0x6E, 0x53},
    [SDL_SCANCODE_NONUSBACKSLASH] = {0xE2, 0x56}, [SDL_SCANCODE_APPLICATION] = {0x5D, 0x15D},
    [SDL_SCANCODE_LCTRL] = {0xA2, 0x1D}, [SDL_SCANCODE_LSHIFT] = {0xA0, 0x2A}, [SDL_SCANCODE_LALT] = {0xA4, 0x38},
    [SDL_SCANCODE_LGUI] = {0x5B, 0x15B}, [SDL_SCANCODE_RCTRL] = {0xA3, 0x11D}, [SDL_SCANCODE_RSHIFT] = {0xA1, 0x36},
    [SDL_SCANCODE_RALT] = {0xA5, 0x138}, [SDL_SCANCODE_RGUI] = {0x5C, 0x15C},
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

/* the character a key types with the given modifiers, 0 for none. */
static uint16_t layout_char(SDL_Scancode code, SDL_Keymod mod)
{
    SDL_Keycode key = SDL_GetKeyFromScancode(code, mod, false);
    if ((key & SDLK_SCANCODE_MASK) || key < 0x20 || key == 0x7F || key > 0xFFFF)
        return 0;
    return (uint16_t)key;
}

static bool lower_letter(uint16_t c)
{
    return c >= 'a' && c <= 'z';
}

/* reads the layout of the host from SDL's keymap, and tells the guest when it changed. */
static void update_layout(void)
{
    MimeKeyboardLayout l = {};
    l.language = 0x0409;
    SDL_Scancode by_scan[MIME_KEYBOARD_KEYS] = {};
    for (int code = 0; code < SDL_SCANCODE_COUNT; code++) {
        const KeyMap *k = &g_keymap[code];
        /* two SDL scancodes share a place, as backslash and the ISO key next to enter: the first is the one keyboards send. */
        if (!k->vk || k->scan >= MIME_KEYBOARD_KEYS || by_scan[k->scan])
            continue;
        by_scan[k->scan] = (SDL_Scancode)code;
        l.vk[k->scan] = k->vk;
        l.chars[k->scan][0] = layout_char((SDL_Scancode)code, SDL_KMOD_NONE);
        l.chars[k->scan][1] = layout_char((SDL_Scancode)code, SDL_KMOD_SHIFT);
        l.chars[k->scan][2] = layout_char((SDL_Scancode)code, SDL_KMOD_MODE);
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

static void emit_key(const SDL_KeyboardEvent *key)
{
    UiWindow *w = find_by_sdl(key->windowID);
    if (!w || key->scancode >= SDL_SCANCODE_COUNT || !g_keymap[key->scancode].vk)
        return;
    const KeyMap *k = &g_keymap[key->scancode];
    MimeInputEvent ev = {};
    ev.type = key->down ? MIME_INPUT_KEY_DOWN : MIME_INPUT_KEY_UP;
    ev.window_id = w->id;
    ev.vk = k->vk;
    if (g_layout_known && k->scan < MIME_KEYBOARD_KEYS)
        ev.vk = g_layout.vk[k->scan];
    /* letter keys take the virtual key of the letter they type in the host layout, like windows layouts do. */
    else if (k->vk >= 'A' && k->vk <= 'Z' && key->key >= 'a' && key->key <= 'z')
        ev.vk = (uint32_t)(key->key - 32);
    ev.scancode = k->scan;
    ev.modifiers = modifiers_of(key->mod);
    if (key->repeat)
        ev.flags = 2;
    emit(&ev);
    /*
     * SDL does not generate typed text while Alt is held. Windows uses WM_SYSCHAR for Alt+Space and
     * Alt+letter menus. AltGr also appears as Alt to SDL, so defer the character until the next event
     * confirms whether text follows.
     */
    if (key->down && (key->mod & SDL_KMOD_ALT) && !(key->mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
        SDL_Keycode code = SDL_GetKeyFromScancode(key->scancode, key->mod & SDL_KMOD_SHIFT, false);
        if (code >= 0x20 && code < 0x7F) {
            alt_char_flush();
            g_alt_char = (MimeInputEvent){.type = MIME_INPUT_CHAR, .window_id = w->id, .ch = (uint32_t)code,
                                          .modifiers = ev.modifiers, .scancode = ev.scancode};
            g_alt_char_pending = true;
        }
    }
}

static void emit_text(const SDL_TextInputEvent *text)
{
    UiWindow *w = find_by_sdl(text->windowID);
    if (!w || !text->text)
        return;
    uint16_t buf[64];
    size_t n = mime_utf8_to_utf16(buf, MIME_ARRAY_COUNT(buf), text->text);
    for (size_t i = 0; i < n; i++) {
        MimeInputEvent ev = {};
        ev.type = MIME_INPUT_CHAR;
        ev.window_id = w->id;
        ev.ch = buf[i];
        emit(&ev);
    }
}

/* ------------------------------------------------------------------ mouse. */

static void emit_mouse(SDL_WindowID id, float x, float y, uint32_t type, uint32_t button)
{
    UiWindow *w = find_by_sdl(id);
    if (!w)
        return;
    MimeInputEvent ev = {};
    ev.type = type;
    ev.window_id = w->id;
    window_point_to_guest(w, x, y, &ev.x, &ev.y);
    ev.button = button;
    ev.modifiers = modifiers_of(SDL_GetModState());
    if (w->window && SDL_GetWindowRelativeMouseMode(w->window))
        ev.flags |= 4;
    if (point_outside(w, x, y))
        ev.flags |= 8;
    emit(&ev);
}

static bool g_pointer_known;
static int32_t g_pointer_x, g_pointer_y;

/* motion in guest pixels; locked, the device's own counts, as raw input reports them. */
static void emit_motion(const SDL_MouseMotionEvent *m)
{
    UiWindow *w = find_by_sdl(m->windowID);
    if (!w)
        return;
    MimeInputEvent ev = {};
    ev.type = MIME_INPUT_MOUSE_MOVE;
    ev.window_id = w->id;
    ev.modifiers = modifiers_of(SDL_GetModState());
    double scale = 1.0;
    if (w->window && SDL_GetWindowRelativeMouseMode(w->window))
        ev.flags |= 4;
    else {
        window_point_to_guest(w, m->x, m->y, &ev.x, &ev.y);
        scale = window_fit(w).scale;
        if (point_outside(w, m->x, m->y))
            ev.flags |= 8;
    }
    w->motion_x += m->xrel / scale;
    w->motion_y += m->yrel / scale;
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

static uint32_t mouse_button(Uint8 button)
{
    switch (button) {
    case SDL_BUTTON_LEFT: return MIME_MOUSE_LEFT;
    case SDL_BUTTON_RIGHT: return MIME_MOUSE_RIGHT;
    case SDL_BUTTON_MIDDLE: return MIME_MOUSE_MIDDLE;
    case SDL_BUTTON_X1: return MIME_MOUSE_X1;
    case SDL_BUTTON_X2: return MIME_MOUSE_X2;
    default: return 0;
    }
}

static void emit_wheel(const SDL_MouseWheelEvent *wheel)
{
    UiWindow *w = find_by_sdl(wheel->windowID);
    if (!w)
        return;
    float sign = wheel->direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
    MimeInputEvent ev = {};
    ev.type = MIME_INPUT_MOUSE_WHEEL;
    ev.window_id = w->id;
    window_point_to_guest(w, wheel->mouse_x, wheel->mouse_y, &ev.x, &ev.y);
    ev.modifiers = modifiers_of(SDL_GetModState());
    /* fractions of a notch from smooth scrolling add up to whole wheel deltas of 120 per notch. */
    w->wheel_y += wheel->y * sign * 120.0;
    w->wheel_x += wheel->x * sign * 120.0;
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
 * caption drag rectangles let the compositor move the window. other input goes to the guest, including resize
 * border handling.
 */
static SDL_HitTestResult hit_test(SDL_Window *window, const SDL_Point *point, void *data)
{
    uint32_t id = (uint32_t)(uintptr_t)data;
    UiWindow *w = find_window(id);
    if (!w || (w->config.flags & (MIME_WIN_MAXIMIZED | MIME_WIN_FULLSCREEN)))
        return SDL_HITTEST_NORMAL;
    /* on Wayland the press reaches the guest, which asks the compositor to move or size the window once the
       pointer drags, and a double click on a caption is its own. */
    if (g_wayland)
        return SDL_HITTEST_NORMAL;
    MimeRect host = host_part(&w->config);
    double scale = config_scale(&w->config);
    int32_t x = (int32_t)lround(point->x * scale) + host.left;
    int32_t y = (int32_t)lround(point->y * scale) + host.top;
    for (uint32_t i = 0; i < w->config.drag_count && i < MIME_WIN_MAX_DRAG_RECTS; i++) {
        MimeRect r = w->config.drag[i];
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
            return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

/*
 * a size past twice the largest display, or past 16384, is none a window should have. kwin gave such sizes, 100000
 * pixels more than the window, while the window geometry was left to the extent of the surfaces, see set_geometry.
 * a buffer of that size is tens of gigabytes, which the nvidia driver takes from system memory when video memory
 * cannot hold it, and the host runs out within a second.
 */
static int sane_limit(void)
{
    int largest = 0, n = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&n);
    for (int i = 0; ids && i < n; i++) {
        SDL_Rect r;
        float scale = SDL_GetDisplayContentScale(ids[i]);
        if (SDL_GetDisplayBounds(ids[i], &r))
            largest = MIME_MAX(largest, (int)lround(MIME_MAX(r.w, r.h) * (scale > 1.0f ? scale : 1.0f)));
    }
    SDL_free(ids);
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
enum { XDG_SURFACE_SET_WINDOW_GEOMETRY = 3 };

static void set_geometry(UiWindow *w)
{
    if (!g_wayland || !w->window)
        return;
    struct wl_proxy *xdg = SDL_GetPointerProperty(SDL_GetWindowProperties(w->window),
                                                  SDL_PROP_WINDOW_WAYLAND_XDG_SURFACE_POINTER, nullptr);
    int width = 0, height = 0;
    SDL_GetWindowSize(w->window, &width, &height);
    if (!xdg || width <= 0 || height <= 0 || !sane_size(width, height) ||
        (width == w->geometry_w && height == w->geometry_h))
        return;
    w->geometry_w = width;
    w->geometry_h = height;
    wl_proxy_marshal_flags(xdg, XDG_SURFACE_SET_WINDOW_GEOMETRY, nullptr, wl_proxy_get_version(xdg), 0, 0, 0, width, height);
}

static void update_pixel_size(UiWindow *w)
{
    int width = 0, height = 0, logical_width = 0, logical_height = 0;
    if (w->window) {
        SDL_GetWindowSizeInPixels(w->window, &width, &height);
        SDL_GetWindowSize(w->window, &logical_width, &logical_height);
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

static void destroy_layers(UiWindow *w);

static bool has_open_popup(const UiWindow *w)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].popup && g_windows[i].window && g_windows[i].shown &&
            g_windows[i].owner == w->id)
            return true;
    return false;
}

/* locked: the caller holds g_windows_lock, which is let go while the host window goes. */
static void destroy_host_window(UiWindow *w, bool locked)
{
    if (w->vk) {
        mime_vk_window_lost(w->id);
        destroy_layers(w);
        w->vk = false;
        w->vk_whole = false;
    }
    mime_gpu_teardown_enter();
    if (w->texture)
        SDL_DestroyTexture(w->texture);
    if (w->renderer)
        SDL_DestroyRenderer(w->renderer);
    SDL_Window *window = w->window;
    w->texture = nullptr;
    w->renderer = nullptr;
    w->window = nullptr;
    if (window) {
        /*
         * the destruction roundtrips to the compositor and serves the clipboard requests that are waiting, which wait
         * for the guest. a processor of the guest that waits for the lock here would hold the guest still.
         */
        if (locked)
            pthread_mutex_unlock(&g_windows_lock);
        SDL_DestroyWindow(window);
        if (locked)
            pthread_mutex_lock(&g_windows_lock);
    }
    mime_gpu_teardown_leave();
    w->display = 0;
    w->texture_width = w->texture_height = 0;
}

/* where a popup goes in its owner's host window, which shows the owner through its fit. */
static void popup_place(const UiWindow *owner, const MimeWinConfig *cfg, int *x, int *y)
{
    MimeRect host = host_part(cfg), oh = host_part(&owner->config);
    Fit fit = window_fit(owner);
    *x = (int)lround(fit.x + (cfg->rect.left + host.left - owner->config.rect.left - oh.left) * fit.scale);
    *y = (int)lround(fit.y + (cfg->rect.top + host.top - owner->config.rect.top - oh.top) * fit.scale);
}

static UiWindow *popup_anchor(SDL_Window *focus, const UiWindow *w, const MimeRect *rect, bool inside)
{
    UiWindow *a = focus ? find_by_sdl(SDL_GetWindowID(focus)) : nullptr;
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
    if (!(cfg->flags & MIME_WIN_TOOL))
        return nullptr;
    UiWindow *owner = cfg->owner_id ? find_window(cfg->owner_id) : nullptr;
    if (owner && owner->window && (owner->shown || !g_wayland))
        return owner;
    if (!g_wayland)
        return nullptr;
    /* the one found first is kept. */
    UiWindow *kept = w->popup && w->window && w->owner ? find_window(w->owner) : nullptr;
    if (kept && kept->window && kept->shown)
        return kept;
    SDL_Window *mouse = SDL_GetMouseFocus(), *keys = SDL_GetKeyboardFocus();
    UiWindow *a = popup_anchor(mouse, w, &cfg->rect, true);
    if (!a)
        a = popup_anchor(keys, w, &cfg->rect, true);
    if (!a)
        a = popup_anchor(mouse, w, &cfg->rect, false);
    if (!a)
        a = popup_anchor(keys, w, &cfg->rect, false);
    return a;
}

/* menus and tooltips become owner-relative popups on Wayland; other windows become toplevels. */
static bool create_host_window(UiWindow *w, const MimeWinConfig *cfg, UiWindow *owner)
{
    MimeRect host = host_part(cfg);
    double scale = config_scale(cfg);
    bool popup = owner != nullptr;
    w->popup_scale = popup ? window_fit(owner).scale : 0;
    double shown = popup ? w->popup_scale : 1.0 / scale;
    int width = MIME_MAX(1, (int)lround((host.right - host.left) * shown));
    int height = MIME_MAX(1, (int)lround((host.bottom - host.top) * shown));
    SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_TRANSPARENT;
    if (g_gl)
        flags |= SDL_WINDOW_OPENGL;
    if (popup) {
        int x, y;
        popup_place(owner, cfg, &x, &y);
        SDL_WindowFlags kind = (cfg->flags & MIME_WIN_NOINPUT) ? SDL_WINDOW_TOOLTIP : SDL_WINDOW_POPUP_MENU;
        /*
         * SDL gives a popup menu keyboard focus despite requesting no compositor grab. this would disable the
         * owner's text input and tag keys with the popup window. keep focus on the compositor's focused
         * window.
         */
        if (g_wayland)
            kind |= SDL_WINDOW_NOT_FOCUSABLE;
        w->window = SDL_CreatePopupWindow(owner->window, x, y, width, height, flags | kind);
    } else {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, (int)lround((cfg->rect.left + host.left) / scale));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, (int)lround((cfg->rect.top + host.top) / scale));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, flags | SDL_WINDOW_BORDERLESS);
        if (cfg->flags & MIME_WIN_TOOL)
            SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_UTILITY_BOOLEAN, true);
        w->window = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);
    }
    if (!w->window) {
        mime_warn("cannot create a host window: %s", SDL_GetError());
        return false;
    }
    w->popup = popup;
    w->says_maximized = false;
    w->owner = popup ? owner->id : cfg->owner_id;
    w->renderer = SDL_CreateRenderer(w->window, nullptr);
    if (!w->renderer) {
        mime_warn("cannot create a renderer: %s", SDL_GetError());
        destroy_host_window(w, false);
        return false;
    }
    SDL_SetRenderVSync(w->renderer, 0);
    update_pixel_size(w);
    SDL_SetWindowHitTest(w->window, hit_test, (void *)(uintptr_t)w->id);
    /* the popup owner retains keyboard focus and text input. */
    if (!(popup && g_wayland))
        SDL_StartTextInput(w->window);
    UiWindow *parent = !popup && cfg->owner_id ? find_window(cfg->owner_id) : nullptr;
    if (parent && parent->window)
        SDL_SetWindowParent(w->window, parent->window);
    return true;
}

/* a hidden window keeps nothing of its size: its renderer goes, and comes back when it shows. */
static void drop_renderer(UiWindow *w)
{
    pthread_mutex_lock(&g_windows_lock);
    SDL_Texture *texture = w->texture;
    SDL_Renderer *renderer = w->renderer;
    w->texture = nullptr;
    w->renderer = nullptr;
    w->texture_width = w->texture_height = 0;
    pthread_mutex_unlock(&g_windows_lock);
    if (texture)
        SDL_DestroyTexture(texture);
    if (renderer)
        SDL_DestroyRenderer(renderer);
}

static void restore_renderer(UiWindow *w)
{
    if (w->renderer || w->vk_whole || !w->window)
        return;
    SDL_Renderer *renderer = SDL_CreateRenderer(w->window, nullptr);
    if (!renderer) {
        mime_warn("cannot create a renderer: %s", SDL_GetError());
        return;
    }
    SDL_SetRenderVSync(renderer, 0);
    pthread_mutex_lock(&g_windows_lock);
    w->renderer = renderer;
    pthread_mutex_unlock(&g_windows_lock);
    pthread_mutex_lock(&w->lock);
    w->dirty = (MimeRect){0, 0, (int32_t)w->width, (int32_t)w->height};
    w->has_dirty = w->pixels != nullptr;
    pthread_mutex_unlock(&w->lock);
}

/* draws the visible surface inside the host window, leaving the resize border clear. */
static void draw(UiWindow *w)
{
    /* presenting would make the renderer's buffers that size. */
    if ((!w->renderer && !w->vk_whole) || w->bogus_size)
        return;
    pthread_mutex_lock(&w->lock);
    w->present_queued = false;
    if (!w->pixels || w->vk_whole) {
        pthread_mutex_unlock(&w->lock);
        /* a window the guest draws nothing in, such as a popup its GPU process fills, is clear, not black. */
        if (w->renderer && !w->pixels) {
            SDL_SetRenderDrawColor(w->renderer, 0, 0, 0, 0);
            SDL_RenderClear(w->renderer);
            SDL_RenderPresent(w->renderer);
        }
        return;
    }
    if (!w->texture || w->texture_width != w->width || w->texture_height != w->height) {
        if (w->texture)
            SDL_DestroyTexture(w->texture);
        w->texture = SDL_CreateTexture(w->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                       (int)w->width, (int)w->height);
        w->texture_width = w->width;
        w->texture_height = w->height;
        w->dirty = (MimeRect){0, 0, (int32_t)w->width, (int32_t)w->height};
        w->has_dirty = true;
        if (w->texture) {
            SDL_SetTextureBlendMode(w->texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            SDL_SetTextureScaleMode(w->texture, SDL_SCALEMODE_NEAREST);
        }
    }
    if (w->texture && w->has_dirty) {
        MimeRect d = w->dirty;
        SDL_Rect r = {d.left, d.top, d.right - d.left, d.bottom - d.top};
        uint8_t table[3][256];
        void *locked;
        int pitch;
        if (r.w > 0 && r.h > 0 && mime_gamma_table(table) && SDL_LockTexture(w->texture, &r, &locked, &pitch)) {
            /* through the display's ramp, blue, green, red then alpha in memory. */
            for (int y = 0; y < r.h; y++) {
                const uint8_t *in = w->pixels + ((size_t)(d.top + y) * w->width + (size_t)d.left) * 4;
                uint8_t *out = (uint8_t *)locked + (size_t)y * (size_t)pitch;
                for (int x = 0; x < r.w; x++, in += 4, out += 4) {
                    out[0] = table[2][in[0]];
                    out[1] = table[1][in[1]];
                    out[2] = table[0][in[2]];
                    out[3] = in[3];
                }
            }
            SDL_UnlockTexture(w->texture);
        } else if (r.w > 0 && r.h > 0) {
            SDL_UpdateTexture(w->texture, &r, w->pixels + ((size_t)d.top * w->width + (size_t)d.left) * 4,
                              (int)w->width * 4);
        }
        w->has_dirty = false;
    }
    double density = w->logical_width > 0 ? (double)w->pixel_width / w->logical_width : 1.0;
    pthread_mutex_unlock(&w->lock);
    if (!w->texture)
        return;
    MimeRect vis = visible_part(&w->config), host = host_part(&w->config);
    /* around a window the fit leaves smaller than the screen, black as a monitor's border. */
    SDL_SetRenderDrawColor(w->renderer, 0, 0, 0, w->fullscreen ? 255 : 0);
    SDL_RenderClear(w->renderer);
    double stretch = surface_stretch(&w->config);
    SDL_FRect src = {(float)(vis.left / stretch), (float)(vis.top / stretch), (float)((vis.right - vis.left) / stretch),
                     (float)((vis.bottom - vis.top) / stretch)};
    /* whole multiples repeat pixels, any other stretch blends them, like the compositor does. */
    SDL_SetTextureScaleMode(w->texture, stretch == floor(stretch) ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    /* the renderer draws in pixels, the fit is in window coordinates. */
    Fit fit = window_fit(w);
    fit.scale *= density;
    fit.x *= density;
    fit.y *= density;
    SDL_FRect dst = {(float)(fit.x + (vis.left - host.left) * fit.scale), (float)(fit.y + (vis.top - host.top) * fit.scale),
                     (float)((vis.right - vis.left) * fit.scale), (float)((vis.bottom - vis.top) * fit.scale)};
    SDL_RenderTexture(w->renderer, w->texture, &src, &dst);
    if (w->config.border_color) {
        uint32_t c = w->config.border_color;
        SDL_SetRenderDrawColor(w->renderer, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF);
        SDL_RenderRect(w->renderer, &dst);
    }
    SDL_RenderPresent(w->renderer);
}

static void present_main(void *arg)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window((uint32_t)(uintptr_t)arg);
    pthread_mutex_unlock(&g_windows_lock);
    if (w && w->shown && !w->renderer && !w->vk_whole && w->window && now_ms() >= w->renderer_retry_ms) {
        restore_renderer(w);
        if (!w->renderer)
            w->renderer_retry_ms = now_ms() + 1000;
    }
    if (w && w->shown)
        draw(w);
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

/* converts a guest size limit to SDL window coordinates; 0 means no limit. */
static int limit_of(int32_t limit, int outside, double scale)
{
    return limit > 0 ? MIME_MAX(1, (int)lround((limit - outside) / scale)) : 0;
}

static void unminimize(UiWindow *w, bool activate);
static Uint32 size_timer(void *data, SDL_TimerID timer, Uint32 interval);

/*
 * hyprland tells every window it is maximized, tiled or floating, so that none draws shadows. the state says nothing
 * there, and sdl sends no request to maximize a window it takes for maximized. hyprland also takes either request as
 * a toggle: unset_maximized on a window that is not maximized maximizes it. such a window is asked directly, and only
 * for a change of the state asked before.
 */
enum { XDG_TOPLEVEL_SET_MAXIMIZED = 9, XDG_TOPLEVEL_UNSET_MAXIMIZED = 10 };

static void set_maximized(UiWindow *w, bool maximized)
{
    if (!w->says_maximized) {
        if (maximized)
            SDL_MaximizeWindow(w->window);
        else
            SDL_RestoreWindow(w->window);
        return;
    }
    struct wl_proxy *toplevel = SDL_GetPointerProperty(SDL_GetWindowProperties(w->window),
                                                       SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_POINTER, nullptr);
    if (toplevel)
        wl_proxy_marshal_flags(toplevel, maximized ? XDG_TOPLEVEL_SET_MAXIMIZED : XDG_TOPLEVEL_UNSET_MAXIMIZED, nullptr,
                               wl_proxy_get_version(toplevel), 0);
    w->maximized = maximized;
}

static void apply_config(UiWindow *w, const MimeWinConfig *cfg)
{
    if (w->state_pending) {
        if (cfg->state_request != w->state_request)
            return;
        w->state_pending = false;
    }
    /* a popup sits on its owner from its creation: another owner or kind makes a new window. */
    UiWindow *owner = popup_owner(w, cfg);
    bool want_popup = owner != nullptr;
    if (w->window && (want_popup != w->popup || (w->popup && w->owner != owner->id))) {
        destroy_host_window(w, false);
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
                          ((w->config.flags ^ cfg->flags) & MIME_WIN_RESIZABLE) ||
                          host_part(&w->config).right - host_part(&w->config).left != host_part(cfg).right - host_part(cfg).left ||
                          host_part(&w->config).bottom - host_part(&w->config).top != host_part(cfg).bottom - host_part(cfg).top;
    pthread_mutex_lock(&w->lock);
    w->config = *cfg;
    pthread_mutex_unlock(&w->lock);
    w->configured = true;
    if (!w->window && !(cfg->flags & MIME_WIN_VISIBLE))
        return;
    bool fresh = !w->window;
    if (fresh && !create_host_window(w, cfg, owner))
        return;
    if (fresh)
        set_geometry(w);

    char title[1024];
    mime_utf16_to_utf8(title, sizeof(title), cfg->title, MIME_MIN(cfg->title_chars, 256u));
    SDL_SetWindowTitle(w->window, title);
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
        SDL_SetWindowSize(w->window, w->want_w, w->want_h);
    }
    /*
     * a compositor that tiles the window keeps its size and sends no event: the guest is told of the size kept, which
     * new limits may let it take.
     */
    if ((size_changed || limits_changed) && !fresh && !w->popup && !minimized && !w->size_recheck) {
        w->size_recheck = true;
        SDL_AddTimer(260, size_timer, (void *)(uintptr_t)w->id);
    }
    if (!w->popup && (limits_changed || fresh) && !minimized) {
        /* the compositor sizes a window the guest lets be sized, within the limits of its program. */
        SDL_SetWindowResizable(w->window, (cfg->flags & MIME_WIN_RESIZABLE) != 0);
        int dw = (cfg->rect.right - cfg->rect.left) - (host.right - host.left);
        int dh = (cfg->rect.bottom - cfg->rect.top) - (host.bottom - host.top);
        int min_w = limit_of(cfg->min_width, dw, scale), min_h = limit_of(cfg->min_height, dh, scale);
        int max_w = limit_of(cfg->max_width, dw, scale), max_h = limit_of(cfg->max_height, dh, scale);
        /* no window is let grow past what a display could show, whatever the compositor proposes. */
        float density = SDL_GetWindowPixelDensity(w->window);
        int cap = (int)(sane_limit() / (density > 1.0f ? density : 1.0f));
        max_w = max_w > 0 ? MIME_MIN(max_w, cap) : cap;
        max_h = max_h > 0 ? MIME_MIN(max_h, cap) : cap;
        min_w = MIME_MIN(min_w, max_w);
        min_h = MIME_MIN(min_h, max_h);
        SDL_SetWindowMinimumSize(w->window, min_w, min_h);
        SDL_SetWindowMaximumSize(w->window, max_w, max_h);
    }
    if (w->popup) {
        int x, y;
        popup_place(owner, cfg, &x, &y);
        SDL_SetWindowPosition(w->window, x, y);
    } else if (!w->fullscreen) {
        /* Wayland leaves top level placement to the compositor; X11 honors it. */
        SDL_SetWindowPosition(w->window, (int)lround((cfg->rect.left + host.left) / scale),
                              (int)lround((cfg->rect.top + host.top) / scale));
    }
    SDL_SetWindowAlwaysOnTop(w->window, (cfg->flags & MIME_WIN_TOPMOST) != 0);

    if (cfg->flags & MIME_WIN_MINIMIZED) {
        if (cfg->flags & MIME_WIN_VISIBLE)
            SDL_MinimizeWindow(w->window);
        else
            SDL_HideWindow(w->window);
        w->shown = (cfg->flags & MIME_WIN_VISIBLE) != 0;
        mime_vk_window_hidden(w->id);
        if (w->shown)
            restore_renderer(w);
        else
            drop_renderer(w);
        return;
    }
    if (cfg->flags & MIME_WIN_VISIBLE) {
        bool opening = !w->shown;
        restore_renderer(w);
        if (opening) {
            SDL_ShowWindow(w->window);
            w->shown_ms = now_ms();
        }
        if (SDL_GetWindowFlags(w->window) & SDL_WINDOW_MINIMIZED)
            unminimize(w, (cfg->flags & MIME_WIN_ACTIVATE) != 0);
        else if (cfg->flags & MIME_WIN_ACTIVATE)
            raise_window(w);
        w->shown = true;
    } else if (w->shown) {
        SDL_HideWindow(w->window);
        w->shown = false;
        /* the window that shows again is a new one to the compositor. */
        if (w->says_maximized)
            w->maximized = false;
        mime_vk_window_hidden(w->id);
        drop_renderer(w);
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
            SDL_DisplayID display = display_of_rect(&cfg->rect);
            const SDL_DisplayMode *mode =
                display && display != SDL_GetDisplayForWindow(w->window) ? SDL_GetDesktopDisplayMode(display) : nullptr;
            SDL_SetWindowFullscreenMode(w->window, mode);
        }
        SDL_SetWindowFullscreen(w->window, fullscreen);
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
    if (token[0] && g_wayland) {
        SDL_PropertiesID props = SDL_GetWindowProperties(w->window);
        struct wl_display *display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        struct wl_surface *surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        if (mime_activation_activate(display, surface, token))
            return;
    }
    SDL_RaiseWindow(w->window);
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
    if (!g_wayland) {
        SDL_RestoreWindow(w->window);
        if (activate)
            raise_window(w);
        return;
    }
    if (activate && (fresh_token() || SDL_GetKeyboardFocus())) {
        raise_window(w);
        return;
    }
    bool maximized = w->maximized;
    SDL_HideWindow(w->window);
    /* a hidden window keeps its minimized state for the next show, this drops it. */
    SDL_RestoreWindow(w->window);
    if (maximized && !w->says_maximized)
        SDL_MaximizeWindow(w->window);
    SDL_ShowWindow(w->window);
    if (maximized && w->says_maximized)
        set_maximized(w, true);
    w->geometry_w = w->geometry_h = 0;
    w->set_app_id[0] = 0;
    apply_app_id(w);
    set_geometry(w);
    commit_parent(w);
}

/* sets xdg_toplevel's application id by protocol opcode. */
enum { XDG_TOPLEVEL_SET_APP_ID = 3 };

static void apply_app_id(UiWindow *w)
{
    if (!g_wayland || !w->window || !w->want_app_id[0] || !strcmp(w->want_app_id, w->set_app_id))
        return;
    SDL_PropertiesID props = SDL_GetWindowProperties(w->window);
    struct wl_proxy *toplevel = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_POINTER, nullptr);
    struct wl_display *display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    if (!toplevel || !display)
        return;
    wl_proxy_marshal_flags(toplevel, XDG_TOPLEVEL_SET_APP_ID, nullptr, wl_proxy_get_version(toplevel), 0, w->want_app_id);
    wl_display_flush(display);
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
    if (w && w->window) {
        SDL_PropertiesID props = SDL_GetWindowProperties(w->window);
        struct wl_display *display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        struct wl_surface *surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        if (call->mode == 0) {
            SDL_FlashWindow(w->window, SDL_FLASH_CANCEL);
        } else if (!g_wayland || !mime_activation_attention(display, surface, w->want_app_id)) {
            /*
             * SDL flashes X11 windows but raises Wayland windows; use the protocol above for Wayland
             * attention requests.
             */
            if (!g_wayland)
                SDL_FlashWindow(w->window, call->mode == 2 ? SDL_FLASH_UNTIL_FOCUSED : SDL_FLASH_BRIEFLY);
        }
    }
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
            destroy_host_window(&g_windows[i], true);
            g_windows[i].shown = false;
        }
    destroy_host_window(w, true);
    pthread_mutex_lock(&w->lock);
    free(w->pixels);
    w->pixels = nullptr;
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
    if (w && w->window && icon->width && icon->height) {
        SDL_Surface *s = SDL_CreateSurfaceFrom((int)icon->width, (int)icon->height, SDL_PIXELFORMAT_ARGB8888,
                                               icon->pixels, (int)icon->width * 4);
        if (s) {
            SDL_SetWindowIcon(w->window, s);
            SDL_DestroySurface(s);
        }
    }
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
        free(w->pixels);
        w->pixels = malloc((size_t)width * height * 4);
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
    bool queue = !w->present_queued;
    w->present_queued = true;
    pthread_mutex_unlock(&w->lock);
    if (queue)
        on_main(present_main, (void *)(uintptr_t)id);
}

/* ------------------------------------------------------------------ Vulkan presentation. */

/*
 * wp_viewporter scales presenter buffers to the client area. its two requests are described here from the
 * protocol.
 */
static const struct wl_interface viewport_interface;
static const struct wl_interface *viewporter_types[] = {&viewport_interface, &wl_surface_interface};
static const struct wl_interface *no_types[] = {nullptr, nullptr, nullptr, nullptr};
static const struct wl_message viewporter_requests[] = {
    {"destroy", "", no_types},
    {"get_viewport", "no", viewporter_types},
};
static const struct wl_interface viewporter_interface = {"wp_viewporter", 1, 2, viewporter_requests, 0, nullptr};
static const struct wl_message viewport_requests[] = {
    {"destroy", "", no_types},
    {"set_source", "ffff", no_types},
    {"set_destination", "ii", no_types},
};
static const struct wl_interface viewport_interface = {"wp_viewport", 1, 3, viewport_requests, 0, nullptr};

/* subsurface globals use their own event queue; SDL's dispatch never receives them. */
static struct wl_display *g_wl;
static struct wl_event_queue *g_wl_queue;
static struct wl_compositor *g_compositor;
static struct wl_subcompositor *g_subcompositor;
static struct wl_proxy *g_viewporter;
static bool g_wl_tried;

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                            uint32_t version)
{
    if (!strcmp(interface, wl_compositor_interface.name))
        g_compositor = wl_registry_bind(registry, name, &wl_compositor_interface, MIME_MIN(version, 4u));
    else if (!strcmp(interface, wl_subcompositor_interface.name))
        g_subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    else if (!strcmp(interface, viewporter_interface.name))
        g_viewporter = wl_registry_bind(registry, name, &viewporter_interface, 1);
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener g_registry_listener = {registry_global, registry_remove};

static bool wayland_globals(struct wl_display *display)
{
    if (g_wl_tried)
        return g_wl == display && g_compositor && g_subcompositor;
    g_wl_tried = true;
    g_wl = display;
    g_wl_queue = wl_display_create_queue(display);
    struct wl_display *wrapper = wl_proxy_create_wrapper(display);
    wl_proxy_set_queue((struct wl_proxy *)wrapper, g_wl_queue);
    struct wl_registry *registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    wl_registry_add_listener(registry, &g_registry_listener, nullptr);
    wl_display_roundtrip_queue(display, g_wl_queue);
    if (!g_compositor || !g_subcompositor)
        mime_warn("the compositor has no subsurfaces: vulkan windows take the whole window");
    return g_compositor && g_subcompositor;
}

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
 * a rectangle of the window's own pixels in SDL window coordinates. the guest gives it in the pixels of the program,
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

static void destroy_layer(VkLayer *l)
{
    if (l->viewport)
        wl_proxy_marshal_flags(l->viewport, 0, nullptr, wl_proxy_get_version(l->viewport), WL_MARSHAL_FLAG_DESTROY);
    if (l->subsurface)
        wl_subsurface_destroy(l->subsurface);
    if (l->surface)
        wl_surface_destroy(l->surface);
    *l = (VkLayer){};
}

static void destroy_layers(UiWindow *w)
{
    for (int i = 0; i < MAX_VK_LAYERS; i++)
        if (w->vk_layers[i].used)
            destroy_layer(&w->vk_layers[i]);
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
    return w->window ? SDL_GetPointerProperty(SDL_GetWindowProperties(w->window), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,
                                              nullptr)
                     : nullptr;
}

/* a change of the parent's stack or of a position lands with the parent's next commit. */
static void commit_parent(UiWindow *w)
{
    if (w->renderer && w->pixels && w->shown) {
        pthread_mutex_lock(&w->lock);
        w->dirty = (MimeRect){0, 0, (int32_t)w->width, (int32_t)w->height};
        w->has_dirty = true;
        pthread_mutex_unlock(&w->lock);
        draw(w);
    } else {
        struct wl_surface *parent = parent_surface(w);
        if (parent)
            wl_surface_commit(parent);
    }
}

/* the layers over the window's own pixels from the back of the guest's paint order to the front. */
static void restack_layers(UiWindow *w)
{
    struct wl_surface *below = parent_surface(w);
    bool done[MAX_VK_LAYERS] = {};
    for (int n = 0; below && n < MAX_VK_LAYERS; n++) {
        int best = -1;
        for (int k = 0; k < MAX_VK_LAYERS; k++)
            if (w->vk_layers[k].used && w->vk_layers[k].subsurface && !done[k] &&
            (best < 0 || w->vk_layers[k].order < w->vk_layers[best].order))
                best = k;
        if (best < 0)
            break;
        done[best] = true;
        wl_subsurface_place_above(w->vk_layers[best].subsurface, below);
        below = w->vk_layers[best].surface;
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
            wl_subsurface_destroy(l->subsurface);
            l->subsurface = nullptr;
            commit_parent(w);
        }
        return;
    }
    bool fresh = !l->subsurface;
    if (fresh) {
        struct wl_surface *parent = parent_surface(w);
        l->subsurface = parent ? wl_subcompositor_get_subsurface(g_subcompositor, l->surface, parent) : nullptr;
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
        if (l->viewport)
            wl_proxy_marshal_flags(l->viewport, 2, nullptr, wl_proxy_get_version(l->viewport), 0, r.right - r.left,
                                   r.bottom - r.top);
    }
    if (restack)
        restack_layers(w);
    commit_parent(w);
}

static VkLayer *make_layer(UiWindow *w, struct wl_surface *parent, uint32_t source, MimeRect area, uint32_t order)
{
    VkLayer *l = nullptr;
    for (int i = 0; i < MAX_VK_LAYERS && !l; i++)
        if (!w->vk_layers[i].used)
            l = &w->vk_layers[i];
    if (!l)
        return nullptr;
    l->surface = wl_compositor_create_surface(g_compositor);
    l->subsurface = l->surface ? wl_subcompositor_get_subsurface(g_subcompositor, l->surface, parent) : nullptr;
    if (!l->subsurface) {
        destroy_layer(l);
        return nullptr;
    }
    l->used = true;
    l->source = source;
    wl_subsurface_set_desync(l->subsurface);
    /* the mouse goes through, to the window SDL knows. */
    struct wl_region *empty = wl_compositor_create_region(g_compositor);
    wl_surface_set_input_region(l->surface, empty);
    wl_region_destroy(empty);
    if (g_viewporter)
        l->viewport = wl_proxy_marshal_flags(g_viewporter, 1, &viewport_interface, wl_proxy_get_version(g_viewporter), 0,
                                             nullptr, l->surface);
    wl_surface_commit(l->surface);
    l->place = (MimeRect){0};
    l->order = ~order;
    place_layer(w, l, area, order);
    return l;
}

typedef struct MoveSize {
    uint32_t window, edge;
    uint64_t status;
    bool done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} MoveSize;

/* xdg_toplevel move and resize requests, by protocol opcode. */
enum { XDG_TOPLEVEL_MOVE = 5, XDG_TOPLEVEL_RESIZE = 6 };

/* xdg_toplevel's resize edges for the guest's WMSZ_* ones. */
static uint32_t xdg_edge(uint32_t edge)
{
    static const uint32_t edges[] = {0, 4, 8, 1, 5, 9, 2, 6, 10};
    return edge < sizeof(edges) / sizeof(edges[0]) ? edges[edge] : 0;
}

static void movesize_main(void *arg)
{
    MoveSize *m = arg;
    UiWindow *w = find_window(m->window);
    void *seat;
    uint32_t serial;
    m->status = MIME_HC_STATUS_UNSUPPORTED;
    if (g_wayland && w && w->window && w->shown && !w->popup && !w->fullscreen && m->edge <= 8 &&
        mime_ui_pointer_press(&seat, &serial)) {
        SDL_PropertiesID props = SDL_GetWindowProperties(w->window);
        struct wl_proxy *toplevel = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_POINTER, nullptr);
        struct wl_display *display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        if (toplevel && display) {
            uint32_t version = wl_proxy_get_version(toplevel);
            if (!m->edge) {
                wl_proxy_marshal_flags(toplevel, XDG_TOPLEVEL_MOVE, nullptr, version, 0, seat, serial);
            } else {
                wl_proxy_marshal_flags(toplevel, XDG_TOPLEVEL_RESIZE, nullptr, version, 0, seat, serial,
                                       xdg_edge(m->edge));
                w->interactive_until = now_ms() + 3000;
            }
            wl_display_flush(display);
            m->status = MIME_HC_STATUS_OK;
        }
    }
    pthread_mutex_lock(&m->lock);
    m->done = true;
    pthread_cond_signal(&m->cond);
    pthread_mutex_unlock(&m->lock);
}

uint64_t mime_ui_window_movesize(uint32_t window, uint32_t edge)
{
    if (!g_video || !g_wayland)
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

/* Wayland gives every window of the guest that presents a subsurface of its own, X11 and the others one swapchain. */
bool mime_ui_vk_layers(void)
{
    return g_video && g_wayland;
}

static void attach_main(void *arg)
{
    VkAttach *a = arg;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(a->id);
    if (w && w->window) {
        SDL_PropertiesID props = SDL_GetWindowProperties(w->window);
        const char *driver = SDL_GetCurrentVideoDriver();
        *a->native = (MimeUiNative){0};
        if (driver && !strcmp(driver, "wayland")) {
            struct wl_display *display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
            struct wl_surface *parent = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
            VkLayer *layer = nullptr;
            if (display && parent && wayland_globals(display) &&
                ((layer = find_layer(w, a->source)) || (layer = make_layer(w, parent, a->source, a->area, a->order)))) {
                a->native->display = display;
                a->native->surface = layer->surface;
                a->native->subsurface = true;
                a->ok = true;
            }
        } else if (driver && !strcmp(driver, "x11")) {
            a->native->x11 = true;
            a->native->display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
            a->native->window = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
            a->ok = a->native->display && a->native->window;
            /* the window takes one swapchain: the renderer's goes, and the presenter draws the frame. */
            if (a->ok) {
                SDL_Texture *texture = w->texture;
                SDL_Renderer *renderer = w->renderer;
                w->texture = nullptr;
                w->renderer = nullptr;
                pthread_mutex_unlock(&g_windows_lock);
                if (texture)
                    SDL_DestroyTexture(texture);
                if (renderer)
                    SDL_DestroyRenderer(renderer);
                pthread_mutex_lock(&g_windows_lock);
                w->texture_width = w->texture_height = 0;
                pthread_mutex_lock(&w->lock);
                w->dirty = (MimeRect){0, 0, (int32_t)w->width, (int32_t)w->height};
                w->has_dirty = w->pixels != nullptr;
                pthread_mutex_unlock(&w->lock);
            }
        }
        if (a->ok) {
            w->vk = true;
            w->vk_whole = !a->native->subsurface;
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
        VkLayer *layer = w->vk_whole ? nullptr : find_layer(w, d->source);
        if (layer)
            destroy_layer(layer);
        /* the window stays under the presenters while a layer is left. */
        if (w->vk_whole || !has_layers(w)) {
            w->vk = false;
            w->vk_whole = false;
            destroy_layers(w);
            if (w->window && !w->renderer) {
                SDL_Renderer *renderer;
                pthread_mutex_unlock(&g_windows_lock);
                renderer = SDL_CreateRenderer(w->window, nullptr);
                pthread_mutex_lock(&g_windows_lock);
                w->renderer = renderer;
                if (w->renderer)
                    SDL_SetRenderVSync(w->renderer, 0);
                else
                    mime_warn("cannot create a renderer: %s", SDL_GetError());
            }
            pthread_mutex_lock(&w->lock);
            w->dirty = (MimeRect){0, 0, (int32_t)w->width, (int32_t)w->height};
            w->has_dirty = w->pixels != nullptr;
            pthread_mutex_unlock(&w->lock);
        } else {
            restack_layers(w);
        }
    }
    pthread_mutex_unlock(&g_windows_lock);
    if (w && w->shown)
        draw(w);
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
    VkLayer *layer = w && w->vk && !w->vk_whole ? find_layer(w, p->source) : nullptr;
    if (layer)
        place_layer(w, layer, p->area, p->order);
    pthread_mutex_unlock(&g_windows_lock);
    free(p);
}

bool mime_ui_vk_frame(uint32_t id, uint32_t source, MimeRect client, MimeRect area, uint32_t order, MimeUiFrame *frame)
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
    /* guest window bounds in host pixels. */
    Fit fit = window_fit(w);
    double density = w->logical_width > 0 ? (double)w->pixel_width / w->logical_width : 1.0;
    frame->scale = fit.scale * density;
    frame->origin_x = fit.x * density;
    frame->origin_y = fit.y * density;
    frame->surface_width = w->pixels ? w->width : 0;
    frame->surface_height = w->pixels ? w->height : 0;
    frame->shown = w->shown && !(w->config.flags & MIME_WIN_MINIMIZED);
    frame->subsurface = !w->vk_whole;
    frame->client = physical_rect(w, client);
    frame->area = physical_rect(w, area);
    bool moved = false;
    if (frame->subsurface) {
        VkLayer *layer = find_layer(w, source);
        if (!layer) {
            pthread_mutex_unlock(&w->lock);
            return false;
        }
        /* the layer's area at the window's density. */
        MimeRect r = client_in_window(w, area);
        frame->pixel_width = (uint32_t)MIME_MAX(lround((r.right - r.left) * density), 0);
        frame->pixel_height = (uint32_t)MIME_MAX(lround((r.bottom - r.top) * density), 0);
        moved = memcmp(&r, &layer->place, sizeof(r)) != 0 || layer->order != order;
    } else {
        frame->pixel_width = w->pixel_width;
        frame->pixel_height = w->pixel_height;
    }
    pthread_mutex_unlock(&w->lock);
    if (moved) {
        VkPlace *p = malloc(sizeof(*p));
        if (p) {
            *p = (VkPlace){id, source, order, area};
            on_main(place_main, p);
        }
    }
    return true;
}

bool mime_ui_vk_copy(uint32_t id, uint8_t *dst, uint32_t width, uint32_t height, MimeRect *dirty)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id);
    if (!w) {
        pthread_mutex_unlock(&g_windows_lock);
        return false;
    }
    pthread_mutex_lock(&w->lock);
    pthread_mutex_unlock(&g_windows_lock);
    bool copied = false;
    if (w->vk_whole && w->pixels && w->has_dirty && w->width == width && w->height == height) {
        MimeRect d = w->dirty;
        for (int32_t y = d.top; y < d.bottom; y++)
            memcpy(dst + ((size_t)y * width + (size_t)d.left) * 4, w->pixels + ((size_t)y * width + (size_t)d.left) * 4,
                   (size_t)(d.right - d.left) * 4);
        *dirty = d;
        w->has_dirty = false;
        copied = true;
    }
    pthread_mutex_unlock(&w->lock);
    return copied;
}

/* ------------------------------------------------------------------ cursor. */

static void cursor_main(void *arg)
{
    static const SDL_SystemCursor kinds[] = {
        SDL_SYSTEM_CURSOR_DEFAULT,     SDL_SYSTEM_CURSOR_TEXT,      SDL_SYSTEM_CURSOR_WAIT,
        SDL_SYSTEM_CURSOR_CROSSHAIR,   SDL_SYSTEM_CURSOR_POINTER,   SDL_SYSTEM_CURSOR_NS_RESIZE,
        SDL_SYSTEM_CURSOR_EW_RESIZE,   SDL_SYSTEM_CURSOR_NWSE_RESIZE, SDL_SYSTEM_CURSOR_NESW_RESIZE,
        SDL_SYSTEM_CURSOR_MOVE,        SDL_SYSTEM_CURSOR_NOT_ALLOWED,
    };
    uint32_t c = (uint32_t)(uintptr_t)arg;
    if (c == g_cursor)
        return;
    g_cursor = c;
    if (c >= MIME_CURSOR_HIDDEN) {
        SDL_HideCursor();
        return;
    }
    if (!g_cursors[c])
        g_cursors[c] = SDL_CreateSystemCursor(kinds[c]);
    if (g_cursors[c])
        SDL_SetCursor(g_cursors[c]);
    SDL_ShowCursor();
}

void mime_ui_set_cursor(uint32_t cursor)
{
    on_main(cursor_main, (void *)(uintptr_t)cursor);
}

typedef struct PointerCall {
    uint32_t mode, window_id;
    MimeRect rect;
} PointerCall;

static void pointer_main(void *arg)
{
    PointerCall *c = arg;
    static SDL_Window *held;
    UiWindow *w = find_window(c->window_id);
    SDL_Window *window = w && c->mode != MIME_POINTER_FREE ? w->window : nullptr;
    if (held && held != window) {
        SDL_SetWindowRelativeMouseMode(held, false);
        SDL_SetWindowMouseRect(held, nullptr);
    }
    held = window;
    if (window) {
        SDL_SetWindowRelativeMouseMode(window, c->mode == MIME_POINTER_RELATIVE);
        if (c->mode == MIME_POINTER_CONFINED) {
            MimeRect host = host_part(&w->config);
            Fit fit = window_fit(w);
            double left = fit.x + (c->rect.left - w->config.rect.left - host.left) * fit.scale;
            double top = fit.y + (c->rect.top - w->config.rect.top - host.top) * fit.scale;
            SDL_Rect r = {(int)ceil(left), (int)ceil(top),
                          (int)floor((c->rect.right - c->rect.left) * fit.scale),
                          (int)floor((c->rect.bottom - c->rect.top) * fit.scale)};
            SDL_SetWindowMouseRect(window, r.w > 0 && r.h > 0 ? &r : nullptr);
        } else {
            SDL_SetWindowMouseRect(window, nullptr);
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

/* ------------------------------------------------------------------ main loop. */

void mime_ui_init(MimeUiInputSink sink, void *ctx)
{
    g_sink = sink;
    g_sink_ctx = ctx;
    g_main_thread = pthread_self();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_APP_ID, "mime");
    /* game controller input is independent of window focus and does not target a window. */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        mime_info("no display (%s): windows are not shown", SDL_GetError());
        return;
    }
    g_call_event = SDL_RegisterEvents(1);
    g_video = g_call_event != 0;
    mime_info("display driver %s", SDL_GetCurrentVideoDriver());
    g_wayland = SDL_GetCurrentVideoDriver() && !strcmp(SDL_GetCurrentVideoDriver(), "wayland");
    mime_color_watch(color_changed);
    /*
     * the windows' renderers draw with gl. a renderer that fails, as when the gpu's memory is full, makes SDL put its
     * window back as it was and drop gl with the last gl window, then destroy the renderer through the dropped gl: the
     * whole machine went down. gl stays loaded, and windows are made for gl from the start, so a failed renderer is
     * only a null one.
     */
    if (g_video && g_wayland && !(g_gl = SDL_GL_LoadLibrary(nullptr)))
        mime_warn("cannot load gl for the windows: %s", SDL_GetError());
    if (g_video) {
        update_display();
        update_layout();
        mime_dnd_init();
    }
}

/* ------------------------------------------------------------------ game controllers. */

/* maps a controller to Xbox 360 state, with positive Y upward as in Windows. */
static void emit_pad(SDL_JoystickID id, uint32_t type)
{
    MimeInputEvent ev = {.type = type, .pad = id};
    SDL_Gamepad *g = SDL_GetGamepadFromID(id);
    if (type == MIME_INPUT_PAD_REMOVED || !g) {
        ev.type = MIME_INPUT_PAD_REMOVED;
        emit(&ev);
        return;
    }
    ev.pad_vid = SDL_GetGamepadVendor(g);
    ev.pad_pid = SDL_GetGamepadProduct(g);
    static const struct {
        SDL_GamepadButton button;
        uint16_t bit;
    } BUTTONS[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, 0x0001},      {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0x0002},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0x0004},    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0x0008},
        {SDL_GAMEPAD_BUTTON_START, 0x0010},        {SDL_GAMEPAD_BUTTON_BACK, 0x0020},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, 0x0040},   {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0x0080},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0x0100}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0x0200},
        {SDL_GAMEPAD_BUTTON_GUIDE, 0x0400},        {SDL_GAMEPAD_BUTTON_SOUTH, 0x1000},
        {SDL_GAMEPAD_BUTTON_EAST, 0x2000},         {SDL_GAMEPAD_BUTTON_WEST, 0x4000},
        {SDL_GAMEPAD_BUTTON_NORTH, 0x8000},
    };
    for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); i++)
        if (SDL_GetGamepadButton(g, BUTTONS[i].button))
            ev.pad_buttons |= BUTTONS[i].bit;
    int lx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    int rx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX), ry = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY);
    ev.pad_axes[MIME_PAD_LX] = (int16_t)lx;
    ev.pad_axes[MIME_PAD_LY] = (int16_t)(ly == -32768 ? 32767 : -ly);
    ev.pad_axes[MIME_PAD_RX] = (int16_t)rx;
    ev.pad_axes[MIME_PAD_RY] = (int16_t)(ry == -32768 ? 32767 : -ry);
    ev.pad_axes[MIME_PAD_LT] = (int16_t)(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) * 255 / 32767);
    ev.pad_axes[MIME_PAD_RT] = (int16_t)(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) * 255 / 32767);
    emit(&ev);
}

static void pad_added(SDL_JoystickID id)
{
    if (!SDL_GetGamepadFromID(id) && !SDL_OpenGamepad(id))
        return;
    emit_pad(id, MIME_INPUT_PAD_ADDED);
    emit_pad(id, MIME_INPUT_PAD_STATE);
}

static void pad_removed(SDL_JoystickID id)
{
    emit_pad(id, MIME_INPUT_PAD_REMOVED);
    SDL_Gamepad *g = SDL_GetGamepadFromID(id);
    if (g)
        SDL_CloseGamepad(g);
}

typedef struct RumbleCall {
    uint32_t pad;
    uint16_t low, high;
} RumbleCall;

/* XInput motors run until stopped; SDL requires a duration, so request a long one. */
static void rumble_main(void *arg)
{
    RumbleCall *c = arg;
    SDL_Gamepad *g = SDL_GetGamepadFromID(c->pad);
    if (g)
        SDL_RumbleGamepad(g, c->low, c->high, c->low || c->high ? 600000 : 0);
    free(c);
}

/* a new ramp shows on every window drawn here at once; swapchains take it at their next present. */
static void regrade_main(void *arg)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        UiWindow *w = &g_windows[i];
        if (!w->used || !w->renderer || !w->pixels || !w->shown)
            continue;
        pthread_mutex_lock(&w->lock);
        w->dirty = (MimeRect){0, 0, (int32_t)w->width, (int32_t)w->height};
        w->has_dirty = true;
        pthread_mutex_unlock(&w->lock);
        draw(w);
    }
}

void mime_ui_gamma(const uint16_t *ramp)
{
    mime_gamma_set(ramp);
    if (g_video)
        on_main(regrade_main, nullptr);
}

void mime_ui_pad_rumble(uint32_t pad, uint16_t low, uint16_t high)
{
    RumbleCall *c = malloc(sizeof(*c));
    if (!c)
        return;
    *c = (RumbleCall){pad, low, high};
    if (!g_video) {
        free(c);
        return;
    }
    on_main(rumble_main, c);
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

static Uint32 size_timer(void *data, SDL_TimerID timer, Uint32 interval)
{
    on_main(size_recheck_main, data);
    return 0;
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
    SDL_GetWindowSize(w->window, &logical_w, &logical_h);
    bool interactive = w->interactive_until > now;
    if (!w->maximized && !interactive) {
        if (logical_w == w->want_w && logical_h == w->want_h)
            return;
        /* this may predate the guest request; check again after the request has had time to complete. */
        if (now - w->want_ms < 250) {
            if (!w->size_recheck) {
                w->size_recheck = true;
                SDL_AddTimer(260, size_timer, (void *)(uintptr_t)w->id);
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
    MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_SIZE, .window_id = w->id, .width = width, .height = height};
    emit(&ev);
}

/* tells the guest which monitor a window is on, as the compositor shows it. */
static void report_window_display(UiWindow *w, SDL_DisplayID display)
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
                report_window_display(w, SDL_GetDisplayForWindow(w->window));
            }
        }
    }
}

static void handle_event(const SDL_Event *e)
{
    if (e->type == g_call_event) {
        ((void (*)(void *))e->user.data1)(e->user.data2);
        return;
    }
    /* an empty composition followed by no other text means cancellation. */
    if (e->type != SDL_EVENT_TEXT_INPUT && e->type != SDL_EVENT_TEXT_EDITING)
        mime_ime_flush();
    if (e->type == SDL_EVENT_TEXT_INPUT)
        g_alt_char_pending = false;
    else
        alt_char_flush();
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (!g_layout_known)
            update_layout();
        emit_key(&e->key);
        break;
    case SDL_EVENT_KEYMAP_CHANGED:
        update_layout();
        break;
    case SDL_EVENT_TEXT_INPUT: {
        UiWindow *w = find_by_sdl(e->text.windowID);
        if (!w || !mime_ime_commit(w->id, e->text.text))
            emit_text(&e->text);
        break;
    }
    case SDL_EVENT_TEXT_EDITING: {
        UiWindow *w = find_by_sdl(e->edit.windowID);
        if (w)
            mime_ime_editing(w->id, e->edit.text, e->edit.start, e->edit.length);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        emit_motion(&e->motion);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        uint32_t button = mouse_button(e->button.button);
        if (button)
            emit_mouse(e->button.windowID, e->button.x, e->button.y,
                       e->button.down ? MIME_INPUT_MOUSE_DOWN : MIME_INPUT_MOUSE_UP, button);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        emit_wheel(&e->wheel);
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        pad_added(e->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        pad_removed(e->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        emit_pad(e->gdevice.which, MIME_INPUT_PAD_STATE);
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w) {
            MimeInputEvent ev = {.type = MIME_INPUT_MOUSE_LEAVE, .window_id = w->id};
            emit(&ev);
        }
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        /* a popup that grabs the keyboard takes it from its owner without the guest's window losing the activation. */
        if (w && (w->popup || (e->type == SDL_EVENT_WINDOW_FOCUS_LOST && has_open_popup(w))))
            break;
        if (w) {
            MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_FOCUS, .window_id = w->id,
                                 .x = e->type == SDL_EVENT_WINDOW_FOCUS_GAINED};
            emit(&ev);
            /* the input method of the window that has the keyboard needs to know where text is entered. */
            if (ev.x)
                apply_text_area();
        }
        break;
    }
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w) {
            MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_CLOSE, .window_id = w->id};
            emit(&ev);
        }
        break;
    }
    case SDL_EVENT_WINDOW_MOVED: {
        /*
         * X11 reports window positions. Wayland hides toplevel positions, including in the initial position
         * event.
         */
        UiWindow *w = find_by_sdl(e->window.windowID);
        const char *driver = SDL_GetCurrentVideoDriver();
        if (w && !w->popup && !w->fullscreen && w->configured && !(driver && !strcmp(driver, "wayland"))) {
            MimeRect host = host_part(&w->config);
            double scale = config_scale(&w->config);
            int32_t x = (int32_t)lround(e->window.data1 * scale) - host.left;
            int32_t y = (int32_t)lround(e->window.data2 * scale) - host.top;
            if (x != w->config.rect.left || y != w->config.rect.top) {
                w->config.rect.right += x - w->config.rect.left;
                w->config.rect.bottom += y - w->config.rect.top;
                w->config.rect.left = x;
                w->config.rect.top = y;
                MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_MOVED, .window_id = w->id, .x = x, .y = y};
                emit(&ev);
            }
        }
        break;
    }
    case SDL_EVENT_WINDOW_MINIMIZED: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w && !(w->config.flags & MIME_WIN_MINIMIZED))
            request_window_state(w, 6);
        break;
    }
    case SDL_EVENT_WINDOW_RESTORED: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w && (w->config.flags & MIME_WIN_MINIMIZED)) {
            request_window_state(w, 9);
        } else if (w && w->maximized && !(SDL_GetWindowFlags(w->window) & SDL_WINDOW_MAXIMIZED)) {
            /* the compositor took the window out of the maximized state, as it does when it is dragged. */
            w->maximized = false;
            if (w->config.flags & MIME_WIN_MAXIMIZED) {
                request_window_state(w, 9);
            } else if (!w->popup && !w->fullscreen) {
                /*
                 * the guest restored it. kwin leaves the size of a window it restores to the client, and sdl keeps the
                 * maximized one, not the size asked while it was maximized.
                 */
                MimeRect host = host_part(&w->config);
                double scale = config_scale(&w->config);
                w->want_w = MIME_MAX(1, (int)lround((host.right - host.left) / scale));
                w->want_h = MIME_MAX(1, (int)lround((host.bottom - host.top) / scale));
                w->want_ms = now_ms();
                SDL_SetWindowSize(w->window, w->want_w, w->want_h);
            }
        }
        break;
    }
    case SDL_EVENT_WINDOW_MAXIMIZED: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        /* a window nobody maximized, called so as it opens: the compositor says it of every window. */
        if (w && !w->popup && g_wayland && !w->maximized && !(w->config.flags & MIME_WIN_MAXIMIZED) &&
            now_ms() - w->shown_ms < 1000)
            w->says_maximized = true;
        if (w && !w->popup && !w->says_maximized) {
            w->maximized = true;
            if (!(w->config.flags & MIME_WIN_MAXIMIZED) && !w->fullscreen)
                request_window_state(w, 3);
        }
        break;
    }
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        bool enter = e->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        /* report compositor changes, skipping echoes of guest requests. */
        if (w && !w->popup && w->fullscreen != enter && now_ms() - w->fullscreen_ms > 500) {
            w->fullscreen = enter;
            MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_FULLSCREEN, .window_id = w->id, .flags = enter};
            MimeRect rect = {0, 0, 0, 0};
            if (enter) {
                SDL_DisplayID display = SDL_GetDisplayForWindow(w->window);
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
        break;
    }
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w)
            update_pixel_size(w);
        if (w && e->type == SDL_EVENT_WINDOW_RESIZED)
            report_size(w);
        if (w && w->shown)
            draw(w);
        break;
    }
    case SDL_EVENT_CLIPBOARD_UPDATE:
        mime_clipboard_event(&e->clipboard);
        break;
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w)
            report_window_display(w, (SDL_DisplayID)e->window.data1);
        break;
    }
    case SDL_EVENT_DISPLAY_ORIENTATION:
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_MOVED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
        if (e->type == SDL_EVENT_DISPLAY_ADDED || e->type == SDL_EVENT_DISPLAY_REMOVED)
            mime_color_forget();
        displays_changed(nullptr);
        break;
    default:
        break;
    }
}

void mime_ui_run(void)
{
    /*
     * controller discovery takes 80 ms, so run it here while the guest boots rather than in mime_ui_init.
     * existing controllers arrive as events afterward.
     */
    uint64_t began = mime_now_ns();
    if (g_video && !SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        mime_info("no game controllers (%s)", SDL_GetError());
    mime_info("game controllers %llu us", (unsigned long long)((mime_now_ns() - began) / 1000));
    if (!g_video) {
        pthread_mutex_lock(&g_stop_lock);
        while (!g_stop)
            pthread_cond_wait(&g_stop_cond, &g_stop_lock);
        pthread_mutex_unlock(&g_stop_lock);
        return;
    }
    SDL_Event e;
    while (!g_stop) {
        /* defer composition clears and Alt characters until the next event shows whether text follows. */
        if (mime_ime_pending() || g_alt_char_pending) {
            if (SDL_WaitEventTimeout(&e, g_alt_char_pending ? 10 : 30))
                handle_event(&e);
            else {
                mime_ime_flush();
                alt_char_flush();
            }
        } else if (SDL_WaitEvent(&e)) {
            handle_event(&e);
        } else {
            break;
        }
    }
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used)
            destroy_host_window(&g_windows[i], false);
    mime_color_stop();
    SDL_Quit();
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
    if (g_video) {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = g_call_event;
        ev.user.data1 = (void *)stop_main;
        SDL_PushEvent(&ev);
    }
}

/* macOS uses this entry for per-program app bundles; Linux needs no companion processes. */
int mime_app_proxy_main(int argc, char **argv)
{
    return -1;
}

/* vkwsi.c supplies this entry without exposing Vulkan headers here. */
bool mvk_wsi_readback(uint32_t window, uint32_t source, void *pixels, uint32_t capacity_width, uint32_t capacity_height,
                      uint32_t stride, uint32_t *width, uint32_t *height, MimeRect *client, uint64_t *serial);

bool mime_ui_window_readback(MimeWinReadback *r, void *pixels)
{
    return mvk_wsi_readback(r->window_id, r->source, pixels, r->capacity_width, r->capacity_height, r->stride, &r->width,
                            &r->height, &r->client, &r->serial);
}
