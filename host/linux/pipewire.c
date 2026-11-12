#include "pipewire.h"

#if MIME_HAVE_PIPEWIRE

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/param/format.h>
#include <spa/param/param.h>
#include <spa/param/route.h>
#include <spa/pod/iter.h>

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ROUTES 8

enum { KIND_NODE, KIND_DEVICE, KIND_METADATA };

typedef struct Route {
    uint32_t direction, device;
    char description[48];
    char type[24];
} Route;

typedef struct Object {
    struct Object *next;
    uint32_t id;
    int kind;
    struct pw_proxy *proxy;
    struct spa_hook listener;
    /* a node. */
    bool capture;
    char name[256];
    char description[sizeof(((MimeAudioDevice *)0)->name)];
    uint32_t device;       /* the id of the card's object. */
    uint32_t route_device; /* the number its routes know the node by. */
    uint32_t resolution;
    uint32_t rate, channels, bits, is_float, valid_bits, channel_mask;
    /* a card. */
    char adapter[sizeof(((MimeAudioDevice *)0)->adapter)];
    char form[24];
    Route routes[MAX_ROUTES];
    uint32_t route_count;
} Object;

static struct {
    void (*init)(int *, char ***);
    struct pw_thread_loop *(*thread_loop_new)(const char *, const struct spa_dict *);
    int (*thread_loop_start)(struct pw_thread_loop *);
    void (*thread_loop_lock)(struct pw_thread_loop *);
    void (*thread_loop_unlock)(struct pw_thread_loop *);
    struct pw_loop *(*thread_loop_get_loop)(struct pw_thread_loop *);
    struct pw_context *(*context_new)(struct pw_loop *, struct pw_properties *, size_t);
    struct pw_core *(*context_connect)(struct pw_context *, struct pw_properties *, size_t);
    void (*proxy_destroy)(struct pw_proxy *);
} pw;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_synced = PTHREAD_COND_INITIALIZER;
static Object *g_objects;
static char g_default[2][256];
static struct pw_core *g_core;
static struct pw_registry *g_registry;
static struct spa_hook g_core_listener, g_registry_listener;
static int g_sync_seq, g_sync_rounds;
static bool g_started, g_alive;

static void copy(char *out, size_t size, const char *text)
{
    snprintf(out, size, "%s", text ? text : "");
}

static uint32_t number(const struct spa_dict *props, const char *key, uint32_t fallback)
{
    const char *text = spa_dict_lookup(props, key);
    return text && text[0] ? (uint32_t)strtoul(text, nullptr, 10) : fallback;
}

/* the first value of a property, which is the one the server prefers when it offers several. */
static const void *first_value(const struct spa_pod *object, uint32_t key, uint32_t type)
{
    const struct spa_pod_prop *prop = spa_pod_find_prop(object, nullptr, key);
    if (!prop)
        return nullptr;
    uint32_t count = 0, choice = 0;
    const struct spa_pod *values = spa_pod_get_values(&prop->value, &count, &choice);
    if (!count || !values || values->type != type || values->size < 4)
        return nullptr;
    return SPA_POD_BODY_CONST(values);
}

static uint32_t speaker_of(uint32_t position)
{
    switch (position) {
    case SPA_AUDIO_CHANNEL_FL: return 0x1;
    case SPA_AUDIO_CHANNEL_FR: return 0x2;
    case SPA_AUDIO_CHANNEL_MONO:
    case SPA_AUDIO_CHANNEL_FC: return 0x4;
    case SPA_AUDIO_CHANNEL_LFE: return 0x8;
    case SPA_AUDIO_CHANNEL_RL: return 0x10;
    case SPA_AUDIO_CHANNEL_RR: return 0x20;
    case SPA_AUDIO_CHANNEL_FLC: return 0x40;
    case SPA_AUDIO_CHANNEL_FRC: return 0x80;
    case SPA_AUDIO_CHANNEL_RC: return 0x100;
    case SPA_AUDIO_CHANNEL_SL: return 0x200;
    case SPA_AUDIO_CHANNEL_SR: return 0x400;
    case SPA_AUDIO_CHANNEL_TC: return 0x800;
    case SPA_AUDIO_CHANNEL_TFL: return 0x1000;
    case SPA_AUDIO_CHANNEL_TFC: return 0x2000;
    case SPA_AUDIO_CHANNEL_TFR: return 0x4000;
    case SPA_AUDIO_CHANNEL_TRL: return 0x8000;
    case SPA_AUDIO_CHANNEL_TRC: return 0x10000;
    case SPA_AUDIO_CHANNEL_TRR: return 0x20000;
    default: return 0;
    }
}

