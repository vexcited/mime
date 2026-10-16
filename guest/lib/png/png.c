#include "png.h"
#include <stdckdint.h>
#include "inflate.h"

#define CHUNK(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define CHUNK_IHDR CHUNK('I', 'H', 'D', 'R')
#define CHUNK_PLTE CHUNK('P', 'L', 'T', 'E')
#define CHUNK_IDAT CHUNK('I', 'D', 'A', 'T')
#define CHUNK_IEND CHUNK('I', 'E', 'N', 'D')
#define CHUNK_TRNS CHUNK('t', 'R', 'N', 'S')

static constexpr uint8_t png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

static constexpr uint32_t crc_table[256] = {
    0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau, 0x076dc419u, 0x706af48fu,
    0xe963a535u, 0x9e6495a3u, 0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u,
    0x09b64c2bu, 0x7eb17cbdu, 0xe7b82d07u, 0x90bf1d91u, 0x1db71064u, 0x6ab020f2u,
    0xf3b97148u, 0x84be41deu, 0x1adad47du, 0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u,
    0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu, 0x14015c4fu, 0x63066cd9u,
    0xfa0f3d63u, 0x8d080df5u, 0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u, 0xa2677172u,
    0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu, 0x35b5a8fau, 0x42b2986cu,
    0xdbbbc9d6u, 0xacbcf940u, 0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u,
    0x26d930acu, 0x51de003au, 0xc8d75180u, 0xbfd06116u, 0x21b4f4b5u, 0x56b3c423u,
    0xcfba9599u, 0xb8bda50fu, 0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u,
    0x2f6f7c87u, 0x58684c11u, 0xc1611dabu, 0xb6662d3du, 0x76dc4190u, 0x01db7106u,
    0x98d220bcu, 0xefd5102au, 0x71b18589u, 0x06b6b51fu, 0x9fbfe4a5u, 0xe8b8d433u,
    0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u, 0x7f6a0dbbu, 0x086d3d2du,
    0x91646c97u, 0xe6635c01u, 0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu,
    0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u, 0x65b0d9c6u, 0x12b7e950u,
    0x8bbeb8eau, 0xfcb9887cu, 0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u,
    0x4db26158u, 0x3ab551ceu, 0xa3bc0074u, 0xd4bb30e2u, 0x4adfa541u, 0x3dd895d7u,
    0xa4d1c46du, 0xd3d6f4fbu, 0x4369e96au, 0x346ed9fcu, 0xad678846u, 0xda60b8d0u,
    0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u, 0x5005713cu, 0x270241aau,
    0xbe0b1010u, 0xc90c2086u, 0x5768b525u, 0x206f85b3u, 0xb966d409u, 0xce61e49fu,
    0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u, 0x59b33d17u, 0x2eb40d81u,
    0xb7bd5c3bu, 0xc0ba6cadu, 0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au,
    0xead54739u, 0x9dd277afu, 0x04db2615u, 0x73dc1683u, 0xe3630b12u, 0x94643b84u,
    0x0d6d6a3eu, 0x7a6a5aa8u, 0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u,
    0xf00f9344u, 0x8708a3d2u, 0x1e01f268u, 0x6906c2feu, 0xf762575du, 0x806567cbu,
    0x196c3671u, 0x6e6b06e7u, 0xfed41b76u, 0x89d32be0u, 0x10da7a5au, 0x67dd4accu,
    0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u, 0xd6d6a3e8u, 0xa1d1937eu,
    0x38d8c2c4u, 0x4fdff252u, 0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
    0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u, 0xdf60efc3u, 0xa867df55u,
    0x316e8eefu, 0x4669be79u, 0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u,
    0xcc0c7795u, 0xbb0b4703u, 0x220216b9u, 0x5505262fu, 0xc5ba3bbeu, 0xb2bd0b28u,
    0x2bb45a92u, 0x5cb36a04u, 0xc2d7ffa7u, 0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du,
    0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au, 0x9c0906a9u, 0xeb0e363fu,
    0x72076785u, 0x05005713u, 0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu, 0x0cb61b38u,
    0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u, 0x86d3d2d4u, 0xf1d4e242u,
    0x68ddb3f8u, 0x1fda836eu, 0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u,
    0x88085ae6u, 0xff0f6a70u, 0x66063bcau, 0x11010b5cu, 0x8f659effu, 0xf862ae69u,
    0x616bffd3u, 0x166ccf45u, 0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u,
    0xa7672661u, 0xd06016f7u, 0x4969474du, 0x3e6e77dbu, 0xaed16a4au, 0xd9d65adcu,
    0x40df0b66u, 0x37d83bf0u, 0xa9bcae53u, 0xdebb9ec5u, 0x47b2cf7fu, 0x30b5ffe9u,
    0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u, 0xbad03605u, 0xcdd70693u,
    0x54de5729u, 0x23d967bfu, 0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u,
    0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du,
};

