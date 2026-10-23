/*
 * recording metafiles: a recording graphics writes emf+ records into comment records of an enhanced metafile that
 * the system gdi records. the drawing core hands it shapes, images and clips instead of drawing them, and state
 * changes since the last draw are written before the next one.
 */
#include "meta_private.h"

/* ------------------------------------------------------------------ growing buffer */

typedef struct Wr {
    BYTE *p;
    SIZE_T len, cap;
    BOOL bad;
} Wr;

static void w_free(Wr *w)
{
    gp_free(w->p);
    memset(w, 0, sizeof(*w));
}

static BOOL w_room(Wr *w, SIZE_T more)
{
    if (w->bad)
        return FALSE;
    if (w->len + more <= w->cap)
        return TRUE;
    SIZE_T cap = w->cap ? w->cap : 256;
    while (cap < w->len + more)
        cap *= 2;
    BYTE *p = gp_realloc(w->p, cap);
    if (!p) {
        w->bad = TRUE;
        return FALSE;
    }
    w->p = p;
    w->cap = cap;
    return TRUE;
}

static void w_bytes(Wr *w, const void *data, SIZE_T n)
{
    if (!n || !w_room(w, n))
        return;
    memcpy(w->p + w->len, data, n);
    w->len += n;
}

static void w_u32(Wr *w, UINT32 v) { w_bytes(w, &v, 4); }
static void w_u16(Wr *w, UINT16 v) { w_bytes(w, &v, 2); }

static void w_f(Wr *w, float v) { w_bytes(w, &v, 4); }

static void w_pad(Wr *w)
{
    static const BYTE zero[4] = {};
    w_bytes(w, zero, (4 - (w->len & 3)) & 3);
}

/* ------------------------------------------------------------------ recorder */

#define REC_CHUNK 60000u

typedef struct RecObject {
    INT type;
    BYTE *data;
    SIZE_T size;
    UINT stamp;
} RecObject;

/* the states the stream has set, and the levels that saved them. */
typedef struct RecState {
    MimeVecMatrix world;
    GpUnit page_unit;
    float page_scale;
    SmoothingMode smoothing;
    CompositingMode compositing;
    CompositingQuality quality;
    InterpolationMode interpolation;
    PixelOffsetMode pixel_offset;
    TextRenderingHint text_hint;
    UINT text_contrast;
    POINT origin;
    /* modes the stream has not set yet are unknown. */
    BOOL known;
} RecState;

typedef struct RecLevel {
    UINT id;
    INT kind;
    RecState state;
} RecLevel;

typedef struct GRecorder {
    GMetafile *mf;
    GGraphics *g;
    HDC dc;
    EmfType type;
    Wr out;
    RecObject objects[64];
    UINT clock;
    RecState sent;
    RecLevel *levels;
    UINT level_count, level_cap;
    BOOL has_frame;
    RECT frame;
    UINT limit;
    BOOL got_dc;
    BOOL failed;
    /* clip change stamps: the graphics' current one and the one sent to the metafile dc. */
    UINT clip_stamp, clip_sent;
    /* what was drawn, in pixels, for a picture without a frame. */
    BOOL has_bounds;
    float bx0, by0, bx1, by1;
} GRecorder;

static BOOL plus_only(const GRecorder *r) { return r->type != EmfTypeEmfOnly; }

/* the comment record carries the identifier, then the records. */
static void out_reset(GRecorder *r)
{
    r->out.len = 0;
    w_u32(&r->out, EMFPLUS_SIGNATURE);
}

static void out_flush(GRecorder *r)
{
    if (!plus_only(r) || r->out.len <= 4)
        return;
    if (!r->out.bad)
        GdiComment(r->dc, (UINT)r->out.len, r->out.p);
    else
        r->failed = TRUE;
    r->out.bad = FALSE;
    out_reset(r);
}

/* data is padded to a multiple of four. */
static void put_record(GRecorder *r, UINT type, UINT flags, const void *data, SIZE_T size)
{
    if (!plus_only(r))
        return;
    SIZE_T total = 12 + ((size + 3) & ~(SIZE_T)3);
    if (r->out.len + total > REC_CHUNK && r->out.len > 4)
        out_flush(r);
    w_u16(&r->out, (UINT16)type);
    w_u16(&r->out, (UINT16)flags);
    w_u32(&r->out, (UINT32)total);
    w_u32(&r->out, (UINT32)(total - 12));
    w_bytes(&r->out, data, size);
    w_pad(&r->out);
}

static void put_wr(GRecorder *r, UINT type, UINT flags, Wr *w)
{
    put_record(r, type, flags, w->p, w->len);
    w_free(w);
}

/* ------------------------------------------------------------------ objects */

static void object_clear(RecObject *o)
{
    gp_free(o->data);
    memset(o, 0, sizeof(*o));
}

/* split into pieces when large. */
static void put_object(GRecorder *r, UINT id, INT type, const BYTE *data, SIZE_T size)
{
    UINT flags = id | ((UINT)type << 8);
    if (size + 12 <= REC_CHUNK) {
        put_record(r, EmfPlusRecordTypeObject, flags, data, size);
        return;
    }
    SIZE_T done = 0;
    BOOL first = TRUE;
    while (done < size || first) {
        SIZE_T room = REC_CHUNK - 32;
        SIZE_T take = size - done < room ? size - done : room;
        Wr w = {};
        if (first)
            w_u32(&w, (UINT32)size);
        w_bytes(&w, data + done, take);
        put_record(r, EmfPlusRecordTypeObject, flags | (first ? 0x8000u : 0), w.p, w.len);
        w_free(&w);
        done += take;
        first = FALSE;
    }
}

/* reuses an object written earlier with the same bytes, else makes a new one. */
static UINT object_number(GRecorder *r, INT type, const Wr *w)
{
    UINT free_slot = 64, oldest = 0;
    for (UINT i = 0; i < 64; i++) {
        RecObject *o = &r->objects[i];
        if (o->data && o->type == type && o->size == w->len && !memcmp(o->data, w->p, w->len)) {
            o->stamp = ++r->clock;
            return i;
        }
        if (!o->data && free_slot == 64)
            free_slot = i;
        if (r->objects[i].stamp < r->objects[oldest].stamp)
            oldest = i;
    }
    UINT slot = free_slot != 64 ? free_slot : oldest;
    RecObject *o = &r->objects[slot];
    object_clear(o);
    o->data = gp_dup(w->p, w->len);
    if (!o->data) {
        r->failed = TRUE;
        return slot;
    }
    o->type = type;
    o->size = w->len;
    o->stamp = ++r->clock;
    put_object(r, slot, type, w->p, w->len);
    return slot;
}

static void head(Wr *w, UINT32 kind)
{
    w_u32(w, EMFPLUS_VERSION);
    w_u32(w, kind);
}

static void w_matrix(Wr *w, const MimeVecMatrix *m)
{
    w_f(w, m->m11);
    w_f(w, m->m12);
    w_f(w, m->m21);
    w_f(w, m->m22);
    w_f(w, m->dx);
    w_f(w, m->dy);
}

static BOOL identity(const MimeVecMatrix *m) { return mime_vec_matrix_is_identity(m); }

/* pixels as 32 bit straight alpha, row after row. */
static BOOL image_bitmap(Wr *w, GBitmap *b)
{
    UINT32 stride = (UINT32)b->width * 4;
    UINT64 total = (UINT64)stride * (UINT32)b->height;
    if (total > 0x20000000u)
        return FALSE;
    w_u32(w, EMFPLUS_VERSION);
    w_u32(w, 1);
    w_u32(w, (UINT32)b->width);
    w_u32(w, (UINT32)b->height);
    w_u32(w, stride);
    w_u32(w, PixelFormat32bppARGB);
    w_u32(w, 0);
    if (!w_room(w, (SIZE_T)total))
        return FALSE;
    for (INT y = 0; y < b->height; y++) {
        gp_bitmap_read_argb(b, 0, y, b->width, (UINT32 *)(w->p + w->len));
        w->len += stride;
    }
    return TRUE;
}

static BOOL image_object(Wr *w, GImage *image)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (b)
        return image_bitmap(w, b);
    GMetafile *m = gp_meta_cast(image);
    if (!m || m->state != GM_READY)
        return FALSE;
    DWORD size;
    const BYTE *bytes = m->header.Type >= MetafileTypeEmf ? gp_meta_bytes(m, &size) : nullptr;
    if (!bytes) {
        if (m->header.Type >= MetafileTypeEmf)
            return FALSE;
        UINT n = GetMetaFileBitsEx((HMETAFILE)m->handle, 0, nullptr);
        BYTE *bits = n ? gp_alloc(n) : nullptr;
        if (!bits || GetMetaFileBitsEx((HMETAFILE)m->handle, n, bits) != n) {
            gp_free(bits);
            return FALSE;
        }
        w_u32(w, EMFPLUS_VERSION);
        w_u32(w, 2);
        w_u32(w, MetafileTypeWmf);
        w_u32(w, n);
        w_bytes(w, bits, n);
        gp_free(bits);
        return !w->bad;
    }
    w_u32(w, EMFPLUS_VERSION);
    w_u32(w, 2);
    w_u32(w, m->header.Type);
    w_u32(w, size);
    w_bytes(w, bytes, size);
    return !w->bad;
}