static void read_format(Object *o, const struct spa_pod *param)
{
    const uint32_t *type = first_value(param, SPA_FORMAT_mediaType, SPA_TYPE_Id);
    const uint32_t *subtype = first_value(param, SPA_FORMAT_mediaSubtype, SPA_TYPE_Id);
    const uint32_t *format = first_value(param, SPA_FORMAT_AUDIO_format, SPA_TYPE_Id);
    const int32_t *rate = first_value(param, SPA_FORMAT_AUDIO_rate, SPA_TYPE_Int);
    const int32_t *channels = first_value(param, SPA_FORMAT_AUDIO_channels, SPA_TYPE_Int);
    if (!type || *type != SPA_MEDIA_TYPE_audio || !subtype || *subtype != SPA_MEDIA_SUBTYPE_raw || !format)
        return;
    uint32_t bits = 0, valid = 0, is_float = 0;
    switch (*format) {
    case SPA_AUDIO_FORMAT_U8:
    case SPA_AUDIO_FORMAT_S8:
    case SPA_AUDIO_FORMAT_U8P:
    case SPA_AUDIO_FORMAT_S16:
    case SPA_AUDIO_FORMAT_S16P:
        bits = 16;
        break;
    case SPA_AUDIO_FORMAT_S24:
    case SPA_AUDIO_FORMAT_S24P:
        bits = 24;
        break;
    case SPA_AUDIO_FORMAT_S24_32:
    case SPA_AUDIO_FORMAT_S24_32P:
        bits = 32;
        valid = 24;
        break;
    case SPA_AUDIO_FORMAT_S32:
    case SPA_AUDIO_FORMAT_S32P:
        bits = 32;
        break;
    case SPA_AUDIO_FORMAT_F32:
    case SPA_AUDIO_FORMAT_F32P:
    case SPA_AUDIO_FORMAT_F64:
    case SPA_AUDIO_FORMAT_F64P:
        bits = 32;
        is_float = 1;
        break;
    default:
        return;
    }
    uint32_t mask = 0, positions = 0;
    const struct spa_pod_prop *prop = spa_pod_find_prop(param, nullptr, SPA_FORMAT_AUDIO_position);
    if (prop) {
        uint32_t count = 0, choice = 0;
        const struct spa_pod *values = spa_pod_get_values(&prop->value, &count, &choice);
        const uint32_t *ids = values && values->type == SPA_TYPE_Array ? spa_pod_get_array(values, &positions) : nullptr;
        for (uint32_t i = 0; ids && i < positions; i++) {
            uint32_t speaker = speaker_of(ids[i]);
            /* windows orders channels as the bits of the mask, and has no name for the rest. */
            if (!speaker || speaker <= mask) {
                mask = 0;
                break;
            }
            mask |= speaker;
        }
    }
    pthread_mutex_lock(&g_lock);
    o->bits = bits;
    o->valid_bits = valid;
    o->is_float = is_float;
    o->rate = rate && *rate > 0 ? (uint32_t)*rate : 0;
    o->channels = channels && *channels > 0 ? (uint32_t)*channels : 0;
    o->channel_mask = o->channels && positions == o->channels ? mask : 0;
    pthread_mutex_unlock(&g_lock);
}

static void read_route(Object *o, uint32_t index, const struct spa_pod *param)
{
    Route route = {0};
    const uint32_t *direction = first_value(param, SPA_PARAM_ROUTE_direction, SPA_TYPE_Id);
    const int32_t *device = first_value(param, SPA_PARAM_ROUTE_device, SPA_TYPE_Int);
    if (!direction || !device)
        return;
    route.direction = *direction;
    route.device = (uint32_t)*device;
    const struct spa_pod_prop *prop = spa_pod_find_prop(param, nullptr, SPA_PARAM_ROUTE_description);
    const char *text = nullptr;
    if (prop && spa_pod_get_string(&prop->value, &text) == 0)
        copy(route.description, sizeof(route.description), text);
    prop = spa_pod_find_prop(param, nullptr, SPA_PARAM_ROUTE_info);
    if (prop && spa_pod_is_struct(&prop->value)) {
        /* a count, then keys and their values. */
        const struct spa_pod *item;
        const char *key = nullptr;
        int at = 0;
        SPA_POD_STRUCT_FOREACH(&prop->value, item) {
            if (at++ == 0 || spa_pod_get_string(item, &text) != 0)
                continue;
            if (at % 2 == 0) {
                key = text;
            } else if (key && !strcmp(key, "port.type")) {
                copy(route.type, sizeof(route.type), text);
            }
        }
    }
    pthread_mutex_lock(&g_lock);
    if (index == 0)
        o->route_count = 0;
    if (o->route_count < MAX_ROUTES)
        o->routes[o->route_count++] = route;
    pthread_mutex_unlock(&g_lock);
}

