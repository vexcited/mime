/*
 * encoders: a bitmap as a windows bitmap or a png. The png writer filters every row with the filter that gives the
 * smallest sum of magnitudes and compresses the rows with the zlib encoder of the png library.
 */
#include "gdiplus_private.h"
#include "../lib/png/deflate.h"

static void *deflate_alloc(void *context, size_t size)
{
    return gp_alloc(size);
}

static void deflate_free(void *context, void *p)
{
    gp_free(p);
}

static const MimeAllocator g_deflate_alloc = {deflate_alloc, deflate_free, nullptr};

BOOL gp_buffer_append(GpBuffer *buffer, const void *data, SIZE_T size)
{
    if (buffer->Size + size > buffer->Capacity) {
        SIZE_T want = buffer->Capacity ? buffer->Capacity * 2 : 4096;
        while (want < buffer->Size + size)
            want *= 2;
        BYTE *bigger = buffer->Data ? HeapReAlloc(GetProcessHeap(), 0, buffer->Data, want) : gp_alloc(want);
        if (!bigger)
            return FALSE;
        buffer->Data = bigger;
        buffer->Capacity = want;
    }
    memcpy(buffer->Data + buffer->Size, data, size);
    buffer->Size += size;
    return TRUE;
}

static BOOL put8(GpBuffer *b, UINT v)
{
    BYTE x = (BYTE)v;
    return gp_buffer_append(b, &x, 1);
}

static BOOL put16le(GpBuffer *b, UINT v)
{
    BYTE x[2] = {(BYTE)v, (BYTE)(v >> 8)};
    return gp_buffer_append(b, x, 2);
}

static BOOL put32le(GpBuffer *b, UINT v)
{
    BYTE x[4] = {(BYTE)v, (BYTE)(v >> 8), (BYTE)(v >> 16), (BYTE)(v >> 24)};
    return gp_buffer_append(b, x, 4);
}

static BOOL put32be(GpBuffer *b, UINT v)
{
    BYTE x[4] = {(BYTE)(v >> 24), (BYTE)(v >> 16), (BYTE)(v >> 8), (BYTE)v};
    return gp_buffer_append(b, x, 4);
}

/* the pixels per meter a resolution in dots per inch stands for. */
static UINT per_meter(float dpi)
{
    return (UINT)(dpi / 0.0254f + 0.5f);
}

/* ------------------------------------------------------------------ bmp */

/*
 * a bitmap with alpha is written as 32 bits per pixel, every other bitmap as 24, bottom up with rows padded to four
 * bytes, under the 40 byte header.
 */
GpStatus gp_encode_bmp(const GpBitmap *b, GpBuffer *out)
{
    BOOL alpha = IsAlphaPixelFormat(b->Format);
    UINT bytes = alpha ? 4 : 3;
    ULONGLONG stride = ((ULONGLONG)b->Width * bytes + 3) & ~3ull;
    ULONGLONG image = stride * (UINT)b->Height;
    if (image > 0x7fffff00u)
        return OutOfMemory;
    if (!put8(out, 'B') || !put8(out, 'M') || !put32le(out, 54 + (UINT)image) || !put32le(out, 0) ||
        !put32le(out, 54) || !put32le(out, 40) || !put32le(out, (UINT)b->Width) || !put32le(out, (UINT)b->Height) ||
        !put16le(out, 1) || !put16le(out, alpha ? 32 : 24) || !put32le(out, 0) || !put32le(out, (UINT)image) ||
        !put32le(out, per_meter(b->DpiX)) || !put32le(out, per_meter(b->DpiY)) || !put32le(out, 0) ||
        !put32le(out, 0))
        return OutOfMemory;
    BYTE *row = gp_alloc((SIZE_T)stride);
    if (!row)
        return OutOfMemory;
    for (INT y = b->Height - 1; y >= 0; y--) {
        const BYTE *source = b->Bits + (INT_PTR)y * b->Stride;
        memset(row, 0, (SIZE_T)stride);
        for (INT x = 0; x < b->Width; x++) {
            ARGB c = gp_read_pixel(source, x, b->Format);
            row[x * bytes] = (BYTE)c;
            row[x * bytes + 1] = (BYTE)(c >> 8);
            row[x * bytes + 2] = (BYTE)(c >> 16);
            if (alpha)
                row[x * 4 + 3] = (BYTE)(c >> 24);
        }
        if (!gp_buffer_append(out, row, (SIZE_T)stride)) {
            gp_free(row);
            return OutOfMemory;
        }
    }
    gp_free(row);
    return Ok;
}

/* ------------------------------------------------------------------ checksums */

static UINT g_crc_table[256];
static LONG volatile g_crc_ready;

