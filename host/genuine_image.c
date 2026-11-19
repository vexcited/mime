#include "genuine.h"
#include "mime_host.h"

#include "../guest/lib/wim/wim.h"

#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define PARALLEL_CHUNKS 4
#define ATTEMPTS 4
#define USER_AGENT "Microsoft-Delivery-Optimization/10.0"

/* the errors of a read, beside the reader's own. */
enum { READ_NETWORK = 1, READ_STATUS, READ_DISK };

typedef struct Chunk {
    int solid;
    uint32_t index;
    uint64_t offset, stored; /* in the image. */
    uint64_t have;           /* bytes in its part file. */
} Chunk;

typedef struct Image {
    const char *url;
    CURL *curl; /* the reads of the header and the tables, one at a time. */
    uint64_t total;
    long status;
    char *detail;
    size_t detail_size;
    Chunk *chunks;
    uint32_t chunk_count;
    uint64_t bytes, expect; /* for the progress. */
    uint64_t shown;
} Image;

typedef struct Sink {
    uint8_t *buffer;
    size_t length, capacity;
    CURL *curl;
    bool refused;
} Sink;

static void chunk_path(const Chunk *c, bool part, char *out, size_t size)
{
    snprintf(out, size, "%s/.chunks/%llu.%s", g_genuine_dir, (unsigned long long)c->offset, part ? "part" : "chunk");
}

/* ------------------------------------------------------------------ ranges */

/* the size after the slash of "Content-Range: bytes 5-9/1000". */
static size_t on_header(char *text, size_t size, size_t count, void *context)
{
    Image *image = context;
    size_t length = size * count;
    if (length > 14 && !strncasecmp(text, "content-range:", 14)) {
        const char *slash = memchr(text, '/', length);
        if (slash)
            image->total = strtoull(slash + 1, nullptr, 10);
    }
    return length;
}

/* a server that does not answer with the range would send the whole image. */
static bool partial(CURL *curl)
{
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    return status == 206;
}

static size_t on_range(char *ptr, size_t size, size_t count, void *context)
{
    Sink *sink = context;
    size_t add = size * count;
    if (!partial(sink->curl) || add > sink->capacity - sink->length) {
        sink->refused = true;
        return 0;
    }
    memcpy(sink->buffer + sink->length, ptr, add);
    sink->length += add;
    return add;
}

static void common_options(CURL *c, const char *url)
{
    curl_easy_setopt(c, CURLOPT_URL, url);
    /* the address is microsoft's own and answers itself: no redirect leads elsewhere. */
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
}

static int read_network(Image *image, uint64_t offset, size_t length, void *buffer)
{
    char range[64];
    snprintf(range, sizeof(range), "%llu-%llu", (unsigned long long)offset, (unsigned long long)(offset + length - 1));
    for (int attempt = 0; attempt < ATTEMPTS; attempt++) {
        Sink sink = {buffer, 0, length, image->curl, false};
        curl_easy_setopt(image->curl, CURLOPT_RANGE, range);
        curl_easy_setopt(image->curl, CURLOPT_WRITEFUNCTION, on_range);
        curl_easy_setopt(image->curl, CURLOPT_WRITEDATA, &sink);
        CURLcode result = curl_easy_perform(image->curl);
        curl_easy_getinfo(image->curl, CURLINFO_RESPONSE_CODE, &image->status);
        if (result == CURLE_OK && sink.length == length && image->status == 206)
            return 0;
        if (image->status && image->status != 206) {
            snprintf(image->detail, image->detail_size, "the server of the Windows image answered %ld to a request for "
                     "a part of %s", image->status, image->url);
            return READ_STATUS;
        }
        if (attempt + 1 < ATTEMPTS)
            sleep((unsigned)attempt + 1);
        else
            snprintf(image->detail, image->detail_size, "cannot read the Windows image from Microsoft: %s. Check the "
                     "connection and try again", curl_easy_strerror(result));
    }
    return READ_NETWORK;
}

/* a range inside a chunk that came down is read from its file, the rest from the server. */
static int io_read(void *context, uint64_t offset, size_t length, void *buffer)
{
    Image *image = context;
    for (uint32_t i = 0; i < image->chunk_count; i++) {
        const Chunk *c = &image->chunks[i];
        if (offset < c->offset || offset + length > c->offset + c->stored)
            continue;
        char path[MIME_PATH_MAX];
        chunk_path(c, false, path, sizeof(path));
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        ssize_t got = fd >= 0 ? pread(fd, buffer, length, (off_t)(offset - c->offset)) : -1;
        if (fd >= 0)
            close(fd);
        if (got == (ssize_t)length)
            return 0;
        snprintf(image->detail, image->detail_size, "cannot read %s: %s", path, got < 0 ? strerror(errno) : "short");
        return READ_DISK;
    }
    return read_network(image, offset, length, buffer);
}

