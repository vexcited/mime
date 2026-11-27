#include "wayland.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#include "cursor-shape-v1.h"
#include "fractional-scale-v1.h"
#include "pointer-constraints-unstable-v1.h"
#include "relative-pointer-unstable-v1.h"
#include "text-input-unstable-v3.h"
#include "viewporter.h"
#include "xdg-output-unstable-v1.h"
#include "xdg-shell.h"
#include "xdg-toplevel-icon-v1.h"

#define MAX_BUFFERS 3
#define MAX_ENTERED 8

/* plasma's order of the outputs, the primary one first. described here from the protocol, which is not in wayland-protocols. */
static const struct wl_interface *order_types[] = {nullptr};
static const struct wl_message order_requests[] = {{"destroy", "", order_types}};
static const struct wl_message order_events[] = {{"output", "s", order_types}, {"done", "", order_types}};
static const struct wl_interface order_interface = {"kde_output_order_v1", 1, 1, order_requests, 2, order_events};

typedef struct Buffer {
    struct wl_buffer *buffer;
    struct wl_shm_pool *pool; /* the memory the buffer is of, which a buffer of another size that fits takes over. */
    MimeWlWindow *window; /* null once its window let go of it. */
    uint8_t *pixels;
    size_t size;
    int width, height;
    bool busy, whole;
    MimeRect stale;
} Buffer;

typedef struct Output {
    struct Output *next;
    uint32_t id;
    struct wl_output *output;
    struct zxdg_output_v1 *xdg;
    int x, y, mode_w, mode_h, refresh, scale, transform;
    bool logical;
    int lx, ly, lw, lh;
    char name[32];
    bool listed;
} Output;

struct MimeWlWindow {
    MimeWlWindow *next;
    MimeWlKind kind;
    void *user;
    struct wl_surface *surface;
    struct xdg_surface *xdg;
    struct xdg_toplevel *toplevel;
    struct xdg_popup *popup;
    struct wp_viewport *viewport;
    struct wp_fractional_scale_v1 *fractional;
    MimeWlWindow *parent;
    int x, y;
    int width, height;         /* in surface coordinates. */
    /*
     * the size of what is drawn while the pointer drags a border, 0 otherwise. the compositor tells a size for each
     * move, which the guest takes a moment later: a frame is of the size the guest has, so the window, its pixels
     * and what it presents are always of one size, and the edge that is not dragged stays where it is.
     */
    int content_w, content_h;
    int floating_w, floating_h; /* the size it has when the compositor leaves it free. */
    double scale;
    bool scale_told; /* by the compositor, for this surface. */
    bool shown, configured;
    bool minimized, maximized, fullscreen, tiled;
    bool want_maximized, want_fullscreen;
    uint32_t want_output;
    int min_w, min_h, max_w, max_h;
    bool resizable;
    char title[1024], app_id[200];
    int pending_w, pending_h;
    bool pending_maximized, pending_fullscreen, pending_active, pending_tiled, pending_resizing;
    bool resizing; /* the compositor sizes the window with the pointer, as of its last configure. */
    /*
     * a configure that gives a size is answered by the commit of the buffer of that size. answered before, a commit
     * that comes in between, of the state of a layer, tells the compositor that the buffer of the size before is the
     * answer: kwin then moves the window by half the difference when the buffer of the right size comes.
     */
    bool frame_owed, ack_owed;
    uint32_t ack_serial;
    uint64_t ack_ms;
    uint32_t entered[MAX_ENTERED];
    uint32_t entered_count;
    uint32_t output;
    Buffer *buffers[MAX_BUFFERS];
    bool starved;
    int attached_w, attached_h; /* the size of the buffer on the surface, 0 with none. */
    struct zwp_locked_pointer_v1 *locked;
    struct zwp_confined_pointer_v1 *confined;
    bool has_area;
    int area[4];
    struct wl_buffer *icon;
    uint8_t *icon_pixels;
    int icon_side;
    bool resize_posted;
};

struct MimeWlCursor {
    struct wl_buffer *buffer;
    int width, height, hot_x, hot_y;
    double scale;
};

typedef struct Call {
    struct Call *next;
    void (*fn)(void *);
    void *arg;
    uint64_t due; /* 0 for a call, else when a timer runs. */
} Call;

static struct {
    MimeWlHandlers h;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    uint32_t seat_version;
    struct xdg_wm_base *base;
    struct wp_viewporter *viewporter;
    struct wp_fractional_scale_manager_v1 *fractional;
    struct zwp_text_input_manager_v3 *text_manager;
    struct zwp_pointer_constraints_v1 *constraints;
    struct zwp_relative_pointer_manager_v1 *relative_manager;
    struct wp_cursor_shape_manager_v1 *shapes;
    struct xdg_toplevel_icon_manager_v1 *icons;
    struct zxdg_output_manager_v1 *xdg_outputs;
    struct wl_proxy *order;
    char primary[32];
    bool order_first;
    Output *outputs;
    bool synced; /* the first list of globals is in. */
    bool outputs_posted, outputs_listed;

    MimeWlWindow *windows;
    struct wl_pointer *pointer;
    struct zwp_relative_pointer_v1 *relative;
    struct wp_cursor_shape_device_v1 *shape_device;
    MimeWlWindow *pointer_focus;
    uint32_t enter_serial;
    double pointer_x, pointer_y;
    double axis[2], axis120[2], axis_steps[2];
    bool axis_any, axis_has120, axis_has_steps;

    struct wl_keyboard *keyboard;
    MimeWlWindow *keyboard_focus;
    struct xkb_context *xkb;
    struct xkb_keymap *keymap;
    struct xkb_state *state;
    /*
     * a keyboard a program plays, as wtype or one on the screen, brings a keymap of its own that numbers the keys as
     * it likes. the one of the keyboard the user has is kept: the guest's layout is made of it, and a played key is
     * told as the key of it that types the same.
     */
    struct xkb_keymap *typed_keymap;
    xkb_layout_index_t typed_layout;
    bool played;
    struct xkb_compose_table *compose_table;
    struct xkb_compose_state *compose;
    int repeat_rate, repeat_delay;
    uint32_t repeat_key;
    uint64_t repeat_due;
    uint32_t serial, focus_serial;
    bool no_minimize; /* the compositor said what it can do with a window, and minimizing is not of it. */

    struct zwp_text_input_v3 *text_input;
    MimeWlWindow *text_focus;
    bool has_preedit;

    struct wl_surface *cursor_surface;
    struct wp_viewport *cursor_viewport;
    struct wl_cursor_theme *theme;
    int theme_scale;
    MimeWlCursor *cursor;
    uint32_t shape;

    int wake;
    pthread_mutex_t lock;
    Call *calls, *calls_tail, *timers;
} g = {.lock = PTHREAD_MUTEX_INITIALIZER, .wake = -1, .repeat_rate = 25, .repeat_delay = 400};

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ------------------------------------------------------------------ calls and timers. */

static void wake(void)
{
    uint64_t one = 1;
    if (g.wake >= 0 && write(g.wake, &one, sizeof(one)) < 0) {
    }
}

void mime_wl_post(void (*fn)(void *), void *arg)
{
    Call *c = mime_xmalloc(sizeof(*c));
    *c = (Call){.fn = fn, .arg = arg};
    pthread_mutex_lock(&g.lock);
    if (g.calls_tail)
        g.calls_tail->next = c;
    else
        g.calls = c;
    g.calls_tail = c;
    pthread_mutex_unlock(&g.lock);
    wake();
}

void mime_wl_timer(uint32_t ms, void (*fn)(void *), void *arg)
{
    Call *c = mime_xmalloc(sizeof(*c));
    *c = (Call){.fn = fn, .arg = arg, .due = now_ms() + ms};
    pthread_mutex_lock(&g.lock);
    Call **at = &g.timers;
    while (*at && (*at)->due <= c->due)
        at = &(*at)->next;
    c->next = *at;
    *at = c;
    pthread_mutex_unlock(&g.lock);
    wake();
}

static void run_calls(void)
{
    for (;;) {
        pthread_mutex_lock(&g.lock);
        Call *c = g.calls;
        if (c) {
            g.calls = c->next;
            if (!g.calls)
                g.calls_tail = nullptr;
        } else if (g.timers && g.timers->due <= now_ms()) {
            c = g.timers;
            g.timers = c->next;
        }
        pthread_mutex_unlock(&g.lock);
        if (!c)
            return;
        c->fn(c->arg);
        free(c);
    }
}

/* ------------------------------------------------------------------ outputs. */

static Output *output_of(const struct wl_output *output)
{
    for (Output *o = g.outputs; o; o = o->next)
        if (o->output == output)
            return o;
    return nullptr;
}

static Output *output_by_id(uint32_t id)
{
    for (Output *o = g.outputs; o; o = o->next)
        if (o->id == id)
            return o;
    return nullptr;
}

static void outputs_main(void *arg)
{
    bool listed = g.outputs_listed;
    g.outputs_posted = g.outputs_listed = false;
    if (g.h.outputs)
        g.h.outputs(listed);
}

static void outputs_changed(bool listed)
{
    g.outputs_listed = g.outputs_listed || listed;
    if (!g.synced || g.outputs_posted)
        return;
    g.outputs_posted = true;
    mime_wl_post(outputs_main, nullptr);
}

static void output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y, int32_t pw, int32_t ph,
                            int32_t subpixel, const char *make, const char *model, int32_t transform)
{
    Output *o = data;
    o->x = x;
    o->y = y;
    o->transform = transform;
}

static void output_mode(void *data, struct wl_output *output, uint32_t flags, int32_t width, int32_t height,
                        int32_t refresh)
{
    Output *o = data;
    if (!(flags & WL_OUTPUT_MODE_CURRENT))
        return;
    o->mode_w = width;
    o->mode_h = height;
    o->refresh = refresh;
}

static void output_done(void *data, struct wl_output *output)
{
    Output *o = data;
    bool fresh = !o->listed;
    o->listed = true;
    outputs_changed(fresh);
}

static void output_scale(void *data, struct wl_output *output, int32_t scale)
{
    ((Output *)data)->scale = scale;
}

static void output_name(void *data, struct wl_output *output, const char *name)
{
    Output *o = data;
    mime_strlcpy(o->name, name, sizeof(o->name));
}

static void output_description(void *data, struct wl_output *output, const char *description)
{
}

