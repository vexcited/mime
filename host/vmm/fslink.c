/*
 * hard links of host files: making a link, listing the names of a file, and finding a file by its id.
 *
 * the host has no call that gives the names of an inode, so they are found by reading directories. a file with one
 * name needs no search. for the others the directory of the name the guest opened is read first, then the directories
 * around it, each level up reading only what the level below did not, until as many names are found as the file has
 * links. the search stays on the volume's file system and gives up after a number of entries, so a name that was
 * linked from outside the volume costs a bounded search, not a stall. the answers are kept for a short while and
 * checked against the directories before use. an id is found the same way, and kept for the directories, which are
 * what a list of names gives the guest to resolve next.
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

/* entries read by one search before it stops. */
#define SEARCH_BUDGET 4000000u
#define SEARCH_BUDGET_HOST 1500000u
#define SEARCH_DEPTH 200
#define ANSWERS 16
#define IDS 256

typedef struct Answer {
    bool used;
    dev_t dev;
    ino_t ino;
    uint32_t count;
    char **paths;
    uint64_t stamp;
} Answer;

typedef struct KnownId {
    uint32_t volume;
    dev_t dev;
    ino_t ino;
    char *path;
} KnownId;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Answer g_answers[ANSWERS];
static uint64_t g_clock;
static KnownId g_ids[IDS];
static uint32_t g_id_next;

static uint64_t status_of(int error)
{
    switch (error) {
    case ENOENT: return MIME_HC_STATUS_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS: return MIME_HC_STATUS_ACCESS_DENIED;
    case EEXIST: return MIME_HC_STATUS_EXISTS;
    case ENOTDIR: return MIME_HC_STATUS_NOT_DIRECTORY;
    case EISDIR: return MIME_HC_STATUS_IS_DIRECTORY;
    case ENOMEM: return MIME_HC_STATUS_NO_MEMORY;
    case ENOTEMPTY: return MIME_HC_STATUS_NOT_EMPTY;
    case EXDEV: return MIME_HC_STATUS_CROSS_DEVICE;
    case EMLINK: return MIME_HC_STATUS_TOO_MANY_LINKS;
    default: return MIME_HC_STATUS_IO_ERROR;
    }
}

static const char *volume_root(MimeVm *vm, uint32_t volume)
{
    if (volume == MIME_FS_VOLUME_SYSTEM)
        return vm->paths->drive_c;
    return volume == MIME_FS_VOLUME_HOST ? "" : nullptr;
}

/* the directory part of a path, the root being "/". */
static void parent_of(const char *path, char *out, size_t size)
{
    mime_strlcpy(out, path, size);
    char *slash = strrchr(out, '/');
    if (!slash)
        mime_strlcpy(out, ".", size);
    else if (slash == out)
        out[1] = 0;
    else
        *slash = 0;
}

/* ------------------------------------------------------------------ names of a file */

typedef struct Search {
    dev_t dev;
    ino_t ino;
    uint32_t want;  /* names to find, 0 for the first of an id. */
    uint32_t found;
    uint32_t capacity;
    char **paths;
    uint32_t budget;
    bool only_directories;
    const char *skip; /* a directory already read. */
} Search;

static bool is_dot(const char *name)
{
    return name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]));
}

static void add_path(Search *s, const char *path)
{
    if (s->found == s->capacity) {
        s->capacity = s->capacity ? s->capacity * 2 : 8;
        s->paths = mime_xrealloc(s->paths, s->capacity * sizeof(char *));
    }
    s->paths[s->found++] = mime_xstrdup(path);
}

static bool done(const Search *s)
{
    return s->budget == 0 || (s->want ? s->found >= s->want : s->found > 0);
}

/* reads a directory and the ones below it, until the search is done. */
static void walk(Search *s, const char *path, int depth)
{
#ifdef O_NOATIME
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_NOATIME);
    if (fd < 0 && errno == EPERM)
        fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
