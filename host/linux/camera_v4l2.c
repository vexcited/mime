/*
 * the host's cameras through video4linux2: the device nodes in /dev that capture video, their formats, sizes and
 * rates, and a stream that reads frames from memory mapped buffers. a node is found again after it is unplugged by
 * the place it is plugged in, which a udev rule or a rename of the node does not change.
 */
#include "../camera/backend.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/videodev2.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUFFERS 4

static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do
        r = ioctl(fd, request, arg);
    while (r < 0 && errno == EINTR);
    return r;
}

/* ------------------------------------------------------------------ enumeration */

static uint32_t fourcc_of(uint32_t pixel_format)
{
    switch (pixel_format) {
    case V4L2_PIX_FMT_MJPEG:
    case V4L2_PIX_FMT_JPEG:
        return MIME_CAMERA_FOURCC_MJPG;
    case V4L2_PIX_FMT_YUYV:
        return MIME_CAMERA_FOURCC_YUY2;
    case V4L2_PIX_FMT_UYVY:
        return MIME_CAMERA_FOURCC_UYVY;
    case V4L2_PIX_FMT_NV12:
        return MIME_CAMERA_FOURCC_NV12;
    case V4L2_PIX_FMT_YUV420:
        return MIME_CAMERA_FOURCC_I420;
    case V4L2_PIX_FMT_YVU420:
        return MIME_CAMERA_FOURCC_YV12;
    default:
        return 0;
    }
}

static uint32_t pixel_format_of(uint32_t fourcc, uint32_t native)
{
    switch (fourcc) {
    case MIME_CAMERA_FOURCC_MJPG:
        return native ? native : V4L2_PIX_FMT_MJPEG;
    case MIME_CAMERA_FOURCC_YUY2:
        return V4L2_PIX_FMT_YUYV;
    case MIME_CAMERA_FOURCC_UYVY:
        return V4L2_PIX_FMT_UYVY;
    case MIME_CAMERA_FOURCC_NV12:
        return V4L2_PIX_FMT_NV12;
    case MIME_CAMERA_FOURCC_I420:
        return V4L2_PIX_FMT_YUV420;
    case MIME_CAMERA_FOURCC_YV12:
        return V4L2_PIX_FMT_YVU420;
    default:
        return 0;
    }
}

typedef struct FormatList {
    MimeCameraFormat *format;
    uint32_t count, capacity;
} FormatList;

static void add_format(FormatList *l, uint32_t fourcc, uint32_t w, uint32_t h, uint32_t num, uint32_t den)
{
    if (l->count >= MIME_CAMERA_MAX_FORMATS || !w || !h || !num || !den)
        return;
    for (uint32_t i = 0; i < l->count; i++) {
        const MimeCameraFormat *f = &l->format[i];
        if (f->fourcc == fourcc && f->width == w && f->height == h &&
            (uint64_t)f->fps_num * den == (uint64_t)num * f->fps_den)
            return;
    }
    if (l->count == l->capacity) {
        uint32_t capacity = l->capacity ? l->capacity * 2 : 32;
        MimeCameraFormat *p = realloc(l->format, capacity * sizeof(*p));
        if (!p)
            return;
        l->format = p;
        l->capacity = capacity;
    }
    l->format[l->count++] = (MimeCameraFormat){fourcc, w, h, num, den};
}

static const uint32_t common_sizes[][2] = {
    {160, 120},  {176, 144},  {320, 240},   {352, 288},   {640, 360},   {640, 480},
    {800, 600},  {1024, 768}, {1280, 720},  {1280, 960},  {1600, 1200}, {1920, 1080},
    {2560, 1440}, {3840, 2160},
};
static const uint32_t common_rates[] = {5, 10, 15, 20, 24, 25, 30, 50, 60};

/* the rates of one size: what the driver lists, a few of the usual ones when it gives a range. */
static void enumerate_intervals(int fd, FormatList *l, uint32_t pixel_format, uint32_t fourcc, uint32_t w, uint32_t h)
{
    struct v4l2_frmivalenum iv = {.pixel_format = pixel_format, .width = w, .height = h};
    for (iv.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &iv) == 0 && iv.index < 64; iv.index++) {
        if (iv.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            add_format(l, fourcc, w, h, iv.discrete.denominator, iv.discrete.numerator);
            continue;
        }
        /* a range of intervals, so a range of rates. */
        double fastest = (double)iv.stepwise.min.denominator / iv.stepwise.min.numerator;
        double slowest = (double)iv.stepwise.max.denominator / iv.stepwise.max.numerator;
        for (size_t i = 0; i < MIME_ARRAY_COUNT(common_rates); i++)
            if (common_rates[i] >= slowest - 0.01 && common_rates[i] <= fastest + 0.01)
                add_format(l, fourcc, w, h, common_rates[i], 1);
        break;
    }
    if (iv.index == 0)
        add_format(l, fourcc, w, h, 30, 1);
}

