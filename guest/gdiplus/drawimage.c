/*
 * drawing images: a rectangle of a picture is mapped onto a parallelogram of the page, and the pixels of the
 * parallelogram take their color from the picture through the interpolation mode of the graphics and the color
 * adjustments and wrap mode of the image attributes. The steps are those of gdiplus.dll, see docs/gdiplus.md.
 */
#include "image_private.h"

/* ------------------------------------------------------------------ the rows of the picture */

typedef struct Rows {
    const UINT32 *px;   /* premultiplied */
    INT w, h;
    SSIZE_T stride;     /* pixels */
    UINT32 *owned;
} Rows;

static void rows_free(Rows *r)
{
    gp_free(r->owned);
    r->owned = nullptr;
}

/* the rows top to bottom of the picture, whole rows, premultiplied and with the adjustments applied. */
static GpStatus rows_build(GBitmap *b, INT top, INT bottom, struct GAttrs *attrs, Rows *r)
{
    memset(r, 0, sizeof(*r));
    r->w = b->width;
    r->h = bottom - top;
    if (r->h <= 0)
        return Ok;
    BOOL adjust = attrs && gp_attrs_active(attrs, ColorAdjustTypeBitmap);
    if (b->format == PixelFormat32bppPARGB && !adjust) {
        r->px = (const UINT32 *)(b->bits + (SSIZE_T)top * b->stride);
        r->stride = b->stride / 4;
        return Ok;
    }
    r->owned = gp_alloc((SIZE_T)r->w * (SIZE_T)r->h * 4);
    if (!r->owned)
        return OutOfMemory;
    for (INT y = 0; y < r->h; y++) {
        UINT32 *row = r->owned + (SSIZE_T)y * r->w;
        gp_bitmap_read_argb(b, 0, top + y, r->w, row);
        if (adjust)
            gp_attrs_apply(attrs, ColorAdjustTypeBitmap, row, r->w);
        for (INT x = 0; x < r->w; x++)
            row[x] = gp_premultiply(row[x]);
    }
    r->px = r->owned;
    r->stride = r->w;
    return Ok;
}

/* the rows top to bottom of a whole picture held in memory, premultiplied. */
static void rows_of_buffer(UINT32 *buf, INT w, INT h, INT top, INT bottom, Rows *r)
{
    memset(r, 0, sizeof(*r));
    r->w = w;
    r->h = bottom - top;
    (void)h;
    if (r->h > 0) {
        r->px = buf + (SSIZE_T)top * w;
        r->stride = w;
    }
}

/*
 * the picture turned or flipped as one of the RotateFlipType values says, premultiplied and with the adjustments
 * applied, in a new buffer of the size the turned picture has.
 */
static GpStatus rotated_buffer(GBitmap *b, INT type, struct GAttrs *attrs, UINT32 **out, INT *rw, INT *rh)
{
    INT w = b->width, h = b->height;
    BOOL turn = type == 1 || type == 3 || type == 5 || type == 7;
    INT nw = turn ? h : w, nh = turn ? w : h;
    UINT32 *buf = gp_alloc((SIZE_T)nw * (SIZE_T)nh * 4);
    UINT32 *row = gp_alloc((SIZE_T)w * 4);
    if (!buf || !row) {
        gp_free(buf);
        gp_free(row);
        return OutOfMemory;
    }
    BOOL adjust = attrs && gp_attrs_active(attrs, ColorAdjustTypeBitmap);
    for (INT y = 0; y < h; y++) {
        gp_bitmap_read_argb(b, 0, y, w, row);
        if (adjust)
            gp_attrs_apply(attrs, ColorAdjustTypeBitmap, row, w);
        for (INT x = 0; x < w; x++) {
            INT nx, ny;
            switch (type) {
            case 1:
                nx = h - 1 - y;
                ny = x;
                break;
            case 2:
                nx = w - 1 - x;
                ny = h - 1 - y;
                break;
            case 3:
                nx = y;
                ny = w - 1 - x;
                break;
            case 4:
                nx = w - 1 - x;
                ny = y;
                break;
            case 5:
                nx = y;
                ny = x;
                break;
            case 6:
                nx = x;
                ny = h - 1 - y;
                break;
            default:
                nx = h - 1 - y;
                ny = w - 1 - x;
                break;
            }
            buf[(SSIZE_T)ny * nw + nx] = gp_premultiply(row[x]);
        }
    }
    gp_free(row);
    *out = buf;
    *rw = nw;
    *rh = nh;
    return Ok;
}

/* ------------------------------------------------------------------ putting pixels on the target */

typedef struct Paint {
    GGraphics *g;
    ImgSpan span;
    BOOL empty;
    UINT32 clamp;
    GSurface surf;
    const MimeVecMask *mask;
    UINT32 *buf;
    SIZE_T cap;
    UINT8 *mrow;
    BOOL modified;
    CompositingMode mode;
} Paint;

