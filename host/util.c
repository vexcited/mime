#include "mime_host.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

bool mime_mkdir_p(const char *path, unsigned mode)
{
    char buf[MIME_PATH_MAX];
    if (mime_strlcpy(buf, path, sizeof(buf)) >= sizeof(buf)) {
        errno = ENAMETOOLONG;
        return false;
    }
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = 0;
        if (mkdir(buf, mode) != 0 && errno != EEXIST)
            return false;
        *p = '/';
    }
    if (mkdir(buf, mode) != 0 && errno != EEXIST)
        return false;
    return true;
}

bool mime_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool mime_dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int64_t mime_file_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return (int64_t)st.st_mtimespec.tv_sec * 1000000000 + st.st_mtimespec.tv_nsec;
}

void *mime_read_file(const char *path, size_t *size_out)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return NULL;
    }
    size_t size = (size_t)st.st_size;
    uint8_t *data = malloc(size + 1);
    if (!data) {
        close(fd);
        return NULL;
    }
    size_t done = 0;
    while (done < size) {
        ssize_t n = read(fd, data + done, size - done);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            free(data);
            close(fd);
            return NULL;
        }
        done += (size_t)n;
    }
    close(fd);
    data[size] = 0;
    if (size_out)
        *size_out = size;
    return data;
}

bool mime_write_file(const char *path, const void *data, size_t size)
{
    char tmp[MIME_PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s.tmp%d", path, (int)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return false;
    const uint8_t *p = data;
    size_t done = 0;
    while (done < size) {
        ssize_t n = write(fd, p + done, size - done);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            unlink(tmp);
            return false;
        }
        done += (size_t)n;
    }
    if (close(fd) != 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return false;
    }
    return true;
}

bool mime_copy_file(const char *src, const char *dst)
{
    size_t size;
    void *data = mime_read_file(src, &size);
    if (!data)
        return false;
    bool ok = mime_write_file(dst, data, size);
    free(data);
    return ok;
}

void mime_path_join(char *out, size_t out_size, const char *a, const char *b)
{
    size_t len = strlen(a);
    if (len && a[len - 1] == '/')
        snprintf(out, out_size, "%s%s", a, b);
    else
        snprintf(out, out_size, "%s/%s", a, b);
}

void *mime_xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (!p)
        mime_fatal("out of memory allocating %zu bytes", size);
    return p;
}

void *mime_xcalloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p)
        mime_fatal("out of memory allocating %zu bytes", count * size);
    return p;
}

void *mime_xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size ? size : 1);
    if (!p)
        mime_fatal("out of memory allocating %zu bytes", size);
    return p;
}

char *mime_xstrdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = mime_xmalloc(len);
    memcpy(p, s, len);
    return p;
}

size_t mime_strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = 0;
    }
    return len;
}

size_t mime_utf16_to_utf8(char *dst, size_t dst_size, const uint16_t *src, size_t src_len)
{
    size_t o = 0;
    for (size_t i = 0; i < src_len; i++) {
        uint32_t c = src[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < src_len && src[i + 1] >= 0xDC00 && src[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (src[i + 1] - 0xDC00);
            i++;
        }
        uint8_t tmp[4];
        size_t n;
        if (c < 0x80) {
            tmp[0] = (uint8_t)c;
            n = 1;
        } else if (c < 0x800) {
            tmp[0] = (uint8_t)(0xC0 | (c >> 6));
            tmp[1] = (uint8_t)(0x80 | (c & 0x3F));
            n = 2;
        } else if (c < 0x10000) {
            tmp[0] = (uint8_t)(0xE0 | (c >> 12));
            tmp[1] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
            tmp[2] = (uint8_t)(0x80 | (c & 0x3F));
            n = 3;
        } else {
            tmp[0] = (uint8_t)(0xF0 | (c >> 18));
            tmp[1] = (uint8_t)(0x80 | ((c >> 12) & 0x3F));
            tmp[2] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
            tmp[3] = (uint8_t)(0x80 | (c & 0x3F));
            n = 4;
        }
        if (o + n >= dst_size)
            break;
        memcpy(dst + o, tmp, n);
        o += n;
    }
    if (dst_size)
        dst[o] = 0;
    return o;
}

size_t mime_utf8_to_utf16(uint16_t *dst, size_t dst_count, const char *src)
{
    const uint8_t *s = (const uint8_t *)src;
    size_t o = 0;
    while (*s) {
        uint32_t c = *s++;
        if (c >= 0xF0 && s[0] && s[1] && s[2]) {
            c = ((c & 0x07) << 18) | ((s[0] & 0x3F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            s += 3;
        } else if (c >= 0xE0 && s[0] && s[1]) {
            c = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
            s += 2;
        } else if (c >= 0xC0 && s[0]) {
            c = ((c & 0x1F) << 6) | (s[0] & 0x3F);
            s += 1;
        }
        if (c >= 0x10000) {
            if (o + 2 >= dst_count)
                break;
            c -= 0x10000;
            dst[o++] = (uint16_t)(0xD800 + (c >> 10));
            dst[o++] = (uint16_t)(0xDC00 + (c & 0x3FF));
        } else {
            if (o + 1 >= dst_count)
                break;
            dst[o++] = (uint16_t)c;
        }
    }
    if (dst_count)
        dst[o] = 0;
    return o;
}

uint64_t mime_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
