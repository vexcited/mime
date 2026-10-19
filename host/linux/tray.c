/*
 * the notification area and the notifications of the desktop, over its session bus. an icon of the guest is a
 * StatusNotifierItem (org.kde.StatusNotifierItem, what Plasma and most other trays show), each on a connection of its
 * own so that the tray, which watches the connection, drops the icon when the guest removes it. the menu an icon opens is
 * a com.canonical.dbusmenu object, filled by the guest when the tray is about to show it: the program is asked for its
 * context menu, and what it pops up is sent here instead of being shown. a balloon is a notification of
 * org.freedesktop.Notifications. one thread of its own does the bus, so a slow tray never holds the guest's vcpus.
 */
#include "ui_internal.h"

#ifndef MIME_HAVE_SDBUS

uint64_t mime_ui_tray(const MimeTrayCommand *command)
{
    return MIME_HC_STATUS_UNSUPPORTED;
}

#else

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <systemd/sd-bus.h>
#include <time.h>
#include <unistd.h>

#define WATCHER "org.kde.StatusNotifierWatcher"
#define ITEM_PATH "/StatusNotifierItem"
#define MENU_PATH "/MenuBar"
#define ASK_TIMEOUT_MS 800   /* what the guest has to answer a menu request with */
#define CLOSE_DELAY_MS 300   /* a menu that closes may be closing after the click that chose */

typedef struct MenuItem {
    uint32_t parent;
    uint32_t flags;
    char label[256];
} MenuItem;

typedef struct Item {
    struct Item *next;
    uint32_t id;
    sd_bus *bus;
    sd_bus_slot *item_slot, *menu_slot;
    char app[128];
    char tip[1024];
    bool hidden;
    uint32_t width, height;
    uint8_t *pixels;
    uint32_t menu_revision;
    uint32_t menu_count;
    MenuItem *menu;
    sd_bus_message *asking; /* a request to show the menu, waiting for the guest's menu */
    uint64_t asking_deadline;
    uint64_t closed_deadline; /* the menu was closed, unless a choice comes */
    uint32_t notification;    /* the server's number for the balloon on show, 0 for none */
    bool balloon_clicked;
    bool balloon_pending;
} Item;

typedef struct Command {
    struct Command *next;
    MimeTrayCommand command;
} Command;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ready = PTHREAD_COND_INITIALIZER;
static Command *g_queue_head, *g_queue_tail;
static int g_wake = -1;
static int g_state; /* 0 not started, 1 starting, 2 running, 3 no bus */

/* used by the tray thread only. */
static sd_bus *g_bus;
static Item *g_items;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void emit_event(Item *item, uint32_t kind, int32_t x, int32_t y, uint32_t extra)
{
    MimeInputEvent ev = {.type = MIME_INPUT_TRAY, .pad = item->id, .flags = kind, .x = x, .y = y, .ch = extra};
    mime_ui_emit(&ev);
}

static Item *find_item(uint32_t id)
{
    for (Item *i = g_items; i; i = i->next)
        if (i->id == id)
            return i;
    return nullptr;
}

/* ------------------------------------------------------------------ the item */

static int get_string(sd_bus_message *reply, const char *value)
{
    return sd_bus_message_append(reply, "s", value);
}

static int append_pixmaps(sd_bus_message *reply, const Item *item)
{
    int r = sd_bus_message_open_container(reply, 'a', "(iiay)");
    if (r < 0)
        return r;
    if (item->pixels && item->width && item->height) {
        r = sd_bus_message_open_container(reply, 'r', "iiay");
        if (r < 0)
            return r;
        r = sd_bus_message_append(reply, "ii", (int32_t)item->width, (int32_t)item->height);
        if (r < 0)
            return r;
        r = sd_bus_message_append_array(reply, 'y', item->pixels, (size_t)item->width * item->height * 4);
        if (r < 0)
            return r;
        r = sd_bus_message_close_container(reply);
        if (r < 0)
            return r;
    }
    return sd_bus_message_close_container(reply);
}