static const struct wl_output_listener output_listener = {output_geometry, output_mode,       output_done,
                                                          output_scale,    output_name,       output_description};

static void logical_position(void *data, struct zxdg_output_v1 *xdg, int32_t x, int32_t y)
{
    Output *o = data;
    o->lx = x;
    o->ly = y;
}

static void logical_size(void *data, struct zxdg_output_v1 *xdg, int32_t width, int32_t height)
{
    Output *o = data;
    o->lw = width;
    o->lh = height;
    o->logical = width > 0 && height > 0;
}

static void logical_done(void *data, struct zxdg_output_v1 *xdg)
{
    outputs_changed(false);
}

static void logical_name(void *data, struct zxdg_output_v1 *xdg, const char *name)
{
    Output *o = data;
    if (!o->name[0])
        mime_strlcpy(o->name, name, sizeof(o->name));
}

static void logical_description(void *data, struct zxdg_output_v1 *xdg, const char *description)
{
}

static const struct zxdg_output_v1_listener logical_listener = {logical_position, logical_size, logical_done,
                                                                logical_name, logical_description};

static void output_logical(Output *o)
{
    if (o->xdg || !g.xdg_outputs)
        return;
    o->xdg = zxdg_output_manager_v1_get_xdg_output(g.xdg_outputs, o->output);
    zxdg_output_v1_add_listener(o->xdg, &logical_listener, o);
}

static void order_output(void *data, struct wl_proxy *order, const char *name)
{
    if (g.order_first)
        return;
    g.order_first = true;
    mime_strlcpy(g.primary, name, sizeof(g.primary));
}

static void order_done(void *data, struct wl_proxy *order)
{
    g.order_first = false;
    outputs_changed(false);
}

static const void *order_listener[2] = {(void *)order_output, (void *)order_done};

static void output_info(const Output *o, MimeWlOutput *out)
{
    bool turned = o->transform & 1;
    int mode_w = turned ? o->mode_h : o->mode_w, mode_h = turned ? o->mode_w : o->mode_h;
    int scale = o->scale > 0 ? o->scale : 1;
    *out = (MimeWlOutput){.id = o->id, .output = o->output, .refresh = o->refresh / 1000.0};
    mime_strlcpy(out->name, o->name, sizeof(out->name));
    if (o->logical) {
        out->x = o->lx;
        out->y = o->ly;
        out->width = o->lw;
        out->height = o->lh;
        out->scale = mode_w > 0 ? (double)mode_w / o->lw : scale;
    } else {
        out->x = o->x;
        out->y = o->y;
        out->width = mode_w / scale;
        out->height = mode_h / scale;
        out->scale = scale;
    }
    if (out->scale < 1.0)
        out->scale = 1.0;
}

uint32_t mime_wl_outputs(MimeWlOutput *out, uint32_t capacity)
{
    const Output *primary = nullptr;
    for (const Output *o = g.outputs; o && !primary; o = o->next)
        if (o->listed && o->mode_w > 0 && g.primary[0] && !strcmp(o->name, g.primary))
            primary = o;
    /* without an order, the one the coordinates start at. */
    for (const Output *o = g.outputs; o && !primary; o = o->next)
        if (o->listed && o->mode_w > 0 && (o->logical ? o->lx == 0 && o->ly == 0 : o->x == 0 && o->y == 0))
            primary = o;
    uint32_t count = 0;
    if (primary && capacity)
        output_info(primary, &out[count++]);
    for (const Output *o = g.outputs; o && count < capacity; o = o->next)
        if (o != primary && o->listed && o->mode_w > 0)
            output_info(o, &out[count++]);
    return count;
}

static double primary_scale(void)
{
    MimeWlOutput o;
    return mime_wl_outputs(&o, 1) ? o.scale : 1.0;
}

/* ------------------------------------------------------------------ buffers. */

static void buffer_free(Buffer *b)
{
    if (b->buffer)
        wl_buffer_destroy(b->buffer);
    if (b->pool)
        wl_shm_pool_destroy(b->pool);
    if (b->pixels)
        munmap(b->pixels, b->size);
    free(b);
}

static void redraw_main(void *arg)
{
    for (MimeWlWindow *w = g.windows; w; w = w->next)
        if (w == arg && w->starved) {
            w->starved = false;
            if (g.h.redraw)
                g.h.redraw(w);
        }
}

static void buffer_release(void *data, struct wl_buffer *buffer)
{
    Buffer *b = data;
    b->busy = false;
    if (!b->window) {
        buffer_free(b);
        return;
    }
    if (b->window->starved)
        mime_wl_post(redraw_main, b->window);
}

static const struct wl_buffer_listener buffer_listener = {buffer_release};

/* with room for a quarter more: a window that is dragged to a size takes a size for each move of the pointer. */
static struct wl_buffer *shm_buffer(int width, int height, uint8_t **pixels, size_t *size, struct wl_shm_pool **kept)
{
    size_t bytes = (size_t)width * (size_t)height * 4;
    bytes += bytes / 4;
    int fd = memfd_create("mime-window", MFD_CLOEXEC);
    if (fd < 0)
        return nullptr;
    /* its pages come at once: one fault a page costs a frame of a window ten times what the pixels do. */
    void *map = ftruncate(fd, (off_t)bytes) == 0
                    ? mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0)
                    : MAP_FAILED;
    if (map == MAP_FAILED) {
        close(fd);
        return nullptr;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(g.shm, fd, (int32_t)bytes);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_ARGB8888);
    close(fd);
    *pixels = map;
    *size = bytes;
    *kept = pool;
    return buffer;
}

static Buffer *buffer_new(MimeWlWindow *w, int width, int height)
{
    Buffer *b = calloc(1, sizeof(*b));
    if (!b)
        return nullptr;
    b->buffer = shm_buffer(width, height, &b->pixels, &b->size, &b->pool);
    if (!b->buffer) {
        free(b);
        return nullptr;
    }
    b->window = w;
    b->width = width;
    b->height = height;
    b->whole = true;
    wl_buffer_add_listener(b->buffer, &buffer_listener, b);
    return b;
}

/* gives a buffer the compositor let go of another size, in the memory it has. false when that does not fit. */
static bool buffer_resize(Buffer *b, int width, int height)
{
    size_t bytes = (size_t)width * (size_t)height * 4;
    if (b->busy || !b->pool || bytes > b->size || bytes < b->size / 4)
        return false;
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(b->pool, 0, width, height, width * 4, WL_SHM_FORMAT_ARGB8888);
    if (!buffer)
        return false;
    wl_buffer_destroy(b->buffer);
    b->buffer = buffer;
    b->width = width;
    b->height = height;
    b->whole = true;
    b->stale = (MimeRect){};
    wl_buffer_add_listener(b->buffer, &buffer_listener, b);
    return true;
}

/* a buffer the compositor still holds goes when it lets go of it. */
static void buffer_drop(Buffer *b)
{
    if (b->busy)
        b->window = nullptr;
    else
        buffer_free(b);
}

static void drop_buffers(MimeWlWindow *w)
{
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (w->buffers[i])
            buffer_drop(w->buffers[i]);
        w->buffers[i] = nullptr;
    }
    w->starved = false;
    w->attached_w = w->attached_h = 0;
}

static int drawn_w(const MimeWlWindow *w)
{
    return w->content_w > 0 ? w->content_w : w->width;
}

static int drawn_h(const MimeWlWindow *w)
{
    return w->content_h > 0 ? w->content_h : w->height;
}

void mime_wl_window_get_pixels(const MimeWlWindow *w, int *width, int *height)
{
    *width = MIME_MAX(1, (int)lround(drawn_w(w) * w->scale));
    *height = MIME_MAX(1, (int)lround(drawn_h(w) * w->scale));
}

bool mime_wl_window_resizing(const MimeWlWindow *w)
{
    return w->resizing;
}

void mime_wl_window_content(MimeWlWindow *w, int width, int height)
{
    w->content_w = width;
    w->content_h = height;
}

static void map(MimeWlWindow *w);

static MimeRect rect_or(MimeRect a, MimeRect b)
{
    if (a.right <= a.left || a.bottom <= a.top)
        return b;
    if (b.right <= b.left || b.bottom <= b.top)
        return a;
    return (MimeRect){MIME_MIN(a.left, b.left), MIME_MIN(a.top, b.top), MIME_MAX(a.right, b.right),
                      MIME_MAX(a.bottom, b.bottom)};
}

/*
 * makes the buffer the next frame of the window is drawn in. one of a size the window had no buffer of takes its
 * time, which the one who draws does not spend with the lock of the pixels it draws.
 */
void mime_wl_window_ready(MimeWlWindow *w)
{
    if (!w->shown || !w->configured || !w->surface)
        return;
    int width, height;
    mime_wl_window_get_pixels(w, &width, &height);
    int room = -1;
    bool has = false;
    for (int i = 0; i < MAX_BUFFERS; i++) {
        Buffer *b = w->buffers[i];
        if (b && (b->width != width || b->height != height) && !buffer_resize(b, width, height)) {
            buffer_drop(b);
            b = w->buffers[i] = nullptr;
        }
        if (!b)
            room = i;
        else if (!b->busy)
            has = true;
    }
    if (!has && room >= 0)
        w->buffers[room] = buffer_new(w, width, height);
}

bool mime_wl_window_frame(MimeWlWindow *w, const MimeRect *damage, MimeWlFrame *frame)
{
    /* a popup the compositor took away is mapped again by its next frame. */
    if (w->shown && !w->xdg && w->kind != MIME_WL_BARE)
        map(w);
    if (!w->shown || !w->configured || !w->surface)
        return false;
    int width, height;
    mime_wl_window_get_pixels(w, &width, &height);
    MimeRect whole = {0, 0, width, height};
    Buffer *pick = nullptr;
    int room = -1;
    for (int i = 0; i < MAX_BUFFERS; i++) {
        Buffer *b = w->buffers[i];
        if (b && (b->width != width || b->height != height) && !buffer_resize(b, width, height)) {
            buffer_drop(b);
            b = w->buffers[i] = nullptr;
        }
        if (!b) {
            room = i;
            continue;
        }
        if (damage)
            b->stale = rect_or(b->stale, *damage);
        else
            b->whole = true;
        if (!b->busy && (!pick || (pick->whole && !b->whole)))
            pick = b;
    }
    if (!pick && room >= 0)
        pick = w->buffers[room] = buffer_new(w, width, height);
    if (!pick) {
        w->starved = true;
        return false;
    }
    MimeRect stale = pick->whole ? whole : pick->stale;
    stale = (MimeRect){MIME_MAX(stale.left, 0), MIME_MAX(stale.top, 0), MIME_MIN(stale.right, width),
                       MIME_MIN(stale.bottom, height)};
    *frame = (MimeWlFrame){.pixels = pick->pixels, .width = width, .height = height, .stride = width * 4,
                           .stale = stale, .buffer = pick};
    return true;
}

