#include "wim.h"

#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

#define WIM_HEADER 208
#define WIM_ENTRY 50
#define WIM_VERSION_SOLID 0xE00u
#define WIM_FLAG_COMPRESSED 0x04
#define WIM_FLAG_SOLID 0x10
#define WIM_SOLID_MARK 0x100000000ull
#define WIM_COMPRESS_LZMS 3
#define WIM_SIZE_MASK 0x00FFFFFFFFFFFFFFull
#define MAX_TABLE (64u << 20)

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)p[0] | (uint64_t)p[1] << 8 | (uint64_t)p[2] << 16 | (uint64_t)p[3] << 24 | (uint64_t)p[4] << 32 |
           (uint64_t)p[5] << 40 | (uint64_t)p[6] << 48 | (uint64_t)p[7] << 56;
}

static uint32_t le32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

bool mime_wim_sha1(const char *hex, size_t length, uint8_t *out)
{
    if (!hex || length != 40)
        return false;
    for (int i = 0; i < 20; i++) {
        int a = hex_digit(hex[2 * i]), b = hex_digit(hex[2 * i + 1]);
        if (a < 0 || b < 0)
            return false;
        out[i] = (uint8_t)(a * 16 + b);
    }
    return true;
}

/* the header of a solid resource and its table of chunk sizes. */
static int load_solid(MimeWim *w, MimeWimSolid *s)
{
    uint8_t header[16];
    int error = w->io.read(w->io.context, s->offset, sizeof(header), header);
    if (error)
        return error;
    s->size = le64(header);
    s->chunk_size = le32(header + 8);
    if (le32(header + 12) != WIM_COMPRESS_LZMS || !s->chunk_size || s->chunk_size > MIME_WIM_MAX_CHUNK ||
        (s->chunk_size & (s->chunk_size - 1)) || !s->size || s->size > (1ull << 40))
        return MIME_WIM_UNSUPPORTED;
    uint64_t count = (s->size + s->chunk_size - 1) / s->chunk_size;
    if (count > 1 << 20 || 16 + count * 4 > s->stored)
        return MIME_WIM_BAD_FORMAT;
    s->chunk_count = (uint32_t)count;
    uint8_t *table = w->io.alloc(w->io.context, (size_t)count * 4);
    s->start = w->io.alloc(w->io.context, (size_t)(count + 1) * sizeof(uint64_t));
    if (!table || !s->start) {
        if (table)
            w->io.free(w->io.context, table);
        return MIME_WIM_NO_MEMORY;
    }
    error = w->io.read(w->io.context, s->offset + 16, (size_t)count * 4, table);
    uint64_t at = s->offset + 16 + count * 4;
    for (uint64_t c = 0; !error && c < count; c++) {
        s->start[c] = at;
        at += le32(table + c * 4);
    }
    s->start[count] = at;
    if (!error && at > s->offset + s->stored)
        error = MIME_WIM_BAD_FORMAT;
    w->io.free(w->io.context, table);
    return error;
}

/*
 * the table of streams has 50 bytes for each: the size and the flags in the top byte, the offset, the original size,
 * the part, the reference count and the sha-1. the entries with the solid flag are streams of a solid resource, their
 * offset counts from the start of the uncompressed bytes of the run of resources, and the entries whose original size
 * is 2^32 describe the solid resources themselves: where they are in the file, and how big.
 */
int mime_wim_open(MimeWim *w, const MimeWimIo *io, MimeWimStream *streams, size_t count)
{
    memset(w, 0, sizeof(*w));
    w->io = *io;
    w->cached_solid = -1;
    for (size_t k = 0; k < count; k++)
        streams[k].found = false;
    uint8_t header[WIM_HEADER];
    int error = w->io.read(w->io.context, 0, sizeof(header), header);
    if (error)
        return error;
    w->total = w->io.total(w->io.context);
    if (memcmp(header, "MSWIM\0\0\0", 8) || le32(header + 8) != WIM_HEADER || le32(header + 12) != WIM_VERSION_SOLID)
        return MIME_WIM_BAD_FORMAT;
    uint64_t table_size = le64(header + 0x30) & WIM_SIZE_MASK, table_flags = le64(header + 0x30) >> 56,
             table_offset = le64(header + 0x38), table_original = le64(header + 0x40);
    if ((table_flags & WIM_FLAG_COMPRESSED) && table_size != table_original)
        return MIME_WIM_UNSUPPORTED;
    if (!table_size || table_size > MAX_TABLE || table_size % WIM_ENTRY || !w->total || table_offset + table_size > w->total)
        return MIME_WIM_BAD_FORMAT;
    uint8_t *table = w->io.alloc(w->io.context, (size_t)table_size);
    if (!table)
        return MIME_WIM_NO_MEMORY;
    error = w->io.read(w->io.context, table_offset, (size_t)table_size, table);

    /* the runs of solid entries: the resources first, then the streams that lie in them. */
    size_t entries = (size_t)(table_size / WIM_ENTRY);
    for (size_t i = 0; !error && i < entries;) {
        if (!(table[i * WIM_ENTRY + 7] & WIM_FLAG_SOLID)) {
            i++;
            continue;
        }
        size_t end = i + 1;
        while (end < entries && (table[end * WIM_ENTRY + 7] & WIM_FLAG_SOLID))
            end++;
        int base = w->solid_count;
        for (size_t e = i; !error && e < end; e++) {
            const uint8_t *entry = table + e * WIM_ENTRY;
            if (le64(entry + 16) != WIM_SOLID_MARK)
                continue;
            if (w->solid_count == MIME_WIM_MAX_SOLIDS) {
                error = MIME_WIM_UNSUPPORTED;
                break;
            }
            MimeWimSolid *s = &w->solids[w->solid_count++];
            s->stored = le64(entry) & WIM_SIZE_MASK;
            s->offset = le64(entry + 8);
            error = s->offset + s->stored > w->total ? MIME_WIM_BAD_FORMAT : load_solid(w, s);
        }
        for (size_t e = i; !error && e < end; e++) {
            const uint8_t *entry = table + e * WIM_ENTRY;
            if (le64(entry + 16) == WIM_SOLID_MARK)
                continue;
            for (size_t k = 0; k < count; k++) {
                if (streams[k].found || memcmp(entry + 30, streams[k].sha1, 20))
                    continue;
                uint64_t offset = le64(entry + 8), size = le64(entry) & WIM_SIZE_MASK;
                int s = base;
                while (s < w->solid_count && (offset > w->solids[s].size || size > w->solids[s].size - offset)) {
                    offset -= w->solids[s].size;
                    s++;
                }
                if (s == w->solid_count || size != streams[k].size)
                    continue;
                streams[k].found = true;
                streams[k].solid = s;
                streams[k].offset = offset;
            }
        }
        i = end;
    }
    w->io.free(w->io.context, table);
    return error;
}