/* every byte of the pixels scaled by cov / cmax, rounded: the way the scan converter's output meets a source. */
static void scale_by_coverage(UINT32 *px, const UINT8 *cov, INT count, UINT cmax)
{
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

#define PAINT_CHUNK 1024

static void paint_run(void *user, INT y, INT x0, INT count, const UINT8 *cov, UINT cmax)
{
    Paint *p = user;
    if ((SIZE_T)count > p->cap) {
        UINT32 *grown = gp_realloc(p->buf, (SIZE_T)count * 4);
        if (!grown)
            return;
        p->buf = grown;
        p->cap = (SIZE_T)count;
    }
    /* the whole run is asked of the span at once, the positions of its pixels follow from where it ends */
    if (p->empty) {
        for (INT i = 0; i < count; i++)
            p->buf[i] = p->clamp;
    } else {
        span_run(&p->span, y, x0, x0 + count, p->buf);
    }
    for (INT done = 0; done < count; done += PAINT_CHUNK) {
        INT n = count - done > PAINT_CHUNK ? PAINT_CHUNK : count - done;
        UINT32 *px = p->buf + done;
        const UINT8 *cv = cov + done;
        if (p->mode == CompositingModeSourceOver) {
            if (cmax > 1)
                scale_by_coverage(px, cv, n, cmax);
            if (p->mask) {
                memset(p->mrow, 255, n);
                mime_vec_mask_apply_row(p->mask, y, x0 + done, n, p->mrow);
                for (INT i = 0; i < n; i++) {
                    if (p->mrow[i] != 255)
                        px[i] = gp_scale_pargb(px[i], p->mrow[i]);
                }
            }
            gp_compose_over(&p->surf, x0 + done, y, n, px);
        } else {
            UINT8 cc[PAINT_CHUNK];
            for (INT i = 0; i < n; i++)
                cc[i] = cv[i];
            if (p->mask) {
                memset(p->mrow, 255, n);
                mime_vec_mask_apply_row(p->mask, y, x0 + done, n, p->mrow);
                for (INT i = 0; i < n; i++)
                    cc[i] = (UINT8)((cc[i] * (UINT)p->mrow[i] + 127) / 255);
            }
            gp_compose_copy(&p->surf, x0 + done, y, n, px, cc, cmax);
        }
        p->modified = TRUE;
    }
}

/* fills the polygon of four device points with the span, on the target of the graphics. */
static GpStatus paint_polygon(Paint *p, const float *pts)
{
    GGraphics *g = p->g;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    mime_vec_polys_begin(&polys);
    GpStatus st = Ok;
    for (INT i = 0; i < 4; i++) {
        if (mime_vec_polys_add(&polys, pts[2 * i], pts[2 * i + 1]) != MIME_VEC_OK)
            st = OutOfMemory;
    }
    mime_vec_polys_end(&polys, true);
    MimeVecBounds bb;
    if (st == Ok && mime_vec_polys_bounds(&polys, &bb)) {
        INT aa = gp_smoothing_samples(g->smoothing);
        INT margin = aa ? 1 : 0;
        float lim = 4.0e6f;
        if (bb.left < -lim)
            bb.left = -lim;
        if (bb.top < -lim)
            bb.top = -lim;
        if (bb.right > lim)
            bb.right = lim;
        if (bb.bottom > lim)
            bb.bottom = lim;
        RECT area = {(INT)mime_vec_floorf(bb.left) - 1 - margin, (INT)mime_vec_floorf(bb.top) - 1 - margin,
                     (INT)mime_vec_ceilf(bb.right) + 2 + margin, (INT)mime_vec_ceilf(bb.bottom) + 2 + margin};
        if (gp_clip_narrow(g, &area, &p->mask)) {
            p->mrow = gp_alloc(PAINT_CHUNK);
            if (!p->mrow) {
                st = OutOfMemory;
            } else {
                st = gp_surface_acquire(g, &area, &p->surf);
                if (st == Ok) {
                    st = gp_raster(&polys, MIME_VEC_FILL_ALTERNATE, aa, &area, paint_run, p);
                    gp_surface_release(g, &p->surf, p->modified);
                }
                gp_free(p->mrow);
            }
        }
    }
    mime_vec_polys_free(&polys);
    return st;
}

/* ------------------------------------------------------------------ the draw of a bitmap */

/* the colour of the interpolation mode a graphics keeps: Default and Low are bilinear, High is bicubic. */
static InterpolationMode effective_mode(const GGraphics *g)
{
    switch (g->interpolation) {
    case InterpolationModeDefault:
    case InterpolationModeLowQuality:
        return InterpolationModeBilinear;
    case InterpolationModeHighQuality:
        return InterpolationModeHighQualityBicubic;
    default:
        return g->interpolation;
    }
}

/* how far beyond the source rectangle the interpolation reads. scale is the device size over the source size. */
static float margin_of(InterpolationMode mode, float scale)
{
    switch (mode) {
    case InterpolationModeBilinear:
        return 1.0f;
    case InterpolationModeBicubic:
        return 2.0f;
    case InterpolationModeHighQualityBilinear:
        return scale >= 1.0f ? 1.0f : 1.0f / scale;
    case InterpolationModeHighQualityBicubic:
        return scale >= 1.0f ? 2.0f : 2.0f / scale;
    default:
        return 0.0f;
    }
}

static void affine_of(const MimeVecMatrix *m, Affine *a) { aff_set(a, m->m11, m->m12, m->m21, m->m22, m->dx, m->dy); }

typedef struct DrawCall {
    GGraphics *g;
    GBitmap *b;
    struct GAttrs *attrs;
    DrawImageAbort abort;
    void *abort_data;
} DrawCall;

/*
 * the driver of the draw: the three corners of the source rectangle on the page, the source rectangle in pixels
 * of the picture and the rows of the picture it needs, to the target.
 */
static GpStatus driver_draw(const DrawCall *c, const float pts[6], const float src[4], const Rows *rows, BOOL half,
                            const Affine *dev, InterpolationMode mode, WrapMode wrap, ARGB clamp)
{
    GGraphics *g = c->g;
    /* the source rectangle as the span sees it, in the rows that were taken */
    float rect[4] = {src[0], src[1], src[2], src[3]};
    if (half) {
        rect[0] -= 0.5f;
        rect[1] -= 0.5f;
    }
    Affine img, total;
    if (!aff_infer(&img, pts, rect))
        aff_identity(&img);
    aff_mul(&total, &img, dev);

    Paint paint;
    memset(&paint, 0, sizeof(paint));
    paint.g = g;
    paint.mode = g->compositing_mode;
    paint.clamp = clamp;
    GpStatus st = Ok;
    float poly[8] = {rect[0], rect[1], rect[0] + rect[2], rect[1], rect[0] + rect[2], rect[1] + rect[3], rect[0],
                     rect[1] + rect[3]};
    aff_transform(&total, poly, 4);
    if (rows->h <= 0 || rows->w <= 0) {
        paint.empty = TRUE;
    } else {
        SpanSetup setup;
        memset(&setup, 0, sizeof(setup));
        setup.px = rows->px;
        setup.w = rows->w;
        setup.h = rows->h;
        setup.stride = rows->stride;
        setup.wrap = wrap;
        setup.clamp = clamp;
        setup.mode = mode;
        setup.fwd = total;
        memcpy(setup.src, rect, sizeof(rect));
        setup.dest[0] = poly[0];
        setup.dest[1] = poly[1];
        setup.dest[2] = poly[4] - poly[0];
        setup.dest[3] = poly[5] - poly[1];
        if (!span_create(&paint.span, &setup))
            return OutOfMemory;
    }
    if (rect[2] > 0.0f && rect[3] > 0.0f)
        st = paint_polygon(&paint, poly);
    if (!paint.empty)
        span_free(&paint.span);
    gp_free(paint.buf);
    return st;
}

/*
 * the RotateFlipType a matrix with unit steps stands for, 0 when it is none of them. A turn or a flip of whole
 * pixels is not sampled, the picture is turned instead and drawn without a matrix.
 */
static INT rotate_flip_of(const Affine *a)
{
    const float eps = 0.00000011920929f;
    if (absf(a->m11) < eps && absf(a->m22) < eps) {
        if (absf(a->m21 - 1.0f) < eps) {
            if (absf(a->m12 - 1.0f) < eps)
                return 5;
            if (absf(a->m12 + 1.0f) < eps)
                return 3;
        }
        if (absf(a->m21 + 1.0f) < eps) {
            if (absf(a->m12 - 1.0f) < eps)
                return 1;
            if (absf(a->m12 + 1.0f) < eps)
                return 7;
        }
    }
    if (absf(a->m12) < eps && absf(a->m21) < eps) {
        if (absf(a->m11 - 1.0f) < eps && absf(a->m22 + 1.0f) < eps)
            return 6;
        if (absf(a->m11 + 1.0f) < eps) {
            if (absf(a->m22 - 1.0f) < eps)
                return 4;
            if (absf(a->m22 + 1.0f) < eps)
                return 2;
        }
    }
    return 0;
}

/*
 * draws the rectangle src of the picture, in pixels, where the matrix puts it. the matrix takes the points of the
 * rectangle to the world. unit is the unit the rectangle was given in, with src and the matrix already in
 * pixels when it is not a pixel unit.
 */
static GpStatus draw_bitmap(const DrawCall *c, const float *src_in, Affine mat, GpUnit unit)
{
    GGraphics *g = c->g;
    GBitmap *b = c->b;
    float sx, sy, sw, sh;
    if (!src_in) {
        sx = sy = 0.0f;
        sw = (float)b->width;
        sh = (float)b->height;
    } else {
        sx = src_in[0];
        sy = src_in[1];
        sw = src_in[2];
        sh = src_in[3];
    }
    if (unit != UnitWorld && unit != UnitDisplay && unit != UnitPixel) {
        float kx = b->dpi_x, ky = b->dpi_y;
        switch (unit) {
        case UnitPoint:
            kx /= 72.0f;
            ky /= 72.0f;
            break;
        case UnitDocument:
            kx /= 300.0f;
            ky /= 300.0f;
            break;
        case UnitMillimeter:
            kx /= 25.4f;
            ky /= 25.4f;
            break;
        default:
            break;
        }
        float ry = 1.0f / ky, rx = 1.0f / kx;
        sy *= ky;
        sh *= ky;
        sx *= kx;
        sw *= kx;
        aff_scale(&mat, rx, ry, FALSE);
    }
    /* a negative size is a flip */
    if (sw < 0.0f) {
        aff_translate(&mat, -sw, 0.0f, FALSE);
        aff_scale(&mat, -1.0f, 1.0f, FALSE);
        sx += sw;
        sw = -sw;
    }
    if (sh < 0.0f) {
        aff_translate(&mat, 0.0f, -sh, FALSE);
        aff_scale(&mat, 1.0f, -1.0f, FALSE);
        sy += sh;
        sh = -sh;
    }
    BOOL half = g->pixel_offset == PixelOffsetModeHighQuality || g->pixel_offset == PixelOffsetModeHalf;
    if (half) {
        sx += -0.5f;
        sy += -0.5f;
        mat.cx |= 1;
        mat.dx += (mat.m21 * 0.5f + mat.m11 * 0.5f);
        mat.dy += (mat.m12 * 0.5f + mat.m22 * 0.5f);
    }
    /* the box on the device must be a size the rasterizer can have */
    Affine w2d, total;
    affine_of(gp_graphics_w2d(g), &w2d);
    aff_mul(&total, &mat, &w2d);
    float box[4];
    aff_bounds(&total, sx, sy, sx + sw, sy + sh, box);
    const float limit = 1073741800.0f;
    if (!(box[0] >= -limit && box[0] <= limit) || !(box[1] >= -limit && box[1] <= limit) ||
        !(box[2] >= 0.0f && box[2] <= limit) || !(box[3] >= 0.0f && box[3] <= limit))
        return ValueOverflow;

    float pts[6] = {sx, sy, sx + sw, sy, sx, sy + sh};
    aff_transform(&mat, pts, 3);
    float src[4] = {sx, sy, sw, sh};

    /* the driver: a parallelogram with no area draws nothing */
    float det = (pts[0] - pts[2]) * (pts[5] - pts[1]) - (pts[1] - pts[3]) * (pts[4] - pts[0]);
    if (absf(det) < 0.00000011920929f)
        return Ok;
    Affine img;
    if (!aff_infer(&img, pts, src))
        aff_identity(&img);
    Affine drv;
    aff_mul(&drv, &img, &w2d);
    if (absf(drv.m22 * drv.m11 - drv.m21 * drv.m12) < 0.00000011920929f)
        return Ok;

    /*
     * a turn or flip by whole pixels: the picture is turned, the rectangle follows it and the page becomes a
     * translation. Edges are reflected, which is why the sizes of the picture are used rather than the sizes
     * less one.
     */
    Affine dev = w2d;
    INT type = aff_integer_translate(&drv) ? 0 : rotate_flip_of(&drv);
    INT img_w = b->width, img_h = b->height;
    UINT32 *turned = nullptr;
    BOOL is_turned = type != 0;
    if (type) {
        float x = src[0], y = src[1], w = src[2], h = src[3];
        float dx = drv.dx, dy = drv.dy;
        if (half) {
            x += 0.5f;
            y += 0.5f;
            dx = (dx + 0.5f) + ((drv.m11 * -0.5f) - (drv.m21 * 0.5f));
            dy = (dy + 0.5f) + ((drv.m22 * -0.5f) - (drv.m12 * 0.5f));
        }
        float fw = (float)b->width, fh = (float)b->height;
        float nx = x, ny = y, nw = w, nh = h, tx = 0.0f, ty = 0.0f;
        float rx = x + (fw - (x * 2.0f + w)), ry = y + (fh - (y * 2.0f + h));
        switch (type) {
        case 1:
            nx = ry;
            ny = x + 0.0f;
            nw = h;
            nh = w;
            tx = -fh;
            break;
        case 2:
            nx = rx;
            ny = ry;
            tx = -fw;
            ty = -fh;
            break;
        case 3:
            nx = y + 0.0f;
            ny = rx;
            nw = h;
            nh = w;
            ty = -fw;
            break;
        case 4:
            nx = rx;
            ny = y + 0.0f;
            tx = -fw;
            break;
        case 5:
            nx = y;
            ny = x;
            nw = h;
            nh = w;
            break;
        case 6:
            nx = x + 0.0f;
            ny = ry;
            ty = -fh;
            break;
        default:
            nx = ry;
            ny = rx;
            nw = h;
            nh = w;
            tx = -fh;
            ty = -fw;
            break;
        }
        dx += tx;
        dy += ty;
        src[0] = nx;
        src[1] = ny;
        src[2] = nw;
        src[3] = nh;
        pts[0] = nx;
        pts[1] = ny;
        pts[2] = nw + nx;
        pts[3] = ny;
        pts[4] = nx;
        pts[5] = nh + ny;
        aff_set(&dev, 1.0f, 0.0f, 0.0f, 1.0f, dx, dy);
        drv = dev;
        INT tw, th;
        GpStatus st = rotated_buffer(b, type, c->attrs, &turned, &tw, &th);
        if (st != Ok)
            return st;
        img_w = tw;
        img_h = th;
    }

    InterpolationMode mode = effective_mode(g);
    ARGB clamp = 0;
    WrapMode wrap = gp_attrs_wrap(c->attrs, &clamp);

    /* the lock: the whole rows from the first to the last the interpolation can reach */
    float lock[4] = {src[0], src[1], src[2], src[3]};
    if (half) {
        lock[0] += 0.5f;
        lock[1] += 0.5f;
    }
    float lbox[4];
    aff_bounds(&drv, lock[0], lock[1], lock[0] + lock[2], lock[1] + lock[3], lbox);
    float wabs = absf(lock[2]), habs = absf(lock[3]);
    float dev_w = absf((lbox[0] + lbox[2]) - lbox[0]), dev_h = absf((lbox[1] + lbox[3]) - lbox[1]);

    /*
     * a picture of 420 pixels or more that is drawn smaller is first averaged down to the size it will have, in the
     * bilinear and bicubic modes: the filters of the span only look at a few pixels around a point.
     */
    UINT32 *scaled = nullptr;
    BOOL prescale = !is_turned && (drv.cx & ~3) == 0 && (drv.m11 - 1.0f < -0.00000011920929f || drv.m22 - 1.0f < -0.00000011920929f) &&
                    mode != InterpolationModeNearestNeighbor;
    if (prescale && wabs < 420.0f && habs < 420.0f)
        prescale = FALSE;
    if (mode == InterpolationModeHighQualityBilinear || mode == InterpolationModeHighQualityBicubic)
        prescale = FALSE;
    INT new_w = 0, new_h = 0;
    float fx = 1.0f, fy = 1.0f;
    if (prescale) {
        float mul = mode == InterpolationModeBicubic ? 2.0f : 1.0f;
        float want_w = wabs < dev_w * mul ? wabs : dev_w * mul;
        float want_h = habs < dev_h * mul ? habs : dev_h * mul;
        const float tiny = 0.00000011920929f;
        if (wabs > want_w + tiny || habs > want_h + tiny) {
            fx = want_w / wabs;
            new_w = floor_int((float)img_w * fx + 0.5f);
            if ((float)img_w / (float)new_w > 255.0f) {
                fx = 0.0039215689f;
                new_w = ceil_int((float)img_w * fx);
            }
            fy = want_h / habs;
            new_h = floor_int((float)img_h * fy + 0.5f);
            if ((float)img_h / (float)new_h > 255.0f) {
                fy = 0.0039215689f;
                new_h = ceil_int((float)img_h * fy);
            }
            lock[0] *= fx;
            lock[2] *= fx;
            lock[1] *= fy;
            lock[3] *= fy;
            wabs = absf(lock[2]);
            habs = absf(lock[3]);
        } else {
            prescale = FALSE;
        }
    }
    INT page_h = prescale ? new_h : img_h;
    float scale_y = dev_h / habs;
    INT first = floor_int(lock[1]);
    INT last = ceil_int(habs + lock[1]);
    INT margin = ceil_int(margin_of(mode, scale_y));
    INT top = first - margin, bottom = last + margin;
    INT height = page_h;
    if ((INT)wrap == 4) {
        if (top > height || bottom < 0) {
            top = bottom = 0;
        } else {
            if (top < 0)
                top = 0;
            if (bottom > height)
                bottom = height;
        }
    } else {
        if (top < 0 || top > height)
            top = 0;
        if (bottom < 0 || bottom > height)
            bottom = height;
    }
    if (c->abort && c->abort(c->abort_data)) {
        gp_free(turned);
        return Aborted;
    }
    Rows rows;
    GpStatus st = Ok;
    if (is_turned) {
        rows_of_buffer(turned, img_w, img_h, top, bottom, &rows);
    } else if (prescale) {
        memset(&rows, 0, sizeof(rows));
        rows.w = new_w;
        rows.h = bottom - top;
        if (rows.h > 0) {
            st = area_scale(b, c->attrs, new_w, new_h, top, bottom, &scaled);
            rows.px = scaled;
            rows.stride = new_w;
        }
    } else {
        st = rows_build(b, top, bottom, c->attrs, &rows);
    }
    if (st != Ok) {
        gp_free(turned);
        return st;
    }
    float rel[4] = {lock[0], lock[1] - (float)top, lock[2], lock[3]};
    st = driver_draw(c, pts, rel, &rows, half, &dev, mode, wrap, clamp);
    rows_free(&rows);
    gp_free(scaled);
    gp_free(turned);
    return st;
}

/* ------------------------------------------------------------------ the shapes of a call */

/* the rectangle with the flips undone: a negative size starts at the other end. */
static void undo_source_flip(float r[4])
{
    if (r[2] < 0.0f) {
        r[0] = r[2] + r[0];
        r[2] = -r[2];
    }
    if (r[3] < 0.0f) {
        r[1] = r[3] + r[1];
        r[3] = -r[3];
    }
}

/* the source rectangle onto three points of the page. */
static GpStatus draw_points(const DrawCall *c, const GpPointF *p, const float src_in[4], GpUnit unit)
{
    float pts[6] = {p[0].X, p[0].Y, p[1].X, p[1].Y, p[2].X, p[2].Y};
    Affine m;
    aff_identity(&m);
    aff_infer(&m, pts, src_in);
    float src[4] = {src_in[0], src_in[1], src_in[2], src_in[3]};
    undo_source_flip(src);
    return draw_bitmap(c, src, m, unit);
}

/* the source rectangle into a destination rectangle. */
static GpStatus draw_rects(const DrawCall *c, const float d[4], const float s[4], GpUnit unit)
{
    float dest_right = d[0] + d[2], dest_bottom = d[1] + d[3];
    float src_right = s[2] + s[0], src_bottom = s[3] + s[1];
    Affine m;
    aff_identity(&m);
    if (s[0] != src_right && s[1] != src_bottom) {
        float m11 = (dest_right - d[0]) / (src_right - s[0]);
        float m22 = (dest_bottom - d[1]) / (src_bottom - s[1]);
        float dx = dest_right - m11 * src_right;
        float dy = dest_bottom - m22 * src_bottom;
        aff_set(&m, m11, 0.0f, 0.0f, m22, dx, dy);
    }
    float src[4] = {s[0], s[1], s[2], s[3]};
    if (s[2] < 0.0f) {
        src[0] = src_right;
        src[2] = -s[2];
    }
    if (s[3] < 0.0f) {
        src[1] = src_bottom;
        src[3] = -s[3];
    }
    return draw_bitmap(c, src, m, unit);
}

/* ------------------------------------------------------------------ units */

/* the sizes a picture has when it is drawn as it is: its own resolution against that of the page. */
static void page_size(const GGraphics *g, const GBitmap *b, float w, float h, GpUnit unit, float *ow, float *oh)
{
    float sx, sy;
    gp_graphics_page_scale(g, &sx, &sy);
    if (unit == UnitPixel) {
        *ow = (g->dpi_x * w) / (b->dpi_x * sx);
        *oh = (g->dpi_y * h) / (b->dpi_y * sy);
        return;
    }
    float mx, my;
    switch (unit) {
    case UnitPoint:
        mx = (1.0f * g->dpi_x) / 72.0f;
        my = (1.0f * g->dpi_y) / 72.0f;
        break;
    case UnitInch:
        mx = 1.0f * g->dpi_x;
        my = 1.0f * g->dpi_y;
        break;
    case UnitDocument:
        mx = (1.0f * g->dpi_x) / 300.0f;
        my = (1.0f * g->dpi_y) / 300.0f;
        break;
    case UnitMillimeter:
        mx = (1.0f * g->dpi_x) / 25.4f;
        my = (1.0f * g->dpi_y) / 25.4f;
        break;
    default:
        mx = (1.0f * g->dpi_x) / 100.0f;
        my = (1.0f * g->dpi_y) / 100.0f;
        break;
    }
    *ow = (mx * w) / sx;
    *oh = (my * h) / sy;
}

static BOOL unit_ok(GpUnit unit) { return (UINT)unit - UnitPixel <= 4; }

/* ------------------------------------------------------------------ images that are not bitmaps */

static void rect_points(float x, float y, float w, float h, GpPointF *p)
{
    p[0].X = x;
    p[0].Y = y;
    p[1].X = x + w;
    p[1].Y = y;
    p[2].X = x;
    p[2].Y = y + h;
}

/* the bounds of an image in its own unit. */
static GpStatus image_bounds(GImage *image, float r[4], GpUnit *unit)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (b) {
        r[0] = r[1] = 0.0f;
        r[2] = (float)b->width;
        r[3] = (float)b->height;
        *unit = UnitPixel;
        return Ok;
    }
    GpRectF f;
    GpStatus st = gp_metafile_bounds(image, &f, unit);
    if (st == Ok) {
        r[0] = f.X;
        r[1] = f.Y;
        r[2] = f.Width;
        r[3] = f.Height;
    }
    return st;
}

