#include "ui_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_PADS 16
#define DEVICES "/dev/input"
#define VALVE 0x28DE
#define STEAM_DECK 0x1205    /* the deck's own controls, as the kernel reads them. */
#define STEAM_VIRTUAL 0x11FF /* the pad Steam makes for a game out of what it reads itself. */

#define BITS(n) (((n) + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long)))

typedef struct Pad {
    int fd; /* -1 for a free slot. */
    uint32_t id;
    char node[32];
    uint16_t vendor, product;
    /*
     * the kernel's drivers for Sony and Nintendo pads name the top and the left button by their place, as the gamepad
     * protocol has it. the others name them X and Y after the letters of an Xbox pad, which are the other way round.
     */
    bool by_place;
    bool hat, button_triggers;
    uint16_t left_trigger, right_trigger; /* the axes of the triggers, 0 without. */
    struct input_absinfo abs[ABS_CNT];
    unsigned long keys[BITS(KEY_CNT)];
    int32_t value[ABS_CNT];
    int rumble; /* the id of the effect on the device, -1 before one. */
    bool has_rumble;
    bool hidden; /* not told to the guest, see hide_shadowed. */
    bool told;
    bool dirty;
    bool dropped; /* the kernel lost events, and what is left of their packet is not read. */
} Pad;