#else
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
#endif
    if (fd < 0)
        return;
    struct stat self;
    DIR *dir = nullptr;
    if (fstat(fd, &self) == 0 && self.st_dev == s->dev)
        dir = fdopendir(fd);
    if (!dir) {
        close(fd);
        return;
    }
    struct dirent *e;
    char full[MIME_PATH_MAX];
    /* directories to enter, kept apart so the names of this one are all seen before the walk goes down. */
    char **below = nullptr;
    uint32_t below_count = 0, below_capacity = 0;
    while (!done(s) && (e = readdir(dir))) {
        if (is_dot(e->d_name))
            continue;
        if (s->budget)
            s->budget--;
        bool directory = e->d_type == DT_DIR;
        if (e->d_ino == s->ino && (!s->only_directories || directory || e->d_type == DT_UNKNOWN)) {
            mime_path_join(full, sizeof(full), path, e->d_name);
            struct stat st;
            if (lstat(full, &st) == 0 && st.st_dev == s->dev && st.st_ino == s->ino &&
                (!s->only_directories || S_ISDIR(st.st_mode)))
                add_path(s, full);
        }
        if (directory || e->d_type == DT_UNKNOWN) {
            mime_path_join(full, sizeof(full), path, e->d_name);
            if (s->skip && !strcmp(full, s->skip))
                continue;
            if (e->d_type == DT_UNKNOWN) {
                struct stat st;
                if (lstat(full, &st) != 0 || !S_ISDIR(st.st_mode))
                    continue;
            }
            if (below_count == below_capacity) {
                below_capacity = below_capacity ? below_capacity * 2 : 16;
                below = mime_xrealloc(below, below_capacity * sizeof(char *));
            }
            below[below_count++] = mime_xstrdup(full);
        }
    }
    closedir(dir);
    for (uint32_t i = 0; i < below_count; i++) {
        if (!done(s) && depth < SEARCH_DEPTH)
            walk(s, below[i], depth + 1);
        free(below[i]);
    }
    free(below);
}

/* the names of the inode in the directories around start, which is the directory of a known name. */
static void search_around(Search *s, const char *root, const char *start)
{
    char level[MIME_PATH_MAX], last[MIME_PATH_MAX] = "";
    mime_strlcpy(level, start, sizeof(level));
    size_t root_length = strlen(root);
    for (;;) {
        s->skip = last[0] ? last : nullptr;
        walk(s, level, 0);
        if (done(s) || !strcmp(level, root) || !strcmp(level, "/") || strlen(level) <= root_length)
            break;
        mime_strlcpy(last, level, sizeof(last));
        char up[MIME_PATH_MAX];
        parent_of(level, up, sizeof(up));
        if (!strcmp(up, level))
            break;
        mime_strlcpy(level, up, sizeof(level));
    }
}

static void free_paths(char **paths, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        free(paths[i]);
    free(paths);
}

static void note_directory(uint32_t volume, const char *path, dev_t dev, ino_t ino)
{
    pthread_mutex_lock(&g_lock);
    for (uint32_t i = 0; i < IDS; i++) {
        if (g_ids[i].path && g_ids[i].volume == volume && g_ids[i].dev == dev && g_ids[i].ino == ino) {
            if (strcmp(g_ids[i].path, path)) {
                free(g_ids[i].path);
                g_ids[i].path = mime_xstrdup(path);
            }
            pthread_mutex_unlock(&g_lock);
            return;
        }
    }
    KnownId *k = &g_ids[g_id_next++ % IDS];
    free(k->path);
    k->volume = volume;
    k->dev = dev;
    k->ino = ino;
    k->path = mime_xstrdup(path);
    pthread_mutex_unlock(&g_lock);
}