/*
 * the format's linear gradient is horizontal over a rectangle, moved by a matrix. every mode here is put in that
 * form: the gradient runs along x over the rectangle and the matrix moves it into place.
 */
static void linear_form(const GBrush *b, GpRectF *rect, MimeVecMatrix *xf)
{
    float rx = b->rect.X, ry = b->rect.Y, rw = b->rect.Width, rh = b->rect.Height;
    MimeVecMatrix a;
    rect->X = rect->Y = 0;
    rect->Width = rect->Height = 1;
    switch (b->lg_mode) {
    case LinearGradientModeHorizontal:
        *rect = b->rect;
        mime_vec_matrix_identity(&a);
        break;
    case LinearGradientModeVertical: {
        rect->Width = rh;
        rect->Height = rw;
        /* x runs down the rectangle and y runs across it. */
        a.m11 = 0;
        a.m12 = 1;
        a.m21 = 1;
        a.m22 = 0;
        a.dx = rx;
        a.dy = ry;
        break;
    }
    case LinearGradientModeForwardDiagonal:
        a.m11 = rw;
        a.m12 = rh;
        a.m21 = rw;
        a.m22 = -rh;
        a.dx = rx;
        a.dy = ry;
        break;
    case LinearGradientModeBackwardDiagonal:
        a.m11 = -rw;
        a.m12 = rh;
        a.m21 = -rw;
        a.m22 = -rh;
        a.dx = rx + rw;
        a.dy = ry;
        break;
    case GP_LINE_POINTS: {
        float dx = b->p2.X - b->p1.X, dy = b->p2.Y - b->p1.Y;
        float l = mime_vec_sqrtf(dx * dx + dy * dy);
        if (l <= 0.0f)
            l = 1.0f;
        float ux = dx / l, uy = dy / l;
        rect->Width = l;
        rect->Height = l;
        a.m11 = ux;
        a.m12 = uy;
        a.m21 = -uy;
        a.m22 = ux;
        a.dx = b->p1.X;
        a.dy = b->p1.Y;
        break;
    }
    default: {
        float sn, cs;
        mime_vec_sincosf(b->angle * 3.14159265358979f / 180.0f, &sn, &cs);
        float ac = cs < 0 ? -cs : cs, as = sn < 0 ? -sn : sn;
        if (b->scalable) {
            /* the direction is in the rectangle's unit square. */
            float len = ac + as;
            float ox = 0.5f - cs * len * 0.5f, oy = 0.5f - sn * len * 0.5f;
            a.m11 = cs * len * rw;
            a.m12 = sn * len * rh;
            a.m21 = -sn * rw;
            a.m22 = cs * rh;
            a.dx = rx + ox * rw;
            a.dy = ry + oy * rh;
        } else {
            float len = rw * ac + rh * as;
            float cx = rx + rw * 0.5f, cy = ry + rh * 0.5f;
            rect->Width = len;
            rect->Height = len;
            a.m11 = cs;
            a.m12 = sn;
            a.m21 = -sn;
            a.m22 = cs;
            a.dx = cx - cs * len * 0.5f;
            a.dy = cy - sn * len * 0.5f;
        }
        break;
    }
    }
    mime_vec_matrix_multiply(xf, &a, &b->xform);
}

static BOOL path_object(Wr *w, const MimeVecPath *path, FillMode mode);

static BOOL brush_object(Wr *w, const GBrush *b)
{
    switch (b->type) {
    case BrushTypeSolidColor:
        head(w, 0);
        w_u32(w, b->color);
        return TRUE;
    case BrushTypeHatchFill:
        head(w, 1);
        w_u32(w, b->hatch_style);
        w_u32(w, b->hatch_fore);
        w_u32(w, b->hatch_back);
        return TRUE;
    case BrushTypeTextureFill: {
        if (!b->image)
            return FALSE;
        UINT32 flags = identity(&b->xform) ? 0 : 0x2;
        head(w, 2);
        w_u32(w, flags);
        w_u32(w, b->wrap);
        if (flags & 0x2)
            w_matrix(w, &b->xform);
        return image_bitmap(w, b->image);
    }
    case BrushTypeLinearGradient: {
        GpRectF rect;
        MimeVecMatrix xf;
        linear_form(b, &rect, &xf);
        UINT32 flags = 0;
        if (!identity(&xf))
            flags |= 0x2;
        if (b->preset.count > 1)
            flags |= 0x4;
        else if (b->blend.count)
            flags |= 0x8;
        if (b->gamma)
            flags |= 0x80;
        head(w, 4);
        w_u32(w, flags);
        w_u32(w, b->wrap);
        w_f(w, rect.X);
        w_f(w, rect.Y);
        w_f(w, rect.Width);
        w_f(w, rect.Height);
        w_u32(w, b->color1);
        w_u32(w, b->color2);
        w_u32(w, b->color1);
        w_u32(w, b->color2);
        if (flags & 0x2)
            w_matrix(w, &xf);
        if (flags & 0x4) {
            w_u32(w, (UINT32)b->preset.count);
            w_bytes(w, b->preset.positions, sizeof(float) * b->preset.count);
            w_bytes(w, b->preset.colors, sizeof(ARGB) * b->preset.count);
        } else if (flags & 0x8) {
            w_u32(w, (UINT32)b->blend.count);
            w_bytes(w, b->blend.positions, sizeof(float) * b->blend.count);
            w_bytes(w, b->blend.factors, sizeof(float) * b->blend.count);
        }
        return !w->bad;
    }
    case BrushTypePathGradient: {
        if (!b->pg_path)
            return FALSE;
        UINT32 flags = 0x1;
        if (!identity(&b->xform))
            flags |= 0x2;
        if (b->preset.count > 1)
            flags |= 0x4;
        else if (b->blend.count)
            flags |= 0x8;
        if (b->focus_x != 0.0f || b->focus_y != 0.0f)
            flags |= 0x40;
        if (b->gamma)
            flags |= 0x80;
        head(w, 3);
        w_u32(w, flags);
        w_u32(w, b->wrap);
        w_u32(w, b->center_color);
        w_f(w, b->center.X);
        w_f(w, b->center.Y);
        w_u32(w, (UINT32)b->surround_count);
        w_bytes(w, b->surround, sizeof(ARGB) * (b->surround_count > 0 ? b->surround_count : 0));
        Wr path = {};
        if (!path_object(&path, &b->pg_path->path, b->pg_path->fill_mode)) {
            w_free(&path);
            return FALSE;
        }
        w_u32(w, (UINT32)path.len);
        w_bytes(w, path.p, path.len);
        w_free(&path);
        if (flags & 0x2)
            w_matrix(w, &b->xform);
        if (flags & 0x4) {
            w_u32(w, (UINT32)b->preset.count);
            w_bytes(w, b->preset.positions, sizeof(float) * b->preset.count);
            w_bytes(w, b->preset.colors, sizeof(ARGB) * b->preset.count);
        } else if (flags & 0x8) {
            w_u32(w, (UINT32)b->blend.count);
            w_bytes(w, b->blend.positions, sizeof(float) * b->blend.count);
            w_bytes(w, b->blend.factors, sizeof(float) * b->blend.count);
        }
        if (flags & 0x40) {
            w_u32(w, 2);
            w_f(w, b->focus_x);
            w_f(w, b->focus_y);
        }
        return !w->bad;
    }
    default:
        return FALSE;
    }
}

static BOOL path_object(Wr *w, const MimeVecPath *path, FillMode mode)
{
    w_u32(w, EMFPLUS_VERSION);
    w_u32(w, (UINT32)path->count);
    w_u32(w, mode == FillModeWinding ? 0x2000u : 0u);
    for (UINT i = 0; i < path->count; i++) {
        w_f(w, path->pts[i].x);
        w_f(w, path->pts[i].y);
    }
    w_bytes(w, path->types, path->count);
    w_pad(w);
    return !w->bad;
}

static BOOL cap_object(Wr *w, const GCap *cap)
{
    if (cap->arrow) {
        head(w, 1);
        w_f(w, cap->arrow_width);
        w_f(w, cap->arrow_height);
        w_f(w, cap->arrow_inset);
        w_u32(w, cap->arrow_fill);
        w_u32(w, cap->start_cap);
        w_u32(w, cap->end_cap);
        w_u32(w, cap->join);
        w_f(w, 10.0f);
        w_f(w, cap->width_scale);
        w_f(w, 0);
        w_f(w, 0);
        w_f(w, 0);
        w_f(w, 0);
        return !w->bad;
    }
    UINT32 flags = (cap->fill ? 1u : 0u) | (cap->stroke ? 2u : 0u);
    head(w, 0);
    w_u32(w, flags);
    w_u32(w, cap->base_cap);
    w_f(w, cap->base_inset);
    w_u32(w, cap->start_cap);
    w_u32(w, cap->end_cap);
    w_u32(w, cap->join);
    w_f(w, 10.0f);
    w_f(w, cap->width_scale);
    w_f(w, 0);
    w_f(w, 0);
    w_f(w, 0);
    w_f(w, 0);
    for (INT k = 0; k < 2; k++) {
        const GPath *p = k ? cap->stroke : cap->fill;
        if (!p)
            continue;
        Wr pw = {};
        if (!path_object(&pw, &p->path, p->fill_mode)) {
            w_free(&pw);
            return FALSE;
        }
        w_u32(w, (UINT32)pw.len);
        w_bytes(w, pw.p, pw.len);
        w_free(&pw);
    }
    return !w->bad;
}

