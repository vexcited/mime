/*
 * playing metafiles: drawing them onto a graphics, and the enumerate calls that show every record to a
 * program first. the gdi records of a metafile are played by the gdi of the system on a device context for the
 * graphics, the emf+ records by emfplus.c on the graphics itself.
 *
 * the metafile is placed by one matrix: its own pixels to the device. a graphics that draws a metafile gets the
 * matrix as the transform its container puts below the world transform, so the records see the pixels of the
 * metafile as their page.
 */
#include "meta_private.h"

static DWORD rd32(const BYTE *p)
{
    DWORD v;
    memcpy(&v, p, 4);
    return v;
}

static WORD rd16(const BYTE *p)
{
    WORD v;
    memcpy(&v, p, 2);
    return v;
}

static INT round_to_int(float v) { return (INT)mime_vec_floorf(v + 0.5f); }

/* ------------------------------------------------------------------ sizes on a page */

/* the size of something that measures width by height in a unit when it is drawn without scaling. */
GpStatus gp_metafile_page_size(struct GGraphics *gfx, GpImage *image, float width, float height, GpUnit unit,
                               float *out_w, float *out_h)
{
    GGraphics *g = (GGraphics *)gfx;
    GMetafile *m = gp_meta_cast(image);
    if (!m)
        return InvalidParameter;
    float sx, sy;
    gp_graphics_page_scale(g, &sx, &sy);
    if (unit == UnitPixel) {
        *out_w = (g->dpi_x * width) / (m->header.DpiX * sx);
        *out_h = (g->dpi_y * height) / (m->header.DpiY * sy);
        return Ok;
    }
    float px = gp_unit_to_pixels(unit, g->dpi_x), py = gp_unit_to_pixels(unit, g->dpi_y);
    *out_w = px * width / sx;
    *out_h = py * height / sy;
    return Ok;
}

/* ------------------------------------------------------------------ the device context */

/* the clip of the graphics as a region of the context, in device pixels. */
static void dc_apply_clip(HDC dc, GGraphics *g)
{
    HRGN region = gp_meta_clip_region(g);
    if (region) {
        ExtSelectClipRgn(dc, region, RGN_AND);
        DeleteObject(region);
    }
}

static XFORM to_xform(const MimeVecMatrix *m)
{
    XFORM x = {m->m11, m->m12, m->m21, m->m22, m->dx, m->dy};
    return x;
}

/* the context the gdi records play on: the one of the graphics, or a dib of its bitmap. */
static BOOL dc_open(GPlayer *p)
{
    GGraphics *g = p->g;
    if (p->dc)
        return TRUE;
    if (g->hdc && !g->target) {
        p->dc = g->hdc;
        p->dc_bitmap = FALSE;
    } else if (g->target && !g->bitmap_dc) {
        if (gp_graphics_make_bitmap_dc(g, &p->dc) != Ok)
            return FALSE;
        p->dc_bitmap = TRUE;
    } else {
        return FALSE;
    }
    p->dc_saved = SaveDC(p->dc) != 0;
    return TRUE;
}

/*
 * a bitmap and its device context are two copies of the pixels. emf+ records draw in the bitmap and gdi records in
 * the context, so the context is brought up to date before the gdi records and its changes go back after them.
 */
void gp_play_gdi_begin(GPlayer *p)
{
    GGraphics *g = p->g;
    if (!p->dc_bitmap || !g->bitmap_bits)
        return;
    GBitmap *b = g->target;
    for (INT y = 0; y < b->height; y++) {
        UINT32 *row = (UINT32 *)(g->bitmap_bits + (SIZE_T)y * b->width * 4);
        gp_row_to_argb(b->bits + (SSIZE_T)y * b->stride, b->format, b->palette, 0, b->width, row);
        memcpy(g->bitmap_copy + (SSIZE_T)y * b->width, row, (SIZE_T)b->width * 4);
    }
    p->gdi_pending = TRUE;
}

void gp_play_gdi_end(GPlayer *p)
{
    GGraphics *g = p->g;
    if (!p->gdi_pending || !p->dc_bitmap || !g->bitmap_bits)
        return;
    p->gdi_pending = FALSE;
    GdiFlush();
    GBitmap *b = g->target;
    for (INT y = 0; y < b->height; y++) {
        UINT32 *row = (UINT32 *)(g->bitmap_bits + (SIZE_T)y * b->width * 4);
        UINT32 *orig = g->bitmap_copy + (SSIZE_T)y * b->width;
        INT x = 0;
        while (x < b->width) {
            if ((row[x] & 0x00ffffffu) == (orig[x] & 0x00ffffffu)) {
                x++;
                continue;
            }
            /* what gdi drew has no alpha of its own, it is opaque */
            INT start = x;
            while (x < b->width && (row[x] & 0x00ffffffu) != (orig[x] & 0x00ffffffu)) {
                row[x] |= 0xff000000u;
                x++;
            }
            gp_bitmap_write_argb(b, start, y, x - start, row + start);
        }
    }
}

