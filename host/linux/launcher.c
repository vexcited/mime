/*
 * the identity of the guest's windows and the entries of its programs in the desktop's launcher. a window belongs to
 * the program whose application user model id it was given, its own or its process's, and the compositor groups the
 * windows of a program by the application id of their toplevels: the id is "mime." and the user model id with the
 * characters a file name cannot have turned into dashes, which is also the name of the entry the shortcuts of the
 * program get (see desktop.c) so that the compositor finds the entry for the windows. a window with no id has the
 * application id "mime" like the rest of the machine's.
 *
 * what the taskbar buttons of Windows show is shown on the launcher entry, over the session bus: the progress of a
 * button, the count of its badge and its call for attention are the properties of com.canonical.Unity.LauncherEntry
 * which Plasma and several docks follow, and a window that asks for attention is flashed to the compositor.
 */
#include "ui_internal.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRACKED 512
#define MAX_PROCESSES 128

typedef struct Tracked {
    bool used;
    uint32_t id, pid;
    char aumid[160];  /* its own */
    char app_id[200]; /* what the compositor was told */
    bool progress_visible, indeterminate;
    uint32_t progress; /* tenths of a percent */
    uint32_t count;
    bool urgent;
    uint64_t progress_serial, count_serial;
} Tracked;

typedef struct Process {
    bool used;
    uint32_t pid;
    char aumid[160];
} Process;

typedef struct Sent {
    char app_id[200];
    bool progress_visible;
    uint32_t progress;
    uint32_t count;
    bool urgent;
    bool known;
} Sent;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Tracked g_windows[MAX_TRACKED];
static Process g_processes[MAX_PROCESSES];
static Sent g_sent[64];
static uint64_t g_serial;

/* "mime." and the id, or "mime" for none. */
void mime_app_id_for(const char *aumid, char *out, size_t size)
{
    if (!aumid || !*aumid) {
        mime_strlcpy(out, "mime", size);
        return;
    }
    size_t n = (size_t)snprintf(out, size, "mime.");
    for (const char *p = aumid; *p && n + 1 < size; p++)
        out[n++] = isalnum((unsigned char)*p) || *p == '.' || *p == '_' || *p == '-' ? *p : '-';
    out[n] = 0;
}

static Tracked *find_window(uint32_t id, bool create)
{
    Tracked *free_slot = nullptr;
    for (int i = 0; i < MAX_TRACKED; i++) {
        if (g_windows[i].used && g_windows[i].id == id)
            return &g_windows[i];
        if (!g_windows[i].used && !free_slot)
            free_slot = &g_windows[i];
    }
    if (create && free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->used = true;
        free_slot->id = id;
        return free_slot;
    }
    return nullptr;
}

static Process *find_process(uint32_t pid, bool create)
{
    Process *free_slot = nullptr;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_processes[i].used && g_processes[i].pid == pid)
            return &g_processes[i];
        if (!g_processes[i].used && !free_slot)
            free_slot = &g_processes[i];
    }
    if (create && free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->used = true;
        free_slot->pid = pid;
        return free_slot;
    }
    return nullptr;
}

/* the application id a window has, and the compositor told when it changed. the lock is held. */
static void refresh_app_id(Tracked *w, char *changed, size_t size)
{
    Process *p = w->pid ? find_process(w->pid, false) : nullptr;
    const char *aumid = w->aumid[0] ? w->aumid : p ? p->aumid : "";
    char id[200];
    mime_app_id_for(aumid, id, sizeof(id));
    if (strcmp(id, w->app_id)) {
        mime_strlcpy(w->app_id, id, sizeof(w->app_id));
        mime_strlcpy(changed, id, size);
    }
}

/* a window is told to the compositor with the id of its program, once it has one. */
void mime_launcher_window_seen(uint32_t id, uint32_t pid)
{
    char changed[200] = "";
    pthread_mutex_lock(&g_lock);
    Tracked *w = find_window(id, true);
    if (w) {
        bool first = !w->pid;
        w->pid = pid;
        if (first)
            refresh_app_id(w, changed, sizeof(changed));
    }
    pthread_mutex_unlock(&g_lock);
    if (changed[0] && strcmp(changed, "mime"))
        mime_ui_window_app_id(id, changed);
}

/* what the application of an id shows: the progress of the button that set it last, the count of the last badge. */
static void publish_app(const char *app_id)
{
    bool visible = false, urgent = false;
    uint32_t progress = 0, count = 0;
    uint64_t progress_serial = 0, count_serial = 0;
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *w = &g_windows[i];
        if (!w->used || strcmp(w->app_id, app_id))
            continue;
        if (w->progress_visible && w->progress_serial >= progress_serial) {
            visible = true;
            progress = w->progress;
            progress_serial = w->progress_serial;
        }
        if (w->count && w->count_serial >= count_serial) {
            count = w->count;
            count_serial = w->count_serial;
        }
        urgent |= w->urgent;
    }
    Sent *s = nullptr, *free_slot = nullptr;
    for (int i = 0; i < 64; i++) {
        if (g_sent[i].known && !strcmp(g_sent[i].app_id, app_id))
            s = &g_sent[i];
        else if (!g_sent[i].known && !free_slot)
            free_slot = &g_sent[i];
    }
    if (!s) {
        if (!free_slot)
            return;
        s = free_slot;
        memset(s, 0, sizeof(*s));
        mime_strlcpy(s->app_id, app_id, sizeof(s->app_id));
        s->known = true;
    }
    MimeLauncherUpdate u = {};
    snprintf(u.uri, sizeof(u.uri), "application://%s.desktop", app_id);
    if (visible != s->progress_visible || (visible && progress != s->progress)) {
        u.progress = true;
        u.progress_visible = visible;
        u.progress_value = progress / 10000.0;
    }
    if (count != s->count) {
        u.count = true;
        u.count_value = count;
    }
    if (urgent != s->urgent) {
        u.urgent = true;
        u.urgent_value = urgent;
    }
    s->progress_visible = visible;
    s->progress = progress;
    s->count = count;
    s->urgent = urgent;
    if (u.progress || u.count || u.urgent)
        mime_shell_launcher_update(&u);
}

