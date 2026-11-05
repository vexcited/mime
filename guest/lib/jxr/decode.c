#include "internal.h"

static uint32_t vlw_esc(JxrBits *b, bool *bad)
{
    uint32_t first = jxr_get(b, 8);
    if (first < 0xfb)
        return first * 256 + jxr_get(b, 8);
    if (first == 0xfb)
        return jxr_get(b, 32);
    if (first == 0xfc) {
        if (jxr_get(b, 32))
            *bad = true;
        return jxr_get(b, 32);
    }
    return 0;
}

static void infer_size(JxrImage *im)
{
    im->ext_w = (int)im->width + im->left + im->right;
    im->ext_h = (int)im->height + im->top + im->bottom;
    im->mbw = im->ext_w / 16;
    im->mbh = im->ext_h / 16;
}

static int parse_image_header(JxrImage *im, JxrBits *b, uint32_t *tile_w, uint32_t *tile_h)
{
    static const uint8_t sig[8] = {'W', 'M', 'P', 'H', 'O', 'T', 'O', 0};
    for (int i = 0; i < 8; i++)
        if (jxr_get(b, 8) != sig[i])
            return MIME_JXR_ERR_FORMAT;
    if (jxr_get(b, 4) != 1)
        return MIME_JXR_ERR_UNSUPPORTED;
    im->hard_tiling = jxr_get(b, 1);
    jxr_get(b, 3);
    im->tiling = jxr_get(b, 1);
    im->frequency = jxr_get(b, 1);
    im->spatial_xfrm = (int)jxr_get(b, 3);
    im->index_table = jxr_get(b, 1);
    im->overlap = (int)jxr_get(b, 2);
    im->short_header = jxr_get(b, 1);
    im->long_word = jxr_get(b, 1);
    im->windowing = jxr_get(b, 1);
    im->trim_flag = jxr_get(b, 1);
    jxr_get(b, 1);
    im->rb_not_swapped = jxr_get(b, 1);
    im->premultiplied = jxr_get(b, 1);
    im->alpha = jxr_get(b, 1);
    im->out_cf = (int)jxr_get(b, 4);
    im->bd = (int)jxr_get(b, 4);
    int wbits = im->short_header ? 16 : 32;
    im->width = jxr_get(b, wbits) + 1;
    im->height = jxr_get(b, wbits) + 1;
    if (!im->width || !im->height || im->width > 0x10000000u || im->height > 0x10000000u)
        return MIME_JXR_ERR_UNSUPPORTED;
    int nvt = 0, nht = 0;
    if (im->tiling) {
        nvt = (int)jxr_get(b, 12);
        nht = (int)jxr_get(b, 12);
    }
    im->ntx = nvt + 1;
    im->nty = nht + 1;
    int tbits = im->short_header ? 8 : 16;
    for (int n = 0; n < nvt; n++)
        tile_w[n] = jxr_get(b, tbits);
    for (int n = 0; n < nht; n++)
        tile_h[n] = jxr_get(b, tbits);
    if (im->windowing) {
        im->top = (int)jxr_get(b, 6);
        im->left = (int)jxr_get(b, 6);
        im->bottom = (int)jxr_get(b, 6);
        im->right = (int)jxr_get(b, 6);
    } else {
        im->top = im->left = 0;
        im->bottom = im->height % 16 ? 16 - (int)(im->height % 16) : 0;
        im->right = im->width % 16 ? 16 - (int)(im->width % 16) : 0;
    }
    infer_size(im);
    if (im->ext_w % 16 || im->ext_h % 16 || im->overlap == 3)
        return MIME_JXR_ERR_FORMAT;
    im->tile_x[0] = 0;
    for (int n = 0; n < nvt; n++)
        im->tile_x[n + 1] = im->tile_x[n] + tile_w[n];
    im->tile_x[im->ntx] = (uint32_t)im->mbw;
    im->tile_y[0] = 0;
    for (int n = 0; n < nht; n++)
        im->tile_y[n + 1] = im->tile_y[n] + tile_h[n];
    im->tile_y[im->nty] = (uint32_t)im->mbh;
    for (int n = 0; n < im->ntx; n++)
        if (im->tile_x[n + 1] <= im->tile_x[n])
            return MIME_JXR_ERR_FORMAT;
    for (int n = 0; n < im->nty; n++)
        if (im->tile_y[n + 1] <= im->tile_y[n])
            return MIME_JXR_ERR_FORMAT;
    return b->error ? MIME_JXR_ERR_FORMAT : MIME_JXR_OK;
}

