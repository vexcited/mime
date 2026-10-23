/* GDI+ style paths and the shape builders. */
#include "vector_internal.h"

#define PI_F 3.14159265358979f

void mime_vec_path_init(MimeVecPath *p, const MimeAllocator *alloc)
{
    __builtin_memset(p, 0, sizeof(*p));
    p->alloc = alloc;
    p->need_start = true;
}

void mime_vec_path_free(MimeVecPath *p)
{
    mime_free(p->alloc, p->pts);
    mime_free(p->alloc, p->types);
    const MimeAllocator *a = p->alloc;
    __builtin_memset(p, 0, sizeof(*p));
    p->alloc = a;
    p->need_start = true;
}

void mime_vec_path_clear(MimeVecPath *p)
{
    p->count = 0;
    p->need_start = true;
}

static int reserve(MimeVecPath *p, uint32_t extra)
{
    if (extra > MIME_VEC_MAX_PATH_POINTS || p->count + extra > MIME_VEC_MAX_PATH_POINTS)
        return MIME_VEC_TOOBIG;
    uint32_t need = p->count + extra;
    if (need <= p->capacity)
        return MIME_VEC_OK;
    uint32_t cap = p->capacity < 32 ? 32 : p->capacity;
    while (cap < need)
        cap = cap > MIME_VEC_MAX_PATH_POINTS / 2 ? MIME_VEC_MAX_PATH_POINTS : cap * 2;
    MimeVecPoint *np = mime_alloc(p->alloc, (size_t)cap * sizeof(MimeVecPoint));
    uint8_t *nt = mime_alloc(p->alloc, cap);
    if (!np || !nt) {
        mime_free(p->alloc, np);
        mime_free(p->alloc, nt);
        return MIME_VEC_NOMEM;
    }
    if (p->count) {
        __builtin_memcpy(np, p->pts, (size_t)p->count * sizeof(MimeVecPoint));
        __builtin_memcpy(nt, p->types, p->count);
    }
    mime_free(p->alloc, p->pts);
    mime_free(p->alloc, p->types);
    p->pts = np;
    p->types = nt;
    p->capacity = cap;
    return MIME_VEC_OK;
}

int mime_vec_path_copy(MimeVecPath *dst, const MimeVecPath *src)
{
    mime_vec_path_clear(dst);
    int r = reserve(dst, src->count);
    if (r)
        return r;
    __builtin_memcpy(dst->pts, src->pts, (size_t)src->count * sizeof(MimeVecPoint));
    __builtin_memcpy(dst->types, src->types, src->count);
    dst->count = src->count;
    dst->need_start = src->need_start;
    return MIME_VEC_OK;
}

static int push(MimeVecPath *p, float x, float y, uint8_t type)
{
    if (!(x == x) || !(y == y))
        return MIME_VEC_INVALID;
    int r = reserve(p, 1);
    if (r)
        return r;
    p->pts[p->count].x = mime_vec_clamp_coord(x);
    p->pts[p->count].y = mime_vec_clamp_coord(y);
    p->types[p->count] = type;
    p->count++;
    p->need_start = false;
    return MIME_VEC_OK;
}

static bool figure_open(const MimeVecPath *p)
{
    return p->count > 0 && !p->need_start;
}

int mime_vec_path_add_raw(MimeVecPath *p, const MimeVecPoint *pts, const uint8_t *types, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        if (!(pts[i].x == pts[i].x) || !(pts[i].y == pts[i].y))
            return MIME_VEC_INVALID;
    int r = reserve(p, count);
    if (r)
        return r;
    for (uint32_t i = 0; i < count; i++) {
        p->pts[p->count + i].x = mime_vec_clamp_coord(pts[i].x);
        p->pts[p->count + i].y = mime_vec_clamp_coord(pts[i].y);
        p->types[p->count + i] = types[i];
    }
    p->count += count;
    if (count)
        p->need_start = (types[count - 1] & MIME_VEC_PT_CLOSE) != 0;
    return MIME_VEC_OK;
}

int mime_vec_path_append(MimeVecPath *p, const MimeVecPath *other, bool connect)
{
    if (p == other) {
        MimeVecPath tmp;
        mime_vec_path_init(&tmp, p->alloc);
        int r = mime_vec_path_copy(&tmp, other);
        if (!r)
            r = mime_vec_path_append(p, &tmp, connect);
        mime_vec_path_free(&tmp);
        return r;
    }
    bool join = connect && figure_open(p) && other->count > 0;
    uint32_t at = p->count;
    int r = mime_vec_path_add_raw(p, other->pts, other->types, other->count);
    if (r)
        return r;
    if (join && (p->types[at] & MIME_VEC_PT_TYPE_MASK) == MIME_VEC_PT_START)
        p->types[at] = (uint8_t)((p->types[at] & ~MIME_VEC_PT_TYPE_MASK) | MIME_VEC_PT_LINE);
    return MIME_VEC_OK;
}

