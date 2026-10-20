#include "bcn.h"

#define DDS_MAGIC 0x20534444u

#define DDSD_MIPMAPCOUNT 0x20000u
#define DDSD_DEPTH 0x800000u

#define DDPF_ALPHAPIXELS 0x1u
#define DDPF_ALPHA 0x2u
#define DDPF_FOURCC 0x4u
#define DDPF_RGB 0x40u
#define DDPF_YUV 0x200u
#define DDPF_LUMINANCE 0x20000u

#define DDSCAPS2_CUBEMAP 0x200u
#define DDSCAPS2_VOLUME 0x200000u

#define FOURCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define FOURCC_DX10 FOURCC('D', 'X', '1', '0')

#define MAX_TOTAL ((uint64_t)1 << 48)

static inline uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    __builtin_memcpy(&v, p, 4);
#else
    v = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
#endif
    return v;
}

static inline void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

bool mime_dds_is_dds(const void *data, size_t size)
{
    return size >= 4 && rd32(data) == DDS_MAGIC;
}

uint32_t mime_dds_mip_size(uint32_t base, unsigned mip)
{
    uint32_t v = mip >= 32 ? 0 : base >> mip;
    return v ? v : 1;
}

/* format tables. */

static void set_bitfield(MimeDdsInfo *info, unsigned bpp, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    info->format = MIME_DDS_FMT_BITFIELD;
    info->bits_per_pixel = bpp;
    info->mask[0] = r;
    info->mask[1] = g;
    info->mask[2] = b;
    info->mask[3] = a;
    info->has_alpha = a != 0;
}

static void set_block(MimeDdsInfo *info, int fmt, unsigned bytes)
{
    info->format = fmt;
    info->block_bytes = bytes;
    info->bits_per_pixel = 0;
}

static void set_channels(MimeDdsInfo *info, int fmt, unsigned channels, unsigned bits_per_channel)
{
    info->format = fmt;
    info->channels = channels;
    info->bits_per_pixel = channels * bits_per_channel;
    info->has_alpha = channels == 4;
}

