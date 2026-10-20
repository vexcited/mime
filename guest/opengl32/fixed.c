/*
 * the state of the fixed function pipeline: matrix stacks, lighting, fog, the texture environment and the
 * capabilities that switch them. a draw without a program packs it into the uniform array of the built-in program
 * in fixedshader.c when it changed. Matrices are column major floats, as gl keeps them.
 */
#include "fixed.h"

#define DIRTY(ctx) ((ctx)->ff.dirty = TRUE)

/* ---------------------------------------------------------------- initial state */

static const GLfloat g_identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static void set4(GLfloat *d, GLfloat a, GLfloat b, GLfloat c, GLfloat e)
{
    d[0] = a;
    d[1] = b;
    d[2] = c;
    d[3] = e;
}

static void material_defaults(GlMaterial *m)
{
    set4(m->ambient, 0.2f, 0.2f, 0.2f, 1);
    set4(m->diffuse, 0.8f, 0.8f, 0.8f, 1);
    set4(m->specular, 0, 0, 0, 1);
    set4(m->emission, 0, 0, 0, 1);
    m->shininess = 0;
}

void gl_ff_init(GlContext *ctx)
{
    GlFixed *f = &ctx->ff;
    f->matrix_mode = GL_MODELVIEW;
    memcpy(f->modelview[0], g_identity, sizeof(g_identity));
    memcpy(f->projection[0], g_identity, sizeof(g_identity));
    for (int u = 0; u < FF_UNITS; u++) {
        memcpy(f->texture[u][0], g_identity, sizeof(g_identity));
        set4(f->texcoord[u], 0, 0, 0, 1);
        f->tex_env_mode[u] = GL_MODULATE;
    }
    set4(f->current[0], 1, 1, 1, 1);
    set4(f->current[1], 0, 0, 1, 0);
    set4(f->current[2], 0, 0, 0, 1);
    f->edge_flag = GL_TRUE;
    f->shade_model = GL_SMOOTH;
    f->alpha_func = GL_ALWAYS;
    f->color_material_face = GL_FRONT_AND_BACK;
    f->color_material_mode = GL_AMBIENT_AND_DIFFUSE;
    f->fog_mode = GL_EXP;
    f->fog_density = 1;
    f->fog_end = 1;
    set4(f->model_ambient, 0.2f, 0.2f, 0.2f, 1);
    f->color_control = GL_SINGLE_COLOR;
    material_defaults(&f->material[0]);
    material_defaults(&f->material[1]);
    for (int i = 0; i < FF_LIGHTS; i++) {
        GlLight *l = &f->lights[i];
        set4(l->ambient, 0, 0, 0, 1);
        set4(l->diffuse, i ? 0 : 1, i ? 0 : 1, i ? 0 : 1, i ? 0 : 1);
        set4(l->specular, i ? 0 : 1, i ? 0 : 1, i ? 0 : 1, i ? 0 : 1);
        set4(l->position, 0, 0, 1, 0);
        l->spot_direction[2] = -1;
        l->spot_cutoff = 180;
        l->attenuation[0] = 1;
    }
    f->render_mode = GL_RENDER;
    f->dirty = TRUE;
}

void gl_ff_destroy(GlContext *ctx)
{
    GlFixed *f = &ctx->ff;
    gl_free(f->vertices);
    f->vertices = nullptr;
    if (f->program) {
        gl_program_free_internal(f->program);
        f->program = nullptr;
    }
}

/* ---------------------------------------------------------------- matrices */

/* the dll has no math library: a square root by Newton's method from a guess made on the exponent. */
static double root(double x)
{
    if (!(x > 0))
        return 0;
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));
    bits = (bits >> 1) + 0x1FF8000000000000ull;
    double g;
    memcpy(&g, &bits, sizeof(g));
    for (int i = 0; i < 6; i++)
        g = 0.5 * (g + x / g);
    return g;
}

/* r = a * b. */
static void multiply(GLfloat *r, const GLfloat *a, const GLfloat *b)
{
    GLfloat t[16];
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            GLfloat s = 0;
            for (int k = 0; k < 4; k++)
                s += a[k * 4 + row] * b[c * 4 + k];
            t[c * 4 + row] = s;
        }
    memcpy(r, t, sizeof(t));
}

static GLint texture_unit(GlContext *ctx)
{
    return ctx->active_texture < FF_UNITS ? (GLint)ctx->active_texture : FF_UNITS - 1;
}

/* the matrix on top of the stack the matrix mode selects, and its depth below the top. */
static GLfloat *current_matrix(GlContext *ctx, GLint **depth)
{
    GlFixed *f = &ctx->ff;
    switch (f->matrix_mode) {
    case GL_PROJECTION:
        *depth = &f->projection_depth;
        return f->projection[f->projection_depth];
    case GL_TEXTURE: {
        GLint u = texture_unit(ctx);
        *depth = &f->texture_depth[u];
        return f->texture[u][f->texture_depth[u]];
    }
    default:
        *depth = &f->modelview_depth;
        return f->modelview[f->modelview_depth];
    }
}

static GLfloat *top(GlContext *ctx)
{
    GLint *depth;
    return current_matrix(ctx, &depth);
}

GLAPI void APIENTRY glMatrixMode(GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->ff.matrix_mode = mode;
}

GLAPI void APIENTRY glLoadIdentity(void)
{
    GL_CONTEXT_OR_RETURN();
    memcpy(top(ctx), g_identity, sizeof(g_identity));
    DIRTY(ctx);
}

GLAPI void APIENTRY glLoadMatrixf(const GLfloat *m)
{
    GL_CONTEXT_OR_RETURN();
    if (!m)
        return;
    memcpy(top(ctx), m, 16 * sizeof(GLfloat));
    DIRTY(ctx);
}

GLAPI void APIENTRY glLoadMatrixd(const GLdouble *m)
{
    GL_CONTEXT_OR_RETURN();
    if (!m)
        return;
    GLfloat *t = top(ctx);
    for (int i = 0; i < 16; i++)
        t[i] = (GLfloat)m[i];
    DIRTY(ctx);
}

static void multiply_top(GlContext *ctx, const GLfloat *m)
{
    GLfloat *t = top(ctx);
    multiply(t, t, m);
    DIRTY(ctx);
}

GLAPI void APIENTRY glMultMatrixf(const GLfloat *m)
{
    GL_CONTEXT_OR_RETURN();
    if (m)
        multiply_top(ctx, m);
}

GLAPI void APIENTRY glMultMatrixd(const GLdouble *m)
{
    GL_CONTEXT_OR_RETURN();
    if (!m)
        return;
    GLfloat f[16];
    for (int i = 0; i < 16; i++)
        f[i] = (GLfloat)m[i];
    multiply_top(ctx, f);
}

static void transpose(GLfloat *r, const GLfloat *m)
{
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            r[c * 4 + row] = m[row * 4 + c];
}

GLAPI void APIENTRY glLoadTransposeMatrixf(const GLfloat *m)
{
    GL_CONTEXT_OR_RETURN();
    if (!m)
        return;
    transpose(top(ctx), m);
    DIRTY(ctx);
}

GLAPI void APIENTRY glLoadTransposeMatrixd(const GLdouble *m)
{
    GL_CONTEXT_OR_RETURN();
    if (!m)
        return;
    GLfloat f[16];
    for (int i = 0; i < 16; i++)
        f[i] = (GLfloat)m[i];
    transpose(top(ctx), f);
    DIRTY(ctx);
}

