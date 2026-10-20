/*
 * drawing images: the GdipDrawImage calls. They all end in one draw of a rectangle of a bitmap through a matrix:
 * the units and the flips of the source rectangle are settled, the rows of the picture that the draw reads are
 * picked (a big picture drawn small is averaged down first), and the parallelogram the picture lands on is filled
 * from a sampler. docs/gdiplus.md has the rules.
 *
 * the matrix of a call takes the source rectangle to the page. The graphics' world to device matrix follows it.
 */
#include "image_private.h"

#define SMALL (1.1920929e-7f)
#define COORDINATE_LIMIT (1073741800.0f)
/* a picture side from which a draw smaller than the picture is averaged down first */
#define BIG_SIDE (420.0f)

static float mag(float v) { return v < 0.0f ? -v : v; }

/* ------------------------------------------------------------------ settings of the graphics */

static BOOL half_pixel(const GGraphics *g)
{
    return g->pixel_offset == PixelOffsetModeHighQuality || g->pixel_offset == PixelOffsetModeHalf;
}

/* the mode the sampler works with: the graphics keeps the plain ones, but a raw value is not refused. */
static InterpolationMode mode_of(const GGraphics *g)
{
    switch (g->interpolation) {
    case InterpolationModeHighQuality:
        return InterpolationModeHighQualityBicubic;
    case InterpolationModeBilinear:
    case InterpolationModeBicubic:
    case InterpolationModeNearestNeighbor:
    case InterpolationModeHighQualityBilinear:
    case InterpolationModeHighQualityBicubic:
        return g->interpolation;
    default:
        return InterpolationModeBilinear;
    }
}

/* how many rows beyond the source rectangle the sampler may look at, at a scale. */
static float rows_beyond(InterpolationMode mode, float scale)
{
    switch (mode) {
    case InterpolationModeNearestNeighbor:
        return 0.0f;
    case InterpolationModeBicubic:
        return 2.0f;
    case InterpolationModeHighQualityBilinear:
        return scale < 1.0f ? 1.0f / scale : 1.0f;
    case InterpolationModeHighQualityBicubic:
        return scale < 1.0f ? 2.0f / scale : 2.0f;
    default:
        return 1.0f;
    }
}

/* ------------------------------------------------------------------ the rows of the picture */

typedef struct Rows {
    const UINT32 *px;
    INT w, h;
    SSIZE_T stride;     /* in pixels */
    UINT32 *owned;
} Rows;

static void rows_free(Rows *r)
{
    gp_free(r->owned);
    memset(r, 0, sizeof(*r));
}

/* the rows top to bottom as premultiplied pixels, adjusted by the attributes. Rows that are already so are not copied. */
static GpStatus rows_read(const GGraphics *g, GBitmap *b, struct GAttrs *attrs, INT top, INT bottom, Rows *out)
{
    INT w = b->width, count = bottom - top;
    memset(out, 0, sizeof(*out));
    out->w = w;
    out->h = count;
    BOOL adjust = attrs && gp_attrs_active(attrs, ColorAdjustTypeBitmap);
    if (b->format == PixelFormat32bppPARGB && !adjust && g->target != b && b->bits && !(b->stride & 3) &&
        !((UINT_PTR)b->bits & 3)) {
        out->px = (const UINT32 *)(b->bits + (SSIZE_T)top * b->stride);
        out->stride = b->stride / 4;
        return Ok;
    }
    if ((SIZE_T)w * (SIZE_T)count * sizeof(UINT32) > GP_MAX_BYTES)
        return OutOfMemory;
    out->owned = gp_alloc(sizeof(UINT32) * (SIZE_T)w * (SIZE_T)count);
    if (!out->owned)
        return OutOfMemory;
    for (INT y = 0; y < count; y++) {
        UINT32 *row = out->owned + (SIZE_T)y * w;
        gp_bitmap_read_argb(b, 0, top + y, w, row);
        if (adjust)
            gp_attrs_apply(attrs, ColorAdjustTypeBitmap, row, w);
        for (INT x = 0; x < w; x++)
            row[x] = gp_premultiply(row[x]);
    }
    out->px = out->owned;
    out->stride = w;
    return Ok;
}

/*
 * the rows turned by a quarter turn or a flip whose linear part is the matrix, so that the matrix becomes a
 * translation. The index of a pixel goes to k' = k * linear + corner, where corner keeps it inside the new rows.
 */
