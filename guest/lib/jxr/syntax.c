#include "internal.h"

typedef struct JxrCode {
    uint8_t code, len;
} JxrCode;

static const JxrCode k_dc_yuv[8] = {{0x2, 2}, {0x1, 3}, {0x1, 5}, {0x1, 4}, {0x3, 2}, {0x2, 3}, {0x0, 5}, {0x3, 3}};
static const JxrCode k_abs_level[2][7] = {
    {{0x1, 2}, {0x2, 2}, {0x3, 2}, {0x1, 3}, {0x1, 4}, {0x0, 5}, {0x1, 5}},
    {{0x1, 1}, {0x1, 2}, {0x1, 3}, {0x1, 4}, {0x1, 5}, {0x0, 6}, {0x1, 6}},
};
static const JxrCode k_cbplp444[8] = {{0x0, 1}, {0x4, 3}, {0xa, 4}, {0xb, 4}, {0xc, 4}, {0xd, 4}, {0xe, 4}, {0xf, 4}};
static const JxrCode k_cbplp420[4] = {{0x0, 1}, {0x2, 2}, {0x6, 3}, {0x7, 3}};
static const JxrCode k_num_cbphp[2][5] = {
    {{0x1, 1}, {0x1, 2}, {0x1, 3}, {0x0, 4}, {0x1, 4}},
    {{0x1, 1}, {0x0, 3}, {0x1, 3}, {0x2, 3}, {0x3, 3}},
};
static const JxrCode k_num_blk_cbphp[2][9] = {
    {{0x2, 3}, {0x0, 5}, {0x2, 4}, {0x1, 5}, {0x2, 5}, {0x1, 1}, {0x3, 3}, {0x3, 5}, {0x3, 4}},
    {{0x1, 1}, {0x1, 3}, {0x2, 3}, {0x1, 4}, {0x1, 6}, {0x3, 3}, {0x1, 5}, {0x0, 7}, {0x1, 7}},
};
static const JxrCode k_chr_cbphp[3] = {{0x1, 1}, {0x1, 2}, {0x0, 2}};
static const JxrCode k_num_ch_blk[4] = {{0x1, 1}, {0x1, 2}, {0x0, 3}, {0x1, 3}};
static const JxrCode k_ref_cbphp1[6] = {{0x0, 2}, {0x1, 2}, {0x4, 3}, {0x5, 3}, {0x6, 3}, {0x7, 3}};
static const uint8_t k_ref_cbphp1_value[6] = {3, 5, 6, 9, 10, 12};
static const JxrCode k_run2[2] = {{0x1, 1}, {0x0, 1}};
static const JxrCode k_run3[3] = {{0x1, 1}, {0x1, 2}, {0x0, 2}};
static const JxrCode k_run4[4] = {{0x1, 1}, {0x1, 2}, {0x1, 3}, {0x0, 3}};
static const JxrCode k_run_index[5] = {{0x1, 1}, {0x1, 2}, {0x1, 3}, {0x0, 4}, {0x1, 4}};
static const JxrCode k_index_a[4][6] = {
    {{0x1, 1}, {0x0, 5}, {0x1, 3}, {0x1, 5}, {0x1, 2}, {0x1, 4}},
    {{0x1, 2}, {0x0, 4}, {0x2, 2}, {0x1, 4}, {0x3, 2}, {0x1, 3}},
    {{0x0, 4}, {0x1, 4}, {0x1, 2}, {0x2, 2}, {0x3, 2}, {0x1, 3}},
    {{0x0, 5}, {0x1, 5}, {0x1, 2}, {0x1, 1}, {0x1, 4}, {0x1, 3}},
};
static const JxrCode k_index_b[4] = {{0x0, 1}, {0x6, 3}, {0x2, 2}, {0x7, 3}};
static const JxrCode k_first_index[5][12] = {
    {{0x01, 5}, {0x01, 6}, {0x00, 7}, {0x01, 7}, {0x04, 5}, {0x2, 3}, {0x05, 5}, {0x1, 1}, {0x06, 5}, {0x1, 4},
     {0x07, 5}, {0x3, 3}},
    {{0x2, 4}, {0x02, 5}, {0x00, 6}, {0x01, 6}, {0x3, 4}, {0x2, 3}, {0x03, 5}, {0x3, 2}, {0x3, 3}, {0x4, 3},
     {0x01, 5}, {0x5, 3}},
    {{0x3, 2}, {0x1, 3}, {0x00, 7}, {0x01, 7}, {0x01, 5}, {0x2, 3}, {0x02, 7}, {0x3, 3}, {0x4, 3}, {0x5, 3},
     {0x03, 7}, {0x1, 4}},
    {{0x1, 3}, {0x3, 2}, {0x00, 7}, {0x01, 5}, {0x02, 5}, {0x2, 3}, {0x01, 7}, {0x3, 3}, {0x03, 5}, {0x4, 3},
     {0x01, 6}, {0x5, 3}},
    {{0x2, 3}, {0x1, 1}, {0x01, 7}, {0x1, 4}, {0x02, 7}, {0x3, 3}, {0x00, 8}, {0x2, 4}, {0x03, 7}, {0x3, 4},
     {0x01, 8}, {0x01, 5}},
};

static const int8_t k_abs_level_delta[7] = {1, 0, -1, -1, -1, -1, -1};
static const int8_t k_first_index_delta[4][12] = {
    {1, 1, 1, 1, 1, 0, 0, -1, 2, 1, 0, 0},
    {2, 2, -1, -1, -1, 0, -2, -1, 0, 0, -2, -1},
    {-1, 1, 0, 2, 0, 0, 0, 0, -2, 0, 1, 1},
    {0, 1, 0, 1, -2, 0, -1, -1, -2, -1, -2, -2},
};
static const int8_t k_index_delta[3][6] = {
    {-1, 1, 1, 1, 0, 1},
    {-2, 0, 0, 2, 0, 0},
    {-1, -1, 0, 1, -2, 0},
};
static const int8_t k_num_cbphp_delta[5] = {0, -1, 0, 1, 1};
static const int8_t k_num_blk_cbphp_delta5[5] = {0, -1, 0, 1, 1};
static const int8_t k_num_blk_cbphp_delta9[9] = {2, 2, 1, 1, -1, -2, -2, -2, -3};

