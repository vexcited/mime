/*
 * the effects of gdi+ 1.1: what the pieces share. the objects are made by effects.c, the filters that work on
 * whole images live in their own files. an image here is straight argb, one 32 bit value per pixel.
 */
#ifndef MIME_GDIPLUS_FX_PRIVATE_H
#define MIME_GDIPLUS_FX_PRIVATE_H

#include "gp_private.h"

/* results, in the numbers of the system so they map to statuses the way the library has them. */
typedef int FxResult;
#define FX_OK 0
#define FX_E_INVALIDARG ((FxResult)0x80070057)
#define FX_E_FAIL ((FxResult)0x80004005)
#define FX_E_OUTOFMEMORY ((FxResult)0x8007000E)

typedef struct FxImage {
    INT width, height;
    INT stride;        /* in pixels */
    UINT32 *pixels;
} FxImage;

/* the part of the rectangle that is in both, all zero when there is none. */
BOOL fx_intersect(RECT *out, const RECT *a, const RECT *b);
BOOL fx_rect_empty(const RECT *r);

/* a float as the processor makes an integer of it: anything out of range gives the lowest integer. */
INT fx_float_to_int(float x);
BYTE fx_saturate(INT v);
/* the exponent that a middle tone level from -100 to 100 stands for. */
float fx_gamma_from_midtone(INT midtone);

/* ------------------------------------------------------------------ blur and sharpen */

/* blurs the part of the source in rect into dst, whose first pixel belongs to the corner of rect. */
FxResult fx_blur(const FxImage *src, const RECT *rect, UINT32 *dst, INT dst_stride, float radius, INT expand,
                 const RECT *bounds);
/* the same followed by the unsharp mask. */
FxResult fx_sharpen(const FxImage *src, const RECT *rect, UINT32 *dst, INT dst_stride, float radius, float amount,
                    const RECT *bounds);

/* ------------------------------------------------------------------ color curves */

typedef struct FxCurve FxCurve;

FxCurve *fx_curve_new(void);
void fx_curve_free(FxCurve *c);
/* parameters of the sdk, three integers. */
FxResult fx_curve_set(FxCurve *c, const INT *params);
void fx_curve_get(const FxCurve *c, INT *params);
/* the lookup tables for the blue, green and red channels, 256 bytes each in that order. */
void fx_curve_tables(FxCurve *c, BYTE *tables);

/* ------------------------------------------------------------------ red eye correction */

typedef struct FxRedEye FxRedEye;

FxRedEye *fx_redeye_new(void);
void fx_redeye_free(FxRedEye *r);
UINT fx_redeye_count(const FxRedEye *r);
const RECT *fx_redeye_areas(const FxRedEye *r);
FxResult fx_redeye_set(FxRedEye *r, UINT count, const RECT *areas);
FxResult fx_redeye_run(FxRedEye *r, const FxImage *src, const RECT *rect, UINT32 *dst, INT dst_stride);

#endif
