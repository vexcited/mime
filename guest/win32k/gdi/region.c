/*
 * regions as y-x banded rectangle lists.
 *
 * rectangles are sorted by top, then by left. rectangles of one band share top and bottom and never touch
 * horizontally, and adjacent bands never have identical spans, so every region has one canonical form and equality
 * is a plain comparison.
 */
#include "../win32k.h"

void RectUnion(RECT *out, const RECT *a, const RECT *b)
{
    if (RectEmpty(a)) {
        *out = *b;
        return;
    }
    if (RectEmpty(b)) {
        *out = *a;
        return;
    }
    RECT r = {W32Min(a->left, b->left), W32Min(a->top, b->top), W32Max(a->right, b->right), W32Max(a->bottom, b->bottom)};
    *out = r;
}

void RgnInit(REGION *r)
{
    r->Count = 0;
    r->Capacity = 0;
    r->Rects = nullptr;
    r->Extents = (RECT){0, 0, 0, 0};
}

void RgnFree(REGION *r)
{
    W32Free(r->Rects);
    RgnInit(r);
}

void RgnClear(REGION *r)
{
    r->Count = 0;
    r->Extents = (RECT){0, 0, 0, 0};
}

static BOOLEAN reserve(REGION *r, ULONG count)
{
    if (count <= r->Capacity)
        return TRUE;
    ULONG capacity = r->Capacity ? r->Capacity : 8;
    while (capacity < count)
        capacity *= 2;
    RECT *rects = W32Alloc(capacity * sizeof(RECT));
    if (!rects)
        return FALSE;
    if (r->Count)
        memcpy(rects, r->Rects, r->Count * sizeof(RECT));
    W32Free(r->Rects);
    r->Rects = rects;
    r->Capacity = capacity;
    return TRUE;
}

static void update_extents(REGION *r)
{
    if (!r->Count) {
        r->Extents = (RECT){0, 0, 0, 0};
        return;
    }
    RECT e = r->Rects[0];
    e.bottom = r->Rects[r->Count - 1].bottom;
    for (ULONG i = 0; i < r->Count; i++) {
        e.left = W32Min(e.left, r->Rects[i].left);
        e.right = W32Max(e.right, r->Rects[i].right);
    }
    r->Extents = e;
}

BOOLEAN RgnSetRect(REGION *r, const RECT *rect)
{
    if (RectEmpty(rect)) {
        RgnClear(r);
        return TRUE;
    }
    if (!reserve(r, 1))
        return FALSE;
    r->Rects[0] = *rect;
    r->Count = 1;
    r->Extents = *rect;
    return TRUE;
}

BOOLEAN RgnCopy(REGION *dst, const REGION *src)
{
    if (dst == src)
        return TRUE;
    if (!reserve(dst, src->Count))
        return FALSE;
    if (src->Count)
        memcpy(dst->Rects, src->Rects, src->Count * sizeof(RECT));
    dst->Count = src->Count;
    dst->Extents = src->Extents;
    return TRUE;
}

BOOLEAN RgnIsEmpty(const REGION *r)
{
    return r->Count == 0;
}

int RgnComplexity(const REGION *r)
{
    return r->Count == 0 ? NULLREGION : r->Count == 1 ? SIMPLEREGION : COMPLEXREGION;
}

void RgnOffset(REGION *r, LONG dx, LONG dy)
{
    for (ULONG i = 0; i < r->Count; i++)
        RectOffset(&r->Rects[i], dx, dy);
    if (r->Count)
        RectOffset(&r->Extents, dx, dy);
}

BOOLEAN RgnPtIn(const REGION *r, LONG x, LONG y)
{
    if (!RectContains(&r->Extents, x, y))
        return FALSE;
    for (ULONG i = 0; i < r->Count; i++) {
        if (r->Rects[i].top > y)
            break;
        if (RectContains(&r->Rects[i], x, y))
            return TRUE;
    }
    return FALSE;
}