static void dc_close(GPlayer *p)
{
    if (!p->dc)
        return;
    if (p->dc_saved)
        RestoreDC(p->dc, -1);
    if (p->dc_bitmap)
        gp_graphics_release_bitmap_dc(p->g, p->dc, !p->mixed);
    p->dc = nullptr;
    p->dc_saved = FALSE;
    p->dc_bitmap = FALSE;
}

/* ------------------------------------------------------------------ records */

/* one record of an enhanced metafile played, or shown to the program. */
static BOOL gdi_record(GPlayer *p, HDC dc, const ENHMETARECORD *r)
{
    if (p->cb) {
        DWORD size = r->nSize > 8 ? r->nSize - 8 : 0;
        if (!p->cb((EmfPlusRecordType)r->iType, 0, size, size ? (const BYTE *)r->dParm : nullptr, p->cb_data)) {
            p->aborted = TRUE;
            return FALSE;
        }
        return TRUE;
    }
    if (dc)
        PlayEnhMetaFileRecord(dc, p->table, r, (UINT)p->table_count);
    return TRUE;
}

/* the emf+ records of one comment. */
static BOOL plus_comment(GPlayer *p, const ENHMETARECORD *r)
{
    gp_play_gdi_end(p);
    p->gdi_enabled = FALSE;
    if (r->nSize < 16)
        return TRUE;
    const BYTE *q = (const BYTE *)&r->dParm[2];
    DWORD avail = r->nSize - 16;
    DWORD left = r->dParm[0] > 4 ? r->dParm[0] - 4 : 0;
    if (left > avail)
        left = avail;
    while (left >= 12) {
        UINT type = rd16(q), flags = rd16(q + 2);
        DWORD size = rd32(q + 4), dsize = rd32(q + 8);
        if (size < 12 || size > left || dsize > size - 12)
            break;
        if (p->cb) {
            if (!p->cb((EmfPlusRecordType)type, flags, dsize, dsize ? q + 12 : nullptr, p->cb_data)) {
                p->aborted = TRUE;
                return FALSE;
            }
        } else {
            gp_plus_record(p, type, flags, dsize, q + 12);
        }
        q += size;
        left -= size;
    }
    return TRUE;
}

static int CALLBACK enum_proc(HDC dc, HANDLETABLE *table, const ENHMETARECORD *r, int count, LPARAM param)
{
    GPlayer *p = (GPlayer *)param;
    p->table = table;
    p->table_count = count;
    if (p->aborted)
        return 0;
    if (r->iType == EMR_GDICOMMENT && r->nSize >= 16 && r->dParm[1] == EMFPLUS_SIGNATURE)
        return plus_comment(p, r);
    if (p->mf->header.Type >= MetafileTypeEmfPlusOnly && !p->gdi_enabled && r->iType != EMR_HEADER &&
        r->iType != EMR_EOF)
        return 1;
    return gdi_record(p, dc, r);
}

/* a windows metafile record, which a program sees as the function with the high flag. */
static int CALLBACK wmf_proc(HDC dc, HANDLETABLE *table, METARECORD *r, int count, LPARAM param)
{
    GPlayer *p = (GPlayer *)param;
    p->table = table;
    p->table_count = count;
    if (p->aborted)
        return 0;
    if (p->cb) {
        DWORD size = r->rdSize > 3 ? (r->rdSize - 3) * 2 : 0;
        if (!p->cb((EmfPlusRecordType)(r->rdFunction | 0x10000), 0, size, size ? (const BYTE *)r->rdParm : nullptr,
                   p->cb_data)) {
            p->aborted = TRUE;
            return 0;
        }
        return 1;
    }
    PlayMetaFileRecord(dc, table, r, (UINT)count);
    return 1;
}

/* ------------------------------------------------------------------ a gdi playback */

/*
 * the frame goes to a rectangle of whole pixels of the metafile, and the little the rectangle is off from the
 * frame is made up in the transform of the context.
 */
static void frame_setup(GPlayer *p, RECT *rect)
{
    GMetafile *m = p->mf;
    float kx = m->header.DpiX / HIMETRIC_PER_INCH, ky = m->header.DpiY / HIMETRIC_PER_INCH;
    const RECTL *f = &m->header.EmfHeader.rclFrame;
    float x0 = (float)f->left * kx, y0 = (float)f->top * ky;
    float w = (float)(f->right - f->left) * kx, h = (float)(f->bottom - f->top) * ky;
    rect->left = round_to_int(x0);
    rect->top = round_to_int(y0);
    rect->right = round_to_int(x0 + w);
    rect->bottom = round_to_int(y0 + h);
    if (rect->right <= rect->left)
        rect->right = rect->left + 1;
    if (rect->bottom <= rect->top)
        rect->bottom = rect->top + 1;
    float ax = w > 0.0f ? w / (float)(rect->right - rect->left) : 1.0f;
    float ay = h > 0.0f ? h / (float)(rect->bottom - rect->top) : 1.0f;
    MimeVecMatrix fix = {ax, 0, 0, ay, x0 - (float)rect->left * ax, y0 - (float)rect->top * ay};
    MimeVecMatrix total;
    mime_vec_matrix_multiply(&total, &fix, &p->to_device);
    XFORM x = to_xform(&total);
    SetWorldTransform(p->dc, &x);
}