typedef struct PngParse {
    MimePngInfo info;
    unsigned channels;
    uint8_t pal[256][3];
    uint8_t pal_alpha[256];
    unsigned pal_count;
    bool has_plte;
    bool has_trns;
    uint16_t trns[3];
    const uint8_t *first_idat;
    size_t idat_total;
    unsigned idat_count;
} PngParse;

typedef struct PngPass {
    uint32_t x0, y0, dx, dy;
    uint32_t w, h;
    size_t rowbytes;
} PngPass;

static constexpr uint8_t adam7[7][4] = {
    {0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2},
};

static inline uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static inline unsigned be16(const uint8_t *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    while (n--)
        c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static bool chunk_letter(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int parse_ihdr(const uint8_t *b, PngParse *p)
{
    uint32_t w = be32(b), h = be32(b + 4);
    uint8_t depth = b[8], type = b[9];

    if (!w || !h || w > 0x7fffffffu || h > 0x7fffffffu)
        return MIME_PNG_ERR_FORMAT;
    if (b[10] != 0 || b[11] != 0 || b[12] > 1)
        return MIME_PNG_ERR_FORMAT;
    switch (type) {
    case MIME_PNG_COLOR_GRAY:
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16)
            return MIME_PNG_ERR_FORMAT;
        p->channels = 1;
        break;
    case MIME_PNG_COLOR_PALETTE:
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8)
            return MIME_PNG_ERR_FORMAT;
        p->channels = 1;
        break;
    case MIME_PNG_COLOR_RGB:
    case MIME_PNG_COLOR_GRAY_ALPHA:
    case MIME_PNG_COLOR_RGBA:
        if (depth != 8 && depth != 16)
            return MIME_PNG_ERR_FORMAT;
        p->channels = type == MIME_PNG_COLOR_RGB ? 3 : type == MIME_PNG_COLOR_RGBA ? 4 : 2;
        break;
    default:
        return MIME_PNG_ERR_FORMAT;
    }
    if (w > MIME_PNG_MAX_DIMENSION || h > MIME_PNG_MAX_DIMENSION)
        return MIME_PNG_ERR_UNSUPPORTED;
    p->info.width = w;
    p->info.height = h;
    p->info.bit_depth = depth;
    p->info.color_type = type;
    p->info.interlaced = b[12] == 1;
    p->info.has_alpha = type == MIME_PNG_COLOR_GRAY_ALPHA || type == MIME_PNG_COLOR_RGBA;
    return MIME_PNG_OK;
}

static void parse_trns(const uint8_t *b, uint32_t len, PngParse *p)
{
    switch (p->info.color_type) {
    case MIME_PNG_COLOR_GRAY:
        if (len < 2)
            return;
        p->trns[0] = (uint16_t)be16(b);
        break;
    case MIME_PNG_COLOR_RGB:
        if (len < 6)
            return;
        for (int i = 0; i < 3; i++)
            p->trns[i] = (uint16_t)be16(b + 2 * i);
        break;
    case MIME_PNG_COLOR_PALETTE:
        for (uint32_t i = 0; i < len && i < 256; i++)
            p->pal_alpha[i] = b[i];
        break;
    default:
        return;
    }
    p->has_trns = true;
    p->info.has_alpha = true;
}

