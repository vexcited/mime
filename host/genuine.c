/*
 * microsoft's own copies of a few system files, kept in a host cache outside the guest drive so an identity check sees
 * the bytes microsoft ships while mime's code still runs. data/genuine-<arch>.manifest, of the guest's architecture,
 * lists each file by its windows path, sha-256, symbol server key and size, all of the manifest's build: on arm64 the
 * arm64x files of windows on arm. mime needs them all to run anything: mime setup,
 * mime up and mime run get the missing ones from microsoft's symbol server, check each against the manifest and
 * install it atomically. nothing goes on the guest drive and nothing of microsoft's is in the build, see
 * guest/ntos/io/mimefs.c and MimeRomEntry.genuine.
 */
#include "mime_cli.h"
#include "mime_host.h"
#include "ui/ui.h"
#include "vmm/vmm.h"

#include "genuine_manifest.h" /* mime_genuine_manifest[], written by scripts/genuine.cmake. */

#include <curl/curl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
static char g_build[32] = "?";
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

    for (const char *line = mime_genuine_manifest; *line && g_count < MIME_GENUINE_MAX;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        const char *next = end ? end + 1 : line + length;
        if (length > 8 && !strncmp(line, "# build ", 8)) {
            size_t n = length - 8 < sizeof(g_build) - 1 ? length - 8 : sizeof(g_build) - 1;
            memcpy(g_build, line + 8, n);
            g_build[n] = 0;
        }
        if (length == 0 || line[0] == '#') {
            line = next;
            continue;
        }
        /* path, sha256, key and size, by tabs. */
        const char *field[4];
        size_t field_length[4];
        const char *at = line, *stop = line + length;
        int fields = 0;
        while (fields < 4 && at <= stop) {
            const char *tab = memchr(at, '\t', (size_t)(stop - at));
            field[fields] = at;
            field_length[fields] = (size_t)((tab ? tab : stop) - at);
            fields++;
            at = tab ? tab + 1 : stop + 1;
        }
        GenuineEntry *e = &g_entries[g_count];
        if (fields != 4 || field_length[0] >= sizeof(e->path) || field_length[1] != 64 ||
            field_length[2] >= sizeof(e->key)) {
            mime_warn("genuine: ignoring a malformed manifest line");
            line = next;
            continue;
        }
        memcpy(e->path, field[0], field_length[0]);
        e->path[field_length[0]] = 0;
        memcpy(e->sha256, field[1], 64);
        e->sha256[64] = 0;
        memcpy(e->key, field[2], field_length[2]);
        e->key[field_length[2]] = 0;
        e->size = strtoull(field[3], nullptr, 10);
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

/* ------------------------------------------------------------------ the cache */

/* a file is in the cache only after its bytes matched the manifest, so its size is all that is looked at here. */
static bool cached(uint32_t index)
{
    char path[MIME_PATH_MAX];
    cache_path(index, path, sizeof(path));
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && (uint64_t)st.st_size == g_entries[index].size;
}

static uint32_t missing(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_count; i++)
        n += !cached(i);
    return n;
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

/* checks the bytes against the manifest and puts them in the cache in one rename, so a reader never sees half a file. */
static bool install(uint32_t index, const uint8_t *bytes, size_t length, const char *from, char *detail, size_t detail_size)
{
    const GenuineEntry *e = &g_entries[index];
    if (length != e->size || !hash_is(bytes, length, e->sha256)) {
        snprintf(detail, detail_size, "the %s that %s gave is not the one of Windows build %s", e->name, from, g_build);
        return false;
    }
    char path[MIME_PATH_MAX], part[MIME_PATH_MAX];
    cache_path(index, path, sizeof(path));
    snprintf(part, sizeof(part), "%s.%d.part", path, (int)getpid());
    int fd = make_parents(path) ? open(part, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : -1;
    if (fd < 0) {
        snprintf(detail, detail_size, "cannot write %s: %s", path, strerror(errno));
        return false;
    }
    size_t written = 0;
    while (written < length) {
        ssize_t n = write(fd, bytes + written, length - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        written += (size_t)n;
    }
    bool flushed = written == length && fsync(fd) == 0;
    close(fd);
    if (flushed && rename(part, path) == 0)
        return true;
    snprintf(detail, detail_size, "cannot write %s: %s", path, strerror(errno));
    unlink(part);
    return false;
}

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

/* the sets of earlier manifests, which a new mime no longer reads. */
static void remove_old_sets(void)
{
    char base[MIME_PATH_MAX];
    mime_strlcpy(base, g_dir, sizeof(base));
    char *slash = strrchr(base, '/');
    if (!slash)
        return;
    const char *current = slash + 1;
    *slash = 0;
    DIR *root = opendir(base);
    if (!root)
        return;
    struct dirent *e;
    while ((e = readdir(root))) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, current))
            continue;
        char child[MIME_PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", base, e->d_name);
        remove_tree(child);
    }
    closedir(root);
}

/* ------------------------------------------------------------------ getting them */

static bool g_terminal_progress;

static void progress(const char *what, uint32_t done, uint32_t total)
{
    if (!g_terminal_progress)
        return;
    printf("\r%s: %u of %u", what, done, total);
    if (done == total)
        printf("\n");
    fflush(stdout);
}

#define PARALLEL_DOWNLOADS 8

typedef struct {
    uint32_t index;
    uint8_t *data;
    size_t length, capacity;
    bool overflow;
    CURL *curl;
} Download;

static size_t on_bytes(char *ptr, size_t size, size_t count, void *context)
{
    Download *d = context;
    size_t add = size * count;
    if (d->overflow || add > d->capacity - d->length) {
        d->overflow = true;
        return 0;
    }
    memcpy(d->data + d->length, ptr, add);
    d->length += add;
    return add;
}

static CURL *start_download(CURLM *multi, Download *d, uint32_t index)
{
    const GenuineEntry *e = &g_entries[index];
    d->index = index;
    d->length = 0;
    d->overflow = false;
    d->capacity = (size_t)e->size;
    d->data = malloc(d->capacity ? d->capacity : 1);
    d->curl = d->data ? curl_easy_init() : nullptr;
    if (!d->curl) {
        free(d->data);
        d->data = nullptr;
        return nullptr;
    }
    char url[512];
    snprintf(url, sizeof(url), SYMBOL_SERVER "/%s/%s/%s", e->name, e->key, e->name);
    CURL *c = d->curl;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Microsoft-Symbol-Server/10.0");
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_bytes);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, d);
    curl_easy_setopt(c, CURLOPT_PRIVATE, d);
    curl_multi_add_handle(multi, c);
    return c;
}

