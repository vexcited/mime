/*
 * brush sources for the fill loop: each gives a row of premultiplied device pixels.
 *
 * gradients sample each pixel at its integer device coordinate, mapped into brush space by the inverse of the
 * brush and world to device transforms, and interpolate premultiplied float colors, then round. a hatch is an
 * 8 by 8 pattern aligned to the render origin. a texture is read with bilinear filtering, or directly when the
 * brush moves it by whole pixels.
 */
#include "gp_private.h"

#include "gamma_table.inc"
#include "hatch_table.inc"

static UINT32 pack_pargb(float a, float r, float g, float b)
{
    if (a < 0.0f)
        a = 0.0f;
    if (a > 255.0f)
        a = 255.0f;
    if (r < 0.0f)
        r = 0.0f;
    if (g < 0.0f)
        g = 0.0f;
    if (b < 0.0f)
        b = 0.0f;
    if (r > a)
        r = a;
    if (g > a)
        g = a;
    if (b > a)
        b = a;
    UINT32 ia = (UINT32)(a + 0.5f), ir = (UINT32)(r + 0.5f), ig = (UINT32)(g + 0.5f), ib = (UINT32)(b + 0.5f);
    return ia << 24 | ir << 16 | ig << 8 | ib;
}

typedef struct FColor {
    float a, r, g, b; /* premultiplied, 0..255. */
} FColor;

static FColor fcolor_of(ARGB c, BOOL gamma)
{
    FColor f;
    float a = (float)(c >> 24);
    float r = (float)((c >> 16) & 255), g = (float)((c >> 8) & 255), b = (float)(c & 255);
    if (gamma) {
        r = gamma_table[(c >> 16) & 255];
        g = gamma_table[(c >> 8) & 255];
        b = gamma_table[c & 255];
    }
    f.a = a;
    f.r = r * a / 255.0f;
    f.g = g * a / 255.0f;
    f.b = b * a / 255.0f;
    return f;
}

static FColor fcolor_lerp(const FColor *x, const FColor *y, float t)
{
    FColor f = {x->a + (y->a - x->a) * t, x->r + (y->r - x->r) * t, x->g + (y->g - x->g) * t, x->b + (y->b - x->b) * t};
    return f;
}

/* maps a linear channel back to 0..255 through the inverse of the gamma table. */
static float gamma_inverse(float v)
{
    if (v <= 0.0f)
        return 0.0f;
    if (v >= 255.0f)
        return 255.0f;
    INT lo = 0, hi = 255;
    while (hi - lo > 1) {
        INT mid = (lo + hi) / 2;
        if (gamma_table[mid] <= v)
            lo = mid;
        else
            hi = mid;
    }
    float span = gamma_table[hi] - gamma_table[lo];
    return (float)lo + (span > 0.0f ? (v - gamma_table[lo]) / span : 0.0f);
}

static UINT32 pack_fcolor(const FColor *c, BOOL gamma)
{
    if (!gamma || c->a <= 0.0f)
        return pack_pargb(c->a, c->r, c->g, c->b);
    /* channels hold alpha times the linear value: unpremultiply, delinearize, premultiply again. */
    float k = 255.0f / c->a;
    float r = gamma_inverse(c->r * k) * c->a / 255.0f, g = gamma_inverse(c->g * k) * c->a / 255.0f,
          b = gamma_inverse(c->b * k) * c->a / 255.0f;
    return pack_pargb(c->a, r, g, b);
}

static float blend_factor(const GBlend *bl, float t)
{
    if (!bl->count)
        return t;
    if (bl->count == 1)
        return bl->factors[0];
    if (t <= bl->positions[0])
        return bl->factors[0];
    for (INT i = 1; i < bl->count; i++) {
        if (t <= bl->positions[i]) {
            float span = bl->positions[i] - bl->positions[i - 1];
            if (span <= 0.0f)
                return bl->factors[i];
            return bl->factors[i - 1] + (bl->factors[i] - bl->factors[i - 1]) * (t - bl->positions[i - 1]) / span;
        }
    }
    return bl->factors[bl->count - 1];
}

/* ------------------------------------------------------------------ solid, hatch */

