/*
 * paths: figures recorded between BeginPath and EndPath, then filled,
 * stroked, turned into regions or selected as the clip region. points are
 * kept in device coordinates, transformed when they are recorded, like gdi.
 */
#include "../win32k.h"

#define PATH_CURVE_STEPS 16

/* ------------------------------------------------------------------ recording */

void PathFree(GDI_PATH *p)
{
    W32Free(p->Points);
    W32Free(p->Types);
    memset(p, 0, sizeof(*p));
}

BOOLEAN DcIsPathOpen(DC *dc)
{
    return dc->PathState == PATH_OPEN;
}

static BOOLEAN path_add(GDI_PATH *p, LONG x, LONG y, BYTE type)
{
    if (p->Count == p->Capacity) {
        ULONG cap = p->Capacity ? p->Capacity * 2 : 32;
        if (cap > 1024 * 1024)
            return FALSE;
        POINT *pts = W32Alloc(cap * sizeof(POINT));
        BYTE *types = W32Alloc(cap);
        if (!pts || !types) {
            W32Free(pts);
            W32Free(types);
            return FALSE;
        }
        if (p->Count) {
            memcpy(pts, p->Points, p->Count * sizeof(POINT));
            memcpy(types, p->Types, p->Count);
        }
        W32Free(p->Points);
        W32Free(p->Types);
        p->Points = pts;
        p->Types = types;
        p->Capacity = cap;
    }
    p->Points[p->Count] = (POINT){x, y};
    p->Types[p->Count] = type;
    p->Count++;
    return TRUE;
}

static void to_device(DC *dc, LONG *x, LONG *y)
{
    DcLogicalToDevice(dc, x, y);
}

/* starts a figure at the current position when the previous one ended. */
static BOOLEAN begin_figure(DC *dc)
{
    GDI_PATH *p = &dc->Path;
    if (!p->NewFigure && p->Count)
        return TRUE;
    LONG x = dc->State.CurrentPos.x, y = dc->State.CurrentPos.y;
    to_device(dc, &x, &y);
    p->NewFigure = FALSE;
    return path_add(p, x, y, PT_MOVETO);
}

BOOLEAN PathMoveTo(DC *dc, LONG x, LONG y)
{
    (void)x;
    (void)y;
    dc->Path.NewFigure = TRUE;
    return TRUE;
}

BOOLEAN PathLineTo(DC *dc, LONG x, LONG y)
{
    if (!begin_figure(dc))
        return FALSE;
    to_device(dc, &x, &y);
    return path_add(&dc->Path, x, y, PT_LINETO);
}

BOOLEAN PathBezierTo(DC *dc, const POINT *pts, ULONG count)
{
    if (count % 3 || !begin_figure(dc))
        return FALSE;
    for (ULONG i = 0; i < count; i++) {
        LONG x = pts[i].x, y = pts[i].y;
        to_device(dc, &x, &y);
        if (!path_add(&dc->Path, x, y, PT_BEZIERTO))
            return FALSE;
    }
    return TRUE;
}

BOOLEAN PathPolygon(DC *dc, const POINT *pts, ULONG count, BOOLEAN close)
{
    if (!count)
        return TRUE;
    for (ULONG i = 0; i < count; i++) {
        LONG x = pts[i].x, y = pts[i].y;
        to_device(dc, &x, &y);
        if (!path_add(&dc->Path, x, y, i ? PT_LINETO : PT_MOVETO))
            return FALSE;
    }
    if (close && count > 1) {
        dc->Path.Types[dc->Path.Count - 1] |= PT_CLOSEFIGURE;
        dc->Path.NewFigure = TRUE;
    } else {
        dc->Path.NewFigure = FALSE;
    }
    return TRUE;
}

BOOLEAN PathRectangle(DC *dc, LONG left, LONG top, LONG right, LONG bottom)
{
    if (left > right) {
        LONG t = left;
        left = right;
        right = t;
    }
    if (top > bottom) {
        LONG t = top;
        top = bottom;
        bottom = t;
    }
    if (dc->State.GraphicsMode == GM_COMPATIBLE) {
        right--;
        bottom--;
    }
    /* counterclockwise, as windows records rectangles with the default arc direction. */
    POINT pts[4] = {{right, top}, {left, top}, {left, bottom}, {right, bottom}};
    return PathPolygon(dc, pts, 4, TRUE);
}

