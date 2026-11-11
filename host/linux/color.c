/*
 * what the compositor tells of each output's colors through wp_color_manager_v1, which SDL keeps to itself. requests
 * and events are described directly from the protocol specification, as for the activation in activation.c. a thread
 * of its own reads the events, as the main thread sleeps in SDL and nothing there wakes for another queue.
 */
#include "ui_internal.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

static const struct wl_interface output_interface, description_interface, info_interface;
static const struct wl_interface *no_types[8] = {};
static const struct wl_interface *types_get_output[] = {&output_interface, &wl_output_interface};
static const struct wl_message manager_requests[] = {
    {"destroy", "", no_types},
    {"get_output", "no", types_get_output},
};
static const struct wl_message manager_events[] = {
    {"supported_intent", "u", no_types},
    {"supported_feature", "u", no_types},
    {"supported_tf_named", "u", no_types},
    {"supported_primaries_named", "u", no_types},
    {"done", "", no_types},
};
static const struct wl_interface manager_interface = {"wp_color_manager_v1", 1, 2, manager_requests, 5, manager_events};
static const struct wl_interface *types_description[] = {&description_interface};
static const struct wl_message output_requests[] = {
    {"destroy", "", no_types},
    {"get_image_description", "n", types_description},
};
static const struct wl_message output_events[] = {{"image_description_changed", "", no_types}};
static const struct wl_interface output_interface = {"wp_color_management_output_v1", 1, 2, output_requests, 1,
                                                     output_events};
static const struct wl_interface *types_info[] = {&info_interface};
static const struct wl_message description_requests[] = {
    {"destroy", "", no_types},
    {"get_information", "n", types_info},
};
static const struct wl_message description_events[] = {
    {"failed", "us", no_types},
    {"ready", "u", no_types},
};
static const struct wl_interface description_interface = {"wp_image_description_v1", 1, 2, description_requests, 2,
                                                          description_events};
static const struct wl_message info_events[] = {
    {"done", "", no_types},
    {"icc_file", "hu", no_types},
    {"primaries", "iiiiiiii", no_types},
    {"primaries_named", "u", no_types},
    {"tf_power", "u", no_types},
    {"tf_named", "u", no_types},
    {"luminances", "uuu", no_types},
    {"target_primaries", "iiiiiiii", no_types},
    {"target_luminance", "uu", no_types},
    {"target_max_cll", "u", no_types},
    {"target_max_fall", "u", no_types},
};
static const struct wl_interface info_interface = {"wp_image_description_info_v1", 1, 0, nullptr, 11, info_events};

enum { TF_ST2084_PQ = 11, TF_HLG = 13 };
enum { PRIMARIES_SRGB = 1, PRIMARIES_BT2020 = 6, PRIMARIES_DCI_P3 = 8, PRIMARIES_DISPLAY_P3 = 9, PRIMARIES_ADOBE = 10 };

/* the luminance a PQ or scRGB surface's reference white is told at, which the compositor shows as its sdr white. */
#define REFERENCE_NITS 203.0

typedef struct Output {
    struct wl_output *output; /* SDL's, what tells the outputs apart. */
    struct wl_proxy *proxy;
    uint32_t serial; /* of the last question asked. */
    bool answered;
    bool hdr;
    MimeMonitorColor color;
} Output;

typedef struct Query {
    Output *o;
    uint32_t serial;
    struct wl_proxy *description, *info;
    int32_t primaries[8], target_primaries[8];
    uint32_t named, tf;
    uint32_t min, max, reference;
    uint32_t target_min, target_max, max_fall;
    bool has_primaries, has_target_primaries, has_luminances, has_target_luminance;
} Query;