typedef struct SolidSource {
    GSource base;
} SolidSource;

static void solid_fetch(GSource *s, INT x, INT y, INT count, UINT32 *out)
{
    for (INT i = 0; i < count; i++)
        out[i] = s->color;
}

static void simple_destroy(GSource *s) { gp_free(s); }

static BOOL close_enough(float a, float b)
{
    float d = a - b;
    return d > -0.0001f && d < 0.0001f;
}

typedef struct HatchSource {
    GSource base;
    const unsigned char *pattern;
    UINT32 fore, back, mid;
    INT ox, oy;
} HatchSource;

static void hatch_fetch(GSource *s, INT x, INT y, INT count, UINT32 *out)
{
    HatchSource *h = (HatchSource *)s;
    const unsigned char *row = h->pattern + ((y - h->oy) & 7) * 8;
    for (INT i = 0; i < count; i++) {
        unsigned char v = row[(x + i - h->ox) & 7];
        out[i] = v == 255 ? h->fore : (v == 0 ? h->back : h->mid);
    }
}

/* three parts background to one part foreground, per byte. */
static UINT32 hatch_between(UINT32 fore, UINT32 back)
{
    UINT32 out = 0;
    for (INT shift = 0; shift < 32; shift += 8)
        out |= ((3 * ((back >> shift) & 255) + ((fore >> shift) & 255)) >> 2) << shift;
    return out;
}

/* diagonal styles pull the foreground slightly toward the background. */
static UINT32 hatch_diagonal_fore(UINT32 fore, UINT32 back)
{
    UINT32 out = 0;
    for (INT shift = 0; shift < 32; shift += 8) {
        float f = (float)((fore >> shift) & 255), b = (float)((back >> shift) & 255);
        INT v = (INT)((f - b) * 0.91421354f + b);
        out |= (UINT32)(v > 255 ? 255 : v) << shift;
    }
    return out;
}

static GpStatus hatch_create(GGraphics *g, const GBrush *brush, GSource **out)
{
    HatchSource *h = gp_alloc_zero(sizeof(*h));
    if (!h)
        return OutOfMemory;
    UINT32 fore = gp_premultiply(brush->hatch_fore), back = gp_premultiply(brush->hatch_back);
    h->pattern = hatch_patterns[brush->hatch_style];
    h->back = back;
    h->mid = hatch_between(fore, back);
    HatchStyle style = brush->hatch_style;
    h->fore = style == HatchStyleForwardDiagonal || style == HatchStyleBackwardDiagonal ||
                      style == HatchStyleDiagonalCross
                  ? hatch_diagonal_fore(fore, back)
                  : fore;
    h->ox = g->origin.x;
    h->oy = g->origin.y;
    h->base.fetch = hatch_fetch;
    h->base.destroy = simple_destroy;
    h->base.opaque = (fore >> 24) == 255 && (back >> 24) == 255;
    *out = &h->base;
    return Ok;
}

/* ------------------------------------------------------------------ wrapping */

/* folds v, in tiles, into 0..1 by the wrap mode of this axis. */
static float wrap_axis(float v, BOOL flip, BOOL clamp_mode, BOOL *outside)
{
    if (clamp_mode) {
        if (v < 0.0f || v >= 1.0f) {
            *outside = TRUE;
            return 0.0f;
        }
        return v;
    }
    float f = mime_vec_floorf(v);
    float frac = v - f;
    if (flip && (((INT)f) & 1))
        frac = 1.0f - frac;
    return frac;
}

/* ------------------------------------------------------------------ linear gradients */

typedef struct LinearSource {
    GSource base;
    const GBrush *b;
    MimeVecMatrix inv; /* device to brush space */
    FColor c1, c2;
    float cx, cy;      /* rectangle center. */
    float dirx, diry, len;
    float rx, ry, rw, rh;
    BOOL flip_x, flip_y, clamp;
    INT table_n;
    UINT32 *table;
    BOOL has_preset;
    FColor *preset;
    GBlend blend;
    GPreset pre;
    BOOL gamma;
    INT mode;
} LinearSource;