BOOLEAN PathEllipse(DC *dc, LONG left, LONG top, LONG right, LONG bottom)
{
    /* four cubic arcs with the usual 0.5523 control distance, in 1/1000 units. */
    LONG cx = (left + right) / 2, cy = (top + bottom) / 2;
    LONG rx = (right - left) / 2, ry = (bottom - top) / 2;
    LONG kx = rx * 552 / 1000, ky = ry * 552 / 1000;
    POINT start = {right, cy};
    POINT curves[12] = {
        {right, cy - ky}, {cx + kx, top},    {cx, top},
        {cx - kx, top},   {left, cy - ky},   {left, cy},
        {left, cy + ky},  {cx - kx, bottom}, {cx, bottom},
        {cx + kx, bottom}, {right, cy + ky}, {right, cy},
    };
    LONG x = start.x, y = start.y;
    to_device(dc, &x, &y);
    if (!path_add(&dc->Path, x, y, PT_MOVETO))
        return FALSE;
    dc->Path.NewFigure = FALSE;
    for (ULONG i = 0; i < 12; i++) {
        x = curves[i].x;
        y = curves[i].y;
        to_device(dc, &x, &y);
        if (!path_add(&dc->Path, x, y, PT_BEZIERTO))
            return FALSE;
    }
    dc->Path.Types[dc->Path.Count - 1] |= PT_CLOSEFIGURE;
    dc->Path.NewFigure = TRUE;
    return TRUE;
}

/* ------------------------------------------------------------------ flattening */

typedef struct POLYGONS {
    POINT *Points;
    ULONG *Counts;
    BOOLEAN *Closed;
    ULONG PointCount;
    ULONG PolygonCount;
    ULONG PointCapacity;
    ULONG PolygonCapacity;
} POLYGONS;

static BOOLEAN poly_point(POLYGONS *pg, LONG x, LONG y)
{
    if (pg->PointCount == pg->PointCapacity) {
        ULONG cap = pg->PointCapacity ? pg->PointCapacity * 2 : 64;
        POINT *pts = W32Alloc(cap * sizeof(POINT));
        if (!pts)
            return FALSE;
        if (pg->PointCount)
            memcpy(pts, pg->Points, pg->PointCount * sizeof(POINT));
        W32Free(pg->Points);
        pg->Points = pts;
        pg->PointCapacity = cap;
    }
    pg->Points[pg->PointCount++] = (POINT){x, y};
    pg->Counts[pg->PolygonCount - 1]++;
    return TRUE;
}

static BOOLEAN poly_begin(POLYGONS *pg)
{
    if (pg->PolygonCount == pg->PolygonCapacity) {
        ULONG cap = pg->PolygonCapacity ? pg->PolygonCapacity * 2 : 8;
        ULONG *counts = W32Alloc(cap * sizeof(ULONG));
        BOOLEAN *closed = W32Alloc(cap * sizeof(BOOLEAN));
        if (!counts || !closed) {
            W32Free(counts);
            W32Free(closed);
            return FALSE;
        }
        if (pg->PolygonCount) {
            memcpy(counts, pg->Counts, pg->PolygonCount * sizeof(ULONG));
            memcpy(closed, pg->Closed, pg->PolygonCount * sizeof(BOOLEAN));
        }
        W32Free(pg->Counts);
        W32Free(pg->Closed);
        pg->Counts = counts;
        pg->Closed = closed;
        pg->PolygonCapacity = cap;
    }
    pg->Counts[pg->PolygonCount] = 0;
    pg->Closed[pg->PolygonCount] = FALSE;
    pg->PolygonCount++;
    return TRUE;
}

static void poly_free(POLYGONS *pg)
{
    W32Free(pg->Points);
    W32Free(pg->Counts);
    W32Free(pg->Closed);
    memset(pg, 0, sizeof(*pg));
}