/* returns false for a dxgi format that is not handled. */
static bool dxgi_format(MimeDdsInfo *info, uint32_t f)
{
    info->dxgi_format = f;
    switch (f) {
    case 2: /* R32G32B32A32_FLOAT. */
        set_channels(info, MIME_DDS_FMT_F32, 4, 32);
        return true;
    case 10: /* R16G16B16A16_FLOAT. */
        set_channels(info, MIME_DDS_FMT_F16, 4, 16);
        return true;
    case 11: /* R16G16B16A16_UNORM. */
        set_channels(info, MIME_DDS_FMT_RGBA16, 4, 16);
        return true;
    case 24: /* R10G10B10A2_UNORM. */
        set_bitfield(info, 32, 0x3ff, 0xffc00, 0x3ff00000, 0xc0000000);
        return true;
    case 27: /* R8G8B8A8_TYPELESS. */
    case 28:
    case 29:
        info->srgb = f == 29;
        set_bitfield(info, 32, 0xff, 0xff00, 0xff0000, 0xff000000);
        return true;
    case 35: /* R16G16_UNORM. */
        set_bitfield(info, 32, 0xffff, 0xffff0000, 0, 0);
        return true;
    case 49: /* R8G8_UNORM. */
        set_bitfield(info, 16, 0xff, 0xff00, 0, 0);
        return true;
    case 56: /* R16_UNORM. */
        set_bitfield(info, 16, 0xffff, 0, 0, 0);
        info->luminance = true;
        return true;
    case 61: /* R8_UNORM. */
        set_bitfield(info, 8, 0xff, 0, 0, 0);
        info->luminance = true;
        return true;
    case 65: /* A8_UNORM. */
        set_bitfield(info, 8, 0, 0, 0, 0xff);
        return true;
    case 70: /* BC1. */
    case 71:
    case 72:
        info->srgb = f == 72;
        set_block(info, MIME_DDS_FMT_BC1, 8);
        return true;
    case 73:
    case 74:
    case 75:
        info->srgb = f == 75;
        set_block(info, MIME_DDS_FMT_BC2, 16);
        return true;
    case 76:
    case 77:
    case 78:
        info->srgb = f == 78;
        set_block(info, MIME_DDS_FMT_BC3, 16);
        return true;
    case 79:
    case 80:
        set_block(info, MIME_DDS_FMT_BC4U, 8);
        return true;
    case 81:
        set_block(info, MIME_DDS_FMT_BC4S, 8);
        return true;
    case 82:
    case 83:
        set_block(info, MIME_DDS_FMT_BC5U, 16);
        return true;
    case 84:
        set_block(info, MIME_DDS_FMT_BC5S, 16);
        return true;
    case 85: /* B5G6R5_UNORM. */
        set_bitfield(info, 16, 0xf800, 0x7e0, 0x1f, 0);
        return true;
    case 86: /* B5G5R5A1_UNORM. */
        set_bitfield(info, 16, 0x7c00, 0x3e0, 0x1f, 0x8000);
        return true;
    case 87: /* B8G8R8A8. */
    case 90:
    case 91:
        info->srgb = f == 91;
        set_bitfield(info, 32, 0xff0000, 0xff00, 0xff, 0xff000000);
        return true;
    case 88: /* B8G8R8X8. */
    case 92:
    case 93:
        info->srgb = f == 93;
        set_bitfield(info, 32, 0xff0000, 0xff00, 0xff, 0);
        return true;
    case 94: /* BC6H. */
    case 95:
        set_block(info, MIME_DDS_FMT_BC6H_UF16, 16);
        return true;
    case 96:
        set_block(info, MIME_DDS_FMT_BC6H_SF16, 16);
        return true;
    case 97: /* BC7. */
    case 98:
    case 99:
        info->srgb = f == 99;
        set_block(info, MIME_DDS_FMT_BC7, 16);
        return true;
    case 115: /* B4G4R4A4_UNORM. */
        set_bitfield(info, 16, 0xf00, 0xf0, 0xf, 0xf000);
        return true;
    default:
        return false;
    }
}

/* the numeric d3d9 formats stored as a four cc. */
static bool d3d9_format(MimeDdsInfo *info, uint32_t f)
{
    switch (f) {
    case 36: /* A16B16G16R16. */
        set_channels(info, MIME_DDS_FMT_RGBA16, 4, 16);
        return true;
    case 111: /* R16F. */
        set_channels(info, MIME_DDS_FMT_F16, 1, 16);
        return true;
    case 112: /* G16R16F. */
        set_channels(info, MIME_DDS_FMT_F16, 2, 16);
        return true;
    case 113: /* A16B16G16R16F. */
        set_channels(info, MIME_DDS_FMT_F16, 4, 16);
        return true;
    case 114: /* R32F. */
        set_channels(info, MIME_DDS_FMT_F32, 1, 32);
        return true;
    case 115: /* G32R32F. */
        set_channels(info, MIME_DDS_FMT_F32, 2, 32);
        return true;
    case 116: /* A32B32G32R32F. */
        set_channels(info, MIME_DDS_FMT_F32, 4, 32);
        return true;
    default:
        return false;
    }
}