static float sample_t(const LinearSource *s, float bx, float by, BOOL *outside)
{
    float u = (bx - s->rx) / s->rw, v = (by - s->ry) / s->rh;
    float t;
    switch (s->mode) {
    case LinearGradientModeHorizontal:
        t = wrap_axis(u, s->flip_x, s->clamp, outside);
        if (s->clamp && (v < 0.0f || v >= 1.0f))
            *outside = TRUE;
        return t;
    case LinearGradientModeVertical:
        t = wrap_axis(v, s->flip_y, s->clamp, outside);
        if (s->clamp && (u < 0.0f || u >= 1.0f))
            *outside = TRUE;
        return t;
    case LinearGradientModeForwardDiagonal: {
        float uu = wrap_axis(u, s->flip_x, s->clamp, outside), vv = wrap_axis(v, s->flip_y, s->clamp, outside);
        return (uu + vv) * 0.5f;
    }
    case LinearGradientModeBackwardDiagonal: {
        float uu = wrap_axis(u, s->flip_x, s->clamp, outside), vv = wrap_axis(v, s->flip_y, s->clamp, outside);
        return ((1.0f - uu) + vv) * 0.5f;
    }
    default: {
        /* line or angle: position along the direction, from the first corner to the opposite one. */
        float px, py, len;
        if (s->mode == GP_LINE_ANGLE && s->b->scalable) {
            px = (bx - s->rx) / s->rw - 0.5f;
            py = (by - s->ry) / s->rh - 0.5f;
            len = (s->dirx < 0 ? -s->dirx : s->dirx) + (s->diry < 0 ? -s->diry : s->diry);
        } else {
            px = bx - s->cx;
            py = by - s->cy;
            len = s->len;
        }
        t = 0.5f + (px * s->dirx + py * s->diry) / len;
        if (s->clamp)
            *outside = t < 0.0f || t >= 1.0f;
        else {
            float f = mime_vec_floorf(t);
            t -= f;
            if (s->flip_x && (((INT)f) & 1))
                t = 1.0f - t;
        }
        return t;
    }
    }
}

static UINT32 color_at(const LinearSource *s, float t)
{
    if (s->has_preset) {
        const GPreset *p = &s->pre;
        if (t <= p->positions[0])
            return pack_fcolor(&s->preset[0], s->gamma);
        for (INT i = 1; i < p->count; i++) {
            if (t <= p->positions[i]) {
                float span = p->positions[i] - p->positions[i - 1];
                float k = span > 0.0f ? (t - p->positions[i - 1]) / span : 1.0f;
                FColor f = fcolor_lerp(&s->preset[i - 1], &s->preset[i], k);
                return pack_fcolor(&f, s->gamma);
            }
        }
        return pack_fcolor(&s->preset[p->count - 1], s->gamma);
    }
    float k = blend_factor(&s->blend, t);
    if (k < 0.0f)
        k = 0.0f;
    if (k > 1.0f)
        k = 1.0f;
    FColor f = fcolor_lerp(&s->c1, &s->c2, k);
    return pack_fcolor(&f, s->gamma);
}

static void linear_fetch(GSource *src, INT x, INT y, INT count, UINT32 *out)
{
    LinearSource *s = (LinearSource *)src;
    float bx = (float)x * s->inv.m11 + (float)y * s->inv.m21 + s->inv.dx;
    float by = (float)x * s->inv.m12 + (float)y * s->inv.m22 + s->inv.dy;
    for (INT i = 0; i < count; i++) {
        BOOL outside = FALSE;
        float t = sample_t(s, bx, by, &outside);
        if (outside) {
            out[i] = 0;
        } else {
            /* one entry per 1/n of the way, the pixel takes the nearest. */
            INT idx = (INT)(t * (float)s->table_n + 0.5f);
            if (idx < 0)
                idx = 0;
            if (idx > s->table_n)
                idx = s->table_n;
            out[i] = s->table[idx];
        }
        bx += s->inv.m11;
        by += s->inv.m12;
    }
}

static void linear_destroy(GSource *s)
{
    LinearSource *l = (LinearSource *)s;
    gp_free(l->table);
    gp_free(l->preset);
    gp_free(l->blend.positions);
    gp_free(l->blend.factors);
    gp_free(l->pre.positions);
    gp_free(l->pre.colors);
    gp_free(l);
}

