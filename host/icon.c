#include "icon.h"

#include <stdlib.h>
#include <string.h>

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* ------------------------------------------------------------------ png */

static uint32_t g_crc_table[256];

static void crc_init(void)
{
    if (g_crc_table[1])
        return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crc_table[n] = c;
    }
}

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    c = ~c;
    for (size_t i = 0; i < n; i++)
        c = g_crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint8_t *chunk(uint8_t *out, const char *type, const uint8_t *data, size_t size)
{
    put_be32(out, (uint32_t)size);
    memcpy(out + 4, type, 4);
    if (size)
        memcpy(out + 8, data, size);
    put_be32(out + 8 + size, crc(crc(0, (const uint8_t *)type, 4), data, size));
    return out + 12 + size;
}

bool mime_png_encode(const uint8_t *rgba, uint32_t width, uint32_t height, void **png, size_t *png_size)
{
    if (!width || !height || width > 4096 || height > 4096)
        return false;
    crc_init();
    size_t row = 1 + (size_t)width * 4, raw = row * height;
    size_t blocks = (raw + 65534) / 65535;
    size_t z_size = 2 + raw + blocks * 5 + 4;
    uint8_t *z = malloc(z_size), *scan = malloc(raw);
    if (!z || !scan) {
        free(z);
        free(scan);
        return false;
    }
    for (uint32_t y = 0; y < height; y++) {
        scan[y * row] = 0;
        memcpy(scan + y * row + 1, rgba + (size_t)y * width * 4, (size_t)width * 4);
    }
    size_t n = 0;
    z[n++] = 0x78;
    z[n++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw; i++) {
        a = (a + scan[i]) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t at = 0; at < raw;) {
        size_t len = raw - at > 65535 ? 65535 : raw - at;
        z[n++] = at + len == raw;
        z[n++] = (uint8_t)len;
        z[n++] = (uint8_t)(len >> 8);
        z[n++] = (uint8_t)~len;
        z[n++] = (uint8_t)(~len >> 8);
        memcpy(z + n, scan + at, len);
        n += len;
        at += len;
    }
    put_be32(z + n, b << 16 | a);
    n += 4;
    free(scan);
    size_t total = 8 + 12 + 13 + 12 + n + 12;
    uint8_t *out = malloc(total);
    if (!out) {
        free(z);
        return false;
    }
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    memcpy(out, signature, 8);
    uint8_t header[13];
    put_be32(header, width);
    put_be32(header + 4, height);
    header[8] = 8;
    header[9] = 6;
    header[10] = header[11] = header[12] = 0;
    uint8_t *p = chunk(out + 8, "IHDR", header, 13);
    p = chunk(p, "IDAT", z, n);
    p = chunk(p, "IEND", nullptr, 0);
    free(z);
    *png = out;
    *png_size = (size_t)(p - out);
    return true;
}

/* ------------------------------------------------------------------ ico */