static void apply_viewport(MimeWlWindow *w, int width)
{
    if (w->viewport) {
        wp_viewport_set_destination(w->viewport, drawn_w(w), drawn_h(w));
    } else {
        /* whole scales alone without a viewport. */
        int scale = MIME_MAX(1, width / MIME_MAX(drawn_w(w), 1));
        wl_surface_set_buffer_scale(w->surface, scale);
    }
}

void mime_wl_window_commit(MimeWlWindow *w, const MimeWlFrame *frame, const MimeRect *damage)
{
    Buffer *b = frame->buffer;
    b->busy = true;
    b->whole = false;
    b->stale = (MimeRect){};
    wl_surface_attach(w->surface, b->buffer, 0, 0);
    w->attached_w = frame->width;
    w->attached_h = frame->height;
    if (damage && damage->right > damage->left && damage->bottom > damage->top)
        wl_surface_damage_buffer(w->surface, damage->left, damage->top, damage->right - damage->left,
                                 damage->bottom - damage->top);
    else
        wl_surface_damage_buffer(w->surface, 0, 0, frame->width, frame->height);
    apply_viewport(w, frame->width);
    if (w->ack_owed && w->xdg)
        xdg_surface_ack_configure(w->xdg, w->ack_serial);
    w->ack_owed = w->frame_owed = false;
    wl_surface_commit(w->surface);
    wl_display_flush(g.display);
}

bool mime_wl_window_attached(const MimeWlWindow *w)
{
    int width, height;
    mime_wl_window_get_pixels(w, &width, &height);
    return w->attached_w == width && w->attached_h == height;
}

void mime_wl_window_stale(MimeWlWindow *w)
{
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (w->buffers[i])
            w->buffers[i]->whole = true;
}

void mime_wl_window_commit_state(MimeWlWindow *w)
{
    if (!w->surface)
        return;
    /* no frame came for the size: the configure is answered as it is. */
    if (w->ack_owed && w->xdg && now_ms() - w->ack_ms > 250) {
        xdg_surface_ack_configure(w->xdg, w->ack_serial);
        w->ack_owed = w->frame_owed = false;
    }
    wl_surface_commit(w->surface);
    wl_display_flush(g.display);
}

/* ------------------------------------------------------------------ windows. */

MimeWlWindow *mime_wl_window_of(const struct wl_surface *surface)
{
    for (MimeWlWindow *w = g.windows; w; w = w->next)
        if (w->surface == surface)
            return w;
    return nullptr;
}

static bool window_alive(const MimeWlWindow *w)
{
    for (const MimeWlWindow *o = g.windows; o; o = o->next)
        if (o == w)
            return true;
    return false;
}

static void resized_main(void *arg)
{
    MimeWlWindow *w = arg;
    if (!window_alive(w))
        return;
    w->resize_posted = false;
    if (g.h.resized)
        g.h.resized(w, true);
}

static void post_resized(MimeWlWindow *w)
{
    if (w->resize_posted)
        return;
    w->resize_posted = true;
    mime_wl_post(resized_main, w);
}

static void set_scale(MimeWlWindow *w, double scale)
{
    if (scale < 1.0)
        scale = 1.0;
    if (fabs(scale - w->scale) < 1e-6)
        return;
    w->scale = scale;
    if (w->configured && g.h.resized)
        g.h.resized(w, false);
}

static void surface_enter(void *data, struct wl_surface *surface, struct wl_output *output)
{
    MimeWlWindow *w = data;
    const Output *o = output_of(output);
    if (!o)
        return;
    for (uint32_t i = 0; i < w->entered_count; i++)
        if (w->entered[i] == o->id)
            return;
    if (w->entered_count < MAX_ENTERED)
        w->entered[w->entered_count++] = o->id;
    w->output = o->id;
    if (!w->scale_told)
        set_scale(w, MIME_MAX(w->scale, (double)o->scale));
    if (g.h.output)
        g.h.output(w, o->id);
}

static void surface_leave(void *data, struct wl_surface *surface, struct wl_output *output)
{
    MimeWlWindow *w = data;
    const Output *o = output_of(output);
    if (!o)
        return;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < w->entered_count; i++)
        if (w->entered[i] != o->id)
            w->entered[kept++] = w->entered[i];
    w->entered_count = kept;
    if (w->output == o->id && kept) {
        w->output = w->entered[kept - 1];
        if (g.h.output)
            g.h.output(w, w->output);
    }
}

static void surface_buffer_scale(void *data, struct wl_surface *surface, int32_t factor)
{
    MimeWlWindow *w = data;
    if (w->fractional)
        return;
    w->scale_told = true;
    set_scale(w, factor);
}

static void surface_buffer_transform(void *data, struct wl_surface *surface, uint32_t transform)
{
}

static const struct wl_surface_listener surface_listener = {surface_enter, surface_leave, surface_buffer_scale,
                                                            surface_buffer_transform};

static void fractional_scale(void *data, struct wp_fractional_scale_v1 *fractional, uint32_t scale)
{
    MimeWlWindow *w = data;
    w->scale_told = true;
    set_scale(w, scale / 120.0);
}

static const struct wp_fractional_scale_v1_listener fractional_listener = {fractional_scale};

MimeWlWindow *mime_wl_window_create(MimeWlKind kind, MimeWlWindow *parent, int x, int y, int width, int height,
                                    void *user)
{
    MimeWlWindow *w = calloc(1, sizeof(*w));
    if (!w)
        return nullptr;
    w->surface = wl_compositor_create_surface(g.compositor);
    if (!w->surface) {
        free(w);
        return nullptr;
    }
    w->kind = kind;
    w->user = user;
    w->parent = parent;
    w->x = x;
    w->y = y;
    w->width = w->floating_w = MIME_MAX(width, 1);
    w->height = w->floating_h = MIME_MAX(height, 1);
    w->resizable = true;
    w->scale = parent ? parent->scale : primary_scale();
    wl_surface_add_listener(w->surface, &surface_listener, w);
    if (g.viewporter)
        w->viewport = wp_viewporter_get_viewport(g.viewporter, w->surface);
    if (g.fractional && w->viewport) {
        w->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(g.fractional, w->surface);
        wp_fractional_scale_v1_add_listener(w->fractional, &fractional_listener, w);
    } else {
        /* a buffer of whole pixels alone. */
        w->scale = ceil(w->scale);
    }
    if (kind == MIME_WL_TOOLTIP) {
        struct wl_region *empty = wl_compositor_create_region(g.compositor);
        wl_surface_set_input_region(w->surface, empty);
        wl_region_destroy(empty);
    }
    /* a surface with no role is its owner's to map. */
    w->configured = kind == MIME_WL_BARE;
    w->next = g.windows;
    g.windows = w;
    return w;
}

void *mime_wl_window_user(const MimeWlWindow *w)
{
    return w->user;
}

struct wl_surface *mime_wl_window_surface(const MimeWlWindow *w)
{
    return w->surface;
}

void mime_wl_window_get_size(const MimeWlWindow *w, int *width, int *height)
{
    *width = drawn_w(w);
    *height = drawn_h(w);
}

/* the size the compositor told, which is the one of what is drawn but while a border is dragged. */
void mime_wl_window_get_told(const MimeWlWindow *w, int *width, int *height)
{
    *width = w->width;
    *height = w->height;
}

double mime_wl_window_scale(const MimeWlWindow *w)
{
    return w->scale;
}

bool mime_wl_window_minimized(const MimeWlWindow *w)
{
    return w->minimized;
}

bool mime_wl_window_maximized(const MimeWlWindow *w)
{
    return w->maximized;
}

uint32_t mime_wl_window_output(const MimeWlWindow *w)
{
    return w->output;
}