static GpStatus rows_turn(Rows *r, const ImgMat *lin, INT corner[2])
{
    INT w = r->w, h = r->h;
    /* the entries are one, minus one or zero, give or take the last bit of a float */
    INT a = (INT)(lin->m11 < 0.0f ? lin->m11 - 0.5f : lin->m11 + 0.5f);
    INT b = (INT)(lin->m12 < 0.0f ? lin->m12 - 0.5f : lin->m12 + 0.5f);
    INT c = (INT)(lin->m21 < 0.0f ? lin->m21 - 0.5f : lin->m21 + 0.5f);
    INT d = (INT)(lin->m22 < 0.0f ? lin->m22 - 0.5f : lin->m22 + 0.5f);
    BOOL swapped = a == 0;
    INT nw = swapped ? h : w, nh = swapped ? w : h;
    corner[0] = (a < 0 ? w - 1 : 0) + (c < 0 ? h - 1 : 0);
    corner[1] = (b < 0 ? w - 1 : 0) + (d < 0 ? h - 1 : 0);
    UINT32 *turned = gp_alloc(sizeof(UINT32) * (SIZE_T)nw * (SIZE_T)nh);
    if (!turned)
        return OutOfMemory;
    for (INT y = 0; y < h; y++) {
        const UINT32 *row = r->px + (SSIZE_T)y * r->stride;
        for (INT x = 0; x < w; x++) {
            INT tx = a * x + c * y + corner[0];
            INT ty = b * x + d * y + corner[1];
            turned[(SIZE_T)ty * nw + tx] = row[x];
        }
    }
    rows_free(r);
    r->owned = turned;
    r->px = turned;
    r->w = nw;
    r->h = nh;
    r->stride = nw;
    return Ok;
}

/* ------------------------------------------------------------------ the fill */

typedef struct ImageSource {
    GSource base;
    Sampler sampler;
} ImageSource;

static void image_fetch(GSource *source, INT x, INT y, INT count, UINT32 *out)
{
    ImageSource *s = (ImageSource *)source;
    sampler_run(&s->sampler, y, x, x + count, out);
}

static void image_destroy(GSource *source)
{
    ImageSource *s = (ImageSource *)source;
    sampler_free(&s->sampler);
    gp_free(s);
}

static void plain_destroy(GSource *source) { gp_free(source); }

static void plain_fetch(GSource *source, INT x, INT y, INT count, UINT32 *out)
{
    for (INT i = 0; i < count; i++)
        out[i] = source->color;
}

typedef struct DriverJob {
    Rows rows;
    float rect[4];       /* the source rectangle in the rows, before the half pixel */
    float pts[6];        /* where its top left, top right and bottom left corners are on the page */
    ImgMat to_device;
    INT wrap;
    UINT32 clamp;
    InterpolationMode mode;
    BOOL half;
} DriverJob;