GLAPI void APIENTRY glMultTransposeMatrixf(const GLfloat *m)
{
    GL_CONTEXT_OR_RETURN();
    if (!m)
        return;
    GLfloat t[16];
    transpose(t, m);
    multiply_top(ctx, t);
}

GLAPI void APIENTRY glMultTransposeMatrixd(const GLdouble *m)
{
    GL_CONTEXT_OR_RETURN();
    if (!m)
        return;
    GLfloat f[16], t[16];
    for (int i = 0; i < 16; i++)
        f[i] = (GLfloat)m[i];
    transpose(t, f);
    multiply_top(ctx, t);
}

GLAPI void APIENTRY glLoadTransposeMatrixfARB(const GLfloat *m)
{
    glLoadTransposeMatrixf(m);
}

GLAPI void APIENTRY glLoadTransposeMatrixdARB(const GLdouble *m)
{
    glLoadTransposeMatrixd(m);
}

GLAPI void APIENTRY glMultTransposeMatrixfARB(const GLfloat *m)
{
    glMultTransposeMatrixf(m);
}

GLAPI void APIENTRY glMultTransposeMatrixdARB(const GLdouble *m)
{
    glMultTransposeMatrixd(m);
}

/* the depths are the stack's limits less one: an entry more fits while the top is below the last slot. */
static GLint stack_limit(GlContext *ctx)
{
    switch (ctx->ff.matrix_mode) {
    case GL_PROJECTION:
        return FF_PROJECTION_DEPTH;
    case GL_TEXTURE:
        return FF_TEXTURE_DEPTH;
    default:
        return FF_MODELVIEW_DEPTH;
    }
}

GLAPI void APIENTRY glPushMatrix(void)
{
    GL_CONTEXT_OR_RETURN();
    GLint *depth;
    GLfloat *t = current_matrix(ctx, &depth);
    if (*depth + 1 >= stack_limit(ctx)) {
        gl_error(ctx, GL_STACK_OVERFLOW);
        return;
    }
    memcpy(t + 16, t, 16 * sizeof(GLfloat));
    ++*depth;
}

GLAPI void APIENTRY glPopMatrix(void)
{
    GL_CONTEXT_OR_RETURN();
    GLint *depth;
    current_matrix(ctx, &depth);
    if (*depth == 0) {
        gl_error(ctx, GL_STACK_UNDERFLOW);
        return;
    }
    --*depth;
    DIRTY(ctx);
}

GLAPI void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat m[16];
    memcpy(m, g_identity, sizeof(m));
    m[12] = x;
    m[13] = y;
    m[14] = z;
    multiply_top(ctx, m);
}

GLAPI void APIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

GLAPI void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat m[16];
    memcpy(m, g_identity, sizeof(m));
    m[0] = x;
    m[5] = y;
    m[10] = z;
    multiply_top(ctx, m);
}

GLAPI void APIENTRY glScaled(GLdouble x, GLdouble y, GLdouble z)
{
    glScalef((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

static double sine(double degrees);
static double cosine(double degrees);

GLAPI void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    GL_CONTEXT_OR_RETURN();
    double length = root((double)x * x + (double)y * y + (double)z * z);
    if (length == 0)
        return;
    double ax = x / length, ay = y / length, az = z / length;
    double s = sine(angle), c = cosine(angle), t = 1 - c;
    GLfloat m[16];
    m[0] = (GLfloat)(t * ax * ax + c);
    m[1] = (GLfloat)(t * ax * ay + s * az);
    m[2] = (GLfloat)(t * ax * az - s * ay);
    m[3] = 0;
    m[4] = (GLfloat)(t * ax * ay - s * az);
    m[5] = (GLfloat)(t * ay * ay + c);
    m[6] = (GLfloat)(t * ay * az + s * ax);
    m[7] = 0;
    m[8] = (GLfloat)(t * ax * az + s * ay);
    m[9] = (GLfloat)(t * ay * az - s * ax);
    m[10] = (GLfloat)(t * az * az + c);
    m[11] = 0;
    m[12] = m[13] = m[14] = 0;
    m[15] = 1;
    multiply_top(ctx, m);
}

GLAPI void APIENTRY glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    glRotatef((GLfloat)angle, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}

GLAPI void APIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,
                            GLdouble far_val)
{
    GL_CONTEXT_OR_RETURN();
    double w = right - left, h = top_ - bottom, d = far_val - near_val;
    if (w == 0 || h == 0 || d == 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLfloat m[16] = {};
    m[0] = (GLfloat)(2 / w);
    m[5] = (GLfloat)(2 / h);
    m[10] = (GLfloat)(-2 / d);
    m[12] = (GLfloat)(-(right + left) / w);
    m[13] = (GLfloat)(-(top_ + bottom) / h);
    m[14] = (GLfloat)(-(far_val + near_val) / d);
    m[15] = 1;
    multiply_top(ctx, m);
}

GLAPI void APIENTRY glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top_, GLdouble near_val,
                              GLdouble far_val)
{
    GL_CONTEXT_OR_RETURN();
    double w = right - left, h = top_ - bottom, d = far_val - near_val;
    if (near_val <= 0 || far_val <= 0 || w == 0 || h == 0 || d == 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLfloat m[16] = {};
    m[0] = (GLfloat)(2 * near_val / w);
    m[5] = (GLfloat)(2 * near_val / h);
    m[8] = (GLfloat)((right + left) / w);
    m[9] = (GLfloat)((top_ + bottom) / h);
    m[10] = (GLfloat)(-(far_val + near_val) / d);
    m[11] = -1;
    m[14] = (GLfloat)(-2 * far_val * near_val / d);
    multiply_top(ctx, m);
}

/* sine and cosine of an angle in degrees, exact at the multiples of ninety. */
static double reduce(double degrees, int *quadrant)
{
    double turns = degrees / 90.0;
    double whole = (double)(int64_t)(turns < 0 ? turns - 0.5 : turns + 0.5);
    *quadrant = (int)(((int64_t)whole % 4 + 4) % 4);
    return (degrees - whole * 90.0) * 0.017453292519943295;
}

/* the sine and cosine of a small angle by their series, which is exact enough within a quarter turn. */
static void small_angle(double x, double *s, double *c)
{
    double x2 = x * x;
    *s = x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110 * (1 - x2 / 156))))));
    *c = 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30 * (1 - x2 / 56 * (1 - x2 / 90 * (1 - x2 / 132 * (1 - x2 / 182))))));
}

static void sincos_degrees(double degrees, double *s, double *c)
{
    int q;
    double x = reduce(degrees, &q);
    double ss, cc;
    small_angle(x, &ss, &cc);
    switch (q) {
    case 0:
        *s = ss;
        *c = cc;
        break;
    case 1:
        *s = cc;
        *c = -ss;
        break;
    case 2:
        *s = -ss;
        *c = -cc;
        break;
    default:
        *s = -cc;
        *c = ss;
        break;
    }
}

static double sine(double degrees)
{
    double s, c;
    sincos_degrees(degrees, &s, &c);
    return s;
}

static double cosine(double degrees)
{
    double s, c;
    sincos_degrees(degrees, &s, &c);
    return c;
}

/* ---------------------------------------------------------------- shading and the texture environment */

GLAPI void APIENTRY glShadeModel(GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    if (mode != GL_FLAT && mode != GL_SMOOTH) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->ff.shade_model = mode;
    DIRTY(ctx);
}

GLAPI void APIENTRY glAlphaFunc(GLenum func, GLfloat ref)
{
    GL_CONTEXT_OR_RETURN();
    if (func < GL_NEVER || func > GL_ALWAYS) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->ff.alpha_func = func;
    ctx->ff.alpha_ref = ref < 0 ? 0 : ref > 1 ? 1 : ref;
    DIRTY(ctx);
}

