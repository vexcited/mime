/*
 * changes the host makes to the files of a directory a program of the guest watches. The guest reports what it does
 * itself, as it does it; what the host's own programs do is told by the kernel of the host, through inotify, to the
 * directories someone watches and no others. A watch is made when a request for changes is made on a directory handle
 * and goes with the handle. A tree is watched directory by directory, the ones below it made later as they appear.
 *
 * The guest's own changes make events here too, and are not told twice: before the host does what a hypercall asks
 * make, remove or move an entry, write a file or set its size, it notes the entry in mime_fswatch_self, and an event for
 * that entry that follows in the next moments is the guest's own. A creation, a removal or a move is one event and
 * its note is used up by it; writes make events in bursts that the kernel merges, so a note of a write lasts a few
 * milliseconds from the last one. A change of the host in the very milliseconds after a write of the guest to the
 * same file is therefore not told. That this is rare is a guess, not a measurement.
 *
 * Events are queued here as records, coalesced when one repeats the last, and the guest is interrupted when the queue
 * goes from empty to not empty. It fetches the queue with MIME_HC_FS_CHANGES until that answers nothing.
 */
#include "vmm.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#endif

#ifdef __linux__

#define WATCHES 128
#define DIRECTORIES 8192 /* directories watched in all, trees included */
#define QUEUE_BYTES (1u << 20)
#define SELF_SLOTS 256
#define MODIFY_NOTE_NS 8000000ll
#define NOTE_LIFE_NS 3000000000ll

/* FILE_NOTIFY_CHANGE_* of the guest. */
#define CHANGE_FILE_NAME 0x001u
#define CHANGE_DIR_NAME 0x002u
#define CHANGE_ATTRIBUTES 0x004u
#define CHANGE_SIZE 0x008u
#define CHANGE_LAST_WRITE 0x010u

typedef struct Watch {
    bool used;
    uint32_t id;
    uint32_t filter;
    bool tree;
    char *base; /* host path of the directory */
} Watch;

typedef struct Directory {
    int wd; /* 0 when the slot is free */
    int watch;
    char *relative; /* below the watch's directory, with slashes, "" for the directory itself */
} Directory;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Watch g_watches[WATCHES];
static Directory g_directories[DIRECTORIES];
static uint32_t g_directory_high; /* one past the last slot ever used */
static int g_inotify = -2; /* -2 not made, -1 refused */
static int g_wake = -1;
static MimeVm *g_vm;
static pthread_t g_thread;
static bool g_thread_started;
static volatile int g_watching; /* directories watched: the fast path of the notes when none are */

/* the queue of records the guest fetches: size, watch, action, name_chars, then the name in utf-16. */
static uint8_t *g_queue;
static uint32_t g_queue_used;
static bool g_overflow[WATCHES];
static bool g_armed = true; /* the guest fetched to the end: the next record raises the interrupt */

/* entries the guest is changing itself. */
typedef struct Self {
    char *path;
    uint32_t created, removed, moved_from, moved_to; /* events to come that are the guest's own */
    int64_t modified_until;                          /* ns of the monotonic clock */
    int64_t expires;
} Self;

static Self g_self[SELF_SLOTS];

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

/* ------------------------------------------------------------------ the guest's own changes */

static Self *self_find(const char *path, bool make)
{
    int64_t now = now_ns();
    Self *free_slot = nullptr, *oldest = &g_self[0];
    for (int i = 0; i < SELF_SLOTS; i++) {
        Self *s = &g_self[i];
        if (s->path && s->expires < now) {
            free(s->path);
            memset(s, 0, sizeof(*s));
        }
        if (s->path && !strcmp(s->path, path))
            return s;
        if (!s->path && !free_slot)
            free_slot = s;
        if (s->path && s->expires < oldest->expires)
            oldest = s;
    }
    if (!make)
        return nullptr;
    Self *s = free_slot ? free_slot : oldest;
    free(s->path);
    memset(s, 0, sizeof(*s));
    s->path = strdup(path);
    s->expires = now + NOTE_LIFE_NS;
    return s->path ? s : nullptr;
}