static BOOL pen_object(Wr *w, const GPen *pen)
{
    UINT32 flags = 0;
    if (!identity(&pen->xform))
        flags |= 0x1;
    if (pen->start_cap != LineCapFlat)
        flags |= 0x2;
    if (pen->end_cap != LineCapFlat)
        flags |= 0x4;
    if (pen->join != LineJoinMiter)
        flags |= 0x8;
    if (pen->miter_limit != 10.0f)
        flags |= 0x10;
    if (pen->dash_style != DashStyleSolid)
        flags |= 0x20;
    if (pen->dash_cap != DashCapFlat)
        flags |= 0x40;
    if (pen->dash_offset != 0.0f)
        flags |= 0x80;
    if (pen->dash_count > 0 && pen->dash_style == DashStyleCustom)
        flags |= 0x100;
    if (pen->align != PenAlignmentCenter)
        flags |= 0x200;
    if (pen->compound_count > 0)
        flags |= 0x400;
    if (pen->custom_start)
        flags |= 0x800;
    if (pen->custom_end)
        flags |= 0x1000;
    head(w, 0);
    w_u32(w, flags);
    w_u32(w, pen->unit);
    w_f(w, pen->width);
    if (flags & 0x1)
        w_matrix(w, &pen->xform);
    if (flags & 0x2)
        w_u32(w, pen->start_cap);
    if (flags & 0x4)
        w_u32(w, pen->end_cap);
    if (flags & 0x8)
        w_u32(w, pen->join);
    if (flags & 0x10)
        w_f(w, pen->miter_limit);
    if (flags & 0x20)
        w_u32(w, pen->dash_style);
    if (flags & 0x40)
        w_u32(w, pen->dash_cap);
    if (flags & 0x80)
        w_f(w, pen->dash_offset);
    if (flags & 0x100) {
        w_u32(w, (UINT32)pen->dash_count);
        w_bytes(w, pen->dash, sizeof(float) * pen->dash_count);
    }
    if (flags & 0x200)
        w_u32(w, pen->align);
    if (flags & 0x400) {
        w_u32(w, (UINT32)pen->compound_count);
        w_bytes(w, pen->compound, sizeof(float) * pen->compound_count);
    }
    for (INT k = 0; k < 2; k++) {
        const GCap *cap = k ? pen->custom_end : pen->custom_start;
        if (!cap)
            continue;
        Wr cw = {};
        if (!cap_object(&cw, cap)) {
            w_free(&cw);
            return FALSE;
        }
        w_u32(w, (UINT32)cw.len);
        w_bytes(w, cw.p, cw.len);
        w_free(&cw);
    }
    return pen->brush ? brush_object(w, pen->brush) : FALSE;
}

/* ------------------------------------------------------------------ states */

static void state_of(const GGraphics *g, RecState *s)
{
    s->world = g->world;
    s->page_unit = g->page_unit;
    s->page_scale = g->page_scale;
    s->smoothing = g->smoothing;
    s->compositing = g->compositing_mode;
    s->quality = g->compositing_quality;
    s->interpolation = g->interpolation;
    s->pixel_offset = g->pixel_offset;
    s->text_hint = g->text_hint;
    s->text_contrast = g->text_contrast;
    s->origin = g->origin;
    s->known = TRUE;
}

static void put_mode(GRecorder *r, UINT type, UINT flags) { put_record(r, type, flags, nullptr, 0); }

/* writes what differs between the graphics and the stream. */
static void sync_state(GRecorder *r)
{
    GGraphics *g = r->g;
    RecState *s = &r->sent;
    RecState now;
    state_of(g, &now);
    BOOL all = !s->known;
    if (all || memcmp(&now.world, &s->world, sizeof(now.world))) {
        if (identity(&now.world)) {
            if (!all || !identity(&s->world))
                put_record(r, EmfPlusRecordTypeResetWorldTransform, 0, nullptr, 0);
        } else {
            Wr w = {};
            w_matrix(&w, &now.world);
            put_wr(r, EmfPlusRecordTypeSetWorldTransform, 0, &w);
        }
    }
    if (all || now.page_unit != s->page_unit || now.page_scale != s->page_scale) {
        if (!all || now.page_unit != UnitPixel || now.page_scale != 1.0f) {
            Wr w = {};
            w_f(&w, now.page_scale);
            put_wr(r, EmfPlusRecordTypeSetPageTransform, (UINT)now.page_unit, &w);
        }
    }
    if (all || now.smoothing != s->smoothing) {
        BOOL aa = now.smoothing == SmoothingModeHighQuality || now.smoothing >= SmoothingModeAntiAlias;
        put_mode(r, EmfPlusRecordTypeSetAntiAliasMode, ((UINT)now.smoothing << 1) | (aa ? 1u : 0u));
    }
    if (all || now.compositing != s->compositing)
        put_mode(r, EmfPlusRecordTypeSetCompositingMode, (UINT)now.compositing);
    if (all || now.quality != s->quality)
        put_mode(r, EmfPlusRecordTypeSetCompositingQuality, (UINT)now.quality);
    if (all || now.interpolation != s->interpolation)
        put_mode(r, EmfPlusRecordTypeSetInterpolationMode, (UINT)now.interpolation);
    if (all || now.pixel_offset != s->pixel_offset)
        put_mode(r, EmfPlusRecordTypeSetPixelOffsetMode, (UINT)now.pixel_offset);
    if (all || now.text_hint != s->text_hint)
        put_mode(r, EmfPlusRecordTypeSetTextRenderingHint, (UINT)now.text_hint);
    if (all || now.text_contrast != s->text_contrast)
        put_mode(r, EmfPlusRecordTypeSetTextContrast, now.text_contrast);
    if (all || now.origin.x != s->origin.x || now.origin.y != s->origin.y) {
        Wr w = {};
        w_u32(&w, (UINT32)now.origin.x);
        w_u32(&w, (UINT32)now.origin.y);
        put_wr(r, EmfPlusRecordTypeSetRenderingOrigin, 0, &w);
    }
    *s = now;
}

static void level_push(GRecorder *r, UINT id, INT kind)
{
    if (r->level_count == r->level_cap) {
        UINT cap = r->level_cap ? r->level_cap * 2 : 8;
        RecLevel *l = gp_realloc(r->levels, sizeof(RecLevel) * cap);
        if (!l) {
            r->failed = TRUE;
            return;
        }
        r->levels = l;
        r->level_cap = cap;
    }
    r->levels[r->level_count].id = id;
    r->levels[r->level_count].kind = kind;
    r->levels[r->level_count].state = r->sent;
    r->level_count++;
}

/* ------------------------------------------------------------------ gdi side */

/*
 * a gdi-only metafile, and the gdi part of a dual one, holds the emf+ drawing as system gdi drawing: polygons
 * filled with a plain brush, and anything else drawn first on a bitmap that is then blended.
 */

static BOOL gdi_wanted(const GRecorder *r) { return r->type != EmfTypeEmfPlusOnly; }

static void bounds_add(GRecorder *r, float x0, float y0, float x1, float y1)
{
    if (!r->has_bounds) {
        r->bx0 = x0;
        r->by0 = y0;
        r->bx1 = x1;
        r->by1 = y1;
        r->has_bounds = TRUE;
        return;
    }
    if (x0 < r->bx0)
        r->bx0 = x0;
    if (y0 < r->by0)
        r->by0 = y0;
    if (x1 > r->bx1)
        r->bx1 = x1;
    if (y1 > r->by1)
        r->by1 = y1;
}

static void bounds_polys(GRecorder *r, const MimeVecPolys *polys)
{
    MimeVecBounds b;
    if (mime_vec_polys_bounds(polys, &b))
        bounds_add(r, b.left, b.top, b.right, b.bottom);
}

static INT round_pixel(float v) { return (INT)mime_vec_floorf(v + 0.5f); }

static void gdi_clip(GRecorder *r)
{
    if (r->clip_stamp == r->clip_sent)
        return;
    HRGN region = gp_meta_clip_region(r->g);
    SelectClipRgn(r->dc, region);
    if (region)
        DeleteObject(region);
    r->clip_sent = r->clip_stamp;
}

