/*
 * framebuffers, renderbuffers and render passes.
 *
 * gl has no passes: one starts when a draw or a clear reaches a target and
 * lasts until the target changes or something must run outside it, like a
 * copy or a readback. a clear issued before anything is drawn becomes the
 * pass's load action, which costs nothing; a clear in the middle of a pass
 * draws through the scissor.
 */
#include "opengl32.h"

GlFramebuffer *gl_framebuffer(GlContext *ctx, GLuint name)
{
    void *f = gl_name_get(&ctx->fbos, name);
    return f == GL_RESERVED ? nullptr : f;
}

GlRenderbuffer *gl_renderbuffer(GlContext *ctx, GLuint name)
{
    void *r = gl_name_get(&ctx->shared->renderbuffers, name);
    return r == GL_RESERVED ? nullptr : r;
}

/* the context whose state the host holds; another one starts from nothing. */
static void take_channel(GlContext *ctx)
{
    if (GlDev.owner == ctx)
        return;
    GlContext *old = GlDev.owner;
    if (old) {
        old->pass_open = FALSE;
        old->dirty = DIRTY_ALL;
        gl_invalidate_bindings(old);
    }
    GlDev.owner = ctx;
    ctx->pass_open = FALSE;
    ctx->dirty = DIRTY_ALL;
    gl_invalidate_bindings(ctx);
}

/* ---------------------------------------------------------------- renderbuffers */

GLAPI void APIENTRY glGenRenderbuffers(GLsizei n, GLuint *names)
{
    GL_CONTEXT_OR_RETURN();
    for (GLsizei i = 0; i < n; i++)
        names[i] = gl_name_new(&ctx->shared->renderbuffers);
}

GLAPI void APIENTRY glBindRenderbuffer(GLenum target, GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    if (name && !gl_renderbuffer(ctx, name)) {
        GlRenderbuffer *r = gl_alloc(sizeof(GlRenderbuffer));
        if (!r)
            return;
        r->name = name;
        gl_name_set(&ctx->shared->renderbuffers, name, r);
    }
    ctx->renderbuffer = name;
}

GLAPI void APIENTRY glDeleteRenderbuffers(GLsizei n, const GLuint *names)
{
    GL_CONTEXT_OR_RETURN();
    for (GLsizei i = 0; i < n; i++) {
        void *o = gl_name_get(&ctx->shared->renderbuffers, names[i]);
        if (!o)
            continue;
        if (o != GL_RESERVED) {
            GlRenderbuffer *r = o;
            if (r->id) {
                gl_device_lock();
                MimeGpuObject *d = gl_emit(MIME_GPU_TEXTURE_DESTROY, sizeof(MimeGpuObject));
                d->id = r->id;
                gl_device_unlock();
                gl_free_id(ID_TEXTURE, r->id);
            }
            gl_free(r);
        }
        gl_name_set(&ctx->shared->renderbuffers, names[i], nullptr);
        if (ctx->renderbuffer == names[i])
            ctx->renderbuffer = 0;
    }
}

GLAPI GLboolean APIENTRY glIsRenderbuffer(GLuint name)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    return gl_renderbuffer(ctx, name) ? GL_TRUE : GL_FALSE;
}

GLAPI void APIENTRY glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internal, GLsizei w,
                                                     GLsizei h)
{
    GL_CONTEXT_OR_RETURN();
    GlRenderbuffer *r = gl_renderbuffer(ctx, ctx->renderbuffer);
    if (!r) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    uint32_t swizzle;
    BOOL compressed;
    uint32_t mtl = gl_mtl_format(internal, GL_RGBA, GL_UNSIGNED_BYTE, &swizzle, &compressed);
    if (!mtl || compressed) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    uint32_t s = samples <= 1 ? 1 : samples <= 2 ? 2 : 4;
    if (r->id && r->mtl_format == mtl && r->width == (uint32_t)w && r->height == (uint32_t)h && r->samples == s)
        return;
    gl_device_lock();
    if (r->id) {
        MimeGpuObject *d = gl_emit(MIME_GPU_TEXTURE_DESTROY, sizeof(MimeGpuObject));
        d->id = r->id;
    } else {
        r->id = gl_new_id(ID_TEXTURE);
    }
    MimeGpuTextureCreate *c = gl_emit(MIME_GPU_TEXTURE_CREATE, sizeof(MimeGpuTextureCreate));
    c->swizzle = 0x05040302;
    c->id = r->id;
    c->type = s > 1 ? 4 : 2;
    c->format = mtl;
    c->width = (uint32_t)w ? (uint32_t)w : 1;
    c->height = (uint32_t)h ? (uint32_t)h : 1;
    c->depth = c->levels = 1;
    c->samples = s;
    c->usage = MIME_GPU_USAGE_TARGET | MIME_GPU_USAGE_SAMPLED;
    gl_device_unlock();
    r->internal_format = internal;
    r->mtl_format = mtl;
    r->width = (uint32_t)w;
    r->height = (uint32_t)h;
    r->samples = s;
    if (ctx->target_owner)
        ctx->target_serial = ~0u;
}