static void apply_limits(MimeWlWindow *w)
{
    if (!w->toplevel)
        return;
    int min_w = w->min_w, min_h = w->min_h, max_w = w->max_w, max_h = w->max_h;
    if (w->want_fullscreen) {
        /* a compositor leaves a window that says it cannot grow as it is: a game's window has no sizing border. */
        min_w = min_h = max_w = max_h = 0;
    } else if (!w->resizable) {
        min_w = max_w = w->floating_w;
        min_h = max_h = w->floating_h;
    }
    xdg_toplevel_set_min_size(w->toplevel, min_w, min_h);
    xdg_toplevel_set_max_size(w->toplevel, max_w, max_h);
}

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height,
                               struct wl_array *states)
{
    MimeWlWindow *w = data;
    w->pending_w = width;
    w->pending_h = height;
    w->pending_maximized = w->pending_fullscreen = w->pending_active = w->pending_tiled = w->pending_resizing = false;
    const uint32_t *state;
    wl_array_for_each(state, states) {
        switch (*state) {
        case XDG_TOPLEVEL_STATE_MAXIMIZED: w->pending_maximized = true; break;
        case XDG_TOPLEVEL_STATE_FULLSCREEN: w->pending_fullscreen = true; break;
        case XDG_TOPLEVEL_STATE_ACTIVATED: w->pending_active = true; break;
        case XDG_TOPLEVEL_STATE_RESIZING: w->pending_resizing = true; break;
        case XDG_TOPLEVEL_STATE_TILED_LEFT:
        case XDG_TOPLEVEL_STATE_TILED_RIGHT:
        case XDG_TOPLEVEL_STATE_TILED_TOP:
        case XDG_TOPLEVEL_STATE_TILED_BOTTOM: w->pending_tiled = true; break;
        default: break;
        }
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    if (g.h.close)
        g.h.close(data);
}

static void toplevel_bounds(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height)
{
}

static void toplevel_capabilities(void *data, struct xdg_toplevel *toplevel, struct wl_array *capabilities)
{
    bool minimize = false;
    const uint32_t *capability;
    wl_array_for_each(capability, capabilities)
        minimize = minimize || *capability == XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE;
    g.no_minimize = !minimize;
}

static const struct xdg_toplevel_listener toplevel_listener = {toplevel_configure, toplevel_close, toplevel_bounds,
                                                               toplevel_capabilities};

static void state_event(MimeWlWindow *w, MimeWlState state)
{
    if (g.h.state && window_alive(w))
        g.h.state(w, state);
}

static void xdg_configure(void *data, struct xdg_surface *xdg, uint32_t serial)
{
    MimeWlWindow *w = data;
    bool first = !w->configured, sized = false;
    w->configured = true;
    if (!w->toplevel || first)
        xdg_surface_ack_configure(xdg, serial);
    if (w->toplevel) {
        bool held = w->pending_maximized || w->pending_fullscreen || w->pending_tiled;
        int width = w->floating_w, height = w->floating_h;
        /* a window that cannot be sized keeps its size where the compositor leaves it free. */
        if (w->pending_w > 0 && w->pending_h > 0 && (held || w->pending_resizing || w->resizable)) {
            width = w->pending_w;
            height = w->pending_h;
            if (!held) {
                w->floating_w = width;
                w->floating_h = height;
            }
        }
        sized = width != w->width || height != w->height;
        w->width = width;
        w->height = height;
        if (sized && !first)
            w->frame_owed = true;
        if (first) {
            w->frame_owed = w->ack_owed = false;
        } else if (w->frame_owed) {
            w->ack_owed = true;
            w->ack_serial = serial;
            w->ack_ms = now_ms();
        } else {
            xdg_surface_ack_configure(xdg, serial);
        }
        if (!w->pending_resizing)
            w->content_w = w->content_h = 0;
        bool was_maximized = w->maximized, was_fullscreen = w->fullscreen, was_minimized = w->minimized;
        w->maximized = w->pending_maximized;
        w->fullscreen = w->pending_fullscreen;
        w->tiled = w->pending_tiled;
        bool was_resizing = w->resizing;
        w->resizing = w->pending_resizing;
        /* nothing tells of a window that left its icon but that it is the active one again. */
        if (w->minimized && w->pending_active)
            w->minimized = false;
        bool same = was_maximized == w->maximized && was_fullscreen == w->fullscreen && was_minimized == w->minimized;
        /*
         * a drag of the border sends a size for each move of the pointer, faster than a frame is drawn. the sizes
         * read together are answered with one frame, of the last one.
         */
        if (!first && sized && same && w->pending_resizing) {
            post_resized(w);
            return;
        }
        /*
         * the state is told before the size that comes with it: a size of the whole screen told first would be taken
         * for the size of the window as it is, and kept as the one to restore to.
         */
        if (was_minimized && !w->minimized)
            state_event(w, MIME_WL_RESTORED);
        if (!was_maximized && w->maximized)
            state_event(w, MIME_WL_MAXIMIZED);
        else if (was_maximized && !w->maximized)
            state_event(w, MIME_WL_RESTORED);
        if ((first || sized) && window_alive(w)) {
            if (g.h.resized)
                g.h.resized(w, sized);
        }
        if (was_resizing && !w->resizing)
            state_event(w, MIME_WL_RESIZE_ENDED);
        if (was_fullscreen != w->fullscreen)
            state_event(w, w->fullscreen ? MIME_WL_FULLSCREEN_ENTERED : MIME_WL_FULLSCREEN_LEFT);
        /*
         * the answer goes with the next commit, which a size that changed brings with its frame. a state alone, as a
         * window that goes fullscreen at the size it has, draws nothing: the compositor would wait for ever.
         */
        if (!first && !sized && !w->frame_owed && window_alive(w))
            wl_surface_commit(w->surface);
        return;
    }
    if (first && g.h.resized)
        g.h.resized(w, false);
    else if (!first)
        wl_surface_commit(w->surface);
}

static const struct xdg_surface_listener xdg_listener = {xdg_configure};

static void popup_configure(void *data, struct xdg_popup *popup, int32_t x, int32_t y, int32_t width, int32_t height)
{
}

static void unmap(MimeWlWindow *w);

static void popup_done(void *data, struct xdg_popup *popup)
{
    /* the compositor took it away. it comes back when it is shown again. */
    MimeWlWindow *w = data;
    bool shown = w->shown;
    unmap(w);
    w->shown = shown;
}

static void popup_repositioned(void *data, struct xdg_popup *popup, uint32_t token)
{
}

static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

static struct xdg_positioner *positioner_of(const MimeWlWindow *w)
{
    struct xdg_positioner *p = xdg_wm_base_create_positioner(g.base);
    xdg_positioner_set_size(p, w->width, w->height);
    xdg_positioner_set_anchor_rect(p, w->x, w->y, 1, 1);
    xdg_positioner_set_anchor(p, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(p, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    xdg_positioner_set_constraint_adjustment(p, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
                                                    XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);
    return p;
}

static void apply_icon(MimeWlWindow *w)
{
    if (!g.icons || !w->toplevel || !w->icon)
        return;
    struct xdg_toplevel_icon_v1 *icon = xdg_toplevel_icon_manager_v1_create_icon(g.icons);
    xdg_toplevel_icon_v1_add_buffer(icon, w->icon, 1);
    xdg_toplevel_icon_manager_v1_set_icon(g.icons, w->toplevel, icon);
    xdg_toplevel_icon_v1_destroy(icon);
}

static void map(MimeWlWindow *w)
{
    if (w->xdg || w->kind == MIME_WL_BARE || !g.base)
        return;
    if (w->kind != MIME_WL_TOPLEVEL && (!w->parent || !w->parent->xdg)) {
        mime_warn("a popup has no window to hang from");
        return;
    }
    w->xdg = xdg_wm_base_get_xdg_surface(g.base, w->surface);
    xdg_surface_add_listener(w->xdg, &xdg_listener, w);
    if (w->kind == MIME_WL_TOPLEVEL) {
        w->toplevel = xdg_surface_get_toplevel(w->xdg);
        xdg_toplevel_add_listener(w->toplevel, &toplevel_listener, w);
        xdg_toplevel_set_title(w->toplevel, w->title);
        xdg_toplevel_set_app_id(w->toplevel, w->app_id[0] ? w->app_id : "mime");
        if (w->parent && w->parent->toplevel)
            xdg_toplevel_set_parent(w->toplevel, w->parent->toplevel);
        apply_limits(w);
        if (w->want_maximized)
            xdg_toplevel_set_maximized(w->toplevel);
        if (w->want_fullscreen) {
            const Output *o = output_by_id(w->want_output);
            xdg_toplevel_set_fullscreen(w->toplevel, o ? o->output : nullptr);
        }
        apply_icon(w);
    } else {
        struct xdg_positioner *p = positioner_of(w);
        w->popup = xdg_surface_get_popup(w->xdg, w->parent->xdg, p);
        xdg_positioner_destroy(p);
        xdg_popup_add_listener(w->popup, &popup_listener, w);
    }
    wl_surface_commit(w->surface);
    wl_display_flush(g.display);
}

static void unmap(MimeWlWindow *w)
{
    /* its popups go first, as the protocol has it. */
    for (MimeWlWindow *o = g.windows; o; o = o->next)
        if (o->parent == w && o->kind != MIME_WL_TOPLEVEL && o->kind != MIME_WL_BARE && o->xdg) {
            bool shown = o->shown;
            unmap(o);
            o->shown = shown;
        }
    if (w->popup)
        xdg_popup_destroy(w->popup);
    if (w->toplevel)
        xdg_toplevel_destroy(w->toplevel);
    if (w->xdg)
        xdg_surface_destroy(w->xdg);
    w->popup = nullptr;
    w->toplevel = nullptr;
    w->xdg = nullptr;
    w->shown = false;
    if (w->kind != MIME_WL_BARE)
        w->configured = false;
    w->maximized = w->fullscreen = w->tiled = false;
    w->entered_count = 0;
    if (w->surface) {
        wl_surface_attach(w->surface, nullptr, 0, 0);
        wl_surface_commit(w->surface);
    }
    drop_buffers(w);
    wl_display_flush(g.display);
}

void mime_wl_window_show(MimeWlWindow *w)
{
    w->shown = true;
    map(w);
}

void mime_wl_window_hide(MimeWlWindow *w)
{
    if (w->shown || w->xdg)
        unmap(w);
}

static void apply_cursor(void);

void mime_wl_window_destroy(MimeWlWindow *w)
{
    unmap(w);
    for (MimeWlWindow **at = &g.windows; *at; at = &(*at)->next)
        if (*at == w) {
            *at = w->next;
            break;
        }
    for (MimeWlWindow *o = g.windows; o; o = o->next)
        if (o->parent == w)
            o->parent = nullptr;
    if (g.pointer_focus == w)
        g.pointer_focus = nullptr;
    if (g.keyboard_focus == w)
        g.keyboard_focus = nullptr;
    if (g.text_focus == w)
        g.text_focus = nullptr;
    if (w->locked)
        zwp_locked_pointer_v1_destroy(w->locked);
    if (w->confined)
        zwp_confined_pointer_v1_destroy(w->confined);
    if (w->icon)
        wl_buffer_destroy(w->icon);
    free(w->icon_pixels);
    if (w->fractional)
        wp_fractional_scale_v1_destroy(w->fractional);
    if (w->viewport)
        wp_viewport_destroy(w->viewport);
    wl_surface_destroy(w->surface);
    wl_display_flush(g.display);
    free(w);
}

void mime_wl_window_title(MimeWlWindow *w, const char *title)
{
    if (!strcmp(w->title, title))
        return;
    mime_strlcpy(w->title, title, sizeof(w->title));
    if (w->toplevel)
        xdg_toplevel_set_title(w->toplevel, w->title);
}

void mime_wl_window_app_id(MimeWlWindow *w, const char *app_id)
{
    mime_strlcpy(w->app_id, app_id, sizeof(w->app_id));
    if (w->toplevel) {
        xdg_toplevel_set_app_id(w->toplevel, w->app_id);
        wl_display_flush(g.display);
    }
}

void mime_wl_window_parent(MimeWlWindow *w, MimeWlWindow *parent)
{
    if (w->kind != MIME_WL_TOPLEVEL || w->parent == parent)
        return;
    w->parent = parent;
    if (w->toplevel)
        xdg_toplevel_set_parent(w->toplevel, parent ? parent->toplevel : nullptr);
}

static void reposition(MimeWlWindow *w)
{
    if (!w->popup)
        return;
    if (xdg_popup_get_version(w->popup) >= XDG_POPUP_REPOSITION_SINCE_VERSION) {
        struct xdg_positioner *p = positioner_of(w);
        xdg_popup_reposition(w->popup, p, 0);
        xdg_positioner_destroy(p);
        return;
    }
    /* an old compositor places a popup once: it is mapped again. */
    unmap(w);
    mime_wl_window_show(w);
}

void mime_wl_window_size(MimeWlWindow *w, int width, int height)
{
    width = MIME_MAX(width, 1);
    height = MIME_MAX(height, 1);
    w->floating_w = width;
    w->floating_h = height;
    if (!w->resizable)
        apply_limits(w);
    w->content_w = w->content_h = 0;
    if (w->maximized || w->fullscreen || w->tiled || (w->width == width && w->height == height))
        return;
    w->width = width;
    w->height = height;
    reposition(w);
    post_resized(w);
}

void mime_wl_window_limits(MimeWlWindow *w, int min_w, int min_h, int max_w, int max_h, bool resizable)
{
    w->min_w = min_w;
    w->min_h = min_h;
    w->max_w = max_w;
    w->max_h = max_h;
    w->resizable = resizable;
    apply_limits(w);
}

void mime_wl_window_geometry(MimeWlWindow *w, int width, int height)
{
    if (w->xdg)
        xdg_surface_set_window_geometry(w->xdg, 0, 0, width, height);
}

void mime_wl_window_position(MimeWlWindow *w, int x, int y)
{
    if (w->x == x && w->y == y)
        return;
    w->x = x;
    w->y = y;
    reposition(w);
}

void mime_wl_window_minimize(MimeWlWindow *w)
{
    if (w->kind != MIME_WL_TOPLEVEL)
        return;
    /* a desktop that cannot minimize shows the window that starts as an icon. */
    if (g.no_minimize) {
        if (!w->toplevel)
            mime_wl_window_show(w);
        return;
    }
    /* a window that starts as an icon is one of the compositor's all the same. */
    if (!w->toplevel)
        mime_wl_window_show(w);
    if (!w->toplevel)
        return;
    xdg_toplevel_set_minimized(w->toplevel);
    w->minimized = true;
    wl_display_flush(g.display);
}

void mime_wl_window_unminimize(MimeWlWindow *w)
{
    w->minimized = false;
}

void mime_wl_window_maximize(MimeWlWindow *w, bool maximized)
{
    w->want_maximized = maximized;
    if (!w->toplevel)
        return;
    if (maximized)
        xdg_toplevel_set_maximized(w->toplevel);
    else
        xdg_toplevel_unset_maximized(w->toplevel);
    wl_display_flush(g.display);
}

void mime_wl_window_fullscreen(MimeWlWindow *w, bool fullscreen, uint32_t output)
{
    w->want_fullscreen = fullscreen;
    w->want_output = output;
    if (!w->toplevel)
        return;
    apply_limits(w);
    const Output *o = output_by_id(output);
    if (fullscreen)
        xdg_toplevel_set_fullscreen(w->toplevel, o ? o->output : nullptr);
    else
        xdg_toplevel_unset_fullscreen(w->toplevel);
    wl_display_flush(g.display);
}

void mime_wl_window_resizable(MimeWlWindow *w, bool resizable)
{
    w->resizable = resizable;
    apply_limits(w);
}

bool mime_wl_window_move(MimeWlWindow *w, uint32_t serial)
{
    if (!w->toplevel || !g.seat)
        return false;
    xdg_toplevel_move(w->toplevel, g.seat, serial);
    wl_display_flush(g.display);
    return true;
}

bool mime_wl_window_resize(MimeWlWindow *w, uint32_t serial, uint32_t edge)
{
    if (!w->toplevel || !g.seat)
        return false;
    xdg_toplevel_resize(w->toplevel, g.seat, serial, edge);
    wl_display_flush(g.display);
    return true;
}

static void premultiply(uint8_t *out, const uint8_t *in, size_t count)
{
    for (size_t i = 0; i < count; i++, in += 4, out += 4) {
        uint32_t a = in[3];
        out[0] = (uint8_t)((in[0] * a + 127) / 255);
        out[1] = (uint8_t)((in[1] * a + 127) / 255);
        out[2] = (uint8_t)((in[2] * a + 127) / 255);
        out[3] = (uint8_t)a;
    }
}

void mime_wl_window_icon(MimeWlWindow *w, const uint8_t *pixels, int width, int height)
{
    if (!g.icons || width <= 0 || height <= 0)
        return;
    /* the protocol takes squares alone. */
    int side = MIME_MAX(width, height);
    uint8_t *map;
    size_t size;
    struct wl_shm_pool *pool = nullptr;
    struct wl_buffer *buffer = shm_buffer(side, side, &map, &size, &pool);
    if (pool)
        wl_shm_pool_destroy(pool);
    if (!buffer)
        return;
    memset(map, 0, size);
    int left = (side - width) / 2, top = (side - height) / 2;
    for (int y = 0; y < height; y++)
        premultiply(map + ((size_t)(top + y) * side + left) * 4, pixels + (size_t)y * width * 4, (size_t)width);
    munmap(map, size);
    if (w->icon)
        wl_buffer_destroy(w->icon);
    w->icon = buffer;
    apply_icon(w);
    wl_display_flush(g.display);
}

/* ------------------------------------------------------------------ text input. */

static void text_enable(void)
{
    MimeWlWindow *w = g.text_focus;
    if (!g.text_input || !w)
        return;
    zwp_text_input_v3_enable(g.text_input);
    zwp_text_input_v3_set_content_type(g.text_input, ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
                                       ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
    if (w->has_area)
        zwp_text_input_v3_set_cursor_rectangle(g.text_input, w->area[0], w->area[1], w->area[2], w->area[3]);
    zwp_text_input_v3_commit(g.text_input);
}

static void text_enter(void *data, struct zwp_text_input_v3 *text, struct wl_surface *surface)
{
    g.text_focus = mime_wl_window_of(surface);
    text_enable();
}

static void text_leave(void *data, struct zwp_text_input_v3 *text, struct wl_surface *surface)
{
    g.text_focus = nullptr;
    zwp_text_input_v3_disable(text);
    zwp_text_input_v3_commit(text);
}

/* the count of characters in the first bytes of a text. */
static int chars_before(const char *text, int bytes)
{
    int count = 0;
    for (int i = 0; i < bytes && text[i]; i++)
        count += ((unsigned char)text[i] & 0xC0) != 0x80;
    return count;
}

static void text_preedit(void *data, struct zwp_text_input_v3 *text, const char *string, int32_t begin, int32_t end)
{
    MimeWlWindow *w = g.text_focus ? g.text_focus : g.keyboard_focus;
    if (!w || !g.h.editing)
        return;
    if (!string || !string[0]) {
        g.h.editing(w, "", 0, 0);
        return;
    }
    g.has_preedit = true;
    int start = begin < 0 ? -1 : chars_before(string, begin);
    int length = begin < 0 || end < begin ? -1 : chars_before(string + begin, end - begin);
    g.h.editing(w, string, start, length);
}

static void text_commit(void *data, struct zwp_text_input_v3 *text, const char *string)
{
    MimeWlWindow *w = g.text_focus ? g.text_focus : g.keyboard_focus;
    if (w && string && string[0] && g.h.text)
        g.h.text(w, string);
}

static void text_delete(void *data, struct zwp_text_input_v3 *text, uint32_t before, uint32_t after)
{
}

static void text_done(void *data, struct zwp_text_input_v3 *text, uint32_t serial)
{
    MimeWlWindow *w = g.text_focus ? g.text_focus : g.keyboard_focus;
    if (!g.has_preedit && w && g.h.editing)
        g.h.editing(w, "", 0, 0);
    g.has_preedit = false;
}

static void text_action(void *data, struct zwp_text_input_v3 *text, uint32_t action, uint32_t serial)
{
}

static void text_language(void *data, struct zwp_text_input_v3 *text, const char *language)
{
}

static void text_hint(void *data, struct zwp_text_input_v3 *text, uint32_t start, uint32_t end, uint32_t hint)
{
}

static const struct zwp_text_input_v3_listener text_listener = {text_enter,  text_leave, text_preedit,
                                                               text_commit, text_delete, text_done,
                                                               text_action, text_language, text_hint};

void mime_wl_window_text_area(MimeWlWindow *w, int x, int y, int width, int height)
{
    w->has_area = true;
    w->area[0] = x;
    w->area[1] = y;
    w->area[2] = width;
    w->area[3] = height;
    if (g.text_input && g.text_focus == w) {
        zwp_text_input_v3_set_cursor_rectangle(g.text_input, x, y, width, height);
        zwp_text_input_v3_commit(g.text_input);
    }
}

/* the protocol resets the state of the input method when the input is enabled again. */
void mime_wl_text_reset(void)
{
    if (!g.text_input || !g.text_focus)
        return;
    zwp_text_input_v3_disable(g.text_input);
    zwp_text_input_v3_commit(g.text_input);
    text_enable();
}

bool mime_wl_has_text_input(void)
{
    return g.text_manager != nullptr;
}

/* ------------------------------------------------------------------ cursor. */

static const struct {
    uint32_t shape;
    const char *names[3];
} g_shapes[MIME_WL_SHAPES] = {
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT, {"default", "left_ptr"}},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT, {"text", "xterm"}},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT, {"wait", "watch"}},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR, {"crosshair", "cross"}},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER, {"pointer", "hand2", "hand1"}},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE, {"ns-resize", "sb_v_double_arrow", "size_ver"}},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE, {"ew-resize", "sb_h_double_arrow", "size_hor"}},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE, {"nwse-resize", "size_fdiag", "bd_double_arrow"}},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE, {"nesw-resize", "size_bdiag", "fd_double_arrow"}},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE, {"move", "fleur", "all-scroll"}},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED, {"not-allowed", "crossed_circle", "forbidden"}},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS, {"progress", "left_ptr_watch", "half-busy"}},
};