static GpStatus linear_create(GGraphics *g, const GBrush *b, GSource **out)
{
    LinearSource *s = gp_alloc_zero(sizeof(*s));
    if (!s)
        return OutOfMemory;
    MimeVecMatrix m;
    mime_vec_matrix_multiply(&m, &b->xform, gp_graphics_w2d(g));
    if (!mime_vec_matrix_invert(&s->inv, &m)) {
        gp_free(s);
        return InvalidParameter;
    }
    s->b = b;
    s->mode = b->lg_mode;
    s->gamma = b->gamma;
    s->rx = b->rect.X;
    s->ry = b->rect.Y;
    s->rw = b->rect.Width;
    s->rh = b->rect.Height;
    s->cx = s->rx + s->rw * 0.5f;
    s->cy = s->ry + s->rh * 0.5f;
    s->clamp = b->wrap == WrapModeClamp;
    s->flip_x = b->wrap == WrapModeTileFlipX || b->wrap == WrapModeTileFlipXY;
    s->flip_y = b->wrap == WrapModeTileFlipY || b->wrap == WrapModeTileFlipXY;
    if (b->lg_mode == GP_LINE_POINTS) {
        float dx = b->p2.X - b->p1.X, dy = b->p2.Y - b->p1.Y;
        float l = mime_vec_sqrtf(dx * dx + dy * dy);
        s->dirx = dx / l;
        s->diry = dy / l;
        s->len = l;
        /* a two point gradient is centered between its points, not in the padded rectangle. */
        s->cx = (b->p1.X + b->p2.X) * 0.5f;
        s->cy = (b->p1.Y + b->p2.Y) * 0.5f;
    } else if (b->lg_mode == GP_LINE_ANGLE) {
        float sn, cs;
        mime_vec_sincosf(b->angle * 3.14159265358979f / 180.0f, &sn, &cs);
        s->dirx = cs;
        s->diry = sn;
        float ac = cs < 0 ? -cs : cs, as = sn < 0 ? -sn : sn;
        s->len = s->rw * ac + s->rh * as;
    }
    s->c1 = fcolor_of(b->color1, b->gamma);
    s->c2 = fcolor_of(b->color2, b->gamma);
    if (b->blend.count) {
        s->blend.count = b->blend.count;
        s->blend.positions = gp_dup(b->blend.positions, sizeof(float) * b->blend.count);
        s->blend.factors = gp_dup(b->blend.factors, sizeof(float) * b->blend.count);
    }
    if (b->preset.count) {
        s->pre.count = b->preset.count;
        s->pre.positions = gp_dup(b->preset.positions, sizeof(float) * b->preset.count);
        s->pre.colors = gp_dup(b->preset.colors, sizeof(ARGB) * b->preset.count);
        s->preset = gp_alloc(sizeof(FColor) * b->preset.count);
        if (s->preset && s->pre.colors) {
            for (INT i = 0; i < b->preset.count; i++)
                s->preset[i] = fcolor_of(b->preset.colors[i], b->gamma);
            s->has_preset = TRUE;
        }
    }
    /* the color table is sized by the device length of the brush rectangle's diagonals. */
    MimeVecPoint corners[4] = {{s->rx, s->ry}, {s->rx + s->rw, s->ry}, {s->rx + s->rw, s->ry + s->rh},
                               {s->rx, s->ry + s->rh}};
    for (INT i = 0; i < 4; i++)
        corners[i] = mime_vec_xform(&m, corners[i].x, corners[i].y);
    float d1x = corners[2].x - corners[0].x, d1y = corners[2].y - corners[0].y;
    float d2x = corners[3].x - corners[1].x, d2y = corners[3].y - corners[1].y;
    float diag = mime_vec_sqrtf(d1x * d1x + d1y * d1y) + mime_vec_sqrtf(d2x * d2x + d2y * d2y);
    INT n = (INT)mime_vec_ceilf(diag);
    if (n < 1)
        n = 1;
    if (n > 8192)
        n = 8192;
    s->table_n = n;
    s->table = gp_alloc(sizeof(UINT32) * (n + 2));
    if (!s->table || (b->blend.count && (!s->blend.positions || !s->blend.factors))) {
        linear_destroy(&s->base);
        return OutOfMemory;
    }
    for (INT i = 0; i < n + 2; i++)
        s->table[i] = color_at(s, (float)i / (float)n);
    s->base.fetch = linear_fetch;
    s->base.destroy = linear_destroy;
    *out = &s->base;
    return Ok;
}