/* fills the parallelogram the rectangle lands on. A picture of no rows is the clamp color all over. */
static GpStatus driver_draw(GGraphics *g, const DriverJob *job)
{
    float rect[4] = {job->rect[0], job->rect[1], job->rect[2], job->rect[3]};
    if (job->half) {
        rect[0] -= 0.5f;
        rect[1] -= 0.5f;
    }
    ImgMat img;
    if (!mat_from_points(&img, job->pts, rect))
        mat_identity(&img);
    ImgMat total;
    mat_then(&total, &img, &job->to_device);
    float c[8] = {rect[0], rect[1], rect[0] + rect[2], rect[1], rect[0] + rect[2], rect[1] + rect[3], rect[0],
                  rect[1] + rect[3]};
    mat_apply(&total, c, 4);
    if (!(rect[2] > 0.0f && rect[3] > 0.0f))
        return Ok;

    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    mime_vec_polys_begin(&polys);
    for (INT i = 0; i < 4; i++)
        mime_vec_polys_add(&polys, c[2 * i], c[2 * i + 1]);
    if (mime_vec_polys_end(&polys, true) != MIME_VEC_OK) {
        mime_vec_polys_free(&polys);
        return OutOfMemory;
    }
    MimeVecBounds bb;
    GpStatus status = Ok;
    if (mime_vec_polys_bounds(&polys, &bb)) {
        INT aa = gp_smoothing_samples(g->smoothing);
        float lim = 4.0e6f;
        bb.left = bb.left < -lim ? -lim : bb.left;
        bb.top = bb.top < -lim ? -lim : bb.top;
        bb.right = bb.right > lim ? lim : bb.right;
        bb.bottom = bb.bottom > lim ? lim : bb.bottom;
        RECT area = {(INT)mime_vec_floorf(bb.left) - 2 - aa, (INT)mime_vec_floorf(bb.top) - 2 - aa,
                     (INT)mime_vec_ceilf(bb.right) + 3 + aa, (INT)mime_vec_ceilf(bb.bottom) + 3 + aa};
        GSource *source = nullptr;
        if (job->rows.h <= 0) {
            source = gp_alloc_zero(sizeof(GSource));
            if (source) {
                source->fetch = plain_fetch;
                source->destroy = plain_destroy;
                source->constant = TRUE;
                source->color = job->clamp;
            }
        } else {
            ImageSource *is = gp_alloc_zero(sizeof(*is));
            SamplerSetup setup = {job->rows.px,   job->rows.w, job->rows.h, job->rows.stride, job->wrap,
                                  job->clamp,     job->mode,   total,       {rect[0], rect[1], rect[2], rect[3]},
                                  {c[0], c[1], c[4] - c[0], c[5] - c[1]}};
            if (is && sampler_create(&is->sampler, &setup)) {
                is->base.fetch = image_fetch;
                is->base.destroy = image_destroy;
                source = &is->base;
            } else {
                gp_free(is);
            }
        }
        if (!source)
            status = OutOfMemory;
        else
            status = gp_fill_polys_src(g, &polys, MIME_VEC_FILL_ALTERNATE, aa, source, &area);
        gp_source_free(source);
    }
    mime_vec_polys_free(&polys);
    return status;
}

/* ------------------------------------------------------------------ one draw of a bitmap */

/*
 * draws the rectangle of the bitmap through the matrix to the page. The rectangle is in the unit, or in pixels
 * for the units of the page. A negative size starts at the other end.
 */
static GpStatus draw_matrix(GGraphics *g, GBitmap *b, const float rect_in[4], const ImgMat *mat_in, GpUnit unit,
                            struct GAttrs *attrs, DrawImageAbort stop, void *stop_data)
{
    float sx = rect_in[0], sy = rect_in[1], sw = rect_in[2], sh = rect_in[3];
    ImgMat mat = *mat_in;

    /* a physical unit is turned into pixels of the picture */
    if (unit != UnitWorld && unit != UnitDisplay && unit != UnitPixel) {
        float kx = b->dpi_x, ky = b->dpi_y;
        if (unit == UnitPoint) {
            kx /= 72.0f;
            ky /= 72.0f;
        } else if (unit == UnitDocument) {
            kx /= 300.0f;
            ky /= 300.0f;
        } else if (unit == UnitMillimeter) {
            kx /= 25.4f;
            ky /= 25.4f;
        }
        sx *= kx;
        sw *= kx;
        sy *= ky;
        sh *= ky;
        mat_stretch(&mat, 1.0f / kx, 1.0f / ky, FALSE);
    }
    /* a rectangle with a negative size is the one the other way round, mirrored */
    if (sw < 0.0f) {
        mat_shift(&mat, -sw, 0.0f, FALSE);
        mat_stretch(&mat, -1.0f, 1.0f, FALSE);
        sx += sw;
        sw = -sw;
    }
    if (sh < 0.0f) {
        mat_shift(&mat, 0.0f, -sh, FALSE);
        mat_stretch(&mat, 1.0f, -1.0f, FALSE);
        sy += sh;
        sh = -sh;
    }
    /* with the half pixel offsets the page is moved back by half a pixel, so the rectangle follows it */
    BOOL half = half_pixel(g);
    if (half) {
        sx -= 0.5f;
        sy -= 0.5f;
        mat.dx += mat.m21 * 0.5f + mat.m11 * 0.5f;
        mat.dy += mat.m12 * 0.5f + mat.m22 * 0.5f;
        mat.kind |= MAT_SHIFT;
    }
    ImgMat world;
    mat_from_vec(&world, gp_graphics_w2d(g));
    ImgMat total;
    mat_then(&total, &mat, &world);
    float box[4];
    mat_box(&total, sx, sy, sx + sw, sy + sh, box);
    if (!(box[0] >= -COORDINATE_LIMIT && box[0] <= COORDINATE_LIMIT) ||
        !(box[1] >= -COORDINATE_LIMIT && box[1] <= COORDINATE_LIMIT) || !(box[2] >= 0.0f && box[2] <= COORDINATE_LIMIT) ||
        !(box[3] >= 0.0f && box[3] <= COORDINATE_LIMIT))
        return ValueOverflow;