void mime_vec_path_transform(MimeVecPath *p, const MimeVecMatrix *m)
{
    for (uint32_t i = 0; i < p->count; i++) {
        MimeVecPoint q = mime_vec_xform(m, p->pts[i].x, p->pts[i].y);
        q.x = mime_vec_clamp_coord(q.x);
        q.y = mime_vec_clamp_coord(q.y);
        p->pts[i] = q;
    }
}

void mime_vec_path_start_figure(MimeVecPath *p)
{
    p->need_start = true;
}

int mime_vec_path_close_figure(MimeVecPath *p)
{
    if (figure_open(p)) {
        p->types[p->count - 1] |= MIME_VEC_PT_CLOSE;
        p->need_start = true;
    }
    return MIME_VEC_OK;
}

int mime_vec_path_move_to(MimeVecPath *p, float x, float y)
{
    return push(p, x, y, MIME_VEC_PT_START);
}

int mime_vec_path_line_to(MimeVecPath *p, float x, float y)
{
    return push(p, x, y, figure_open(p) ? MIME_VEC_PT_LINE : MIME_VEC_PT_START);
}

int mime_vec_path_bezier_to(MimeVecPath *p, float x1, float y1, float x2, float y2, float x3, float y3)
{
    if (!figure_open(p))
        return MIME_VEC_INVALID;
    if (!(x1 == x1 && y1 == y1 && x2 == x2 && y2 == y2 && x3 == x3 && y3 == y3))
        return MIME_VEC_INVALID;
    int r = reserve(p, 3);
    if (r)
        return r;
    push(p, x1, y1, MIME_VEC_PT_BEZIER);
    push(p, x2, y2, MIME_VEC_PT_BEZIER);
    push(p, x3, y3, MIME_VEC_PT_BEZIER);
    return MIME_VEC_OK;
}

int mime_vec_path_add_line(MimeVecPath *p, float x0, float y0, float x1, float y1)
{
    MimeVecPoint pts[2] = {{x0, y0}, {x1, y1}};
    return mime_vec_path_add_lines(p, pts, 2);
}

int mime_vec_path_add_lines(MimeVecPath *p, const MimeVecPoint *pts, uint32_t count)
{
    if (count < 2)
        return MIME_VEC_INVALID;
    for (uint32_t i = 0; i < count; i++)
        if (!(pts[i].x == pts[i].x) || !(pts[i].y == pts[i].y))
            return MIME_VEC_INVALID;
    int r = reserve(p, count);
    if (r)
        return r;
    for (uint32_t i = 0; i < count; i++) {
        r = mime_vec_path_line_to(p, pts[i].x, pts[i].y);
        if (r)
            return r;
    }
    return MIME_VEC_OK;
}

int mime_vec_path_add_bezier(MimeVecPath *p, float x0, float y0, float x1, float y1, float x2, float y2, float x3,
                             float y3)
{
    MimeVecPoint pts[4] = {{x0, y0}, {x1, y1}, {x2, y2}, {x3, y3}};
    return mime_vec_path_add_beziers(p, pts, 4);
}

int mime_vec_path_add_beziers(MimeVecPath *p, const MimeVecPoint *pts, uint32_t count)
{
    if (count < 4 || (count - 1) % 3 != 0)
        return MIME_VEC_INVALID;
    for (uint32_t i = 0; i < count; i++)
        if (!(pts[i].x == pts[i].x) || !(pts[i].y == pts[i].y))
            return MIME_VEC_INVALID;
    int r = reserve(p, count);
    if (r)
        return r;
    r = mime_vec_path_line_to(p, pts[0].x, pts[0].y);
    for (uint32_t i = 1; !r && i < count; i += 3)
        r = mime_vec_path_bezier_to(p, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, pts[i + 2].x, pts[i + 2].y);
    return r;
}

