/*
 * guest toasts through org.freedesktop.Notifications on a dedicated bus thread, so a slow server never blocks vCPUs.
 * titles, bodies, images and buttons map to notifications; supported servers provide inline replies. actions return
 * as MIME_INPUT_SHELL.
 */
#include "ui_internal.h"

/* guest shell requests: Windows, launcher entries and toast notifications. */
static uint64_t toast_command(void *command, size_t size, uint64_t *value);

uint64_t mime_ui_shell(void *command, size_t size, uint64_t *value)
{
    uint32_t op = *(uint32_t *)command;
    switch (op) {
    case MIME_SHELL_WINDOW:
    case MIME_SHELL_LAUNCHER:
    case MIME_SHELL_FLASH:
        return size >= sizeof(MimeShellWindow) ? mime_ui_shell_window(command) : MIME_HC_STATUS_INVALID;
    case MIME_SHELL_JUMPLIST:
        if (size < sizeof(MimeShellJumpList))
            return MIME_HC_STATUS_INVALID;
        mime_desktop_jump_list(command);
        return MIME_HC_STATUS_OK;
    case MIME_SHELL_DIALOG:
        return size >= sizeof(MimeShellDialog) ? mime_ui_dialog(command) : MIME_HC_STATUS_INVALID;
    default:
        return toast_command(command, size, value);
    }
}

#ifndef MIME_HAVE_SDBUS

static uint64_t toast_command(void *command, size_t size, uint64_t *value)
{
    return MIME_HC_STATUS_UNSUPPORTED;
}

bool mime_shell_job(const MimeShellDialog *dialog, const char *text)
{
    return false;
}

