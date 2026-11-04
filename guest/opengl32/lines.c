/*
 * lines that are wide or stippled. the pipeline draws lines one pixel wide and solid, so such a line is made into triangles
 * here: its ends go through the matrices and the viewport, each piece of the pattern that is on becomes a rectangle whose width
 * runs along the minor axis of the line, as the specification has it, and the rectangles are drawn in window coordinates with
 * the colors and texture coordinates of the ends. the pattern counts one step for each pixel along the major axis, goes on
 * along a strip or a loop and starts again for each line of GL_LINES. a context that lights or fogs its lines keeps them thin,
 * as both need the coordinates of the eye.
 */
#include "fixed.h"
#include "immediate.h"

GLAPI void APIENTRY glLineStipple(GLint factor, GLushort pattern)
{
    GL_CONTEXT_OR_RETURN();
    ctx->ff.line_stipple_factor = factor < 1 ? 1 : factor > 256 ? 256 : factor;
    ctx->ff.line_stipple_pattern = pattern;
}

BOOL gl_lines_wanted(GlContext *ctx, GLenum mode)
{
    GlFixed *f = &ctx->ff;
    if (mode != GL_LINES && mode != GL_LINE_STRIP && mode != GL_LINE_LOOP)
        return FALSE;
    if (ctx->current_program || f->window_draw || f->render_mode != GL_RENDER || f->lighting || f->fog)
        return FALSE;
    return ctx->line_width > 1.0f || gl_ff_stored(ctx, GL_LINE_STIPPLE);
}

typedef struct Batch {
    GlFfVertex *vertices;
    uint32_t count, capacity;
    BOOL failed;
} Batch;

static void push(Batch *b, const GlFfVertex *v)
{
    if (b->failed)
        return;
    if (b->count == b->capacity) {
        uint32_t capacity = b->capacity ? b->capacity * 2 : 1024;
        if (capacity > 0x2000000u / sizeof(GlFfVertex)) {
            b->failed = TRUE;
            return;
        }
        GlFfVertex *grown = gl_realloc(b->vertices, (SIZE_T)capacity * sizeof(GlFfVertex));
        if (!grown) {
            b->failed = TRUE;
            return;
        }
        b->vertices = grown;
        b->capacity = capacity;
    }
    b->vertices[b->count++] = *v;
}

typedef struct LineState {
    GLfloat mvp[16], mv[16];
    GLfloat planes[FF_CLIP_PLANES][4];
    int plane_count;
    GLint viewport[4];
    GLfloat half_width;
    BOOL stipple, flat;
    GLint factor;
    GLushort pattern;
    int64_t counter;
} LineState;

static void multiply(GLfloat *r, const GLfloat *a, const GLfloat *b)
{
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            GLfloat s = 0;
            for (int k = 0; k < 4; k++)
                s += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = s;
        }
}

static void lerp_vertex(GlFfVertex *r, const GlFfVertex *a, const GlFfVertex *b, GLfloat t)
{
    const GLfloat *pa = a->position, *pb = b->position;
    GLfloat *pr = r->position;
    for (int i = 0; i < 16; i++)
        pr[i] = pa[i] + (pb[i] - pa[i]) * t;
}

/* a piece of the line from s0 to s1 pixels along its major axis, as two triangles. */
static void piece(Batch *out, const LineState *st, const GlFfVertex *a, const GlFfVertex *b, const GLfloat *wa, const GLfloat *wb,
                  GLfloat length, GLfloat s0, GLfloat s1, GLfloat ox, GLfloat oy)
{
    GLfloat t0 = s0 / length, t1 = s1 / length;
    GlFfVertex p[2];
    GLfloat w[2][3];
    GLfloat t[2] = {t0, t1};
    for (int i = 0; i < 2; i++) {
        lerp_vertex(&p[i], a, b, t[i]);
        for (int k = 0; k < 3; k++)
            w[i][k] = wa[k] + (wb[k] - wa[k]) * t[i];
        p[i].position[3] = 1;
    }
    GlFfVertex c[4];
    for (int i = 0; i < 4; i++) {
        c[i] = p[i >> 1];
        GLfloat sign = (i & 1) ? 1.0f : -1.0f;
        c[i].position[0] = w[i >> 1][0] + ox * sign;
        c[i].position[1] = w[i >> 1][1] + oy * sign;
        c[i].position[2] = w[i >> 1][2];
        c[i].position[3] = 1;
    }
    /* c0 and c1 are the start of the piece, c2 and c3 its end. */
    push(out, &c[0]);
    push(out, &c[1]);
    push(out, &c[2]);
    push(out, &c[1]);
    push(out, &c[3]);
    push(out, &c[2]);
}

static GLfloat plane_distance(const LineState *st, const GLfloat *plane, const GlFfVertex *v)
{
    GLfloat d = 0;
    for (int r = 0; r < 4; r++) {
        GLfloat e = st->mv[r] * v->position[0] + st->mv[4 + r] * v->position[1] + st->mv[8 + r] * v->position[2] +
                    st->mv[12 + r] * v->position[3];
        d += plane[r] * e;
    }
    return d;
}