static int item_property(sd_bus *bus, const char *path, const char *interface, const char *property,
                         sd_bus_message *reply, void *userdata, sd_bus_error *error)
{
    Item *item = userdata;
    if (!strcmp(property, "Category"))
        return get_string(reply, "ApplicationStatus");
    if (!strcmp(property, "Id")) {
        char id[160];
        snprintf(id, sizeof(id), "mime-%d-%u", (int)getpid(), item->id);
        return get_string(reply, id);
    }
    if (!strcmp(property, "Title"))
        return get_string(reply, item->app);
    if (!strcmp(property, "Status"))
        return get_string(reply, item->hidden ? "Passive" : "Active");
    if (!strcmp(property, "WindowId"))
        return sd_bus_message_append(reply, "u", 0u);
    if (!strcmp(property, "IconName") || !strcmp(property, "OverlayIconName") || !strcmp(property, "AttentionIconName") ||
        !strcmp(property, "AttentionMovieName"))
        return get_string(reply, "");
    if (!strcmp(property, "IconPixmap"))
        return append_pixmaps(reply, item);
    if (!strcmp(property, "OverlayIconPixmap") || !strcmp(property, "AttentionIconPixmap")) {
        Item none = {};
        return append_pixmaps(reply, &none);
    }
    if (!strcmp(property, "ToolTip")) {
        /* the first line of the tip is the title, the others the description. */
        char title[1024], text[1024] = "";
        mime_strlcpy(title, item->tip, sizeof(title));
        char *nl = strchr(title, '\n');
        if (nl) {
            *nl = 0;
            mime_strlcpy(text, nl + 1, sizeof(text));
        }
        int r = sd_bus_message_open_container(reply, 'r', "sa(iiay)ss");
        if (r < 0)
            return r;
        r = sd_bus_message_append(reply, "s", "");
        if (r < 0)
            return r;
        Item none = {};
        r = append_pixmaps(reply, &none);
        if (r < 0)
            return r;
        r = sd_bus_message_append(reply, "ss", title, text);
        if (r < 0)
            return r;
        return sd_bus_message_close_container(reply);
    }
    if (!strcmp(property, "ItemIsMenu"))
        return sd_bus_message_append(reply, "b", 0);
    if (!strcmp(property, "Menu"))
        return sd_bus_message_append(reply, "o", MENU_PATH);
    return -ENOENT;
}

static int item_click(sd_bus_message *m, void *userdata, sd_bus_error *error, uint32_t kind)
{
    Item *item = userdata;
    int32_t x = 0, y = 0;
    sd_bus_message_read(m, "ii", &x, &y);
    emit_event(item, kind, x, y, 0);
    return sd_bus_reply_method_return(m, nullptr);
}

static int method_activate(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    return item_click(m, userdata, error, MIME_TRAY_EVENT_ACTIVATE);
}

static int method_context(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    return item_click(m, userdata, error, MIME_TRAY_EVENT_CONTEXT);
}

static int method_secondary(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    return item_click(m, userdata, error, MIME_TRAY_EVENT_SECONDARY);
}

static int method_scroll(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    Item *item = userdata;
    int32_t delta = 0;
    const char *orientation = "";
    sd_bus_message_read(m, "is", &delta, &orientation);
    /* the wheel has steps of 120 units, positive up; a horizontal scroll is not told. */
    if (strcmp(orientation, "vertical") == 0 || strcmp(orientation, "Vertical") == 0)
        emit_event(item, MIME_TRAY_EVENT_SCROLL, 0, 0, (uint32_t)delta);
    return sd_bus_reply_method_return(m, nullptr);
}

static const sd_bus_vtable item_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ContextMenu", "ii", nullptr, method_context, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Activate", "ii", nullptr, method_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SecondaryActivate", "ii", nullptr, method_secondary, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Scroll", "is", nullptr, method_scroll, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Category", "s", item_property, 0, 0),
    SD_BUS_PROPERTY("Id", "s", item_property, 0, 0),
    SD_BUS_PROPERTY("Title", "s", item_property, 0, 0),
    SD_BUS_PROPERTY("Status", "s", item_property, 0, 0),
    SD_BUS_PROPERTY("WindowId", "u", item_property, 0, 0),
    SD_BUS_PROPERTY("IconName", "s", item_property, 0, 0),
    SD_BUS_PROPERTY("IconPixmap", "a(iiay)", item_property, 0, 0),
    SD_BUS_PROPERTY("OverlayIconName", "s", item_property, 0, 0),
    SD_BUS_PROPERTY("OverlayIconPixmap", "a(iiay)", item_property, 0, 0),
    SD_BUS_PROPERTY("AttentionIconName", "s", item_property, 0, 0),
    SD_BUS_PROPERTY("AttentionIconPixmap", "a(iiay)", item_property, 0, 0),
    SD_BUS_PROPERTY("AttentionMovieName", "s", item_property, 0, 0),
    SD_BUS_PROPERTY("ToolTip", "(sa(iiay)ss)", item_property, 0, 0),
    SD_BUS_PROPERTY("ItemIsMenu", "b", item_property, 0, 0),
    SD_BUS_PROPERTY("Menu", "o", item_property, 0, 0),
    SD_BUS_SIGNAL("NewTitle", "", 0),
    SD_BUS_SIGNAL("NewIcon", "", 0),
    SD_BUS_SIGNAL("NewAttentionIcon", "", 0),
    SD_BUS_SIGNAL("NewOverlayIcon", "", 0),
    SD_BUS_SIGNAL("NewToolTip", "", 0),
    SD_BUS_SIGNAL("NewStatus", "s", 0),
    SD_BUS_VTABLE_END,
};

