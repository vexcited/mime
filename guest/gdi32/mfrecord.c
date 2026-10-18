/*
 * what each call records on a metafile dc: enhanced metafile records with
 * their device bounds, and the windows metafile records of the same calls.
 * the bounds are those of the reference device, clipped to the clip region
 * and kept inclusive like windows' kernel accumulates them.
 */
#include "emf_private.h"
#include <mime/win32/winnls.h>

static void *heap_alloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, size);
}

static void heap_free(void *p)
{
    if (p)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}

static double sqrt_d(double v)
{
    if (v <= 0)
        return 0;
    double r = v > 1 ? v : 1;
    for (int i = 0; i < 64; i++) {
        double n = (r + v / r) / 2;
        if (n == r)
            break;
        r = n;
    }
    return r;
}

static WORD lo(DWORD v)
{
    return (WORD)(v & 0xFFFF);
}

static WORD hi(DWORD v)
{
    return (WORD)(v >> 16);
}

/* ------------------------------------------------------------------ bounds helpers */

/*
 * half the pen width in device units, 0 for a cosmetic pen and -1 for a
 * null pen, which draws nothing.
 */
static double pen_half(EMF_DC *emf, const XFORM *m)
{
    HGDIOBJ pen = NtGdiGetDCObject(emf->Dc, OBJ_PEN);
    DWORD style = PS_SOLID, width = 1;
    if ((((ULONG_PTR)pen >> 16) & 0xFF) == 0x50) {
        EXTLOGPEN elp;
        if (GetObjectW(pen, sizeof(elp), &elp)) {
            style = elp.elpPenStyle;
            width = (elp.elpPenStyle & PS_TYPE_MASK) == PS_GEOMETRIC ? elp.elpWidth : 1;
        }
    } else {
        LOGPEN lp;
        if (GetObjectW(pen, sizeof(lp), &lp)) {
            style = lp.lopnStyle;
            width = (DWORD)lp.lopnWidth.x;
        }
    }
    if ((style & PS_STYLE_MASK) == PS_NULL)
        return -1;
    if (width <= 1)
        return 0;
    double det = (double)m->eM11 * m->eM22 - (double)m->eM12 * m->eM21;
    return width * sqrt_d(det < 0 ? -det : det) / 2;
}

static void add_logical(EMF_BOUNDS *b, const XFORM *m, double x, double y)
{
    double dx, dy;
    EmfTransformPoint(m, x, y, &dx, &dy);
    EmfBoundsPoint(b, dx, dy);
}

/* the four corners of a logical rectangle; exclusive drops the right and bottom device pixel. */
static void add_rect(EMF_BOUNDS *b, const XFORM *m, double l, double t, double r, double bt, BOOL exclusive)
{
    EMF_BOUNDS box;
    EmfBoundsInit(&box);
    add_logical(&box, m, l, t);
    add_logical(&box, m, r, t);
    add_logical(&box, m, l, bt);
    add_logical(&box, m, r, bt);
    if (exclusive) {
        box.Right -= 1;
        box.Bottom -= 1;
    }
    if (box.Right < box.Left || box.Bottom < box.Top)
        return;
    EmfBoundsPoint(b, box.Left, box.Top);
    EmfBoundsPoint(b, box.Right, box.Bottom);
}

static BOOL compatible(EMF_DC *emf)
{
    DWORD mode = GM_COMPATIBLE;
    NtGdiGetDCDword(emf->Dc, MIME_DCDW_GRAPHICSMODE, &mode);
    return mode == GM_COMPATIBLE;
}

/* the bounds of records without a bounds field go to the header only. */
static void commit_header(EMF_DC *emf, const EMF_BOUNDS *b)
{
    EmfBoundsCommit(emf, nullptr, b);
}

/* ------------------------------------------------------------------ state records */

static BOOL emf_dword(HDC dc, UINT emr, DWORD value)
{
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    DWORD *r = EmfRecordAlloc(emf, emr, 12);
    if (!r)
        return FALSE;
    r[2] = value;
    return TRUE;
}

static BOOL emf_pair(HDC dc, UINT emr, LONG a, LONG b)
{
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    LONG *r = EmfRecordAlloc(emf, emr, 16);
    if (!r)
        return FALSE;
    r[2] = a;
    r[3] = b;
    return TRUE;
}

static BOOL emf_four(HDC dc, UINT emr, LONG a, LONG b, LONG c, LONG d)
{
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    LONG *r = EmfRecordAlloc(emf, emr, 24);
    if (!r)
        return FALSE;
    r[2] = a;
    r[3] = b;
    r[4] = c;
    r[5] = d;
    return TRUE;
}

BOOL MfSetValue(HDC dc, UINT emr, UINT meta, DWORD value)
{
    if (MfIsWmfDc(dc)) {
        if (!meta)
            return FALSE;
        WORD p[2] = {lo(value), hi(value)};
        BOOL wide = meta == META_SETMAPPERFLAGS || meta == META_SETLAYOUT;
        return WmfRecord(dc, (WORD)meta, wide ? 2 : 1, p);
    }
    return emr ? emf_dword(dc, emr, value) : TRUE;
}

BOOL MfSetColor(HDC dc, UINT emr, UINT meta, COLORREF color)
{
    if (MfIsWmfDc(dc)) {
        WORD p[2] = {lo(color), hi(color)};
        return WmfRecord(dc, (WORD)meta, 2, p);
    }
    return emf_dword(dc, emr, color);
}

BOOL MfSetPoint(HDC dc, UINT emr, UINT meta, int x, int y)
{
    if (MfIsWmfDc(dc)) {
        if (!meta)
            return FALSE;
        WORD p[2] = {(WORD)y, (WORD)x};
        return WmfRecord(dc, (WORD)meta, 2, p);
    }
    if (!emf_pair(dc, emr, x, y))
        return FALSE;
    if (emr == EMR_OFFSETCLIPRGN)
        EmfClipChanged(EmfDc(dc));
    return TRUE;
}

/* windows records the origin the offset leads to. */
BOOL MfOffsetOrg(HDC dc, BOOL viewport, int x, int y)
{
    if (MfIsWmfDc(dc)) {
        WORD p[2] = {(WORD)y, (WORD)x};
        return WmfRecord(dc, viewport ? META_OFFSETVIEWPORTORG : META_OFFSETWINDOWORG, 2, p);
    }
    POINT org;
    if (!NtGdiGetDCPoint(dc, viewport ? MIME_DCPT_VIEWPORTORG : MIME_DCPT_WINDOWORG, &org))
        return FALSE;
    return emf_pair(dc, viewport ? EMR_SETVIEWPORTORGEX : EMR_SETWINDOWORGEX, org.x + x, org.y + y);
}

BOOL MfScaleExt(HDC dc, BOOL viewport, int xn, int xd, int yn, int yd)
{
    if (MfIsWmfDc(dc)) {
        WORD p[4] = {(WORD)yd, (WORD)yn, (WORD)xd, (WORD)xn};
        return WmfRecord(dc, viewport ? META_SCALEVIEWPORTEXT : META_SCALEWINDOWEXT, 4, p);
    }
    return emf_four(dc, viewport ? EMR_SCALEVIEWPORTEXTEX : EMR_SCALEWINDOWEXTEX, xn, xd, yn, yd);
}

/* like windows, only a change of mode is recorded, and the isotropic mode always. */
BOOL MfSetMapMode(HDC dc, int mode)
{
    if (MfIsWmfDc(dc)) {
        WORD p = (WORD)mode;
        return WmfRecord(dc, META_SETMAPMODE, 1, &p);
    }
    DWORD current = 0;
    NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &current);
    if ((int)current == mode && mode != MM_ISOTROPIC)
        return TRUE;
    return emf_dword(dc, EMR_SETMAPMODE, (DWORD)mode);
}

