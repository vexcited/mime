/*
 * freestanding 2D vector geometry and scan conversion, the engine below the
 * GDI+ flat API. paths with GDI+ point types, flattening, stroking, an
 * antialiased scan converter, run based coverage masks for clipping, hit
 * testing and scanline blending.
 *
 * conventions, all of them the GDI+ ones:
 *   y grows downward and positive angles are clockwise on screen.
 *   matrices use the row vector form, x' = x * m11 + y * m21 + dx.
 *   pixel x, y covers one unit square from x, y and its center is x + .5, y + .5.
 *
 * robustness: coordinates are clamped to +-MIME_VEC_COORD_LIMIT and infinities
 * are clamped too. a NaN makes the builders and the flattener fail with
 * MIME_VEC_INVALID, the scan converter and the stroker skip the polygon or
 * subpath that holds one. all memory comes from a MimeAllocator and every size
 * is capped. a function that fails leaves its output valid but incomplete.
 *
 * values marked GUESS below were not verified against the real gdiplus.dll.
 * the anchor cap shapes, the miter limit rule and the dash cap pattern
 * adjustment were read from it.
 */
#ifndef MIME_LIB_VECTOR_H
#define MIME_LIB_VECTOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../alloc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* status and limits. */

#define MIME_VEC_OK 0
#define MIME_VEC_NOMEM (-1)
#define MIME_VEC_INVALID (-2)
#define MIME_VEC_TOOBIG (-3)

#define MIME_VEC_COORD_LIMIT 1.0e9f
/* largest point count of a flattened or stroked result. */
#define MIME_VEC_MAX_POINTS (1u << 21)
/* largest point count of a path. */
#define MIME_VEC_MAX_PATH_POINTS (1u << 24)
/* default flatness of GDI+ in device pixels. */
#define MIME_VEC_DEFAULT_FLATNESS 0.25f
/* the scan converter accepts clip coordinates up to this magnitude. */
#define MIME_VEC_CLIP_LIMIT (1 << 26)

/* math. */

float mime_vec_sqrtf(float x);
float mime_vec_floorf(float x);
float mime_vec_ceilf(float x);
/* sine and cosine of an angle in radians, absolute error about 1e-7. */
void mime_vec_sincosf(float a, float *s, float *c);
/* angle of the vector x, y in -pi to pi, absolute error about 3e-7. */
float mime_vec_atan2f(float y, float x);
/* true when v is neither NaN nor infinite. */
static inline bool mime_vec_finite(float v)
{
    return v == v && v - v == 0.0f;
}
/* clamps to the coordinate limit, NaN becomes 0. */
static inline float mime_vec_clamp_coord(float v)
{
    if (!(v == v))
        return 0.0f;
    if (v < -MIME_VEC_COORD_LIMIT)
        return -MIME_VEC_COORD_LIMIT;
    if (v > MIME_VEC_COORD_LIMIT)
        return MIME_VEC_COORD_LIMIT;
    return v;
}

/* basic types. */

typedef struct MimeVecPoint {
    float x, y;
} MimeVecPoint;

/* float box, right and bottom are the largest coordinates and not exclusive. */
typedef struct MimeVecBounds {
    float left, top, right, bottom;
} MimeVecBounds;

/* integer rectangle with exclusive right and bottom. it is empty when right <= left or bottom <= top. */
typedef struct MimeVecRect {
    int32_t left, top, right, bottom;
} MimeVecRect;

typedef struct MimeVecMatrix {
    float m11, m12, m21, m22, dx, dy;
} MimeVecMatrix;

void mime_vec_matrix_identity(MimeVecMatrix *m);
/* out = a * b, a is applied first. out may alias a or b. */
void mime_vec_matrix_multiply(MimeVecMatrix *out, const MimeVecMatrix *a, const MimeVecMatrix *b);
/* returns false for a singular or non finite matrix. */
bool mime_vec_matrix_invert(MimeVecMatrix *out, const MimeVecMatrix *m);
/* the next three prepend, like GDI+ MatrixOrderPrepend, so the new transform is applied first. */
void mime_vec_matrix_translate(MimeVecMatrix *m, float dx, float dy);
void mime_vec_matrix_scale(MimeVecMatrix *m, float sx, float sy);
void mime_vec_matrix_rotate(MimeVecMatrix *m, float degrees);
/* scale factor of the longest axis, used to turn a device tolerance into a world one. */
float mime_vec_matrix_max_scale(const MimeVecMatrix *m);
static inline bool mime_vec_matrix_is_identity(const MimeVecMatrix *m)
{
    return m->m11 == 1.0f && m->m12 == 0.0f && m->m21 == 0.0f && m->m22 == 1.0f && m->dx == 0.0f &&
           m->dy == 0.0f;
}
static inline MimeVecPoint mime_vec_xform(const MimeVecMatrix *m, float x, float y)
{
    MimeVecPoint p = {x * m->m11 + y * m->m21 + m->dx, x * m->m12 + y * m->m22 + m->dy};
    return p;
}