void mime_shell_launcher_update(const MimeLauncherUpdate *update)
{
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

#define NOTIFICATIONS "org.freedesktop.Notifications"
#define NOTIFICATIONS_PATH "/org/freedesktop/Notifications"

typedef struct Toast {
    struct Toast *next;
    uint32_t id;         /* the guest's number. */
    uint32_t server_id;  /* the server's, 0 until it answers. */
    uint32_t process_id;
    char aumid[256];
    char tag[128];
    char group[128];
    bool answered;       /* the person acted on it: its closing is no news. */
    bool hide;           /* the program took it away before the server had answered. */
} Toast;

/* reply text retained for the guest, protected by the lock. */
#define REPLIES_MAX 16
static struct {
    uint32_t id, length;
    uint16_t units[256];
} g_replies[REPLIES_MAX];
static uint32_t g_reply_next;

typedef enum CommandKind { COMMAND_TOAST, COMMAND_LAUNCHER, COMMAND_JOB } CommandKind;

typedef struct Command {
    struct Command *next;
    CommandKind kind;
    union {
        MimeShellToast toast;
        MimeLauncherUpdate update;
        struct {
            MimeShellDialog dialog;
            char text[1024];
        } job;
    };
} Command;

/* the progress views of plasma's job server, the one dolphin shows copies in. */
#define JOB_SERVER "org.kde.JobViewServer"
#define JOBS_MAX 16

typedef struct Job {
    uint32_t id;
    char path[128];
} Job;

static Job g_jobs[JOBS_MAX];
static bool g_job_server;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ready = PTHREAD_COND_INITIALIZER;
static Command *g_queue_head, *g_queue_tail;
static int g_wake = -1;
static int g_state; /* 0 not started, 1 starting, 2 running, 3 no bus. */
static bool g_server;       /* a notification server is on the bus. */
static bool g_markup;       /* and it shows markup in the body. */
static bool g_inline_reply; /* and has the reply box of Plasma. */
static bool g_body_images;

/* used by the shell thread only, except the replies which the lock covers. */
static sd_bus *g_bus;
static Toast *g_toasts;

static void emit(uint32_t kind, uint32_t id, int32_t arg, uint32_t length)
{
    MimeInputEvent ev = {.type = MIME_INPUT_SHELL, .pad = id, .flags = kind, .x = arg, .y = (int32_t)length};
    mime_ui_emit(&ev);
}

static Toast *find_server(uint32_t server_id)
{
    for (Toast *t = g_toasts; t; t = t->next)
        if (t->server_id == server_id && server_id)
            return t;
    return nullptr;
}

static Toast *find_id(uint32_t id)
{
    for (Toast *t = g_toasts; t; t = t->next)
        if (t->id == id)
            return t;
    return nullptr;
}

static void forget(Toast *toast)
{
    for (Toast **p = &g_toasts; *p; p = &(*p)->next)
        if (*p == toast) {
            *p = toast->next;
            break;
        }
    free(toast);
}

/* ------------------------------------------------------------------ the server */

static void read_capabilities(void)
{
    g_markup = g_inline_reply = g_body_images = false;
    sd_bus_message *reply = nullptr;
    if (sd_bus_call_method(g_bus, NOTIFICATIONS, NOTIFICATIONS_PATH, NOTIFICATIONS, "GetCapabilities", nullptr, &reply,
                           "") < 0)
        return;
    if (sd_bus_message_enter_container(reply, 'a', "s") >= 0) {
        const char *name;
        while (sd_bus_message_read(reply, "s", &name) > 0) {
            if (!strcmp(name, "body-markup"))
                g_markup = true;
            else if (!strcmp(name, "inline-reply"))
                g_inline_reply = true;
            else if (!strcmp(name, "body-images"))
                g_body_images = true;
        }
    }
    sd_bus_message_unref(reply);
}

static bool owner_of_name(const char *name)
{
    sd_bus_message *reply = nullptr;
    int has = 0;
    if (sd_bus_call_method(g_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
                           nullptr, &reply, "s", name) >= 0)
        sd_bus_message_read(reply, "b", &has);
    sd_bus_message_unref(reply);
    return has != 0;
}

static bool name_has_owner(void)
{
    return owner_of_name(NOTIFICATIONS);
}

static int owner_changed(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    const char *name, *old_owner, *new_owner;
    if (sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner) < 0 || strcmp(name, NOTIFICATIONS))
        return 0;
    bool present = new_owner[0] != 0;
    if (present)
        read_capabilities();
    pthread_mutex_lock(&g_lock);
    g_server = present;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

/* ------------------------------------------------------------------ the events */

static int closed_signal(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    uint32_t id, reason;
    if (sd_bus_message_read(m, "uu", &id, &reason) < 0)
        return 0;
    Toast *t = find_server(id);
    if (!t)
        return 0;
    if (!t->answered) {
        /* 1 expired, 2 dismissed by the person, 3 closed by the program, 4 unknown. */
        int32_t why = reason == 1 ? 2 : reason == 3 ? 1 : 0;
        emit(MIME_SHELL_EVENT_TOAST_DISMISSED, t->id, why, 0);
    }
    forget(t);
    return 0;
}

/* the server supplies an activation token before the click action, allowing the target window to be raised. */
static int token_signal(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    uint32_t id;
    const char *token;
    if (sd_bus_message_read(m, "us", &id, &token) >= 0 && find_server(id))
        mime_ui_set_activation_token(token);
    return 0;
}

static int action_signal(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    uint32_t id;
    const char *key;
    if (sd_bus_message_read(m, "us", &id, &key) < 0)
        return 0;
    Toast *t = find_server(id);
    if (!t || t->answered)
        return 0;
    int32_t arg;
    if (!strcmp(key, "default"))
        arg = -1;
    else if (!strcmp(key, "inline-reply"))
        return 0;
    else
        arg = atoi(key);
    t->answered = true;
    emit(MIME_SHELL_EVENT_TOAST_ACTIVATED, t->id, arg, 0);
    return 0;
}

static int replied_signal(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    uint32_t id;
    const char *text;
    if (sd_bus_message_read(m, "us", &id, &text) < 0)
        return 0;
    Toast *t = find_server(id);
    if (!t || t->answered)
        return 0;
    /* retains reply text as UTF-16 for guest retrieval. */
    size_t n = strlen(text);
    uint16_t units[256];
    uint32_t count = 0;
    for (size_t i = 0; i < n && count < 255;) {
        unsigned char c = (unsigned char)text[i];
        uint32_t cp;
        size_t len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (i + len > n) {
            break;
        }
        cp = len == 1 ? c : len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (size_t k = 1; k < len; k++)
            cp = (cp << 6) | ((unsigned char)text[i + k] & 0x3Fu);
        i += len;
        if (cp >= 0x10000) {
            if (count + 2 > 255)
                break;
            cp -= 0x10000;
            units[count++] = (uint16_t)(0xD800 + (cp >> 10));
            units[count++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
        } else {
            units[count++] = (uint16_t)cp;
        }
    }
    pthread_mutex_lock(&g_lock);
    uint32_t slot = g_reply_next++ % REPLIES_MAX;
    g_replies[slot].id = t->id;
    g_replies[slot].length = count;
    memcpy(g_replies[slot].units, units, count * sizeof(uint16_t));
    pthread_mutex_unlock(&g_lock);
    t->answered = true;
    emit(MIME_SHELL_EVENT_TOAST_REPLIED, t->id, 0, count);
    return 0;
}

/* ------------------------------------------------------------------ showing */

static void close_server(uint32_t server_id)
{
    if (server_id)
        sd_bus_call_method_async(g_bus, nullptr, NOTIFICATIONS, NOTIFICATIONS_PATH, NOTIFICATIONS, "CloseNotification",
                                 nullptr, nullptr, "u", server_id);
}

static int notify_reply(sd_bus_message *reply, void *userdata, sd_bus_error *error)
{
    uint32_t id = (uint32_t)(uintptr_t)userdata;
    Toast *t = find_id(id);
    uint32_t server_id = 0;
    if (sd_bus_message_is_method_error(reply, nullptr) || sd_bus_message_read(reply, "u", &server_id) < 0) {
        if (t) {
            emit(MIME_SHELL_EVENT_TOAST_FAILED, t->id, (int32_t)0x80004005, 0);
            forget(t);
        }
        return 0;
    }
    if (!t) {
        close_server(server_id);
        return 0;
    }
    t->server_id = server_id;
    if (t->hide)
        close_server(server_id);
    return 0;
}

/* resolves a guest path to an existing host path, or an empty string. */
static bool host_picture(const uint16_t *units, char *out, size_t size)
{
    char guest[1024];
    mime_utf16_to_utf8(guest, sizeof(guest), units, 260);
    if (!guest[0])
        return false;
    const MimePaths *paths = mime_daemon_paths();
    return paths && mime_paths_to_host(paths, guest, out, size) && mime_file_exists(out);
}

/* escapes markup-sensitive characters in notification text. */
static void body_text(char *out, size_t size, const char *text)
{
    size_t k = 0;
    for (; *text && k + 7 < size; text++) {
        const char *entity = nullptr;
        if (g_markup) {
            if (*text == '&')
                entity = "&amp;";
            else if (*text == '<')
                entity = "&lt;";
            else if (*text == '>')
                entity = "&gt;";
        }
        if (entity) {
            size_t n = strlen(entity);
            memcpy(out + k, entity, n);
            k += n;
        } else {
            out[k++] = *text;
        }
    }
    out[k] = 0;
}

static void show_toast(const MimeShellToast *c)
{
    char aumid[256], tag[128], group[128], app[128], title[512], text[2048], body[4200], placeholder[128];
    mime_utf16_to_utf8(aumid, sizeof(aumid), c->aumid, 128);
    mime_utf16_to_utf8(tag, sizeof(tag), c->tag, 64);
    mime_utf16_to_utf8(group, sizeof(group), c->group, 64);
    mime_utf16_to_utf8(app, sizeof(app), c->app, 64);
    mime_utf16_to_utf8(title, sizeof(title), c->title, 256);
    mime_utf16_to_utf8(text, sizeof(text), c->body, 1024);
    mime_utf16_to_utf8(placeholder, sizeof(placeholder), c->placeholder, 64);
    body_text(body, sizeof(body), text);

    /* a toast of the same application, tag and group replaces the one before it. */
    uint32_t replaces = 0;
    if (tag[0]) {
        for (Toast *t = g_toasts; t; t = t->next)
            if (!strcmp(t->aumid, aumid) && !strcmp(t->tag, tag) && !strcmp(t->group, group)) {
                replaces = t->server_id;
                forget(t);
                break;
            }
    }
    Toast *toast = calloc(1, sizeof(*toast));
    if (!toast)
        return;
    toast->id = c->id;
    toast->process_id = c->process_id;
    mime_strlcpy(toast->aumid, aumid, sizeof(toast->aumid));
    mime_strlcpy(toast->tag, tag, sizeof(toast->tag));
    mime_strlcpy(toast->group, group, sizeof(toast->group));
    toast->next = g_toasts;
    g_toasts = toast;

    sd_bus_message *m;
    if (sd_bus_message_new_method_call(g_bus, &m, NOTIFICATIONS, NOTIFICATIONS_PATH, NOTIFICATIONS, "Notify") < 0) {
        emit(MIME_SHELL_EVENT_TOAST_FAILED, c->id, (int32_t)0x8007000E, 0);
        forget(toast);
        return;
    }
    int r = sd_bus_message_append(m, "susss", app[0] ? app : "Mime", replaces, "", title, body);
    /* the whole toast is the default action, then the buttons by their numbers. */
    if (r >= 0)
        r = sd_bus_message_open_container(m, 'a', "s");
    if (r >= 0)
        r = sd_bus_message_append(m, "ss", "default", "");
    bool reply_box = (c->flags & MIME_TOAST_REPLY) && g_inline_reply;
    for (uint32_t i = 0; r >= 0 && i < c->action_count && i < MIME_TOAST_ACTIONS; i++) {
        char key[16], label[256];
        snprintf(key, sizeof(key), "%u", i);
        mime_utf16_to_utf8(label, sizeof(label), c->actions[i].label, 64);
        /* the first button of a reply box is the box's own button, which the server labels itself. */
        if (reply_box && i == 0)
            r = sd_bus_message_append(m, "ss", "inline-reply", label[0] ? label : "Reply");
        else
            r = sd_bus_message_append(m, "ss", key, label);
    }
    if (r >= 0)
        r = sd_bus_message_close_container(m);
    if (r >= 0)
        r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0)
        r = sd_bus_message_append(m, "{sv}", "urgency", "y", (c->flags & MIME_TOAST_URGENT) ? 2 : 1);
    if (r >= 0 && (c->flags & MIME_TOAST_SILENT))
        r = sd_bus_message_append(m, "{sv}", "suppress-sound", "b", 1);
    if (r >= 0 && (c->flags & (MIME_TOAST_URGENT | MIME_TOAST_RESIDENT)))
        r = sd_bus_message_append(m, "{sv}", "resident", "b", 1);
    if (r >= 0 && reply_box && placeholder[0])
        r = sd_bus_message_append(m, "{sv}", "x-kde-reply-placeholder-text", "s", placeholder);
    char picture[MIME_PATH_MAX];
    if (r >= 0 &&
        (host_picture(c->logo, picture, sizeof(picture)) || host_picture(c->hero, picture, sizeof(picture)) ||
         host_picture(c->picture, picture, sizeof(picture))))
        r = sd_bus_message_append(m, "{sv}", "image-path", "s", picture);
    if (r >= 0)
        r = sd_bus_message_close_container(m);
    int32_t timeout = (c->flags & (MIME_TOAST_URGENT | MIME_TOAST_RESIDENT)) ? 0 : c->timeout_ms ? (int32_t)c->timeout_ms : -1;
    if (r >= 0)
        r = sd_bus_message_append(m, "i", timeout);
    if (r >= 0)
        r = sd_bus_call_async(g_bus, nullptr, m, notify_reply, (void *)(uintptr_t)c->id, 2 * 1000 * 1000);
    sd_bus_message_unref(m);
    if (r < 0) {
        emit(MIME_SHELL_EVENT_TOAST_FAILED, c->id, (int32_t)0x80004005, 0);
        forget(toast);
    }
}

static void hide_toast(const MimeShellToast *c)
{
    Toast *t = find_id(c->id);
    if (!t)
        return;
    if (t->server_id)
        close_server(t->server_id);
    else
        t->hide = true;
}

/* selects an application's toasts, optionally matching tag and group. */
static void remove_toasts(const MimeShellToast *c)
{
    char aumid[256], tag[128], group[128];
    mime_utf16_to_utf8(aumid, sizeof(aumid), c->aumid, 128);
    mime_utf16_to_utf8(tag, sizeof(tag), c->tag, 64);
    mime_utf16_to_utf8(group, sizeof(group), c->group, 64);
    for (Toast *t = g_toasts; t; t = t->next) {
        if (strcmp(t->aumid, aumid) || (tag[0] && strcmp(t->tag, tag)) || (group[0] && strcmp(t->group, group)))
            continue;
        if (t->server_id)
            close_server(t->server_id);
        else
            t->hide = true;
    }
}

/* publishes launcher properties through Unity-compatible signals used by Plasma and other docks. */
static void run_launcher(const MimeLauncherUpdate *u)
{
    sd_bus_message *m;
    char path[96];
    uint32_t h = 2166136261u;
    for (const char *p = u->uri; *p; p++)
        h = (h ^ (uint8_t)*p) * 16777619u;
    snprintf(path, sizeof(path), "/com/canonical/unity/launcherentry/%u", h);
    if (sd_bus_message_new_signal(g_bus, &m, path, "com.canonical.Unity.LauncherEntry", "Update") < 0)
        return;
    int r = sd_bus_message_append(m, "s", u->uri);
    if (r >= 0)
        r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0 && u->progress) {
        r = sd_bus_message_append(m, "{sv}", "progress-visible", "b", (int)u->progress_visible);
        if (r >= 0)
            r = sd_bus_message_append(m, "{sv}", "progress", "d", u->progress_value);
    }
    if (r >= 0 && u->count) {
        r = sd_bus_message_append(m, "{sv}", "count-visible", "b", u->count_value != 0);
        if (r >= 0)
            r = sd_bus_message_append(m, "{sv}", "count", "x", u->count_value);
    }
    if (r >= 0 && u->urgent)
        r = sd_bus_message_append(m, "{sv}", "urgent", "b", (int)u->urgent_value);
    if (r >= 0)
        r = sd_bus_message_close_container(m);
    if (r >= 0)
        sd_bus_send(g_bus, m, nullptr);
    sd_bus_message_unref(m);
}