BOOL MfRecordEmpty(HDC dc, UINT emr, UINT meta)
{
    if (MfIsWmfDc(dc))
        return meta ? WmfRecord(dc, (WORD)meta, 0, nullptr) : FALSE;
    EMF_DC *emf = EmfDc(dc);
    if (!emf || !EmfRecordAlloc(emf, emr, 8))
        return FALSE;
    if (emr == EMR_SAVEDC)
        emf->Saves++;
    if (emr == EMR_SETMETARGN)
        EmfClipChanged(emf);
    return TRUE;
}

/* recorded relative to the current level, and only when the level exists. */
BOOL MfRestoreDC(HDC dc, int level)
{
    if (MfIsWmfDc(dc)) {
        WORD p = (WORD)level;
        return WmfRecord(dc, META_RESTOREDC, 1, &p);
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    int rel = level > 0 ? level - (emf->Saves + 1) : level;
    if (rel >= 0 || rel + emf->Saves + 1 <= 0)
        return FALSE;
    if (!emf_dword(dc, EMR_RESTOREDC, (DWORD)rel))
        return FALSE;
    emf->Saves += rel;
    EmfClipChanged(emf);
    return TRUE;
}

BOOL MfSetWorldTransform(HDC dc, const XFORM *xform, DWORD mode, BOOL modify)
{
    if (MfIsWmfDc(dc))
        return FALSE;
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    static const XFORM identity = {1, 0, 0, 1, 0, 0};
    if (!xform)
        xform = &identity;
    if (!modify) {
        EMRSETWORLDTRANSFORM *r = EmfRecordAlloc(emf, EMR_SETWORLDTRANSFORM, sizeof(*r));
        if (!r)
            return FALSE;
        r->xform = *xform;
        return TRUE;
    }
    EMRMODIFYWORLDTRANSFORM *r = EmfRecordAlloc(emf, EMR_MODIFYWORLDTRANSFORM, sizeof(*r));
    if (!r)
        return FALSE;
    r->xform = *xform;
    r->iMode = mode;
    return TRUE;
}

/* windows records the miter limit as an integer. */
BOOL MfSetMiterLimit(HDC dc, FLOAT limit)
{
    if (MfIsWmfDc(dc))
        return FALSE;
    return emf_dword(dc, EMR_SETMITERLIMIT, (DWORD)(LONG)limit);
}

/* ------------------------------------------------------------------ objects */

BOOL MfSelectObject(HDC dc, HGDIOBJ object, HGDIOBJ *previous)
{
    if (MfIsWmfDc(dc))
        return WmfSelectObject(dc, object, previous);
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    switch (((ULONG_PTR)object >> 16) & 0xFF) {
    case 0x04:
        return MfSelectClipRgn(dc, object, RGN_COPY);
    case 0x10:
    case 0x30:
    case 0x50:
    case 0x0A:
        break;
    default:
        /* bitmaps cannot be selected into a metafile dc. */
        return FALSE;
    }
    DWORD index = EmfObjectIndex(emf, object, nullptr);
    if (!index)
        return FALSE;
    return emf_dword(dc, EMR_SELECTOBJECT, index);
}

BOOL MfSelectPalette(HDC dc, HPALETTE palette)
{
    if (MfIsWmfDc(dc))
        return WmfSelectObject(dc, palette, nullptr);
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    DWORD index = EmfObjectIndex(emf, palette, nullptr);
    return index && emf_dword(dc, EMR_SELECTPALETTE, index);
}

BOOL MfSetDCColor(HDC dc, BOOL pen, COLORREF color)
{
    return TRUE;
}

/* ------------------------------------------------------------------ clipping */

BOOL MfClipRect(HDC dc, UINT emr, UINT meta, int left, int top, int right, int bottom)
{
    if (MfIsWmfDc(dc)) {
        WORD p[4] = {(WORD)bottom, (WORD)right, (WORD)top, (WORD)left};
        return WmfRecord(dc, (WORD)meta, 4, p);
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf || !emf_four(dc, emr, left, top, right, bottom))
        return FALSE;
    EmfClipChanged(emf);
    return TRUE;
}

/* the region data is in device units, as the clip region. */
BOOL MfSelectClipRgn(HDC dc, HRGN region, int mode)
{
    if (MfIsWmfDc(dc)) {
        /* windows metafiles keep only rectangles: a reset, then the box of the region. */
        WORD zero = 0;
        if (!WmfRecord(dc, META_SELECTCLIPREGION, 1, &zero))
            return FALSE;
        RECT box;
        if (!region || NtGdiGetRgnBox(region, &box) == ERROR)
            return TRUE;
        WORD p[4] = {(WORD)box.bottom, (WORD)box.right, (WORD)box.top, (WORD)box.left};
        return WmfRecord(dc, META_INTERSECTCLIPRECT, 4, p);
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    DWORD size = 0;
    if (region || mode != RGN_COPY) {
        size = NtGdiGetRegionData(region, 0, nullptr);
        if (!size)
            return FALSE;
    }
    EMREXTSELECTCLIPRGN *r = EmfRecordAlloc(emf, EMR_EXTSELECTCLIPRGN, offsetof(EMREXTSELECTCLIPRGN, RgnData) + size);
    if (!r)
        return FALSE;
    r->cbRgnData = size;
    r->iMode = (DWORD)mode;
    if (size && NtGdiGetRegionData(region, size, (LPRGNDATA)r->RgnData) != size) {
        emf->Failed = TRUE;
        return FALSE;
    }
    EmfClipChanged(emf);
    return TRUE;
}

BOOL MfSelectClipPath(HDC dc, int mode)
{
    if (MfIsWmfDc(dc))
        return FALSE;
    EMF_DC *emf = EmfDc(dc);
    if (!emf || !emf_dword(dc, EMR_SELECTCLIPPATH, (DWORD)mode))
        return FALSE;
    EmfClipChanged(emf);
    return TRUE;
}

/* ------------------------------------------------------------------ lines and shapes */

static BOOL path_open(EMF_DC *emf)
{
    DWORD state = 0;
    NtGdiGetDCDword(emf->Dc, MIME_DCDW_PATHSTATE, &state);
    return state == 1;
}

BOOL MfMoveTo(HDC dc, int x, int y)
{
    return MfSetPoint(dc, EMR_MOVETOEX, META_MOVETO, x, y);
}

BOOL MfLineTo(HDC dc, int x, int y)
{
    if (MfIsWmfDc(dc)) {
        WORD p[2] = {(WORD)y, (WORD)x};
        return WmfRecord(dc, META_LINETO, 2, p);
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    POINT cur = {0, 0};
    NtGdiGetDCPoint(dc, MIME_DCPT_CURRENTPOSITION, &cur);
    if (!emf_pair(dc, EMR_LINETO, x, y))
        return FALSE;
    XFORM m;
    EmfWorldToDevice(emf, &m);
    double half = pen_half(emf, &m);
    if (half >= 0 && !path_open(emf)) {
        EMF_BOUNDS b;
        EmfBoundsInit(&b);
        add_logical(&b, &m, cur.x, cur.y);
        add_logical(&b, &m, x, y);
        EmfBoundsInflate(&b, half);
        commit_header(emf, &b);
    }
    return TRUE;
}

/* the arc direction windows records before arcs and boxes, flipped when the mapping flips y in the compatible mode. */
static BOOL validate_arc_direction(EMF_DC *emf)
{
    DWORD dir = AD_COUNTERCLOCKWISE, mode = MM_TEXT;
    NtGdiGetDCDword(emf->Dc, MIME_DCDW_ARCDIRECTION, &dir);
    BOOL clockwise = dir == AD_CLOCKWISE;
    if (compatible(emf)) {
        NtGdiGetDCDword(emf->Dc, MIME_DCDW_MAPMODE, &mode);
        BOOL flipped = mode >= MM_LOMETRIC && mode <= MM_TWIPS;
        if (mode == MM_ISOTROPIC || mode == MM_ANISOTROPIC) {
            XFORM m;
            NtGdiGetTransform(emf->Dc, MIME_XFORM_PAGE_TO_DEVICE, &m);
            flipped = (m.eM11 < 0) != (m.eM22 < 0);
        }
        if (flipped)
            clockwise = !clockwise;
    }
    int want = clockwise ? AD_CLOCKWISE : AD_COUNTERCLOCKWISE;
    if ((emf->ArcDirection ? emf->ArcDirection : AD_COUNTERCLOCKWISE) == want)
        return TRUE;
    emf->ArcDirection = want;
    return emf_dword(emf->Dc, EMR_SETARCDIRECTION, (DWORD)want);
}

/*
 * in the compatible mode the box loses its right and bottom device pixel,
 * so playback, always in the advanced mode, draws the same pixels. returns
 * FALSE for a box that becomes empty, which windows does not record.
 */
static BOOL convert_box(EMF_DC *emf, RECTL *box)
{
    if (!compatible(emf))
        return TRUE;
    POINT p[2] = {{box->left, box->top}, {box->right, box->bottom}};
    if (!NtGdiTransformPoints(emf->Dc, p, p, 2, 1))
        return TRUE;
    LONG l = p[0].x < p[1].x ? p[0].x : p[1].x, r = p[0].x < p[1].x ? p[1].x : p[0].x;
    LONG t = p[0].y < p[1].y ? p[0].y : p[1].y, b = p[0].y < p[1].y ? p[1].y : p[0].y;
    r--;
    b--;
    if (r < l || b < t)
        return FALSE;
    p[0] = (POINT){l, t};
    p[1] = (POINT){r, b};
    NtGdiTransformPoints(emf->Dc, p, p, 2, 0);
    *box = (RECTL){p[0].x, p[0].y, p[1].x, p[1].y};
    return TRUE;
}

static void box_bounds(EMF_DC *emf, LONG l, LONG t, LONG r, LONG b)
{
    if (path_open(emf))
        return;
    XFORM m;
    EmfWorldToDevice(emf, &m);
    double half = pen_half(emf, &m);
    EMF_BOUNDS bounds;
    EmfBoundsInit(&bounds);
    add_rect(&bounds, &m, l, t, r, b, compatible(emf));
    if (half > 0)
        EmfBoundsInflate(&bounds, half);
    commit_header(emf, &bounds);
}

BOOL MfBox(HDC dc, UINT emr, UINT meta, int left, int top, int right, int bottom)
{
    if (MfIsWmfDc(dc)) {
        WORD p[4] = {(WORD)bottom, (WORD)right, (WORD)top, (WORD)left};
        return WmfRecord(dc, (WORD)meta, 4, p);
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf || !validate_arc_direction(emf))
        return FALSE;
    RECTL box = {left, top, right, bottom};
    box_bounds(emf, left, top, right, bottom);
    if (!convert_box(emf, &box))
        return TRUE;
    return emf_four(dc, emr, box.left, box.top, box.right, box.bottom);
}

BOOL MfRoundRect(HDC dc, int left, int top, int right, int bottom, int width, int height)
{
    if (MfIsWmfDc(dc)) {
        WORD p[6] = {(WORD)height, (WORD)width, (WORD)bottom, (WORD)right, (WORD)top, (WORD)left};
        return WmfRecord(dc, META_ROUNDRECT, 6, p);
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf || !validate_arc_direction(emf))
        return FALSE;
    RECTL box = {left, top, right, bottom};
    box_bounds(emf, left, top, right, bottom);
    if (!convert_box(emf, &box))
        return TRUE;
    EMRROUNDRECT *r = EmfRecordAlloc(emf, EMR_ROUNDRECT, sizeof(*r));
    if (!r)
        return FALSE;
    r->rclBox = box;
    r->szlCorner = (SIZEL){width, height};
    return TRUE;
}

BOOL MfArc(HDC dc, UINT emr, UINT meta, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2)
{
    if (MfIsWmfDc(dc)) {
        if (!meta)
            return FALSE;
        WORD p[8] = {(WORD)y2, (WORD)x2, (WORD)y1, (WORD)x1, (WORD)bottom, (WORD)right, (WORD)top, (WORD)left};
        return WmfRecord(dc, (WORD)meta, 8, p);
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf || !validate_arc_direction(emf))
        return FALSE;
    RECTL box = {left, top, right, bottom};
    box_bounds(emf, left, top, right, bottom);
    if (!convert_box(emf, &box))
        return TRUE;
    EMRARC *r = EmfRecordAlloc(emf, emr, sizeof(*r));
    if (!r)
        return FALSE;
    r->rclBox = box;
    r->ptlStart = (POINTL){x1, y1};
    r->ptlEnd = (POINTL){x2, y2};
    return TRUE;
}

BOOL MfAngleArc(HDC dc, int x, int y, DWORD radius, FLOAT start, FLOAT sweep)
{
    if (MfIsWmfDc(dc))
        return FALSE;
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    POINT cur = {0, 0};
    NtGdiGetDCPoint(dc, MIME_DCPT_CURRENTPOSITION, &cur);
    EMRANGLEARC *r = EmfRecordAlloc(emf, EMR_ANGLEARC, sizeof(*r));
    if (!r)
        return FALSE;
    r->ptlCenter = (POINTL){x, y};
    r->nRadius = radius;
    r->eStartAngle = start;
    r->eSweepAngle = sweep;
    if (!path_open(emf)) {
        XFORM m;
        EmfWorldToDevice(emf, &m);
        double half = pen_half(emf, &m);
        EMF_BOUNDS b;
        EmfBoundsInit(&b);
        add_rect(&b, &m, (double)x - radius, (double)y - radius, (double)x + radius, (double)y + radius, FALSE);
        add_logical(&b, &m, cur.x, cur.y);
        if (half > 0)
            EmfBoundsInflate(&b, half);
        commit_header(emf, &b);
    }
    return TRUE;
}

static BOOL fits16(const POINT *points, DWORD count)
{
    for (DWORD i = 0; i < count; i++)
        if (points[i].x < -32768 || points[i].x > 32767 || points[i].y < -32768 || points[i].y > 32767)
            return FALSE;
    return TRUE;
}

/* the bounds of points, with the current position for the functions that start there. */
static void poly_bounds(EMF_DC *emf, EMF_BOUNDS *b, const POINT *points, DWORD count, BOOL from_current,
                        BOOL filled)
{
    EmfBoundsInit(b);
    if (path_open(emf))
        return;
    XFORM m;
    EmfWorldToDevice(emf, &m);
    double half = pen_half(emf, &m);
    if (half < 0 && !filled)
        return;
    if (from_current) {
        POINT cur = {0, 0};
        NtGdiGetDCPoint(emf->Dc, MIME_DCPT_CURRENTPOSITION, &cur);
        add_logical(b, &m, cur.x, cur.y);
    }
    for (DWORD i = 0; i < count; i++)
        add_logical(b, &m, points[i].x, points[i].y);
    if (half > 0)
        EmfBoundsInflate(b, half);
}

BOOL MfPoly(HDC dc, UINT emr, UINT meta, const POINT *points, DWORD count)
{
    if (MfIsWmfDc(dc)) {
        if (!meta || count > 0x7FFF)
            return FALSE;
        WORD *p = heap_alloc((1 + 2 * (SIZE_T)count) * sizeof(WORD));
        if (!p)
            return FALSE;
        p[0] = (WORD)count;
        for (DWORD i = 0; i < count; i++) {
            p[1 + 2 * i] = (WORD)points[i].x;
            p[2 + 2 * i] = (WORD)points[i].y;
        }
        BOOL ok = WmfRecord(dc, (WORD)meta, 1 + 2 * count, p);
        heap_free(p);
        return ok;
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf || count > 0x1FFFFFF0)
        return FALSE;
    EMF_BOUNDS b;
    poly_bounds(emf, &b, points, count, emr == EMR_POLYBEZIERTO || emr == EMR_POLYLINETO, emr == EMR_POLYGON);
    if (fits16(points, count)) {
        EMRPOLYLINE16 *r = EmfRecordAlloc(emf, emr + 83, offsetof(EMRPOLYLINE16, apts) + count * sizeof(POINTS));
        if (!r)
            return FALSE;
        r->cpts = count;
        for (DWORD i = 0; i < count; i++)
            r->apts[i] = (POINTS){(SHORT)points[i].x, (SHORT)points[i].y};
        EmfBoundsCommit(emf, &r->rclBounds, &b);
        return TRUE;
    }
    EMRPOLYLINE *r = EmfRecordAlloc(emf, emr, offsetof(EMRPOLYLINE, aptl) + count * sizeof(POINTL));
    if (!r)
        return FALSE;
    r->cptl = count;
    for (DWORD i = 0; i < count; i++)
        r->aptl[i] = (POINTL){points[i].x, points[i].y};
    EmfBoundsCommit(emf, &r->rclBounds, &b);
    return TRUE;
}

BOOL MfPolyPoly(HDC dc, UINT emr, UINT meta, const POINT *points, const DWORD *counts, DWORD polys)
{
    DWORD total = 0;
    for (DWORD i = 0; i < polys; i++) {
        if (counts[i] > 0x1FFFFFF0 - total)
            return FALSE;
        total += counts[i];
    }
    if (MfIsWmfDc(dc)) {
        if (!meta || total > 0x3FFF || polys > 0x3FFF)
            return FALSE;
        WORD *p = heap_alloc((1 + polys + 2 * (SIZE_T)total) * sizeof(WORD));
        if (!p)
            return FALSE;
        p[0] = (WORD)polys;
        for (DWORD i = 0; i < polys; i++)
            p[1 + i] = (WORD)counts[i];
        for (DWORD i = 0; i < total; i++) {
            p[1 + polys + 2 * i] = (WORD)points[i].x;
            p[2 + polys + 2 * i] = (WORD)points[i].y;
        }
        BOOL ok = WmfRecord(dc, (WORD)meta, 1 + polys + 2 * total, p);
        heap_free(p);
        return ok;
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf || polys > 0x1000000)
        return FALSE;
    EMF_BOUNDS b;
    poly_bounds(emf, &b, points, total, FALSE, emr == EMR_POLYPOLYGON);
    if (fits16(points, total)) {
        DWORD size = offsetof(EMRPOLYPOLYLINE16, aPolyCounts) + polys * 4 + total * sizeof(POINTS);
        EMRPOLYPOLYLINE16 *r = EmfRecordAlloc(emf, emr + 83, size);
        if (!r)
            return FALSE;
        r->nPolys = polys;
        r->cpts = total;
        memcpy(r->aPolyCounts, counts, polys * 4);
        POINTS *pts = (POINTS *)(r->aPolyCounts + polys);
        for (DWORD i = 0; i < total; i++)
            pts[i] = (POINTS){(SHORT)points[i].x, (SHORT)points[i].y};
        EmfBoundsCommit(emf, &r->rclBounds, &b);
        return TRUE;
    }
    DWORD size = offsetof(EMRPOLYPOLYLINE, aPolyCounts) + polys * 4 + total * sizeof(POINTL);
    EMRPOLYPOLYLINE *r = EmfRecordAlloc(emf, emr, size);
    if (!r)
        return FALSE;
    r->nPolys = polys;
    r->cptl = total;
    memcpy(r->aPolyCounts, counts, polys * 4);
    POINTL *pts = (POINTL *)(r->aPolyCounts + polys);
    for (DWORD i = 0; i < total; i++)
        pts[i] = (POINTL){points[i].x, points[i].y};
    EmfBoundsCommit(emf, &r->rclBounds, &b);
    return TRUE;
}

BOOL MfPolyDraw(HDC dc, const POINT *points, const BYTE *types, DWORD count)
{
    if (MfIsWmfDc(dc))
        return FALSE;
    EMF_DC *emf = EmfDc(dc);
    if (!emf || count > 0x10000000)
        return FALSE;
    EMF_BOUNDS b;
    poly_bounds(emf, &b, points, count, TRUE, FALSE);
    if (fits16(points, count)) {
        EMRPOLYDRAW16 *r = EmfRecordAlloc(emf, EMR_POLYDRAW16, offsetof(EMRPOLYDRAW16, apts) + count * 5);
        if (!r)
            return FALSE;
        r->cpts = count;
        for (DWORD i = 0; i < count; i++)
            r->apts[i] = (POINTS){(SHORT)points[i].x, (SHORT)points[i].y};
        memcpy((BYTE *)(r->apts + count), types, count);
        EmfBoundsCommit(emf, &r->rclBounds, &b);
        return TRUE;
    }
    EMRPOLYDRAW *r = EmfRecordAlloc(emf, EMR_POLYDRAW, offsetof(EMRPOLYDRAW, aptl) + count * 9);
    if (!r)
        return FALSE;
    r->cptl = count;
    for (DWORD i = 0; i < count; i++)
        r->aptl[i] = (POINTL){points[i].x, points[i].y};
    memcpy((BYTE *)(r->aptl + count), types, count);
    EmfBoundsCommit(emf, &r->rclBounds, &b);
    return TRUE;
}

BOOL MfSetPixel(HDC dc, int x, int y, COLORREF color)
{
    if (MfIsWmfDc(dc)) {
        WORD p[4] = {lo(color), hi(color), (WORD)y, (WORD)x};
        return WmfRecord(dc, META_SETPIXEL, 4, p);
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    EMRSETPIXELV *r = EmfRecordAlloc(emf, EMR_SETPIXELV, sizeof(*r));
    if (!r)
        return FALSE;
    r->ptlPixel = (POINTL){x, y};
    r->crColor = color;
    XFORM m;
    EmfWorldToDevice(emf, &m);
    EMF_BOUNDS b;
    EmfBoundsInit(&b);
    add_logical(&b, &m, x, y);
    commit_header(emf, &b);
    return TRUE;
}

BOOL MfExtFloodFill(HDC dc, int x, int y, COLORREF color, UINT type)
{
    if (MfIsWmfDc(dc)) {
        WORD p[5] = {(WORD)type, lo(color), hi(color), (WORD)y, (WORD)x};
        return WmfRecord(dc, META_EXTFLOODFILL, 5, p);
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    LONG *r = EmfRecordAlloc(emf, EMR_EXTFLOODFILL, 24);
    if (!r)
        return FALSE;
    r[2] = x;
    r[3] = y;
    r[4] = (LONG)color;
    r[5] = (LONG)type;
    return TRUE;
}

/* ------------------------------------------------------------------ paths */

/* the device bounds of the closed path, through GetPath before the path goes away. */
static void path_bounds(EMF_DC *emf, EMF_BOUNDS *b, BOOL stroke, BOOL fill)
{
    EmfBoundsInit(b);
    int n = NtGdiGetPath(emf->Dc, nullptr, nullptr, 0);
    if (n <= 0)
        return;
    POINT *pts = heap_alloc((SIZE_T)n * sizeof(POINT));
    BYTE *types = heap_alloc((SIZE_T)n);
    XFORM m;
    EmfWorldToDevice(emf, &m);
    if (pts && types && NtGdiGetPath(emf->Dc, pts, types, n) == n) {
        double half = stroke ? pen_half(emf, &m) : 0;
        if (half >= 0 || fill) {
            for (int i = 0; i < n; i++)
                add_logical(b, &m, pts[i].x, pts[i].y);
            if (half > 0)
                EmfBoundsInflate(b, half);
        }
    }
    heap_free(pts);
    heap_free(types);
}

BOOL MfPath(HDC dc, UINT emr)
{
    if (MfIsWmfDc(dc))
        return FALSE;
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    if (emr != EMR_FILLPATH && emr != EMR_STROKEPATH && emr != EMR_STROKEANDFILLPATH)
        return EmfRecordAlloc(emf, emr, 8) != nullptr;
    EMF_BOUNDS b;
    path_bounds(emf, &b, emr != EMR_FILLPATH, emr != EMR_STROKEPATH);
    EMRFILLPATH *r = EmfRecordAlloc(emf, emr, sizeof(*r));
    if (!r)
        return FALSE;
    EmfBoundsCommit(emf, &r->rclBounds, &b);
    return TRUE;
}

/* ------------------------------------------------------------------ text */

/* GDI comment and text scale: hundredths of millimeters per logical unit in the compatible mode, 0 otherwise. */
static void text_scale(EMF_DC *emf, FLOAT *ex, FLOAT *ey)
{
    *ex = *ey = 0;
    if (!compatible(emf))
        return;
    XFORM m;
    if (!NtGdiGetTransform(emf->Dc, MIME_XFORM_PAGE_TO_DEVICE, &m))
        return;
    ENHMETAHEADER *h = EmfHeader(emf);
    if (h->szlDevice.cx && h->szlDevice.cy) {
        *ex = (FLOAT)(m.eM11 * 100.0 * h->szlMillimeters.cx / h->szlDevice.cx);
        *ey = (FLOAT)(m.eM22 * 100.0 * h->szlMillimeters.cy / h->szlDevice.cy);
    }
}

/* the device box of a text run: the advances along the baseline, the cell around it, upright like win32k draws it. */
static void text_bounds(EMF_DC *emf, EMF_BOUNDS *b, int x, int y, UINT options, const RECT *rect, const INT *dx,
                        UINT count)
{
    EmfBoundsInit(b);
    XFORM m;
    EmfWorldToDevice(emf, &m);
    UINT step = (options & ETO_PDY) ? 2 : 1;
    if (count) {
        LONG width = 0;
        for (UINT i = 0; i < count; i++)
            width += dx[i * step];
        TEXTMETRICW tm;
        memset(&tm, 0, sizeof(tm));
        NtGdiGetTextMetricsW(emf->Dc, &tm, sizeof(tm));
        DWORD align = TA_LEFT;
        NtGdiGetDCDword(emf->Dc, MIME_DCDW_TEXTALIGN, &align);
        if (align & TA_UPDATECP) {
            POINT cur = {x, y};
            NtGdiGetDCPoint(emf->Dc, MIME_DCPT_CURRENTPOSITION, &cur);
            x = cur.x;
            y = cur.y;
        }
        double left = 0;
        if ((align & TA_CENTER) == TA_CENTER)
            left = -width / 2.0;
        else if (align & TA_RIGHT)
            left = -width;
        double top;
        switch (align & (TA_TOP | TA_BOTTOM | TA_BASELINE)) {
        case TA_BASELINE:
            top = -tm.tmAscent;
            break;
        case TA_BOTTOM:
            top = -tm.tmHeight;
            break;
        default:
            top = 0;
            break;
        }
        double ox, oy;
        EmfTransformPoint(&m, x, y, &ox, &oy);
        /* text stays upright: without rotation only the magnitudes of the scales count. */
        double a = m.eM11, bb = m.eM12, c = m.eM21, d = m.eM22;
        if (!bb && !c) {
            a = a < 0 ? -a : a;
            d = d < 0 ? -d : d;
        }
        double xs[2] = {left, left + width}, ys[2] = {top, top + tm.tmHeight};
        for (int i = 0; i < 2; i++)
            for (int k = 0; k < 2; k++)
                EmfBoundsPoint(b, ox + xs[i] * a + ys[k] * c, oy + xs[i] * bb + ys[k] * d);
        b->Right -= 1;
        b->Bottom -= 1;
    }
    if (rect && (options & ETO_OPAQUE))
        add_rect(b, &m, rect->left, rect->top, rect->right, rect->bottom, TRUE);
}

static BOOL wmf_text(HDC dc, int x, int y, UINT options, const RECT *rect, LPCSTR text, UINT count, const INT *dx)
{
    if (count > 0x7FF0)
        return FALSE;
    BOOL has_rect = rect && (options & (ETO_OPAQUE | ETO_CLIPPED));
    UINT words = 4 + (has_rect ? 4 : 0) + (count + 1) / 2 + (dx ? count : 0);
    WORD *p = heap_alloc(words * sizeof(WORD) + 2);
    if (!p)
        return FALSE;
    memset(p, 0, words * sizeof(WORD) + 2);
    UINT i = 0;
    p[i++] = (WORD)y;
    p[i++] = (WORD)x;
    p[i++] = (WORD)count;
    p[i++] = (WORD)options;
    if (has_rect) {
        p[i++] = (WORD)rect->left;
        p[i++] = (WORD)rect->top;
        p[i++] = (WORD)rect->right;
        p[i++] = (WORD)rect->bottom;
    }
    memcpy(p + i, text, count);
    i += (count + 1) / 2;
    for (UINT k = 0; dx && k < count; k++)
        p[i++] = (WORD)dx[(options & ETO_PDY) ? 2 * k : k];
    BOOL ok = WmfRecord(dc, META_EXTTEXTOUT, i, p);
    heap_free(p);
    return ok;
}

BOOL MfTextOut(HDC dc, int x, int y, UINT options, const RECT *rect, LPCWSTR text, UINT count, const INT *dx)
{
    if (MfIsWmfDc(dc)) {
        int n = count ? WideCharToMultiByte(CP_ACP, 0, text, (int)count, nullptr, 0, nullptr, nullptr) : 0;
        char *a = heap_alloc((SIZE_T)n + 1);
        if (!a)
            return FALSE;
        if (n)
            WideCharToMultiByte(CP_ACP, 0, text, (int)count, a, n, nullptr, nullptr);
        BOOL ok = wmf_text(dc, x, y, options & ~(UINT)ETO_GLYPH_INDEX, rect, a, (UINT)n,
                           (UINT)n == count ? dx : nullptr);
        heap_free(a);
        return ok;
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf || count > 0x1000000)
        return FALSE;
    UINT step = (options & ETO_PDY) ? 2 : 1;
    INT *adv = heap_alloc(((SIZE_T)count * step + 1) * sizeof(INT));
    if (!adv)
        return FALSE;
    if (dx) {
        memcpy(adv, dx, (SIZE_T)count * step * sizeof(INT));
    } else if (count) {
        /* the advances from the partial extents, as windows records them. */
        ULONG *ext = (ULONG *)adv;
        SIZE size;
        if (!NtGdiGetTextExtentExW(dc, text, count, 0, nullptr, ext, &size,
                                   (options & ETO_GLYPH_INDEX) ? MIME_GTEE_INDICES : 0))
            memset(adv, 0, (SIZE_T)count * sizeof(INT));
        for (UINT i = count - 1; i > 0; i--)
            adv[i] -= adv[i - 1];
        step = 1;
        options &= ~(UINT)ETO_PDY;
    }
    EMF_BOUNDS b;
    text_bounds(emf, &b, x, y, options, rect, adv, count);
    FLOAT ex, ey;
    text_scale(emf, &ex, &ey);
    DWORD text_size = ((DWORD)count * 2 + 3) & ~3u;
    DWORD size = sizeof(EMREXTTEXTOUTW) + text_size + count * 4 * step;
    EMREXTTEXTOUTW *r = EmfRecordAlloc(emf, EMR_EXTTEXTOUTW, size);
    if (!r) {
        heap_free(adv);
        return FALSE;
    }
    r->iGraphicsMode = compatible(emf) ? GM_COMPATIBLE : GM_ADVANCED;
    r->exScale = ex;
    r->eyScale = ey;
    r->emrtext.ptlReference = (POINTL){x, y};
    r->emrtext.nChars = count;
    r->emrtext.offString = sizeof(EMREXTTEXTOUTW);
    r->emrtext.fOptions = options;
    r->emrtext.rcl = rect && (options & (ETO_OPAQUE | ETO_CLIPPED))
                         ? (RECTL){rect->left, rect->top, rect->right, rect->bottom}
                         : (RECTL){0, 0, -1, -1};
    r->emrtext.offDx = sizeof(EMREXTTEXTOUTW) + text_size;
    memcpy((BYTE *)r + r->emrtext.offString, text, (SIZE_T)count * 2);
    memcpy((BYTE *)r + r->emrtext.offDx, adv, (SIZE_T)count * 4 * step);
    heap_free(adv);
    EmfBoundsCommit(emf, &r->rclBounds, &b);
    return TRUE;
}

/* windows records ansi text as unicode in enhanced metafiles. */
BOOL MfTextOutA(HDC dc, int x, int y, UINT options, const RECT *rect, LPCSTR text, UINT count, const INT *dx)
{
    if (MfIsWmfDc(dc))
        return wmf_text(dc, x, y, options, rect, text, count, dx);
    int n = count ? MultiByteToWideChar(CP_ACP, 0, text, (int)count, nullptr, 0) : 0;
    LPWSTR w = heap_alloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!w)
        return FALSE;
    if (n)
        MultiByteToWideChar(CP_ACP, 0, text, (int)count, w, n);
    BOOL ok = MfTextOut(dc, x, y, options, rect, w, (UINT)n, (UINT)n == count ? dx : nullptr);
    heap_free(w);
    return ok;
}

/* ------------------------------------------------------------------ bitmaps */

static BOOL rop_uses_source(DWORD rop)
{
    return ((rop ^ (rop << 2)) & 0xCCCC0000) != 0;
}

/*
 * the pixels of a source dc area as a bottom up 32 bpp dib: the area in
 * device units, clipped to the source bitmap, and the transform from the
 * source's logical coordinates to the captured pixels.
 */
typedef struct CAPTURE {
    BITMAPINFOHEADER Info;
    BYTE *Bits;
    XFORM Xform;
    COLORREF BkColor;
} CAPTURE;

static BOOL capture_source(HDC src, int sx, int sy, int sw, int sh, CAPTURE *c, BOOL *empty)
{
    *empty = FALSE;
    memset(c, 0, sizeof(*c));
    if (MfActive(src))
        return FALSE;
    XFORM m;
    if (!NtGdiGetTransform(src, MIME_XFORM_WORLD_TO_DEVICE, &m) || m.eM12 || m.eM21)
        return FALSE;
    double x0, y0, x1, y1;
    EmfTransformPoint(&m, sx, sy, &x0, &y0);
    EmfTransformPoint(&m, (double)sx + sw, (double)sy + sh, &x1, &y1);
    LONG l = (LONG)(x0 < x1 ? x0 : x1), t = (LONG)(y0 < y1 ? y0 : y1);
    LONG r = (LONG)(x0 < x1 ? x1 + 0.999 : x0 + 0.999), b = (LONG)(y0 < y1 ? y1 + 0.999 : y0 + 0.999);
    LONG width = 0, height = 0;
    HGDIOBJ bitmap = NtGdiGetDCObject(src, OBJ_BITMAP);
    BITMAP bm;
    if (bitmap && NtGdiExtGetObjectW(bitmap, sizeof(bm), &bm)) {
        width = bm.bmWidth;
        height = bm.bmHeight;
    } else {
        width = NtGdiGetDeviceCaps(src, DESKTOPHORZRES);
        height = NtGdiGetDeviceCaps(src, DESKTOPVERTRES);
    }
    if (l < 0)
        l = 0;
    if (t < 0)
        t = 0;
    if (r > width)
        r = width;
    if (b > height)
        b = height;
    if (r <= l || b <= t) {
        *empty = TRUE;
        return FALSE;
    }
    LONG cw = r - l, ch = b - t;
    if ((UINT64)cw * ch > 0x4000000)
        return FALSE;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cw;
    bi.bmiHeader.biHeight = -ch;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HDC mem = NtGdiCreateCompatibleDC(nullptr);
    void *pixels = nullptr;
    HBITMAP dib = mem ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    BOOL ok = FALSE;
    if (dib && pixels) {
        HGDIOBJ old = NtGdiSelectBitmap(mem, dib);
        /* the source in device units for the copy. */
        int saved = NtGdiSaveDC(src);
        DWORD prev;
        NtGdiGetAndSetDCDword(src, MIME_DCDW_GRAPHICSMODE, GM_ADVANCED, &prev);
        NtGdiModifyWorldTransform(src, nullptr, MWT_IDENTITY);
        NtGdiGetAndSetDCDword(src, MIME_DCDW_MAPMODE, MM_TEXT, &prev);
        NtGdiSetWindowOrgEx(src, 0, 0, nullptr);
        NtGdiSetViewportOrgEx(src, 0, 0, nullptr);
        ok = NtGdiBitBlt(mem, 0, 0, cw, ch, src, l, t, SRCCOPY, 0, 0);
        if (saved)
            NtGdiRestoreDC(src, -1);
        NtGdiSelectBitmap(mem, old);
        if (ok) {
            c->Bits = heap_alloc((SIZE_T)cw * ch * 4);
            ok = c->Bits != nullptr;
        }
        if (ok) {
            /* bottom up, as windows stores them. */
            for (LONG row = 0; row < ch; row++)
                memcpy(c->Bits + (SIZE_T)(ch - 1 - row) * cw * 4, (BYTE *)pixels + (SIZE_T)row * cw * 4,
                       (SIZE_T)cw * 4);
        }
    }
    if (dib)
        NtGdiDeleteObjectApp(dib);
    if (mem)
        NtGdiDeleteObjectApp(mem);
    if (!ok)
        return FALSE;
    c->Info = bi.bmiHeader;
    c->Info.biHeight = ch;
    c->Info.biSizeImage = (DWORD)cw * ch * 4;
    c->Xform = m;
    c->Xform.eDx -= (FLOAT)l;
    c->Xform.eDy -= (FLOAT)t;
    DWORD bk = 0xFFFFFF;
    NtGdiGetDCDword(src, MIME_DCDW_BKCOLOR, &bk);
    c->BkColor = bk;
    return TRUE;
}

static void dest_bounds(EMF_DC *emf, EMF_BOUNDS *b, int x, int y, int width, int height)
{
    XFORM m;
    EmfWorldToDevice(emf, &m);
    EmfBoundsInit(b);
    add_rect(b, &m, x, y, (double)x + width, (double)y + height, TRUE);
}

static BOOL wmf_blt(HDC dc, int x, int y, int width, int height, HDC src, int sx, int sy, int sw, int sh, DWORD rop)
{
    if (!src || !rop_uses_source(rop)) {
        WORD p[6] = {lo(rop), hi(rop), (WORD)height, (WORD)width, (WORD)y, (WORD)x};
        return WmfRecord(dc, META_PATBLT, 6, p);
    }
    CAPTURE c;
    BOOL empty;
    if (!capture_source(src, sx, sy, sw, sh, &c, &empty))
        return empty;
    /* the capture starts where the source does: a stretch from all of it. */
    WORD p[10] = {lo(rop), hi(rop), (WORD)c.Info.biHeight, (WORD)c.Info.biWidth, 0, 0, (WORD)height, (WORD)width,
                  (WORD)y, (WORD)x};
    BOOL ok = WmfDibRecord(dc, META_DIBSTRETCHBLT, p, 10, (const BITMAPINFO *)&c.Info,
                           sizeof(BITMAPINFOHEADER), c.Bits, c.Info.biSizeImage);
    heap_free(c.Bits);
    return ok;
}

/*
 * bit blt, stretch blt, alpha blend and transparent blt. a raster
 * operation without a source records a bit blt without a bitmap, like
 * windows; for the blends rop is the blend function or the transparent
 * color.
 */
BOOL MfBitBlt(HDC dc, int x, int y, int width, int height, HDC src, int sx, int sy, int sw, int sh, DWORD rop,
              UINT emr)
{
    BOOL blend = emr == EMR_ALPHABLEND || emr == EMR_TRANSPARENTBLT;
    if (MfIsWmfDc(dc))
        return blend ? FALSE : wmf_blt(dc, x, y, width, height, src, sx, sy, sw, sh, rop);
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    EMF_BOUNDS b;
    dest_bounds(emf, &b, x, y, width, height);
    if (!blend && (!src || !rop_uses_source(rop))) {
        EMRBITBLT *r = EmfRecordAlloc(emf, EMR_BITBLT, sizeof(EMRBITBLT));
        if (!r)
            return FALSE;
        r->xDest = x;
        r->yDest = y;
        r->cxDest = width;
        r->cyDest = height;
        r->dwRop = rop;
        r->xSrc = sx;
        r->ySrc = sy;
        r->xformSrc = (XFORM){1, 0, 0, 1, 0, 0};
        EmfBoundsCommit(emf, &r->rclBounds, &b);
        return TRUE;
    }
    CAPTURE c;
    BOOL empty;
    if (!capture_source(src, sx, sy, sw, sh, &c, &empty))
        return empty;
    DWORD head = emr == EMR_BITBLT ? sizeof(EMRBITBLT) : sizeof(EMRSTRETCHBLT);
    DWORD size = head + sizeof(BITMAPINFOHEADER) + c.Info.biSizeImage;
    EMRSTRETCHBLT *r = EmfRecordAlloc(emf, emr, size);
    if (!r) {
        heap_free(c.Bits);
        return FALSE;
    }
    r->xDest = x;
    r->yDest = y;
    r->cxDest = width;
    r->cyDest = height;
    r->dwRop = rop;
    r->xSrc = sx;
    r->ySrc = sy;
    r->xformSrc = c.Xform;
    r->crBkColorSrc = c.BkColor;
    r->iUsageSrc = DIB_RGB_COLORS;
    r->offBmiSrc = head;
    r->cbBmiSrc = sizeof(BITMAPINFOHEADER);
    r->offBitsSrc = head + sizeof(BITMAPINFOHEADER);
    r->cbBitsSrc = c.Info.biSizeImage;
    if (emr != EMR_BITBLT) {
        r->cxSrc = sw;
        r->cySrc = sh;
    }
    memcpy((BYTE *)r + r->offBmiSrc, &c.Info, sizeof(BITMAPINFOHEADER));
    memcpy((BYTE *)r + r->offBitsSrc, c.Bits, c.Info.biSizeImage);
    heap_free(c.Bits);
    EmfBoundsCommit(emf, &r->rclBounds, &b);
    return TRUE;
}

static DWORD info_size(const BITMAPINFO *info, UINT usage)
{
    return info->bmiHeader.biSize + GdiDibTableSize(&info->bmiHeader, usage);
}

static DWORD bits_size(const BITMAPINFO *info, UINT lines)
{
    const BITMAPINFOHEADER *h = &info->bmiHeader;
    if (h->biCompression != BI_RGB && h->biCompression != BI_BITFIELDS)
        return h->biSizeImage;
    DWORD stride = (DWORD)((((h->biWidth < 0 ? -h->biWidth : h->biWidth) * h->biBitCount + 31) / 32) * 4);
    return stride * lines;
}

BOOL MfStretchDIBits(HDC dc, int x, int y, int width, int height, int sx, int sy, int sw, int sh, const void *bits,
                     const BITMAPINFO *info, UINT usage, DWORD rop)
{
    BOOL source = rop_uses_source(rop) && bits && info;
    DWORD isize = source ? info_size(info, usage) : 0;
    DWORD bsize = 0;
    if (source) {
        LONG h = info->bmiHeader.biHeight;
        bsize = bits_size(info, (UINT)(h < 0 ? -h : h));
    }
    if (MfIsWmfDc(dc)) {
        WORD p[11] = {lo(rop), hi(rop), (WORD)usage, (WORD)sh, (WORD)sw, (WORD)sy, (WORD)sx, (WORD)height,
                      (WORD)width, (WORD)y, (WORD)x};
        return WmfDibRecord(dc, META_STRETCHDIB, p, 11, source ? info : nullptr, isize, bits, bsize);
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    EMF_BOUNDS b;
    dest_bounds(emf, &b, x, y, width, height);
    EMRSTRETCHDIBITS *r = EmfRecordAlloc(emf, EMR_STRETCHDIBITS, sizeof(EMRSTRETCHDIBITS) + isize + bsize);
    if (!r)
        return FALSE;
    r->xDest = x;
    r->yDest = y;
    r->xSrc = sx;
    r->ySrc = sy;
    r->cxSrc = sw;
    r->cySrc = sh;
    r->iUsageSrc = usage;
    r->dwRop = rop;
    r->cxDest = width;
    r->cyDest = height;
    if (source) {
        r->offBmiSrc = sizeof(EMRSTRETCHDIBITS);
        r->cbBmiSrc = isize;
        r->offBitsSrc = sizeof(EMRSTRETCHDIBITS) + isize;
        r->cbBitsSrc = bsize;
        memcpy((BYTE *)r + r->offBmiSrc, info, isize);
        memcpy((BYTE *)r + r->offBitsSrc, bits, bsize);
    }
    EmfBoundsCommit(emf, &r->rclBounds, &b);
    return TRUE;
}

BOOL MfSetDIBitsToDevice(HDC dc, int x, int y, DWORD width, DWORD height, int sx, int sy, UINT start, UINT lines,
                         const void *bits, const BITMAPINFO *info, UINT usage)
{
    if (!bits || !info)
        return FALSE;
    DWORD isize = info_size(info, usage), bsize = bits_size(info, lines);
    if (MfIsWmfDc(dc)) {
        WORD p[9] = {(WORD)usage, (WORD)lines, (WORD)start, (WORD)sy, (WORD)sx, (WORD)height, (WORD)width, (WORD)y,
                     (WORD)x};
        return WmfDibRecord(dc, META_SETDIBTODEV, p, 9, info, isize, bits, bsize);
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    /* the destination is a device rectangle at the logical point. */
    XFORM m;
    EmfWorldToDevice(emf, &m);
    double ox, oy;
    EmfTransformPoint(&m, x, y, &ox, &oy);
    EMF_BOUNDS b;
    EmfBoundsInit(&b);
    if (width && height) {
        EmfBoundsPoint(&b, ox, oy);
        EmfBoundsPoint(&b, ox + width - 1, oy + height - 1);
    }
    EMRSETDIBITSTODEVICE *r = EmfRecordAlloc(emf, EMR_SETDIBITSTODEVICE, sizeof(EMRSETDIBITSTODEVICE) + isize + bsize);
    if (!r)
        return FALSE;
    r->xDest = x;
    r->yDest = y;
    r->xSrc = sx;
    r->ySrc = sy;
    r->cxSrc = (LONG)width;
    r->cySrc = (LONG)height;
    r->offBmiSrc = sizeof(EMRSETDIBITSTODEVICE);
    r->cbBmiSrc = isize;
    r->offBitsSrc = sizeof(EMRSETDIBITSTODEVICE) + isize;
    r->cbBitsSrc = bsize;
    r->iUsageSrc = usage;
    r->iStartScan = start;
    r->cScans = lines;
    memcpy((BYTE *)r + r->offBmiSrc, info, isize);
    memcpy((BYTE *)r + r->offBitsSrc, bits, bsize);
    EmfBoundsCommit(emf, &r->rclBounds, &b);
    return TRUE;
}

BOOL MfGradientFill(HDC dc, const TRIVERTEX *vertices, ULONG count, const void *mesh, ULONG mesh_count, ULONG mode)
{
    if (MfIsWmfDc(dc))
        return FALSE;
    EMF_DC *emf = EmfDc(dc);
    if (!emf || count > 0x100000 || mesh_count > 0x100000)
        return FALSE;
    DWORD mesh_size = mode == GRADIENT_FILL_TRIANGLE ? mesh_count * sizeof(GRADIENT_TRIANGLE)
                                                     : mesh_count * sizeof(GRADIENT_RECT);
    XFORM m;
    EmfWorldToDevice(emf, &m);
    EMF_BOUNDS b;
    EmfBoundsInit(&b);
    for (ULONG i = 0; i < count; i++)
        add_logical(&b, &m, vertices[i].x, vertices[i].y);
    if (!b.Empty) {
        b.Right -= 1;
        b.Bottom -= 1;
    }
    EMRGRADIENTFILL *r =
        EmfRecordAlloc(emf, EMR_GRADIENTFILL, offsetof(EMRGRADIENTFILL, Ver) + count * sizeof(TRIVERTEX) + mesh_size);
    if (!r)
        return FALSE;
    r->nVer = count;
    r->nTri = mesh_count;
    r->ulMode = mode;
    memcpy(r->Ver, vertices, count * sizeof(TRIVERTEX));
    memcpy((BYTE *)(r->Ver + count), mesh, mesh_size);
    EmfBoundsCommit(emf, &r->rclBounds, &b);
    return TRUE;
}

/* ------------------------------------------------------------------ regions */

BOOL MfRegion(HDC dc, UINT emr, UINT meta, HRGN region, HBRUSH brush, int width, int height)
{
    if (MfIsWmfDc(dc))
        return FALSE;
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    DWORD size = NtGdiGetRegionData(region, 0, nullptr);
    if (!size)
        return FALSE;
    DWORD brush_index = 0;
    if (emr == EMR_FILLRGN || emr == EMR_FRAMERGN) {
        brush_index = EmfObjectIndex(emf, brush, nullptr);
        if (!brush_index)
            return FALSE;
    }
    RECT box = {0, 0, 0, 0};
    NtGdiGetRgnBox(region, &box);
    XFORM m;
    EmfWorldToDevice(emf, &m);
    EMF_BOUNDS b;
    EmfBoundsInit(&b);
    add_rect(&b, &m, box.left, box.top, box.right, box.bottom, TRUE);
    DWORD head = emr == EMR_FILLRGN ? offsetof(EMRFILLRGN, RgnData)
                 : emr == EMR_FRAMERGN ? offsetof(EMRFRAMERGN, RgnData)
                                       : offsetof(EMRINVERTRGN, RgnData);
    BYTE *r = EmfRecordAlloc(emf, emr, head + size);
    if (!r)
        return FALSE;
    RECTL *bounds = (RECTL *)(r + 8);
    *(DWORD *)(r + 24) = size;
    if (emr == EMR_FILLRGN || emr == EMR_FRAMERGN)
        *(DWORD *)(r + 28) = brush_index;
    if (emr == EMR_FRAMERGN)
        ((EMRFRAMERGN *)r)->szlStroke = (SIZEL){width, height};
    if (NtGdiGetRegionData(region, size, (LPRGNDATA)(r + head)) != size) {
        emf->Failed = TRUE;
        return FALSE;
    }
    EmfBoundsCommit(emf, bounds, &b);
    return TRUE;
}

/* ------------------------------------------------------------------ comments */

BOOL MfComment(HDC dc, UINT size, const BYTE *data)
{
    if (size && !data)
        return FALSE;
    if (MfIsWmfDc(dc)) {
        if (size > 0xFFF0)
            return FALSE;
        WORD *p = heap_alloc(4 + (SIZE_T)size + 2);
        if (!p)
            return FALSE;
        p[0] = MFCOMMENT;
        p[1] = (WORD)size;
        memcpy(p + 2, data, size);
        if (size & 1)
            ((BYTE *)(p + 2))[size] = 0;
        BOOL ok = WmfRecord(dc, META_ESCAPE, 2 + (size + 1) / 2, p);
        heap_free(p);
        return ok;
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf || size > 0x7FFFFF00)
        return FALSE;
    DWORD ident = 0, type = 0;
    if (size >= 8) {
        memcpy(&ident, data, 4);
        memcpy(&type, data + 4, 4);
    }
    /* windows keeps the public comments with the high bit for itself. */
    if (ident == GDICOMMENT_IDENTIFIER && (type & 0x80000000))
        return TRUE;
    if (ident == GDICOMMENT_IDENTIFIER && (type & 0x40000000)) {
        if (size < 24)
            return FALSE;
        /* a comment with an output rectangle adds it to the bounds. */
        RECTL out;
        memcpy(&out, data + 8, sizeof(out));
        XFORM m;
        EmfWorldToDevice(emf, &m);
        EMF_BOUNDS b;
        EmfBoundsInit(&b);
        add_rect(&b, &m, out.left, out.top, out.right, out.bottom, FALSE);
        commit_header(emf, &b);
    }
    EMRGDICOMMENT *r = EmfRecordAlloc(emf, EMR_GDICOMMENT, offsetof(EMRGDICOMMENT, Data) + size);
    if (!r)
        return FALSE;
    r->cbData = size;
    memcpy(r->Data, data, size);
    return TRUE;
}
