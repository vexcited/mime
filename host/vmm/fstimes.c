/*
 * the four times of a host file as windows reports them.
 *
 * the write time is the host's modification time and the access time its access time. the creation time is the
 * host's birth time where it has one. NTFS lets a program set the creation time and keeps the change time apart from
 * what a host file system calls the same, so a file can carry a record in an extended attribute: the creation time to
 * report instead of the birth time, and the change time with the modification time it was taken at. the change time
 * is that one while the file's modification time is still the one seen then. a later write moves the modification
 * time, and the change time follows it. a file without a record has the change time of its modification time.
 *
 * where the file system keeps no extended attributes the record lives in memory until the machine stops, like a
 * security descriptor that could not be stored.
 */
#include "vmm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/xattr.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#endif

#ifdef __APPLE__
#define TIMES_ATTRIBUTE "mime.times"
#define times_fget(fd, buf, size) fgetxattr((fd), TIMES_ATTRIBUTE, (buf), (size), 0, 0)
#define times_get(path, buf, size) getxattr((path), TIMES_ATTRIBUTE, (buf), (size), 0, 0)
#define times_fset(fd, buf, size) fsetxattr((fd), TIMES_ATTRIBUTE, (buf), (size), 0, 0)
#define times_set(path, buf, size) setxattr((path), TIMES_ATTRIBUTE, (buf), (size), 0, 0)
#else
#define TIMES_ATTRIBUTE "user.mime.times"
#define times_fget(fd, buf, size) fgetxattr((fd), TIMES_ATTRIBUTE, (buf), (size))
#define times_get(path, buf, size) getxattr((path), TIMES_ATTRIBUTE, (buf), (size))
#define times_fset(fd, buf, size) fsetxattr((fd), TIMES_ATTRIBUTE, (buf), (size), 0)
#define times_set(path, buf, size) setxattr((path), TIMES_ATTRIBUTE, (buf), (size), 0)
#endif

#define EPOCH_TICKS (11644473600ull * 10000000ull)
#define RECORD_MAGIC 0x314D544Du /* "MTM1". */
#define RECORD_CREATION 1u
#define RECORD_CHANGE 2u

typedef struct TimesRecord {
    uint32_t magic;
    uint32_t flags;
    uint64_t creation;
    uint64_t change;
    int64_t seen_sec;  /* the modification time the change time was taken at. */
    int64_t seen_nsec;
} TimesRecord;

static uint64_t ts_ticks(struct timespec ts)
{
    return ((uint64_t)ts.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)ts.tv_nsec / 100;
}

uint64_t mime_fs_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts_ticks(ts);
}

static int64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

/* ------------------------------------------------------------------ stat with the birth time */

#ifdef __linux__
static void from_statx(const struct statx *x, MimeFsStat *out)
{
    memset(out, 0, sizeof(*out));
    struct stat *st = &out->st;
    st->st_dev = makedev(x->stx_dev_major, x->stx_dev_minor);
    st->st_ino = x->stx_ino;
    st->st_mode = x->stx_mode;
    st->st_nlink = x->stx_nlink;
    st->st_uid = x->stx_uid;
    st->st_gid = x->stx_gid;
    st->st_rdev = makedev(x->stx_rdev_major, x->stx_rdev_minor);
    st->st_size = (off_t)x->stx_size;
    st->st_blksize = (blksize_t)x->stx_blksize;
    st->st_blocks = (blkcnt_t)x->stx_blocks;
    st->st_atim = (struct timespec){.tv_sec = x->stx_atime.tv_sec, .tv_nsec = x->stx_atime.tv_nsec};
    st->st_mtim = (struct timespec){.tv_sec = x->stx_mtime.tv_sec, .tv_nsec = x->stx_mtime.tv_nsec};
    st->st_ctim = (struct timespec){.tv_sec = x->stx_ctime.tv_sec, .tv_nsec = x->stx_ctime.tv_nsec};
    if (x->stx_mask & STATX_BTIME) {
        out->birth = (struct timespec){.tv_sec = x->stx_btime.tv_sec, .tv_nsec = x->stx_btime.tv_nsec};
        out->has_birth = true;
    }
}

