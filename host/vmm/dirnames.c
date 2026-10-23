/*
 * caches directory names for case-insensitive Windows path lookup. missing spellings and file creation
 * otherwise require full directory scans: 170 microseconds for 400 names, increasing with directory size.
 *
 * on Linux, drain inotify events before each lookup. the kernel queues a change before its modifying call
 * returns, so this includes changes by the guest and other host programs.
 *
 * without a watch, reuse names only while inode and timestamps match, after the directory has been unchanged
 * for 100 milliseconds. reread if it changes during listing, since host filesystem timestamps advance in
 * millisecond steps.
 */
#include "vmm.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/inotify.h>
#endif

#ifdef __APPLE__
#define ST_MTIME(st) ((st)->st_mtimespec)
#define ST_CTIME(st) ((st)->st_ctimespec)
#else
#define ST_MTIME(st) ((st)->st_mtim)
#define ST_CTIME(st) ((st)->st_ctim)
#endif

#define DIR_NAMES_SLOTS 64
#define DIR_NAMES_MAX_DIRECTORY 200000u /* names in one directory. */
#define DIR_NAMES_MAX_TOTAL 600000u     /* names in all. */
#define DIR_NAMES_SETTLE_NS 100000000ll

typedef struct DirName {
    struct DirName *next;
    uint32_t hash;
    char name[];
} DirName;

typedef struct DirNames {
    char *path;
    dev_t dev;
    ino_t ino;
    struct timespec mtime, ctime; /* when there is no watch. */
    int watch;                    /* 0 when there is none, as inotify numbers its watches from 1. */
    DirName **table;
    uint32_t buckets, count;
    uint64_t stamp; /* of the last use, the oldest slot is replaced. */
} DirNames;

static DirNames g_slots[DIR_NAMES_SLOTS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_clock;
static uint32_t g_total; /* names in all the slots. */
#ifdef __linux__
static int g_inotify = -2; /* -2 not made yet, -1 refused. */
#endif

/* uppercases UTF-16 units for Windows name comparisons: ASCII, Latin-1 and Cyrillic. */
static uint16_t upcase(uint16_t c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7) || (c >= 0x430 && c <= 0x44F))
        return c - 0x20;
    if (c == 0xFF)
        return 0x178;
    if (c >= 0x450 && c <= 0x45F)
        return c - 0x50;
    return c;
}

bool mime_names_same(const char *a, const char *b)
{
    /* compare ASCII directly to avoid repeated UTF-16 conversion during scans. */
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (;; x++, y++) {
        if (*x >= 0x80 || *y >= 0x80)
            break;
        unsigned cx = *x >= 'a' && *x <= 'z' ? *x - 32u : *x, cy = *y >= 'a' && *y <= 'z' ? *y - 32u : *y;
        if (cx != cy)
            return false;
        if (!cx)
            return true;
    }
    uint16_t wa[256], wb[256];
    size_t na = mime_utf8_to_utf16(wa, MIME_ARRAY_COUNT(wa), a), nb = mime_utf8_to_utf16(wb, MIME_ARRAY_COUNT(wb), b);
    if (na != nb)
        return false;
    for (size_t i = 0; i < na; i++) {
        if (upcase(wa[i]) != upcase(wb[i]))
            return false;
    }
    return true;
}

/* case-insensitive name hash. */
static uint32_t fold_hash(const char *name)
{
    uint32_t h = 2166136261u;
    const unsigned char *p = (const unsigned char *)name;
    for (; *p && *p < 0x80; p++)
        h = (h ^ (*p >= 'a' && *p <= 'z' ? *p - 32u : *p)) * 16777619u;
    if (*p) {
        uint16_t wide[256];
        size_t n = mime_utf8_to_utf16(wide, MIME_ARRAY_COUNT(wide), (const char *)p);
        for (size_t i = 0; i < n; i++)
            h = (h ^ upcase(wide[i])) * 16777619u;
    }
    return h;
}

static bool same_time(struct timespec a, struct timespec b)
{
    return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

/* requires a sufficiently old modification time before trusting an unwatched listing. */
static bool directory_settled(const struct stat *st)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int64_t nanoseconds = (int64_t)now.tv_sec * 1000000000ll + now.tv_nsec;
    int64_t changed = MIME_MAX((int64_t)ST_MTIME(st).tv_sec * 1000000000ll + ST_MTIME(st).tv_nsec,
                               (int64_t)ST_CTIME(st).tv_sec * 1000000000ll + ST_CTIME(st).tv_nsec);
    return nanoseconds - changed > DIR_NAMES_SETTLE_NS;
}