/* integers that stand for colors span -1 to 1 over their range. */
static GLfloat color_from_int(GLint i)
{
    return (GLfloat)((2.0 * i + 1.0) / 4294967295.0);
}

static GLint color_to_int(GLfloat c)
{
    double v = ((double)c * 4294967295.0 - 1.0) / 2.0;
    return v >= 2147483647.0 ? 2147483647 : v <= -2147483648.0 ? (GLint)-2147483647 - 1 : (GLint)v;
}

static GLfloat clamp01(GLfloat v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

static BOOL env_mode(GLenum mode)
{
    return mode == GL_MODULATE || mode == GL_REPLACE || mode == GL_DECAL || mode == GL_BLEND || mode == GL_ADD ||
           mode == GL_COMBINE;
}

/* sets one environment parameter; v holds one value, or a color for GL_TEXTURE_ENV_COLOR. */
static void tex_env(GlContext *ctx, GLenum target, GLenum pname, const GLfloat *v, BOOL vector)
{
    GlFixed *f = &ctx->ff;
    GLint u = texture_unit(ctx);
    if (target == GL_TEXTURE_FILTER_CONTROL) {
        if (pname != GL_TEXTURE_LOD_BIAS)
            gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (target == GL_POINT_SPRITE) {
        if (pname != GL_COORD_REPLACE)
            gl_error(ctx, GL_INVALID_ENUM);
        else if (v[0] != 0 && v[0] != 1)
            gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (target != GL_TEXTURE_ENV) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    switch (pname) {
    case GL_TEXTURE_ENV_MODE:
        if (!env_mode((GLenum)v[0])) {
            gl_error(ctx, GL_INVALID_ENUM);
            return;
        }
        f->tex_env_mode[u] = (GLenum)v[0];
        break;
    case GL_TEXTURE_ENV_COLOR:
        if (!vector) {
            gl_error(ctx, GL_INVALID_ENUM);
            return;
        }
        for (int i = 0; i < 4; i++)
            f->tex_env_color[u][i] = clamp01(v[i]);
        break;
    case GL_COMBINE_RGB:
    case GL_COMBINE_ALPHA:
    case GL_SOURCE0_RGB:
    case GL_SOURCE1_RGB:
    case GL_SOURCE2_RGB:
    case GL_SOURCE0_ALPHA:
    case GL_SOURCE1_ALPHA:
    case GL_SOURCE2_ALPHA:
    case GL_OPERAND0_RGB:
    case GL_OPERAND1_RGB:
    case GL_OPERAND2_RGB:
    case GL_OPERAND0_ALPHA:
    case GL_OPERAND1_ALPHA:
    case GL_OPERAND2_ALPHA:
    case GL_RGB_SCALE:
    case GL_ALPHA_SCALE:
        /* the combiner is not emulated: accepted, and GL_COMBINE draws like GL_MODULATE. */
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    DIRTY(ctx);
}

GLAPI void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    GL_CONTEXT_OR_RETURN();
    tex_env(ctx, target, pname, &param, FALSE);
}

GLAPI void APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v = (GLfloat)param;
    tex_env(ctx, target, pname, &v, FALSE);
}

GLAPI void APIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    tex_env(ctx, target, pname, params, TRUE);
}

GLAPI void APIENTRY glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4] = {(GLfloat)params[0]};
    if (pname == GL_TEXTURE_ENV_COLOR)
        for (int i = 0; i < 4; i++)
            v[i] = color_from_int(params[i]);
    tex_env(ctx, target, pname, v, TRUE);
}

/* the values of a texture environment parameter; returns how many, or 0 after raising the error. */
static int get_tex_env(GlContext *ctx, GLenum target, GLenum pname, GLfloat *v)
{
    GlFixed *f = &ctx->ff;
    GLint u = texture_unit(ctx);
    if (target == GL_TEXTURE_FILTER_CONTROL && pname == GL_TEXTURE_LOD_BIAS) {
        v[0] = 0;
        return 1;
    }
    if (target == GL_POINT_SPRITE && pname == GL_COORD_REPLACE) {
        v[0] = 0;
        return 1;
    }
    if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_MODE) {
        v[0] = (GLfloat)f->tex_env_mode[u];
        return 1;
    }
    if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_COLOR) {
        memcpy(v, f->tex_env_color[u], 4 * sizeof(GLfloat));
        return 4;
    }
    gl_error(ctx, GL_INVALID_ENUM);
    return 0;
}

GLAPI void APIENTRY glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4];
    int n = get_tex_env(ctx, target, pname, v);
    for (int i = 0; i < n; i++)
        params[i] = v[i];
}

GLAPI void APIENTRY glGetTexEnviv(GLenum target, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4];
    int n = get_tex_env(ctx, target, pname, v);
    for (int i = 0; i < n; i++)
        params[i] = pname == GL_TEXTURE_ENV_COLOR ? color_to_int(v[i]) : (GLint)v[i];
}

/* ---------------------------------------------------------------- fog */

static void fog(GlContext *ctx, GLenum pname, const GLfloat *v, BOOL color)
{
    GlFixed *f = &ctx->ff;
    switch (pname) {
    case GL_FOG_MODE:
        if ((GLenum)v[0] != GL_LINEAR && (GLenum)v[0] != GL_EXP && (GLenum)v[0] != GL_EXP2) {
            gl_error(ctx, GL_INVALID_ENUM);
            return;
        }
        f->fog_mode = (GLenum)v[0];
        break;
    case GL_FOG_DENSITY:
        if (v[0] < 0) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        f->fog_density = v[0];
        break;
    case GL_FOG_START:
        f->fog_start = v[0];
        break;
    case GL_FOG_END:
        f->fog_end = v[0];
        break;
    case GL_FOG_INDEX:
        break;
    case GL_FOG_COORD_SRC:
        if ((GLenum)v[0] != GL_FRAGMENT_DEPTH && (GLenum)v[0] != GL_FOG_COORD)
            gl_error(ctx, GL_INVALID_ENUM);
        return;
    case GL_FOG_COLOR:
        if (!color) {
            gl_error(ctx, GL_INVALID_ENUM);
            return;
        }
        memcpy(f->fog_color, v, 4 * sizeof(GLfloat));
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    DIRTY(ctx);
}

GLAPI void APIENTRY glFogf(GLenum pname, GLfloat param)
{
    GL_CONTEXT_OR_RETURN();
    fog(ctx, pname, &param, FALSE);
}

GLAPI void APIENTRY glFogi(GLenum pname, GLint param)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v = (GLfloat)param;
    fog(ctx, pname, &v, FALSE);
}

GLAPI void APIENTRY glFogfv(GLenum pname, const GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    fog(ctx, pname, params, TRUE);
}

GLAPI void APIENTRY glFogiv(GLenum pname, const GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4] = {(GLfloat)params[0]};
    if (pname == GL_FOG_COLOR)
        for (int i = 0; i < 4; i++)
            v[i] = color_from_int(params[i]);
    fog(ctx, pname, v, TRUE);
}

/* ---------------------------------------------------------------- lighting */

/* the normal matrix: the inverse transpose of the upper 3x3 of the modelview matrix. */
static void normal_matrix(const GLfloat *m, GLfloat *n)
{
    double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    double c00 = e * i - f * h, c01 = f * g - d * i, c02 = d * h - e * g;
    double det = a * c00 + b * c01 + c * c02;
    memset(n, 0, 9 * sizeof(GLfloat));
    if (det == 0) {
        n[0] = n[4] = n[8] = 1;
        return;
    }
    double r = 1 / det;
    /* the inverse transpose is the cofactor matrix over the determinant. */
    double cof[3][3] = {
        {c00, c01, c02},
        {c * h - b * i, a * i - c * g, b * g - a * h},
        {b * f - c * e, c * d - a * f, a * e - b * d},
    };
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++)
            n[col * 3 + row] = (GLfloat)(cof[row][col] * r);
}