static uint64_t io_total(void *context)
{
    return ((Image *)context)->total;
}

static void *io_alloc(void *context, size_t size)
{
    return malloc(size ? size : 1);
}

static void io_free(void *context, void *block)
{
    free(block);
}

/* ------------------------------------------------------------------ the chunks */

static void show_bytes(Image *image, bool last)
{
    if (!g_genuine_terminal)
        return;
    uint64_t total = (image->expect + (1 << 20) - 1) >> 20, mb = last ? total : image->bytes >> 20;
    if (!last && mb == image->shown)
        return;
    image->shown = mb;
    printf("\rDownloaded %llu of %llu MB", (unsigned long long)mb, (unsigned long long)total);
    if (last)
        printf("\n");
    fflush(stdout);
}

typedef struct Transfer {
    Image *image;
    Chunk *chunk;
    FILE *file;
    CURL *curl;
    bool refused;
} Transfer;

static size_t on_chunk(char *ptr, size_t size, size_t count, void *context)
{
    Transfer *t = context;
    size_t add = size * count;
    if (!partial(t->curl) || t->chunk->have + add > t->chunk->stored || fwrite(ptr, 1, add, t->file) != add) {
        t->refused = true;
        return 0;
    }
    t->chunk->have += add;
    t->image->bytes += add;
    show_bytes(t->image, false);
    return add;
}

static bool start_chunk(CURLM *multi, Transfer *t, Image *image, Chunk *chunk)
{
    char path[MIME_PATH_MAX];
    chunk_path(chunk, true, path, sizeof(path));
    *t = (Transfer){image, chunk, fopen(path, "ab"), nullptr, false};
    if (t->file)
        t->curl = curl_easy_init();
    if (!t->curl) {
        if (t->file)
            fclose(t->file);
        t->file = nullptr;
        return false;
    }
    /* a part file from a run that was cut short goes on where it stopped. */
    char range[64];
    snprintf(range, sizeof(range), "%llu-%llu", (unsigned long long)(chunk->offset + chunk->have),
             (unsigned long long)(chunk->offset + chunk->stored - 1));
    common_options(t->curl, image->url);
    curl_easy_setopt(t->curl, CURLOPT_RANGE, range);
    curl_easy_setopt(t->curl, CURLOPT_WRITEFUNCTION, on_chunk);
    curl_easy_setopt(t->curl, CURLOPT_WRITEDATA, t);
    curl_easy_setopt(t->curl, CURLOPT_PRIVATE, t);
    curl_multi_add_handle(multi, t->curl);
    return true;
}

/* ends a transfer, and makes its part file the chunk's file once it is whole. */
static bool end_chunk(CURLM *multi, Transfer *t)
{
    curl_multi_remove_handle(multi, t->curl);
    curl_easy_cleanup(t->curl);
    bool flushed = fflush(t->file) == 0 && fsync(fileno(t->file)) == 0;
    fclose(t->file);
    t->curl = nullptr;
    t->file = nullptr;
    if (!flushed || t->chunk->have != t->chunk->stored)
        return false;
    char part[MIME_PATH_MAX], path[MIME_PATH_MAX];
    chunk_path(t->chunk, true, part, sizeof(part));
    chunk_path(t->chunk, false, path, sizeof(path));
    return rename(part, path) == 0;
}