static bool legacy_format(MimeDdsInfo *info, uint32_t pf_flags, uint32_t fourcc, uint32_t bpp, const uint32_t mask[4])
{
    if (pf_flags & DDPF_FOURCC) {
        info->fourcc = fourcc;
        switch (fourcc) {
        case FOURCC('D', 'X', 'T', '1'):
            set_block(info, MIME_DDS_FMT_BC1, 8);
            return true;
        case FOURCC('D', 'X', 'T', '2'):
            info->premultiplied = true;
            set_block(info, MIME_DDS_FMT_BC2, 16);
            return true;
        case FOURCC('D', 'X', 'T', '3'):
            set_block(info, MIME_DDS_FMT_BC2, 16);
            return true;
        case FOURCC('D', 'X', 'T', '4'):
            info->premultiplied = true;
            set_block(info, MIME_DDS_FMT_BC3, 16);
            return true;
        case FOURCC('D', 'X', 'T', '5'):
            set_block(info, MIME_DDS_FMT_BC3, 16);
            return true;
        case FOURCC('A', 'T', 'I', '1'):
        case FOURCC('B', 'C', '4', 'U'):
            set_block(info, MIME_DDS_FMT_BC4U, 8);
            return true;
        case FOURCC('B', 'C', '4', 'S'):
            set_block(info, MIME_DDS_FMT_BC4S, 8);
            return true;
        case FOURCC('A', 'T', 'I', '2'):
        case FOURCC('B', 'C', '5', 'U'):
            set_block(info, MIME_DDS_FMT_BC5U, 16);
            return true;
        case FOURCC('B', 'C', '5', 'S'):
            set_block(info, MIME_DDS_FMT_BC5S, 16);
            return true;
        default:
            return d3d9_format(info, fourcc);
        }
    }
    if (pf_flags & (DDPF_RGB | DDPF_LUMINANCE | DDPF_ALPHA)) {
        if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
            return false;
        uint32_t a = (pf_flags & (DDPF_ALPHAPIXELS | DDPF_ALPHA)) ? mask[3] : 0;
        uint32_t r = mask[0], g = mask[1], b = mask[2];
        if (pf_flags & DDPF_ALPHA) {
            r = g = b = 0;
            if (!a)
                a = bpp == 8 ? 0xff : 0;
        }
        uint64_t full = bpp == 32 ? 0xffffffffu : ((uint64_t)1 << bpp) - 1;
        if (pf_flags & DDPF_LUMINANCE) {
            g = b = 0;
            info->luminance = true;
            if ((r | a) & ~full) {
                /*
                 * some writers put the masks of a 32 bit pixel here. the gray gets the low half of the bits with
                 * alpha, all of them without, and alpha the rest.
                 */
                uint64_t low = a ? ((uint64_t)1 << (bpp / 2)) - 1 : full;
                r = (uint32_t)low;
                a = a ? (uint32_t)(full & ~low) : 0;
            }
        }
        if (((uint64_t)r | g | b | a) & ~full)
            return false;
        if (!(r | g | b | a))
            return false;
        set_bitfield(info, bpp, r, g, b, a);
        return true;
    }
    return false;
}

/* parse. */

static unsigned popcount(uint32_t v)
{
    unsigned n = 0;
    for (; v; v &= v - 1)
        n++;
    return n;
}

static bool subresource_size(const MimeDdsInfo *info, unsigned mip, MimeDdsSubresource *sub)
{
    sub->width = mime_dds_mip_size(info->width, mip);
    sub->height = mime_dds_mip_size(info->height, mip);
    sub->depth = mime_dds_mip_size(info->depth, mip);
    if (info->block_bytes) {
        sub->row_pitch = (size_t)((sub->width + 3) / 4) * info->block_bytes;
        sub->slice_pitch = sub->row_pitch * ((sub->height + 3) / 4);
    } else {
        sub->row_pitch = ((size_t)sub->width * info->bits_per_pixel + 7) / 8;
        sub->slice_pitch = sub->row_pitch * sub->height;
    }
    sub->size = (uint64_t)sub->slice_pitch * sub->depth;
    return true;
}