    /* where three corners of the rectangle are on the page, no area is nothing to draw */
    float pts[6] = {sx, sy, sx + sw, sy, sx, sy + sh};
    mat_apply(&mat, pts, 3);
    if (mag((pts[0] - pts[2]) * (pts[5] - pts[1]) - (pts[1] - pts[3]) * (pts[4] - pts[0])) < SMALL)
        return Ok;
    float shifted[4] = {sx, sy, sw, sh};
    ImgMat img;
    if (!mat_from_points(&img, pts, shifted))
        mat_identity(&img);
    ImgMat drv;
    mat_then(&drv, &img, &world);
    if (mag(drv.m22 * drv.m11 - drv.m21 * drv.m12) < SMALL)
        return Ok;

    /* the rectangle to read, the way the picture is before the half pixel */
    float lx = sx + (half ? 0.5f : 0.0f), ly = sy + (half ? 0.5f : 0.0f), lw = sw, lh = sh;
    float dev[4];
    mat_box(&drv, lx, ly, lx + lw, ly + lh, dev);
    float dev_w = mag((dev[0] + dev[2]) - dev[0]), dev_h = mag((dev[1] + dev[3]) - dev[1]);
    InterpolationMode mode = mode_of(g);
    BOOL turned = mat_turn_type(&drv) != 0;
    INT pic_w = b->width, pic_h = b->height;
    INT new_w = pic_w, new_h = pic_h;
    BOOL shrink = FALSE;
    float wabs = mag(lw), habs = mag(lh);

    /* a big picture drawn much smaller is averaged down first, to about the size it will have */
    if ((drv.kind & ~3u) == 0 && (drv.m11 - 1.0f < -SMALL || drv.m22 - 1.0f < -SMALL) &&
        mode != InterpolationModeNearestNeighbor && mode != InterpolationModeHighQualityBilinear &&
        mode != InterpolationModeHighQualityBicubic && !turned && (wabs >= BIG_SIDE || habs >= BIG_SIDE)) {
        float reach = mode == InterpolationModeBicubic ? 2.0f : 1.0f;
        float want_w = dev_w * reach < wabs ? dev_w * reach : wabs;
        float want_h = dev_h * reach < habs ? dev_h * reach : habs;
        if (wabs > want_w + SMALL || habs > want_h + SMALL) {
            float fx = want_w / wabs, fy = want_h / habs;
            new_w = floor_int((float)pic_w * fx + 0.5f);
            if ((float)pic_w / (float)new_w > 255.0f) {
                fx = 0.0039215689f;
                new_w = ceil_int((float)pic_w * fx);
            }
            new_h = floor_int((float)pic_h * fy + 0.5f);
            if ((float)pic_h / (float)new_h > 255.0f) {
                fy = 0.0039215689f;
                new_h = ceil_int((float)pic_h * fy);
            }
            lx *= fx;
            lw *= fx;
            ly *= fy;
            lh *= fy;
            habs = mag(lh);
            shrink = TRUE;
        }
    }

    /* the rows the sampler may touch: the rectangle's, and the ones it blends with past its edges */
    INT page_h = shrink ? new_h : pic_h;
    float scale_y = habs > 0.0f ? dev_h / habs : 1.0f;
    INT top = floor_int(ly) - ceil_int(rows_beyond(mode, scale_y));
    INT bottom = ceil_int(habs + ly) + ceil_int(rows_beyond(mode, scale_y));
    ARGB clamp_color = 0;
    WrapMode wrap = attrs ? gp_attrs_wrap(attrs, &clamp_color) : WrapModeClamp;
    if (wrap == WrapModeClamp) {
        if (top > page_h || bottom < 0) {
            top = bottom = 0;
        } else {
            if (top < 0)
                top = 0;
            if (bottom > page_h)
                bottom = page_h;
        }
    } else {
        if (top < 0 || top > page_h)
            top = 0;
        if (bottom < 0 || bottom > page_h)
            bottom = page_h;
    }
    if (stop && stop(stop_data))
        return Aborted;