BOOLEAN RgnRectIn(const REGION *r, const RECT *rect)
{
    RECT tmp;
    if (!RectIntersect(&tmp, &r->Extents, rect))
        return FALSE;
    for (ULONG i = 0; i < r->Count; i++) {
        if (r->Rects[i].top >= rect->bottom)
            break;
        if (RectIntersect(&tmp, &r->Rects[i], rect))
            return TRUE;
    }
    return FALSE;
}

BOOLEAN RgnEqual(const REGION *a, const REGION *b)
{
    if (a->Count != b->Count)
        return FALSE;
    return a->Count == 0 || !memcmp(a->Rects, b->Rects, a->Count * sizeof(RECT));
}

/* ------------------------------------------------------------------ combination */

/*
 * x spans of a region within the band [y0, y1), returns the span count. bands come in ascending order, so
 * rectangles ending above this band can never be in a later one: *first skips them for good, which keeps a
 * combination linear in the rectangles instead of quadratic.
 */
static ULONG band_spans(const REGION *r, ULONG *first, LONG y0, LONG y1, LONG *spans, ULONG max_pairs)
{
    ULONG n = 0;
    while (*first < r->Count && r->Rects[*first].bottom <= y0)
        (*first)++;
    for (ULONG i = *first; i < r->Count; i++) {
        const RECT *rc = &r->Rects[i];
        if (rc->top >= y1)
            break;
        if (rc->top <= y0 && rc->bottom >= y1 && n < max_pairs) {
            spans[2 * n] = rc->left;
            spans[2 * n + 1] = rc->right;
            n++;
        }
    }
    return n;
}

static BOOLEAN op_result(int mode, BOOLEAN in_a, BOOLEAN in_b)
{
    switch (mode) {
    case RGN_AND:
        return in_a && in_b;
    case RGN_OR:
        return in_a || in_b;
    case RGN_XOR:
        return in_a != in_b;
    case RGN_DIFF:
        return in_a && !in_b;
    default:
        return in_a;
    }
}

/* returns the number of output spans. */
static ULONG combine_spans(const LONG *a, ULONG na, const LONG *b, ULONG nb, int mode, LONG *out)
{
    ULONG ia = 0, ib = 0, n = 0;
    BOOLEAN in_a = FALSE, in_b = FALSE, inside = FALSE;
    LONG start = 0;
    /* walk every span edge in increasing x. */
    while (ia < 2 * na || ib < 2 * nb) {
        LONG xa = ia < 2 * na ? a[ia] : 0x7FFFFFFF;
        LONG xb = ib < 2 * nb ? b[ib] : 0x7FFFFFFF;
        LONG x = xa < xb ? xa : xb;
        if (xa == x) {
            in_a = !(ia & 1);
            ia++;
        }
        if (xb == x) {
            in_b = !(ib & 1);
            ib++;
        }
        BOOLEAN now = op_result(mode, in_a, in_b);
        if (now && !inside) {
            start = x;
        } else if (!now && inside && x > start) {
            if (n && out[2 * n - 1] == start)
                out[2 * n - 1] = x; /* touching spans merge. */
            else {
                out[2 * n] = start;
                out[2 * n + 1] = x;
                n++;
            }
        }
        inside = now;
    }
    return n;
}

static void sift_down(LONG *v, ULONG root, ULONG n)
{
    for (;;) {
        ULONG child = 2 * root + 1;
        if (child >= n)
            return;
        if (child + 1 < n && v[child + 1] > v[child])
            child++;
        if (v[root] >= v[child])
            return;
        LONG t = v[root];
        v[root] = v[child];
        v[child] = t;
        root = child;
    }
}

/* heap sort: in place, n log n for any input, and the edges of a region can be many thousands. */
static void sort_longs(LONG *v, ULONG n)
{
    for (ULONG i = n / 2; i-- > 0;)
        sift_down(v, i, n);
    for (ULONG end = n; end-- > 1;) {
        LONG t = v[0];
        v[0] = v[end];
        v[end] = t;
        sift_down(v, 0, end);
    }
}

