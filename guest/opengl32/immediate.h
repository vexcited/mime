/*
 * immediate mode vertex commands, inline since they are small and hot: a vertex copies the current color, normal and
 * texture coordinate next to its position.
 */
#ifndef MIME_OPENGL32_IMMEDIATE_H
#define MIME_OPENGL32_IMMEDIATE_H

#include "opengl32.h"
#include "dlist.h"

BOOL gl_ff_grow(GlFixed *f);

static inline void gl_ff_vertex(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    GlFixed *f = &ctx->ff;
    if (!f->in_begin)
        return;
    if (f->vertex_count == f->vertex_cap && !gl_ff_grow(f))
        return;
    GlFfVertex *v = &f->vertices[f->vertex_count++];
    v->position[0] = x;
    v->position[1] = y;
    v->position[2] = z;
    v->position[3] = w;
    /* the color, normal and texture coordinate of unit 0 follow each other in both. */
    memcpy(v->color, f->current, sizeof(f->current));
}

void gl_ff_track_color(GlContext *ctx);

static inline void gl_ff_color(GlContext *ctx, GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GlFixed *f = &ctx->ff;
    f->current[0][0] = r;
    f->current[0][1] = g;
    f->current[0][2] = b;
    f->current[0][3] = a;
    f->varying |= 1;
    if (f->color_material)
        gl_ff_track_color(ctx);
}

static inline void gl_ff_normal(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z)
{
    GlFixed *f = &ctx->ff;
    f->current[1][0] = x;
    f->current[1][1] = y;
    f->current[1][2] = z;
    f->varying |= 2;
}

/* FALSE when the unit does not exist. */
static inline BOOL gl_ff_texcoord(GlContext *ctx, GLuint unit, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    GlFixed *f = &ctx->ff;
    GLfloat *c;
    if (unit == 0) {
        c = f->current[2];
        f->varying |= 4;
    } else if (unit < FF_UNITS) {
        c = f->texcoord[unit];
    } else {
        return unit < 32;
    }
    c[0] = s;
    c[1] = t;
    c[2] = r;
    c[3] = q;
    return TRUE;
}

/*
 * the entry points of the vertex commands: while a list is compiled they are recorded, and only run now when the list also
 * executes. the functions above are what runs, and what a list runs again.
 */
static inline void gl_api_vertex(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    if (__builtin_expect(ctx->dl.mode != 0, 0) && gl_dl_vertex(ctx, x, y, z, w))
        return;
    gl_ff_vertex(ctx, x, y, z, w);
}

static inline void gl_api_color(GlContext *ctx, GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    if (__builtin_expect(ctx->dl.mode != 0, 0) && gl_dl_color(ctx, r, g, b, a))
        return;
    gl_ff_color(ctx, r, g, b, a);
}

static inline void gl_api_normal(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z)
{
    if (__builtin_expect(ctx->dl.mode != 0, 0) && gl_dl_normal(ctx, x, y, z))
        return;
    gl_ff_normal(ctx, x, y, z);
}

static inline BOOL gl_api_texcoord(GlContext *ctx, GLuint unit, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    if (__builtin_expect(ctx->dl.mode != 0, 0) && gl_dl_texcoord(ctx, unit, s, t, r, q))
        return unit < 32;
    return gl_ff_texcoord(ctx, unit, s, t, r, q);
}

#endif