static void enumerate_formats(int fd, FormatList *l)
{
    struct v4l2_fmtdesc fd_desc = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    for (fd_desc.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &fd_desc) == 0 && fd_desc.index < 64; fd_desc.index++) {
        uint32_t fourcc = fourcc_of(fd_desc.pixelformat);
        if (!fourcc)
            continue;
        struct v4l2_frmsizeenum sz = {.pixel_format = fd_desc.pixelformat};
        for (sz.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &sz) == 0 && sz.index < 128; sz.index++) {
            if (sz.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                enumerate_intervals(fd, l, fd_desc.pixelformat, fourcc, sz.discrete.width, sz.discrete.height);
                continue;
            }
            uint32_t step_w = sz.type == V4L2_FRMSIZE_TYPE_CONTINUOUS ? 1 : sz.stepwise.step_width;
            uint32_t step_h = sz.type == V4L2_FRMSIZE_TYPE_CONTINUOUS ? 1 : sz.stepwise.step_height;
            for (size_t i = 0; i < MIME_ARRAY_COUNT(common_sizes); i++) {
                uint32_t w = common_sizes[i][0], h = common_sizes[i][1];
                if (w < sz.stepwise.min_width || w > sz.stepwise.max_width || h < sz.stepwise.min_height ||
                    h > sz.stepwise.max_height || (step_w > 1 && (w - sz.stepwise.min_width) % step_w) ||
                    (step_h > 1 && (h - sz.stepwise.min_height) % step_h))
                    continue;
                enumerate_intervals(fd, l, fd_desc.pixelformat, fourcc, w, h);
            }
            break;
        }
        if (sz.index == 0) {
            /* a driver that cannot say what sizes it has: what it is set to. */
            struct v4l2_format cur = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
            if (xioctl(fd, VIDIOC_G_FMT, &cur) == 0)
                enumerate_intervals(fd, l, fd_desc.pixelformat, fourcc, cur.fmt.pix.width, cur.fmt.pix.height);
        }
    }
}

static bool read_sysfs(const char *path, char *out, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    bool ok = fgets(out, (int)size, f) != nullptr;
    fclose(f);
    if (ok)
        out[strcspn(out, "\r\n")] = 0;
    return ok;
}

static void usb_identity(const char *node, MimeCameraInfo *info, char *product, size_t product_size, char *device_path,
                         size_t device_size)
{
    char link[PATH_MAX], real[PATH_MAX], file[PATH_MAX + 32], text[64];
    snprintf(link, sizeof(link), "/sys/class/video4linux/%s/device", node);
    if (!realpath(link, real))
        return;
    snprintf(device_path, device_size, "%s", real);
    /* the interface is under the usb device, whose id and strings the camera is known by. */
    snprintf(file, sizeof(file), "%s/bInterfaceNumber", real);
    if (read_sysfs(file, text, sizeof(text)))
        info->interface_number = (uint16_t)strtoul(text, nullptr, 16);
    snprintf(file, sizeof(file), "%s/../idVendor", real);
    bool usb = read_sysfs(file, text, sizeof(text));
    if (usb) {
        info->vendor = (uint16_t)strtoul(text, nullptr, 16);
        snprintf(file, sizeof(file), "%s/../idProduct", real);
        if (read_sysfs(file, text, sizeof(text)))
            info->product = (uint16_t)strtoul(text, nullptr, 16);
        info->flags |= MIME_CAMERA_FLAG_USB;
        snprintf(file, sizeof(file), "%s/../product", real);
        read_sysfs(file, product, product_size);
    }
}