/* ------------------------------------------------------------------ path gradients */

typedef struct Tri {
    float x0, y0;
    float x1, y1;     /* the next vertex. */
    FColor s0, s1;
    float minx, maxx, miny, maxy;
    float denom;      /* for the barycentric coordinates. */
} Tri;

typedef struct PathSource {
    GSource base;
    MimeVecMatrix inv;
    Tri *tris;
    INT ntris;
    float cx, cy;
    FColor cc;
    GBlend blend;
    GPreset pre;
    FColor *preset;
    BOOL has_preset;
    BOOL gamma;
    float fx, fy;
    BOOL tile, flip_x, flip_y;
    float bx, by, bw, bh;
} PathSource;

static void path_destroy(GSource *src)
{
    PathSource *s = (PathSource *)src;
    gp_free(s->tris);
    gp_free(s->preset);
    gp_free(s->blend.positions);
    gp_free(s->blend.factors);
    gp_free(s->pre.positions);
    gp_free(s->pre.colors);
    gp_free(s);
}

static void path_fetch(GSource *src, INT x, INT y, INT count, UINT32 *out)
{
    PathSource *s = (PathSource *)src;
    float px = (float)x * s->inv.m11 + (float)y * s->inv.m21 + s->inv.dx;
    float py = (float)x * s->inv.m12 + (float)y * s->inv.m22 + s->inv.dy;
    for (INT i = 0; i < count; i++, px += s->inv.m11, py += s->inv.m12) {
        float qx = px, qy = py;
        if (s->tile) {
            float u = (qx - s->bx) / s->bw, v = (qy - s->by) / s->bh;
            BOOL outside = FALSE;
            u = wrap_axis(u, s->flip_x, FALSE, &outside);
            v = wrap_axis(v, s->flip_y, FALSE, &outside);
            qx = s->bx + u * s->bw;
            qy = s->by + v * s->bh;
        }
        out[i] = 0;
        for (INT k = 0; k < s->ntris; k++) {
            const Tri *t = &s->tris[k];
            if (qx < t->minx || qx > t->maxx || qy < t->miny || qy > t->maxy)
                continue;
            /* barycentric coordinates in the triangle of the center and the two vertices. */
            float ax = t->x0 - s->cx, ay = t->y0 - s->cy, bx = t->x1 - s->cx, by = t->y1 - s->cy;
            float rx = qx - s->cx, ry = qy - s->cy;
            float w1 = (rx * by - ry * bx) / t->denom;
            float w2 = (ax * ry - ay * rx) / t->denom;
            if (w1 < -1e-6f || w2 < -1e-6f || w1 + w2 > 1.000001f)
                continue;
            if (w1 < 0.0f)
                w1 = 0.0f;
            if (w2 < 0.0f)
                w2 = 0.0f;
            float tt = w1 + w2;
            if (tt > 1.0f)
                tt = 1.0f;
            FColor sb = tt > 0.0f ? fcolor_lerp(&t->s0, &t->s1, w2 / tt) : t->s0;
            /* the focus scales keep the center color over the first part of the way. */
            if (s->fx > 0.0f || s->fy > 0.0f) {
                float len = mime_vec_sqrtf(rx * rx + ry * ry);
                if (len > 0.0f) {
                    float ux = rx / len * s->fx, uy = ry / len * s->fy;
                    float f = mime_vec_sqrtf(ux * ux + uy * uy);
                    if (f > 0.0f && f < 1.0f)
                        tt = (tt - f) / (1.0f - f);
                    if (tt < 0.0f)
                        tt = 0.0f;
                }
            }
            if (s->has_preset) {
                const GPreset *p = &s->pre;
                FColor f = s->preset[p->count - 1];
                if (tt <= p->positions[0]) {
                    f = s->preset[0];
                } else {
                    for (INT j = 1; j < p->count; j++) {
                        if (tt <= p->positions[j]) {
                            float span = p->positions[j] - p->positions[j - 1];
                            float kk = span > 0.0f ? (tt - p->positions[j - 1]) / span : 1.0f;
                            f = fcolor_lerp(&s->preset[j - 1], &s->preset[j], kk);
                            break;
                        }
                    }
                }
                out[i] = pack_fcolor(&f, s->gamma);
            } else {
                /* position 0 is the center color, 1 the surround. */
                float kk = blend_factor(&s->blend, tt);
                if (kk < 0.0f)
                    kk = 0.0f;
                if (kk > 1.0f)
                    kk = 1.0f;
                FColor f = fcolor_lerp(&s->cc, &sb, kk);
                out[i] = pack_fcolor(&f, s->gamma);
            }
            break;
        }
    }
}