static const uint8_t k_scan0[16] = {0, 4, 1, 5, 8, 2, 9, 6, 12, 3, 10, 13, 7, 14, 11, 15};
static const uint8_t k_scan1[16] = {0, 1, 2, 5, 4, 3, 6, 9, 8, 7, 12, 15, 13, 10, 11, 14};
static const uint8_t k_transpose444[16] = {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15};
static const uint8_t k_transpose422[8] = {0, 2, 1, 3, 4, 6, 5, 7};
static const uint8_t k_transpose420[4] = {0, 2, 1, 3};
static const uint8_t k_hier[16] = {0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15};

static int iabs(int v) { return v < 0 ? -v : v; }

/* ------------------------------------------------------------------ codes */

static int vlc_io(JxrBits *b, const JxrCode *t, int count, int v)
{
    if (b->enc) {
        jxr_put(b, t[v].code, t[v].len);
        return v;
    }
    uint32_t code = 0;
    for (int len = 1; len <= 8; len++) {
        code = (code << 1) | jxr_get(b, 1);
        for (int i = 0; i < count; i++)
            if (t[i].len == len && t[i].code == code)
                return i;
        if (b->error)
            break;
    }
    b->error = true;
    return 0;
}

static void adapt_init1(JxrAdapt *a)
{
    a->table = 0;
    a->delta = 0;
    a->d1 = 0;
}

static void adapt_init2(JxrAdapt *a)
{
    a->table = 1;
    a->delta = 0;
    a->delta2 = 1;
    a->d1 = 0;
    a->d2 = 0;
}

static int clip64(int v) { return v < -64 ? -64 : v > 64 ? 64 : v; }

static void adapt_vlc1(JxrAdapt *a)
{
    if (a->d1 < -8 && a->table != 0) {
        a->table--;
        a->d1 = 0;
    } else if (a->d1 > 8 && a->table != 1) {
        a->table++;
        a->d1 = 0;
    } else {
        a->d1 = clip64(a->d1);
    }
}

static void adapt_vlc2(JxrAdapt *a, int max)
{
    bool change = false;
    if (a->d1 < -8 && a->table != 0) {
        a->table--;
        change = true;
    } else if (a->d2 > 8 && a->table != max) {
        a->table++;
        change = true;
    }
    if (change) {
        a->d1 = a->d2 = 0;
        if (a->table == max) {
            a->delta = a->delta2 = a->table - 1;
        } else if (a->table == 0) {
            a->delta = a->delta2 = 0;
        } else {
            a->delta = a->table - 1;
            a->delta2 = a->table;
        }
    } else {
        a->d1 = clip64(a->d1);
        a->d2 = clip64(a->d2);
    }
}

static void model_init(JxrModel *m, int band)
{
    m->state[0] = m->state[1] = 0;
    m->bits[0] = m->bits[1] = (2 - band) * 4;
}

static void model_update(const JxrPlane *p, JxrModel *m, int band, int lap[2])
{
    static const int w0[3] = {240, 12, 1};
    static const int w1[3][16] = {
        {0, 240, 120, 80, 60, 48, 40, 34, 30, 27, 24, 22, 20, 18, 17, 16},
        {0, 12, 6, 4, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1},
        {0, 16, 8, 5, 4, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1},
    };
    static const int w2[6] = {120, 37, 2, 120, 18, 1};
    lap[0] *= w0[band];
    if (p->cf == MIME_JXR_CF_YUV420) {
        lap[1] *= w2[band];
    } else if (p->cf == MIME_JXR_CF_YUV422) {
        lap[1] *= w2[3 + band];
    } else {
        lap[1] *= w1[band][p->ncomp - 1];
        if (band == 2)
            lap[1] >>= 4;
    }
    int models = p->cf == MIME_JXR_CF_YONLY ? 1 : 2;
    for (int j = 0; j < models; j++) {
        int ms = m->state[j];
        int delta = (lap[j] - 70) >> 2;
        if (delta <= -8) {
            delta += 4;
            if (delta < -16)
                delta = -16;
            ms += delta;
            if (ms < -8) {
                if (m->bits[j] == 0) {
                    ms = -8;
                } else {
                    ms = 0;
                    m->bits[j]--;
                }
            }
        } else if (delta >= 8) {
            delta -= 4;
            if (delta > 15)
                delta = 15;
            ms += delta;
            if (ms > 8) {
                if (m->bits[j] >= 15) {
                    m->bits[j] = 15;
                    ms = 8;
                } else {
                    ms = 0;
                    m->bits[j]++;
                }
            }
        }
        m->state[j] = ms;
    }
}

/* the level of a coefficient whose magnitude is at least 2. */
static int level_io(JxrBits *b, JxrAdapt *a, int level)
{
    static const int remap[6] = {2, 3, 4, 6, 10, 14};
    static const int fixed_len[6] = {0, 0, 1, 2, 2, 2};
    int idx = 6;
    if (b->enc) {
        for (int i = 0; i < 6; i++)
            if (level >= remap[i] && level < remap[i] + (1 << fixed_len[i]))
                idx = i;
    }
    idx = vlc_io(b, k_abs_level[a->table], 7, idx);
    a->d1 += k_abs_level_delta[idx];
    if (idx < 6) {
        int v = remap[idx];
        if (fixed_len[idx])
            v += (int)jxr_io(b, fixed_len[idx], (uint32_t)(level - remap[idx]));
        return v;
    }
    int fixed = 0;
    if (b->enc) {
        uint32_t d = (uint32_t)(level - 2);
        while (fixed < 31 && (d >> (fixed + 1)))
            fixed++;
    }
    int num = (int)jxr_io(b, 4, (uint32_t)(fixed >= 19 ? 15 : fixed - 4));
    int f = num + 4;
    if (f == 19) {
        f += (int)jxr_io(b, 2, (uint32_t)(fixed >= 22 ? 3 : fixed - 19));
        if (f == 22)
            f += (int)jxr_io(b, 3, (uint32_t)(fixed - 22));
    }
    uint32_t ref = jxr_io(b, f, b->enc ? (uint32_t)(level - 2) - (1u << f) : 0);
    return 2 + (int)(1u << f) + (int)ref;
}