GLAPI void APIENTRY glRenderbufferStorage(GLenum target, GLenum internal, GLsizei w, GLsizei h)
{
    glRenderbufferStorageMultisample(target, 0, internal, w, h);
}

GLAPI void APIENTRY glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlRenderbuffer *r = gl_renderbuffer(ctx, ctx->renderbuffer);
    if (!r) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    switch (pname) {
    case GL_RENDERBUFFER_WIDTH:
        *params = (GLint)r->width;
        break;
    case GL_RENDERBUFFER_HEIGHT:
        *params = (GLint)r->height;
        break;
    case GL_RENDERBUFFER_INTERNAL_FORMAT:
        *params = (GLint)r->internal_format;
        break;
    case GL_RENDERBUFFER_SAMPLES:
        *params = r->samples > 1 ? (GLint)r->samples : 0;
        break;
    default:
        *params = 8;
        break;
    }
}

/* ---------------------------------------------------------------- framebuffers */

GLAPI void APIENTRY glGenFramebuffers(GLsizei n, GLuint *names)
{
    GL_CONTEXT_OR_RETURN();
    for (GLsizei i = 0; i < n; i++)
        names[i] = gl_name_new(&ctx->fbos);
}

static GlFramebuffer *make(GlContext *ctx, GLuint name)
{
    GlFramebuffer *f = gl_framebuffer(ctx, name);
    if (f)
        return f;
    f = gl_alloc(sizeof(GlFramebuffer));
    if (!f)
        return nullptr;
    f->name = name;
    f->draw_buffers[0] = GL_COLOR_ATTACHMENT0;
    f->read_buffer = GL_COLOR_ATTACHMENT0;
    gl_name_set(&ctx->fbos, name, f);
    return f;
}

GLAPI void APIENTRY glBindFramebuffer(GLenum target, GLuint name)
{
    GL_CONTEXT_OR_RETURN();
    if (name && !make(ctx, name)) {
        gl_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
        if (ctx->draw_fbo != name) {
            ctx->draw_fbo = name;
            ctx->dirty |= DIRTY_PIPELINE | DIRTY_SCISSOR | DIRTY_VIEWPORT;
        }
    }
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
        ctx->read_fbo = name;
}

GLAPI void APIENTRY glDeleteFramebuffers(GLsizei n, const GLuint *names)
{
    GL_CONTEXT_OR_RETURN();
    for (GLsizei i = 0; i < n; i++) {
        void *o = gl_name_get(&ctx->fbos, names[i]);
        if (!o)
            continue;
        if (o != GL_RESERVED) {
            if (ctx->target_owner == o) {
                gl_device_lock();
                gl_flush_clears(ctx);
                gl_end_pass(ctx);
                gl_device_unlock();
                ctx->target_owner = nullptr;
            }
            gl_free(o);
        }
        gl_name_set(&ctx->fbos, names[i], nullptr);
        if (ctx->draw_fbo == names[i])
            ctx->draw_fbo = 0;
        if (ctx->read_fbo == names[i])
            ctx->read_fbo = 0;
    }
    ctx->dirty |= DIRTY_PIPELINE;
}

GLAPI GLboolean APIENTRY glIsFramebuffer(GLuint name)
{
    GL_CONTEXT_OR_RETURN(GL_FALSE);
    return gl_framebuffer(ctx, name) ? GL_TRUE : GL_FALSE;
}