static GpStatus path_create(GGraphics *g, const GBrush *b, GSource **out)
{
    PathSource *s = gp_alloc_zero(sizeof(*s));
    if (!s)
        return OutOfMemory;
    MimeVecMatrix m;
    mime_vec_matrix_multiply(&m, &b->xform, gp_graphics_w2d(g));
    if (!mime_vec_matrix_invert(&s->inv, &m)) {
        gp_free(s);
        return InvalidParameter;
    }
    s->cx = b->center.X;
    s->cy = b->center.Y;
    s->gamma = b->gamma;
    s->cc = fcolor_of(b->center_color, b->gamma);
    s->fx = b->focus_x;
    s->fy = b->focus_y;
    s->tile = b->wrap != WrapModeClamp;
    s->flip_x = b->wrap == WrapModeTileFlipX || b->wrap == WrapModeTileFlipXY;
    s->flip_y = b->wrap == WrapModeTileFlipY || b->wrap == WrapModeTileFlipXY;
    s->bx = b->rect.X;
    s->by = b->rect.Y;
    s->bw = b->rect.Width;
    s->bh = b->rect.Height;
    if (b->blend.count) {
        s->blend.count = b->blend.count;
        s->blend.positions = gp_dup(b->blend.positions, sizeof(float) * b->blend.count);
        s->blend.factors = gp_dup(b->blend.factors, sizeof(float) * b->blend.count);
    }
    if (b->preset.count) {
        s->pre.count = b->preset.count;
        s->pre.positions = gp_dup(b->preset.positions, sizeof(float) * b->preset.count);
        s->pre.colors = gp_dup(b->preset.colors, sizeof(ARGB) * b->preset.count);
        s->preset = gp_alloc(sizeof(FColor) * b->preset.count);
        if (s->preset) {
            for (INT i = 0; i < b->preset.count; i++)
                s->preset[i] = fcolor_of(b->preset.colors[i], b->gamma);
            s->has_preset = TRUE;
        }
    }
    /* the boundary is the flattened path, each point colored by the vertex it comes from. */
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    const GPath *path = b->pg_path;
    if (mime_vec_path_flatten(&path->path, nullptr, 0.25f, &polys) != MIME_VEC_OK || !polys.npts) {
        mime_vec_polys_free(&polys);
        path_destroy(&s->base);
        return OutOfMemory;
    }
    s->tris = gp_alloc(sizeof(Tri) * polys.npts);
    if (!s->tris) {
        mime_vec_polys_free(&polys);
        path_destroy(&s->base);
        return OutOfMemory;
    }
    /* a flattened point takes the color of the nearest path point at or before it. */
    UINT raw = path->path.count;
    for (UINT sub = 0; sub < polys.nsubs; sub++) {
        const MimeVecSub *sb = &polys.subs[sub];
        for (UINT j = 0; j < sb->count; j++) {
            const MimeVecPoint *p0 = &polys.pts[sb->first + j], *p1 = &polys.pts[sb->first + (j + 1) % sb->count];
            Tri *t = &s->tris[s->ntris];
            t->x0 = p0->x;
            t->y0 = p0->y;
            t->x1 = p1->x;
            t->y1 = p1->y;
            INT i0 = 0, i1 = 0;
            float d0 = 3.4e38f, d1 = 3.4e38f;
            for (UINT r = 0; r < raw; r++) {
                float dx = path->path.pts[r].x - p0->x, dy = path->path.pts[r].y - p0->y;
                float d = dx * dx + dy * dy;
                if (d < d0) {
                    d0 = d;
                    i0 = (INT)r;
                }
                dx = path->path.pts[r].x - p1->x;
                dy = path->path.pts[r].y - p1->y;
                d = dx * dx + dy * dy;
                if (d < d1) {
                    d1 = d;
                    i1 = (INT)r;
                }
            }
            t->s0 = fcolor_of(b->surround[i0], b->gamma);
            t->s1 = fcolor_of(b->surround[i1], b->gamma);
            float ax = t->x0 - s->cx, ay = t->y0 - s->cy, bx = t->x1 - s->cx, by = t->y1 - s->cy;
            t->denom = ax * by - ay * bx;
            if (t->denom == 0.0f)
                continue;
            t->minx = t->x0 < t->x1 ? t->x0 : t->x1;
            t->maxx = t->x0 > t->x1 ? t->x0 : t->x1;
            t->miny = t->y0 < t->y1 ? t->y0 : t->y1;
            t->maxy = t->y0 > t->y1 ? t->y0 : t->y1;
            if (s->cx < t->minx)
                t->minx = s->cx;
            if (s->cx > t->maxx)
                t->maxx = s->cx;
            if (s->cy < t->miny)
                t->miny = s->cy;
            if (s->cy > t->maxy)
                t->maxy = s->cy;
            s->ntris++;
        }
    }
    mime_vec_polys_free(&polys);
    s->base.fetch = path_fetch;
    s->base.destroy = path_destroy;
    *out = &s->base;
    return Ok;
}