#define STATX_WANTED (STATX_BASIC_STATS | STATX_BTIME)

int mime_fs_stat(const char *path, MimeFsStat *out)
{
    struct statx x;
    if (statx(AT_FDCWD, path, AT_STATX_SYNC_AS_STAT, STATX_WANTED, &x) != 0)
        return -1;
    from_statx(&x, out);
    return 0;
}

int mime_fs_lstat(const char *path, MimeFsStat *out)
{
    struct statx x;
    if (statx(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW | AT_STATX_SYNC_AS_STAT, STATX_WANTED, &x) != 0)
        return -1;
    from_statx(&x, out);
    return 0;
}

int mime_fs_fstat(int fd, MimeFsStat *out)
{
    struct statx x;
    if (statx(fd, "", AT_EMPTY_PATH | AT_STATX_SYNC_AS_STAT, STATX_WANTED, &x) != 0)
        return -1;
    from_statx(&x, out);
    return 0;
}
#else
static void from_stat(MimeFsStat *out)
{
    out->birth = out->st.st_birthtimespec;
    out->has_birth = true;
}

int mime_fs_stat(const char *path, MimeFsStat *out)
{
    memset(out, 0, sizeof(*out));
    if (stat(path, &out->st) != 0)
        return -1;
    from_stat(out);
    return 0;
}

int mime_fs_lstat(const char *path, MimeFsStat *out)
{
    memset(out, 0, sizeof(*out));
    if (lstat(path, &out->st) != 0)
        return -1;
    from_stat(out);
    return 0;
}

int mime_fs_fstat(int fd, MimeFsStat *out)
{
    memset(out, 0, sizeof(*out));
    if (fstat(fd, &out->st) != 0)
        return -1;
    from_stat(out);
    return 0;
}
#endif

/* ------------------------------------------------------------------ records */

typedef struct MemoryRecord {
    struct MemoryRecord *next;
    dev_t dev;
    ino_t ino;
    TimesRecord record;
} MemoryRecord;

#define MEMORY_BUCKETS 256
static pthread_mutex_t g_memory_lock = PTHREAD_MUTEX_INITIALIZER;
static MemoryRecord *g_memory[MEMORY_BUCKETS];
static volatile size_t g_memory_count;

static MemoryRecord **memory_slot(dev_t dev, ino_t ino)
{
    MemoryRecord **link = &g_memory[((uint64_t)ino * 0x9E3779B97F4A7C15ull >> 56) % MEMORY_BUCKETS];
    while (*link && ((*link)->dev != dev || (*link)->ino != ino))
        link = &(*link)->next;
    return link;
}

static bool memory_load(const struct stat *st, TimesRecord *record)
{
    if (!g_memory_count)
        return false;
    pthread_mutex_lock(&g_memory_lock);
    MemoryRecord *m = *memory_slot(st->st_dev, st->st_ino);
    if (m)
        *record = m->record;
    pthread_mutex_unlock(&g_memory_lock);
    return m != nullptr;
}

static void memory_save(const struct stat *st, const TimesRecord *record)
{
    pthread_mutex_lock(&g_memory_lock);
    MemoryRecord **link = memory_slot(st->st_dev, st->st_ino);
    if (!*link) {
        *link = mime_xcalloc(1, sizeof(MemoryRecord));
        (*link)->dev = st->st_dev;
        (*link)->ino = st->st_ino;
        g_memory_count++;
    }
    (*link)->record = *record;
    pthread_mutex_unlock(&g_memory_lock);
}