static void node_info(void *data, const struct pw_node_info *info)
{
    Object *o = data;
    if (!(info->change_mask & PW_NODE_CHANGE_MASK_PROPS) || !info->props)
        return;
    const char *name = spa_dict_lookup(info->props, PW_KEY_NODE_NAME);
    const char *description = spa_dict_lookup(info->props, PW_KEY_NODE_DESCRIPTION);
    pthread_mutex_lock(&g_lock);
    copy(o->name, sizeof(o->name), name);
    copy(o->description, sizeof(o->description), description ? description : name);
    o->device = number(info->props, PW_KEY_DEVICE_ID, SPA_ID_INVALID);
    o->route_device = number(info->props, "card.profile.device", SPA_ID_INVALID);
    o->resolution = number(info->props, "alsa.resolution_bits", 0);
    pthread_mutex_unlock(&g_lock);
}

static void node_param(void *data, int seq, uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param)
{
    if (id == SPA_PARAM_EnumFormat && param && spa_pod_is_object(param))
        read_format(data, param);
}

static void device_info(void *data, const struct pw_device_info *info)
{
    Object *o = data;
    if (!(info->change_mask & PW_DEVICE_CHANGE_MASK_PROPS) || !info->props)
        return;
    pthread_mutex_lock(&g_lock);
    copy(o->adapter, sizeof(o->adapter), spa_dict_lookup(info->props, PW_KEY_DEVICE_DESCRIPTION));
    copy(o->form, sizeof(o->form), spa_dict_lookup(info->props, PW_KEY_DEVICE_FORM_FACTOR));
    pthread_mutex_unlock(&g_lock);
}

static void device_param(void *data, int seq, uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param)
{
    if (id == SPA_PARAM_Route && param && spa_pod_is_object(param))
        read_route(data, index, param);
}