static GlFramebuffer *bound_fb(GlContext *ctx, GLenum target)
{
    GLuint name = target == GL_READ_FRAMEBUFFER ? ctx->read_fbo : ctx->draw_fbo;
    return name ? gl_framebuffer(ctx, name) : nullptr;
}

static GlAttachment *attachment(GlFramebuffer *f, GLenum point, GlAttachment **second)
{
    *second = nullptr;
    if (point >= GL_COLOR_ATTACHMENT0 && point < GL_COLOR_ATTACHMENT0 + MIME_GPU_MAX_COLORS)
        return &f->color[point - GL_COLOR_ATTACHMENT0];
    if (point == GL_DEPTH_ATTACHMENT)
        return &f->depth;
    if (point == GL_STENCIL_ATTACHMENT)
        return &f->stencil;
    if (point == GL_DEPTH_STENCIL_ATTACHMENT) {
        *second = &f->stencil;
        return &f->depth;
    }
    return nullptr;
}

static void set_attachment(GlContext *ctx, GLenum target, GLenum point, int type, GLuint name, GLint level,
                           GLint layer)
{
    GlFramebuffer *f = bound_fb(ctx, target);
    GlAttachment *second;
    GlAttachment *a = f ? attachment(f, point, &second) : nullptr;
    if (!a) {
        gl_error(ctx, f ? GL_INVALID_ENUM : GL_INVALID_OPERATION);
        return;
    }
    GlAttachment v = {name ? type : ATTACH_NONE, name, level, layer};
    *a = v;
    if (second)
        *second = v;
    f->serial++;
    ctx->dirty |= DIRTY_PIPELINE;
}

GLAPI void APIENTRY glFramebufferTexture2D(GLenum target, GLenum point, GLenum textarget, GLuint texture, GLint level)
{
    GL_CONTEXT_OR_RETURN();
    GLint layer = textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && textarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
                      ? (GLint)(textarget - GL_TEXTURE_CUBE_MAP_POSITIVE_X)
                      : 0;
    set_attachment(ctx, target, point, ATTACH_TEXTURE, texture, level, layer);
}

GLAPI void APIENTRY glFramebufferTexture(GLenum target, GLenum point, GLuint texture, GLint level)
{
    GL_CONTEXT_OR_RETURN();
    set_attachment(ctx, target, point, ATTACH_TEXTURE, texture, level, 0);
}

GLAPI void APIENTRY glFramebufferTexture1D(GLenum target, GLenum point, GLenum textarget, GLuint texture, GLint level)
{
    GL_CONTEXT_OR_RETURN();
    set_attachment(ctx, target, point, ATTACH_TEXTURE, texture, level, 0);
}

GLAPI void APIENTRY glFramebufferTextureLayer(GLenum target, GLenum point, GLuint texture, GLint level, GLint layer)
{
    GL_CONTEXT_OR_RETURN();
    set_attachment(ctx, target, point, ATTACH_TEXTURE, texture, level, layer);
}

GLAPI void APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum point, GLenum rbtarget, GLuint rb)
{
    GL_CONTEXT_OR_RETURN();
    set_attachment(ctx, target, point, ATTACH_RENDERBUFFER, rb, 0, 0);
}

/* fills a pass attachment from a framebuffer attachment; FALSE when it has no image. */
static BOOL resolve_attachment(GlContext *ctx, const GlAttachment *a, MimeGpuAttachment *out, uint16_t *format,
                               uint32_t *w, uint32_t *h, uint32_t *samples)
{
    memset(out, 0, sizeof(*out));
    if (a->type == ATTACH_TEXTURE) {
        GlTexture *t = gl_texture(ctx, a->name);
        if (!t || !t->id)
            return FALSE;
        out->id = t->id;
        out->level = (uint32_t)a->level;
        out->slice = (uint32_t)a->layer;
        *format = (uint16_t)t->mtl_format;
        *w = t->width >> a->level ? t->width >> a->level : 1;
        *h = t->height >> a->level ? t->height >> a->level : 1;
        *samples = t->samples ? t->samples : 1;
        return TRUE;
    }
    if (a->type == ATTACH_RENDERBUFFER) {
        GlRenderbuffer *r = gl_renderbuffer(ctx, a->name);
        if (!r || !r->id)
            return FALSE;
        out->id = r->id;
        *format = (uint16_t)r->mtl_format;
        *w = r->width;
        *h = r->height;
        *samples = r->samples;
        return TRUE;
    }
    return FALSE;
}