static bool slot_add(DirNames *d, const char *name)
{
    uint32_t hash = fold_hash(name);
    if (!d->buckets || d->count >= d->buckets / 2) {
        uint32_t buckets = d->buckets ? d->buckets * 2 : 256;
        DirName **table = calloc(buckets, sizeof(*table));
        if (!table)
            return false;
        for (uint32_t b = 0; b < d->buckets; b++) {
            for (DirName *n = d->table[b]; n;) {
                DirName *next = n->next;
                n->next = table[n->hash & (buckets - 1)];
                table[n->hash & (buckets - 1)] = n;
                n = next;
            }
        }
        free(d->table);
        d->table = table;
        d->buckets = buckets;
    }
    for (DirName *n = d->table[hash & (d->buckets - 1)]; n; n = n->next) {
        if (n->hash == hash && !strcmp(n->name, name))
            return true;
    }
    size_t length = strlen(name);
    DirName *n = malloc(sizeof(*n) + length + 1);
    if (!n)
        return false;
    n->hash = hash;
    memcpy(n->name, name, length + 1);
    n->next = d->table[hash & (d->buckets - 1)];
    d->table[hash & (d->buckets - 1)] = n;
    d->count++;
    g_total++;
    return true;
}

#ifdef __linux__
static void slot_remove(DirNames *d, const char *name)
{
    if (!d->buckets)
        return;
    uint32_t hash = fold_hash(name);
    for (DirName **link = &d->table[hash & (d->buckets - 1)]; *link; link = &(*link)->next) {
        if ((*link)->hash == hash && !strcmp((*link)->name, name)) {
            DirName *gone = *link;
            *link = gone->next;
            free(gone);
            d->count--;
            g_total--;
            return;
        }
    }
}

#endif

/* finds the directory entry matching the component without case. */
static bool slot_find(const DirNames *d, const char *component, char *spelling)
{
    if (!d->buckets)
        return false;
    uint32_t hash = fold_hash(component);
    for (DirName *n = d->table[hash & (d->buckets - 1)]; n; n = n->next) {
        if (n->hash == hash && mime_names_same(n->name, component)) {
            memcpy(spelling, n->name, strlen(n->name) + 1);
            return true;
        }
    }
    return false;
}

/* releases a slot and its watch unless another slot shares it. */
static void slot_free(DirNames *d)
{
#ifdef __linux__
    if (d->watch > 0) {
        bool shared = false;
        for (int i = 0; i < DIR_NAMES_SLOTS; i++)
            shared |= &g_slots[i] != d && g_slots[i].path && g_slots[i].watch == d->watch;
        if (!shared)
            inotify_rm_watch(g_inotify, d->watch);
    }
#endif
    for (uint32_t b = 0; b < d->buckets; b++) {
        for (DirName *n = d->table[b]; n;) {
            DirName *next = n->next;
            free(n);
            n = next;
        }
    }
    free(d->table);
    free(d->path);
    g_total -= d->count;
    memset(d, 0, sizeof(*d));
}

#ifdef __linux__
/* applies the changes the host's kernel has queued, with the lock held. */
static void drain(void)
{
    if (g_inotify < 0)
        return;
    char buffer[16384] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t n;
    while ((n = read(g_inotify, buffer, sizeof(buffer))) > 0) {
        for (char *at = buffer; at < buffer + n;) {
            struct inotify_event *e = (struct inotify_event *)at;
            at += sizeof(*e) + e->len;
            if (e->mask & IN_Q_OVERFLOW) {
                for (int i = 0; i < DIR_NAMES_SLOTS; i++) {
                    if (g_slots[i].path) {
                        g_slots[i].watch = 0;
                        slot_free(&g_slots[i]);
                    }
                }
                continue;
            }
            for (int i = 0; i < DIR_NAMES_SLOTS; i++) {
                DirNames *d = &g_slots[i];
                if (!d->path || d->watch != e->wd)
                    continue;
                if (e->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT)) {
                    /* discard names and the ended watch when the directory is removed or moved. */
                    if (e->mask & IN_IGNORED)
                        d->watch = 0;
                    slot_free(d);
                } else if ((e->mask & (IN_CREATE | IN_MOVED_TO)) && e->len) {
                    if (!slot_add(d, e->name))
                        slot_free(d);
                } else if ((e->mask & (IN_DELETE | IN_MOVED_FROM)) && e->len) {
                    slot_remove(d, e->name);
                }
            }
        }
    }
}
#endif