static bool fetch_chunks(Image *image)
{
    CURLM *multi = curl_multi_init();
    if (!multi) {
        snprintf(image->detail, image->detail_size, "cannot start a download");
        return false;
    }
    Transfer slots[PARALLEL_CHUNKS] = {};
    uint32_t next = 0;
    int attempts[PARALLEL_CHUNKS] = {};
    bool ok = true;
    for (;;) {
        for (int k = 0; k < PARALLEL_CHUNKS && ok; k++) {
            if (slots[k].curl)
                continue;
            while (next < image->chunk_count && image->chunks[next].have == image->chunks[next].stored)
                next++;
            if (next == image->chunk_count)
                break;
            attempts[k] = 0;
            if (!start_chunk(multi, &slots[k], image, &image->chunks[next++])) {
                snprintf(image->detail, image->detail_size, "cannot start a download into %s/.chunks: %s",
                         g_genuine_dir, strerror(errno));
                ok = false;
            }
        }
        int running = 0;
        curl_multi_perform(multi, &running);
        CURLMsg *message;
        int left;
        while ((message = curl_multi_info_read(multi, &left))) {
            if (message->msg != CURLMSG_DONE)
                continue;
            Transfer *t = nullptr;
            curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, (char **)&t);
            CURLcode result = message->data.result;
            long status = 0;
            curl_easy_getinfo(t->curl, CURLINFO_RESPONSE_CODE, &status);
            Chunk *chunk = t->chunk;
            int k = (int)(t - slots);
            bool whole = end_chunk(multi, t);
            if (!ok || whole)
                continue;
            if (status && status != 206) {
                snprintf(image->detail, image->detail_size, "the server of the Windows image answered %ld to a request "
                         "for a part of %s", status, image->url);
                ok = false;
            } else if (++attempts[k] == ATTEMPTS || !start_chunk(multi, t, image, chunk)) {
                snprintf(image->detail, image->detail_size, "cannot download the Windows image from Microsoft: %s. "
                         "Check the connection and try again", curl_easy_strerror(result));
                ok = false;
            }
        }
        bool busy = false;
        for (int k = 0; k < PARALLEL_CHUNKS; k++)
            busy |= slots[k].curl != nullptr;
        if (!ok) {
            /* the bytes that came stay in their part files for the next run. */
            for (int k = 0; k < PARALLEL_CHUNKS; k++)
                if (slots[k].curl)
                    end_chunk(multi, &slots[k]);
            break;
        }
        if (!busy && next == image->chunk_count)
            break;
        curl_multi_poll(multi, nullptr, 0, 1000, nullptr);
    }
    curl_multi_cleanup(multi);
    return ok;
}

/* ------------------------------------------------------------------ the files */

static int compare_chunks(const void *a, const void *b)
{
    const Chunk *x = a, *y = b;
    return x->offset < y->offset ? -1 : x->offset > y->offset;
}

static void add_chunk(Image *image, const MimeWim *wim, int solid, uint32_t index)
{
    for (uint32_t i = 0; i < image->chunk_count; i++)
        if (image->chunks[i].solid == solid && image->chunks[i].index == index)
            return;
    Chunk *c = &image->chunks[image->chunk_count++];
    *c = (Chunk){solid, index, 0, 0, 0};
    mime_wim_chunk_range(wim, solid, index, &c->offset, &c->stored);
}

/* what is on the disk already, a whole chunk or a part of one. */
static void count_cached(Image *image)
{
    for (uint32_t i = 0; i < image->chunk_count; i++) {
        Chunk *c = &image->chunks[i];
        char path[MIME_PATH_MAX];
        struct stat st;
        chunk_path(c, false, path, sizeof(path));
        if (stat(path, &st) == 0 && (uint64_t)st.st_size == c->stored) {
            c->have = c->stored;
            continue;
        }
        unlink(path);
        char part[MIME_PATH_MAX];
        chunk_path(c, true, part, sizeof(part));
        if (stat(part, &st) != 0 || (uint64_t)st.st_size > c->stored)
            unlink(part);
        else if ((uint64_t)st.st_size < c->stored)
            c->have = (uint64_t)st.st_size;
        else if (rename(part, path) == 0)
            /* the run that fetched it was stopped before it could say so. */
            c->have = c->stored;
    }
}

static void forget_chunks(Image *image, const MimeWim *wim, const MimeWimStream *stream)
{
    uint32_t first, last;
    mime_wim_chunks(wim, stream, &first, &last);
    for (uint32_t i = 0; i < image->chunk_count; i++) {
        Chunk *c = &image->chunks[i];
        if (c->solid != stream->solid || c->index < first || c->index > last)
            continue;
        char path[MIME_PATH_MAX];
        chunk_path(c, false, path, sizeof(path));
        unlink(path);
    }
}

static const char *wim_error(int error)
{
    switch (error) {
    case MIME_WIM_BAD_FORMAT:
        return "is not a Windows image Mime can read";
    case MIME_WIM_UNSUPPORTED:
        return "is stored in a way Mime cannot read";
    case MIME_WIM_NO_MEMORY:
        return "needs more memory than there is";
    default:
        return "holds damaged data";
    }
}