/* turns the path into polylines, curves flattened, with the figures that were closed marked. */
static BOOLEAN flatten(const GDI_PATH *p, POLYGONS *pg)
{
    memset(pg, 0, sizeof(*pg));
    for (ULONG i = 0; i < p->Count;) {
        BYTE type = p->Types[i] & ~PT_CLOSEFIGURE;
        if (type == PT_MOVETO || !pg->PolygonCount) {
            if (!poly_begin(pg) || !poly_point(pg, p->Points[i].x, p->Points[i].y))
                goto fail;
            if (p->Types[i] & PT_CLOSEFIGURE)
                pg->Closed[pg->PolygonCount - 1] = TRUE;
            i++;
            continue;
        }
        if (type == PT_BEZIERTO && i + 2 < p->Count) {
            POINT p0 = pg->Points[pg->PointCount - 1], c1 = p->Points[i], c2 = p->Points[i + 1], p3 = p->Points[i + 2];
            for (int s = 1; s <= PATH_CURVE_STEPS; s++) {
                LONG64 t = s, u = PATH_CURVE_STEPS - s, d = (LONG64)PATH_CURVE_STEPS * PATH_CURVE_STEPS * PATH_CURVE_STEPS;
                LONG64 x = u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p3.x;
                LONG64 y = u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p3.y;
                if (!poly_point(pg, (LONG)((x + d / 2) / d), (LONG)((y + d / 2) / d)))
                    goto fail;
            }
            if (p->Types[i + 2] & PT_CLOSEFIGURE)
                pg->Closed[pg->PolygonCount - 1] = TRUE;
            i += 3;
            continue;
        }
        if (!poly_point(pg, p->Points[i].x, p->Points[i].y))
            goto fail;
        if (p->Types[i] & PT_CLOSEFIGURE)
            pg->Closed[pg->PolygonCount - 1] = TRUE;
        i++;
    }
    return TRUE;
fail:
    poly_free(pg);
    return FALSE;
}

/* ------------------------------------------------------------------ scan conversion */

typedef struct CROSSING {
    LONG X;
    int Dir;
} CROSSING;

static void sort_crossings(CROSSING *c, ULONG n)
{
    for (ULONG i = 1; i < n; i++) {
        CROSSING v = c[i];
        ULONG k = i;
        while (k && c[k - 1].X > v.X) {
            c[k] = c[k - 1];
            k--;
        }
        c[k] = v;
    }
}