static void run_command(const MimeShellToast *c)
{
    switch (c->op) {
    case MIME_SHELL_TOAST_SHOW:
        show_toast(c);
        break;
    case MIME_SHELL_TOAST_HIDE:
        hide_toast(c);
        break;
    case MIME_SHELL_TOAST_REMOVE:
        remove_toasts(c);
        break;
    }
}

/* ------------------------------------------------------------------ the thread */

static Job *find_job(uint32_t id)
{
    for (int i = 0; i < JOBS_MAX; i++)
        if (g_jobs[i].id == id)
            return &g_jobs[i];
    return nullptr;
}

static void run_job(const MimeShellDialog *d, const char *text)
{
    char title[256];
    mime_utf16_to_utf8(title, sizeof(title), d->title, SIZE_MAX);
    Job *job = find_job(d->id);
    if (d->kind == MIME_DIALOG_PROGRESS && !job) {
        job = find_job(0);
        sd_bus_message *reply = nullptr;
        const char *path = nullptr;
        if (!job || sd_bus_call_method(g_bus, JOB_SERVER, "/JobViewServer", JOB_SERVER, "requestView", nullptr, &reply,
                                       "ssi", title, "system-software-install", 0) < 0 ||
            sd_bus_message_read(reply, "o", &path) < 0) {
            sd_bus_message_unref(reply);
            return;
        }
        job->id = d->id;
        mime_strlcpy(job->path, path, sizeof(job->path));
        sd_bus_message_unref(reply);
    }
    if (!job)
        return;
    if (d->kind == MIME_DIALOG_END) {
        sd_bus_call_method(g_bus, JOB_SERVER, job->path, "org.kde.JobViewV2", "terminate", nullptr, nullptr, "s", "");
        job->id = 0;
        return;
    }
    char instruction[512];
    mime_utf16_to_utf8(instruction, sizeof(instruction), d->instruction, SIZE_MAX);
    sd_bus_call_method(g_bus, JOB_SERVER, job->path, "org.kde.JobViewV2", "setInfoMessage", nullptr, nullptr, "s",
                       instruction);
    sd_bus_call_method(g_bus, JOB_SERVER, job->path, "org.kde.JobViewV2", "setDescriptionField", nullptr, nullptr, "uss",
                       0u, title, text);
    if (d->permille >= 0)
        sd_bus_call_method(g_bus, JOB_SERVER, job->path, "org.kde.JobViewV2", "setPercent", nullptr, nullptr, "u",
                           (uint32_t)(d->permille / 10));
}