/*
 * walks the chunk list. with stop_at_idat the walk ends at the first IDAT
 * chunk, otherwise it needs IEND and all IDAT chunks must be consecutive.
 */
static int parse(const uint8_t *d, size_t size, bool check_crc, bool stop_at_idat, PngParse *p)
{
    bool seen_ihdr = false, idat_run = false, idat_done = false;
    size_t pos = 8;

    __builtin_memset(p, 0, sizeof(*p));
    __builtin_memset(p->pal_alpha, 255, sizeof(p->pal_alpha));
    if (!mime_png_is_png(d, size))
        return MIME_PNG_ERR_FORMAT;

    for (;;) {
        if (size - pos < 12)
            return MIME_PNG_ERR_FORMAT;
        uint32_t len = be32(d + pos);
        uint32_t type = be32(d + pos + 4);
        const uint8_t *body = d + pos + 8;
        if (len > 0x7fffffffu || len > size - pos - 12)
            return MIME_PNG_ERR_FORMAT;
        for (int i = 4; i < 8; i++)
            if (!chunk_letter(d[pos + i]))
                return MIME_PNG_ERR_FORMAT;
        if (check_crc && crc32(d + pos + 4, (size_t)len + 4) != be32(body + len))
            return MIME_PNG_ERR_CRC;
        if (!seen_ihdr && type != CHUNK_IHDR)
            return MIME_PNG_ERR_FORMAT;
        if (idat_run && type != CHUNK_IDAT) {
            idat_run = false;
            idat_done = true;
        }

        int r;
        switch (type) {
        case CHUNK_IHDR:
            if (seen_ihdr || len != 13)
                return MIME_PNG_ERR_FORMAT;
            r = parse_ihdr(body, p);
            if (r)
                return r;
            seen_ihdr = true;
            break;
        case CHUNK_PLTE:
            /* a palette after the image data is ignored, it is only a suggestion for non indexed images. */
            if (p->idat_count || p->has_plte)
                break;
            if (len == 0 || len % 3 || len > 768)
                return MIME_PNG_ERR_FORMAT;
            p->pal_count = len / 3;
            __builtin_memcpy(p->pal, body, len);
            p->has_plte = true;
            break;
        case CHUNK_TRNS:
            if (!p->idat_count && !p->has_trns)
                parse_trns(body, len, p);
            break;
        case CHUNK_IDAT:
            if (idat_done)
                return MIME_PNG_ERR_FORMAT;
            if (p->info.color_type == MIME_PNG_COLOR_PALETTE && !p->has_plte)
                return MIME_PNG_ERR_FORMAT;
            if (!p->idat_count)
                p->first_idat = d + pos;
            p->idat_count++;
            p->idat_total += len;
            idat_run = true;
            if (stop_at_idat)
                return MIME_PNG_OK;
            break;
        case CHUNK_IEND:
            return p->idat_count ? MIME_PNG_OK : MIME_PNG_ERR_FORMAT;
        default:
            if (!(d[pos + 4] & 0x20))
                return MIME_PNG_ERR_UNSUPPORTED;
            break;
        }
        pos += (size_t)len + 12;
    }
}

bool mime_png_is_png(const void *data, size_t size)
{
    if (!data || size < 8)
        return false;
    const uint8_t *d = data;
    for (int i = 0; i < 8; i++)
        if (d[i] != png_signature[i])
            return false;
    return true;
}

bool mime_png_info(const void *data, size_t size, MimePngInfo *info)
{
    PngParse p;

    if (!data || !info || parse(data, size, false, true, &p))
        return false;
    *info = p.info;
    return true;
}

static inline uint8_t paeth(int a, int b, int c)
{
    int pa = b - c, pb = a - c, pc = pa + pb;
    pa = pa < 0 ? -pa : pa;
    pb = pb < 0 ? -pb : pb;
    pc = pc < 0 ? -pc : pc;
    if (pa <= pb && pa <= pc)
        return (uint8_t)a;
    return (uint8_t)(pb <= pc ? b : c);
}

