/*
 * gdi32 paths, world transforms, device dependent bitmap bits, printing
 * and pixel formats.
 */
#include "gdi32_private.h"
#include "gdimath.h"
#include <mime/pixelformat.h>
#include <mime/stub.h>

/* ------------------------------------------------------------------ paths */

WINGDIAPI BOOL WINAPI BeginPath(HDC dc)
{
    MF_RECORD(dc, MfPath(dc, EMR_BEGINPATH), FALSE, FALSE);
    return NtGdiBeginPath(dc);
}

WINGDIAPI BOOL WINAPI EndPath(HDC dc)
{
    MF_RECORD(dc, MfPath(dc, EMR_ENDPATH), FALSE, FALSE);
    return NtGdiEndPath(dc);
}

WINGDIAPI BOOL WINAPI AbortPath(HDC dc)
{
    MF_RECORD(dc, MfPath(dc, EMR_ABORTPATH), FALSE, FALSE);
    return NtGdiAbortPath(dc);
}

WINGDIAPI BOOL WINAPI CloseFigure(HDC dc)
{
    MF_RECORD(dc, MfPath(dc, EMR_CLOSEFIGURE), FALSE, FALSE);
    return NtGdiCloseFigure(dc);
}

WINGDIAPI BOOL WINAPI FillPath(HDC dc)
{
    MF_RECORD(dc, MfPath(dc, EMR_FILLPATH), FALSE, FALSE);
    return NtGdiFillPath(dc);
}

WINGDIAPI BOOL WINAPI StrokePath(HDC dc)
{
    MF_RECORD(dc, MfPath(dc, EMR_STROKEPATH), FALSE, FALSE);
    return NtGdiStrokePath(dc);
}

WINGDIAPI BOOL WINAPI StrokeAndFillPath(HDC dc)
{
    MF_RECORD(dc, MfPath(dc, EMR_STROKEANDFILLPATH), FALSE, FALSE);
    return NtGdiStrokeAndFillPath(dc);
}

WINGDIAPI BOOL WINAPI FlattenPath(HDC dc)
{
    MF_RECORD(dc, MfPath(dc, EMR_FLATTENPATH), FALSE, FALSE);
    return NtGdiFlattenPath(dc);
}

WINGDIAPI BOOL WINAPI SelectClipPath(HDC dc, int mode)
{
    MF_RECORD(dc, MfSelectClipPath(dc, mode), FALSE, FALSE);
    return NtGdiSelectClipPath(dc, mode);
}

WINGDIAPI BOOL WINAPI WidenPath(HDC dc)
{
    MF_RECORD(dc, MfPath(dc, EMR_WIDENPATH), FALSE, FALSE);
    return NtGdiWidenPath(dc);
}

/* the limit is passed as the bits of a float, like windows does. */
WINGDIAPI BOOL WINAPI SetMiterLimit(HDC dc, FLOAT limit, PFLOAT old)
{
    MF_RECORD(dc, MfSetMiterLimit(dc, limit), FALSE, FALSE);
    DWORD bits;
    memcpy(&bits, &limit, sizeof(bits));
    return NtGdiSetMiterLimit(dc, bits, (DWORD *)old);
}

WINGDIAPI BOOL WINAPI GetMiterLimit(HDC dc, PFLOAT limit)
{
    return NtGdiGetMiterLimit(dc, (DWORD *)limit);
}

WINGDIAPI HRGN WINAPI PathToRegion(HDC dc)
{
    return NtGdiPathToRegion(dc);
}

