#ifndef MIME_LIB_JXR_INTERNAL_H
#define MIME_LIB_JXR_INTERNAL_H

#include "jxr.h"

#define JXR_MAX_COMP 16
#define JXR_MAX_TILES 4096
/* int32 samples of all planes of one image, so a forged header can't make us allocate more than a gigabyte. */
#define JXR_MAX_SAMPLES ((size_t)1 << 28)

#define jxr_memset __builtin_memset
#define jxr_memcpy __builtin_memcpy

enum { BAND_DC, BAND_LP, BAND_HP, BAND_FLEX };
enum { BANDS_ALL, BANDS_NOFLEX, BANDS_NOHP, BANDS_DCONLY };

/* bits, msb first. the writer grows its buffer, the reader stops at the end of its data and flags it. */
typedef struct JxrBits {
    bool enc;
    bool error;
    const MimeAllocator *alloc;
    uint8_t *buf;
    size_t len, cap;
    uint32_t acc;
    int nacc;
    const uint8_t *data;
    size_t size;
    size_t pos; /* in bits. */
} JxrBits;

void jxr_bits_reader(JxrBits *b, const uint8_t *data, size_t size);
void jxr_bits_writer(JxrBits *b, const MimeAllocator *alloc);
uint32_t jxr_get(JxrBits *b, int n);
void jxr_put(JxrBits *b, uint32_t v, int n);
void jxr_align(JxrBits *b);
/* writes or reads n bits, returning the value. */
static inline uint32_t jxr_io(JxrBits *b, int n, uint32_t v)
{
    if (!n)
        return 0;
    if (b->enc) {
        jxr_put(b, v, n);
        return v;
    }
    return jxr_get(b, n);
}

typedef struct JxrAdapt {
    int d1, d2, table, delta, delta2;
} JxrAdapt;

typedef struct JxrModel {
    int state[2], bits[2];
} JxrModel;

/* the adaptive state of one image plane, reset at the top left macroblock of every tile. */
typedef struct JxrCtx {
    JxrAdapt abs_dc[2];
    JxrAdapt first_lp[2], ind_lp[2][2], abs_lp[2];
    JxrAdapt first_hp[2], ind_hp[2][2], abs_hp[2];
    JxrAdapt num_cbphp, num_blk_cbphp;
    JxrModel model_dc, model_lp, model_hp;
    int cbphp_state[2], count_ones[2], count_zeroes[2];
    int count_zero_cbplp, count_max_cbplp;
    int lp_order[16], lp_totals[16];
    int hp_hor[16], hp_hor_totals[16], hp_ver[16], hp_ver_totals[16];
} JxrCtx;

typedef struct JxrQp {
    uint8_t dc[JXR_MAX_COMP];
    uint8_t lp[16][JXR_MAX_COMP];
    uint8_t hp[16][JXR_MAX_COMP];
    int nlp, nhp;
} JxrQp;

/* the coded values of one macroblock: full quantized residuals, raster positions inside each 4x4 block. */
typedef struct JxrMbCoef {
    int32_t dc[JXR_MAX_COMP];
    int32_t lp[JXR_MAX_COMP][16];
    int32_t hp[JXR_MAX_COMP][16][16];
} JxrMbCoef;

typedef struct JxrPlane {
    bool alpha;
    int cf;     /* internal colour format. */
    int ncomp;
    int bands;
    bool scaled;
    int shift_bits;
    int len_mantissa;
    int exp_bias;
    int chroma_x, chroma_y;
    bool dc_uniform, lp_uniform, hp_uniform;
    JxrQp qp;   /* of the current tile. */
    bool use_dc_qp, use_lp_qp;
    /* samples or coefficients per component, extended size. */
    int32_t *c[JXR_MAX_COMP];
    int cw, chh;     /* chroma plane size. */
    int cmbw, cmbh;  /* chroma macroblock size, 16 or 8. */
    int ccols;       /* blocks per chroma macroblock row, 4 or 2. */
    int cblocks;     /* blocks per chroma macroblock, 16, 8 or 4. */
    /* per macroblock state. */
    int32_t *pred;   /* [mb][comp][7] */
    uint16_t *cbphp; /* [mb][comp] */
    uint8_t *lp_qp, *hp_qp, *hpmode, *dcmode, *mbits;
    JxrCtx ctx;
} JxrPlane;