int mime_dds_parse(const void *data, size_t size, MimeDdsInfo *info)
{
    if (!info)
        return MIME_BCN_ERR_PARAM;
    __builtin_memset(info, 0, sizeof(*info));
    if (!data || size < 4 + 124 || rd32(data) != DDS_MAGIC)
        return MIME_BCN_ERR_FORMAT;
    const uint8_t *h = (const uint8_t *)data + 4;
    if (rd32(h) != 124 || rd32(h + 72) != 32)
        return MIME_BCN_ERR_FORMAT;
    uint32_t flags = rd32(h + 4), height = rd32(h + 8), width = rd32(h + 12), depth = rd32(h + 20),
             mips = rd32(h + 24);
    uint32_t pf_flags = rd32(h + 76), fourcc = rd32(h + 80), bpp = rd32(h + 84);
    uint32_t mask[4] = {rd32(h + 88), rd32(h + 92), rd32(h + 96), rd32(h + 100)};
    uint32_t caps2 = rd32(h + 108);

    if (!width || !height || width > MIME_DDS_MAX_DIMENSION || height > MIME_DDS_MAX_DIMENSION)
        return MIME_BCN_ERR_FORMAT;
    info->width = width;
    info->height = height;
    info->depth = 1;
    info->dimension = 2;
    info->array_size = 1;
    info->face_count = 1;
    info->data_offset = 4 + 124;

    bool dx10 = (pf_flags & DDPF_FOURCC) && fourcc == FOURCC_DX10;
    uint32_t misc = 0;
    if (dx10) {
        if (size < 4 + 124 + 20)
            return MIME_BCN_ERR_FORMAT;
        const uint8_t *x = h + 124;
        uint32_t fmt = rd32(x), dim = rd32(x + 4), arr = rd32(x + 12);
        misc = rd32(x + 8);
        info->alpha_mode = rd32(x + 16) & 7;
        info->data_offset += 20;
        info->fourcc = fourcc;
        if (!dxgi_format(info, fmt)) {
            info->format = MIME_DDS_FMT_UNKNOWN;
            info->dxgi_format = fmt;
            return MIME_BCN_ERR_UNSUPPORTED;
        }
        info->premultiplied = info->alpha_mode == 2;
        if (dim == 2) {
            info->dimension = 1;
            if (height != 1)
                return MIME_BCN_ERR_FORMAT;
        } else if (dim == 3) {
            info->dimension = 2;
        } else if (dim == 4) {
            if (!(flags & DDSD_DEPTH) || !depth)
                return MIME_BCN_ERR_FORMAT;
            info->dimension = 3;
            info->volume = true;
            info->depth = depth;
        } else {
            return MIME_BCN_ERR_FORMAT;
        }
        /* a count of 0 is written by some tools, it is read as 1. */
        if (arr > MIME_DDS_MAX_ARRAY)
            return MIME_BCN_ERR_FORMAT;
        info->array_size = arr ? arr : 1;
        if (misc & 4) {
            if (info->dimension != 2)
                return MIME_BCN_ERR_FORMAT;
            info->cube = true;
            info->face_mask = 0x3f;
        }
    } else {
        if (!legacy_format(info, pf_flags, fourcc, bpp, mask)) {
            info->format = MIME_DDS_FMT_UNKNOWN;
            return MIME_BCN_ERR_UNSUPPORTED;
        }
        if (pf_flags & DDPF_ALPHAPIXELS)
            info->has_alpha = true;
        if ((flags & DDSD_DEPTH) && (caps2 & DDSCAPS2_VOLUME)) {
            if (!depth)
                return MIME_BCN_ERR_FORMAT;
            info->dimension = 3;
            info->volume = true;
            info->depth = depth;
        } else if (caps2 & DDSCAPS2_CUBEMAP) {
            info->cube = true;
            info->face_mask = (caps2 >> 10) & 0x3f;
            if (!info->face_mask)
                info->face_mask = 0x3f;
        }
    }
    if (info->cube) {
        if (width != height)
            return MIME_BCN_ERR_FORMAT;
        info->face_count = popcount(info->face_mask);
    }
    if (info->volume && info->depth > MIME_DDS_MAX_DEPTH)
        return MIME_BCN_ERR_UNSUPPORTED;

    uint32_t longest = width > height ? width : height;
    if (info->depth > longest)
        longest = info->depth;
    unsigned chain = 1;
    while ((longest >> chain) != 0)
        chain++;
    if (!(flags & DDSD_MIPMAPCOUNT) || !mips)
        mips = 1;
    if (mips > 32)
        return MIME_BCN_ERR_FORMAT;
    info->mip_count = mips > chain ? chain : mips;

    uint64_t per_slice = 0;
    for (unsigned m = 0; m < info->mip_count; m++) {
        MimeDdsSubresource sub;
        subresource_size(info, m, &sub);
        per_slice += sub.size;
    }
    uint64_t total = per_slice * info->array_size * info->face_count;
    if (total > MAX_TOTAL)
        return MIME_BCN_ERR_UNSUPPORTED;
    info->total_size = total;
    info->complete = size - info->data_offset >= total;
    return MIME_BCN_OK;
}