int mime_vec_path_add_rect(MimeVecPath *p, float x, float y, float w, float h)
{
    if (!(w == w && h == h && x == x && y == y))
        return MIME_VEC_INVALID;
    /* GDI+ accepts negative sizes by normalizing. */
    if (w < 0) {
        x += w;
        w = -w;
    }
    if (h < 0) {
        y += h;
        h = -h;
    }
    int r = reserve(p, 4);
    if (r)
        return r;
    p->need_start = true;
    push(p, x, y, MIME_VEC_PT_START);
    push(p, x + w, y, MIME_VEC_PT_LINE);
    push(p, x + w, y + h, MIME_VEC_PT_LINE);
    push(p, x, y + h, MIME_VEC_PT_LINE | MIME_VEC_PT_CLOSE);
    p->need_start = true;
    return MIME_VEC_OK;
}

/* sine and cosine of an angle in degrees, exact at multiples of 90. */
static void sincos_deg(float deg, float *s, float *c)
{
    float q = mime_vec_floorf(deg * (1.0f / 90.0f) + 0.5f);
    float r = deg - q * 90.0f;
    float sr, cr;
    mime_vec_sincosf(r * (PI_F / 180.0f), &sr, &cr);
    if (r == 0.0f) {
        sr = 0.0f;
        cr = 1.0f;
    }
    float qm = q - mime_vec_floorf(q * 0.25f) * 4.0f;
    switch ((int)qm & 3) {
    case 0:
        *s = sr;
        *c = cr;
        break;
    case 1:
        *s = cr;
        *c = -sr;
        break;
    case 2:
        *s = -sr;
        *c = -cr;
        break;
    default:
        *s = -cr;
        *c = sr;
        break;
    }
}

/* adds the Bezier segments of an arc, the current point must be the arc start. */
static int arc_segments(MimeVecPath *p, float cx, float cy, float rx, float ry, float start, float sweep)
{
    if (sweep > 360.0f)
        sweep = 360.0f;
    if (sweep < -360.0f)
        sweep = -360.0f;
    float asw = sweep < 0 ? -sweep : sweep;
    int nseg = (int)mime_vec_ceilf(asw / 90.0f);
    if (nseg < 1)
        nseg = 1;
    if (nseg > 4)
        nseg = 4;
    float delta = sweep / (float)nseg;
    float s0, c0;
    sincos_deg(start, &s0, &c0);
    float dr = delta * (PI_F / 180.0f);
    float st, ct;
    mime_vec_sincosf(dr * 0.25f, &st, &ct);
    float k = (4.0f / 3.0f) * st / ct;
    for (int i = 0; i < nseg; i++) {
        float s1, c1;
        sincos_deg(start + delta * (float)(i + 1), &s1, &c1);
        float x0 = cx + rx * c0, y0 = cy + ry * s0;
        float x3 = cx + rx * c1, y3 = cy + ry * s1;
        int r = mime_vec_path_bezier_to(p, x0 - k * rx * s0, y0 + k * ry * c0, x3 + k * rx * s1, y3 - k * ry * c1, x3, y3);
        if (r)
            return r;
        s0 = s1;
        c0 = c1;
    }
    return MIME_VEC_OK;
}

static int check_ellipse(float x, float y, float w, float h)
{
    if (!(w == w && h == h && x == x && y == y))
        return MIME_VEC_INVALID;
    return MIME_VEC_OK;
}

int mime_vec_path_add_ellipse(MimeVecPath *p, float x, float y, float w, float h)
{
    int r = check_ellipse(x, y, w, h);
    if (r)
        return r;
    if (w < 0) {
        x += w;
        w = -w;
    }
    if (h < 0) {
        y += h;
        h = -h;
    }
    float rx = w * 0.5f, ry = h * 0.5f, cx = x + rx, cy = y + ry;
    r = reserve(p, 13);
    if (r)
        return r;
    p->need_start = true;
    r = push(p, cx + rx, cy, MIME_VEC_PT_START);
    if (!r)
        r = arc_segments(p, cx, cy, rx, ry, 0.0f, 360.0f);
    mime_vec_path_close_figure(p);
    return r;
}

int mime_vec_path_add_arc(MimeVecPath *p, float x, float y, float w, float h, float start, float sweep)
{
    int r = check_ellipse(x, y, w, h);
    if (r)
        return r;
    if (!(start == start) || !(sweep == sweep) || w <= 0 || h <= 0)
        return MIME_VEC_INVALID;
    if (sweep == 0.0f)
        return MIME_VEC_OK;
    float rx = w * 0.5f, ry = h * 0.5f, cx = x + rx, cy = y + ry;
    /* keep the start angle small so degrees stay precise. */
    start -= mime_vec_floorf(start * (1.0f / 360.0f)) * 360.0f;
    float s, c;
    sincos_deg(start, &s, &c);
    r = reserve(p, 14);
    if (r)
        return r;
    r = mime_vec_path_line_to(p, cx + rx * c, cy + ry * s);
    if (!r)
        r = arc_segments(p, cx, cy, rx, ry, start, sweep);
    return r;
}

