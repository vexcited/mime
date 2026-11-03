/*
 * symbolic links and junctions of the drives, kept as symbolic links of the host.
 *
 * a link of the guest is a host symbolic link whose text is the target, so a program of the host that looks at the drive
 * sees links it can follow. the kind of the link goes in the end of the text, which the host resolves the same way:
 *
 *   target        a symbolic link to a file
 *   target/       a symbolic link to a directory
 *   target/.      a junction
 *   target/./.     a symbolic link made as a file link, to a directory
 *
 * a link with none of these is a file link, unless it points to a directory, which makes it a directory link: that is what
 * a link made by a program of the host is.
 *
 * an absolute target of the drives is a host path: C: is the directory of the drive and Z: the host's root. a relative
 * target stays relative, to the directory of the link. a target the host cannot reach, another drive letter, a network
 * path or a volume, is kept as the guest wrote it, beginning with a backslash, which no host path does.
 *
 * the print name is made from the substitute name: the target without its \??\ prefix. what a program gave as its print
 * name is not kept, as a host link has one text only.
 */
#include "vmm.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SYMLINK_FLAG_RELATIVE 1u

static const char *root_of(MimeVm *vm, uint32_t volume)
{
    return volume == MIME_FS_VOLUME_SYSTEM ? vm->paths->drive_c : "";
}

/* ------------------------------------------------------------------ reading a link */

/* the text of a link without its kind suffix. */
static bool split_text(const char *link, char *text, size_t size, bool *junction, bool *directory)
{
    bool forced_file = false;
    ssize_t n = readlink(link, text, size - 1);
    if (n <= 0)
        return false;
    size_t length = (size_t)n;
    text[length] = 0;
    *junction = *directory = false;
    if (length >= 6 && !strcmp(text + length - 4, "/./.")) {
        forced_file = true;
        text[length -= 4] = 0;
    } else if (length >= 3 && !strcmp(text + length - 2, "/.")) {
        *junction = *directory = true;
        text[length -= 2] = 0;
    } else if (length > 1 && text[length - 1] == '/') {
        *directory = true;
        text[--length] = 0;
    } else {
        struct stat st;
        *directory = stat(link, &st) == 0 && S_ISDIR(st.st_mode);
    }
    if (forced_file)
        *directory = false;
    return true;
}

bool mime_fs_link_kind(const char *link, uint32_t *tag, bool *directory)
{
    char text[MIME_PATH_MAX];
    bool junction;
    if (!split_text(link, text, sizeof(text), &junction, directory))
        return false;
    *tag = junction ? MIME_REPARSE_TAG_MOUNT_POINT : MIME_REPARSE_TAG_SYMLINK;
    return true;
}

static void put_names(MimeFsReparse *r, const char *substitute, const char *print)
{
    r->substitute_chars = (uint32_t)mime_utf8_to_utf16(r->names, MIME_REPARSE_MAX_NAME / 2, substitute);
    r->print_chars = (uint32_t)mime_utf8_to_utf16(r->names + r->substitute_chars, MIME_REPARSE_MAX_NAME / 2, print);
}

/* a path below a host directory as a guest path: backslashes and no end one. */
static void backslashes(char *out, const char *tail)
{
    size_t n = strlen(tail);
    memcpy(out, tail, n + 1);
    for (size_t i = 0; i < n; i++)
        if (out[i] == '/')
            out[i] = '\\';
    while (n > 1 && out[n - 1] == '\\')
        out[--n] = 0;
}

bool mime_fs_link_read(MimeVm *vm, const char *link, MimeFsReparse *r)
{
    char text[MIME_PATH_MAX];
    bool junction, directory;
    if (!split_text(link, text, sizeof(text), &junction, &directory))
        return false;
    memset(r, 0, sizeof(*r));
    r->directory = directory;
    r->tag = junction ? MIME_REPARSE_TAG_MOUNT_POINT : MIME_REPARSE_TAG_SYMLINK;
    char substitute[MIME_PATH_MAX + 16], tail[MIME_PATH_MAX];
    const char *print;
    if (text[0] == '\\') {
        mime_strlcpy(substitute, text, sizeof(substitute));
        print = !strncmp(text, "\\??\\", 4) ? substitute + 4 : substitute;
    } else if (text[0] == '/') {
        const char *root = vm->paths->drive_c;
        size_t root_length = strlen(root);
        if (!strncmp(text, root, root_length) && (text[root_length] == '/' || !text[root_length])) {
            backslashes(tail, text + root_length);
            snprintf(substitute, sizeof(substitute), "\\??\\C:%s", tail[0] ? tail : "\\");
        } else {
            backslashes(tail, text);
            snprintf(substitute, sizeof(substitute), "\\??\\Z:%s", tail);
        }
        print = substitute + 4;
    } else {
        backslashes(substitute, text);
        r->flags = SYMLINK_FLAG_RELATIVE;
        print = substitute;
    }
    if (junction)
        r->flags = 0;
    put_names(r, substitute, print);
    return true;
}