static GpStatus gdi_run(GPlayer *p)
{
    GMetafile *m = p->mf;
    GGraphics *g = p->g;
    if (!dc_open(p))
        return g->got_dc || g->bitmap_dc ? ObjectBusy : GenericError;
    HDC dc = p->dc;
    SetMapMode(dc, MM_TEXT);
    SetWindowOrgEx(dc, 0, 0, nullptr);
    SetViewportOrgEx(dc, 0, 0, nullptr);
    SetGraphicsMode(dc, GM_ADVANCED);
    dc_apply_clip(dc, g);
    BOOL ok;
    if (m->header.Type < MetafileTypeEmf) {
        /* the window is the box of the picture, one unit a pixel of the metafile */
        XFORM x = to_xform(&p->to_device);
        SetWorldTransform(dc, &x);
        SetMapMode(dc, MM_ANISOTROPIC);
        SetWindowOrgEx(dc, m->header.X, m->header.Y, nullptr);
        SetWindowExtEx(dc, m->header.Width, m->header.Height, nullptr);
        SetViewportOrgEx(dc, m->header.X, m->header.Y, nullptr);
        SetViewportExtEx(dc, m->header.Width, m->header.Height, nullptr);
        ok = EnumMetaFile(dc, (HMETAFILE)m->handle, wmf_proc, (LPARAM)p);
    } else {
        RECT rect;
        frame_setup(p, &rect);
        ok = EnumEnhMetaFile(dc, (HENHMETAFILE)m->handle, enum_proc, p, &rect);
    }
    GdiFlush();
    dc_close(p);
    if (p->aborted)
        return Aborted;
    return ok ? Ok : GenericError;
}

/* ------------------------------------------------------------------ an emf+ playback */

static BOOL has_get_dc(GMetafile *m)
{
    DWORD size;
    const BYTE *b = gp_meta_bytes(m, &size);
    if (!b)
        return FALSE;
    DWORD pos = rd32(b + 4);
    while (pos + 8 <= size) {
        DWORD type = rd32(b + pos), n = rd32(b + pos + 4);
        if (n < 8 || n > size - pos)
            break;
        if (type == EMR_GDICOMMENT && n >= 16 && rd32(b + pos + 12) == EMFPLUS_SIGNATURE) {
            DWORD left = rd32(b + pos + 8);
            if (left > n - 12)
                left = n - 12;
            left = left > 4 ? left - 4 : 0;
            const BYTE *q = b + pos + 16;
            while (left >= 12) {
                DWORD rs = rd32(q + 4);
                if (rs < 12 || rs > left)
                    break;
                if (rd16(q) == EmfPlusRecordTypeGetDC)
                    return TRUE;
                q += rs;
                left -= rs;
            }
        }
        if (type == EMR_EOF)
            break;
        pos += n;
    }
    return FALSE;
}

static GpStatus plus_run(GPlayer *p)
{
    GMetafile *m = p->mf;
    gp_plus_player_init(p);
    GpStatus st = Ok;
    BOOL ok;
    if (has_get_dc(m)) {
        if (!dc_open(p))
            return GenericError;
        SetGraphicsMode(p->dc, GM_ADVANCED);
        SetMapMode(p->dc, MM_TEXT);
        SetWindowOrgEx(p->dc, 0, 0, nullptr);
        SetViewportOrgEx(p->dc, 0, 0, nullptr);
        dc_apply_clip(p->dc, p->g);
        RECT rect;
        frame_setup(p, &rect);
        p->mixed = TRUE;
        ok = EnumEnhMetaFile(p->dc, (HENHMETAFILE)m->handle, enum_proc, p, &rect);
        gp_play_gdi_end(p);
        dc_close(p);
    } else {
        ok = EnumEnhMetaFile(nullptr, (HENHMETAFILE)m->handle, enum_proc, p, nullptr);
    }
    if (p->aborted)
        st = Aborted;
    else if (!ok)
        st = GenericError;
    gp_plus_player_unwind(p);
    gp_plus_player_free(p);
    return st;
}

/* ------------------------------------------------------------------ the placement */

static BOOL rotated(const MimeVecMatrix *m)
{
    float a = m->m11 < 0 ? -m->m11 : m->m11, b = m->m22 < 0 ? -m->m22 : m->m22;
    float c = m->m12 < 0 ? -m->m12 : m->m12, d = m->m21 < 0 ? -m->m21 : m->m21;
    float scale = a + b;
    return c > 1.0e-4f * scale || d > 1.0e-4f * scale;
}