static void end_download(CURLM *multi, Download *d)
{
    curl_multi_remove_handle(multi, d->curl);
    curl_easy_cleanup(d->curl);
    free(d->data);
    d->curl = nullptr;
    d->data = nullptr;
}

/* a few at a time: each file costs the server's redirect and a connection more than its bytes. */
static bool from_network(char *detail, size_t detail_size)
{
    static bool curl_ready;
    if (!curl_ready) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_ready = true;
    }
    CURLM *multi = curl_multi_init();
    if (!multi) {
        snprintf(detail, detail_size, "cannot start a download");
        return false;
    }
    uint32_t todo = missing(), done = 0, next = 0;
    printf("Downloading %u Windows system files from Microsoft...\n", todo);
    Download slots[PARALLEL_DOWNLOADS] = {};
    bool ok = true;
    int running = 0;
    for (;;) {
        for (int k = 0; k < PARALLEL_DOWNLOADS && ok; k++) {
            if (slots[k].curl)
                continue;
            while (next < g_count && cached(next))
                next++;
            if (next == g_count)
                break;
            if (!start_download(multi, &slots[k], next++)) {
                snprintf(detail, detail_size, "cannot start a download");
                ok = false;
            }
        }
        curl_multi_perform(multi, &running);
        CURLMsg *message;
        int left;
        while ((message = curl_multi_info_read(multi, &left))) {
            if (message->msg != CURLMSG_DONE)
                continue;
            Download *d = nullptr;
            curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, (char **)&d);
            CURLcode result = message->data.result;
            const GenuineEntry *e = &g_entries[d->index];
            if (ok && result != CURLE_OK) {
                long status = 0;
                curl_easy_getinfo(d->curl, CURLINFO_RESPONSE_CODE, &status);
                if (g_terminal_progress && done)
                    printf("\n");
                if (d->overflow)
                    snprintf(detail, detail_size, "Microsoft's server gave a %s larger than the one of build %s.",
                             e->name, g_build);
                else if (status)
                    snprintf(detail, detail_size, "Microsoft's server did not give %s: %s, http %ld.", e->name,
                             curl_easy_strerror(result), status);
                else
                    snprintf(detail, detail_size, "Cannot download %s from Microsoft: %s. Check the connection and "
                             "try again.", e->name, curl_easy_strerror(result));
                ok = false;
            } else if (ok && !install(d->index, d->data, d->length, "Microsoft's server", detail, detail_size)) {
                ok = false;
            } else if (ok) {
                progress("Downloaded", ++done, todo);
            }
            end_download(multi, d);
        }
        bool busy = false;
        for (int k = 0; k < PARALLEL_DOWNLOADS; k++)
            busy |= slots[k].curl != nullptr;
        if (!busy && (!ok || next == g_count))
            break;
        if (!ok) {
            /* the first failure says it all: the rest are dropped. */
            for (int k = 0; k < PARALLEL_DOWNLOADS; k++)
                if (slots[k].curl)
                    end_download(multi, &slots[k]);
            break;
        }
        curl_multi_poll(multi, nullptr, 0, 1000, nullptr);
    }
    curl_multi_cleanup(multi);
    return ok;
}