static GlLight *light_of(GlContext *ctx, GLenum light)
{
    if (light < GL_LIGHT0 || light >= GL_LIGHT0 + FF_LIGHTS) {
        gl_error(ctx, GL_INVALID_ENUM);
        return nullptr;
    }
    return &ctx->ff.lights[light - GL_LIGHT0];
}

static int light_size(GLenum pname)
{
    switch (pname) {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR:
    case GL_POSITION:
        return 4;
    case GL_SPOT_DIRECTION:
        return 3;
    case GL_SPOT_EXPONENT:
    case GL_SPOT_CUTOFF:
    case GL_CONSTANT_ATTENUATION:
    case GL_LINEAR_ATTENUATION:
    case GL_QUADRATIC_ATTENUATION:
        return 1;
    default:
        return 0;
    }
}

/* ints[] tells the values are integers, whose colors are scaled. */
static void light(GlContext *ctx, GLenum light_enum, GLenum pname, const GLfloat *v, BOOL vector)
{
    GlLight *l = light_of(ctx, light_enum);
    if (!l)
        return;
    int size = light_size(pname);
    if (!size || (size > 1 && !vector)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlFixed *f = &ctx->ff;
    switch (pname) {
    case GL_AMBIENT:
        memcpy(l->ambient, v, sizeof(l->ambient));
        break;
    case GL_DIFFUSE:
        memcpy(l->diffuse, v, sizeof(l->diffuse));
        break;
    case GL_SPECULAR:
        memcpy(l->specular, v, sizeof(l->specular));
        break;
    case GL_POSITION: {
        const GLfloat *m = f->modelview[f->modelview_depth];
        for (int row = 0; row < 4; row++)
            l->position[row] = m[row] * v[0] + m[4 + row] * v[1] + m[8 + row] * v[2] + m[12 + row] * v[3];
        break;
    }
    case GL_SPOT_DIRECTION: {
        GLfloat n[9];
        normal_matrix(f->modelview[f->modelview_depth], n);
        for (int row = 0; row < 3; row++)
            l->spot_direction[row] = n[row] * v[0] + n[3 + row] * v[1] + n[6 + row] * v[2];
        break;
    }
    case GL_SPOT_EXPONENT:
        if (v[0] < 0 || v[0] > 128) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        l->spot_exponent = v[0];
        break;
    case GL_SPOT_CUTOFF:
        if (v[0] != 180 && (v[0] < 0 || v[0] > 90)) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        l->spot_cutoff = v[0];
        break;
    default:
        if (v[0] < 0) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        l->attenuation[pname - GL_CONSTANT_ATTENUATION] = v[0];
        break;
    }
    DIRTY(ctx);
}

GLAPI void APIENTRY glLightf(GLenum l, GLenum pname, GLfloat param)
{
    GL_CONTEXT_OR_RETURN();
    light(ctx, l, pname, &param, FALSE);
}

GLAPI void APIENTRY glLighti(GLenum l, GLenum pname, GLint param)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v = (GLfloat)param;
    light(ctx, l, pname, &v, FALSE);
}

GLAPI void APIENTRY glLightfv(GLenum l, GLenum pname, const GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    light(ctx, l, pname, params, TRUE);
}

GLAPI void APIENTRY glLightiv(GLenum l, GLenum pname, const GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4] = {};
    int n = light_size(pname);
    for (int i = 0; i < n; i++)
        v[i] = pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR ? color_from_int(params[i])
                                                                                 : (GLfloat)params[i];
    light(ctx, l, pname, v, TRUE);
}

static int get_light(GlContext *ctx, GLenum light_enum, GLenum pname, GLfloat *v)
{
    GlLight *l = light_of(ctx, light_enum);
    if (!l)
        return 0;
    switch (pname) {
    case GL_AMBIENT:
        memcpy(v, l->ambient, sizeof(l->ambient));
        return 4;
    case GL_DIFFUSE:
        memcpy(v, l->diffuse, sizeof(l->diffuse));
        return 4;
    case GL_SPECULAR:
        memcpy(v, l->specular, sizeof(l->specular));
        return 4;
    case GL_POSITION:
        memcpy(v, l->position, sizeof(l->position));
        return 4;
    case GL_SPOT_DIRECTION:
        memcpy(v, l->spot_direction, sizeof(l->spot_direction));
        return 3;
    case GL_SPOT_EXPONENT:
        v[0] = l->spot_exponent;
        return 1;
    case GL_SPOT_CUTOFF:
        v[0] = l->spot_cutoff;
        return 1;
    case GL_CONSTANT_ATTENUATION:
    case GL_LINEAR_ATTENUATION:
    case GL_QUADRATIC_ATTENUATION:
        v[0] = l->attenuation[pname - GL_CONSTANT_ATTENUATION];
        return 1;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
}

GLAPI void APIENTRY glGetLightfv(GLenum l, GLenum pname, GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4];
    int n = get_light(ctx, l, pname, v);
    for (int i = 0; i < n; i++)
        params[i] = v[i];
}

GLAPI void APIENTRY glGetLightiv(GLenum l, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4];
    int n = get_light(ctx, l, pname, v);
    BOOL color = pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR;
    for (int i = 0; i < n; i++)
        params[i] = color ? color_to_int(v[i]) : (GLint)(v[i] < 0 ? v[i] - 0.5f : v[i] + 0.5f);
}

static void light_model(GlContext *ctx, GLenum pname, const GLfloat *v, BOOL vector)
{
    GlFixed *f = &ctx->ff;
    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT:
        if (!vector) {
            gl_error(ctx, GL_INVALID_ENUM);
            return;
        }
        memcpy(f->model_ambient, v, sizeof(f->model_ambient));
        break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
        f->local_viewer = v[0] != 0;
        break;
    case GL_LIGHT_MODEL_TWO_SIDE:
        f->two_side = v[0] != 0;
        break;
    case GL_LIGHT_MODEL_COLOR_CONTROL:
        if ((GLenum)v[0] != GL_SINGLE_COLOR && (GLenum)v[0] != GL_SEPARATE_SPECULAR_COLOR) {
            gl_error(ctx, GL_INVALID_ENUM);
            return;
        }
        f->color_control = (GLenum)v[0];
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    DIRTY(ctx);
}

GLAPI void APIENTRY glLightModelf(GLenum pname, GLfloat param)
{
    GL_CONTEXT_OR_RETURN();
    light_model(ctx, pname, &param, FALSE);
}

GLAPI void APIENTRY glLightModeli(GLenum pname, GLint param)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v = (GLfloat)param;
    light_model(ctx, pname, &v, FALSE);
}

GLAPI void APIENTRY glLightModelfv(GLenum pname, const GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    light_model(ctx, pname, params, TRUE);
}

GLAPI void APIENTRY glLightModeliv(GLenum pname, const GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4] = {(GLfloat)params[0]};
    if (pname == GL_LIGHT_MODEL_AMBIENT)
        for (int i = 0; i < 4; i++)
            v[i] = color_from_int(params[i]);
    light_model(ctx, pname, v, TRUE);
}