/* a pixel is inside when its center is; spans of one scanline become one band. */
BOOLEAN RgnFromPolygons(REGION *out, const POINT *pts, const ULONG *counts, ULONG polygons, int fill_mode)
{
    RgnClear(out);
    ULONG total = 0;
    LONG top = 0x7FFFFFFF, bottom = -0x7FFFFFFF;
    for (ULONG i = 0; i < polygons; i++)
        total += counts[i];
    if (!total)
        return TRUE;
    for (ULONG i = 0; i < total; i++) {
        top = W32Min(top, pts[i].y);
        bottom = W32Max(bottom, pts[i].y);
    }
    CROSSING *cross = W32Alloc((total + 2) * sizeof(CROSSING));
    LONG *spans = W32Alloc((total + 2) * sizeof(LONG));
    RECT *rects = nullptr;
    ULONG count = 0, capacity = 0;
    BOOLEAN ok = cross && spans;
    LONG prev_first = -1, prev_count = 0; /* rects of the previous band, for vertical merging */
    for (LONG y = top; ok && y < bottom; y++) {
        /* edges are sampled at the pixel center, half open at their lower end. */
        LONG64 sy2 = (LONG64)y * 2 + 1;
        ULONG n = 0, base = 0;
        for (ULONG p = 0; p < polygons; p++) {
            ULONG c = counts[p];
            for (ULONG k = 0; k < c; k++) {
                POINT a = pts[base + k], b = pts[base + (k + 1) % c];
                LONG64 ay = (LONG64)a.y * 2, by = (LONG64)b.y * 2;
                if (ay == by || (sy2 < ay && sy2 < by) || (sy2 >= ay && sy2 >= by))
                    continue;
                /* x at the center line, rounded to the pixel whose center it passes. */
                LONG64 num = (LONG64)(b.x - a.x) * (sy2 - ay);
                LONG64 den = by - ay;
                LONG64 x2 = (LONG64)a.x * 2 + num * 2 / den; /* doubled x */
                cross[n].X = (LONG)(x2 >> 1);
                cross[n].Dir = by > ay ? 1 : -1;
                n++;
            }
            base += c;
        }
        sort_crossings(cross, n);
        ULONG ns = 0;
        if (fill_mode == WINDING) {
            int wind = 0;
            for (ULONG k = 0; k < n; k++) {
                int before = wind;
                wind += cross[k].Dir;
                if (!before && wind)
                    spans[ns++] = cross[k].X;
                else if (before && !wind)
                    spans[ns++] = cross[k].X;
            }
        } else {
            for (ULONG k = 0; k < n; k++)
                spans[ns++] = cross[k].X;
        }
        ns &= ~1u;
        /* drop empty spans and merge touching ones. */
        ULONG m = 0;
        for (ULONG k = 0; k < ns; k += 2) {
            if (spans[k] >= spans[k + 1])
                continue;
            if (m && spans[m - 1] >= spans[k]) {
                spans[m - 1] = W32Max(spans[m - 1], spans[k + 1]);
                continue;
            }
            spans[m++] = spans[k];
            spans[m++] = spans[k + 1];
        }
        ULONG pairs = m / 2;
        /* extend the previous band when this scanline has the same spans. */
        if (prev_first >= 0 && (LONG)pairs == prev_count && rects[prev_first].bottom == y) {
            BOOLEAN same = TRUE;
            for (ULONG k = 0; k < pairs && same; k++)
                same = rects[prev_first + k].left == spans[2 * k] && rects[prev_first + k].right == spans[2 * k + 1];
            if (same) {
                for (ULONG k = 0; k < pairs; k++)
                    rects[prev_first + k].bottom = y + 1;
                continue;
            }
        }
        if (!pairs) {
            prev_first = -1;
            prev_count = 0;
            continue;
        }
        if (count + pairs > capacity) {
            ULONG cap = capacity ? capacity * 2 : 64;
            while (cap < count + pairs)
                cap *= 2;
            RECT *grown = W32Alloc(cap * sizeof(RECT));
            if (!grown) {
                ok = FALSE;
                break;
            }
            if (count)
                memcpy(grown, rects, count * sizeof(RECT));
            W32Free(rects);
            rects = grown;
            capacity = cap;
        }
        prev_first = (LONG)count;
        prev_count = (LONG)pairs;
        for (ULONG k = 0; k < pairs; k++)
            rects[count++] = (RECT){spans[2 * k], y, spans[2 * k + 1], y + 1};
    }
    W32Free(cross);
    W32Free(spans);
    if (!ok) {
        W32Free(rects);
        return FALSE;
    }
    W32Free(out->Rects);
    out->Rects = rects;
    out->Count = count;
    out->Capacity = capacity;
    if (count) {
        RECT e = rects[0];
        for (ULONG i = 1; i < count; i++) {
            e.left = W32Min(e.left, rects[i].left);
            e.right = W32Max(e.right, rects[i].right);
            e.bottom = W32Max(e.bottom, rects[i].bottom);
        }
        out->Extents = e;
    } else {
        out->Extents = (RECT){0, 0, 0, 0};
    }
    return TRUE;
}

/* ------------------------------------------------------------------ path operations */

static BOOLEAN path_region(DC *dc, REGION *out)
{
    POLYGONS pg;
    if (!flatten(&dc->Path, &pg))
        return FALSE;
    BOOLEAN ok = RgnFromPolygons(out, pg.Points, pg.Counts, pg.PolygonCount,
                                 dc->State.PolyFillMode == WINDING ? WINDING : ALTERNATE);
    poly_free(&pg);
    return ok;
}

static void discard_path(DC *dc)
{
    PathFree(&dc->Path);
    dc->PathState = PATH_NONE;
}

static void fill_path(DC *dc, REGION *clip, RECT *dirty)
{
    REGION area;
    RgnInit(&area);
    if (path_region(dc, &area)) {
        RgnOffset(&area, dc->Origin.x, dc->Origin.y);
        REGION c;
        RgnInit(&c);
        RgnCombine(&c, clip, &area, RGN_AND);
        if (!RgnIsEmpty(&c) && dc->State.Brush->Style != BS_NULL) {
            DrawBrushRect(dc, &c, &c.Extents, dc->State.Brush, PATCOPY);
            RectUnion(dirty, dirty, &c.Extents);
        }
        RgnFree(&c);
    }
    RgnFree(&area);
}

static void stroke_path(DC *dc, REGION *clip, RECT *dirty)
{
    POLYGONS pg;
    if (!flatten(&dc->Path, &pg))
        return;
    ULONG base = 0;
    for (ULONG i = 0; i < pg.PolygonCount; i++) {
        ULONG n = pg.Counts[i];
        for (ULONG k = 0; k + 1 < n + (pg.Closed[i] ? 1 : 0); k++) {
            POINT a = pg.Points[base + k], b = pg.Points[base + (k + 1) % n];
            DrawLine(dc, clip, a.x + dc->Origin.x, a.y + dc->Origin.y, b.x + dc->Origin.x, b.y + dc->Origin.y, dirty);
        }
        base += n;
    }
    poly_free(&pg);
}

