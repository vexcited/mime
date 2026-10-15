/*
 * mime host side common declarations.
 */
#ifndef MIME_HOST_H
#define MIME_HOST_H

#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIME_VERSION_STRING "0.1.0"

#define MIME_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MIME_MIN(a, b) ((a) < (b) ? (a) : (b))
#define MIME_MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIME_ALIGN_UP(v, a) (((v) + ((a) - 1)) & ~((__typeof__(v))(a) - 1))
#define MIME_ALIGN_DOWN(v, a) ((v) & ~((__typeof__(v))(a) - 1))

#define MIME_PATH_MAX 1024

/* names the calling thread, as ps and debuggers show it. */
#if defined(__APPLE__)
#define mime_set_thread_name(name) pthread_setname_np(name)
#else
#define mime_set_thread_name(name) pthread_setname_np(pthread_self(), name)
#endif

/* log.c */
typedef enum MimeLogLevel {
    MIME_LOG_DEBUG,
    MIME_LOG_INFO,
    MIME_LOG_WARN,
    MIME_LOG_ERROR,
} MimeLogLevel;

void mime_log_set_level(MimeLogLevel level);
void mime_log_set_prefix(const char *prefix);
void mime_log(MimeLogLevel level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void mime_logv(MimeLogLevel level, const char *fmt, va_list ap);
_Noreturn void mime_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define mime_debug(...) mime_log(MIME_LOG_DEBUG, __VA_ARGS__)
#define mime_info(...) mime_log(MIME_LOG_INFO, __VA_ARGS__)
#define mime_warn(...) mime_log(MIME_LOG_WARN, __VA_ARGS__)
#define mime_error(...) mime_log(MIME_LOG_ERROR, __VA_ARGS__)

/* paths.c, xdg base directories. */
typedef struct MimePaths {
    char config[MIME_PATH_MAX];  /* $XDG_CONFIG_HOME/mime */
    char data[MIME_PATH_MAX];    /* $XDG_DATA_HOME/mime */
    char state[MIME_PATH_MAX];   /* $XDG_STATE_HOME/mime */
    char cache[MIME_PATH_MAX];   /* $XDG_CACHE_HOME/mime */
    char runtime[MIME_PATH_MAX]; /* $XDG_RUNTIME_DIR/mime */
    char drive_c[MIME_PATH_MAX]; /* data/drive_c */
    char socket[MIME_PATH_MAX];  /* runtime/mime.sock */
    char pidfile[MIME_PATH_MAX]; /* runtime/mime.pid */
    char lockfile[MIME_PATH_MAX]; /* per user, outside the xdg dirs */
    char logfile[MIME_PATH_MAX]; /* state/vmm.log */
    char setup_stamp[MIME_PATH_MAX];
    char exe_dir[MIME_PATH_MAX];  /* directory of the running mime binary */
} MimePaths;

bool mime_paths_init(MimePaths *paths);
bool mime_paths_ensure(const MimePaths *paths);

/* util.c */
bool mime_mkdir_p(const char *path, unsigned mode);
bool mime_file_exists(const char *path);
bool mime_dir_exists(const char *path);
void *mime_read_file(const char *path, size_t *size_out);
bool mime_write_file(const char *path, const void *data, size_t size);
bool mime_copy_file(const char *src, const char *dst);
int64_t mime_file_mtime(const char *path);
void mime_path_join(char *out, size_t out_size, const char *a, const char *b);
void *mime_xmalloc(size_t size);
void *mime_xcalloc(size_t count, size_t size);
void *mime_xrealloc(void *ptr, size_t size);
char *mime_xstrdup(const char *s);
size_t mime_strlcpy(char *dst, const char *src, size_t size);
size_t mime_utf16_to_utf8(char *dst, size_t dst_size, const uint16_t *src, size_t src_len);
size_t mime_utf8_to_utf16(uint16_t *dst, size_t dst_count, const char *src);
uint64_t mime_now_ns(void);

/* sha1.c */
typedef struct MimeSha1 {
    uint32_t state[5];
    uint64_t length;
    uint8_t buffer[64];
    uint32_t buffered;
} MimeSha1;

void mime_sha1_init(MimeSha1 *ctx);
void mime_sha1_update(MimeSha1 *ctx, const void *data, size_t size);
void mime_sha1_final(MimeSha1 *ctx, uint8_t digest[20]);

#endif