WINGDIAPI int WINAPI GetPath(HDC dc, LPPOINT points, LPBYTE types, int count)
{
    if (count < 0 || (count && (!points || !types))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    return NtGdiGetPath(dc, points, types, count);
}

static BOOL poly_draw(HDC dc, const POINT *points, DWORD count, int function)
{
    ULONG n = count;
    if (!points || !count)
        return FALSE;
    return NtGdiPolyPolyDraw(dc, (POINT *)points, &n, 1, function) != 0;
}

static BOOL poly_record(HDC dc, const POINT *points, DWORD count, UINT emr)
{
    return points && count && MfPoly(dc, emr, 0, points, count);
}

WINGDIAPI BOOL WINAPI PolyBezier(HDC dc, const POINT *points, DWORD count)
{
    if (count < 4 || (count - 1) % 3) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MF_RECORD(dc, poly_record(dc, points, count, EMR_POLYBEZIER), FALSE, FALSE);
    return poly_draw(dc, points, count, MIME_POLY_BEZIER);
}

WINGDIAPI BOOL WINAPI PolyBezierTo(HDC dc, const POINT *points, DWORD count)
{
    if (!count || count % 3) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MF_RECORD(dc, poly_record(dc, points, count, EMR_POLYBEZIERTO), FALSE, FALSE);
    return poly_draw(dc, points, count, MIME_POLY_BEZIERTO);
}

WINGDIAPI BOOL WINAPI PolylineTo(HDC dc, const POINT *points, DWORD count)
{
    MF_RECORD(dc, poly_record(dc, points, count, EMR_POLYLINETO), FALSE, FALSE);
    return poly_draw(dc, points, count, MIME_POLY_LINETO);
}

static BOOL poly_draw_kernel(HDC dc, const POINT *points, const BYTE *types, int count);

/* like windows, every type is validated before any figure is drawn. */
WINGDIAPI BOOL WINAPI PolyDraw(HDC dc, const POINT *points, const BYTE *types, int count)
{
    if (count < 0 || (count && (!points || !types))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (int i = 0; i < count; i++) {
        BYTE type = types[i] & ~PT_CLOSEFIGURE;
        if (type == PT_BEZIERTO) {
            if (i + 2 >= count || (types[i + 1] & ~PT_CLOSEFIGURE) != PT_BEZIERTO ||
                (types[i + 2] & ~PT_CLOSEFIGURE) != PT_BEZIERTO)
                return FALSE;
            i += 2;
        } else if (type != PT_MOVETO && type != PT_LINETO) {
            return FALSE;
        }
    }
    if (MfActive(dc)) {
        if (!MfPolyDraw(dc, points, types, (DWORD)count) || MfIsWmfDc(dc))
            return FALSE;
        /* win32k keeps the current position and the path, so the figures go there unrecorded. */
        return poly_draw_kernel(dc, points, types, count);
    }
    return poly_draw_kernel(dc, points, types, count);
}

static BOOL poly_draw_kernel(HDC dc, const POINT *points, const BYTE *types, int count)
{
    DWORD state = 0;
    NtGdiGetDCDword(dc, MIME_DCDW_PATHSTATE, &state);
    POINT start;
    if (!NtGdiGetDCPoint(dc, MIME_DCPT_CURRENTPOSITION, &start))
        return FALSE;
    for (int i = 0; i < count; i++) {
        BYTE type = types[i] & ~PT_CLOSEFIGURE;
        BOOL ok;
        if (type == PT_MOVETO) {
            start = points[i];
            ok = NtGdiMoveTo(dc, points[i].x, points[i].y, nullptr);
        } else if (type == PT_LINETO) {
            ok = NtGdiLineTo(dc, points[i].x, points[i].y);
        } else {
            ok = poly_draw(dc, &points[i], 3, MIME_POLY_BEZIERTO);
            i += 2;
        }
        if (!ok)
            return FALSE;
        if (type != PT_MOVETO && (types[i] & PT_CLOSEFIGURE)) {
            if (state == 1)
                NtGdiCloseFigure(dc);
            else
                NtGdiLineTo(dc, start.x, start.y);
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------ arcs */

/*
 * elliptic arcs as cubic beziers of at most a quarter turn each. the box
 * gives the ellipse, the radials through the two points give the angles and
 * the dc's arc direction picks the way around. angles grow counterclockwise
 * on screen, with y down.
 */
typedef struct ARC {
    double Cx, Cy, Rx, Ry;
    double Start, Sweep;
} ARC;

static void ellipse_point(const ARC *a, double t, POINT *p)
{
    double s, c;
    GdiSinCos(t, &s, &c);
    *p = (POINT){(LONG)(a->Cx + a->Rx * c + (a->Cx + a->Rx * c >= 0 ? 0.5 : -0.5)),
                 (LONG)(a->Cy - a->Ry * s + (a->Cy - a->Ry * s >= 0 ? 0.5 : -0.5))};
}

static void arc_setup(HDC dc, ARC *a, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2)
{
    a->Cx = (left + right) / 2.0;
    a->Cy = (top + bottom) / 2.0;
    a->Rx = (right > left ? right - left : left - right) / 2.0;
    a->Ry = (bottom > top ? bottom - top : top - bottom) / 2.0;
    double rx = a->Rx ? a->Rx : 1, ry = a->Ry ? a->Ry : 1;
    double t1 = GdiAtan2(-(y1 - a->Cy) / ry, (x1 - a->Cx) / rx);
    double t2 = GdiAtan2(-(y2 - a->Cy) / ry, (x2 - a->Cx) / rx);
    DWORD dir = AD_COUNTERCLOCKWISE;
    NtGdiGetDCDword(dc, MIME_DCDW_ARCDIRECTION, &dir);
    double sweep = t2 - t1;
    if (dir == AD_CLOCKWISE) {
        while (sweep >= 0)
            sweep -= 2 * GDI_PI;
    } else {
        while (sweep <= 0)
            sweep += 2 * GDI_PI;
    }
    a->Start = t1;
    a->Sweep = sweep;
}

/* the points after the start, three per segment. */
static UINT arc_curves(const ARC *a, POINT *out)
{
    double sweep = a->Sweep < 0 ? -a->Sweep : a->Sweep;
    int segs = (int)(sweep / (GDI_PI / 2) + 0.999);
    if (segs < 1)
        segs = 1;
    if (segs > 16)
        segs = 16;
    double step = a->Sweep / segs, s, c;
    GdiSinCos(step / 4, &s, &c);
    double k = 4.0 / 3.0 * s / c;
    UINT n = 0;
    for (int i = 0; i < segs; i++) {
        double t0 = a->Start + step * i, t1 = t0 + step;
        double s0, c0, s1, c1;
        GdiSinCos(t0, &s0, &c0);
        GdiSinCos(t1, &s1, &c1);
        double x0 = a->Cx + a->Rx * c0, y0 = a->Cy - a->Ry * s0;
        double x3 = a->Cx + a->Rx * c1, y3 = a->Cy - a->Ry * s1;
        double xs[3] = {x0 - k * a->Rx * s0, x3 + k * a->Rx * s1, x3};
        double ys[3] = {y0 - k * a->Ry * c0, y3 + k * a->Ry * c1, y3};
        for (int j = 0; j < 3; j++)
            out[n++] = (POINT){(LONG)(xs[j] + (xs[j] >= 0 ? 0.5 : -0.5)), (LONG)(ys[j] + (ys[j] >= 0 ? 0.5 : -0.5))};
    }
    return n;
}

/* for chords and pies. */
static UINT arc_flat(const ARC *a, POINT *out, UINT max)
{
    UINT steps = max - 1;
    for (UINT i = 0; i <= steps; i++)
        ellipse_point(a, a->Start + a->Sweep * i / steps, &out[i]);
    return steps + 1;
}

/* sends figures to win32k, which keeps the current position and any open path. */
static BOOL arc_kernel(HDC dc, int kind, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2)
{
    ARC a;
    arc_setup(dc, &a, left, top, right, bottom, x1, y1, x2, y2);
    POINT pts[1 + 48 + 2];
    ellipse_point(&a, a.Start, &pts[0]);
    switch (kind) {
    case 0: /* arc. */
    case 1: { /* arc to. */
        UINT n = arc_curves(&a, pts + 1);
        if (kind == 1) {
            if (!NtGdiLineTo(dc, pts[0].x, pts[0].y))
                return FALSE;
            return poly_draw(dc, pts + 1, n, MIME_POLY_BEZIERTO);
        }
        return poly_draw(dc, pts, n + 1, MIME_POLY_BEZIER);
    }
    default: { /* chord and pie: one closed polygon, filled and outlined. */
        UINT n = arc_flat(&a, pts, 49);
        if (kind == 3)
            pts[n++] = (POINT){(LONG)a.Cx, (LONG)a.Cy};
        return poly_draw(dc, pts, n, MIME_POLY_POLYGON);
    }
    }
}

WINGDIAPI BOOL WINAPI Arc(HDC dc, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2)
{
    MF_RECORD(dc, MfArc(dc, EMR_ARC, META_ARC, left, top, right, bottom, x1, y1, x2, y2), FALSE, TRUE);
    return arc_kernel(dc, 0, left, top, right, bottom, x1, y1, x2, y2);
}

WINGDIAPI BOOL WINAPI ArcTo(HDC dc, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2)
{
    MF_RECORD(dc, MfArc(dc, EMR_ARCTO, 0, left, top, right, bottom, x1, y1, x2, y2), FALSE, TRUE);
    return arc_kernel(dc, 1, left, top, right, bottom, x1, y1, x2, y2);
}

WINGDIAPI BOOL WINAPI Chord(HDC dc, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2)
{
    MF_RECORD(dc, MfArc(dc, EMR_CHORD, META_CHORD, left, top, right, bottom, x1, y1, x2, y2), FALSE, TRUE);
    return arc_kernel(dc, 2, left, top, right, bottom, x1, y1, x2, y2);
}

WINGDIAPI BOOL WINAPI Pie(HDC dc, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2)
{
    MF_RECORD(dc, MfArc(dc, EMR_PIE, META_PIE, left, top, right, bottom, x1, y1, x2, y2), FALSE, TRUE);
    return arc_kernel(dc, 3, left, top, right, bottom, x1, y1, x2, y2);
}

/* a line to the start of a circular arc, then the arc. positive sweeps turn counterclockwise. */
WINGDIAPI BOOL WINAPI AngleArc(HDC dc, int x, int y, DWORD radius, FLOAT start, FLOAT sweep)
{
    if ((LONG)radius < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MF_RECORD(dc, MfAngleArc(dc, x, y, radius, start, sweep), FALSE, FALSE);
    ARC a = {x, y, radius, radius, start * GDI_PI / 180, sweep * GDI_PI / 180};
    if (a.Sweep > 2 * GDI_PI)
        a.Sweep = 2 * GDI_PI;
    if (a.Sweep < -2 * GDI_PI)
        a.Sweep = -2 * GDI_PI;
    POINT pts[1 + 48];
    ellipse_point(&a, a.Start, &pts[0]);
    if (!NtGdiLineTo(dc, pts[0].x, pts[0].y))
        return FALSE;
    if (!a.Sweep)
        return TRUE;
    UINT n = arc_curves(&a, pts + 1);
    return poly_draw(dc, pts + 1, n, MIME_POLY_BEZIERTO);
}

/* ------------------------------------------------------------------ world transform */

WINGDIAPI BOOL WINAPI SetWorldTransform(HDC dc, const XFORM *xform)
{
    if (!xform)
        return FALSE;
    MF_RECORD(dc, MfSetWorldTransform(dc, xform, MWT_SET, FALSE), FALSE, FALSE);
    return NtGdiModifyWorldTransform(dc, xform, MWT_SET);
}

WINGDIAPI BOOL WINAPI ModifyWorldTransform(HDC dc, const XFORM *xform, DWORD mode)
{
    if (mode != MWT_IDENTITY && !xform)
        return FALSE;
    MF_RECORD(dc, MfSetWorldTransform(dc, xform, mode, TRUE), FALSE, FALSE);
    return NtGdiModifyWorldTransform(dc, xform, mode);
}

WINGDIAPI BOOL WINAPI GetWorldTransform(HDC dc, LPXFORM xform)
{
    if (!xform)
        return FALSE;
    return NtGdiGetTransform(dc, MIME_XFORM_WORLD, xform);
}

/* ------------------------------------------------------------------ device dependent bits */

WINGDIAPI LONG WINAPI GetBitmapBits(HBITMAP bitmap, LONG size, LPVOID bits)
{
    if (size < 0)
        return 0;
    return NtGdiGetBitmapBits(bitmap, (ULONG)size, bits);
}

WINGDIAPI LONG WINAPI SetBitmapBits(HBITMAP bitmap, DWORD size, const void *bits)
{
    return NtGdiSetBitmapBits(bitmap, size, bits);
}

/* ------------------------------------------------------------------ escapes */

/* the display driver has no escapes. QUERYESCSUPPORT answers for the ones gdi32 handles itself. */
WINGDIAPI int WINAPI ExtEscape(HDC dc, int escape, int in_size, LPCSTR in, int out_size, LPSTR out)
{
    if (PrnDcCount) {
        BOOL handled;
        int result = PrnEscape(dc, escape, in_size, in, out_size, out, &handled);
        if (handled)
            return result;
    }
    if (escape == QUERYESCSUPPORT) {
        if (!in || in_size < 4)
            return 0;
        DWORD query;
        memcpy(&query, in, sizeof(query));
        return query == QUERYESCSUPPORT;
    }
    return 0;
}

WINGDIAPI int WINAPI Escape(HDC dc, int escape, int in_size, LPCSTR in, LPVOID out)
{
    return ExtEscape(dc, escape, in_size, in, out ? 0x10000 : 0, out);
}

/* ------------------------------------------------------------------ pixel formats */

#define FORMAT_COUNT MIME_PIXEL_FORMAT_COUNT
#define describe MimeDescribePixelFormat

WINGDIAPI int WINAPI DescribePixelFormat(HDC dc, int format, UINT size, LPPIXELFORMATDESCRIPTOR pfd)
{
    DWORD v;
    if (!NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!pfd)
        return FORMAT_COUNT;
    if (format < 1 || format > FORMAT_COUNT || size < sizeof(PIXELFORMATDESCRIPTOR)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    describe(format, pfd);
    return FORMAT_COUNT;
}

WINGDIAPI int WINAPI ChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR *want)
{
    if (!want || want->nSize != sizeof(PIXELFORMATDESCRIPTOR)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    DWORD v;
    if (!NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return MimeChoosePixelFormat(want);
}

WINGDIAPI BOOL WINAPI SetPixelFormat(HDC dc, int format, const PIXELFORMATDESCRIPTOR *pfd)
{
    if (format < 1 || format > FORMAT_COUNT) {
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
        return FALSE;
    }
    return NtGdiSetPixelFormat(dc, format);
}

WINGDIAPI int WINAPI GetPixelFormat(HDC dc)
{
    DWORD v = 0;
    if (!NtGdiGetDCDword(dc, MIME_DCDW_PIXELFORMAT, &v)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!v)
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
    return (int)v;
}

/* like windows, whichever opengl32 the process loaded does the present. */
WINGDIAPI BOOL WINAPI SwapBuffers(HDC dc)
{
    int format = GetPixelFormat(dc);
    if (!format)
        return FALSE;
    PIXELFORMATDESCRIPTOR pfd;
    describe(format, &pfd);
    if (!(pfd.dwFlags & PFD_DOUBLEBUFFER)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HMODULE gl = GetModuleHandleW(L"opengl32.dll");
    BOOL(WINAPI * swap)(HDC) = gl ? (BOOL(WINAPI *)(HDC))GetProcAddress(gl, "wglSwapBuffers") : nullptr;
    return swap ? swap(dc) : TRUE;
}