static Pad g_pads[MAX_PADS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_next_id;
static bool g_started;

static bool bit(const unsigned long *bits, unsigned n)
{
    return bits[n / (8 * sizeof(unsigned long))] >> (n % (8 * sizeof(unsigned long))) & 1;
}

/* an axis as a signed 16 bit value over its whole range. */
static int stick(const Pad *p, unsigned axis)
{
    const struct input_absinfo *a = &p->abs[axis];
    if (a->maximum <= a->minimum)
        return 0;
    double v = ((double)p->value[axis] - a->minimum) / ((double)a->maximum - a->minimum);
    int out = (int)(v * 65535.0 + 0.5) - 32768;
    int flat = (int)((double)a->flat / ((double)a->maximum - a->minimum) * 65535.0);
    return abs(out) <= flat ? 0 : out;
}

static int trigger(const Pad *p, unsigned axis, unsigned button)
{
    if (axis) {
        const struct input_absinfo *a = &p->abs[axis];
        if (a->maximum > a->minimum)
            return (int)(((double)p->value[axis] - a->minimum) / ((double)a->maximum - a->minimum) * 255.0 + 0.5);
    }
    return p->button_triggers && bit(p->keys, button) ? 255 : 0;
}

/* the state as an Xbox 360 pad has it, with positive Y upward as in Windows. */
static void emit_pad(Pad *p, uint32_t type)
{
    MimeInputEvent ev = {.type = type, .pad = p->id};
    if (type == MIME_INPUT_PAD_REMOVED) {
        mime_ui_emit(&ev);
        return;
    }
    ev.pad_vid = p->vendor;
    ev.pad_pid = p->product;
    static const struct {
        uint16_t key, bit;
    } buttons[] = {
        {BTN_DPAD_UP, 0x0001}, {BTN_DPAD_DOWN, 0x0002}, {BTN_DPAD_LEFT, 0x0004}, {BTN_DPAD_RIGHT, 0x0008},
        {BTN_START, 0x0010},   {BTN_SELECT, 0x0020},    {BTN_THUMBL, 0x0040},    {BTN_THUMBR, 0x0080},
        {BTN_TL, 0x0100},      {BTN_TR, 0x0200},        {BTN_MODE, 0x0400},      {BTN_SOUTH, 0x1000},
        {BTN_EAST, 0x2000},
    };
    for (size_t i = 0; i < MIME_ARRAY_COUNT(buttons); i++)
        if (bit(p->keys, buttons[i].key))
            ev.pad_buttons |= buttons[i].bit;
    if (bit(p->keys, p->by_place ? BTN_WEST : BTN_NORTH))
        ev.pad_buttons |= 0x4000;
    if (bit(p->keys, p->by_place ? BTN_NORTH : BTN_WEST))
        ev.pad_buttons |= 0x8000;
    if (p->hat) {
        int x = p->value[ABS_HAT0X], y = p->value[ABS_HAT0Y];
        ev.pad_buttons |= (y < 0 ? 0x0001 : 0) | (y > 0 ? 0x0002 : 0) | (x < 0 ? 0x0004 : 0) | (x > 0 ? 0x0008 : 0);
    }
    int ly = stick(p, ABS_Y), ry = stick(p, ABS_RY);
    ev.pad_axes[MIME_PAD_LX] = (int16_t)stick(p, ABS_X);
    ev.pad_axes[MIME_PAD_LY] = (int16_t)(ly == -32768 ? 32767 : -ly);
    ev.pad_axes[MIME_PAD_RX] = (int16_t)stick(p, ABS_RX);
    ev.pad_axes[MIME_PAD_RY] = (int16_t)(ry == -32768 ? 32767 : -ry);
    ev.pad_axes[MIME_PAD_LT] = (int16_t)trigger(p, p->left_trigger, BTN_TL2);
    ev.pad_axes[MIME_PAD_RT] = (int16_t)trigger(p, p->right_trigger, BTN_TR2);
    mime_ui_emit(&ev);
}

/* a list Steam gives the programs it starts, of the pads it wraps in one of its own: 0xVVVV/0xPPPP, with commas. */
static bool ignored(uint16_t vendor, uint16_t product)
{
    const char *list = getenv("SDL_GAMECONTROLLER_IGNORE_DEVICES");
    while (list && *list) {
        unsigned v = 0, p = 0;
        if (sscanf(list, "%x/%x", &v, &p) == 2 && v == vendor && p == product)
            return true;
        list = strchr(list, ',');
        if (list)
            list++;
    }
    return false;
}

/*
 * while Steam runs it reads the deck's controls itself, and the kernel's pad for them stays there and tells nothing: a
 * game would take it for the first player. it is hidden as long as a pad of Steam's own is there. with the lock.
 */
static void hide_shadowed(void)
{
    bool steam = false;
    for (int i = 0; i < MAX_PADS; i++)
        steam = steam || (g_pads[i].fd >= 0 && g_pads[i].vendor == VALVE && g_pads[i].product == STEAM_VIRTUAL);
    for (int i = 0; i < MAX_PADS; i++) {
        Pad *p = &g_pads[i];
        if (p->fd < 0)
            continue;
        p->hidden = steam && p->vendor == VALVE && p->product == STEAM_DECK;
        if (p->hidden && p->told) {
            p->told = false;
            emit_pad(p, MIME_INPUT_PAD_REMOVED);
        } else if (!p->hidden && !p->told) {
            p->told = true;
            emit_pad(p, MIME_INPUT_PAD_ADDED);
            emit_pad(p, MIME_INPUT_PAD_STATE);
        }
    }
}

static void pad_open(const char *name)
{
    if (strncmp(name, "event", 5))
        return;
    pthread_mutex_lock(&g_lock);
    Pad *p = nullptr;
    for (int i = 0; i < MAX_PADS; i++) {
        if (g_pads[i].fd >= 0 && !strcmp(g_pads[i].node, name)) {
            pthread_mutex_unlock(&g_lock);
            return;
        }
        if (g_pads[i].fd < 0 && !p)
            p = &g_pads[i];
    }
    pthread_mutex_unlock(&g_lock);
    if (!p)
        return;
    char path[64];
    snprintf(path, sizeof(path), DEVICES "/%s", name);
    /* writing is for the rumble, which a device that is read only does without. */
    int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return;
    unsigned long types[BITS(EV_CNT)] = {}, keys[BITS(KEY_CNT)] = {}, axes[BITS(ABS_CNT)] = {}, effects[BITS(FF_CNT)] = {};
    struct input_id id = {};
    bool pad = ioctl(fd, EVIOCGBIT(0, sizeof(types)), types) >= 0 && bit(types, EV_KEY) && bit(types, EV_ABS) &&
               ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0 && bit(keys, BTN_SOUTH) &&
               ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(axes)), axes) >= 0 && bit(axes, ABS_X) && bit(axes, ABS_Y) &&
               ioctl(fd, EVIOCGID, &id) >= 0 && !ignored(id.vendor, id.product);
    if (!pad) {
        close(fd);
        return;
    }
    Pad made = {.fd = fd, .vendor = id.vendor, .product = id.product, .rumble = -1};
    mime_strlcpy(made.node, name, sizeof(made.node));
    made.by_place = id.vendor == 0x054C || id.vendor == 0x057E;
    /*
     * hid-steam has the directions as buttons and the left trackpad on the first hat, which is no direction pad then.
     * its triggers are on the third hat, the left one on Y.
     */
    made.hat = bit(axes, ABS_HAT0X) && bit(axes, ABS_HAT0Y) && !bit(keys, BTN_DPAD_UP);
    if (bit(axes, ABS_Z) && bit(axes, ABS_RZ))
        made.left_trigger = ABS_Z, made.right_trigger = ABS_RZ;
    else if (bit(axes, ABS_HAT2Y) && bit(axes, ABS_HAT2X))
        made.left_trigger = ABS_HAT2Y, made.right_trigger = ABS_HAT2X;
    made.button_triggers = bit(keys, BTN_TL2) && bit(keys, BTN_TR2);
    for (unsigned axis = 0; axis < ABS_CNT; axis++)
        if (bit(axes, axis) && ioctl(fd, EVIOCGABS(axis), &made.abs[axis]) >= 0)
            made.value[axis] = made.abs[axis].value;
    if (ioctl(fd, EVIOCGKEY(sizeof(made.keys)), made.keys) < 0)
        memset(made.keys, 0, sizeof(made.keys));
    made.has_rumble = bit(types, EV_FF) && ioctl(fd, EVIOCGBIT(EV_FF, sizeof(effects)), effects) >= 0 &&
                      bit(effects, FF_RUMBLE);
    char label[96] = "";
    if (ioctl(fd, EVIOCGNAME(sizeof(label)), label) < 0)
        label[0] = 0;
    pthread_mutex_lock(&g_lock);
    made.id = ++g_next_id;
    *p = made;
    mime_info("game controller %s: %s, %04x:%04x%s", name, label, id.vendor, id.product,
              made.has_rumble ? ", rumble" : "");
    hide_shadowed();
    pthread_mutex_unlock(&g_lock);
}