BOOLEAN RgnCombine(REGION *dst, const REGION *a, const REGION *b, int mode)
{
    if (mode == RGN_COPY)
        return RgnCopy(dst, a);
    if (mode == RGN_AND && (!a->Count || !b->Count)) {
        RgnClear(dst);
        return TRUE;
    }
    if ((mode == RGN_DIFF || mode == RGN_OR || mode == RGN_XOR) && !b->Count)
        return RgnCopy(dst, a);
    if ((mode == RGN_OR || mode == RGN_XOR) && !a->Count)
        return RgnCopy(dst, b);
    if (mode == RGN_DIFF && !a->Count) {
        RgnClear(dst);
        return TRUE;
    }
    RECT tmp;
    if ((mode == RGN_AND || mode == RGN_DIFF) && !RectIntersect(&tmp, &a->Extents, &b->Extents)) {
        if (mode == RGN_AND) {
            RgnClear(dst);
            return TRUE;
        }
        return RgnCopy(dst, a);
    }
    if (mode == RGN_AND && a->Count == 1 && b->Count == 1)
        return RgnSetRect(dst, &tmp);

    ULONG edges = 2 * (a->Count + b->Count);
    ULONG max_spans = a->Count + b->Count + 1;
    LONG *ys = W32Alloc(edges * sizeof(LONG) + 6 * max_spans * sizeof(LONG));
    if (!ys)
        return FALSE;
    LONG *sa = ys + edges;
    LONG *sb = sa + 2 * max_spans;
    LONG *so = sb + 2 * max_spans;
    ULONG ny = 0;
    for (ULONG i = 0; i < a->Count; i++) {
        ys[ny++] = a->Rects[i].top;
        ys[ny++] = a->Rects[i].bottom;
    }
    for (ULONG i = 0; i < b->Count; i++) {
        ys[ny++] = b->Rects[i].top;
        ys[ny++] = b->Rects[i].bottom;
    }
    sort_longs(ys, ny);
    ULONG unique = 0;
    for (ULONG i = 0; i < ny; i++)
        if (!unique || ys[unique - 1] != ys[i])
            ys[unique++] = ys[i];

    REGION out;
    RgnInit(&out);
    BOOLEAN ok = TRUE;
    ULONG prev_band_start = 0, prev_band_count = 0;
    ULONG first_a = 0, first_b = 0;
    for (ULONG i = 0; i + 1 < unique && ok; i++) {
        LONG y0 = ys[i], y1 = ys[i + 1];
        ULONG na = band_spans(a, &first_a, y0, y1, sa, max_spans);
        ULONG nb = band_spans(b, &first_b, y0, y1, sb, max_spans);
        ULONG no = combine_spans(sa, na, sb, nb, mode, so);
        if (!no)
            continue;
        /* coalesce with the previous band when it ends here with the same spans. */
        if (prev_band_count == no && out.Rects[prev_band_start].bottom == y0) {
            BOOLEAN same = TRUE;
            for (ULONG k = 0; k < no && same; k++)
                same = out.Rects[prev_band_start + k].left == so[2 * k] &&
                       out.Rects[prev_band_start + k].right == so[2 * k + 1];
            if (same) {
                for (ULONG k = 0; k < no; k++)
                    out.Rects[prev_band_start + k].bottom = y1;
                continue;
            }
        }
        if (!reserve(&out, out.Count + no)) {
            ok = FALSE;
            break;
        }
        prev_band_start = out.Count;
        prev_band_count = no;
        for (ULONG k = 0; k < no; k++)
            out.Rects[out.Count++] = (RECT){so[2 * k], y0, so[2 * k + 1], y1};
    }
    W32Free(ys);
    if (!ok) {
        RgnFree(&out);
        return FALSE;
    }
    update_extents(&out);
    RgnFree(dst);
    *dst = out;
    return TRUE;
}