/* polygons and polylines, the flattened data. */

typedef struct MimeVecSub {
    uint32_t first;  /* index of the first point. */
    uint32_t count;  /* point count. */
    uint32_t closed; /* nonzero when the closing segment exists. fills always close implicitly. */
} MimeVecSub;

typedef struct MimeVecPolys {
    const MimeAllocator *alloc;
    MimeVecPoint *pts;
    uint32_t npts, cap_pts;
    MimeVecSub *subs;
    uint32_t nsubs, cap_subs;
    uint32_t cur_first; /* first point of the subpath being built. */
    bool open;          /* a subpath is being built. */
} MimeVecPolys;

void mime_vec_polys_init(MimeVecPolys *p, const MimeAllocator *alloc);
void mime_vec_polys_free(MimeVecPolys *p);
void mime_vec_polys_clear(MimeVecPolys *p);
/* building is begin, add points, end. a subpath with no point is dropped. */
int mime_vec_polys_begin(MimeVecPolys *p);
int mime_vec_polys_add(MimeVecPolys *p, float x, float y);
int mime_vec_polys_end(MimeVecPolys *p, bool closed);
/* a whole subpath at once. */
int mime_vec_polys_add_sub(MimeVecPolys *p, const MimeVecPoint *pts, uint32_t count, bool closed);
/* exact bounds of the points, false when there are none. */
bool mime_vec_polys_bounds(const MimeVecPolys *p, MimeVecBounds *b);
void mime_vec_polys_transform(MimeVecPolys *p, const MimeVecMatrix *m);

/* paths. */

#define MIME_VEC_PT_START 0
#define MIME_VEC_PT_LINE 1
#define MIME_VEC_PT_BEZIER 3
#define MIME_VEC_PT_TYPE_MASK 0x07
/* the dash mode flag is kept in the data but the stroker ignores it. this is a limitation. */
#define MIME_VEC_PT_DASHMODE 0x10
#define MIME_VEC_PT_MARKER 0x20
#define MIME_VEC_PT_CLOSE 0x80

typedef struct MimeVecPath {
    const MimeAllocator *alloc;
    MimeVecPoint *pts;
    uint8_t *types;
    uint32_t count, capacity;
    bool need_start; /* the next point must start a figure. */
} MimeVecPath;

void mime_vec_path_init(MimeVecPath *p, const MimeAllocator *alloc);
void mime_vec_path_free(MimeVecPath *p);
void mime_vec_path_clear(MimeVecPath *p);
int mime_vec_path_copy(MimeVecPath *dst, const MimeVecPath *src);
/* appends raw GDI+ point and type arrays. */
int mime_vec_path_add_raw(MimeVecPath *p, const MimeVecPoint *pts, const uint8_t *types, uint32_t count);
/* with connect, the first figure of other continues the open figure of p with a line. */
int mime_vec_path_append(MimeVecPath *p, const MimeVecPath *other, bool connect);
void mime_vec_path_transform(MimeVecPath *p, const MimeVecMatrix *m);

/* the next figure starts a new subpath. */
void mime_vec_path_start_figure(MimeVecPath *p);
int mime_vec_path_close_figure(MimeVecPath *p);
int mime_vec_path_move_to(MimeVecPath *p, float x, float y);
int mime_vec_path_line_to(MimeVecPath *p, float x, float y);
int mime_vec_path_bezier_to(MimeVecPath *p, float x1, float y1, float x2, float y2, float x3, float y3);

/*
 * the adders follow GDI+. lines, curves and arcs continue the open figure with
 * a connecting line when there is one. the closed shapes always start a new
 * figure and close it. invalid sizes and counts give MIME_VEC_INVALID.
 */
int mime_vec_path_add_line(MimeVecPath *p, float x0, float y0, float x1, float y1);
int mime_vec_path_add_lines(MimeVecPath *p, const MimeVecPoint *pts, uint32_t count);
int mime_vec_path_add_bezier(MimeVecPath *p, float x0, float y0, float x1, float y1, float x2, float y2,
                             float x3, float y3);