/* the attachments draws or reads go to, with their formats and the smallest size. */
void gl_resolve_target(GlContext *ctx, BOOL draw, GlTarget *t, const void **owner, uint32_t *serial)
{
    memset(t, 0, sizeof(*t));
    GLuint name = draw ? ctx->draw_fbo : ctx->read_fbo;
    GlFramebuffer *f = name ? gl_framebuffer(ctx, name) : nullptr;
    if (!f) {
        GlSurface *s = ctx->surface;
        *owner = s;
        *serial = 0;
        if (!s)
            return;
        t->color[0].id = s->id;
        t->color[0].surface = 1;
        t->color_format[0] = 80;
        if (s->depth_format) {
            t->depth.id = s->id;
            t->depth.surface = 1;
            t->depth_format = (uint16_t)s->depth_format;
        }
        if (s->stencil_format) {
            t->stencil.id = s->id;
            t->stencil.surface = 1;
            t->stencil_format = (uint16_t)s->stencil_format;
        }
        t->width = s->info.Width ? s->info.Width : 1;
        t->height = s->info.Height ? s->info.Height : 1;
        t->samples = 1;
        t->valid = TRUE;
        return;
    }
    *owner = f;
    *serial = f->serial;
    uint32_t w = 0xFFFFFFFF, h = 0xFFFFFFFF, samples = 1;
    BOOL any = FALSE;
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++) {
        GLenum db = f->draw_buffers[i];
        if (db < GL_COLOR_ATTACHMENT0 || db >= GL_COLOR_ATTACHMENT0 + MIME_GPU_MAX_COLORS)
            continue;
        uint32_t aw, ah, as;
        if (resolve_attachment(ctx, &f->color[db - GL_COLOR_ATTACHMENT0], &t->color[i], &t->color_format[i], &aw, &ah,
                               &as)) {
            w = aw < w ? aw : w;
            h = ah < h ? ah : h;
            samples = as;
            any = TRUE;
        }
    }
    uint32_t aw, ah, as;
    if (resolve_attachment(ctx, &f->depth, &t->depth, &t->depth_format, &aw, &ah, &as)) {
        if (!gl_format_is_depth(t->depth_format) || t->depth_format == 253) {
            memset(&t->depth, 0, sizeof(t->depth));
            t->depth_format = 0;
        } else {
            w = aw < w ? aw : w;
            h = ah < h ? ah : h;
            samples = as;
            any = TRUE;
        }
    }
    if (resolve_attachment(ctx, &f->stencil, &t->stencil, &t->stencil_format, &aw, &ah, &as)) {
        if (!gl_format_has_stencil(t->stencil_format)) {
            memset(&t->stencil, 0, sizeof(t->stencil));
            t->stencil_format = 0;
        } else {
            w = aw < w ? aw : w;
            h = ah < h ? ah : h;
            samples = as;
            any = TRUE;
        }
    }
    t->width = any ? w : 1;
    t->height = any ? h : 1;
    t->samples = samples;
    t->valid = any;
}

GLAPI GLenum APIENTRY glCheckFramebufferStatus(GLenum target)
{
    GL_CONTEXT_OR_RETURN(0);
    GlFramebuffer *f = bound_fb(ctx, target);
    if (!f)
        return GL_FRAMEBUFFER_COMPLETE;
    GlTarget t;
    const void *owner;
    uint32_t serial;
    gl_resolve_target(ctx, target != GL_READ_FRAMEBUFFER, &t, &owner, &serial);
    BOOL attached = FALSE;
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
        attached |= f->color[i].type != ATTACH_NONE;
    attached |= f->depth.type != ATTACH_NONE || f->stencil.type != ATTACH_NONE;
    if (!attached)
        return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    return t.valid ? GL_FRAMEBUFFER_COMPLETE : GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
}