/* which material parameter the color material follows, as the shader numbers them; 0 for none. */
static int tracked_parameter(const GlFixed *f)
{
    if (!f->color_material)
        return 0;
    switch (f->color_material_mode) {
    case GL_EMISSION:
        return 1;
    case GL_AMBIENT:
        return 2;
    case GL_DIFFUSE:
        return 3;
    case GL_SPECULAR:
        return 4;
    default:
        return 5;
    }
}

static BOOL faces_front(GLenum face)
{
    return face == GL_FRONT || face == GL_FRONT_AND_BACK;
}

static BOOL faces_back(GLenum face)
{
    return face == GL_BACK || face == GL_FRONT_AND_BACK;
}

/* a material parameter takes the current color while it is tracked, and keeps it afterwards. */
void gl_ff_track_color(GlContext *ctx)
{
    GlFixed *f = &ctx->ff;
    int tracked = tracked_parameter(f);
    for (int face = 0; face < 2; face++) {
        if (!(face ? faces_back(f->color_material_face) : faces_front(f->color_material_face)))
            continue;
        GlMaterial *m = &f->material[face];
        if (tracked == 1)
            memcpy(m->emission, f->current[0], 16);
        if (tracked == 2 || tracked == 5)
            memcpy(m->ambient, f->current[0], 16);
        if (tracked == 3 || tracked == 5)
            memcpy(m->diffuse, f->current[0], 16);
        if (tracked == 4)
            memcpy(m->specular, f->current[0], 16);
    }
    DIRTY(ctx);
}

GLAPI void APIENTRY glColorMaterial(GLenum face, GLenum mode)
{
    GL_CONTEXT_OR_RETURN();
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (mode != GL_EMISSION && mode != GL_AMBIENT && mode != GL_DIFFUSE && mode != GL_SPECULAR &&
        mode != GL_AMBIENT_AND_DIFFUSE) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->ff.color_material_face = face;
    ctx->ff.color_material_mode = mode;
    if (ctx->ff.color_material)
        gl_ff_track_color(ctx);
    DIRTY(ctx);
}

static int material_size(GLenum pname)
{
    switch (pname) {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR:
    case GL_EMISSION:
    case GL_AMBIENT_AND_DIFFUSE:
        return 4;
    case GL_SHININESS:
        return 1;
    case GL_COLOR_INDEXES:
        return 3;
    default:
        return 0;
    }
}

static void material(GlContext *ctx, GLenum face, GLenum pname, const GLfloat *v, BOOL vector)
{
    GlFixed *f = &ctx->ff;
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    int size = material_size(pname);
    if (!size || (size > 1 && !vector)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_SHININESS && (v[0] < 0 || v[0] > 128)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    int tracked = tracked_parameter(f);
    for (int side = 0; side < 2; side++) {
        if (!(side ? faces_back(face) : faces_front(face)))
            continue;
        GlMaterial *m = &f->material[side];
        BOOL followed = side ? faces_back(f->color_material_face) : faces_front(f->color_material_face);
        /* a parameter the color follows ignores the material commands. */
        if (pname == GL_AMBIENT || pname == GL_AMBIENT_AND_DIFFUSE)
            if (!(followed && (tracked == 2 || tracked == 5)))
                memcpy(m->ambient, v, sizeof(m->ambient));
        if (pname == GL_DIFFUSE || pname == GL_AMBIENT_AND_DIFFUSE)
            if (!(followed && (tracked == 3 || tracked == 5)))
                memcpy(m->diffuse, v, sizeof(m->diffuse));
        if (pname == GL_SPECULAR && !(followed && tracked == 4))
            memcpy(m->specular, v, sizeof(m->specular));
        if (pname == GL_EMISSION && !(followed && tracked == 1))
            memcpy(m->emission, v, sizeof(m->emission));
        if (pname == GL_SHININESS)
            m->shininess = v[0];
    }
    DIRTY(ctx);
}

GLAPI void APIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    material(ctx, face, pname, &param, FALSE);
}

GLAPI void APIENTRY glMateriali(GLenum face, GLenum pname, GLint param)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    GLfloat v = (GLfloat)param;
    material(ctx, face, pname, &v, FALSE);
}

GLAPI void APIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    material(ctx, face, pname, params, TRUE);
}

GLAPI void APIENTRY glMaterialiv(GLenum face, GLenum pname, const GLint *params)
{
    GL_CONTEXT_ALLOWED_IN_BEGIN();
    GLfloat v[4] = {};
    int n = material_size(pname);
    for (int i = 0; i < n; i++)
        v[i] = pname == GL_SHININESS || pname == GL_COLOR_INDEXES ? (GLfloat)params[i] : color_from_int(params[i]);
    material(ctx, face, pname, v, TRUE);
}

static int get_material(GlContext *ctx, GLenum face, GLenum pname, GLfloat *v)
{
    if (face != GL_FRONT && face != GL_BACK) {
        gl_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
    const GlMaterial *m = &ctx->ff.material[face == GL_BACK];
    switch (pname) {
    case GL_AMBIENT:
        memcpy(v, m->ambient, sizeof(m->ambient));
        return 4;
    case GL_DIFFUSE:
        memcpy(v, m->diffuse, sizeof(m->diffuse));
        return 4;
    case GL_SPECULAR:
        memcpy(v, m->specular, sizeof(m->specular));
        return 4;
    case GL_EMISSION:
        memcpy(v, m->emission, sizeof(m->emission));
        return 4;
    case GL_SHININESS:
        v[0] = m->shininess;
        return 1;
    case GL_COLOR_INDEXES:
        v[0] = 0;
        v[1] = 1;
        v[2] = 1;
        return 3;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
}

GLAPI void APIENTRY glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4];
    int n = get_material(ctx, face, pname, v);
    for (int i = 0; i < n; i++)
        params[i] = v[i];
}

GLAPI void APIENTRY glGetMaterialiv(GLenum face, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4];
    int n = get_material(ctx, face, pname, v);
    BOOL color = pname != GL_SHININESS && pname != GL_COLOR_INDEXES;
    for (int i = 0; i < n; i++)
        params[i] = color ? color_to_int(v[i]) : (GLint)(v[i] < 0 ? v[i] - 0.5f : v[i] + 0.5f);
}

/* ---------------------------------------------------------------- the render mode */

#define NAME_STACK_DEPTH 128

