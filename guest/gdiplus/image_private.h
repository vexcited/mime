/*
 * the parts of image drawing: the matrices with their kind, the samplers that turn a run of device pixels into
 * premultiplied pixels of a picture, and the filters that shrink or stretch a picture first. The rules are in
 * docs/gdiplus.md.
 *
 * a sampler is asked for the pixels x0 to x1 of one device row, and finds the positions in the picture from the
 * inverse matrix at both ends of the run, so the runs the caller asks for matter: the positions in between are
 * interpolated.
 */
#ifndef MIME_GDIPLUS_IMAGE_PRIVATE_H
#define MIME_GDIPLUS_IMAGE_PRIVATE_H

#include "gp_private.h"

/* ------------------------------------------------------------------ matrices */

/*
 * what a matrix does to a point, as bits. A matrix whose off diagonal part is tiny counts as a plain scale and the
 * code that uses it ignores that part. 0 is no change at all, MAT_SHIFT alone a translation.
 */
enum {
    MAT_SHIFT = 1,    /* a translation */
    MAT_SCALE = 2,    /* the diagonal differs from one */
    MAT_ROTATE = 4,   /* the off diagonal part counts and the matrix is near a rotation */
    MAT_SHEAR = 8,    /* the off diagonal part counts and the matrix is anything else */
    MAT_ANY_LINEAR = MAT_SCALE | MAT_ROTATE | MAT_SHEAR,
    MAT_TURNING = MAT_ROTATE | MAT_SHEAR,
};

typedef struct ImgMat {
    float m11, m12, m21, m22, dx, dy;
    UINT kind;
} ImgMat;

void mat_identity(ImgMat *m);
void mat_set(ImgMat *m, float m11, float m12, float m21, float m22, float dx, float dy);
/* works out the kind from the numbers, after they were changed by hand. */
void mat_classify(ImgMat *m);
void mat_from_vec(ImgMat *m, const MimeVecMatrix *v);
/* out = first, then second. out may be either. */
void mat_then(ImgMat *out, const ImgMat *first, const ImgMat *second);
/* false when the determinant is zero. A matrix of kind 0 is left alone. */
BOOL mat_invert(ImgMat *m);
/* the matrix that takes the rectangle x, y, width, height to the parallelogram of three points, false for no area. */
BOOL mat_from_points(ImgMat *m, const float pts[6], const float rect[4]);
void mat_apply(const ImgMat *m, float *pts, INT count);
/* a translation or a scale done before the matrix, or after it when after is set. */
void mat_shift(ImgMat *m, float tx, float ty, BOOL after);
void mat_stretch(ImgMat *m, float sx, float sy, BOOL after);
/* a translation by whole pixels, give or take 1/64. */
BOOL mat_is_pixel_shift(const ImgMat *m);
/* the box x, y, width, height of a rectangle after the matrix; a size under 0.0006 is 0, not a number stays one. */
void mat_box(const ImgMat *m, float x0, float y0, float x1, float y1, float out[4]);
/* which of the 7 turns and flips of GdipImageRotateFlip the linear part is, 0 when it is none of them. */
INT mat_turn_type(const ImgMat *m);

/* floor and ceiling as integers, held to a range far wider than any picture. */
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

/* a number as 16.16, rounded half up and held to what 32 bits can say. */
static inline INT fix16(float v)
{
    float f = v * 65536.0f + 0.5f;
    if (!(f > -2147483648.0f))
        return (INT)0x80000000;
    if (!(f < 2147483648.0f))
        return 0x7fffffff;
    INT i = (INT)f;
    return (float)i > f ? i - 1 : i;
}

/* ------------------------------------------------------------------ samplers */

struct Stretch;

typedef enum SamplerKind {
    SAMPLER_COPY,       /* whole pixel shift */
    SAMPLER_BILINEAR,   /* floats, 11 bit weights */
    SAMPLER_BILINEAR16, /* 16.16 positions and 8 bit weights, of the 32 bit library */
    SAMPLER_NEAREST,
    SAMPLER_BICUBIC,
    SAMPLER_STRETCH,    /* the area filter of the high quality modes */
} SamplerKind;

typedef struct Sampler {
    SamplerKind kind;
    const UINT32 *px;    /* the premultiplied pixels of the rows that were read */
    INT w, h;
    SSIZE_T stride;      /* in pixels, may be negative */
    INT wrap;
    UINT32 clamp;        /* the clamp color as the attributes gave it, not premultiplied */
    ImgMat inv;          /* device to picture */
    INT shift_x, shift_y; /* the whole pixel shift of a copy */
    /* the 16.16 form of the inverse, for the 32 bit bilinear sampler */
    BOOL fixed_matrix;
    INT f11, f12, f21, f22, fdx, fdy;
    INT step_u, step_v;
    struct Stretch *stretch;
} Sampler;

typedef struct SamplerSetup {
    const UINT32 *px;
    INT w, h;
    SSIZE_T stride;
    INT wrap;
    UINT32 clamp;
    InterpolationMode mode;
    ImgMat fwd;          /* picture to device */
    float src[4];        /* the source rectangle in the rows that were read: x, y, width, height */
    float dest[4];       /* where its corners are on the device: x, y, and the size from corner 0 to corner 2 */
} SamplerSetup;

/* false when none can be made, which is when the rows are empty or memory is short. */
BOOL sampler_create(Sampler *s, const SamplerSetup *setup);
void sampler_free(Sampler *s);
/* the premultiplied pixels x0 to x1 of device row y. */
void sampler_run(Sampler *s, INT y, INT x0, INT x1, UINT32 *out);

/* the area filter for a picture stretched or shrunk with the kernel of the high quality modes, NULL when it cannot be. */
struct Stretch *stretch_create(const SamplerSetup *setup, BOOL cubic);
void stretch_free(struct Stretch *s);
void stretch_run(struct Stretch *s, INT y, INT x0, INT x1, UINT32 *out);

/*
 * the rows top to bottom of a picture averaged down to nw by nh, premultiplied and adjusted by the attributes of a
 * bitmap. A new buffer of nw pixels a row, and the width it really has when the picture is narrower.
 */
GpStatus area_scale(const GBitmap *b, struct GAttrs *attrs, INT nw, INT nh, INT top, INT bottom, UINT32 **out,
                    INT *out_w);

#endif