GLAPI void APIENTRY glGetFramebufferAttachmentParameteriv(GLenum target, GLenum point, GLenum pname, GLint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlFramebuffer *f = bound_fb(ctx, target);
    if (!f) {
        /* the default framebuffer. */
        switch (pname) {
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
            *params = GL_FRAMEBUFFER_DEFAULT;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
        case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
        case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
            *params = 8;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
            *params = ctx->pfd.cAlphaBits;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
            *params = ctx->pfd.cDepthBits;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
            *params = ctx->pfd.cStencilBits;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
            *params = GL_LINEAR;
            break;
        default:
            *params = 0;
            break;
        }
        return;
    }
    GlAttachment *second;
    GlAttachment *a = attachment(f, point, &second);
    if (!a) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    switch (pname) {
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
        *params = a->type == ATTACH_TEXTURE ? GL_TEXTURE : a->type == ATTACH_RENDERBUFFER ? GL_RENDERBUFFER : GL_NONE;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
        *params = (GLint)a->name;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
        *params = a->level;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
        *params = a->layer;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
        *params = GL_LINEAR;
        break;
    default:
        *params = a->type ? 8 : 0;
        break;
    }
}

GLAPI void APIENTRY glDrawBuffers(GLsizei n, const GLenum *bufs)
{
    GL_CONTEXT_OR_RETURN();
    GlFramebuffer *f = ctx->draw_fbo ? gl_framebuffer(ctx, ctx->draw_fbo) : nullptr;
    if (!f) {
        if (n >= 1)
            ctx->draw_buffer = bufs[0];
        return;
    }
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++)
        f->draw_buffers[i] = i < n ? bufs[i] : GL_NONE;
    f->serial++;
    ctx->dirty |= DIRTY_PIPELINE;
}

GLAPI void APIENTRY glDrawBuffer(GLenum buf)
{
    GL_CONTEXT_OR_RETURN();
    GlFramebuffer *f = ctx->draw_fbo ? gl_framebuffer(ctx, ctx->draw_fbo) : nullptr;
    if (f)
        glDrawBuffers(1, &buf);
    else
        ctx->draw_buffer = buf;
}

GLAPI void APIENTRY glReadBuffer(GLenum buf)
{
    GL_CONTEXT_OR_RETURN();
    GlFramebuffer *f = ctx->read_fbo ? gl_framebuffer(ctx, ctx->read_fbo) : nullptr;
    if (f)
        f->read_buffer = buf;
    else
        ctx->read_buffer = buf;
}

BOOL gl_read_source(GlContext *ctx, MimeGpuAttachment *a, uint32_t *format, uint32_t *width, uint32_t *height)
{
    memset(a, 0, sizeof(*a));
    GlFramebuffer *f = ctx->read_fbo ? gl_framebuffer(ctx, ctx->read_fbo) : nullptr;
    if (!f) {
        GlSurface *s = ctx->surface;
        if (!s)
            return FALSE;
        a->id = s->id;
        a->surface = 1;
        *format = 80;
        *width = s->info.Width;
        *height = s->info.Height;
        return TRUE;
    }
    GLenum rb = f->read_buffer;
    if (rb < GL_COLOR_ATTACHMENT0 || rb >= GL_COLOR_ATTACHMENT0 + MIME_GPU_MAX_COLORS)
        return FALSE;
    uint16_t fmt;
    uint32_t samples;
    if (!resolve_attachment(ctx, &f->color[rb - GL_COLOR_ATTACHMENT0], a, &fmt, width, height, &samples))
        return FALSE;
    *format = fmt;
    return TRUE;
}

/*
 * the buffer depth or stencil reads come from, the read framebuffer's or
 * the window's, and the format of what a copy of it gives: the depth or the
 * stencil alone of a format with both. flags is what the copy names.
 */
BOOL gl_read_depth_source(GlContext *ctx, BOOL stencil, MimeGpuAttachment *a, uint32_t *format, uint32_t *flags,
                          uint32_t *width, uint32_t *height)
{
    memset(a, 0, sizeof(*a));
    GlFramebuffer *f = ctx->read_fbo ? gl_framebuffer(ctx, ctx->read_fbo) : nullptr;
    uint32_t mtl;
    if (!f) {
        GlSurface *s = ctx->surface;
        if (!s || !(stencil ? s->stencil_format : s->depth_format))
            return FALSE;
        a->id = s->id;
        mtl = stencil ? s->stencil_format : s->depth_format;
        *flags = MIME_GPU_COPY_SURFACE_DEPTH;
        *width = s->info.Width;
        *height = s->info.Height;
    } else {
        uint16_t fmt;
        uint32_t samples;
        if (!resolve_attachment(ctx, stencil ? &f->stencil : &f->depth, a, &fmt, width, height, &samples) ||
            samples > 1)
            return FALSE;
        mtl = fmt;
        *flags = MIME_GPU_COPY_TEXTURE;
    }
    if (mtl == 260) {
        mtl = stencil ? 253 : 252;
        if (stencil)
            *flags |= MIME_GPU_COPY_STENCIL;
    } else if (stencil != (mtl == 253)) {
        return FALSE;
    }
    *format = mtl;
    return TRUE;
}