static UINT crc32_update(UINT crc, const BYTE *data, SIZE_T size)
{
    if (!g_crc_ready) {
        for (UINT n = 0; n < 256; n++) {
            UINT c = n;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            g_crc_table[n] = c;
        }
        g_crc_ready = 1;
    }
    crc = ~crc;
    while (size--)
        crc = g_crc_table[(crc ^ *data++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

/* ------------------------------------------------------------------ png */

static BOOL chunk_put(GpBuffer *out, const char *type, const BYTE *data, SIZE_T size)
{
    if (!put32be(out, (UINT)size) || !gp_buffer_append(out, type, 4) || (size && !gp_buffer_append(out, data, size)))
        return FALSE;
    UINT crc = crc32_update(0, (const BYTE *)type, 4);
    crc = crc32_update(crc, data, size);
    return put32be(out, crc);
}

static INT paeth(INT a, INT b, INT c)
{
    INT p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* a row filtered by the type, with the row before it. */
static void filter_row(int type, const BYTE *row, const BYTE *above, SIZE_T length, UINT bpp, BYTE *out)
{
    for (SIZE_T i = 0; i < length; i++) {
        INT a = i >= bpp ? row[i - bpp] : 0, b = above ? above[i] : 0, c = above && i >= bpp ? above[i - bpp] : 0;
        INT predicted = type == 1 ? a : type == 2 ? b : type == 3 ? (a + b) / 2 : type == 4 ? paeth(a, b, c) : 0;
        out[i] = (BYTE)(row[i] - predicted);
    }
}

GpStatus gp_encode_png(const GpBitmap *b, GpBuffer *out)
{
    BOOL alpha = IsAlphaPixelFormat(b->Format);
    UINT bpp = alpha ? 4 : 3;
    SIZE_T length = (SIZE_T)b->Width * bpp;
    ULONGLONG raw_size = (ULONGLONG)(length + 1) * (UINT)b->Height;
    if (raw_size > 0x40000000u)
        return OutOfMemory;
    BYTE *raw = gp_alloc((SIZE_T)raw_size);
    BYTE *rows = gp_alloc(length * 2);
    BYTE *trial = gp_alloc(length);
    BYTE *best = gp_alloc(length);
    GpStatus status = OutOfMemory;
    if (raw && rows && trial && best) {
        BYTE *current = rows, *above = rows + length;
        BOOL first = TRUE;
        for (INT y = 0; y < b->Height; y++) {
            const BYTE *source = b->Bits + (INT_PTR)y * b->Stride;
            for (INT x = 0; x < b->Width; x++) {
                ARGB c = gp_read_pixel(source, x, b->Format);
                current[x * bpp] = (BYTE)(c >> 16);
                current[x * bpp + 1] = (BYTE)(c >> 8);
                current[x * bpp + 2] = (BYTE)c;
                if (alpha)
                    current[x * 4 + 3] = (BYTE)(c >> 24);
            }
            UINT best_cost = ~0u;
            int best_type = 0;
            for (int type = 0; type < 5; type++) {
                filter_row(type, current, first ? nullptr : above, length, bpp, trial);
                UINT cost = 0;
                for (SIZE_T i = 0; i < length; i++)
                    cost += trial[i] < 128 ? trial[i] : 256 - trial[i];
                if (cost < best_cost) {
                    best_cost = cost;
                    best_type = type;
                    memcpy(best, trial, length);
                }
            }
            BYTE *line = raw + (SIZE_T)y * (length + 1);
            line[0] = (BYTE)best_type;
            memcpy(line + 1, best, length);
            BYTE *swap = current;
            current = above;
            above = swap;
            first = FALSE;
        }
        void *packed = nullptr;
        size_t packed_size = 0;
        if (mime_deflate_zlib(raw, (size_t)raw_size, 6, &g_deflate_alloc, &packed, &packed_size) == MIME_DEFLATE_OK) {
            static const BYTE signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
            BYTE header[13] = {(BYTE)(b->Width >> 24), (BYTE)(b->Width >> 16), (BYTE)(b->Width >> 8), (BYTE)b->Width,
                               (BYTE)(b->Height >> 24), (BYTE)(b->Height >> 16), (BYTE)(b->Height >> 8),
                               (BYTE)b->Height, 8, (BYTE)(alpha ? 6 : 2), 0, 0, 0};
            UINT x = per_meter(b->DpiX), y = per_meter(b->DpiY);
            BYTE physical[9] = {(BYTE)(x >> 24), (BYTE)(x >> 16), (BYTE)(x >> 8), (BYTE)x, (BYTE)(y >> 24),
                                (BYTE)(y >> 16), (BYTE)(y >> 8), (BYTE)y, 1};
            if (gp_buffer_append(out, signature, 8) && chunk_put(out, "IHDR", header, 13) &&
                chunk_put(out, "pHYs", physical, 9) && chunk_put(out, "IDAT", packed, packed_size) &&
                chunk_put(out, "IEND", nullptr, 0))
                status = Ok;
            mime_free(&g_deflate_alloc, packed);
        }
    }
    gp_free(raw);
    gp_free(rows);
    gp_free(trial);
    gp_free(best);
    return status;
}