GLAPI GLint APIENTRY glRenderMode(GLenum mode)
{
    GL_CONTEXT_OR_RETURN(0);
    GlFixed *f = &ctx->ff;
    if (mode != GL_RENDER && mode != GL_FEEDBACK && mode != GL_SELECT) {
        gl_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
    /* a mode needs its buffer first. */
    if ((mode == GL_SELECT && !f->select_buffer) || (mode == GL_FEEDBACK && !f->feedback_buffer)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return 0;
    }
    /* nothing is picked or recorded: leaving a mode reports no hits and no values. */
    f->render_mode = mode;
    f->name_depth = 0;
    return 0;
}

GLAPI void APIENTRY glSelectBuffer(GLsizei size, GLuint *buffer)
{
    GL_CONTEXT_OR_RETURN();
    if (size < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (ctx->ff.render_mode == GL_SELECT) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    ctx->ff.select_buffer = buffer != nullptr;
}

GLAPI void APIENTRY glFeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer)
{
    GL_CONTEXT_OR_RETURN();
    if (type < GL_2D || type > GL_4D_COLOR_TEXTURE) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (size < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (ctx->ff.render_mode == GL_FEEDBACK) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    ctx->ff.feedback_buffer = buffer != nullptr;
}

GLAPI void APIENTRY glInitNames(void)
{
    GL_CONTEXT_OR_RETURN();
    ctx->ff.name_depth = 0;
}

GLAPI void APIENTRY glLoadName(GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    if (ctx->ff.render_mode == GL_SELECT && ctx->ff.name_depth == 0)
        gl_error(ctx, GL_INVALID_OPERATION);
}

GLAPI void APIENTRY glPushName(GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    if (ctx->ff.render_mode != GL_SELECT)
        return;
    if (ctx->ff.name_depth >= NAME_STACK_DEPTH) {
        gl_error(ctx, GL_STACK_OVERFLOW);
        return;
    }
    ctx->ff.name_depth++;
}

GLAPI void APIENTRY glPopName(void)
{
    GL_CONTEXT_OR_RETURN();
    if (ctx->ff.render_mode != GL_SELECT)
        return;
    if (ctx->ff.name_depth == 0) {
        gl_error(ctx, GL_STACK_UNDERFLOW);
        return;
    }
    ctx->ff.name_depth--;
}

GLAPI void APIENTRY glPassThrough(GLfloat token)
{
    GL_CONTEXT_OR_RETURN();
    if (ctx->ff.render_mode == GL_FEEDBACK)
        return;
}

/* ---------------------------------------------------------------- capabilities */

/* the capabilities that only have to read back what was set: the rest of the fixed function ones. */
static const GLenum g_stored_caps[] = {
    GL_LINE_SMOOTH,        GL_POINT_SMOOTH,       GL_POLYGON_SMOOTH,    GL_LINE_STIPPLE,      GL_POLYGON_STIPPLE,
    GL_AUTO_NORMAL,        GL_COLOR_LOGIC_OP,     GL_INDEX_LOGIC_OP,    GL_TEXTURE_GEN_S,     GL_TEXTURE_GEN_T,
    GL_TEXTURE_GEN_R,      GL_TEXTURE_GEN_Q,      GL_CLIP_PLANE0,       GL_CLIP_PLANE1,       GL_CLIP_PLANE2,
    GL_CLIP_PLANE3,        GL_CLIP_PLANE4,        GL_CLIP_PLANE5,       GL_COLOR_SUM,         GL_MAP1_COLOR_4,
    GL_MAP1_INDEX,         GL_MAP1_NORMAL,        GL_MAP1_TEXTURE_COORD_1, GL_MAP1_TEXTURE_COORD_2, GL_MAP1_TEXTURE_COORD_3,
    GL_MAP1_TEXTURE_COORD_4, GL_MAP1_VERTEX_3,    GL_MAP1_VERTEX_4,     GL_MAP2_COLOR_4,      GL_MAP2_INDEX,
    GL_MAP2_NORMAL,        GL_MAP2_TEXTURE_COORD_1, GL_MAP2_TEXTURE_COORD_2, GL_MAP2_TEXTURE_COORD_3, GL_MAP2_TEXTURE_COORD_4,
    GL_MAP2_VERTEX_3,      GL_MAP2_VERTEX_4,      GL_TEXTURE_CUBE_MAP,  GL_POINT_SPRITE,      GL_POLYGON_OFFSET_LINE,
    GL_POLYGON_OFFSET_POINT, GL_SAMPLE_COVERAGE,  GL_SAMPLE_ALPHA_TO_ONE, GL_TEXTURE_CUBE_MAP_SEAMLESS,
    GL_DEBUG_OUTPUT,       GL_DEBUG_OUTPUT_SYNCHRONOUS, GL_RASTERIZER_DISCARD, GL_CLIP_DISTANCE0, GL_CLIP_DISTANCE1,
};

#define STORED_CAPS (sizeof(g_stored_caps) / sizeof(g_stored_caps[0]))
static_assert(STORED_CAPS <= FF_STORED_CAPS, "GlFixed::stored is too small");

static int stored_index(GLenum cap)
{
    for (size_t i = 0; i < STORED_CAPS; i++)
        if (g_stored_caps[i] == cap)
            return (int)i;
    return -1;
}

/* a capability of the fixed function pipeline: TRUE when it is one, whatever it did. */
BOOL gl_ff_capability(GlContext *ctx, GLenum cap, BOOL on)
{
    GlFixed *f = &ctx->ff;
    GLint unit = texture_unit(ctx);
    switch (cap) {
    case GL_TEXTURE_1D:
        f->texture_1d[unit] = on;
        break;
    case GL_TEXTURE_2D:
        f->texture_2d[unit] = on;
        break;
    case GL_TEXTURE_3D:
        f->texture_3d[unit] = on;
        break;
    case GL_LIGHTING:
        f->lighting = on;
        break;
    case GL_ALPHA_TEST:
        f->alpha_test = on;
        break;
    case GL_FOG:
        f->fog = on;
        break;
    case GL_NORMALIZE:
        f->normalize = on;
        break;
    case GL_RESCALE_NORMAL:
        f->rescale_normal = on;
        break;
    case GL_COLOR_MATERIAL:
        f->color_material = on;
        if (on)
            gl_ff_track_color(ctx);
        break;
    default:
        if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + FF_LIGHTS) {
            f->lights[cap - GL_LIGHT0].enabled = on;
            break;
        }
        int i = stored_index(cap);
        if (i < 0)
            return FALSE;
        f->stored[i] = on;
        return TRUE;
    }
    DIRTY(ctx);
    return TRUE;
}

/* the client states that exist without doing anything: edge flags, color indexes, secondary colors, fog coordinates. */
int gl_ff_unused_array(GLenum cap)
{
    switch (cap) {
    case GL_EDGE_FLAG_ARRAY:
        return 0;
    case GL_INDEX_ARRAY:
        return 1;
    case GL_SECONDARY_COLOR_ARRAY:
        return 2;
    case GL_FOG_COORD_ARRAY:
        return 3;
    default:
        return -1;
    }
}

BOOL gl_ff_is_enabled(GlContext *ctx, GLenum cap, BOOL *known)
{
    GlFixed *f = &ctx->ff;
    GLint unit = texture_unit(ctx);
    *known = TRUE;
    switch (cap) {
    case GL_TEXTURE_1D:
        return f->texture_1d[unit];
    case GL_TEXTURE_2D:
        return f->texture_2d[unit];
    case GL_TEXTURE_3D:
        return f->texture_3d[unit];
    case GL_LIGHTING:
        return f->lighting;
    case GL_ALPHA_TEST:
        return f->alpha_test;
    case GL_FOG:
        return f->fog;
    case GL_NORMALIZE:
        return f->normalize;
    case GL_RESCALE_NORMAL:
        return f->rescale_normal;
    case GL_COLOR_MATERIAL:
        return f->color_material;
    case GL_VERTEX_ARRAY:
        return ctx->vao->legacy[0].enabled;
    case GL_COLOR_ARRAY:
        return ctx->vao->legacy[1].enabled;
    case GL_NORMAL_ARRAY:
        return ctx->vao->legacy[2].enabled;
    case GL_TEXTURE_COORD_ARRAY:
        return ctx->vao->legacy[3 + (f->client_unit < FF_UNITS ? f->client_unit : 0)].enabled;
    default:
        break;
    }
    if (gl_ff_unused_array(cap) >= 0)
        return f->unused_arrays[gl_ff_unused_array(cap)];
    if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + FF_LIGHTS)
        return f->lights[cap - GL_LIGHT0].enabled;
    int i = stored_index(cap);
    if (i >= 0)
        return f->stored[i];
    *known = FALSE;
    return FALSE;
}

/* ---------------------------------------------------------------- queries */

