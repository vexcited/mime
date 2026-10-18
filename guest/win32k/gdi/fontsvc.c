/*
 * font services: raw font data, outline text metrics, unicode coverage,
 * glyph indices and outlines, font enumeration and fonts added at run time.
 */
#include "font_private.h"
#include "ttf/ttf_internal.h"

/* ------------------------------------------------------------------ helpers */

static USHORT be16(const UCHAR *p)
{
    return (USHORT)(p[0] << 8 | p[1]);
}

static LONG round_f(float v)
{
    return (LONG)(v >= 0 ? v + 0.5f : v - 0.5f);
}

static LONG floor_f(float v)
{
    LONG i = (LONG)v;
    return (float)i > v ? i - 1 : i;
}

static LONG ceil_f(float v)
{
    LONG i = (LONG)v;
    return (float)i < v ? i + 1 : i;
}

/* the face of the font selected in a dc, without loading the font file. */
static FONT_FACE *dc_face(HDC hdc)
{
    DC *dc = DcLookup(hdc);
    if (!dc)
        return nullptr;
    if (dc->Realized && dc->RealizedFor == dc->State.Font)
        return dc->Realized->Face;
    return FontMatchFace(&dc->State.Font->Logical);
}

static ULONG swap32(ULONG v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

/* ------------------------------------------------------------------ font data */

ULONG NTAPI NtGdiGetFontData(HDC hdc, DWORD table, DWORD offset, PVOID buffer, ULONG size)
{
    UserEnter();
    FONT_FACE *face = dc_face(hdc);
    ULONG length = 0;
    BOOLEAN ok = face && FontReadTable(face, swap32(table), 0, nullptr, 0, &length);
    UserLeave();
    if (!ok || offset > length)
        return GDI_ERROR;
    ULONG available = length - offset;
    if (!buffer || !size)
        return available;
    ULONG n = size < available ? size : available;
    if (!n)
        return 0;
    PUCHAR copy = W32Alloc(n);
    if (!copy)
        return GDI_ERROR;
    UserEnter();
    ok = FontReadTable(face, swap32(table), offset, copy, n, nullptr);
    UserLeave();
    if (ok)
        ok = NT_SUCCESS(MimeCopyToUser(buffer, copy, n));
    W32Free(copy);
    return ok ? n : GDI_ERROR;
}

/* ------------------------------------------------------------------ outline text metrics */

static ULONG name_chars(FONT_FACE *f, unsigned id, PWSTR out, ULONG capacity)
{
    size_t length = 0;
    if (!mime_ttf_name(&f->Ttf, id, (uint16_t *)out, capacity, &length))
        return 0;
    if (length >= capacity)
        length = capacity - 1;
    out[length] = 0;
    return (ULONG)length;
}

static const UCHAR *table_data(const MimeTtfTable *t, ULONG need)
{
    return t->data && t->size >= need ? t->data : nullptr;
}

ULONG NTAPI NtGdiGetOutlineTextMetricsInternalW(HDC hdc, ULONG size, OUTLINETEXTMETRICW *user_otm, PVOID tmdiff)
{
    WCHAR family[LF_FACESIZE], face_name[LF_FULLFACESIZE], style[LF_FACESIZE], full[LF_FULLFACESIZE];
    OUTLINETEXTMETRICW otm;
    memset(&otm, 0, sizeof(otm));
    UserEnter();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    if (fi) {
        FONT_FACE *f = fi->Face;
        const MimeTtfMetrics *m = &f->Metrics;
        float sy = fi->ScaleY, sx = fi->ScaleX;
        ULONG nf = name_chars(f, 1, family, LF_FACESIZE);
        if (!nf)
            nf = (ULONG)wcslen(wcscpy(family, f->Family));
        ULONG nface = name_chars(f, 4, face_name, LF_FULLFACESIZE);
        if (!nface)
            nface = (ULONG)wcslen(wcscpy(face_name, f->FullName));
        ULONG ns = name_chars(f, 2, style, LF_FACESIZE);
        if (!ns)
            ns = (ULONG)wcslen(wcscpy(style, L"Regular"));
        ULONG nfull = name_chars(f, 3, full, LF_FULLFACESIZE);
        if (!nfull)
            nfull = (ULONG)wcslen(wcscpy(full, face_name));
        otm.otmTextMetrics = fi->Metrics;
        const UCHAR *os2 = table_data(&f->Ttf.os2, 78);
        const UCHAR *hhea = table_data(&f->Ttf.hhea, 22);
        const UCHAR *head = table_data(&f->Ttf.head, 48);
        if (os2) {
            memcpy(&otm.otmPanoseNumber, os2 + 32, sizeof(PANOSE));
            otm.otmfsSelection = be16(os2 + 62);
            otm.otmfsType = be16(os2 + 8);
            otm.otmptSubscriptSize = (POINT){round_f((SHORT)be16(os2 + 10) * sx), round_f((SHORT)be16(os2 + 12) * sy)};
            otm.otmptSubscriptOffset = (POINT){round_f((SHORT)be16(os2 + 14) * sx), round_f((SHORT)be16(os2 + 16) * sy)};
            otm.otmptSuperscriptSize = (POINT){round_f((SHORT)be16(os2 + 18) * sx), round_f((SHORT)be16(os2 + 20) * sy)};
            otm.otmptSuperscriptOffset =
                (POINT){round_f((SHORT)be16(os2 + 22) * sx), round_f((SHORT)be16(os2 + 24) * sy)};
        }
        otm.otmsCharSlopeRise = 1;
        otm.otmsCharSlopeRun = 0;
        if (hhea && (SHORT)be16(hhea + 18)) {
            otm.otmsCharSlopeRise = (SHORT)be16(hhea + 18);
            otm.otmsCharSlopeRun = (SHORT)be16(hhea + 20);
        }
        /* post italicAngle is 16.16 degrees, otmItalicAngle tenths of a degree. */
        otm.otmItalicAngle = (int)(((LONG64)m->italic_angle * 10) / 65536);
        otm.otmEMSquare = (UINT)m->units_per_em;
        otm.otmAscent = round_f(m->typo_ascender * sy);
        otm.otmDescent = round_f(m->typo_descender * sy);
        otm.otmLineGap = (UINT)W32Max(0, round_f(m->typo_line_gap * sy));
        otm.otmsCapEmHeight = (UINT)W32Max(0, round_f(m->cap_height * sy));
        otm.otmsXHeight = (UINT)W32Max(0, round_f(m->x_height * sy));
        otm.otmrcFontBox = (RECT){round_f(m->x_min * sx), round_f(m->y_max * sy), round_f(m->x_max * sx),
                                  round_f(m->y_min * sy)};
        otm.otmMacAscent = round_f(m->ascender * sy);
        otm.otmMacDescent = round_f(m->descender * sy);
        otm.otmMacLineGap = (UINT)W32Max(0, round_f(m->line_gap * sy));
        otm.otmusMinimumPPEM = head ? be16(head + 46) : 0;
        otm.otmsStrikeoutSize = (UINT)W32Max(1, round_f(m->strikeout_size * sy));
        otm.otmsStrikeoutPosition = round_f(m->strikeout_position * sy);
        otm.otmsUnderscoreSize = W32Max(1, round_f(m->underline_thickness * sy));
        otm.otmsUnderscorePosition = round_f(m->underline_position * sy);
        /* the strings follow the structure; the name fields hold their byte offsets. */
        ULONG offset = sizeof(OUTLINETEXTMETRICW);
        otm.otmpFamilyName = (PSTR)(ULONG_PTR)offset;
        offset += (nf + 1) * sizeof(WCHAR);
        otm.otmpFaceName = (PSTR)(ULONG_PTR)offset;
        offset += (nface + 1) * sizeof(WCHAR);
        otm.otmpStyleName = (PSTR)(ULONG_PTR)offset;
        offset += (ns + 1) * sizeof(WCHAR);
        otm.otmpFullName = (PSTR)(ULONG_PTR)offset;
        offset += (nfull + 1) * sizeof(WCHAR);
        otm.otmSize = offset;
    }
    UserLeave();
    (void)tmdiff;
    if (!fi)
        return 0;
    if (!user_otm)
        return otm.otmSize;
    PUCHAR full_buffer = W32Alloc(otm.otmSize);
    if (!full_buffer)
        return 0;
    memcpy(full_buffer, &otm, sizeof(otm));
    memcpy(full_buffer + (ULONG_PTR)otm.otmpFamilyName, family, (wcslen(family) + 1) * sizeof(WCHAR));
    memcpy(full_buffer + (ULONG_PTR)otm.otmpFaceName, face_name, (wcslen(face_name) + 1) * sizeof(WCHAR));
    memcpy(full_buffer + (ULONG_PTR)otm.otmpStyleName, style, (wcslen(style) + 1) * sizeof(WCHAR));
    memcpy(full_buffer + (ULONG_PTR)otm.otmpFullName, full, (wcslen(full) + 1) * sizeof(WCHAR));
    ULONG n = size < otm.otmSize ? size : otm.otmSize;
    BOOLEAN ok = n && NT_SUCCESS(MimeCopyToUser(user_otm, full_buffer, n));
    W32Free(full_buffer);
    return ok ? n : 0;
}

/* ------------------------------------------------------------------ unicode coverage and glyph indices */

static BOOLEAN build_ranges(FONT_FACE *f)
{
    if (f->Ranges)
        return TRUE;
    ULONG count = 0, total = 0;
    for (int pass = 0; pass < 2; pass++) {
        ULONG n = 0;
        total = 0;
        LONG start = -1;
        for (ULONG c = 0; c <= 0x10000; c++) {
            BOOLEAN has = c < 0x10000 && mime_ttf_glyph_index(&f->Ttf, c) != 0;
            if (has && start < 0)
                start = (LONG)c;
            if ((!has || c - (ULONG)start >= 0xFFFF) && start >= 0) {
                if (pass == 1) {
                    f->Ranges[n].wcLow = (WCHAR)start;
                    f->Ranges[n].cGlyphs = (USHORT)(c - (ULONG)start);
                }
                total += c - (ULONG)start;
                n++;
                start = has ? (LONG)c : -1;
            }
        }
        count = n;
        if (pass == 0) {
            f->Ranges = W32Alloc((count ? count : 1) * sizeof(WCRANGE));
            if (!f->Ranges)
                return FALSE;
        }
    }
    f->RangeCount = count;
    f->GlyphsSupported = total;
    return TRUE;
}

DWORD NTAPI NtGdiGetFontUnicodeRanges(HDC hdc, LPGLYPHSET user_set)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    FONT_FACE *f = fi ? fi->Face : nullptr;
    DWORD size = 0;
    PUCHAR copy = nullptr;
    if (f && build_ranges(f)) {
        size = (DWORD)(offsetof(GLYPHSET, ranges) + f->RangeCount * sizeof(WCRANGE));
        if (user_set) {
            copy = W32Alloc(size);
            if (copy) {
                GLYPHSET *gs = (GLYPHSET *)copy;
                gs->cbThis = size;
                gs->flAccel = 0;
                gs->cGlyphsSupported = f->GlyphsSupported;
                gs->cRanges = f->RangeCount;
                memcpy(gs->ranges, f->Ranges, f->RangeCount * sizeof(WCRANGE));
            } else {
                size = 0;
            }
        }
    }
    UserLeave();
    if (copy && !NT_SUCCESS(MimeCopyToUser(user_set, copy, size)))
        size = 0;
    W32Free(copy);
    return size;
}