static int parse_plane_header(JxrImage *im, JxrPlane *p, JxrBits *b)
{
    p->cf = (int)jxr_get(b, 3);
    p->scaled = jxr_get(b, 1);
    p->bands = (int)jxr_get(b, 4);
    if (p->bands > 3 || p->cf == 5 || p->cf == 7)
        return MIME_JXR_ERR_FORMAT;
    p->chroma_x = p->chroma_y = 0;
    if (p->cf == MIME_JXR_CF_YUV444 || p->cf == MIME_JXR_CF_YUV420 || p->cf == MIME_JXR_CF_YUV422) {
        if (p->cf != MIME_JXR_CF_YUV444) {
            jxr_get(b, 1);
            p->chroma_x = (int)jxr_get(b, 3);
        } else {
            jxr_get(b, 4);
        }
        if (p->cf == MIME_JXR_CF_YUV420) {
            jxr_get(b, 1);
            p->chroma_y = (int)jxr_get(b, 3);
        } else {
            jxr_get(b, 4);
        }
        p->ncomp = 3;
    } else if (p->cf == MIME_JXR_CF_NCOMPONENT) {
        int n = (int)jxr_get(b, 4);
        if (n == 15)
            n = (int)jxr_get(b, 12) + 15;
        else
            jxr_get(b, 4);
        p->ncomp = n + 1;
    } else {
        p->ncomp = p->cf == MIME_JXR_CF_YONLY ? 1 : 4;
    }
    if (p->ncomp > JXR_MAX_COMP)
        return MIME_JXR_ERR_UNSUPPORTED;
    if (p->alpha && p->ncomp != 1)
        return MIME_JXR_ERR_FORMAT;
    p->shift_bits = 0;
    if (im->bd == MIME_JXR_BD16 || im->bd == MIME_JXR_BD16S || im->bd == MIME_JXR_BD32S)
        p->shift_bits = (int)jxr_get(b, 8);
    if (im->bd == MIME_JXR_BD32F) {
        p->len_mantissa = (int)jxr_get(b, 8);
        p->exp_bias = (int8_t)jxr_get(b, 8);
    }
    jxr_memset(&p->qp, 0, sizeof(p->qp));
    p->qp.nlp = p->qp.nhp = 1;
    p->dc_uniform = jxr_get(b, 1);
    if (p->dc_uniform)
        jxr_qp_io(b, p->ncomp, 1, (uint8_t (*)[JXR_MAX_COMP])p->qp.dc);
    p->lp_uniform = p->hp_uniform = false;
    if (p->bands != BANDS_DCONLY) {
        jxr_get(b, 1);
        p->lp_uniform = jxr_get(b, 1);
        if (p->lp_uniform)
            jxr_qp_io(b, p->ncomp, 1, p->qp.lp);
        if (p->bands != BANDS_NOHP) {
            jxr_get(b, 1);
            p->hp_uniform = jxr_get(b, 1);
            if (p->hp_uniform)
                jxr_qp_io(b, p->ncomp, 1, p->qp.hp);
        }
    }
    jxr_align(b);
    return b->error ? MIME_JXR_ERR_FORMAT : MIME_JXR_OK;
}

static int plane_alloc(JxrImage *im, JxrPlane *p)
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

static void plane_free(JxrImage *im, JxrPlane *p)
{
    for (int i = 0; i < JXR_MAX_COMP; i++) {
        mime_free(im->alloc, p->c[i]);
        p->c[i] = nullptr;
    }
    mime_free(im->alloc, p->pred);
    mime_free(im->alloc, p->cbphp);
    mime_free(im->alloc, p->lp_qp);
    p->pred = nullptr;
    p->cbphp = nullptr;
    p->lp_qp = nullptr;
}