/* the size, type or stride of a client array, numbered 0 to 2. */
static int array_query(GlContext *ctx, int which, GLenum kind, GLdouble *v)
{
    const GlVertexAttrib *a = &ctx->vao->legacy[which];
    v[0] = kind == 0 ? (a->size ? a->size : (which == 2 ? 3 : 4)) : kind == 1 ? (a->type ? a->type : GL_FLOAT) : a->stride;
    return 1;
}

/* the values of the fixed function state that glGet reads; returns how many, 0 for a name that is not its own. */
int gl_ff_get(GlContext *ctx, GLenum pname, GLdouble *v, BOOL *colors)
{
    GlFixed *f = &ctx->ff;
    GLint unit = texture_unit(ctx);
    const GLfloat *m = nullptr;
    BOOL transposed = FALSE;
    switch (pname) {
    case GL_MATRIX_MODE:
        v[0] = f->matrix_mode;
        return 1;
    case GL_TRANSPOSE_MODELVIEW_MATRIX:
        transposed = TRUE;
        [[fallthrough]];
    case GL_MODELVIEW_MATRIX:
        m = f->modelview[f->modelview_depth];
        break;
    case GL_TRANSPOSE_PROJECTION_MATRIX:
        transposed = TRUE;
        [[fallthrough]];
    case GL_PROJECTION_MATRIX:
        m = f->projection[f->projection_depth];
        break;
    case GL_TRANSPOSE_TEXTURE_MATRIX:
        transposed = TRUE;
        [[fallthrough]];
    case GL_TEXTURE_MATRIX:
        m = f->texture[unit][f->texture_depth[unit]];
        break;
    case GL_MODELVIEW_STACK_DEPTH:
        v[0] = f->modelview_depth + 1;
        return 1;
    case GL_PROJECTION_STACK_DEPTH:
        v[0] = f->projection_depth + 1;
        return 1;
    case GL_TEXTURE_STACK_DEPTH:
        v[0] = f->texture_depth[unit] + 1;
        return 1;
    case GL_CURRENT_COLOR:
        *colors = TRUE;
        for (int i = 0; i < 4; i++)
            v[i] = f->current[0][i];
        return 4;
    case GL_CURRENT_NORMAL:
        for (int i = 0; i < 3; i++)
            v[i] = f->current[1][i];
        return 3;
    case GL_CURRENT_TEXTURE_COORDS: {
        GLint c = f->client_unit < FF_UNITS ? (GLint)f->client_unit : 0;
        const GLfloat *t = c == 0 ? f->current[2] : f->texcoord[c];
        for (int i = 0; i < 4; i++)
            v[i] = t[i];
        return 4;
    }
    case GL_CURRENT_RASTER_POSITION:
    case GL_CURRENT_RASTER_TEXTURE_COORDS:
        v[0] = v[1] = v[2] = 0;
        v[3] = 1;
        return 4;
    case GL_CURRENT_RASTER_COLOR:
        *colors = TRUE;
        v[0] = v[1] = v[2] = v[3] = 1;
        return 4;
    case GL_CURRENT_RASTER_DISTANCE:
        v[0] = 0;
        return 1;
    case GL_CURRENT_RASTER_POSITION_VALID:
        v[0] = 1;
        return 1;
    case GL_EDGE_FLAG:
        v[0] = f->edge_flag;
        return 1;
    case GL_SHADE_MODEL:
        v[0] = f->shade_model;
        return 1;
    case GL_ALPHA_TEST_FUNC:
        v[0] = f->alpha_func;
        return 1;
    case GL_ALPHA_TEST_REF:
        v[0] = f->alpha_ref;
        return 1;
    case GL_FOG_MODE:
        v[0] = f->fog_mode;
        return 1;
    case GL_FOG_DENSITY:
        v[0] = f->fog_density;
        return 1;
    case GL_FOG_START:
        v[0] = f->fog_start;
        return 1;
    case GL_FOG_END:
        v[0] = f->fog_end;
        return 1;
    case GL_FOG_INDEX:
        v[0] = 0;
        return 1;
    case GL_FOG_COLOR:
        *colors = TRUE;
        for (int i = 0; i < 4; i++)
            v[i] = f->fog_color[i];
        return 4;
    case GL_LIGHT_MODEL_AMBIENT:
        *colors = TRUE;
        for (int i = 0; i < 4; i++)
            v[i] = f->model_ambient[i];
        return 4;
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
        v[0] = f->local_viewer;
        return 1;
    case GL_LIGHT_MODEL_TWO_SIDE:
        v[0] = f->two_side;
        return 1;
    case GL_LIGHT_MODEL_COLOR_CONTROL:
        v[0] = f->color_control;
        return 1;
    case GL_COLOR_MATERIAL_FACE:
        v[0] = f->color_material_face;
        return 1;
    case GL_COLOR_MATERIAL_PARAMETER:
        v[0] = f->color_material_mode;
        return 1;
    case GL_RENDER_MODE:
        v[0] = f->render_mode;
        return 1;
    case GL_NAME_STACK_DEPTH:
        v[0] = f->name_depth;
        return 1;
    case GL_MAX_NAME_STACK_DEPTH:
        v[0] = NAME_STACK_DEPTH;
        return 1;
    case GL_MAX_MODELVIEW_STACK_DEPTH:
        v[0] = FF_MODELVIEW_DEPTH;
        return 1;
    case GL_MAX_PROJECTION_STACK_DEPTH:
        v[0] = FF_PROJECTION_DEPTH;
        return 1;
    case GL_MAX_TEXTURE_STACK_DEPTH:
        v[0] = FF_TEXTURE_DEPTH;
        return 1;
    case GL_MAX_LIGHTS:
        v[0] = FF_LIGHTS;
        return 1;
    case GL_CLIENT_ACTIVE_TEXTURE:
        v[0] = GL_TEXTURE0 + f->client_unit;
        return 1;
    case GL_VERTEX_ARRAY_SIZE:
    case GL_VERTEX_ARRAY_TYPE:
    case GL_VERTEX_ARRAY_STRIDE:
        return array_query(ctx, 0, pname - GL_VERTEX_ARRAY_SIZE, v);
    case GL_COLOR_ARRAY_SIZE:
    case GL_COLOR_ARRAY_TYPE:
    case GL_COLOR_ARRAY_STRIDE:
        return array_query(ctx, 1, pname - GL_COLOR_ARRAY_SIZE, v);
    case GL_NORMAL_ARRAY_TYPE:
        return array_query(ctx, 2, 1, v);
    case GL_NORMAL_ARRAY_STRIDE:
        return array_query(ctx, 2, 2, v);
    case GL_TEXTURE_COORD_ARRAY_SIZE:
    case GL_TEXTURE_COORD_ARRAY_TYPE:
    case GL_TEXTURE_COORD_ARRAY_STRIDE:
        return array_query(ctx, 3 + (f->client_unit < FF_UNITS ? (int)f->client_unit : 0),
                           pname - GL_TEXTURE_COORD_ARRAY_SIZE, v);
    default:
        break;
    }
    if (!m) {
        BOOL known;
        BOOL on = gl_ff_is_enabled(ctx, pname, &known);
        if (!known)
            return 0;
        v[0] = on;
        return 1;
    }
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            v[c * 4 + r] = transposed ? m[r * 4 + c] : m[c * 4 + r];
    return 16;
}

/* ---------------------------------------------------------------- the uniform array */