/* ------------------------------------------------------------------ textures */

typedef struct TextureSource {
    GSource base;
    MimeVecMatrix inv;
    UINT32 *pixels; /* premultiplied, w * h. */
    INT w, h;
    BOOL clamp, flip_x, flip_y;
    BOOL whole;     /* the brush moves pixels by whole pixels only. */
    INT shift_x, shift_y;
    ARGB clamp_color;
} TextureSource;

static void texture_destroy(GSource *s)
{
    TextureSource *t = (TextureSource *)s;
    gp_free(t->pixels);
    gp_free(t);
}

static INT wrap_index(INT i, INT n, BOOL flip)
{
    INT q = i / n, r = i % n;
    if (r < 0) {
        r += n;
        q--;
    }
    if (flip && (q & 1))
        r = n - 1 - r;
    return r;
}

static UINT32 texel(const TextureSource *t, INT x, INT y)
{
    if (t->clamp && (x < 0 || y < 0 || x >= t->w || y >= t->h))
        return 0;
    return t->pixels[wrap_index(y, t->h, t->flip_y) * t->w + wrap_index(x, t->w, t->flip_x)];
}

static UINT32 bilerp(UINT32 a, UINT32 b, UINT32 c, UINT32 d, UINT fx, UINT fy)
{
    UINT32 out = 0;
    for (INT sh = 0; sh < 32; sh += 8) {
        UINT va = (a >> sh) & 255, vb = (b >> sh) & 255, vc = (c >> sh) & 255, vd = (d >> sh) & 255;
        UINT top = va * (256 - fx) + vb * fx, bot = vc * (256 - fx) + vd * fx;
        UINT v = (top * (256 - fy) + bot * fy + 32768) >> 16;
        out |= v << sh;
    }
    return out;
}