static struct wl_surface *cursor_surface(void)
{
    if (!g.cursor_surface) {
        g.cursor_surface = wl_compositor_create_surface(g.compositor);
        if (g.cursor_surface && g.viewporter)
            g.cursor_viewport = wp_viewporter_get_viewport(g.viewporter, g.cursor_surface);
    }
    return g.cursor_surface;
}

static void theme_cursor(uint32_t shape)
{
    int scale = g.pointer_focus ? (int)ceil(g.pointer_focus->scale) : 1;
    if (!g.theme || g.theme_scale != scale) {
        if (g.theme)
            wl_cursor_theme_destroy(g.theme);
        const char *size_text = getenv("XCURSOR_SIZE");
        int size = size_text ? atoi(size_text) : 0;
        g.theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"), (size > 0 ? size : 24) * scale, g.shm);
        g.theme_scale = scale;
    }
    struct wl_cursor *cursor = nullptr;
    for (int i = 0; g.theme && i < 3 && !cursor; i++)
        if (g_shapes[shape].names[i])
            cursor = wl_cursor_theme_get_cursor(g.theme, g_shapes[shape].names[i]);
    struct wl_surface *surface = cursor ? cursor_surface() : nullptr;
    if (!surface || !cursor->image_count)
        return;
    struct wl_cursor_image *image = cursor->images[0];
    if (g.cursor_viewport)
        wp_viewport_set_destination(g.cursor_viewport, -1, -1);
    wl_surface_set_buffer_scale(surface, scale);
    wl_surface_attach(surface, wl_cursor_image_get_buffer(image), 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, (int32_t)image->width, (int32_t)image->height);
    wl_surface_commit(surface);
    wl_pointer_set_cursor(g.pointer, g.enter_serial, surface, (int32_t)image->hotspot_x / scale,
                          (int32_t)image->hotspot_y / scale);
}

