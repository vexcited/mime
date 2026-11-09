/*
 * microsoft's own copies of a few system files, kept in a host cache outside the guest drive so an identity check sees
 * the bytes microsoft ships while mime's code still runs. data/genuine.manifest lists each file by its windows path,
 * sha-256, symbol server key and size; the host fetches them from microsoft's symbol server, checks the bytes against
 * the manifest and installs them atomically. nothing is written to the guest drive and nothing of microsoft's is in
 * the build: a machine with no cache keeps mime's own files, see guest/ntos/io/mimefs.c and MimeRomEntry.genuine.
 */
#include "mime_host.h"
#include "vmm/vmm.h"

#include "genuine_manifest.h" /* mime_genuine_manifest[], written by cmake/genuine.cmake. */

#include <curl/curl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SYMBOL_SERVER "https://msdl.microsoft.com/download/symbols"
#define GENUINE_MAX_FILE (64u << 20) /* no system file the manifest names is near this big. */

typedef struct {
    char path[128]; /* below \Windows, forward slashes, e.g. System32/wow64.dll. */
    char name[64];  /* the leaf, for the symbol server. */
    char sha256[65];
    char key[40]; /* time-date stamp and size-of-image, the symbol server key. */
    uint64_t size;
} GenuineEntry;

static GenuineEntry g_entries[MIME_GENUINE_MAX];
static uint32_t g_count;
static char g_dir[MIME_PATH_MAX]; /* <cache>/genuine/<manifest digest>. */
static bool g_parsed;

/* ------------------------------------------------------------------ the manifest */

static void hex_of(const uint8_t *bytes, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 15];
    }
    out[n * 2] = 0;
}

/* parses the embedded manifest once, and names the cache directory after its digest so a change starts a fresh one. */
static bool parse_manifest(const MimePaths *paths)
{
    if (g_parsed)
        return g_count > 0;
    g_parsed = true;
    uint8_t digest[32];
    MimeSha256 sha;
    mime_sha256_init(&sha);
    mime_sha256_update(&sha, mime_genuine_manifest, strlen(mime_genuine_manifest));
    mime_sha256_final(&sha, digest);
    char tag[17];
    hex_of(digest, 8, tag);
    snprintf(g_dir, sizeof(g_dir), "%s/genuine/%s", paths->cache, tag);

    const char *line = mime_genuine_manifest;
    while (*line && g_count < MIME_GENUINE_MAX) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        const char *next = end ? end + 1 : line + length;
        if (length == 0 || line[0] == '#') {
            line = next;
            continue;
        }
        /* path<TAB>sha256<TAB>key<TAB>size. */
        const char *t1 = memchr(line, '\t', length);
        const char *t2 = t1 ? memchr(t1 + 1, '\t', length - (size_t)(t1 + 1 - line)) : nullptr;
        const char *t3 = t2 ? memchr(t2 + 1, '\t', length - (size_t)(t2 + 1 - line)) : nullptr;
        if (!t1 || !t2 || !t3) {
            mime_warn("genuine: ignoring a malformed manifest line");
            line = next;
            continue;
        }
        GenuineEntry *e = &g_entries[g_count];
        size_t path_len = (size_t)(t1 - line);
        if (path_len >= sizeof(e->path) || (size_t)(t2 - t1 - 1) != 64 || (size_t)(t3 - t2 - 1) >= sizeof(e->key)) {
            mime_warn("genuine: ignoring an over-long manifest line");
            line = next;
            continue;
        }
        memcpy(e->path, line, path_len);
        e->path[path_len] = 0;
        memcpy(e->sha256, t1 + 1, 64);
        e->sha256[64] = 0;
        memcpy(e->key, t2 + 1, (size_t)(t3 - t2 - 1));
        e->key[t3 - t2 - 1] = 0;
        e->size = strtoull(t3 + 1, nullptr, 10);
        const char *slash = strrchr(e->path, '/');
        mime_strlcpy(e->name, slash ? slash + 1 : e->path, sizeof(e->name));
        g_count++;
        line = next;
    }
    return g_count > 0;
}

static void cache_path(uint32_t index, char *out, size_t size)
{
    snprintf(out, size, "%s/%s", g_dir, g_entries[index].path);
}

/* ------------------------------------------------------------------ checks */

static bool size_is(const char *path, uint64_t want)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && (uint64_t)st.st_size == want;
}

static bool hash_is(const uint8_t *bytes, size_t length, const char *want_hex)
{
    uint8_t digest[32];
    char got[65];
    MimeSha256 sha;
    mime_sha256_init(&sha);
    mime_sha256_update(&sha, bytes, length);
    mime_sha256_final(&sha, digest);
    hex_of(digest, 32, got);
    return strcasecmp(got, want_hex) == 0;
}

