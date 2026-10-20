/*
 * filling: the path of a shape becomes polygons in device space, the scan converter turns them into coverage,
 * and the rows of the brush's pixels, scaled by the coverage and the clip, are composed onto the surface.
 */
#include "gp_private.h"

INT gp_smoothing_samples(SmoothingMode mode)
{
    switch (mode) {
    case SmoothingModeHighQuality:
    case SmoothingModeAntiAlias:
        return 1;
    case SmoothingModeAntiAlias8x8:
        return 2;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ the clip */

/*
 * narrows the area to what the clip lets through. a clip that is a rectangle only narrows it, any other shape
 * comes back as the mask to apply to the pixels. FALSE in the result: nothing is visible.
 */
BOOL gp_clip_narrow(GGraphics *g, RECT *area, const MimeVecMask **mask)
{
    *mask = nullptr;
    RECT bounds;
    gp_graphics_bounds(g, &bounds);
    if (area->left < bounds.left)
        area->left = bounds.left;
    if (area->top < bounds.top)
        area->top = bounds.top;
    if (area->right > bounds.right)
        area->right = bounds.right;
    if (area->bottom > bounds.bottom)
        area->bottom = bounds.bottom;
    if (g->hdc && !g->target) {
        RECT box;
        if (GetClipBox(g->hdc, &box) == NULLREGION)
            return FALSE;
        if (area->left < box.left)
            area->left = box.left;
        if (area->top < box.top)
            area->top = box.top;
        if (area->right > box.right)
            area->right = box.right;
        if (area->bottom > box.bottom)
            area->bottom = box.bottom;
    }
    if (g->clip_set) {
        MimeVecRect r;
        if (mime_vec_mask_is_rect(&g->clip, &r)) {
            if (area->left < r.left)
                area->left = r.left;
            if (area->top < r.top)
                area->top = r.top;
            if (area->right > r.right)
                area->right = r.right;
            if (area->bottom > r.bottom)
                area->bottom = r.bottom;
        } else {
            MimeVecRect mb = mime_vec_mask_bounds(&g->clip);
            if (area->left < mb.left)
                area->left = mb.left;
            if (area->top < mb.top)
                area->top = mb.top;
            if (area->right > mb.right)
                area->right = mb.right;
            if (area->bottom > mb.bottom)
                area->bottom = mb.bottom;
            *mask = &g->clip;
        }
    }
    return area->right > area->left && area->bottom > area->top;
}

/* ------------------------------------------------------------------ the fill loop */

typedef struct FillCtx {
    GGraphics *g;
    GSource *src;
    GSurface surf;
    const MimeVecMask *mask;
    UINT32 *buf;
    UINT8 *mrow;
    INT cap;
    BOOL modified;
    CompositingMode mode;
    const UINT8 *map;           /* coverage 0 to 255 to alpha, for text */
    const MimeVecMask *extra;   /* a second clip, for the rectangle text is cut to */
} FillCtx;

/* every byte of the pixels scaled by cov / cmax, rounded: the way the scan converter's output meets the brush. */
static void scale_by_coverage(UINT32 *px, const UINT8 *cov, INT count, UINT cmax)
{
    if (cmax == 255) {
        for (INT i = 0; i < count; i++) {
            if (cov[i] != 255)
                px[i] = gp_scale_pargb(px[i], cov[i]);
        }
        return;
    }
    UINT half = cmax / 2;
    UINT shift = cmax == 32 ? 5 : (cmax == 64 ? 6 : 0);
    UINT32 add = (half << 16) | half;
    for (INT i = 0; i < count; i++) {
        UINT c = cov[i];
        if (c >= cmax)
            continue;
        if (!c) {
            px[i] = 0;
            continue;
        }
        UINT32 p = px[i];
        UINT32 lo = (c * (p & 0x00ff00ffu) + add) >> shift;
        UINT32 hi = (c * ((p >> 8) & 0x00ff00ffu) + add) >> shift;
        px[i] = (lo & 0x00ff00ffu) | ((hi & 0x00ff00ffu) << 8);
    }
}

static void fill_span(void *user, INT y, INT x0, INT count, const UINT8 *cov, UINT cmax)
{
    FillCtx *c = user;
    INT x = x0;
    INT left = count;
    while (left > 0) {
        INT n = left > c->cap ? c->cap : left;
        const UINT8 *cv = cov + (x - x0);
        UINT8 mapped[1024];
        UINT cm = cmax;
        if (c->map) {
            for (INT i = 0; i < n; i++) {
                UINT v = cmax > 1 ? (cv[i] * 255u + cmax / 2) / cmax : (cv[i] ? 255u : 0u);
                mapped[i] = c->map[v];
            }
            cv = mapped;
            cm = 255;
        }
        if (c->src->constant) {
            for (INT i = 0; i < n; i++)
                c->buf[i] = c->src->color;
        } else {
            c->src->fetch(c->src, x, y, n, c->buf);
        }
        if (c->mode == CompositingModeSourceOver) {
            if (cm > 1)
                scale_by_coverage(c->buf, cv, n, cm);
            if (c->mask || c->extra) {
                memset(c->mrow, 255, n);
                if (c->mask)
                    mime_vec_mask_apply_row(c->mask, y, x, n, c->mrow);
                if (c->extra)
                    mime_vec_mask_apply_row(c->extra, y, x, n, c->mrow);
                for (INT i = 0; i < n; i++) {
                    if (c->mrow[i] != 255)
                        c->buf[i] = gp_scale_pargb(c->buf[i], c->mrow[i]);
                }
            }
            gp_compose_over(&c->surf, x, y, n, c->buf);
        } else {
            /* source copy: pixels the clip rejects keep the destination, partly covered ones blend */
            UINT8 cc[1024];
            INT m = n > 1024 ? 1024 : n;
            for (INT i = 0; i < m; i++)
                cc[i] = cv[i];
            if (c->mask || c->extra) {
                memset(c->mrow, 255, m);
                if (c->mask)
                    mime_vec_mask_apply_row(c->mask, y, x, m, c->mrow);
                if (c->extra)
                    mime_vec_mask_apply_row(c->extra, y, x, m, c->mrow);
                for (INT i = 0; i < m; i++)
                    cc[i] = (UINT8)((cc[i] * (UINT)c->mrow[i] + 127) / 255);
            }
            gp_compose_copy(&c->surf, x, y, m, c->buf, cc, cm);
            n = m;
        }
        c->modified = TRUE;
        x += n;
        left -= n;
    }
}

static GpStatus fill_polys_src(GGraphics *g, const MimeVecPolys *polys, INT fill_rule, INT aa, GSource *src,
                               const RECT *area_in, const UINT8 *map, const MimeVecMask *extra)
{
    RECT area = *area_in;
    const MimeVecMask *mask;
    if (!gp_clip_narrow(g, &area, &mask))
        return Ok;
    FillCtx c;
    memset(&c, 0, sizeof(c));
    c.g = g;
    c.src = src;
    c.mask = mask;
    c.map = map;
    c.extra = extra;
    c.mode = g->compositing_mode;
    c.cap = 1024;
    c.buf = gp_alloc(sizeof(UINT32) * c.cap);
    c.mrow = gp_alloc(c.cap);
    if (!c.buf || !c.mrow) {
        gp_free(c.buf);
        gp_free(c.mrow);
        return OutOfMemory;
    }
    GpStatus status = gp_surface_acquire(g, &area, &c.surf);
    if (status == Ok) {
        status = gp_raster(polys, fill_rule, aa, &area, fill_span, &c);
        gp_surface_release(g, &c.surf, c.modified);
    }
    gp_free(c.buf);
    gp_free(c.mrow);
    return status;
}

/* the device rectangle a shape touches, with the margin the antialiasing may spill into. */
static BOOL polys_area(const MimeVecPolys *polys, INT aa, RECT *area)
{
    MimeVecBounds b;
    if (!mime_vec_polys_bounds(polys, &b))
        return FALSE;
    INT margin = aa ? 1 : 0;
    if (b.left < -4.0e6f || b.top < -4.0e6f || b.right > 4.0e6f || b.bottom > 4.0e6f) {
        if (b.left < -4.0e6f)
            b.left = -4.0e6f;
        if (b.top < -4.0e6f)
            b.top = -4.0e6f;
        if (b.right > 4.0e6f)
            b.right = 4.0e6f;
        if (b.bottom > 4.0e6f)
            b.bottom = 4.0e6f;
    }
    area->left = (INT)mime_vec_floorf(b.left) - 1 - margin;
    area->top = (INT)mime_vec_floorf(b.top) - 1 - margin;
    area->right = (INT)mime_vec_ceilf(b.right) + 2 + margin;
    area->bottom = (INT)mime_vec_ceilf(b.bottom) + 2 + margin;
    return TRUE;
}

GpStatus gp_fill_polys_src(GGraphics *g, const MimeVecPolys *polys, INT fill_rule, INT aa, GSource *src,
                           const RECT *area)
{
    return fill_polys_src(g, polys, fill_rule, aa, src, area, nullptr, nullptr);
}

static GpStatus fill_polys(GGraphics *g, const MimeVecPolys *polys, INT fill_rule, INT aa, const GBrush *brush,
                           const UINT8 *map, const MimeVecMask *extra)
{
    RECT area;
    if (!polys_area(polys, aa, &area))
        return Ok;
    const MimeVecMask *mask;
    RECT narrowed = area;
    if (!gp_clip_narrow(g, &narrowed, &mask))
        return Ok;
    GSource *src;
    GpStatus status = gp_source_create(g, brush, &narrowed, &src);
    if (status != Ok)
        return status;
    status = fill_polys_src(g, polys, fill_rule, aa, src, &narrowed, map, extra);
    gp_source_free(src);
    return status;
}

GpStatus gp_fill_polys(GGraphics *g, const MimeVecPolys *polys, INT fill_rule, INT aa, const GBrush *brush)
{
    return fill_polys(g, polys, fill_rule, aa, brush, nullptr, nullptr);
}

GpStatus gp_fill_polys_text(GGraphics *g, const MimeVecPolys *polys, INT aa, const GBrush *brush, const UINT8 *map,
                            const MimeVecMask *clip)
{
    return fill_polys(g, polys, MIME_VEC_FILL_WINDING, aa, brush, map, clip);
}

GpStatus gp_fill_path_xform(GGraphics *g, const MimeVecPath *path, const MimeVecMatrix *extra, FillMode mode,
                            const GBrush *brush)
{
    if (!path->count)
        return Ok;
    if (g->meta) {
        /* a recording graphics keeps the shape, the extra matrix is part of it */
        if (!extra)
            return gp_rec_fill_path(g, path, mode, brush);
        MimeVecPath moved;
        mime_vec_path_init(&moved, &gp_allocator);
        GpStatus rs = mime_vec_path_copy(&moved, path) == MIME_VEC_OK ? Ok : OutOfMemory;
        if (rs == Ok) {
            mime_vec_path_transform(&moved, extra);
            rs = gp_rec_fill_path(g, &moved, mode, brush);
        }
        mime_vec_path_free(&moved);
        return rs;
    }
    MimeVecMatrix m = *gp_graphics_w2d(g);
    if (extra)
        mime_vec_matrix_multiply(&m, extra, &m);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    int e = mime_vec_path_flatten(path, &m, MIME_VEC_DEFAULT_FLATNESS, &polys);
    GpStatus status = Ok;
    if (e == MIME_VEC_OK) {
        INT aa = gp_smoothing_samples(g->smoothing);
        status = gp_fill_polys(g, &polys, mode == FillModeWinding ? MIME_VEC_FILL_WINDING : MIME_VEC_FILL_ALTERNATE, aa,
                               brush);
    } else {
        status = e == MIME_VEC_NOMEM ? OutOfMemory : InvalidParameter;
    }
    mime_vec_polys_free(&polys);
    return status;
}

GpStatus gp_fill_path(GGraphics *g, const MimeVecPath *path, FillMode mode, const GBrush *brush)
{
    return gp_fill_path_xform(g, path, nullptr, mode, brush);
}

/* fills the pixels of a mask, with no antialiasing: the rows of the runs get the brush. */
GpStatus gp_fill_mask(GGraphics *g, const MimeVecMask *mask, const GBrush *brush)
{
    if (mime_vec_mask_is_empty(mask))
        return Ok;
    MimeVecRect mb = mime_vec_mask_bounds(mask);
    RECT area = {mb.left, mb.top, mb.right, mb.bottom};
    const MimeVecMask *clip;
    if (!gp_clip_narrow(g, &area, &clip))
        return Ok;
    GSource *src;
    GpStatus status = gp_source_create(g, brush, &area, &src);
    if (status != Ok)
        return status;
    FillCtx c;
    memset(&c, 0, sizeof(c));
    c.g = g;
    c.src = src;
    c.mask = clip;
    c.mode = g->compositing_mode;
    c.cap = 1024;
    c.buf = gp_alloc(sizeof(UINT32) * c.cap);
    c.mrow = gp_alloc(c.cap);
    UINT8 *ones = gp_alloc(c.cap);
    if (!c.buf || !c.mrow || !ones) {
        gp_free(c.buf);
        gp_free(c.mrow);
        gp_free(ones);
        gp_source_free(src);
        return OutOfMemory;
    }
    memset(ones, 1, c.cap);
    status = gp_surface_acquire(g, &area, &c.surf);
    if (status == Ok) {
        for (UINT b = 0; b < mask->nbands; b++) {
            const MimeVecBand *band = &mask->bands[b];
            for (INT y = band->y0; y < band->y1; y++) {
                if (y < area.top || y >= area.bottom)
                    continue;
                for (UINT i = 0; i < band->count; i++) {
                    const MimeVecRun *run = &mask->runs[band->first + i];
                    INT x0 = run->x0 < area.left ? area.left : run->x0, x1 = run->x1 > area.right ? area.right : run->x1;
                    for (INT x = x0; x < x1; x += c.cap) {
                        INT n = x1 - x > c.cap ? c.cap : x1 - x;
                        fill_span(&c, y, x, n, ones, 1);
                    }
                }
            }
        }
        gp_surface_release(g, &c.surf, c.modified);
    }
    gp_free(c.buf);
    gp_free(c.mrow);
    gp_free(ones);
    gp_source_free(src);
    return status;
}