static void probe(const char *node, CameraList *list)
{
    char path[64];
    snprintf(path, sizeof(path), "/dev/%s", node);
    int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM)
            mime_debug("camera: no access to %s", path);
        return;
    }
    struct v4l2_capability cap;
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        close(fd);
        return;
    }
    uint32_t caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
        close(fd);
        return;
    }
    FormatList formats = {};
    enumerate_formats(fd, &formats);
    close(fd);
    if (!formats.count) {
        free(formats.format);
        return;
    }
    CameraSource *s = camera_list_add(list);
    if (!s) {
        free(formats.format);
        return;
    }
    char product[96] = "", device[PATH_MAX] = "";
    usb_identity(node, &s->info, product, sizeof(product), device, sizeof(device));
    /* the card is "product: interface" for usb cameras, the product alone is what windows shows. */
    char card[64];
    snprintf(card, sizeof(card), "%s", (const char *)cap.card);
    char *colon = strstr(card, ": ");
    if (colon)
        *colon = 0;
    snprintf(s->info.name, sizeof(s->info.name), "%s", product[0] ? product : card);
    snprintf(s->info.location, sizeof(s->info.location), "%s", (const char *)cap.bus_info);
    snprintf(s->key, sizeof(s->key), "v4l2:%s:%s", device[0] ? device : (const char *)cap.bus_info, node);
    snprintf(s->path, sizeof(s->path), "%s", path);
    s->formats = formats.format;
    s->format_count = formats.count;
}

static int by_node(const void *a, const void *b)
{
    const char *x = a, *y = b;
    return atoi(x + 5) - atoi(y + 5);
}

static void scan(CameraList *list)
{
    /* MIME_CAMERA_REAL=0 hides the host's own cameras, for tests that count them. */
    const char *real = getenv("MIME_CAMERA_REAL");
    if (real && !strcmp(real, "0"))
        return;
    DIR *dir = opendir("/dev");
    if (!dir)
        return;
    char nodes[32][16];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(dir)) && n < 32) {
        if (!strncmp(e->d_name, "video", 5) && e->d_name[5] >= '0' && e->d_name[5] <= '9' && strlen(e->d_name) < 16)
            snprintf(nodes[n++], sizeof(nodes[0]), "%s", e->d_name);
    }
    closedir(dir);
    qsort(nodes, (size_t)n, sizeof(nodes[0]), by_node);
    for (int i = 0; i < n; i++)
        probe(nodes[i], list);
}

/* ------------------------------------------------------------------ hotplug */

static int g_inotify = -2;

static int watch_fd(void)
{
    if (g_inotify == -2) {
        g_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (g_inotify >= 0 && inotify_add_watch(g_inotify, "/dev", IN_CREATE | IN_DELETE | IN_ATTRIB) < 0) {
            close(g_inotify);
            g_inotify = -1;
        }
    }
    return g_inotify;
}

/* a node of video appeared, went or changed: what else is made or removed in /dev is not for this. */
static bool watch_drain(void)
{
    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    bool relevant = false;
    for (;;) {
        ssize_t n = g_inotify >= 0 ? read(g_inotify, buffer, sizeof(buffer)) : -1;
        if (n <= 0)
            break;
        for (char *p = buffer; p < buffer + n;) {
            const struct inotify_event *e = (const struct inotify_event *)p;
            relevant |= e->len && !strncmp(e->name, "video", 5);
            p += sizeof(*e) + e->len;
        }
    }
    return relevant;
}

/* ------------------------------------------------------------------ streams */

typedef struct Stream {
    int fd;
    int wake[2];
    pthread_t thread;
    void *map[BUFFERS];
    size_t length[BUFFERS];
    int buffers;
    CameraDeliver deliver;
    CameraFailed failed;
    void *ctx;
    bool stop;
} Stream;

static void *capture(void *arg)
{
    Stream *s = arg;
    mime_set_thread_name("mime-v4l2");
    int silent = 0;
    for (;;) {
        struct pollfd fds[2] = {{.fd = s->fd, .events = POLLIN}, {.fd = s->wake[0], .events = POLLIN}};
        int r = poll(fds, 2, 1000);
        if (__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE))
            break;
        if (r < 0 && errno != EINTR)
            break;
        if (r == 0) {
            if (++silent == 5)
                s->failed(s->ctx, false);
            continue;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            s->failed(s->ctx, true);
            break;
        }
        if (!(fds[0].revents & POLLIN))
            continue;
        silent = 0;
        struct v4l2_buffer b = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
        if (xioctl(s->fd, VIDIOC_DQBUF, &b) < 0) {
            if (errno == EAGAIN)
                continue;
            s->failed(s->ctx, errno == ENODEV || errno == EIO);
            break;
        }
        uint64_t ns = camera_now_ns();
        if ((b.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC &&
            (b.timestamp.tv_sec || b.timestamp.tv_usec)) {
            uint64_t stamp = (uint64_t)b.timestamp.tv_sec * 1000000000ull + (uint64_t)b.timestamp.tv_usec * 1000ull;
            /* a driver's clock that is not this one would put frames far from now. */
            if (stamp <= ns && ns - stamp < 2000000000ull)
                ns = stamp;
        }
        if (b.index < (uint32_t)s->buffers)
            s->deliver(s->ctx, s->map[b.index], b.bytesused, ns, b.flags & V4L2_BUF_FLAG_ERROR ? MIME_CAMERA_FRAME_CORRUPT : 0);
        if (xioctl(s->fd, VIDIOC_QBUF, &b) < 0 && errno != EAGAIN) {
            s->failed(s->ctx, errno == ENODEV);
            break;
        }
    }
    return nullptr;
}

