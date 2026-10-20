/*
 * the emf+ records played onto a graphics: the objects a metafile defines, and each drawing, state and clip
 * record turned into the calls of the api on the graphics of the player.
 *
 * the layouts are those of the emf+ file format. what the records do when they are not what the format says is
 * from reading the library: a record with less data than it needs is skipped, an object that cannot be made leaves
 * its slot empty, and a record that names an object that is not there draws nothing.
 */
#include "meta_private.h"

typedef struct Rd {
    const BYTE *p;
    SIZE_T left;
    BOOL bad;
} Rd;

static UINT32 rd_u32(Rd *r)
{
    UINT32 v = 0;
    if (r->left < 4) {
        r->bad = TRUE;
        return 0;
    }
    memcpy(&v, r->p, 4);
    r->p += 4;
    r->left -= 4;
    return v;
}

static float rd_f(Rd *r)
{
    float v = 0;
    if (r->left < 4) {
        r->bad = TRUE;
        return 0;
    }
    memcpy(&v, r->p, 4);
    r->p += 4;
    r->left -= 4;
    return v;
}

static const BYTE *rd_bytes(Rd *r, SIZE_T n)
{
    if (r->left < n) {
        r->bad = TRUE;
        return nullptr;
    }
    const BYTE *p = r->p;
    r->p += n;
    r->left -= n;
    return p;
}

static void rd_align(Rd *r, const BYTE *start)
{
    SIZE_T used = (SIZE_T)(r->p - start);
    SIZE_T pad = (4 - (used & 3)) & 3;
    if (pad > r->left)
        pad = r->left;
    r->p += pad;
    r->left -= pad;
}

static UINT32 le32(const BYTE *p)
{
    UINT32 v;
    memcpy(&v, p, 4);
    return v;
}

/* ------------------------------------------------------------------ points and rectangles */

#define PLUS_COMPRESSED 0x4000
#define PLUS_RELATIVE 0x0800
#define PLUS_SOLID 0x8000
#define PLUS_MAX_ITEMS (1u << 22)

/* one coordinate of a relative point: a byte of 7 bits, or two bytes of 15, both signed. */
static BOOL rd_relative_value(Rd *r, INT *out)
{
    if (r->left < 1)
        return FALSE;
    BYTE b = r->p[0];
    if (b & 0x80) {
        INT v = b & 0x7f;
        if (v & 0x40)
            v |= ~0x7f;
        *out = v;
        r->p++;
        r->left--;
        return TRUE;
    }
    if (r->left < 2)
        return FALSE;
    INT v = ((b & 0x7f) << 8) | r->p[1];
    if (v & 0x4000)
        v |= ~0x7fff;
    *out = v;
    r->p += 2;
    r->left -= 2;
    return TRUE;
}

/* the points of a record: whole, in 16 bits, or as steps from the one before. */
static GpPointF *read_points(Rd *r, UINT count, UINT flags)
{
    if (!count || count > PLUS_MAX_ITEMS)
        return nullptr;
    GpPointF *pts = gp_alloc(sizeof(GpPointF) * count);
    if (!pts)
        return nullptr;
    const BYTE *start = r->p;
    if (flags & PLUS_RELATIVE) {
        INT x = 0, y = 0;
        for (UINT i = 0; i < count; i++) {
            INT dx, dy;
            if (!rd_relative_value(r, &dx) || !rd_relative_value(r, &dy)) {
                gp_free(pts);
                r->bad = TRUE;
                return nullptr;
            }
            x = (INT16)(x + dx);
            y = (INT16)(y + dy);
            pts[i].X = (float)x;
            pts[i].Y = (float)y;
        }
        rd_align(r, start);
    } else if (flags & PLUS_COMPRESSED) {
        const BYTE *b = rd_bytes(r, (SIZE_T)count * 4);
        if (!b) {
            gp_free(pts);
            return nullptr;
        }
        for (UINT i = 0; i < count; i++) {
            INT16 xy[2];
            memcpy(xy, b + i * 4, 4);
            pts[i].X = (float)xy[0];
            pts[i].Y = (float)xy[1];
        }
    } else {
        const BYTE *b = rd_bytes(r, (SIZE_T)count * 8);
        if (!b) {
            gp_free(pts);
            return nullptr;
        }
        memcpy(pts, b, (SIZE_T)count * 8);
    }
    return pts;
}

static GpRectF *read_rects(Rd *r, UINT count, UINT flags)
{
    if (!count || count > PLUS_MAX_ITEMS)
        return nullptr;
    GpRectF *rects = gp_alloc(sizeof(GpRectF) * count);
    if (!rects)
        return nullptr;
    if (flags & PLUS_COMPRESSED) {
        const BYTE *b = rd_bytes(r, (SIZE_T)count * 8);
        if (!b) {
            gp_free(rects);
            return nullptr;
        }
        for (UINT i = 0; i < count; i++) {
            INT16 v[4];
            memcpy(v, b + i * 8, 8);
            rects[i].X = (float)v[0];
            rects[i].Y = (float)v[1];
            rects[i].Width = (float)v[2];
            rects[i].Height = (float)v[3];
        }
    } else {
        const BYTE *b = rd_bytes(r, (SIZE_T)count * 16);
        if (!b) {
            gp_free(rects);
            return nullptr;
        }
        memcpy(rects, b, (SIZE_T)count * 16);
    }
    return rects;
}

/* ------------------------------------------------------------------ objects */

static void object_release(PlayObject *o)
{
    if (!o->obj)
        return;
    switch (o->type) {
    case PLAY_BRUSH:
        GdipDeleteBrush(o->obj);
        break;
    case PLAY_PEN:
        GdipDeletePen(o->obj);
        break;
    case PLAY_PATH:
        GdipDeletePath(o->obj);
        break;
    case PLAY_REGION:
        GdipDeleteRegion(o->obj);
        break;
    case PLAY_IMAGE:
        GdipDisposeImage(o->obj);
        break;
    case PLAY_FONT:
        GdipDeleteFont(o->obj);
        break;
    case PLAY_STRING_FORMAT:
        GdipDeleteStringFormat(o->obj);
        break;
    case PLAY_ATTRS:
        GdipDisposeImageAttributes(o->obj);
        break;
    case PLAY_CUSTOM_CAP:
        GdipDeleteCustomLineCap(o->obj);
        break;
    default:
        break;
    }
    o->obj = nullptr;
    o->type = 0;
}

void gp_plus_player_init(GPlayer *p)
{
    memset(p->objects, 0, sizeof(p->objects));
    p->saves = nullptr;
    p->save_count = p->save_cap = 0;
    p->piece = nullptr;
    p->piece_size = p->piece_got = 0;
}

void gp_plus_player_free(GPlayer *p)
{
    for (UINT i = 0; i < 256; i++)
        object_release(&p->objects[i]);
    gp_free(p->saves);
    gp_free(p->piece);
    p->saves = nullptr;
    p->piece = nullptr;
}

void gp_plus_player_unwind(GPlayer *p)
{
    p->save_count = 0;
}

/* the object a record names, when it has the type the record needs. */
static void *object_of(GPlayer *p, UINT id, INT type)
{
    if (id >= 64)
        return nullptr;
    PlayObject *o = &p->objects[id];
    return o->type == type ? o->obj : nullptr;
}