static int by_path(const void *a, const void *b)
{
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

/*
 * the paths of every name of the file whose own name is known, the known one first and the others in path order.
 * returns the count, the caller frees. a name that was removed from the directory is not one.
 */
static uint32_t names_of(MimeVm *vm, uint32_t volume, const char *known, const struct stat *st, char ***out)
{
    const char *root = volume_root(vm, volume);
    uint32_t links = (uint32_t)st->st_nlink;
    *out = nullptr;

    /* the answer of a recent search, if the directories still say so. */
    pthread_mutex_lock(&g_lock);
    for (uint32_t i = 0; i < ANSWERS; i++) {
        Answer *a = &g_answers[i];
        if (!a->used || a->dev != st->st_dev || a->ino != st->st_ino || a->count != links)
            continue;
        bool valid = true;
        bool has_known = false;
        for (uint32_t n = 0; n < a->count && valid; n++) {
            struct stat check;
            valid = lstat(a->paths[n], &check) == 0 && check.st_dev == st->st_dev && check.st_ino == st->st_ino;
            has_known |= !strcmp(a->paths[n], known);
        }
        if (valid && has_known) {
            /* the name asked about first, the others as they were found. */
            char **copy = mime_xmalloc(a->count * sizeof(char *));
            uint32_t put = 0;
            copy[put++] = mime_xstrdup(known);
            for (uint32_t n = 0; n < a->count; n++) {
                if (strcmp(a->paths[n], known))
                    copy[put++] = mime_xstrdup(a->paths[n]);
            }
            a->stamp = ++g_clock;
            pthread_mutex_unlock(&g_lock);
            *out = copy;
            return links;
        }
        free_paths(a->paths, a->count);
        a->used = false;
    }
    pthread_mutex_unlock(&g_lock);

    Search around = {.dev = st->st_dev, .ino = st->st_ino, .want = links,
                     .budget = volume == MIME_FS_VOLUME_HOST ? SEARCH_BUDGET_HOST : SEARCH_BUDGET};
    char start[MIME_PATH_MAX];
    parent_of(known, start, sizeof(start));
    search_around(&around, root, start);
    Search s = {};
    add_path(&s, known);
    for (uint32_t i = 0; i < around.found; i++) {
        if (strcmp(around.paths[i], known))
            add_path(&s, around.paths[i]);
    }
    free_paths(around.paths, around.found);
    if (s.found > 2)
        qsort(s.paths + 1, s.found - 1, sizeof(char *), by_path);

    if (s.found == links && links > 1) {
        pthread_mutex_lock(&g_lock);
        Answer *oldest = &g_answers[0];
        for (uint32_t i = 0; i < ANSWERS; i++) {
            if (!g_answers[i].used) {
                oldest = &g_answers[i];
                break;
            }
            if (g_answers[i].stamp < oldest->stamp)
                oldest = &g_answers[i];
        }
        if (oldest->used)
            free_paths(oldest->paths, oldest->count);
        oldest->used = true;
        oldest->dev = st->st_dev;
        oldest->ino = st->st_ino;
        oldest->count = s.found;
        oldest->stamp = ++g_clock;
        oldest->paths = mime_xmalloc(s.found * sizeof(char *));
        for (uint32_t i = 0; i < s.found; i++)
            oldest->paths[i] = mime_xstrdup(s.paths[i]);
        pthread_mutex_unlock(&g_lock);
    }
    *out = s.paths;
    return s.found;
}

/* writes the record of a name, returns the bytes it takes. */
static size_t put_name(MimeFsLinkName *record, size_t room, uint64_t parent, const char *name)
{
    uint16_t wide[512];
    size_t chars = mime_utf8_to_utf16(wide, MIME_ARRAY_COUNT(wide), name);
    size_t size = (sizeof(*record) + chars * 2 + 7) & ~(size_t)7;
    if (record && size <= room) {
        record->size = (uint32_t)size;
        record->name_chars = (uint32_t)chars;
        record->parent_id = parent;
        memcpy(record->name, wide, chars * 2);
    }
    return size;
}

uint64_t mime_fs_links(MimeVm *vm, MimeFsHandle *file, uint64_t pa, uint64_t capacity, uint64_t *needed)
{
    void *buffer = capacity ? mime_vm_phys(vm, pa, capacity) : nullptr;
    if (capacity && !buffer)
        return MIME_HC_STATUS_INVALID;
    struct stat st;
    if (!file->host_path || file->fd < 0 || fstat(file->fd, &st) != 0)
        return MIME_HC_STATUS_INVALID;

    const char *root = volume_root(vm, file->volume);
    if (!root)
        return MIME_HC_STATUS_INVALID;
    char **paths = nullptr;
    uint32_t count = 0;
    struct stat own;
    bool named = lstat(file->host_path, &own) == 0 && own.st_dev == st.st_dev && own.st_ino == st.st_ino;
    if (S_ISDIR(st.st_mode) || st.st_nlink <= 1) {
        if (named) {
            paths = mime_xmalloc(sizeof(char *));
            paths[0] = mime_xstrdup(file->host_path);
            count = 1;
        }
    } else if (named) {
        count = names_of(vm, file->volume, file->host_path, &st, &paths);
    }

    size_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        char parent[MIME_PATH_MAX];
        parent_of(paths[i], parent, sizeof(parent));
        /* the root has no name in a directory. */
        if (!strcmp(paths[i], root) || (!root[0] && !strcmp(paths[i], "/")))
            continue;
        struct stat dir;
        uint64_t parent_id = stat(parent[0] ? parent : "/", &dir) == 0 ? (uint64_t)dir.st_ino : 0;
        if (parent_id)
            note_directory(file->volume, parent, dir.st_dev, dir.st_ino);
        const char *leaf = strrchr(paths[i], '/');
        leaf = leaf ? leaf + 1 : paths[i];
        MimeFsLinkName *record = buffer ? (MimeFsLinkName *)((uint8_t *)buffer + total) : nullptr;
        size_t room = buffer && total <= capacity ? (size_t)capacity - total : 0;
        total += put_name(record, room, parent_id, leaf);
    }
    free_paths(paths, count);
    *needed = total;
    return MIME_HC_STATUS_OK;
}