DWORD NTAPI NtGdiGetGlyphIndicesW(HDC hdc, LPCWSTR user_text, int count, LPWORD user_indices, DWORD flags)
{
    if (count < 0 || count > 65536 || (count && (!user_text || !user_indices)))
        return GDI_ERROR;
    PWSTR text = W32Alloc((SIZE_T)count * sizeof(WCHAR) + 2);
    WORD *indices = W32Alloc((SIZE_T)count * sizeof(WORD) + 2);
    DWORD result = GDI_ERROR;
    if (text && indices && NT_SUCCESS(MimeCopyFromUser(text, user_text, (SIZE_T)count * sizeof(WCHAR)))) {
        UserEnter();
        DC *dc = DcLookup(hdc);
        FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
        if (fi) {
            for (int i = 0; i < count; i++) {
                USHORT g = FontGlyphIndex(fi, text[i]);
                indices[i] = !g && (flags & GGI_MARK_NONEXISTING_GLYPHS) ? 0xFFFF : g;
            }
            result = (DWORD)count;
        }
        UserLeave();
        if (result != GDI_ERROR && count && !NT_SUCCESS(MimeCopyToUser(user_indices, indices, (SIZE_T)count * 2)))
            result = GDI_ERROR;
    }
    W32Free(text);
    W32Free(indices);
    return result;
}

/* ------------------------------------------------------------------ outlines */

typedef struct OUTLINE {
    float *X;
    float *Y;
    UCHAR *On;
    ULONG Count;
    ULONG Capacity;
    ULONG *Ends; /* index after the last point of each contour */
    ULONG Contours;
    ULONG ContourCapacity;
    BOOLEAN Failed;
} OUTLINE;