static void segment(Batch *out, LineState *st, const GlFfVertex *va, const GlFfVertex *vb)
{
    GlFfVertex a = *va, b = *vb;
    if (st->flat)
        memcpy(a.color, b.color, sizeof(a.color));
    /* the user clip planes, which a line made of triangles in window coordinates no longer meets. */
    for (int i = 0; i < st->plane_count; i++) {
        GLfloat da = plane_distance(st, st->planes[i], &a), db = plane_distance(st, st->planes[i], &b);
        if (da < 0 && db < 0)
            return;
        if (da < 0) {
            GlFfVertex clipped;
            lerp_vertex(&clipped, &a, &b, da / (da - db));
            a = clipped;
        } else if (db < 0) {
            GlFfVertex clipped;
            lerp_vertex(&clipped, &b, &a, db / (db - da));
            b = clipped;
        }
    }
    GLfloat ca[4], cb[4];
    for (int r = 0; r < 4; r++) {
        ca[r] = st->mvp[r] * a.position[0] + st->mvp[4 + r] * a.position[1] + st->mvp[8 + r] * a.position[2] +
                st->mvp[12 + r] * a.position[3];
        cb[r] = st->mvp[r] * b.position[0] + st->mvp[4 + r] * b.position[1] + st->mvp[8 + r] * b.position[2] +
                st->mvp[12 + r] * b.position[3];
    }
    /* the near plane; the rest is left to the clipping of the draw. */
    GLfloat da = ca[2] + ca[3], db = cb[2] + cb[3];
    if (da < 0 && db < 0)
        return;
    if (da < 0 || db < 0) {
        GLfloat t = da < 0 ? da / (da - db) : db / (db - da);
        GlFfVertex clipped;
        GLfloat cc[4];
        if (da < 0) {
            lerp_vertex(&clipped, &a, &b, t);
            for (int r = 0; r < 4; r++)
                cc[r] = ca[r] + (cb[r] - ca[r]) * t;
            a = clipped;
            memcpy(ca, cc, sizeof(ca));
        } else {
            lerp_vertex(&clipped, &b, &a, t);
            for (int r = 0; r < 4; r++)
                cc[r] = cb[r] + (ca[r] - cb[r]) * t;
            b = clipped;
            memcpy(cb, cc, sizeof(cb));
        }
    }
    if (ca[3] <= 0 || cb[3] <= 0)
        return;
    GLfloat wa[3], wb[3];
    for (int k = 0; k < 2; k++) {
        wa[k] = (GLfloat)st->viewport[k] + (ca[k] / ca[3] + 1) * 0.5f * (GLfloat)st->viewport[2 + k];
        wb[k] = (GLfloat)st->viewport[k] + (cb[k] / cb[3] + 1) * 0.5f * (GLfloat)st->viewport[2 + k];
    }
    wa[2] = ca[2] / ca[3];
    wb[2] = cb[2] / cb[3];
    GLfloat dx = wb[0] - wa[0], dy = wb[1] - wa[1];
    GLfloat adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    GLfloat length = adx > ady ? adx : ady;
    if (length < 1e-4f)
        return;
    BOOL x_major = adx >= ady;
    GLfloat ox = x_major ? 0 : st->half_width, oy = x_major ? st->half_width : 0;
    if (!st->stipple) {
        piece(out, st, &a, &b, wa, wb, length, 0, length, ox, oy);
        return;
    }
    /* fragment 0 is the pixel the line starts in, and covers the part of the line from -offset to 1 - offset. */
    GLfloat start = x_major ? wa[0] : wa[1];
    BOOL increasing = x_major ? dx > 0 : dy > 0;
    /* the matrices leave an error of a millionth of a pixel on a coordinate that is a whole number. */
    GLfloat scaled = start * 64 + (start < 0 ? -0.5f : 0.5f);
    start = (GLfloat)(int64_t)scaled / 64;
    GLfloat whole = (GLfloat)(int64_t)start;
    if (start < 0 && whole != start)
        whole -= 1;
    GLfloat offset = increasing ? start - whole : whole + 1 - start;
    int64_t c0 = st->counter;
    int64_t k = 0;
    GLfloat s = -offset, run_start = 0, run_end = 0;
    BOOL open = FALSE;
    GLint factor = st->factor;
    for (int guard = 0; s < length && guard < 200000; guard++) {
        int64_t global = c0 + k;
        int bit = (int)((global / factor) & 15);
        int64_t left = factor - global % factor;
        GLfloat s_end = s + (GLfloat)left;
        if ((st->pattern >> bit) & 1) {
            if (!open) {
                run_start = s;
                open = TRUE;
            }
            run_end = s_end;
        } else if (open) {
            open = FALSE;
            GLfloat r0 = run_start < 0 ? 0 : run_start, r1 = run_end > length ? length : run_end;
            if (r1 > r0)
                piece(out, st, &a, &b, wa, wb, length, r0, r1, ox, oy);
        }
        k += left;
        s = s_end;
    }
    if (open) {
        GLfloat r0 = run_start < 0 ? 0 : run_start, r1 = run_end > length ? length : run_end;
        if (r1 > r0)
            piece(out, st, &a, &b, wa, wb, length, r0, r1, ox, oy);
    }
    st->counter = c0 + (int64_t)(length + 0.5f);
}