static bool unfilter_row(uint8_t *cur, const uint8_t *prev, size_t n, size_t bpp, unsigned type)
{
    size_t i;

    switch (type) {
    case 0:
        break;
    case 1:
        for (i = bpp; i < n; i++)
            cur[i] = (uint8_t)(cur[i] + cur[i - bpp]);
        break;
    case 2:
        for (i = 0; i < n; i++)
            cur[i] = (uint8_t)(cur[i] + prev[i]);
        break;
    case 3:
        for (i = 0; i < bpp && i < n; i++)
            cur[i] = (uint8_t)(cur[i] + (prev[i] >> 1));
        for (; i < n; i++)
            cur[i] = (uint8_t)(cur[i] + ((cur[i - bpp] + prev[i]) >> 1));
        break;
    case 4:
        for (i = 0; i < bpp && i < n; i++)
            cur[i] = (uint8_t)(cur[i] + prev[i]);
        for (; i < n; i++)
            cur[i] = (uint8_t)(cur[i] + paeth(cur[i - bpp], prev[i], prev[i - bpp]));
        break;
    default:
        return false;
    }
    return true;
}

/* exact round of c * a / 255. */
static inline unsigned mul255(unsigned c, unsigned a)
{
    unsigned t = c * a + 128;
    return (t + (t >> 8)) >> 8;
}

/* exact round of v * 255 / 65535. */
static inline unsigned to8(unsigned v)
{
    return (v * 255 + 32895) >> 16;
}

static inline void put(uint8_t *d, unsigned r, unsigned g, unsigned b, unsigned a, bool pm)
{
    if (pm && a != 255) {
        r = mul255(r, a);
        g = mul255(g, a);
        b = mul255(b, a);
    }
    d[0] = (uint8_t)b;
    d[1] = (uint8_t)g;
    d[2] = (uint8_t)r;
    d[3] = (uint8_t)a;
}

typedef struct PngConvert {
    const PngParse *p;
    bool pm;
    bool indexed;
    uint8_t lut[256][4];
} PngConvert;

static void build_lut(PngConvert *cv)
{
    const PngParse *p = cv->p;

    for (unsigned i = 0; i < 256; i++) {
        unsigned r = 0, g = 0, b = 0, a = 255;
        if (p->info.color_type == MIME_PNG_COLOR_PALETTE) {
            if (i < p->pal_count) {
                r = p->pal[i][0];
                g = p->pal[i][1];
                b = p->pal[i][2];
                a = p->pal_alpha[i];
            }
        } else {
            unsigned maxv = (1u << p->info.bit_depth) - 1;
            if (i <= maxv) {
                r = g = b = i * 255 / maxv;
                if (p->has_trns && p->trns[0] == i)
                    a = 0;
            }
        }
        put(cv->lut[i], r, g, b, a, cv->pm);
    }
}