/* count must be 1 + 3k. */
int mime_vec_path_add_beziers(MimeVecPath *p, const MimeVecPoint *pts, uint32_t count);
int mime_vec_path_add_rect(MimeVecPath *p, float x, float y, float w, float h);
/* starts at angle 0 on the right side and runs clockwise with four Beziers. GUESS for the start point. */
int mime_vec_path_add_ellipse(MimeVecPath *p, float x, float y, float w, float h);
/*
 * elliptical arc inside the rectangle. the angles are in degrees, measured
 * clockwise from the x axis on the unit circle before it is scaled to the
 * ellipse, so the arc point at angle a is the center plus rx cos a and ry sin a.
 * a sweep of 360 or more is a full ellipse and a zero sweep adds nothing.
 */
int mime_vec_path_add_arc(MimeVecPath *p, float x, float y, float w, float h, float start, float sweep);
/* the center, a line to the arc start, the arc, closed. */
int mime_vec_path_add_pie(MimeVecPath *p, float x, float y, float w, float h, float start, float sweep);
int mime_vec_path_add_polygon(MimeVecPath *p, const MimeVecPoint *pts, uint32_t count);
/*
 * cardinal spline through the points. tension 0 gives straight lines and 0.5
 * is the Catmull-Rom spline, the GDI+ default. the end tangents of the open
 * form use the first and last chord. GUESS for the end tangents.
 */
int mime_vec_path_add_curve(MimeVecPath *p, const MimeVecPoint *pts, uint32_t count, float tension);
int mime_vec_path_add_closed_curve(MimeVecPath *p, const MimeVecPoint *pts, uint32_t count, float tension);

/*
 * flattens raw point and type arrays into polylines and appends them to out.
 * the matrix, when given, is applied to the control points first. flatness is
 * the largest distance in the output space between the polyline and the curve
 * and is 0.25 when it is not positive. Beziers are subdivided adaptively on a
 * fixed size stack, at most 1024 segments per curve. a closed subpath does not
 * repeat its first point. a Bezier with fewer than three points left is
 * dropped and a figure that goes on after a close restarts at the previous
 * start. MIME_VEC_INVALID for a NaN, and out is left as it was then.
 */
int mime_vec_flatten(MimeVecPolys *out, const MimeVecPoint *pts, const uint8_t *types, uint32_t count,
                     const MimeVecMatrix *m, float flatness);
int mime_vec_path_flatten(const MimeVecPath *p, const MimeVecMatrix *m, float flatness, MimeVecPolys *out);

/* stroker. the numbers are the GDI+ values. */

enum {
    MIME_VEC_CAP_FLAT = 0,
    MIME_VEC_CAP_SQUARE = 1,
    MIME_VEC_CAP_ROUND = 2,
    MIME_VEC_CAP_TRIANGLE = 3,
    MIME_VEC_CAP_NOANCHOR = 0x10,
    MIME_VEC_CAP_SQUAREANCHOR = 0x11,
    MIME_VEC_CAP_ROUNDANCHOR = 0x12,
    MIME_VEC_CAP_DIAMONDANCHOR = 0x13,
    MIME_VEC_CAP_ARROWANCHOR = 0x14
};
enum { MIME_VEC_DASHCAP_FLAT = 0, MIME_VEC_DASHCAP_ROUND = 2, MIME_VEC_DASHCAP_TRIANGLE = 3 };
enum { MIME_VEC_JOIN_MITER = 0, MIME_VEC_JOIN_BEVEL = 1, MIME_VEC_JOIN_ROUND = 2, MIME_VEC_JOIN_MITERCLIPPED = 3 };
enum {
    MIME_VEC_DASH_SOLID = 0,
    MIME_VEC_DASH_DASH = 1,       /* 3 on and 1 off. */
    MIME_VEC_DASH_DOT = 2,        /* 1 on and 1 off. */
    MIME_VEC_DASH_DASHDOT = 3,    /* 3 1 1 1. */
    MIME_VEC_DASH_DASHDOTDOT = 4, /* 3 1 1 1 1 1. */
    MIME_VEC_DASH_CUSTOM = 5
};
enum { MIME_VEC_ALIGN_CENTER = 0, MIME_VEC_ALIGN_INSET = 1 };

