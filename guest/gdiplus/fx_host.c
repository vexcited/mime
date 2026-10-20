/*
 * the bitmap host of the effects: working out which pixels an effect is run for, reading them as straight argb,
 * running the effect and writing the result back or into a new bitmap. the histograms and the drawing of an image
 * through an effect are here too.
 */
#include "fx_core.h"

/* ------------------------------------------------------------------ rectangles */

static BOOL rect_is_empty(const RECT *r) { return r->right <= r->left || r->bottom <= r->top; }

static void rect_clip(RECT *r, const RECT *to)
{
    r->left = fx_max(r->left, to->left);
    r->top = fx_max(r->top, to->top);
    r->right = fx_min(r->right, to->right);
    r->bottom = fx_min(r->bottom, to->bottom);
}

/* ------------------------------------------------------------------ the run */

typedef struct Outcome {
    GBitmap *bitmap;   /* a new bitmap, for the runs that make one */
    RECT area;
} Outcome;

/* the straight argb pixels of a rectangle of the bitmap, which lies inside it. */
static UINT32 *read_pixels(const GBitmap *b, const RECT *r)
{
    SIZE_T width = (SIZE_T)(r->right - r->left), height = (SIZE_T)(r->bottom - r->top);
    UINT32 *pixels = gp_alloc(width * height * 4);
    if (!pixels)
        return nullptr;
    for (SIZE_T y = 0; y < height; y++)
        gp_bitmap_read_argb(b, r->left, r->top + (INT)y, (INT)width, pixels + y * width);
    return pixels;
}

static void write_pixels(GBitmap *b, INT x, INT y, INT width, INT height, const UINT32 *pixels)
{
    for (INT row = 0; row < height; row++)
        gp_bitmap_write_argb(b, x, y + row, width, pixels + (SIZE_T)row * width);
}

/*
 * runs the effect for the bitmap. with in_place the result goes into the bitmap, without it a new bitmap is made
 * that covers the area, which may reach beyond the input when the effect grows its bounds. an area with nothing in
 * it is not an error, there is no result then.
 */
static GpStatus run_effect(GBitmap *in, FxEffect *e, const RECT *region, BOOL in_place, Outcome *out)
{
    if (in->locked)
        return GenericError;
    const FxClass *cls = e->cls;
    void *state = FX_STATE(e);
    INT margin = cls->margin ? cls->margin(state) : 0;
    RECT input = {0, 0, in->width, in->height};
    RECT area = {-margin, -margin, in->width + margin, in->height + margin};
    if (region)
        rect_clip(&area, region);
    if (in_place)
        rect_clip(&area, &input);
    if (rect_is_empty(&area)) {
        memset(&out->area, 0, sizeof(out->area));
        return Ok;
    }
    out->area = area;

    /* an effect that looks at neighbors gets the whole input, the others only the pixels they make. */
    RECT wanted = input;
    if (!(cls->flags & FX_NEIGHBORS))
        rect_clip(&wanted, &area);
    UINT32 *src = read_pixels(in, &wanted);
    INT aw = area.right - area.left, ah = area.bottom - area.top;
    UINT32 *dst = gp_alloc((SIZE_T)aw * (SIZE_T)ah * 4);
    if (!src || !dst) {
        gp_free(src);
        gp_free(dst);
        return OutOfMemory;
    }
    FxJob job = {src, wanted.left, wanted.top, wanted.right - wanted.left, wanted.bottom - wanted.top, area, dst, in_place};
    GpStatus st = cls->run(state, &job);
    if (st == OutOfMemory) {
        gp_free(src);
        gp_free(dst);
        return st;
    }
    /* what an effect did before it refused the rest of its work stays. */
    if (in_place) {
        write_pixels(in, area.left, area.top, aw, ah, dst);
    } else if (st == Ok) {
        PixelFormat format = (cls->flags & FX_KEEPS_24) && in->format == PixelFormat24bppRGB ? PixelFormat24bppRGB
                                                                                              : PixelFormat32bppARGB;
        GBitmap *made;
        GpStatus made_st = gp_bitmap_create(aw, ah, format, &made);
        if (made_st != Ok) {
            st = made_st;
        } else {
            made->dpi_x = in->dpi_x;
            made->dpi_y = in->dpi_y;
            write_pixels(made, 0, 0, aw, ah, dst);
            out->bitmap = made;
        }
    }
    gp_free(src);
    gp_free(dst);
    return st;
}

/* the tables of an effect that has them, for the program to keep. */
static GpStatus give_tables(FxEffect *e, BOOL want, void **aux, INT *aux_size)
{
    if (!want || !aux || !aux_size || !e->cls->tables)
        return Ok;
    const BYTE *tables = e->cls->tables(FX_STATE(e));
    void *copy = gp_dup(tables, 1024);
    if (!copy)
        return OutOfMemory;
    *aux = copy;
    *aux_size = 1024;
    return Ok;
}