static void texture_fetch(GSource *src, INT x, INT y, INT count, UINT32 *out)
{
    TextureSource *t = (TextureSource *)src;
    if (t->whole) {
        for (INT i = 0; i < count; i++)
            out[i] = texel(t, x + i + t->shift_x, y + t->shift_y);
        return;
    }
    /* texel centers are at half integers, a pixel samples at its integer coordinate. */
    float u = (float)x * t->inv.m11 + (float)y * t->inv.m21 + t->inv.dx - 0.5f;
    float v = (float)x * t->inv.m12 + (float)y * t->inv.m22 + t->inv.dy - 0.5f;
    for (INT i = 0; i < count; i++, u += t->inv.m11, v += t->inv.m12) {
        float fu = mime_vec_floorf(u), fv = mime_vec_floorf(v);
        INT iu = (INT)fu, iv = (INT)fv;
        UINT fx = (UINT)((u - fu) * 256.0f), fy = (UINT)((v - fv) * 256.0f);
        out[i] = bilerp(texel(t, iu, iv), texel(t, iu + 1, iv), texel(t, iu, iv + 1), texel(t, iu + 1, iv + 1), fx, fy);
    }
}

static GpStatus texture_create(GGraphics *g, const GBrush *b, GSource **out)
{
    TextureSource *t = gp_alloc_zero(sizeof(*t));
    if (!t)
        return OutOfMemory;
    MimeVecMatrix m;
    mime_vec_matrix_multiply(&m, &b->xform, gp_graphics_w2d(g));
    if (!mime_vec_matrix_invert(&t->inv, &m)) {
        gp_free(t);
        return InvalidParameter;
    }
    t->w = b->image->width;
    t->h = b->image->height;
    t->pixels = gp_alloc(sizeof(UINT32) * t->w * t->h);
    if (!t->pixels) {
        gp_free(t);
        return OutOfMemory;
    }
    for (INT row = 0; row < t->h; row++) {
        UINT32 *dst = t->pixels + (SSIZE_T)row * t->w;
        gp_bitmap_read_argb(b->image, 0, row, t->w, dst);
        if (b->attrs && gp_attrs_active(b->attrs, ColorAdjustTypeBrush))
            gp_attrs_apply(b->attrs, ColorAdjustTypeBrush, dst, t->w);
        for (INT i = 0; i < t->w; i++)
            dst[i] = gp_premultiply(dst[i]);
    }
    t->clamp = b->wrap == WrapModeClamp;
    t->flip_x = b->wrap == WrapModeTileFlipX || b->wrap == WrapModeTileFlipXY;
    t->flip_y = b->wrap == WrapModeTileFlipY || b->wrap == WrapModeTileFlipXY;
    /* a brush that moves the picture by whole pixels needs no filtering. */
    float dx = t->inv.dx, dy = t->inv.dy;
    BOOL scale_free = close_enough(t->inv.m11, 1.0f) && close_enough(t->inv.m22, 1.0f) &&
                      close_enough(t->inv.m12, 0.0f) && close_enough(t->inv.m21, 0.0f);
    float rx = mime_vec_floorf(dx + 0.5f), ry = mime_vec_floorf(dy + 0.5f);
    if (scale_free && close_enough(dx, rx) && close_enough(dy, ry)) {
        t->whole = TRUE;
        t->shift_x = (INT)rx;
        t->shift_y = (INT)ry;
    }
    t->base.fetch = texture_fetch;
    t->base.destroy = texture_destroy;
    *out = &t->base;
    return Ok;
}

/* ------------------------------------------------------------------ entry points */

GpStatus gp_source_create(GGraphics *g, const GBrush *brush, const RECT *bounds, GSource **out)
{
    *out = nullptr;
    switch (brush->type) {
    case BrushTypeSolidColor: {
        SolidSource *s = gp_alloc_zero(sizeof(*s));
        if (!s)
            return OutOfMemory;
        s->base.fetch = solid_fetch;
        s->base.destroy = simple_destroy;
        s->base.constant = TRUE;
        s->base.color = gp_premultiply(brush->color);
        s->base.opaque = (brush->color >> 24) == 255;
        *out = &s->base;
        return Ok;
    }
    case BrushTypeHatchFill:
        return hatch_create(g, brush, out);
    case BrushTypeLinearGradient:
        return linear_create(g, brush, out);
    case BrushTypePathGradient:
        return path_create(g, brush, out);
    case BrushTypeTextureFill:
        return texture_create(g, brush, out);
    default:
        return InvalidParameter;
    }
}

void gp_source_free(GSource *s)
{
    if (s)
        s->destroy(s);
}