static int run_io(JxrBits *b, int max_run, int run)
{
    static const int remap[15] = {1, 2, 3, 5, 7, 1, 2, 3, 5, 7, 1, 2, 3, 4, 5};
    static const int bin[15] = {-1, -1, -1, -1, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0};
    static const int fixed_len[15] = {0, 0, 1, 1, 3, 0, 0, 1, 1, 2, 0, 0, 0, 0, 1};
    if (max_run < 5) {
        if (max_run == 1)
            return 1;
        const JxrCode *t = max_run == 2 ? k_run2 : max_run == 3 ? k_run3 : k_run4;
        return vlc_io(b, t, max_run, run - 1) + 1;
    }
    if (max_run > 14) {
        b->error = true;
        return 1;
    }
    int base = 5 * bin[max_run];
    int ri = 0;
    if (b->enc) {
        for (int i = 0; i < 5; i++)
            if (run >= remap[base + i] && run < remap[base + i] + (1 << fixed_len[base + i]))
                ri = i;
    }
    ri = vlc_io(b, k_run_index, 5, ri);
    int idx = ri + base;
    int v = remap[idx];
    if (fixed_len[idx])
        v += (int)jxr_io(b, fixed_len[idx], (uint32_t)(run - remap[idx]));
    return v;
}

static int first_index_io(JxrBits *b, JxrAdapt *a, int v)
{
    v = vlc_io(b, k_first_index[a->table], 12, v);
    a->d1 += k_first_index_delta[a->delta][v];
    a->d2 += k_first_index_delta[a->delta2][v];
    return v;
}

static int index_io(JxrBits *b, JxrAdapt *a, int location, int v)
{
    if (location < 15) {
        v = vlc_io(b, k_index_a[a->table], 6, v);
        a->d1 += k_index_delta[a->delta][v];
        a->d2 += k_index_delta[a->delta2][v];
        return v;
    }
    if (location == 15)
        return vlc_io(b, k_index_b, 4, v);
    return (int)jxr_io(b, 1, (uint32_t)v);
}

/*
 * one block of run level coded values, count = 16 - location of them, in scan order. the encoder reads the values,
 * the decoder writes them. returns the count of nonzero values.
 */
static int block_io(JxrBits *b, JxrCtx *x, bool chroma, int band, int location, int32_t *v)
{
    int count = 16 - location;
    int pos[16], n = 0;
    if (b->enc) {
        for (int i = 0; i < count; i++)
            if (v[i])
                pos[n++] = i;
        if (!n)
            return 0;
    }
    JxrAdapt *first = band == BAND_LP ? &x->first_lp[chroma] : &x->first_hp[chroma];
    JxrAdapt *abs_ctx = band == BAND_LP ? x->abs_lp : x->abs_hp;
    JxrAdapt (*ind)[2] = band == BAND_LP ? x->ind_lp : x->ind_hp;
    int next_srn = 0, level = 0;
    if (b->enc) {
        level = iabs(v[pos[0]]);
        next_srn = n == 1 ? 0 : pos[1] - pos[0] == 1 ? 1 : 2;
    }
    int fi = first_index_io(b, first, b->enc ? (pos[0] == 0) | ((level > 1) << 1) | (next_srn << 2) : 0);
    int sr = fi & 1, srn = fi >> 2;
    int ctx = sr & srn;
    int sign = (int)jxr_io(b, 1, b->enc && v[pos[0]] < 0);
    level = fi & 2 ? level_io(b, &abs_ctx[ctx], level) : 1;
    int run = 0;
    if (!sr)
        run = run_io(b, 15 - location, b->enc ? pos[0] : 0);
    location += run + 1;
    if (location > 16) {
        b->error = true;
        return 0;
    }
    if (!b->enc)
        v[location - 1 - (16 - count)] = sign ? -level : level;
    int k = 1;
    while (srn) {
        if (k >= count) {
            b->error = true;
            return k;
        }
        sr = srn & 1;
        run = 0;
        if (!sr)
            run = run_io(b, 15 - location, b->enc ? pos[k] - pos[k - 1] - 1 : 0);
        location += run + 1;
        if (location > 16) {
            b->error = true;
            return k;
        }
        level = 0;
        next_srn = 0;
        if (b->enc) {
            level = iabs(v[pos[k]]);
            next_srn = k == n - 1 ? 0 : pos[k + 1] - pos[k] == 1 ? 1 : 2;
        }
        int idx = index_io(b, &ind[chroma][ctx], location, (level > 1) | (next_srn << 1));
        srn = idx >> 1;
        ctx &= srn;
        sign = (int)jxr_io(b, 1, b->enc && v[pos[k]] < 0);
        level = idx & 1 ? level_io(b, &abs_ctx[ctx], level) : 1;
        if (!b->enc)
            v[location - 1 - (16 - count)] = sign ? -level : level;
        k++;
        if (b->error)
            break;
    }
    return k;
}

/* the part of a value above the model bits, signed. */
static int32_t high_part(int32_t v, int m) { return v < 0 ? -(int32_t)((uint32_t)-v >> m) : (int32_t)((uint32_t)v >> m); }

/* refinement of a value whose high part is known: the low bits and the sign of a zero high part. */
static int32_t refine_io(JxrBits *b, int32_t v, int m)
{
    if (b->enc) {
        uint32_t a = (uint32_t)iabs(v);
        uint32_t ref = a & ((1u << m) - 1);
        jxr_put(b, ref, m);
        if (!(a >> m) && ref)
            jxr_put(b, v < 0, 1);
        return v;
    }
    int32_t ref = (int32_t)jxr_get(b, m);
    if (v > 0)
        return (v << m) + ref;
    if (v < 0)
        return (int32_t)((uint32_t)v << m) - ref;
    if (ref && jxr_get(b, 1))
        return -ref;
    return ref;
}

static void scan_init(int *order, int *totals, const uint8_t *init)
{
    static const int k_totals[16] = {0, 32, 30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4};
    for (int i = 1; i < 16; i++) {
        order[i] = init[i];
        totals[i] = k_totals[i];
    }
}

static void scan_reset(int *totals)
{
    static const int k_totals[16] = {0, 32, 30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4};
    for (int i = 1; i < 16; i++)
        totals[i] = k_totals[i];
}