/* ------------------------------------------------------------------ files by id */

/* the host path of the file or directory with the id, for the open of an id. */
bool mime_fs_path_of_id(MimeVm *vm, uint32_t volume, const uint16_t *id, uint32_t chars, char *out, size_t size)
{
    const char *root = volume_root(vm, volume);
    if (!root || (chars != 4 && chars != 8))
        return false;
    uint64_t ino = (uint64_t)id[0] | (uint64_t)id[1] << 16 | (uint64_t)id[2] << 32 | (uint64_t)id[3] << 48;
    for (uint32_t i = 4; i < chars; i++) {
        if (id[i])
            return false;
    }
    struct stat st;
    const char *top = root[0] ? root : "/";
    if (stat(top, &st) != 0)
        return false;
    if (ino == (uint64_t)st.st_ino) {
        mime_strlcpy(out, top, size);
        return true;
    }
    dev_t dev = st.st_dev;

    pthread_mutex_lock(&g_lock);
    for (uint32_t i = 0; i < IDS; i++) {
        KnownId *k = &g_ids[i];
        struct stat check;
        if (k->path && k->volume == volume && k->ino == ino) {
            if (lstat(k->path, &check) == 0 && check.st_dev == k->dev && check.st_ino == ino) {
                mime_strlcpy(out, k->path, size);
                pthread_mutex_unlock(&g_lock);
                return true;
            }
            free(k->path);
            k->path = nullptr;
        }
    }
    pthread_mutex_unlock(&g_lock);

    Search s = {.dev = dev, .ino = (ino_t)ino, .want = 0, .budget = volume == MIME_FS_VOLUME_HOST ? SEARCH_BUDGET_HOST
                                                                                                   : SEARCH_BUDGET};
    walk(&s, top, 0);
    bool found = s.found > 0;
    if (found) {
        mime_strlcpy(out, s.paths[0], size);
        struct stat hit;
        if (lstat(out, &hit) == 0 && S_ISDIR(hit.st_mode))
            note_directory(volume, out, hit.st_dev, (ino_t)ino);
    }
    free_paths(s.paths, s.found);
    return found;
}

/* the volume relative name of a host path, with backslashes, as the guest keeps names. */
uint32_t mime_fs_guest_name(MimeVm *vm, uint32_t volume, const char *host, uint16_t *out, size_t capacity)
{
    const char *root = volume_root(vm, volume);
    size_t root_length = root ? strlen(root) : 0;
    const char *rest = root && !strncmp(host, root, root_length) ? host + root_length : host;
    char text[MIME_PATH_MAX];
    mime_strlcpy(text, *rest ? rest : "/", sizeof(text));
    for (char *p = text; *p; p++) {
        if (*p == '/')
            *p = '\\';
    }
    return (uint32_t)mime_utf8_to_utf16(out, capacity, text);
}