/* a path: points, then the types of its points, either may be packed. */
static GpStatus make_path(const BYTE *d, SIZE_T n, GpPath **out)
{
    Rd r = {d, n, FALSE};
    UINT32 version = rd_u32(&r), count = rd_u32(&r), flags = rd_u32(&r);
    if (r.bad || (version & 0xfffff000u) != EMFPLUS_VERSION_BASE || !count || count > PLUS_MAX_ITEMS)
        return InvalidParameter;
    const BYTE *start = r.p;
    GpPointF *pts = read_points(&r, count, flags);
    if (!pts)
        return InvalidParameter;
    BYTE *types = gp_alloc(count);
    if (!types) {
        gp_free(pts);
        return OutOfMemory;
    }
    BOOL ok = TRUE;
    if (flags & 0x1000) {
        /* runs of lines or beziers, or single types */
        const BYTE *tstart = r.p;
        UINT filled = 0;
        while (filled < count && r.left) {
            BYTE v = r.p[0];
            r.p++;
            r.left--;
            if (v & 0x40) {
                UINT run = v & 0x3f;
                if (run > count - filled) {
                    ok = FALSE;
                    break;
                }
                memset(types + filled, (v >> 7) ? PathPointTypeBezier : PathPointTypeLine, run);
                filled += run;
            } else {
                types[filled++] = v;
            }
        }
        ok = ok && filled == count;
        rd_align(&r, tstart);
    } else {
        const BYTE *t = rd_bytes(&r, count);
        ok = t != nullptr;
        if (ok)
            memcpy(types, t, count);
    }
    (void)start;
    GpPath *path = nullptr;
    GpStatus st = InvalidParameter;
    if (ok) {
        st = GdipCreatePath2(pts, types, (INT)count, (flags & 0x2000) ? FillModeWinding : FillModeAlternate, &path);
    }
    gp_free(pts);
    gp_free(types);
    if (st != Ok)
        return st;
    *out = path;
    return Ok;
}