/* ---------------------------------------------------------------- passes */

void gl_end_pass(GlContext *ctx)
{
    if (!ctx->pass_open || GlDev.owner != ctx)
        return;
    gl_emit(MIME_GPU_PASS_END, sizeof(MimeGpuCmd));
    ctx->pass_open = FALSE;
}

/* the surface must match the window before a frame starts drawing to it. */
static void check_surface(GlContext *ctx)
{
    if (!ctx->draw_fbo && ctx->surface && ctx->dc)
        gl_surface_refresh(ctx->surface, ctx->dc);
}

static void begin(GlContext *ctx, const GlTarget *t, BOOL clear_pending)
{
    MimeGpuPass *p = gl_emit(MIME_GPU_PASS_BEGIN, sizeof(MimeGpuPass));
    p->width = t->width;
    p->height = t->height;
    GLbitfield mask = clear_pending ? ctx->pending_clear.mask : 0;
    for (int i = 0; i < MIME_GPU_MAX_COLORS; i++) {
        if (!t->color[i].id)
            continue;
        p->color[i] = t->color[i];
        p->color[i].load = (mask & GL_COLOR_BUFFER_BIT) ? 2 : 1;
        p->color[i].store = 1;
        memcpy(p->clear_color[i], ctx->pending_clear.color, sizeof(p->clear_color[i]));
    }
    if (t->depth.id) {
        p->depth = t->depth;
        p->depth.load = (mask & GL_DEPTH_BUFFER_BIT) ? 2 : 1;
        p->depth.store = 1;
    }
    if (t->stencil.id) {
        p->stencil = t->stencil;
        p->stencil.load = (mask & GL_STENCIL_BUFFER_BIT) ? 2 : 1;
        p->stencil.store = 1;
    }
    p->clear_depth = ctx->pending_clear.depth;
    p->clear_stencil = (uint32_t)ctx->pending_clear.stencil;
    ctx->pass_open = TRUE;
    ctx->dirty |= DIRTY_VIEWPORT | DIRTY_SCISSOR;
}

/* opens a pass on the draw target, executing a pending clear of it as the load action. */
void gl_begin_pass(GlContext *ctx)
{
    take_channel(ctx);
    const void *owner;
    uint32_t serial;
    GlTarget t;
    if (!ctx->pass_open)
        check_surface(ctx);
    gl_resolve_target(ctx, TRUE, &t, &owner, &serial);
    if (ctx->pass_open && owner == ctx->target_owner && serial == ctx->target_serial &&
        !memcmp(&t, &ctx->target, sizeof(t)))
        return;
    gl_end_pass(ctx);
    BOOL pending = ctx->pending_clear.mask && ctx->pending_clear.owner == owner &&
                   ctx->pending_clear.serial == serial && !memcmp(&ctx->pending_clear.target, &t, sizeof(t));
    if (ctx->pending_clear.mask && !pending) {
        gl_flush_clears(ctx);
        gl_end_pass(ctx);
    }
    ctx->target = t;
    ctx->target_owner = owner;
    ctx->target_serial = serial;
    if (!t.valid)
        return;
    begin(ctx, &t, pending);
    if (pending)
        ctx->pending_clear.mask = 0;
    ctx->dirty |= DIRTY_PIPELINE;
}

/* executes a clear still waiting for its pass, on the target it was made for. */
void gl_flush_clears(GlContext *ctx)
{
    if (!ctx->pending_clear.mask)
        return;
    take_channel(ctx);
    if (ctx->pass_open)
        gl_end_pass(ctx);
    GlTarget t = ctx->pending_clear.target;
    if (t.valid) {
        begin(ctx, &t, TRUE);
        ctx->target = t;
        ctx->target_owner = ctx->pending_clear.owner;
        ctx->target_serial = ctx->pending_clear.serial;
    }
    ctx->pending_clear.mask = 0;
}