void jxr_image_free(JxrImage *im)
{
    if (!im)
        return;
    for (int i = 0; i < 2; i++)
        plane_free(im, &im->plane[i]);
    mime_free(im->alloc, im);
}

/* ------------------------------------------------------------------ coefficient decoding */

static void dclp_finish(JxrImage *im, JxrPlane *p, const JxrMbPos *pos, const JxrMbCoef *m)
{
    int nc = p->ncomp;
    int32_t *cur = p->pred + (size_t)pos->mb * nc * 7;
    const int32_t *left = cur - nc * 7;
    const int32_t *top = cur - (size_t)im->mbw * nc * 7;
    int dcmode = jxr_dc_mode(im, p, pos);
    p->dcmode[pos->mb] = (uint8_t)dcmode;
    bool sub = jxr_chroma_sub(p);
    for (int i = 0; i < nc; i++) {
        int32_t dc = m->dc[i];
        int32_t l = dcmode == 0 || dcmode == 2 ? left[i * 7] : 0, t = dcmode >= 1 && dcmode <= 2 ? top[i * 7] : 0;
        if (dcmode == 0)
            dc += l;
        else if (dcmode == 1)
            dc += t;
        else if (dcmode == 2)
            dc += i == 0 || !sub ? (t + l) >> 1 : (t + l + 1) >> 1;
        cur[i * 7] = dc;
    }
    int lpmode = jxr_lp_mode(im, p, pos, dcmode);
    int32_t lp[JXR_MAX_COMP][16];
    for (int i = 0; i < nc; i++) {
        const int32_t *li = left + i * 7, *ti = top + i * 7;
        int32_t *c = cur + i * 7;
        jxr_memcpy(lp[i], m->lp[i], sizeof(lp[i]));
        if (i == 0 || !sub) {
            if (lpmode == 0) {
                lp[i][4] += li[4];
                lp[i][8] += li[5];
                lp[i][12] += li[6];
            } else if (lpmode == 1) {
                lp[i][1] += ti[1];
                lp[i][2] += ti[2];
                lp[i][3] += ti[3];
            }
            c[1] = lp[i][1];
            c[2] = lp[i][2];
            c[3] = lp[i][3];
            c[4] = lp[i][4];
            c[5] = lp[i][8];
            c[6] = lp[i][12];
        } else if (p->cf == MIME_JXR_CF_YUV420) {
            if (lpmode == 0)
                lp[i][2] += li[2];
            else if (lpmode == 1)
                lp[i][1] += ti[1];
            c[1] = lp[i][1];
            c[2] = lp[i][2];
        } else {
            if (lpmode == 0) {
                lp[i][4] += li[4];
                lp[i][2] += li[2];
                lp[i][6] += li[6];
            } else if (lpmode == 1) {
                lp[i][4] += ti[4];
                lp[i][1] += ti[5];
                lp[i][5] += lp[i][1];
            } else if (dcmode == 1) {
                lp[i][5] += lp[i][1];
            }
            c[1] = lp[i][1];
            c[2] = lp[i][2];
            c[4] = lp[i][4];
            c[5] = lp[i][5];
            c[6] = lp[i][6];
        }
    }
    int lpq = p->lp_qp[pos->mb];
    for (int i = 0; i < nc; i++) {
        int sdc = jxr_quant_map(p->qp.dc[i], p->scaled, i == 0);
        int slp = jxr_quant_map(p->qp.lp[lpq][i], p->scaled, i == 0);
        int nb = jxr_blocks(p, i);
        *jxr_coef(im, p, i, pos->mbx, pos->mby, 0, 0) = (int32_t)((uint32_t)cur[i * 7] * (uint32_t)sdc);
        lp[i][0] = 0;
        for (int j = 1; j < nb; j++) {
            lp[i][j] = (int32_t)((uint32_t)lp[i][j] * (uint32_t)slp);
            *jxr_coef(im, p, i, pos->mbx, pos->mby, j, 0) = lp[i][j];
        }
    }
    p->hpmode[pos->mb] = (uint8_t)jxr_hp_mode(p, (const int32_t (*)[16])lp);
}