/*
 * a pen. the polylines given to the stroker live in the stroke space and the
 * width is in that space. the offset geometry is built there and the result
 * is transformed by out, so a pen transform and the world to device transform
 * can be combined in out and a non uniform scale stretches the stroke
 * correctly. flatness is the curve tolerance in stroke space units, the
 * device flatness divided by the scale of out. it also sets how smooth the
 * round joins and caps are.
 *
 * dash lengths are multiples of the pen width. the width actually used is the
 * larger of width and min_width. for a GDI+ zero width pen the caller passes
 * a min_width of one device pixel in stroke space units.
 */
typedef struct MimeVecStroke {
    float width;
    float min_width;
    int start_cap, end_cap, dash_cap, join;
    float miter_limit; /* ratio of the miter length to the width, the GDI+ default is 10. */
    int align;
    int dash_style;
    const float *dash; /* custom pattern in width multiples, on off on off and so on. */
    uint32_t ndash;
    float dash_offset; /* in width multiples. */
    /*
     * compound pen, pairs of fractions of the width measured from the left of
     * the direction of travel, start then end, with 0 <= start < end <= 1.
     */
    const float *compound;
    uint32_t ncompound; /* number of floats, 2 per line. */
    float flatness;
    const MimeVecMatrix *out;
} MimeVecStroke;

void mime_vec_stroke_defaults(MimeVecStroke *s, float width);
/*
 * widens the polylines into polygons to be filled with the nonzero rule and
 * appends them to out. every polygon has the same orientation, so overlapping
 * pieces never cancel and antialiasing shows no seams.
 *
 * behaviors:
 *   an open subpath gets start and end caps. a closed one gets joins at every
 *   vertex and no caps.
 *   a zero length subpath, which means two or more equal points, is a disc
 *   with a round cap, a square with a square cap, a diamond with a triangle
 *   cap and nothing with a flat cap. a lone point draws nothing.
 *   a miter join past the miter limit becomes a bevel. the limit is the ratio
 *   of the miter length to the width, 1 over the sine of half the interior
 *   angle, as in gdiplus.dll. a limit under 1 acts as 1. MiterClipped cuts
 *   the miter at miter_limit times half the width from the vertex, as SVG 2. GUESS
 *   dashes follow gdiplus.dll. the pattern period is kept when the dash cap is
 *   round or triangle: the dashes get shorter by one width, at least 0.001
 *   width, and the gaps longer by one width, so the caps fill the difference.
 *   the dash ends take the dash cap. at the real ends of an open subpath the
 *   start and end caps are used when they are not flat. GUESS
 *   a closed subpath that starts and ends inside a dash merges the two
 *   halves into one dash. GUESS
 *   a pattern that would need too many dashes falls back to a solid line.
 *   inset alignment puts the pen on the interior side of a closed subpath,
 *   the side is picked from the orientation of the polygon, so the outer edge
 *   of the pen follows the path and convex corners are sharp. open subpaths
 *   stay centered. GUESS
 *   anchor caps are filled shapes, with the proportions of the reference caps
 *   of gdiplus.dll scaled by the pen width, at least one unit. the square
 *   anchor has a half side of 0.7071, the round one a radius of 1, the
 *   diamond a half diagonal of 1, and the arrow is a triangle with its apex
 *   on the line end and a base 1.732 behind it and 2 wide. the arrow pulls
 *   the line back by one scale. the width scale of 1 and the minimum scale
 *   are GUESS.
 */
int mime_vec_stroke(MimeVecPolys *out, const MimeVecPolys *in, const MimeVecStroke *pen);
/* exact bounds of the widened outline, false when empty or on failure. */
bool mime_vec_stroke_bounds(const MimeVecPolys *in, const MimeVecStroke *pen, MimeVecBounds *b);

/* scan converter. */

enum { MIME_VEC_FILL_ALTERNATE = 0, MIME_VEC_FILL_WINDING = 1 };

enum {
    /* a pixel is set when its center is inside, with the top left rule on the edges. */
    MIME_VEC_AA_NONE = 0,
    /*
     * 8 by 4 samples per pixel on a box filter and coverage rounded from
     * count * 255 / 32. the 32 levels are the GDI+ SmoothingModeAntiAlias
     * behavior. GUESS
     */
    MIME_VEC_AA_8X4 = 1,
    /* 8 by 8 samples and 64 levels, as SmoothingModeAntiAlias8x8. GUESS. */
    MIME_VEC_AA_8X8 = 2,
    /* exact horizontal area and 16 sub scanlines, for the scaling quality paths. */
    MIME_VEC_AA_EXACT = 3
};