static BOOL scissor_covers(GlContext *ctx, const GlTarget *t)
{
    return !ctx->scissor_test || (ctx->scissor[0] <= 0 && ctx->scissor[1] <= 0 &&
                                  ctx->scissor[0] + ctx->scissor[2] >= (GLint)t->width &&
                                  ctx->scissor[1] + ctx->scissor[3] >= (GLint)t->height);
}

GLAPI void APIENTRY glClear(GLbitfield mask)
{
    GL_CONTEXT_OR_RETURN();
    gl_device_lock();
    take_channel(ctx);
    GlTarget t;
    const void *owner;
    uint32_t serial;
    if (!ctx->pass_open)
        check_surface(ctx);
    gl_resolve_target(ctx, TRUE, &t, &owner, &serial);
    BOOL full_color = ctx->color_mask[0] && ctx->color_mask[1] && ctx->color_mask[2] && ctx->color_mask[3];
    BOOL foldable = scissor_covers(ctx, &t) && (!(mask & GL_COLOR_BUFFER_BIT) || full_color) &&
                    (!(mask & GL_DEPTH_BUFFER_BIT) || ctx->depth_mask) &&
                    (!(mask & GL_STENCIL_BUFFER_BIT) || (ctx->stencil_write_mask[0] & 0xFF) == 0xFF);
    BOOL same_target = owner == ctx->target_owner && serial == ctx->target_serial;
    if (foldable && !(ctx->pass_open && same_target)) {
        /* nothing drawn yet: the next pass starts cleared. */
        if (!(ctx->pending_clear.mask && ctx->pending_clear.owner == owner && ctx->pending_clear.serial == serial)) {
            gl_flush_clears(ctx);
            ctx->pending_clear.mask = 0;
        }
        ctx->pending_clear.mask |= mask & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        memcpy(ctx->pending_clear.color, ctx->clear_color, sizeof(ctx->clear_color));
        ctx->pending_clear.depth = (GLfloat)ctx->clear_depth;
        ctx->pending_clear.stencil = ctx->clear_stencil;
        ctx->pending_clear.owner = owner;
        ctx->pending_clear.serial = serial;
        ctx->pending_clear.target = t;
        gl_device_unlock();
        return;
    }
    gl_begin_pass(ctx);
    if (ctx->pass_open) {
        if (ctx->dirty & DIRTY_SCISSOR) {
            MimeGpuRect *r = gl_emit(MIME_GPU_SET_SCISSOR, sizeof(MimeGpuRect));
            if (ctx->scissor_test) {
                r->x = ctx->scissor[0];
                r->y = ctx->scissor[1];
                r->width = (uint32_t)ctx->scissor[2];
                r->height = (uint32_t)ctx->scissor[3];
            } else {
                r->width = t.width;
                r->height = t.height;
            }
            ctx->dirty &= ~DIRTY_SCISSOR;
        }
        MimeGpuClear *c = gl_emit(MIME_GPU_CLEAR, sizeof(MimeGpuClear));
        c->mask = ((mask & GL_COLOR_BUFFER_BIT) ? MIME_GPU_CLEAR_COLOR : 0) |
                  ((mask & GL_DEPTH_BUFFER_BIT) && ctx->depth_mask ? MIME_GPU_CLEAR_DEPTH : 0) |
                  ((mask & GL_STENCIL_BUFFER_BIT) ? MIME_GPU_CLEAR_STENCIL : 0);
        c->color_mask = (ctx->color_mask[0] ? 8u : 0) | (ctx->color_mask[1] ? 4u : 0) | (ctx->color_mask[2] ? 2u : 0) |
                        (ctx->color_mask[3] ? 1u : 0);
        memcpy(c->color, ctx->clear_color, sizeof(c->color));
        c->depth = (float)ctx->clear_depth;
        c->stencil = (uint32_t)ctx->clear_stencil;
        c->stencil_write_mask = ctx->stencil_write_mask[0];
        ctx->dirty |= DIRTY_PIPELINE | DIRTY_DEPTH | DIRTY_RASTER | DIRTY_VIEWPORT | DIRTY_UNIFORMS;
    }
    gl_device_unlock();
}