static void convert_row(const PngConvert *cv, const uint8_t *s, uint32_t n, uint8_t *d, size_t step)
{
    const PngParse *p = cv->p;
    unsigned depth = p->info.bit_depth;
    bool pm = cv->pm, trns = p->has_trns;
    uint32_t x;

    if (cv->indexed) {
        if (depth == 8) {
            for (x = 0; x < n; x++, d += step)
                __builtin_memcpy(d, cv->lut[s[x]], 4);
        } else {
            unsigned mask = (1u << depth) - 1, per_byte = 8 / depth;
            for (x = 0; x < n; x++, d += step) {
                unsigned shift = 8 - depth * (x % per_byte + 1);
                __builtin_memcpy(d, cv->lut[(s[x / per_byte] >> shift) & mask], 4);
            }
        }
        return;
    }

    switch (p->info.color_type) {
    case MIME_PNG_COLOR_GRAY: /* 16 bits only, lower depths are indexed. */
        for (x = 0; x < n; x++, s += 2, d += step) {
            unsigned v = be16(s), g = to8(v);
            put(d, g, g, g, trns && v == p->trns[0] ? 0 : 255, pm);
        }
        break;
    case MIME_PNG_COLOR_RGB:
        if (depth == 8) {
            for (x = 0; x < n; x++, s += 3, d += step) {
                unsigned a = trns && s[0] == p->trns[0] && s[1] == p->trns[1] && s[2] == p->trns[2] ? 0 : 255;
                put(d, s[0], s[1], s[2], a, pm);
            }
        } else {
            for (x = 0; x < n; x++, s += 6, d += step) {
                unsigned r = be16(s), g = be16(s + 2), b = be16(s + 4);
                unsigned a = trns && r == p->trns[0] && g == p->trns[1] && b == p->trns[2] ? 0 : 255;
                put(d, to8(r), to8(g), to8(b), a, pm);
            }
        }
        break;
    case MIME_PNG_COLOR_GRAY_ALPHA:
        if (depth == 8) {
            for (x = 0; x < n; x++, s += 2, d += step)
                put(d, s[0], s[0], s[0], s[1], pm);
        } else {
            for (x = 0; x < n; x++, s += 4, d += step) {
                unsigned g = to8(be16(s));
                put(d, g, g, g, to8(be16(s + 2)), pm);
            }
        }
        break;
    case MIME_PNG_COLOR_RGBA:
        if (depth == 8) {
            for (x = 0; x < n; x++, s += 4, d += step)
                put(d, s[0], s[1], s[2], s[3], pm);
        } else {
            for (x = 0; x < n; x++, s += 8, d += step)
                put(d, to8(be16(s)), to8(be16(s + 2)), to8(be16(s + 4)), to8(be16(s + 6)), pm);
        }
        break;
    }
}