static DIR *open_directory(const char *path)
{
    int fd = -1;
#ifdef O_NOATIME
    fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOATIME);
#endif
    if (fd < 0)
        fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *dir = fd < 0 ? nullptr : fdopendir(fd);
    if (fd >= 0 && !dir)
        close(fd);
    return dir;
}

/* reuse the directory slot, then a free slot, then the least recently used slot. */
static DirNames *slot_for(const char *dir)
{
    DirNames *pick = nullptr;
    for (int i = 0; i < DIR_NAMES_SLOTS; i++) {
        DirNames *d = &g_slots[i];
        if (d->path && !strcmp(d->path, dir)) {
            pick = d;
            break;
        }
        if (!d->path) {
            if (!pick || pick->path)
                pick = d;
        } else if (!pick || (pick->path && d->stamp < pick->stamp)) {
            pick = d;
        }
    }
    if (pick->path)
        slot_free(pick);
    return pick;
}

/* the least recently used slot, other than keep. */
static DirNames *oldest_slot(const DirNames *keep)
{
    DirNames *oldest = nullptr;
    for (int i = 0; i < DIR_NAMES_SLOTS; i++)
        if (g_slots[i].path && &g_slots[i] != keep && (!oldest || g_slots[i].stamp < oldest->stamp))
            oldest = &g_slots[i];
    return oldest;
}

bool mime_dir_spelling(const char *dir, const char *component, char *spelling, bool *known)
{
    *known = false;
    struct stat st;
    if (stat(dir, &st) != 0)
        return false;
    pthread_mutex_lock(&g_lock);
#ifdef __linux__
    if (g_inotify == -2) {
        g_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (g_inotify < 0)
            g_inotify = -1;
    }
    drain();
#endif
    for (int i = 0; i < DIR_NAMES_SLOTS; i++) {
        DirNames *d = &g_slots[i];
        if (!d->path || d->dev != st.st_dev || d->ino != st.st_ino || strcmp(d->path, dir))
            continue;
        if (d->watch <= 0 && !(same_time(d->mtime, ST_MTIME(&st)) && same_time(d->ctime, ST_CTIME(&st)) && directory_settled(&st)))
            break;
        d->stamp = ++g_clock;
        bool found = slot_find(d, component, spelling);
        pthread_mutex_unlock(&g_lock);
        *known = true;
        return found;
    }

    /*
     * install the watch before reading an uncached directory, then apply queued changes over the listing.
     * duplicate additions and removals are harmless. hold the lock so no other thread drains events before
     * this slot exists.
     */
    DirNames *d = slot_for(dir);
    d->path = strdup(dir);
    d->dev = st.st_dev;
    d->ino = st.st_ino;
    d->mtime = ST_MTIME(&st);
    d->ctime = ST_CTIME(&st);
    d->stamp = ++g_clock;
#ifdef __linux__
    if (g_inotify >= 0)
        d->watch = inotify_add_watch(g_inotify, dir, IN_ONLYDIR | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO |
                                                          IN_DELETE_SELF | IN_MOVE_SELF);
    if (d->watch < 0)
        d->watch = 0;
#endif
    bool complete = d->path && (d->watch > 0 || directory_settled(&st));
    DIR *handle = complete ? open_directory(dir) : nullptr;
    complete = handle != nullptr;
    struct dirent *e;
    while (complete && (e = readdir(handle))) {
        if (strlen(e->d_name) > 255 || d->count >= DIR_NAMES_MAX_DIRECTORY || !slot_add(d, e->d_name)) {
            complete = false;
            break;
        }
        while (g_total > DIR_NAMES_MAX_TOTAL) {
            DirNames *old = oldest_slot(d);
            if (!old) {
                complete = false;
                break;
            }
            slot_free(old);
        }
    }
    if (handle)
        closedir(handle);
    struct stat after;
    if (complete && d->watch <= 0 &&
        (stat(dir, &after) != 0 || after.st_ino != st.st_ino || !same_time(ST_MTIME(&after), ST_MTIME(&st)) ||
         !same_time(ST_CTIME(&after), ST_CTIME(&st))))
        complete = false;
    if (!complete) {
        slot_free(d);
        pthread_mutex_unlock(&g_lock);
        return false;
    }
    *known = true;
    bool found = slot_find(d, component, spelling);
    pthread_mutex_unlock(&g_lock);
    return found;
}
