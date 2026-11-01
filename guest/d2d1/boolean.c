/*
 * regions as trapezoids: polygons are cut at every vertex and crossing height, and each slab between two heights
 * holds edges that never cross. the inside of a slab follows from the winding of the edges left of a point.
 * areas, tessellation and the boolean operations of geometries use it.
 */
#include "d2d_private.h"

typedef struct Edge {
    double x0, y0, x1, y1;
    int dir;
    int set;
} Edge;

#define MAX_CROSSING_EDGES 3000

static double edge_x(const Edge *e, double y)
{
    if (e->y1 == e->y0)
        return e->x0;
    return e->x0 + (e->x1 - e->x0) * (y - e->y0) / (e->y1 - e->y0);
}

static HRESULT add_edges(D2dArray *edges, const MimeVecPolys *p, int set)
{
    for (UINT32 i = 0; i < p->nsubs; i++) {
        const MimeVecSub *s = &p->subs[i];
        for (UINT32 j = 0; j < s->count; j++) {
            const MimeVecPoint *a = &p->pts[s->first + j];
            const MimeVecPoint *b = &p->pts[s->first + (j + 1) % s->count];
            if (a->y == b->y)
                continue;
            if (!D2dArrayGrow(edges, sizeof(Edge), 1))
                return E_OUTOFMEMORY;
            Edge e;
            if (a->y < b->y) {
                e.x0 = a->x;
                e.y0 = a->y;
                e.x1 = b->x;
                e.y1 = b->y;
                e.dir = 1;
            } else {
                e.x0 = b->x;
                e.y0 = b->y;
                e.x1 = a->x;
                e.y1 = a->y;
                e.dir = -1;
            }
            e.set = set;
            ((Edge *)edges->data)[edges->count++] = e;
        }
    }
    return S_OK;
}

/* insertion sort of doubles for small lists, the heap sort below for the rest. */
static void sort_doubles(double *v, UINT32 n)
{
    if (n < 2)
        return;
    /* heap sort. */
    for (UINT32 i = n / 2; i-- > 0;) {
        UINT32 root = i;
        for (;;) {
            UINT32 child = 2 * root + 1;
            if (child >= n)
                break;
            if (child + 1 < n && v[child + 1] > v[child])
                child++;
            if (v[root] >= v[child])
                break;
            double t = v[root];
            v[root] = v[child];
            v[child] = t;
            root = child;
        }
    }
    for (UINT32 end = n - 1; end > 0; end--) {
        double t = v[0];
        v[0] = v[end];
        v[end] = t;
        UINT32 root = 0;
        for (;;) {
            UINT32 child = 2 * root + 1;
            if (child >= end)
                break;
            if (child + 1 < end && v[child + 1] > v[child])
                child++;
            if (v[root] >= v[child])
                break;
            double u = v[root];
            v[root] = v[child];
            v[child] = u;
            root = child;
        }
    }
}

typedef struct Active {
    double xmid;
    double xa, xb;
    const Edge *edge;
} Active;

static void sort_active(Active *a, UINT32 n)
{
    for (UINT32 i = 1; i < n; i++) {
        Active t = a[i];
        UINT32 j = i;
        while (j > 0 && a[j - 1].xmid > t.xmid) {
            a[j] = a[j - 1];
            j--;
        }
        a[j] = t;
    }
}

static BOOL inside(int winding, BOOL nonzero) { return nonzero ? winding != 0 : (winding & 1) != 0; }

static BOOL combine(int op, BOOL a, BOOL b)
{
    switch (op) {
    case 0:
        return a || b;
    case 1:
        return a && b;
    case 2:
        return a != b;
    case 3:
        return a && !b;
    default:
        return a;
    }
}