/* makes every directory on the way to a file. */
static bool make_parents(const char *path)
{
    char work[MIME_PATH_MAX];
    mime_strlcpy(work, path, sizeof(work));
    char *slash = strrchr(work, '/');
    if (!slash)
        return false;
    *slash = 0;
    return mime_mkdir_p(work, 0755);
}

/* ------------------------------------------------------------------ the download */

typedef struct {
    uint8_t *data;
    size_t length;
    bool overflow;
} Buffer;

static size_t on_bytes(char *ptr, size_t size, size_t count, void *context)
{
    Buffer *b = context;
    size_t add = size * count;
    if (b->overflow || b->length + add > GENUINE_MAX_FILE) {
        b->overflow = true;
        return 0;
    }
    memcpy(b->data + b->length, ptr, add);
    b->length += add;
    return add;
}

/* fetches one file from the symbol server and, if its bytes match the manifest, installs it. */
static bool fetch_one(uint32_t index, char *detail, size_t detail_size)
{
    GenuineEntry *e = &g_entries[index];
    char url[512];
    snprintf(url, sizeof(url), SYMBOL_SERVER "/%s/%s/%s", e->name, e->key, e->name);

    Buffer buffer = {.data = malloc(GENUINE_MAX_FILE)};
    if (!buffer.data) {
        snprintf(detail, detail_size, "out of memory");
        return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        free(buffer.data);
        snprintf(detail, detail_size, "curl unavailable");
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Microsoft-Symbol-Server/10.0");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_bytes);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    bool ok = false;
    if (result != CURLE_OK) {
        snprintf(detail, detail_size, buffer.overflow ? "the file is larger than the manifest says"
                                                      : "%s (http %ld)", curl_easy_strerror(result), status);
    } else if (buffer.length != e->size) {
        snprintf(detail, detail_size, "got %zu bytes, the manifest says %llu", buffer.length,
                 (unsigned long long)e->size);
    } else if (!hash_is(buffer.data, buffer.length, e->sha256)) {
        snprintf(detail, detail_size, "the bytes do not match the manifest hash");
    } else {
        char path[MIME_PATH_MAX], part[MIME_PATH_MAX];
        cache_path(index, path, sizeof(path));
        snprintf(part, sizeof(part), "%s.part", path);
        int fd = make_parents(path) ? open(part, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : -1;
        if (fd < 0) {
            snprintf(detail, detail_size, "cannot write the cache: %s", strerror(errno));
        } else {
            size_t written = 0;
            while (written < buffer.length) {
                ssize_t n = write(fd, buffer.data + written, buffer.length - written);
                if (n <= 0) {
                    if (n < 0 && errno == EINTR)
                        continue;
                    break;
                }
                written += (size_t)n;
            }
            bool flushed = written == buffer.length && fsync(fd) == 0;
            close(fd);
            if (flushed && rename(part, path) == 0) {
                ok = true;
            } else {
                snprintf(detail, detail_size, "cannot write the cache: %s", strerror(errno));
                unlink(part);
            }
        }
    }
    free(buffer.data);
    return ok;
}

/* ------------------------------------------------------------------ readiness, for the running machine */

static MimeVm *g_vm;

static void mark_ready(uint32_t index, bool ready)
{
    if (!g_vm)
        return;
    uint64_t bit = 1ull << (index % 64);
    if (ready)
        __atomic_fetch_or(&g_vm->shared->genuine_ready[index / 64], bit, __ATOMIC_RELEASE);
    else
        __atomic_fetch_and(&g_vm->shared->genuine_ready[index / 64], ~bit, __ATOMIC_RELEASE);
}

/*
 * at startup, every file already in the cache is announced to the guest at once; a missing one is fetched in the
 * background and announced when it lands. a file the cache holds is taken on trust: it was hash-checked before it was
 * installed, under a path named for the manifest, so only its size is looked at here, off the boot path.
 */
static void *worker(void *arg)
{
    (void)arg;
    mime_set_thread_name("mime-genuine");
    uint32_t ready = 0, fetched = 0, failed = 0;
    for (uint32_t i = 0; i < g_count; i++) {
        char path[MIME_PATH_MAX];
        cache_path(i, path, sizeof(path));
        if (size_is(path, g_entries[i].size)) {
            mark_ready(i, true);
            ready++;
        }
    }
    /* MIME_GENUINE_NO_FETCH keeps the machine to what is already cached, for an offline or a measured run. */
    if (getenv("MIME_GENUINE_NO_FETCH")) {
        mime_info("genuine: %u system files ready, fetching disabled", ready);
        return nullptr;
    }
    for (uint32_t i = 0; i < g_count; i++) {
        char path[MIME_PATH_MAX];
        cache_path(i, path, sizeof(path));
        if (size_is(path, g_entries[i].size))
            continue;
        char detail[256];
        if (fetch_one(i, detail, sizeof(detail))) {
            mark_ready(i, true);
            fetched++;
        } else {
            mime_info("genuine: %s not fetched: %s", g_entries[i].path, detail);
            failed++;
        }
    }
    if (fetched || failed)
        mime_info("genuine: %u system files ready, %u fetched, %u unavailable", ready + fetched, fetched, failed);
    else
        mime_info("genuine: %u system files ready", ready);
    return nullptr;
}

void mime_genuine_start(MimeVm *vm)
{
    g_vm = vm;
    if (!parse_manifest(vm->paths))
        return;
    static bool curl_ready;
    if (!curl_ready) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_ready = true;
    }
    pthread_t t;
    if (pthread_create(&t, nullptr, worker, nullptr) == 0)
        pthread_detach(t);
}

bool mime_genuine_path(uint32_t index, char *out, size_t size)
{
    if (index >= g_count)
        return false;
    char path[MIME_PATH_MAX];
    cache_path(index, path, sizeof(path));
    if (!size_is(path, g_entries[index].size)) {
        /* the file went since it was announced: take the announcement back so the guest stops asking. */
        mark_ready(index, false);
        return false;
    }
    mime_strlcpy(out, path, size);
    return true;
}

/* ------------------------------------------------------------------ the command */

/* the file's windows path, backslashes and all, for a person to read. */
static const char *windows_path(uint32_t index, char *out, size_t size)
{
    snprintf(out, size, "\\Windows\\%s", g_entries[index].path);
    for (char *c = out; *c; c++)
        if (*c == '/')
            *c = '\\';
    return out;
}

static int cmd_status(void)
{
    uint32_t ready = 0;
    for (uint32_t i = 0; i < g_count; i++) {
        char path[MIME_PATH_MAX], shown[160];
        cache_path(i, path, sizeof(path));
        bool have = size_is(path, g_entries[i].size);
        ready += have;
        printf("%-10s %s\n", have ? "cached" : "missing", windows_path(i, shown, sizeof(shown)));
    }
    printf("\n%u of %u system files cached in %s\n", ready, g_count, g_dir);
    if (ready < g_count)
        printf("run `mime genuine fetch` to download the rest from Microsoft's symbol server.\n");
    return 0;
}

static int cmd_fetch(void)
{
    static bool curl_ready;
    if (!curl_ready) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_ready = true;
    }
    uint32_t have = 0, got = 0, failed = 0;
    for (uint32_t i = 0; i < g_count; i++) {
        char path[MIME_PATH_MAX], shown[160];
        cache_path(i, path, sizeof(path));
        if (size_is(path, g_entries[i].size)) {
            have++;
            continue;
        }
        char detail[256];
        printf("fetching %s ... ", windows_path(i, shown, sizeof(shown)));
        fflush(stdout);
        if (fetch_one(i, detail, sizeof(detail))) {
            printf("ok\n");
            got++;
        } else {
            printf("failed: %s\n", detail);
            failed++;
        }
    }
    printf("\n%u already cached, %u fetched, %u failed\n", have, got, failed);
    return failed ? 1 : 0;
}