    DriverJob job;
    memset(&job, 0, sizeof(job));
    GpStatus status = Ok;
    if (bottom > top) {
        if (shrink) {
            UINT32 *averaged = nullptr;
            INT averaged_w = 0;
            status = area_scale(b, attrs, new_w, new_h, top, bottom, &averaged, &averaged_w);
            if (status != Ok)
                return status;
            job.rows.owned = averaged;
            job.rows.px = averaged;
            job.rows.w = averaged_w;
            job.rows.h = bottom - top;
            job.rows.stride = averaged_w;
        } else {
            status = rows_read(g, b, attrs, top, bottom, &job.rows);
            if (status != Ok)
                return status;
        }
    }
    job.rect[0] = lx;
    job.rect[1] = ly - (float)top;
    job.rect[2] = lw;
    job.rect[3] = lh;
    memcpy(job.pts, pts, sizeof(pts));
    job.to_device = world;
    job.wrap = wrap;
    job.clamp = clamp_color;
    job.mode = mode;
    job.half = half;

    if (turned && job.rows.h > 0) {
        /* the rows are turned so that the draw becomes a translation, which the sampler copies */
        INT corner[2];
        ImgMat lin = drv;
        status = rows_turn(&job.rows, &lin, corner);
        if (status != Ok) {
            rows_free(&job.rows);
            return status;
        }
        /*
         * the pixels went to k' = k * linear + corner, while the edges of the rectangle go to the ones of the turned
         * picture, which is one pixel further for each axis that mirrors.
         */
        float edge[2] = {(float)corner[0] + (lin.m11 < 0.0f) + (lin.m21 < 0.0f),
                         (float)corner[1] + (lin.m12 < 0.0f) + (lin.m22 < 0.0f)};
        ImgMat to_page;
        mat_from_points(&to_page, pts, job.rect);
        float x0 = job.rect[0], y0 = job.rect[1], x1 = x0 + job.rect[2], y1 = y0 + job.rect[3];
        float c0x = lin.m11 * x0 + lin.m21 * y0 + edge[0], c0y = lin.m12 * x0 + lin.m22 * y0 + edge[1];
        float c1x = lin.m11 * x1 + lin.m21 * y1 + edge[0], c1y = lin.m12 * x1 + lin.m22 * y1 + edge[1];
        float nx = c0x < c1x ? c0x : c1x, ny = c0y < c1y ? c0y : c1y;
        float nw = mag(c1x - c0x), nh = mag(c1y - c0y);
        float corners[6] = {nx, ny, nx + nw, ny, nx, ny + nh};
        for (INT i = 0; i < 3; i++) {
            /* back into the rows before the turn, the transpose of the linear part, then to the page */
            float ux = corners[2 * i] - edge[0], uy = corners[2 * i + 1] - edge[1];
            float p[2] = {ux * lin.m11 + uy * lin.m12, ux * lin.m21 + uy * lin.m22};
            mat_apply(&to_page, p, 1);
            job.pts[2 * i] = p[0];
            job.pts[2 * i + 1] = p[1];
        }
        job.rect[0] = nx;
        job.rect[1] = ny;
        job.rect[2] = nw;
        job.rect[3] = nh;
        if (mag(lin.m11) < 0.5f) {
            if (job.wrap == 1)
                job.wrap = 2;
            else if (job.wrap == 2)
                job.wrap = 1;
        }
    }
    status = driver_draw(g, &job);
    rows_free(&job.rows);
    return status;
}

/* ------------------------------------------------------------------ the calls */

/* a source rectangle turned the right way round: a negative size starts at the far end. */
static void rect_upright(float r[4])
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

/* draws the rectangle of the image on a parallelogram of three points of the page. */
static GpStatus draw_corners(GGraphics *g, GImage *image, const GpPointF dst[3], const float src[4], GpUnit unit,
                             struct GAttrs *attrs, DrawImageAbort stop, void *stop_data)
{
    if (g->meta)
        return gp_rec_draw_image(g, image, dst, src[0], src[1], src[2], src[3], unit, attrs);
    GBitmap *b = gp_bitmap_cast(image);
    if (!b)
        return gp_metafile_draw(g, image, dst, src[0], src[1], src[2], src[3], unit, attrs);
    float pts[6] = {dst[0].X, dst[0].Y, dst[1].X, dst[1].Y, dst[2].X, dst[2].Y};
    ImgMat m;
    mat_identity(&m);
    mat_from_points(&m, pts, src);
    float rect[4] = {src[0], src[1], src[2], src[3]};
    rect_upright(rect);
    return draw_matrix(g, b, rect, &m, unit, attrs, stop, stop_data);
}