void mime_wim_close(MimeWim *w)
{
    for (int i = 0; i < w->solid_count; i++)
        if (w->solids[i].start)
            w->io.free(w->io.context, w->solids[i].start);
    if (w->lzms)
        w->io.free(w->io.context, w->lzms);
    if (w->chunk)
        w->io.free(w->io.context, w->chunk);
    if (w->compressed)
        w->io.free(w->io.context, w->compressed);
    w->solid_count = 0;
    w->lzms = nullptr;
    w->chunk = w->compressed = nullptr;
}

void mime_wim_chunks(const MimeWim *w, const MimeWimStream *stream, uint32_t *first, uint32_t *last)
{
    const MimeWimSolid *s = &w->solids[stream->solid];
    *first = (uint32_t)(stream->offset / s->chunk_size);
    *last = (uint32_t)((stream->offset + (stream->size ? stream->size - 1 : 0)) / s->chunk_size);
}

void mime_wim_chunk_range(const MimeWim *w, int solid, uint32_t chunk, uint64_t *offset, uint64_t *stored)
{
    const MimeWimSolid *s = &w->solids[solid];
    *offset = s->start[chunk];
    *stored = s->start[chunk + 1] - s->start[chunk];
}

static uint64_t chunk_size(const MimeWimSolid *s, uint32_t chunk)
{
    uint64_t size = s->size - (uint64_t)chunk * s->chunk_size;
    return size > s->chunk_size ? s->chunk_size : size;
}

/* decodes a chunk into w->chunk, unless it is the one that is there. */
static int decode_chunk(MimeWim *w, int solid, uint32_t chunk)
{
    if (w->cached_solid == solid && w->cached_chunk == chunk)
        return MIME_WIM_OK;
    if (!w->lzms) {
        w->lzms = w->io.alloc(w->io.context, mime_lzms_size());
        w->chunk = w->io.alloc(w->io.context, MIME_WIM_MAX_CHUNK);
        w->compressed = w->io.alloc(w->io.context, MIME_WIM_MAX_CHUNK);
        if (!w->lzms || !w->chunk || !w->compressed)
            return MIME_WIM_NO_MEMORY;
        mime_lzms_init(w->lzms);
    }
    const MimeWimSolid *s = &w->solids[solid];
    w->cached_solid = -1;
    uint64_t stored = s->start[chunk + 1] - s->start[chunk], size = chunk_size(s, chunk);
    if (stored > size || stored > MIME_WIM_MAX_CHUNK)
        return MIME_WIM_BAD_FORMAT;
    int error = w->io.read(w->io.context, s->start[chunk], (size_t)stored, w->compressed);
    if (error)
        return error;
    if (stored == size)
        memcpy(w->chunk, w->compressed, (size_t)size); /* a chunk that does not shrink is kept as it is. */
    else if (!mime_lzms_decompress(w->lzms, w->compressed, (size_t)stored, w->chunk, (size_t)size))
        return MIME_WIM_BAD_DATA;
    w->cached_solid = solid;
    w->cached_chunk = chunk;
    return MIME_WIM_OK;
}

int mime_wim_read(MimeWim *w, const MimeWimStream *stream, void *out)
{
    const MimeWimSolid *s = &w->solids[stream->solid];
    uint64_t offset = stream->offset;
    size_t done = 0;
    while (done < stream->size) {
        uint32_t chunk = (uint32_t)(offset / s->chunk_size);
        int error = decode_chunk(w, stream->solid, chunk);
        if (error)
            return error;
        size_t within = (size_t)(offset - (uint64_t)chunk * s->chunk_size);
        size_t take = (size_t)(chunk_size(s, chunk) - within);
        if (take > stream->size - done)
            take = (size_t)(stream->size - done);
        memcpy((uint8_t *)out + done, w->chunk + within, take);
        done += take;
        offset += take;
    }
    return MIME_WIM_OK;
}