static void scan_update(int *order, int *totals, int i)
{
    totals[i]++;
    if (i > 1 && totals[i] > totals[i - 1]) {
        int t = totals[i];
        totals[i] = totals[i - 1];
        totals[i - 1] = t;
        t = order[i];
        order[i] = order[i - 1];
        order[i - 1] = t;
    }
}

int jxr_num_lp(const JxrPlane *p, int i)
{
    if (i && p->cf == MIME_JXR_CF_YUV420)
        return 3;
    if (i && p->cf == MIME_JXR_CF_YUV422)
        return 7;
    return 15;
}

int jxr_quant_map(int qp, bool scaled, int shift)
{
    if (qp == 0)
        return 1;
    int man, exp;
    if (!scaled) {
        if (qp < 32) {
            man = (qp + 3) >> 2;
            exp = 0;
        } else if (qp < 48) {
            man = (16 + (qp % 16) + 1) >> 1;
            exp = (qp >> 4) - 2;
        } else {
            man = 16 + (qp % 16);
            exp = (qp >> 4) - 3;
        }
    } else if (qp < 16) {
        man = qp;
        exp = shift;
    } else {
        man = 16 + (qp % 16);
        exp = (qp >> 4) - 1 + shift;
    }
    return man << exp;
}

/* ------------------------------------------------------------------ macroblock syntax */

static bool per_channel(const JxrPlane *p)
{
    return p->cf == MIME_JXR_CF_YONLY || p->cf == MIME_JXR_CF_YUVK || p->cf == MIME_JXR_CF_NCOMPONENT;
}

static int32_t dc_io(JxrBits *b, JxrAdapt *a, int m, bool abs_level, int32_t v)
{
    uint32_t mag = (uint32_t)iabs(v);
    int32_t dc = 0;
    if (abs_level)
        dc = level_io(b, a, (int)(mag >> m) + 1) - 1;
    if (m)
        dc = (int32_t)(((uint32_t)dc << m) | jxr_io(b, m, mag & ((1u << m) - 1)));
    if (dc && jxr_io(b, 1, v < 0))
        dc = -dc;
    return dc;
}

void jxr_mb_dc(JxrPlane *p, JxrBits *b, const JxrMbPos *pos, JxrMbCoef *m)
{
    JxrCtx *x = &p->ctx;
    if (pos->left && pos->top) {
        adapt_init1(&x->abs_dc[0]);
        adapt_init1(&x->abs_dc[1]);
        model_init(&x->model_dc, 0);
    }
    int lap[2] = {0, 0};
    if (per_channel(p)) {
        for (int n = 0; n < p->ncomp; n++) {
            int mi = n != 0;
            int bits = x->model_dc.bits[mi];
            bool nz = jxr_io(b, 1, b->enc && ((uint32_t)iabs(m->dc[n]) >> bits) != 0);
            if (nz)
                lap[mi]++;
            m->dc[n] = dc_io(b, &x->abs_dc[0], bits, nz, m->dc[n]);
        }
    } else {
        int flags = 0;
        if (b->enc) {
            flags = (((uint32_t)iabs(m->dc[0]) >> x->model_dc.bits[0]) != 0) << 2 |
                    (((uint32_t)iabs(m->dc[1]) >> x->model_dc.bits[1]) != 0) << 1 |
                    (((uint32_t)iabs(m->dc[2]) >> x->model_dc.bits[1]) != 0);
        }
        flags = vlc_io(b, k_dc_yuv, 8, flags);
        for (int n = 0; n < 3; n++) {
            bool nz = (flags >> (2 - n)) & 1;
            int mi = n != 0;
            if (nz)
                lap[mi]++;
            m->dc[n] = dc_io(b, &x->abs_dc[mi], x->model_dc.bits[mi], nz, m->dc[n]);
        }
    }
    model_update(p, &x->model_dc, 0, lap);
    if (pos->reset) {
        adapt_vlc1(&x->abs_dc[0]);
        adapt_vlc1(&x->abs_dc[1]);
    }
}