/* draws an image given three points of the page. a bitmap goes through the matrix, a metafile is played. */
static GpStatus draw_any_points(const DrawCall *c, GImage *image, const GpPointF *p, const float src[4], GpUnit unit)
{
    if (c->g->meta)
        return gp_rec_draw_image(c->g, image, p, src[0], src[1], src[2], src[3], unit, c->attrs);
    if (c->b)
        return draw_points(c, p, src, unit);
    return gp_metafile_draw(c->g, image, p, src[0], src[1], src[2], src[3], unit, c->attrs);
}

/* the destination and source rectangles, for the calls that have both. */
static GpStatus draw_any_rects(const DrawCall *c, GImage *image, const float d[4], const float s[4], GpUnit unit)
{
    if (c->g->meta) {
        GpPointF q[3];
        rect_points(d[0], d[1], d[2], d[3], q);
        return gp_rec_draw_image(c->g, image, q, s[0], s[1], s[2], s[3], unit, c->attrs);
    }
    if (c->b)
        return draw_rects(c, d, s, unit);
    GpPointF p[3];
    rect_points(d[0], d[1], d[2], d[3], p);
    return gp_metafile_draw(c->g, image, p, s[0], s[1], s[2], s[3], unit, c->attrs);
}

GpStatus gp_draw_image(GGraphics *g, GImage *image, const GpPointF *dst_points, INT dst_count, float sx, float sy,
                       float sw, float sh, GpUnit src_unit, struct GAttrs *attrs)
{
    if (!g || !image || !dst_points || dst_count != 3)
        return InvalidParameter;
    if (g->busy)
        return ObjectBusy;
    DrawCall c = {g, gp_bitmap_cast(image), attrs, nullptr, nullptr};
    float src[4] = {sx, sy, sw, sh};
    return draw_any_points(&c, image, dst_points, src, src_unit);
}

