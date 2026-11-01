/*
 * paths: figures of lines, beziers, quadratic beziers and arcs, and their flattening into polygons.
 */
#include "d2d_private.h"

void D2dPathFree(D2dPath *p)
{
    D2dArrayFree(&p->figures);
    D2dArrayFree(&p->segments);
}

BOOL D2dPathCount(const D2dPath *p, UINT32 *figures, UINT32 *segments)
{
    *figures = p->figures.count;
    *segments = p->segments.count;
    return TRUE;
}

BOOL D2dPathCopy(D2dPath *dst, const D2dPath *src)
{
    D2dPathFree(dst);
    dst->fill_mode = src->fill_mode;
    if (src->figures.count) {
        if (!D2dArrayGrow(&dst->figures, sizeof(D2dFigure), src->figures.count))
            return FALSE;
        memcpy(dst->figures.data, src->figures.data, src->figures.count * sizeof(D2dFigure));
        dst->figures.count = src->figures.count;
    }
    if (src->segments.count) {
        if (!D2dArrayGrow(&dst->segments, sizeof(D2dSegment), src->segments.count))
            return FALSE;
        memcpy(dst->segments.data, src->segments.data, src->segments.count * sizeof(D2dSegment));
        dst->segments.count = src->segments.count;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ arcs */

/*
 * the beziers of an arc from p0, at most four of 90 degrees. returns the number of beziers, 0 for a point and -1
 * for a line.
 */
static int arc_beziers(D2D1_POINT_2F p0, const D2D1_ARC_SEGMENT *arc, D2D1_POINT_2F out[12])
{
    float rx = D2dAbsF(arc->size.width), ry = D2dAbsF(arc->size.height);
    float x1 = p0.x, y1 = p0.y, x2 = arc->point.x, y2 = arc->point.y;
    if (x1 == x2 && y1 == y2)
        return 0;
    if (rx == 0.0f || ry == 0.0f)
        return -1;
    float phi = arc->rotationAngle * 0.017453292f;
    float sp, cp;
    D2dSinCosF(phi, &sp, &cp);
    float dx = (x1 - x2) * 0.5f, dy = (y1 - y2) * 0.5f;
    float xp = cp * dx + sp * dy, yp = -sp * dx + cp * dy;
    float lambda = (xp * xp) / (rx * rx) + (yp * yp) / (ry * ry);
    if (lambda > 1.0f) {
        float s = D2dSqrtF(lambda);
        rx *= s;
        ry *= s;
    }
    float num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp;
    float den = rx * rx * yp * yp + ry * ry * xp * xp;
    float coef = den == 0.0f ? 0.0f : D2dSqrtF(D2dMaxF(num / den, 0.0f));
    int large = arc->arcSize == D2D1_ARC_SIZE_LARGE;
    int sweep = arc->sweepDirection == D2D1_SWEEP_DIRECTION_CLOCKWISE;
    if (large == sweep)
        coef = -coef;
    float cxp = coef * (rx * yp / ry), cyp = coef * -(ry * xp / rx);
    float cx = cp * cxp - sp * cyp + (x1 + x2) * 0.5f;
    float cy = sp * cxp + cp * cyp + (y1 + y2) * 0.5f;
    float ux = (xp - cxp) / rx, uy = (yp - cyp) / ry;
    float vx = (-xp - cxp) / rx, vy = (-yp - cyp) / ry;
    float theta = D2dAtan2F(uy, ux);
    float delta = D2dAtan2F(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && delta > 0)
        delta -= 6.2831853f;
    else if (sweep && delta < 0)
        delta += 6.2831853f;
    int n = (int)(D2dAbsF(delta) / 1.5707964f + 0.999f);
    if (n < 1)
        n = 1;
    if (n > 4)
        n = 4;
    float step = delta / n;
    float k = 4.0f / 3.0f * (D2dTanF(step * 0.25f));
    for (int i = 0; i < n; i++) {
        float a0 = theta + step * i, a1 = a0 + step;
        float s0, c0, s1, c1;
        D2dSinCosF(a0, &s0, &c0);
        D2dSinCosF(a1, &s1, &c1);
        float q[4][2] = {{c0, s0}, {c0 - k * s0, s0 + k * c0}, {c1 + k * s1, s1 - k * c1}, {c1, s1}};
        for (int j = 1; j < 4; j++) {
            float ex = q[j][0] * rx, ey = q[j][1] * ry;
            D2D1_POINT_2F p = {cp * ex - sp * ey + cx, sp * ex + cp * ey + cy};
            out[i * 3 + j - 1] = p;
        }
    }
    out[n * 3 - 1] = arc->point;
    return n;
}

/* ------------------------------------------------------------------ appending */

static BOOL add_segment(D2dPath *p, const D2dSegment *s)
{
    if (!D2dArrayGrow(&p->segments, sizeof(D2dSegment), 1))
        return FALSE;
    ((D2dSegment *)p->segments.data)[p->segments.count++] = *s;
    return TRUE;
}

BOOL D2dPathAppend(D2dPath *dst, const D2dPath *src, const D2dMatrix *m)
{
    BOOL identity = !m || D2dMatIsIdentity(m);
    const D2dFigure *figs = src->figures.data;
    const D2dSegment *segs = src->segments.data;
    for (UINT32 i = 0; i < src->figures.count; i++) {
        D2dFigure fig = figs[i];
        D2D1_POINT_2F cur = fig.start;
        if (!identity)
            fig.start = D2dMatApply(m, fig.start.x, fig.start.y);
        if (!D2dArrayGrow(&dst->figures, sizeof(D2dFigure), 1))
            return FALSE;
        fig.first = dst->segments.count;
        fig.count = 0;
        for (UINT32 j = 0; j < figs[i].count; j++) {
            D2dSegment s = segs[figs[i].first + j];
            D2dSegment out[5];
            int n = 1;
            D2D1_POINT_2F next = s.kind == D2D_SEG_ARC ? s.arc.point : s.p[s.kind == D2D_SEG_BEZIER ? 2 : (s.kind == D2D_SEG_QUAD ? 1 : 0)];
            if (identity) {
                out[0] = s;
            } else if (s.kind == D2D_SEG_ARC) {
                D2D1_POINT_2F b[12];
                int nb = arc_beziers(cur, &s.arc, b);
                n = 0;
                if (nb < 0) {
                    out[n] = s;
                    out[n].kind = D2D_SEG_LINE;
                    out[n].p[0] = D2dMatApply(m, s.arc.point.x, s.arc.point.y);
                    n++;
                } else {
                    for (int k = 0; k < nb; k++) {
                        out[n] = s;
                        out[n].kind = D2D_SEG_BEZIER;
                        for (int q = 0; q < 3; q++)
                            out[n].p[q] = D2dMatApply(m, b[k * 3 + q].x, b[k * 3 + q].y);
                        n++;
                    }
                }
            } else {
                out[0] = s;
                int pts = s.kind == D2D_SEG_BEZIER ? 3 : (s.kind == D2D_SEG_QUAD ? 2 : 1);
                for (int q = 0; q < pts; q++)
                    out[0].p[q] = D2dMatApply(m, s.p[q].x, s.p[q].y);
            }
            for (int k = 0; k < n; k++) {
                if (!add_segment(dst, &out[k]))
                    return FALSE;
                fig.count++;
            }
            cur = next;
        }
        ((D2dFigure *)dst->figures.data)[dst->figures.count++] = fig;
    }
    return TRUE;
}

BOOL D2dPathFromPolys(D2dPath *dst, const MimeVecPolys *polys)
{
    for (UINT32 i = 0; i < polys->nsubs; i++) {
        const MimeVecSub *sub = &polys->subs[i];
        if (!sub->count)
            continue;
        if (!D2dArrayGrow(&dst->figures, sizeof(D2dFigure), 1))
            return FALSE;
        D2dFigure fig = {};
        fig.start.x = polys->pts[sub->first].x;
        fig.start.y = polys->pts[sub->first].y;
        fig.begin = D2D1_FIGURE_BEGIN_FILLED;
        fig.end = sub->closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN;
        fig.first = dst->segments.count;
        for (UINT32 j = 1; j < sub->count; j++) {
            D2dSegment s = {};
            s.kind = D2D_SEG_LINE;
            s.p[0].x = polys->pts[sub->first + j].x;
            s.p[0].y = polys->pts[sub->first + j].y;
            if (!add_segment(dst, &s))
                return FALSE;
            fig.count++;
        }
        ((D2dFigure *)dst->figures.data)[dst->figures.count++] = fig;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ flattening */

BOOL D2dPathFlatten(const D2dPath *p, const D2dMatrix *m, float tolerance, MimeVecPolys *out, BOOL only_filled)
{
    const D2dFigure *figs = p->figures.data;
    const D2dSegment *segs = p->segments.data;
    MimeVecPoint *pts = nullptr;
    uint8_t *types = nullptr;
    UINT32 cap = 0;
    BOOL ok = TRUE;
    for (UINT32 i = 0; i < p->figures.count && ok; i++) {
        if (only_filled && figs[i].begin == D2D1_FIGURE_BEGIN_HOLLOW)
            continue;
        UINT32 need = 2;
        for (UINT32 j = 0; j < figs[i].count; j++)
            need += segs[figs[i].first + j].kind == D2D_SEG_ARC ? 12 : 3;
        if (need > cap) {
            D2dFree(pts);
            D2dFree(types);
            cap = need + 16;
            pts = D2dAlloc(cap * sizeof(*pts));
            types = D2dAlloc(cap);
            if (!pts || !types) {
                ok = FALSE;
                break;
            }
        }
        UINT32 n = 0;
        pts[n].x = figs[i].start.x;
        pts[n].y = figs[i].start.y;
        types[n++] = MIME_VEC_PT_START;
        D2D1_POINT_2F cur = figs[i].start;
        for (UINT32 j = 0; j < figs[i].count; j++) {
            const D2dSegment *s = &segs[figs[i].first + j];
            switch (s->kind) {
            case D2D_SEG_LINE:
                pts[n].x = s->p[0].x;
                pts[n].y = s->p[0].y;
                types[n++] = MIME_VEC_PT_LINE;
                cur = s->p[0];
                break;
            case D2D_SEG_BEZIER:
                for (int q = 0; q < 3; q++) {
                    pts[n].x = s->p[q].x;
                    pts[n].y = s->p[q].y;
                    types[n++] = MIME_VEC_PT_BEZIER;
                }
                cur = s->p[2];
                break;
            case D2D_SEG_QUAD: {
                /* a quadratic raised to a cubic. */
                D2D1_POINT_2F q0 = cur, q1 = s->p[0], q2 = s->p[1];
                D2D1_POINT_2F c1 = {q0.x + 2.0f / 3.0f * (q1.x - q0.x), q0.y + 2.0f / 3.0f * (q1.y - q0.y)};
                D2D1_POINT_2F c2 = {q2.x + 2.0f / 3.0f * (q1.x - q2.x), q2.y + 2.0f / 3.0f * (q1.y - q2.y)};
                pts[n].x = c1.x;
                pts[n].y = c1.y;
                types[n++] = MIME_VEC_PT_BEZIER;
                pts[n].x = c2.x;
                pts[n].y = c2.y;
                types[n++] = MIME_VEC_PT_BEZIER;
                pts[n].x = q2.x;
                pts[n].y = q2.y;
                types[n++] = MIME_VEC_PT_BEZIER;
                cur = q2;
                break;
            }
            default: {
                D2D1_POINT_2F b[12];
                int nb = arc_beziers(cur, &s->arc, b);
                if (nb < 0) {
                    pts[n].x = s->arc.point.x;
                    pts[n].y = s->arc.point.y;
                    types[n++] = MIME_VEC_PT_LINE;
                } else {
                    for (int k = 0; k < nb * 3; k++) {
                        pts[n].x = b[k].x;
                        pts[n].y = b[k].y;
                        types[n++] = MIME_VEC_PT_BEZIER;
                    }
                }
                cur = s->arc.point;
                break;
            }
            }
        }
        if (figs[i].end == D2D1_FIGURE_END_CLOSED)
            types[n - 1] |= MIME_VEC_PT_CLOSE;
        if (mime_vec_flatten(out, pts, types, n, D2dVec(m), tolerance) != MIME_VEC_OK)
            ok = FALSE;
    }
    D2dFree(pts);
    D2dFree(types);
    return ok;
}