/* ------------------------------------------------------------------ the menu */

/* a label as dbusmenu has it: _ marks the mnemonic where windows has &. */
static void menu_label(char *out, size_t size, const char *label)
{
    size_t n = 0;
    for (const char *p = label; *p && n + 3 < size; p++) {
        if (*p == '&' && p[1] == '&') {
            out[n++] = '&';
            p++;
        } else if (*p == '&') {
            out[n++] = '_';
        } else if (*p == '_') {
            out[n++] = '_';
            out[n++] = '_';
        } else if (*p == '\t') {
            break; /* what follows the tab is a shortcut text, which the desktop draws itself */
        } else {
            out[n++] = *p;
        }
    }
    out[n] = 0;
}

static bool has_children(const Item *item, uint32_t index)
{
    for (uint32_t i = 0; i < item->menu_count; i++)
        if (item->menu[i].parent == index + 1)
            return true;
    return false;
}

static int append_properties(sd_bus_message *m, const Item *item, uint32_t index)
{
    int r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r < 0)
        return r;
    if (index == UINT32_MAX) {
        r = sd_bus_message_append(m, "{sv}", "children-display", "s", "submenu");
        if (r < 0)
            return r;
    } else {
        const MenuItem *mi = &item->menu[index];
        if (mi->flags & MIME_TRAY_ITEM_SEPARATOR) {
            r = sd_bus_message_append(m, "{sv}", "type", "s", "separator");
        } else {
            char label[256];
            menu_label(label, sizeof(label), mi->label);
            r = sd_bus_message_append(m, "{sv}", "label", "s", label);
            if (r >= 0 && (mi->flags & MIME_TRAY_ITEM_DISABLED))
                r = sd_bus_message_append(m, "{sv}", "enabled", "b", 0);
            if (r >= 0 && (mi->flags & (MIME_TRAY_ITEM_CHECKED | MIME_TRAY_ITEM_RADIO))) {
                r = sd_bus_message_append(m, "{sv}", "toggle-type", "s",
                                          (mi->flags & MIME_TRAY_ITEM_RADIO) ? "radio" : "checkmark");
                if (r >= 0)
                    r = sd_bus_message_append(m, "{sv}", "toggle-state", "i", (mi->flags & MIME_TRAY_ITEM_CHECKED) ? 1 : 0);
            }
            if (r >= 0 && has_children(item, index))
                r = sd_bus_message_append(m, "{sv}", "children-display", "s", "submenu");
        }
        if (r < 0)
            return r;
    }
    return sd_bus_message_close_container(m);
}

/* a node and, to the depth asked (-1 for all), its children, as (ia{sv}av). */
static int append_node(sd_bus_message *m, const Item *item, uint32_t index, int depth)
{
    uint32_t id = index == UINT32_MAX ? 0 : index + 1;
    int r = sd_bus_message_open_container(m, 'r', "ia{sv}av");
    if (r < 0)
        return r;
    r = sd_bus_message_append(m, "i", (int32_t)id);
    if (r < 0)
        return r;
    r = append_properties(m, item, index);
    if (r < 0)
        return r;
    r = sd_bus_message_open_container(m, 'a', "v");
    if (r < 0)
        return r;
    if (depth != 0) {
        for (uint32_t i = 0; i < item->menu_count; i++) {
            if (item->menu[i].parent != id)
                continue;
            r = sd_bus_message_open_container(m, 'v', "(ia{sv}av)");
            if (r < 0)
                return r;
            r = append_node(m, item, i, depth - 1);
            if (r < 0)
                return r;
            r = sd_bus_message_close_container(m);
            if (r < 0)
                return r;
        }
    }
    r = sd_bus_message_close_container(m);
    if (r < 0)
        return r;
    return sd_bus_message_close_container(m);
}

static int menu_get_layout(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    Item *item = userdata;
    int32_t parent, depth;
    int r = sd_bus_message_read(m, "ii", &parent, &depth);
    if (r < 0)
        return r;
    /* the names of the properties wanted are not looked at: every property of a node is given. */
    sd_bus_message_skip(m, "as");
    if (parent < 0 || (uint32_t)parent > item->menu_count)
        return sd_bus_error_set(error, SD_BUS_ERROR_INVALID_ARGS, "no such item");
    sd_bus_message *reply;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0)
        return r;
    r = sd_bus_message_append(reply, "u", item->menu_revision);
    if (r >= 0)
        r = append_node(reply, item, parent == 0 ? UINT32_MAX : (uint32_t)parent - 1, depth);
    if (r >= 0)
        r = sd_bus_send(sd_bus_message_get_bus(m), reply, nullptr);
    sd_bus_message_unref(reply);
    return r < 0 ? r : 1;
}

