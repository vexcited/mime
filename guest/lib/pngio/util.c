#include <stdckdint.h>

#include "pngio_internal.h"
#include "pngio_crc_tables.h"

bool pngio_simd = true;

const uint8_t pngio_adam7[7][4] = {
    {0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2},
};

void mime_pngio_set_simd(bool enable)
{
    pngio_simd = enable;
}

uint32_t mime_pngio_crc32(uint32_t crc, const void *data, size_t n)
{
    const uint8_t *p = data;
    uint32_t c = ~crc;

#if PNGIO_LE
    while (n >= 8) {
        uint32_t a, b;
        __builtin_memcpy(&a, p, 4);
        __builtin_memcpy(&b, p + 4, 4);
        a ^= c;
        c = crc_tables[7][a & 255] ^ crc_tables[6][(a >> 8) & 255] ^ crc_tables[5][(a >> 16) & 255] ^
            crc_tables[4][a >> 24] ^ crc_tables[3][b & 255] ^ crc_tables[2][(b >> 8) & 255] ^
            crc_tables[1][(b >> 16) & 255] ^ crc_tables[0][b >> 24];
        p += 8;
        n -= 8;
    }
#endif
    while (n--)
        c = crc_tables[0][(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

unsigned mime_pngio_bits_per_pixel(int format)
{
    switch (format) {
    case MIME_PNGIO_FMT_GRAY1:
    case MIME_PNGIO_FMT_INDEXED1:
        return 1;
    case MIME_PNGIO_FMT_GRAY2:
    case MIME_PNGIO_FMT_INDEXED2:
        return 2;
    case MIME_PNGIO_FMT_GRAY4:
    case MIME_PNGIO_FMT_INDEXED4:
        return 4;
    case MIME_PNGIO_FMT_GRAY8:
    case MIME_PNGIO_FMT_INDEXED8:
        return 8;
    case MIME_PNGIO_FMT_GRAY16:
    case MIME_PNGIO_FMT_GRAY_ALPHA16:
        return 16;
    case MIME_PNGIO_FMT_BGR24:
    case MIME_PNGIO_FMT_RGB24:
        return 24;
    case MIME_PNGIO_FMT_BGRA32:
    case MIME_PNGIO_FMT_BGRX32:
    case MIME_PNGIO_FMT_RGBA32:
    case MIME_PNGIO_FMT_GRAY_ALPHA32:
        return 32;
    case MIME_PNGIO_FMT_RGB48:
        return 48;
    case MIME_PNGIO_FMT_RGBA64:
        return 64;
    default:
        return 0;
    }
}

size_t mime_pngio_row_bytes(int format, uint32_t width)
{
    unsigned bits = mime_pngio_bits_per_pixel(format);
    size_t n;

    if (!bits || ckd_mul(&n, (size_t)width, (size_t)bits))
        return 0;
    return (n >> 3) + ((n & 7) != 0);
}

unsigned pngio_pass_layout(uint32_t width, uint32_t height, bool interlaced, unsigned bits_per_pixel,
                           PngioPass pass[7])
{
    unsigned npass = interlaced ? 7 : 1;

    for (unsigned i = 0; i < npass; i++) {
        PngioPass *ps = &pass[i];
        size_t bits;

        if (interlaced) {
            ps->x0 = pngio_adam7[i][0];
            ps->y0 = pngio_adam7[i][1];
            ps->dx = pngio_adam7[i][2];
            ps->dy = pngio_adam7[i][3];
        } else {
            ps->x0 = ps->y0 = 0;
            ps->dx = ps->dy = 1;
        }
        ps->w = width > ps->x0 ? (width - ps->x0 + ps->dx - 1) / ps->dx : 0;
        ps->h = height > ps->y0 ? (height - ps->y0 + ps->dy - 1) / ps->dy : 0;
        if (!ps->w)
            ps->h = 0;
        if (ckd_mul(&bits, (size_t)ps->w, (size_t)bits_per_pixel))
            return 0;
        ps->rowbytes = (bits >> 3) + ((bits & 7) != 0);
    }
    return npass;
}

bool mime_pngio_is_png(const void *data, size_t size)
{
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};

    return data && size >= 8 && __builtin_memcmp(data, sig, 8) == 0;
}