static COLORREF colorref_of(ARGB c) { return RGB((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff); }

static void gdi_flat_polys(GRecorder *r, const MimeVecPolys *polys, INT fill_rule, ARGB color)
{
    if (!polys->nsubs)
        return;
    POINT *pts = gp_alloc(sizeof(POINT) * (polys->npts ? polys->npts : 1));
    INT *counts = gp_alloc(sizeof(INT) * polys->nsubs);
    if (!pts || !counts) {
        gp_free(pts);
        gp_free(counts);
        return;
    }
    UINT k = 0, n = 0;
    for (UINT i = 0; i < polys->nsubs; i++) {
        const MimeVecSub *sub = &polys->subs[i];
        if (sub->count < 2)
            continue;
        for (UINT j = 0; j < sub->count; j++) {
            pts[k].x = round_pixel(polys->pts[sub->first + j].x);
            pts[k].y = round_pixel(polys->pts[sub->first + j].y);
            k++;
        }
        counts[n++] = (INT)sub->count;
    }
    if (n) {
        HBRUSH brush = CreateSolidBrush(colorref_of(color));
        HGDIOBJ old_brush = SelectObject(r->dc, brush);
        HGDIOBJ old_pen = SelectObject(r->dc, GetStockObject(NULL_PEN));
        SetPolyFillMode(r->dc, fill_rule == MIME_VEC_FILL_WINDING ? WINDING : ALTERNATE);
        PolyPolygon(r->dc, pts, counts, (INT)n);
        SelectObject(r->dc, old_pen);
        SelectObject(r->dc, old_brush);
        DeleteObject(brush);
    }
    gp_free(pts);
    gp_free(counts);
}

typedef struct RasterJob {
    const MimeVecPolys *polys;
    INT rule;
    const GBrush *brush;
    const MimeVecMask *mask;
    GImage *image;
    const GpPointF *dst;
    float src[4];
    GpUnit unit;
    struct GAttrs *attrs;
} RasterJob;

/* draws the job on a bitmap of the area, then blends the bitmap onto the metafile dc. */
static void gdi_raster(GRecorder *r, RECT area, const RasterJob *job)
{
    GGraphics *g = r->g;
    if (area.left < 0)
        area.left = 0;
    if (area.top < 0)
        area.top = 0;
    if (area.right > g->width)
        area.right = g->width;
    if (area.bottom > g->height)
        area.bottom = g->height;
    INT w = area.right - area.left, h = area.bottom - area.top;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
        return;
    GBitmap *b;
    if (gp_bitmap_create(w, h, PixelFormat32bppARGB, &b) != Ok)
        return;
    GpGraphics *tg;
    if (GdipGetImageGraphicsContext(&b->image, &tg) != Ok) {
        gp_bitmap_free(b);
        return;
    }
    GGraphics *t = gp_graphics_cast(tg);
    t->smoothing = g->smoothing;
    t->interpolation = g->interpolation;
    t->compositing_quality = g->compositing_quality;
    t->pixel_offset = g->pixel_offset;
    t->dpi_x = g->dpi_x;
    t->dpi_y = g->dpi_y;
    t->world = g->world;
    t->page_unit = g->page_unit;
    t->page_scale = g->page_scale;
    MimeVecMatrix shift = {1, 0, 0, 1, (float)-area.left, (float)-area.top};
    mime_vec_matrix_multiply(&t->container, &g->container, &shift);
    t->w2d_valid = FALSE;
    GpStatus st = Ok;
    if (job->polys) {
        MimeVecPolys moved;
        mime_vec_polys_init(&moved, &gp_allocator);
        for (UINT i = 0; i < job->polys->nsubs; i++) {
            const MimeVecSub *sub = &job->polys->subs[i];
            mime_vec_polys_add_sub(&moved, job->polys->pts + sub->first, sub->count, sub->closed != 0);
        }
        mime_vec_polys_transform(&moved, &shift);
        st = gp_fill_polys(t, &moved, job->rule, gp_smoothing_samples(g->smoothing), job->brush);
        mime_vec_polys_free(&moved);
    } else if (job->mask) {
        MimeVecMask moved;
        mime_vec_mask_init(&moved, &gp_allocator);
        if (mime_vec_mask_copy(&moved, job->mask) == MIME_VEC_OK) {
            mime_vec_mask_translate(&moved, -area.left, -area.top);
            st = gp_fill_mask(t, &moved, job->brush);
        }
        mime_vec_mask_free(&moved);
    } else if (job->image) {
        st = gp_draw_image(t, job->image, job->dst, 3, job->src[0], job->src[1], job->src[2], job->src[3], job->unit,
                           job->attrs);
    }
    GdipDeleteGraphics(tg);
    if (st == Ok) {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        UINT32 *bits = nullptr;
        HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
        HDC mem = dib ? CreateCompatibleDC(nullptr) : nullptr;
        if (dib && bits && mem) {
            for (INT y = 0; y < h; y++) {
                UINT32 *row = bits + (SIZE_T)y * w;
                gp_bitmap_read_argb(b, 0, y, w, row);
                for (INT x = 0; x < w; x++)
                    row[x] = gp_premultiply(row[x]);
            }
            HGDIOBJ old = SelectObject(mem, dib);
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            GdiAlphaBlend(r->dc, area.left, area.top, w, h, mem, 0, 0, w, h, bf);
            SelectObject(mem, old);
        }
        if (mem)
            DeleteDC(mem);
        if (dib)
            DeleteObject(dib);
    }
    gp_bitmap_free(b);
}

static RECT area_of_polys(const MimeVecPolys *polys)
{
    RECT a = {0, 0, 0, 0};
    MimeVecBounds b;
    if (mime_vec_polys_bounds(polys, &b)) {
        a.left = (INT)mime_vec_floorf(b.left) - 1;
        a.top = (INT)mime_vec_floorf(b.top) - 1;
        a.right = (INT)mime_vec_ceilf(b.right) + 1;
        a.bottom = (INT)mime_vec_ceilf(b.bottom) + 1;
    }
    return a;
}

/* polys are in device space. */
static void gdi_fill_polys(GRecorder *r, const MimeVecPolys *polys, INT rule, const GBrush *brush)
{
    if (!polys->nsubs)
        return;
    bounds_polys(r, polys);
    if (!gdi_wanted(r))
        return;
    out_flush(r);
    gdi_clip(r);
    if (brush->type == BrushTypeSolidColor && (brush->color >> 24) == 0xff) {
        gdi_flat_polys(r, polys, rule, brush->color);
        return;
    }
    if (brush->type == BrushTypeSolidColor && (brush->color >> 24) == 0)
        return;
    RasterJob job = {};
    job.polys = polys;
    job.rule = rule;
    job.brush = brush;
    gdi_raster(r, area_of_polys(polys), &job);
}

static void gdi_fill_path(GRecorder *r, const MimeVecPath *path, FillMode mode, const GBrush *brush)
{
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    if (mime_vec_path_flatten(path, gp_graphics_w2d(r->g), MIME_VEC_DEFAULT_FLATNESS, &polys) == MIME_VEC_OK)
        gdi_fill_polys(r, &polys, mode == FillModeWinding ? MIME_VEC_FILL_WINDING : MIME_VEC_FILL_ALTERNATE, brush);
    mime_vec_polys_free(&polys);
}

static void gdi_stroke_path(GRecorder *r, const MimeVecPath *path, const GPen *pen)
{
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    if (gp_pen_widen_ex(pen, path, gp_graphics_w2d(r->g), MIME_VEC_DEFAULT_FLATNESS, r->g->dpi_x, &polys) == Ok)
        gdi_fill_polys(r, &polys, MIME_VEC_FILL_WINDING, pen->brush);
    mime_vec_polys_free(&polys);
}

static void gdi_fill_region(GRecorder *r, const GBrush *brush, const GRegion *region)
{
    MimeVecMask mask;
    mime_vec_mask_init(&mask, &gp_allocator);
    MimeVecRect clip = {0, 0, r->g->width, r->g->height};
    if (gp_region_to_mask(region, gp_graphics_w2d(r->g), &clip, &mask) == Ok && !mime_vec_mask_is_empty(&mask)) {
        MimeVecRect mb = mime_vec_mask_bounds(&mask);
        bounds_add(r, (float)mb.left, (float)mb.top, (float)mb.right, (float)mb.bottom);
        if (gdi_wanted(r)) {
            out_flush(r);
            gdi_clip(r);
            RasterJob job = {};
            job.mask = &mask;
            job.brush = brush;
            RECT area = {mb.left, mb.top, mb.right, mb.bottom};
            gdi_raster(r, area, &job);
        }
    }
    mime_vec_mask_free(&mask);
}

static void gdi_image(GRecorder *r, GImage *image, const GpPointF *dst, float sx, float sy, float sw, float sh,
                      GpUnit unit, struct GAttrs *attrs)
{
    const MimeVecMatrix *m = gp_graphics_w2d(r->g);
    GpPointF c[4] = {dst[0], dst[1], {dst[1].X + dst[2].X - dst[0].X, dst[1].Y + dst[2].Y - dst[0].Y}, dst[2]};
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (INT i = 0; i < 4; i++) {
        MimeVecPoint p = mime_vec_xform(m, c[i].X, c[i].Y);
        if (p.x < x0)
            x0 = p.x;
        if (p.y < y0)
            y0 = p.y;
        if (p.x > x1)
            x1 = p.x;
        if (p.y > y1)
            y1 = p.y;
    }
    bounds_add(r, x0, y0, x1, y1);
    if (!gdi_wanted(r))
        return;
    out_flush(r);
    gdi_clip(r);
    RasterJob job = {};
    job.image = image;
    job.dst = dst;
    job.src[0] = sx;
    job.src[1] = sy;
    job.src[2] = sw;
    job.src[3] = sh;
    job.unit = unit;
    job.attrs = attrs;
    RECT area = {(INT)mime_vec_floorf(x0) - 1, (INT)mime_vec_floorf(y0) - 1, (INT)mime_vec_ceilf(x1) + 1,
                 (INT)mime_vec_ceilf(y1) + 1};
    gdi_raster(r, area, &job);
}

/* ------------------------------------------------------------------ recorded drawing */

static GRecorder *rec_of(GGraphics *g) { return g->meta ? g->meta->rec : nullptr; }

/* the region data without its size and checksum. */
static BOOL region_bytes(const GRegion *region, Wr *w)
{
    UINT size = 0;
    if (GdipGetRegionDataSize((GpRegion *)region, &size) != Ok || size < 8 || !w_room(w, size))
        return FALSE;
    UINT filled = 0;
    if (GdipGetRegionData((GpRegion *)region, w->p, size, &filled) != Ok || filled < 8)
        return FALSE;
    memmove(w->p, w->p + 8, filled - 8);
    w->len = filled - 8;
    return TRUE;
}

/* a solid brush goes in the record as a color, others as an object number. */
static void brush_field(GRecorder *r, const GBrush *b, UINT *flags, UINT32 *field, BOOL *ok)
{
    if (b->type == BrushTypeSolidColor) {
        *flags |= 0x8000;
        *field = b->color;
        *ok = TRUE;
        return;
    }
    Wr w = {};
    *ok = brush_object(&w, b) && !w.bad;
    if (*ok)
        *field = object_number(r, 1, &w);
    w_free(&w);
}

static BOOL path_number(GRecorder *r, const MimeVecPath *path, FillMode mode, UINT *id)
{
    Wr w = {};
    BOOL ok = path_object(&w, path, mode);
    if (ok)
        *id = object_number(r, 3, &w);
    w_free(&w);
    return ok;
}

GpStatus gp_rec_fill_path(GGraphics *g, const MimeVecPath *path, FillMode mode, const GBrush *brush)
{
    GRecorder *r = rec_of(g);
    if (!r || !path->count)
        return Ok;
    sync_state(r);
    UINT flags = 0;
    UINT32 field = 0;
    BOOL ok;
    brush_field(r, brush, &flags, &field, &ok);
    UINT pid;
    if (plus_only(r)) {
        if (!ok || !path_number(r, path, mode, &pid))
            return NotImplemented;
        Wr w = {};
        w_u32(&w, field);
        put_wr(r, EmfPlusRecordTypeFillPath, flags | pid, &w);
    }
    gdi_fill_path(r, path, mode, brush);
    return r->failed ? OutOfMemory : Ok;
}

GpStatus gp_rec_stroke_path(GGraphics *g, const MimeVecPath *path, const GPen *pen)
{
    GRecorder *r = rec_of(g);
    if (!r || !path->count)
        return Ok;
    sync_state(r);
    Wr pw = {};
    if (plus_only(r)) {
        UINT pen_id = 0, pid;
        BOOL ok = pen_object(&pw, pen) && !pw.bad;
        if (ok)
            pen_id = object_number(r, 2, &pw);
        w_free(&pw);
        if (!ok || !path_number(r, path, FillModeAlternate, &pid))
            return NotImplemented;
        Wr w = {};
        w_u32(&w, pen_id);
        put_wr(r, EmfPlusRecordTypeDrawPath, pid, &w);
    }
    gdi_stroke_path(r, path, pen);
    return r->failed ? OutOfMemory : Ok;
}

GpStatus gp_rec_fill_rect(GGraphics *g, const GBrush *brush, float x, float y, float w, float h)
{
    GRecorder *r = rec_of(g);
    if (!r)
        return Ok;
    sync_state(r);
    UINT flags = 0;
    UINT32 field = 0;
    BOOL ok;
    brush_field(r, brush, &flags, &field, &ok);
    if (plus_only(r)) {
        if (!ok)
            return NotImplemented;
        Wr b = {};
        w_u32(&b, field);
        w_u32(&b, 1);
        w_f(&b, x);
        w_f(&b, y);
        w_f(&b, w);
        w_f(&b, h);
        put_wr(r, EmfPlusRecordTypeFillRects, flags, &b);
    }
    MimeVecPath rect;
    mime_vec_path_init(&rect, &gp_allocator);
    if (mime_vec_path_add_rect(&rect, x, y, w, h) == MIME_VEC_OK)
        gdi_fill_path(r, &rect, FillModeWinding, brush);
    mime_vec_path_free(&rect);
    return r->failed ? OutOfMemory : Ok;
}

GpStatus gp_rec_fill_region(GGraphics *g, const GBrush *brush, const GRegion *region)
{
    GRecorder *r = rec_of(g);
    if (!r)
        return Ok;
    sync_state(r);
    UINT flags = 0;
    UINT32 field = 0;
    BOOL ok;
    brush_field(r, brush, &flags, &field, &ok);
    if (plus_only(r)) {
        if (!ok)
            return NotImplemented;
        Wr rw = {};
        UINT rid;
        if (!region_bytes(region, &rw)) {
            w_free(&rw);
            return NotImplemented;
        }
        rid = object_number(r, 4, &rw);
        w_free(&rw);
        Wr w = {};
        w_u32(&w, field);
        put_wr(r, EmfPlusRecordTypeFillRegion, flags | rid, &w);
    }
    gdi_fill_region(r, brush, region);
    return r->failed ? OutOfMemory : Ok;
}

GpStatus gp_rec_clear(GGraphics *g, ARGB color)
{
    GRecorder *r = rec_of(g);
    if (!r)
        return Ok;
    sync_state(r);
    Wr w = {};
    w_u32(&w, color);
    put_wr(r, EmfPlusRecordTypeClear, 0, &w);
    return Ok;
}

GpStatus gp_rec_draw_image(GGraphics *g, GImage *image, const GpPointF *dst, float sx, float sy, float sw, float sh,
                           GpUnit unit, struct GAttrs *attrs)
{
    GRecorder *r = rec_of(g);
    if (!r)
        return Ok;
    sync_state(r);
    gdi_image(r, image, dst, sx, sy, sw, sh, unit, attrs);
    if (!plus_only(r))
        return Ok;
    Wr iw = {};
    UINT iid = 0;
    BOOL ok = image_object(&iw, image) && !iw.bad;
    if (ok)
        iid = object_number(r, 5, &iw);
    w_free(&iw);
    if (!ok)
        return NotImplemented;
    UINT32 attr_id = 0xffffffffu;
    if (attrs) {
        ARGB clamp;
        WrapMode wrap = gp_attrs_wrap(attrs, &clamp);
        Wr aw = {};
        w_u32(&aw, EMFPLUS_VERSION);
        w_u32(&aw, 0);
        w_u32(&aw, wrap);
        w_u32(&aw, clamp);
        w_u32(&aw, 0);
        w_u32(&aw, 0);
        attr_id = object_number(r, 8, &aw);
        w_free(&aw);
    }
    Wr w = {};
    w_u32(&w, attr_id);
    w_u32(&w, unit == UnitWorld ? UnitPixel : unit);
    w_f(&w, sx);
    w_f(&w, sy);
    w_f(&w, sw);
    w_f(&w, sh);
    if (dst[1].Y == dst[0].Y && dst[2].X == dst[0].X) {
        w_f(&w, dst[0].X);
        w_f(&w, dst[0].Y);
        w_f(&w, dst[1].X - dst[0].X);
        w_f(&w, dst[2].Y - dst[0].Y);
        put_wr(r, EmfPlusRecordTypeDrawImage, iid, &w);
    } else {
        w_u32(&w, 3);
        for (INT i = 0; i < 3; i++) {
            w_f(&w, dst[i].X);
            w_f(&w, dst[i].Y);
        }
        put_wr(r, EmfPlusRecordTypeDrawImagePoints, iid, &w);
    }
    return r->failed ? OutOfMemory : Ok;
}

/* ------------------------------------------------------------------ levels and clips */

/* called before a call changes the states in a way the records do not express. */
void gp_rec_sync(GGraphics *g)
{
    GRecorder *r = rec_of(g);
    if (r)
        sync_state(r);
}

void gp_rec_save(GGraphics *g, UINT id)
{
    GRecorder *r = rec_of(g);
    if (!r)
        return;
    sync_state(r);
    Wr w = {};
    w_u32(&w, id);
    put_wr(r, EmfPlusRecordTypeSave, 0, &w);
    level_push(r, id, 0);
}

/* called before the graphics drops the level. restoring a missing level does nothing. */
void gp_rec_restore(GGraphics *g, UINT id, INT kind)
{
    GRecorder *r = rec_of(g);
    if (!r)
        return;
    UINT i = r->level_count;
    while (i > 0 && !(r->levels[i - 1].id == id && r->levels[i - 1].kind == kind))
        i--;
    if (!i)
        return;
    r->clip_stamp++;
    Wr w = {};
    w_u32(&w, id);
    put_wr(r, kind ? EmfPlusRecordTypeEndContainer : EmfPlusRecordTypeRestore, 0, &w);
    r->sent = r->levels[i - 1].state;
    r->level_count = i - 1;
}

void gp_rec_begin_container(GGraphics *g, UINT id, const GpRectF *dst, const GpRectF *src, GpUnit unit)
{
    GRecorder *r = rec_of(g);
    if (!r)
        return;
    Wr w = {};
    if (dst) {
        w_f(&w, dst->X);
        w_f(&w, dst->Y);
        w_f(&w, dst->Width);
        w_f(&w, dst->Height);
        w_f(&w, src->X);
        w_f(&w, src->Y);
        w_f(&w, src->Width);
        w_f(&w, src->Height);
        w_u32(&w, id);
        put_wr(r, EmfPlusRecordTypeBeginContainer, (UINT)unit, &w);
    } else {
        w_u32(&w, id);
        put_wr(r, EmfPlusRecordTypeBeginContainerNoParams, 0, &w);
    }
    level_push(r, id, 1);
    /* a container starts with the world and page of a plain graphics. */
    mime_vec_matrix_identity(&r->sent.world);
    r->sent.page_unit = UnitPixel;
    r->sent.page_scale = 1.0f;
}

GpStatus gp_rec_set_clip(GGraphics *g, const GRegion *region, CombineMode mode)
{
    GRecorder *r = rec_of(g);
    if (!r)
        return Ok;
    r->clip_stamp++;
    sync_state(r);
    /* a rectangle has a record of its own. */
    const GRegionNode *n = region->root;
    if (n->kind == GREGION_RECT) {
        Wr w = {};
        w_f(&w, n->rect.X);
        w_f(&w, n->rect.Y);
        w_f(&w, n->rect.Width);
        w_f(&w, n->rect.Height);
        put_wr(r, EmfPlusRecordTypeSetClipRect, (UINT)mode << 8, &w);
        return Ok;
    }
    Wr rw = {};
    if (!region_bytes(region, &rw)) {
        w_free(&rw);
        return NotImplemented;
    }
    UINT rid = object_number(r, 4, &rw);
    w_free(&rw);
    put_record(r, EmfPlusRecordTypeSetClipRegion, rid | ((UINT)mode << 8), nullptr, 0);
    return Ok;
}

void gp_rec_reset_clip(GGraphics *g)
{
    GRecorder *r = rec_of(g);
    if (r) {
        r->clip_stamp++;
        put_record(r, EmfPlusRecordTypeResetClip, 0, nullptr, 0);
    }
}

void gp_rec_offset_clip(GGraphics *g, float dx, float dy)
{
    GRecorder *r = rec_of(g);
    if (!r)
        return;
    r->clip_stamp++;
    sync_state(r);
    Wr w = {};
    w_f(&w, dx);
    w_f(&w, dy);
    put_wr(r, EmfPlusRecordTypeOffsetClip, 0, &w);
}

void gp_rec_comment(GGraphics *g, UINT size, const BYTE *data)
{
    GRecorder *r = rec_of(g);
    if (r && size)
        put_record(r, EmfPlusRecordTypeComment, 0, data, size);
}

GpStatus gp_rec_get_dc(GGraphics *g, HDC *dc)
{
    GRecorder *r = rec_of(g);
    if (!r)
        return GenericError;
    sync_state(r);
    put_record(r, EmfPlusRecordTypeGetDC, 0, nullptr, 0);
    out_flush(r);
    r->got_dc = TRUE;
    *dc = r->dc;
    return Ok;
}

void gp_rec_release_dc(GGraphics *g)
{
    GRecorder *r = rec_of(g);
    if (r)
        r->got_dc = FALSE;
}

/* plays the enhanced metafile into the recording as its gdi part, after a GetDC record. */
static GpStatus play_into(GGraphics *g, HENHMETAFILE hemf, const MetafileHeader *h)
{
    GRecorder *r = rec_of(g);
    if (!r)
        return InvalidParameter;
    sync_state(r);
    put_record(r, EmfPlusRecordTypeGetDC, 0, nullptr, 0);
    out_flush(r);
    float kx = h->DpiX / HIMETRIC_PER_INCH, ky = h->DpiY / HIMETRIC_PER_INCH;
    const RECTL *f = &h->EmfHeader.rclFrame;
    RECT rect;
    rect.left = (INT)mime_vec_floorf((float)f->left * kx + 0.5f);
    rect.top = (INT)mime_vec_floorf((float)f->top * ky + 0.5f);
    rect.right = (INT)mime_vec_floorf((float)f->right * kx + 0.5f) + 1;
    rect.bottom = (INT)mime_vec_floorf((float)f->bottom * ky + 0.5f) + 1;
    bounds_add(r, (float)rect.left, (float)rect.top, (float)rect.right, (float)rect.bottom);
    return PlayEnhMetaFile(r->dc, hemf, &rect) ? Ok : GenericError;
}

/* ------------------------------------------------------------------ ending */

static void rec_free(GRecorder *r)
{
    for (UINT i = 0; i < 64; i++)
        object_clear(&r->objects[i]);
    gp_free(r->levels);
    w_free(&r->out);
    gp_free(r);
}

void gp_meta_recorder_free(GMetafile *m)
{
    if (m->state == GM_RECORDING && m->rec)
        gp_meta_end_recording(m);
    if (m->rec) {
        rec_free(m->rec);
        m->rec = nullptr;
    }
}

GGraphics *gp_meta_recording_graphics(GMetafile *m)
{
    if (m->state != GM_RECORDING || !m->graphics || m->graphics_taken)
        return nullptr;
    m->graphics_taken = TRUE;
    return m->graphics;
}

/* closes the metafile dc, the resulting enhanced metafile becomes the metafile. */
void gp_meta_end_recording(GMetafile *m)
{
    GRecorder *r = m->rec;
    if (!r || m->state != GM_RECORDING)
        return;
    GGraphics *g = r->g;
    out_flush(r);
    if (r->has_frame) {
        /* the picture bounds are its frame, whatever was drawn. */
        PatBlt(r->dc, r->frame.left, r->frame.top, r->frame.right - r->frame.left, r->frame.bottom - r->frame.top,
               0x00aa0029);
    } else if (r->has_bounds) {
        INT x0 = (INT)mime_vec_floorf(r->bx0), y0 = (INT)mime_vec_floorf(r->by0);
        INT x1 = (INT)mime_vec_ceilf(r->bx1), y1 = (INT)mime_vec_ceilf(r->by1);
        if (x1 > x0 && y1 > y0)
            PatBlt(r->dc, x0, y0, x1 - x0, y1 - y0, 0x00aa0029);
    }
    if (plus_only(r)) {
        put_record(r, EmfPlusRecordTypeEndOfFile, 0, nullptr, 0);
        out_flush(r);
    }
    HENHMETAFILE hemf = CloseEnhMetaFile(r->dc);
    r->dc = nullptr;
    GpStatus st = GenericError;
    m->state = GM_INVALID;
    if (hemf) {
        MetafileHeader h;
        if (gp_meta_header_of_emf(hemf, &h, nullptr) == Ok) {
            m->header = h;
            m->handle = hemf;
            m->owns_handle = TRUE;
            m->state = GM_READY;
            st = Ok;
            if (m->stream) {
                DWORD size;
                const BYTE *bytes = gp_meta_bytes(m, &size);
                ULONG wrote = 0;
                if (!bytes || FAILED(m->stream->lpVtbl->Write(m->stream, bytes, size, &wrote)) || wrote != size)
                    st = GenericError;
                m->stream->lpVtbl->Release(m->stream);
                m->stream = nullptr;
            }
        } else {
            DeleteEnhMetaFile(hemf);
        }
    }
    (void)st;
    /* a graphics nobody took is freed now, one a program holds lives on without a metafile. */
    if (g) {
        g->meta = nullptr;
        g->width = g->height = 0;
        if (!m->graphics_taken)
            GdipDeleteGraphics(g);
    }
    m->graphics = nullptr;
    rec_free(r);
    m->rec = nullptr;
}

/* deleting the recording graphics ends the recording. */
void gp_meta_graphics_deleted(GGraphics *g)
{
    GMetafile *m = g->meta;
    if (m && m->graphics == g)
        gp_meta_end_recording(m);
}

GpStatus gp_meta_graphics_context(GpImage *image, GpGraphics **graphics)
{
    GMetafile *m = gp_meta_cast(image);
    if (!m || m->state < GM_RECORDING)
        return InvalidParameter;
    GGraphics *g = gp_meta_recording_graphics(m);
    if (!g)
        return OutOfMemory;
    *graphics = g;
    return Ok;
}

/* ------------------------------------------------------------------ creation */

/* the frame in hundredths of a millimeter of the device, rounded to whole ones. */
static BOOL frame_mm100(HDC ref, const GpRectF *f, MetafileFrameUnit unit, RECT *out)
{
    float kx, ky;
    switch (unit) {
    case MetafileFrameUnitPixel:
        kx = 100.0f * (float)GetDeviceCaps(ref, HORZSIZE) / (float)GetDeviceCaps(ref, HORZRES);
        ky = 100.0f * (float)GetDeviceCaps(ref, VERTSIZE) / (float)GetDeviceCaps(ref, VERTRES);
        break;
    case MetafileFrameUnitPoint:
        kx = ky = HIMETRIC_PER_INCH / 72.0f;
        break;
    case MetafileFrameUnitInch:
        kx = ky = HIMETRIC_PER_INCH;
        break;
    case MetafileFrameUnitDocument:
        kx = ky = HIMETRIC_PER_INCH / 300.0f;
        break;
    case MetafileFrameUnitMillimeter:
        kx = ky = 100.0f;
        break;
    default:
        kx = ky = 1.0f;
        break;
    }
    out->left = (INT)mime_vec_floorf(f->X * kx + 0.5f);
    out->top = (INT)mime_vec_floorf(f->Y * ky + 0.5f);
    out->right = (INT)mime_vec_floorf((f->X + f->Width) * kx + 0.5f);
    out->bottom = (INT)mime_vec_floorf((f->Y + f->Height) * ky + 0.5f);
    return out->left <= out->right && out->top <= out->bottom;
}

static GpStatus record(HDC ref, EmfType type, const GpRectF *frame, MetafileFrameUnit unit, const WCHAR *description,
                       const WCHAR *file, IStream *stream, GpMetafile **out)
{
    GP_REQUIRE_INIT();
    if (!ref || !out)
        return InvalidParameter;
    if ((UINT)type - 3 > 2 || (UINT)unit - 2 > 5)
        return InvalidParameter;
    *out = nullptr;
    GMetafile *m = gp_meta_new();
    if (!m)
        return GenericError;
    m->state = GM_INVALID;
    RECT mm = {};
    BOOL framed = frame != nullptr;
    if (framed) {
        if (frame->Width < 0.0f || frame->Height < 0.0f || !frame_mm100(ref, frame, unit, &mm)) {
            gp_meta_free(m);
            return GenericError;
        }
    }
    HDC dc = CreateEnhMetaFileW(ref, file, framed ? &mm : nullptr, description);
    if (!dc) {
        gp_meta_free(m);
        return GenericError;
    }
    float res_x = (float)GetDeviceCaps(dc, HORZRES), res_y = (float)GetDeviceCaps(dc, VERTRES);
    float size_x = (float)GetDeviceCaps(dc, HORZSIZE), size_y = (float)GetDeviceCaps(dc, VERTSIZE);
    if (res_x <= 0 || res_y <= 0 || size_x <= 0 || size_y <= 0) {
        HENHMETAFILE junk = CloseEnhMetaFile(dc);
        if (junk)
            DeleteEnhMetaFile(junk);
        gp_meta_free(m);
        return GenericError;
    }
    GRecorder *r = gp_alloc_zero(sizeof(*r));
    if (!r) {
        HENHMETAFILE junk = CloseEnhMetaFile(dc);
        if (junk)
            DeleteEnhMetaFile(junk);
        gp_meta_free(m);
        return OutOfMemory;
    }
    r->mf = m;
    r->dc = dc;
    r->type = type;
    r->has_frame = framed;
    mime_vec_matrix_identity(&r->sent.world);
    m->rec = r;
    m->header.DpiX = res_x / size_x * 25.4f;
    m->header.DpiY = res_y / size_y * 25.4f;
    m->header.Type = type == EmfTypeEmfOnly ? MetafileTypeEmf : (MetafileType)type;
    if (framed) {
        /* the frame in device pixels. */
        float kx = res_x / size_x / 100.0f, ky = res_y / size_y / 100.0f;
        r->frame.left = (INT)mime_vec_ceilf((float)mm.left * kx);
        r->frame.top = (INT)mime_vec_ceilf((float)mm.top * ky);
        r->frame.right = (INT)mime_vec_ceilf((float)mm.right * kx);
        r->frame.bottom = (INT)mime_vec_ceilf((float)mm.bottom * ky);
    }
    float dpi_x = (float)GetDeviceCaps(dc, LOGPIXELSX), dpi_y = (float)GetDeviceCaps(dc, LOGPIXELSY);
    if (dpi_x <= 0)
        dpi_x = 96;
    if (dpi_y <= 0)
        dpi_y = 96;
    r->limit = (UINT)mime_vec_floorf((m->header.DpiX > m->header.DpiY ? m->header.DpiX : m->header.DpiY) + 0.5f);
    if (plus_only(r)) {
        out_reset(r);
        Wr w = {};
        w_u32(&w, EMFPLUS_VERSION);
        w_u32(&w, GetDeviceCaps(dc, TECHNOLOGY) == DT_RASDISPLAY ? 1u : 0u);
        w_u32(&w, (UINT32)dpi_x);
        w_u32(&w, (UINT32)dpi_y);
        put_wr(r, EmfPlusRecordTypeHeader, type == EmfTypeEmfPlusOnly ? 0u : 1u, &w);
        out_flush(r);
        m->header.EmfPlusHeaderSize = EMFPLUS_HEADER_RECORD;
        m->header.LogicalDpiX = (INT)dpi_x;
        m->header.LogicalDpiY = (INT)dpi_y;
    }
    INT gw = framed ? (r->frame.right > 0 ? r->frame.right : 1) : (INT)res_x;
    INT gh = framed ? (r->frame.bottom > 0 ? r->frame.bottom : 1) : (INT)res_y;
    if (gw < (INT)res_x)
        gw = (INT)res_x;
    if (gh < (INT)res_y)
        gh = (INT)res_y;
    r->g = gp_graphics_new_recording(m, dpi_x, dpi_y, gw, gh);
    if (!r->g) {
        gp_meta_free(m);
        return GenericError;
    }
    m->graphics = r->g;
    m->state = GM_RECORDING;
    if (file) {
        m->file_name = gp_dup(file, (wcslen(file) + 1) * sizeof(WCHAR));
    }
    if (stream) {
        stream->lpVtbl->AddRef(stream);
        m->stream = stream;
    }
    *out = m;
    return Ok;
}

static GpStatus rect_i(const GpRect *in, GpRectF *out)
{
    if (!in)
        return InvalidParameter;
    out->X = (float)in->X;
    out->Y = (float)in->Y;
    out->Width = (float)in->Width;
    out->Height = (float)in->Height;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipRecordMetafile(HDC ref, EmfType type, const GpRectF *frame, MetafileFrameUnit unit,
                                              const WCHAR *description, GpMetafile **metafile)
{
    GP_REQUIRE_INIT();
    return record(ref, type, frame, unit, description, nullptr, nullptr, metafile);
}

WINGDIPAPI GpStatus WINAPI GdipRecordMetafileI(HDC ref, EmfType type, const GpRect *frame, MetafileFrameUnit unit,
                                               const WCHAR *description, GpMetafile **metafile)
{
    GpRectF f;
    if (!ref || rect_i(frame, &f) != Ok)
        return InvalidParameter;
    return GdipRecordMetafile(ref, type, &f, unit, description, metafile);
}

WINGDIPAPI GpStatus WINAPI GdipRecordMetafileFileName(const WCHAR *file, HDC ref, EmfType type, const GpRectF *frame,
                                                      MetafileFrameUnit unit, const WCHAR *description,
                                                      GpMetafile **metafile)
{
    GP_REQUIRE_INIT();
    if (!file)
        return InvalidParameter;
    return record(ref, type, frame, unit, description, file, nullptr, metafile);
}

WINGDIPAPI GpStatus WINAPI GdipRecordMetafileFileNameI(const WCHAR *file, HDC ref, EmfType type, const GpRect *frame,
                                                       MetafileFrameUnit unit, const WCHAR *description,
                                                       GpMetafile **metafile)
{
    GpRectF f;
    if (!file || !ref || rect_i(frame, &f) != Ok)
        return InvalidParameter;
    return GdipRecordMetafileFileName(file, ref, type, &f, unit, description, metafile);
}

WINGDIPAPI GpStatus WINAPI GdipRecordMetafileStream(IStream *stream, HDC ref, EmfType type, const GpRectF *frame,
                                                    MetafileFrameUnit unit, const WCHAR *description,
                                                    GpMetafile **metafile)
{
    GP_REQUIRE_INIT();
    if (!stream)
        return InvalidParameter;
    return record(ref, type, frame, unit, description, nullptr, stream, metafile);
}

WINGDIPAPI GpStatus WINAPI GdipRecordMetafileStreamI(IStream *stream, HDC ref, EmfType type, const GpRect *frame,
                                                     MetafileFrameUnit unit, const WCHAR *description,
                                                     GpMetafile **metafile)
{
    GpRectF f;
    if (!stream || !ref || rect_i(frame, &f) != Ok)
        return InvalidParameter;
    return GdipRecordMetafileStream(stream, ref, type, &f, unit, description, metafile);
}

/* ------------------------------------------------------------------ conversion to emf+ */

/* wraps a windows metafile as an enhanced one, through the placeable header of its box. */
static GMetafile *wmf_as_enhanced(const GMetafile *source)
{
    WmfPlaceableFileHeader placeable;
    if (!gp_meta_placeable_for(source, &placeable))
        return nullptr;
    HMETAFILE wmf = (HMETAFILE)source->handle;
    UINT size = GetMetaFileBitsEx(wmf, 0, nullptr);
    BYTE *bits = size ? gp_alloc(size) : nullptr;
    if (!bits)
        return nullptr;
    HENHMETAFILE emf = nullptr;
    if (GetMetaFileBitsEx(wmf, size, bits) == size)
        emf = gp_emf_from_wmf_data(wmf, &placeable, bits, size, source->compat);
    gp_free(bits);
    GMetafile *made = emf ? gp_meta_new() : nullptr;
    if (!made) {
        if (emf)
            DeleteEnhMetaFile(emf);
        return nullptr;
    }
    if (gp_meta_init_emf(made, emf, TRUE) != Ok) {
        gp_meta_free(made);
        return nullptr;
    }
    return made;
}

/*
 * records the picture again as the requested type. the source's gdi records are kept as they are, in the gdi part
 * of the new metafile after a GetDC record, and the frame is the source's pixel bounds on the screen. how gdi+
 * handles the gdi records here is a guess.
 */
static GpStatus convert(const GpGraphics *graphics, GpMetafile *metafile, BOOL *success, const WCHAR *file,
                        IStream *stream, EmfType type, const WCHAR *description, GpMetafile **out)
{
    if (success)
        *success = FALSE;
    GGraphics *g = gp_graphics_cast(graphics);
    GMetafile *source = gp_meta_cast(metafile);
    if (!g || !source || !out || source->state < GM_RECORDING || source->header.Type >= MetafileTypeEmfPlusOnly)
        return InvalidParameter;
    if ((UINT)type - 3 > 2)
        return InvalidParameter;
    if (source->state != GM_READY)
        return WrongState;
    GMetafile *enhanced = source;
    GMetafile *made_from_wmf = nullptr;
    if (source->header.Type < MetafileTypeEmf) {
        made_from_wmf = wmf_as_enhanced(source);
        if (!made_from_wmf)
            return GenericError;
        enhanced = made_from_wmf;
    }
    float bx, by, bw, bh;
    gp_meta_real_bounds(enhanced, &bx, &by, &bw, &bh);
    GpRectF frame = {bx, by, bw, bh};
    HDC screen = GetDC(nullptr);
    GpMetafile *recording = nullptr;
    GpStatus st = record(screen, type, &frame, MetafileFrameUnitPixel, description, file, stream, &recording);
    if (screen)
        ReleaseDC(nullptr, screen);
    if (st == Ok) {
        GMetafile *m = gp_meta_cast(recording);
        GGraphics *rg = gp_meta_recording_graphics(m);
        st = rg ? play_into(rg, (HENHMETAFILE)enhanced->handle, &enhanced->header) : GenericError;
        /* deleting the graphics completes the metafile. */
        if (rg)
            GdipDeleteGraphics(rg);
        if (st == Ok && m->state == GM_READY) {
            *out = recording;
            if (success)
                *success = TRUE;
        } else {
            gp_meta_free(m);
            st = GenericError;
        }
    }
    gp_meta_free(made_from_wmf);
    return st;
}


WINGDIPAPI GpStatus WINAPI GdipConvertToEmfPlus(const GpGraphics *graphics, GpMetafile *metafile, BOOL *success,
                                                EmfType type, const WCHAR *description, GpMetafile **out)
{
    return convert(graphics, metafile, success, nullptr, nullptr, type, description, out);
}

WINGDIPAPI GpStatus WINAPI GdipConvertToEmfPlusToFile(const GpGraphics *graphics, GpMetafile *metafile, BOOL *success,
                                                      const WCHAR *file, EmfType type, const WCHAR *description,
                                                      GpMetafile **out)
{
    GP_REQUIRE_INIT();
    if (!file)
        return InvalidParameter;
    return convert(graphics, metafile, success, file, nullptr, type, description, out);
}

WINGDIPAPI GpStatus WINAPI GdipConvertToEmfPlusToStream(const GpGraphics *graphics, GpMetafile *metafile,
                                                        BOOL *success, IStream *stream, EmfType type,
                                                        const WCHAR *description, GpMetafile **out)
{
    GP_REQUIRE_INIT();
    if (!stream)
        return InvalidParameter;
    return convert(graphics, metafile, success, nullptr, stream, type, description, out);
}

/* ------------------------------------------------------------------ down level */

/*
 * the placeable header for a windows metafile made from an enhanced one. the box is the enhanced frame scaled by
 * half the device pixels per hundredth of a millimeter, and the inch unit is that scale for an inch. both are
 * guesses that match gdi+'s output. an unreadable header gets a 2000 unit square box and 96 per inch.
 */
static void wmf_placeable_for(HENHMETAFILE emf, BYTE out[PLACEABLE_SIZE])
{
    INT16 box[4] = {0, 0, 2000, 2000}, inch = 96;
    ENHMETAHEADER eh;
    if (GetEnhMetaFileHeader(emf, sizeof(eh), &eh) >= sizeof(eh) && eh.szlMillimeters.cx > 0 && eh.szlMillimeters.cy > 0) {
        float across = (float)eh.szlDevice.cx / (float)eh.szlMillimeters.cx;
        float down = (float)eh.szlDevice.cy / (float)eh.szlMillimeters.cy;
        float scale = (across + down) * 0.25f / 100.0f;
        box[0] = (INT16)((float)eh.rclFrame.left * scale);
        box[1] = (INT16)((float)eh.rclFrame.top * scale);
        box[2] = (INT16)((float)eh.rclFrame.right * scale);
        box[3] = (INT16)((float)eh.rclFrame.bottom * scale);
        inch = (INT16)(scale * HIMETRIC_PER_INCH);
    }
    UINT32 key = PLACEABLE_KEY, reserved = 0;
    INT16 hmf = 0;
    memcpy(out, &key, 4);
    memcpy(out + 4, &hmf, 2);
    memcpy(out + 6, box, 8);
    memcpy(out + 14, &inch, 2);
    memcpy(out + 16, &reserved, 4);
    UINT16 sum = 0, word;
    for (INT i = 0; i < 10; i++) {
        memcpy(&word, out + 2 * i, 2);
        sum ^= word;
    }
    memcpy(out + 20, &sum, 2);
}

WINGDIPAPI UINT WINAPI GdipEmfToWmfBits(HENHMETAFILE emf, UINT size, LPBYTE buffer, INT map_mode, INT flags)
{
    if (map_mode < MM_TEXT || map_mode > MM_ANISOTROPIC || !emf || GetObjectType(emf) != OBJ_ENHMETAFILE)
        return 0;
    BOOL placeable = (flags & EmfToWmfBitsFlagsIncludePlaceable) != 0;
    UINT lead = placeable ? PLACEABLE_SIZE : 0;
    if (buffer && size < lead)
        return 0;
    HDC ref = GetDC(nullptr);
    UINT bits = GetWinMetaFileBits(emf, buffer ? size - lead : 0, buffer ? buffer + lead : nullptr, map_mode, ref);
    if (ref)
        ReleaseDC(nullptr, ref);
    if (!bits)
        return 0;
    if (buffer && placeable)
        wmf_placeable_for(emf, buffer);
    return bits + lead;
}

/* ------------------------------------------------------------------ rasterization limit */

WINGDIPAPI GpStatus WINAPI GdipSetMetafileDownLevelRasterizationLimit(GpMetafile *metafile, UINT limit)
{
    GMetafile *m = gp_meta_cast(metafile);
    if (!m || m->state < GM_RECORDING)
        return InvalidParameter;
    if (limit >= 1 && limit <= 9)
        return InvalidParameter;
    if (m->state != GM_RECORDING || !m->rec)
        return WrongState;
    GRecorder *r = m->rec;
    if (limit)
        r->limit = limit;
    else
        r->limit = (UINT)mime_vec_floorf((m->header.DpiX > m->header.DpiY ? m->header.DpiX : m->header.DpiY) + 0.5f);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetMetafileDownLevelRasterizationLimit(const GpMetafile *metafile, UINT *limit)
{
    GMetafile *m = gp_meta_cast(metafile);
    if (!m || m->state < GM_RECORDING)
        return InvalidParameter;
    if (!limit)
        return InvalidParameter;
    if (m->state != GM_RECORDING || !m->rec)
        return WrongState;
    *limit = m->rec->limit;
    return Ok;
}