void mime_fs_times_forget(const struct stat *st)
{
    if (!g_memory_count)
        return;
    pthread_mutex_lock(&g_memory_lock);
    MemoryRecord **link = memory_slot(st->st_dev, st->st_ino);
    MemoryRecord *m = *link;
    if (m) {
        *link = m->next;
        free(m);
        g_memory_count--;
    }
    pthread_mutex_unlock(&g_memory_lock);
}

static bool record_load(const char *path, int fd, const struct stat *st, TimesRecord *record)
{
    ssize_t n = fd >= 0 ? times_fget(fd, record, sizeof(*record)) : times_get(path, record, sizeof(*record));
    if (n == (ssize_t)sizeof(*record) && record->magic == RECORD_MAGIC)
        return true;
    return memory_load(st, record);
}

/* a read only file takes the attribute once its owner may write it for a moment. */
static int attribute_store(const char *path, int fd, const struct stat *st, const TimesRecord *record)
{
    int result = fd >= 0 ? times_fset(fd, record, sizeof(*record)) : times_set(path, record, sizeof(*record));
    if (result == 0 || (errno != EACCES && errno != EPERM) || st->st_uid != geteuid() || !S_ISREG(st->st_mode))
        return result;
    int error = errno;
    mode_t mode = st->st_mode & 07777;
    if ((fd >= 0 ? fchmod(fd, mode | S_IWUSR) : chmod(path, mode | S_IWUSR)) != 0) {
        errno = error;
        return -1;
    }
    result = fd >= 0 ? times_fset(fd, record, sizeof(*record)) : times_set(path, record, sizeof(*record));
    error = errno;
    if (fd >= 0)
        fchmod(fd, mode);
    else
        chmod(path, mode);
    errno = error;
    return result;
}

static void record_save(const char *path, int fd, const struct stat *st, TimesRecord *record)
{
    record->magic = RECORD_MAGIC;
    if (path)
        mime_fswatch_self(path, MIME_FSWATCH_MODIFY);
    if (attribute_store(path, fd, st, record) == 0)
        mime_fs_times_forget(st);
    else
        memory_save(st, record);
    if (path)
        mime_fswatch_self(path, MIME_FSWATCH_MODIFY);
}

static int stat_of(const char *path, int fd, MimeFsStat *s)
{
    return fd >= 0 ? mime_fs_fstat(fd, s) : mime_fs_stat(path, s);
}

/* ------------------------------------------------------------------ reading and writing the times */

static uint64_t guessed_birth(const struct stat *st)
{
    /* without a birth time the oldest of the change and modification times is the closest there is. */
    return ts_ticks(st->st_ctim.tv_sec < st->st_mtim.tv_sec ? st->st_ctim : st->st_mtim);
}

void mime_fs_times(const char *path, int fd, const MimeFsStat *s, MimeFsTimes *t)
{
    TimesRecord record;
    /* a link has no record: its extended attributes would be the ones of what it points to. */
    bool have = !S_ISLNK(s->st.st_mode) && record_load(path, fd, &s->st, &record);
    t->access = ts_ticks(s->st.st_atim);
    t->write = ts_ticks(s->st.st_mtim);
    t->creation = have && (record.flags & RECORD_CREATION) ? record.creation
                  : s->has_birth                            ? ts_ticks(s->birth)
                                                            : guessed_birth(&s->st);
    t->change = t->write;
    if (have && (record.flags & RECORD_CHANGE)) {
        bool seen = record.seen_sec == s->st.st_mtim.tv_sec && record.seen_nsec == s->st.st_mtim.tv_nsec;
        /* a modification time in the future was set by a program, it is not a change that happened. */
        if (seen || t->write <= record.change || t->write > mime_fs_now())
            t->change = record.change;
    }
}

void mime_fs_times_created(const char *path, int fd, const MimeFsStat *s)
{
    if (s->has_birth)
        return;
    TimesRecord record = {.flags = RECORD_CREATION, .creation = mime_fs_now()};
    record_save(path, fd, &s->st, &record);
}