/* a window, or all the windows of a process, was given the application user model id. */
static void window_identity(const MimeShellWindow *c)
{
    char aumid[160];
    mime_utf16_to_utf8(aumid, sizeof(aumid), c->aumid, 128);
    typedef struct {
        uint32_t id;
        char app_id[200];
    } Change;
    Change *changes = mime_xcalloc(MAX_TRACKED, sizeof(*changes));
    int n = 0;
    pthread_mutex_lock(&g_lock);
    if (c->kind == MIME_SHELL_PROCESS_APPID) {
        Process *p = find_process(c->process_id, true);
        if (p)
            mime_strlcpy(p->aumid, aumid, sizeof(p->aumid));
    } else {
        Tracked *w = find_window(c->window_id, true);
        if (w) {
            if (!w->pid)
                w->pid = c->process_id;
            mime_strlcpy(w->aumid, aumid, sizeof(w->aumid));
        }
    }
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *w = &g_windows[i];
        if (!w->used || !w->pid)
            continue;
        char old[200], changed[200] = "";
        mime_strlcpy(old, w->app_id, sizeof(old));
        refresh_app_id(w, changed, sizeof(changed));
        if (changed[0]) {
            changes[n].id = w->id;
            mime_strlcpy(changes[n].app_id, changed, sizeof(changes[n].app_id));
            n++;
            /* what the old id showed passes to the new one. */
            if (old[0])
                publish_app(old);
            publish_app(changed);
        }
    }
    pthread_mutex_unlock(&g_lock);
    for (int i = 0; i < n; i++)
        mime_ui_window_app_id(changes[i].id, changes[i].app_id);
    free(changes);
}

static void window_launcher(const MimeShellWindow *c)
{
    pthread_mutex_lock(&g_lock);
    Tracked *w = find_window(c->window_id, true);
    if (w) {
        if (!w->pid) {
            char changed[200] = "";
            w->pid = c->process_id;
            refresh_app_id(w, changed, sizeof(changed));
            if (changed[0]) {
                pthread_mutex_unlock(&g_lock);
                mime_ui_window_app_id(w->id, changed);
                pthread_mutex_lock(&g_lock);
            }
        }
        if (c->changed & MIME_LAUNCHER_PROGRESS) {
            w->progress_visible = c->progress_visible != 0;
            w->indeterminate = c->indeterminate != 0;
            w->progress = c->progress > 10000 ? 10000 : c->progress;
            w->progress_serial = ++g_serial;
        }
        if (c->changed & MIME_LAUNCHER_BADGE) {
            w->count = c->count;
            w->count_serial = ++g_serial;
        }
        if (c->changed & MIME_LAUNCHER_ATTENTION)
            w->urgent = c->urgent != 0;
        if (w->app_id[0])
            publish_app(w->app_id);
    }
    pthread_mutex_unlock(&g_lock);
}

static void window_flash(const MimeShellWindow *c)
{
    pthread_mutex_lock(&g_lock);
    Tracked *w = find_window(c->window_id, true);
    if (w) {
        w->urgent = c->urgent != 0;
        if (w->app_id[0])
            publish_app(w->app_id);
    }
    pthread_mutex_unlock(&g_lock);
    mime_ui_window_flash(c->window_id, c->urgent);
}

void mime_launcher_window_gone(uint32_t id)
{
    pthread_mutex_lock(&g_lock);
    Tracked *w = find_window(id, false);
    if (w) {
        char app[200];
        mime_strlcpy(app, w->app_id, sizeof(app));
        w->used = false;
        if (app[0])
            publish_app(app);
    }
    pthread_mutex_unlock(&g_lock);
}

void mime_launcher_process_gone(uint32_t pid)
{
    pthread_mutex_lock(&g_lock);
    Process *p = find_process(pid, false);
    if (p)
        p->used = false;
    pthread_mutex_unlock(&g_lock);
}

uint64_t mime_ui_shell_window(const void *command)
{
    const MimeShellWindow *c = command;
    switch (c->op) {
    case MIME_SHELL_WINDOW:
        window_identity(c);
        break;
    case MIME_SHELL_LAUNCHER:
        window_launcher(c);
        break;
    case MIME_SHELL_FLASH:
        window_flash(c);
        break;
    }
    return MIME_HC_STATUS_OK;
}