static void apply_cursor(void)
{
    if (!g.pointer || !g.pointer_focus)
        return;
    bool hidden = (!g.cursor && g.shape >= MIME_WL_SHAPES) || g.pointer_focus->locked;
    if (hidden) {
        wl_pointer_set_cursor(g.pointer, g.enter_serial, nullptr, 0, 0);
    } else if (g.cursor) {
        const MimeWlCursor *c = g.cursor;
        struct wl_surface *surface = cursor_surface();
        if (!surface)
            return;
        int width = MIME_MAX(1, (int)lround(c->width / c->scale)), height = MIME_MAX(1, (int)lround(c->height / c->scale));
        wl_surface_set_buffer_scale(surface, 1);
        if (g.cursor_viewport)
            wp_viewport_set_destination(g.cursor_viewport, width, height);
        wl_surface_attach(surface, c->buffer, 0, 0);
        wl_surface_damage_buffer(surface, 0, 0, c->width, c->height);
        wl_surface_commit(surface);
        wl_pointer_set_cursor(g.pointer, g.enter_serial, surface, (int32_t)lround(c->hot_x / c->scale),
                              (int32_t)lround(c->hot_y / c->scale));
    } else if (g.shape_device) {
        wp_cursor_shape_device_v1_set_shape(g.shape_device, g.enter_serial, g_shapes[g.shape].shape);
    } else {
        theme_cursor(g.shape);
    }
    wl_display_flush(g.display);
}

/* the mean of the image pixels each pixel of a smaller image covers. */
static void shrink(uint8_t *out, int out_w, int out_h, const uint8_t *in, int in_w, int in_h)
{
    for (int y = 0; y < out_h; y++) {
        int top = y * in_h / out_h, bottom = MIME_MAX((y + 1) * in_h / out_h, top + 1);
        for (int x = 0; x < out_w; x++) {
            int left = x * in_w / out_w, right = MIME_MAX((x + 1) * in_w / out_w, left + 1);
            uint32_t sum[4] = {}, count = 0;
            for (int sy = top; sy < bottom && sy < in_h; sy++)
                for (int sx = left; sx < right && sx < in_w; sx++, count++)
                    for (int k = 0; k < 4; k++)
                        sum[k] += in[((size_t)sy * in_w + sx) * 4 + k];
            for (int k = 0; k < 4; k++)
                out[((size_t)y * out_w + x) * 4 + k] = (uint8_t)(count ? sum[k] / count : 0);
        }
    }
}

MimeWlCursor *mime_wl_cursor_create(const uint8_t *pixels, int width, int height, int hot_x, int hot_y, double scale)
{
    if (width <= 0 || height <= 0 || !g.shm)
        return nullptr;
    MimeWlCursor *c = calloc(1, sizeof(*c));
    uint8_t *straight = malloc((size_t)width * height * 4);
    if (!c || !straight) {
        free(c);
        free(straight);
        return nullptr;
    }
    premultiply(straight, pixels, (size_t)width * height);
    *c = (MimeWlCursor){.width = width, .height = height, .hot_x = hot_x, .hot_y = hot_y,
                        .scale = scale >= 1.0 ? scale : 1.0};
    /* without a viewport the compositor shows a buffer at its own size alone. */
    if (!g.viewporter && c->scale > 1.0) {
        int small_w = MIME_MAX(1, (int)lround(width / c->scale)), small_h = MIME_MAX(1, (int)lround(height / c->scale));
        uint8_t *small = malloc((size_t)small_w * small_h * 4);
        if (small) {
            shrink(small, small_w, small_h, straight, width, height);
            free(straight);
            straight = small;
            c->hot_x = (int)lround(hot_x / c->scale);
            c->hot_y = (int)lround(hot_y / c->scale);
            c->width = small_w;
            c->height = small_h;
            c->scale = 1.0;
        }
    }
    uint8_t *map;
    size_t size;
    struct wl_shm_pool *pool = nullptr;
    c->buffer = shm_buffer(c->width, c->height, &map, &size, &pool);
    if (pool)
        wl_shm_pool_destroy(pool);
    if (!c->buffer) {
        free(straight);
        free(c);
        return nullptr;
    }
    memcpy(map, straight, size);
    munmap(map, size);
    free(straight);
    return c;
}

void mime_wl_cursor_destroy(MimeWlCursor *cursor)
{
    if (!cursor)
        return;
    if (g.cursor == cursor) {
        g.cursor = nullptr;
        apply_cursor();
    }
    wl_buffer_destroy(cursor->buffer);
    free(cursor);
}

void mime_wl_cursor_set(MimeWlCursor *cursor, uint32_t shape)
{
    g.cursor = cursor;
    g.shape = shape;
    apply_cursor();
}

/* ------------------------------------------------------------------ pointer. */

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t x, wl_fixed_t y)
{
    g.pointer_focus = surface ? mime_wl_window_of(surface) : nullptr;
    g.enter_serial = serial;
    g.pointer_x = wl_fixed_to_double(x);
    g.pointer_y = wl_fixed_to_double(y);
    if (!g.pointer_focus)
        return;
    apply_cursor();
    if (g.h.motion)
        g.h.motion(g.pointer_focus, g.pointer_x, g.pointer_y, 0, 0);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface)
{
    MimeWlWindow *w = g.pointer_focus;
    g.pointer_focus = nullptr;
    if (w && g.h.leave)
        g.h.leave(w);
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    double px = wl_fixed_to_double(x), py = wl_fixed_to_double(y);
    double dx = px - g.pointer_x, dy = py - g.pointer_y;
    g.pointer_x = px;
    g.pointer_y = py;
    if (g.pointer_focus && g.h.motion)
        g.h.motion(g.pointer_focus, px, py, dx, dy);
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state)
{
    bool down = state == WL_POINTER_BUTTON_STATE_PRESSED;
    if (down)
        g.serial = serial;
    if (g.pointer_focus && g.h.button)
        g.h.button(g.pointer_focus, g.pointer_x, g.pointer_y, button, down);
}

static void axis_flush(void)
{
    if (!g.axis_any)
        return;
    double wheel[2];
    for (int i = 0; i < 2; i++) {
        /* a wheel tells its notches, a touchpad a distance of which ten are taken for one. */
        double v = g.axis_has120 ? g.axis120[i] / 120.0 : g.axis_has_steps ? g.axis_steps[i] : g.axis[i] / 10.0;
        wheel[i] = v;
    }
    g.axis_any = g.axis_has120 = g.axis_has_steps = false;
    memset(g.axis, 0, sizeof(g.axis));
    memset(g.axis120, 0, sizeof(g.axis120));
    memset(g.axis_steps, 0, sizeof(g.axis_steps));
    /* the compositor counts down and right, the handler up and right. */
    if (g.pointer_focus && g.h.wheel && (wheel[0] != 0 || wheel[1] != 0))
        g.h.wheel(g.pointer_focus, g.pointer_x, g.pointer_y, wheel[1], -wheel[0]);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    if (axis > 1)
        return;
    g.axis[axis] += wl_fixed_to_double(value);
    g.axis_any = true;
    if (wl_pointer_get_version(pointer) < WL_POINTER_FRAME_SINCE_VERSION)
        axis_flush();
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    axis_flush();
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source)
{
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete)
{
    if (axis > 1)
        return;
    g.axis_steps[axis] += discrete;
    g.axis_has_steps = g.axis_any = true;
}

static void pointer_axis_value120(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t value120)
{
    if (axis > 1)
        return;
    g.axis120[axis] += value120;
    g.axis_has120 = g.axis_any = true;
}

/* a wheel the desktop turned around stays so: the guest scrolls the way the host's programs do. */
static void pointer_axis_direction(void *data, struct wl_pointer *pointer, uint32_t axis, uint32_t direction)
{
}

static void pointer_warp(void *data, struct wl_pointer *pointer, wl_fixed_t x, wl_fixed_t y)
{
    g.pointer_x = wl_fixed_to_double(x);
    g.pointer_y = wl_fixed_to_double(y);
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_enter,         pointer_leave,         pointer_motion,         pointer_button,
    pointer_axis,          pointer_frame,         pointer_axis_source,    pointer_axis_stop,
    pointer_axis_discrete, pointer_axis_value120, pointer_axis_direction, pointer_warp};

static void relative_motion(void *data, struct zwp_relative_pointer_v1 *relative, uint32_t hi, uint32_t lo,
                            wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t raw_dx, wl_fixed_t raw_dy)
{
    MimeWlWindow *w = g.pointer_focus;
    if (w && w->locked && g.h.motion)
        g.h.motion(w, g.pointer_x, g.pointer_y, wl_fixed_to_double(raw_dx), wl_fixed_to_double(raw_dy));
}

static const struct zwp_relative_pointer_v1_listener relative_listener = {relative_motion};

