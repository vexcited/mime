/*
 * host file system device. guest volumes map onto host directories; the
 * guest file system driver (mimefs) forwards irps here.
 */
#include "vmm.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#ifdef __APPLE__
#include <sys/mount.h>
#define ST_BIRTH(st) ((st)->st_birthtimespec)
#define ST_ATIME(st) ((st)->st_atimespec)
#define ST_MTIME(st) ((st)->st_mtimespec)
#define ST_CTIME(st) ((st)->st_ctimespec)
#else
#include <sys/vfs.h>
/* linux keeps the birth time out of stat; the change time is the closest a plain stat has. */
#define ST_BIRTH(st) ((st)->st_ctim.tv_sec < (st)->st_mtim.tv_sec ? (st)->st_ctim : (st)->st_mtim)
#define ST_ATIME(st) ((st)->st_atim)
#define ST_MTIME(st) ((st)->st_mtim)
#define ST_CTIME(st) ((st)->st_ctim)
#endif
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define FILE_SUPERSEDED 0
#define FILE_OPENED 1
#define FILE_CREATED 2
#define FILE_OVERWRITTEN 3

#define FILE_ATTRIBUTE_READONLY 0x01
#define FILE_ATTRIBUTE_HIDDEN 0x02
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_ARCHIVE 0x20
#define FILE_ATTRIBUTE_NORMAL 0x80

static uint64_t errno_status(int err)
{
    switch (err) {
    case ENOENT: return MIME_HC_STATUS_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS: return MIME_HC_STATUS_ACCESS_DENIED;
    case EEXIST: return MIME_HC_STATUS_EXISTS;
    case ENOTDIR: return MIME_HC_STATUS_NOT_DIRECTORY;
    case EISDIR: return MIME_HC_STATUS_IS_DIRECTORY;
    case ENOMEM: return MIME_HC_STATUS_NO_MEMORY;
    case ENOTEMPTY: return MIME_HC_STATUS_NOT_EMPTY;
    default: return MIME_HC_STATUS_IO_ERROR;
    }
}

/*
 * MIME_FS_FAULT=<end>,<skip>,<count>[;...] makes the writes to host files whose path ends in the text come back short, after
 * the first <skip> of them, <count> times: half of the bytes written and no error, which is what a host filesystem that
 * runs out of room does in the middle of a write. a way to test what the guest does with a write that did not all land.
 */
#define FAULT_MAX 4
static struct {
    char text[256];
    unsigned skip, count, seen;
} g_fault[FAULT_MAX];
static unsigned g_fault_count;
static pthread_once_t g_fault_once = PTHREAD_ONCE_INIT;

static void fault_init(void)
{
    const char *spec = getenv("MIME_FS_FAULT");
    while (spec && *spec && g_fault_count < FAULT_MAX) {
        const char *c1 = strchr(spec, ',');
        const char *c2 = c1 ? strchr(c1 + 1, ',') : nullptr;
        if (!c2 || (size_t)(c1 - spec) >= sizeof(g_fault[0].text))
            return;
        char *end;
        __typeof__(g_fault[0]) *f = &g_fault[g_fault_count++];
        memcpy(f->text, spec, (size_t)(c1 - spec));
        f->skip = (unsigned)strtoul(c1 + 1, nullptr, 10);
        f->count = (unsigned)strtoul(c2 + 1, &end, 10);
        mime_warn("fs: writes to files with a path ending in '%s' come back short %u times after %u", f->text, f->count,
                  f->skip);
        spec = *end == ';' ? end + 1 : end;
    }
}

static bool fault_hits(const char *path)
{
    pthread_once(&g_fault_once, fault_init);
    size_t have = strlen(path);
    for (unsigned i = 0; i < g_fault_count; i++) {
        size_t want = strlen(g_fault[i].text);
        if (have < want || strcmp(path + have - want, g_fault[i].text))
            continue;
        unsigned seen = __atomic_fetch_add(&g_fault[i].seen, 1, __ATOMIC_SEQ_CST);
        if (seen >= g_fault[i].skip && seen - g_fault[i].skip < g_fault[i].count)
            return true;
    }
    return false;
}

static uint64_t flush_fd(int fd)
{
    int result;
    do {
        result = fsync(fd);
    } while (result != 0 && errno == EINTR);
    return result == 0 ? MIME_HC_STATUS_OK : errno_status(errno);
}

static uint64_t flush_parent(const char *path)
{
    char parent[MIME_PATH_MAX];
    mime_strlcpy(parent, path, sizeof(parent));
    char *slash = strrchr(parent, '/');
    if (!slash)
        return MIME_HC_STATUS_INVALID;
    if (slash == parent)
        slash++;
    *slash = 0;
    int fd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return errno_status(errno);
    uint64_t status = flush_fd(fd);
    close(fd);
    return status;
}

/*
 * opens without recording the access. reading a file or listing a directory would update its access time, a
 * change of the host's disk that waits when a flush is committing the journal: Helium's start was stopped for
 * 4 to 16 ms, with dozens of file calls of all its processes at once, by a flush of one database. Only the
 * owner of a file may do it, and others get the plain open.
 */
static int open_quiet(const char *path, int flags, mode_t mode)
{
#ifdef O_NOATIME
    int fd = open(path, flags | O_NOATIME, mode);
    if (fd >= 0 || errno != EPERM)
        return fd;
#endif
    return open(path, flags, mode);
}