/* the matrix that takes a rectangle of metafile pixels onto the parallelogram of three world points. */
static void rect_to_points(const GpPointF *pt, const GpRectF *src, MimeVecMatrix *out)
{
    float ux = (pt[1].X - pt[0].X) / src->Width, uy = (pt[1].Y - pt[0].Y) / src->Width;
    float vx = (pt[2].X - pt[0].X) / src->Height, vy = (pt[2].Y - pt[0].Y) / src->Height;
    out->m11 = ux;
    out->m12 = uy;
    out->m21 = vx;
    out->m22 = vy;
    out->dx = pt[0].X - src->X * ux - src->Y * vx;
    out->dy = pt[0].Y - src->X * uy - src->Y * vy;
}

static GpStatus play_placed(GGraphics *g, GMetafile *m, const GpPointF *pt, const GpRectF *src,
                            EnumerateMetafileProc cb, void *data, struct GAttrs *attrs);

/*
 * a metafile that is turned, sheared or recolored is drawn on a bitmap of its own size first and the bitmap
 * is drawn as an image.
 */
static GpStatus play_raster(GGraphics *g, GMetafile *m, const GpPointF *pt, const GpRectF *src,
                            EnumerateMetafileProc cb, void *data, struct GAttrs *attrs)
{
    const MimeVecMatrix *w2d = gp_graphics_w2d(g);
    float ex = mime_vec_sqrtf(w2d->m11 * w2d->m11 + w2d->m12 * w2d->m12);
    float ey = mime_vec_sqrtf(w2d->m21 * w2d->m21 + w2d->m22 * w2d->m22);
    float lx = mime_vec_sqrtf((pt[1].X - pt[0].X) * (pt[1].X - pt[0].X) + (pt[1].Y - pt[0].Y) * (pt[1].Y - pt[0].Y));
    float ly = mime_vec_sqrtf((pt[2].X - pt[0].X) * (pt[2].X - pt[0].X) + (pt[2].Y - pt[0].Y) * (pt[2].Y - pt[0].Y));
    INT tw = (INT)mime_vec_ceilf(lx * ex), th = (INT)mime_vec_ceilf(ly * ey);
    if (tw < 1)
        tw = 1;
    if (th < 1)
        th = 1;
    if (tw > 4096)
        tw = 4096;
    if (th > 4096)
        th = 4096;
    GBitmap *b;
    GpStatus st = gp_bitmap_create(tw, th, PixelFormat32bppARGB, &b);
    if (st != Ok)
        return st;
    GpGraphics *tg;
    st = GdipGetImageGraphicsContext(&b->image, &tg);
    if (st != Ok) {
        gp_bitmap_free(b);
        return st;
    }
    GGraphics *t = gp_graphics_cast(tg);
    t->interpolation = g->interpolation;
    t->smoothing = g->smoothing;
    GpPointF tp[3] = {{0, 0}, {(float)tw, 0}, {0, (float)th}};
    /* the callback sees the records once, whatever the way they are drawn */
    st = play_placed(t, m, tp, src, cb, data, nullptr);
    GdipDeleteGraphics(tg);
    if (st == Ok)
        st = gp_draw_image(g, &b->image, pt, 3, 0, 0, (float)tw, (float)th, UnitPixel, attrs);
    gp_bitmap_free(b);
    return st;
}

static GpStatus play_placed(GGraphics *g, GMetafile *m, const GpPointF *pt, const GpRectF *src,
                            EnumerateMetafileProc cb, void *data, struct GAttrs *attrs)
{
    GPlayer *p = gp_alloc_zero(sizeof(*p));
    if (!p)
        return OutOfMemory;
    p->mf = m;
    p->g = g;
    p->cb = cb;
    p->cb_data = data;
    p->attrs = attrs;
    /* the part of the graphics the metafile can reach is the parallelogram of the three points */
    GpPointF corners[4] = {pt[0], pt[1], {pt[1].X + pt[2].X - pt[0].X, pt[1].Y + pt[2].Y - pt[0].Y}, pt[2]};
    GraphicsState saved;
    GpStatus st = GdipSaveGraphics(g, &saved);
    if (st != Ok) {
        gp_free(p);
        return st;
    }
    float old_dpi_x = g->dpi_x, old_dpi_y = g->dpi_y;
    GpPath *clip = nullptr;
    st = GdipCreatePath(FillModeAlternate, &clip);
    if (st == Ok) {
        st = GdipAddPathPolygon(clip, corners, 4);
        if (st == Ok)
            st = GdipSetClipPath(g, clip, CombineModeIntersect);
        GdipDeletePath(clip);
    }
    if (st == Ok) {
        MimeVecMatrix to_world, base;
        rect_to_points(pt, src, &to_world);
        base = gp_graphics_w2d_base(g);
        mime_vec_matrix_multiply(&p->to_device, &to_world, &base);
        mime_vec_matrix_identity(&g->world);
        mime_vec_matrix_identity(&g->container);
        g->container = p->to_device;
        g->page_unit = UnitPixel;
        g->page_scale = 1.0f;
        g->w2d_valid = FALSE;
        if (m->header.Type >= MetafileTypeEmfPlusOnly) {
            p->dpi_x = m->header.LogicalDpiX > 0 ? (float)m->header.LogicalDpiX : m->header.DpiX;
            p->dpi_y = m->header.LogicalDpiY > 0 ? (float)m->header.LogicalDpiY : m->header.DpiY;
            g->dpi_x = p->dpi_x;
            g->dpi_y = p->dpi_y;
        }
        m->state = GM_PLAYING;
        m->play_thread = GetCurrentThreadId();
        m->player = p;
        if (m->header.Type >= MetafileTypeEmfPlusOnly)
            st = plus_run(p);
        else
            st = gdi_run(p);
        m->player = nullptr;
        m->play_thread = 0;
        m->state = GM_READY;
    }
    g->dpi_x = old_dpi_x;
    g->dpi_y = old_dpi_y;
    GdipRestoreGraphics(g, saved);
    gp_free(p);
    return st;
}