int mime_dds_subresource(const MimeDdsInfo *info, size_t file_size, unsigned slice, unsigned mip,
                         MimeDdsSubresource *out)
{
    if (!info || !out || mip >= info->mip_count || slice >= info->array_size * info->face_count)
        return MIME_BCN_ERR_PARAM;
    uint64_t per_slice = 0, before = 0;
    for (unsigned m = 0; m < info->mip_count; m++) {
        MimeDdsSubresource s;
        subresource_size(info, m, &s);
        if (m < mip)
            before += s.size;
        per_slice += s.size;
    }
    subresource_size(info, mip, out);
    uint64_t off = (uint64_t)info->data_offset + per_slice * slice + before;
    out->offset = off > (size_t)-1 ? (size_t)-1 : (size_t)off;
    if (off + out->size > file_size)
        return MIME_BCN_ERR_DATA;
    return MIME_BCN_OK;
}

/* decode. */

static inline uint8_t expand_n(uint32_t v, unsigned n)
{
    switch (n) {
    case 0:
        return 0;
    case 1:
        return v ? 255 : 0;
    case 2:
        return (uint8_t)(v * 85);
    case 4:
        return (uint8_t)(v * 17);
    case 8:
        return (uint8_t)v;
    case 16:
        return (uint8_t)((v + 128) / 257);
    default: {
        uint32_t max = (1u << n) - 1;
        return (uint8_t)((v * 255 + max / 2) / max);
    }
    }
}

typedef struct Field {
    uint32_t mask;
    unsigned shift;
    unsigned bits;
} Field;

static Field field_of(uint32_t mask)
{
    Field f = {mask, 0, 0};
    if (!mask)
        return f;
    f.shift = (unsigned)__builtin_ctz(mask);
    f.bits = popcount(mask >> f.shift);
    return f;
}

static void unpremultiply_row(uint8_t *row, unsigned width)
{
    for (unsigned x = 0; x < width; x++, row += 4) {
        unsigned a = row[3];
        if (a == 0 || a == 255)
            continue;
        for (int c = 0; c < 3; c++) {
            unsigned v = (row[c] * 255u + a / 2) / a;
            row[c] = (uint8_t)(v > 255 ? 255 : v);
        }
    }
}

static uint8_t float_to_u8(uint32_t bits)
{
    float f;
    __builtin_memcpy(&f, &bits, 4);
    if (!(f > 0.0f))
        return 0;
    if (f >= 1.0f)
        return 255;
    return (uint8_t)(f * 255.0f + 0.5f);
}