static void outline_point(void *ctx, float x, float y, bool on)
{
    OUTLINE *o = ctx;
    if (o->Failed)
        return;
    if (o->Count == o->Capacity) {
        ULONG cap = o->Capacity ? o->Capacity * 2 : 64;
        float *nx = W32Alloc(cap * sizeof(float)), *ny = W32Alloc(cap * sizeof(float));
        UCHAR *non = W32Alloc(cap);
        if (!nx || !ny || !non) {
            W32Free(nx);
            W32Free(ny);
            W32Free(non);
            o->Failed = TRUE;
            return;
        }
        if (o->Count) {
            memcpy(nx, o->X, o->Count * sizeof(float));
            memcpy(ny, o->Y, o->Count * sizeof(float));
            memcpy(non, o->On, o->Count);
        }
        W32Free(o->X);
        W32Free(o->Y);
        W32Free(o->On);
        o->X = nx;
        o->Y = ny;
        o->On = non;
        o->Capacity = cap;
    }
    o->X[o->Count] = x;
    o->Y[o->Count] = y;
    o->On[o->Count] = on;
    o->Count++;
}

static void outline_end(void *ctx)
{
    OUTLINE *o = ctx;
    if (o->Failed)
        return;
    ULONG start = o->Contours ? o->Ends[o->Contours - 1] : 0;
    if (o->Count == start)
        return;
    if (o->Contours == o->ContourCapacity) {
        ULONG cap = o->ContourCapacity ? o->ContourCapacity * 2 : 16;
        ULONG *ends = W32Alloc(cap * sizeof(ULONG));
        if (!ends) {
            o->Failed = TRUE;
            return;
        }
        if (o->Contours)
            memcpy(ends, o->Ends, o->Contours * sizeof(ULONG));
        W32Free(o->Ends);
        o->Ends = ends;
        o->ContourCapacity = cap;
    }
    o->Ends[o->Contours++] = o->Count;
}

static void outline_free(OUTLINE *o)
{
    W32Free(o->X);
    W32Free(o->Y);
    W32Free(o->On);
    W32Free(o->Ends);
    memset(o, 0, sizeof(*o));
}

static BOOLEAN outline_load(FONT_INSTANCE *fi, USHORT glyph, OUTLINE *o)
{
    memset(o, 0, sizeof(*o));
    MimeTtfSink sink = {outline_point, outline_end, o};
    if (!mime_ttf_walk_outline(&fi->Face->Ttf, glyph, &sink) || o->Failed) {
        outline_free(o);
        return FALSE;
    }
    return TRUE;
}

/* font units to pixels, y up: x' = x * A + y * C, y' = x * B + y * D. */
typedef struct GLYPH_XFORM {
    float A, B, C, D;
} GLYPH_XFORM;

static void xform_apply(const GLYPH_XFORM *t, float x, float y, float *ox, float *oy)
{
    *ox = x * t->A + y * t->C;
    *oy = x * t->B + y * t->D;
}

void FontGlyphAbcFloat(FONT_INSTANCE *fi, USHORT index, ABCFLOAT *abc)
{
    int advance = 0, lsb = 0;
    mime_ttf_glyph_hmetrics(&fi->Face->Ttf, index, &advance, &lsb);
    float adv = advance * fi->ScaleX;
    abc->abcfA = 0;
    abc->abcfB = 0;
    abc->abcfC = adv;
    OUTLINE o;
    if (!outline_load(fi, index, &o))
        return;
    if (o.Count) {
        float shear = fi->SimItalic ? 0.2f * fi->ScaleY : 0;
        float lo = 0, hi = 0;
        for (ULONG i = 0; i < o.Count; i++) {
            float x = o.X[i] * fi->ScaleX + o.Y[i] * shear;
            if (!i || x < lo)
                lo = x;
            if (!i || x > hi)
                hi = x;
        }
        abc->abcfA = lo;
        abc->abcfB = hi - lo;
        abc->abcfC = adv - hi;
    }
    outline_free(&o);
}

/* outline segments with explicit points: lines, or quadratic curves with their control point. */
typedef struct SEGMENT {
    BOOLEAN Curve;
    float Cx, Cy;
    float X, Y;
} SEGMENT;

/* walks one contour as segments, starting at an on curve point; returns the start point through sx, sy. */
static ULONG contour_segments(const OUTLINE *o, ULONG first, ULONG end, float *sx, float *sy, SEGMENT *out)
{
    ULONG n = end - first;
    if (n < 2)
        return 0;
    ULONG start = n;
    for (ULONG i = 0; i < n; i++)
        if (o->On[first + i]) {
            start = i;
            break;
        }
    float x0, y0;
    ULONG k0;
    if (start == n) {
        /* no on curve point: start at the implied midpoint between the last and first points. */
        x0 = (o->X[first + n - 1] + o->X[first]) / 2;
        y0 = (o->Y[first + n - 1] + o->Y[first]) / 2;
        k0 = 0;
    } else {
        x0 = o->X[first + start];
        y0 = o->Y[first + start];
        k0 = start + 1;
    }
    *sx = x0;
    *sy = y0;
    ULONG count = 0;
    BOOLEAN pending = FALSE;
    float px = 0, py = 0;
    ULONG steps = n;
    for (ULONG s = 0; s <= steps; s++) {
        float x, y;
        BOOLEAN on;
        if (s == steps) {
            x = x0;
            y = y0;
            on = TRUE;
        } else {
            ULONG i = first + (k0 + s) % n;
            if (start != n && (k0 + s) % n == start)
                continue;
            x = o->X[i];
            y = o->Y[i];
            on = o->On[i];
        }
        if (on) {
            if (pending)
                out[count++] = (SEGMENT){TRUE, px, py, x, y};
            else if (s != steps || x != x0 || y != y0 || count)
                out[count++] = (SEGMENT){FALSE, 0, 0, x, y};
            pending = FALSE;
        } else {
            if (pending) {
                float mx = (px + x) / 2, my = (py + y) / 2;
                out[count++] = (SEGMENT){TRUE, px, py, mx, my};
            }
            px = x;
            py = y;
            pending = TRUE;
        }
    }
    /* a closing line back to the start is implied by gdi polygons. */
    if (count && !out[count - 1].Curve && out[count - 1].X == x0 && out[count - 1].Y == y0)
        count--;
    return count;
}

static void put_fixed(FIXED *f, float v)
{
    LONG fx = (LONG)(v * 65536.0f + (v >= 0 ? 0.5f : -0.5f));
    f->value = (short)(fx >> 16);
    f->fract = (WORD)(fx & 0xFFFF);
}

