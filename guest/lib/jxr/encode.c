#include "internal.h"

/* ------------------------------------------------------------------ quantizer choice, as windows makes it */

static int step_size(int qp, bool scaled) { return jxr_quant_map(qp + 1, scaled, 1); }

static int step_to_qp(int target, bool scaled)
{
    int best = 0;
    uint32_t err = 0xffffffffu;
    for (int i = 1; i != 255; i++) {
        int d = target - step_size(i, scaled);
        uint32_t e = (uint32_t)(d < 0 ? -d : d);
        if (e < err) {
            err = e;
            best = i;
        }
    }
    return best;
}

static void choose_qp(JxrPlane *p, int out_cf, int quality)
{
    jxr_memset(&p->qp, 0, sizeof(p->qp));
    p->qp.nlp = p->qp.nhp = 1;
    bool perceptual = !p->alpha && out_cf == MIME_JXR_CF_RGB &&
                      (p->cf == MIME_JXR_CF_YUV420 || p->cf == MIME_JXR_CF_YUV422 || p->cf == MIME_JXR_CF_YUV444);
    uint8_t dc[3], lp[3], hp[3];
    if (perceptual) {
        if (quality <= 1)
            return;
        int s = step_size(quality, p->scaled);
        dc[0] = (uint8_t)quality;
        dc[1] = dc[2] = (uint8_t)step_to_qp((int)((double)s * 1.65), p->scaled);
        lp[0] = (uint8_t)step_to_qp(s, p->scaled);
        lp[1] = lp[2] = (uint8_t)step_to_qp((int)((double)s * 2.5), p->scaled);
        hp[0] = (uint8_t)step_to_qp((int)((double)s * 1.5), p->scaled);
        hp[1] = hp[2] = (uint8_t)step_to_qp((int)((double)s * 3.2), p->scaled);
        for (int i = 0; i < 3; i++)
            if (hp[i] < 2)
                hp[i] = 2;
        for (int i = 0; i < 3; i++) {
            p->qp.dc[i] = dc[i];
            p->qp.lp[0][i] = lp[i];
            p->qp.hp[0][i] = hp[i];
        }
        return;
    }
    int luma = quality >= 2 ? quality : 0, chroma;
    if (luma >= 16) {
        chroma = luma + (p->cf == MIME_JXR_CF_YUV420 ? 4 : p->cf == MIME_JXR_CF_YUV422 ? 8 : 18);
        if (luma > 48)
            chroma += 2;
    } else if (p->cf == MIME_JXR_CF_YUV420) {
        chroma = luma + ((luma + 2) >> 2);
    } else if (p->cf == MIME_JXR_CF_YUV422) {
        chroma = luma + ((luma + 1) >> 1);
    } else {
        chroma = 2 * luma;
    }
    if (chroma > 255)
        chroma = 255;
    for (int i = 0; i < p->ncomp; i++) {
        uint8_t q = (uint8_t)(i ? chroma : luma);
        p->qp.dc[i] = q;
        p->qp.lp[0][i] = q;
        p->qp.hp[0][i] = q;
    }
}

/* ------------------------------------------------------------------ input samples */

static int32_t get_le(const uint8_t *p, int bytes)
{
    uint32_t v = 0;
    for (int i = 0; i < bytes; i++)
        v |= (uint32_t)p[i] << (8 * i);
    if (bytes == 2)
        return (int32_t)v;
    return (int32_t)v;
}

static int32_t pre_float32(uint32_t f, int len_mantissa, int exp_bias)
{
    if ((f & 0x7fffffffu) == 0)
        return 0;
    int s = (int)(f >> 31);
    int e = (int)((f >> 23) & 0xff);
    uint32_t m = (f & 0x7fffff) | 0x800000;
    if (e == 0) {
        m ^= 0x800000;
        e++;
    }
    e = e - 127 + exp_bias;
    if (e <= 1) {
        if (e < 1)
            m = 1 - e < 32 ? m >> (1 - e) : 0;
        e = 1;
        if (!(m & 0x800000))
            e = 0;
    }
    m = ((m & 0x7fffff) + (1u << (23 - len_mantissa - 1))) >> (23 - len_mantissa);
    int32_t x = (int32_t)(((uint32_t)e << len_mantissa) | m);
    return s ? -x : x;
}

static int32_t rgbe_forward(int32_t rgb, int32_t e)
{
    if (e == 0)
        return 0;
    int32_t append = 1;
    e -= 1;
    while (!(rgb & 0x80) && e > 0) {
        rgb = (rgb << 1) + append;
        append = 0;
        e -= 1;
    }
    if (e == 0)
        return rgb;
    e++;
    return (rgb & 0x7f) + (e << 7);
}