BOOLEAN RgnCombineRect(REGION *dst, const REGION *a, const RECT *rect, int mode)
{
    REGION r;
    RgnInit(&r);
    RECT one = *rect;
    if (RectEmpty(&one)) {
        RgnClear(&r);
    } else {
        r.Rects = &one;
        r.Count = 1;
        r.Capacity = 1;
        r.Extents = one;
    }
    /* the temporary region borrows the stack rectangle, never free it. */
    REGION tmp;
    RgnInit(&tmp);
    BOOLEAN ok = RgnCombine(&tmp, a, &r, mode);
    if (ok) {
        RgnFree(dst);
        *dst = tmp;
    } else {
        RgnFree(&tmp);
    }
    return ok;
}

BOOLEAN RgnFromRects(REGION *r, const RECT *rects, ULONG count)
{
    RgnClear(r);
    for (ULONG i = 0; i < count; i++)
        if (!RgnCombineRect(r, r, &rects[i], RGN_OR))
            return FALSE;
    return TRUE;
}

/* integer square root for the ellipse corners. */
static LONG isqrt(LONG64 v)
{
    if (v <= 0)
        return 0;
    LONG64 x = v, y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + v / x) / 2;
    }
    return (LONG)x;
}

/* adds the rows from top to bottom between left and right to the end of the bands, which grow downward. */
static void append_rows(REGION *r, LONG left, LONG top, LONG right, LONG bottom)
{
    if (right <= left || bottom <= top)
        return;
    if (r->Count) {
        RECT *last = &r->Rects[r->Count - 1];
        if (last->bottom == top && last->left == left && last->right == right) {
            last->bottom = bottom;
            return;
        }
    }
    r->Rects[r->Count++] = (RECT){left, top, right, bottom};
}

/* the corner of a round rectangle takes one band for each scanline, and a million of them is as far as one goes. */
#define ROUND_RECT_MAX_CORNER 0x100000

/* how far in from the sides the scanline i of a corner starts, for ellipse radii rx and ry. */
static LONG corner_inset(LONG i, LONG rx, LONG ry)
{
    /* distance of this scanline center from the ellipse center, in doubled units. */
    LONG64 dy = 2 * ((LONG64)ry - i) - 1;
    LONG64 inner = (LONG64)ry * ry * 4 - dy * dy;
    LONG dx = (LONG)(((LONG64)rx * isqrt(inner)) / (2 * ry));
    return rx - dx;
}

/* a rectangle with elliptic corners of diameters ew and eh, one band per scanline in the corners. */
BOOLEAN RgnRoundRect(REGION *r, LONG left, LONG top, LONG right, LONG bottom, LONG ew, LONG eh)
{
    if (right < left) {
        LONG t = left;
        left = right;
        right = t;
    }
    if (bottom < top) {
        LONG t = top;
        top = bottom;
        bottom = t;
    }
    LONG64 width = (LONG64)right - left, height = (LONG64)bottom - top;
    LONG64 ew64 = ew < width ? ew : width;
    LONG64 eh64 = eh < height ? eh : height;
    RgnClear(r);
    if (right <= left || bottom <= top)
        return TRUE;
    if (ew64 < 2 || eh64 < 2) {
        RECT rc = {left, top, right, bottom};
        return RgnSetRect(r, &rc);
    }
    if (eh64 / 2 > ROUND_RECT_MAX_CORNER)
        return FALSE;
    LONG rx = (LONG)(ew64 / 2), ry = (LONG)(eh64 / 2);
    /* the bands are made from the top down: the upper corners, the middle and the lower corners. */
    if (!reserve(r, (ULONG)(2 * ry + 1)))
        return FALSE;
    for (LONG i = 0; i < ry; i++) {
        LONG inset = corner_inset(i, rx, ry);
        append_rows(r, left + inset, top + i, right - inset, top + i + 1);
    }
    append_rows(r, left, top + ry, right, bottom - ry);
    for (LONG i = ry - 1; i >= 0; i--) {
        LONG inset = corner_inset(i, rx, ry);
        append_rows(r, left + inset, bottom - i - 1, right - inset, bottom - i);
    }
    update_extents(r);
    return TRUE;
}