static GLfloat texture_format_bits(GLenum internal)
{
    switch (internal) {
    case GL_ALPHA:
    case GL_ALPHA4:
    case GL_ALPHA8:
    case GL_ALPHA12:
    case GL_ALPHA16:
    case GL_COMPRESSED_ALPHA:
        return FF_TEX_ALPHA;
    case GL_LUMINANCE:
    case 1:
    case GL_LUMINANCE4:
    case GL_LUMINANCE8:
    case GL_LUMINANCE12:
    case GL_LUMINANCE16:
    case GL_COMPRESSED_LUMINANCE:
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32:
    case GL_RED:
    case GL_R8:
    case GL_R16:
    case GL_R16F:
    case GL_R32F:
    case GL_RG:
    case GL_RG8:
    case GL_RG16:
    case GL_RG16F:
    case GL_RG32F:
        return FF_TEX_COLOR;
    case GL_LUMINANCE_ALPHA:
    case 2:
    case GL_LUMINANCE4_ALPHA4:
    case GL_LUMINANCE6_ALPHA2:
    case GL_LUMINANCE8_ALPHA8:
    case GL_LUMINANCE12_ALPHA4:
    case GL_LUMINANCE12_ALPHA12:
    case GL_LUMINANCE16_ALPHA16:
    case GL_COMPRESSED_LUMINANCE_ALPHA:
        return FF_TEX_COLOR | FF_TEX_ALPHA;
    case GL_INTENSITY:
    case GL_INTENSITY4:
    case GL_INTENSITY8:
    case GL_INTENSITY12:
    case GL_INTENSITY16:
    case GL_COMPRESSED_INTENSITY:
        return FF_TEX_COLOR | FF_TEX_ALPHA | FF_TEX_INTENSITY;
    case GL_RGB:
    case 3:
    case GL_R3_G3_B2:
    case GL_RGB4:
    case GL_RGB5:
    case GL_RGB8:
    case GL_RGB10:
    case GL_RGB12:
    case GL_RGB16:
    case GL_SRGB:
    case GL_SRGB8:
    case GL_COMPRESSED_RGB:
    case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
    case GL_RGB16F:
    case GL_RGB32F:
    case GL_R11F_G11F_B10F:
    case GL_RGB9_E5:
        return FF_TEX_COLOR;
    default:
        return FF_TEX_COLOR | FF_TEX_ALPHA;
    }
}

static BOOL texture_complete(GlContext *ctx, GlTexture *t)
{
    if (!gl_texture_ready(ctx, t))
        return FALSE;
    GLenum min = t->sampler.min_filter;
    if (min == GL_NEAREST || min == GL_LINEAR)
        return TRUE;
    uint32_t size = t->width > t->height ? t->width : t->height, levels = 1;
    while (size > 1) {
        size >>= 1;
        levels++;
    }
    uint32_t mask = (1u << levels) - 1;
    return (t->defined_levels & mask) == mask;
}

/* texture unit 0 as the shader sees it: the environment mode and the format bits, or no texturing. */
BOOL gl_ff_texture_state(GlContext *ctx, uint32_t *env, uint32_t *format)
{
    GlFixed *f = &ctx->ff;
    *env = FF_ENV_OFF;
    *format = 0;
    if (!f->texture_2d[0])
        return FALSE;
    GlTexture *t = gl_unit_texture(ctx, 0, GL_SAMPLER_2D);
    if (!t || !texture_complete(ctx, t))
        return FALSE;
    switch (f->tex_env_mode[0]) {
    case GL_REPLACE:
        *env = FF_ENV_REPLACE;
        break;
    case GL_DECAL:
        *env = FF_ENV_DECAL;
        break;
    case GL_BLEND:
        *env = FF_ENV_BLEND;
        break;
    case GL_ADD:
        *env = FF_ENV_ADD;
        break;
    default:
        *env = FF_ENV_MODULATE;
        break;
    }
    *format = (uint32_t)texture_format_bits(t->internal_format);
    return TRUE;
}

static void put_matrix(GLfloat (*slots)[4], const GLfloat *m)
{
    memcpy(slots, m, 16 * sizeof(GLfloat));
}

/* writes the state a draw reads into the uniform array. the texture and point size slots are left to the caller. */
void gl_ff_pack(GlContext *ctx, GLfloat (*u)[4])
{
    GlFixed *f = &ctx->ff;
    const GLfloat *mv = f->modelview[f->modelview_depth];
    GLfloat mvp[16];
    multiply(mvp, f->projection[f->projection_depth], mv);
    put_matrix(&u[FF_U_MVP], mvp);
    put_matrix(&u[FF_U_MV], mv);
    GLfloat n[9];
    normal_matrix(mv, n);
    for (int c = 0; c < 3; c++)
        set4(u[FF_U_NORMAL + c], n[c * 3], n[c * 3 + 1], n[c * 3 + 2], 0);
    put_matrix(&u[FF_U_TEXMAT], f->texture[0][f->texture_depth[0]]);

    /* the alpha function is numbered from never, and always means the test is off. */
    GLfloat func = f->alpha_test ? (GLfloat)(f->alpha_func - GL_NEVER) : 7;
    GLfloat fogmode = !f->fog ? 0 : f->fog_mode == GL_LINEAR ? 1 : f->fog_mode == GL_EXP ? 2 : 3;
    set4(u[FF_U_FLAGS0], f->lighting, func, f->alpha_ref, fogmode);
    set4(u[FF_U_FLAGS1], 0, 0, f->shade_model == GL_FLAT, f->normalize || f->rescale_normal);
    memcpy(u[FF_U_ENV_COLOR], f->tex_env_color[0], 16);
    GLfloat range = f->fog_end - f->fog_start;
    set4(u[FF_U_FOG], f->fog_density, f->fog_start, f->fog_end, range != 0 ? 1 / range : 0);
    memcpy(u[FF_U_FOG_COLOR], f->fog_color, 16);
    set4(u[FF_U_MISC], ctx->point_size, (GLfloat)tracked_parameter(f), f->local_viewer,
         f->color_control == GL_SEPARATE_SPECULAR_COLOR);
    memcpy(u[FF_U_MODEL_AMBIENT], f->model_ambient, 16);
    const GlMaterial *mat = &f->material[0];
    memcpy(u[FF_U_EMISSION], mat->emission, 16);
    memcpy(u[FF_U_AMBIENT], mat->ambient, 16);
    memcpy(u[FF_U_DIFFUSE], mat->diffuse, 16);
    memcpy(u[FF_U_SPECULAR], mat->specular, 16);
    set4(u[FF_U_SHININESS], mat->shininess, 0, 0, 0);
    for (int i = 0; i < FF_LIGHTS; i++) {
        const GlLight *l = &f->lights[i];
        GLfloat(*s)[4] = &u[FF_U_LIGHT + i * FF_U_LIGHT_SIZE];
        memcpy(s[0], l->ambient, 16);
        memcpy(s[1], l->diffuse, 16);
        memcpy(s[2], l->specular, 16);
        memcpy(s[3], l->position, 16);
        GLfloat d[3] = {l->spot_direction[0], l->spot_direction[1], l->spot_direction[2]};
        double len = root((double)d[0] * d[0] + (double)d[1] * d[1] + (double)d[2] * d[2]);
        if (len > 0)
            for (int k = 0; k < 3; k++)
                d[k] = (GLfloat)(d[k] / len);
        set4(s[4], d[0], d[1], d[2], l->spot_exponent);
        /* the cutoff is stored as the cosine to compare with; 180 degrees has no cone. */
        double cutoff = l->spot_cutoff;
        double cosine_of_cutoff = cutoff == 180 ? -1 : cosine(cutoff);
        set4(s[5], (GLfloat)cosine_of_cutoff, cutoff != 180, 0, 0);
        set4(s[6], l->attenuation[0], l->attenuation[1], l->attenuation[2], l->enabled);
    }
}