static DIR *opendir_quiet(const char *path)
{
    int fd = open_quiet(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    DIR *dir = fd < 0 ? nullptr : fdopendir(fd);
    if (fd >= 0 && !dir)
        close(fd);
    return dir;
}

static uint64_t ts_to_filetime(struct timespec ts)
{
    return ((uint64_t)ts.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)ts.tv_nsec / 100;
}

static struct timespec filetime_to_ts(uint64_t filetime)
{
    uint64_t ticks = filetime;
    /* a time before 1970 is the start of it. */
    if (ticks < 11644473600ull * 10000000ull)
        ticks = 11644473600ull * 10000000ull;
    ticks -= 11644473600ull * 10000000ull;
    return (struct timespec){.tv_sec = (time_t)(ticks / 10000000ull), .tv_nsec = (long)(ticks % 10000000ull) * 100};
}

static void fill_info(const struct stat *st, const char *name, MimeFsInfo *info)
{
    memset(info, 0, sizeof(*info));
    bool dir = S_ISDIR(st->st_mode);
    info->size = dir ? 0 : (uint64_t)st->st_size;
    info->allocation_size = dir ? 0 : (uint64_t)st->st_blocks * 512;
    info->creation_time = ts_to_filetime(ST_BIRTH(st));
    info->access_time = ts_to_filetime(ST_ATIME(st));
    info->write_time = ts_to_filetime(ST_MTIME(st));
    info->change_time = ts_to_filetime(ST_CTIME(st));
    info->file_id = (uint64_t)st->st_ino;
    info->links = (uint32_t)st->st_nlink;
    uint32_t attrs = dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (!(st->st_mode & S_IWUSR))
        attrs |= FILE_ATTRIBUTE_READONLY;
    if (name && name[0] == '.' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
        attrs |= FILE_ATTRIBUTE_HIDDEN;
    info->attributes = attrs;
}

/*
 * windows names ignore case and host ones do not: when the path is missing
 * as written, each component after the volume root that is missing takes
 * the spelling of an entry of its directory that differs only in case. the
 * first missing one without such an entry, a file to create, stays as written.
 */
static void match_case(char *path, size_t root_length)
{
    struct stat st;
    if (lstat(path, &st) == 0)
        return;
    char *component = path + root_length;
    while (*component == '/')
        component++;
    while (*component) {
        char *end = strchr(component, '/');
        if (end)
            *end = 0;
        bool found = lstat(path, &st) == 0;
        if (!found) {
            char *dir_end = component - 1;
            *dir_end = 0;
            const char *dir_path = dir_end == path ? "/" : path;
            char spelling[256];
            bool known;
            found = strlen(component) < sizeof(spelling) && mime_dir_spelling(dir_path, component, spelling, &known);
            if (found) {
                memcpy(component, spelling, strlen(component));
            } else if (!known || strlen(component) >= sizeof(spelling)) {
                DIR *dir = opendir_quiet(dir_path);
                struct dirent *e;
                while (dir && (e = readdir(dir))) {
                    if (strlen(e->d_name) == strlen(component) && mime_names_same(e->d_name, component)) {
                        memcpy(component, e->d_name, strlen(component));
                        found = true;
                        break;
                    }
                }
                if (dir)
                    closedir(dir);
            }
            *dir_end = '/';
        }
        if (end)
            *end = '/';
        if (!found || !end)
            return;
        component = end + 1;
    }
}

/* converts a guest volume path to a host path, refusing to escape the volume. */
static bool host_path(MimeVm *vm, uint32_t volume, const uint16_t *path, uint32_t chars, char *out, size_t out_size)
{
    const char *root;
    if (volume == MIME_FS_VOLUME_SYSTEM)
        root = vm->paths->drive_c;
    else if (volume == MIME_FS_VOLUME_HOST)
        root = "";
    else
        return false;

    char utf8[MIME_PATH_MAX * 2];
    mime_utf16_to_utf8(utf8, sizeof(utf8), path, chars);

    /* split into components, normalising separators, '.' and '..'. */
    char *components[256];
    int depth = 0;
    char *save = nullptr;
    for (char *p = utf8; *p; p++) {
        if (*p == '\\')
            *p = '/';
    }
    for (char *tok = strtok_r(utf8, "/", &save); tok; tok = strtok_r(nullptr, "/", &save)) {
        if (strcmp(tok, ".") == 0)
            continue;
        if (strcmp(tok, "..") == 0) {
            if (depth > 0)
                depth--;
            continue;
        }
        if (depth == 256)
            return false;
        components[depth++] = tok;
    }

    size_t n = (size_t)snprintf(out, out_size, "%s", root);
    for (int i = 0; i < depth; i++) {
        n += (size_t)snprintf(out + n, n < out_size ? out_size - n : 0, "/%s", components[i]);
        if (n >= out_size)
            return false;
    }
    if (n == 0)
        snprintf(out, out_size, "/");
    if (depth)
        match_case(out, strlen(root));
    return true;
}

static void sd_memory_forget(const struct stat *st);

/* the registry journal, and the file a new one is written to before it takes the journal's place. */
static bool is_private_registry(MimeVm *vm, const char *path)
{
    const char *name = path + strlen(vm->paths->drive_c);
    return !strcasecmp(name, "/Windows/System32/config/mime-registry.log") ||
           !strcasecmp(name, "/Windows/System32/config/mime-registry.log.new");
}

/*
 * the windows are changed under their own lock on linux: taking a window away or placing one is a change of the
 * memory map that waits for every processor to leave the guest, a millisecond during which the other file calls
 * must not wait. macos fills windows when the guest touches them, with the file lock.
 */
#ifdef __linux__
#define WINDOW_LOCK(vm) (&(vm)->window_lock)
#else
#define WINDOW_LOCK(vm) (&(vm)->fs_lock)
#endif

/*
 * vm->fs_lock guards the table of handles, the windows and the stored descriptors, and is held for a lookup or
 * an update. Calls that can take the host long, such as opens, reads and flushes, run without it, as each one is a
 * guest thread waiting, and a flush that lasts 13 ms must not hold up every other file call of the machine.
 * A handle is used by one guest request at a time and closed after the last one ends.
 */
static MimeFsHandle *get_handle(MimeVm *vm, uint64_t h)
{
    if (h == 0 || h > MIME_VMM_MAX_FS_HANDLES)
        return nullptr;
    pthread_mutex_lock(&vm->fs_lock);
    MimeFsHandle *fh = vm->fs_handles[h - 1];
    pthread_mutex_unlock(&vm->fs_lock);
    return fh;
}

static uint64_t alloc_handle(MimeVm *vm, MimeFsHandle *fh)
{
    uint64_t h = 0;
    pthread_mutex_lock(&vm->fs_lock);
    for (uint32_t i = 0; i < MIME_VMM_MAX_FS_HANDLES; i++) {
        if (!vm->fs_handles[i]) {
            vm->fs_handles[i] = fh;
            h = i + 1;
            break;
        }
    }
    pthread_mutex_unlock(&vm->fs_lock);
    return h;
}

static void free_handle(MimeVm *vm, uint64_t h)
{
    MimeFsHandle *fh = nullptr;
    if (h && h <= MIME_VMM_MAX_FS_HANDLES) {
        pthread_mutex_lock(&vm->fs_lock);
        fh = vm->fs_handles[h - 1];
        vm->fs_handles[h - 1] = nullptr;
        pthread_mutex_unlock(&vm->fs_lock);
    }
    if (!fh)
        return;
    if (fh->fd >= 0)
        close(fh->fd);
    if (fh->delete_on_close && fh->host_path) {
        struct stat gone;
        if (stat(fh->host_path, &gone) == 0) {
            pthread_mutex_lock(&vm->fs_lock);
            sd_memory_forget(&gone);
            pthread_mutex_unlock(&vm->fs_lock);
        }
        mime_fswatch_self(fh->host_path, MIME_FSWATCH_REMOVE);
        if (fh->is_dir ? rmdir(fh->host_path) != 0 : unlink(fh->host_path) != 0)
            mime_fswatch_self(fh->host_path, MIME_FSWATCH_UNDO_REMOVE);
    }
    free(fh->host_path);
    free(fh->dir_entries);
    free(fh);
}

/* the times and the read only attribute of an open file, as a program sets them. */
static uint64_t fs_set_basic(MimeVm *vm, MimeFsHandle *fh, uint64_t pa)
{
    const MimeFsBasic *basic = mime_vm_phys(vm, pa, sizeof(*basic));
    if (!basic || fh->fd < 0)
        return MIME_HC_STATUS_INVALID;
    MimeFsBasic b = *basic;
    if (b.mask & (MIME_FS_BASIC_ACCESS | MIME_FS_BASIC_WRITE)) {
        struct timespec times[2] = {{.tv_nsec = UTIME_OMIT}, {.tv_nsec = UTIME_OMIT}};
        if (b.mask & MIME_FS_BASIC_ACCESS)
            times[0] = filetime_to_ts(b.access_time);
        if (b.mask & MIME_FS_BASIC_WRITE)
            times[1] = filetime_to_ts(b.write_time);
        if (futimens(fh->fd, times) != 0)
            return errno_status(errno);
    }
    /* a directory without write permission cannot have files made in it, which is not what read only means for it. */
    if ((b.mask & MIME_FS_BASIC_READONLY) && !fh->is_dir) {
        struct stat st;
        if (fstat(fh->fd, &st) != 0)
            return errno_status(errno);
        mode_t mode = st.st_mode & 07777;
        if (b.attributes & FILE_ATTRIBUTE_READONLY)
            mode &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
        else
            mode |= S_IWUSR;
        if (mode != (st.st_mode & 07777) && fchmod(fh->fd, mode) != 0)
            return errno_status(errno);
    }
    return MIME_HC_STATUS_OK;
}

static void open_fill_sd(MimeVm *vm, MimeFsOpen *req, const char *path);

static uint64_t fs_open(MimeVm *vm, MimeFsOpen *req)
{
    char path[MIME_PATH_MAX];
    if (req->path_chars > MIME_ARRAY_COUNT(req->path) ||
        !host_path(vm, req->volume, req->path, req->path_chars, path, sizeof(path)))
        return MIME_HC_STATUS_INVALID;

    struct stat st;
    bool exists = stat(path, &st) == 0;
    if (req->flags & MIME_FS_OPEN_STAT_ONLY) {
        /* the attributes of a name, for a query that has no use for a handle. */
        if (!exists) {
            char parent[MIME_PATH_MAX];
            mime_strlcpy(parent, path, sizeof(parent));
            char *slash = strrchr(parent, '/');
            if (slash && slash != parent)
                *slash = 0;
            req->action_out = mime_dir_exists(parent) ? 0 : 1;
            return MIME_HC_STATUS_NOT_FOUND;
        }
        bool is_dir = S_ISDIR(st.st_mode);
        if ((req->flags & MIME_FS_OPEN_DIRECTORY) && !is_dir)
            return MIME_HC_STATUS_NOT_DIRECTORY;
        if ((req->flags & MIME_FS_OPEN_NON_DIRECTORY) && is_dir)
            return MIME_HC_STATUS_IS_DIRECTORY;
        const char *leaf = strrchr(path, '/');
        fill_info(&st, leaf ? leaf + 1 : path, &req->info_out);
        req->handle_out = 0;
        req->action_out = FILE_OPENED;
        open_fill_sd(vm, req, path);
        return MIME_HC_STATUS_OK;
    }
    bool want_dir = req->flags & MIME_FS_OPEN_DIRECTORY;
    bool want_file = req->flags & MIME_FS_OPEN_NON_DIRECTORY;
    bool write = req->flags & MIME_FS_OPEN_WRITE;
    bool private_registry = req->volume == MIME_FS_VOLUME_SYSTEM && is_private_registry(vm, path);
    int private_flags = private_registry ? O_NOFOLLOW : 0;
    uint32_t action = FILE_OPENED;
    int fd;

    if (exists) {
        bool is_dir = S_ISDIR(st.st_mode);
        if (req->disposition == MIME_FS_DISP_CREATE)
            return MIME_HC_STATUS_EXISTS;
        if (want_dir && !is_dir)
            return MIME_HC_STATUS_NOT_DIRECTORY;
        if (want_file && is_dir)
            return MIME_HC_STATUS_IS_DIRECTORY;
        if (is_dir) {
            fd = open_quiet(path, O_RDONLY | O_DIRECTORY, 0);
        } else {
            int flags = write ? O_RDWR : O_RDONLY;
            if (req->disposition == MIME_FS_DISP_OVERWRITE || req->disposition == MIME_FS_DISP_OVERWRITE_IF ||
                req->disposition == MIME_FS_DISP_SUPERSEDE) {
                flags = O_RDWR | O_TRUNC;
                action = req->disposition == MIME_FS_DISP_SUPERSEDE ? FILE_SUPERSEDED : FILE_OVERWRITTEN;
            }
            if (flags & O_TRUNC)
                mime_fswatch_self(path, MIME_FSWATCH_MODIFY);
            fd = open_quiet(path, flags | private_flags, 0);
            if (fd < 0 && errno == EACCES && !write)
                fd = open_quiet(path, O_RDONLY | private_flags, 0);
        }
    } else {
        if (req->disposition == MIME_FS_DISP_OPEN || req->disposition == MIME_FS_DISP_OVERWRITE) {
            char parent[MIME_PATH_MAX];
            mime_strlcpy(parent, path, sizeof(parent));
            char *slash = strrchr(parent, '/');
            if (slash && slash != parent)
                *slash = 0;
            req->action_out = mime_dir_exists(parent) ? 0 : 1; /* 1 means a parent directory is missing */
            return MIME_HC_STATUS_NOT_FOUND;
        }
        /* the event of the making is the guest's own, noted before it can come. */
        mime_fswatch_self(path, MIME_FSWATCH_CREATE);
        if (want_dir) {
            if (mkdir(path, 0755) != 0) {
                mime_fswatch_self(path, MIME_FSWATCH_UNDO_CREATE);
                return errno_status(errno);
            }
            fd = open_quiet(path, O_RDONLY | O_DIRECTORY, 0);
        } else {
            fd = open(path, O_RDWR | O_CREAT | O_EXCL | private_flags, private_registry ? 0600 : 0644);
            if (fd < 0)
                mime_fswatch_self(path, MIME_FSWATCH_UNDO_CREATE);
        }
        action = FILE_CREATED;
    }
    if (fd < 0)
        return errno_status(errno);
    if (fstat(fd, &st) != 0 || (private_registry && S_ISREG(st.st_mode) && fchmod(fd, 0600) != 0)) {
        uint64_t status = errno_status(errno);
        close(fd);
        return status;
    }
    if (private_registry && S_ISREG(st.st_mode) && (action == FILE_CREATED || st.st_size == 0)) {
        /* retry the directory sync until a record is in the journal, when the entry is known to be there. */
        uint64_t status = flush_parent(path);
        if (status != MIME_HC_STATUS_OK) {
            close(fd);
            return status;
        }
    }

    MimeFsHandle *fh = mime_xcalloc(1, sizeof(*fh));
    fh->fd = fd;
    fh->is_dir = S_ISDIR(st.st_mode);
    fh->volume = req->volume;
    fh->host_path = mime_xstrdup(path);
    uint64_t h = alloc_handle(vm, fh);
    if (!h) {
        close(fd);
        free(fh->host_path);
        free(fh);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    const char *leaf = strrchr(path, '/');
    fill_info(&st, leaf ? leaf + 1 : path, &req->info_out);
    req->handle_out = h;
    req->action_out = action;
    open_fill_sd(vm, req, path);
    return MIME_HC_STATUS_OK;
}

static int dirent_cmp(const void *a, const void *b)
{
    const MimeFsDirEntry *x = a, *y = b;
    uint32_t n = MIME_MIN(x->name_chars, y->name_chars);
    for (uint32_t i = 0; i < n; i++) {
        uint16_t cx = x->name[i], cy = y->name[i];
        if (cx >= 'a' && cx <= 'z')
            cx -= 32;
        if (cy >= 'a' && cy <= 'z')
            cy -= 32;
        if (cx != cy)
            return cx < cy ? -1 : 1;
    }
    return (int)x->name_chars - (int)y->name_chars;
}

static void load_dir(MimeFsHandle *fh)
{
    DIR *dir = opendir_quiet(fh->host_path);
    if (!dir)
        return;
    uint32_t capacity = 64;
    MimeFsDirEntry *entries = mime_xmalloc(capacity * sizeof(MimeFsDirEntry));
    uint32_t count = 0;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char full[MIME_PATH_MAX];
        mime_path_join(full, sizeof(full), fh->host_path, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        if (count == capacity) {
            capacity *= 2;
            entries = mime_xrealloc(entries, capacity * sizeof(MimeFsDirEntry));
        }
        MimeFsDirEntry *de = &entries[count++];
        memset(de, 0, sizeof(*de));
        fill_info(&st, e->d_name, &de->info);
        de->name_chars = (uint32_t)mime_utf8_to_utf16(de->name, MIME_ARRAY_COUNT(de->name), e->d_name);
    }
    closedir(dir);
    qsort(entries, count, sizeof(MimeFsDirEntry), dirent_cmp);
    fh->dir_entries = entries;
    fh->dir_count = count;
}


/*
 * security descriptors of files. They are the guest's, kept with the file in an extended attribute so they
 * follow it and survive a restart. A file system that refuses attributes, and the host's own files that the
 * machine has no business marking, keep theirs in memory for the session, by device and inode.
 */
#ifdef __APPLE__
#define SD_ATTRIBUTE "mime.security"
#define sd_get_attribute(path, buf, size) getxattr((path), SD_ATTRIBUTE, (buf), (size), 0, 0)
#define sd_set_attribute(path, buf, size) setxattr((path), SD_ATTRIBUTE, (buf), (size), 0, 0)
#define sd_remove_attribute(path) removexattr((path), SD_ATTRIBUTE, 0)
#else
#define SD_ATTRIBUTE "user.mime.security"
#define sd_get_attribute(path, buf, size) getxattr((path), SD_ATTRIBUTE, (buf), (size))
#define sd_set_attribute(path, buf, size) setxattr((path), SD_ATTRIBUTE, (buf), (size), 0)
#define sd_remove_attribute(path) removexattr((path), SD_ATTRIBUTE)
#endif

#define SD_MAX_BYTES (64u << 10)

typedef struct SdMemory {
    dev_t dev;
    ino_t ino;
    uint32_t length;
    uint8_t *data;
} SdMemory;

static SdMemory *g_sd_memory;
static size_t g_sd_memory_count;

static SdMemory *sd_memory_find(const struct stat *st)
{
    for (size_t i = 0; i < g_sd_memory_count; i++)
        if (g_sd_memory[i].dev == st->st_dev && g_sd_memory[i].ino == st->st_ino)
            return &g_sd_memory[i];
    return NULL;
}

static void sd_memory_forget(const struct stat *st)
{
    SdMemory *m = sd_memory_find(st);
    if (!m)
        return;
    free(m->data);
    *m = g_sd_memory[--g_sd_memory_count];
}

static void sd_memory_set(const struct stat *st, const void *data, uint32_t length)
{
    SdMemory *m = sd_memory_find(st);
    if (!length) {
        sd_memory_forget(st);
        return;
    }
    if (!m) {
        g_sd_memory = mime_xrealloc(g_sd_memory, (g_sd_memory_count + 1) * sizeof(*g_sd_memory));
        m = &g_sd_memory[g_sd_memory_count++];
        m->dev = st->st_dev;
        m->ino = st->st_ino;
        m->data = NULL;
    }
    m->data = mime_xrealloc(m->data, length);
    memcpy(m->data, data, length);
    m->length = length;
}

/* the descriptor stored for one path: its length, 0 for none, and the bytes when they fit. */
static ssize_t sd_read_path(const char *path, bool memory_only, void *buf, size_t capacity)
{
    if (!memory_only) {
        ssize_t n = sd_get_attribute(path, buf, capacity);
        if (n >= 0)
            return n;
        if (errno == ERANGE) {
            n = sd_get_attribute(path, NULL, 0);
            if (n >= 0)
                return n;
        }
    }
    /* the table only holds what a file system that keeps no attributes could not keep, usually nothing. */
    if (!g_sd_memory_count)
        return 0;
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    SdMemory *m = sd_memory_find(&st);
    if (!m)
        return 0;
    if (m->length <= capacity)
        memcpy(buf, m->data, m->length);
    return m->length;
}

/*
 * the directories known to have no descriptor of their own, by a hash of their path. Nearly every open asks the
 * directories above its file, and nearly none has one, so a hit saves a call to the file system for each. A new
 * generation, from every change the guest makes to a descriptor or to a name, forgets them all; a descriptor set
 * on the files by a program of the host in the meantime is not seen until then.
 */
#define SD_NEGATIVE_SLOTS 4096
static struct {
    uint64_t hash;
    uint32_t generation;
} g_sd_negative[SD_NEGATIVE_SLOTS];
static uint32_t g_sd_generation = 1;

static uint64_t path_hash(const char *path, size_t length)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < length; i++)
        h = (h ^ (uint8_t)path[i]) * 1099511628211ull;
    return h | 1;
}

static bool sd_known_empty(const char *path, size_t length)
{
    uint64_t h = path_hash(path, length);
    return g_sd_negative[h % SD_NEGATIVE_SLOTS].hash == h && g_sd_negative[h % SD_NEGATIVE_SLOTS].generation == g_sd_generation;
}

static void sd_note_empty(const char *path, size_t length)
{
    uint64_t h = path_hash(path, length);
    g_sd_negative[h % SD_NEGATIVE_SLOTS].hash = h;
    g_sd_negative[h % SD_NEGATIVE_SLOTS].generation = g_sd_generation;
}

/*
 * the nearest descriptor from a file up: the file's own, else its parent's and so on to the volume root. The
 * answer is the distance in levels in the upper half and the length in the lower, 0 length for none.
 */
static uint64_t sd_nearest(MimeVm *vm, uint32_t volume, const char *host_path, void *buf, uint64_t capacity, uint64_t up,
                           uint64_t *result)
{
    bool system = volume == MIME_FS_VOLUME_SYSTEM;
    char path[MIME_PATH_MAX];
    mime_strlcpy(path, host_path, sizeof(path));
    size_t root = system ? strlen(vm->paths->drive_c) : 0;
    uint64_t distance = 0;
    for (uint64_t i = 0; i < up; i++) {
        char *slash = strrchr(path, '/');
        if (!slash || (size_t)(slash - path) < root || !system)
            return MIME_HC_STATUS_NOT_FOUND;
        *slash = 0;
        if (!path[0])
            return MIME_HC_STATUS_NOT_FOUND;
    }
    uint8_t scratch[16];
    for (;;) {
        size_t length = strlen(path);
        /* only a directory above the file is remembered as having none: the file itself is asked every time. */
        bool above = system && (distance > 0 || up > 0);
        ssize_t n = above && sd_known_empty(path, length) ? 0 : sd_read_path(path, !system, buf ? buf : scratch, buf ? capacity : 0);
        if (n > 0) {
            *result = (distance << 32) | (uint64_t)n;
            return (uint64_t)n > capacity ? MIME_HC_STATUS_NO_MEMORY : MIME_HC_STATUS_OK;
        }
        if (above && n == 0)
            sd_note_empty(path, length);
        if (!system || length <= root)
            break;
        char *slash = strrchr(path, '/');
        if (!slash || (size_t)(slash - path) < root)
            break;
        *slash = 0;
        distance++;
    }
    *result = 0;
    return MIME_HC_STATUS_OK;
}

/* the nearest stored descriptor of an open that asked for it, so that the guest which checks the open needs no second exit. */
static void open_fill_sd(MimeVm *vm, MimeFsOpen *req, const char *path)
{
    req->sd_length = 0;
    req->sd_distance = 0;
    req->sd_status = MIME_HC_STATUS_OK;
    if (!(req->flags & MIME_FS_OPEN_WANT_SD))
        return;
    uint64_t result = 0;
    pthread_mutex_lock(&vm->fs_lock);
    uint64_t status = sd_nearest(vm, req->volume, path, req->sd_data, sizeof(req->sd_data), 0, &result);
    pthread_mutex_unlock(&vm->fs_lock);
    req->sd_status = (uint32_t)status;
    req->sd_length = (uint32_t)result;
    req->sd_distance = (uint32_t)(result >> 32);
}

static uint64_t fs_get_sd(MimeVm *vm, MimeFsHandle *fh, uint64_t pa, uint64_t capacity, uint64_t up, uint64_t *result)
{
    void *buf = capacity ? mime_vm_phys(vm, pa, capacity) : NULL;
    if (capacity && !buf)
        return MIME_HC_STATUS_INVALID;
    return sd_nearest(vm, fh->volume, fh->host_path, buf, capacity, up, result);
}

static uint64_t fs_set_sd(MimeVm *vm, MimeFsHandle *fh, uint64_t pa, uint64_t length)
{
    g_sd_generation++;
    const void *buf = length ? mime_vm_phys(vm, pa, length) : NULL;
    if (length > SD_MAX_BYTES || (length && !buf))
        return MIME_HC_STATUS_INVALID;
    struct stat st;
    if (stat(fh->host_path, &st) != 0)
        return errno_status(errno);
    if (fh->volume == MIME_FS_VOLUME_SYSTEM) {
        if (!length) {
            sd_remove_attribute(fh->host_path);
            sd_memory_forget(&st);
            return MIME_HC_STATUS_OK;
        }
        if (sd_set_attribute(fh->host_path, buf, length) == 0) {
            sd_memory_forget(&st);
            return MIME_HC_STATUS_OK;
        }
    }
    sd_memory_set(&st, buf, (uint32_t)length);
    return MIME_HC_STATUS_OK;
}

/*
 * moves the file or directory of a handle to a name in the directory of another. the names of windows
 * ignore case, so an entry of the directory that differs from the new name by case only is the name taken
 * when the file replaces it, and the name asked for when it is the file itself, a rename that changes case.
 * the paths of the handles open on the moved file, or below a moved directory, follow it.
 */
static uint64_t fs_rename(MimeVm *vm, const uint64_t a[8])
{
    MimeFsHandle *src = get_handle(vm, a[1]), *dir = get_handle(vm, a[2]);
    if (!src || !dir || !dir->is_dir || src->volume != dir->volume || !src->host_path || !dir->host_path)
        return MIME_HC_STATUS_INVALID;
    const uint16_t *wide = a[4] && a[4] < 256 ? mime_vm_phys(vm, a[3], a[4] * 2) : nullptr;
    if (!wide)
        return MIME_HC_STATUS_INVALID;
    char leaf[1024];
    mime_utf16_to_utf8(leaf, sizeof(leaf), wide, a[4]);
    if (!leaf[0] || strchr(leaf, '/') || !strcmp(leaf, ".") || !strcmp(leaf, ".."))
        return MIME_HC_STATUS_INVALID;
    bool replace = (a[5] & MIME_FS_RENAME_REPLACE) != 0;

    struct stat from;
    if (lstat(src->host_path, &from) != 0)
        return errno_status(errno);
    char target[MIME_PATH_MAX], found[1024] = "";
    mime_path_join(target, sizeof(target), dir->host_path, leaf);
    struct stat there;
    bool exists = lstat(target, &there) == 0;
    if (!exists) {
        bool known;
        if (!mime_dir_spelling(dir->host_path, leaf, found, &known) && !known) {
            DIR *d = opendir_quiet(dir->host_path);
            struct dirent *e;
            while (d && (e = readdir(d))) {
                if (strlen(e->d_name) == strlen(leaf) && mime_names_same(e->d_name, leaf)) {
                    mime_strlcpy(found, e->d_name, sizeof(found));
                    break;
                }
            }
            if (d)
                closedir(d);
        }
        if (found[0]) {
            char twin[MIME_PATH_MAX];
            mime_path_join(twin, sizeof(twin), dir->host_path, found);
            exists = lstat(twin, &there) == 0;
            if (exists && !(there.st_dev == from.st_dev && there.st_ino == from.st_ino))
                mime_strlcpy(target, twin, sizeof(target));
        }
    }
    bool same = exists && there.st_dev == from.st_dev && there.st_ino == from.st_ino;
    if (exists && !same) {
        if (!replace)
            return MIME_HC_STATUS_EXISTS;
        /* a directory is never replaced, and neither is a file by a directory. */
        if (S_ISDIR(there.st_mode) || S_ISDIR(from.st_mode))
            return MIME_HC_STATUS_ACCESS_DENIED;
    }
    if (!strcmp(src->host_path, target))
        return MIME_HC_STATUS_OK;
    size_t from_length = strlen(src->host_path);
    if (S_ISDIR(from.st_mode) && !strncmp(target, src->host_path, from_length) && target[from_length] == '/')
        return MIME_HC_STATUS_INVALID;
    mime_fswatch_self(src->host_path, MIME_FSWATCH_MOVE_FROM);
    mime_fswatch_self(target, MIME_FSWATCH_MOVE_TO);
    if (rename(src->host_path, target) != 0) {
        mime_fswatch_self(src->host_path, MIME_FSWATCH_UNDO_MOVE);
        mime_fswatch_self(target, MIME_FSWATCH_UNDO_MOVE);
        return errno_status(errno);
    }
    /* the journal takes its place for good only when the directory says so. */
    if (src->volume == MIME_FS_VOLUME_SYSTEM && is_private_registry(vm, target))
        flush_parent(target);

    char *old = mime_xstrdup(src->host_path);
    pthread_mutex_lock(&vm->fs_lock);
    g_sd_generation++;
    for (uint32_t i = 0; i < MIME_VMM_MAX_FS_HANDLES; i++) {
        MimeFsHandle *fh = vm->fs_handles[i];
        if (!fh || !fh->host_path)
            continue;
        if (exists && !same && !strcmp(fh->host_path, target)) {
            /* the file that was replaced has no name any more, so closing it must not delete the new one. */
            fh->delete_on_close = false;
        } else if (!strncmp(fh->host_path, old, from_length) &&
                   (fh->host_path[from_length] == '/' || !fh->host_path[from_length])) {
            char moved[MIME_PATH_MAX];
            snprintf(moved, sizeof(moved), "%s%s", target, fh->host_path + from_length);
            free(fh->host_path);
            fh->host_path = mime_xstrdup(moved);
        }
    }
    pthread_mutex_unlock(&vm->fs_lock);
    free(old);
    return MIME_HC_STATUS_OK;
}

uint64_t mime_fs_hcall(MimeVm *vm, uint64_t nr, uint64_t regs[8])
{
    uint64_t status = MIME_HC_STATUS_OK;
    uint64_t a[8]; /* arguments, regs[1] doubles as the result */
    memcpy(a, regs, sizeof(a));
    regs[1] = 0;
    switch (nr) {
    case MIME_HC_FS_OPEN: {
        MimeFsOpen *req = mime_vm_phys(vm, a[1], sizeof(MimeFsOpen));
        status = req ? fs_open(vm, req) : MIME_HC_STATUS_INVALID;
        break;
    }
    case MIME_HC_FS_CLOSE:
        free_handle(vm, a[1]);
        break;
    case MIME_HC_FS_FLUSH: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        if (!fh) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        /* the flush takes milliseconds and the other files must not wait for it, so it runs on a duplicate. */
        int fd = dup(fh->fd);
        if (fd < 0) {
            status = errno_status(errno);
            break;
        }
        status = flush_fd(fd);
        close(fd);
        return status;
    }
    case MIME_HC_FS_READ:
    case MIME_HC_FS_WRITE: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        void *buf = mime_vm_phys(vm, a[3], a[4]);
        if (!fh || !buf) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        if (fh->is_dir) {
            status = MIME_HC_STATUS_IS_DIRECTORY;
            break;
        }
        if (nr == MIME_HC_FS_WRITE)
            mime_fswatch_self(fh->host_path, MIME_FSWATCH_MODIFY);
        ssize_t n;
        if (nr == MIME_HC_FS_READ) {
            n = pread(fh->fd, buf, (size_t)a[4], (off_t)a[2]);
        } else {
            n = 0;
            if (a[4] > 1 && fault_hits(fh->host_path)) {
                n = pwrite(fh->fd, buf, (size_t)(a[4] / 2), (off_t)a[2]);
                mime_warn("fs: write of %llu bytes at %llu to %s cut to %lld bytes", (unsigned long long)a[4],
                          (unsigned long long)a[2], fh->host_path, (long long)n);
            } else {
                /* a write that comes back short is asked again for the rest: the guest takes a short count for a full disk. */
                while ((uint64_t)n < a[4]) {
                    ssize_t part = pwrite(fh->fd, (const char *)buf + n, (size_t)(a[4] - (uint64_t)n),
                                          (off_t)(a[2] + (uint64_t)n));
                    if (part < 0 && errno == EINTR)
                        continue;
                    if (part == 0)
                        errno = ENOSPC;
                    if (part <= 0) {
                        if (part < 0 && n == 0)
                            n = -1;
                        break;
                    }
                    n += part;
                }
                if (n >= 0 && (uint64_t)n < a[4])
                    mime_warn("fs: write of %llu bytes at %llu to %s stopped after %lld bytes: %s", (unsigned long long)a[4],
                              (unsigned long long)a[2], fh->host_path, (long long)n, strerror(errno));
            }
            mime_fswatch_self(fh->host_path, MIME_FSWATCH_MODIFY);
        }
        if (n < 0)
            status = errno_status(errno);
        else
            regs[1] = (uint64_t)n;
        break;
    }
    case MIME_HC_FS_VOLUME_QUERY: {
        MimeFsVolumeInfo *info = mime_vm_phys(vm, a[2], sizeof(*info));
        const char *root = a[1] == MIME_FS_VOLUME_SYSTEM ? vm->paths->drive_c :
                           a[1] == MIME_FS_VOLUME_HOST ? "/" : nullptr;
        if (!info || !root) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        struct statfs volume;
        if (statfs(root, &volume) != 0) {
            status = errno_status(errno);
            break;
        }
        memset(info, 0, sizeof(*info));
        info->total_units = volume.f_blocks;
        info->available_units = volume.f_bavail;
        info->free_units = volume.f_bfree;
        info->bytes_per_unit = volume.f_bsize;
        break;
    }
    case MIME_HC_FS_QUERY: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        MimeFsInfo *info = mime_vm_phys(vm, a[2], sizeof(MimeFsInfo));
        struct stat st;
        if (!fh || !info) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        if (fstat(fh->fd, &st) != 0) {
            status = errno_status(errno);
            break;
        }
        const char *name = strrchr(fh->host_path, '/');
        fill_info(&st, name ? name + 1 : fh->host_path, info);
        break;
    }
    case MIME_HC_FS_READDIR: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        /* a guest that asks for several entries at once pays one exit for them, a directory of hundreds one for each otherwise. */
        uint64_t want = a[4] ? MIME_MIN(a[4], 64u) : 1;
        MimeFsDirEntry *de = mime_vm_phys(vm, a[3], want * sizeof(MimeFsDirEntry));
        if (!fh || !de || !fh->is_dir) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        if (a[2] == 0 || !fh->dir_entries) {
            free(fh->dir_entries);
            fh->dir_entries = nullptr;
            fh->dir_count = 0;
            load_dir(fh);
        }
        uint64_t n = 0;
        while (n < want && a[2] + n < fh->dir_count) {
            de[n] = ((MimeFsDirEntry *)fh->dir_entries)[a[2] + n];
            n++;
        }
        regs[1] = n;
        break;
    }
    case MIME_HC_FS_SET_SIZE: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        if (!fh || fh->is_dir)
            status = MIME_HC_STATUS_INVALID;
        else {
            mime_fswatch_self(fh->host_path, MIME_FSWATCH_MODIFY);
            if (ftruncate(fh->fd, (off_t)a[2]) != 0)
                status = errno_status(errno);
            mime_fswatch_self(fh->host_path, MIME_FSWATCH_MODIFY);
        }
        break;
    }
    case MIME_HC_FS_SET_BASIC: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        status = fh ? fs_set_basic(vm, fh, a[2]) : MIME_HC_STATUS_INVALID;
        if (status == MIME_HC_STATUS_OK && fh->host_path)
            mime_fswatch_self(fh->host_path, MIME_FSWATCH_MODIFY);
        break;
    }
    case MIME_HC_FS_WATCH: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        status = fh && fh->is_dir && fh->host_path ? mime_fswatch_add(vm, fh->host_path, (uint32_t)a[2], (uint32_t)a[3], a[4] != 0)
                                                    : MIME_HC_STATUS_INVALID;
        break;
    }
    case MIME_HC_FS_UNWATCH:
        mime_fswatch_remove((uint32_t)a[1]);
        break;
    case MIME_HC_FS_CHANGES:
        status = mime_fswatch_fetch(vm, a[1], a[2], &regs[1]);
        break;
    case MIME_HC_FS_GET_SD: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        pthread_mutex_lock(&vm->fs_lock);
        status = fh ? fs_get_sd(vm, fh, a[2], a[3], a[4], &regs[1]) : MIME_HC_STATUS_INVALID;
        pthread_mutex_unlock(&vm->fs_lock);
        break;
    }
    case MIME_HC_FS_SET_SD: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        pthread_mutex_lock(&vm->fs_lock);
        if (fh)
            mime_fswatch_self(fh->host_path, MIME_FSWATCH_MODIFY);
        status = fh ? fs_set_sd(vm, fh, a[2], a[3]) : MIME_HC_STATUS_INVALID;
        if (fh)
            mime_fswatch_self(fh->host_path, MIME_FSWATCH_MODIFY);
        pthread_mutex_unlock(&vm->fs_lock);
        break;
    }
    case MIME_HC_FS_DELETE: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        if (!fh)
            status = MIME_HC_STATUS_INVALID;
        else
            fh->delete_on_close = a[2] != 0;
        break;
    }
    case MIME_HC_FS_RENAME:
        status = fs_rename(vm, a);
        break;
    case MIME_HC_FS_MAP: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        pthread_mutex_lock(WINDOW_LOCK(vm));
        status = fh && !fh->is_dir ? mime_window_map(vm, fh->fd, (a[2] & MIME_FS_MAP_WRITABLE) != 0, &regs[1])
                                    : MIME_HC_STATUS_INVALID;
        pthread_mutex_unlock(WINDOW_LOCK(vm));
        break;
    }
    case MIME_HC_FS_WARM:
#ifdef __linux__
        pthread_mutex_lock(WINDOW_LOCK(vm));
        mime_window_warm(vm, a[1]);
        pthread_mutex_unlock(WINDOW_LOCK(vm));
#endif
        break;
    case MIME_HC_FS_WINDOW_LOST:
#ifdef __linux__
        status = mime_window_lost(vm, a[1]);
#else
        status = MIME_HC_STATUS_UNSUPPORTED;
#endif
        break;
    case MIME_HC_FS_UNMAP:
        pthread_mutex_lock(WINDOW_LOCK(vm));
        status = mime_window_unmap(vm, a[1]);
        pthread_mutex_unlock(WINDOW_LOCK(vm));
        break;
    case MIME_HC_FS_SYNC: {
        /* the machine is going down: whatever the programs wrote reaches the disk of the drive, not just the host's cache. */
        int fd = open(vm->paths->drive_c, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0) {
            status = errno_status(errno);
            break;
        }
#ifdef __linux__
        status = syncfs(fd) == 0 ? MIME_HC_STATUS_OK : errno_status(errno);
#else
        sync();
#endif
        close(fd);
        break;
    }
    default:
        status = MIME_HC_STATUS_UNSUPPORTED;
        break;
    }
    return status;
}

void mime_fs_close_all(MimeVm *vm)
{
    mime_fswatch_stop();
    for (uint32_t i = 0; i < MIME_VMM_MAX_FS_HANDLES; i++) {
        MimeFsHandle *fh = get_handle(vm, i + 1);
        if (fh) {
            fh->delete_on_close = false;
            free_handle(vm, i + 1);
        }
    }
    pthread_mutex_lock(WINDOW_LOCK(vm));
    mime_window_unmap_all(vm);
    pthread_mutex_unlock(WINDOW_LOCK(vm));
}
