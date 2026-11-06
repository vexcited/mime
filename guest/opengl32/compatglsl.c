/*
 * the fixed function built-in variables of GLSL, as the compatibility profile has them. the host rewrites a program that
 * reads them (host/gpu/glsl_compat.c): the inputs become generic attributes at the locations of mime/glcompat.h, the rest one
 * array of uniforms that this file fills from the state of the context before a draw.
 */
#include "fixed.h"

#include <mime/glcompat.h>

#define COLUMNS(m, c) ((m) + 4 * (c))

static void multiply(const GLfloat *a, const GLfloat *b, GLfloat *r)
{
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            GLfloat sum = 0;
            for (int k = 0; k < 4; k++)
                sum += a[4 * k + row] * b[4 * c + k];
            r[4 * c + row] = sum;
        }
}

/* the inverse of a 4 by 4 matrix, or the identity when it has none. */
static void invert(const GLfloat *m, GLfloat *out)
{
    GLfloat inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] -
             m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] +
             m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] +
              m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] +
             m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] +
             m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] +
             m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] +
              m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] +
             m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] +
              m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    GLfloat det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0) {
        for (int i = 0; i < 16; i++)
            out[i] = i % 5 == 0 ? 1.0f : 0.0f;
        return;
    }
    det = 1.0f / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * det;
}

static void copy4(GLfloat *dst, const GLfloat *src)
{
    memcpy(dst, src, 4 * sizeof(GLfloat));
}

static void normalize3(GLfloat *v)
{
    GLfloat length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (length <= 0)
        return;
    length = (GLfloat)(1.0 / gl_ff_root(length));
    v[0] *= length;
    v[1] *= length;
    v[2] *= length;
}

static void pack_material(GLfloat (*u)[4], uint32_t at, const GlMaterial *m)
{
    copy4(u[at], m->emission);
    copy4(u[at + 1], m->ambient);
    copy4(u[at + 2], m->diffuse);
    copy4(u[at + 3], m->specular);
    u[at + 4][0] = m->shininess;
}