static struct wl_display *g_display;
static struct wl_event_queue *g_queue;
static struct wl_proxy *g_manager;
static bool g_tried;
static pthread_t g_thread;
static bool g_running;
static int g_wake = -1;
static void (*g_changed)(void);
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static Output g_outputs[MIME_MAX_MONITORS * 2];

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                            uint32_t version)
{
    if (!strcmp(interface, manager_interface.name))
        g_manager = wl_registry_bind(registry, name, &manager_interface, 1);
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {registry_global, registry_remove};

static const int32_t *named_primaries(uint32_t named)
{
    static const int32_t srgb[8] = {640000, 330000, 300000, 600000, 150000, 60000, 312700, 329000};
    static const int32_t bt2020[8] = {708000, 292000, 170000, 797000, 131000, 46000, 312700, 329000};
    static const int32_t dci_p3[8] = {680000, 320000, 265000, 690000, 150000, 60000, 314000, 351000};
    static const int32_t display_p3[8] = {680000, 320000, 265000, 690000, 150000, 60000, 312700, 329000};
    static const int32_t adobe[8] = {640000, 330000, 210000, 710000, 150000, 60000, 312700, 329000};
    switch (named) {
    case PRIMARIES_SRGB:
        return srgb;
    case PRIMARIES_BT2020:
        return bt2020;
    case PRIMARIES_DCI_P3:
        return dci_p3;
    case PRIMARIES_DISPLAY_P3:
        return display_p3;
    case PRIMARIES_ADOBE:
        return adobe;
    default:
        return nullptr;
    }
}

static uint32_t ten_thousandths(double nits)
{
    return nits <= 0 ? 0 : nits > 400000.0 ? 4000000000u : (uint32_t)(nits * 10000.0 + 0.5);
}

/*
 * an output is in hdr when what it can show reaches above its reference white. programs draw in the nits of PQ, whose
 * reference white the compositor shows at the output's own, so the output's range is told on that scale.
 */
static void answer(const Query *q, bool *hdr, MimeMonitorColor *color)
{
    memset(color, 0, sizeof(*color));
    double min = 0.2, max = 80.0, reference = 80.0;
    if (q->tf == TF_ST2084_PQ)
        min = 0.005, max = 10000.0, reference = REFERENCE_NITS;
    else if (q->tf == TF_HLG)
        min = 0.005, max = 1000.0, reference = REFERENCE_NITS;
    if (q->has_luminances && q->reference) {
        min = q->min / 10000.0;
        max = q->max;
        reference = q->reference;
    }
    const int32_t *primaries = q->has_target_primaries ? q->target_primaries
                               : q->has_primaries      ? q->primaries
                                                       : named_primaries(q->named);
    if (primaries) {
        uint32_t *out = &color->red_x;
        for (int i = 0; i < 8; i++)
            out[i] = primaries[i] > 0 ? (uint32_t)primaries[i] : 0;
    }
    /* the luminances are the range the encoding spans, which KWin puts above the white of an output it shows in sdr. */
    double peak = max, floor = min;
    if (q->has_target_luminance && q->target_max) {
        peak = q->target_max;
        floor = q->target_min / 10000.0;
    }
    *hdr = peak / reference > 1.05;
    if (!*hdr)
        return;
    double scale = REFERENCE_NITS / reference;
    double frame = q->max_fall && q->max_fall < peak ? q->max_fall : peak;
    color->min_nits = ten_thousandths(floor * scale);
    color->max_nits = ten_thousandths(peak * scale);
    color->max_frame_nits = ten_thousandths(frame * scale);
    color->white_nits = ten_thousandths(REFERENCE_NITS);
}

static void finish(Query *q, bool ok)
{
    bool changed = false;
    pthread_mutex_lock(&g_lock);
    Output *o = q->o;
    if (o->proxy && o->serial == q->serial) {
        bool hdr = false;
        MimeMonitorColor color = {};
        if (ok)
            answer(q, &hdr, &color);
        changed = o->answered && (hdr != o->hdr || memcmp(&color, &o->color, sizeof(color)));
        if (!o->answered || changed)
            mime_info("color: output %p %s, %u to %u nits, %u a frame, primaries %u %u, %u %u, %u %u",
                      (void *)o->output, hdr ? "hdr" : "sdr", color.min_nits / 10000, color.max_nits / 10000,
                      color.max_frame_nits / 10000, color.red_x, color.red_y, color.green_x, color.green_y,
                      color.blue_x, color.blue_y);
        o->hdr = hdr;
        o->color = color;
        o->answered = true;
        pthread_cond_broadcast(&g_cond);
    }
    pthread_mutex_unlock(&g_lock);
    if (q->info)
        wl_proxy_destroy(q->info);
    wl_proxy_marshal_flags(q->description, 0, nullptr, wl_proxy_get_version(q->description), WL_MARSHAL_FLAG_DESTROY);
    free(q);
    if (changed && g_changed)
        g_changed();
}

static void info_done(void *data, struct wl_proxy *proxy)
{
    finish(data, true);
}

static void info_icc_file(void *data, struct wl_proxy *proxy, int32_t fd, uint32_t size)
{
    close(fd);
}

static void info_primaries(void *data, struct wl_proxy *proxy, int32_t rx, int32_t ry, int32_t gx, int32_t gy,
                           int32_t bx, int32_t by, int32_t wx, int32_t wy)
{
    Query *q = data;
    int32_t values[8] = {rx, ry, gx, gy, bx, by, wx, wy};
    memcpy(q->primaries, values, sizeof(values));
    q->has_primaries = true;
}

static void info_primaries_named(void *data, struct wl_proxy *proxy, uint32_t named)
{
    ((Query *)data)->named = named;
}

static void info_tf_power(void *data, struct wl_proxy *proxy, uint32_t exponent)
{
}

static void info_tf_named(void *data, struct wl_proxy *proxy, uint32_t tf)
{
    ((Query *)data)->tf = tf;
}

static void info_luminances(void *data, struct wl_proxy *proxy, uint32_t min, uint32_t max, uint32_t reference)
{
    Query *q = data;
    q->min = min;
    q->max = max;
    q->reference = reference;
    q->has_luminances = true;
}

static void info_target_primaries(void *data, struct wl_proxy *proxy, int32_t rx, int32_t ry, int32_t gx, int32_t gy,
                                  int32_t bx, int32_t by, int32_t wx, int32_t wy)
{
    Query *q = data;
    int32_t values[8] = {rx, ry, gx, gy, bx, by, wx, wy};
    memcpy(q->target_primaries, values, sizeof(values));
    q->has_target_primaries = true;
}

static void info_target_luminance(void *data, struct wl_proxy *proxy, uint32_t min, uint32_t max)
{
    Query *q = data;
    q->target_min = min;
    q->target_max = max;
    q->has_target_luminance = true;
}

static void info_target_max_cll(void *data, struct wl_proxy *proxy, uint32_t max_cll)
{
}

static void info_target_max_fall(void *data, struct wl_proxy *proxy, uint32_t max_fall)
{
    ((Query *)data)->max_fall = max_fall;
}

static const void *info_listener[11] = {
    (void *)info_done,          (void *)info_icc_file,         (void *)info_primaries,
    (void *)info_primaries_named, (void *)info_tf_power,       (void *)info_tf_named,
    (void *)info_luminances,    (void *)info_target_primaries, (void *)info_target_luminance,
    (void *)info_target_max_cll, (void *)info_target_max_fall,
};

static void description_failed(void *data, struct wl_proxy *proxy, uint32_t cause, const char *message)
{
    finish(data, false);
}

static void description_ready(void *data, struct wl_proxy *proxy, uint32_t identity)
{
    Query *q = data;
    q->info = wl_proxy_marshal_flags(proxy, 1, &info_interface, wl_proxy_get_version(proxy), 0, nullptr);
    if (!q->info) {
        finish(q, false);
        return;
    }
    wl_proxy_add_listener(q->info, (void (**)(void))info_listener, q);
    wl_display_flush(g_display);
}

static const void *description_listener[2] = {(void *)description_failed, (void *)description_ready};

/* with the lock held. */
static void ask(Output *o)
{
    Query *q = calloc(1, sizeof(*q));
    if (!q)
        return;
    q->o = o;
    q->serial = ++o->serial;
    q->description = wl_proxy_marshal_flags(o->proxy, 1, &description_interface, wl_proxy_get_version(o->proxy), 0,
                                            nullptr);
    if (!q->description) {
        free(q);
        return;
    }
    wl_proxy_add_listener(q->description, (void (**)(void))description_listener, q);
    wl_display_flush(g_display);
}

static void output_changed(void *data, struct wl_proxy *proxy)
{
    Output *o = data;
    pthread_mutex_lock(&g_lock);
    if (o->proxy == proxy)
        ask(o);
    pthread_mutex_unlock(&g_lock);
}

static const void *output_listener[1] = {(void *)output_changed};

static void *reader(void *arg)
{
    int fd = wl_display_get_fd(g_display);
    for (;;) {
        while (wl_display_prepare_read_queue(g_display, g_queue) != 0)
            wl_display_dispatch_queue_pending(g_display, g_queue);
        wl_display_flush(g_display);
        struct pollfd fds[2] = {{fd, POLLIN, 0}, {g_wake, POLLIN, 0}};
        int n = poll(fds, 2, -1);
        if (n < 0 || (fds[1].revents & POLLIN) || !(fds[0].revents & POLLIN)) {
            wl_display_cancel_read(g_display);
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0 || fds[1].revents || (fds[0].revents & (POLLERR | POLLHUP)))
                break;
            continue;
        }
        if (wl_display_read_events(g_display) < 0)
            break;
        wl_display_dispatch_queue_pending(g_display, g_queue);
    }
    return nullptr;
}