/* writes GGO_NATIVE or GGO_BEZIER polygons; returns the size, and fills out when it is not NULL. */
static ULONG build_polygons(const OUTLINE *o, const GLYPH_XFORM *t, BOOLEAN cubic, PUCHAR out)
{
    ULONG total = 0;
    SEGMENT *segs = W32Alloc((o->Count * 2 + 4) * sizeof(SEGMENT));
    if (!segs)
        return 0;
    ULONG first = 0;
    for (ULONG c = 0; c < o->Contours; c++) {
        ULONG end = o->Ends[c];
        float sx, sy;
        ULONG n = contour_segments(o, first, end, &sx, &sy, segs);
        first = end;
        if (!n)
            continue;
        ULONG poly_start = total;
        TTPOLYGONHEADER *hdr = out ? (TTPOLYGONHEADER *)(out + total) : nullptr;
        total += sizeof(TTPOLYGONHEADER);
        if (hdr) {
            float x, y;
            xform_apply(t, sx, sy, &x, &y);
            hdr->dwType = TT_POLYGON_TYPE;
            put_fixed(&hdr->pfxStart.x, x);
            put_fixed(&hdr->pfxStart.y, y);
        }
        float cx = sx, cy = sy;
        for (ULONG i = 0; i < n;) {
            /* consecutive lines share one record, every curve has its own. */
            WORD type = segs[i].Curve ? (cubic ? TT_PRIM_CSPLINE : TT_PRIM_QSPLINE) : TT_PRIM_LINE;
            ULONG points = 0;
            ULONG j = i;
            if (type == TT_PRIM_LINE) {
                while (j < n && !segs[j].Curve)
                    j++;
                points = j - i;
            } else {
                j = i + 1;
                points = cubic ? 3 : 2;
            }
            ULONG record = offsetof(TTPOLYCURVE, apfx) + points * sizeof(POINTFX);
            if (out) {
                TTPOLYCURVE *pc = (TTPOLYCURVE *)(out + total);
                pc->wType = type;
                pc->cpfx = (WORD)points;
                POINTFX *p = pc->apfx;
                for (ULONG k = i; k < j; k++) {
                    const SEGMENT *s = &segs[k];
                    float x, y;
                    if (type == TT_PRIM_LINE) {
                        xform_apply(t, s->X, s->Y, &x, &y);
                        put_fixed(&p->x, x);
                        put_fixed(&p->y, y);
                        p++;
                    } else if (!cubic) {
                        xform_apply(t, s->Cx, s->Cy, &x, &y);
                        put_fixed(&p->x, x);
                        put_fixed(&p->y, y);
                        p++;
                        xform_apply(t, s->X, s->Y, &x, &y);
                        put_fixed(&p->x, x);
                        put_fixed(&p->y, y);
                        p++;
                    } else {
                        /* the quadratic curve raised to a cubic one. */
                        float c1x = cx + 2.0f / 3.0f * (s->Cx - cx), c1y = cy + 2.0f / 3.0f * (s->Cy - cy);
                        float c2x = s->X + 2.0f / 3.0f * (s->Cx - s->X), c2y = s->Y + 2.0f / 3.0f * (s->Cy - s->Y);
                        float pts[3][2] = {{c1x, c1y}, {c2x, c2y}, {s->X, s->Y}};
                        for (int q = 0; q < 3; q++) {
                            xform_apply(t, pts[q][0], pts[q][1], &x, &y);
                            put_fixed(&p->x, x);
                            put_fixed(&p->y, y);
                            p++;
                        }
                    }
                    cx = s->X;
                    cy = s->Y;
                }
            } else {
                cx = segs[j - 1].X;
                cy = segs[j - 1].Y;
            }
            total += record;
            i = j;
        }
        if (hdr)
            hdr->cb = total - poly_start;
    }
    W32Free(segs);
    return total;
}

/* coverage of a transformed outline with 4x4 supersampling and the nonzero rule, for rotated glyphs. */
static void rasterize_outline(const OUTLINE *o, const GLYPH_XFORM *t, LONG left, LONG top, LONG width, LONG height,
                              UCHAR *cov)
{
    /* flatten into edges in bitmap space, y down, in 1/4 pixel units. */
    ULONG max_edges = o->Count * 8 + 8;
    float *ex0 = W32Alloc(max_edges * sizeof(float) * 4);
    UCHAR *row = W32Alloc((SIZE_T)width * 4 + 4);
    if (!ex0 || !row) {
        W32Free(ex0);
        W32Free(row);
        return;
    }
    float *ey0 = ex0 + max_edges, *ex1 = ey0 + max_edges, *ey1 = ex1 + max_edges;
    ULONG edges = 0;
    SEGMENT *segs = W32Alloc((o->Count * 2 + 4) * sizeof(SEGMENT));
    ULONG first = 0;
    for (ULONG c = 0; segs && c < o->Contours; c++) {
        float sx, sy;
        ULONG n = contour_segments(o, first, o->Ends[c], &sx, &sy, segs);
        first = o->Ends[c];
        float cx = sx, cy = sy;
        for (ULONG i = 0; i <= n; i++) {
            BOOLEAN closing = i == n;
            const SEGMENT *s = closing ? nullptr : &segs[i];
            float nx = closing ? sx : s->X, ny = closing ? sy : s->Y;
            int steps = !closing && s->Curve ? 8 : 1;
            float px = cx, py = cy;
            for (int k = 1; k <= steps; k++) {
                float u = (float)k / steps, x, y;
                if (steps == 1) {
                    x = nx;
                    y = ny;
                } else {
                    float a = (1 - u) * (1 - u), b = 2 * u * (1 - u), d = u * u;
                    x = a * cx + b * s->Cx + d * nx;
                    y = a * cy + b * s->Cy + d * ny;
                }
                if (edges < max_edges) {
                    float ax, ay, bx, by;
                    xform_apply(t, px, py, &ax, &ay);
                    xform_apply(t, x, y, &bx, &by);
                    ex0[edges] = (ax - left) * 4;
                    ey0[edges] = (top - ay) * 4;
                    ex1[edges] = (bx - left) * 4;
                    ey1[edges] = (top - by) * 4;
                    edges++;
                }
                px = x;
                py = y;
            }
            cx = nx;
            cy = ny;
        }
    }
    W32Free(segs);
    float xs[64];
    int dirs[64];
    for (LONG y = 0; y < height; y++) {
        memset(row, 0, (SIZE_T)width * 4);
        for (int sub = 0; sub < 4; sub++) {
            float sy = (float)(y * 4 + sub) + 0.5f;
            ULONG n = 0;
            for (ULONG e = 0; e < edges && n < 64; e++) {
                float y0 = ey0[e], y1 = ey1[e];
                if (y0 == y1 || (sy < y0 && sy < y1) || (sy >= y0 && sy >= y1))
                    continue;
                float x = ex0[e] + (sy - y0) * (ex1[e] - ex0[e]) / (y1 - y0);
                /* insertion sort by x. */
                ULONG k = n++;
                while (k && xs[k - 1] > x) {
                    xs[k] = xs[k - 1];
                    dirs[k] = dirs[k - 1];
                    k--;
                }
                xs[k] = x;
                dirs[k] = y1 > y0 ? 1 : -1;
            }
            int wind = 0;
            for (ULONG k = 0; k + 1 < n; k++) {
                wind += dirs[k];
                if (!wind)
                    continue;
                LONG a = W32Max(0, (LONG)(xs[k] + 0.5f)), b = W32Min(width * 4, (LONG)(xs[k + 1] + 0.5f));
                for (LONG x = a; x < b; x++)
                    row[x]++;
            }
        }
        for (LONG x = 0; x < width; x++) {
            ULONG sum = row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2] + row[x * 4 + 3];
            cov[(SIZE_T)y * width + x] = (UCHAR)(sum >= 16 ? 255 : sum * 255 / 16);
        }
    }
    W32Free(ex0);
    W32Free(row);
}

