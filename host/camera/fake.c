/*
 * the test camera: cameras that exist only because MIME_FAKE_CAMERA says so, drawing a known picture so that tests can
 * check what reaches the guest. "1" makes one camera for as long as the machine runs. any other value is the path of
 * a file the camera is plugged in while it exists: each line of it names a camera, an empty file makes a camera
 * called "Mime Fake Camera". creating and removing the file is plugging and unplugging.
 *
 * the picture is eight colour bars under a strip that counts frames: 16 blocks of 8 by 8 pixels, a lit one (luma 235)
 * for each bit of the frame number from 0, an unlit one (luma 16) for the others. the bars are white, yellow, cyan,
 * green, magenta, red, blue and black at the usual studio levels. the brightness control moves the luma of the bars.
 */
#include "backend.h"
#include "convert.h"
#include "jpeg.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_FAKE 4

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static char g_path[512];
static bool g_always, g_enabled;

static void read_env(void)
{
    const char *v = getenv("MIME_FAKE_CAMERA");
    if (!v || !*v || !strcmp(v, "0"))
        return;
    g_enabled = true;
    if (!strcmp(v, "1"))
        g_always = true;
    else
        snprintf(g_path, sizeof(g_path), "%s", v);
}

static const MimeCameraFormat g_formats[] = {
    {MIME_CAMERA_FOURCC_MJPG, 1280, 720, 30, 1}, {MIME_CAMERA_FOURCC_MJPG, 640, 480, 30, 1},
    {MIME_CAMERA_FOURCC_MJPG, 320, 240, 30, 1},  {MIME_CAMERA_FOURCC_YUY2, 640, 480, 30, 1},
    {MIME_CAMERA_FOURCC_YUY2, 320, 240, 30, 1},  {MIME_CAMERA_FOURCC_YUY2, 160, 120, 30, 1},
    {MIME_CAMERA_FOURCC_NV12, 640, 480, 30, 1},  {MIME_CAMERA_FOURCC_NV12, 320, 240, 15, 1},
};

static int poll_ms(void)
{
    pthread_once(&g_once, read_env);
    return g_path[0] ? 100 : 0;
}

static void add_camera(CameraList *list, int index, const char *name)
{
    CameraSource *s = camera_list_add(list);
    if (!s)
        return;
    snprintf(s->info.name, sizeof(s->info.name), "%s", name);
    /* a camera is the same one for as long as its name is in the file, wherever its line moves to. */
    int same = 0;
    for (uint32_t i = 0; i + 1 < list->count; i++)
        if (!strcmp(list->source[i].info.name, name))
            same++;
    snprintf(s->info.location, sizeof(s->info.location), "fake-%s-%d", name, same);
    snprintf(s->key, sizeof(s->key), "fake:%s:%d", name, same);
    snprintf(s->path, sizeof(s->path), "%d", index);
    s->info.flags = MIME_CAMERA_FLAG_FAKE | MIME_CAMERA_FLAG_USB;
    s->info.vendor = 0x1D6B;
    s->info.product = (uint16_t)(0xC0DE + index);
    s->format_count = MIME_ARRAY_COUNT(g_formats);
    s->formats = malloc(sizeof(g_formats));
    if (s->formats)
        memcpy(s->formats, g_formats, sizeof(g_formats));
    else
        s->format_count = 0;
}

static void scan(CameraList *list)
{
    pthread_once(&g_once, read_env);
    if (!g_enabled)
        return;
    if (g_always) {
        add_camera(list, 0, "Mime Fake Camera");
        return;
    }
    FILE *f = fopen(g_path, "r");
    if (!f)
        return;
    char line[128];
    int n = 0;
    while (n < MAX_FAKE && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0])
            add_camera(list, n++, line);
    }
    if (!n)
        add_camera(list, 0, "Mime Fake Camera");
    fclose(f);
}