/* ------------------------------------------------------------------ making a link */

static bool dir_is_empty(const char *path)
{
    DIR *d = opendir(path);
    if (!d)
        return true;
    struct dirent *e;
    bool empty = true;
    while (empty && (e = readdir(d)))
        empty = is_dot(e->d_name);
    closedir(d);
    return empty;
}

/*
 * links the file of src into a directory under a name. an existing name of another file is replaced when asked, by
 * linking to a temporary name and moving that over it, so the name is never missing. the name of the file itself,
 * spelled the same, is left alone, and spelled with other letters it takes the new spelling.
 */
uint64_t mime_fs_link(MimeVm *vm, MimeFsHandle *src, MimeFsHandle *dir, const char *leaf, uint32_t flags,
                      uint64_t *links)
{
    if (!src->host_path || !leaf[0] || strchr(leaf, '/') || is_dot(leaf))
        return MIME_HC_STATUS_INVALID;
    char dir_path[MIME_PATH_MAX];
    if (dir) {
        if (!dir->is_dir || !dir->host_path || dir->volume != src->volume)
            return MIME_HC_STATUS_INVALID;
        mime_strlcpy(dir_path, dir->host_path, sizeof(dir_path));
    } else {
        parent_of(src->host_path, dir_path, sizeof(dir_path));
    }

    struct stat from, into;
    if (lstat(src->host_path, &from) != 0)
        return status_of(errno);
    if (S_ISDIR(from.st_mode))
        return MIME_HC_STATUS_IS_DIRECTORY;
    if (stat(dir_path, &into) != 0)
        return status_of(errno);
    if (!S_ISDIR(into.st_mode))
        return MIME_HC_STATUS_NOT_DIRECTORY;
    if (into.st_dev != from.st_dev)
        return MIME_HC_STATUS_CROSS_DEVICE;
    if (from.st_nlink >= MIME_FS_MAX_LINKS)
        return MIME_HC_STATUS_TOO_MANY_LINKS;

    bool replace = (flags & MIME_FS_LINK_REPLACE) != 0;
    char target[MIME_PATH_MAX], twin[MIME_PATH_MAX], found[1024] = "";
    mime_path_join(target, sizeof(target), dir_path, leaf);
    struct stat there;
    bool exists = lstat(target, &there) == 0;
    mime_strlcpy(twin, target, sizeof(twin));
    /* a case-insensitive host finds the entry under the new spelling too, the twin is the spelling it keeps. */
    bool alias = false;
    if (mime_fs_find_entry(dir_path, leaf, found, sizeof(found))) {
        char spelled[MIME_PATH_MAX];
        struct stat other;
        mime_path_join(spelled, sizeof(spelled), dir_path, found);
        if (lstat(spelled, &other) == 0 && (!exists || (other.st_dev == there.st_dev && other.st_ino == there.st_ino))) {
            alias = exists && strcmp(spelled, target);
            mime_strlcpy(twin, spelled, sizeof(twin));
            exists = true;
            there = other;
        }
    }

    uint64_t status = MIME_HC_STATUS_OK;
    if (exists && there.st_dev == from.st_dev && there.st_ino == from.st_ino) {
        /* the name of the file itself. */
        if (!replace)
            return MIME_HC_STATUS_EXISTS;
        if (!strcmp(twin, target))
            goto linked;
        /* the same name in other letters: the new spelling goes in, then the old one goes. */
        bool respelled;
        if (alias) {
            /* one entry under both spellings, a rename changes its letters. */
            mime_fswatch_self(twin, MIME_FSWATCH_MOVE_FROM);
            mime_fswatch_self(target, MIME_FSWATCH_MOVE_TO);
            if (rename(twin, target) != 0) {
                int error = errno;
                mime_fswatch_self(twin, MIME_FSWATCH_UNDO_MOVE);
                mime_fswatch_self(target, MIME_FSWATCH_UNDO_MOVE);
                return status_of(error);
            }
            respelled = true;
        } else {
            mime_fswatch_self(target, MIME_FSWATCH_CREATE);
            if (link(src->host_path, target) != 0) {
                int error = errno;
                mime_fswatch_self(target, MIME_FSWATCH_UNDO_CREATE);
                return status_of(error);
            }
            mime_fswatch_self(twin, MIME_FSWATCH_REMOVE);
            respelled = unlink(twin) == 0;
            if (!respelled)
                mime_fswatch_self(twin, MIME_FSWATCH_UNDO_REMOVE);
        }
        if (respelled) {
            /* the handles that were open by the old spelling follow the name. */
            pthread_mutex_lock(&vm->fs_lock);
            for (uint32_t i = 0; i < vm->fs_handle_slots; i++) {
                MimeFsHandle *fh = *mime_fs_handle_slot(vm, i);
                if (fh && fh->host_path && !strcmp(fh->host_path, twin)) {
                    free(fh->host_path);
                    fh->host_path = mime_xstrdup(target);
                }
            }
            pthread_mutex_unlock(&vm->fs_lock);
        }
        goto linked;
    }
    if (exists) {
        if (!replace)
            return MIME_HC_STATUS_EXISTS;
        if (S_ISDIR(there.st_mode))
            return dir_is_empty(twin) ? MIME_HC_STATUS_ACCESS_DENIED : MIME_HC_STATUS_NOT_EMPTY;
        if (!(there.st_mode & S_IWUSR) && !(flags & MIME_FS_LINK_IGNORE_READONLY))
            return MIME_HC_STATUS_ACCESS_DENIED;
        static uint32_t counter;
        char temporary[MIME_PATH_MAX];
        snprintf(temporary, sizeof(temporary), "%s/.mimelink-%d-%u", dir_path, (int)getpid(),
                 __atomic_fetch_add(&counter, 1, __ATOMIC_RELAXED));
        mime_fswatch_self(temporary, MIME_FSWATCH_CREATE);
        if (link(src->host_path, temporary) != 0) {
            int error = errno;
            mime_fswatch_self(temporary, MIME_FSWATCH_UNDO_CREATE);
            return status_of(error);
        }
        /* a case-insensitive host keeps the spelling of the entry it replaces, the new one is given after. */
        const char *into = alias ? twin : target;
        mime_fswatch_self(temporary, MIME_FSWATCH_MOVE_FROM);
        mime_fswatch_self(into, MIME_FSWATCH_MOVE_TO);
        if (rename(temporary, into) != 0) {
            int error = errno;
            mime_fswatch_self(temporary, MIME_FSWATCH_UNDO_MOVE);
            mime_fswatch_self(into, MIME_FSWATCH_UNDO_MOVE);
            unlink(temporary);
            return status_of(error);
        }
        if (alias) {
            mime_fswatch_self(twin, MIME_FSWATCH_MOVE_FROM);
            mime_fswatch_self(target, MIME_FSWATCH_MOVE_TO);
            if (rename(twin, target) != 0) {
                mime_fswatch_self(twin, MIME_FSWATCH_UNDO_MOVE);
                mime_fswatch_self(target, MIME_FSWATCH_UNDO_MOVE);
            }
        } else if (strcmp(twin, target)) {
            mime_fswatch_self(twin, MIME_FSWATCH_REMOVE);
            if (unlink(twin) != 0)
                mime_fswatch_self(twin, MIME_FSWATCH_UNDO_REMOVE);
        }
        /* a handle open on the replaced file must not take the new name with it when it closes. */
        pthread_mutex_lock(&vm->fs_lock);
        for (uint32_t i = 0; i < vm->fs_handle_slots; i++) {
            MimeFsHandle *fh = *mime_fs_handle_slot(vm, i);
            if (fh && fh->host_path && (!strcmp(fh->host_path, target) || !strcmp(fh->host_path, twin)))
                fh->delete_on_close = false;
        }
        pthread_mutex_unlock(&vm->fs_lock);
        goto linked;
    }
    mime_fswatch_self(target, MIME_FSWATCH_CREATE);
    if (link(src->host_path, target) != 0) {
        int error = errno;
        mime_fswatch_self(target, MIME_FSWATCH_UNDO_CREATE);
        return status_of(error);
    }
linked:
    if (src->fd >= 0 && fstat(src->fd, &from) == 0)
        *links = (uint64_t)from.st_nlink;
    return status;
}