/*
 * renders a glyph through the 2x2 part of a world transform, which maps y down
 * logical pixels to y down device pixels. returns pool coverage and its offset
 * from the glyph origin, or NULL for an empty glyph.
 */
UCHAR *FontRenderTransformed(FONT_INSTANCE *fi, USHORT index, const XFORM *m, LONG *left, LONG *top, LONG *width,
                             LONG *height)
{
    /* outlines are y up: the off diagonal terms change sign. */
    float a11 = m->eM11, a12 = -m->eM12, a21 = -m->eM21, a22 = m->eM22;
    float sx = fi->ScaleX, sy = fi->ScaleY, shear = fi->SimItalic ? 0.2f : 0.0f;
    UCHAR *cov = nullptr;
    if (a12 == 0 && a11 > 0 && a22 > 0) {
        MimeTtfRenderParams params = {sx * a11, sy * a22, 0, 0, (shear * a11 + a21) / a22,
                                      fi->SimBold ? (float)W32Max(1, fi->EmPixels / 24) : 0.0f};
        int x0, y0, x1, y1;
        if (!mime_ttf_glyph_box_ex(&fi->Face->Ttf, index, &params, &x0, &y0, &x1, &y1) || x1 <= x0 || y1 <= y0)
            return nullptr;
        cov = W32Alloc((SIZE_T)(x1 - x0) * (y1 - y0));
        if (!cov)
            return nullptr;
        memset(cov, 0, (SIZE_T)(x1 - x0) * (y1 - y0));
        mime_ttf_render_glyph_ex(&fi->Face->Ttf, &FontAllocator, index, &params, cov, x1 - x0, y1 - y0, x1 - x0);
        *left = x0;
        *top = y0;
        *width = x1 - x0;
        *height = y1 - y0;
        return cov;
    }
    OUTLINE o;
    if (!outline_load(fi, index, &o))
        return nullptr;
    GLYPH_XFORM t = {sx * a11, sx * a12, shear * sy * a11 + sy * a21, shear * sy * a12 + sy * a22};
    float lo_x = 0, hi_x = 0, lo_y = 0, hi_y = 0;
    for (ULONG i = 0; i < o.Count; i++) {
        float x, y;
        xform_apply(&t, o.X[i], o.Y[i], &x, &y);
        if (!i || x < lo_x)
            lo_x = x;
        if (!i || x > hi_x)
            hi_x = x;
        if (!i || y < lo_y)
            lo_y = y;
        if (!i || y > hi_y)
            hi_y = y;
    }
    LONG bx0 = floor_f(lo_x), bx1 = ceil_f(hi_x), top_up = ceil_f(hi_y), by1 = -floor_f(lo_y);
    LONG w = bx1 - bx0, h = by1 + top_up;
    if (o.Count && w > 0 && h > 0 && (cov = W32Alloc((SIZE_T)w * h)) != nullptr) {
        memset(cov, 0, (SIZE_T)w * h);
        rasterize_outline(&o, &t, bx0, top_up, w, h, cov);
        *left = bx0;
        *top = -top_up;
        *width = w;
        *height = h;
    }
    outline_free(&o);
    return cov;
}

static float fixed_to_float(FIXED f)
{
    return (float)f.value + (float)f.fract / 65536.0f;
}

