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

static uint64_t ts_to_filetime(struct timespec ts)
{
    return ((uint64_t)ts.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)ts.tv_nsec / 100;
}

static void fill_info(const struct stat *st, const char *name, MimeFsInfo *info)
{
    memset(info, 0, sizeof(*info));
    bool dir = S_ISDIR(st->st_mode);
    info->size = dir ? 0 : (uint64_t)st->st_size;
    info->allocation_size = dir ? 0 : (uint64_t)st->st_blocks * 512;
    info->creation_time = ts_to_filetime(st->st_birthtimespec);
    info->access_time = ts_to_filetime(st->st_atimespec);
    info->write_time = ts_to_filetime(st->st_mtimespec);
    info->change_time = ts_to_filetime(st->st_ctimespec);
    info->file_id = (uint64_t)st->st_ino;
    info->links = (uint32_t)st->st_nlink;
    uint32_t attrs = dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (!(st->st_mode & S_IWUSR))
        attrs |= FILE_ATTRIBUTE_READONLY;
    if (name && name[0] == '.' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
        attrs |= FILE_ATTRIBUTE_HIDDEN;
    info->attributes = attrs;
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
    char *save = NULL;
    for (char *p = utf8; *p; p++) {
        if (*p == '\\')
            *p = '/';
    }
    for (char *tok = strtok_r(utf8, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
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
    return true;
}

static MimeFsHandle *get_handle(MimeVm *vm, uint64_t h)
{
    if (h == 0 || h > MIME_VMM_MAX_FS_HANDLES)
        return NULL;
    return vm->fs_handles[h - 1];
}

static uint64_t alloc_handle(MimeVm *vm, MimeFsHandle *fh)
{
    for (uint32_t i = 0; i < MIME_VMM_MAX_FS_HANDLES; i++) {
        if (!vm->fs_handles[i]) {
            vm->fs_handles[i] = fh;
            return i + 1;
        }
    }
    return 0;
}

static void free_handle(MimeVm *vm, uint64_t h)
{
    MimeFsHandle *fh = get_handle(vm, h);
    if (!fh)
        return;
    if (fh->fd >= 0)
        close(fh->fd);
    if (fh->delete_on_close && fh->host_path) {
        if (fh->is_dir)
            rmdir(fh->host_path);
        else
            unlink(fh->host_path);
    }
    free(fh->host_path);
    free(fh->dir_entries);
    free(fh);
    vm->fs_handles[h - 1] = NULL;
}

static uint64_t fs_open(MimeVm *vm, MimeFsOpen *req)
{
    char path[MIME_PATH_MAX];
    if (req->path_chars > MIME_ARRAY_COUNT(req->path) ||
        !host_path(vm, req->volume, req->path, req->path_chars, path, sizeof(path)))
        return MIME_HC_STATUS_INVALID;

    struct stat st;
    bool exists = stat(path, &st) == 0;
    bool want_dir = req->flags & MIME_FS_OPEN_DIRECTORY;
    bool want_file = req->flags & MIME_FS_OPEN_NON_DIRECTORY;
    bool write = req->flags & MIME_FS_OPEN_WRITE;
    bool private_registry = req->volume == MIME_FS_VOLUME_SYSTEM &&
                            !strcasecmp(path + strlen(vm->paths->drive_c),
                                        "/Windows/System32/config/mime-registry.log");
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
            fd = open(path, O_RDONLY | O_DIRECTORY);
        } else {
            int flags = write ? O_RDWR : O_RDONLY;
            if (req->disposition == MIME_FS_DISP_OVERWRITE || req->disposition == MIME_FS_DISP_OVERWRITE_IF ||
                req->disposition == MIME_FS_DISP_SUPERSEDE) {
                flags = O_RDWR | O_TRUNC;
                action = req->disposition == MIME_FS_DISP_SUPERSEDE ? FILE_SUPERSEDED : FILE_OVERWRITTEN;
            }
            fd = open(path, flags | private_flags);
            if (fd < 0 && errno == EACCES && !write)
                fd = open(path, O_RDONLY | private_flags);
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
        if (want_dir) {
            if (mkdir(path, 0755) != 0)
                return errno_status(errno);
            fd = open(path, O_RDONLY | O_DIRECTORY);
        } else {
            fd = open(path, O_RDWR | O_CREAT | O_EXCL | private_flags, private_registry ? 0600 : 0644);
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
    if (private_registry && S_ISREG(st.st_mode)) {
        /* retry directory sync even when an earlier open already created the journal. */
        uint64_t status = flush_parent(path);
        if (status != MIME_HC_STATUS_OK) {
            close(fd);
            return status;
        }
    }

    MimeFsHandle *fh = mime_xcalloc(1, sizeof(*fh));
    fh->fd = fd;
    fh->is_dir = S_ISDIR(st.st_mode);
    fh->host_path = mime_xstrdup(path);
    uint64_t h = alloc_handle(vm, fh);
    if (!h) {
        close(fd);
        free(fh->host_path);
        free(fh);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    req->handle_out = h;
    req->action_out = action;
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
    DIR *dir = opendir(fh->host_path);
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

#define WINDOW_BUDGET (64ull << 20) /* filled window bytes kept before older windows are emptied */

/* places a file read only at the first free window address, first fit. nothing is read yet. */
static uint64_t window_map(MimeVm *vm, MimeFsHandle *fh, uint64_t *ipa_out)
{
    struct stat st;
    if (fh->is_dir || fstat(fh->fd, &st) != 0 || st.st_size <= 0)
        return MIME_HC_STATUS_INVALID;
    if (vm->window_count == MIME_VMM_MAX_WINDOWS)
        return MIME_HC_STATUS_NO_MEMORY;
    uint64_t size = MIME_ALIGN_UP((uint64_t)st.st_size, (uint64_t)MIME_WINDOW_ALIGN);
    uint64_t ipa = MIME_WINDOW_BASE;
    uint32_t at = 0;
    for (; at < vm->window_count; at++) {
        if (ipa + size <= vm->windows[at].ipa)
            break;
        ipa = vm->windows[at].ipa + MIME_ALIGN_UP(vm->windows[at].size, (uint64_t)MIME_WINDOW_ALIGN);
    }
    if (ipa + size > MIME_WINDOW_BASE + MIME_WINDOW_SIZE)
        return MIME_HC_STATUS_NO_MEMORY;
    int fd = dup(fh->fd);
    if (fd < 0)
        return errno_status(errno);
    MimeLazyRegion r;
    if (!mime_lazy_init(&r, ipa, size, fd, NULL, (uint64_t)st.st_size)) {
        close(fd);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    memmove(&vm->windows[at + 1], &vm->windows[at], (vm->window_count - at) * sizeof(MimeLazyRegion));
    vm->windows[at] = r;
    vm->window_count++;
    *ipa_out = ipa;
    return MIME_HC_STATUS_OK;
}

static MimeLazyRegion *window_find(MimeVm *vm, uint64_t ipa)
{
    for (uint32_t i = 0; i < vm->window_count; i++)
        if (mime_lazy_contains(&vm->windows[i], ipa))
            return &vm->windows[i];
    return NULL;
}

static void window_unmap(MimeVm *vm, MimeLazyRegion *w)
{
    vm->window_bytes -= w->filled_bytes;
    mime_lazy_free(w);
    uint32_t at = (uint32_t)(w - vm->windows);
    memmove(w, w + 1, (vm->window_count - at - 1) * sizeof(MimeLazyRegion));
    vm->window_count--;
}

/* empties the least recently filled windows until a chunk fits the budget. */
static void window_trim(MimeVm *vm, MimeLazyRegion *keep)
{
    while (vm->window_bytes + MIME_LAZY_CHUNK > WINDOW_BUDGET) {
        MimeLazyRegion *oldest = NULL;
        for (uint32_t i = 0; i < vm->window_count; i++) {
            MimeLazyRegion *w = &vm->windows[i];
            if (w != keep && w->filled_bytes && (!oldest || w->last_use < oldest->last_use))
                oldest = w;
        }
        if (!oldest)
            return;
        vm->window_bytes -= oldest->filled_bytes;
        mime_lazy_drop(oldest);
    }
}

/* a stage 2 fault in the rom or a window: fills the touched chunk. */
bool mime_fs_lazy_fault(MimeVm *vm, uint64_t ipa)
{
    pthread_mutex_lock(&vm->fs_lock);
    bool handled;
    MimeLazyRegion *w;
    if (mime_lazy_contains(&vm->rom, ipa)) {
        handled = mime_lazy_fill(&vm->rom, ipa);
    } else if ((w = window_find(vm, ipa))) {
        window_trim(vm, w);
        uint64_t before = w->filled_bytes;
        handled = mime_lazy_fill(w, ipa);
        vm->window_bytes += w->filled_bytes - before;
        w->last_use = ++vm->window_clock;
    } else {
        handled = false;
    }
    pthread_mutex_unlock(&vm->fs_lock);
    return handled;
}

uint64_t mime_fs_hcall(MimeVm *vm, uint64_t nr, uint64_t regs[8])
{
    uint64_t status = MIME_HC_STATUS_OK;
    pthread_mutex_lock(&vm->fs_lock);
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
        status = flush_fd(fh->fd);
        break;
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
        ssize_t n = nr == MIME_HC_FS_READ ? pread(fh->fd, buf, (size_t)a[4], (off_t)a[2])
                                          : pwrite(fh->fd, buf, (size_t)a[4], (off_t)a[2]);
        if (n < 0)
            status = errno_status(errno);
        else
            regs[1] = (uint64_t)n;
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
        MimeFsDirEntry *de = mime_vm_phys(vm, a[3], sizeof(MimeFsDirEntry));
        if (!fh || !de || !fh->is_dir) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        if (a[2] == 0 || !fh->dir_entries) {
            free(fh->dir_entries);
            fh->dir_entries = NULL;
            fh->dir_count = 0;
            load_dir(fh);
        }
        if (a[2] < fh->dir_count) {
            *de = ((MimeFsDirEntry *)fh->dir_entries)[a[2]];
            regs[1] = 1;
        }
        break;
    }
    case MIME_HC_FS_SET_SIZE: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        if (!fh || fh->is_dir)
            status = MIME_HC_STATUS_INVALID;
        else if (ftruncate(fh->fd, (off_t)a[2]) != 0)
            status = errno_status(errno);
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
    case MIME_HC_FS_MAP: {
        MimeFsHandle *fh = get_handle(vm, a[1]);
        status = fh ? window_map(vm, fh, &regs[1]) : MIME_HC_STATUS_INVALID;
        break;
    }
    case MIME_HC_FS_UNMAP: {
        MimeLazyRegion *w = window_find(vm, a[1]);
        if (w && w->ipa == a[1])
            window_unmap(vm, w);
        else
            status = MIME_HC_STATUS_INVALID;
        break;
    }
    default:
        status = MIME_HC_STATUS_UNSUPPORTED;
        break;
    }
    pthread_mutex_unlock(&vm->fs_lock);
    return status;
}

void mime_fs_close_all(MimeVm *vm)
{
    pthread_mutex_lock(&vm->fs_lock);
    for (uint32_t i = 0; i < MIME_VMM_MAX_FS_HANDLES; i++) {
        if (vm->fs_handles[i]) {
            vm->fs_handles[i]->delete_on_close = false;
            free_handle(vm, i + 1);
        }
    }
    while (vm->window_count)
        window_unmap(vm, &vm->windows[0]);
    pthread_mutex_unlock(&vm->fs_lock);
}