static int menu_group_properties(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    Item *item = userdata;
    int r = sd_bus_message_enter_container(m, 'a', "i");
    if (r < 0)
        return r;
    sd_bus_message *reply;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0)
        return r;
    r = sd_bus_message_open_container(reply, 'a', "(ia{sv})");
    int32_t id;
    while (r >= 0 && sd_bus_message_read(m, "i", &id) > 0) {
        if (id < 0 || (uint32_t)id > item->menu_count)
            continue;
        r = sd_bus_message_open_container(reply, 'r', "ia{sv}");
        if (r >= 0)
            r = sd_bus_message_append(reply, "i", id);
        if (r >= 0)
            r = append_properties(reply, item, id == 0 ? UINT32_MAX : (uint32_t)id - 1);
        if (r >= 0)
            r = sd_bus_message_close_container(reply);
    }
    if (r >= 0)
        r = sd_bus_message_close_container(reply);
    if (r >= 0)
        r = sd_bus_send(sd_bus_message_get_bus(m), reply, nullptr);
    sd_bus_message_unref(reply);
    return r < 0 ? r : 1;
}

static int menu_get_property(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    return sd_bus_error_set(error, SD_BUS_ERROR_INVALID_ARGS, "no such property");
}

/* a click, or the menu opening or closing; the desktop sends the closing of the menu as a closed event of the root. */
static void menu_event(Item *item, int32_t id, const char *event)
{
    if (!strcmp(event, "clicked")) {
        if (id > 0 && (uint32_t)id <= item->menu_count) {
            item->closed_deadline = 0;
            emit_event(item, MIME_TRAY_EVENT_MENU_CHOSEN, 0, 0, (uint32_t)id - 1);
        }
    } else if (!strcmp(event, "closed") && id == 0) {
        item->closed_deadline = now_ms() + CLOSE_DELAY_MS;
    }
}

static int menu_method_event(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    int32_t id;
    const char *event;
    int r = sd_bus_message_read(m, "is", &id, &event);
    if (r < 0)
        return r;
    menu_event(userdata, id, event);
    return sd_bus_reply_method_return(m, nullptr);
}

static int menu_method_event_group(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    int r = sd_bus_message_enter_container(m, 'a', "(isvu)");
    if (r < 0)
        return r;
    for (;;) {
        r = sd_bus_message_enter_container(m, 'r', "isvu");
        if (r <= 0)
            break;
        int32_t id;
        const char *event;
        if (sd_bus_message_read(m, "is", &id, &event) >= 0) {
            sd_bus_message_skip(m, "vu");
            menu_event(userdata, id, event);
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message *reply;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0)
        return r;
    r = sd_bus_message_open_container(reply, 'a', "i");
    if (r >= 0)
        r = sd_bus_message_close_container(reply);
    if (r >= 0)
        r = sd_bus_send(sd_bus_message_get_bus(m), reply, nullptr);
    sd_bus_message_unref(reply);
    return r < 0 ? r : 1;
}

/* the desktop will show the menu: the program is asked for it, and the answer waits for it. */
static int menu_about_to_show(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    Item *item = userdata;
    int32_t id;
    int r = sd_bus_message_read(m, "i", &id);
    if (r < 0)
        return r;
    if (id != 0)
        return sd_bus_reply_method_return(m, "b", 0);
    if (item->asking) {
        sd_bus_message_unref(item->asking);
        item->asking = nullptr;
    }
    item->asking = sd_bus_message_ref(m);
    item->asking_deadline = now_ms() + ASK_TIMEOUT_MS;
    item->closed_deadline = 0;
    emit_event(item, MIME_TRAY_EVENT_MENU_REQUEST, 0, 0, 0);
    return 1;
}

static int menu_about_to_show_group(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    sd_bus_message *reply;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0)
        return r;
    r = sd_bus_message_append(reply, "aiai", 0, 0);
    if (r >= 0)
        r = sd_bus_send(sd_bus_message_get_bus(m), reply, nullptr);
    sd_bus_message_unref(reply);
    return r < 0 ? r : 1;
}