static void *shell_thread(void *arg)
{
    mime_set_thread_name("mime-shell");
    bool ok = sd_bus_open_user(&g_bus) >= 0;
    if (ok) {
        sd_bus_match_signal(g_bus, nullptr, NOTIFICATIONS, NOTIFICATIONS_PATH, NOTIFICATIONS, "NotificationClosed",
                            closed_signal, nullptr);
        sd_bus_match_signal(g_bus, nullptr, NOTIFICATIONS, NOTIFICATIONS_PATH, NOTIFICATIONS, "ActionInvoked",
                            action_signal, nullptr);
        sd_bus_match_signal(g_bus, nullptr, NOTIFICATIONS, NOTIFICATIONS_PATH, NOTIFICATIONS, "ActivationToken",
                            token_signal, nullptr);
        sd_bus_match_signal(g_bus, nullptr, NOTIFICATIONS, NOTIFICATIONS_PATH, NOTIFICATIONS, "NotificationReplied",
                            replied_signal, nullptr);
        sd_bus_add_match(g_bus, nullptr,
                         "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
                         "member='NameOwnerChanged',arg0='" NOTIFICATIONS "'",
                         owner_changed, nullptr);
        bool present = name_has_owner();
        if (present)
            read_capabilities();
        bool jobs = owner_of_name(JOB_SERVER);
        pthread_mutex_lock(&g_lock);
        g_server = present;
        g_job_server = jobs;
        pthread_mutex_unlock(&g_lock);
    }
    pthread_mutex_lock(&g_lock);
    g_state = ok ? 2 : 3;
    pthread_cond_broadcast(&g_ready);
    pthread_mutex_unlock(&g_lock);
    if (!ok)
        return nullptr;
    for (;;) {
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
            if (c->kind == COMMAND_LAUNCHER)
                run_launcher(&c->update);
            else if (c->kind == COMMAND_JOB)
                run_job(&c->job.dialog, c->job.text);
            else
                run_command(&c->toast);
            free(c);
        }
        struct pollfd fds[2];
        fds[0] = (struct pollfd){.fd = g_wake, .events = POLLIN};
        fds[1] = (struct pollfd){.fd = sd_bus_get_fd(g_bus), .events = (short)sd_bus_get_events(g_bus)};
        uint64_t usec = UINT64_MAX;
        sd_bus_get_timeout(g_bus, &usec);
        int timeout_ms = 1000;
        if (usec != UINT64_MAX) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t now_us = (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
            uint64_t ms = usec > now_us ? (usec - now_us + 999) / 1000 : 0;
            if (ms < (uint64_t)timeout_ms)
                timeout_ms = (int)ms;
        }
        poll(fds, 2, timeout_ms);
        if (fds[0].revents & POLLIN) {
            uint64_t v;
            if (read(g_wake, &v, sizeof(v)) < 0)
                ;
        }
        while (sd_bus_process(g_bus, nullptr) > 0)
            ;
    }
    return nullptr;
}