GLAPI void APIENTRY glClearBufferfv(GLenum buffer, GLint index, const GLfloat *value)
{
    GL_CONTEXT_OR_RETURN();
    GLfloat saved[4];
    GLdouble saved_depth = ctx->clear_depth;
    memcpy(saved, ctx->clear_color, sizeof(saved));
    if (buffer == GL_COLOR) {
        memcpy(ctx->clear_color, value, sizeof(saved));
        glClear(GL_COLOR_BUFFER_BIT);
    } else if (buffer == GL_DEPTH) {
        ctx->clear_depth = value[0];
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    memcpy(ctx->clear_color, saved, sizeof(saved));
    ctx->clear_depth = saved_depth;
}

GLAPI void APIENTRY glClearBufferiv(GLenum buffer, GLint index, const GLint *value)
{
    GL_CONTEXT_OR_RETURN();
    if (buffer == GL_STENCIL) {
        GLint saved = ctx->clear_stencil;
        ctx->clear_stencil = value[0];
        glClear(GL_STENCIL_BUFFER_BIT);
        ctx->clear_stencil = saved;
    }
}

GLAPI void APIENTRY glClearBufferfi(GLenum buffer, GLint index, GLfloat depth, GLint stencil)
{
    GL_CONTEXT_OR_RETURN();
    GLdouble sd = ctx->clear_depth;
    GLint ss = ctx->clear_stencil;
    ctx->clear_depth = depth;
    ctx->clear_stencil = stencil;
    glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    ctx->clear_depth = sd;
    ctx->clear_stencil = ss;
}

/* ---------------------------------------------------------------- blits */

GLAPI void APIENTRY glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1,
                                      GLint dy1, GLbitfield mask, GLenum filter)
{
    GL_CONTEXT_OR_RETURN();
    GlTarget src, dst;
    const void *so, *dow;
    uint32_t ss, ds;
    gl_resolve_target(ctx, FALSE, &src, &so, &ss);
    gl_resolve_target(ctx, TRUE, &dst, &dow, &ds);
    MimeGpuAttachment read;
    uint32_t rf, rw, rh;
    if (!gl_read_source(ctx, &read, &rf, &rw, &rh) && (mask & GL_COLOR_BUFFER_BIT))
        return;
    gl_device_lock();
    take_channel(ctx);
    gl_flush_clears(ctx);
    gl_end_pass(ctx);
    if (mask & GL_COLOR_BUFFER_BIT) {
        for (int i = 0; i < MIME_GPU_MAX_COLORS; i++) {
            if (!dst.color[i].id)
                continue;
            MimeGpuBlit *b = gl_emit(MIME_GPU_BLIT, sizeof(MimeGpuBlit));
            b->src = read;
            b->dst = dst.color[i];
            b->src_x0 = sx0;
            b->src_y0 = sy0;
            b->src_x1 = sx1;
            b->src_y1 = sy1;
            b->dst_x0 = dx0;
            b->dst_y0 = dy0;
            b->dst_x1 = dx1;
            b->dst_y1 = dy1;
            b->mask = MIME_GPU_CLEAR_COLOR;
            b->linear = filter == GL_LINEAR;
        }
    }
    if ((mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) && src.depth.id && dst.depth.id) {
        MimeGpuBlit *b = gl_emit(MIME_GPU_BLIT, sizeof(MimeGpuBlit));
        b->src = src.depth;
        b->dst = dst.depth;
        b->src_x0 = sx0;
        b->src_y0 = sy0;
        b->src_x1 = sx1;
        b->src_y1 = sy1;
        b->dst_x0 = dx0;
        b->dst_y0 = dy0;
        b->dst_x1 = dx1;
        b->dst_y1 = dy1;
        b->mask = MIME_GPU_CLEAR_DEPTH;
    }
    gl_device_unlock();
}

GLAPI void APIENTRY glInvalidateFramebuffer(GLenum target, GLsizei n, const GLenum *attachments)
{
}

GLAPI void APIENTRY glDiscardFramebufferEXT(GLenum target, GLsizei n, const GLenum *attachments)
{
}