static GpStatus make_region(const BYTE *d, SIZE_T n, GpRegion **out)
{
    /* the library takes the region with a size and a checksum in front */
    static UINT32 table[256];
    static BOOL ready;
    if (!ready) {
        for (UINT i = 0; i < 256; i++) {
            UINT c = i;
            for (INT k = 0; k < 8; k++)
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = TRUE;
    }
    BYTE *buf = gp_alloc(n + 8);
    if (!buf)
        return OutOfMemory;
    UINT32 crc = 0xffffffffu;
    for (SIZE_T i = 0; i < n; i++)
        crc = table[(crc ^ d[i]) & 255] ^ (crc >> 8);
    crc = ~crc;
    UINT32 body = (UINT32)n;
    memcpy(buf, &body, 4);
    memcpy(buf + 4, &crc, 4);
    memcpy(buf + 8, d, n);
    GpStatus st = GdipCreateRegionRgnData(buf, (INT)(n + 8), out);
    gp_free(buf);
    return st;
}

static GpStatus make_image(const BYTE *d, SIZE_T n, GpImage **out);

/* a brush of any of the five kinds. the matrix is the optional one of the kinds that have it. */
static void read_matrix(Rd *r, GMatrix *m)
{
    m->tag = GP_TAG_MATRIX;
    float v[6];
    for (INT i = 0; i < 6; i++)
        v[i] = rd_f(r);
    if (!r->bad) {
        m->M[0] = v[0];
        m->M[1] = v[1];
        m->M[2] = v[2];
        m->M[3] = v[3];
        m->M[4] = v[4];
        m->M[5] = v[5];
    }
}

enum {
    BRUSH_PATH = 0x1,
    BRUSH_TRANSFORM = 0x2,
    BRUSH_PRESET = 0x4,
    BRUSH_BLEND_H = 0x8,
    BRUSH_BLEND_V = 0x10,
    BRUSH_FOCUS = 0x40,
    BRUSH_GAMMA = 0x80,
    BRUSH_NO_TRANSFORM = 0x100,
};

static GpStatus make_brush(const BYTE *d, SIZE_T n, GpBrush **out)
{
    Rd r = {d, n, FALSE};
    UINT32 version = rd_u32(&r), type = rd_u32(&r);
    if (r.bad || (version & 0xfffff000u) != EMFPLUS_VERSION_BASE)
        return InvalidParameter;
    switch (type) {
    case BrushTypeSolidColor: {
        ARGB color = rd_u32(&r);
        if (r.bad)
            return InvalidParameter;
        return GdipCreateSolidFill(color, (GpSolidFill **)out);
    }
    case BrushTypeHatchFill: {
        UINT32 style = rd_u32(&r), fore = rd_u32(&r), back = rd_u32(&r);
        if (r.bad)
            return InvalidParameter;
        return GdipCreateHatchBrush((HatchStyle)style, fore, back, (GpHatch **)out);
    }
    case BrushTypeTextureFill: {
        UINT32 flags = rd_u32(&r), wrap = rd_u32(&r);
        GMatrix m;
        BOOL has = (flags & BRUSH_TRANSFORM) != 0;
        if (has)
            read_matrix(&r, &m);
        if (r.bad)
            return InvalidParameter;
        GpImage *image;
        GpStatus st = make_image(r.p, r.left, &image);
        if (st != Ok)
            return st;
        GpTexture *tex;
        st = GdipCreateTexture(image, (GpWrapMode)wrap, &tex);
        GdipDisposeImage(image);
        if (st != Ok)
            return st;
        if (has)
            GdipSetTextureTransform(tex, &m);
        *out = tex;
        return Ok;
    }
    case BrushTypeLinearGradient: {
        UINT32 flags = rd_u32(&r), wrap = rd_u32(&r);
        GpRectF rect;
        rect.X = rd_f(&r);
        rect.Y = rd_f(&r);
        rect.Width = rd_f(&r);
        rect.Height = rd_f(&r);
        ARGB c1 = rd_u32(&r), c2 = rd_u32(&r);
        rd_u32(&r);
        rd_u32(&r);
        if (r.bad)
            return InvalidParameter;
        GpLineGradient *lg;
        GpStatus st = GdipCreateLineBrushFromRect(&rect, c1, c2, LinearGradientModeHorizontal, (GpWrapMode)wrap, &lg);
        if (st != Ok)
            return st;
        if (flags & BRUSH_TRANSFORM) {
            GMatrix m;
            read_matrix(&r, &m);
            if (!r.bad)
                GdipSetLineTransform(lg, &m);
        }
        if (flags & BRUSH_PRESET) {
            UINT32 count = rd_u32(&r);
            const BYTE *pos = rd_bytes(&r, (SIZE_T)count * 4);
            const BYTE *col = pos ? rd_bytes(&r, (SIZE_T)count * 4) : nullptr;
            if (col && count >= 2)
                GdipSetLinePresetBlend(lg, (const ARGB *)col, (const REAL *)pos, (INT)count);
        }
        if (flags & BRUSH_BLEND_H) {
            UINT32 count = rd_u32(&r);
            const BYTE *pos = rd_bytes(&r, (SIZE_T)count * 4);
            const BYTE *fac = pos ? rd_bytes(&r, (SIZE_T)count * 4) : nullptr;
            if (fac && count >= 1)
                GdipSetLineBlend(lg, (const REAL *)fac, (const REAL *)pos, (INT)count);
        }
        GdipSetLineGammaCorrection(lg, (flags & BRUSH_GAMMA) != 0);
        *out = lg;
        return Ok;
    }
    case BrushTypePathGradient: {
        UINT32 flags = rd_u32(&r), wrap = rd_u32(&r);
        ARGB center_color = rd_u32(&r);
        GpPointF center;
        center.X = rd_f(&r);
        center.Y = rd_f(&r);
        UINT32 scount = rd_u32(&r);
        if (r.bad || scount > 4096)
            return InvalidParameter;
        const BYTE *colors = rd_bytes(&r, (SIZE_T)scount * 4);
        if (!colors)
            return InvalidParameter;
        GpPathGradient *pg;
        GpStatus st;
        if (flags & BRUSH_PATH) {
            UINT32 size = rd_u32(&r);
            const BYTE *pd = rd_bytes(&r, size);
            GpPath *path;
            if (!pd || make_path(pd, size, &path) != Ok)
                return InvalidParameter;
            st = GdipCreatePathGradientFromPath(path, &pg);
            GdipDeletePath(path);
            if (st == Ok)
                GdipSetPathGradientWrapMode(pg, (GpWrapMode)wrap);
        } else {
            UINT32 count = rd_u32(&r);
            GpPointF *pts = r.bad ? nullptr : read_points(&r, count, 0);
            if (!pts)
                return InvalidParameter;
            st = GdipCreatePathGradient(pts, (INT)count, (GpWrapMode)wrap, &pg);
            gp_free(pts);
        }
        if (st != Ok)
            return st;
        GdipSetPathGradientCenterColor(pg, center_color);
        GdipSetPathGradientCenterPoint(pg, &center);
        if (scount) {
            INT cnt = (INT)scount;
            GdipSetPathGradientSurroundColorsWithCount(pg, (const ARGB *)colors, &cnt);
        }
        if (flags & BRUSH_TRANSFORM) {
            GMatrix m;
            read_matrix(&r, &m);
            if (!r.bad)
                GdipSetPathGradientTransform(pg, &m);
        }
        if (flags & BRUSH_PRESET) {
            UINT32 count = rd_u32(&r);
            const BYTE *pos = rd_bytes(&r, (SIZE_T)count * 4);
            const BYTE *col = pos ? rd_bytes(&r, (SIZE_T)count * 4) : nullptr;
            if (col && count >= 2)
                GdipSetPathGradientPresetBlend(pg, (const ARGB *)col, (const REAL *)pos, (INT)count);
        }
        if (flags & BRUSH_BLEND_H) {
            UINT32 count = rd_u32(&r);
            const BYTE *pos = rd_bytes(&r, (SIZE_T)count * 4);
            const BYTE *fac = pos ? rd_bytes(&r, (SIZE_T)count * 4) : nullptr;
            if (fac && count >= 1)
                GdipSetPathGradientBlend(pg, (const REAL *)fac, (const REAL *)pos, (INT)count);
        }
        if (flags & BRUSH_FOCUS) {
            rd_u32(&r);
            float fx = rd_f(&r), fy = rd_f(&r);
            if (!r.bad)
                GdipSetPathGradientFocusScales(pg, fx, fy);
        }
        GdipSetPathGradientGammaCorrection(pg, (flags & BRUSH_GAMMA) != 0);
        *out = pg;
        return Ok;
    }
    default:
        return InvalidParameter;
    }
}

enum {
    PEN_TRANSFORM = 0x1,
    PEN_START_CAP = 0x2,
    PEN_END_CAP = 0x4,
    PEN_JOIN = 0x8,
    PEN_MITER = 0x10,
    PEN_STYLE = 0x20,
    PEN_DASH_CAP = 0x40,
    PEN_DASH_OFFSET = 0x80,
    PEN_DASHES = 0x100,
    PEN_ALIGN = 0x200,
    PEN_COMPOUND = 0x400,
    PEN_START_CUSTOM = 0x800,
    PEN_END_CUSTOM = 0x1000,
};

static GpStatus make_cap(const BYTE *d, SIZE_T n, GpCustomLineCap **out);

static GpStatus make_pen(const BYTE *d, SIZE_T n, GpPen **out)
{
    Rd r = {d, n, FALSE};
    UINT32 version = rd_u32(&r);
    rd_u32(&r);
    UINT32 flags = rd_u32(&r), unit = rd_u32(&r);
    float width = rd_f(&r);
    if (r.bad || (version & 0xfffff000u) != EMFPLUS_VERSION_BASE)
        return InvalidParameter;
    GMatrix m = {};
    BOOL has_matrix = FALSE;
    UINT32 start_cap = 0, end_cap = 0, join = 0, style = 0, dash_cap = 0, align = 0;
    float miter = 10.0f, dash_offset = 0;
    const BYTE *dashes = nullptr, *compound = nullptr;
    UINT32 dash_count = 0, compound_count = 0;
    GpCustomLineCap *cap_start = nullptr, *cap_end = nullptr;
    if (flags & PEN_TRANSFORM) {
        read_matrix(&r, &m);
        has_matrix = !r.bad;
    }
    if (flags & PEN_START_CAP)
        start_cap = rd_u32(&r);
    if (flags & PEN_END_CAP)
        end_cap = rd_u32(&r);
    if (flags & PEN_JOIN)
        join = rd_u32(&r);
    if (flags & PEN_MITER)
        miter = rd_f(&r);
    if (flags & PEN_STYLE)
        style = rd_u32(&r);
    if (flags & PEN_DASH_CAP)
        dash_cap = rd_u32(&r);
    if (flags & PEN_DASH_OFFSET)
        dash_offset = rd_f(&r);
    if (flags & PEN_DASHES) {
        dash_count = rd_u32(&r);
        dashes = dash_count > 4096 ? nullptr : rd_bytes(&r, (SIZE_T)dash_count * 4);
        if (!dashes)
            r.bad = TRUE;
    }
    if (flags & PEN_ALIGN)
        align = rd_u32(&r);
    if (flags & PEN_COMPOUND) {
        compound_count = rd_u32(&r);
        compound = compound_count > 4096 ? nullptr : rd_bytes(&r, (SIZE_T)compound_count * 4);
        if (!compound)
            r.bad = TRUE;
    }
    for (INT k = 0; k < 2 && !r.bad; k++) {
        UINT32 bit = k ? PEN_END_CUSTOM : PEN_START_CUSTOM;
        if (!(flags & bit))
            continue;
        UINT32 size = rd_u32(&r);
        const BYTE *cd = size >= 8 ? rd_bytes(&r, size) : nullptr;
        if (!cd || make_cap(cd, size, k ? &cap_end : &cap_start) != Ok)
            r.bad = TRUE;
    }
    if (r.bad) {
        if (cap_start)
            GdipDeleteCustomLineCap(cap_start);
        if (cap_end)
            GdipDeleteCustomLineCap(cap_end);
        return InvalidParameter;
    }
    GpBrush *brush;
    GpStatus st = make_brush(r.p, r.left, &brush);
    if (st != Ok) {
        if (cap_start)
            GdipDeleteCustomLineCap(cap_start);
        if (cap_end)
            GdipDeleteCustomLineCap(cap_end);
        return st;
    }
    GpPen *pen;
    st = GdipCreatePen2(brush, width, (GpUnit)unit, &pen);
    GdipDeleteBrush(brush);
    if (st == Ok) {
        if (has_matrix)
            GdipSetPenTransform(pen, &m);
        if (flags & PEN_START_CAP)
            GdipSetPenStartCap(pen, (GpLineCap)start_cap);
        if (flags & PEN_END_CAP)
            GdipSetPenEndCap(pen, (GpLineCap)end_cap);
        if (flags & PEN_JOIN)
            GdipSetPenLineJoin(pen, (GpLineJoin)join);
        if (flags & PEN_MITER)
            GdipSetPenMiterLimit(pen, miter);
        if (flags & PEN_STYLE)
            GdipSetPenDashStyle(pen, (GpDashStyle)style);
        if (flags & PEN_DASH_CAP)
            GdipSetPenDashCap197819(pen, (GpDashCap)dash_cap);
        if (flags & PEN_DASH_OFFSET)
            GdipSetPenDashOffset(pen, dash_offset);
        if (dashes && dash_count)
            GdipSetPenDashArray(pen, (const REAL *)dashes, (INT)dash_count);
        if (flags & PEN_ALIGN)
            GdipSetPenMode(pen, (GpPenAlignment)align);
        if (compound && compound_count)
            GdipSetPenCompoundArray(pen, (const REAL *)compound, (INT)compound_count);
        if (cap_start)
            GdipSetPenCustomStartCap(pen, cap_start);
        if (cap_end)
            GdipSetPenCustomEndCap(pen, cap_end);
        *out = pen;
    }
    /* the pen keeps copies */
    if (cap_start)
        GdipDeleteCustomLineCap(cap_start);
    if (cap_end)
        GdipDeleteCustomLineCap(cap_end);
    return st;
}

/* a custom line cap: an arrow, or the paths of a cap that a program made. */
static GpStatus make_cap(const BYTE *d, SIZE_T n, GpCustomLineCap **out)
{
    Rd r = {d, n, FALSE};
    UINT32 version = rd_u32(&r), type = rd_u32(&r);
    if (r.bad || (version & 0xfffff000u) != EMFPLUS_VERSION_BASE)
        return InvalidParameter;
    if (type == 1) {
        float width = rd_f(&r), height = rd_f(&r), inset = rd_f(&r);
        UINT32 fill = rd_u32(&r), start = rd_u32(&r), end = rd_u32(&r), join = rd_u32(&r);
        float miter = rd_f(&r), scale = rd_f(&r);
        (void)miter;
        if (r.bad)
            return InvalidParameter;
        GpAdjustableArrowCap *cap;
        GpStatus st = GdipCreateAdjustableArrowCap(height, width, fill != 0, &cap);
        if (st != Ok)
            return st;
        GdipSetAdjustableArrowCapMiddleInset(cap, inset);
        GdipSetCustomLineCapStrokeCaps(cap, (GpLineCap)start, (GpLineCap)end);
        GdipSetCustomLineCapStrokeJoin(cap, (GpLineJoin)join);
        GdipSetCustomLineCapWidthScale(cap, scale);
        *out = cap;
        return Ok;
    }
    if (type != 0)
        return InvalidParameter;
    UINT32 flags = rd_u32(&r), base = rd_u32(&r);
    float inset = rd_f(&r);
    UINT32 start = rd_u32(&r), end = rd_u32(&r), join = rd_u32(&r);
    rd_f(&r);
    float scale = rd_f(&r);
    rd_bytes(&r, 16);
    if (r.bad)
        return InvalidParameter;
    GpPath *fill = nullptr, *line = nullptr;
    for (INT k = 0; k < 2; k++) {
        if (!(flags & (k ? 2u : 1u)))
            continue;
        UINT32 size = rd_u32(&r);
        const BYTE *pd = rd_bytes(&r, size);
        if (!pd || make_path(pd, size, k ? &line : &fill) != Ok) {
            GdipDeletePath(fill);
            GdipDeletePath(line);
            return InvalidParameter;
        }
    }
    GpCustomLineCap *cap;
    GpStatus st = GdipCreateCustomLineCap(fill, line, (GpLineCap)base, inset, &cap);
    GdipDeletePath(fill);
    GdipDeletePath(line);
    if (st != Ok)
        return st;
    GdipSetCustomLineCapStrokeCaps(cap, (GpLineCap)start, (GpLineCap)end);
    GdipSetCustomLineCapStrokeJoin(cap, (GpLineJoin)join);
    GdipSetCustomLineCapWidthScale(cap, scale);
    *out = cap;
    return Ok;
}

static GpStatus make_image(const BYTE *d, SIZE_T n, GpImage **out)
{
    Rd r = {d, n, FALSE};
    UINT32 version = rd_u32(&r), type = rd_u32(&r);
    if (r.bad || (version & 0xfffff000u) != EMFPLUS_VERSION_BASE)
        return InvalidParameter;
    if (type == 2) {
        UINT32 kind = rd_u32(&r), size = rd_u32(&r);
        (void)kind;
        const BYTE *md = rd_bytes(&r, size);
        if (!md)
            return InvalidParameter;
        GMetafile *m;
        GpStatus st = gp_meta_from_memory(md, size, 0, &m);
        if (st != Ok)
            return st;
        *out = &m->image;
        return Ok;
    }
    if (type != 1)
        return InvalidParameter;
    UINT32 width = rd_u32(&r), height = rd_u32(&r), stride = rd_u32(&r), format = rd_u32(&r), kind = rd_u32(&r);
    if (r.bad)
        return InvalidParameter;
    if (kind == 1) {
        /* an encoded picture */
        BYTE *copy = gp_dup(r.p, r.left);
        if (!copy)
            return OutOfMemory;
        GImage *img;
        GpStatus st = gp_decode_bytes(copy, r.left, &img);
        if (st != Ok)
            return st;
        *out = img;
        return Ok;
    }
    if (!width || !height || width > GP_MAX_DIMENSION || height > GP_MAX_DIMENSION)
        return InvalidParameter;
    ColorPalette *pal = nullptr;
    if (format & PixelFormatIndexed) {
        UINT32 flags = rd_u32(&r), count = rd_u32(&r);
        if (r.bad || count > 256)
            return InvalidParameter;
        const BYTE *pe = rd_bytes(&r, (SIZE_T)count * 4);
        if (!pe)
            return InvalidParameter;
        pal = gp_alloc_zero(sizeof(ColorPalette) + sizeof(ARGB) * (count ? count : 1));
        if (!pal)
            return OutOfMemory;
        pal->Flags = flags;
        pal->Count = count;
        memcpy(pal->Entries, pe, (SIZE_T)count * 4);
    }
    GpBitmap *bmp;
    GpStatus st = GdipCreateBitmapFromScan0((INT)width, (INT)height, 0, (PixelFormat)format, nullptr, &bmp);
    if (st != Ok) {
        gp_free(pal);
        return st;
    }
    if (pal) {
        GdipSetImagePalette(bmp, pal);
        gp_free(pal);
    }
    GpRect all = {0, 0, (INT)width, (INT)height};
    BitmapData bd;
    st = GdipBitmapLockBits(bmp, &all, ImageLockModeWrite, (PixelFormat)format, &bd);
    if (st != Ok) {
        GdipDisposeImage(bmp);
        return st;
    }
    INT in_stride = (INT)stride;
    SIZE_T row = in_stride < 0 ? (SIZE_T)-in_stride : (SIZE_T)in_stride;
    SIZE_T copy = row < (SIZE_T)(bd.Stride < 0 ? -bd.Stride : bd.Stride) ? row : (SIZE_T)(bd.Stride < 0 ? -bd.Stride : bd.Stride);
    for (UINT32 y = 0; y < height; y++) {
        if (r.left < row * (y + 1) && r.left < row * y + copy)
            break;
        SIZE_T have = r.left - row * y;
        memcpy((BYTE *)bd.Scan0 + (SSIZE_T)y * bd.Stride, r.p + row * y, have < copy ? have : copy);
    }
    GdipBitmapUnlockBits(bmp, &bd);
    *out = bmp;
    return Ok;
}

static GpStatus make_attrs(const BYTE *d, SIZE_T n, GpImageAttributes **out)
{
    Rd r = {d, n, FALSE};
    UINT32 version = rd_u32(&r);
    rd_u32(&r);
    UINT32 wrap = rd_u32(&r), clamp_color = rd_u32(&r), clamp = rd_u32(&r);
    if (r.bad || (version & 0xfffff000u) != EMFPLUS_VERSION_BASE)
        return InvalidParameter;
    GpImageAttributes *a;
    GpStatus st = GdipCreateImageAttributes(&a);
    if (st != Ok)
        return st;
    GdipSetImageAttributesWrapMode(a, (WrapMode)wrap, clamp_color, clamp != 0);
    *out = a;
    return Ok;
}

static GpStatus make_font(const BYTE *d, SIZE_T n, GpFont **out)
{
    Rd r = {d, n, FALSE};
    UINT32 version = rd_u32(&r);
    float size = rd_f(&r);
    UINT32 unit = rd_u32(&r), style = rd_u32(&r);
    rd_u32(&r);
    UINT32 len = rd_u32(&r);
    if (r.bad || (version & 0xfffff000u) != EMFPLUS_VERSION_BASE || len > 32)
        return InvalidParameter;
    const BYTE *name = rd_bytes(&r, (SIZE_T)len * 2);
    if (!name)
        return InvalidParameter;
    WCHAR face[33];
    memcpy(face, name, (SIZE_T)len * 2);
    face[len] = 0;
    GpFontFamily *family = nullptr;
    if (GdipCreateFontFamilyFromName(face, nullptr, &family) != Ok || !family) {
        if (GdipGetGenericFontFamilySansSerif(&family) != Ok || !family)
            return GenericError;
    }
    GpStatus st = GdipCreateFont(family, size, (INT)style, (Unit)unit, out);
    GdipDeleteFontFamily(family);
    return st;
}

static GpStatus make_string_format(const BYTE *d, SIZE_T n, GpStringFormat **out)
{
    Rd r = {d, n, FALSE};
    UINT32 version = rd_u32(&r), flags = rd_u32(&r), language = rd_u32(&r), align = rd_u32(&r), line_align = rd_u32(&r),
           digit = rd_u32(&r), digit_lang = rd_u32(&r);
    float first_tab = rd_f(&r);
    UINT32 hotkey = rd_u32(&r);
    rd_f(&r);
    rd_f(&r);
    rd_f(&r);
    UINT32 trimming = rd_u32(&r), tab_count = rd_u32(&r), range_count = rd_u32(&r);
    if (r.bad || (version & 0xfffff000u) != EMFPLUS_VERSION_BASE || tab_count > 1024 || range_count > 1024)
        return InvalidParameter;
    const BYTE *tabs = rd_bytes(&r, (SIZE_T)tab_count * 4);
    const BYTE *ranges = rd_bytes(&r, (SIZE_T)range_count * 8);
    if (!tabs || !ranges)
        return InvalidParameter;
    GpStringFormat *fmt;
    GpStatus st = GdipCreateStringFormat((INT)flags, (LANGID)language, &fmt);
    if (st != Ok)
        return st;
    GdipSetStringFormatAlign(fmt, (StringAlignment)align);
    GdipSetStringFormatLineAlign(fmt, (StringAlignment)line_align);
    GdipSetStringFormatDigitSubstitution(fmt, (LANGID)digit_lang, (StringDigitSubstitute)digit);
    GdipSetStringFormatHotkeyPrefix(fmt, (INT)hotkey);
    GdipSetStringFormatTrimming(fmt, (StringTrimming)trimming);
    if (tab_count)
        GdipSetStringFormatTabStops(fmt, first_tab, (INT)tab_count, (const REAL *)tabs);
    if (range_count)
        GdipSetStringFormatMeasurableCharacterRanges(fmt, (INT)range_count, (const CharacterRange *)ranges);
    *out = fmt;
    return Ok;
}

static void object_store(GPlayer *p, UINT id, INT type, void *obj)
{
    if (id >= 64) {
        PlayObject tmp = {type, obj};
        object_release(&tmp);
        return;
    }
    object_release(&p->objects[id]);
    p->objects[id].type = type;
    p->objects[id].obj = obj;
}

/* an object record is complete: the object is made and takes its slot. */
static void object_define(GPlayer *p, UINT flags, const BYTE *d, SIZE_T n)
{
    UINT id = flags & 0xff, type = (flags >> 8) & 0x7f;
    void *obj = nullptr;
    GpStatus st = InvalidParameter;
    switch (type) {
    case PLAY_BRUSH:
        st = make_brush(d, n, (GpBrush **)&obj);
        break;
    case PLAY_PEN:
        st = make_pen(d, n, (GpPen **)&obj);
        break;
    case PLAY_PATH:
        st = make_path(d, n, (GpPath **)&obj);
        break;
    case PLAY_REGION:
        st = make_region(d, n, (GpRegion **)&obj);
        break;
    case PLAY_IMAGE:
        st = make_image(d, n, (GpImage **)&obj);
        break;
    case PLAY_FONT:
        st = make_font(d, n, (GpFont **)&obj);
        break;
    case PLAY_STRING_FORMAT:
        st = make_string_format(d, n, (GpStringFormat **)&obj);
        break;
    case PLAY_ATTRS:
        st = make_attrs(d, n, (GpImageAttributes **)&obj);
        break;
    case PLAY_CUSTOM_CAP:
        st = make_cap(d, n, (GpCustomLineCap **)&obj);
        break;
    default:
        return;
    }
    if (st != Ok)
        obj = nullptr;
    /* a slot of a failed object is emptied, so the draw records that name it do nothing */
    object_store(p, id, (INT)type, obj);
}

/* an object record: whole, or one piece of several. */
static void object_record(GPlayer *p, UINT flags, const BYTE *d, SIZE_T n)
{
    if (!(flags & 0x8000) && !p->piece) {
        object_define(p, flags, d, n);
        return;
    }
    if (flags & 0x8000) {
        if (n < 4)
            return;
        DWORD total = le32(d);
        d += 4;
        n -= 4;
        gp_free(p->piece);
        p->piece = nullptr;
        if (total <= n || total > 0x10000000u)
            return;
        p->piece = gp_alloc(total);
        if (!p->piece)
            return;
        p->piece_size = total;
        p->piece_got = 0;
        p->piece_flags = flags & ~0x8000u;
    } else if ((flags & ~0x8000u) != p->piece_flags) {
        gp_free(p->piece);
        p->piece = nullptr;
        return;
    }
    SIZE_T room = p->piece_size - p->piece_got;
    SIZE_T take = n < room ? n : room;
    memcpy(p->piece + p->piece_got, d, take);
    p->piece_got += (DWORD)take;
    if (p->piece_got == p->piece_size) {
        object_define(p, p->piece_flags, p->piece, p->piece_size);
        gp_free(p->piece);
        p->piece = nullptr;
    }
}

/* ------------------------------------------------------------------ brushes in records */

/* the brush of a draw record: the color the record holds, or an object. the made one is to be freed. */
static GpBrush *record_brush(GPlayer *p, UINT32 field, UINT flags, BOOL *temporary)
{
    *temporary = FALSE;
    if (flags & PLUS_SOLID) {
        GpSolidFill *b;
        if (GdipCreateSolidFill(field, &b) != Ok)
            return nullptr;
        *temporary = TRUE;
        return b;
    }
    return object_of(p, field, PLAY_BRUSH);
}

static void brush_done(GpBrush *b, BOOL temporary)
{
    if (temporary)
        GdipDeleteBrush(b);
}

/* ------------------------------------------------------------------ the records */

static void save_push(GPlayer *p, UINT index, UINT id, INT kind)
{
    if (p->save_count == p->save_cap) {
        UINT cap = p->save_cap ? p->save_cap * 2 : 16;
        PlaySave *s = gp_realloc(p->saves, sizeof(PlaySave) * cap);
        if (!s)
            return;
        p->saves = s;
        p->save_cap = cap;
    }
    p->saves[p->save_count].index = index;
    p->saves[p->save_count].id = id;
    p->saves[p->save_count].kind = kind;
    p->save_count++;
}

/* the level a record names, and the levels above it which go with it. returns the graphics id or 0. */
static UINT save_pop(GPlayer *p, UINT index, INT kind)
{
    for (UINT i = p->save_count; i > 0; i--) {
        PlaySave *s = &p->saves[i - 1];
        if (s->index == index && s->kind == kind) {
            UINT id = s->id;
            p->save_count = i - 1;
            return id;
        }
    }
    return 0;
}

GpStatus gp_plus_record(GPlayer *p, UINT type, UINT flags, UINT size, const BYTE *data)
{
    GGraphics *g = p->g;
    GpGraphics *gfx = g;
    Rd r = {data, size, FALSE};
    if (type != EmfPlusRecordTypeGetDC)
        gp_play_gdi_end(p);
    switch (type) {
    case EmfPlusRecordTypeHeader:
    case EmfPlusRecordTypeEndOfFile:
    case EmfPlusRecordTypeComment:
    case EmfPlusRecordTypeMultiFormatStart:
    case EmfPlusRecordTypeMultiFormatSection:
    case EmfPlusRecordTypeMultiFormatEnd:
    case EmfPlusRecordTypeSetTSGraphics:
    case EmfPlusRecordTypeSetTSClip:
    case EmfPlusRecordTypeSerializableObject:
    case EmfPlusRecordTypeSetGammaValue:
        return Ok;
    case EmfPlusRecordTypeGetDC:
        p->gdi_enabled = TRUE;
        gp_play_gdi_begin(p);
        return Ok;
    case EmfPlusRecordTypeObject:
        object_record(p, flags, data, size);
        return Ok;
    case EmfPlusRecordTypeClear: {
        UINT32 color = rd_u32(&r);
        return r.bad ? Ok : GdipGraphicsClear(gfx, color);
    }
    case EmfPlusRecordTypeFillRects: {
        UINT32 brush = rd_u32(&r), count = rd_u32(&r);
        if (r.bad)
            return Ok;
        BOOL tmp;
        GpBrush *b = record_brush(p, brush, flags, &tmp);
        GpRectF *rects = read_rects(&r, count, flags);
        if (b && rects)
            GdipFillRectangles(gfx, b, rects, (INT)count);
        gp_free(rects);
        brush_done(b, tmp);
        return Ok;
    }
    case EmfPlusRecordTypeDrawRects: {
        UINT32 count = rd_u32(&r);
        GpPen *pen = object_of(p, flags & 0xff, PLAY_PEN);
        GpRectF *rects = r.bad ? nullptr : read_rects(&r, count, flags);
        if (pen && rects)
            GdipDrawRectangles(gfx, pen, rects, (INT)count);
        gp_free(rects);
        return Ok;
    }
    case EmfPlusRecordTypeFillPolygon: {
        UINT32 brush = rd_u32(&r), count = rd_u32(&r);
        if (r.bad)
            return Ok;
        BOOL tmp;
        GpBrush *b = record_brush(p, brush, flags, &tmp);
        GpPointF *pts = read_points(&r, count, flags);
        if (b && pts)
            GdipFillPolygon(gfx, b, pts, (INT)count, (flags & 0x2000) ? FillModeWinding : FillModeAlternate);
        gp_free(pts);
        brush_done(b, tmp);
        return Ok;
    }
    case EmfPlusRecordTypeDrawLines: {
        UINT32 count = rd_u32(&r);
        GpPen *pen = object_of(p, flags & 0xff, PLAY_PEN);
        GpPointF *pts = r.bad ? nullptr : read_points(&r, count, flags);
        if (pen && pts) {
            if (flags & 0x2000)
                GdipDrawPolygon(gfx, pen, pts, (INT)count);
            else
                GdipDrawLines(gfx, pen, pts, (INT)count);
        }
        gp_free(pts);
        return Ok;
    }
    case EmfPlusRecordTypeFillEllipse: {
        UINT32 brush = rd_u32(&r);
        if (r.bad)
            return Ok;
        BOOL tmp;
        GpBrush *b = record_brush(p, brush, flags, &tmp);
        GpRectF *rc = read_rects(&r, 1, flags);
        if (b && rc)
            GdipFillEllipse(gfx, b, rc->X, rc->Y, rc->Width, rc->Height);
        gp_free(rc);
        brush_done(b, tmp);
        return Ok;
    }
    case EmfPlusRecordTypeDrawEllipse: {
        GpPen *pen = object_of(p, flags & 0xff, PLAY_PEN);
        GpRectF *rc = read_rects(&r, 1, flags);
        if (pen && rc)
            GdipDrawEllipse(gfx, pen, rc->X, rc->Y, rc->Width, rc->Height);
        gp_free(rc);
        return Ok;
    }
    case EmfPlusRecordTypeFillPie: {
        UINT32 brush = rd_u32(&r);
        float start = rd_f(&r), sweep = rd_f(&r);
        if (r.bad)
            return Ok;
        BOOL tmp;
        GpBrush *b = record_brush(p, brush, flags, &tmp);
        GpRectF *rc = read_rects(&r, 1, flags);
        if (b && rc)
            GdipFillPie(gfx, b, rc->X, rc->Y, rc->Width, rc->Height, start, sweep);
        gp_free(rc);
        brush_done(b, tmp);
        return Ok;
    }
    case EmfPlusRecordTypeDrawPie:
    case EmfPlusRecordTypeDrawArc: {
        float start = rd_f(&r), sweep = rd_f(&r);
        GpPen *pen = object_of(p, flags & 0xff, PLAY_PEN);
        GpRectF *rc = r.bad ? nullptr : read_rects(&r, 1, flags);
        if (pen && rc) {
            if (type == EmfPlusRecordTypeDrawPie)
                GdipDrawPie(gfx, pen, rc->X, rc->Y, rc->Width, rc->Height, start, sweep);
            else
                GdipDrawArc(gfx, pen, rc->X, rc->Y, rc->Width, rc->Height, start, sweep);
        }
        gp_free(rc);
        return Ok;
    }
    case EmfPlusRecordTypeFillRegion: {
        UINT32 brush = rd_u32(&r);
        if (r.bad)
            return Ok;
        BOOL tmp;
        GpBrush *b = record_brush(p, brush, flags, &tmp);
        GpRegion *region = object_of(p, flags & 0xff, PLAY_REGION);
        if (b && region)
            GdipFillRegion(gfx, b, region);
        brush_done(b, tmp);
        return Ok;
    }
    case EmfPlusRecordTypeFillPath: {
        UINT32 brush = rd_u32(&r);
        if (r.bad)
            return Ok;
        BOOL tmp;
        GpBrush *b = record_brush(p, brush, flags, &tmp);
        GpPath *path = object_of(p, flags & 0xff, PLAY_PATH);
        if (b && path)
            GdipFillPath(gfx, b, path);
        brush_done(b, tmp);
        return Ok;
    }
    case EmfPlusRecordTypeDrawPath: {
        UINT32 pen_id = rd_u32(&r);
        if (r.bad)
            return Ok;
        GpPen *pen = object_of(p, pen_id, PLAY_PEN);
        GpPath *path = object_of(p, flags & 0xff, PLAY_PATH);
        if (pen && path)
            GdipDrawPath(gfx, pen, path);
        return Ok;
    }
    case EmfPlusRecordTypeStrokeFillPath: {
        UINT32 pen_id = rd_u32(&r), brush_id = rd_u32(&r);
        if (r.bad)
            return Ok;
        GpPen *pen = pen_id != 0xffffffffu ? object_of(p, pen_id, PLAY_PEN) : nullptr;
        GpBrush *brush = brush_id != 0xffffffffu ? object_of(p, brush_id, PLAY_BRUSH) : nullptr;
        GpPath *path = object_of(p, flags & 0xff, PLAY_PATH);
        if (path) {
            if (brush)
                GdipFillPath(gfx, brush, path);
            if (pen)
                GdipDrawPath(gfx, pen, path);
        }
        return Ok;
    }
    case EmfPlusRecordTypeFillClosedCurve: {
        UINT32 brush = rd_u32(&r);
        float tension = rd_f(&r);
        UINT32 count = rd_u32(&r);
        if (r.bad)
            return Ok;
        BOOL tmp;
        GpBrush *b = record_brush(p, brush, flags, &tmp);
        GpPointF *pts = read_points(&r, count, flags);
        if (b && pts)
            GdipFillClosedCurve2(gfx, b, pts, (INT)count, tension, (flags & 0x2000) ? FillModeWinding : FillModeAlternate);
        gp_free(pts);
        brush_done(b, tmp);
        return Ok;
    }
    case EmfPlusRecordTypeDrawClosedCurve: {
        float tension = rd_f(&r);
        UINT32 count = rd_u32(&r);
        GpPen *pen = object_of(p, flags & 0xff, PLAY_PEN);
        GpPointF *pts = r.bad ? nullptr : read_points(&r, count, flags);
        if (pen && pts)
            GdipDrawClosedCurve2(gfx, pen, pts, (INT)count, tension);
        gp_free(pts);
        return Ok;
    }
    case EmfPlusRecordTypeDrawCurve: {
        float tension = rd_f(&r);
        UINT32 offset = rd_u32(&r), segments = rd_u32(&r), count = rd_u32(&r);
        GpPen *pen = object_of(p, flags & 0xff, PLAY_PEN);
        GpPointF *pts = r.bad ? nullptr : read_points(&r, count, flags);
        if (pen && pts)
            GdipDrawCurve3(gfx, pen, pts, (INT)count, (INT)offset, (INT)segments, tension);
        gp_free(pts);
        return Ok;
    }
    case EmfPlusRecordTypeDrawBeziers: {
        UINT32 count = rd_u32(&r);
        GpPen *pen = object_of(p, flags & 0xff, PLAY_PEN);
        GpPointF *pts = r.bad ? nullptr : read_points(&r, count, flags);
        if (pen && pts)
            GdipDrawBeziers(gfx, pen, pts, (INT)count);
        gp_free(pts);
        return Ok;
    }
    case EmfPlusRecordTypeDrawImage: {
        UINT32 attrs_id = rd_u32(&r), unit = rd_u32(&r);
        GpRectF src;
        src.X = rd_f(&r);
        src.Y = rd_f(&r);
        src.Width = rd_f(&r);
        src.Height = rd_f(&r);
        if (r.bad)
            return Ok;
        GpImage *image = object_of(p, flags & 0xff, PLAY_IMAGE);
        GpRectF *dst = read_rects(&r, 1, flags);
        GpImageAttributes *attrs = object_of(p, attrs_id, PLAY_ATTRS);
        if (image && dst)
            GdipDrawImageRectRect(gfx, image, dst->X, dst->Y, dst->Width, dst->Height, src.X, src.Y, src.Width, src.Height,
                                  (GpUnit)unit, attrs, nullptr, nullptr);
        gp_free(dst);
        return Ok;
    }
    case EmfPlusRecordTypeDrawImagePoints: {
        UINT32 attrs_id = rd_u32(&r), unit = rd_u32(&r);
        GpRectF src;
        src.X = rd_f(&r);
        src.Y = rd_f(&r);
        src.Width = rd_f(&r);
        src.Height = rd_f(&r);
        UINT32 count = rd_u32(&r);
        if (r.bad || count != 3)
            return Ok;
        GpImage *image = object_of(p, flags & 0xff, PLAY_IMAGE);
        GpPointF *pts = read_points(&r, 3, flags);
        GpImageAttributes *attrs = object_of(p, attrs_id, PLAY_ATTRS);
        if (image && pts)
            GdipDrawImagePointsRect(gfx, image, pts, 3, src.X, src.Y, src.Width, src.Height, (GpUnit)unit, attrs, nullptr,
                                    nullptr);
        gp_free(pts);
        return Ok;
    }
    case EmfPlusRecordTypeDrawString: {
        UINT32 brush = rd_u32(&r), format_id = rd_u32(&r), length = rd_u32(&r);
        GpRectF rect;
        rect.X = rd_f(&r);
        rect.Y = rd_f(&r);
        rect.Width = rd_f(&r);
        rect.Height = rd_f(&r);
        if (r.bad || !length)
            return Ok;
        const BYTE *text = rd_bytes(&r, (SIZE_T)length * 2);
        if (!text)
            return Ok;
        BOOL tmp;
        GpBrush *b = record_brush(p, brush, flags, &tmp);
        GpFont *font = object_of(p, flags & 0xff, PLAY_FONT);
        GpStringFormat *fmt = object_of(p, format_id, PLAY_STRING_FORMAT);
        if (b && font)
            GdipDrawString(gfx, (const WCHAR *)text, (INT)length, font, &rect, fmt, b);
        brush_done(b, tmp);
        return Ok;
    }
    case EmfPlusRecordTypeDrawDriverString: {
        UINT32 brush = rd_u32(&r), options = rd_u32(&r), has_matrix = rd_u32(&r), count = rd_u32(&r);
        if (r.bad || !count || count > PLUS_MAX_ITEMS)
            return Ok;
        const BYTE *glyphs = rd_bytes(&r, (SIZE_T)count * 2);
        const BYTE *pos = glyphs ? rd_bytes(&r, (SIZE_T)count * 8) : nullptr;
        if (!pos)
            return Ok;
        GMatrix m = {};
        if (has_matrix) {
            read_matrix(&r, &m);
            if (r.bad)
                return Ok;
        }
        BOOL tmp;
        GpBrush *b = record_brush(p, brush, flags, &tmp);
        GpFont *font = object_of(p, flags & 0xff, PLAY_FONT);
        GpPointF *pts = gp_dup(pos, (SIZE_T)count * 8);
        UINT16 *gl = gp_dup(glyphs, (SIZE_T)count * 2);
        if (b && font && pts && gl)
            GdipDrawDriverString(gfx, gl, (INT)count, font, b, pts, (INT)options, has_matrix ? (GpMatrix *)&m : nullptr);
        gp_free(pts);
        gp_free(gl);
        brush_done(b, tmp);
        return Ok;
    }
    case EmfPlusRecordTypeSetRenderingOrigin: {
        INT x = (INT)rd_u32(&r), y = (INT)rd_u32(&r);
        return r.bad ? Ok : GdipSetRenderingOrigin(gfx, x, y);
    }
    case EmfPlusRecordTypeSetAntiAliasMode: {
        /* an older stream has one bit that says whether to smooth, the newer ones name the mode. */
        INT mode = p->mf->header.Version < EMFPLUS_VERSION ? (INT)(flags & 1) + 3 : (INT)((flags >> 1) & 0xf);
        return GdipSetSmoothingMode(gfx, (SmoothingMode)mode);
    }
    case EmfPlusRecordTypeSetTextRenderingHint:
        return GdipSetTextRenderingHint(gfx, (TextRenderingHint)(flags & 0xff));
    case EmfPlusRecordTypeSetTextContrast:
        return GdipSetTextContrast(gfx, flags & 0xfff);
    case EmfPlusRecordTypeSetInterpolationMode:
        return GdipSetInterpolationMode(gfx, (InterpolationMode)(flags & 0xff));
    case EmfPlusRecordTypeSetPixelOffsetMode:
        return GdipSetPixelOffsetMode(gfx, (PixelOffsetMode)(flags & 0xff));
    case EmfPlusRecordTypeSetCompositingMode:
        return GdipSetCompositingMode(gfx, (CompositingMode)(flags & 0xff));
    case EmfPlusRecordTypeSetCompositingQuality:
        return GdipSetCompositingQuality(gfx, (CompositingQuality)(flags & 0xff));
    case EmfPlusRecordTypeSave: {
        UINT32 index = rd_u32(&r);
        GraphicsState id;
        if (r.bad || GdipSaveGraphics(gfx, &id) != Ok)
            return Ok;
        save_push(p, index, id, 0);
        return Ok;
    }
    case EmfPlusRecordTypeRestore: {
        UINT32 index = rd_u32(&r);
        if (r.bad)
            return Ok;
        UINT id = save_pop(p, index, 0);
        return id ? GdipRestoreGraphics(gfx, id) : Ok;
    }
    case EmfPlusRecordTypeBeginContainer: {
        GpRectF dst, src;
        dst.X = rd_f(&r);
        dst.Y = rd_f(&r);
        dst.Width = rd_f(&r);
        dst.Height = rd_f(&r);
        src.X = rd_f(&r);
        src.Y = rd_f(&r);
        src.Width = rd_f(&r);
        src.Height = rd_f(&r);
        UINT32 index = rd_u32(&r);
        GraphicsContainer id;
        if (r.bad || GdipBeginContainer(gfx, &dst, &src, (GpUnit)(flags & 0xff), &id) != Ok)
            return Ok;
        save_push(p, index, id, 1);
        return Ok;
    }
    case EmfPlusRecordTypeBeginContainerNoParams: {
        UINT32 index = rd_u32(&r);
        GraphicsContainer id;
        if (r.bad || GdipBeginContainer2(gfx, &id) != Ok)
            return Ok;
        save_push(p, index, id, 1);
        return Ok;
    }
    case EmfPlusRecordTypeEndContainer: {
        UINT32 index = rd_u32(&r);
        if (r.bad)
            return Ok;
        UINT id = save_pop(p, index, 1);
        return id ? GdipEndContainer(gfx, id) : Ok;
    }
    case EmfPlusRecordTypeSetWorldTransform: {
        GMatrix m;
        read_matrix(&r, &m);
        return r.bad ? Ok : GdipSetWorldTransform(gfx, &m);
    }
    case EmfPlusRecordTypeResetWorldTransform:
        return GdipResetWorldTransform(gfx);
    case EmfPlusRecordTypeMultiplyWorldTransform: {
        GMatrix m;
        read_matrix(&r, &m);
        return r.bad ? Ok : GdipMultiplyWorldTransform(gfx, &m, (flags & 0x2000) ? MatrixOrderAppend : MatrixOrderPrepend);
    }
    case EmfPlusRecordTypeTranslateWorldTransform: {
        float dx = rd_f(&r), dy = rd_f(&r);
        return r.bad ? Ok
                     : GdipTranslateWorldTransform(gfx, dx, dy, (flags & 0x2000) ? MatrixOrderAppend : MatrixOrderPrepend);
    }
    case EmfPlusRecordTypeScaleWorldTransform: {
        float sx = rd_f(&r), sy = rd_f(&r);
        return r.bad ? Ok : GdipScaleWorldTransform(gfx, sx, sy, (flags & 0x2000) ? MatrixOrderAppend : MatrixOrderPrepend);
    }
    case EmfPlusRecordTypeRotateWorldTransform: {
        float angle = rd_f(&r);
        return r.bad ? Ok : GdipRotateWorldTransform(gfx, angle, (flags & 0x2000) ? MatrixOrderAppend : MatrixOrderPrepend);
    }
    case EmfPlusRecordTypeSetPageTransform: {
        float scale = rd_f(&r);
        if (r.bad)
            return Ok;
        GpUnit unit = (GpUnit)(flags & 0xff);
        /* the unit of a display counts the pixels of the recording device */
        if (GdipSetPageUnit(gfx, unit) == Ok)
            GdipSetPageScale(gfx, scale);
        return Ok;
    }
    case EmfPlusRecordTypeResetClip:
        return GdipResetClip(gfx);
    case EmfPlusRecordTypeSetClipRect: {
        GpRectF rc;
        rc.X = rd_f(&r);
        rc.Y = rd_f(&r);
        rc.Width = rd_f(&r);
        rc.Height = rd_f(&r);
        return r.bad ? Ok : GdipSetClipRect(gfx, rc.X, rc.Y, rc.Width, rc.Height, (CombineMode)((flags >> 8) & 0xf));
    }
    case EmfPlusRecordTypeSetClipPath: {
        GpPath *path = object_of(p, flags & 0xff, PLAY_PATH);
        return path ? GdipSetClipPath(gfx, path, (CombineMode)((flags >> 8) & 0xf)) : Ok;
    }
    case EmfPlusRecordTypeSetClipRegion: {
        GpRegion *region = object_of(p, flags & 0xff, PLAY_REGION);
        return region ? GdipSetClipRegion(gfx, region, (CombineMode)((flags >> 8) & 0xf)) : Ok;
    }
    case EmfPlusRecordTypeOffsetClip: {
        float dx = rd_f(&r), dy = rd_f(&r);
        return r.bad ? Ok : GdipTranslateClip(gfx, dx, dy);
    }
    default:
        return Ok;
    }
}