void mime_wl_pointer_lock(MimeWlWindow *w, bool locked)
{
    if (!g.constraints || !g.pointer || locked == (w->locked != nullptr))
        return;
    if (locked) {
        /* a surface holds one constraint of a pointer. */
        if (w->confined)
            zwp_confined_pointer_v1_destroy(w->confined);
        w->confined = nullptr;
        w->locked = zwp_pointer_constraints_v1_lock_pointer(g.constraints, w->surface, g.pointer, nullptr,
                                                            ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    } else {
        zwp_locked_pointer_v1_destroy(w->locked);
        w->locked = nullptr;
    }
    apply_cursor();
    wl_display_flush(g.display);
}

bool mime_wl_pointer_locked(const MimeWlWindow *w)
{
    return w->locked != nullptr;
}

void mime_wl_pointer_confine(MimeWlWindow *w, const MimeRect *rect)
{
    if (!g.constraints || !g.pointer)
        return;
    if (w->confined)
        zwp_confined_pointer_v1_destroy(w->confined);
    w->confined = nullptr;
    if (rect && !w->locked) {
        struct wl_region *region = wl_compositor_create_region(g.compositor);
        wl_region_add(region, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top);
        w->confined = zwp_pointer_constraints_v1_confine_pointer(g.constraints, w->surface, g.pointer, region,
                                                                 ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        wl_region_destroy(region);
    }
    wl_display_flush(g.display);
}

MimeWlWindow *mime_wl_pointer_focus(void)
{
    return g.pointer_focus;
}

/* ------------------------------------------------------------------ keyboard. */

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size)
{
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    char *text = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (text == MAP_FAILED)
        return;
    struct xkb_keymap *keymap = xkb_keymap_new_from_buffer(g.xkb, text, strnlen(text, size), XKB_KEYMAP_FORMAT_TEXT_V1,
                                                          XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(text, size);
    struct xkb_state *state = keymap ? xkb_state_new(keymap) : nullptr;
    if (!state) {
        xkb_keymap_unref(keymap);
        return;
    }
    xkb_state_unref(g.state);
    xkb_keymap_unref(g.keymap);
    g.keymap = keymap;
    g.state = state;
    /* a played keyboard's layout has no name. */
    const char *name = xkb_keymap_layout_get_name(keymap, 0);
    g.played = (!name || !*name) && g.typed_keymap;
    if (g.played)
        return;
    xkb_keymap_unref(g.typed_keymap);
    g.typed_keymap = xkb_keymap_ref(keymap);
    g.typed_layout = 0;
    if (g.h.keymap)
        g.h.keymap();
}

/* the key of the user's keyboard that types what a played key does, false when it has none. */
static bool typed_key(uint32_t key, uint32_t *out)
{
    if (!g.played) {
        *out = key;
        return true;
    }
    const xkb_keysym_t *syms;
    xkb_layout_index_t layout = xkb_state_key_get_layout(g.state, key + 8);
    if (layout == XKB_LAYOUT_INVALID || xkb_keymap_key_get_syms_by_level(g.keymap, key + 8, layout, 0, &syms) < 1)
        return false;
    xkb_keysym_t sym = syms[0];
    xkb_keycode_t first = xkb_keymap_min_keycode(g.typed_keymap), last = xkb_keymap_max_keycode(g.typed_keymap);
    for (xkb_level_index_t level = 0; level < 2; level++)
        for (xkb_keycode_t code = first < 8 ? 8 : first; code <= last; code++)
            if (level < xkb_keymap_num_levels_for_key(g.typed_keymap, code, g.typed_layout) &&
                xkb_keymap_key_get_syms_by_level(g.typed_keymap, code, g.typed_layout, level, &syms) >= 1 &&
                syms[0] == sym) {
                *out = code - 8;
                return true;
            }
    return false;
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys)
{
    MimeWlWindow *w = surface ? mime_wl_window_of(surface) : nullptr;
    g.keyboard_focus = w;
    g.focus_serial = serial;
    if (w && g.h.focus)
        g.h.focus(w, true);
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface)
{
    MimeWlWindow *w = g.keyboard_focus;
    g.keyboard_focus = nullptr;
    g.repeat_due = 0;
    if (g.compose)
        xkb_compose_state_reset(g.compose);
    if (w && g.h.focus)
        g.h.focus(w, false);
}

uint32_t mime_wl_mods(void)
{
    static const struct {
        const char *name;
        uint32_t bit;
    } mods[] = {{XKB_MOD_NAME_SHIFT, MIME_WL_MOD_SHIFT}, {XKB_MOD_NAME_CTRL, MIME_WL_MOD_CTRL},
                {XKB_MOD_NAME_ALT, MIME_WL_MOD_ALT},     {XKB_MOD_NAME_LOGO, MIME_WL_MOD_LOGO},
                {XKB_MOD_NAME_CAPS, MIME_WL_MOD_CAPS}};
    uint32_t out = 0;
    for (size_t i = 0; g.state && i < MIME_ARRAY_COUNT(mods); i++)
        if (xkb_state_mod_name_is_active(g.state, mods[i].name, XKB_STATE_MODS_EFFECTIVE) > 0)
            out |= mods[i].bit;
    return out;
}

/* the text a key press types, through the dead keys and the compose sequences of the locale. */
static void key_text(uint32_t key, bool repeat)
{
    MimeWlWindow *w = g.keyboard_focus;
    if (!g.state || !w || !g.h.text)
        return;
    xkb_keycode_t code = key + 8;
    char text[64] = "";
    bool composed = false;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(g.state, code);
    if (!repeat && g.compose && sym != XKB_KEY_NoSymbol &&
        xkb_compose_state_feed(g.compose, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
        switch (xkb_compose_state_get_status(g.compose)) {
        case XKB_COMPOSE_COMPOSING:
            return;
        case XKB_COMPOSE_CANCELLED:
            xkb_compose_state_reset(g.compose);
            return;
        case XKB_COMPOSE_COMPOSED:
            xkb_compose_state_get_utf8(g.compose, text, sizeof(text));
            xkb_compose_state_reset(g.compose);
            composed = true;
            break;
        default:
            break;
        }
    }
    if (!composed)
        xkb_state_key_get_utf8(g.state, code, text, sizeof(text));
    /* a key held with control or alt is a command, which the guest makes of the key itself. */
    if (mime_wl_mods() & (MIME_WL_MOD_CTRL | MIME_WL_MOD_ALT))
        return;
    if ((unsigned char)text[0] < 0x20 || text[0] == 0x7F)
        return;
    g.h.text(w, text);
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key,
                         uint32_t state)
{
    bool down = state != WL_KEYBOARD_KEY_STATE_RELEASED;
    /* the compositor repeats keys itself from version 10 on. */
    bool repeat = state == 2;
    MimeWlWindow *w = g.keyboard_focus;
    if (down)
        g.serial = serial;
    if (!w)
        return;
    uint32_t typed;
    if (g.h.key && typed_key(key, &typed))
        g.h.key(w, typed, down, repeat);
    if (!window_alive(w) || g.keyboard_focus != w)
        return;
    if (down) {
        key_text(key, repeat);
        if (!repeat && g.keymap && g.repeat_rate > 0 && xkb_keymap_key_repeats(g.keymap, key + 8)) {
            g.repeat_key = key;
            g.repeat_due = now_ms() + (uint64_t)g.repeat_delay;
        }
    } else if (key == g.repeat_key) {
        g.repeat_due = 0;
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed,
                               uint32_t latched, uint32_t locked, uint32_t group)
{
    if (!g.state)
        return;
    xkb_layout_index_t before = xkb_state_serialize_layout(g.state, XKB_STATE_LAYOUT_EFFECTIVE);
    xkb_state_update_mask(g.state, depressed, latched, locked, 0, 0, group);
    if (g.played)
        return;
    g.typed_layout = xkb_state_serialize_layout(g.state, XKB_STATE_LAYOUT_EFFECTIVE);
    /* another layout of the keymap types other characters. */
    if (before != g.typed_layout && g.h.keymap)
        g.h.keymap();
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay)
{
    g.repeat_rate = rate;
    g.repeat_delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {keyboard_keymap, keyboard_enter,     keyboard_leave,
                                                              keyboard_key,    keyboard_modifiers, keyboard_repeat_info};

static void run_repeat(void)
{
    if (!g.repeat_due || g.repeat_due > now_ms())
        return;
    MimeWlWindow *w = g.keyboard_focus;
    if (!w || g.repeat_rate <= 0) {
        g.repeat_due = 0;
        return;
    }
    g.repeat_due = now_ms() + (uint64_t)MIME_MAX(1000 / g.repeat_rate, 1);
    uint32_t key = g.repeat_key, typed;
    if (g.h.key && typed_key(key, &typed))
        g.h.key(w, typed, true, true);
    if (window_alive(w) && g.keyboard_focus == w)
        key_text(key, true);
}

MimeWlWindow *mime_wl_keyboard_focus(void)
{
    return g.keyboard_focus;
}

uint32_t mime_wl_key_char(uint32_t key, int level)
{
    struct xkb_keymap *keymap = g.typed_keymap;
    if (!keymap || !g.state)
        return 0;
    xkb_keycode_t code = key + 8;
    xkb_layout_index_t layout = g.played ? g.typed_layout : xkb_state_key_get_layout(g.state, code);
    if (layout == XKB_LAYOUT_INVALID || (xkb_level_index_t)level >= xkb_keymap_num_levels_for_key(keymap, code, layout))
        return 0;
    const xkb_keysym_t *syms;
    if (xkb_keymap_key_get_syms_by_level(keymap, code, layout, (xkb_level_index_t)level, &syms) < 1)
        return 0;
    return xkb_keysym_to_utf32(syms[0]);
}

uint32_t mime_wl_serial(void)
{
    return g.serial;
}

uint32_t mime_wl_selection_serial(void)
{
    return g.serial ? g.serial : g.focus_serial;
}

/* ------------------------------------------------------------------ seat and globals. */

/* what hangs from the pointer, once it and the global each comes from are both there. */
static void pointer_extras(void)
{
    if (!g.pointer)
        return;
    if (g.relative_manager && !g.relative) {
        g.relative = zwp_relative_pointer_manager_v1_get_relative_pointer(g.relative_manager, g.pointer);
        zwp_relative_pointer_v1_add_listener(g.relative, &relative_listener, nullptr);
    }
    if (g.shapes && !g.shape_device)
        g.shape_device = wp_cursor_shape_manager_v1_get_pointer(g.shapes, g.pointer);
}

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !g.pointer) {
        g.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(g.pointer, &pointer_listener, nullptr);
        pointer_extras();
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !g.keyboard) {
        g.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g.keyboard, &keyboard_listener, nullptr);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {seat_capabilities, seat_name};

static void base_ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
    xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener base_listener = {base_ping};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                            uint32_t version)
{
    if (!strcmp(interface, wl_compositor_interface.name)) {
        g.compositor = wl_registry_bind(registry, name, &wl_compositor_interface, MIME_MIN(version, 6u));
    } else if (!strcmp(interface, wl_subcompositor_interface.name)) {
        g.subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        g.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, wl_seat_interface.name) && !g.seat) {
        g.seat_version = MIME_MIN(version, 9u);
        g.seat = wl_registry_bind(registry, name, &wl_seat_interface, g.seat_version);
        /* the seat tells its devices once, as soon as it is bound. */
        wl_seat_add_listener(g.seat, &seat_listener, nullptr);
    } else if (!strcmp(interface, xdg_wm_base_interface.name)) {
        g.base = wl_registry_bind(registry, name, &xdg_wm_base_interface, MIME_MIN(version, 6u));
        xdg_wm_base_add_listener(g.base, &base_listener, nullptr);
    } else if (!strcmp(interface, wp_viewporter_interface.name)) {
        g.viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    } else if (!strcmp(interface, wp_fractional_scale_manager_v1_interface.name)) {
        g.fractional = wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
    } else if (!strcmp(interface, zwp_text_input_manager_v3_interface.name)) {
        g.text_manager = wl_registry_bind(registry, name, &zwp_text_input_manager_v3_interface, 1);
    } else if (!strcmp(interface, zwp_pointer_constraints_v1_interface.name)) {
        g.constraints = wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1);
    } else if (!strcmp(interface, zwp_relative_pointer_manager_v1_interface.name)) {
        g.relative_manager = wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, 1);
    } else if (!strcmp(interface, wp_cursor_shape_manager_v1_interface.name)) {
        g.shapes = wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, 1);
    } else if (!strcmp(interface, xdg_toplevel_icon_manager_v1_interface.name)) {
        g.icons = wl_registry_bind(registry, name, &xdg_toplevel_icon_manager_v1_interface, 1);
    } else if (!strcmp(interface, zxdg_output_manager_v1_interface.name)) {
        g.xdg_outputs = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, MIME_MIN(version, 3u));
        for (Output *o = g.outputs; o; o = o->next)
            output_logical(o);
    } else if (!strcmp(interface, order_interface.name)) {
        g.order = wl_registry_bind(registry, name, &order_interface, 1);
        wl_proxy_add_listener(g.order, (void (**)(void))order_listener, nullptr);
    } else if (!strcmp(interface, wl_output_interface.name)) {
        Output *o = calloc(1, sizeof(*o));
        if (!o)
            return;
        o->id = name;
        o->scale = 1;
        o->output = wl_registry_bind(registry, name, &wl_output_interface, MIME_MIN(version, 4u));
        wl_output_add_listener(o->output, &output_listener, o);
        Output **last = &g.outputs;
        while (*last)
            last = &(*last)->next;
        *last = o;
        output_logical(o);
    }
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    for (Output **at = &g.outputs; *at; at = &(*at)->next) {
        Output *o = *at;
        if (o->id != name)
            continue;
        *at = o->next;
        for (MimeWlWindow *w = g.windows; w; w = w->next) {
            uint32_t kept = 0;
            for (uint32_t i = 0; i < w->entered_count; i++)
                if (w->entered[i] != name)
                    w->entered[kept++] = w->entered[i];
            w->entered_count = kept;
        }
        if (o->xdg)
            zxdg_output_v1_destroy(o->xdg);
        wl_output_destroy(o->output);
        free(o);
        outputs_changed(true);
        return;
    }
}