static void convert_row(const MimeDdsInfo *info, const uint8_t *s, uint8_t *d, unsigned width,
                        const Field f[4])
{
    switch (info->format) {
    case MIME_DDS_FMT_RGBA16:
    case MIME_DDS_FMT_F16:
    case MIME_DDS_FMT_F32: {
        unsigned ch = info->channels;
        unsigned size = info->format == MIME_DDS_FMT_F32 ? 4 : 2;
        for (unsigned x = 0; x < width; x++, d += 4) {
            uint8_t v[4] = {0, 0, 0, 255};
            for (unsigned c = 0; c < ch; c++) {
                const uint8_t *p = s + ((size_t)x * ch + c) * size;
                if (info->format == MIME_DDS_FMT_RGBA16)
                    v[c] = (uint8_t)((((uint32_t)p[0] | (uint32_t)p[1] << 8) + 128) / 257);
                else if (info->format == MIME_DDS_FMT_F16)
                    v[c] = mime_bcn_half_to_u8((uint16_t)(p[0] | p[1] << 8));
                else
                    v[c] = float_to_u8(rd32(p));
            }
            if (ch == 1) {
                v[1] = v[2] = v[0];
            }
            d[0] = v[2];
            d[1] = v[1];
            d[2] = v[0];
            d[3] = v[3];
        }
        return;
    }
    default:
        break;
    }

    unsigned bytes = info->bits_per_pixel / 8;
    uint8_t alpha_default = f[3].mask ? 0 : 255;
    for (unsigned x = 0; x < width; x++, d += 4, s += bytes) {
        uint32_t px = s[0];
        if (bytes > 1)
            px |= (uint32_t)s[1] << 8;
        if (bytes > 2)
            px |= (uint32_t)s[2] << 16;
        if (bytes > 3)
            px |= (uint32_t)s[3] << 24;
        uint8_t r = expand_n((px & f[0].mask) >> f[0].shift, f[0].bits);
        uint8_t g = expand_n((px & f[1].mask) >> f[1].shift, f[1].bits);
        uint8_t b = expand_n((px & f[2].mask) >> f[2].shift, f[2].bits);
        uint8_t a = f[3].mask ? expand_n((px & f[3].mask) >> f[3].shift, f[3].bits) : alpha_default;
        if (info->luminance)
            g = b = r;
        d[0] = b;
        d[1] = g;
        d[2] = r;
        d[3] = a;
    }
}

int mime_dds_decode(const MimeDdsInfo *info, const void *file, size_t file_size, unsigned slice, unsigned mip,
                    unsigned z, uint8_t *dst, size_t dst_stride, unsigned flags)
{
    if (!info || !file || !dst)
        return MIME_BCN_ERR_PARAM;
    MimeDdsSubresource sub;
    int rc = mime_dds_subresource(info, file_size, slice, mip, &sub);
    if (rc < 0)
        return rc;
    if (z >= sub.depth || dst_stride < (size_t)sub.width * 4)
        return MIME_BCN_ERR_PARAM;
    const uint8_t *base = (const uint8_t *)file + sub.offset + (size_t)z * sub.slice_pitch;
    bool unpremul = (flags & MIME_BCN_UNPREMULTIPLY) && info->premultiplied;

    if (info->block_bytes) {
        static const uint8_t map[] = {[MIME_DDS_FMT_BC1] = MIME_BCN_BC1, [MIME_DDS_FMT_BC2] = MIME_BCN_BC2,
                                      [MIME_DDS_FMT_BC3] = MIME_BCN_BC3, [MIME_DDS_FMT_BC4U] = MIME_BCN_BC4U,
                                      [MIME_DDS_FMT_BC4S] = MIME_BCN_BC4S, [MIME_DDS_FMT_BC5U] = MIME_BCN_BC5U,
                                      [MIME_DDS_FMT_BC5S] = MIME_BCN_BC5S, [MIME_DDS_FMT_BC6H_UF16] = MIME_BCN_BC6H_UF16,
                                      [MIME_DDS_FMT_BC6H_SF16] = MIME_BCN_BC6H_SF16, [MIME_DDS_FMT_BC7] = MIME_BCN_BC7};
        unsigned fmt = map[info->format];
        unsigned f = flags & ~(unsigned)MIME_BCN_UNPREMULTIPLY;
        if (unpremul && (fmt == MIME_BCN_BC2 || fmt == MIME_BCN_BC3))
            f |= MIME_BCN_UNPREMULTIPLY;
        return mime_bcn_decode(fmt, base, sub.slice_pitch, sub.width, sub.height, dst, dst_stride, f);
    }

    Field fld[4] = {field_of(info->mask[0]), field_of(info->mask[1]), field_of(info->mask[2]),
                    field_of(info->mask[3])};
    for (unsigned y = 0; y < sub.height; y++) {
        uint8_t *d = dst + (size_t)y * dst_stride;
        convert_row(info, base + (size_t)y * sub.row_pitch, d, sub.width, fld);
        if (unpremul)
            unpremultiply_row(d, sub.width);
    }
    return MIME_BCN_OK;
}