ULONG NTAPI NtGdiGetGlyphOutline(HDC hdc, WCHAR ch, UINT format, LPGLYPHMETRICS user_gm, ULONG size, PVOID user_buffer,
                                 LPMAT2 user_mat, BOOL ignore_rotation)
{
    MAT2 mat;
    if (!user_mat || !NT_SUCCESS(MimeCopyFromUser(&mat, user_mat, sizeof(mat))))
        return GDI_ERROR;
    UINT kind = format & ~(GGO_GLYPH_INDEX | GGO_UNHINTED);
    if (kind > GGO_GRAY8_BITMAP)
        return GDI_ERROR;
    float m11 = fixed_to_float(mat.eM11), m12 = fixed_to_float(mat.eM12), m21 = fixed_to_float(mat.eM21),
          m22 = fixed_to_float(mat.eM22);
    UserEnter();
    DC *dc = DcLookup(hdc);
    FONT_INSTANCE *fi = dc ? DcGetFont(dc) : nullptr;
    if (!fi) {
        UserLeave();
        return GDI_ERROR;
    }
    if (dc->State.HasXform || dc->State.HasPage) {
        /* the font is realized through the world transform and the page scale, y up here: the matrix applies first. */
        XFORM full;
        DcGetXform(dc, &full);
        if (full.eM11 < 0 && !dc->State.World.eM12 && !dc->State.World.eM21 && dc->State.World.eM11 > 0)
            full.eM11 = -full.eM11;
        if (full.eM22 < 0 && !dc->State.World.eM12 && !dc->State.World.eM21 && dc->State.World.eM22 > 0)
            full.eM22 = -full.eM22;
        const XFORM *w = &full;
        float w11 = w->eM11, w12 = -w->eM12, w21 = -w->eM21, w22 = w->eM22;
        float c11 = m11 * w11 + m12 * w21, c12 = m11 * w12 + m12 * w22;
        float c21 = m21 * w11 + m22 * w21, c22 = m21 * w12 + m22 * w22;
        m11 = c11;
        m12 = c12;
        m21 = c21;
        m22 = c22;
    }
    USHORT index = (format & GGO_GLYPH_INDEX) ? ch : FontGlyphIndex(fi, ch);
    const GLYPH *cached = FontGetGlyphIndex(fi, index);
    float sx = fi->ScaleX, sy = fi->ScaleY, shear = fi->SimItalic ? 0.2f : 0.0f;
    /* pixels before the matrix: x0 = X sx + shear Y sy, y0 = Y sy; then x' = x0 m11 + y0 m21, y' = x0 m12 + y0 m22. */
    GLYPH_XFORM t = {sx * m11, sx * m12, shear * sy * m11 + sy * m21, shear * sy * m12 + sy * m22};
    BOOLEAN identity = m11 == 1 && m22 == 1 && m12 == 0 && m21 == 0;
    BOOLEAN rotated = m12 != 0 && !ignore_rotation;
    GLYPHMETRICS gm;
    memset(&gm, 0, sizeof(gm));
    LONG advance = cached ? cached->Advance : 0;
    gm.gmCellIncX = (short)round_f(advance * m11);
    gm.gmCellIncY = (short)round_f(advance * m12);
    LONG bx0 = 0, by0 = 0, bx1 = 0, by1 = 0; /* bitmap box, y down */
    /* without rotation the rasterizer takes the matrix as scales and a shear: oblique = (shear m11 + m21) / m22. */
    MimeTtfRenderParams params = {sx * m11, sy * m22, 0, 0, m22 ? (shear * m11 + m21) / m22 : 0,
                                  fi->SimBold ? (float)W32Max(1, fi->EmPixels / 24) : 0.0f};
    OUTLINE o;
    BOOLEAN have_outline = outline_load(fi, index, &o);
    if (!rotated && identity && cached && cached->Width) {
        bx0 = cached->Left;
        by0 = cached->Top;
        bx1 = cached->Left + cached->Width;
        by1 = cached->Top + cached->Height;
    } else if (!rotated) {
        int x0, y0, x1, y1;
        if (params.scale_x > 0 && params.scale_y > 0 &&
            mime_ttf_glyph_box_ex(&fi->Face->Ttf, index, &params, &x0, &y0, &x1, &y1) && x1 > x0 && y1 > y0) {
            bx0 = x0;
            by0 = y0;
            bx1 = x1;
            by1 = y1;
        }
    } else if (have_outline && o.Count) {
        float lo_x = 0, hi_x = 0, lo_y = 0, hi_y = 0;
        for (ULONG i = 0; i < o.Count; i++) {
            float x, y;
            xform_apply(&t, o.X[i], o.Y[i], &x, &y);
            if (!i || x < lo_x)
                lo_x = x;
            if (!i || x > hi_x)
                hi_x = x;
            if (!i || y < lo_y)
                lo_y = y;
            if (!i || y > hi_y)
                hi_y = y;
        }
        bx0 = floor_f(lo_x);
        bx1 = ceil_f(hi_x);
        by0 = -ceil_f(hi_y);
        by1 = -floor_f(lo_y);
    }
    BOOLEAN empty = bx1 <= bx0 || by1 <= by0;
    gm.gmBlackBoxX = empty ? 1 : (UINT)(bx1 - bx0);
    gm.gmBlackBoxY = empty ? 1 : (UINT)(by1 - by0);
    gm.gmptGlyphOrigin = empty ? (POINT){0, 0} : (POINT){bx0, -by0};
    ULONG result = GDI_ERROR;
    PUCHAR out = nullptr;
    ULONG needed = 0;
    if (kind == GGO_METRICS) {
        result = 1;
    } else if (kind == GGO_NATIVE || kind == GGO_BEZIER) {
        if (have_outline) {
            needed = build_polygons(&o, &t, kind == GGO_BEZIER, nullptr);
            result = needed;
            if (user_buffer && size && needed) {
                out = W32Alloc(needed);
                if (out && size >= needed)
                    build_polygons(&o, &t, kind == GGO_BEZIER, out);
                else
                    result = GDI_ERROR;
            }
        }
    } else {
        /* bitmaps: rows padded to 32 bits, 1 bit per pixel or gray levels 0..4, 0..16 or 0..64. */
        LONG w = empty ? 0 : bx1 - bx0, h = empty ? 0 : by1 - by0;
        ULONG stride = kind == GGO_BITMAP ? (ULONG)((w + 31) / 32 * 4) : (ULONG)((w + 3) & ~3);
        needed = stride * (ULONG)h;
        result = needed;
        if (user_buffer && size && needed) {
            UCHAR *cov = W32Alloc((SIZE_T)w * h);
            out = W32Alloc(needed);
            if (cov && out && size >= needed) {
                if (rotated) {
                    if (have_outline)
                        rasterize_outline(&o, &t, bx0, -by0, w, h, cov);
                } else if (identity && cached && cached->Coverage && cached->Width == w && cached->Height == h) {
                    memcpy(cov, cached->Coverage, (SIZE_T)w * h);
                } else {
                    params.shift_x = 0;
                    params.shift_y = 0;
                    int x0, y0, x1, y1;
                    if (mime_ttf_glyph_box_ex(&fi->Face->Ttf, index, &params, &x0, &y0, &x1, &y1) && x1 - x0 == w &&
                        y1 - y0 == h)
                        mime_ttf_render_glyph_ex(&fi->Face->Ttf, &FontAllocator, index, &params, cov, (int)w, (int)h,
                                                 (int)w);
                }
                UINT levels = kind == GGO_GRAY2_BITMAP ? 4 : kind == GGO_GRAY4_BITMAP ? 16 : 64;
                for (LONG y = 0; y < h; y++) {
                    PUCHAR dst = out + y * stride;
                    const UCHAR *src = cov + (SIZE_T)y * w;
                    for (LONG x = 0; x < w; x++) {
                        if (kind == GGO_BITMAP) {
                            if (src[x] >= 128)
                                dst[x >> 3] |= (UCHAR)(0x80 >> (x & 7));
                        } else {
                            dst[x] = (UCHAR)((src[x] * levels + 127) / 255);
                        }
                    }
                }
            } else {
                result = GDI_ERROR;
            }
            W32Free(cov);
        }
    }
    if (have_outline)
        outline_free(&o);
    UserLeave();
    if (result != GDI_ERROR && user_gm && !NT_SUCCESS(MimeCopyToUser(user_gm, &gm, sizeof(gm))))
        result = GDI_ERROR;
    if (result != GDI_ERROR && out && !NT_SUCCESS(MimeCopyToUser(user_buffer, out, needed)))
        result = GDI_ERROR;
    W32Free(out);
    return result;
}