static void hp_store(JxrImage *im, JxrPlane *p, const JxrMbPos *pos, const JxrMbCoef *m, bool load)
{
    for (int i = 0; i < p->ncomp; i++) {
        int nb = jxr_blocks(p, i);
        for (int blk = 0; blk < nb; blk++)
            for (int j = 1; j < 16; j++) {
                int32_t *c = jxr_coef(im, p, i, pos->mbx, pos->mby, blk, j);
                if (load)
                    ((JxrMbCoef *)m)->hp[i][blk][j] = *c;
                else
                    *c = m->hp[i][blk][j];
            }
    }
}

static void hp_finish(JxrImage *im, JxrPlane *p, const JxrMbPos *pos)
{
    int hpq = p->hp_qp[pos->mb];
    int mode = p->hpmode[pos->mb];
    for (int i = 0; i < p->ncomp; i++) {
        int step = jxr_quant_map(p->qp.hp[hpq][i], p->scaled, 1);
        int nb = jxr_blocks(p, i), cols = jxr_cols(p, i);
        for (int blk = 0; blk < nb; blk++)
            for (int j = 1; j < 16; j++) {
                int32_t *c = jxr_coef(im, p, i, pos->mbx, pos->mby, blk, j);
                *c = (int32_t)((uint32_t)*c * (uint32_t)step);
            }
        if (mode == 2)
            continue;
        for (int blk = 0; blk < nb; blk++) {
            if (mode == 0 && blk % cols) {
                for (int j = 4; j < 16; j += 4)
                    *jxr_coef(im, p, i, pos->mbx, pos->mby, blk, j) +=
                        *jxr_coef(im, p, i, pos->mbx, pos->mby, blk - 1, j);
            } else if (mode == 1 && blk >= cols) {
                for (int j = 1; j < 4; j++)
                    *jxr_coef(im, p, i, pos->mbx, pos->mby, blk, j) +=
                        *jxr_coef(im, p, i, pos->mbx, pos->mby, blk - cols, j);
            }
        }
    }
}

static void mb_clear(JxrMbCoef *m, int ncomp)
{
    jxr_memset(m->dc, 0, sizeof(m->dc));
    jxr_memset(m->lp, 0, sizeof(m->lp[0]) * ncomp);
    jxr_memset(m->hp, 0, sizeof(m->hp[0]) * ncomp);
}

static void tile_qp_dc(JxrPlane *p, JxrBits *b)
{
    if (!p->dc_uniform)
        jxr_qp_io(b, p->ncomp, 1, (uint8_t (*)[JXR_MAX_COMP])p->qp.dc);
}

static void tile_qp_lp(JxrPlane *p, JxrBits *b)
{
    if (p->bands == BANDS_DCONLY || p->lp_uniform)
        return;
    p->use_dc_qp = jxr_get(b, 1);
    if (p->use_dc_qp) {
        p->qp.nlp = 1;
        jxr_memcpy(p->qp.lp[0], p->qp.dc, sizeof(p->qp.dc));
    } else {
        p->qp.nlp = (int)jxr_get(b, 4) + 1;
        jxr_qp_io(b, p->ncomp, p->qp.nlp, p->qp.lp);
    }
}

static void tile_qp_hp(JxrPlane *p, JxrBits *b)
{
    if (p->bands == BANDS_DCONLY || p->bands == BANDS_NOHP || p->hp_uniform)
        return;
    p->use_lp_qp = jxr_get(b, 1);
    if (p->use_lp_qp) {
        p->qp.nhp = p->qp.nlp;
        jxr_memcpy(p->qp.hp, p->qp.lp, sizeof(p->qp.lp));
    } else {
        p->qp.nhp = (int)jxr_get(b, 4) + 1;
        jxr_qp_io(b, p->ncomp, p->qp.nhp, p->qp.hp);
    }
}