static void enqueue(Command *c);

static bool ensure_thread(void)
{
    pthread_mutex_lock(&g_lock);
    if (g_state == 0) {
        g_state = 1;
        g_wake = eventfd(0, EFD_CLOEXEC);
        pthread_t t;
        if (g_wake < 0 || pthread_create(&t, nullptr, shell_thread, nullptr) != 0) {
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

static uint64_t toast_command(void *command, size_t size, uint64_t *value)
{
    uint32_t op = *(uint32_t *)command;
    if (size < sizeof(MimeShellToast) || op < MIME_SHELL_TOAST_SHOW || op > MIME_SHELL_TOAST_REPLY)
        return MIME_HC_STATUS_INVALID;
    if (!ensure_thread())
        return MIME_HC_STATUS_UNSUPPORTED;
    MimeShellToast *toast = command;
    if (op == MIME_SHELL_TOAST_PROBE) {
        pthread_mutex_lock(&g_lock);
        *value = g_server ? 1 : 0;
        pthread_mutex_unlock(&g_lock);
        return MIME_HC_STATUS_OK;
    }
    if (op == MIME_SHELL_TOAST_REPLY) {
        /* the text of a reply is written where the body goes. */
        uint64_t units = 0;
        pthread_mutex_lock(&g_lock);
        for (uint32_t i = 0; i < REPLIES_MAX; i++)
            if (g_replies[i].id == toast->id && g_replies[i].length) {
                units = g_replies[i].length;
                memcpy(toast->body, g_replies[i].units, units * sizeof(uint16_t));
                toast->body[units] = 0;
                g_replies[i].id = 0;
                break;
            }
        pthread_mutex_unlock(&g_lock);
        *value = units;
        return MIME_HC_STATUS_OK;
    }
    Command *c = malloc(sizeof(*c));
    if (!c)
        return MIME_HC_STATUS_NO_MEMORY;
    c->next = nullptr;
    c->kind = COMMAND_TOAST;
    memcpy(&c->toast, toast, sizeof(*toast));
    enqueue(c);
    return MIME_HC_STATUS_OK;
}

void mime_shell_launcher_update(const MimeLauncherUpdate *update)
{
    if (!ensure_thread())
        return;
    Command *c = malloc(sizeof(*c));
    if (!c)
        return;
    c->next = nullptr;
    c->kind = COMMAND_LAUNCHER;
    c->update = *update;
    enqueue(c);
}

bool mime_shell_job(const MimeShellDialog *dialog, const char *text)
{
    if (!ensure_thread())
        return false;
    pthread_mutex_lock(&g_lock);
    bool jobs = g_job_server;
    pthread_mutex_unlock(&g_lock);
    if (!jobs)
        return false;
    Command *c = malloc(sizeof(*c));
    if (!c)
        return false;
    c->next = nullptr;
    c->kind = COMMAND_JOB;
    c->job.dialog = *dialog;
    mime_strlcpy(c->job.text, text, sizeof(c->job.text));
    enqueue(c);
    return true;
}

static void enqueue(Command *c)
{
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
}

#endif