/* draws the rectangle of the image on a rectangle of the page. A negative size on either side mirrors. */
static GpStatus draw_rectangle(GGraphics *g, GImage *image, const float dest[4], const float src[4], GpUnit unit,
                               struct GAttrs *attrs, DrawImageAbort stop, void *stop_data)
{
    if (g->meta || !gp_bitmap_cast(image)) {
        GpPointF corners[3] = {{dest[0], dest[1]}, {dest[0] + dest[2], dest[1]}, {dest[0], dest[1] + dest[3]}};
        if (g->meta)
            return gp_rec_draw_image(g, image, corners, src[0], src[1], src[2], src[3], unit, attrs);
        return gp_metafile_draw(g, image, corners, src[0], src[1], src[2], src[3], unit, attrs);
    }
    GBitmap *b = gp_bitmap_cast(image);
    float dest_right = dest[0] + dest[2], dest_bottom = dest[1] + dest[3];
    float src_right = src[2] + src[0], src_bottom = src[3] + src[1];
    ImgMat m;
    mat_identity(&m);
    if (src[0] != src_right && src[1] != src_bottom) {
        float sx = (dest_right - dest[0]) / (src_right - src[0]);
        float sy = (dest_bottom - dest[1]) / (src_bottom - src[1]);
        mat_set(&m, sx, 0.0f, 0.0f, sy, dest_right - sx * src_right, dest_bottom - sy * src_bottom);
    }
    float rect[4] = {src[0], src[1], src[2], src[3]};
    if (src[2] < 0.0f) {
        rect[0] = src_right;
        rect[2] = -src[2];
    }
    if (src[3] < 0.0f) {
        rect[1] = src_bottom;
        rect[3] = -src[3];
    }
    return draw_matrix(g, b, rect, &m, unit, attrs, stop, stop_data);
}

GpStatus gp_draw_image(GGraphics *g, GImage *image, const GpPointF *dst_points, INT dst_count, float sx, float sy,
                       float sw, float sh, GpUnit src_unit, struct GAttrs *attrs)
{
    if (dst_count != 3)
        return InvalidParameter;
    float src[4] = {sx, sy, sw, sh};
    return draw_corners(g, image, dst_points, src, src_unit, attrs, nullptr, nullptr);
}

/* the size a rectangle of w by h in a unit has on the page of the graphics. */
static float unit_multiplier(GpUnit unit, float dpi)
{
    double d = 1.0 * (double)dpi;
    switch (unit) {
    case UnitPoint:
        return (float)(d / 72.0);
    case UnitInch:
        return (float)(d / 1.0);
    case UnitDocument:
        return (float)(d / 300.0);
    case UnitMillimeter:
        return (float)(d / 25.4);
    default:
        return (float)(d / 100.0);
    }
}

static void page_size(const GGraphics *g, const GBitmap *b, float w, float h, GpUnit unit, float *out_w, float *out_h)
{
    float sx, sy;
    gp_graphics_page_scale(g, &sx, &sy);
    if (unit == UnitPixel) {
        *out_w = (g->dpi_x * w) / (b->dpi_x * sx);
        *out_h = (g->dpi_y * h) / (b->dpi_y * sy);
        return;
    }
    *out_w = (unit_multiplier(unit, g->dpi_x) * w) / sx;
    *out_h = (unit_multiplier(unit, g->dpi_y) * h) / sy;
}

/* the whole picture as a source: in pixels for a bitmap, in its own unit for a metafile. */
static GpStatus whole_source(GImage *image, float src[4], GpUnit *unit)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (b) {
        src[0] = src[1] = 0.0f;
        src[2] = (float)b->width;
        src[3] = (float)b->height;
        *unit = UnitPixel;
        return Ok;
    }
    GpRectF bounds;
    GpStatus st = gp_metafile_bounds(image, &bounds, unit);
    if (st != Ok)
        return st;
    src[0] = bounds.X;
    src[1] = bounds.Y;
    src[2] = bounds.Width;
    src[3] = bounds.Height;
    return Ok;
}