/* the cache holds only regular files in one level of directories the host made below <cache>/genuine/<digest>. */
static void remove_tree(const char *path)
{
    DIR *root = opendir(path);
    if (!root)
        return;
    struct dirent *e;
    while ((e = readdir(root))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char child[MIME_PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode))
            remove_tree(child);
        else
            unlink(child);
    }
    closedir(root);
    rmdir(path);
}

static int cmd_clear(void)
{
    /* clears every cached set, not only the current manifest's, so an old one does not linger. */
    char base[MIME_PATH_MAX];
    mime_strlcpy(base, g_dir, sizeof(base));
    char *slash = strrchr(base, '/');
    if (slash)
        *slash = 0;
    remove_tree(base);
    printf("cleared the genuine file cache.\n");
    return 0;
}

int mime_cmd_genuine(const MimePaths *paths, int argc, char **argv)
{
    if (!parse_manifest(paths)) {
        fprintf(stderr, "mime: this build lists no genuine system files.\n");
        return 1;
    }
    const char *action = argc >= 1 ? argv[0] : "status";
    if (!strcmp(action, "status"))
        return cmd_status();
    if (!strcmp(action, "fetch"))
        return cmd_fetch();
    if (!strcmp(action, "clear"))
        return cmd_clear();
    fprintf(stderr, "usage: mime genuine [status|fetch|clear]\n");
    return 2;
}