/* ------------------------------------------------------------------ the api */

/* the checks every export makes first, in the order gdiplus.dll makes them. */
static GpStatus enter(GpGraphics *graphics, GpImage *image, GGraphics **gp, GImage **ip, GBitmap **bp)
{
    GGraphics *g = gp_graphics_cast(graphics);
    if (!g)
        return InvalidParameter;
    if (g->busy)
        return ObjectBusy;
    GImage *i = gp_image_cast(image);
    if (!i)
        return InvalidParameter;
    GBitmap *b = gp_bitmap_cast(i);
    if (b && b->locked && (b->lock_flags & ImageLockModeWrite))
        return ObjectBusy;
    *gp = g;
    *ip = i;
    *bp = b;
    return Ok;
}

static GpStatus attrs_ok(const GpImageAttributes *attrs, struct GAttrs **out)
{
    *out = nullptr;
    if (!attrs)
        return Ok;
    *out = gp_attrs_cast(attrs);
    return *out ? Ok : InvalidParameter;
}

/* the natural size of a picture on the page, and the rectangle of the call that wants it. */
static GpStatus natural_rect(const DrawCall *c, GImage *image, float x, float y, const float src[4], GpUnit unit,
                             float d[4])
{
    float w, h;
    if (c->b) {
        page_size(c->g, c->b, src[2], src[3], unit, &w, &h);
    } else {
        GpStatus st = gp_metafile_page_size(c->g, image, src[2], src[3], unit, &w, &h);
        if (st != Ok)
            return st;
    }
    d[0] = x;
    d[1] = y;
    d[2] = w;
    d[3] = h;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDrawImage(GpGraphics *graphics, GpImage *image, REAL x, REAL y)
{
    GGraphics *g;
    GImage *i;
    GBitmap *b;
    GpStatus st = enter(graphics, image, &g, &i, &b);
    if (st != Ok)
        return st;
    DrawCall c = {g, b, nullptr, nullptr, nullptr};
    float src[4];
    GpUnit unit;
    st = image_bounds(i, src, &unit);
    if (st != Ok)
        return st;
    float d[4];
    st = natural_rect(&c, i, x, y, src, unit, d);
    if (st != Ok)
        return st;
    return draw_any_rects(&c, i, d, src, unit == UnitPixel ? UnitPixel : unit);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageI(GpGraphics *graphics, GpImage *image, INT x, INT y)
{
    return GdipDrawImage(graphics, image, (float)x, (float)y);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRect(GpGraphics *graphics, GpImage *image, REAL x, REAL y, REAL w, REAL h)
{
    GGraphics *g;
    GImage *i;
    GBitmap *b;
    GpStatus st = enter(graphics, image, &g, &i, &b);
    if (st != Ok)
        return st;
    DrawCall c = {g, b, nullptr, nullptr, nullptr};
    float src[4];
    GpUnit unit;
    st = image_bounds(i, src, &unit);
    if (st != Ok)
        return st;
    float d[4] = {x, y, w, h};
    return draw_any_rects(&c, i, d, src, unit);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT w, INT h)
{
    return GdipDrawImageRect(graphics, image, (float)x, (float)y, (float)w, (float)h);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePoints(GpGraphics *graphics, GpImage *image, const GpPointF *points, INT count)
{
    if (!points || count <= 0)
        return InvalidParameter;
    GGraphics *g;
    GImage *i;
    GBitmap *b;
    GpStatus st = enter(graphics, image, &g, &i, &b);
    if (st != Ok)
        return st;
    DrawCall c = {g, b, nullptr, nullptr, nullptr};
    float src[4];
    GpUnit unit;
    st = image_bounds(i, src, &unit);
    if (st != Ok)
        return st;
    if (count == 4)
        return NotImplemented;
    if (count != 3)
        return InvalidParameter;
    return draw_any_points(&c, i, points, src, unit);
}

static GpStatus points_from_int(const GpPoint *points, INT count, GpPointF **out)
{
    if (!points || count <= 0)
        return InvalidParameter;
    GpPointF *f = gp_alloc((SIZE_T)count * sizeof(GpPointF));
    if (!f)
        return OutOfMemory;
    for (INT k = 0; k < count; k++) {
        f[k].X = (float)points[k].X;
        f[k].Y = (float)points[k].Y;
    }
    *out = f;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsI(GpGraphics *graphics, GpImage *image, const GpPoint *points, INT count)
{
    GpPointF *f;
    GpStatus st = points_from_int(points, count, &f);
    if (st != Ok)
        return st;
    st = GdipDrawImagePoints(graphics, image, f, count);
    gp_free(f);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointRect(GpGraphics *graphics, GpImage *image, REAL x, REAL y, REAL sx,
                                                  REAL sy, REAL sw, REAL sh, GpUnit unit)
{
    GGraphics *g;
    GImage *i;
    GBitmap *b;
    GpStatus st = enter(graphics, image, &g, &i, &b);
    if (st != Ok)
        return st;
    if (!unit_ok(unit))
        return InvalidParameter;
    DrawCall c = {g, b, nullptr, nullptr, nullptr};
    float src[4] = {sx, sy, sw, sh};
    float d[4];
    st = natural_rect(&c, i, x, y, src, unit, d);
    if (st != Ok)
        return st;
    return draw_any_rects(&c, i, d, src, unit);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT sx, INT sy,
                                                   INT sw, INT sh, GpUnit unit)
{
    return GdipDrawImagePointRect(graphics, image, (float)x, (float)y, (float)sx, (float)sy, (float)sw, (float)sh, unit);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRectRect(GpGraphics *graphics, GpImage *image, REAL dx, REAL dy, REAL dw,
                                                 REAL dh, REAL sx, REAL sy, REAL sw, REAL sh, GpUnit unit,
                                                 const GpImageAttributes *attrs, DrawImageAbort abort, VOID *data)
{
    GGraphics *g;
    GImage *i;
    GBitmap *b;
    GpStatus st = enter(graphics, image, &g, &i, &b);
    if (st != Ok)
        return st;
    if (!unit_ok(unit))
        return InvalidParameter;
    struct GAttrs *a;
    st = attrs_ok(attrs, &a);
    if (st != Ok)
        return st;
    DrawCall c = {g, b, a, abort, data};
    float d[4] = {dx, dy, dw, dh}, s[4] = {sx, sy, sw, sh};
    return draw_any_rects(&c, i, d, s, unit);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRectRectI(GpGraphics *graphics, GpImage *image, INT dx, INT dy, INT dw, INT dh,
                                                  INT sx, INT sy, INT sw, INT sh, GpUnit unit,
                                                  const GpImageAttributes *attrs, DrawImageAbort abort, VOID *data)
{
    return GdipDrawImageRectRect(graphics, image, (float)dx, (float)dy, (float)dw, (float)dh, (float)sx, (float)sy,
                                 (float)sw, (float)sh, unit, attrs, abort, data);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsRect(GpGraphics *graphics, GpImage *image, const GpPointF *points,
                                                   INT count, REAL sx, REAL sy, REAL sw, REAL sh, GpUnit unit,
                                                   const GpImageAttributes *attrs, DrawImageAbort abort, VOID *data)
{
    if (!points || count <= 0)
        return InvalidParameter;
    GGraphics *g;
    GImage *i;
    GBitmap *b;
    GpStatus st = enter(graphics, image, &g, &i, &b);
    if (st != Ok)
        return st;
    if (!unit_ok(unit))
        return InvalidParameter;
    struct GAttrs *a;
    st = attrs_ok(attrs, &a);
    if (st != Ok)
        return st;
    DrawCall c = {g, b, a, abort, data};
    if (count == 4)
        return NotImplemented;
    if (count != 3)
        return InvalidParameter;
    float s[4] = {sx, sy, sw, sh};
    return draw_any_points(&c, i, points, s, unit);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsRectI(GpGraphics *graphics, GpImage *image, const GpPoint *points,
                                                    INT count, INT sx, INT sy, INT sw, INT sh, GpUnit unit,
                                                    const GpImageAttributes *attrs, DrawImageAbort abort, VOID *data)
{
    GpPointF *f;
    GpStatus st = points_from_int(points, count, &f);
    if (st != Ok)
        return st;
    st = GdipDrawImagePointsRect(graphics, image, f, count, (float)sx, (float)sy, (float)sw, (float)sh, unit, attrs,
                                 abort, data);
    gp_free(f);
    return st;
}