static bool bind(struct wl_display *display)
{
    if (g_tried)
        return g_manager && g_display == display;
    g_tried = true;
    g_display = display;
    g_queue = wl_display_create_queue(display);
    struct wl_display *wrapper = wl_proxy_create_wrapper(display);
    wl_proxy_set_queue((struct wl_proxy *)wrapper, g_queue);
    struct wl_registry *registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    wl_registry_add_listener(registry, &registry_listener, nullptr);
    wl_display_roundtrip_queue(display, g_queue);
    if (!g_manager) {
        mime_info("color: the compositor has no color management, every output is sdr");
        return false;
    }
    g_wake = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (g_wake < 0 || pthread_create(&g_thread, nullptr, reader, nullptr)) {
        g_manager = nullptr;
        return false;
    }
    g_running = true;
    return true;
}

void mime_color_watch(void (*changed)(void))
{
    g_changed = changed;
}

bool mime_color_output(struct wl_display *display, struct wl_output *output, bool *hdr, MimeMonitorColor *color)
{
    *hdr = false;
    memset(color, 0, sizeof(*color));
    if (!display || !output || !bind(display))
        return false;
    pthread_mutex_lock(&g_lock);
    Output *o = nullptr, *spare = nullptr;
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_outputs); i++) {
        if (g_outputs[i].proxy && g_outputs[i].output == output)
            o = &g_outputs[i];
        else if (!g_outputs[i].proxy && !spare)
            spare = &g_outputs[i];
    }
    if (!o && spare) {
        struct wl_proxy *proxy = wl_proxy_marshal_flags(g_manager, 1, &output_interface,
                                                        wl_proxy_get_version(g_manager), 0, nullptr, output);
        if (proxy) {
            o = spare;
            o->output = output;
            o->proxy = proxy;
            o->answered = false;
            wl_proxy_add_listener(proxy, (void (**)(void))output_listener, o);
            ask(o);
        }
    }
    if (o && !o->answered) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += 500000000;
        if (until.tv_nsec >= 1000000000) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000;
        }
        while (!o->answered && pthread_cond_timedwait(&g_cond, &g_lock, &until) == 0) {
        }
    }
    bool known = o && o->answered;
    if (known) {
        *hdr = o->hdr;
        *color = o->color;
    }
    pthread_mutex_unlock(&g_lock);
    return known;
}

/* the outputs came or went: SDL's objects for them are other ones now. */
void mime_color_forget(void)
{
    if (!g_manager)
        return;
    pthread_mutex_lock(&g_lock);
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_outputs); i++) {
        Output *o = &g_outputs[i];
        if (!o->proxy)
            continue;
        wl_proxy_marshal_flags(o->proxy, 0, nullptr, wl_proxy_get_version(o->proxy), WL_MARSHAL_FLAG_DESTROY);
        o->proxy = nullptr;
        o->output = nullptr;
        o->serial++;
    }
    pthread_mutex_unlock(&g_lock);
}

/* before the display closes. */
void mime_color_stop(void)
{
    if (!g_running)
        return;
    g_changed = nullptr;
    uint64_t one = 1;
    if (write(g_wake, &one, sizeof(one)) < 0) {
    }
    pthread_join(g_thread, nullptr);
    g_running = false;
    g_manager = nullptr;
}