void mime_fswatch_self(const char *path, int kind)
{
    if (!__atomic_load_n(&g_watching, __ATOMIC_ACQUIRE) || !path)
        return;
    pthread_mutex_lock(&g_lock);
    Self *s = self_find(path, true);
    if (s) {
        s->expires = now_ns() + NOTE_LIFE_NS;
        switch (kind) {
        case MIME_FSWATCH_CREATE: s->created++; break;
        case MIME_FSWATCH_REMOVE: s->removed++; break;
        case MIME_FSWATCH_MOVE_FROM: s->moved_from++; break;
        case MIME_FSWATCH_MOVE_TO: s->moved_to++; break;
        case MIME_FSWATCH_MODIFY: s->modified_until = now_ns() + MODIFY_NOTE_NS; break;
        case MIME_FSWATCH_UNDO_CREATE: s->created -= s->created ? 1 : 0; break;
        case MIME_FSWATCH_UNDO_REMOVE: s->removed -= s->removed ? 1 : 0; break;
        case MIME_FSWATCH_UNDO_MOVE:
            s->moved_from -= s->moved_from ? 1 : 0;
            s->moved_to -= s->moved_to ? 1 : 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

/* whether an event is the echo of a change the guest made, which is then used up. with the lock held. */
static bool self_echo(const char *path, uint32_t mask)
{
    Self *s = self_find(path, false);
    if (!s)
        return false;
    if ((mask & IN_CREATE) && s->created) {
        s->created--;
        return true;
    }
    if ((mask & IN_DELETE) && s->removed) {
        s->removed--;
        return true;
    }
    if ((mask & IN_MOVED_FROM) && s->moved_from) {
        s->moved_from--;
        return true;
    }
    if ((mask & IN_MOVED_TO) && s->moved_to) {
        s->moved_to--;
        return true;
    }
    if ((mask & (IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE)) && s->modified_until > now_ns())
        return true;
    return false;
}

/* ------------------------------------------------------------------ the queue */

static void queue_raise(void)
{
    if (g_armed) {
        g_armed = false;
        mime_vm_raise_irq(g_vm, MIME_IRQ_FS);
    }
}

/* adds a record unless it repeats the last, with the lock held. */
static void queue_push(uint32_t watch_id, uint32_t action, const char *relative)
{
    uint16_t wide[520];
    uint32_t chars = (uint32_t)mime_utf8_to_utf16(wide, MIME_ARRAY_COUNT(wide), relative);
    for (uint32_t i = 0; i < chars; i++)
        if (wide[i] == '/')
            wide[i] = '\\';
    uint32_t size = (uint32_t)((sizeof(MimeFsChange) + chars * 2 + 3) & ~3u);
    if (g_queue_used + size > QUEUE_BYTES) {
        /* more than the guest has fetched fits: it only learns that something happened to this watch. */
        for (int i = 0; i < WATCHES; i++)
            if (g_watches[i].used && g_watches[i].id == watch_id)
                g_overflow[i] = true;
        queue_raise();
        return;
    }
    /* the last record, if it is the same change. */
    uint32_t at = 0, last = ~0u;
    while (at < g_queue_used) {
        last = at;
        at += ((MimeFsChange *)(g_queue + at))->size;
    }
    if (last != ~0u) {
        MimeFsChange *r = (MimeFsChange *)(g_queue + last);
        if (r->watch == watch_id && r->action == action && r->name_chars == chars && !memcmp(r->name, wide, chars * 2))
            return;
    }
    MimeFsChange *r = (MimeFsChange *)(g_queue + g_queue_used);
    r->size = size;
    r->watch = watch_id;
    r->action = action;
    r->name_chars = chars;
    memcpy(r->name, wide, chars * 2);
    g_queue_used += size;
    queue_raise();
}

/* ------------------------------------------------------------------ directories and their watches */

static uint32_t mask_of(uint32_t filter)
{
    uint32_t mask = 0;
    if (filter & (CHANGE_FILE_NAME | CHANGE_DIR_NAME))
        mask |= IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
    if (filter & (CHANGE_SIZE | CHANGE_LAST_WRITE))
        mask |= IN_MODIFY;
    if (filter & CHANGE_ATTRIBUTES)
        mask |= IN_ATTRIB;
    return mask | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR | IN_DONT_FOLLOW;
}

/* the watches of one directory of the host: more than one when programs of the guest watch it separately. */
static bool wd_shared(int wd, const Directory *except)
{
    for (uint32_t i = 0; i < g_directory_high; i++)
        if (g_directories[i].wd == wd && &g_directories[i] != except)
            return true;
    return false;
}

/* watches a directory for a watch, whose events add to those other watches of it ask for. with the lock held. */
static bool add_directory(int watch, const char *path, const char *relative)
{
    int wd = inotify_add_watch(g_inotify, path, mask_of(g_watches[watch].filter) | IN_MASK_ADD);
    if (wd < 0)
        return false;
    for (uint32_t i = 0; i < g_directory_high; i++)
        if (g_directories[i].wd == wd && g_directories[i].watch == watch)
            return true;
    for (uint32_t i = 0; i < DIRECTORIES; i++) {
        if (!g_directories[i].wd) {
            if (i >= g_directory_high)
                g_directory_high = i + 1;
            g_directories[i].wd = wd;
            g_directories[i].watch = watch;
            g_directories[i].relative = strdup(relative);
            __atomic_add_fetch(&g_watching, 1, __ATOMIC_SEQ_CST);
            return true;
        }
    }
    if (!wd_shared(wd, nullptr))
        inotify_rm_watch(g_inotify, wd);
    return false;
}

static void remove_directory(Directory *d)
{
    if (!wd_shared(d->wd, d))
        inotify_rm_watch(g_inotify, d->wd);
    free(d->relative);
    memset(d, 0, sizeof(*d));
    __atomic_sub_fetch(&g_watching, 1, __ATOMIC_SEQ_CST);
}

/* the directories below one, watched, with the names already in them told as made when `announce`. */
static void add_tree(int watch, const char *path, const char *relative, bool announce)
{
    DIR *dir = opendir(path);
    if (!dir)
        return;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char child[MIME_PATH_MAX], rel[MIME_PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        snprintf(rel, sizeof(rel), "%s%s%s", relative, relative[0] ? "/" : "", e->d_name);
        if (announce)
            queue_push(g_watches[watch].id, MIME_FS_CHANGE_ADDED, rel);
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            add_directory(watch, child, rel);
            add_tree(watch, child, rel, announce);
        }
    }
    closedir(dir);
}

static void drop_watch(int watch)
{
    for (uint32_t i = 0; i < g_directory_high; i++)
        if (g_directories[i].wd && g_directories[i].watch == watch)
            remove_directory(&g_directories[i]);
    free(g_watches[watch].base);
    memset(&g_watches[watch], 0, sizeof(g_watches[watch]));
    g_overflow[watch] = false;
}

/* ------------------------------------------------------------------ events */

/* the first slot of a descriptor, which looks up the guest's own notes for all that share it. */
static uint32_t first_for_wd(int wd)
{
    for (uint32_t i = 0; i < g_directory_high; i++)
        if (g_directories[i].wd == wd)
            return i;
    return ~0u;
}

static void handle_events(const char *buffer, ssize_t size)
{
    bool own_seen = false;
    struct pending_move {
        uint32_t cookie;
        int watch;
        char relative[MIME_PATH_MAX];
        bool directory;
        bool used;
    } moves[16] = {};
    const char *at = buffer;
    pthread_mutex_lock(&g_lock);
    while (at < buffer + size) {
        const struct inotify_event *e = (const struct inotify_event *)at;
        at += sizeof(*e) + e->len;
        if (e->mask & IN_Q_OVERFLOW) {
            for (int i = 0; i < WATCHES; i++)
                if (g_watches[i].used)
                    g_overflow[i] = true;
            queue_raise();
            continue;
        }
        for (uint32_t di = 0; di < DIRECTORIES; di++) {
            Directory *d = &g_directories[di];
            if (!d->wd || d->wd != e->wd)
                continue;
            Watch *w = &g_watches[d->watch];
            if (e->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) {
                /* the directory is gone or moved; the watch of the guest stays, with nothing to tell. */
                if (e->mask & IN_IGNORED) {
                    free(d->relative);
                    memset(d, 0, sizeof(*d));
                                    __atomic_sub_fetch(&g_watching, 1, __ATOMIC_SEQ_CST);
                }
                continue;
            }
            if (!e->len)
                continue;
            char rel[MIME_PATH_MAX], full[MIME_PATH_MAX];
            snprintf(rel, sizeof(rel), "%s%s%s", d->relative, d->relative[0] ? "/" : "", e->name);
            snprintf(full, sizeof(full), "%s/%s", w->base, rel);
            bool directory = (e->mask & IN_ISDIR) != 0;
            uint32_t name_filter = directory ? CHANGE_DIR_NAME : CHANGE_FILE_NAME;
            /* a note of the guest's own change is used up by the first of the watches to see its event. */
            bool own = di == first_for_wd(e->wd) ? self_echo(full, e->mask & ~(uint32_t)IN_ISDIR) : own_seen;
            if (di == first_for_wd(e->wd))
                own_seen = own;
            /* a directory made in a tree is watched too, and what it already holds is told. */
            if (w->tree && directory && (e->mask & (IN_CREATE | IN_MOVED_TO))) {
                add_directory(d->watch, full, rel);
                add_tree(d->watch, full, rel, !own);
                if (!own && (w->filter & name_filter))
                    queue_push(w->id, MIME_FS_CHANGE_ADDED, rel);
                continue;
            }
            if (own)
                continue;
            if (e->mask & IN_CREATE) {
                if (w->filter & name_filter)
                    queue_push(w->id, MIME_FS_CHANGE_ADDED, rel);
            } else if (e->mask & IN_DELETE) {
                if (w->filter & name_filter)
                    queue_push(w->id, MIME_FS_CHANGE_REMOVED, rel);
            } else if (e->mask & IN_MOVED_FROM) {
                /* the matching arrival, if it comes in this batch, makes the pair; else it left the watch. */
                int slot = -1;
                for (int i = 0; i < 16 && slot < 0; i++)
                    if (!moves[i].used)
                        slot = i;
                if (slot >= 0) {
                    moves[slot].used = true;
                    moves[slot].cookie = e->cookie;
                    moves[slot].watch = d->watch;
                    moves[slot].directory = directory;
                    snprintf(moves[slot].relative, sizeof(moves[slot].relative), "%s", rel);
                } else if (w->filter & name_filter) {
                    queue_push(w->id, MIME_FS_CHANGE_REMOVED, rel);
                }
            } else if (e->mask & IN_MOVED_TO) {
                int found = -1;
                for (int i = 0; i < 16; i++)
                    if (moves[i].used && moves[i].cookie == e->cookie && moves[i].watch == d->watch)
                        found = i;
                if (found >= 0) {
                    if (w->filter & name_filter) {
                        queue_push(w->id, MIME_FS_CHANGE_RENAMED_OLD, moves[found].relative);
                        queue_push(w->id, MIME_FS_CHANGE_RENAMED_NEW, rel);
                    }
                    moves[found].used = false;
                } else if (w->filter & name_filter) {
                    queue_push(w->id, MIME_FS_CHANGE_ADDED, rel);
                }
            } else if (!directory && (((e->mask & IN_MODIFY) && (w->filter & (CHANGE_SIZE | CHANGE_LAST_WRITE))) ||
                                       ((e->mask & IN_ATTRIB) && (w->filter & CHANGE_ATTRIBUTES)))) {
                queue_push(w->id, MIME_FS_CHANGE_MODIFIED, rel);
            }
        }
    }
    /* departures with no arrival in the batch: something moved out of the watched directories. */
    for (int i = 0; i < 16; i++) {
        if (moves[i].used) {
            Watch *w = &g_watches[moves[i].watch];
            if (w->filter & (moves[i].directory ? CHANGE_DIR_NAME : CHANGE_FILE_NAME))
                queue_push(w->id, MIME_FS_CHANGE_REMOVED, moves[i].relative);
        }
    }
    pthread_mutex_unlock(&g_lock);
}

static void *reader_main(void *arg)
{
    (void)arg;
    mime_set_thread_name("mime-fswatch");
    char buffer[65536] __attribute__((aligned(__alignof__(struct inotify_event))));
    struct pollfd fds[2] = {{g_inotify, POLLIN, 0}, {g_wake, POLLIN, 0}};
    for (;;) {
        if (poll(fds, 2, -1) < 0 && errno != EINTR)
            break;
        if (fds[1].revents & POLLIN)
            break;
        if (fds[0].revents & POLLIN) {
            ssize_t n = read(g_inotify, buffer, sizeof(buffer));
            if (n > 0) {
                /* a moved entry is two events that the kernel queues together; one that ended the batch gets its mate. */
                const struct inotify_event *last = nullptr;
                for (const char *p = buffer; p < buffer + n; p += sizeof(*last) + last->len)
                    last = (const struct inotify_event *)p;
                if (last && (last->mask & IN_MOVED_FROM)) {
                    struct pollfd more = {g_inotify, POLLIN, 0};
                    if (poll(&more, 1, 2) > 0) {
                        ssize_t extra = read(g_inotify, buffer + n, sizeof(buffer) - (size_t)n);
                        if (extra > 0)
                            n += extra;
                    }
                }
                handle_events(buffer, n);
            }
        }
    }
    return nullptr;
}

static bool start(MimeVm *vm)
{
    if (g_inotify >= 0)
        return true;
    if (g_inotify == -1)
        return false;
    g_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    g_wake = eventfd(0, EFD_CLOEXEC);
    g_queue = malloc(QUEUE_BYTES);
    if (g_inotify < 0 || g_wake < 0 || !g_queue) {
        if (g_inotify >= 0)
            close(g_inotify);
        if (g_wake >= 0)
            close(g_wake);
        free(g_queue);
        g_inotify = -1;
        return false;
    }
    g_vm = vm;
    g_thread_started = pthread_create(&g_thread, nullptr, reader_main, nullptr) == 0;
    if (!g_thread_started) {
        close(g_inotify);
        close(g_wake);
        free(g_queue);
        g_inotify = -1;
        return false;
    }
    return true;
}

uint64_t mime_fswatch_add(MimeVm *vm, const char *path, uint32_t id, uint32_t filter, bool tree)
{
    if (!path || !id || !(filter & (CHANGE_FILE_NAME | CHANGE_DIR_NAME | CHANGE_ATTRIBUTES | CHANGE_SIZE | CHANGE_LAST_WRITE)))
        return MIME_HC_STATUS_INVALID;
    pthread_mutex_lock(&g_lock);
    if (!start(vm)) {
        pthread_mutex_unlock(&g_lock);
        return MIME_HC_STATUS_UNSUPPORTED;
    }
    int slot = -1;
    for (int i = 0; i < WATCHES; i++) {
        if (g_watches[i].used && g_watches[i].id == id) {
            slot = i;
            break;
        }
        if (!g_watches[i].used && slot < 0)
            slot = i;
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_lock);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    Watch *w = &g_watches[slot];
    if (w->used)
        drop_watch(slot);
    w->used = true;
    w->id = id;
    w->filter = filter;
    w->tree = tree;
    w->base = strdup(path);
    if (!w->base || !add_directory(slot, path, "")) {
        drop_watch(slot);
        pthread_mutex_unlock(&g_lock);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    if (tree)
        add_tree(slot, path, "", false);
    pthread_mutex_unlock(&g_lock);
    return MIME_HC_STATUS_OK;
}

void mime_fswatch_remove(uint32_t id)
{
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < WATCHES; i++)
        if (g_watches[i].used && g_watches[i].id == id)
            drop_watch(i);
    pthread_mutex_unlock(&g_lock);
}

/* the records for the guest, up to capacity, with a record that says what overflowed first. */
uint64_t mime_fswatch_fetch(MimeVm *vm, uint64_t pa, uint64_t capacity, uint64_t *bytes)
{
    *bytes = 0;
    uint8_t *out = mime_vm_phys(vm, pa, capacity);
    if (!out || capacity < sizeof(MimeFsChange) + 1040)
        return MIME_HC_STATUS_INVALID;
    pthread_mutex_lock(&g_lock);
    uint64_t used = 0;
    for (int i = 0; i < WATCHES; i++) {
        if (g_overflow[i] && g_watches[i].used && used + sizeof(MimeFsChange) <= capacity) {
            MimeFsChange *r = (MimeFsChange *)(out + used);
            r->size = sizeof(MimeFsChange);
            r->watch = g_watches[i].id;
            r->action = MIME_FS_CHANGE_OVERFLOW;
            r->name_chars = 0;
            used += r->size;
            g_overflow[i] = false;
        }
    }
    uint32_t taken = 0;
    while (taken < g_queue_used) {
        uint32_t size = ((MimeFsChange *)(g_queue + taken))->size;
        if (used + size > capacity)
            break;
        memcpy(out + used, g_queue + taken, size);
        used += size;
        taken += size;
    }
    if (taken) {
        memmove(g_queue, g_queue + taken, g_queue_used - taken);
        g_queue_used -= taken;
    }
    if (!used && !g_queue_used)
        g_armed = true; /* the guest has everything: what comes next interrupts it */
    pthread_mutex_unlock(&g_lock);
    *bytes = used;
    return MIME_HC_STATUS_OK;
}

void mime_fswatch_stop(void)
{
    if (!g_thread_started)
        return;
    uint64_t one = 1;
    if (write(g_wake, &one, sizeof(one)) < 0)
        return;
    pthread_join(g_thread, nullptr);
    g_thread_started = false;
}

#else

uint64_t mime_fswatch_add(MimeVm *vm, const char *path, uint32_t id, uint32_t filter, bool tree)
{
    (void)vm, (void)path, (void)id, (void)filter, (void)tree;
    return MIME_HC_STATUS_UNSUPPORTED;
}

void mime_fswatch_remove(uint32_t id)
{
    (void)id;
}

uint64_t mime_fswatch_fetch(MimeVm *vm, uint64_t pa, uint64_t capacity, uint64_t *bytes)
{
    (void)vm, (void)pa, (void)capacity;
    *bytes = 0;
    return MIME_HC_STATUS_UNSUPPORTED;
}

void mime_fswatch_self(const char *path, int kind)
{
    (void)path, (void)kind;
}

void mime_fswatch_stop(void)
{
}

#endif