bool genuine_from_image(const char *url, char *detail, size_t detail_size)
{
    uint32_t want = 0;
    for (uint32_t i = 0; i < g_genuine_count; i++)
        want += g_genuine[i].image && !genuine_cached(i);
    if (!want)
        return true;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    Image image = {.url = url, .detail = detail, .detail_size = detail_size};
    uint32_t *index = calloc(want, sizeof(*index));
    MimeWimStream *streams = calloc(want, sizeof(*streams));
    MimeWim *wim = calloc(1, sizeof(*wim));
    image.curl = curl_easy_init();
    bool ok = false, opened = false;
    uint8_t *bytes = nullptr;
    if (!index || !streams || !wim || !image.curl) {
        snprintf(detail, detail_size, "cannot start a download");
        goto done;
    }
    common_options(image.curl, url);
    curl_easy_setopt(image.curl, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(image.curl, CURLOPT_HEADERDATA, &image);
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_genuine_count; i++) {
        if (!g_genuine[i].image || genuine_cached(i))
            continue;
        index[n] = i;
        memcpy(streams[n].sha1, g_genuine[i].sha1, 20);
        streams[n].size = g_genuine[i].size;
        n++;
    }
    printf("Reading the Windows image for %u more files...\n", want);
    fflush(stdout);

    MimeWimIo io = {&image, io_read, io_total, io_alloc, io_free};
    int error = mime_wim_open(wim, &io, streams, n);
    opened = true;
    if (error) {
        if (error < 0)
            snprintf(detail, detail_size, "the file at %s %s", url, wim_error(error));
        goto done;
    }
    size_t room = 0;
    for (uint32_t k = 0; k < n; k++) {
        if (!streams[k].found) {
            snprintf(detail, detail_size, "the Windows image at %s does not have %s, a file of Windows build %s", url,
                     g_genuine[index[k]].path, g_genuine_build);
            goto done;
        }
        uint32_t first, last;
        mime_wim_chunks(wim, &streams[k], &first, &last);
        room += last - first + 1;
    }
    image.chunks = calloc(room, sizeof(Chunk));
    if (!image.chunks) {
        snprintf(detail, detail_size, "cannot start a download");
        goto done;
    }
    for (uint32_t k = 0; k < n; k++) {
        uint32_t first, last;
        mime_wim_chunks(wim, &streams[k], &first, &last);
        for (uint32_t c = first; c <= last; c++)
            add_chunk(&image, wim, streams[k].solid, c);
    }
    qsort(image.chunks, image.chunk_count, sizeof(Chunk), compare_chunks);
    char folder[MIME_PATH_MAX];
    snprintf(folder, sizeof(folder), "%s/.chunks", g_genuine_dir);
    if (!mime_mkdir_p(folder, 0755)) {
        snprintf(detail, detail_size, "cannot make %s: %s", folder, strerror(errno));
        goto done;
    }
    count_cached(&image);
    for (uint32_t i = 0; i < image.chunk_count; i++)
        image.expect += image.chunks[i].stored - image.chunks[i].have;
    if (image.expect) {
        printf("Downloading %u parts of the Windows image, %llu MB...\n", image.chunk_count,
               (unsigned long long)((image.expect + (1 << 20) - 1) >> 20));
        fflush(stdout);
        if (!fetch_chunks(&image))
            goto done;
        show_bytes(&image, true);
    }

    /* the files in the order of the image, so each chunk is decoded once. */
    for (uint32_t done_files = 0, k = 0; k < n; k++) {
        uint32_t best = k;
        for (uint32_t j = k + 1; j < n; j++)
            if (streams[j].solid < streams[best].solid ||
                (streams[j].solid == streams[best].solid && streams[j].offset < streams[best].offset))
                best = j;
        MimeWimStream s = streams[k];
        streams[k] = streams[best];
        streams[best] = s;
        uint32_t i = index[k];
        index[k] = index[best];
        index[best] = i;
        const GenuineEntry *e = &g_genuine[index[k]];
        bytes = malloc(e->size ? e->size : 1);
        if (!bytes) {
            snprintf(detail, detail_size, "cannot hold %s in memory", e->path);
            goto done;
        }
        error = mime_wim_read(wim, &streams[k], bytes);
        if (error > 0)
            goto done;
        if (error < 0 || !genuine_install(index[k], bytes, e->size, "the Windows image", detail, detail_size)) {
            if (error < 0)
                snprintf(detail, detail_size, "the part of the Windows image that holds %s %s", e->path, wim_error(error));
            /* the next run fetches those chunks again. */
            forget_chunks(&image, wim, &streams[k]);
            goto done;
        }
        free(bytes);
        bytes = nullptr;
        if (g_genuine_terminal) {
            printf("\rUnpacked %u of %u", ++done_files, n);
            if (done_files == n)
                printf("\n");
            fflush(stdout);
        }
    }
    ok = true;
    genuine_remove_tree(folder);
done:
    free(bytes);
    if (opened)
        mime_wim_close(wim);
    if (image.curl)
        curl_easy_cleanup(image.curl);
    free(image.chunks);
    free(wim);
    free(streams);
    free(index);
    return ok;
}