static void release(Stream *s)
{
    if (s->fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(s->fd, VIDIOC_STREAMOFF, &type);
    }
    for (int i = 0; i < s->buffers; i++)
        if (s->map[i] && s->map[i] != MAP_FAILED)
            munmap(s->map[i], s->length[i]);
    if (s->fd >= 0)
        close(s->fd);
    if (s->wake[0] >= 0)
        close(s->wake[0]);
    if (s->wake[1] >= 0)
        close(s->wake[1]);
    free(s);
}

static uint64_t status_of(int error)
{
    switch (error) {
    case EBUSY:
        return MIME_HC_STATUS_EXISTS;
    case EACCES:
    case EPERM:
        return MIME_HC_STATUS_ACCESS_DENIED;
    case ENOENT:
    case ENODEV:
        return MIME_HC_STATUS_NOT_FOUND;
    case EINVAL:
        return MIME_HC_STATUS_UNSUPPORTED;
    case ENOMEM:
        return MIME_HC_STATUS_NO_MEMORY;
    default:
        return MIME_HC_STATUS_IO_ERROR;
    }
}

static void *start(const CameraSource *source, const MimeCameraFormat *format, CameraDeliver deliver,
                   CameraFailed failed, void *ctx, uint64_t *error)
{
    Stream *s = calloc(1, sizeof(*s));
    if (!s) {
        *error = MIME_HC_STATUS_NO_MEMORY;
        return nullptr;
    }
    s->fd = -1;
    s->wake[0] = s->wake[1] = -1;
    s->deliver = deliver;
    s->failed = failed;
    s->ctx = ctx;
    s->fd = open(source->path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (s->fd < 0 || pipe2(s->wake, O_CLOEXEC | O_NONBLOCK) < 0) {
        *error = status_of(errno);
        release(s);
        return nullptr;
    }
    /* motion jpeg is what the driver calls it: ask for the name the camera listed. */
    uint32_t native = 0;
    if (format->fourcc == MIME_CAMERA_FOURCC_MJPG) {
        struct v4l2_fmtdesc d = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
        for (d.index = 0; xioctl(s->fd, VIDIOC_ENUM_FMT, &d) == 0 && !native; d.index++)
            if (fourcc_of(d.pixelformat) == MIME_CAMERA_FOURCC_MJPG)
                native = d.pixelformat;
    }
    uint32_t pixel_format = pixel_format_of(format->fourcc, native);
    struct v4l2_format f = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    f.fmt.pix.width = format->width;
    f.fmt.pix.height = format->height;
    f.fmt.pix.pixelformat = pixel_format;
    f.fmt.pix.field = V4L2_FIELD_ANY;
    if (xioctl(s->fd, VIDIOC_S_FMT, &f) < 0) {
        *error = status_of(errno);
        release(s);
        return nullptr;
    }
    if (f.fmt.pix.width != format->width || f.fmt.pix.height != format->height || f.fmt.pix.pixelformat != pixel_format) {
        *error = MIME_HC_STATUS_UNSUPPORTED;
        release(s);
        return nullptr;
    }
    struct v4l2_streamparm parm = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    parm.parm.capture.timeperframe.numerator = format->fps_den;
    parm.parm.capture.timeperframe.denominator = format->fps_num;
    xioctl(s->fd, VIDIOC_S_PARM, &parm);
    struct v4l2_requestbuffers req = {.count = BUFFERS, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
    if (xioctl(s->fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        *error = req.count < 2 && errno == 0 ? MIME_HC_STATUS_NO_MEMORY : status_of(errno);
        release(s);
        return nullptr;
    }
    for (uint32_t i = 0; i < req.count && i < BUFFERS; i++) {
        struct v4l2_buffer b = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i};
        if (xioctl(s->fd, VIDIOC_QUERYBUF, &b) < 0) {
            *error = status_of(errno);
            release(s);
            return nullptr;
        }
        s->map[i] = mmap(nullptr, b.length, PROT_READ, MAP_SHARED, s->fd, b.m.offset);
        s->length[i] = b.length;
        s->buffers = (int)i + 1;
        if (s->map[i] == MAP_FAILED || xioctl(s->fd, VIDIOC_QBUF, &b) < 0) {
            *error = status_of(errno);
            release(s);
            return nullptr;
        }
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(s->fd, VIDIOC_STREAMON, &type) < 0) {
        *error = status_of(errno);
        release(s);
        return nullptr;
    }
    if (pthread_create(&s->thread, nullptr, capture, s) != 0) {
        *error = MIME_HC_STATUS_NO_MEMORY;
        release(s);
        return nullptr;
    }
    return s;
}

static void stop(void *stream)
{
    Stream *s = stream;
    __atomic_store_n(&s->stop, true, __ATOMIC_RELEASE);
    char byte = 1;
    if (write(s->wake[1], &byte, 1) < 0) {
    }
    pthread_join(s->thread, nullptr);
    release(s);
}

/* ------------------------------------------------------------------ controls */

typedef struct Mapping {
    uint32_t set, property;
    uint32_t cid;
    uint32_t auto_cid;   /* the control that makes it automatic, 0 for none */
    bool auto_menu;      /* the auto control is the exposure menu rather than a boolean */
    bool exposure;       /* the value is the base 2 logarithm of seconds, not a count of 100 microseconds */
} Mapping;

static const Mapping g_mappings[] = {
    {.set = MIME_CAMERA_CONTROL_PROCAMP, .property = 0, .cid = V4L2_CID_BRIGHTNESS},
    {.set = MIME_CAMERA_CONTROL_PROCAMP, .property = 1, .cid = V4L2_CID_CONTRAST},
    {.set = MIME_CAMERA_CONTROL_PROCAMP, .property = 2, .cid = V4L2_CID_HUE, .auto_cid = V4L2_CID_HUE_AUTO},
    {.set = MIME_CAMERA_CONTROL_PROCAMP, .property = 3, .cid = V4L2_CID_SATURATION},
    {.set = MIME_CAMERA_CONTROL_PROCAMP, .property = 4, .cid = V4L2_CID_SHARPNESS},
    {.set = MIME_CAMERA_CONTROL_PROCAMP, .property = 5, .cid = V4L2_CID_GAMMA},
    {.set = MIME_CAMERA_CONTROL_PROCAMP, .property = 7, .cid = V4L2_CID_WHITE_BALANCE_TEMPERATURE,
     .auto_cid = V4L2_CID_AUTO_WHITE_BALANCE},
    {.set = MIME_CAMERA_CONTROL_PROCAMP, .property = 8, .cid = V4L2_CID_BACKLIGHT_COMPENSATION},
    {.set = MIME_CAMERA_CONTROL_PROCAMP, .property = 9, .cid = V4L2_CID_GAIN, .auto_cid = V4L2_CID_AUTOGAIN},
    {.set = MIME_CAMERA_CONTROL_CAMERA, .property = 0, .cid = V4L2_CID_PAN_ABSOLUTE},
    {.set = MIME_CAMERA_CONTROL_CAMERA, .property = 1, .cid = V4L2_CID_TILT_ABSOLUTE},
    {.set = MIME_CAMERA_CONTROL_CAMERA, .property = 3, .cid = V4L2_CID_ZOOM_ABSOLUTE},
    {.set = MIME_CAMERA_CONTROL_CAMERA, .property = 4, .cid = V4L2_CID_EXPOSURE_ABSOLUTE,
     .auto_cid = V4L2_CID_EXPOSURE_AUTO, .auto_menu = true, .exposure = true},
    {.set = MIME_CAMERA_CONTROL_CAMERA, .property = 5, .cid = V4L2_CID_IRIS_ABSOLUTE},
    {.set = MIME_CAMERA_CONTROL_CAMERA, .property = 6, .cid = V4L2_CID_FOCUS_ABSOLUTE, .auto_cid = V4L2_CID_FOCUS_AUTO},
};

static bool query(int fd, uint32_t cid, struct v4l2_queryctrl *q)
{
    memset(q, 0, sizeof(*q));
    q->id = cid;
    return xioctl(fd, VIDIOC_QUERYCTRL, q) == 0 && !(q->flags & V4L2_CTRL_FLAG_DISABLED);
}

static int get_ctrl(int fd, uint32_t cid, int32_t *value)
{
    struct v4l2_control c = {.id = cid};
    if (xioctl(fd, VIDIOC_G_CTRL, &c) < 0)
        return -1;
    *value = c.value;
    return 0;
}

static int set_ctrl(int fd, uint32_t cid, int32_t value)
{
    struct v4l2_control c = {.id = cid, .value = value};
    return xioctl(fd, VIDIOC_S_CTRL, &c);
}

/* an exposure time in units of 100 microseconds as the power of two of seconds it is closest to from below. */
static int32_t exposure_to_log2(int32_t t)
{
    return (int32_t)floor(log2(t <= 0 ? 1.0 : t * 1e-4));
}

static int32_t exposure_from_log2(int32_t e)
{
    return (int32_t)lrint(pow(2.0, e) * 1e4);
}

static void control(const CameraSource *source, MimeCameraControl *c)
{
    const Mapping *m = nullptr;
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_mappings); i++)
        if (g_mappings[i].set == c->set && g_mappings[i].property == c->property)
            m = &g_mappings[i];
    if (!m) {
        c->status = MIME_HC_STATUS_NOT_FOUND;
        return;
    }
    int fd = open(source->path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        c->status = (int32_t)status_of(errno);
        return;
    }
    struct v4l2_queryctrl q;
    if (!query(fd, m->cid, &q)) {
        c->status = MIME_HC_STATUS_NOT_FOUND;
        close(fd);
        return;
    }
    c->status = MIME_HC_STATUS_OK;
    uint32_t flags = MIME_CAMERA_CONTROL_FLAG_MANUAL;
    int32_t automatic = 0;
    if (m->auto_cid) {
        struct v4l2_queryctrl a;
        if (query(fd, m->auto_cid, &a)) {
            flags |= MIME_CAMERA_CONTROL_FLAG_AUTO;
            if (get_ctrl(fd, m->auto_cid, &automatic) == 0 && !m->auto_menu)
                automatic = automatic != 0;
            else if (m->auto_menu)
                automatic = automatic != V4L2_EXPOSURE_MANUAL;
        }
    }
    int32_t value = 0;
    switch (c->operation) {
    case MIME_CAMERA_CONTROL_RANGE:
        c->flags = flags;
        c->step = q.step ? q.step : 1;
        if (m->exposure) {
            c->min = exposure_to_log2(q.minimum);
            c->max = exposure_to_log2(q.maximum);
            c->step = 1;
            c->default_value = exposure_to_log2(q.default_value);
        } else {
            c->min = q.minimum;
            c->max = q.maximum;
            c->default_value = q.default_value;
        }
        [[fallthrough]]; /* the range also says what the value is */
    case MIME_CAMERA_CONTROL_GET:
        if (get_ctrl(fd, m->cid, &value) < 0)
            c->status = (int32_t)status_of(errno);
        else
            c->value = m->exposure ? exposure_to_log2(value) : value;
        c->flags = (automatic ? MIME_CAMERA_CONTROL_FLAG_AUTO : MIME_CAMERA_CONTROL_FLAG_MANUAL) |
                   (c->operation == MIME_CAMERA_CONTROL_RANGE ? flags : 0);
        break;
    case MIME_CAMERA_CONTROL_SET:
        if (m->auto_cid && (flags & MIME_CAMERA_CONTROL_FLAG_AUTO)) {
            bool want_auto = (c->flags & MIME_CAMERA_CONTROL_FLAG_AUTO) != 0;
            int32_t mode = m->auto_menu ? (want_auto ? V4L2_EXPOSURE_APERTURE_PRIORITY : V4L2_EXPOSURE_MANUAL) : want_auto;
            if (set_ctrl(fd, m->auto_cid, mode) < 0) {
                c->status = (int32_t)status_of(errno);
                break;
            }
            if (want_auto)
                break;
        }
        value = m->exposure ? exposure_from_log2(c->value) : c->value;
        if (value < q.minimum)
            value = q.minimum;
        if (value > q.maximum)
            value = q.maximum;
        if (set_ctrl(fd, m->cid, value) < 0)
            c->status = (int32_t)status_of(errno);
        break;
    default:
        c->status = MIME_HC_STATUS_INVALID;
    }
    close(fd);
}

const CameraBackend camera_backend_v4l2 = {
    .name = "v4l2",
    .scan = scan,
    .watch_fd = watch_fd,
    .watch_drain = watch_drain,
    .poll_ms = nullptr,
    .start = start,
    .stop = stop,
    .control = control,
};