typedef struct InParams {
    int bd, iscale, bias, shift, len_mantissa, exp_bias;
} InParams;

static void in_params(const JxrImage *im, const JxrPlane *p, InParams *o)
{
    o->bd = im->bd;
    o->iscale = p->scaled ? 3 : 0;
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

/* one stored sample to the internal integer scale, before the bias. */
static int32_t in_value(const InParams *o, const uint8_t *p, int ch)
{
    int32_t v;
    switch (o->bd) {
    case MIME_JXR_BD8:
        v = p[0];
        break;
    case MIME_JXR_BD16:
        v = get_le(p, 2) >> o->shift;
        break;
    case MIME_JXR_BD16S:
        v = (int16_t)get_le(p, 2) >> o->shift;
        break;
    case MIME_JXR_BD16F: {
        int32_t f = get_le(p, 2);
        v = f & 0x8000 ? -(f & 0x7fff) : f & 0x7fff;
        break;
    }
    case MIME_JXR_BD32S:
        v = get_le(p, 4) >> o->shift;
        break;
    case MIME_JXR_BD32F:
        v = pre_float32((uint32_t)get_le(p, 4), o->len_mantissa, o->exp_bias);
        break;
    default:
        v = 0;
    }
    (void)ch;
    return v;
}

static int32_t in_scale(const InParams *o, int32_t v, int ch)
{
    int js = o->iscale + (o->bd == MIME_JXR_BD565 && ch != 1);
    return (int32_t)((uint32_t)v << js);
}

static void downsample(int32_t *dst, const int32_t *src, int n, ptrdiff_t ds, ptrdiff_t ss)
{
    for (int k = 0; k < n / 2; k++) {
        int32_t a = src[(2 * k > 0 ? 2 * k - 1 : 0) * ss], b = src[2 * k * ss];
        int32_t c = src[(2 * k + 1 < n ? 2 * k + 1 : n - 1) * ss];
        dst[k * ds] = (a + 2 * b + c + 2) >> 2;
    }
}

static int fill_planes(JxrImage *im, const MimeJxrEncodeParams *e, const JxrLayout *l)
{
    JxrPlane *p = &im->plane[0];
    JxrPlane *ap = im->nplanes > 1 ? &im->plane[1] : nullptr;
    InParams o, oa;
    in_params(im, p, &o);
    int bytes = l->bd == MIME_JXR_BD8 ? 1 : l->bd == MIME_JXR_BD32S || l->bd == MIME_JXR_BD32F ? 4 : 2;
    int w = im->ext_w, h = im->ext_h;
    bool sub = jxr_chroma_sub(p);
    bool rgb = l->cf == MIME_JXR_CF_RGB || l->cf == MIME_JXR_CF_RGBE;
    int32_t *full[2] = {nullptr, nullptr};
    if (sub) {
        for (int i = 0; i < 2; i++) {
            full[i] = mime_alloc(im->alloc, (size_t)w * h * sizeof(int32_t));
            if (!full[i]) {
                mime_free(im->alloc, full[0]);
                return MIME_JXR_ERR_NOMEM;
            }
        }
    }
    int32_t *alpha_dst = ap ? ap->c[0] : nullptr;
    if (e->planar_alpha && l->alpha)
        alpha_dst = im->plane[1].c[0];
    if (alpha_dst)
        in_params(im, &im->plane[1], &oa);
    bool swap = (l->bd == MIME_JXR_BD5 || l->bd == MIME_JXR_BD565 || l->bd == MIME_JXR_BD10) && !im->rb_not_swapped;
    for (int y = 0; y < (int)im->height; y++) {
        const uint8_t *row = (const uint8_t *)e->pixels + (size_t)y * e->stride;
        for (int x = 0; x < (int)im->width; x++) {
            size_t s = (size_t)y * w + x;
            int32_t c[JXR_MAX_COMP];
            const uint8_t *px = row + (size_t)x * (l->bits / 8);
            int nc = l->nc;
            if (l->bd == MIME_JXR_BD1WHITE1) {
                c[0] = (row[x >> 3] >> (7 - (x & 7))) & 1;
            } else if (l->bd == MIME_JXR_BD565 || l->bd == MIME_JXR_BD5) {
                uint32_t v = (uint32_t)get_le(px, 2);
                c[0] = (int32_t)(v & 31);
                c[1] = (int32_t)(l->bd == MIME_JXR_BD565 ? (v >> 5) & 63 : (v >> 5) & 31);
                c[2] = (int32_t)(l->bd == MIME_JXR_BD565 ? v >> 11 : (v >> 10) & 31);
            } else if (l->bd == MIME_JXR_BD10) {
                uint32_t v = (uint32_t)get_le(px, 4);
                c[0] = (int32_t)(v & 1023);
                c[1] = (int32_t)((v >> 10) & 1023);
                c[2] = (int32_t)((v >> 20) & 1023);
            } else if (l->cf == MIME_JXR_CF_RGBE) {
                int32_t ex = px[l->pos[3]];
                for (int i = 0; i < 3; i++)
                    c[i] = rgbe_forward(px[l->pos[i]], ex);
            } else {
                for (int i = 0; i < nc; i++)
                    c[i] = in_value(&o, px + l->pos[i] * bytes, i);
            }
            for (int i = 0; i < nc; i++) {
                c[i] = in_scale(&o, c[i], i);
                if (l->cf == MIME_JXR_CF_CMYK)
                    c[i] -= i == 3 ? -((o.bias >> 1) << o.iscale) : (o.bias >> 1) << o.iscale;
                else if (l->cf != MIME_JXR_CF_RGBE)
                    c[i] -= o.bias << o.iscale;
            }
            int32_t v[JXR_MAX_COMP];
            if (rgb) {
                int32_t t0 = swap ? c[2] : c[0], t1 = c[1], t2 = swap ? c[0] : c[2];
                v[2] = t2 - t0;
                int32_t t = t0 - t1 + ((v[2] + 1) >> 1);
                v[0] = t1 + (t >> 1);
                v[1] = -t;
            } else if (l->cf == MIME_JXR_CF_CMYK) {
                v[2] = c[0] - c[2];
                v[1] = c[0] - c[1] - (v[2] >> 1);
                v[0] = c[3] - c[1] - (v[1] >> 1);
                v[3] = c[3] - (v[0] >> 1);
            } else {
                for (int i = 0; i < nc; i++)
                    v[i] = c[i];
            }
            p->c[0][s] = v[0];
            if (sub) {
                full[0][s] = v[1];
                full[1][s] = v[2];
            } else {
                for (int i = 1; i < p->ncomp; i++)
                    p->c[i][s] = v[i];
            }
            if (alpha_dst) {
                int32_t a = in_value(&oa, px + l->pos[nc] * bytes, nc);
                alpha_dst[s] = in_scale(&oa, a, 0) - (oa.bias << oa.iscale);
            }
        }
    }
    /* replicate the last column and row into the padding. */
    int32_t *planes[JXR_MAX_COMP + 3];
    int count = 0;
    planes[count++] = p->c[0];
    if (sub) {
        planes[count++] = full[0];
        planes[count++] = full[1];
    } else {
        for (int i = 1; i < p->ncomp; i++)
            planes[count++] = p->c[i];
    }
    if (alpha_dst)
        planes[count++] = alpha_dst;
    for (int k = 0; k < count; k++) {
        int32_t *c = planes[k];
        for (int y = 0; y < (int)im->height; y++)
            for (int x = (int)im->width; x < w; x++)
                c[(size_t)y * w + x] = c[(size_t)y * w + im->width - 1];
        for (int y = (int)im->height; y < h; y++)
            jxr_memcpy(c + (size_t)y * w, c + (size_t)(im->height - 1) * w, (size_t)w * sizeof(int32_t));
    }
    if (sub) {
        int cw = p->cw, ch = p->chh;
        for (int i = 0; i < 2; i++) {
            int32_t *dst = p->c[i + 1];
            if (p->cf == MIME_JXR_CF_YUV422) {
                for (int y = 0; y < h; y++)
                    downsample(dst + (size_t)y * cw, full[i] + (size_t)y * w, w, 1, 1);
            } else {
                /* horizontally in place into the left half, then vertically into the plane. */
                for (int y = 0; y < h; y++) {
                    int32_t *row = full[i] + (size_t)y * w;
                    int32_t tmp[32];
                    int32_t *line = w <= 32 ? tmp : mime_alloc(im->alloc, (size_t)w * sizeof(int32_t));
                    if (!line) {
                        mime_free(im->alloc, full[0]);
                        mime_free(im->alloc, full[1]);
                        return MIME_JXR_ERR_NOMEM;
                    }
                    jxr_memcpy(line, row, (size_t)w * sizeof(int32_t));
                    downsample(row, line, w, 1, 1);
                    if (line != tmp)
                        mime_free(im->alloc, line);
                }
                for (int x = 0; x < cw; x++)
                    downsample(dst + x, full[i] + x, h, cw, w);
            }
            (void)ch;
        }
    }
    mime_free(im->alloc, full[0]);
    mime_free(im->alloc, full[1]);
    return MIME_JXR_OK;
}

/* ------------------------------------------------------------------ coefficients */

static int32_t quant(int32_t v, int step, int off)
{
    if (step == 1)
        return v;
    if (v < 0)
        return -(int32_t)(((uint32_t)-v + (uint32_t)off) / (uint32_t)step);
    return (int32_t)(((uint32_t)v + (uint32_t)off) / (uint32_t)step);
}

static void encode_mb(JxrImage *im, JxrPlane *p, const JxrMbPos *pos, JxrMbCoef *m)
{
    int nc = p->ncomp;
    int32_t *cur = p->pred + (size_t)pos->mb * nc * 7;
    const int32_t *left = cur - nc * 7;
    const int32_t *top = cur - (size_t)im->mbw * nc * 7;
    int dcmode = jxr_dc_mode(im, p, pos);
    bool sub = jxr_chroma_sub(p);
    for (int i = 0; i < nc; i++) {
        int step = jxr_quant_map(p->qp.dc[i], p->scaled, i == 0);
        int32_t q = quant(*jxr_coef(im, p, i, pos->mbx, pos->mby, 0, 0), step, step >> 1);
        int32_t l = dcmode == 0 || dcmode == 2 ? left[i * 7] : 0, t = dcmode == 1 || dcmode == 2 ? top[i * 7] : 0;
        int32_t pred = dcmode == 0 ? l : dcmode == 1 ? t : dcmode == 2 ? (i == 0 || !sub ? (t + l) >> 1 : (t + l + 1) >> 1) : 0;
        m->dc[i] = q - pred;
        cur[i * 7] = q;
    }
    int lpmode = jxr_lp_mode(im, p, pos, dcmode);
    int32_t rec[JXR_MAX_COMP][16];
    for (int i = 0; i < nc; i++) {
        int step = jxr_quant_map(p->qp.lp[0][i], p->scaled, i == 0);
        int nb = jxr_blocks(p, i);
        int32_t q[16];
        q[0] = 0;
        for (int j = 1; j < nb; j++)
            q[j] = quant(*jxr_coef(im, p, i, pos->mbx, pos->mby, j, 0), step, step >> 1);
        for (int j = nb; j < 16; j++)
            q[j] = 0;
        int32_t *r = m->lp[i];
        jxr_memcpy(r, q, sizeof(q));
        const int32_t *li = left + i * 7, *ti = top + i * 7;
        int32_t *c = cur + i * 7;
        if (i == 0 || !sub) {
            if (lpmode == 0) {
                r[4] -= li[4];
                r[8] -= li[5];
                r[12] -= li[6];
            } else if (lpmode == 1) {
                r[1] -= ti[1];
                r[2] -= ti[2];
                r[3] -= ti[3];
            }
            c[1] = q[1];
            c[2] = q[2];
            c[3] = q[3];
            c[4] = q[4];
            c[5] = q[8];
            c[6] = q[12];
        } else if (p->cf == MIME_JXR_CF_YUV420) {
            if (lpmode == 0)
                r[2] -= li[2];
            else if (lpmode == 1)
                r[1] -= ti[1];
            c[1] = q[1];
            c[2] = q[2];
        } else {
            if (lpmode == 0) {
                r[4] -= li[4];
                r[2] -= li[2];
                r[6] -= li[6];
            } else if (lpmode == 1) {
                r[4] -= ti[4];
                r[1] -= ti[5];
                r[5] -= q[1];
            } else if (dcmode == 1) {
                r[5] -= q[1];
            }
            c[1] = q[1];
            c[2] = q[2];
            c[4] = q[4];
            c[5] = q[5];
            c[6] = q[6];
        }
        for (int j = 0; j < 16; j++)
            rec[i][j] = (int32_t)((uint32_t)q[j] * (uint32_t)step);
        rec[i][0] = 0;
    }
    int mode = jxr_hp_mode(p, (const int32_t (*)[16])rec);
    p->hpmode[pos->mb] = (uint8_t)mode;
    if (p->bands >= BANDS_NOHP)
        return;
    for (int i = 0; i < nc; i++) {
        int step = jxr_quant_map(p->qp.hp[0][i], p->scaled, 1);
        int off = (3 * step + 1) >> 3;
        int nb = jxr_blocks(p, i), cols = jxr_cols(p, i);
        int32_t d[16][16];
        for (int blk = 0; blk < nb; blk++)
            for (int j = 1; j < 16; j++) {
                int32_t pred = 0;
                if (mode == 0 && blk % cols && (j & 3) == 0)
                    pred = d[blk - 1][j];
                else if (mode == 1 && blk >= cols && j < 4)
                    pred = d[blk - cols][j];
                int32_t r = quant(*jxr_coef(im, p, i, pos->mbx, pos->mby, blk, j) - pred, step, off);
                m->hp[i][blk][j] = r;
                d[blk][j] = (int32_t)((uint32_t)r * (uint32_t)step) + pred;
            }
    }
}

/* ------------------------------------------------------------------ codestream */

static void put_vlw(JxrBits *b, uint32_t v)
{
    if (v < 0xfb00) {
        jxr_put(b, v, 16);
    } else {
        jxr_put(b, 0xfb, 8);
        jxr_put(b, v, 32);
    }
}

static void write_plane_header(JxrImage *im, JxrPlane *p, JxrBits *b)
{
    jxr_put(b, (uint32_t)p->cf, 3);
    jxr_put(b, p->scaled, 1);
    jxr_put(b, (uint32_t)p->bands, 4);
    if (p->cf == MIME_JXR_CF_YUV444 || p->cf == MIME_JXR_CF_YUV420 || p->cf == MIME_JXR_CF_YUV422) {
        jxr_put(b, 0, 4);
        jxr_put(b, 0, 4);
    } else if (p->cf == MIME_JXR_CF_NCOMPONENT) {
        jxr_put(b, (uint32_t)p->ncomp - 1, 4);
        jxr_put(b, 0, 4);
    }
    if (im->bd == MIME_JXR_BD16 || im->bd == MIME_JXR_BD16S || im->bd == MIME_JXR_BD32S)
        jxr_put(b, (uint32_t)p->shift_bits, 8);
    if (im->bd == MIME_JXR_BD32F) {
        jxr_put(b, (uint32_t)p->len_mantissa, 8);
        jxr_put(b, (uint32_t)p->exp_bias & 0xff, 8);
    }
    jxr_put(b, 1, 1);
    jxr_qp_io(b, p->ncomp, 1, (uint8_t (*)[JXR_MAX_COMP])p->qp.dc);
    if (p->bands != BANDS_DCONLY) {
        jxr_put(b, 0, 1);
        jxr_put(b, 1, 1);
        jxr_qp_io(b, p->ncomp, 1, p->qp.lp);
        if (p->bands != BANDS_NOHP) {
            jxr_put(b, 0, 1);
            jxr_put(b, 1, 1);
            jxr_qp_io(b, p->ncomp, 1, p->qp.hp);
        }
    }
    jxr_align(b);
}

static void write_image_header(JxrImage *im, JxrBits *b)
{
    static const char sig[8] = "WMPHOTO";
    for (int i = 0; i < 8; i++)
        jxr_put(b, (uint8_t)sig[i], 8);
    jxr_put(b, 1, 4);
    jxr_put(b, im->hard_tiling, 1);
    jxr_put(b, 1, 3);
    jxr_put(b, im->tiling, 1);
    jxr_put(b, im->frequency, 1);
    jxr_put(b, (uint32_t)im->spatial_xfrm, 3);
    jxr_put(b, im->index_table, 1);
    jxr_put(b, (uint32_t)im->overlap, 2);
    jxr_put(b, im->short_header, 1);
    jxr_put(b, 1, 1);
    jxr_put(b, 0, 1);
    jxr_put(b, 0, 1);
    jxr_put(b, 0, 1);
    jxr_put(b, 0, 1);
    jxr_put(b, im->premultiplied, 1);
    jxr_put(b, im->alpha, 1);
    jxr_put(b, (uint32_t)im->out_cf, 4);
    jxr_put(b, (uint32_t)im->bd, 4);
    int wbits = im->short_header ? 16 : 32;
    jxr_put(b, im->width - 1, wbits);
    jxr_put(b, im->height - 1, wbits);
    if (im->tiling) {
        jxr_put(b, (uint32_t)im->ntx - 1, 12);
        jxr_put(b, (uint32_t)im->nty - 1, 12);
    }
    int tbits = im->short_header ? 8 : 16;
    for (int n = 0; n < im->ntx - 1; n++)
        jxr_put(b, im->tile_x[n + 1] - im->tile_x[n], tbits);
    for (int n = 0; n < im->nty - 1; n++)
        jxr_put(b, im->tile_y[n + 1] - im->tile_y[n], tbits);
    for (int k = 0; k < im->nplanes; k++)
        write_plane_header(im, &im->plane[k], b);
}

static void split(uint32_t *edges, int count, int mbs)
{
    edges[0] = 0;
    int rem = mbs;
    for (int k = 0; k < count - 1; k++) {
        int size = (rem + (count - k) - 1) / (count - k);
        edges[k + 1] = edges[k] + (uint32_t)size;
        rem -= size;
    }
    edges[count] = (uint32_t)mbs;
}

static int tile_count(int requested, int mbs)
{
    int n = requested ? requested : 1;
    if (n > mbs)
        n = 1;
    while ((mbs + n - 1) / n > 256)
        n++;
    return n > JXR_MAX_TILES ? JXR_MAX_TILES : n;
}

static int encode_image(JxrImage *im, uint8_t **out, size_t *out_size)
{
    int nbands = im->frequency ? 4 - im->plane[0].bands : 1;
    int tiles = im->ntx * im->nty;
    JxrBits *w = mime_alloc(im->alloc, (size_t)tiles * nbands * sizeof(JxrBits));
    if (!w)
        return MIME_JXR_ERR_NOMEM;
    for (int n = 0; n < tiles * nbands; n++)
        jxr_bits_writer(&w[n], im->alloc);
    int rc = MIME_JXR_OK;
    JxrMbCoef *m = &im->mb;
    for (int ty = 0; ty < im->nty; ty++)
        for (int tx = 0; tx < im->ntx; tx++) {
            JxrBits *tw = &w[(ty * im->ntx + tx) * nbands];
            for (int k = 0; k < nbands; k++) {
                jxr_put(&tw[k], 1, 24);
                jxr_put(&tw[k], (uint32_t)k, 8);
            }
            for (int mby = (int)im->tile_y[ty]; mby < (int)im->tile_y[ty + 1]; mby++)
                for (int mbx = (int)im->tile_x[tx]; mbx < (int)im->tile_x[tx + 1]; mbx++) {
                    JxrMbPos pos;
                    pos.mbx = mbx;
                    pos.mby = mby;
                    pos.mb = mby * im->mbw + mbx;
                    pos.left = mbx == (int)im->tile_x[tx];
                    pos.top = mby == (int)im->tile_y[ty];
                    int rel = mbx - (int)im->tile_x[tx];
                    pos.reset = mbx == (int)im->tile_x[tx + 1] - 1 || rel % 16 == 0;
                    pos.reset_totals = rel % 16 == 0;
                    for (int k = 0; k < im->nplanes; k++) {
                        JxrPlane *p = &im->plane[k];
                        encode_mb(im, p, &pos, m);
                        JxrBits *bdc = &tw[0];
                        JxrBits *blp = &tw[im->frequency ? 1 : 0];
                        JxrBits *bhp = &tw[im->frequency ? 2 : 0];
                        jxr_mb_dc(p, bdc, &pos, m);
                        if (p->bands != BANDS_DCONLY)
                            jxr_mb_lp(p, blp, &pos, m);
                        if (p->bands < BANDS_NOHP) {
                            jxr_mb_cbphp(im, p, bhp, &pos, m);
                            jxr_mb_hp(im, p, bhp, &pos, m, !im->frequency);
                            if (im->frequency && p->bands == BANDS_ALL)
                                jxr_mb_flex(im, p, &tw[3], &pos, m);
                        }
                    }
                }
            for (int k = 0; k < nbands; k++)
                jxr_align(&tw[k]);
        }
    JxrBits h;
    jxr_bits_writer(&h, im->alloc);
    write_image_header(im, &h);
    if (im->index_table) {
        jxr_put(&h, 1, 16);
        uint32_t offset = 0;
        /* the packets are laid out band by band, the index lists them tile by tile. */
        uint32_t *offsets = mime_alloc(im->alloc, (size_t)tiles * nbands * sizeof(uint32_t));
        if (!offsets) {
            rc = MIME_JXR_ERR_NOMEM;
            goto done;
        }
        for (int k = 0; k < nbands; k++)
            for (int n = 0; n < tiles; n++) {
                offsets[n * nbands + k] = offset;
                offset += (uint32_t)w[n * nbands + k].len;
            }
        for (int n = 0; n < tiles * nbands; n++)
            put_vlw(&h, offsets[n]);
        mime_free(im->alloc, offsets);
    }
    put_vlw(&h, 0);
    size_t total = h.len;
    for (int n = 0; n < tiles * nbands; n++) {
        if (w[n].error)
            h.error = true;
        total += w[n].len;
    }
    if (h.error) {
        rc = MIME_JXR_ERR_NOMEM;
        goto done;
    }
    uint8_t *buf = mime_alloc(im->alloc, total ? total : 1);
    if (!buf) {
        rc = MIME_JXR_ERR_NOMEM;
        goto done;
    }
    jxr_memcpy(buf, h.buf, h.len);
    size_t at = h.len;
    for (int k = 0; k < nbands; k++)
        for (int n = 0; n < tiles; n++) {
            JxrBits *t = &w[n * nbands + k];
            if (t->len)
                jxr_memcpy(buf + at, t->buf, t->len);
            at += t->len;
        }
    *out = buf;
    *out_size = total;
done:
    mime_free(im->alloc, h.buf);
    for (int n = 0; n < tiles * nbands; n++)
        mime_free(im->alloc, w[n].buf);
    mime_free(im->alloc, w);
    return rc;
}

static int plane_setup(JxrImage *im, JxrPlane *p)
{
    const MimeAllocator *a = im->alloc;
    bool sub = jxr_chroma_sub(p);
    p->cmbw = sub ? 8 : 16;
    p->cmbh = p->cf == MIME_JXR_CF_YUV420 ? 8 : 16;
    p->ccols = sub ? 2 : 4;
    p->cblocks = p->cf == MIME_JXR_CF_YUV420 ? 4 : p->cf == MIME_JXR_CF_YUV422 ? 8 : 16;
    p->cw = im->mbw * p->cmbw;
    p->chh = im->mbh * p->cmbh;
    size_t nmb = (size_t)im->mbw * im->mbh;
    for (int i = 0; i < p->ncomp; i++) {
        size_t n = (size_t)jxr_comp_w(im, p, i) * jxr_comp_h(im, p, i);
        if (n > ((size_t)1 << 31) / sizeof(int32_t))
            return MIME_JXR_ERR_UNSUPPORTED;
        p->c[i] = mime_alloc(a, n * sizeof(int32_t));
        if (!p->c[i])
            return MIME_JXR_ERR_NOMEM;
        jxr_memset(p->c[i], 0, n * sizeof(int32_t));
    }
    p->pred = mime_alloc(a, nmb * p->ncomp * 7 * sizeof(int32_t));
    p->cbphp = mime_alloc(a, nmb * p->ncomp * sizeof(uint16_t));
    p->lp_qp = mime_alloc(a, nmb * 6);
    if (!p->pred || !p->cbphp || !p->lp_qp)
        return MIME_JXR_ERR_NOMEM;
    p->hp_qp = p->lp_qp + nmb;
    p->hpmode = p->hp_qp + nmb;
    p->dcmode = p->hpmode + nmb;
    p->mbits = p->dcmode + nmb;
    jxr_memset(p->lp_qp, 0, nmb * 6);
    return MIME_JXR_OK;
}

static JxrImage *image_new(const MimeJxrEncodeParams *e, const MimeAllocator *alloc, const JxrLayout *l)
{
    JxrImage *im = mime_alloc(alloc, sizeof(*im));
    if (!im)
        return nullptr;
    jxr_memset(im, 0, sizeof(*im));
    im->alloc = alloc;
    im->width = e->width;
    im->height = e->height;
    im->bottom = e->height % 16 ? 16 - (int)(e->height % 16) : 0;
    im->right = e->width % 16 ? 16 - (int)(e->width % 16) : 0;
    im->ext_w = (int)e->width + im->right;
    im->ext_h = (int)e->height + im->bottom;
    im->mbw = im->ext_w / 16;
    im->mbh = im->ext_h / 16;
    im->out_cf = l->cf;
    im->bd = l->bd;
    im->spatial_xfrm = e->spatial_xfrm & 7;
    im->overlap = e->overlap > 2 ? 2 : e->overlap;
    im->short_header = im->ext_w <= 0xff0 && im->ext_h <= 0xff0;
    im->long_word = true;
    return im;
}

int mime_jxr_encode(const MimeJxrEncodeParams *e, const MimeAllocator *alloc, uint8_t **out, size_t *out_size,
                    uint8_t **alpha_out, size_t *alpha_size)
{
    *out = nullptr;
    *out_size = 0;
    if (alpha_out) {
        *alpha_out = nullptr;
        *alpha_size = 0;
    }
    JxrLayout l;
    if (!jxr_find_layout(e->layout, &l))
        return MIME_JXR_ERR_UNSUPPORTED;
    if (!e->width || !e->height || e->width > 0x10000000u || e->height > 0x10000000u || !e->pixels)
        return MIME_JXR_ERR_PARAM;
    bool planar = l.alpha && e->planar_alpha;
    if (planar && !alpha_out)
        return MIME_JXR_ERR_PARAM;
    JxrImage *im = image_new(e, alloc, &l);
    if (!im)
        return MIME_JXR_ERR_NOMEM;
    int cf;
    switch (l.cf) {
    case MIME_JXR_CF_RGB: {
        static const int sub[4] = {MIME_JXR_CF_YONLY, MIME_JXR_CF_YUV420, MIME_JXR_CF_YUV422, MIME_JXR_CF_YUV444};
        cf = sub[e->subsampling > 3 ? 3 : e->subsampling];
        if (l.bd == MIME_JXR_BD16F || l.bd == MIME_JXR_BD32F)
            cf = MIME_JXR_CF_YUV444;
        break;
    }
    case MIME_JXR_CF_RGBE:
        cf = MIME_JXR_CF_YUV444;
        break;
    case MIME_JXR_CF_CMYK:
        cf = MIME_JXR_CF_YUVK;
        break;
    default:
        cf = l.cf;
    }
    if ((cf == MIME_JXR_CF_YUV420 || cf == MIME_JXR_CF_YUV422) && im->overlap == 2 && im->mbw < 2)
        im->overlap = 1;
    im->ntx = tile_count(e->tile_cols ? e->tile_cols : (int)((e->width - 1) >> 8) + 1, im->mbw);
    im->nty = tile_count(e->tile_rows ? e->tile_rows : (int)((e->height - 1) >> 8) + 1, im->mbh);
    split(im->tile_x, im->ntx, im->mbw);
    split(im->tile_y, im->nty, im->mbh);
    im->tiling = im->ntx > 1 || im->nty > 1;
    im->hard_tiling = e->hard_tiling;
    im->frequency = e->frequency_mode;
    im->index_table = im->tiling || im->frequency;
    im->alpha = l.alpha && !planar;
    im->premultiplied = l.premul && im->alpha;
    im->nplanes = im->alpha ? 2 : 1;
    bool wide32 = l.bd == MIME_JXR_BD32S || l.bd == MIME_JXR_BD32F;
    JxrPlane *p = &im->plane[0];
    p->cf = cf;
    p->ncomp = cf == MIME_JXR_CF_YONLY ? 1 : cf == MIME_JXR_CF_YUVK ? 4 : cf == MIME_JXR_CF_NCOMPONENT ? l.nc : 3;
    p->bands = e->bands > 3 ? 3 : e->bands;
    p->scaled = (e->quality > 1 || p->bands != BANDS_ALL || jxr_chroma_sub(p)) && !wide32;
    p->shift_bits = l.bd == MIME_JXR_BD32S ? 8 : 0;
    p->len_mantissa = 13;
    p->exp_bias = 4;
    choose_qp(p, l.cf, e->quality);
    JxrPlane *ap = &im->plane[1];
    ap->alpha = true;
    ap->cf = MIME_JXR_CF_YONLY;
    ap->ncomp = 1;
    ap->bands = e->alpha_bands > 3 ? 3 : e->alpha_bands;
    if (ap->bands < p->bands && im->alpha)
        ap->bands = p->bands;
    ap->scaled = (e->alpha_quality > 1 || ap->bands != BANDS_ALL) && !wide32;
    ap->shift_bits = p->shift_bits;
    ap->len_mantissa = p->len_mantissa;
    ap->exp_bias = p->exp_bias;
    choose_qp(ap, MIME_JXR_CF_YONLY, e->alpha_quality);
    int rc = plane_setup(im, p);
    if (!rc && l.alpha)
        rc = plane_setup(im, ap);
    if (!rc)
        rc = fill_planes(im, e, &l);
    if (rc)
        goto done;
    if (planar) {
        /* the alpha plane becomes the luma of an image of its own. */
        JxrImage *aim = image_new(e, alloc, &l);
        if (!aim) {
            rc = MIME_JXR_ERR_NOMEM;
            goto done;
        }
        jxr_memcpy(aim->tile_x, im->tile_x, sizeof(im->tile_x));
        jxr_memcpy(aim->tile_y, im->tile_y, sizeof(im->tile_y));
        aim->ntx = im->ntx;
        aim->nty = im->nty;
        aim->tiling = im->tiling;
        aim->hard_tiling = im->hard_tiling;
        aim->frequency = im->frequency;
        aim->index_table = im->index_table;
        aim->out_cf = MIME_JXR_CF_YONLY;
        aim->nplanes = 1;
        aim->plane[0] = *ap;
        aim->plane[0].alpha = false;
        jxr_memset(ap, 0, sizeof(*ap));
        jxr_forward(aim, &aim->plane[0]);
        rc = encode_image(aim, alpha_out, alpha_size);
        jxr_image_free(aim);
        if (rc)
            goto done;
    }
    for (int k = 0; k < im->nplanes; k++)
        jxr_forward(im, &im->plane[k]);
    rc = encode_image(im, out, out_size);
    if (rc && planar) {
        mime_free(alloc, *alpha_out);
        *alpha_out = nullptr;
    }
done:
    jxr_image_free(im);
    return rc;
}