static void pack(GlContext *ctx, GLfloat (*u)[4])
{
    GlFixed *f = &ctx->ff;
    const GLfloat *mv = f->modelview[f->modelview_depth], *pr = f->projection[f->projection_depth];
    GLfloat mvp[16], inverse[16];
    multiply(pr, mv, mvp);
    memcpy(u[MIME_GLC_MV], mv, 64);
    memcpy(u[MIME_GLC_P], pr, 64);
    memcpy(u[MIME_GLC_MVP], mvp, 64);
    invert(mv, inverse);
    memcpy(u[MIME_GLC_MV_INVERSE], inverse, 64);
    invert(pr, (GLfloat *)u[MIME_GLC_P_INVERSE]);
    invert(mvp, (GLfloat *)u[MIME_GLC_MVP_INVERSE]);
    /* the normal matrix is the inverse transpose of the upper 3 by 3 of the modelview matrix: its columns are the rows of the
       inverse. */
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++)
            u[MIME_GLC_NORMAL + c][r] = inverse[4 * r + c];
    GLfloat scale = 1;
    if (f->rescale_normal) {
        GLfloat length = inverse[2] * inverse[2] + inverse[6] * inverse[6] + inverse[10] * inverse[10];
        if (length > 0)
            scale = (GLfloat)(1.0 / gl_ff_root(length));
    }
    u[MIME_GLC_MISC][0] = scale;
    u[MIME_GLC_MISC][1] = (GLfloat)ctx->depth_range[0];
    u[MIME_GLC_MISC][2] = (GLfloat)ctx->depth_range[1];
    u[MIME_GLC_MISC][3] = (GLfloat)(ctx->depth_range[1] - ctx->depth_range[0]);

    u[MIME_GLC_POINT0][0] = gl_point_size(ctx);
    u[MIME_GLC_POINT0][1] = ctx->point_size_min;
    u[MIME_GLC_POINT0][2] = ctx->point_size_max;
    u[MIME_GLC_POINT0][3] = ctx->point_fade_threshold;
    u[MIME_GLC_POINT1][0] = ctx->point_attenuation[0];
    u[MIME_GLC_POINT1][1] = ctx->point_attenuation[1];
    u[MIME_GLC_POINT1][2] = ctx->point_attenuation[2];

    /* the alpha test runs on a program's output in a compatibility context, the clamps are those of glClampColor. */
    u[MIME_GLC_STATE0][0] = f->alpha_test ? (GLfloat)(f->alpha_func - GL_NEVER) : 7.0f;
    u[MIME_GLC_STATE0][1] = f->alpha_ref;
    u[MIME_GLC_STATE0][2] = ctx->clamp[0] == GL_TRUE;
    u[MIME_GLC_STATE0][3] = gl_ff_stored(ctx, GL_VERTEX_PROGRAM_TWO_SIDE);
    u[MIME_GLC_STATE1][0] = ctx->clamp[1] == GL_TRUE;
    uint32_t planes = 0;
    for (int i = 0; i < FF_CLIP_PLANES; i++)
        if (gl_ff_stored(ctx, GL_CLIP_PLANE0 + (GLenum)i))
            planes |= 1u << i;
    u[MIME_GLC_STATE1][1] = (GLfloat)planes;

    copy4(u[MIME_GLC_FOG_COLOR], f->fog_color);
    u[MIME_GLC_FOG][0] = f->fog_density;
    u[MIME_GLC_FOG][1] = f->fog_start;
    u[MIME_GLC_FOG][2] = f->fog_end;
    u[MIME_GLC_FOG][3] = f->fog_end != f->fog_start ? 1.0f / (f->fog_end - f->fog_start) : 0.0f;
    copy4(u[MIME_GLC_LIGHT_MODEL], f->model_ambient);
    pack_material(u, MIME_GLC_FRONT_MATERIAL, &f->material[0]);
    pack_material(u, MIME_GLC_BACK_MATERIAL, &f->material[1]);
    for (int i = 0; i < FF_UNITS; i++) {
        copy4(u[MIME_GLC_TEXENV + i], f->tex_env_color[i]);
        memcpy(u[MIME_GLC_TEXMAT + 4 * i], f->texture[i][f->texture_depth[i]], 64);
    }
    for (int i = 0; i < FF_CLIP_PLANES; i++)
        for (int k = 0; k < 4; k++)
            u[MIME_GLC_CLIP + i][k] = (GLfloat)f->clip_plane[i][k];
    for (int i = 0; i < FF_LIGHTS; i++) {
        const GlLight *l = &f->lights[i];
        GLfloat (*b)[4] = u + MIME_GLC_LIGHT + MIME_GLC_LIGHT_SIZE * i;
        copy4(b[0], l->ambient);
        copy4(b[1], l->diffuse);
        copy4(b[2], l->specular);
        copy4(b[3], l->position);
        GLfloat half[3] = {l->position[0], l->position[1], l->position[2]};
        normalize3(half);
        half[2] += 1.0f;
        normalize3(half);
        b[4][0] = half[0];
        b[4][1] = half[1];
        b[4][2] = half[2];
        b[4][3] = 1.0f;
        b[5][0] = l->spot_direction[0];
        b[5][1] = l->spot_direction[1];
        b[5][2] = l->spot_direction[2];
        b[5][3] = l->spot_exponent;
        b[6][0] = l->spot_cutoff;
        b[6][1] = l->spot_cutoff >= 180.0f ? -1.0f : (GLfloat)gl_ff_cosine(l->spot_cutoff);
        b[6][2] = l->attenuation[0];
        b[6][3] = l->attenuation[1];
        b[7][0] = l->attenuation[2];
    }
}

/* writes the array into the program's uniforms when it changed. */
void gl_cg_prepare(GlContext *ctx, GlProgram *p)
{
    if (!p->cg_slots || !p->ublock)
        return;
    GLfloat u[MIME_GLC_COUNT][4] = {};
    pack(ctx, u);
    uint32_t slots = p->cg_slots < MIME_GLC_COUNT ? p->cg_slots : MIME_GLC_COUNT;
    for (uint32_t i = 0; i < slots; i++) {
        uint8_t *dst = p->ublock + p->cg_offset + i * p->cg_stride;
        if (p->cg_offset + (i + 1) * p->cg_stride > p->ublock_size + 16)
            break;
        if (memcmp(dst, u[i], 16)) {
            memcpy(dst, u[i], 16);
            p->ublock_dirty = TRUE;
        }
    }
}

/* the value a built-in input has when no array feeds it: the current vertex, color, normal, texture coordinate... */
BOOL gl_cg_current(GlContext *ctx, uint32_t location, GLfloat out[4])
{
    const GlFixed *f = &ctx->ff;
    switch (location) {
    case MIME_GLC_ATTRIB_VERTEX:
        out[0] = out[1] = out[2] = 0;
        out[3] = 1;
        return TRUE;
    case MIME_GLC_ATTRIB_NORMAL:
        memcpy(out, f->current[1], 12);
        out[3] = 1;
        return TRUE;
    case MIME_GLC_ATTRIB_COLOR:
        copy4(out, f->current[0]);
        return TRUE;
    case MIME_GLC_ATTRIB_SECONDARY:
        copy4(out, f->secondary);
        return TRUE;
    case MIME_GLC_ATTRIB_FOG:
        out[0] = f->fog_coord;
        out[1] = out[2] = 0;
        out[3] = 1;
        return TRUE;
    default:
        if (location >= MIME_GLC_ATTRIB_TEXCOORD && location < MIME_GLC_ATTRIB_TEXCOORD + FF_UNITS) {
            unsigned unit = location - MIME_GLC_ATTRIB_TEXCOORD;
            copy4(out, unit == 0 ? f->current[2] : f->texcoord[unit]);
            return TRUE;
        }
        return FALSE;
    }
}