static void mb_lp_index(JxrPlane *p, JxrBits *b, const JxrMbPos *pos)
{
    if (p->bands != BANDS_DCONLY && p->qp.nlp > 1 && !p->use_dc_qp)
        p->lp_qp[pos->mb] = (uint8_t)jxr_qp_index(b, p->qp.nlp, 0);
    if (p->use_lp_qp)
        p->hp_qp[pos->mb] = p->lp_qp[pos->mb];
}

static void mb_hp_index(JxrPlane *p, JxrBits *b, const JxrMbPos *pos)
{
    if (p->bands != BANDS_DCONLY && p->bands != BANDS_NOHP && p->qp.nhp > 1 && !p->use_lp_qp)
        p->hp_qp[pos->mb] = (uint8_t)jxr_qp_index(b, p->qp.nhp, 0);
}

static void mb_pos(const JxrImage *im, int tx, int ty, int mbx, int mby, JxrMbPos *pos)
{
    pos->mbx = mbx;
    pos->mby = mby;
    pos->mb = mby * im->mbw + mbx;
    pos->left = mbx == (int)im->tile_x[tx];
    pos->top = mby == (int)im->tile_y[ty];
    int rel = mbx - (int)im->tile_x[tx];
    pos->reset = mbx == (int)im->tile_x[tx + 1] - 1 || rel % 16 == 0;
    pos->reset_totals = rel % 16 == 0;
}

static bool packet_start(JxrImage *im, JxrBits *b, const uint8_t *data, size_t size, size_t offset, bool trim)
{
    if (offset >= size)
        return false;
    jxr_bits_reader(b, data + offset, size - offset);
    if (jxr_get(b, 24) != 1)
        return false;
    jxr_get(b, 8);
    if (trim && im->trim_flag)
        im->trim = (int)jxr_get(b, 4);
    return !b->error;
}