static int menu_property(sd_bus *bus, const char *path, const char *interface, const char *property,
                         sd_bus_message *reply, void *userdata, sd_bus_error *error)
{
    if (!strcmp(property, "Version"))
        return sd_bus_message_append(reply, "u", 3u);
    if (!strcmp(property, "TextDirection"))
        return sd_bus_message_append(reply, "s", "ltr");
    if (!strcmp(property, "Status"))
        return sd_bus_message_append(reply, "s", "normal");
    if (!strcmp(property, "IconThemePath"))
        return sd_bus_message_append(reply, "as", 0);
    return -ENOENT;
}

static const sd_bus_vtable menu_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetLayout", "iias", "u(ia{sv}av)", menu_get_layout, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetGroupProperties", "aias", "a(ia{sv})", menu_group_properties, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetProperty", "is", "v", menu_get_property, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Event", "isvu", nullptr, menu_method_event, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("EventGroup", "a(isvu)", "ai", menu_method_event_group, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AboutToShow", "i", "b", menu_about_to_show, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AboutToShowGroup", "ai", "aiai", menu_about_to_show_group, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Version", "u", menu_property, 0, 0),
    SD_BUS_PROPERTY("TextDirection", "s", menu_property, 0, 0),
    SD_BUS_PROPERTY("Status", "s", menu_property, 0, 0),
    SD_BUS_PROPERTY("IconThemePath", "as", menu_property, 0, 0),
    SD_BUS_SIGNAL("ItemsPropertiesUpdated", "a(ia{sv})a(ias)", 0),
    SD_BUS_SIGNAL("LayoutUpdated", "ui", 0),
    SD_BUS_SIGNAL("ItemActivationRequested", "iu", 0),
    SD_BUS_VTABLE_END,
};

/* ------------------------------------------------------------------ the watcher and the server */

static void register_item(Item *item)
{
    /* the item is at a path of the connection, which is the one the tray watches. */
    sd_bus_call_method_async(item->bus, nullptr, WATCHER, "/StatusNotifierWatcher", WATCHER, "RegisterStatusNotifierItem",
                             nullptr, nullptr, "s", ITEM_PATH);
}

static int watcher_changed(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    const char *name, *old_owner, *new_owner;
    if (sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner) < 0)
        return 0;
    /* a tray that starts after the guest's programs has its icons put in. */
    if (!strcmp(name, WATCHER) && new_owner[0])
        for (Item *i = g_items; i; i = i->next)
            register_item(i);
    return 0;
}

static void create_item_bus(Item *item)
{
    if (sd_bus_open_user(&item->bus) < 0) {
        item->bus = nullptr;
        return;
    }
    sd_bus_add_object_vtable(item->bus, &item->item_slot, ITEM_PATH, "org.kde.StatusNotifierItem", item_vtable, item);
    sd_bus_add_object_vtable(item->bus, &item->menu_slot, MENU_PATH, "com.canonical.dbusmenu", menu_vtable, item);
    register_item(item);
}

static void destroy_item(Item *item)
{
    if (item->asking)
        sd_bus_message_unref(item->asking);
    if (item->notification && g_bus)
        sd_bus_call_method_async(g_bus, nullptr, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                 "org.freedesktop.Notifications", "CloseNotification", nullptr, nullptr, "u",
                                 item->notification);
    if (item->bus) {
        sd_bus_slot_unref(item->item_slot);
        sd_bus_slot_unref(item->menu_slot);
        sd_bus_flush_close_unref(item->bus);
    }
    free(item->pixels);
    free(item->menu);
    free(item);
}

static Item *notification_item(uint32_t notification)
{
    for (Item *i = g_items; i; i = i->next)
        if (i->notification == notification)
            return i;
    return nullptr;
}

static int notification_closed(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    uint32_t id, reason;
    if (sd_bus_message_read(m, "uu", &id, &reason) < 0)
        return 0;
    Item *item = notification_item(id);
    if (!item)
        return 0;
    item->notification = 0;
    if (item->balloon_clicked)
        item->balloon_clicked = false;
    else if (reason == 3)
        emit_event(item, MIME_TRAY_EVENT_BALLOON_HIDDEN, 0, 0, 0);
    else
        emit_event(item, MIME_TRAY_EVENT_BALLOON_TIMEOUT, 0, 0, 0);
    return 0;
}

static int action_invoked(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    uint32_t id;
    const char *action;
    if (sd_bus_message_read(m, "us", &id, &action) < 0)
        return 0;
    Item *item = notification_item(id);
    if (item) {
        item->balloon_clicked = true;
        emit_event(item, MIME_TRAY_EVENT_BALLOON_CLICKED, 0, 0, 0);
    }
    return 0;
}