/* ------------------------------------------------------------------ enumeration */

typedef struct CHARSET_INFO {
    UCHAR Charset;
    UCHAR Bit; /* fsCsb[0] bit */
    PCWSTR Script;
} CHARSET_INFO;

static const CHARSET_INFO g_charsets[] = {
    {ANSI_CHARSET, 0, L"Western"},          {EASTEUROPE_CHARSET, 1, L"Central European"},
    {RUSSIAN_CHARSET, 2, L"Cyrillic"},      {GREEK_CHARSET, 3, L"Greek"},
    {TURKISH_CHARSET, 4, L"Turkish"},       {HEBREW_CHARSET, 5, L"Hebrew"},
    {ARABIC_CHARSET, 6, L"Arabic"},         {BALTIC_CHARSET, 7, L"Baltic"},
    {VIETNAMESE_CHARSET, 8, L"Vietnamese"}, {THAI_CHARSET, 16, L"Thai"},
    {SHIFTJIS_CHARSET, 17, L"Japanese"},    {GB2312_CHARSET, 18, L"CHINESE_GB2312"},
    {HANGUL_CHARSET, 19, L"Hangul"},        {CHINESEBIG5_CHARSET, 20, L"CHINESE_BIG5"},
    {JOHAB_CHARSET, 21, L"Johab"},          {SYMBOL_CHARSET, 31, L"Symbol"},
};

static const CHARSET_INFO *face_charset(FONT_FACE *f, ULONG wanted)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_charsets); i++) {
        const CHARSET_INFO *c = &g_charsets[i];
        if (wanted != DEFAULT_CHARSET && wanted != c->Charset)
            continue;
        if (f->Signature.fsCsb[0] & (1u << c->Bit))
            return c;
    }
    return nullptr;
}

/* metrics for a cell height of 16 pixels, computed from the registration data without loading the file. */
static void enum_record(FONT_FACE *f, const CHARSET_INFO *cs, MIME_FONT_ENUM_RECORD *r)
{
    memset(r, 0, sizeof(*r));
    LONG cell = f->WinAscent + f->WinDescent;
    if (cell <= 0)
        cell = f->UnitsPerEm;
    LONG height = 16;
    NEWTEXTMETRICW *tm = &r->Metrics.ntmTm;
    tm->tmHeight = height;
    tm->tmAscent = W32MulDiv(f->WinAscent, height, cell);
    tm->tmDescent = height - tm->tmAscent;
    tm->tmInternalLeading = W32Max(0, height - W32MulDiv(f->UnitsPerEm, height, cell));
    tm->tmExternalLeading = W32MulDiv(f->LineGap, height, cell);
    tm->tmAveCharWidth = W32MulDiv(f->AvgWidth, height, cell);
    tm->tmMaxCharWidth = W32MulDiv(f->MaxAdvance, height, cell);
    tm->tmWeight = f->Weight;
    tm->tmDigitizedAspectX = 96;
    tm->tmDigitizedAspectY = 96;
    tm->tmFirstChar = f->Symbol ? 0x20 : 0x1E;
    tm->tmLastChar = f->Symbol ? 0xFF : 0xFFFC;
    tm->tmDefaultChar = 0x1F;
    tm->tmBreakChar = 0x20;
    tm->tmItalic = f->Italic ? 255 : 0;
    tm->tmPitchAndFamily = (f->Fixed ? 0 : TMPF_FIXED_PITCH) | TMPF_VECTOR | TMPF_TRUETYPE | f->PitchFamily;
    tm->tmCharSet = cs->Charset;
    tm->ntmFlags = (f->Weight >= FW_SEMIBOLD ? NTM_BOLD : 0) | (f->Italic ? NTM_ITALIC : 0);
    if (!tm->ntmFlags)
        tm->ntmFlags = NTM_REGULAR;
    tm->ntmSizeEM = f->UnitsPerEm;
    tm->ntmCellHeight = (UINT)cell;
    tm->ntmAvgWidth = (UINT)f->AvgWidth;
    r->Metrics.ntmFontSig = f->Signature;
    LOGFONTW *lf = &r->Font.elfLogFont;
    lf->lfHeight = tm->tmHeight;
    lf->lfWidth = tm->tmAveCharWidth;
    lf->lfWeight = f->Weight;
    lf->lfItalic = f->Italic;
    lf->lfCharSet = cs->Charset;
    lf->lfOutPrecision = 3; /* OUT_STROKE_PRECIS */
    lf->lfClipPrecision = 2; /* CLIP_STROKE_PRECIS */
    lf->lfQuality = DRAFT_QUALITY;
    lf->lfPitchAndFamily = (f->Fixed ? FIXED_PITCH : VARIABLE_PITCH) | f->PitchFamily;
    wcscpy(lf->lfFaceName, f->Family);
    wcscpy(r->Font.elfFullName, f->FullName);
    wcscpy(r->Font.elfStyle, f->Style);
    wcscpy(r->Font.elfScript, cs->Script);
    r->Type = TRUETYPE_FONTTYPE;
}

/* how far a face is from the regular style, to pick one face per family. */
static LONG regular_distance(FONT_FACE *f)
{
    return (f->Weight > FW_NORMAL ? f->Weight - FW_NORMAL : FW_NORMAL - f->Weight) + (f->Italic ? 1000 : 0);
}