void jxr_mb_lp(JxrPlane *p, JxrBits *b, const JxrMbPos *pos, JxrMbCoef *m)
{
    JxrCtx *x = &p->ctx;
    if (pos->left && pos->top) {
        x->count_zero_cbplp = 1;
        x->count_max_cbplp = 1;
        for (int i = 0; i < 2; i++) {
            adapt_init2(&x->first_lp[i]);
            adapt_init2(&x->ind_lp[i][0]);
            adapt_init2(&x->ind_lp[i][1]);
            adapt_init1(&x->abs_lp[i]);
        }
        scan_init(x->lp_order, x->lp_totals, k_scan0);
        model_init(&x->model_lp, 1);
    }
    if (pos->reset_totals)
        scan_reset(x->lp_totals);
    int lap[2] = {0, 0};
    bool sub = jxr_chroma_sub(p);
    int full = sub ? 2 : p->ncomp;
    int32_t hi[JXR_MAX_COMP][16];
    int cbplp = 0;
    if (b->enc) {
        for (int n = 0; n < p->ncomp; n++) {
            int bits = x->model_lp.bits[n != 0];
            for (int j = 0; j < 16; j++)
                hi[n][j] = j ? high_part(m->lp[n][j], bits) : 0;
            for (int j = 1; j < 16; j++)
                if (hi[n][j])
                    cbplp |= 1 << (sub && n ? 1 : n);
        }
    } else {
        jxr_memset(hi, 0, sizeof(hi[0]) * p->ncomp);
    }
    if (p->cf == MIME_JXR_CF_YUV420 || p->cf == MIME_JXR_CF_YUV422 || p->cf == MIME_JXR_CF_YUV444) {
        int max = full * 4 - 5;
        if (x->count_zero_cbplp <= 0 || x->count_max_cbplp < 0) {
            bool flip = x->count_max_cbplp < x->count_zero_cbplp;
            int v = flip ? max - cbplp : cbplp;
            v = full == 3 ? vlc_io(b, k_cbplp444, 8, v) : vlc_io(b, k_cbplp420, 4, v);
            cbplp = flip ? max - v : v;
        } else {
            cbplp = (int)jxr_io(b, full, (uint32_t)cbplp);
        }
        x->count_zero_cbplp += 1 - 4 * (cbplp == 0);
        x->count_zero_cbplp = x->count_zero_cbplp < -8 ? -8 : x->count_zero_cbplp > 7 ? 7 : x->count_zero_cbplp;
        x->count_max_cbplp += 1 - 4 * (cbplp == max);
        x->count_max_cbplp = x->count_max_cbplp < -8 ? -8 : x->count_max_cbplp > 7 ? 7 : x->count_max_cbplp;
    } else {
        for (int n = 0; n < p->ncomp; n++)
            if (jxr_io(b, 1, (uint32_t)(cbplp >> n) & 1))
                cbplp |= 1 << n;
    }
    for (int n = 0; n < full; n++) {
        int idx = n != 0;
        int nnz = 0;
        if ((cbplp >> n) & 1) {
            int32_t v[16];
            jxr_memset(v, 0, sizeof(v));
            if (sub && n) {
                static const uint8_t remap[7] = {4, 1, 2, 3, 5, 6, 7};
                bool is420 = p->cf == MIME_JXR_CF_YUV420;
                int off = is420 ? 1 : 0, count = is420 ? 6 : 14;
                const uint8_t *tr = is420 ? k_transpose420 : k_transpose422;
                if (b->enc)
                    for (int k = 0; k < count; k++)
                        v[k] = hi[(k & 1) + 1][tr[remap[(k >> 1) + off]]];
                nnz = block_io(b, x, true, BAND_LP, is420 ? 10 : 2, v);
                if (!b->enc)
                    for (int k = 0; k < count; k++)
                        hi[(k & 1) + 1][tr[remap[(k >> 1) + off]]] = v[k];
            } else {
                if (b->enc)
                    for (int i = 1; i < 16; i++)
                        v[i - 1] = hi[n][x->lp_order[i]];
                nnz = block_io(b, x, idx, BAND_LP, 1, v);
                for (int i = 1; i < 16; i++) {
                    if (!v[i - 1])
                        continue;
                    if (!b->enc)
                        hi[n][x->lp_order[i]] = v[i - 1];
                    scan_update(x->lp_order, x->lp_totals, i);
                }
            }
        }
        int bits = x->model_lp.bits[idx];
        lap[idx] += nnz;
        if (sub && n) {
            bool is420 = p->cf == MIME_JXR_CF_YUV420;
            int count = is420 ? 4 : 8;
            const uint8_t *tr = is420 ? k_transpose420 : k_transpose422;
            for (int c = 1; c <= 2; c++)
                if (!b->enc)
                    for (int k = 1; k < count; k++)
                        m->lp[c][tr[k]] = hi[c][tr[k]];
            if (bits)
                for (int k = 1; k < count; k++)
                    for (int c = 1; c <= 2; c++) {
                        int j = tr[k];
                        m->lp[c][j] = refine_io(b, b->enc ? m->lp[c][j] : hi[c][j], bits);
                    }
        } else {
            if (!b->enc)
                for (int k = 1; k < 16; k++)
                    m->lp[n][k] = hi[n][k];
            if (bits)
                for (int k = 1; k < 16; k++) {
                    int j = k_transpose444[k];
                    m->lp[n][j] = refine_io(b, b->enc ? m->lp[n][j] : hi[n][j], bits);
                }
        }
    }
    model_update(p, &x->model_lp, 1, lap);
    if (pos->reset) {
        for (int i = 0; i < 2; i++) {
            adapt_vlc2(&x->first_lp[i], 4);
            adapt_vlc2(&x->ind_lp[i][0], 3);
            adapt_vlc2(&x->ind_lp[i][1], 3);
        }
        adapt_vlc1(&x->abs_lp[0]);
        adapt_vlc1(&x->abs_lp[1]);
    }
}

static uint32_t popcount(uint32_t v)
{
    uint32_t n = 0;
    for (; v; v &= v - 1)
        n++;
    return n;
}

/* which blocks of a 2x2 group are coded, given their count. */
static uint32_t refine_cbphp_io(JxrBits *b, int num, uint32_t pattern)
{
    if (num == 2) {
        int idx = 0;
        if (b->enc)
            for (int i = 0; i < 6; i++)
                if (k_ref_cbphp1_value[i] == pattern)
                    idx = i;
        return k_ref_cbphp1_value[vlc_io(b, k_ref_cbphp1, 6, idx)];
    }
    if (num == 1) {
        uint32_t r = 0;
        while (b->enc && r < 3 && !((pattern >> r) & 1))
            r++;
        return 1u << jxr_io(b, 2, r);
    }
    if (num == 3) {
        uint32_t r = 0;
        while (b->enc && r < 3 && ((pattern >> r) & 1))
            r++;
        return 0xf ^ (1u << jxr_io(b, 2, r));
    }
    return num == 4 ? 0xf : 0;
}

/*
 * the coded block pattern prediction. every step reads bits it does not write, so the encoder undoes the decoder's
 * steps by running them backwards on the actual pattern.
 */
static uint32_t cbphp_predict(JxrPlane *p, int kind, int c1, uint32_t v, uint32_t neighbour, bool enc)
{
    JxrCtx *x = &p->ctx;
    if (x->cbphp_state[c1] == 0) {
        if (kind == 0) {
            if (!enc) {
                v ^= neighbour;
                v ^= 0x02 & (v << 1);
                v ^= 0x10 & (v << 3);
                v ^= 0x20 & (v << 1);
                v ^= (v & 0x33) << 2;
                v ^= (v & 0xcc) << 6;
                v ^= (v & 0x3300) << 2;
            } else {
                v ^= (v & 0x3300) << 2;
                v ^= (v & 0xcc) << 6;
                v ^= (v & 0x33) << 2;
                v ^= 0x20 & (v << 1);
                v ^= 0x10 & (v << 3);
                v ^= 0x02 & (v << 1);
                v ^= neighbour;
            }
        } else if (kind == 2) {
            if (!enc) {
                v ^= neighbour;
                v ^= (v & 0x01) << 1;
                v ^= (v & 0x03) << 2;
                v ^= (v & 0x0c) << 2;
                v ^= (v & 0x30) << 2;
            } else {
                v ^= (v & 0x30) << 2;
                v ^= (v & 0x0c) << 2;
                v ^= (v & 0x03) << 2;
                v ^= (v & 0x01) << 1;
                v ^= neighbour;
            }
        } else {
            if (!enc) {
                v ^= neighbour;
                v ^= 0x02 & (v << 1);
                v ^= (v & 0x3) << 2;
            } else {
                v ^= (v & 0x3) << 2;
                v ^= 0x02 & (v << 1);
                v ^= neighbour;
            }
        }
    } else if (x->cbphp_state[c1] == 2) {
        v ^= kind == 0 ? 0xffff : kind == 2 ? 0xff : 0xf;
    }
    return v;
}