/* the controls: brightness in the video processing amplifier and a zoom that does nothing. */
static int g_brightness[MAX_FAKE] = {128, 128, 128, 128};
static int g_zoom[MAX_FAKE] = {100, 100, 100, 100};

static void control(const CameraSource *source, MimeCameraControl *c)
{
    int index = atoi(source->path) % MAX_FAKE;
    int *value = nullptr;
    int min, max, step, def;
    if (c->set == MIME_CAMERA_CONTROL_PROCAMP && c->property == 0) {
        value = &g_brightness[index];
        min = 0, max = 255, step = 1, def = 128;
    } else if (c->set == MIME_CAMERA_CONTROL_CAMERA && c->property == 3) {
        value = &g_zoom[index];
        min = 100, max = 400, step = 10, def = 100;
    } else {
        c->status = MIME_HC_STATUS_NOT_FOUND;
        return;
    }
    c->status = MIME_HC_STATUS_OK;
    if (c->operation == MIME_CAMERA_CONTROL_RANGE) {
        c->min = min, c->max = max, c->step = step, c->default_value = def;
        c->flags = MIME_CAMERA_CONTROL_FLAG_MANUAL;
        c->value = __atomic_load_n(value, __ATOMIC_RELAXED);
    } else if (c->operation == MIME_CAMERA_CONTROL_GET) {
        c->value = __atomic_load_n(value, __ATOMIC_RELAXED);
        c->flags = MIME_CAMERA_CONTROL_FLAG_MANUAL;
    } else if (c->operation == MIME_CAMERA_CONTROL_SET) {
        if (c->value < min || c->value > max)
            c->status = MIME_HC_STATUS_INVALID;
        else
            __atomic_store_n(value, c->value, __ATOMIC_RELAXED);
    } else {
        c->status = MIME_HC_STATUS_INVALID;
    }
}

/* ------------------------------------------------------------------ the picture */

static const uint8_t g_bars[8][3] = {
    {235, 128, 128}, {210, 16, 146}, {170, 166, 16}, {145, 54, 34}, {106, 202, 222}, {81, 90, 240}, {41, 240, 110},
    {16, 128, 128},
};

static void draw(uint8_t *y, uint8_t *u, uint8_t *v, int w, int h, uint32_t n, int brightness)
{
    int cw = (w + 1) / 2;
    int shift = (brightness - 128) / 2;
    int bar = w / 8 ? w / 8 : 1;
    for (int r = 0; r < h; r++) {
        for (int x = 0; x < w; x++) {
            int b = x / bar > 7 ? 7 : x / bar;
            int luma = g_bars[b][0] + (b == 7 ? 0 : shift);
            y[(size_t)r * w + x] = (uint8_t)(luma < 16 ? 16 : luma > 235 ? 235 : luma);
        }
        if (!(r & 1))
            for (int x = 0; x < cw; x++) {
                int b = (2 * x) / bar > 7 ? 7 : (2 * x) / bar;
                u[(size_t)(r / 2) * cw + x] = g_bars[b][1];
                v[(size_t)(r / 2) * cw + x] = g_bars[b][2];
            }
    }
    /* the counter strip over the first eight rows. */
    for (int r = 0; r < 8 && r < h; r++) {
        for (int x = 0; x < w; x++) {
            int bit = x / 8;
            y[(size_t)r * w + x] = bit < 16 ? ((n >> bit) & 1 ? 235 : 16) : 16;
        }
        if (!(r & 1))
            for (int x = 0; x < cw; x++) {
                u[(size_t)(r / 2) * cw + x] = 128;
                v[(size_t)(r / 2) * cw + x] = 128;
            }
    }
}

typedef struct FakeStream {
    pthread_t thread;
    bool stop;
    int index;
    MimeCameraFormat format;
    CameraDeliver deliver;
    void *ctx;
    CameraConverter converter;
} FakeStream;