void gl_lines_draw(GlContext *ctx, GLenum mode, const GlFfVertex *vertices, uint32_t count)
{
    GlFixed *f = &ctx->ff;
    if (count < 2)
        return;
    LineState st;
    memset(&st, 0, sizeof(st));
    multiply(st.mvp, f->projection[f->projection_depth], f->modelview[f->modelview_depth]);
    memcpy(st.mv, f->modelview[f->modelview_depth], sizeof(st.mv));
    for (int i = 0; i < FF_CLIP_PLANES; i++) {
        if (!gl_ff_stored(ctx, GL_CLIP_PLANE0 + (GLenum)i))
            continue;
        for (int k = 0; k < 4; k++)
            st.planes[st.plane_count][k] = (GLfloat)f->clip_plane[i][k];
        st.plane_count++;
    }
    for (int i = 0; i < 4; i++)
        st.viewport[i] = ctx->viewport[i];
    st.half_width = (ctx->line_width > 1.0f ? ctx->line_width : 1.0f) * 0.5f;
    st.stipple = gl_ff_stored(ctx, GL_LINE_STIPPLE);
    st.flat = f->shade_model == GL_FLAT;
    st.factor = f->line_stipple_factor > 0 ? f->line_stipple_factor : 1;
    st.pattern = f->line_stipple_pattern;
    Batch out = {};
    if (mode == GL_LINES) {
        for (uint32_t i = 0; i + 1 < count; i += 2) {
            st.counter = 0;
            segment(&out, &st, &vertices[i], &vertices[i + 1]);
        }
    } else {
        for (uint32_t i = 0; i + 1 < count; i++)
            segment(&out, &st, &vertices[i], &vertices[i + 1]);
        if (mode == GL_LINE_LOOP)
            segment(&out, &st, &vertices[count - 1], &vertices[0]);
    }
    if (out.count && !out.failed) {
        memset(f->window_mvp, 0, sizeof(f->window_mvp));
        f->window_mvp[0] = 2.0f / (GLfloat)(st.viewport[2] ? st.viewport[2] : 1);
        f->window_mvp[5] = 2.0f / (GLfloat)(st.viewport[3] ? st.viewport[3] : 1);
        f->window_mvp[10] = 1;
        f->window_mvp[12] = -1 - 2.0f * (GLfloat)st.viewport[0] / (GLfloat)(st.viewport[2] ? st.viewport[2] : 1);
        f->window_mvp[13] = -1 - 2.0f * (GLfloat)st.viewport[1] / (GLfloat)(st.viewport[3] ? st.viewport[3] : 1);
        f->window_mvp[15] = 1;
        f->window_draw = TRUE;
        f->raster_op = FF_RASTER_LINES;
        GLenum shade = f->shade_model;
        f->shade_model = GL_SMOOTH;
        gl_draw_vertices(ctx, GL_TRIANGLES, out.vertices, out.count, 7);
        f->shade_model = shade;
        f->window_draw = FALSE;
    }
    gl_free(out.vertices);
}

void gl_lines_draw_arrays(GlContext *ctx, GLenum mode, GLint first, GLsizei count)
{
    if (!ctx->vao->legacy[0].enabled || count < 2)
        return;
    GlFfVertex *v = gl_alloc((SIZE_T)count * sizeof(GlFfVertex));
    if (!v)
        return;
    for (GLsizei i = 0; i < count; i++)
        gl_ff_array_vertex(ctx, first + i, &v[i]);
    gl_lines_draw(ctx, mode, v, (uint32_t)count);
    gl_free(v);
}

void gl_lines_draw_elements(GlContext *ctx, GLenum mode, GLsizei count, GLenum type, const void *indices, GLint base_vertex)
{
    if (!ctx->vao->legacy[0].enabled || count < 2)
        return;
    uint32_t size = type == GL_UNSIGNED_BYTE ? 1 : type == GL_UNSIGNED_SHORT ? 2 : 4;
    const uint8_t *data = indices;
    if (ctx->vao->element_buffer) {
        GlBuffer *eb = gl_buffer(ctx, ctx->vao->element_buffer);
        if (!eb || !eb->mem.ptr || (uint64_t)(ULONG_PTR)indices + (uint64_t)count * size > eb->size)
            return;
        data = eb->mem.ptr + (ULONG_PTR)indices;
    }
    if (!data)
        return;
    GlFfVertex *v = gl_alloc((SIZE_T)count * sizeof(GlFfVertex));
    if (!v)
        return;
    for (GLsizei i = 0; i < count; i++) {
        uint32_t index = 0;
        memcpy(&index, data + (SIZE_T)i * size, size);
        gl_ff_array_vertex(ctx, (GLint)((int64_t)index + base_vertex), &v[i]);
    }
    gl_lines_draw(ctx, mode, v, (uint32_t)count);
    gl_free(v);
}