static void cbphp_model_update(JxrCtx *x, int i, int norig)
{
    x->count_ones[i] += norig - 3;
    x->count_ones[i] = x->count_ones[i] < -16 ? -16 : x->count_ones[i] > 15 ? 15 : x->count_ones[i];
    x->count_zeroes[i] += 16 - norig - 3;
    x->count_zeroes[i] = x->count_zeroes[i] < -16 ? -16 : x->count_zeroes[i] > 15 ? 15 : x->count_zeroes[i];
    if (x->count_ones[i] < 0)
        x->cbphp_state[i] = x->count_ones[i] < x->count_zeroes[i] ? 1 : 2;
    else if (x->count_zeroes[i] < 0)
        x->cbphp_state[i] = 2;
    else
        x->cbphp_state[i] = 0;
}

/* turns differential patterns into actual ones, or the other way around for the encoder. */
static void cbphp_pred_all(JxrImage *im, JxrPlane *p, const JxrMbPos *pos, uint32_t *v, bool enc)
{
    JxrCtx *x = &p->ctx;
    if (pos->left && pos->top) {
        x->cbphp_state[0] = x->cbphp_state[1] = 0;
        x->count_ones[0] = x->count_ones[1] = -4;
        x->count_zeroes[0] = x->count_zeroes[1] = 4;
    }
    bool sub = jxr_chroma_sub(p);
    int n444 = sub ? 1 : p->ncomp;
    int mbw = im->mbw;
    for (int i = 0; i < p->ncomp; i++) {
        int kind = i < n444 ? 0 : p->cf == MIME_JXR_CF_YUV422 ? 2 : 1;
        int c1 = i > 0;
        static const int top_shift[3] = {10, 2, 6}, left_shift[3] = {5, 1, 1};
        uint32_t nb;
        if (pos->left)
            nb = pos->top ? 1 : (p->cbphp[(size_t)(pos->mb - mbw) * p->ncomp + i] >> top_shift[kind]) & 1;
        else
            nb = (p->cbphp[(size_t)(pos->mb - 1) * p->ncomp + i] >> left_shift[kind]) & 1;
        uint32_t actual_in = v[i];
        v[i] = cbphp_predict(p, kind, c1, v[i], nb, enc);
        uint32_t actual = enc ? actual_in : v[i];
        int mul = kind == 0 ? 1 : kind == 2 ? 2 : 4;
        cbphp_model_update(x, c1, (int)popcount(actual) * mul);
    }
}

static int luma_class(uint32_t l)
{
    static const uint8_t cls[16] = {0, 1, 1, 2, 1, 3, 3, 4, 1, 3, 3, 4, 2, 4, 4, 5};
    return cls[l];
}