int mime_vec_path_add_pie(MimeVecPath *p, float x, float y, float w, float h, float start, float sweep)
{
    int r = check_ellipse(x, y, w, h);
    if (r)
        return r;
    if (!(start == start) || !(sweep == sweep) || w <= 0 || h <= 0)
        return MIME_VEC_INVALID;
    if (sweep == 0.0f)
        return MIME_VEC_OK;
    float rx = w * 0.5f, ry = h * 0.5f, cx = x + rx, cy = y + ry;
    start -= mime_vec_floorf(start * (1.0f / 360.0f)) * 360.0f;
    float s, c;
    sincos_deg(start, &s, &c);
    r = reserve(p, 16);
    if (r)
        return r;
    p->need_start = true;
    r = push(p, cx, cy, MIME_VEC_PT_START);
    if (!r)
        r = push(p, cx + rx * c, cy + ry * s, MIME_VEC_PT_LINE);
    if (!r)
        r = arc_segments(p, cx, cy, rx, ry, start, sweep);
    mime_vec_path_close_figure(p);
    return r;
}

int mime_vec_path_add_polygon(MimeVecPath *p, const MimeVecPoint *pts, uint32_t count)
{
    if (count < 3)
        return MIME_VEC_INVALID;
    for (uint32_t i = 0; i < count; i++)
        if (!(pts[i].x == pts[i].x) || !(pts[i].y == pts[i].y))
            return MIME_VEC_INVALID;
    int r = reserve(p, count);
    if (r)
        return r;
    p->need_start = true;
    for (uint32_t i = 0; i < count; i++)
        push(p, pts[i].x, pts[i].y, i == 0 ? MIME_VEC_PT_START : MIME_VEC_PT_LINE);
    mime_vec_path_close_figure(p);
    return MIME_VEC_OK;
}

static MimeVecPoint tangent(const MimeVecPoint *pts, uint32_t n, uint32_t i, bool closed, float t)
{
    uint32_t a, b;
    if (closed) {
        a = i == 0 ? n - 1 : i - 1;
        b = i + 1 == n ? 0 : i + 1;
    } else {
        a = i == 0 ? 0 : i - 1;
        b = i + 1 == n ? n - 1 : i + 1;
    }
    MimeVecPoint r = {t * (pts[b].x - pts[a].x), t * (pts[b].y - pts[a].y)};
    return r;
}

static int spline(MimeVecPath *p, const MimeVecPoint *pts, uint32_t n, float tension, bool closed)
{
    if (!(tension == tension))
        return MIME_VEC_INVALID;
    for (uint32_t i = 0; i < n; i++)
        if (!(pts[i].x == pts[i].x) || !(pts[i].y == pts[i].y))
            return MIME_VEC_INVALID;
    int r = reserve(p, 3 * n + 2);
    if (r)
        return r;
    if (closed)
        p->need_start = true;
    r = mime_vec_path_line_to(p, pts[0].x, pts[0].y);
    uint32_t segs = closed ? n : n - 1;
    for (uint32_t i = 0; !r && i < segs; i++) {
        uint32_t j = i + 1 == n ? 0 : i + 1;
        MimeVecPoint t0 = tangent(pts, n, i, closed, tension);
        MimeVecPoint t1 = tangent(pts, n, j, closed, tension);
        r = mime_vec_path_bezier_to(p, pts[i].x + t0.x / 3.0f, pts[i].y + t0.y / 3.0f, pts[j].x - t1.x / 3.0f,
                                    pts[j].y - t1.y / 3.0f, pts[j].x, pts[j].y);
    }
    if (!r && closed)
        mime_vec_path_close_figure(p);
    return r;
}

int mime_vec_path_add_curve(MimeVecPath *p, const MimeVecPoint *pts, uint32_t count, float tension)
{
    if (count < 2)
        return MIME_VEC_INVALID;
    return spline(p, pts, count, tension, false);
}

int mime_vec_path_add_closed_curve(MimeVecPath *p, const MimeVecPoint *pts, uint32_t count, float tension)
{
    if (count < 3)
        return MIME_VEC_INVALID;
    return spline(p, pts, count, tension, true);
}