void mime_fs_times_set(const char *path, int fd, const uint64_t *creation, const uint64_t *change)
{
    MimeFsStat s;
    if (stat_of(path, fd, &s) != 0)
        return;
    TimesRecord record;
    if (!record_load(path, fd, &s.st, &record))
        memset(&record, 0, sizeof(record));
    if (creation) {
        record.creation = *creation;
        record.flags |= RECORD_CREATION;
    }
    if (change) {
        record.change = *change;
        record.flags |= RECORD_CHANGE;
        record.seen_sec = s.st.st_mtim.tv_sec;
        record.seen_nsec = s.st.st_mtim.tv_nsec;
    }
    record_save(path, fd, &s.st, &record);
}

void mime_fs_times_changed(const char *path, int fd)
{
    uint64_t now = mime_fs_now();
    mime_fs_times_set(path, fd, nullptr, &now);
}

/* ------------------------------------------------------------------ the access time */

void mime_fs_touch_access(MimeFsHandle *fh, bool force)
{
    if (fh->fd < 0 || fh->touch_failed || (fh->frozen & MIME_FS_TIME_ACCESS))
        return;
    int64_t now = mono_ns();
    if (!force && fh->touched_ns && now - fh->touched_ns < 1000000000ll)
        return;
    fh->touched_ns = now ? now : 1;
    struct timespec times[2] = {{.tv_nsec = UTIME_NOW}, {.tv_nsec = UTIME_OMIT}};
    clock_gettime(CLOCK_REALTIME, &times[0]);
    if (fh->host_path)
        mime_fswatch_self(fh->host_path, MIME_FSWATCH_MODIFY);
    if (futimens(fh->fd, times) != 0)
        fh->touch_failed = true;
}

/* ------------------------------------------------------------------ names that lost their file */

/*
 * as FsRtl does for NTFS, a file deleted or renamed away passes its creation time to a file made under the same name
 * within 15 seconds, so that a program saving through a temporary file keeps the creation time of the original.
 */
#define TUNNEL_SLOTS 64
#define TUNNEL_LIFE_NS 15000000000ll

static struct {
    char *path;
    uint64_t creation;
    int64_t expires;
} g_tunnel[TUNNEL_SLOTS];
static unsigned g_tunnel_next;
static pthread_mutex_t g_tunnel_lock = PTHREAD_MUTEX_INITIALIZER;

void mime_fs_tunnel_add(const char *path, uint64_t creation)
{
    int64_t now = mono_ns();
    pthread_mutex_lock(&g_tunnel_lock);
    unsigned slot = TUNNEL_SLOTS;
    for (unsigned i = 0; i < TUNNEL_SLOTS; i++) {
        if (g_tunnel[i].path && !strcasecmp(g_tunnel[i].path, path)) {
            slot = i;
            break;
        }
    }
    if (slot == TUNNEL_SLOTS) {
        slot = g_tunnel_next++ % TUNNEL_SLOTS;
        free(g_tunnel[slot].path);
        g_tunnel[slot].path = mime_xstrdup(path);
    }
    g_tunnel[slot].creation = creation;
    g_tunnel[slot].expires = now + TUNNEL_LIFE_NS;
    pthread_mutex_unlock(&g_tunnel_lock);
}

bool mime_fs_tunnel_take(const char *path, uint64_t *creation)
{
    bool found = false;
    int64_t now = mono_ns();
    pthread_mutex_lock(&g_tunnel_lock);
    for (unsigned i = 0; i < TUNNEL_SLOTS; i++) {
        if (!g_tunnel[i].path || strcasecmp(g_tunnel[i].path, path))
            continue;
        found = g_tunnel[i].expires > now;
        *creation = g_tunnel[i].creation;
        free(g_tunnel[i].path);
        g_tunnel[i].path = nullptr;
        break;
    }
    pthread_mutex_unlock(&g_tunnel_lock);
    return found;
}