typedef struct JxrImage {
    const MimeAllocator *alloc;
    uint32_t width, height;
    int ext_w, ext_h;
    int mbw, mbh;
    int top, left, bottom, right;
    bool hard_tiling, tiling, frequency, index_table, short_header, long_word, windowing, trim_flag;
    bool rb_not_swapped, premultiplied, alpha;
    int spatial_xfrm, overlap, out_cf, bd;
    int ntx, nty;
    uint32_t tile_x[JXR_MAX_TILES + 1], tile_y[JXR_MAX_TILES + 1];
    JxrPlane plane[2];
    int nplanes;
    int trim;
    JxrMbCoef mb;
} JxrImage;

static inline bool jxr_chroma_sub(const JxrPlane *p) { return p->cf == MIME_JXR_CF_YUV420 || p->cf == MIME_JXR_CF_YUV422; }
static inline int jxr_comp_w(const JxrImage *im, const JxrPlane *p, int i) { return i && jxr_chroma_sub(p) ? p->cw : im->ext_w; }
static inline int jxr_comp_h(const JxrImage *im, const JxrPlane *p, int i) { return i && jxr_chroma_sub(p) ? p->chh : im->ext_h; }
static inline int jxr_blocks(const JxrPlane *p, int i) { return i && jxr_chroma_sub(p) ? p->cblocks : 16; }
static inline int jxr_cols(const JxrPlane *p, int i) { return i && jxr_chroma_sub(p) ? p->ccols : 4; }
static inline int jxr_mbsize_w(const JxrPlane *p, int i) { return i && jxr_chroma_sub(p) ? p->cmbw : 16; }
static inline int jxr_mbsize_h(const JxrPlane *p, int i) { return i && jxr_chroma_sub(p) ? p->cmbh : 16; }
/* coefficient j of block b in component i of a macroblock. */
static inline int32_t *jxr_coef(const JxrImage *im, const JxrPlane *p, int i, int mbx, int mby, int b, int j)
{
    int cols = jxr_cols(p, i);
    int x = mbx * jxr_mbsize_w(p, i) + 4 * (b % cols) + (j & 3);
    int y = mby * jxr_mbsize_h(p, i) + 4 * (b / cols) + (j >> 2);
    return p->c[i] + (size_t)y * jxr_comp_w(im, p, i) + x;
}

/* macroblock position inside its tile. */
typedef struct JxrMbPos {
    int mbx, mby, mb;
    bool left, top;      /* first column or row of the tile. */
    bool reset;          /* the adaptive tables adapt after this macroblock. */
    bool reset_totals;   /* the scan totals reset before it. */
} JxrMbPos;

int jxr_quant_map(int qp, bool scaled, int shift);
int jxr_num_lp(const JxrPlane *p, int i); /* lowpass coefficients per component: 15, 7 or 3. */

/* syntax, shared by the decoder and the encoder. */
void jxr_mb_dc(JxrPlane *p, JxrBits *b, const JxrMbPos *pos, JxrMbCoef *m);
void jxr_mb_lp(JxrPlane *p, JxrBits *b, const JxrMbPos *pos, JxrMbCoef *m);
void jxr_mb_cbphp(JxrImage *im, JxrPlane *p, JxrBits *b, const JxrMbPos *pos, JxrMbCoef *m);
void jxr_mb_hp(JxrImage *im, JxrPlane *p, JxrBits *b, const JxrMbPos *pos, JxrMbCoef *m, bool flex);
void jxr_mb_flex(JxrImage *im, JxrPlane *p, JxrBits *b, const JxrMbPos *pos, JxrMbCoef *m);
int jxr_qp_index(JxrBits *b, int n, int v);
void jxr_qp_io(JxrBits *b, int ncomp, int count, uint8_t (*q)[JXR_MAX_COMP]);

/* prediction, shared too. */
int jxr_dc_mode(const JxrImage *im, const JxrPlane *p, const JxrMbPos *pos);
int jxr_lp_mode(const JxrImage *im, const JxrPlane *p, const JxrMbPos *pos, int dcmode);
int jxr_hp_mode(const JxrPlane *p, const int32_t (*lp)[16]);

/* transforms. */
void jxr_inverse(JxrImage *im, JxrPlane *p);
void jxr_forward(JxrImage *im, JxrPlane *p);

/* whether the planes of an image fit in JXR_MAX_SAMPLES, chroma counted at full size for the upsampling. */
bool jxr_image_fits(const JxrImage *im);
int jxr_image_decode(const uint8_t *data, size_t size, const MimeAllocator *alloc, bool header_only, JxrImage **out);
void jxr_image_free(JxrImage *im);

/* pixel layouts. */
typedef struct JxrLayout {
    uint8_t id, cf, bd, nc, alpha, slots, bits;
    bool premul;
    uint8_t pos[9];
} JxrLayout;
bool jxr_find_layout(uint8_t id, JxrLayout *layout);

#endif