/* the size on the page of a rectangle of the image measured in a unit. */
static GpStatus size_on_page(GGraphics *g, GImage *image, float w, float h, GpUnit unit, float *out_w, float *out_h)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (b) {
        page_size(g, b, w, h, unit, out_w, out_h);
        return Ok;
    }
    return gp_metafile_page_size(g, image, w, h, unit, out_w, out_h);
}

/* the checks the calls share, in the order of the library: the graphics, its busy state, the image. */
static GpStatus check_call(const void *graphics, const void *image, GGraphics **g, GImage **img)
{
    *g = gp_graphics_cast(graphics);
    if (!*g)
        return InvalidParameter;
    if ((*g)->busy || (*g)->got_dc)
        return ObjectBusy;
    *img = gp_image_cast(image);
    return *img ? Ok : InvalidParameter;
}

static BOOL physical_or_pixel(GpUnit unit) { return (UINT)unit - UnitPixel <= 4; }

WINGDIPAPI GpStatus WINAPI GdipDrawImage(GpGraphics *graphics, GpImage *image, REAL x, REAL y)
{
    GGraphics *g;
    GImage *img;
    GpStatus st = check_call(graphics, image, &g, &img);
    if (st != Ok)
        return st;
    float src[4];
    GpUnit unit;
    st = whole_source(img, src, &unit);
    if (st != Ok)
        return st;
    float dest[4] = {x, y, 0.0f, 0.0f};
    st = size_on_page(g, img, src[2], src[3], unit, &dest[2], &dest[3]);
    if (st != Ok)
        return st;
    return draw_rectangle(g, img, dest, src, unit, nullptr, nullptr, nullptr);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageI(GpGraphics *graphics, GpImage *image, INT x, INT y)
{
    return GdipDrawImage(graphics, image, (float)x, (float)y);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRect(GpGraphics *graphics, GpImage *image, REAL x, REAL y, REAL width,
                                             REAL height)
{
    GGraphics *g;
    GImage *img;
    GpStatus st = check_call(graphics, image, &g, &img);
    if (st != Ok)
        return st;
    float src[4];
    GpUnit unit;
    st = whole_source(img, src, &unit);
    if (st != Ok)
        return st;
    float dest[4] = {x, y, width, height};
    return draw_rectangle(g, img, dest, src, unit, nullptr, nullptr, nullptr);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT width, INT height)
{
    return GdipDrawImageRect(graphics, image, (float)x, (float)y, (float)width, (float)height);
}

/* the points calls share the count rule: four points are a perspective the library does not do. */
static GpStatus check_points(const void *points, INT count)
{
    if (!points || count <= 0)
        return InvalidParameter;
    return Ok;
}

static GpStatus points_count(INT count)
{
    if (count == 4)
        return NotImplemented;
    return count == 3 ? Ok : InvalidParameter;
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePoints(GpGraphics *graphics, GpImage *image, const GpPointF *dst_points,
                                               INT count)
{
    GpStatus st = check_points(dst_points, count);
    if (st != Ok)
        return st;
    GGraphics *g;
    GImage *img;
    st = check_call(graphics, image, &g, &img);
    if (st != Ok)
        return st;
    st = points_count(count);
    if (st != Ok)
        return st;
    float src[4];
    GpUnit unit;
    st = whole_source(img, src, &unit);
    if (st != Ok)
        return st;
    return draw_corners(g, img, dst_points, src, unit, nullptr, nullptr, nullptr);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsI(GpGraphics *graphics, GpImage *image, const GpPoint *dst_points,
                                                INT count)
{
    GpStatus st = check_points(dst_points, count);
    if (st != Ok)
        return st;
    if (count != 3)
        return count == 4 ? NotImplemented : InvalidParameter;
    GpPointF points[3];
    for (INT i = 0; i < 3; i++) {
        points[i].X = (float)dst_points[i].X;
        points[i].Y = (float)dst_points[i].Y;
    }
    return GdipDrawImagePoints(graphics, image, points, 3);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointRect(GpGraphics *graphics, GpImage *image, REAL x, REAL y, REAL src_x,
                                                  REAL src_y, REAL src_width, REAL src_height, GpUnit src_unit)
{
    GGraphics *g;
    GImage *img;
    GpStatus st = check_call(graphics, image, &g, &img);
    if (st != Ok)
        return st;
    if (!physical_or_pixel(src_unit))
        return InvalidParameter;
    float src[4] = {src_x, src_y, src_width, src_height};
    float dest[4] = {x, y, 0.0f, 0.0f};
    st = size_on_page(g, img, src_width, src_height, src_unit, &dest[2], &dest[3]);
    if (st != Ok)
        return st;
    return draw_rectangle(g, img, dest, src, src_unit, nullptr, nullptr, nullptr);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT src_x,
                                                   INT src_y, INT src_width, INT src_height, GpUnit src_unit)
{
    return GdipDrawImagePointRect(graphics, image, (float)x, (float)y, (float)src_x, (float)src_y, (float)src_width,
                                  (float)src_height, src_unit);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRectRect(GpGraphics *graphics, GpImage *image, REAL dst_x, REAL dst_y,
                                                 REAL dst_width, REAL dst_height, REAL src_x, REAL src_y,
                                                 REAL src_width, REAL src_height, GpUnit src_unit,
                                                 const GpImageAttributes *attrs, DrawImageAbort callback,
                                                 VOID *callback_data)
{
    GGraphics *g;
    GImage *img;
    GpStatus st = check_call(graphics, image, &g, &img);
    if (st != Ok)
        return st;
    if (!physical_or_pixel(src_unit))
        return InvalidParameter;
    struct GAttrs *a = nullptr;
    if (attrs) {
        a = gp_attrs_cast(attrs);
        if (!a)
            return InvalidParameter;
    }
    float dest[4] = {dst_x, dst_y, dst_width, dst_height}, src[4] = {src_x, src_y, src_width, src_height};
    return draw_rectangle(g, img, dest, src, src_unit, a, callback, callback_data);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageRectRectI(GpGraphics *graphics, GpImage *image, INT dst_x, INT dst_y,
                                                  INT dst_width, INT dst_height, INT src_x, INT src_y, INT src_width,
                                                  INT src_height, GpUnit src_unit, const GpImageAttributes *attrs,
                                                  DrawImageAbort callback, VOID *callback_data)
{
    return GdipDrawImageRectRect(graphics, image, (float)dst_x, (float)dst_y, (float)dst_width, (float)dst_height,
                                 (float)src_x, (float)src_y, (float)src_width, (float)src_height, src_unit, attrs,
                                 callback, callback_data);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsRect(GpGraphics *graphics, GpImage *image, const GpPointF *dst_points,
                                                   INT count, REAL src_x, REAL src_y, REAL src_width,
                                                   REAL src_height, GpUnit src_unit, const GpImageAttributes *attrs,
                                                   DrawImageAbort callback, VOID *callback_data)
{
    GpStatus st = check_points(dst_points, count);
    if (st != Ok)
        return st;
    GGraphics *g;
    GImage *img;
    st = check_call(graphics, image, &g, &img);
    if (st != Ok)
        return st;
    st = points_count(count);
    if (st != Ok)
        return st;
    if (!physical_or_pixel(src_unit))
        return InvalidParameter;
    struct GAttrs *a = nullptr;
    if (attrs) {
        a = gp_attrs_cast(attrs);
        if (!a)
            return InvalidParameter;
    }
    float src[4] = {src_x, src_y, src_width, src_height};
    return draw_corners(g, img, dst_points, src, src_unit, a, callback, callback_data);
}

WINGDIPAPI GpStatus WINAPI GdipDrawImagePointsRectI(GpGraphics *graphics, GpImage *image, const GpPoint *dst_points,
                                                    INT count, INT src_x, INT src_y, INT src_width, INT src_height,
                                                    GpUnit src_unit, const GpImageAttributes *attrs,
                                                    DrawImageAbort callback, VOID *callback_data)
{
    GpStatus st = check_points(dst_points, count);
    if (st != Ok)
        return st;
    if (count != 3)
        return count == 4 ? NotImplemented : InvalidParameter;
    GpPointF points[3];
    for (INT i = 0; i < 3; i++) {
        points[i].X = (float)dst_points[i].X;
        points[i].Y = (float)dst_points[i].Y;
    }
    return GdipDrawImagePointsRect(graphics, image, points, 3, (float)src_x, (float)src_y, (float)src_width,
                                   (float)src_height, src_unit, attrs, callback, callback_data);
}
