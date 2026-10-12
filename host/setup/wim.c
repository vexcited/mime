#include "wim.h"

#include "lzx.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define WIM_HEADER_SIZE 208
#define WIM_BLOB_ENTRY_SIZE 50

#define WIM_HDR_FLAG_COMPRESSION 0x00000002u
#define WIM_HDR_FLAG_COMPRESS_XPRESS 0x00020000u
#define WIM_HDR_FLAG_COMPRESS_LZX 0x00040000u
#define WIM_HDR_FLAG_COMPRESS_LZMS 0x00080000u

#define WIM_RES_FLAG_FREE 0x01
#define WIM_RES_FLAG_METADATA 0x02
#define WIM_RES_FLAG_COMPRESSED 0x04
#define WIM_RES_FLAG_SPANNED 0x08
#define WIM_RES_FLAG_SOLID 0x10

typedef struct WimResource {
    uint64_t stored_size;
    uint64_t offset;
    uint64_t original_size;
    uint8_t flags;
} WimResource;

typedef struct WimBlob {
    WimResource res;
    uint8_t hash[20];
    bool used;
} WimBlob;

struct MimeWim {
    MimeReader reader;
    uint32_t flags;
    uint32_t chunk_size;
    uint32_t image_count;
    WimResource blob_table_res;
    WimResource xml_res;

    WimBlob *blobs; /* open addressing hash table keyed by sha-1 */
    size_t blob_capacity;
    WimResource *metadata; /* metadata resources, in image order */
    uint32_t metadata_count;

    uint16_t *xml;
    size_t xml_chars;

    uint8_t *meta_data; /* decompressed metadata of the selected image */
    uint64_t meta_size;
    MimeWimDentry *root;
    MimeWimDentry *dentry_pool;
    size_t dentry_count, dentry_capacity;
    char *name_pool;
    size_t name_used, name_capacity;

    MimeLzx *lzx;
    uint8_t *chunk_in;
    uint8_t *chunk_out;
};

static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

static void parse_reshdr(const uint8_t *p, WimResource *res)
{
    uint64_t size_flags = rd64(p);
    res->stored_size = size_flags & 0x00FFFFFFFFFFFFFFull;
    res->flags = (uint8_t)(size_flags >> 56);
    res->offset = rd64(p + 8);
    res->original_size = rd64(p + 16);
}

static size_t blob_slot(const uint8_t hash[20], size_t capacity)
{
    uint64_t h;
    memcpy(&h, hash, sizeof(h));
    return (size_t)(h & (capacity - 1));
}

static WimBlob *find_blob(MimeWim *wim, const uint8_t hash[20])
{
    if (!wim->blob_capacity)
        return NULL;
    size_t i = blob_slot(hash, wim->blob_capacity);
    while (wim->blobs[i].used) {
        if (memcmp(wim->blobs[i].hash, hash, 20) == 0)
            return &wim->blobs[i];
        i = (i + 1) & (wim->blob_capacity - 1);
    }
    return NULL;
}

static bool is_zero_hash(const uint8_t hash[20])
{
    for (int i = 0; i < 20; i++)
        if (hash[i])
            return false;
    return true;
}

static bool wim_read(MimeWim *wim, uint64_t offset, void *buf, size_t len)
{
    if (offset + len > wim->reader.size)
        return false;
    return wim->reader.read_at(wim->reader.ctx, offset, buf, len);
}

/*
 * reads a resource and streams the uncompressed bytes into sink.
 * compressed resources start with a chunk table that holds the offsets
 * of chunks 1..n-1 relative to the end of the table.
 */