/* ------------------------------------------------------------------ finding a link in a path */

/*
 * the first component below the root of the volume that is a link, not counting the last one when the caller opens a link
 * as itself. returns the number of components through it, 1 for the first, or 0, and the length of the path through it.
 */
int mime_fs_find_link(const char *path, size_t root_length, bool last_is_the_file, size_t *through)
{
    size_t total = strlen(path);
    char prefix[MIME_PATH_MAX];
    if (total >= sizeof(prefix))
        return 0;
    memcpy(prefix, path, total + 1);
    size_t at = root_length;
    int index = 0;
    while (at < total) {
        while (at < total && path[at] == '/')
            at++;
        if (at >= total)
            break;
        size_t end = at;
        while (end < total && path[end] != '/')
            end++;
        index++;
        prefix[end] = 0;
        struct stat st;
        if (lstat(prefix, &st) != 0)
            return 0;
        prefix[end] = end < total ? '/' : 0;
        bool last = end >= total;
        if (S_ISLNK(st.st_mode) && !(last && last_is_the_file)) {
            *through = end;
            return index;
        }
        if (!S_ISDIR(st.st_mode) && !last)
            return 0;
        at = end;
    }
    return 0;
}

/*
 * the offset in guest characters where the component with the number ends, counting the components the host counted: the
 * empty ones and . are not components and .. takes the one before it away.
 */
uint32_t mime_fs_guest_prefix(const uint16_t *path, uint32_t chars, int component)
{
    uint32_t ends[256];
    int depth = 0;
    uint32_t at = 0;
    while (at < chars) {
        while (at < chars && (path[at] == '\\' || path[at] == '/'))
            at++;
        uint32_t start = at;
        while (at < chars && path[at] != '\\' && path[at] != '/')
            at++;
        uint32_t length = at - start;
        if (!length || (length == 1 && path[start] == '.'))
            continue;
        if (length == 2 && path[start] == '.' && path[start + 1] == '.') {
            if (depth > 0)
                depth--;
            continue;
        }
        if (depth == 256)
            return 0;
        ends[depth++] = at;
    }
    return component >= 1 && component <= depth ? ends[component - 1] : 0;
}

/* ------------------------------------------------------------------ writing a link */

/* the text of a link to the target the guest named, or false when the guest named nothing sensible. */
static bool link_text(MimeVm *vm, uint32_t volume, const char *link, const MimeFsReparse *r, bool directory, char *text,
                      size_t size)
{
    if (!r->substitute_chars || r->substitute_chars + r->print_chars > MIME_REPARSE_MAX_NAME)
        return false;
    char substitute[MIME_PATH_MAX];
    mime_utf16_to_utf8(substitute, sizeof(substitute), r->names, r->substitute_chars);
    bool junction = r->tag == MIME_REPARSE_TAG_MOUNT_POINT;
    char path[MIME_PATH_MAX];
    if (junction || !(r->flags & SYMLINK_FLAG_RELATIVE)) {
        if (!strncmp(substitute, "\\??\\", 4) && ((substitute[4] | 32) == 'c' || (substitute[4] | 32) == 'z') &&
            substitute[5] == ':' && (substitute[6] == '\\' || !substitute[6])) {
            bool system = (substitute[4] | 32) == 'c';
            const char *root = system ? vm->paths->drive_c : "";
            snprintf(path, sizeof(path), "%s/%s", root, substitute + (substitute[6] ? 7 : 6));
            for (char *p = path; *p; p++)
                if (*p == '\\')
                    *p = '/';
            size_t n = strlen(path);
            while (n > 1 && path[n - 1] == '/')
                path[--n] = 0;
            mime_fs_match_case(path, strlen(root));
        } else {
            mime_strlcpy(path, substitute, sizeof(path));
        }
    } else if (substitute[0] == '\\') {
        /* a path from the root of the volume the link is on. */
        const char *root = root_of(vm, volume);
        snprintf(path, sizeof(path), "%s%s", root, substitute);
        for (char *p = path; *p; p++)
            if (*p == '\\')
                *p = '/';
        mime_fs_match_case(path, strlen(root));
    } else {
        mime_strlcpy(path, substitute, sizeof(path));
        for (char *p = path; *p; p++)
            if (*p == '\\')
                *p = '/';
    }
    size_t n = strlen(path);
    if (!n)
        return false;
    const char *suffix = junction ? "/." : directory ? "/" : "";
    /* a file link to a directory has to say so, or the host's way of telling would take it for a directory link. */
    if (!junction && !directory) {
        char where[MIME_PATH_MAX];
        if (path[0] == '/') {
            mime_strlcpy(where, path, sizeof(where));
        } else {
            mime_strlcpy(where, link, sizeof(where));
            char *slash = strrchr(where, '/');
            snprintf(slash ? slash + 1 : where, sizeof(where) - (slash ? (size_t)(slash + 1 - where) : 0), "%s", path);
        }
        struct stat st;
        if (stat(where, &st) == 0 && S_ISDIR(st.st_mode))
            suffix = "/./.";
    }
    if (n + strlen(suffix) + 2 >= size)
        return false;
    /* the root has the slash it is made of, so a link to it needs another. */
    if (!strcmp(path, "/") && suffix[0])
        snprintf(text, size, "/%s", suffix);
    else
        snprintf(text, size, "%s%s", path, suffix);
    return true;
}