static int decode_tile(JxrImage *im, const uint8_t *data, size_t size, const uint32_t *offsets, int tx, int ty)
{
    int n = ty * im->ntx + tx;
    JxrBits b;
    for (int k = 0; k < im->nplanes; k++) {
        JxrPlane *p = &im->plane[k];
        p->use_dc_qp = p->use_lp_qp = false;
        if (!p->lp_uniform)
            p->qp.nlp = 1;
        if (!p->hp_uniform)
            p->qp.nhp = 1;
    }
    int x0 = (int)im->tile_x[tx], x1 = (int)im->tile_x[tx + 1];
    int y0 = (int)im->tile_y[ty], y1 = (int)im->tile_y[ty + 1];
    JxrMbCoef *m = &im->mb;
    JxrMbPos pos;
    if (!im->frequency) {
        im->trim = 0;
        if (!packet_start(im, &b, data, size, offsets[n], true))
            return MIME_JXR_ERR_DATA;
        for (int k = 0; k < im->nplanes; k++) {
            tile_qp_dc(&im->plane[k], &b);
            tile_qp_lp(&im->plane[k], &b);
            tile_qp_hp(&im->plane[k], &b);
        }
        for (int mby = y0; mby < y1; mby++)
            for (int mbx = x0; mbx < x1; mbx++) {
                mb_pos(im, tx, ty, mbx, mby, &pos);
                for (int k = 0; k < im->nplanes; k++) {
                    JxrPlane *p = &im->plane[k];
                    mb_clear(m, p->ncomp);
                    mb_lp_index(p, &b, &pos);
                    mb_hp_index(p, &b, &pos);
                    jxr_mb_dc(p, &b, &pos, m);
                    if (p->bands != BANDS_DCONLY)
                        jxr_mb_lp(p, &b, &pos, m);
                    dclp_finish(im, p, &pos, m);
                    if (p->bands < BANDS_NOHP) {
                        jxr_mb_cbphp(im, p, &b, &pos, m);
                        jxr_mb_hp(im, p, &b, &pos, m, true);
                        hp_store(im, p, &pos, m, false);
                    }
                    hp_finish(im, p, &pos);
                }
                if (b.error)
                    return MIME_JXR_ERR_DATA;
            }
        return MIME_JXR_OK;
    }
    int bands = im->plane[0].bands;
    int nbands = 4 - bands;
    for (int band = 0; band < nbands; band++) {
        im->trim = 0;
        if (!packet_start(im, &b, data, size, offsets[n * nbands + band], band == BAND_FLEX))
            return MIME_JXR_ERR_DATA;
        for (int k = 0; k < im->nplanes; k++) {
            if (band == BAND_DC)
                tile_qp_dc(&im->plane[k], &b);
            else if (band == BAND_LP)
                tile_qp_lp(&im->plane[k], &b);
            else if (band == BAND_HP)
                tile_qp_hp(&im->plane[k], &b);
        }
        for (int mby = y0; mby < y1; mby++)
            for (int mbx = x0; mbx < x1; mbx++) {
                mb_pos(im, tx, ty, mbx, mby, &pos);
                for (int k = 0; k < im->nplanes; k++) {
                    JxrPlane *p = &im->plane[k];
                    if (band == BAND_DC) {
                        jxr_memset(m->dc, 0, sizeof(m->dc));
                        jxr_mb_dc(p, &b, &pos, m);
                        for (int i = 0; i < p->ncomp; i++)
                            *jxr_coef(im, p, i, mbx, mby, 0, 0) = m->dc[i];
                        if (p->bands == BANDS_DCONLY) {
                            jxr_memset(m->lp, 0, sizeof(m->lp));
                            dclp_finish(im, p, &pos, m);
                        }
                    } else if (band == BAND_LP) {
                        if (p->bands == BANDS_DCONLY)
                            continue;
                        for (int i = 0; i < p->ncomp; i++)
                            m->dc[i] = *jxr_coef(im, p, i, mbx, mby, 0, 0);
                        jxr_memset(m->lp, 0, sizeof(m->lp[0]) * p->ncomp);
                        mb_lp_index(p, &b, &pos);
                        jxr_mb_lp(p, &b, &pos, m);
                        dclp_finish(im, p, &pos, m);
                    } else if (band == BAND_HP) {
                        if (p->bands >= BANDS_NOHP)
                            continue;
                        jxr_memset(m->hp, 0, sizeof(m->hp[0]) * p->ncomp);
                        mb_hp_index(p, &b, &pos);
                        jxr_mb_cbphp(im, p, &b, &pos, m);
                        jxr_mb_hp(im, p, &b, &pos, m, false);
                        hp_store(im, p, &pos, m, false);
                    } else {
                        if (p->bands != BANDS_ALL)
                            continue;
                        hp_store(im, p, &pos, m, true);
                        jxr_mb_flex(im, p, &b, &pos, m);
                        hp_store(im, p, &pos, m, false);
                    }
                }
                if (b.error)
                    return MIME_JXR_ERR_DATA;
            }
    }
    for (int mby = y0; mby < y1; mby++)
        for (int mbx = x0; mbx < x1; mbx++) {
            mb_pos(im, tx, ty, mbx, mby, &pos);
            for (int k = 0; k < im->nplanes; k++)
                hp_finish(im, &im->plane[k], &pos);
        }
    return MIME_JXR_OK;
}

