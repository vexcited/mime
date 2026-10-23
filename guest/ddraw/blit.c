/*
 * blt pixel loops: nearest-pixel copies, stretches and mirrors, source and
 * destination color keys, and fills, on 1 to 4 byte pixels. blt.c decides
 * what is valid to ask of them.
 */
#include "ddraw_private.h"
#include "blit.h"

static inline DWORD load(const BYTE *p, DWORD size)
{
    switch (size) {
    case 1:
        return *p;
    case 2:
        return *(const UINT16 *)p;
    case 3:
        return (DWORD)p[0] | (DWORD)p[1] << 8 | (DWORD)p[2] << 16;
    default:
        return *(const UINT32 *)p;
    }
}

static inline void store(BYTE *p, DWORD size, DWORD v)
{
    switch (size) {
    case 1:
        *p = (BYTE)v;
        break;
    case 2:
        *(UINT16 *)p = (UINT16)v;
        break;
    case 3:
        p[0] = (BYTE)v;
        p[1] = (BYTE)(v >> 8);
        p[2] = (BYTE)(v >> 16);
        break;
    default:
        *(UINT32 *)p = v;
        break;
    }
}

void DdFill(BYTE *dst, LONG pitch, DWORD size, LONG width, LONG height, DWORD value)
{
    for (LONG y = 0; y < height; y++, dst += pitch) {
        switch (size) {
        case 1:
            memset(dst, (BYTE)value, (SIZE_T)width);
            break;
        case 2: {
            UINT16 *p = (UINT16 *)dst;
            for (LONG x = 0; x < width; x++)
                p[x] = (UINT16)value;
            break;
        }
        case 4: {
            UINT32 *p = (UINT32 *)dst;
            for (LONG x = 0; x < width; x++)
                p[x] = value;
            break;
        }
        default:
            for (LONG x = 0; x < width; x++)
                store(dst + x * 3, 3, value);
            break;
        }
    }
}

/* same size, no keys. rows go in the order an overlap needs. */
static void copy(const DDBLIT *b)
{
    SIZE_T row = (SIZE_T)b->width * b->size;
    LONG y0 = 0, y1 = b->height, step = 1;
    if (b->src + (SSIZE_T)b->src_pitch * 0 < b->dst && b->src + (SSIZE_T)b->src_pitch * b->height > b->dst) {
        y0 = b->height - 1;
        y1 = -1;
        step = -1;
    }
    for (LONG y = y0; y != y1; y += step)
        memmove(b->dst + (SSIZE_T)y * b->dst_pitch, b->src + (SSIZE_T)y * b->src_pitch, row);
}

/*
 * each destination pixel takes the source pixel its center falls on,
 * mirrored as asked and filtered by the keys. a source overlapping the
 * destination is read from a copy first.
 */
static void general(const DDBLIT *b)
{
    const BYTE *src = b->src;
    LONG src_pitch = b->src_pitch;
    BYTE *copy_bytes = nullptr;
    SSIZE_T src_span = (SSIZE_T)b->src_pitch * b->src_height, dst_span = (SSIZE_T)b->dst_pitch * b->height;
    if (b->src < b->dst + dst_span && b->dst < b->src + src_span) {
        SIZE_T row = (SIZE_T)b->src_width * b->size;
        copy_bytes = HeapAlloc(GetProcessHeap(), 0, row * (SIZE_T)b->src_height);
        if (copy_bytes) {
            for (LONG y = 0; y < b->src_height; y++)
                memcpy(copy_bytes + row * (SIZE_T)y, b->src + (SSIZE_T)y * b->src_pitch, row);
            src = copy_bytes;
            src_pitch = (LONG)row;
        }
    }
    DWORD size = b->size;
    for (LONG y = 0; y < b->height; y++) {
        LONG sy = (LONG)(((LONGLONG)y * b->src_height + b->src_height / 2) / b->height);
        if (sy >= b->src_height)
            sy = b->src_height - 1;
        if (b->mirror_y)
            sy = b->src_height - 1 - sy;
        const BYTE *srow = src + (SSIZE_T)sy * src_pitch;
        BYTE *drow = b->dst + (SSIZE_T)y * b->dst_pitch;
        for (LONG x = 0; x < b->width; x++) {
            LONG sx = (LONG)(((LONGLONG)x * b->src_width + b->src_width / 2) / b->width);
            if (sx >= b->src_width)
                sx = b->src_width - 1;
            if (b->mirror_x)
                sx = b->src_width - 1 - sx;
            DWORD v = load(srow + (SIZE_T)sx * size, size);
            if (b->src_key) {
                DWORD k = v & b->key_mask;
                if (k >= b->src_low && k <= b->src_high)
                    continue;
            }
            BYTE *d = drow + (SIZE_T)x * size;
            if (b->dst_key) {
                DWORD k = load(d, size) & b->key_mask;
                if (k < b->dst_low || k > b->dst_high)
                    continue;
            }
            store(d, size, v);
        }
    }
    HeapFree(GetProcessHeap(), 0, copy_bytes);
}

/* the most common sprite blt: one key, no stretch, 1, 2 or 4 byte pixels. */
#define KEYED_COPY(type)                                                                                              \
    for (LONG y = 0; y < b->height; y++) {                                                                            \
        const type *s = (const type *)(b->src + (SSIZE_T)y * b->src_pitch);                                           \
        type *d = (type *)(b->dst + (SSIZE_T)y * b->dst_pitch);                                                       \
        for (LONG x = 0; x < b->width; x++) {                                                                         \
            type v = s[x];                                                                                            \
            if ((v & mask) != key)                                                                                    \
                d[x] = v;                                                                                             \
        }                                                                                                             \
    }

void DdBlit(const DDBLIT *b)
{
    if (b->width <= 0 || b->height <= 0)
        return;
    BOOL same = b->src_width == b->width && b->src_height == b->height && !b->mirror_x && !b->mirror_y;
    if (same && !b->src_key && !b->dst_key) {
        copy(b);
        return;
    }
    BOOL overlap = b->src < b->dst + (SSIZE_T)b->dst_pitch * b->height &&
                   b->dst < b->src + (SSIZE_T)b->src_pitch * b->src_height;
    if (same && b->src_key && !b->dst_key && b->src_low == b->src_high && !overlap) {
        DWORD mask = b->key_mask, key = b->src_low;
        switch (b->size) {
        case 1:
            KEYED_COPY(BYTE)
            return;
        case 2:
            KEYED_COPY(UINT16)
            return;
        case 4:
            KEYED_COPY(UINT32)
            return;
        }
    }
    general(b);
}
