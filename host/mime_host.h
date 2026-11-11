/* common host declarations. */
#ifndef MIME_HOST_H
#define MIME_HOST_H

#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define MIME_VERSION_STRING "0.1.0"

#define MIME_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MIME_MIN(a, b) ((a) < (b) ? (a) : (b))
#define MIME_MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIME_ALIGN_UP(v, a) (((v) + ((a) - 1)) & ~((typeof(v))(a) - 1))
#define MIME_ALIGN_DOWN(v, a) ((v) & ~((typeof(v))(a) - 1))

#define MIME_PATH_MAX 1024

/* names the calling thread, as ps and debuggers show it. */
#ifdef __APPLE__
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
[[gnu::format(printf, 2, 3)]] void mime_log(MimeLogLevel level, const char *fmt, ...);
void mime_logv(MimeLogLevel level, const char *fmt, va_list ap);
[[noreturn, gnu::format(printf, 1, 2)]] void mime_fatal(const char *fmt, ...);

#define mime_debug(...) mime_log(MIME_LOG_DEBUG, __VA_ARGS__)
#define mime_info(...) mime_log(MIME_LOG_INFO, __VA_ARGS__)
#define mime_warn(...) mime_log(MIME_LOG_WARN, __VA_ARGS__)
#define mime_error(...) mime_log(MIME_LOG_ERROR, __VA_ARGS__)

/* paths.c, XDG base directories. */
typedef struct MimePaths {
    char config[MIME_PATH_MAX];  /* $XDG_CONFIG_HOME/mime */
    char data[MIME_PATH_MAX];    /* $XDG_DATA_HOME/mime */
    char state[MIME_PATH_MAX];   /* $XDG_STATE_HOME/mime */
    char cache[MIME_PATH_MAX];   /* $XDG_CACHE_HOME/mime */
    char runtime[MIME_PATH_MAX]; /* $XDG_RUNTIME_DIR/mime */
    char drive_c[MIME_PATH_MAX]; /* data/drive_c */
    char socket[MIME_PATH_MAX];  /* runtime/mime.sock */
    char pidfile[MIME_PATH_MAX]; /* runtime/mime.pid */
    char lockfile[MIME_PATH_MAX]; /* per user, outside the XDG dirs. */
    char logfile[MIME_PATH_MAX]; /* state/vmm.log */
    char exe_dir[MIME_PATH_MAX];  /* directory of the running mime binary. */
} MimePaths;

bool mime_paths_init(MimePaths *paths);
bool mime_paths_ensure(const MimePaths *paths);
/* maps a host path to the guest: drive_c becomes C:, a host drive its letter, everything else Z:. */
void mime_paths_to_guest(const MimePaths *paths, const char *host, char *out, size_t size);
/* the reverse, for paths on C:, Z: or a host drive with either separator; the result never leaves the drive. */
bool mime_paths_to_host(const MimePaths *paths, const char *guest, char *out, size_t size);

/* drives.c: a drive the host mounted, as the guest sees it. */
typedef struct MimeHostDrive {
    char letter;
    uint32_t flags;  /* MIME_FS_DRIVE_*. */
    uint32_t serial;
    char label[32];
    char root[512];
} MimeHostDrive;

/* the host's drives that have a letter, ordered by mount point. assign gives letters to new ones and records them. */
size_t mime_drives_scan(const MimePaths *paths, bool assign, MimeHostDrive *out, size_t max);
/* the host directory of a guest drive: drive_c for C:, "" for Z: and the mount point of a host drive. */
bool mime_paths_drive_root(const MimePaths *paths, char letter, char *out, size_t size);
/* manifest execution level: 0 absent, 1 asInvoker, 2 highestAvailable, 3 requireAdministrator. */
uint32_t mime_pe_run_level(const char *path);
/* the paths of the running machine process. */
const MimePaths *mime_daemon_paths(void);

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

/* build_id.c */
#define MIME_BUILD_ID_MAX 32
size_t mime_build_id(uint8_t out[MIME_BUILD_ID_MAX]);
/* in hex, or "unknown" when length is 0. */
void mime_build_id_text(const uint8_t *id, size_t length, char *out, size_t size);

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

/* sha256.c */
typedef struct MimeSha256 {
    uint32_t state[8];
    uint64_t length;
    uint8_t buffer[64];
    uint32_t buffered;
} MimeSha256;

void mime_sha256_init(MimeSha256 *ctx);
void mime_sha256_update(MimeSha256 *ctx, const void *data, size_t size);
void mime_sha256_final(MimeSha256 *ctx, uint8_t digest[32]);

#endif
