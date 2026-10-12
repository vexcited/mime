/*
 * pixel transfer: rows between the application's format and type and the
 * metal format a texture is stored in. common layouts are copied or
 * swizzled directly; anything else goes through floats.
 */
#include "opengl32.h"

enum {
    MTL_A8 = 1,
    MTL_R8 = 10,
    MTL_R16 = 20,
    MTL_R16F = 25,
    MTL_RG8 = 30,
    MTL_R32UI = 53,
    MTL_R32F = 55,
    MTL_RG16 = 60,
    MTL_RG16F = 65,
    MTL_RGBA8 = 70,
    MTL_RGBA8_SRGB = 71,
    MTL_BGRA8 = 80,
    MTL_BGRA8_SRGB = 81,
    MTL_RGB10A2 = 90,
    MTL_RG32F = 105,
    MTL_RGBA16 = 110,
    MTL_RGBA16F = 115,
    MTL_RGBA32F = 125,
    MTL_D16 = 250,
    MTL_D32F = 252,
    MTL_S8 = 253,
    MTL_D32F_S8 = 260,
};

static float half_to_float(uint16_t h)
{
    uint32_t s = (uint32_t)(h & 0x8000) << 16, e = (h >> 10) & 0x1F, m = h & 0x3FF, bits;
    if (e == 0) {
        float f = (float)m * (1.0f / 16777216.0f);
        return s ? -f : f;
    }
    bits = e == 31 ? s | 0x7F800000 | (m << 13) : s | ((e + 112) << 23) | (m << 13);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static uint16_t float_to_half(float f)
{
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t s = (x >> 16) & 0x8000;
    int32_t e = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t m = x & 0x7FFFFF;
    if (((x >> 23) & 0xFF) == 0xFF)
        return (uint16_t)(s | 0x7C00 | (m ? 0x200 : 0));
    if (e >= 31)
        return (uint16_t)(s | 0x7C00);
    if (e <= 0) {
        if (e < -10)
            return (uint16_t)s;
        m |= 0x800000;
        return (uint16_t)(s | (m >> (14 - e)));
    }
    return (uint16_t)(s | ((uint32_t)e << 10) | (m >> 13));
}

uint32_t gl_format_bytes(uint32_t f)
{
    switch (f) {
    case MTL_A8:
    case MTL_R8:
    case MTL_S8:
        return 1;
    case MTL_R16:
    case MTL_R16F:
    case MTL_RG8:
    case MTL_D16:
        return 2;
    case MTL_RGBA16:
    case MTL_RGBA16F:
    case MTL_RG32F:
    case MTL_D32F_S8:
        return 8;
    case MTL_RGBA32F:
        return 16;
    default:
        return 4;
    }
}

BOOL gl_format_is_depth(uint32_t f)
{
    return f == MTL_D16 || f == MTL_D32F || f == MTL_D32F_S8 || f == MTL_S8;
}

BOOL gl_format_has_stencil(uint32_t f)
{
    return f == MTL_S8 || f == MTL_D32F_S8;
}

/* components of the client format, 0 if it is not a color format. */
static int components(GLenum format)
{
    switch (format) {
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_RED_INTEGER:
    case GL_DEPTH_COMPONENT:
    case GL_STENCIL_INDEX:
        return 1;
    case GL_RG:
    case GL_LUMINANCE_ALPHA:
    case GL_RG_INTEGER:
    case GL_DEPTH_STENCIL:
        return 2;
    case GL_RGB:
    case GL_BGR:
    case GL_RGB_INTEGER:
        return 3;
    case GL_RGBA:
    case GL_BGRA:
    case GL_RGBA_INTEGER:
        return 4;
    default:
        return 0;
    }
}

uint32_t gl_client_pixel_bytes(GLenum format, GLenum type)
{
    switch (type) {
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_6_5_REV:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
        return 2;
    case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_10_10_10_2:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_24_8:
        return 4;
    case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
        return 8;
    default:
        break;
    }
    int n = components(format);
    switch (type) {
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
        return (uint32_t)n;
    case GL_UNSIGNED_SHORT:
    case GL_SHORT:
    case GL_HALF_FLOAT:
        return (uint32_t)n * 2;
    default:
        return (uint32_t)n * 4;
    }
}

/* ---------------------------------------------------------------- decode */

static float unorm(uint32_t v, int bits)
{
    return (float)v / (float)((1u << bits) - 1);
}

/* one client pixel as rgba floats; depth lands in red. */
static void decode(GLenum format, GLenum type, const uint8_t *p, float out[4])
{
    float c[4] = {0, 0, 0, 1};
    switch (type) {
    case GL_UNSIGNED_SHORT_5_6_5: {
        uint16_t v = *(const uint16_t *)p;
        c[0] = unorm(v >> 11, 5);
        c[1] = unorm((v >> 5) & 63, 6);
        c[2] = unorm(v & 31, 5);
        goto order;
    }
    case GL_UNSIGNED_SHORT_4_4_4_4: {
        uint16_t v = *(const uint16_t *)p;
        c[0] = unorm(v >> 12, 4);
        c[1] = unorm((v >> 8) & 15, 4);
        c[2] = unorm((v >> 4) & 15, 4);
        c[3] = unorm(v & 15, 4);
        goto order;
    }
    case GL_UNSIGNED_SHORT_4_4_4_4_REV: {
        uint16_t v = *(const uint16_t *)p;
        c[0] = unorm(v & 15, 4);
        c[1] = unorm((v >> 4) & 15, 4);
        c[2] = unorm((v >> 8) & 15, 4);
        c[3] = unorm(v >> 12, 4);
        goto order;
    }
    case GL_UNSIGNED_SHORT_5_5_5_1: {
        uint16_t v = *(const uint16_t *)p;
        c[0] = unorm(v >> 11, 5);
        c[1] = unorm((v >> 6) & 31, 5);
        c[2] = unorm((v >> 1) & 31, 5);
        c[3] = (float)(v & 1);
        goto order;
    }
    case GL_UNSIGNED_SHORT_1_5_5_5_REV: {
        uint16_t v = *(const uint16_t *)p;
        c[0] = unorm(v & 31, 5);
        c[1] = unorm((v >> 5) & 31, 5);
        c[2] = unorm((v >> 10) & 31, 5);
        c[3] = (float)(v >> 15);
        goto order;
    }
    case GL_UNSIGNED_INT_8_8_8_8: {
        uint32_t v = *(const uint32_t *)p;
        c[0] = unorm(v >> 24, 8);
        c[1] = unorm((v >> 16) & 255, 8);
        c[2] = unorm((v >> 8) & 255, 8);
        c[3] = unorm(v & 255, 8);
        goto order;
    }
    case GL_UNSIGNED_INT_8_8_8_8_REV: {
        uint32_t v = *(const uint32_t *)p;
        c[0] = unorm(v & 255, 8);
        c[1] = unorm((v >> 8) & 255, 8);
        c[2] = unorm((v >> 16) & 255, 8);
        c[3] = unorm(v >> 24, 8);
        goto order;
    }
    case GL_UNSIGNED_INT_2_10_10_10_REV: {
        uint32_t v = *(const uint32_t *)p;
        c[0] = unorm(v & 1023, 10);
        c[1] = unorm((v >> 10) & 1023, 10);
        c[2] = unorm((v >> 20) & 1023, 10);
        c[3] = unorm(v >> 30, 2);
        goto order;
    }
    case GL_UNSIGNED_INT_24_8: {
        uint32_t v = *(const uint32_t *)p;
        c[0] = unorm(v >> 8, 24);
        c[1] = (float)(v & 255);
        break;
    }
    default: {
        int n = components(format);
        float v[4] = {0, 0, 0, 1};
        for (int i = 0; i < n; i++) {
            switch (type) {
            case GL_UNSIGNED_BYTE:
                v[i] = p[i] / 255.0f;
                break;
            case GL_BYTE:
                v[i] = ((const int8_t *)p)[i] / 127.0f;
                break;
            case GL_UNSIGNED_SHORT:
                v[i] = ((const uint16_t *)p)[i] / 65535.0f;
                break;
            case GL_SHORT:
                v[i] = ((const int16_t *)p)[i] / 32767.0f;
                break;
            case GL_UNSIGNED_INT:
                v[i] = (float)(((const uint32_t *)p)[i] / 4294967295.0);
                break;
            case GL_INT:
                v[i] = (float)(((const int32_t *)p)[i] / 2147483647.0);
                break;
            case GL_HALF_FLOAT:
                v[i] = half_to_float(((const uint16_t *)p)[i]);
                break;
            default:
                v[i] = ((const float *)p)[i];
                break;
            }
        }
        switch (format) {
        case GL_ALPHA:
            c[3] = v[0];
            break;
        case GL_LUMINANCE:
            c[0] = c[1] = c[2] = v[0];
            break;
        case GL_LUMINANCE_ALPHA:
            c[0] = c[1] = c[2] = v[0];
            c[3] = v[1];
            break;
        case GL_GREEN:
            c[1] = v[0];
            break;
        case GL_BLUE:
            c[2] = v[0];
            break;
        case GL_BGR:
        case GL_BGRA:
            c[0] = v[2];
            c[1] = v[1];
            c[2] = v[0];
            c[3] = n == 4 ? v[3] : 1;
            break;
        default:
            for (int i = 0; i < n; i++)
                c[i] = v[i];
            break;
        }
        break;
    }
    }
    memcpy(out, c, sizeof(c));
    return;
order:
    if (format == GL_BGRA || format == GL_BGR) {
        float t = c[0];
        c[0] = c[2];
        c[2] = t;
    }
    memcpy(out, c, sizeof(c));
}

static uint8_t to8(float v)
{
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    return (uint8_t)(v * 255.0f + 0.5f);
}

static uint16_t to16(float v)
{
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    return (uint16_t)(v * 65535.0f + 0.5f);
}

/* one texel of the metal format from rgba floats. the source format decides how luminance and alpha land. */
static void encode_texel(uint32_t f, GLenum src_format, const float c[4], uint8_t *d)
{
    float r = c[0];
    if (src_format == GL_ALPHA)
        r = c[3];
    switch (f) {
    case MTL_A8:
        d[0] = to8(c[3]);
        break;
    case MTL_R8:
        d[0] = to8(r);
        break;
    case MTL_RG8:
        d[0] = to8(r);
        d[1] = to8(src_format == GL_LUMINANCE_ALPHA ? c[3] : c[1]);
        break;
    case MTL_R16:
        *(uint16_t *)d = to16(r);
        break;
    case MTL_RG16:
        ((uint16_t *)d)[0] = to16(c[0]);
        ((uint16_t *)d)[1] = to16(c[1]);
        break;
    case MTL_R16F:
        *(uint16_t *)d = float_to_half(r);
        break;
    case MTL_RG16F:
        ((uint16_t *)d)[0] = float_to_half(c[0]);
        ((uint16_t *)d)[1] = float_to_half(c[1]);
        break;
    case MTL_R32F:
    case MTL_D32F:
        *(float *)d = r;
        break;
    case MTL_RG32F:
        ((float *)d)[0] = c[0];
        ((float *)d)[1] = c[1];
        break;
    case MTL_RGBA16F:
        for (int i = 0; i < 4; i++)
            ((uint16_t *)d)[i] = float_to_half(c[i]);
        break;
    case MTL_RGBA16:
        for (int i = 0; i < 4; i++)
            ((uint16_t *)d)[i] = to16(c[i]);
        break;
    case MTL_RGBA32F:
        memcpy(d, c, 16);
        break;
    case MTL_RGB10A2: {
        uint32_t v = (uint32_t)(c[0] * 1023 + 0.5f) | (uint32_t)(c[1] * 1023 + 0.5f) << 10 |
                     (uint32_t)(c[2] * 1023 + 0.5f) << 20 | (uint32_t)(c[3] * 3 + 0.5f) << 30;
        *(uint32_t *)d = v;
        break;
    }
    case MTL_BGRA8:
    case MTL_BGRA8_SRGB:
        d[0] = to8(c[2]);
        d[1] = to8(c[1]);
        d[2] = to8(c[0]);
        d[3] = to8(c[3]);
        break;
    case MTL_D16:
        *(uint16_t *)d = to16(r);
        break;
    default:
        d[0] = to8(c[0]);
        d[1] = to8(c[1]);
        d[2] = to8(c[2]);
        d[3] = to8(c[3]);
        break;
    }
}

static void decode_texel(uint32_t f, const uint8_t *s, float c[4])
{
    c[0] = c[1] = c[2] = 0;
    c[3] = 1;
    switch (f) {
    case MTL_A8:
        c[3] = s[0] / 255.0f;
        break;
    case MTL_R8:
        c[0] = s[0] / 255.0f;
        break;
    case MTL_RG8:
        c[0] = s[0] / 255.0f;
        c[1] = s[1] / 255.0f;
        break;
    case MTL_R16F:
        c[0] = half_to_float(*(const uint16_t *)s);
        break;
    case MTL_R32F:
    case MTL_D32F:
    case MTL_D32F_S8:
        c[0] = *(const float *)s;
        break;
    case MTL_RG32F:
        c[0] = ((const float *)s)[0];
        c[1] = ((const float *)s)[1];
        break;
    case MTL_RGBA16F:
        for (int i = 0; i < 4; i++)
            c[i] = half_to_float(((const uint16_t *)s)[i]);
        break;
    case MTL_RGBA32F:
        memcpy(c, s, 16);
        break;
    case MTL_BGRA8:
    case MTL_BGRA8_SRGB:
        c[0] = s[2] / 255.0f;
        c[1] = s[1] / 255.0f;
        c[2] = s[0] / 255.0f;
        c[3] = s[3] / 255.0f;
        break;
    default:
        for (int i = 0; i < 4; i++)
            c[i] = s[i] / 255.0f;
        break;
    }
}

/* writes rgba floats as one client pixel. */
static void encode_client(GLenum format, GLenum type, const float c[4], uint8_t *p)
{
    float v[4];
    int n = components(format);
    switch (format) {
    case GL_ALPHA:
        v[0] = c[3];
        break;
    case GL_LUMINANCE:
        v[0] = c[0];
        break;
    case GL_LUMINANCE_ALPHA:
        v[0] = c[0];
        v[1] = c[3];
        break;
    case GL_GREEN:
        v[0] = c[1];
        break;
    case GL_BLUE:
        v[0] = c[2];
        break;
    case GL_BGR:
    case GL_BGRA:
        v[0] = c[2];
        v[1] = c[1];
        v[2] = c[0];
        v[3] = c[3];
        break;
    default:
        memcpy(v, c, sizeof(v));
        break;
    }
    if (type == GL_UNSIGNED_INT_8_8_8_8_REV) {
        *(uint32_t *)p = to8(v[0]) | (uint32_t)to8(v[1]) << 8 | (uint32_t)to8(v[2]) << 16 | (uint32_t)to8(v[3]) << 24;
        return;
    }
    if (type == GL_UNSIGNED_INT_8_8_8_8) {
        *(uint32_t *)p = to8(v[3]) | (uint32_t)to8(v[2]) << 8 | (uint32_t)to8(v[1]) << 16 | (uint32_t)to8(v[0]) << 24;
        return;
    }
    if (type == GL_UNSIGNED_SHORT_5_6_5) {
        *(uint16_t *)p = (uint16_t)((uint32_t)(v[0] * 31 + 0.5f) << 11 | (uint32_t)(v[1] * 63 + 0.5f) << 5 |
                                    (uint32_t)(v[2] * 31 + 0.5f));
        return;
    }
    for (int i = 0; i < n; i++) {
        switch (type) {
        case GL_UNSIGNED_BYTE:
            p[i] = to8(v[i]);
            break;
        case GL_BYTE:
            ((int8_t *)p)[i] = (int8_t)(v[i] * 127.0f);
            break;
        case GL_UNSIGNED_SHORT:
            ((uint16_t *)p)[i] = to16(v[i]);
            break;
        case GL_SHORT:
            ((int16_t *)p)[i] = (int16_t)(v[i] * 32767.0f);
            break;
        case GL_UNSIGNED_INT:
            ((uint32_t *)p)[i] = (uint32_t)((double)(v[i] < 0 ? 0 : v[i] > 1 ? 1 : v[i]) * 4294967295.0);
            break;
        case GL_INT:
            ((int32_t *)p)[i] = (int32_t)((double)v[i] * 2147483647.0);
            break;
        case GL_HALF_FLOAT:
            ((uint16_t *)p)[i] = float_to_half(v[i]);
            break;
        default:
            ((float *)p)[i] = v[i];
            break;
        }
    }
}

/* ---------------------------------------------------------------- rows */

void gl_unpack_row(uint32_t dst_format, uint8_t *dst, GLenum format, GLenum type, const uint8_t *src, uint32_t width)
{
    if (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_8_8_8_8_REV) {
        BOOL rgba = format == GL_RGBA && (dst_format == MTL_RGBA8 || dst_format == MTL_RGBA8_SRGB);
        BOOL bgra = format == GL_BGRA && (dst_format == MTL_BGRA8 || dst_format == MTL_BGRA8_SRGB);
        if (rgba || bgra) {
            memcpy(dst, src, (SIZE_T)width * 4);
            return;
        }
        BOOL swap = (format == GL_BGRA && (dst_format == MTL_RGBA8 || dst_format == MTL_RGBA8_SRGB)) ||
                    (format == GL_RGBA && (dst_format == MTL_BGRA8 || dst_format == MTL_BGRA8_SRGB));
        if (swap) {
            for (uint32_t x = 0; x < width; x++, src += 4, dst += 4) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
            }
            return;
        }
        if ((format == GL_RGB || format == GL_BGR) && dst_format == MTL_RGBA8) {
            int r = format == GL_RGB ? 0 : 2, b = 2 - r;
            for (uint32_t x = 0; x < width; x++, src += 3, dst += 4) {
                dst[0] = src[r];
                dst[1] = src[1];
                dst[2] = src[b];
                dst[3] = 255;
            }
            return;
        }
        if (dst_format == MTL_R8 && components(format) == 1) {
            memcpy(dst, src, width);
            return;
        }
        if (dst_format == MTL_RG8 && components(format) == 2) {
            memcpy(dst, src, (SIZE_T)width * 2);
            return;
        }
    }
    if (type == GL_FLOAT && dst_format == MTL_RGBA32F && format == GL_RGBA) {
        memcpy(dst, src, (SIZE_T)width * 16);
        return;
    }
    if (type == GL_HALF_FLOAT && dst_format == MTL_RGBA16F && format == GL_RGBA) {
        memcpy(dst, src, (SIZE_T)width * 8);
        return;
    }
    uint32_t sb = gl_client_pixel_bytes(format, type), db = gl_format_bytes(dst_format);
    for (uint32_t x = 0; x < width; x++, src += sb, dst += db) {
        float c[4];
        decode(format, type, src, c);
        encode_texel(dst_format, format, c, dst);
    }
}

void gl_pack_row(uint32_t src_format, const uint8_t *src, GLenum format, GLenum type, uint8_t *dst, uint32_t width)
{
    if (type == GL_UNSIGNED_BYTE) {
        if ((format == GL_RGBA && src_format == MTL_RGBA8) || (format == GL_BGRA && src_format == MTL_BGRA8)) {
            memcpy(dst, src, (SIZE_T)width * 4);
            return;
        }
        if ((format == GL_RGBA && src_format == MTL_BGRA8) || (format == GL_BGRA && src_format == MTL_RGBA8)) {
            for (uint32_t x = 0; x < width; x++, src += 4, dst += 4) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
            }
            return;
        }
    }
    uint32_t sb = gl_format_bytes(src_format), db = gl_client_pixel_bytes(format, type);
    for (uint32_t x = 0; x < width; x++, src += sb, dst += db) {
        float c[4];
        decode_texel(src_format, src, c);
        encode_client(format, type, c, dst);
    }
}
