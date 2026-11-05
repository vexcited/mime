#include "internal.h"

static int sample_bytes(int bd)
{
    switch (bd) {
    case MIME_JXR_BD8:
        return 1;
    case MIME_JXR_BD16:
    case MIME_JXR_BD16S:
    case MIME_JXR_BD16F:
        return 2;
    case MIME_JXR_BD32S:
    case MIME_JXR_BD32F:
        return 4;
    default:
        return 0;
    }
}

static void put_le(uint8_t *p, int bytes, uint32_t v)
{
    for (int i = 0; i < bytes; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

typedef struct OutParams {
    int bd, iscale, round, bias, shift, len_mantissa, exp_bias;
} OutParams;

static void out_params(const JxrImage *im, const JxrPlane *p, OutParams *o)
{
    o->bd = im->bd;
    o->iscale = p->scaled ? 3 : 0;
    o->round = 0;
    if (p->scaled) {
        if (im->bd == MIME_JXR_BD1WHITE1 || im->bd == MIME_JXR_BD1BLACK1 || im->bd == MIME_JXR_BD16)
            o->round = 4;
        else
            o->round = 3;
    }
    switch (im->bd) {
    case MIME_JXR_BD5:
        o->bias = 1 << 4;
        break;
    case MIME_JXR_BD565:
        o->bias = 1 << 5;
        break;
    case MIME_JXR_BD8:
        o->bias = 1 << 7;
        break;
    case MIME_JXR_BD10:
        o->bias = 1 << 9;
        break;
    case MIME_JXR_BD16:
        o->bias = 1 << 15;
        break;
    default:
        o->bias = 0;
    }
    o->shift = p->shift_bits;
    if (im->bd == MIME_JXR_BD16 || im->bd == MIME_JXR_BD16S || im->bd == MIME_JXR_BD32S)
        o->bias >>= o->shift;
    o->len_mantissa = p->len_mantissa;
    o->exp_bias = p->exp_bias;
}

static uint32_t post_float(const OutParams *o, int32_t x)
{
    uint32_t s = x < 0;
    uint32_t a = x < 0 ? (uint32_t)-(int64_t)x : (uint32_t)x;
    if (o->bd == MIME_JXR_BD16F)
        return (s << 15) | (a > 32767 ? 32767 : a);
    int lm = o->len_mantissa;
    if (lm > 23)
        lm = 23;
    int e = (int)(a >> lm);
    uint32_t m = (a & ((1u << lm) - 1)) | (1u << lm);
    if (e == 0) {
        m ^= 1u << lm;
        e = 1;
    }
    e = e - o->exp_bias + 127;
    while (m < (1u << lm) && e > 1 && m > 0) {
        e--;
        m <<= 1;
    }
    if (m < (1u << lm))
        e = 0;
    else
        m ^= 1u << lm;
    if (e > 255)
        e = 255;
    if (e < 0)
        e = 0;
    m <<= 23 - lm;
    return (s << 31) | ((uint32_t)e << 23) | m;
}

/* scaling, post scaling and clipping of one output sample of channel ch. */
static int32_t out_value(const OutParams *o, int32_t v, int ch)
{
    int js = o->iscale + (o->bd == MIME_JXR_BD565 && ch != 1);
    v = (v + o->round) >> js;
    switch (o->bd) {
    case MIME_JXR_BD8:
        return v < 0 ? 0 : v > 255 ? 255 : v;
    case MIME_JXR_BD16:
        v = (int32_t)((uint32_t)v << o->shift);
        return v < 0 ? 0 : v > 65535 ? 65535 : v;
    case MIME_JXR_BD16S:
        v = (int32_t)((uint32_t)v << o->shift);
        return v < -32768 ? -32768 : v > 32767 ? 32767 : v;
    case MIME_JXR_BD32S:
        return (int32_t)((uint32_t)v << o->shift);
    case MIME_JXR_BD16F:
    case MIME_JXR_BD32F:
        return (int32_t)post_float(o, v);
    case MIME_JXR_BD5:
        return v < 0 ? 0 : v > 31 ? 31 : v;
    case MIME_JXR_BD565:
        return v < 0 ? 0 : v > (ch == 1 ? 63 : 31) ? (ch == 1 ? 63 : 31) : v;
    case MIME_JXR_BD10:
        return v < 0 ? 0 : v > 1023 ? 1023 : v;
    default:
        return v < 0 ? 0 : v > 1 ? 1 : v;
    }
}

static void upsample(int32_t *dst, const int32_t *src, int n, ptrdiff_t ds, ptrdiff_t ss, int centering)
{
    static const int h[5][4] = {{4, 4, 0, 8}, {5, 3, 1, 7}, {6, 2, 2, 6}, {7, 1, 3, 5}, {8, 0, 4, 4}};
    const int *f = h[centering > 4 ? 0 : centering];
    for (int k = 0; k < n; k++) {
        int32_t prev = src[(k > 0 ? k - 1 : 0) * ss], cur = src[k * ss], next = src[(k + 1 < n ? k + 1 : n - 1) * ss];
        dst[2 * k * ds] = (f[2] * prev + f[3] * cur + 4) >> 3;
        dst[(2 * k + 1) * ds] = (f[0] * cur + f[1] * next + 4) >> 3;
    }
}

/* full size chroma planes for 4:2:0 and 4:2:2. */
static int upsample_chroma(JxrImage *im, JxrPlane *p, int32_t **full)
{
    int w = im->ext_w, h = im->ext_h, cw = p->cw, ch = p->chh;
    for (int i = 1; i <= 2; i++) {
        full[i - 1] = mime_alloc(im->alloc, (size_t)w * h * sizeof(int32_t));
        if (!full[i - 1])
            return MIME_JXR_ERR_NOMEM;
        int32_t *dst = full[i - 1];
        const int32_t *src = p->c[i];
        if (p->cf == MIME_JXR_CF_YUV420) {
            /* vertical into the left half columns of the destination, then horizontal from the right. */
            for (int x = 0; x < cw; x++)
                upsample(dst + x, src + x, ch, w, cw, p->chroma_y);
            for (int y = 0; y < h; y++) {
                int32_t *row = dst + (size_t)y * w;
                int32_t tmp[64];
                int32_t *line = cw <= 64 ? tmp : nullptr;
                if (!line) {
                    line = mime_alloc(im->alloc, (size_t)cw * sizeof(int32_t));
                    if (!line)
                        return MIME_JXR_ERR_NOMEM;
                }
                jxr_memcpy(line, row, (size_t)cw * sizeof(int32_t));
                upsample(row, line, cw, 1, 1, p->chroma_x);
                if (line != tmp)
                    mime_free(im->alloc, line);
            }
        } else {
            for (int y = 0; y < h; y++)
                upsample(dst + (size_t)y * w, src + (size_t)y * cw, cw, 1, 1, p->chroma_x);
        }
    }
    return MIME_JXR_OK;
}

static int32_t opaque(int bd)
{
    switch (bd) {
    case MIME_JXR_BD8:
        return 255;
    case MIME_JXR_BD16:
        return 65535;
    case MIME_JXR_BD16S:
        return 0x2000;
    case MIME_JXR_BD16F:
        return 0x3c00;
    case MIME_JXR_BD32S:
        return 0x1000000;
    case MIME_JXR_BD32F:
        return 0x3f800000;
    default:
        return 0;
    }
}

static bool layout_matches(const JxrImage *im, const JxrLayout *l)
{
    const JxrPlane *p = &im->plane[0];
    if (l->bd == MIME_JXR_BD1WHITE1)
        return (im->bd == MIME_JXR_BD1WHITE1 || im->bd == MIME_JXR_BD1BLACK1) && p->cf == MIME_JXR_CF_YONLY;
    if (l->bd != im->bd || l->cf != im->out_cf)
        return false;
    switch (im->out_cf) {
    case MIME_JXR_CF_RGB:
        return p->cf == MIME_JXR_CF_YONLY || p->cf == MIME_JXR_CF_YUV420 || p->cf == MIME_JXR_CF_YUV422 ||
               p->cf == MIME_JXR_CF_YUV444;
    case MIME_JXR_CF_RGBE:
        return p->cf == MIME_JXR_CF_YUV444;
    case MIME_JXR_CF_CMYK:
        return p->cf == MIME_JXR_CF_YUVK;
    case MIME_JXR_CF_YONLY:
        return p->cf == MIME_JXR_CF_YONLY;
    case MIME_JXR_CF_NCOMPONENT:
        return p->cf == MIME_JXR_CF_NCOMPONENT && p->ncomp == l->nc;
    default:
        return false;
    }
}

static int output(JxrImage *im, const JxrPlane *alpha, const JxrImage *aim, const JxrLayout *l, uint8_t *out,
                  size_t stride)
{
    JxrPlane *p = &im->plane[0];
    int32_t *full[2] = {nullptr, nullptr};
    int rc = MIME_JXR_OK;
    if (jxr_chroma_sub(p)) {
        rc = upsample_chroma(im, p, full);
        if (rc)
            goto done;
    }
    OutParams o, oa;
    out_params(im, p, &o);
    if (alpha)
        out_params(aim, alpha, &oa);
    int bytes = sample_bytes(im->bd);
    int w = im->ext_w;
    bool swap = (im->bd == MIME_JXR_BD5 || im->bd == MIME_JXR_BD565 || im->bd == MIME_JXR_BD10) && !im->rb_not_swapped;
    for (uint32_t y = 0; y < im->height; y++) {
        uint8_t *row = out + (size_t)y * stride;
        size_t sy = (size_t)(y + (uint32_t)im->top) * w;
        if (l->bd == MIME_JXR_BD1WHITE1)
            jxr_memset(row, 0, (im->width + 7) / 8);
        for (uint32_t x = 0; x < im->width; x++) {
            size_t s = sy + x + (uint32_t)im->left;
            int32_t v[JXR_MAX_COMP];
            for (int i = 0; i < p->ncomp; i++)
                v[i] = i && full[0] ? full[i - 1][s] : p->c[i][s];
            int32_t c[JXR_MAX_COMP];
            int nout = l->nc;
            if (im->out_cf == MIME_JXR_CF_RGB || im->out_cf == MIME_JXR_CF_RGBE) {
                if (p->cf == MIME_JXR_CF_YONLY) {
                    c[0] = c[1] = c[2] = v[0];
                } else {
                    int32_t t = -v[1];
                    c[1] = v[0] - (t >> 1);
                    c[0] = t + c[1] - ((v[2] + 1) >> 1);
                    c[2] = v[2] + c[0];
                    if (swap) {
                        int32_t s2 = c[0];
                        c[0] = c[2];
                        c[2] = s2;
                    }
                }
            } else if (im->out_cf == MIME_JXR_CF_CMYK) {
                c[3] = v[3] + (v[0] >> 1);
                c[1] = c[3] - v[0] - (v[1] >> 1);
                c[0] = v[1] + c[1] + (v[2] >> 1);
                c[2] = c[0] - v[2];
            } else {
                for (int i = 0; i < p->ncomp; i++)
                    c[i] = v[i];
            }
            uint8_t *px = row + (size_t)x * (l->bits / 8);
            if (im->out_cf == MIME_JXR_CF_RGBE) {
                int32_t in[3], e[3], r[4];
                for (int i = 0; i < 3; i++)
                    in[i] = (c[i] + o.round) >> o.iscale;
                for (int i = 0; i < 3; i++) {
                    if (in[i] <= 0) {
                        r[i] = 0;
                        e[i] = 0;
                    } else if ((in[i] >> 7) > 1) {
                        r[i] = (in[i] & 0x7f) + 128;
                        e[i] = in[i] >> 7;
                    } else {
                        r[i] = in[i];
                        e[i] = 1;
                    }
                }
                r[3] = e[0] > e[1] ? e[0] : e[1];
                r[3] = r[3] > e[2] ? r[3] : e[2];
                for (int i = 0; i < 3; i++)
                    if (r[3] > e[i])
                        r[i] = (2 * r[i] + 1) >> (r[3] - e[i] + 1);
                for (int i = 0; i < 4; i++)
                    px[l->pos[i]] = (uint8_t)(r[i] > 255 ? 255 : r[i]);
                continue;
            }
            for (int i = 0; i < nout; i++) {
                if (im->out_cf == MIME_JXR_CF_CMYK)
                    c[i] += i == 3 ? -((o.bias >> 1) << o.iscale) : (o.bias >> 1) << o.iscale;
                else
                    c[i] += o.bias << o.iscale;
                c[i] = out_value(&o, c[i], i);
            }
            if (l->bd == MIME_JXR_BD1WHITE1) {
                int bit = c[0] & 1;
                if (im->bd == MIME_JXR_BD1BLACK1)
                    bit ^= 1;
                if (bit)
                    row[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
                continue;
            }
            if (im->bd == MIME_JXR_BD565) {
                put_le(px, 2, (uint32_t)(c[0] | c[1] << 5 | c[2] << 11));
                continue;
            }
            if (im->bd == MIME_JXR_BD5) {
                put_le(px, 2, (uint32_t)(c[0] | c[1] << 5 | c[2] << 10));
                continue;
            }
            if (im->bd == MIME_JXR_BD10) {
                put_le(px, 4, (uint32_t)c[0] | (uint32_t)c[1] << 10 | (uint32_t)c[2] << 20);
                continue;
            }
            for (int i = 0; i < nout; i++)
                put_le(px + l->pos[i] * bytes, bytes, (uint32_t)c[i]);
            if (l->alpha) {
                int32_t a = opaque(im->bd);
                if (alpha) {
                    size_t sa = (size_t)(y + (uint32_t)aim->top) * aim->ext_w + x + (uint32_t)aim->left;
                    a = out_value(&oa, alpha->c[0][sa] + (oa.bias << oa.iscale), 0);
                }
                put_le(px + l->pos[nout] * bytes, bytes, (uint32_t)a);
            }
            for (int i = nout + l->alpha; i < l->slots; i++)
                put_le(px + i * bytes, bytes, bytes == 1 ? 0xff : 0);
        }
    }
done:
    mime_free(im->alloc, full[0]);
    mime_free(im->alloc, full[1]);
    return rc;
}

int mime_jxr_decode(const uint8_t *data, size_t size, const uint8_t *alpha, size_t alpha_size, uint8_t layout,
                    const MimeAllocator *alloc, uint32_t width, uint32_t height, void *out, size_t stride)
{
    JxrLayout l;
    if (!jxr_find_layout(layout, &l))
        return MIME_JXR_ERR_UNSUPPORTED;
    if (stride < ((size_t)width * l.bits + 7) / 8)
        return MIME_JXR_ERR_PARAM;
    JxrImage *im, *aim = nullptr;
    int rc = jxr_image_decode(data, size, alloc, false, &im);
    if (rc)
        return rc;
    if (im->width != width || im->height != height) {
        jxr_image_free(im);
        return MIME_JXR_ERR_FORMAT;
    }
    if (!layout_matches(im, &l)) {
        jxr_image_free(im);
        return MIME_JXR_ERR_UNSUPPORTED;
    }
    const JxrPlane *ap = nullptr;
    const JxrImage *ai = im;
    if (l.alpha && im->alpha) {
        ap = &im->plane[1];
    } else if (l.alpha && alpha && alpha_size) {
        rc = jxr_image_decode(alpha, alpha_size, alloc, false, &aim);
        if (rc == MIME_JXR_OK && (aim->width != im->width || aim->height != im->height || aim->plane[0].ncomp != 1))
            rc = MIME_JXR_ERR_FORMAT;
        if (rc) {
            jxr_image_free(aim);
            jxr_image_free(im);
            return rc;
        }
        ap = &aim->plane[0];
        ai = aim;
    }
    rc = output(im, ap, ai, &l, out, stride);
    jxr_image_free(aim);
    jxr_image_free(im);
    return rc;
}