/* with the lock. */
static void pad_close(Pad *p)
{
    if (p->told)
        emit_pad(p, MIME_INPUT_PAD_REMOVED);
    close(p->fd);
    *p = (Pad){.fd = -1};
    hide_shadowed();
}

/* with the lock. */
static void pad_read(Pad *p)
{
    struct input_event events[32];
    for (;;) {
        ssize_t got = read(p->fd, events, sizeof(events));
        if (got < 0 && errno == EINTR)
            continue;
        if (got < 0 && errno == EAGAIN)
            break;
        if (got <= 0) {
            pad_close(p);
            return;
        }
        for (size_t i = 0; i < (size_t)got / sizeof(events[0]); i++) {
            const struct input_event *e = &events[i];
            if (p->dropped) {
                /* the state is read whole once the packet that lost events ends. */
                if (e->type != EV_SYN || e->code != SYN_REPORT)
                    continue;
                p->dropped = false;
                if (ioctl(p->fd, EVIOCGKEY(sizeof(p->keys)), p->keys) < 0) {
                }
                for (unsigned axis = 0; axis < ABS_CNT; axis++)
                    if (p->abs[axis].maximum > p->abs[axis].minimum && ioctl(p->fd, EVIOCGABS(axis), &p->abs[axis]) >= 0)
                        p->value[axis] = p->abs[axis].value;
                p->dirty = true;
                continue;
            }
            if (e->type == EV_KEY && e->code < KEY_CNT) {
                unsigned long mask = 1ul << (e->code % (8 * sizeof(unsigned long)));
                unsigned long *word = &p->keys[e->code / (8 * sizeof(unsigned long))];
                *word = e->value ? *word | mask : *word & ~mask;
                p->dirty = true;
            } else if (e->type == EV_ABS && e->code < ABS_CNT) {
                p->value[e->code] = e->value;
                p->dirty = true;
            } else if (e->type == EV_SYN && e->code == SYN_DROPPED) {
                p->dropped = true;
            }
        }
    }
    if (p->dirty && p->told)
        emit_pad(p, MIME_INPUT_PAD_STATE);
    p->dirty = false;
}

