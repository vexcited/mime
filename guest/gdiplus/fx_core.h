/* gdi+ effects: an effect is a class with parameters, a bounds rule and a run over the pixels of a job. */
#ifndef MIME_GDIPLUS_FX_CORE_H
#define MIME_GDIPLUS_FX_CORE_H

#include "gp_private.h"

#define FX_INT_MIN (-2147483647 - 1)

/* one run of an effect. all pixels are straight argb. */
typedef struct FxJob {
    /* the readable part of the input, and its first pixel in input coordinates. */
    const UINT32 *src;
    INT src_x, src_y, src_w, src_h;
    /* the rectangle to produce, in input coordinates, and its output: area_w by area_h pixels, no padding. */
    RECT area;
    UINT32 *dst;
    /* the bitmap is overwritten by the result, so later work in the run sees earlier work. */
    BOOL in_place;
} FxJob;

enum {
    FX_NEIGHBORS = 1,  /* the result needs the whole input, not just the area. */
    FX_KEEPS_24 = 2,   /* a 24-bit input stays 24-bit in a new bitmap. */
    FX_REPORTS_SIZE = 4, /* reading the parameters writes their size back. */
};

typedef struct FxClass {
    const GUID *id;
    UINT state_size;
    UINT flags;
    void (*init)(void *state);
    void (*finish)(void *state);
    UINT (*param_size)(const void *state);
    GpStatus (*set)(void *state, const void *params, UINT size);
    /* out has room for the size the class reports. */
    void (*get)(const void *state, void *out);
    /* how many pixels the bounds grow on every side, NULL for none. */
    INT (*margin)(const void *state);
    GpStatus (*run)(void *state, const FxJob *job);
    /* 1024 bytes of lookup tables in blue, green, red, alpha order, or NULL. */
    const BYTE *(*tables)(void *state);
} FxClass;

typedef struct FxEffect {
    DWORD tag;
    const FxClass *cls;
    /* the state of the class follows. */
} FxEffect;

#define FX_STATE(e) ((void *)((FxEffect *)(e) + 1))

extern const FxClass fx_class_blur, fx_class_sharpen, fx_class_matrix, fx_class_lookup, fx_class_brightness,
    fx_class_hsl, fx_class_levels, fx_class_tint, fx_class_balance, fx_class_curve, fx_class_redeye;

FxEffect *fx_effect_cast(const void *p);

/* the region is optional. */
GpStatus fx_apply_in_place(GBitmap *bitmap, FxEffect *effect, const RECT *region);

/* out of range values and NaN give the lowest integer, like the processor does. */
static inline INT fx_floor_int(float v)
{
    if (!(v >= -2147483648.0f && v < 2147483648.0f))
        return FX_INT_MIN;
    INT i = (INT)v;
    return (float)i > v ? i - 1 : i;
}

static inline BYTE fx_byte(INT v) { return (BYTE)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* rounds halves up. */
static inline BYTE fx_round_byte(float v) { return fx_byte(fx_floor_int(v + 0.5f)); }

static inline INT fx_min(INT a, INT b) { return a < b ? a : b; }
static inline INT fx_max(INT a, INT b) { return a > b ? a : b; }

/* the first pixel of the area on row y, in the source and in the destination. */
static inline const UINT32 *fx_src_row(const FxJob *job, INT y)
{
    return job->src + (SIZE_T)(y - job->src_y) * job->src_w + (job->area.left - job->src_x);
}

static inline UINT32 *fx_dst_row(const FxJob *job, INT y)
{
    return job->dst + (SIZE_T)(y - job->area.top) * (job->area.right - job->area.left);
}

void fx_apply_tables(const BYTE *tables, const FxJob *job);
void fx_identity_tables(BYTE *tables);

/* lightness, saturation and hue as bytes, and back. */
void fx_rgb_to_lsh(INT r, INT g, INT b, INT *l, INT *s, INT *h);
void fx_lsh_to_rgb(INT l, INT s, INT h, INT *r, INT *g, INT *b);

/* the gamma exponent of the midtone setting of levels and curves. */
float fx_midtone_gamma(INT midtone);

#endif