/* parses and reconstructs a codestream into its planes of internal samples. */
int jxr_image_decode(const uint8_t *data, size_t size, const MimeAllocator *alloc, bool header_only, JxrImage **out)
{
    *out = nullptr;
    JxrImage *im = mime_alloc(alloc, sizeof(*im));
    if (!im)
        return MIME_JXR_ERR_NOMEM;
    jxr_memset(im, 0, sizeof(*im));
    im->alloc = alloc;
    uint32_t *tw = mime_alloc(alloc, 2 * JXR_MAX_TILES * sizeof(uint32_t));
    if (!tw) {
        mime_free(alloc, im);
        return MIME_JXR_ERR_NOMEM;
    }
    uint32_t *offsets = nullptr;
    JxrBits b;
    jxr_bits_reader(&b, data, size);
    int rc = parse_image_header(im, &b, tw, tw + JXR_MAX_TILES);
    mime_free(alloc, tw);
    if (rc)
        goto fail;
    im->nplanes = im->alpha ? 2 : 1;
    for (int k = 0; k < im->nplanes; k++) {
        im->plane[k].alpha = k == 1;
        rc = parse_plane_header(im, &im->plane[k], &b);
        if (rc)
            goto fail;
    }
    if (im->plane[0].cf == MIME_JXR_CF_YUV420 || im->plane[0].cf == MIME_JXR_CF_YUV422) {
        if (im->overlap == 2 && im->mbw < 2) {
            rc = MIME_JXR_ERR_FORMAT;
            goto fail;
        }
    }
    if (header_only) {
        *out = im;
        return MIME_JXR_OK;
    }
    int tiles = im->ntx * im->nty;
    int per_tile = im->frequency ? 4 - im->plane[0].bands : 1;
    offsets = mime_alloc(alloc, (size_t)tiles * per_tile * sizeof(uint32_t));
    if (!offsets) {
        rc = MIME_JXR_ERR_NOMEM;
        goto fail;
    }
    jxr_memset(offsets, 0, (size_t)tiles * per_tile * sizeof(uint32_t));
    bool bad = false;
    if (im->index_table) {
        if (jxr_get(&b, 16) != 1) {
            rc = MIME_JXR_ERR_FORMAT;
            goto fail;
        }
        for (int n = 0; n < tiles * per_tile; n++)
            offsets[n] = vlw_esc(&b, &bad);
    } else if (tiles * per_tile > 1) {
        rc = MIME_JXR_ERR_FORMAT;
        goto fail;
    }
    uint32_t subsequent = vlw_esc(&b, &bad);
    if (b.error || bad) {
        rc = MIME_JXR_ERR_FORMAT;
        goto fail;
    }
    size_t start = (b.pos >> 3) + subsequent;
    if (start > size) {
        rc = MIME_JXR_ERR_DATA;
        goto fail;
    }
    for (int k = 0; k < im->nplanes; k++) {
        rc = plane_alloc(im, &im->plane[k]);
        if (rc)
            goto fail;
    }
    for (int ty = 0; ty < im->nty; ty++)
        for (int tx = 0; tx < im->ntx; tx++) {
            rc = decode_tile(im, data + start, size - start, offsets, tx, ty);
            if (rc)
                goto fail;
        }
    mime_free(alloc, offsets);
    for (int k = 0; k < im->nplanes; k++)
        jxr_inverse(im, &im->plane[k]);
    *out = im;
    return MIME_JXR_OK;
fail:
    mime_free(alloc, offsets);
    jxr_image_free(im);
    return rc;
}

int mime_jxr_header(const uint8_t *data, size_t size, const MimeAllocator *alloc, MimeJxrHeader *h)
{
    JxrImage *im;
    int rc = jxr_image_decode(data, size, alloc, true, &im);
    if (rc)
        return rc;
    jxr_memset(h, 0, sizeof(*h));
    h->width = im->width;
    h->height = im->height;
    h->output_format = (uint8_t)im->out_cf;
    h->bit_depth = (uint8_t)im->bd;
    h->internal_format = (uint8_t)im->plane[0].cf;
    h->components = (uint8_t)im->plane[0].ncomp;
    h->overlap = (uint8_t)im->overlap;
    h->bands = (uint8_t)im->plane[0].bands;
    h->spatial_xfrm = (uint8_t)im->spatial_xfrm;
    h->alpha = im->alpha;
    h->premultiplied = im->premultiplied;
    h->frequency_mode = im->frequency;
    h->hard_tiling = im->hard_tiling;
    h->tile_cols = (uint16_t)im->ntx;
    h->tile_rows = (uint16_t)im->nty;
    jxr_image_free(im);
    return MIME_JXR_OK;
}