static bool read_resource(MimeWim *wim, const WimResource *res, MimeWimSink sink, void *ctx)
{
    if (res->flags & WIM_RES_FLAG_SOLID) {
        mime_error("solid (esd style) wim resources are not supported yet");
        return false;
    }
    if (!(res->flags & WIM_RES_FLAG_COMPRESSED) || res->stored_size == res->original_size) {
        uint8_t *buf = wim->chunk_out;
        uint64_t done = 0;
        while (done < res->original_size) {
            size_t n = (size_t)MIME_MIN((uint64_t)wim->chunk_size, res->original_size - done);
            if (!wim_read(wim, res->offset + done, buf, n) || !sink(ctx, buf, n))
                return false;
            done += n;
        }
        return true;
    }

    if (!(wim->flags & WIM_HDR_FLAG_COMPRESS_LZX)) {
        mime_error("unsupported wim compression (flags 0x%x), only lzx is implemented", wim->flags);
        return false;
    }

    uint64_t chunks = (res->original_size + wim->chunk_size - 1) / wim->chunk_size;
    if (chunks == 0)
        return true;
    size_t entry_size = res->original_size > 0xFFFFFFFFull ? 8 : 4;
    size_t table_size = (size_t)(chunks - 1) * entry_size;
    if (table_size > res->stored_size)
        return false;
    uint8_t *table = mime_xmalloc(table_size ? table_size : 1);
    if (table_size && !wim_read(wim, res->offset, table, table_size)) {
        free(table);
        return false;
    }
    uint64_t data_start = res->offset + table_size;
    uint64_t data_size = res->stored_size - table_size;

    bool ok = true;
    for (uint64_t c = 0; c < chunks && ok; c++) {
        uint64_t start = 0, end;
        if (c > 0)
            start = entry_size == 8 ? rd64(table + (c - 1) * 8) : rd32(table + (c - 1) * 4);
        if (c + 1 < chunks)
            end = entry_size == 8 ? rd64(table + c * 8) : rd32(table + c * 4);
        else
            end = data_size;
        uint64_t out_size = MIME_MIN((uint64_t)wim->chunk_size, res->original_size - c * wim->chunk_size);
        if (end < start || end - start > wim->chunk_size || end > data_size) {
            ok = false;
            break;
        }
        size_t in_size = (size_t)(end - start);
        if (!wim_read(wim, data_start + start, wim->chunk_in, in_size)) {
            ok = false;
            break;
        }
        if (in_size == out_size) {
            ok = sink(ctx, wim->chunk_in, in_size);
        } else {
            ok = mime_lzx_decompress(wim->lzx, wim->chunk_in, in_size, wim->chunk_out, (size_t)out_size);
            if (!ok)
                mime_error("lzx decompression failed at chunk %llu", (unsigned long long)c);
            else
                ok = sink(ctx, wim->chunk_out, (size_t)out_size);
        }
    }
    free(table);
    return ok;
}

typedef struct MemSink {
    uint8_t *data;
    size_t size, capacity;
} MemSink;

static bool mem_sink(void *ctx, const void *data, size_t size)
{
    MemSink *m = ctx;
    if (m->size + size > m->capacity) {
        m->capacity = MIME_MAX(m->capacity * 2, m->size + size);
        m->data = mime_xrealloc(m->data, m->capacity);
    }
    memcpy(m->data + m->size, data, size);
    m->size += size;
    return true;
}

static void *read_resource_mem(MimeWim *wim, const WimResource *res, size_t *size_out)
{
    MemSink m = {0};
    m.capacity = (size_t)res->original_size + 1;
    m.data = mime_xmalloc(m.capacity);
    if (!read_resource(wim, res, mem_sink, &m)) {
        free(m.data);
        return NULL;
    }
    *size_out = m.size;
    return m.data;
}

static bool load_blob_table(MimeWim *wim)
{
    size_t size;
    uint8_t *table = read_resource_mem(wim, &wim->blob_table_res, &size);
    if (!table)
        return false;
    size_t count = size / WIM_BLOB_ENTRY_SIZE;
    wim->blob_capacity = 1;
    while (wim->blob_capacity < count * 2)
        wim->blob_capacity <<= 1;
    wim->blobs = mime_xcalloc(wim->blob_capacity, sizeof(WimBlob));

    for (size_t i = 0; i < count; i++) {
        const uint8_t *e = table + i * WIM_BLOB_ENTRY_SIZE;
        WimResource res;
        parse_reshdr(e, &res);
        const uint8_t *hash = e + 30;
        if (res.flags & WIM_RES_FLAG_METADATA) {
            wim->metadata = mime_xrealloc(wim->metadata, sizeof(WimResource) * (wim->metadata_count + 1));
            wim->metadata[wim->metadata_count++] = res;
            continue;
        }
        if (find_blob(wim, hash))
            continue;
        size_t slot = blob_slot(hash, wim->blob_capacity);
        while (wim->blobs[slot].used)
            slot = (slot + 1) & (wim->blob_capacity - 1);
        wim->blobs[slot].used = true;
        wim->blobs[slot].res = res;
        memcpy(wim->blobs[slot].hash, hash, 20);
    }
    free(table);
    return true;
}