/* called for the rows touched with coverage 0..255 for count pixels from x. */
typedef void (*MimeVecSpanFn)(void *user, int y, int x, int count, const uint8_t *coverage);

/*
 * fills the polygons inside the clip rectangle. every subpath closes
 * implicitly. memory is proportional to the points and the clip width: the
 * rows are streamed, tall clips are split in bands and clips wider than 16384
 * in vertical strips. the rows come top to bottom within a strip, so for a
 * clip wider than 16384 the order is strip by strip. the callback is never
 * called with pixels outside the clip. polygons with a NaN are skipped.
 * MIME_VEC_NOMEM when memory is short, and some rows may have been emitted.
 */
int mime_vec_fill(const MimeAllocator *a, const MimeVecPolys *polys, int rule, int aa, const MimeVecRect *clip,
                  MimeVecSpanFn fn, void *user);
/* the same for one plain polygon. */
int mime_vec_fill_polygon(const MimeAllocator *a, const MimeVecPoint *pts, uint32_t count, int rule, int aa,
                          const MimeVecRect *clip, MimeVecSpanFn fn, void *user);
/* the integer rectangle of the pixels a shape with these bounds may touch, clamped to the limits. */
MimeVecRect mime_vec_bounds_to_rect(const MimeVecBounds *b);

/* coverage masks for clip regions. */

/*
 * a mask is a set of horizontal bands, each with a sorted list of runs of
 * constant coverage 1..255, and everything else is 0. a rectangle costs one
 * band and one run, an antialiased shape one band per row at most. identical
 * neighboring rows are merged. the infinite region is a large rectangle, for
 * instance the clip limit in each direction.
 */
typedef struct MimeVecRun {
    int32_t x0, x1;
    uint32_t value;
} MimeVecRun;

typedef struct MimeVecBand {
    int32_t y0, y1;
    uint32_t first, count;
} MimeVecBand;

typedef struct MimeVecMask {
    const MimeAllocator *alloc;
    MimeVecBand *bands;
    uint32_t nbands, cap_bands;
    MimeVecRun *runs;
    uint32_t nruns, cap_runs;
    MimeVecRect bounds; /* tight bounds of the nonzero coverage. */
    bool failed;        /* an operation ran out of memory or hit a limit. */
} MimeVecMask;

/* the GDI+ CombineMode numbering. */
enum {
    MIME_VEC_COMBINE_REPLACE = 0,
    MIME_VEC_COMBINE_INTERSECT = 1,
    MIME_VEC_COMBINE_UNION = 2,
    MIME_VEC_COMBINE_XOR = 3,
    MIME_VEC_COMBINE_EXCLUDE = 4,   /* a minus b. */
    MIME_VEC_COMBINE_COMPLEMENT = 5 /* b minus a. */
};
enum { MIME_VEC_TEST_OUTSIDE = 0, MIME_VEC_TEST_INSIDE = 1, MIME_VEC_TEST_PARTIAL = 2 };

void mime_vec_mask_init(MimeVecMask *m, const MimeAllocator *alloc);
void mime_vec_mask_free(MimeVecMask *m);
void mime_vec_mask_clear(MimeVecMask *m);
int mime_vec_mask_copy(MimeVecMask *dst, const MimeVecMask *src);
int mime_vec_mask_set_rect(MimeVecMask *m, const MimeVecRect *r);
/* rasterizes a shape and keeps only the pixels inside clip. */
int mime_vec_mask_set_polys(MimeVecMask *m, const MimeVecPolys *polys, int rule, int aa, const MimeVecRect *clip);
/*
 * dst = a op b on the coverage with rounded products. with a and b as
 * fractions the results are intersect a*b, union a+b-a*b, xor a*ib+b*ia,
 * exclude a*ib and complement b*ia, where ia and ib are one minus a and b.
 * dst may be a or b. when it fails dst is left as it was.
 */