int mime_png_decode(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags, uint8_t *out,
                    size_t out_stride)
{
    PngParse p;
    PngPass pass[7];
    unsigned npass;
    size_t total = 0, max_rowbytes = 0;
    int r;

    if (!data || !alloc || !out)
        return MIME_PNG_ERR_PARAM;
    r = parse(data, size, !(flags & MIME_PNG_SKIP_CRC), false, &p);
    if (r)
        return r;
    if (out_stride / 4 < p.info.width)
        return MIME_PNG_ERR_PARAM;

    /* sizes stay far below overflow with 16384 pixels per side, the checks guard 32 bit targets. */
    size_t bits_per_pixel = (size_t)p.channels * p.info.bit_depth;
    size_t bpp = bits_per_pixel >= 8 ? bits_per_pixel / 8 : 1;
    npass = p.info.interlaced ? 7 : 1;
    for (unsigned i = 0; i < npass; i++) {
        PngPass *ps = &pass[i];
        size_t bits, rowsize;
        if (p.info.interlaced) {
            ps->x0 = adam7[i][0];
            ps->y0 = adam7[i][1];
            ps->dx = adam7[i][2];
            ps->dy = adam7[i][3];
        } else {
            ps->x0 = ps->y0 = 0;
            ps->dx = ps->dy = 1;
        }
        ps->w = p.info.width > ps->x0 ? (p.info.width - ps->x0 + ps->dx - 1) / ps->dx : 0;
        ps->h = p.info.height > ps->y0 ? (p.info.height - ps->y0 + ps->dy - 1) / ps->dy : 0;
        if (!ps->w)
            ps->h = 0;
        if (ckd_mul(&bits, (size_t)ps->w, bits_per_pixel))
            return MIME_PNG_ERR_UNSUPPORTED;
        ps->rowbytes = bits / 8 + (bits % 8 != 0);
        if (ckd_mul(&rowsize, ps->rowbytes + 1, (size_t)ps->h) ||
            ckd_add(&total, total, rowsize))
            return MIME_PNG_ERR_UNSUPPORTED;
        if (ps->rowbytes > max_rowbytes)
            max_rowbytes = ps->rowbytes;
    }

    /* workspace: decoder state, the filtered image data and a zero row standing in above the first row. */
    size_t state_size = (sizeof(MimeInflate) + 15) & ~(size_t)15;
    size_t ws_size;
    if (ckd_add(&ws_size, state_size, total) ||
        ckd_add(&ws_size, ws_size, max_rowbytes))
        return MIME_PNG_ERR_UNSUPPORTED;
    uint8_t *ws = mime_alloc(alloc, ws_size);
    if (!ws)
        return MIME_PNG_ERR_NOMEM;
    uint8_t *raw = ws + state_size;
    uint8_t *zero_row = raw + total;
    __builtin_memset(zero_row, 0, max_rowbytes);

    const uint8_t *z;
    uint8_t *zbuf = nullptr;
    if (p.idat_count == 1) {
        z = p.first_idat + 8;
    } else {
        zbuf = mime_alloc(alloc, p.idat_total ? p.idat_total : 1);
        if (!zbuf) {
            mime_free(alloc, ws);
            return MIME_PNG_ERR_NOMEM;
        }
        const uint8_t *c = p.first_idat;
        size_t off = 0;
        for (unsigned i = 0; i < p.idat_count; i++) {
            uint32_t len = be32(c);
            __builtin_memcpy(zbuf + off, c + 8, len);
            off += len;
            c += (size_t)len + 12;
        }
        z = zbuf;
    }

    size_t got;
    unsigned zflags = MIME_INFLATE_ZLIB | ((flags & MIME_PNG_SKIP_ADLER) ? MIME_INFLATE_NO_CHECKSUM : 0);
    r = mime_inflate_with((MimeInflate *)ws, z, p.idat_total, raw, total, &got, zflags);
    mime_free(alloc, zbuf);
    /* extra data after a complete image is tolerated. */
    if (r == MIME_INFLATE_ERR_OUTPUT && got == total)
        r = MIME_INFLATE_OK;
    if (r || got != total) {
        mime_free(alloc, ws);
        return MIME_PNG_ERR_DATA;
    }

    PngConvert cv;
    cv.p = &p;
    cv.pm = (flags & MIME_PNG_PREMULTIPLIED) != 0;
    cv.indexed = p.info.color_type == MIME_PNG_COLOR_PALETTE ||
                 (p.info.color_type == MIME_PNG_COLOR_GRAY && p.info.bit_depth <= 8);
    if (cv.indexed)
        build_lut(&cv);

    uint8_t *row = raw;
    for (unsigned i = 0; i < npass; i++) {
        const PngPass *ps = &pass[i];
        const uint8_t *prev = zero_row;
        for (uint32_t y = 0; y < ps->h; y++) {
            uint8_t *cur = row + 1;
            if (!unfilter_row(cur, prev, ps->rowbytes, bpp, row[0])) {
                mime_free(alloc, ws);
                return MIME_PNG_ERR_DATA;
            }
            uint8_t *dst = out + (size_t)(ps->y0 + y * ps->dy) * out_stride + (size_t)ps->x0 * 4;
            convert_row(&cv, cur, ps->w, dst, (size_t)ps->dx * 4);
            prev = cur;
            row = cur + ps->rowbytes;
        }
    }

    mime_free(alloc, ws);
    return MIME_PNG_OK;
}

int mime_png_decode_alloc(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags, uint8_t **out,
                          MimePngInfo *info)
{
    MimePngInfo inf;
    size_t bytes;

    if (!out)
        return MIME_PNG_ERR_PARAM;
    *out = nullptr;
    if (!data || !alloc)
        return MIME_PNG_ERR_PARAM;
    PngParse p;
    int r = parse(data, size, false, true, &p);
    if (r)
        return r;
    inf = p.info;
    if (ckd_mul(&bytes, (size_t)inf.width * 4, (size_t)inf.height))
        return MIME_PNG_ERR_UNSUPPORTED;
    uint8_t *buf = mime_alloc(alloc, bytes);
    if (!buf)
        return MIME_PNG_ERR_NOMEM;
    r = mime_png_decode(data, size, alloc, flags, buf, (size_t)inf.width * 4);
    if (r) {
        mime_free(alloc, buf);
        return r;
    }
    *out = buf;
    if (info)
        *info = inf;
    return MIME_PNG_OK;
}