static DC *closed_path_dc(HDC hdc)
{
    DC *dc = DcLookup(hdc);
    if (!dc) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    if (dc->PathState != PATH_CLOSED) {
        W32SetLastError(ERROR_CAN_NOT_COMPLETE);
        return nullptr;
    }
    return dc;
}

BOOL NTAPI NtGdiBeginPath(HDC hdc)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    if (dc) {
        PathFree(&dc->Path);
        dc->PathState = PATH_OPEN;
        dc->Path.NewFigure = TRUE;
    }
    UserLeave();
    if (!dc)
        W32SetLastError(ERROR_INVALID_HANDLE);
    return dc != nullptr;
}

BOOL NTAPI NtGdiEndPath(HDC hdc)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    BOOL ok = dc && dc->PathState == PATH_OPEN;
    if (ok)
        dc->PathState = PATH_CLOSED;
    UserLeave();
    if (!ok)
        W32SetLastError(dc ? ERROR_CAN_NOT_COMPLETE : ERROR_INVALID_HANDLE);
    return ok;
}

BOOL NTAPI NtGdiAbortPath(HDC hdc)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    if (dc)
        discard_path(dc);
    UserLeave();
    if (!dc)
        W32SetLastError(ERROR_INVALID_HANDLE);
    return dc != nullptr;
}

BOOL NTAPI NtGdiCloseFigure(HDC hdc)
{
    UserEnter();
    DC *dc = DcLookup(hdc);
    BOOL ok = dc && dc->PathState == PATH_OPEN && dc->Path.Count;
    if (ok) {
        BYTE *last = &dc->Path.Types[dc->Path.Count - 1];
        if ((*last & ~PT_CLOSEFIGURE) == PT_MOVETO)
            ok = FALSE;
        else
            *last |= PT_CLOSEFIGURE;
        dc->Path.NewFigure = TRUE;
    }
    UserLeave();
    if (!ok)
        W32SetLastError(dc ? ERROR_CAN_NOT_COMPLETE : ERROR_INVALID_HANDLE);
    return ok;
}

BOOL NTAPI NtGdiFlattenPath(HDC hdc)
{
    UserEnter();
    DC *dc = closed_path_dc(hdc);
    BOOL ok = FALSE;
    POLYGONS pg;
    if (dc && flatten(&dc->Path, &pg)) {
        GDI_PATH flat;
        memset(&flat, 0, sizeof(flat));
        ULONG base = 0;
        ok = TRUE;
        for (ULONG i = 0; i < pg.PolygonCount && ok; i++) {
            for (ULONG k = 0; k < pg.Counts[i] && ok; k++)
                ok = path_add(&flat, pg.Points[base + k].x, pg.Points[base + k].y, k ? PT_LINETO : PT_MOVETO);
            if (ok && pg.Closed[i] && pg.Counts[i] > 1)
                flat.Types[flat.Count - 1] |= PT_CLOSEFIGURE;
            base += pg.Counts[i];
        }
        poly_free(&pg);
        if (ok) {
            PathFree(&dc->Path);
            dc->Path = flat;
        } else {
            PathFree(&flat);
        }
    }
    UserLeave();
    return ok;
}

static BOOL paint_path(HDC hdc, BOOLEAN fill, BOOLEAN stroke)
{
    UserEnter();
    DC *dc = closed_path_dc(hdc);
    BOOL ok = dc != nullptr;
    if (dc) {
        REGION clip;
        RgnInit(&clip);
        RECT dirty = {0, 0, 0, 0};
        if (DcGetClip(dc, &clip)) {
            if (fill)
                fill_path(dc, &clip, &dirty);
            if (stroke)
                stroke_path(dc, &clip, &dirty);
            SurfaceMarkDirty(dc->Surface, &dirty);
        }
        RgnFree(&clip);
        discard_path(dc);
    }
    UserLeave();
    return ok;
}

BOOL NTAPI NtGdiFillPath(HDC hdc)
{
    return paint_path(hdc, TRUE, FALSE);
}