static int notify_reply(sd_bus_message *reply, void *userdata, sd_bus_error *error)
{
    uint32_t icon = (uint32_t)(uintptr_t)userdata;
    Item *item = find_item(icon);
    uint32_t id = 0;
    if (!item || sd_bus_message_is_method_error(reply, nullptr) || sd_bus_message_read(reply, "u", &id) < 0)
        return 0;
    item->balloon_pending = false;
    item->notification = id;
    item->balloon_clicked = false;
    emit_event(item, MIME_TRAY_EVENT_BALLOON_SHOWN, 0, 0, 0);
    return 0;
}

static void show_balloon(Item *item, const MimeTrayCommand *c)
{
    char title[256], text[1024];
    mime_utf16_to_utf8(title, sizeof(title), c->balloon_title, 64);
    mime_utf16_to_utf8(text, sizeof(text), c->balloon_text, 256);
    if (!text[0] && !title[0]) {
        /* an empty one takes the balloon away. */
        if (item->notification)
            sd_bus_call_method_async(g_bus, nullptr, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                     "org.freedesktop.Notifications", "CloseNotification", nullptr, nullptr, "u",
                                     item->notification);
        return;
    }
    sd_bus_message *m;
    if (sd_bus_message_new_method_call(g_bus, &m, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                       "org.freedesktop.Notifications", "Notify") < 0)
        return;
    /* the balloon takes the place of the one before it. */
    int r = sd_bus_message_append(m, "susss", item->app, item->notification, "", title[0] ? title : item->app,
                                  text);
    /* the whole balloon is a click: it is the default action. */
    if (r >= 0)
        r = sd_bus_message_append(m, "as", 2, "default", "");
    if (r >= 0)
        r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) {
        uint8_t urgency = (c->flags & MIME_TRAY_ERROR) ? 2 : (c->flags & MIME_TRAY_WARNING) ? 1 : 1;
        r = sd_bus_message_append(m, "{sv}", "urgency", "y", urgency);
        if (r >= 0 && (c->flags & MIME_TRAY_SILENT))
            r = sd_bus_message_append(m, "{sv}", "suppress-sound", "b", 1);
        if (r >= 0 && c->balloon_width && c->balloon_height && c->balloon_width <= MIME_TRAY_PICTURE_MAX &&
            c->balloon_height <= MIME_TRAY_PICTURE_MAX) {
            /* the picture as the specification has it: rows of r, g, b, a. */
            size_t n = (size_t)c->balloon_width * c->balloon_height;
            uint8_t *rgba = mime_xmalloc(n * 4);
            for (size_t i = 0; i < n; i++) {
                rgba[i * 4] = c->balloon_pixels[i * 4 + 1];
                rgba[i * 4 + 1] = c->balloon_pixels[i * 4 + 2];
                rgba[i * 4 + 2] = c->balloon_pixels[i * 4 + 3];
                rgba[i * 4 + 3] = c->balloon_pixels[i * 4];
            }
            r = sd_bus_message_open_container(m, 'e', "sv");
            if (r >= 0)
                r = sd_bus_message_append(m, "s", "image-data");
            if (r >= 0)
                r = sd_bus_message_open_container(m, 'v', "(iiibiiay)");
            if (r >= 0)
                r = sd_bus_message_open_container(m, 'r', "iiibiiay");
            if (r >= 0)
                r = sd_bus_message_append(m, "iiibii", (int32_t)c->balloon_width, (int32_t)c->balloon_height,
                                          (int32_t)c->balloon_width * 4, 1, 8, 4);
            if (r >= 0)
                r = sd_bus_message_append_array(m, 'y', rgba, n * 4);
            if (r >= 0)
                r = sd_bus_message_close_container(m);
            if (r >= 0)
                r = sd_bus_message_close_container(m);
            if (r >= 0)
                r = sd_bus_message_close_container(m);
            free(rgba);
        }
    }
    if (r >= 0)
        r = sd_bus_message_close_container(m);
    if (r >= 0)
        r = sd_bus_message_append(m, "i", -1);
    if (r >= 0) {
        item->balloon_pending = true;
        sd_bus_call_async(g_bus, nullptr, m, notify_reply, (void *)(uintptr_t)item->id, 2 * 1000 * 1000);
    }
    sd_bus_message_unref(m);
}

/* ------------------------------------------------------------------ the commands */