GpStatus gp_meta_play(GGraphics *g, GMetafile *m, const GpRectF *dest, const GpRectF *src, GpUnit src_unit,
                      EnumerateMetafileProc callback, void *data, struct GAttrs *attrs)
{
    GpPointF pt[3] = {{dest->X, dest->Y}, {dest->X + dest->Width, dest->Y}, {dest->X, dest->Y + dest->Height}};
    float fx, fy;
    if (!gp_meta_unit_factor(m, src_unit, &fx, &fy))
        return InvalidParameter;
    GpRectF s = {src->X * fx, src->Y * fy, src->Width * fx, src->Height * fy};
    if (dest->Width == 0.0f || dest->Height == 0.0f || s.Width == 0.0f || s.Height == 0.0f)
        return Ok;
    if (m->state != GM_READY)
        return InvalidParameter;
    if (m->header.Type == MetafileTypeEmf && m->header.EmfHeader.nRecords <= 2)
        return Ok;
    if (g->meta)
        return NotImplemented;
    const MimeVecMatrix *w2d = gp_graphics_w2d(g);
    BOOL turned = rotated(w2d) || (pt[1].Y != pt[0].Y) || (pt[2].X != pt[0].X);
    if (turned || (attrs && gp_attrs_active(attrs, ColorAdjustTypeDefault)))
        return play_raster(g, m, pt, &s, callback, data, attrs);
    return play_placed(g, m, pt, &s, callback, data, attrs);
}

/* the metafile drawn by the drawing calls of images: three corners of the page for a rectangle of the source. */
GpStatus gp_metafile_draw(GGraphics *g, GImage *image, const GpPointF *dst, float sx, float sy, float sw, float sh,
                          GpUnit src_unit, struct GAttrs *attrs)
{
    GMetafile *m = gp_meta_cast(image);
    if (!m)
        return InvalidParameter;
    if (m->state != GM_READY)
        return InvalidParameter;
    float fx, fy;
    if (src_unit == UnitWorld)
        src_unit = UnitPixel;
    if (!gp_meta_unit_factor(m, src_unit, &fx, &fy))
        return InvalidParameter;
    GpRectF s = {sx * fx, sy * fy, sw * fx, sh * fy};
    if (s.Width == 0.0f || s.Height == 0.0f)
        return Ok;
    if (m->header.Type == MetafileTypeEmf && m->header.EmfHeader.nRecords <= 2)
        return Ok;
    if (g->meta)
        return NotImplemented;
    const MimeVecMatrix *w2d = gp_graphics_w2d(g);
    BOOL turned = rotated(w2d) || (dst[1].Y != dst[0].Y) || (dst[2].X != dst[0].X);
    if (turned || (attrs && gp_attrs_active(attrs, ColorAdjustTypeDefault)))
        return play_raster(g, m, dst, &s, nullptr, nullptr, attrs);
    return play_placed(g, m, dst, &s, nullptr, nullptr, attrs);
}

void gp_meta_player_free(GMetafile *m) { m->player = nullptr; }

/* ------------------------------------------------------------------ one record from a callback */

