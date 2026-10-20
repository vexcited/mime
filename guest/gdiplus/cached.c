/*
 * cached bitmaps, the abort and validation calls on images, and the test control hook.
 */
#include "gp_private.h"

typedef struct GCached {
    DWORD tag;
    GBitmap *copy;  /* premultiplied */
} GCached;

typedef struct CachedSource {
    GSource base;
    const GBitmap *bits;
    INT ox, oy;     /* device position of the first pixel */
} CachedSource;

static void cached_fetch(GSource *src, INT x, INT y, INT count, UINT32 *out)
{
    CachedSource *s = (CachedSource *)src;
    INT px = x - s->ox, py = y - s->oy;
    if (py < 0 || py >= s->bits->height) {
        memset(out, 0, sizeof(UINT32) * count);
        return;
    }
    const UINT32 *row = (const UINT32 *)(s->bits->bits + (SSIZE_T)py * s->bits->stride);
    for (INT i = 0; i < count; i++) {
        INT u = px + i;
        out[i] = u >= 0 && u < s->bits->width ? row[u] : 0;
    }
}

static void cached_destroy(GSource *src) { gp_free(src); }

WINGDIPAPI GpStatus WINAPI GdipCreateCachedBitmap(GpBitmap *bitmap, GpGraphics *graphics, GpCachedBitmap **cached)
{
    GP_REQUIRE_INIT();
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!cached || !gp_graphics_cast(graphics) || !b)
        return InvalidParameter;
    GCached *c = gp_alloc_zero(sizeof(*c));
    if (!c)
        return OutOfMemory;
    GpStatus st = gp_bitmap_create(b->width, b->height, PixelFormat32bppPARGB, &c->copy);
    if (st != Ok) {
        gp_free(c);
        return st;
    }
    for (INT y = 0; y < b->height; y++) {
        UINT32 *row = (UINT32 *)(c->copy->bits + (SSIZE_T)y * c->copy->stride);
        gp_bitmap_read_argb(b, 0, y, b->width, row);
        for (INT x = 0; x < b->width; x++)
            row[x] = gp_premultiply(row[x]);
    }
    c->tag = GP_TAG_CACHED;
    *cached = c;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDeleteCachedBitmap(GpCachedBitmap *cached)
{
    GCached *c = GP_CAST(GCached, GP_TAG_CACHED, cached);
    if (!c)
        return InvalidParameter;
    c->tag = 0;
    gp_bitmap_free(c->copy);
    gp_free(c);
    return Ok;
}

/* the bitmap goes to the device position given, whatever the transforms of the graphics are. */
WINGDIPAPI GpStatus WINAPI GdipDrawCachedBitmap(GpGraphics *graphics, GpCachedBitmap *cached, INT x, INT y)
{
    GGraphics *g = gp_graphics_cast(graphics);
    GCached *c = GP_CAST(GCached, GP_TAG_CACHED, cached);
    if (!g || !c)
        return InvalidParameter;
    if (g->busy)
        return ObjectBusy;
    if (g->meta)
        return WrongState;
    CachedSource *s = gp_alloc_zero(sizeof(*s));
    if (!s)
        return OutOfMemory;
    s->bits = c->copy;
    s->ox = x;
    s->oy = y;
    s->base.fetch = cached_fetch;
    s->base.destroy = cached_destroy;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    mime_vec_polys_begin(&polys);
    float right = (float)x + (float)c->copy->width, bottom = (float)y + (float)c->copy->height;
    mime_vec_polys_add(&polys, (float)x, (float)y);
    mime_vec_polys_add(&polys, right, (float)y);
    mime_vec_polys_add(&polys, right, bottom);
    mime_vec_polys_add(&polys, (float)x, bottom);
    mime_vec_polys_end(&polys, true);
    RECT area = {x, y, x + c->copy->width, y + c->copy->height};
    GpStatus st = gp_fill_polys_src(g, &polys, MIME_VEC_FILL_WINDING, 0, &s->base, &area);
    mime_vec_polys_free(&polys);
    gp_source_free(&s->base);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipImageForceValidation(GpImage *image)
{
    if (!gp_image_cast(image))
        return InvalidParameter;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipImageSetAbort(GpImage *image, GdiplusAbort *abort)
{
    if (!gp_image_cast(image))
        return InvalidParameter;
    return Ok;
}

/* the switches of the library that programs and tests flip: forced bilinear drawing and color management. */
BOOL gp_force_bilinear;
BOOL gp_no_icm;

WINGDIPAPI GpStatus WINAPI GdipTestControl(GpTestControlEnum control, VOID *param)
{
    switch (control) {
    case 0:
        gp_force_bilinear = *(INT *)param != 0;
        return Ok;
    case 1:
        gp_no_icm = *(INT *)param != 0;
        return Ok;
    case 2:
        *(INT *)param = 10011;
        return Ok;
    default:
        return InvalidParameter;
    }
}
