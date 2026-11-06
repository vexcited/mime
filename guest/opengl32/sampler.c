/*
 * sampler objects: sampling parameters that belong to a texture unit and replace those of the texture bound to it. The
 * host sampler of an object is made with the levels of the texture that is sampled, as for the texture's own parameters.
 */
#include "opengl32.h"

typedef struct GlSamplerObject {
    GLuint name;
    GlSamplerState state;
} GlSamplerObject;

static GlSamplerObject *sampler_of(GlContext *ctx, GLuint name)
{
    void *s = gl_name_get(&ctx->shared->samplers, name);
    return s == GL_RESERVED ? nullptr : s;
}

static GlSamplerObject *sampler_make(GlContext *ctx, GLuint name)
{
    GlSamplerObject *s = gl_alloc(sizeof(GlSamplerObject));
    if (!s)
        return nullptr;
    s->name = name;
    GlSamplerState *d = &s->state;
    d->min_filter = GL_NEAREST_MIPMAP_LINEAR;
    d->mag_filter = GL_LINEAR;
    d->wrap_s = d->wrap_t = d->wrap_r = GL_REPEAT;
    d->min_lod = -1000;
    d->max_lod = 1000;
    d->compare_func = GL_LEQUAL;
    d->max_anisotropy = 1;
    gl_name_set(&ctx->shared->samplers, name, s);
    return s;
}

/* the host sampler for a texture on a unit: the bound object's parameters, else the texture's own. */
uint32_t gl_unit_sampler(GlContext *ctx, GLuint unit, GlTexture *t)
{
    GlSamplerObject *s = unit < 32 && ctx->sampler_units[unit] ? sampler_of(ctx, ctx->sampler_units[unit]) : nullptr;
    if (!s)
        return gl_texture_sampler(t);
    return gl_sampler_state_id(&s->state, t->levels, t->base_level, t->max_level);
}

GLAPI void APIENTRY glGenSamplers(GLsizei count, GLuint *samplers)
{
    GL_CONTEXT_OR_RETURN();
    if (count < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < count; i++) {
        samplers[i] = gl_name_new(&ctx->shared->samplers);
        sampler_make(ctx, samplers[i]);
    }
}

GLAPI void APIENTRY glCreateSamplers(GLsizei count, GLuint *samplers)
{
    glGenSamplers(count, samplers);
}

GLAPI void APIENTRY glDeleteSamplers(GLsizei count, const GLuint *samplers)
{
    GL_CONTEXT_OR_RETURN();
    if (count < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < count; i++) {
        GlSamplerObject *s = sampler_of(ctx, samplers[i]);
        if (!s)
            continue;
        for (int unit = 0; unit < 32; unit++)
            if (ctx->sampler_units[unit] == samplers[i]) {
                ctx->sampler_units[unit] = 0;
                ctx->dirty |= DIRTY_TEXTURES;
            }
        gl_name_set(&ctx->shared->samplers, samplers[i], nullptr);
        gl_free(s);
    }
}

GLAPI GLboolean APIENTRY glIsSampler(GLuint sampler)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    return sampler_of(ctx, sampler) ? GL_TRUE : GL_FALSE;
}

GLAPI void APIENTRY glBindSampler(GLuint unit, GLuint sampler)
{
    GL_CONTEXT_OR_RETURN();
    if (unit >= 32) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (sampler && !sampler_of(ctx, sampler)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    ctx->sampler_units[unit] = sampler;
    ctx->dirty |= DIRTY_TEXTURES;
}

GLAPI void APIENTRY glBindSamplers(GLuint first, GLsizei count, const GLuint *samplers)
{
    GL_CONTEXT_OR_RETURN();
    if (count < 0 || first + (GLuint)count > 32) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    for (GLsizei i = 0; samplers && i < count; i++)
        if (samplers[i] && !sampler_of(ctx, samplers[i])) {
            gl_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    for (GLsizei i = 0; i < count; i++)
        ctx->sampler_units[first + (GLuint)i] = samplers ? samplers[i] : 0;
    ctx->dirty |= DIRTY_TEXTURES;
}

static BOOL filter_valid(GLenum f, BOOL mag)
{
    if (f == GL_NEAREST || f == GL_LINEAR)
        return TRUE;
    return !mag && (f == GL_NEAREST_MIPMAP_NEAREST || f == GL_LINEAR_MIPMAP_NEAREST || f == GL_NEAREST_MIPMAP_LINEAR ||
                    f == GL_LINEAR_MIPMAP_LINEAR);
}

static BOOL wrap_valid(GLenum w)
{
    return w == GL_REPEAT || w == GL_CLAMP_TO_EDGE || w == GL_MIRRORED_REPEAT || w == GL_CLAMP_TO_BORDER ||
           w == GL_MIRROR_CLAMP_TO_EDGE || w == GL_CLAMP;
}

/* v holds the parameter as floats; integer border colors arrive scaled the way glTexParameteriv does. */
static void sampler_parameter(GlContext *ctx, GLuint name, GLenum pname, const GLfloat *v)
{
    GlSamplerObject *o = sampler_of(ctx, name);
    if (!o) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GlSamplerState *s = &o->state;
    GLenum e = (GLenum)v[0];
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
        if (!filter_valid(e, FALSE))
            goto bad_value;
        s->min_filter = e;
        break;
    case GL_TEXTURE_MAG_FILTER:
        if (!filter_valid(e, TRUE))
            goto bad_value;
        s->mag_filter = e;
        break;
    case GL_TEXTURE_WRAP_S:
        if (!wrap_valid(e))
            goto bad_value;
        s->wrap_s = e;
        break;
    case GL_TEXTURE_WRAP_T:
        if (!wrap_valid(e))
            goto bad_value;
        s->wrap_t = e;
        break;
    case GL_TEXTURE_WRAP_R:
        if (!wrap_valid(e))
            goto bad_value;
        s->wrap_r = e;
        break;
    case GL_TEXTURE_MIN_LOD:
        s->min_lod = v[0];
        break;
    case GL_TEXTURE_MAX_LOD:
        s->max_lod = v[0];
        break;
    case GL_TEXTURE_LOD_BIAS:
        s->lod_bias = v[0];
        break;
    case GL_TEXTURE_COMPARE_MODE:
        if (e != GL_NONE && e != GL_COMPARE_REF_TO_TEXTURE)
            goto bad_value;
        s->compare_mode = e;
        break;
    case GL_TEXTURE_COMPARE_FUNC:
        if (e < GL_NEVER || e > GL_ALWAYS)
            goto bad_value;
        s->compare_func = e;
        break;
    case GL_TEXTURE_MAX_ANISOTROPY:
        if (v[0] < 1) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
        s->max_anisotropy = v[0] > 16 ? 16 : v[0];
        break;
    case GL_TEXTURE_BORDER_COLOR:
        memcpy(s->border, v, sizeof(s->border));
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->dirty |= DIRTY_TEXTURES;
    return;
bad_value:
    gl_error(ctx, GL_INVALID_ENUM);
}

GLAPI void APIENTRY glSamplerParameteri(GLuint sampler, GLenum pname, GLint param)
{
    GL_CONTEXT_OR_RETURN();
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GLfloat v[4] = {(GLfloat)param};
    sampler_parameter(ctx, sampler, pname, v);
}

GLAPI void APIENTRY glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param)
{
    GL_CONTEXT_OR_RETURN();
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GLfloat v[4] = {param};
    sampler_parameter(ctx, sampler, pname, v);
}

GLAPI void APIENTRY glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4] = {(GLfloat)params[0]};
    if (pname == GL_TEXTURE_BORDER_COLOR)
        for (int i = 0; i < 4; i++)
            v[i] = (GLfloat)params[i] / 2147483647.0f;
    sampler_parameter(ctx, sampler, pname, v);
}