static int open_link(const char *path)
{
#ifdef __APPLE__
    return open(path, O_RDONLY | O_SYMLINK | O_CLOEXEC);
#else
    return open(path, O_PATH | O_NOFOLLOW | O_CLOEXEC);
#endif
}

int mime_fs_open_link(const char *path)
{
    return open_link(path);
}

static bool directory_is_empty(const char *path)
{
    DIR *d = opendir(path);
    if (!d)
        return false;
    struct dirent *e;
    bool empty = true;
    while (empty && (e = readdir(d)))
        empty = !strcmp(e->d_name, ".") || !strcmp(e->d_name, "..");
    closedir(d);
    return empty;
}

/* makes the file or directory of the handle a link, or changes where the link of the handle points. */
uint64_t mime_fs_reparse_set(MimeVm *vm, MimeFsHandle *fh, uint64_t pa)
{
    const MimeFsReparse *given = mime_vm_phys(vm, pa, sizeof(*given));
    if (!given || !fh->host_path || fh->fd < 0)
        return MIME_HC_STATUS_INVALID;
    MimeFsReparse r = *given;
    if (r.tag != MIME_REPARSE_TAG_SYMLINK && r.tag != MIME_REPARSE_TAG_MOUNT_POINT)
        return MIME_HC_STATUS_UNSUPPORTED;
    struct stat st;
    if (lstat(fh->host_path, &st) != 0)
        return mime_fs_status_of(errno);
    bool was_link = S_ISLNK(st.st_mode), directory;
    if (was_link) {
        uint32_t tag;
        if (!mime_fs_link_kind(fh->host_path, &tag, &directory))
            return MIME_HC_STATUS_IO_ERROR;
        if (tag != r.tag)
            return MIME_HC_STATUS_TAG_MISMATCH;
    } else if (S_ISDIR(st.st_mode)) {
        directory = true;
        if (!directory_is_empty(fh->host_path))
            return MIME_HC_STATUS_NOT_EMPTY;
    } else if (S_ISREG(st.st_mode)) {
        directory = false;
        if (r.tag == MIME_REPARSE_TAG_MOUNT_POINT)
            return MIME_HC_STATUS_NOT_DIRECTORY;
        if (st.st_size != 0)
            return MIME_HC_STATUS_INVALID;
    } else {
        return MIME_HC_STATUS_INVALID;
    }
    char text[MIME_PATH_MAX];
    if (!link_text(vm, fh->volume, fh->host_path, &r, directory, text, sizeof(text)))
        return MIME_HC_STATUS_INVALID;

    static uint32_t counter;
    char temporary[MIME_PATH_MAX];
    mime_strlcpy(temporary, fh->host_path, sizeof(temporary));
    char *slash = strrchr(temporary, '/');
    if (!slash)
        return MIME_HC_STATUS_INVALID;
    snprintf(slash + 1, sizeof(temporary) - (size_t)(slash + 1 - temporary), ".mimelink-%d-%u", (int)getpid(),
             __atomic_fetch_add(&counter, 1, __ATOMIC_RELAXED));
    mime_fswatch_self(temporary, MIME_FSWATCH_CREATE);
    if (symlink(text, temporary) != 0) {
        int error = errno;
        mime_fswatch_self(temporary, MIME_FSWATCH_UNDO_CREATE);
        return mime_fs_status_of(error);
    }
    bool emptied = false;
    if (directory && !was_link) {
        mime_fswatch_self(fh->host_path, MIME_FSWATCH_REMOVE);
        if (rmdir(fh->host_path) != 0) {
            int error = errno;
            mime_fswatch_self(fh->host_path, MIME_FSWATCH_UNDO_REMOVE);
            mime_fswatch_self(temporary, MIME_FSWATCH_UNDO_CREATE);
            unlink(temporary);
            return mime_fs_status_of(error);
        }
        emptied = true;
    }
    mime_fswatch_self(temporary, MIME_FSWATCH_MOVE_FROM);
    mime_fswatch_self(fh->host_path, MIME_FSWATCH_MOVE_TO);
    if (rename(temporary, fh->host_path) != 0) {
        int error = errno;
        mime_fswatch_self(temporary, MIME_FSWATCH_UNDO_MOVE);
        mime_fswatch_self(fh->host_path, MIME_FSWATCH_UNDO_MOVE);
        unlink(temporary);
        if (emptied)
            mkdir(fh->host_path, 0755);
        return mime_fs_status_of(error);
    }
    int fd = open_link(fh->host_path);
    if (fd < 0)
        return mime_fs_status_of(errno);
    pthread_mutex_lock(&vm->fs_lock);
    close(fh->fd);
    fh->fd = fd;
    fh->is_dir = false;
    fh->is_link = true;
    pthread_mutex_unlock(&vm->fs_lock);
    return MIME_HC_STATUS_OK;
}