static void set_menu(Item *item, const MimeTrayCommand *c)
{
    uint32_t count = c->menu_count < MIME_TRAY_MENU_MAX ? c->menu_count : MIME_TRAY_MENU_MAX;
    free(item->menu);
    item->menu = count ? mime_xcalloc(count, sizeof(MenuItem)) : nullptr;
    item->menu_count = count;
    for (uint32_t i = 0; i < count; i++) {
        item->menu[i].parent = c->menu[i].parent;
        item->menu[i].flags = c->menu[i].flags;
        mime_utf16_to_utf8(item->menu[i].label, sizeof(item->menu[i].label), c->menu[i].label, 64);
    }
    item->menu_revision++;
    if (item->bus) {
        sd_bus_emit_signal(item->bus, MENU_PATH, "com.canonical.dbusmenu", "LayoutUpdated", "ui", item->menu_revision, 0);
    }
    if (item->asking) {
        sd_bus_reply_method_return(item->asking, "b", 1);
        sd_bus_message_unref(item->asking);
        item->asking = nullptr;
    }
}

static void set_icon(Item *item, const MimeTrayCommand *c)
{
    char app[128], tip[1024];
    mime_utf16_to_utf8(app, sizeof(app), c->app, 64);
    mime_utf16_to_utf8(tip, sizeof(tip), c->tip, 128);
    bool new_item = !item->bus;
    bool title_changed = strcmp(app, item->app) != 0;
    bool tip_changed = (c->flags & MIME_TRAY_HAS_TIP) && strcmp(tip, item->tip) != 0;
    bool hidden = (c->flags & MIME_TRAY_HIDDEN) != 0;
    bool status_changed = hidden != item->hidden;
    mime_strlcpy(item->app, app, sizeof(item->app));
    if (c->flags & MIME_TRAY_HAS_TIP)
        mime_strlcpy(item->tip, tip, sizeof(item->tip));
    item->hidden = hidden;
    bool icon_changed = false;
    if ((c->flags & MIME_TRAY_HAS_PICTURE) && c->width && c->height && c->width <= MIME_TRAY_PICTURE_MAX &&
        c->height <= MIME_TRAY_PICTURE_MAX) {
        size_t size = (size_t)c->width * c->height * 4;
        icon_changed = item->width != c->width || item->height != c->height || !item->pixels ||
                       memcmp(item->pixels, c->pixels, size) != 0;
        if (icon_changed) {
            free(item->pixels);
            item->pixels = mime_xmalloc(size);
            memcpy(item->pixels, c->pixels, size);
            item->width = c->width;
            item->height = c->height;
        }
    }
    if (new_item) {
        create_item_bus(item);
        return;
    }
    if (!item->bus)
        return;
    if (title_changed)
        sd_bus_emit_signal(item->bus, ITEM_PATH, "org.kde.StatusNotifierItem", "NewTitle", "");
    if (icon_changed)
        sd_bus_emit_signal(item->bus, ITEM_PATH, "org.kde.StatusNotifierItem", "NewIcon", "");
    if (tip_changed)
        sd_bus_emit_signal(item->bus, ITEM_PATH, "org.kde.StatusNotifierItem", "NewToolTip", "");
    if (status_changed)
        sd_bus_emit_signal(item->bus, ITEM_PATH, "org.kde.StatusNotifierItem", "NewStatus", "s",
                           hidden ? "Passive" : "Active");
}

static void run_command(const MimeTrayCommand *c)
{
    Item *item = find_item(c->id);
    switch (c->op) {
    case MIME_TRAY_SET:
        if (!item) {
            item = mime_xcalloc(1, sizeof(*item));
            item->id = c->id;
            item->next = g_items;
            g_items = item;
        }
        set_icon(item, c);
        break;
    case MIME_TRAY_REMOVE:
        for (Item **link = &g_items; *link; link = &(*link)->next) {
            if (*link == item) {
                *link = item->next;
                destroy_item(item);
                break;
            }
        }
        break;
    case MIME_TRAY_BALLOON:
        if (item)
            show_balloon(item, c);
        break;
    case MIME_TRAY_MENU:
        if (item)
            set_menu(item, c);
        break;
    }
}

/* the timers of the items: a menu request the guest did not answer, a menu that closed. */
static uint64_t run_timers(void)
{
    uint64_t now = now_ms(), next = UINT64_MAX;
    for (Item *i = g_items; i; i = i->next) {
        if (i->asking) {
            if (now >= i->asking_deadline) {
                sd_bus_reply_method_return(i->asking, "b", 0);
                sd_bus_message_unref(i->asking);
                i->asking = nullptr;
            } else if (i->asking_deadline < next) {
                next = i->asking_deadline;
            }
        }
        if (i->closed_deadline) {
            if (now >= i->closed_deadline) {
                i->closed_deadline = 0;
                emit_event(i, MIME_TRAY_EVENT_MENU_CLOSED, 0, 0, 0);
            } else if (i->closed_deadline < next) {
                next = i->closed_deadline;
            }
        }
    }
    return next;
}

