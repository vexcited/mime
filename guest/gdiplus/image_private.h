/*
 * the pieces of image drawing: the matrices in the float arithmetic of gdiplus.dll, and the spans that turn a
 * row of device pixels into premultiplied pixels of a picture.
 *
 * a span is asked for the pixels y, x0 to x1 of one run of the scan converter, and works out where the run starts
 * and ends in the picture from the inverse of the matrix, so the runs the caller asks for matter: the sample
 * positions of a run are interpolated between its two ends.
 */
#ifndef MIME_GDIPLUS_IMAGE_PRIVATE_H
#define MIME_GDIPLUS_IMAGE_PRIVATE_H

#include "gp_private.h"

/* ------------------------------------------------------------------ matrices */

/*
 * a matrix with the complexity gdi+ keeps next to it: bit 0 is a translation, bit 1 a scale, bits 2 and 3 a
 * rotation or a shear. 0 is the identity, 1 a translation, 3 a scale with a translation. a matrix whose
 * off diagonal elements are tiny counts as a scale, and the code that uses it then ignores them.
 */
typedef struct Affine {
    float m11, m12, m21, m22, dx, dy;
    INT cx;
} Affine;

void aff_identity(Affine *a);
INT aff_complexity(const Affine *a);
void aff_set(Affine *a, float m11, float m12, float m21, float m22, float dx, float dy);
/* a first, then b. r may be a or b. */
void aff_mul(Affine *r, const Affine *a, const Affine *b);
/* false when the matrix cannot be inverted, which is when its determinant is zero. */
BOOL aff_invert(Affine *a);
/* the matrix that takes the rectangle x, y, w, h to the parallelogram of three points, false for no area. */
BOOL aff_infer(Affine *a, const float pts[6], const float rect[4]);
void aff_transform(const Affine *a, float *pts, INT count);
/* the translation or scale applied before (prepend) or after (append) the matrix. */
void aff_translate(Affine *a, float tx, float ty, BOOL append);
void aff_scale(Affine *a, float sx, float sy, BOOL append);
/* a translation by whole pixels, within 1/64. */
BOOL aff_integer_translate(const Affine *a);
/* the box a rectangle covers under the matrix: x, y, w, h. A size under 0.0006 counts as 0. */
void aff_bounds(const Affine *a, float x0, float y0, float x1, float y1, float out[4]);

static inline float absf(float v) { return v < 0.0f ? -v : v; }

/* floor and ceiling as integers, saturating far from the range pixels can have. */
static inline INT floor_int(float v)
{
    if (!(v > -1.0e9f))
        return -1000000000;
    if (!(v < 1.0e9f))
        return 1000000000;
    INT i = (INT)v;
    return (float)i > v ? i - 1 : i;
}

static inline INT ceil_int(float v)
{
    if (!(v > -1.0e9f))
        return -1000000000;
    if (!(v < 1.0e9f))
        return 1000000000;
    INT i = (INT)v;
    return (float)i < v ? i + 1 : i;
}

/* ------------------------------------------------------------------ spans */

typedef enum SpanKind {
    SPAN_IDENTITY,
    SPAN_BILINEAR,      /* floats and 11 bit weights */
    SPAN_BILINEAR_FIXED, /* 16.16 positions and 8 bit weights, of the 32 bit library */
    SPAN_NEAREST,
    SPAN_BICUBIC,
    SPAN_STRETCH,
} SpanKind;

struct Stretch;

typedef struct ImgSpan {
    SpanKind kind;
    const UINT32 *px;   /* the premultiplied pixels of the locked rows */
    INT w, h;
    SSIZE_T stride;     /* in pixels, may be negative */
    INT wrap;
    UINT32 clamp;       /* the clamp color as it was given, not premultiplied */
    Affine inv;         /* device to picture */
    INT off_x, off_y;   /* identity: the whole pixel shift */
    /* the fixed point state of the bilinear span of the 32 bit library */
    BOOL fix_ok, fix_matrix_ok;
    INT fix_m11, fix_m12, fix_m21, fix_m22, fix_dx, fix_dy;
    INT fix_step_u, fix_step_v;
    struct Stretch *stretch;
} ImgSpan;

typedef struct SpanSetup {
    const UINT32 *px;
    INT w, h;
    SSIZE_T stride;
    INT wrap;
    UINT32 clamp;
    InterpolationMode mode;
    Affine fwd;         /* picture to device */
    float src[4];       /* the source rectangle the driver was given, in the pixels of the locked rows */
    float dest[4];      /* the device rectangle of its corners: x, y, width, height */
} SpanSetup;

/* the filter of the high quality modes for a scale of the picture, see scale.c. */
struct Stretch *stretch_create(const SpanSetup *setup, BOOL cubic);
void stretch_free(struct Stretch *s);
void stretch_run(struct Stretch *s, INT y, INT x0, INT x1, UINT32 *out);

/*
 * the rows top to bottom of the picture averaged down to nw by nh, premultiplied, adjusted by the attributes of a
 * bitmap. A new buffer of nw pixels a row.
 */
GpStatus area_scale(const GBitmap *b, struct GAttrs *attrs, INT nw, INT nh, INT top, INT bottom, UINT32 **out);

/* false when the span cannot be made: the status of the draw is then OutOfMemory. */
BOOL span_create(ImgSpan *s, const SpanSetup *setup);
void span_free(ImgSpan *s);
/* the pixels x0 to x1 of row y, premultiplied. */
void span_run(ImgSpan *s, INT y, INT x0, INT x1, UINT32 *out);

/* the cubic weights of the picture scalers: the kernel at k / 64 for k = 0 to 128, as 16.16 fractions. */
const INT *cubic_table(void);

#endif