static void *pad_thread(void *arg)
{
    mime_set_thread_name("mime-gamepad");
    /* a node shows up before udev lets the user read it, which a change of its attributes then tells. */
    int watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch >= 0 && inotify_add_watch(watch, DEVICES, IN_CREATE | IN_ATTRIB | IN_DELETE) < 0) {
        close(watch);
        watch = -1;
    }
    DIR *dir = opendir(DEVICES);
    struct dirent *entry;
    while (dir && (entry = readdir(dir)))
        pad_open(entry->d_name);
    if (dir)
        closedir(dir);
    for (;;) {
        struct pollfd fds[MAX_PADS + 1] = {{watch, POLLIN, 0}};
        Pad *of[MAX_PADS + 1] = {};
        nfds_t count = 1;
        pthread_mutex_lock(&g_lock);
        for (int i = 0; i < MAX_PADS; i++)
            if (g_pads[i].fd >= 0) {
                of[count] = &g_pads[i];
                fds[count++] = (struct pollfd){g_pads[i].fd, POLLIN, 0};
            }
        pthread_mutex_unlock(&g_lock);
        if (watch < 0 && count == 1)
            return nullptr;
        if (poll(fds, count, -1) < 0 && errno != EINTR)
            return nullptr;
        pthread_mutex_lock(&g_lock);
        for (nfds_t i = 1; i < count; i++)
            if ((fds[i].revents & (POLLIN | POLLERR | POLLHUP)) && of[i]->fd == fds[i].fd)
                pad_read(of[i]);
        pthread_mutex_unlock(&g_lock);
        if (!(fds[0].revents & POLLIN))
            continue;
        char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
        ssize_t got;
        while ((got = read(watch, buffer, sizeof(buffer))) > 0) {
            for (char *at = buffer; at < buffer + got;) {
                const struct inotify_event *e = (const struct inotify_event *)(void *)at;
                at += sizeof(*e) + e->len;
                if (!e->len)
                    continue;
                if (e->mask & (IN_CREATE | IN_ATTRIB))
                    pad_open(e->name);
            }
        }
    }
}

void mime_gamepad_start(void)
{
    pthread_mutex_lock(&g_lock);
    bool first = !g_started;
    g_started = true;
    for (int i = 0; first && i < MAX_PADS; i++)
        g_pads[i].fd = -1;
    pthread_mutex_unlock(&g_lock);
    pthread_t thread;
    if (first && pthread_create(&thread, nullptr, pad_thread, nullptr) == 0)
        pthread_detach(thread);
}

/* XInput motors run until stopped, an effect of the kernel for a time: a long one, played again at each change. */
void mime_ui_pad_rumble(uint32_t pad, uint16_t low, uint16_t high)
{
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_PADS; i++) {
        Pad *p = &g_pads[i];
        if (p->fd < 0 || p->id != pad || !p->has_rumble)
            continue;
        struct input_event stop = {.type = EV_FF, .code = (uint16_t)p->rumble, .value = 0};
        if (p->rumble >= 0 && write(p->fd, &stop, sizeof(stop)) < 0) {
        }
        if (!low && !high)
            break;
        struct ff_effect effect = {.type = FF_RUMBLE, .id = (int16_t)p->rumble, .replay.length = 60000};
        effect.u.rumble.strong_magnitude = low;
        effect.u.rumble.weak_magnitude = high;
        if (ioctl(p->fd, EVIOCSFF, &effect) < 0)
            break;
        p->rumble = effect.id;
        struct input_event play = {.type = EV_FF, .code = (uint16_t)effect.id, .value = 1};
        if (write(p->fd, &play, sizeof(play)) < 0) {
        }
        break;
    }
    pthread_mutex_unlock(&g_lock);
}