BOOL NTAPI NtGdiStrokePath(HDC hdc)
{
    return paint_path(hdc, FALSE, TRUE);
}

BOOL NTAPI NtGdiStrokeAndFillPath(HDC hdc)
{
    return paint_path(hdc, TRUE, TRUE);
}

BOOL NTAPI NtGdiSelectClipPath(HDC hdc, int mode)
{
    if (mode < RGN_AND || mode > RGN_COPY) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    DC *dc = closed_path_dc(hdc);
    BOOL ok = FALSE;
    if (dc) {
        REGION area;
        RgnInit(&area);
        if (path_region(dc, &area)) {
            /* the clip region is in device coordinates, like the path. */
            if (mode == RGN_COPY || !dc->State.HasClip) {
                if (mode == RGN_COPY || mode == RGN_AND || mode == RGN_OR) {
                    RgnCopy(&dc->State.ClipRgn, &area);
                } else {
                    RECT all = {-0x4000000, -0x4000000, 0x4000000, 0x4000000};
                    REGION full;
                    RgnInit(&full);
                    RgnSetRect(&full, &all);
                    RgnCombine(&dc->State.ClipRgn, &full, &area, mode);
                    RgnFree(&full);
                }
            } else {
                RgnCombine(&dc->State.ClipRgn, &dc->State.ClipRgn, &area, mode);
            }
            dc->State.HasClip = TRUE;
            ok = TRUE;
        }
        RgnFree(&area);
        discard_path(dc);
    }
    UserLeave();
    return ok;
}

GDI_REGION *GdiCreateRegion(void);

HRGN NTAPI NtGdiPathToRegion(HDC hdc)
{
    UserEnter();
    DC *dc = closed_path_dc(hdc);
    HRGN h = nullptr;
    if (dc) {
        GDI_REGION *r = GdiCreateRegion();
        if (r && path_region(dc, &r->Region))
            h = (HRGN)r->Header.Handle;
        else if (r)
            GdiDeleteObject(&r->Header);
        discard_path(dc);
    }
    UserLeave();
    return h;
}

/* device points back to logical ones through the inverse of the mapping. */
static void device_to_logical(DC *dc, LONG *x, LONG *y)
{
    LONG px = *x - dc->State.ViewportOrg.x + dc->State.WindowOrg.x;
    LONG py = *y - dc->State.ViewportOrg.y + dc->State.WindowOrg.y;
    if (dc->State.HasXform) {
        const XFORM *m = &dc->State.World;
        float det = m->eM11 * m->eM22 - m->eM12 * m->eM21;
        if (det != 0) {
            float fx = (float)px - m->eDx, fy = (float)py - m->eDy;
            float lx = (fx * m->eM22 - fy * m->eM21) / det;
            float ly = (fy * m->eM11 - fx * m->eM12) / det;
            px = (LONG)(lx >= 0 ? lx + 0.5f : lx - 0.5f);
            py = (LONG)(ly >= 0 ? ly + 0.5f : ly - 0.5f);
        }
    }
    *x = px;
    *y = py;
}

int NTAPI NtGdiGetPath(HDC hdc, POINT *user_points, BYTE *user_types, int count)
{
    UserEnter();
    DC *dc = closed_path_dc(hdc);
    int result = -1;
    POINT *pts = nullptr;
    BYTE *types = nullptr;
    if (dc) {
        int n = (int)dc->Path.Count;
        if (count == 0) {
            result = n;
        } else if (count < n) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
        } else if (n) {
            pts = W32Alloc((SIZE_T)n * sizeof(POINT));
            types = W32Alloc((SIZE_T)n);
            if (pts && types) {
                for (int i = 0; i < n; i++) {
                    pts[i] = dc->Path.Points[i];
                    device_to_logical(dc, &pts[i].x, &pts[i].y);
                }
                memcpy(types, dc->Path.Types, (SIZE_T)n);
                result = n;
            }
        } else {
            result = 0;
        }
    }
    UserLeave();
    if (result > 0 && pts) {
        if (!NT_SUCCESS(MimeCopyToUser(user_points, pts, (SIZE_T)result * sizeof(POINT))) ||
            !NT_SUCCESS(MimeCopyToUser(user_types, types, (SIZE_T)result)))
            result = -1;
    }
    W32Free(pts);
    W32Free(types);
    return result;
}