/* what the link of the handle says. */
uint64_t mime_fs_reparse_get(MimeVm *vm, MimeFsHandle *fh, uint64_t pa)
{
    MimeFsReparse *out = mime_vm_phys(vm, pa, sizeof(*out));
    if (!out || !fh->host_path)
        return MIME_HC_STATUS_INVALID;
    if (!fh->is_link)
        return MIME_HC_STATUS_NOT_REPARSE;
    return mime_fs_link_read(vm, fh->host_path, out) ? MIME_HC_STATUS_OK : MIME_HC_STATUS_NOT_REPARSE;
}

/* the link goes and an empty file or directory takes its name. a tag of 0 is not checked. */
uint64_t mime_fs_reparse_delete(MimeVm *vm, MimeFsHandle *fh, uint32_t expected)
{
    if (!fh->host_path)
        return MIME_HC_STATUS_INVALID;
    if (!fh->is_link)
        return MIME_HC_STATUS_NOT_REPARSE;
    uint32_t tag;
    bool directory;
    if (!mime_fs_link_kind(fh->host_path, &tag, &directory))
        return MIME_HC_STATUS_NOT_REPARSE;
    if (expected && expected != tag)
        return MIME_HC_STATUS_TAG_MISMATCH;
    mime_fswatch_self(fh->host_path, MIME_FSWATCH_REMOVE);
    if (unlink(fh->host_path) != 0) {
        int error = errno;
        mime_fswatch_self(fh->host_path, MIME_FSWATCH_UNDO_REMOVE);
        return mime_fs_status_of(error);
    }
    mime_fswatch_self(fh->host_path, MIME_FSWATCH_CREATE);
    int fd;
    if (directory) {
        if (mkdir(fh->host_path, 0755) != 0) {
            int error = errno;
            mime_fswatch_self(fh->host_path, MIME_FSWATCH_UNDO_CREATE);
            return mime_fs_status_of(error);
        }
        fd = open(fh->host_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } else {
        fd = open(fh->host_path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    }
    if (fd < 0) {
        int error = errno;
        mime_fswatch_self(fh->host_path, MIME_FSWATCH_UNDO_CREATE);
        return mime_fs_status_of(error);
    }
    pthread_mutex_lock(&vm->fs_lock);
    close(fh->fd);
    fh->fd = fd;
    fh->is_dir = directory;
    fh->is_link = false;
    pthread_mutex_unlock(&vm->fs_lock);
    return MIME_HC_STATUS_OK;
}