static void *produce(void *arg)
{
    FakeStream *fs = arg;
    mime_set_thread_name("mime-fakecam");
    int w = (int)fs->format.width, h = (int)fs->format.height, cw = (w + 1) / 2, ch = (h + 1) / 2;
    size_t i420_bytes = mime_camera_frame_bytes(MIME_CAMERA_FOURCC_I420, (uint32_t)w, (uint32_t)h);
    uint8_t *i420 = malloc(i420_bytes);
    size_t cap = (size_t)w * h * 2 + 4096;
    uint8_t *frame = malloc(cap);
    uint8_t *full = malloc(i420_bytes);
    if (!i420 || !frame || !full)
        goto out;
    uint8_t *y = i420, *u = y + (size_t)w * h, *v = u + (size_t)cw * ch;
    uint64_t interval = 1000000000ull * fs->format.fps_den / fs->format.fps_num;
    uint64_t next = camera_now_ns() + interval;
    for (uint32_t n = 0;; n++) {
        struct timespec at = {.tv_sec = (time_t)(next / 1000000000ull), .tv_nsec = (long)(next % 1000000000ull)};
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &at, nullptr) == EINTR)
            ;
        if (__atomic_load_n(&fs->stop, __ATOMIC_ACQUIRE))
            break;
        draw(y, u, v, w, h, n, __atomic_load_n(&g_brightness[fs->index], __ATOMIC_RELAXED));
        size_t length = 0;
        if (fs->format.fourcc == MIME_CAMERA_FOURCC_MJPG) {
            /* the encoder wants full range samples. */
            for (size_t i = 0; i < (size_t)w * h; i++)
                full[i] = (uint8_t)(((int)y[i] - 16) * 255 / 219);
            for (size_t i = 0; i < (size_t)cw * ch * 2; i++) {
                int c = 128 + ((int)u[i] - 128) * 255 / 224;
                full[(size_t)w * h + i] = (uint8_t)(c < 0 ? 0 : c > 255 ? 255 : c);
            }
            length = mime_jpeg_encode(full, w, full + (size_t)w * h, full + (size_t)w * h + (size_t)cw * ch, cw, w, h,
                                      90, frame, cap);
        } else if (camera_convert(&fs->converter, MIME_CAMERA_FOURCC_I420, i420, i420_bytes, (uint32_t)w, (uint32_t)h,
                                  fs->format.fourcc, frame) == CAMERA_CONVERT_OK) {
            length = mime_camera_frame_bytes(fs->format.fourcc, (uint32_t)w, (uint32_t)h);
        }
        if (length)
            fs->deliver(fs->ctx, frame, length, next, 0);
        next += interval;
        uint64_t now = camera_now_ns();
        if (now > next + 5 * interval)
            next = now + interval;
    }
out:
    free(i420);
    free(frame);
    free(full);
    return nullptr;
}

static void *start(const CameraSource *source, const MimeCameraFormat *format, CameraDeliver deliver,
                   CameraFailed failed, void *ctx, uint64_t *error)
{
    FakeStream *fs = calloc(1, sizeof(*fs));
    if (!fs) {
        *error = MIME_HC_STATUS_NO_MEMORY;
        return nullptr;
    }
    fs->index = atoi(source->path) % MAX_FAKE;
    fs->format = *format;
    fs->deliver = deliver;
    fs->ctx = ctx;
    if (pthread_create(&fs->thread, nullptr, produce, fs) != 0) {
        free(fs);
        *error = MIME_HC_STATUS_NO_MEMORY;
        return nullptr;
    }
    return fs;
}

static void stop(void *stream)
{
    FakeStream *fs = stream;
    __atomic_store_n(&fs->stop, true, __ATOMIC_RELEASE);
    pthread_join(fs->thread, nullptr);
    camera_converter_free(&fs->converter);
    free(fs);
}

const CameraBackend camera_backend_fake = {
    .name = "fake",
    .scan = scan,
    .watch_fd = nullptr,
    .poll_ms = poll_ms,
    .start = start,
    .stop = stop,
    .control = control,
};