GpStatus gp_meta_play_record(GMetafile *m, UINT type, UINT flags, UINT size, const BYTE *data)
{
    GPlayer *p = m->player;
    if (!p)
        return InvalidParameter;
    if (type & 0x10000) {
        if (!p->dc)
            return Ok;
        METARECORD *r = gp_alloc_zero(6 + (SIZE_T)size + 2);
        if (!r)
            return OutOfMemory;
        r->rdSize = (6 + size + 1) / 2;
        r->rdFunction = (WORD)(type & 0xffff);
        if (size)
            memcpy(r->rdParm, data, size);
        PlayMetaFileRecord(p->dc, p->table, r, (UINT)p->table_count);
        gp_free(r);
        return Ok;
    }
    if (type >= EmfPlusRecordTypeInvalid)
        return gp_plus_record(p, type & 0xffff, flags, size, data);
    ENHMETARECORD *r = gp_alloc_zero(8 + (SIZE_T)size);
    if (!r)
        return OutOfMemory;
    r->iType = type;
    r->nSize = 8 + size;
    if (size)
        memcpy(r->dParm, data, size);
    HDC dc = p->dc;
    if (!dc && p->mf->header.Type >= MetafileTypeEmfPlusOnly && p->gdi_enabled)
        dc = p->dc;
    if (dc)
        PlayEnhMetaFileRecord(dc, p->table, r, (UINT)p->table_count);
    gp_free(r);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPlayMetafileRecord(const GpMetafile *metafile, EmfPlusRecordType type, UINT flags,
                                                  UINT size, const BYTE *data)
{
    GMetafile *m = gp_meta_cast(metafile);
    if (!m || m->state < GM_RECORDING || !type)
        return InvalidParameter;
    if (GetCurrentThreadId() != m->play_thread)
        return ObjectBusy;
    if (m->state != GM_PLAYING || ((size & 3) && !(type & 0x10000)))
        return InvalidParameter;
    gp_meta_play_record(m, type, flags, size, data);
    return Ok;
}

/* ------------------------------------------------------------------ the enumerate calls */

/* the common check of every call: a graphics, a metafile, a callback and attributes that are what they say. */
static GpStatus enumerate_args(GpGraphics *graphics, const GpMetafile *metafile, EnumerateMetafileProc callback,
                               const GpImageAttributes *attrs, GGraphics **g, GMetafile **m, struct GAttrs **a)
{
    if (!callback)
        return InvalidParameter;
    *g = gp_graphics_cast(graphics);
    if (!*g)
        return InvalidParameter;
    *m = gp_meta_cast(metafile);
    if (!*m || (*m)->state < GM_RECORDING)
        return InvalidParameter;
    *a = nullptr;
    if (attrs) {
        *a = (struct GAttrs *)gp_attrs_cast(attrs);
        if (!*a)
            return InvalidParameter;
    }
    if ((*g)->got_dc)
        return ObjectBusy;
    return Ok;
}

static GpStatus enumerate(GGraphics *g, GMetafile *m, const GpPointF *pt, const GpRectF *src, GpUnit unit,
                          EnumerateMetafileProc callback, void *data, struct GAttrs *attrs)
{
    if (m->state != GM_READY)
        return InvalidParameter;
    float fx, fy;
    if (!gp_meta_unit_factor(m, unit, &fx, &fy))
        return InvalidParameter;
    GpRectF s = {src->X * fx, src->Y * fy, src->Width * fx, src->Height * fy};
    if (s.Width == 0.0f || s.Height == 0.0f)
        return Ok;
    if (m->header.Type == MetafileTypeEmf && m->header.EmfHeader.nRecords <= 2)
        return Ok;
    if (g->meta)
        return NotImplemented;
    const MimeVecMatrix *w2d = gp_graphics_w2d(g);
    BOOL turned = rotated(w2d) || (pt[1].Y != pt[0].Y) || (pt[2].X != pt[0].X);
    if (turned || (attrs && gp_attrs_active(attrs, ColorAdjustTypeDefault)))
        return play_raster(g, m, pt, &s, callback, data, attrs);
    return play_placed(g, m, pt, &s, callback, data, attrs);
}

static void bounds_of(GMetafile *m, GpRectF *r)
{
    r->X = (float)m->header.X;
    r->Y = (float)m->header.Y;
    r->Width = (float)m->header.Width;
    r->Height = (float)m->header.Height;
}

static void rect_points(const GpRectF *r, GpPointF *pt)
{
    pt[0].X = r->X;
    pt[0].Y = r->Y;
    pt[1].X = r->X + r->Width;
    pt[1].Y = r->Y;
    pt[2].X = r->X;
    pt[2].Y = r->Y + r->Height;
}

/* the destination of a point: the source rectangle at its natural size. */
static void point_dest(GGraphics *g, GMetafile *m, const GpPointF *at, const GpRectF *src, GpUnit unit, GpPointF *pt)
{
    float w, h;
    gp_metafile_page_size(g, &m->image, src->Width, src->Height, unit, &w, &h);
    GpRectF d = {at->X, at->Y, w, h};
    rect_points(&d, pt);
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestRect(GpGraphics *graphics, const GpMetafile *metafile,
                                                         const GpRectF *dest, EnumerateMetafileProc callback,
                                                         VOID *data, const GpImageAttributes *attrs)
{
    GGraphics *g;
    GMetafile *m;
    struct GAttrs *a;
    GpStatus st = enumerate_args(graphics, metafile, callback, attrs, &g, &m, &a);
    if (st != Ok || !dest)
        return st != Ok ? st : InvalidParameter;
    GpRectF src;
    bounds_of(m, &src);
    GpPointF pt[3];
    rect_points(dest, pt);
    return enumerate(g, m, pt, &src, UnitPixel, callback, data, a);
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestRectI(GpGraphics *graphics, const GpMetafile *metafile,
                                                          const GpRect *dest, EnumerateMetafileProc callback,
                                                          VOID *data, const GpImageAttributes *attrs)
{
    if (!dest)
        return InvalidParameter;
    GpRectF r = {(float)dest->X, (float)dest->Y, (float)dest->Width, (float)dest->Height};
    return GdipEnumerateMetafileDestRect(graphics, metafile, &r, callback, data, attrs);
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPoint(GpGraphics *graphics, const GpMetafile *metafile,
                                                          const GpPointF *dest, EnumerateMetafileProc callback,
                                                          VOID *data, const GpImageAttributes *attrs)
{
    GGraphics *g;
    GMetafile *m;
    struct GAttrs *a;
    GpStatus st = enumerate_args(graphics, metafile, callback, attrs, &g, &m, &a);
    if (st != Ok || !dest)
        return st != Ok ? st : InvalidParameter;
    GpRectF src;
    bounds_of(m, &src);
    GpPointF pt[3];
    point_dest(g, m, dest, &src, UnitPixel, pt);
    return enumerate(g, m, pt, &src, UnitPixel, callback, data, a);
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPointI(GpGraphics *graphics, const GpMetafile *metafile,
                                                           const GpPoint *dest, EnumerateMetafileProc callback,
                                                           VOID *data, const GpImageAttributes *attrs)
{
    if (!dest)
        return InvalidParameter;
    GpPointF p = {(float)dest->X, (float)dest->Y};
    return GdipEnumerateMetafileDestPoint(graphics, metafile, &p, callback, data, attrs);
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPoints(GpGraphics *graphics, const GpMetafile *metafile,
                                                           const GpPointF *dest, INT count,
                                                           EnumerateMetafileProc callback, VOID *data,
                                                           const GpImageAttributes *attrs)
{
    GGraphics *g;
    GMetafile *m;
    struct GAttrs *a;
    GpStatus st = enumerate_args(graphics, metafile, callback, attrs, &g, &m, &a);
    if (st != Ok || !dest)
        return st != Ok ? st : InvalidParameter;
    if (count != 3)
        return NotImplemented;
    GpRectF src;
    bounds_of(m, &src);
    return enumerate(g, m, dest, &src, UnitPixel, callback, data, a);
}

static GpStatus points_i(const GpPoint *in, INT count, GpPointF *out)
{
    if (!in)
        return InvalidParameter;
    if (count != 3)
        return NotImplemented;
    for (INT i = 0; i < 3; i++) {
        out[i].X = (float)in[i].X;
        out[i].Y = (float)in[i].Y;
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPointsI(GpGraphics *graphics, const GpMetafile *metafile,
                                                            const GpPoint *dest, INT count,
                                                            EnumerateMetafileProc callback, VOID *data,
                                                            const GpImageAttributes *attrs)
{
    GpPointF pt[3];
    GpStatus st = points_i(dest, count, pt);
    if (st != Ok)
        return st;
    return GdipEnumerateMetafileDestPoints(graphics, metafile, pt, 3, callback, data, attrs);
}

/* the source rectangle variants name a unit, which the calls check first. */
static GpStatus src_unit_ok(Unit unit)
{
    return unit >= UnitPixel && unit <= UnitMillimeter ? Ok : InvalidParameter;
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestRect(GpGraphics *graphics, const GpMetafile *metafile,
                                                                const GpRectF *dest, const GpRectF *src, Unit unit,
                                                                EnumerateMetafileProc callback, VOID *data,
                                                                const GpImageAttributes *attrs)
{
    GGraphics *g;
    GMetafile *m;
    struct GAttrs *a;
    GpStatus st = enumerate_args(graphics, metafile, callback, attrs, &g, &m, &a);
    if (st != Ok)
        return st;
    if (src_unit_ok(unit) != Ok || !dest || !src)
        return InvalidParameter;
    GpPointF pt[3];
    rect_points(dest, pt);
    return enumerate(g, m, pt, src, unit, callback, data, a);
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestRectI(GpGraphics *graphics, const GpMetafile *metafile,
                                                                 const GpRect *dest, const GpRect *src, Unit unit,
                                                                 EnumerateMetafileProc callback, VOID *data,
                                                                 const GpImageAttributes *attrs)
{
    if (!dest || !src)
        return InvalidParameter;
    GpRectF d = {(float)dest->X, (float)dest->Y, (float)dest->Width, (float)dest->Height};
    GpRectF s = {(float)src->X, (float)src->Y, (float)src->Width, (float)src->Height};
    return GdipEnumerateMetafileSrcRectDestRect(graphics, metafile, &d, &s, unit, callback, data, attrs);
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPoint(GpGraphics *graphics, const GpMetafile *metafile,
                                                                 const GpPointF *dest, const GpRectF *src, Unit unit,
                                                                 EnumerateMetafileProc callback, VOID *data,
                                                                 const GpImageAttributes *attrs)
{
    GGraphics *g;
    GMetafile *m;
    struct GAttrs *a;
    GpStatus st = enumerate_args(graphics, metafile, callback, attrs, &g, &m, &a);
    if (st != Ok)
        return st;
    if (src_unit_ok(unit) != Ok || !dest || !src)
        return InvalidParameter;
    GpPointF pt[3];
    point_dest(g, m, dest, src, unit, pt);
    return enumerate(g, m, pt, src, unit, callback, data, a);
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPointI(GpGraphics *graphics, const GpMetafile *metafile,
                                                                  const GpPoint *dest, const GpRect *src, Unit unit,
                                                                  EnumerateMetafileProc callback, VOID *data,
                                                                  const GpImageAttributes *attrs)
{
    if (!dest || !src)
        return InvalidParameter;
    GpPointF p = {(float)dest->X, (float)dest->Y};
    GpRectF s = {(float)src->X, (float)src->Y, (float)src->Width, (float)src->Height};
    return GdipEnumerateMetafileSrcRectDestPoint(graphics, metafile, &p, &s, unit, callback, data, attrs);
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPoints(GpGraphics *graphics, const GpMetafile *metafile,
                                                                  const GpPointF *dest, INT count, const GpRectF *src,
                                                                  Unit unit, EnumerateMetafileProc callback,
                                                                  VOID *data, const GpImageAttributes *attrs)
{
    GGraphics *g;
    GMetafile *m;
    struct GAttrs *a;
    GpStatus st = enumerate_args(graphics, metafile, callback, attrs, &g, &m, &a);
    if (st != Ok)
        return st;
    if (src_unit_ok(unit) != Ok || !dest || !src)
        return InvalidParameter;
    if (count != 3)
        return NotImplemented;
    return enumerate(g, m, dest, src, unit, callback, data, a);
}

WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPointsI(GpGraphics *graphics, const GpMetafile *metafile,
                                                                   const GpPoint *dest, INT count, const GpRect *src,
                                                                   Unit unit, EnumerateMetafileProc callback,
                                                                   VOID *data, const GpImageAttributes *attrs)
{
    if (!src)
        return InvalidParameter;
    GpPointF pt[3];
    GpStatus st = points_i(dest, count, pt);
    if (st != Ok)
        return st;
    GpRectF s = {(float)src->X, (float)src->Y, (float)src->Width, (float)src->Height};
    return GdipEnumerateMetafileSrcRectDestPoints(graphics, metafile, pt, 3, &s, unit, callback, data, attrs);
}

/* ------------------------------------------------------------------ records from a terminal services client */

/*
 * the client of a terminal session gets emf+ records for a device context. kinds: 1 starts a session on the
 * context, 2 ends it, 3 and 4 tell the format and the palette of the remote surface, 5 plays the records.
 * the objects the records define stay between the calls of a session.
 */
static GpGraphics *ts_graphics;
static HDC ts_dc;
static GPlayer ts_player;
static GMetafile ts_meta;

WINGDIPAPI GpStatus WINAPI GdipPlayTSClientRecord(HDC hdc, INT kind, const BYTE *data, UINT size, void *extra)
{
    GP_REQUIRE_INIT();
    switch (kind) {
    case 0:
        return (GpStatus)10011;
    case 1:
        if (!ts_graphics) {
            if (GdipCreateFromHDC(hdc, &ts_graphics) != Ok)
                return GenericError;
            ts_dc = hdc;
            memset(&ts_player, 0, sizeof(ts_player));
            memset(&ts_meta, 0, sizeof(ts_meta));
            ts_meta.header.Version = EMFPLUS_VERSION;
            ts_player.mf = &ts_meta;
            ts_player.g = gp_graphics_cast(ts_graphics);
            gp_plus_player_init(&ts_player);
        }
        return Ok;
    case 2:
        if (ts_graphics) {
            gp_plus_player_free(&ts_player);
            GdipDeleteGraphics(ts_graphics);
            ts_graphics = nullptr;
            ts_dc = nullptr;
        }
        return Ok;
    case 3:
        return Ok;
    case 4:
        return Ok;
    case 5:
        break;
    default:
        return InvalidParameter;
    }
    if (!data || !size || !ts_graphics)
        return InvalidParameter;
    if (hdc != ts_dc) {
        GpGraphics *again;
        if (GdipCreateFromHDC(hdc, &again) != Ok)
            return GenericError;
        GdipDeleteGraphics(ts_graphics);
        ts_graphics = again;
        ts_dc = hdc;
        ts_player.g = gp_graphics_cast(again);
    }
    ts_player.cb = nullptr;
    GGraphics *g = ts_player.g;
    GraphicsState saved;
    if (GdipSaveGraphics(ts_graphics, &saved) != Ok)
        return GenericError;
    UINT pos = 0;
    GpStatus st = Ok;
    while (size - pos >= 12) {
        UINT type = rd16(data + pos), flags = rd16(data + pos + 2);
        UINT n = rd32(data + pos + 4), d = rd32(data + pos + 8);
        if (n < 12 || n > size - pos || d != n - 12) {
            st = Aborted;
            break;
        }
        gp_plus_record(&ts_player, type, flags, d, data + pos + 12);
        pos += n;
    }
    (void)g;
    GdipRestoreGraphics(ts_graphics, saved);
    return st;
}