static void *tray_thread(void *arg)
{
    bool ok = sd_bus_open_user(&g_bus) >= 0;
    if (ok) {
        sd_bus_add_match(g_bus, nullptr,
                         "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
                         "member='NameOwnerChanged',arg0='" WATCHER "'",
                         watcher_changed, nullptr);
        sd_bus_match_signal(g_bus, nullptr, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                            "org.freedesktop.Notifications", "NotificationClosed", notification_closed, nullptr);
        sd_bus_match_signal(g_bus, nullptr, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                            "org.freedesktop.Notifications", "ActionInvoked", action_invoked, nullptr);
    }
    pthread_mutex_lock(&g_lock);
    g_state = ok ? 2 : 3;
    pthread_cond_broadcast(&g_ready);
    pthread_mutex_unlock(&g_lock);
    if (!ok) {
        mime_info("no desktop bus: no notification icons or notifications");
        return nullptr;
    }
    for (;;) {
        /* the commands of the guest first, in order. */
        for (;;) {
            pthread_mutex_lock(&g_lock);
            Command *c = g_queue_head;
            if (c) {
                g_queue_head = c->next;
                if (!g_queue_head)
                    g_queue_tail = nullptr;
            }
            pthread_mutex_unlock(&g_lock);
            if (!c)
                break;
            run_command(&c->command);
            free(c);
        }
        uint64_t next = run_timers();
        int n = 0;
        struct pollfd fds[2 + 64];
        sd_bus *buses[2 + 64];
        fds[n] = (struct pollfd){.fd = g_wake, .events = POLLIN};
        buses[n++] = nullptr;
        int r = sd_bus_get_fd(g_bus);
        fds[n] = (struct pollfd){.fd = r, .events = (short)sd_bus_get_events(g_bus)};
        buses[n++] = g_bus;
        for (Item *i = g_items; i && n < 2 + 64; i = i->next) {
            if (!i->bus)
                continue;
            fds[n] = (struct pollfd){.fd = sd_bus_get_fd(i->bus), .events = (short)sd_bus_get_events(i->bus)};
            buses[n++] = i->bus;
        }
        uint64_t usec = UINT64_MAX;
        sd_bus_get_timeout(g_bus, &usec);
        uint64_t timeout_ms = 1000;
        if (usec != UINT64_MAX) {
            uint64_t now_us = now_ms() * 1000;
            /* the bus' timeout is on the monotonic clock in microseconds. */
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            now_us = (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
            timeout_ms = usec > now_us ? (usec - now_us + 999) / 1000 : 0;
        }
        if (next != UINT64_MAX) {
            uint64_t d = next > now_ms() ? next - now_ms() : 0;
            if (d < timeout_ms)
                timeout_ms = d;
        }
        if (timeout_ms > 1000)
            timeout_ms = 1000;
        poll(fds, (nfds_t)n, (int)timeout_ms);
        if (fds[0].revents & POLLIN) {
            uint64_t v;
            if (read(g_wake, &v, sizeof(v)) < 0)
                ;
        }
        for (int i = 1; i < n; i++)
            while (sd_bus_process(buses[i], nullptr) > 0)
                ;
    }
    return nullptr;
}

static bool ensure_thread(void)
{
    pthread_mutex_lock(&g_lock);
    if (g_state == 0) {
        g_state = 1;
        g_wake = eventfd(0, EFD_CLOEXEC);
        pthread_t t;
        if (g_wake < 0 || pthread_create(&t, nullptr, tray_thread, nullptr) != 0) {
            g_state = 3;
        } else {
            pthread_detach(t);
        }
    }
    while (g_state == 1)
        pthread_cond_wait(&g_ready, &g_lock);
    bool running = g_state == 2;
    pthread_mutex_unlock(&g_lock);
    return running;
}

uint64_t mime_ui_tray(const MimeTrayCommand *command)
{
    if (command->op < MIME_TRAY_SET || command->op > MIME_TRAY_MENU)
        return MIME_HC_STATUS_INVALID;
    if (!ensure_thread())
        return MIME_HC_STATUS_UNSUPPORTED;
    Command *c = mime_xmalloc(sizeof(*c));
    c->next = nullptr;
    memcpy(&c->command, command, sizeof(*command));
    pthread_mutex_lock(&g_lock);
    if (g_queue_tail)
        g_queue_tail->next = c;
    else
        g_queue_head = c;
    g_queue_tail = c;
    pthread_mutex_unlock(&g_lock);
    uint64_t one = 1;
    if (write(g_wake, &one, sizeof(one)) < 0)
        ;
    return MIME_HC_STATUS_OK;
}

#endif