GpStatus fx_apply_in_place(GBitmap *bitmap, FxEffect *effect, const RECT *region)
{
    Outcome out = {nullptr};
    return run_effect(bitmap, effect, region, TRUE, &out);
}

WINGDIPAPI GpStatus WINAPI GdipBitmapApplyEffect(GpBitmap *bitmap, CGpEffect *effect, RECT *roi, BOOL use_aux,
                                                 VOID **aux, INT *aux_size)
{
    GBitmap *b = gp_bitmap_cast(bitmap);
    FxEffect *e = fx_effect_cast(effect);
    if (!b || !e)
        return InvalidParameter;
    Outcome out = {nullptr};
    GpStatus st = run_effect(b, e, roi, TRUE, &out);
    if (st != Ok)
        return st;
    /* with nothing to work on the program gets nothing. */
    if (rect_is_empty(&out.area))
        return Ok;
    return give_tables(e, use_aux, aux, aux_size);
}

WINGDIPAPI GpStatus WINAPI GdipBitmapCreateApplyEffect(GpBitmap **inputs, INT count, CGpEffect *effect, RECT *roi,
                                                       RECT *out_rect, GpBitmap **output, BOOL use_aux, VOID **aux,
                                                       INT *aux_size)
{
    if (!output || !inputs || count != 1)
        return InvalidParameter;
    GBitmap *b = gp_bitmap_cast(inputs[0]);
    FxEffect *e = fx_effect_cast(effect);
    if (!b || !e)
        return InvalidParameter;
    Outcome out = {nullptr};
    GpStatus st = run_effect(b, e, roi, FALSE, &out);
    if (st != Ok)
        return st;
    if (rect_is_empty(&out.area)) {
        *output = nullptr;
        if (out_rect)
            memset(out_rect, 0, sizeof(*out_rect));
        return Ok;
    }
    st = give_tables(e, use_aux, aux, aux_size);
    if (st != Ok) {
        gp_bitmap_free(out.bitmap);
        return st;
    }
    *output = (GpBitmap *)out.bitmap;
    if (out_rect)
        *out_rect = out.area;
    return Ok;
}

/* ------------------------------------------------------------------ histograms */

WINGDIPAPI GpStatus WINAPI GdipBitmapGetHistogramSize(HistogramFormat format, UINT *entries)
{
    if (!entries)
        return InvalidParameter;
    *entries = 256;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipBitmapGetHistogram(GpBitmap *bitmap, HistogramFormat format, UINT entries, UINT *ch0,
                                                  UINT *ch1, UINT *ch2, UINT *ch3)
{
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!b || entries != 256 || (UINT)format > HistogramFormatA)
        return InvalidParameter;
    /* the channels the format counts, in the order of the arrays: alpha red green blue, red green blue, or one. */
    UINT *arrays[4] = {ch0, ch1, ch2, ch3};
    UINT used = format == HistogramFormatARGB || format == HistogramFormatPARGB ? 4 : format == HistogramFormatRGB ? 3 : 1;
    for (UINT i = 0; i < 4; i++)
        if ((i < used) != (arrays[i] != nullptr))
            return InvalidParameter;
    for (UINT i = 0; i < used; i++)
        memset(arrays[i], 0, 256 * sizeof(UINT));
    UINT32 *row = gp_alloc((SIZE_T)b->width * 4);
    if (!row)
        return OutOfMemory;
    for (INT y = 0; y < b->height; y++) {
        gp_bitmap_read_argb(b, 0, y, b->width, row);
        for (INT x = 0; x < b->width; x++) {
            UINT32 p = format == HistogramFormatPARGB ? gp_premultiply(row[x]) : row[x];
            UINT a = p >> 24, r = (p >> 16) & 255, g = (p >> 8) & 255, bl = p & 255;
            switch (format) {
            case HistogramFormatARGB:
            case HistogramFormatPARGB:
                ch0[a]++;
                ch1[r]++;
                ch2[g]++;
                ch3[bl]++;
                break;
            case HistogramFormatRGB:
                ch0[r]++;
                ch1[g]++;
                ch2[bl]++;
                break;
            case HistogramFormatGray: {
                float luma = ((float)g * 0.71516f + (float)r * 0.212671f) + (float)bl * 0.072169f + 0.5f;
                ch0[fx_floor_int(luma) & 255]++;
                break;
            }
            case HistogramFormatB:
                ch0[bl]++;
                break;
            case HistogramFormatG:
                ch0[g]++;
                break;
            case HistogramFormatR:
                ch0[r]++;
                break;
            default:
                ch0[a]++;
                break;
            }
        }
    }
    gp_free(row);
    return Ok;
}

/* ------------------------------------------------------------------ drawing through an effect */

/*
 * the pixels that hold a rectangle, and one more column and row. the numbers must be in the range the pixels of
 * an image can be, else nothing is held.
 */