int mime_genuine_ensure(const MimePaths *paths)
{
    if (!parse_manifest(paths))
        return 0;
    if (!missing()) {
        remove_old_sets();
        return 0;
    }
    g_terminal_progress = isatty(STDOUT_FILENO);
    char detail[1024] = "";
    if (from_network(detail, sizeof(detail)) && !missing()) {
        remove_old_sets();
        printf("The Windows system files are ready.\n");
        return 0;
    }
    if (detail[0] >= 'a' && detail[0] <= 'z')
        detail[0] = (char)(detail[0] - 'a' + 'A');
    fprintf(stderr, "mime: %s\n", detail);
    /* started from the desktop, there is no terminal to read the error in. */
    if (!isatty(STDERR_FILENO))
        mime_ui_alert(detail);
    return 1;
}

void mime_genuine_print_status(const MimePaths *paths)
{
    if (!parse_manifest(paths))
        return;
    uint32_t have = g_count - missing();
    printf("Windows system files: %u of %u, build %s%s\n", have, g_count, g_build,
           have < g_count ? ", run mime setup for the rest" : "");
}

/* ------------------------------------------------------------------ the running machine */

static int g_fds[MIME_GENUINE_MAX];

bool mime_genuine_open_all(MimeVm *vm)
{
    if (!parse_manifest(vm->paths))
        return true;
    for (uint32_t i = 0; i < g_count; i++)
        g_fds[i] = -1;
    for (uint32_t i = 0; i < g_count; i++) {
        char path[MIME_PATH_MAX];
        cache_path(i, path, sizeof(path));
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        struct stat st;
        if (fd < 0 || fstat(fd, &st) != 0 || (uint64_t)st.st_size != g_entries[i].size) {
            mime_error("the Windows system file %s is not in %s, run mime setup", g_entries[i].path, g_dir);
            if (fd >= 0)
                close(fd);
            for (uint32_t k = 0; k < i; k++)
                close(g_fds[k]);
            return false;
        }
        g_fds[i] = fd;
    }
    mime_info("genuine: %u Windows system files of build %s", g_count, g_build);
    return true;
}

int mime_genuine_dup(uint32_t index, char *path, size_t size)
{
    if (index >= g_count || g_fds[index] < 0)
        return -1;
    cache_path(index, path, size);
    return fcntl(g_fds[index], F_DUPFD_CLOEXEC, 0);
}