/* the value is {"name": "the node's name"}, and none at all when the server has no device to choose. */
static int metadata_property(void *data, uint32_t subject, const char *key, const char *type, const char *value)
{
    int which = !key ? -1 : !strcmp(key, "default.audio.sink") ? 0 : !strcmp(key, "default.audio.source") ? 1 : -1;
    if (which < 0 && key)
        return 0;
    char name[sizeof(g_default[0])] = "";
    const char *at = value ? strstr(value, "\"name\"") : nullptr;
    at = at ? strchr(at + 6, ':') : nullptr;
    at = at ? strchr(at, '"') : nullptr;
    if (at) {
        size_t n = 0;
        for (at++; *at && *at != '"' && n + 1 < sizeof(name); at++) {
            if (*at == '\\' && at[1])
                at++;
            name[n++] = *at;
        }
        name[n] = 0;
    }
    pthread_mutex_lock(&g_lock);
    /* no key: every property of the subject is gone. */
    for (int i = 0; i < 2; i++)
        if (which == i || which < 0)
            copy(g_default[i], sizeof(g_default[i]), name);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static const struct pw_node_events node_events = {
    PW_VERSION_NODE_EVENTS,
    .info = node_info,
    .param = node_param,
};
static const struct pw_device_events device_events = {
    PW_VERSION_DEVICE_EVENTS,
    .info = device_info,
    .param = device_param,
};
static const struct pw_metadata_events metadata_events = {
    PW_VERSION_METADATA_EVENTS,
    .property = metadata_property,
};

static void registry_global(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version,
                            const struct spa_dict *props)
{
    if (!props)
        return;
    const char *media = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    int kind;
    bool capture = false;
    if (!strcmp(type, PW_TYPE_INTERFACE_Node) && media && !strncmp(media, "Audio/Sink", 10)) {
        kind = KIND_NODE;
    } else if (!strcmp(type, PW_TYPE_INTERFACE_Node) && media && !strncmp(media, "Audio/Source", 12)) {
        kind = KIND_NODE;
        capture = true;
    } else if (!strcmp(type, PW_TYPE_INTERFACE_Device) && media && !strncmp(media, "Audio/", 6)) {
        kind = KIND_DEVICE;
    } else if (!strcmp(type, PW_TYPE_INTERFACE_Metadata)) {
        const char *name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
        if (!name || strcmp(name, "default"))
            return;
        kind = KIND_METADATA;
    } else {
        return;
    }
    Object *o = calloc(1, sizeof(*o));
    if (!o)
        return;
    o->id = id;
    o->kind = kind;
    o->capture = capture;
    o->device = o->route_device = SPA_ID_INVALID;
    uint32_t wanted = kind == KIND_NODE ? PW_VERSION_NODE : kind == KIND_DEVICE ? PW_VERSION_DEVICE : PW_VERSION_METADATA;
    o->proxy = pw_registry_bind(g_registry, id, type, wanted < version ? wanted : version, 0);
    if (!o->proxy) {
        free(o);
        return;
    }
    pthread_mutex_lock(&g_lock);
    Object **last = &g_objects;
    while (*last)
        last = &(*last)->next;
    *last = o;
    pthread_mutex_unlock(&g_lock);
    if (kind == KIND_NODE) {
        uint32_t params[] = {SPA_PARAM_EnumFormat};
        pw_node_add_listener((struct pw_node *)o->proxy, &o->listener, &node_events, o);
        pw_node_subscribe_params((struct pw_node *)o->proxy, params, 1);
    } else if (kind == KIND_DEVICE) {
        uint32_t params[] = {SPA_PARAM_Route};
        pw_device_add_listener((struct pw_device *)o->proxy, &o->listener, &device_events, o);
        pw_device_subscribe_params((struct pw_device *)o->proxy, params, 1);
    } else {
        pw_metadata_add_listener((struct pw_metadata *)o->proxy, &o->listener, &metadata_events, o);
    }
}

static void registry_global_remove(void *data, uint32_t id)
{
    pthread_mutex_lock(&g_lock);
    Object *found = nullptr;
    for (Object **at = &g_objects; *at; at = &(*at)->next) {
        if ((*at)->id == id) {
            found = *at;
            *at = found->next;
            break;
        }
    }
    if (found && found->kind == KIND_METADATA)
        g_default[0][0] = g_default[1][0] = 0;
    pthread_mutex_unlock(&g_lock);
    if (!found)
        return;
    spa_hook_remove(&found->listener);
    pw.proxy_destroy(found->proxy);
    free(found);
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* the objects are bound in a first round trip and tell their properties in a second one. */
static void core_done(void *data, uint32_t id, int seq)
{
    if (id != PW_ID_CORE || seq != g_sync_seq)
        return;
    pthread_mutex_lock(&g_lock);
    if (++g_sync_rounds < 3)
        g_sync_seq = pw_core_sync(g_core, PW_ID_CORE, 0);
    else
        pthread_cond_broadcast(&g_synced);
    pthread_mutex_unlock(&g_lock);
}

static void core_error(void *data, uint32_t id, int seq, int res, const char *message)
{
    if (id != PW_ID_CORE)
        return;
    /* the server is gone: what it told no longer stands. */
    pthread_mutex_lock(&g_lock);
    g_alive = false;
    pthread_mutex_unlock(&g_lock);
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = core_done,
    .error = core_error,
};

static bool load(void)
{
    void *lib = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!lib)
        return false;
    const struct {
        void *slot;
        const char *name;
    } symbols[] = {
        {&pw.init, "pw_init"},
        {&pw.thread_loop_new, "pw_thread_loop_new"},
        {&pw.thread_loop_start, "pw_thread_loop_start"},
        {&pw.thread_loop_lock, "pw_thread_loop_lock"},
        {&pw.thread_loop_unlock, "pw_thread_loop_unlock"},
        {&pw.thread_loop_get_loop, "pw_thread_loop_get_loop"},
        {&pw.context_new, "pw_context_new"},
        {&pw.context_connect, "pw_context_connect"},
        {&pw.proxy_destroy, "pw_proxy_destroy"},
    };
    for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
        void *address = dlsym(lib, symbols[i].name);
        if (!address)
            return false;
        memcpy(symbols[i].slot, &address, sizeof(address));
    }
    return true;
}