ULONG NTAPI NtGdiEnumFonts(HDC hdc, ULONG type, ULONG flags, ULONG face_chars, LPCWSTR user_face, ULONG charset,
                           ULONG *user_size, PVOID user_buffer)
{
    WCHAR face[LF_FACESIZE] = {};
    ULONG capacity = 0;
    if (face_chars >= LF_FACESIZE || !user_size || !NT_SUCCESS(MimeCopyFromUser(&capacity, user_size, sizeof(ULONG))))
        return 0;
    if (face_chars && (!user_face || !NT_SUCCESS(MimeCopyFromUser(face, user_face, face_chars * sizeof(WCHAR)))))
        return 0;
    face[face_chars] = 0;
    UserEnter();
    ULONG total = FontFaceCount();
    ULONG count = 0;
    MIME_FONT_ENUM_RECORD *records = W32Alloc((total ? total : 1) * sizeof(MIME_FONT_ENUM_RECORD));
    if (records) {
        for (ULONG i = 0; i < total; i++) {
            FONT_FACE *f = FontFaceAt(i);
            if (!FontFaceVisible(f) || f->File->Hidden)
                continue;
            const CHARSET_INFO *cs = face_charset(f, charset);
            if (!cs)
                continue;
            if (face[0]) {
                if (_wcsicmp(f->Family, face) && _wcsicmp(f->TypoFamily, face))
                    continue;
            } else {
                /* one face per family: keep the one closest to regular. */
                BOOLEAN replaced = FALSE, seen = FALSE;
                for (ULONG k = 0; k < count; k++) {
                    if (_wcsicmp(records[k].Font.elfLogFont.lfFaceName, f->Family))
                        continue;
                    seen = TRUE;
                    FONT_FACE *other = nullptr;
                    for (ULONG j = 0; j < i && !other; j++) {
                        FONT_FACE *g = FontFaceAt(j);
                        if (!_wcsicmp(g->Family, f->Family) && !_wcsicmp(g->FullName, records[k].Font.elfFullName))
                            other = g;
                    }
                    if (other && regular_distance(f) < regular_distance(other)) {
                        enum_record(f, cs, &records[k]);
                        replaced = TRUE;
                    }
                    break;
                }
                if (seen || replaced)
                    continue;
            }
            enum_record(f, cs, &records[count++]);
        }
    }
    UserLeave();
    ULONG size = count * (ULONG)sizeof(MIME_FONT_ENUM_RECORD);
    BOOLEAN ok = records != nullptr;
    if (ok && user_buffer && capacity >= size && size)
        ok = NT_SUCCESS(MimeCopyToUser(user_buffer, records, size));
    if (ok)
        ok = NT_SUCCESS(MimeCopyToUser(user_size, &size, sizeof(size)));
    W32Free(records);
    (void)hdc;
    (void)type;
    (void)flags;
    return ok ? count : 0;
}

/* ------------------------------------------------------------------ fonts added at run time */

#define FR_PRIVATE_FLAG 0x10
#define FR_NOT_ENUM_FLAG 0x20

/* the path list of AddFontResource is a single nt path here; gdi32 converts the dos name. */
static PWSTR capture_path(LPCWSTR user, ULONG chars)
{
    if (!user || !chars || chars >= MAX_PATH)
        return nullptr;
    PWSTR path = W32Alloc((chars + 1) * sizeof(WCHAR));
    if (path && !NT_SUCCESS(MimeCopyFromUser(path, user, chars * sizeof(WCHAR)))) {
        W32Free(path);
        return nullptr;
    }
    if (path)
        path[chars] = 0;
    return path;
}

int NTAPI NtGdiAddFontResourceW(LPCWSTR files, ULONG chars, ULONG files_count, ULONG flags, DWORD tid, PVOID dv)
{
    PWSTR path = capture_path(files, chars);
    if (!path)
        return 0;
    UserEnter();
    HANDLE owner = (flags & FR_PRIVATE_FLAG) ? PsGetCurrentProcessId() : nullptr;
    int added = 0;
    /* a file added twice by the same owner is counted again without a second registration. */
    for (ULONG i = 0; i < FontFaceCount(); i++) {
        FONT_FACE *f = FontFaceAt(i);
        if (!f->File->Removed && f->File->OwnerPid == owner && !_wcsicmp(f->File->Path, path)) {
            added = (int)f->File->FaceCount;
            break;
        }
    }
    if (!added)
        added = (int)FontRegisterFile(path, owner, (flags & FR_NOT_ENUM_FLAG) != 0);
    UserLeave();
    W32Free(path);
    (void)files_count;
    (void)tid;
    (void)dv;
    return added;
}

BOOL NTAPI NtGdiRemoveFontResourceW(LPCWSTR files, ULONG chars, ULONG files_count, ULONG flags, DWORD tid, PVOID dv)
{
    PWSTR path = capture_path(files, chars);
    if (!path)
        return FALSE;
    UserEnter();
    HANDLE owner = (flags & FR_PRIVATE_FLAG) ? PsGetCurrentProcessId() : nullptr;
    BOOL found = FALSE;
    for (ULONG i = 0; i < FontFaceCount(); i++) {
        FONT_FILE *file = FontFaceAt(i)->File;
        if (!file->Removed && !file->Memory && file->OwnerPid == owner && !_wcsicmp(file->Path, path)) {
            file->Removed = TRUE;
            found = TRUE;
        }
    }
    UserLeave();
    W32Free(path);
    if (!found)
        W32SetLastError(ERROR_FILE_NOT_FOUND);
    (void)files_count;
    (void)tid;
    (void)dv;
    return found;
}

#define MAX_MEM_FONTS 256
static FONT_FILE *g_mem_fonts[MAX_MEM_FONTS];

HANDLE NTAPI NtGdiAddFontMemResourceEx(PVOID data, DWORD size, PVOID dv, ULONG dv_size, DWORD *user_count)
{
    if (!data || !size || size > 64 * 1024 * 1024 || !user_count) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    PUCHAR copy = W32Alloc(size);
    if (!copy) {
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    if (!NT_SUCCESS(MimeCopyFromUser(copy, data, size))) {
        W32Free(copy);
        W32SetLastError(ERROR_NOACCESS);
        return nullptr;
    }
    UserEnter();
    HANDLE handle = nullptr;
    FONT_FILE *file = nullptr;
    DWORD count = 0;
    ULONG slot = MAX_MEM_FONTS;
    for (ULONG i = 0; i < MAX_MEM_FONTS && slot == MAX_MEM_FONTS; i++)
        if (!g_mem_fonts[i])
            slot = i;
    if (slot < MAX_MEM_FONTS)
        count = FontRegisterMemory(copy, size, PsGetCurrentProcessId(), &file);
    if (file) {
        g_mem_fonts[slot] = file;
        /* the handle encodes the slot; any nonzero value works for the caller. */
        handle = (HANDLE)(ULONG_PTR)(0x4D000000u | (slot + 1));
    }
    UserLeave();
    W32Free(copy);
    if (!handle) {
        W32SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
    MimeCopyToUser(user_count, &count, sizeof(count));
    (void)dv;
    (void)dv_size;
    return handle;
}

BOOL NTAPI NtGdiRemoveFontMemResourceEx(HANDLE handle)
{
    ULONG_PTR v = (ULONG_PTR)handle;
    ULONG slot = (ULONG)(v & 0xFFFF) - 1;
    if ((v & 0xFF000000u) != 0x4D000000u || slot >= MAX_MEM_FONTS)
        return FALSE;
    UserEnter();
    FONT_FILE *file = g_mem_fonts[slot];
    BOOL ok = file && file->OwnerPid == PsGetCurrentProcessId() && !file->Removed;
    if (ok) {
        file->Removed = TRUE;
        g_mem_fonts[slot] = nullptr;
    }
    UserLeave();
    return ok;
}
