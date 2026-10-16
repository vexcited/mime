/*
 * the dxbc container: a header with a checksum, the chunks' offsets, and
 * tagged chunks. the checksum is md5 over everything after it, finished
 * with the bit count first in the last block instead of md5's padding.
 */
#include "dxbc.h"

typedef struct DxbcHeader {
    uint32_t magic;
    uint32_t checksum[4];
    uint32_t one;
    uint32_t size;
    uint32_t chunk_count;
} DxbcHeader;

static uint32_t read32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int dxbc_check(const void *bytecode, size_t length)
{
    const uint8_t *b = bytecode;
    if (!b || length < sizeof(DxbcHeader) || read32(b) != DXBC_FOURCC('D', 'X', 'B', 'C'))
        return -1;
    uint32_t size = read32(b + 24), count = read32(b + 28);
    if (size > length || size < sizeof(DxbcHeader) || count > (size - sizeof(DxbcHeader)) / 4)
        return -1;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t offset = read32(b + sizeof(DxbcHeader) + i * 4);
        if (offset > size - 8 || read32(b + offset + 4) > size - offset - 8)
            return -1;
    }
    return 0;
}

const void *dxbc_find_chunk(const void *bytecode, size_t length, uint32_t tag, uint32_t *size)
{
    if (dxbc_check(bytecode, length))
        return nullptr;
    const uint8_t *b = bytecode;
    uint32_t count = read32(b + 28);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t offset = read32(b + sizeof(DxbcHeader) + i * 4);
        if (read32(b + offset) == tag) {
            if (size)
                *size = read32(b + offset + 4);
            return b + offset + 8;
        }
    }
    return nullptr;
}

/* ------------------------------------------------------------------ the checksum */

static uint32_t rol(uint32_t v, int n) { return v << n | v >> (32 - n); }

static void md5_block(uint32_t s[4], const uint8_t *block)
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const int R[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
    uint32_t m[16];
    for (int i = 0; i < 16; i++)
        m[i] = read32(block + i * 4);
    uint32_t a = s[0], b = s[1], c = s[2], d = s[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16)
            f = (b & c) | (~b & d), g = i;
        else if (i < 32)
            f = (d & b) | (~d & c), g = (5 * i + 1) & 15;
        else if (i < 48)
            f = b ^ c ^ d, g = (3 * i + 5) & 15;
        else
            f = c ^ (b | ~d), g = (7 * i) & 15;
        uint32_t t = d;
        d = c;
        c = b;
        b = b + rol(a + f + K[i] + m[g], R[(i / 16) * 4 + (i & 3)]);
        a = t;
    }
    s[0] += a, s[1] += b, s[2] += c, s[3] += d;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24; }

void dxbc_checksum(const void *bytecode, size_t length, uint32_t out[4])
{
    const uint8_t *data = (const uint8_t *)bytecode + 20;
    size_t size = length - 20;
    uint32_t s[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    size_t full = size & ~(size_t)63;
    for (size_t i = 0; i < full; i += 64)
        md5_block(s, data + i);
    size_t left = size - full;
    uint32_t bits = (uint32_t)(size * 8);
    uint8_t block[64];
    __builtin_memset(block, 0, sizeof(block));
    if (left >= 56) {
        /* the rest and its padding fill a block; the counts go in one more. */
        __builtin_memcpy(block, data + full, left);
        block[left] = 0x80;
        md5_block(s, block);
        __builtin_memset(block, 0, sizeof(block));
        put32(block, bits);
    } else {
        put32(block, bits);
        __builtin_memcpy(block + 4, data + full, left);
        block[4 + left] = 0x80;
    }
    put32(block + 60, (bits >> 2) | 1);
    md5_block(s, block);
    __builtin_memcpy(out, s, sizeof(s));
}

/* ------------------------------------------------------------------ signatures */

int dxbc_parse_signature(const void *bytecode, size_t length, uint32_t tag, DxbcSignature *out,
                         const MimeAllocator *alloc)
{
    out->count = 0;
    out->elements = nullptr;
    uint32_t size;
    const uint8_t *chunk = dxbc_find_chunk(bytecode, length, tag, &size);
    if (!chunk || size < 8)
        return -1;
    uint32_t count = read32(chunk), first = read32(chunk + 4);
    /* the 5 variant leads with the stream, the 1 variants also end with the minimum precision. */
    int has_stream = tag == DXBC_FOURCC('O', 'S', 'G', '5') || tag == DXBC_FOURCC('I', 'S', 'G', '1') ||
                     tag == DXBC_FOURCC('O', 'S', 'G', '1') || tag == DXBC_FOURCC('P', 'S', 'G', '1');
    int has_precision = has_stream && tag != DXBC_FOURCC('O', 'S', 'G', '5');
    uint32_t stride = 24 + (has_stream ? 4 : 0) + (has_precision ? 4 : 0);
    if (count > 1024 || first > size || (size - first) / stride < count)
        return -1;
    if (!count)
        return 0;
    DxbcSignatureElement *e = mime_alloc(alloc, count * sizeof(*e));
    if (!e)
        return -1;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *p = chunk + first + i * stride;
        e[i].stream = has_stream ? read32(p) : 0;
        if (has_stream)
            p += 4;
        uint32_t name = read32(p);
        e[i].name = name < size ? (const char *)chunk + name : "";
        /* a name runs to its nul, which must lie inside the chunk. */
        uint32_t end = name;
        while (end < size && chunk[end])
            end++;
        if (end >= size)
            e[i].name = "";
        e[i].semantic_index = read32(p + 4);
        e[i].system_value = read32(p + 8);
        e[i].component_type = read32(p + 12);
        e[i].reg = read32(p + 16);
        e[i].mask = p[20];
        e[i].used_mask = p[21];
        e[i].min_precision = has_precision ? read32(p + 24) : 0;
    }
    out->count = count;
    out->elements = e;
    return 0;
}

int dxbc_parse_io(const void *bytecode, size_t length, int output, DxbcSignature *out, const MimeAllocator *alloc)
{
    static const uint32_t inputs[] = {DXBC_FOURCC('I', 'S', 'G', '1'), DXBC_FOURCC('I', 'S', 'G', 'N')};
    static const uint32_t outputs[] = {DXBC_FOURCC('O', 'S', 'G', '1'), DXBC_FOURCC('O', 'S', 'G', '5'),
                                       DXBC_FOURCC('O', 'S', 'G', 'N')};
    const uint32_t *tags = output ? outputs : inputs;
    uint32_t count = output ? 3 : 2;
    for (uint32_t i = 0; i < count; i++)
        if (dxbc_find_chunk(bytecode, length, tags[i], nullptr))
            return dxbc_parse_signature(bytecode, length, tags[i], out, alloc);
    /* no signature chunk: nothing goes in or out. */
    out->count = 0;
    out->elements = nullptr;
    return 0;
}