int mime_vec_mask_combine(MimeVecMask *dst, const MimeVecMask *a, const MimeVecMask *b, int op);
/* dst is one minus src inside r and 0 outside r. dst may be src. */
int mime_vec_mask_invert(MimeVecMask *dst, const MimeVecMask *src, const MimeVecRect *r);
void mime_vec_mask_translate(MimeVecMask *m, int dx, int dy);
bool mime_vec_mask_is_empty(const MimeVecMask *m);
/* true when the mask is exactly one fully covered rectangle, which is returned in r when r is not NULL. */
bool mime_vec_mask_is_rect(const MimeVecMask *m, MimeVecRect *r);
MimeVecRect mime_vec_mask_bounds(const MimeVecMask *m);
uint32_t mime_vec_mask_coverage_at(const MimeVecMask *m, int x, int y);
/* INSIDE only when every pixel of r has coverage 255, OUTSIDE only when every pixel has 0. */
int mime_vec_mask_test_rect(const MimeVecMask *m, const MimeVecRect *r);
/* multiplies a coverage row by the mask in place. */
void mime_vec_mask_apply_row(const MimeVecMask *m, int y, int x, int count, uint8_t *coverage);
/* feeds the mask inside r to a span callback, one call per row and run group. */
void mime_vec_mask_emit(const MimeVecMask *m, const MimeVecRect *r, MimeVecSpanFn fn, void *user);

/* hit testing. */

/* winding number of the closed polygons around the point. every subpath closes. */
int mime_vec_winding_at(const MimeVecPolys *p, float x, float y);
bool mime_vec_point_in_polys(const MimeVecPolys *p, float x, float y, int rule);
/* squared distance from the point to the nearest segment, honoring the closed flag. negative when empty. */
float mime_vec_distance2(const MimeVecPolys *p, float x, float y);
/* true when the point is within dist of the polylines, which is a hit on a stroke of width 2 * dist with round caps. */
bool mime_vec_point_near(const MimeVecPolys *p, float x, float y, float dist);

/* scanline blending. */

/*
 * destination pixels are 32 bit BGRA, which is 0xAARRGGBB as a little endian
 * dword, with straight alpha, ARGB, or premultiplied alpha, PARGB. colors are
 * straight unless stated.
 *
 * R[x] below is the exact rounded division of x by 255. the code adds 128 to
 * x, adds that sum shifted right by 8 to itself and shifts the result right
 * by 8. it is exact for 0 <= x <= 65535 and no ties exist because 255 is odd.
 * mime_vec_div255 is R. Q[n, d] is n / d rounded to nearest.
 *
 * effective source alpha: e = R[cov * alpha] and sa = R[srcA * e].
 * SourceOver, premultiplied or opaque destination, for each channel c of B G
 * R and A, with srcC taken as 255 for the A channel:
 *   out = R[srcC * sa + dst * ia] where ia = 255 - sa.
 * SourceOver, straight destination with alpha dA:
 *   den = sa * 255 + dA * ia
 *   outA = R[den]
 *   outC = Q[srcC * sa * 255 + dstC * dA * ia, den], and the pixel is unchanged when den is 0.
 * SourceCopy, where the coverage is the interpolation weight and alpha scales
 * the source alpha only:
 *   premultiplied destination: out = R[srcP * cov + dst * icov] where
 *     icov = 255 - cov, srcP = R[srcC * sa] and srcP of the A channel is sa.
 *   straight destination: den = sa * cov + dA * icov, outA = R[den] and
 *     outC = Q[srcC * sa * cov + dstC * dA * icov, den]. full coverage copies
 *     the source pixel with its alpha scaled.
 * a premultiplied source uses srcP = R[srcPC * e] instead of the product by sa
 * and is converted back to straight for a straight destination.
 * this is our best formulation of the GDI+ 8 bit math and it is not verified
 * to be bit exact.
 */
enum { MIME_VEC_BLEND_OVER = 0, MIME_VEC_BLEND_COPY = 1 };
enum { MIME_VEC_FMT_ARGB = 0, MIME_VEC_FMT_PARGB = 1 };

/* constant straight 0xAARRGGBB color. cov may be NULL for full coverage. */
void mime_vec_blend_color(uint32_t *dst, uint32_t color, const uint8_t *cov, uint32_t count, int mode, int dst_fmt);
/* a source pixel per destination pixel, alpha is a constant 0..255 factor. cov may be NULL. */
void mime_vec_blend_row(uint32_t *dst, const uint32_t *src, const uint8_t *cov, uint32_t count, uint32_t alpha,
                        int mode, int dst_fmt, int src_fmt);
/* exact rounded x / 255 for 0 <= x <= 65535. */
static inline uint32_t mime_vec_div255(uint32_t x)
{
    x += 128;
    return (x + (x >> 8)) >> 8;
}

#ifdef __cplusplus
}
#endif

#endif