void jxr_mb_cbphp(JxrImage *im, JxrPlane *p, JxrBits *b, const JxrMbPos *pos, JxrMbCoef *m)
{
    static const int flc[6] = {0, 2, 1, 2, 2, 0};
    static const int off[6] = {0, 4, 2, 8, 12, 1};
    static const uint8_t out[16] = {0, 15, 3, 12, 1, 2, 4, 8, 5, 6, 9, 10, 7, 11, 13, 14};
    static const uint8_t out_inv[16] = {0, 4, 5, 2, 6, 8, 9, 12, 7, 10, 11, 13, 3, 14, 15, 1};
    static const int shift422[4] = {0, 1, 4, 5};
    JxrCtx *x = &p->ctx;
    if (pos->left && pos->top) {
        adapt_init1(&x->num_cbphp);
        adapt_init1(&x->num_blk_cbphp);
    }
    uint32_t diff[JXR_MAX_COMP];
    jxr_memset(diff, 0, sizeof(diff));
    if (b->enc) {
        for (int i = 0; i < p->ncomp; i++) {
            /* the hp model starts over in jxr_mb_hp at the first macroblock of a tile. */
            int bits = pos->left && pos->top ? 0 : x->model_hp.bits[i != 0];
            int nb = jxr_blocks(p, i);
            uint32_t pat = 0;
            for (int blk = 0; blk < nb; blk++) {
                int map = nb == 16 ? k_hier[blk] : blk;
                for (int j = 1; j < 16; j++)
                    if (high_part(m->hp[i][map][j], bits)) {
                        pat |= 1u << blk;
                        break;
                    }
            }
            diff[i] = pat;
            p->cbphp[(size_t)pos->mb * p->ncomp + i] = (uint16_t)pat;
        }
        cbphp_pred_all(im, p, pos, diff, true);
    }
    bool ch = per_channel(p);
    int icomp = ch ? p->ncomp : 1;
    for (int i = 0; i < icomp; i++) {
        uint32_t groups = 0;
        if (b->enc) {
            for (int g = 0; g < 4; g++) {
                uint32_t any = (diff[i] >> (4 * g)) & 0xf;
                if (!ch) {
                    if (p->cf == MIME_JXR_CF_YUV444)
                        any |= ((diff[1] >> (4 * g)) & 0xf) | ((diff[2] >> (4 * g)) & 0xf);
                    else if (p->cf == MIME_JXR_CF_YUV422)
                        any |= ((diff[1] | diff[2]) >> shift422[g]) & 0x5;
                    else
                        any |= ((diff[1] | diff[2]) >> g) & 1;
                }
                if (any)
                    groups |= 1u << g;
            }
        }
        int num = vlc_io(b, k_num_cbphp[x->num_cbphp.table], 5, (int)popcount(groups));
        x->num_cbphp.d1 += k_num_cbphp_delta[num];
        groups = refine_cbphp_io(b, num, groups);
        for (int g = 0; g < 4; g++) {
            if (!((groups >> g) & 1))
                continue;
            uint32_t luma = (diff[i] >> (4 * g)) & 0xf;
            uint32_t u = 0, v = 0;
            if (b->enc && !ch) {
                if (p->cf == MIME_JXR_CF_YUV444) {
                    u = (diff[1] >> (4 * g)) & 0xf;
                    v = (diff[2] >> (4 * g)) & 0xf;
                } else if (p->cf == MIME_JXR_CF_YUV422) {
                    u = (diff[1] >> shift422[g]) & 0x5;
                    v = (diff[2] >> shift422[g]) & 0x5;
                } else {
                    u = (diff[1] >> g) & 1;
                    v = (diff[2] >> g) & 1;
                }
            }
            int chr = (u != 0) | ((v != 0) << 1);
            int num_blk = 0, val_inc = 0;
            if (b->enc) {
                if (!chr) {
                    num_blk = luma_class(luma) - 1;
                } else {
                    int c = luma_class(luma);
                    if (c < 3) {
                        num_blk = c + 5;
                    } else {
                        num_blk = 8;
                        val_inc = c - 3;
                    }
                }
            }
            if (ch) {
                num_blk = vlc_io(b, k_num_cbphp[x->num_blk_cbphp.table], 5, num_blk);
                x->num_blk_cbphp.d1 += k_num_blk_cbphp_delta5[num_blk];
            } else {
                num_blk = vlc_io(b, k_num_blk_cbphp[x->num_blk_cbphp.table], 9, num_blk);
                x->num_blk_cbphp.d1 += k_num_blk_cbphp_delta9[num_blk];
            }
            int val = num_blk + 1;
            uint32_t blk = 0;
            if (val >= 6) {
                int c = vlc_io(b, k_chr_cbphp, 3, chr - 1);
                blk = 0x10u * (uint32_t)(c + 1);
                if (val >= 9)
                    val += vlc_io(b, k_chr_cbphp, 3, val_inc);
                val -= 6;
            }
            int code = off[val];
            if (flc[val])
                code += (int)jxr_io(b, flc[val], (uint32_t)(out_inv[luma] - off[val]));
            blk += out[code];
            if (p->cf == MIME_JXR_CF_YUV444) {
                diff[0] |= (blk & 0xf) << (4 * g);
                for (int k = 0; k < 2; k++)
                    if ((blk >> (k + 4)) & 1) {
                        uint32_t pat = k ? v : u;
                        int nch = vlc_io(b, k_num_ch_blk, 4, (int)popcount(pat) - 1);
                        pat = refine_cbphp_io(b, nch + 1, pat);
                        diff[k + 1] |= pat << (4 * g);
                    }
            } else if (p->cf == MIME_JXR_CF_YUV422) {
                diff[0] |= (blk & 0xf) << (4 * g);
                for (int k = 0; k < 2; k++)
                    if ((blk >> (k + 4)) & 1) {
                        uint32_t pat = k ? v : u;
                        int c = vlc_io(b, k_chr_cbphp, 3, pat == 1 ? 0 : pat == 4 ? 1 : 2);
                        diff[k + 1] |= (uint32_t)shift422[c + 1] << shift422[g];
                    }
            } else if (p->cf == MIME_JXR_CF_YUV420) {
                diff[0] |= (blk & 0xf) << (4 * g);
                diff[1] |= ((blk >> 4) & 1) << g;
                diff[2] |= ((blk >> 5) & 1) << g;
            } else {
                diff[i] |= blk << (4 * g);
            }
        }
    }
    if (!b->enc) {
        cbphp_pred_all(im, p, pos, diff, false);
        for (int i = 0; i < p->ncomp; i++)
            p->cbphp[(size_t)pos->mb * p->ncomp + i] = (uint16_t)diff[i];
    }
}

static void flex_io(JxrBits *b, int32_t *c, int m, int trim)
{
    int left = m - trim;
    if (left <= 0)
        return;
    for (int n = 1; n < 16; n++) {
        int j = k_transpose444[n];
        if (b->enc) {
            uint32_t a = (uint32_t)iabs(c[j]);
            uint32_t ref = (a & ((1u << m) - 1)) >> trim;
            jxr_put(b, ref, left);
            if (!(a >> m) && ref)
                jxr_put(b, c[j] < 0, 1);
            continue;
        }
        int32_t ref = (int32_t)jxr_get(b, left);
        int32_t f;
        if (c[j] > 0)
            f = ref;
        else if (c[j] < 0)
            f = -ref;
        else
            f = ref && jxr_get(b, 1) ? -ref : ref;
        c[j] += (int32_t)((uint32_t)f << trim);
    }
}

void jxr_mb_hp(JxrImage *im, JxrPlane *p, JxrBits *b, const JxrMbPos *pos, JxrMbCoef *m, bool flex)
{
    JxrCtx *x = &p->ctx;
    if (pos->left && pos->top) {
        for (int i = 0; i < 2; i++) {
            adapt_init2(&x->first_hp[i]);
            adapt_init2(&x->ind_hp[i][0]);
            adapt_init2(&x->ind_hp[i][1]);
            adapt_init1(&x->abs_hp[i]);
        }
        scan_init(x->hp_hor, x->hp_hor_totals, k_scan0);
        scan_init(x->hp_ver, x->hp_ver_totals, k_scan1);
        model_init(&x->model_hp, 2);
    }
    if (pos->reset_totals) {
        scan_reset(x->hp_hor_totals);
        scan_reset(x->hp_ver_totals);
    }
    bool vertical = p->hpmode[pos->mb] == 1;
    int *order = vertical ? x->hp_ver : x->hp_hor;
    int *totals = vertical ? x->hp_ver_totals : x->hp_hor_totals;
    int lap[2] = {0, 0};
    for (int i = 0; i < p->ncomp; i++) {
        bool chroma = i > 0;
        int bits = x->model_hp.bits[chroma];
        int nb = jxr_blocks(p, i);
        uint32_t cb = p->cbphp[(size_t)pos->mb * p->ncomp + i];
        for (int blk = 0; blk < nb; blk++, cb >>= 1) {
            int map = nb == 16 ? k_hier[blk] : blk;
            int32_t *c = m->hp[i][map];
            int nnz = 0;
            if (cb & 1) {
                int32_t v[16];
                jxr_memset(v, 0, sizeof(v));
                if (b->enc)
                    for (int s = 1; s < 16; s++)
                        v[s - 1] = high_part(c[order[s]], bits);
                nnz = block_io(b, x, chroma, BAND_HP, 1, v);
                for (int s = 1; s < 16; s++) {
                    if (!v[s - 1])
                        continue;
                    if (!b->enc)
                        c[order[s]] = (int32_t)((uint32_t)v[s - 1] << bits);
                    scan_update(order, totals, s);
                }
            }
            if (flex && p->bands == BANDS_ALL)
                flex_io(b, c, bits, im->trim);
            lap[chroma] += nnz;
        }
    }
    p->mbits[pos->mb * 2] = (uint8_t)x->model_hp.bits[0];
    p->mbits[pos->mb * 2 + 1] = (uint8_t)x->model_hp.bits[1];
    model_update(p, &x->model_hp, 2, lap);
    if (pos->reset) {
        for (int i = 0; i < 2; i++) {
            adapt_vlc2(&x->first_hp[i], 4);
            adapt_vlc2(&x->ind_hp[i][0], 3);
            adapt_vlc2(&x->ind_hp[i][1], 3);
        }
        adapt_vlc1(&x->abs_hp[0]);
        adapt_vlc1(&x->abs_hp[1]);
        adapt_vlc1(&x->num_cbphp);
        adapt_vlc1(&x->num_blk_cbphp);
    }
}