/* write. */

static size_t level_bytes(unsigned format, unsigned w, unsigned h)
{
    if (format == MIME_DDS_WRITE_BC1)
        return mime_bcn_size(MIME_BCN_BC1, w, h);
    if (format == MIME_DDS_WRITE_BC3)
        return mime_bcn_size(MIME_BCN_BC3, w, h);
    return (size_t)w * h * 4;
}

int mime_dds_write(const uint8_t *bgra, size_t stride, unsigned width, unsigned height, unsigned format,
                   unsigned flags, const MimeAllocator *alloc, uint8_t **out, size_t *out_size)
{
    if (!out || !out_size)
        return MIME_BCN_ERR_PARAM;
    *out = NULL;
    *out_size = 0;
    if (!bgra || !alloc || !width || !height || width > MIME_DDS_MAX_DIMENSION || height > MIME_DDS_MAX_DIMENSION ||
        stride < (size_t)width * 4 || format < MIME_DDS_WRITE_BC1 || format > MIME_DDS_WRITE_RGBA8)
        return MIME_BCN_ERR_PARAM;

    unsigned levels = 1;
    if (flags & MIME_DDS_WRITE_MIPMAPS) {
        unsigned longest = width > height ? width : height;
        while ((longest >> levels) != 0)
            levels++;
    }
    bool dx10 = format == MIME_DDS_WRITE_RGBA8 || (flags & MIME_DDS_WRITE_DX10);
    size_t header = 4 + 124 + (dx10 ? 20 : 0);
    uint64_t total = header;
    for (unsigned m = 0; m < levels; m++)
        total += level_bytes(format, mime_dds_mip_size(width, m), mime_dds_mip_size(height, m));
    if (total > 0x7fffffffu)
        return MIME_BCN_ERR_UNSUPPORTED;
    uint8_t *file = mime_alloc(alloc, (size_t)total);
    if (!file)
        return MIME_BCN_ERR_NOMEM;
    __builtin_memset(file, 0, header);

    bool compressed = format == MIME_DDS_WRITE_BC1 || format == MIME_DDS_WRITE_BC3;
    uint8_t *h = file + 4;
    wr32(file, DDS_MAGIC);
    wr32(h, 124);
    uint32_t dflags = 0x1 | 0x2 | 0x4 | 0x1000 | (compressed ? 0x80000u : 0x8u);
    if (levels > 1)
        dflags |= DDSD_MIPMAPCOUNT;
    wr32(h + 4, dflags);
    wr32(h + 8, height);
    wr32(h + 12, width);
    wr32(h + 16, compressed ? (uint32_t)level_bytes(format, width, height) : width * 4);
    wr32(h + 24, levels > 1 ? levels : 0);
    wr32(h + 72, 32);
    uint32_t caps = 0x1000;
    if (levels > 1)
        caps |= 0x8 | 0x400000;
    wr32(h + 104, caps);
    if (dx10) {
        wr32(h + 76, DDPF_FOURCC);
        wr32(h + 80, FOURCC_DX10);
        uint32_t fmt = format == MIME_DDS_WRITE_BC1 ? 71 : format == MIME_DDS_WRITE_BC3 ? 77 : format == MIME_DDS_WRITE_BGRA8 ? 87 : 28;
        wr32(h + 124, fmt);
        wr32(h + 128, 3);
        wr32(h + 132, 0);
        wr32(h + 136, 1);
        wr32(h + 140, format == MIME_DDS_WRITE_BC1 ? 0 : 1);
    } else if (compressed) {
        wr32(h + 76, DDPF_FOURCC);
        wr32(h + 80, format == MIME_DDS_WRITE_BC1 ? FOURCC('D', 'X', 'T', '1') : FOURCC('D', 'X', 'T', '5'));
    } else {
        wr32(h + 76, DDPF_RGB | DDPF_ALPHAPIXELS);
        wr32(h + 84, 32);
        wr32(h + 88, 0xff0000);
        wr32(h + 92, 0xff00);
        wr32(h + 96, 0xff);
        wr32(h + 100, 0xff000000u);
    }

    /* the levels, each from the one before it. */
    uint8_t *tmp[2] = {NULL, NULL};
    int rc = MIME_BCN_OK;
    if (levels > 1) {
        size_t cap = (size_t)mime_dds_mip_size(width, 1) * mime_dds_mip_size(height, 1) * 4;
        tmp[0] = mime_alloc(alloc, cap);
        tmp[1] = levels > 2 ? mime_alloc(alloc, cap) : NULL;
        if (!tmp[0] || (levels > 2 && !tmp[1]))
            rc = MIME_BCN_ERR_NOMEM;
    }
    const uint8_t *src = bgra;
    size_t sstride = stride;
    unsigned sw = width, sh = height;
    uint8_t *dst = file + header;
    for (unsigned m = 0; m < levels && rc == MIME_BCN_OK; m++) {
        size_t bytes = level_bytes(format, sw, sh);
        if (format == MIME_DDS_WRITE_BC1) {
            rc = mime_bcn_encode(MIME_BCN_BC1, src, sstride, sw, sh, (flags & MIME_DDS_WRITE_ALPHA) != 0, dst, bytes);
        } else if (format == MIME_DDS_WRITE_BC3) {
            rc = mime_bcn_encode(MIME_BCN_BC3, src, sstride, sw, sh, true, dst, bytes);
        } else {
            for (unsigned y = 0; y < sh; y++) {
                const uint8_t *s = src + (size_t)y * sstride;
                uint8_t *d = dst + (size_t)y * sw * 4;
                if (format == MIME_DDS_WRITE_BGRA8) {
                    __builtin_memcpy(d, s, (size_t)sw * 4);
                } else {
                    for (unsigned x = 0; x < sw; x++) {
                        d[x * 4] = s[x * 4 + 2];
                        d[x * 4 + 1] = s[x * 4 + 1];
                        d[x * 4 + 2] = s[x * 4];
                        d[x * 4 + 3] = s[x * 4 + 3];
                    }
                }
            }
        }
        dst += bytes;
        if (m + 1 < levels && rc == MIME_BCN_OK) {
            unsigned nw = mime_dds_mip_size(width, m + 1), nh = mime_dds_mip_size(height, m + 1);
            uint8_t *next = tmp[m & 1];
            for (unsigned y = 0; y < nh; y++) {
                unsigned y0 = y * 2 < sh ? y * 2 : sh - 1, y1 = y * 2 + 1 < sh ? y * 2 + 1 : sh - 1;
                for (unsigned x = 0; x < nw; x++) {
                    unsigned x0 = x * 2 < sw ? x * 2 : sw - 1, x1 = x * 2 + 1 < sw ? x * 2 + 1 : sw - 1;
                    const uint8_t *p00 = src + (size_t)y0 * sstride + x0 * 4, *p01 = src + (size_t)y0 * sstride + x1 * 4,
                                  *p10 = src + (size_t)y1 * sstride + x0 * 4, *p11 = src + (size_t)y1 * sstride + x1 * 4;
                    for (int c = 0; c < 4; c++)
                        next[((size_t)y * nw + x) * 4 + (unsigned)c] =
                            (uint8_t)((p00[c] + p01[c] + p10[c] + p11[c] + 2) >> 2);
                }
            }
            src = next;
            sstride = (size_t)nw * 4;
            sw = nw;
            sh = nh;
        }
    }
    mime_free(alloc, tmp[0]);
    mime_free(alloc, tmp[1]);
    if (rc != MIME_BCN_OK) {
        mime_free(alloc, file);
        return rc;
    }
    *out = file;
    *out_size = (size_t)total;
    return MIME_BCN_OK;
}