HRESULT D2dRegionTraps(const MimeVecPolys *a, BOOL a_winding, const MimeVecPolys *b, BOOL b_winding, int op,
                       D2dArray *traps)
{
    D2dArray edges = {}, ys = {};
    Active *active = nullptr;
    HRESULT hr = add_edges(&edges, a, 0);
    if (SUCCEEDED(hr) && b)
        hr = add_edges(&edges, b, 1);
    if (FAILED(hr))
        goto done;
    {
        const Edge *e = edges.data;
        UINT32 n = edges.count;
        if (!n)
            goto done;
        UINT32 cap = n * 2 + 2;
        if (n <= MAX_CROSSING_EDGES)
            cap += n * (n - 1) / 2 > 200000 ? 200000 : n * (n - 1) / 2;
        double *y = D2dAlloc(cap * sizeof(double));
        if (!y) {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        UINT32 ny = 0;
        for (UINT32 i = 0; i < n; i++) {
            y[ny++] = e[i].y0;
            y[ny++] = e[i].y1;
        }
        if (n <= MAX_CROSSING_EDGES) {
            for (UINT32 i = 0; i < n && ny + 1 < cap; i++) {
                for (UINT32 j = i + 1; j < n && ny + 1 < cap; j++) {
                    double lo = e[i].y0 > e[j].y0 ? e[i].y0 : e[j].y0;
                    double hi = e[i].y1 < e[j].y1 ? e[i].y1 : e[j].y1;
                    if (lo >= hi)
                        continue;
                    double dl = edge_x(&e[i], lo) - edge_x(&e[j], lo);
                    double dh = edge_x(&e[i], hi) - edge_x(&e[j], hi);
                    if ((dl < 0 && dh > 0) || (dl > 0 && dh < 0)) {
                        double t = dl / (dl - dh);
                        y[ny++] = lo + (hi - lo) * t;
                    }
                }
            }
        }
        sort_doubles(y, ny);
        UINT32 m = 0;
        for (UINT32 i = 0; i < ny; i++) {
            if (m == 0 || y[i] - y[m - 1] > 1e-7)
                y[m++] = y[i];
        }
        active = D2dAlloc(n * sizeof(Active));
        if (!active) {
            D2dFree(y);
            hr = E_OUTOFMEMORY;
            goto done;
        }
        for (UINT32 s = 0; s + 1 < m; s++) {
            double ya = y[s], yb = y[s + 1], ym = (ya + yb) * 0.5;
            UINT32 na = 0;
            for (UINT32 i = 0; i < n; i++) {
                if (e[i].y0 <= ya + 1e-9 && e[i].y1 >= yb - 1e-9) {
                    active[na].edge = &e[i];
                    active[na].xmid = edge_x(&e[i], ym);
                    active[na].xa = edge_x(&e[i], ya);
                    active[na].xb = edge_x(&e[i], yb);
                    na++;
                }
            }
            sort_active(active, na);
            int wa = 0, wb = 0;
            BOOL was = FALSE;
            UINT32 left = 0;
            for (UINT32 i = 0; i < na; i++) {
                if (active[i].edge->set == 0)
                    wa += active[i].edge->dir;
                else
                    wb += active[i].edge->dir;
                BOOL now = combine(op, inside(wa, a_winding), b ? inside(wb, b_winding) : FALSE);
                if (now && !was) {
                    left = i;
                } else if (!now && was) {
                    if (!D2dArrayGrow(traps, sizeof(D2dTrap), 1)) {
                        hr = E_OUTOFMEMORY;
                        break;
                    }
                    D2dTrap t = {(float)ya, (float)yb, (float)active[left].xa, (float)active[left].xb,
                                 (float)active[i].xa, (float)active[i].xb};
                    ((D2dTrap *)traps->data)[traps->count++] = t;
                }
                was = now;
            }
            if (FAILED(hr))
                break;
        }
        D2dFree(y);
    }
done:
    D2dFree(active);
    D2dArrayFree(&edges);
    D2dArrayFree(&ys);
    return hr;
}

HRESULT D2dTrapsToPolys(const D2dArray *traps, MimeVecPolys *out)
{
    const D2dTrap *t = traps->data;
    for (UINT32 i = 0; i < traps->count; i++) {
        MimeVecPoint pts[4] = {{t[i].xl0, t[i].y0}, {t[i].xr0, t[i].y0}, {t[i].xr1, t[i].y1}, {t[i].xl1, t[i].y1}};
        if (mime_vec_polys_add_sub(out, pts, 4, true) != MIME_VEC_OK)
            return E_OUTOFMEMORY;
    }
    return S_OK;
}