static bool load_xml(MimeWim *wim)
{
    size_t size;
    uint8_t *xml = read_resource_mem(wim, &wim->xml_res, &size);
    if (!xml)
        return false;
    wim->xml = (uint16_t *)xml;
    wim->xml_chars = size / 2;
    return true;
}

MimeWim *mime_wim_open(MimeReader reader)
{
    MimeWim *wim = mime_xcalloc(1, sizeof(*wim));
    wim->reader = reader;
    uint8_t hdr[WIM_HEADER_SIZE];
    if (!wim_read(wim, 0, hdr, sizeof(hdr)) || memcmp(hdr, "MSWIM\0\0\0", 8) != 0) {
        mime_error("not a wim file");
        free(wim);
        return NULL;
    }
    wim->flags = rd32(hdr + 16);
    wim->chunk_size = rd32(hdr + 20);
    if (rd16(hdr + 42) > 1) {
        mime_error("split wim files are not supported");
        free(wim);
        return NULL;
    }
    wim->image_count = rd32(hdr + 44);
    parse_reshdr(hdr + 48, &wim->blob_table_res);
    parse_reshdr(hdr + 72, &wim->xml_res);
    if (wim->chunk_size == 0)
        wim->chunk_size = 32768;
    if (wim->flags & WIM_HDR_FLAG_COMPRESS_LZMS) {
        mime_error("lzms compressed wim (install.esd) is not supported yet, use a .wim based iso");
        free(wim);
        return NULL;
    }
    if ((wim->flags & WIM_HDR_FLAG_COMPRESSION) && wim->chunk_size != MIME_LZX_MAX_CHUNK) {
        mime_error("unsupported wim chunk size %u", wim->chunk_size);
        free(wim);
        return NULL;
    }
    wim->lzx = mime_lzx_create();
    wim->chunk_in = mime_xmalloc(wim->chunk_size + 64);
    wim->chunk_out = mime_xmalloc(wim->chunk_size + 64);
    if (!load_blob_table(wim) || !load_xml(wim)) {
        mime_wim_close(wim);
        return NULL;
    }
    return wim;
}

void mime_wim_close(MimeWim *wim)
{
    if (!wim)
        return;
    free(wim->blobs);
    free(wim->metadata);
    free(wim->xml);
    free(wim->meta_data);
    free(wim->dentry_pool);
    free(wim->name_pool);
    mime_lzx_destroy(wim->lzx);
    free(wim->chunk_in);
    free(wim->chunk_out);
    free(wim);
}

uint32_t mime_wim_image_count(const MimeWim *wim)
{
    return wim->image_count;
}

/* finds an ascii tag inside the utf-16 xml between from and to. */
static size_t xml_find(const MimeWim *wim, size_t from, size_t to, const char *needle)
{
    size_t n = strlen(needle);
    for (size_t i = from; i + n <= to; i++) {
        size_t j = 0;
        while (j < n && wim->xml[i + j] == (uint16_t)(unsigned char)needle[j])
            j++;
        if (j == n)
            return i;
    }
    return SIZE_MAX;
}