static const struct wl_registry_listener registry_listener = {registry_global, registry_remove};

struct wl_display *mime_wl_display(void)
{
    return g.display;
}

struct wl_compositor *mime_wl_compositor(void)
{
    return g.compositor;
}

struct wl_subcompositor *mime_wl_subcompositor(void)
{
    return g.subcompositor;
}

struct wl_shm *mime_wl_shm(void)
{
    return g.shm;
}

struct wl_seat *mime_wl_seat(void)
{
    return g.seat;
}

struct wp_viewport *mime_wl_viewport(struct wl_surface *surface)
{
    return g.viewporter ? wp_viewporter_get_viewport(g.viewporter, surface) : nullptr;
}

void mime_wl_viewport_size(struct wp_viewport *viewport, int width, int height)
{
    wp_viewport_set_destination(viewport, width, height);
}

/* shows the whole buffer at a size. */
void mime_wl_viewport_whole(struct wp_viewport *viewport, int width, int height)
{
    wp_viewport_set_source(viewport, wl_fixed_from_int(-1), wl_fixed_from_int(-1), wl_fixed_from_int(-1),
                           wl_fixed_from_int(-1));
    wp_viewport_set_destination(viewport, width, height);
}

/* shows the top left part of the buffer, of the given size in its pixels, at a size. */
void mime_wl_viewport_part(struct wp_viewport *viewport, double part_width, double part_height, int width, int height)
{
    wp_viewport_set_source(viewport, 0, 0, wl_fixed_from_double(part_width), wl_fixed_from_double(part_height));
    wp_viewport_set_destination(viewport, width, height);
}

void mime_wl_viewport_destroy(struct wp_viewport *viewport)
{
    wp_viewport_destroy(viewport);
}

static void probe_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height,
                            struct wl_array *states)
{
}

static void probe_close(void *data, struct xdg_toplevel *toplevel)
{
}

static const struct xdg_toplevel_listener probe_listener = {probe_configure, probe_close, toplevel_bounds,
                                                            toplevel_capabilities};

/*
 * what the compositor can do with a window is told to a window, and the guest draws its title bars before it has
 * one. a window that never gets a buffer is never shown, and is told all the same.
 */
static void probe_capabilities(void)
{
    if (xdg_wm_base_get_version(g.base) < XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
        return;
    struct wl_surface *surface = wl_compositor_create_surface(g.compositor);
    struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(g.base, surface);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg);
    xdg_toplevel_add_listener(toplevel, &probe_listener, nullptr);
    wl_surface_commit(surface);
    wl_display_roundtrip(g.display);
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg);
    wl_surface_destroy(surface);
    wl_display_roundtrip(g.display);
}

bool mime_wl_init(const MimeWlHandlers *handlers)
{
    g.h = *handlers;
    g.display = wl_display_connect(nullptr);
    if (!g.display)
        return false;
    g.wake = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    g.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &registry_listener, nullptr);
    /* the globals, then what each of them tells, the logical sizes of the outputs last. */
    for (int i = 0; i < 3; i++)
        wl_display_roundtrip(g.display);
    if (!g.compositor || !g.shm || !g.base || g.wake < 0 || !g.xkb) {
        mime_warn("the compositor lacks %s", !g.compositor ? "wl_compositor" : !g.shm ? "wl_shm" : !g.base ? "xdg_wm_base" : "nothing, the host an event descriptor or a key context");
        wl_display_disconnect(g.display);
        g.display = nullptr;
        return false;
    }
    if (g.seat && g.text_manager) {
        g.text_input = zwp_text_input_manager_v3_get_text_input(g.text_manager, g.seat);
        zwp_text_input_v3_add_listener(g.text_input, &text_listener, nullptr);
    }
    const char *locale = getenv("LC_ALL");
    if (!locale || !locale[0])
        locale = getenv("LC_CTYPE");
    if (!locale || !locale[0])
        locale = getenv("LANG");
    if (!locale || !locale[0])
        locale = "C";
    g.compose_table = xkb_compose_table_new_from_locale(g.xkb, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (g.compose_table)
        g.compose = xkb_compose_state_new(g.compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
    /* the globals may have come after the pointer. */
    pointer_extras();
    wl_display_roundtrip(g.display);
    probe_capabilities();
    g.synced = true;
    return true;
}

bool mime_wl_can_minimize(void)
{
    return !g.no_minimize;
}

void mime_wl_quit(void)
{
    if (!g.display)
        return;
    while (g.windows)
        mime_wl_window_destroy(g.windows);
    wl_display_flush(g.display);
    wl_display_disconnect(g.display);
    g.display = nullptr;
}

bool mime_wl_wait(int timeout_ms)
{
    struct wl_display *d = g.display;
    run_calls();
    run_repeat();
    while (wl_display_prepare_read(d) != 0)
        if (wl_display_dispatch_pending(d) < 0)
            return false;
    while (wl_display_flush(d) < 0) {
        if (errno != EAGAIN) {
            /* the compositor closed the connection, what it said last is still to read. */
            break;
        }
        struct pollfd out = {wl_display_get_fd(d), POLLOUT, 0};
        if (poll(&out, 1, 1000) <= 0)
            break;
    }
    uint64_t now = now_ms(), due = 0;
    pthread_mutex_lock(&g.lock);
    bool ready = g.calls != nullptr;
    if (g.timers)
        due = g.timers->due;
    pthread_mutex_unlock(&g.lock);
    if (g.repeat_due && (!due || g.repeat_due < due))
        due = g.repeat_due;
    int wait = timeout_ms;
    if (due) {
        int until = due > now ? (int)MIME_MIN(due - now, 60000u) : 0;
        wait = wait < 0 ? until : MIME_MIN(wait, until);
    }
    if (ready)
        wait = 0;
    struct pollfd fds[2] = {{wl_display_get_fd(d), POLLIN, 0}, {g.wake, POLLIN, 0}};
    int n = poll(fds, 2, wait);
    if (n > 0 && (fds[0].revents & POLLIN)) {
        if (wl_display_read_events(d) < 0)
            return false;
    } else {
        wl_display_cancel_read(d);
        if (n > 0 && (fds[0].revents & (POLLERR | POLLHUP)))
            return false;
    }
    if (wl_display_dispatch_pending(d) < 0)
        return false;
    if (n > 0 && (fds[1].revents & POLLIN)) {
        uint64_t count;
        if (read(g.wake, &count, sizeof(count)) < 0) {
        }
    }
    run_calls();
    run_repeat();
    return true;
}