/* the arrays of glVertexPointer and its siblings feed the built-in inputs when they are enabled. */
void gl_cg_input(GlContext *ctx, GlProgram *p, GlVertexInput *in)
{
    memcpy(ctx->cg.attribs, ctx->vao->attribs, sizeof(ctx->cg.attribs));
    for (uint32_t m = p->cg_attribs; m; m &= m - 1) {
        uint32_t l = (uint32_t)__builtin_ctz(m);
        int legacy = l == MIME_GLC_ATTRIB_VERTEX ? 0 : l == MIME_GLC_ATTRIB_COLOR ? 1 : l == MIME_GLC_ATTRIB_NORMAL ? 2
                     : l >= MIME_GLC_ATTRIB_TEXCOORD ? 3 + (int)(l - MIME_GLC_ATTRIB_TEXCOORD) : -1;
        if (legacy >= 0 && ctx->vao->legacy[legacy].enabled) {
            ctx->cg.attribs[l] = ctx->vao->legacy[legacy];
            ctx->cg.attribs[l].divisor = 0;
        }
        gl_cg_current(ctx, l, ctx->cg.constants[l]);
    }
    in->attribs = ctx->cg.attribs;
    in->mask = p->attrib_mask;
    in->constants = ctx->cg.constants;
    in->constant_mask = p->cg_attribs;
    in->fixed = in->flat = FALSE;
}

/* ---------------------------------------------------------------- the core profile */

static BOOL ident(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* a core profile context has none of these: a program of version 1.40 or later that names one does not compile. */
BOOL gl_cg_core_violation(const char *source)
{
    static const char *const names[] = {"gl_Vertex", "gl_Normal", "gl_Color", "gl_SecondaryColor", "gl_FogCoord",
                                        "gl_MultiTexCoord", "gl_ModelViewMatrix", "gl_ProjectionMatrix",
                                        "gl_ModelViewProjectionMatrix", "gl_NormalMatrix", "gl_TextureMatrix",
                                        "gl_FrontColor", "gl_BackColor", "gl_TexCoord", "gl_FogFragCoord", "gl_ClipVertex",
                                        "gl_LightSource", "gl_FrontMaterial", "gl_BackMaterial", "gl_Fog", "gl_Point",
                                        "ftransform"};
    int version = 110;
    for (const char *s = source; *s; s++) {
        if (s[0] == '/' && s[1] == '/') {
            while (s[1] && s[1] != '\n')
                s++;
            continue;
        }
        if (s[0] == '/' && s[1] == '*') {
            s += 2;
            while (s[0] && !(s[0] == '*' && s[1] == '/'))
                s++;
            if (!*s)
                break;
            s++;
            continue;
        }
        if (s[0] == '#') {
            const char *d = s + 1;
            while (*d == ' ' || *d == '\t')
                d++;
            if (!strncmp(d, "version", 7)) {
                const char *v = d + 7;
                while (*v == ' ' || *v == '\t')
                    v++;
                version = 0;
                while (*v >= '0' && *v <= '9')
                    version = version * 10 + (*v++ - '0');
                break;
            }
        }
    }
    if (version < 140)
        return FALSE;
    for (const char *s = source; *s;) {
        if (s[0] == '/' && s[1] == '/') {
            while (*s && *s != '\n')
                s++;
            continue;
        }
        if (s[0] == '/' && s[1] == '*') {
            s += 2;
            while (s[0] && !(s[0] == '*' && s[1] == '/'))
                s++;
            if (*s)
                s += 2;
            continue;
        }
        if (!ident(*s)) {
            s++;
            continue;
        }
        const char *start = s;
        while (ident(*s))
            s++;
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            size_t n = strlen(names[i]);
            BOOL prefix_ok = i == 5 ? (size_t)(s - start) == n + 1 : (size_t)(s - start) == n;
            if (prefix_ok && !memcmp(start, names[i], n))
                return TRUE;
        }
    }
    return FALSE;
}