static void xml_text(const MimeWim *wim, size_t from, size_t to, const char *tag, char *out, size_t out_size)
{
    char open[64], close[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    out[0] = 0;
    size_t a = xml_find(wim, from, to, open);
    if (a == SIZE_MAX)
        return;
    a += strlen(open);
    size_t b = xml_find(wim, a, to, close);
    if (b == SIZE_MAX)
        return;
    mime_utf16_to_utf8(out, out_size, wim->xml + a, b - a);
}

bool mime_wim_image_info(const MimeWim *wim, uint32_t index, MimeWimImageInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->index = index;
    char tag[64];
    snprintf(tag, sizeof(tag), "<IMAGE INDEX=\"%u\">", index);
    size_t start = xml_find(wim, 0, wim->xml_chars, tag);
    if (start == SIZE_MAX)
        return false;
    size_t end = xml_find(wim, start, wim->xml_chars, "</IMAGE>");
    if (end == SIZE_MAX)
        end = wim->xml_chars;
    xml_text(wim, start, end, "NAME", info->name, sizeof(info->name));
    xml_text(wim, start, end, "EDITIONID", info->edition, sizeof(info->edition));
    char arch[16], build[16];
    xml_text(wim, start, end, "ARCH", arch, sizeof(arch));
    xml_text(wim, start, end, "BUILD", build, sizeof(build));
    switch (atoi(arch)) {
    case 0: snprintf(info->arch, sizeof(info->arch), "x86"); break;
    case 5: snprintf(info->arch, sizeof(info->arch), "arm"); break;
    case 9: snprintf(info->arch, sizeof(info->arch), "x64"); break;
    case 12: snprintf(info->arch, sizeof(info->arch), "arm64"); break;
    default: snprintf(info->arch, sizeof(info->arch), "unknown"); break;
    }
    info->build = (uint32_t)atoi(build);
    return true;
}

static char *pool_name(MimeWim *wim, const uint8_t *utf16, size_t chars)
{
    size_t need = chars * 3 + 1;
    if (wim->name_used + need > wim->name_capacity)
        return NULL;
    char *out = wim->name_pool + wim->name_used;
    uint16_t tmp[512];
    if (chars > 511)
        chars = 511;
    for (size_t i = 0; i < chars; i++)
        tmp[i] = rd16(utf16 + i * 2);
    size_t len = mime_utf16_to_utf8(out, need, tmp, chars);
    wim->name_used += len + 1;
    return out;
}

static MimeWimDentry *new_dentry(MimeWim *wim)
{
    if (wim->dentry_count == wim->dentry_capacity)
        return NULL;
    MimeWimDentry *d = &wim->dentry_pool[wim->dentry_count++];
    memset(d, 0, sizeof(*d));
    return d;
}

/* parses one on-disk dentry at offset; returns its total footprint or 0. */
static uint64_t parse_dentry(MimeWim *wim, uint64_t off, MimeWimDentry *d, uint64_t *subdir)
{
    const uint8_t *p = wim->meta_data + off;
    if (off + 102 > wim->meta_size)
        return 0;
    uint64_t length = rd64(p);
    if (length < 102 || off + length > wim->meta_size)
        return 0;
    d->attributes = rd32(p + 8);
    *subdir = rd64(p + 16);
    d->creation_time = rd64(p + 40);
    d->write_time = rd64(p + 56);
    memcpy(d->hash, p + 64, 20);
    uint16_t streams = rd16(p + 96);
    uint16_t name_bytes = rd16(p + 100);
    if (102 + (uint64_t)name_bytes > length)
        return 0;
    d->name = pool_name(wim, p + 102, name_bytes / 2);
    if (!d->name)
        return 0;

    uint64_t total = MIME_ALIGN_UP(length, 8);
    /* extra streams follow the dentry; an unnamed one holds the file data. */
    for (uint16_t s = 0; s < streams; s++) {
        uint64_t so = off + total;
        if (so + 38 > wim->meta_size)
            return 0;
        const uint8_t *sp = wim->meta_data + so;
        uint64_t slen = rd64(sp);
        if (slen < 38)
            return 0;
        uint16_t sname = rd16(sp + 36);
        if (sname == 0 && is_zero_hash(d->hash))
            memcpy(d->hash, sp + 16, 20);
        total += MIME_ALIGN_UP(slen, 8);
    }
    return total;
}

static bool load_children(MimeWim *wim, MimeWimDentry *dir, uint64_t off, int depth)
{
    if (depth > 128)
        return false;
    MimeWimDentry *last = NULL;
    while (off + 8 <= wim->meta_size) {
        uint64_t length = rd64(wim->meta_data + off);
        if (length <= 8)
            break;
        MimeWimDentry *d = new_dentry(wim);
        if (!d)
            return false;
        uint64_t subdir = 0;
        uint64_t total = parse_dentry(wim, off, d, &subdir);
        if (!total)
            return false;
        d->parent = dir;
        if (last)
            last->next_sibling = d;
        else
            dir->first_child = d;
        last = d;
        if ((d->attributes & MIME_FILE_ATTRIBUTE_DIRECTORY) && subdir && !(d->attributes & MIME_FILE_ATTRIBUTE_REPARSE_POINT)) {
            if (!load_children(wim, d, subdir, depth + 1))
                return false;
        }
        off += total;
    }
    return true;
}

bool mime_wim_select_image(MimeWim *wim, uint32_t index)
{
    if (index == 0 || index > wim->metadata_count)
        return false;
    free(wim->meta_data);
    free(wim->dentry_pool);
    free(wim->name_pool);
    wim->meta_data = NULL;
    wim->root = NULL;

    size_t size;
    wim->meta_data = read_resource_mem(wim, &wim->metadata[index - 1], &size);
    if (!wim->meta_data)
        return false;
    wim->meta_size = size;

    /* a rough upper bound: every dentry needs at least 104 bytes. */
    wim->dentry_capacity = size / 104 + 16;
    wim->dentry_pool = mime_xmalloc(wim->dentry_capacity * sizeof(MimeWimDentry));
    wim->dentry_count = 0;
    wim->name_capacity = size * 2 + 1024;
    wim->name_pool = mime_xmalloc(wim->name_capacity);
    wim->name_used = 0;

    if (size < 8)
        return false;
    uint32_t security_len = rd32(wim->meta_data);
    uint64_t root_off = MIME_ALIGN_UP((uint64_t)(security_len ? security_len : 8), 8);
    MimeWimDentry *root = new_dentry(wim);
    uint64_t subdir = 0;
    if (!parse_dentry(wim, root_off, root, &subdir))
        return false;
    root->name = "";
    wim->root = root;
    if (subdir && !load_children(wim, root, subdir, 0)) {
        mime_error("corrupt wim metadata");
        return false;
    }
    return true;
}

MimeWimDentry *mime_wim_root(MimeWim *wim)
{
    return wim->root;
}

MimeWimDentry *mime_wim_lookup(MimeWim *wim, const char *path)
{
    MimeWimDentry *cur = wim->root;
    char component[512];
    const char *p = path;
    while (cur && *p) {
        while (*p == '/' || *p == '\\')
            p++;
        if (!*p)
            break;
        size_t n = 0;
        while (p[n] && p[n] != '/' && p[n] != '\\')
            n++;
        if (n >= sizeof(component))
            return NULL;
        memcpy(component, p, n);
        component[n] = 0;
        p += n;
        MimeWimDentry *child = cur->first_child;
        while (child && strcasecmp(child->name, component) != 0)
            child = child->next_sibling;
        cur = child;
    }
    return cur;
}

uint64_t mime_wim_blob_size(MimeWim *wim, const uint8_t hash[20])
{
    if (is_zero_hash(hash))
        return 0;
    WimBlob *b = find_blob(wim, hash);
    return b ? b->res.original_size : 0;
}

typedef struct HashSink {
    MimeSha1 sha;
    MimeWimSink sink;
    void *ctx;
} HashSink;

static bool hash_sink(void *ctx, const void *data, size_t size)
{
    HashSink *h = ctx;
    mime_sha1_update(&h->sha, data, size);
    return h->sink(h->ctx, data, size);
}

bool mime_wim_read_blob(MimeWim *wim, const uint8_t hash[20], MimeWimSink sink, void *ctx)
{
    if (is_zero_hash(hash))
        return true;
    WimBlob *b = find_blob(wim, hash);
    if (!b) {
        mime_error("wim blob missing from blob table");
        return false;
    }
    HashSink h;
    mime_sha1_init(&h.sha);
    h.sink = sink;
    h.ctx = ctx;
    if (!read_resource(wim, &b->res, hash_sink, &h))
        return false;
    uint8_t digest[20];
    mime_sha1_final(&h.sha, digest);
    if (memcmp(digest, hash, 20) != 0) {
        mime_error("wim blob sha-1 mismatch");
        return false;
    }
    return true;
}

typedef struct FileSink {
    int fd;
} FileSink;

static bool file_sink(void *ctx, const void *data, size_t size)
{
    FileSink *f = ctx;
    const uint8_t *p = data;
    while (size) {
        ssize_t n = write(f->fd, p, size);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        p += n;
        size -= (size_t)n;
    }
    return true;
}

bool mime_wim_extract_file(MimeWim *wim, const MimeWimDentry *dentry, const char *host_path)
{
    char tmp[MIME_PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s.part", host_path);
    FileSink f;
    f.fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (f.fd < 0) {
        mime_error("cannot create %s: %s", tmp, strerror(errno));
        return false;
    }
    bool ok = mime_wim_read_blob(wim, dentry->hash, file_sink, &f);
    if (close(f.fd) != 0)
        ok = false;
    if (ok && rename(tmp, host_path) != 0)
        ok = false;
    if (!ok)
        unlink(tmp);
    return ok;
}