GLAPI void APIENTRY glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4] = {params[0]};
    if (pname == GL_TEXTURE_BORDER_COLOR)
        memcpy(v, params, sizeof(v));
    sampler_parameter(ctx, sampler, pname, v);
}

GLAPI void APIENTRY glSamplerParameterIiv(GLuint sampler, GLenum pname, const GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4] = {(GLfloat)params[0]};
    if (pname == GL_TEXTURE_BORDER_COLOR)
        for (int i = 0; i < 4; i++)
            v[i] = (GLfloat)params[i];
    sampler_parameter(ctx, sampler, pname, v);
}

GLAPI void APIENTRY glSamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4] = {(GLfloat)params[0]};
    if (pname == GL_TEXTURE_BORDER_COLOR)
        for (int i = 0; i < 4; i++)
            v[i] = (GLfloat)params[i];
    sampler_parameter(ctx, sampler, pname, v);
}

/* the parameter as floats, and the number of values. */
static int sampler_value(GlContext *ctx, GLuint name, GLenum pname, GLfloat *v)
{
    GlSamplerObject *o = sampler_of(ctx, name);
    if (!o) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return 0;
    }
    const GlSamplerState *s = &o->state;
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
        v[0] = (GLfloat)s->min_filter;
        return 1;
    case GL_TEXTURE_MAG_FILTER:
        v[0] = (GLfloat)s->mag_filter;
        return 1;
    case GL_TEXTURE_WRAP_S:
        v[0] = (GLfloat)s->wrap_s;
        return 1;
    case GL_TEXTURE_WRAP_T:
        v[0] = (GLfloat)s->wrap_t;
        return 1;
    case GL_TEXTURE_WRAP_R:
        v[0] = (GLfloat)s->wrap_r;
        return 1;
    case GL_TEXTURE_MIN_LOD:
        v[0] = s->min_lod;
        return 1;
    case GL_TEXTURE_MAX_LOD:
        v[0] = s->max_lod;
        return 1;
    case GL_TEXTURE_LOD_BIAS:
        v[0] = s->lod_bias;
        return 1;
    case GL_TEXTURE_COMPARE_MODE:
        v[0] = (GLfloat)(s->compare_mode ? s->compare_mode : GL_NONE);
        return 1;
    case GL_TEXTURE_COMPARE_FUNC:
        v[0] = (GLfloat)s->compare_func;
        return 1;
    case GL_TEXTURE_MAX_ANISOTROPY:
        v[0] = s->max_anisotropy;
        return 1;
    case GL_TEXTURE_BORDER_COLOR:
        memcpy(v, s->border, sizeof(s->border));
        return 4;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
}

GLAPI void APIENTRY glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4];
    int n = sampler_value(ctx, sampler, pname, v);
    for (int i = 0; i < n; i++)
        params[i] = v[i];
}

GLAPI void APIENTRY glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4];
    int n = sampler_value(ctx, sampler, pname, v);
    for (int i = 0; i < n; i++)
        params[i] = pname == GL_TEXTURE_BORDER_COLOR ? (GLint)(v[i] * 2147483647.0f) : (GLint)v[i];
}

GLAPI void APIENTRY glGetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4];
    int n = sampler_value(ctx, sampler, pname, v);
    for (int i = 0; i < n; i++)
        params[i] = (GLint)v[i];
}

GLAPI void APIENTRY glGetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint *params)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat v[4];
    int n = sampler_value(ctx, sampler, pname, v);
    for (int i = 0; i < n; i++)
        params[i] = (GLuint)v[i];
}