void jxr_mb_flex(JxrImage *im, JxrPlane *p, JxrBits *b, const JxrMbPos *pos, JxrMbCoef *m)
{
    for (int i = 0; i < p->ncomp; i++) {
        int bits = p->mbits[pos->mb * 2 + (i != 0)];
        int nb = jxr_blocks(p, i);
        for (int blk = 0; blk < nb; blk++) {
            int map = nb == 16 ? k_hier[blk] : blk;
            flex_io(b, m->hp[i][map], bits, im->trim);
        }
    }
}

int jxr_qp_index(JxrBits *b, int n, int v)
{
    static const int bits[17] = {0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4};
    if (!jxr_io(b, 1, v != 0))
        return 0;
    return (int)jxr_io(b, bits[n], (uint32_t)(v - 1)) + 1;
}

/* a dc, lp or hp qp set list, count sets of ncomp values. */
void jxr_qp_io(JxrBits *b, int ncomp, int count, uint8_t (*q)[JXR_MAX_COMP])
{
    for (int s = 0; s < count; s++) {
        int mode = 0;
        if (ncomp != 1) {
            if (b->enc) {
                bool same = true, chroma_same = true;
                for (int i = 1; i < ncomp; i++) {
                    same &= q[s][i] == q[s][0];
                    chroma_same &= q[s][i] == q[s][1];
                }
                mode = same ? 0 : chroma_same ? 1 : 2;
            }
            mode = (int)jxr_io(b, 2, (uint32_t)mode);
        }
        if (mode == 0) {
            int v = (int)jxr_io(b, 8, q[s][0]);
            for (int i = 0; i < ncomp; i++)
                q[s][i] = (uint8_t)v;
        } else if (mode == 1) {
            int l = (int)jxr_io(b, 8, q[s][0]);
            int c = (int)jxr_io(b, 8, ncomp > 1 ? q[s][1] : 0);
            q[s][0] = (uint8_t)l;
            for (int i = 1; i < ncomp; i++)
                q[s][i] = (uint8_t)c;
        } else if (mode == 2) {
            for (int i = 0; i < ncomp; i++)
                q[s][i] = (uint8_t)jxr_io(b, 8, q[s][i]);
        } else {
            b->error = true;
        }
    }
}

/* ------------------------------------------------------------------ prediction modes */

int jxr_dc_mode(const JxrImage *im, const JxrPlane *p, const JxrMbPos *pos)
{
    if (pos->left && pos->top)
        return 3;
    if (pos->left)
        return 1;
    if (pos->top)
        return 0;
    int nc = p->ncomp;
    const int32_t *l = p->pred + ((size_t)(pos->mb - 1) * nc) * 7;
    const int32_t *t = p->pred + ((size_t)(pos->mb - im->mbw) * nc) * 7;
    const int32_t *tl = p->pred + ((size_t)(pos->mb - im->mbw - 1) * nc) * 7;
    int hor, ver;
    if (p->cf == MIME_JXR_CF_YONLY || p->cf == MIME_JXR_CF_NCOMPONENT) {
        hor = iabs(tl[0] - l[0]);
        ver = iabs(tl[0] - t[0]);
    } else {
        int scale = p->cf == MIME_JXR_CF_YUV420 ? 8 : p->cf == MIME_JXR_CF_YUV422 ? 4 : 2;
        hor = iabs(tl[0] - l[0]) * scale + iabs(tl[7] - l[7]) + iabs(tl[14] - l[14]);
        ver = iabs(tl[0] - t[0]) * scale + iabs(tl[7] - t[7]) + iabs(tl[14] - t[14]);
    }
    if (hor * 4 < ver)
        return 1;
    if (ver * 4 < hor)
        return 0;
    return 2;
}

int jxr_lp_mode(const JxrImage *im, const JxrPlane *p, const JxrMbPos *pos, int dcmode)
{
    if (dcmode == 0 && p->lp_qp[pos->mb] == p->lp_qp[pos->mb - 1])
        return 0;
    if (dcmode == 1 && p->lp_qp[pos->mb] == p->lp_qp[pos->mb - im->mbw])
        return 1;
    return 2;
}

int jxr_hp_mode(const JxrPlane *p, const int32_t (*lp)[16])
{
    int hor = iabs(lp[0][1]) + iabs(lp[0][2]) + iabs(lp[0][3]);
    int ver = iabs(lp[0][4]) + iabs(lp[0][8]) + iabs(lp[0][12]);
    if (p->cf != MIME_JXR_CF_YONLY && p->cf != MIME_JXR_CF_NCOMPONENT) {
        for (int i = 1; i <= 2; i++) {
            hor += iabs(lp[i][1]);
            if (p->cf == MIME_JXR_CF_YUV420) {
                ver += iabs(lp[i][2]);
            } else if (p->cf == MIME_JXR_CF_YUV422) {
                ver += iabs(lp[i][2]) + iabs(lp[i][6]);
                hor += iabs(lp[i][5]);
            } else {
                ver += iabs(lp[i][4]);
            }
        }
    }
    if (hor * 4 < ver)
        return 0;
    if (ver * 4 < hor)
        return 1;
    return 2;
}