bool mime_pipewire_start(void)
{
    pthread_mutex_lock(&g_lock);
    bool first = !g_started;
    g_started = true;
    pthread_mutex_unlock(&g_lock);
    if (!first)
        return g_alive;
    if (!load())
        return false;
    pw.init(nullptr, nullptr);
    struct pw_thread_loop *loop = pw.thread_loop_new("mime-audio-devices", nullptr);
    struct pw_context *context = loop ? pw.context_new(pw.thread_loop_get_loop(loop), nullptr, 0) : nullptr;
    if (!context || pw.thread_loop_start(loop) < 0)
        return false;
    pw.thread_loop_lock(loop);
    g_core = pw.context_connect(context, nullptr, 0);
    g_registry = g_core ? pw_core_get_registry(g_core, PW_VERSION_REGISTRY, 0) : nullptr;
    if (g_registry) {
        pw_core_add_listener(g_core, &g_core_listener, &core_events, nullptr);
        pw_registry_add_listener(g_registry, &g_registry_listener, &registry_events, nullptr);
        pthread_mutex_lock(&g_lock);
        g_alive = true;
        g_sync_seq = pw_core_sync(g_core, PW_ID_CORE, 0);
        pthread_mutex_unlock(&g_lock);
    }
    pw.thread_loop_unlock(loop);
    if (!g_registry)
        return false;
    /* the first list the guest reads already holds what the server tells. */
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += 1;
    pthread_mutex_lock(&g_lock);
    while (g_sync_rounds < 3 && pthread_cond_timedwait(&g_synced, &g_lock, &until) == 0) {
    }
    pthread_mutex_unlock(&g_lock);
    return true;
}

static uint32_t form_of(const char *type)
{
    static const struct {
        const char *type;
        uint32_t form;
    } forms[] = {
        {"speaker", MIME_AUDIO_FORM_SPEAKERS},   {"internal", MIME_AUDIO_FORM_SPEAKERS},
        {"hifi", MIME_AUDIO_FORM_SPEAKERS},      {"car", MIME_AUDIO_FORM_SPEAKERS},
        {"portable", MIME_AUDIO_FORM_SPEAKERS},  {"headphones", MIME_AUDIO_FORM_HEADPHONES},
        {"headphone", MIME_AUDIO_FORM_HEADPHONES}, {"line", MIME_AUDIO_FORM_LINE},
        {"aux", MIME_AUDIO_FORM_LINE},           {"mic", MIME_AUDIO_FORM_MICROPHONE},
        {"microphone", MIME_AUDIO_FORM_MICROPHONE}, {"webcam", MIME_AUDIO_FORM_MICROPHONE},
        {"headset", MIME_AUDIO_FORM_HEADSET},    {"handsfree", MIME_AUDIO_FORM_HEADSET},
        {"hands-free", MIME_AUDIO_FORM_HEADSET}, {"handset", MIME_AUDIO_FORM_HANDSET},
        {"earpiece", MIME_AUDIO_FORM_HANDSET},   {"phone", MIME_AUDIO_FORM_HANDSET},
        {"spdif", MIME_AUDIO_FORM_SPDIF},        {"hdmi", MIME_AUDIO_FORM_DISPLAY},
        {"tv", MIME_AUDIO_FORM_DISPLAY},         {"video", MIME_AUDIO_FORM_DISPLAY},
    };
    for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); i++)
        if (!strcmp(type, forms[i].type))
            return forms[i].form;
    return MIME_AUDIO_FORM_UNKNOWN;
}

int mime_pipewire_describe(bool capture, uint32_t twin, MimeAudioDevice *d)
{
    int result = -1;
    pthread_mutex_lock(&g_lock);
    Object *node = nullptr;
    for (Object *o = g_alive ? g_objects : nullptr; o && !node; o = o->next) {
        if (o->kind == KIND_NODE && o->capture == capture && !strcmp(o->description, d->name) && !twin--)
            node = o;
    }
    if (node) {
        result = g_default[capture][0] && !strcmp(g_default[capture], node->name);
        if (node->bits) {
            d->bits = node->bits;
            d->is_float = node->is_float;
            d->valid_bits = node->valid_bits;
            if (node->resolution && node->resolution < d->bits && !d->is_float)
                d->valid_bits = node->resolution;
            if (node->rate)
                d->rate = node->rate;
            if (node->channels) {
                d->channels = node->channels;
                d->channel_mask = node->channel_mask;
            }
        }
        const Object *card = nullptr;
        for (const Object *o = g_objects; o && !card; o = o->next)
            if (o->kind == KIND_DEVICE && o->id == node->device)
                card = o;
        if (card) {
            copy(d->adapter, sizeof(d->adapter), card->adapter);
            d->form = form_of(card->form);
            for (uint32_t i = 0; i < card->route_count; i++) {
                const Route *route = &card->routes[i];
                if (route->device != node->route_device || (route->direction == SPA_DIRECTION_INPUT) != capture)
                    continue;
                copy(d->port, sizeof(d->port), route->description);
                if (form_of(route->type) != MIME_AUDIO_FORM_UNKNOWN)
                    d->form = form_of(route->type);
            }
        }
    }
    pthread_mutex_unlock(&g_lock);
    return result;
}

#else

bool mime_pipewire_start(void) { return false; }

int mime_pipewire_describe(bool capture, uint32_t twin, MimeAudioDevice *d) { return -1; }

#endif