static RECT pixels_holding(float x, float y, float w, float h)
{
    /* 2 to the 30th, which no pixel of an image gets near. */
    const float limit = 1073741824.0f;
    RECT none = {0, 0, 0, 0};
    if (!(x >= -limit && x <= limit && y >= -limit && y <= limit && w >= 0.0f && w <= limit && h >= 0.0f && h <= limit))
        return none;
    INT left = fx_floor_int(x), top = fx_floor_int(y);
    INT right = -fx_floor_int(-(w + x)), bottom = -fx_floor_int(-(h + y));
    RECT r = {left, top, right + 1, bottom + 1};
    return r;
}

WINGDIPAPI GpStatus WINAPI GdipDrawImageFX(GpGraphics *graphics, GpImage *image, GpRectF *source, GpMatrix *xform,
                                           CGpEffect *effect, GpImageAttributes *attrs, GpUnit unit)
{
    GGraphics *g = gp_graphics_cast(graphics);
    GImage *img = gp_image_cast(image);
    if (!g || !img)
        return InvalidParameter;
    if (unit != UnitPixel && unit != UnitPoint && unit != UnitInch && unit != UnitDocument && unit != UnitMillimeter)
        return InvalidParameter;
    FxEffect *e = nullptr;
    if (effect && !(e = fx_effect_cast(effect)))
        return InvalidParameter;
    GMatrix *m = nullptr;
    if (xform && !(m = gp_matrix_cast(xform)))
        return InvalidParameter;
    struct GAttrs *a = nullptr;
    if (attrs && !(a = gp_attrs_cast(attrs)))
        return InvalidParameter;
    if (g->busy)
        return ObjectBusy;

    /* an effect works on pixels, a metafile is turned into some first. */
    GBitmap *bitmap = gp_bitmap_cast(img);
    GBitmap *made = nullptr;
    if (e && !bitmap) {
        GpStatus st = gp_metafile_to_bitmap(img, &made);
        if (st != Ok)
            return st;
        bitmap = made;
    }
    float dpi_x = 96.0f, dpi_y = 96.0f;
    INT width = 0, height = 0;
    if (bitmap) {
        dpi_x = bitmap->dpi_x;
        dpi_y = bitmap->dpi_y;
        width = bitmap->width;
        height = bitmap->height;
    } else {
        UINT w, h;
        gp_metafile_resolution((GpImage *)img, &dpi_x, &dpi_y);
        if (gp_metafile_pixel_size((GpImage *)img, &w, &h) == Ok) {
            width = (INT)w;
            height = (INT)h;
        }
    }
    float fx = gp_unit_to_pixels(unit, dpi_x), fy = gp_unit_to_pixels(unit, dpi_y);

    /* the rectangle in pixels, as given: a negative size runs the other way and mirrors the picture. */
    float rx = 0.0f, ry = 0.0f, rw = (float)width, rh = (float)height;
    if (source) {
        rx = source->X * fx;
        ry = source->Y * fy;
        rw = source->Width * fx;
        rh = source->Height * fy;
    }
    float sx = rw < 0.0f ? rx + rw : rx, sy = rh < 0.0f ? ry + rh : ry;
    float sw = rw < 0.0f ? -rw : rw, sh = rh < 0.0f ? -rh : rh;
    /* the corners the picture goes to, in the units the rectangle is given in, moved by the matrix. */
    GpPointF corners[3] = {{rx / fx, ry / fy}, {(rx + rw) / fx, ry / fy}, {rx / fx, (ry + rh) / fy}};
    if (m) {
        for (INT i = 0; i < 3; i++) {
            float ox, oy;
            gp_matrix_point(m->M, corners[i].X, corners[i].Y, &ox, &oy);
            corners[i].X = ox;
            corners[i].Y = oy;
        }
    }

    GpStatus st;
    if (!e) {
        st = gp_draw_image(g, img, corners, 3, sx, sy, sw, sh, UnitPixel, a);
        return st;
    }
    /* the effect is run on a copy of the pixels that hold the rectangle, and the copy is drawn. */
    RECT hold = pixels_holding(sx, sy, sw, sh);
    RECT whole = {0, 0, width, height};
    rect_clip(&hold, &whole);
    if (rect_is_empty(&hold)) {
        gp_bitmap_free(made);
        return Ok;
    }
    GBitmap *copy;
    st = gp_bitmap_create(hold.right - hold.left, hold.bottom - hold.top, PixelFormat32bppARGB, &copy);
    if (st == Ok) {
        UINT32 *pixels = read_pixels(bitmap, &hold);
        if (!pixels) {
            st = OutOfMemory;
        } else {
            write_pixels(copy, 0, 0, hold.right - hold.left, hold.bottom - hold.top, pixels);
            gp_free(pixels);
            copy->dpi_x = bitmap->dpi_x;
            copy->dpi_y = bitmap->dpi_y;
            st = fx_apply_in_place(copy, e, nullptr);
            if (st == Ok)
                st = gp_draw_image(g, (GImage *)copy, corners, 3, sx - (float)hold.left, sy - (float)hold.top, sw, sh,
                                   UnitPixel, a);
        }
        gp_bitmap_free(copy);
    }
    gp_bitmap_free(made);
    return st;
}
