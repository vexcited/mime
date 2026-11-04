/* the raster position: where glBitmap and glDrawPixels start, with the color that was current when it was set. */
#include "dlist.h"

void gl_raster_init(GlContext *ctx)
{
    GlRaster *r = &ctx->raster;
    r->valid = TRUE;
    r->position[3] = 1;
    for (int i = 0; i < 4; i++)
        r->color[i] = 1;
    r->zoom[0] = r->zoom[1] = 1;
    ctx->ff.line_stipple_factor = 1;
    ctx->ff.line_stipple_pattern = 0xFFFF;
}

/* the position goes through the matrices and the viewport, and is valid when it is inside the clip volume. */
void gl_raster_position(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    GlFixed *f = &ctx->ff;
    const GLfloat *mv = f->modelview[f->modelview_depth], *pr = f->projection[f->projection_depth];
    GLfloat e[4], c[4];
    for (int r = 0; r < 4; r++)
        e[r] = mv[r] * x + mv[4 + r] * y + mv[8 + r] * z + mv[12 + r] * w;
    for (int r = 0; r < 4; r++)
        c[r] = pr[r] * e[0] + pr[4 + r] * e[1] + pr[8 + r] * e[2] + pr[12 + r] * e[3];
    GlRaster *r = &ctx->raster;
    GLfloat cw = c[3];
    r->valid = cw > 0 && c[0] >= -cw && c[0] <= cw && c[1] >= -cw && c[1] <= cw && c[2] >= -cw && c[2] <= cw;
    /* a position cut off by a user clip plane is not valid either. */
    for (int i = 0; r->valid && i < FF_CLIP_PLANES; i++) {
        if (!gl_ff_stored(ctx, GL_CLIP_PLANE0 + (GLenum)i))
            continue;
        const GLdouble *p = f->clip_plane[i];
        if (p[0] * e[0] + p[1] * e[1] + p[2] * e[2] + p[3] * e[3] < 0)
            r->valid = FALSE;
    }
    if (cw != 0) {
        GLfloat inv = 1 / cw;
        r->position[0] = (GLfloat)ctx->viewport[0] + (c[0] * inv + 1) * 0.5f * (GLfloat)ctx->viewport[2];
        r->position[1] = (GLfloat)ctx->viewport[1] + (c[1] * inv + 1) * 0.5f * (GLfloat)ctx->viewport[3];
        r->position[2] = (GLfloat)(ctx->depth_range[0] + (ctx->depth_range[1] - ctx->depth_range[0]) * (c[2] * inv + 1) * 0.5);
    }
    r->position[3] = cw;
    memcpy(r->color, f->current[0], sizeof(r->color));
}

static void set_position(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    GL_CONTEXT_OR_RETURN();
    if (ctx->dl.mode && gl_dl_rasterpos(ctx, x, y, z, w))
        return;
    gl_raster_position(ctx, x, y, z, w);
}

GLAPI void APIENTRY glRasterPos2d(GLdouble x, GLdouble y)
{
    set_position((GLfloat)x, (GLfloat)y, 0, 1);
}

GLAPI void APIENTRY glRasterPos2dv(const GLdouble *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glRasterPos2f(GLfloat x, GLfloat y)
{
    set_position((GLfloat)x, (GLfloat)y, 0, 1);
}

GLAPI void APIENTRY glRasterPos2fv(const GLfloat *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glRasterPos2i(GLint x, GLint y)
{
    set_position((GLfloat)x, (GLfloat)y, 0, 1);
}

GLAPI void APIENTRY glRasterPos2iv(const GLint *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glRasterPos2s(GLshort x, GLshort y)
{
    set_position((GLfloat)x, (GLfloat)y, 0, 1);
}

GLAPI void APIENTRY glRasterPos2sv(const GLshort *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], 0, 1);
}

GLAPI void APIENTRY glRasterPos3d(GLdouble x, GLdouble y, GLdouble z)
{
    set_position((GLfloat)x, (GLfloat)y, (GLfloat)z, 1);
}

GLAPI void APIENTRY glRasterPos3dv(const GLdouble *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glRasterPos3f(GLfloat x, GLfloat y, GLfloat z)
{
    set_position((GLfloat)x, (GLfloat)y, (GLfloat)z, 1);
}

GLAPI void APIENTRY glRasterPos3fv(const GLfloat *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glRasterPos3i(GLint x, GLint y, GLint z)
{
    set_position((GLfloat)x, (GLfloat)y, (GLfloat)z, 1);
}

GLAPI void APIENTRY glRasterPos3iv(const GLint *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glRasterPos3s(GLshort x, GLshort y, GLshort z)
{
    set_position((GLfloat)x, (GLfloat)y, (GLfloat)z, 1);
}

GLAPI void APIENTRY glRasterPos3sv(const GLshort *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);
}

GLAPI void APIENTRY glRasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
    set_position((GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

GLAPI void APIENTRY glRasterPos4dv(const GLdouble *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glRasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    set_position((GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

GLAPI void APIENTRY glRasterPos4fv(const GLfloat *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glRasterPos4i(GLint x, GLint y, GLint z, GLint w)
{
    set_position((GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

GLAPI void APIENTRY glRasterPos4iv(const GLint *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}

GLAPI void APIENTRY glRasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w)
{
    set_position((GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

GLAPI void APIENTRY glRasterPos4sv(const GLshort *v)
{
    set_position((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}