/* the picture of a bitmap in an icon: the pixels of the colors and the mask below them, upright, as rgba. */
static bool dib_to_rgba(const uint8_t *d, size_t size, uint32_t side_w, uint32_t side_h, uint8_t **rgba)
{
    if (size < 40 || u32(d) < 40)
        return false;
    uint32_t header = u32(d);
    int32_t w = (int32_t)u32(d + 4), h2 = (int32_t)u32(d + 8);
    uint16_t bits = u16(d + 14);
    uint32_t compression = u32(d + 16), colors = u32(d + 32);
    if (compression != 0 || w <= 0 || w > 1024 || h2 <= 0 || h2 > 2048)
        return false;
    uint32_t h = (uint32_t)h2 / 2;
    if (!h || (uint32_t)w != side_w || (side_h && h != side_h))
        return false;
    if (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32)
        return false;
    size_t palette_entries = bits <= 8 ? (colors ? colors : (size_t)1 << bits) : 0;
    size_t palette_at = header, pixels_at = palette_at + palette_entries * 4;
    size_t stride = (((size_t)w * bits + 31) / 32) * 4, mask_stride = (((size_t)w + 31) / 32) * 4;
    size_t mask_at = pixels_at + stride * h;
    if (pixels_at > size || mask_at > size)
        return false;
    bool has_mask = mask_at + mask_stride * h <= size;
    uint8_t *out = calloc((size_t)w * h, 4);
    if (!out)
        return false;
    bool any_alpha = false;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src = d + pixels_at + stride * (h - 1 - y);
        for (uint32_t x = 0; x < (uint32_t)w; x++) {
            uint8_t r, g, b, a = 255;
            if (bits == 32) {
                b = src[x * 4];
                g = src[x * 4 + 1];
                r = src[x * 4 + 2];
                a = src[x * 4 + 3];
                any_alpha |= a != 0;
            } else if (bits == 24) {
                b = src[x * 3];
                g = src[x * 3 + 1];
                r = src[x * 3 + 2];
            } else {
                uint32_t index = bits == 8 ? src[x] : bits == 4 ? (src[x / 2] >> (x & 1 ? 0 : 4)) & 15
                                                               : (src[x / 8] >> (7 - (x & 7))) & 1;
                if (index >= palette_entries)
                    index = 0;
                const uint8_t *e = d + palette_at + index * 4;
                b = e[0];
                g = e[1];
                r = e[2];
            }
            uint8_t *o = out + ((size_t)y * w + x) * 4;
            o[0] = r;
            o[1] = g;
            o[2] = b;
            o[3] = a;
        }
    }
    /* the mask makes pixels clear, unless the colors carry an alpha of their own. */
    if (has_mask && !(bits == 32 && any_alpha)) {
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t *m = d + mask_at + mask_stride * (h - 1 - y);
            for (uint32_t x = 0; x < (uint32_t)w; x++)
                out[((size_t)y * w + x) * 4 + 3] = (m[x / 8] >> (7 - (x & 7))) & 1 ? 0 : 255;
        }
    }
    *rgba = out;
    return true;
}

bool mime_ico_to_png(const void *ico, size_t size, void **png, size_t *png_size, uint32_t *side)
{
    const uint8_t *d = ico;
    if (size < 6 || u16(d) != 0 || u16(d + 2) != 1)
        return false;
    unsigned count = u16(d + 4);
    if (size < 6 + (size_t)count * 16)
        return false;
    /* the largest picture is tried first, then the smaller ones when it cannot be read. */
    bool tried[256] = {};
    for (unsigned round = 0; round < count && round < 256; round++) {
        int best = -1;
        uint32_t best_side = 0;
        for (unsigned i = 0; i < count && i < 256; i++) {
            const uint8_t *e = d + 6 + i * 16;
            uint32_t w = e[0] ? e[0] : 256;
            if (!tried[i] && w >= best_side) {
                best = (int)i;
                best_side = w;
            }
        }
        if (best < 0)
            break;
        tried[best] = true;
        const uint8_t *e = d + 6 + best * 16;
        uint32_t bytes = u32(e + 8), offset = u32(e + 12);
        if ((size_t)offset + bytes > size || bytes < 8)
            continue;
        const uint8_t *image = d + offset;
        if (!memcmp(image, "\x89PNG\r\n\x1a\n", 8)) {
            *png = malloc(bytes);
            if (!*png)
                return false;
            memcpy(*png, image, bytes);
            *png_size = bytes;
            *side = best_side;
            return true;
        }
        uint8_t *rgba;
        if (!dib_to_rgba(image, bytes, e[0] ? e[0] : 256, e[1] ? e[1] : 256, &rgba))
            continue;
        bool ok = mime_png_encode(rgba, e[0] ? e[0] : 256, e[1] ? e[1] : 256, png, png_size);
        free(rgba);
        if (ok) {
            *side = best_side;
            return true;
        }
    }
    return false;
}
